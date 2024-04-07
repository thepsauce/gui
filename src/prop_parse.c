#include "gui.h"

#define PARSER_BUFFER 1024

struct parser {
	Union uni;
	FILE *file;
	const char *source;
	Uint32 sourcePointer;
	Uint32 sourceLength;
	/* circular buffer */
	char buffer[PARSER_BUFFER];
	Uint32 iRead, iWrite;
	long line;
	int column;
	int c;
	char word[MAX_WORD];
	int nWord;
	Value value;
	RawProperty property;
	Instruction instruction;
	Instruction *instructions;
	Uint32 numInstructions;
};

static void Refresh(struct parser *parser)
{
	Uint32 nWritten;

	/* this check is needed because the following code expects at least
	 * one character to be readable (not EOF)
	 */
	if (feof(parser->file)) {
		return;
	}
	if (parser->iRead > parser->iWrite) {
		nWritten = sizeof(parser->buffer) - parser->iRead +
			parser->iWrite;
	} else {
		nWritten = parser->iWrite - parser->iRead;
	}
	if (nWritten < sizeof(parser->buffer) / 2) {
		if (parser->iRead == 0) {
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) - 1 -
					nWritten, parser->file);
			parser->iWrite += n;
		} else if (parser->iRead < parser->iWrite) {
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) -
					parser->iWrite, parser->file);
			if (parser->iWrite + n >= sizeof(parser->buffer)) {
				parser->iWrite = fread(&parser->buffer[0], 1,
						parser->iRead - 1,
						parser->file);
			} else {
				parser->iWrite += n;
			}
		} else {
			/* in this configuration (buffer size = 1024 and reading
			 * when the buffer was half read this part of the code
			 * will never be reached */
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) - 1 -
					nWritten, parser->file);
			parser->iWrite += n;
		}
	}
}

static int NextChar(struct parser *parser)
{
	if (parser->file == NULL) {
		if (parser->sourcePointer == parser->sourceLength) {
			parser->c = EOF;
			return EOF;
		}
		parser->c = parser->source[parser->sourcePointer++];
		if (parser->c == '\n') {
			parser->line++;
			parser->column = 0;
		} else {
			parser->column++;
		}
		return parser->c;
	}
	Refresh(parser);
	if (parser->iRead == parser->iWrite) {
		parser->c = EOF;
		return EOF;
	}
	parser->c = parser->buffer[parser->iRead++];
	parser->iRead %= sizeof(parser->buffer);
	if (parser->c == '\n') {
		parser->line++;
		parser->column = 0;
	} else {
		parser->column++;
	}
	return parser->c;
}

static int SkipSpace(struct parser *parser)
{
	bool isComment = false;

	do {
		if (parser->c == ';') {
			isComment = !isComment;
		} else if (!isspace(parser->c) && !isComment) {
			break;
		}
	} while (NextChar(parser) != EOF);
	return 0;
}

static Uint32 LookAhead(struct parser *parser, char *buf, Uint32 nBuf)
{
	Uint32 l, i;

	SkipSpace(parser);

	if (nBuf == 0 || parser->c == EOF) {
		return 0;
	}

	buf[0] = parser->c;

	if (parser->file == NULL) {
		for (l = 1, i = parser->sourcePointer; l < nBuf; l++) {
			if (i == parser->sourceLength) {
				break;
			}
			buf[l] = parser->source[i++];
		}
		return l;
	}

	if (nBuf >= sizeof(parser->buffer) / 2) {
		return 0;
	}

	for (l = 1, i = parser->iRead; l < nBuf; l++) {
		if (i == parser->iWrite) {
			break;
		}
		buf[l] = parser->buffer[i++];
		i %= sizeof(parser->buffer);
	}
	return l;
}

static int ReadWord(struct parser *parser)
{
	parser->nWord = 0;
	if (parser->c != '_' && !isalpha(parser->c)) {
		return -1;
	}
	do {
		if (parser->nWord + 1 == MAX_WORD) {
			return -1;
		}
		parser->word[parser->nWord++] = parser->c;
		NextChar(parser);
	} while (parser->c == '_' || isalnum(parser->c));
	parser->word[parser->nWord] = '\0';
	return 0;
}

static int HexToInt(char ch)
{
	if (isdigit(ch)) {
		return ch - '0';
	}
	if (isalpha(ch)) {
		return ch >= 'a' ? ch - 'a' + 0xa : ch - 'A' + 0xA;
	}
	return -1;
}

static type_t CheckType(struct parser *parser)
{
	static const char *typeNames[] = {
		[TYPE_ARRAY] = "array",
		[TYPE_BOOL] = "bool",
		[TYPE_COLOR] = "color",
		[TYPE_EVENT] = "event",
		[TYPE_FLOAT] = "float",
		[TYPE_FUNCTION] = "function",
		[TYPE_INTEGER] = "int",
		[TYPE_POINT] = "point",
		[TYPE_RECT] = "rect",
		[TYPE_STRING] = "string",
		[TYPE_SUCCESS] = "", /* hidden type */
		[TYPE_VIEW] = "view"
	};
	if (parser->nWord == 0) {
		return TYPE_NULL;
	}
	for (type_t i = 0; i < (type_t) ARRLEN(typeNames); i++) {
		if (strcmp(parser->word, typeNames[i]) == 0) {
			return i;
		}
	}
	return TYPE_NULL;
}

static int ReadInt(struct parser *parser);
static int ReadValue(struct parser *parser);

struct int_or_float {
	int radix;
	Sint64 sign;
	Sint64 front, back;
	Sint64 fShift, bShift;
};

#define IOF_SHIFT(n, r, s) ({ \
	__auto_type _n = (n); \
	const __auto_type _r = (r); \
	const __auto_type _s = (s); \
	if (_s > 0) { \
		for (Sint64 i = 0; i < _s; i++) { \
			_n *= _r; \
		} \
	} else { \
		for (Sint64 i = _s; i < 0; i++) { \
			_n /= _r; \
		} \
	} \
	_n; \
})

static Sint64 iof_AsInt(const struct int_or_float *iof)
{
	Sint64 front, back;

	front = IOF_SHIFT(iof->front, iof->radix, iof->fShift);
	back = IOF_SHIFT(iof->back, iof->radix, iof->bShift);
	return iof->sign * (front + back);
}

static float iof_AsFloat(const struct int_or_float *iof)
{
	float front, back;

	front = IOF_SHIFT((float) iof->front, (float) iof->radix, iof->fShift);
	back = IOF_SHIFT((float) iof->back, (float) iof->radix, iof->bShift);
	return iof->sign * (front + back);
}

static void iof_AsPrecise(const struct int_or_float *iof, Value *value)
{
	if (iof->fShift >= 0 && iof->bShift >= 0 && iof->back == 0) {
		value->type = TYPE_INTEGER;
		value->i = iof->sign * iof->front;
		for (Sint64 i = 0; i < iof->fShift; i++) {
			value->i *= iof->radix;
		}
	} else {
		value->type = TYPE_FLOAT;
		value->f = iof_AsFloat(iof);
	}
}

static inline bool isrdigit(int c, int radix)
{
	switch (radix) {
	case 2:
		return c == '0' || c == '1';
	case 8:
		return c >= '0' && c <= '7';
	case 10:
		return isdigit(c);
	case 16:
		return isxdigit(c);
	}
	return false;
}

static int ReadIntOrFloat(struct parser *parser, struct int_or_float *iof)
{
	int c;
	Sint64 front = 0, back = 0;
	Uint32 nFront = 0, nBack = 0;
	Sint64 exponent;

	iof->radix = 10;
	iof->front = 0;
	iof->back = 0;
	iof->fShift = 0;
	iof->bShift = 0;
	if (parser->c == '+') {
		iof->sign = 1;
		NextChar(parser);
	} else if (parser->c == '-') {
		iof->sign = -1;
		NextChar(parser);
	} else {
		iof->sign = 1;
	}

	if (parser->c == '\'') {
		NextChar(parser);
		if (parser->c == '\\') {
			NextChar(parser);
			switch (parser->c) {
			case 'a': c = '\a'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'v': c = '\b'; break;
			case 'x': {
				int d1, d2;

				d1 = HexToInt(NextChar(parser));
				if (d1 < 0) {
					return -1;
				}
				d2 = HexToInt(NextChar(parser));
				if (d2 < 0) {
					return -1;
				}
				c = (d1 << 4) | d2;
				break;
			}
			default:
				c = parser->c;
			}
		} else {
			c = parser->c;
		}
		NextChar(parser);
		if (parser->c != '\'') {
			return -1;
		}
		NextChar(parser);
		iof->front = c;
		return 0;
	}

	if (parser->c == '0') {
		switch (NextChar(parser)) {
		case 'X':
		case 'x':
			iof->radix = 16;
			break;
		case 'O':
		case 'o':
			iof->radix = 8;
			break;
		case 'B':
		case 'b':
			iof->radix = 2;
			break;
		default:
			nFront = 1;
		}
		if (iof->radix != 10) {
			NextChar(parser);
		}
	}

	while (c = parser->c, isrdigit(c, iof->radix)) {
		if (iof->radix == 16 && c > '9') {
			c = tolower(c) - 'a' + 0xa;
		} else {
			c -= '0';
		}
		front *= iof->radix;
		front += c;
		nFront++;
		NextChar(parser);
	}

	if (parser->c == '.') {
		while (c = NextChar(parser), isrdigit(c, iof->radix)) {
			if (iof->radix == 16 && c > '9') {
				c = tolower(c) - 'a' + 0xa;
			} else {
				c -= '0';
			}
			back *= iof->radix;
			back += c;
			nBack++;
		}
	}

	if (nFront == 0 && nBack == 0) {
		return -1;
	}

	exponent = 0;
	if (parser->c == 'e' || parser->c == 'E') {
		Sint64 signExponent;

		NextChar(parser);
		if (parser->c == '+') {
			signExponent = 1;
			NextChar(parser);
		} else if (parser->c == '-') {
			signExponent = -1;
			NextChar(parser);
		} else {
			signExponent = 1;
		}
		while (isdigit(parser->c)) {
			exponent *= 10;
			exponent += parser->c - '0';
			NextChar(parser);
		}
		exponent *= signExponent;
	}

	iof->fShift = exponent;
	iof->bShift = exponent - nBack;
	iof->front = front;
	iof->back = back;
	return 0;
}

static int ReadArray(struct parser *parser)
{
	struct value_array *arr;
	Value *values = NULL, *newValues;
	Uint32 numValues = 0;

	arr = union_Alloc(union_Default(), sizeof(*arr));
	if (arr == NULL) {
		return -1;
	}

	if (parser->c != '[') {
		return -1;
	}
	NextChar(parser);
	SkipSpace(parser);
	while (parser->c != ']') {
		if (ReadValue(parser) < 0) {
			return -1;
		}
		newValues = union_Realloc(union_Default(), values,
				sizeof(*values) * (numValues + 1));
		if (newValues == NULL) {
			return -1;
		}
		values = newValues;
		values[numValues++] = parser->value;
		SkipSpace(parser);
		if (parser->c != ',') {
			break;
		}
		NextChar(parser);
		SkipSpace(parser);
	}
	if (parser->c != ']') {
		return -1;
	}
	NextChar(parser); /* skip ']' */
	arr->values = values;
	arr->numValues = numValues;
	parser->value.a = arr;
	return 0;
}

static int ReadBool(struct parser *parser)
{
	if (ReadWord(parser) < 0) {
		return -1;
	}
	if (strcmp(parser->word, "true") == 0) {
		parser->value.b = true;
		return 0;
	}
	if (strcmp(parser->word, "false") == 0) {
		parser->value.b = false;
		return 0;
	}
	return -1;
}

static int ReadColor(struct parser *parser)
{
	static const struct {
		const char *name;
		Uint32 color;
	} colors[] = {
		{ "black", 0xff000000 },
		{ "white", 0xffffffff },
		{ "red", 0xffff0000 },
		{ "green", 0xff00ff00 },
		{ "blue", 0xff0000ff },
		{ "yellow", 0xffffff00 },
		{ "cyan", 0xff00ffff },
		{ "magenta", 0xffff00ff },
		{ "gray", 0xff808080 },
		{ "orange", 0xffffa500 },
		{ "purple", 0xff800080 },
		{ "brown", 0xffa52a2a },
		{ "pink", 0xffffc0cb },
		{ "olive", 0xff808000 },
		{ "teal", 0xff008080 },
		{ "navy", 0xff000080 }
	};

	if (isalpha(parser->c)) {
		if (ReadWord(parser) < 0) {
			return -1;
		}
		for (size_t i = 0; i < ARRLEN(colors); i++) {
			if (strcmp(colors[i].name, parser->word) == 0) {
				IntToRgb(colors[i].color, &parser->value.c);
				return 0;
			}
		}
		return -1;
	} else {
		struct int_or_float iof;

		if (ReadIntOrFloat(parser, &iof) < 0) {
			return -1;
		}
		IntToRgb(iof_AsInt(&iof), &parser->value.c);
	}
	return 0;
}

static int ReadKeyDown(struct parser *parser)
{
	struct key_info ki;

	/* TODO: handle variations */
	ki.state = 0;
	ki.repeat = 0;
	ki.sym.scancode = 0;
	ki.sym.sym = (SDL_Keycode) parser->c;
	ki.sym.mod = KMOD_NONE;
	parser->value.e.info.ki = ki;
	NextChar(parser);
	return 0;
}

static int ReadEvent(struct parser *parser)
{
	static const struct {
		const char *word;
		event_t event;
		int (*read)(struct parser *parser);
	} events[] = {
		{ "keydown", EVENT_KEYDOWN, ReadKeyDown },
		/* TODO: */
	};
	if (ReadWord(parser) < 0) {
		return -1;
	}
	SkipSpace(parser);
	for (size_t i = 0; i < ARRLEN(events); i++) {
		if (strcmp(events[i].word, parser->word) == 0) {
			parser->value.e.event = events[i].event;
			events[i].read(parser);
			return 0;
		}
	}
	return -1;
}

static int ReadFloat(struct parser *parser)
{
	struct int_or_float iof;

	if (ReadIntOrFloat(parser, &iof) < 0) {
		return -1;
	}
	parser->value.f = iof_AsFloat(&iof);
	return 0;
}

static int ReadExpression(struct parser *parser, int precedence);

static int ReadBody(struct parser *parser)
{
	Instruction *instructions = NULL, *newInstructions;
	Uint32 numInstructions = 0;

	if (parser->c != '{') {
		return -1;
	}
	NextChar(parser); /* skip '{' */
	while (SkipSpace(parser), parser->c != '}') {
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		newInstructions = union_Realloc(union_Default(), instructions,
				sizeof(*instructions) * (numInstructions + 1));
		if (newInstructions == NULL) {
			return -1;
		}
		instructions = newInstructions;
		instructions[numInstructions++] = parser->instruction;
	}
	NextChar(parser); /* skip '}' */
	parser->instructions = instructions;
	parser->numInstructions = numInstructions;
	return 0;
}

static int ReadFunction(struct parser *parser)
{
	Function *func;
	type_t type;
	Parameter p, *params = NULL, *newParams;
	Uint32 numParams = 0;

	func = union_Alloc(union_Default(), sizeof(*func));
	if (func == NULL) {
		return -1;
	}

	/* read parameters */
	while (parser->c != '{') {
		/* parameter type */
		if (ReadWord(parser) < 0) {
			return -1;
		}
		type = CheckType(parser);
		if (type == TYPE_NULL) {
			return -1;
		}
		/* parameter name */
		SkipSpace(parser);
		if (ReadWord(parser) < 0) {
			return -1;
		}
		/* NOT parser->uni! */
		newParams = union_Realloc(union_Default(), params,
				sizeof(*params) * (numParams + 1));
		if (newParams == NULL) {
			return -1;
		}
		params = newParams;
		p.type = type;
		strcpy(p.name, parser->word);
		params[numParams++] = p;
		SkipSpace(parser);
		if (parser->c != ',' && parser->c != '{') {
			return -1;
		}
		if (parser->c == ',') {
			NextChar(parser); /* skip ',' */
			SkipSpace(parser);
		}
	}

	if (ReadBody(parser) < 0) {
		return -1;
	}
	func->params = params;
	func->numParams = numParams;
	func->instructions = parser->instructions;
	func->numInstructions = parser->numInstructions;
	parser->value.func = func;
	return 0;
}

static int ReadInt(struct parser *parser)
{
	struct int_or_float iof;
	if (ReadIntOrFloat(parser, &iof) < 0) {
		return -1;
	}
	parser->value.type = TYPE_INTEGER;
	parser->value.i = iof_AsInt(&iof);
	return 0;
}

static int ReadString(struct parser *parser)
{
	struct value_string *s;
	char *newData;

	s = union_Alloc(union_Default(), sizeof(*s));
	if (s == NULL) {
		return -1;
	}

	s->data = NULL;
	s->length = 0;

	if (parser->c != '\"') {
		return -1;
	}
	while (NextChar(parser) != EOF) {
		int c;

		if (parser->c == '\"') {
			NextChar(parser); /* skip '"' */
			break;
		}
		if (parser->c == '\\') {
			NextChar(parser);
			switch (parser->c) {
			case 'a': c = '\a'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'v': c = '\b'; break;
			case 'x': {
				int d1, d2;

				d1 = HexToInt(NextChar(parser));
				if (d1 < 0) {
					return -1;
				}
				d2 = HexToInt(NextChar(parser));
				if (d2 < 0) {
					return -1;
				}
				c = (d1 << 4) | d2;
				break;
			}
			default:
				  c = parser->c;
			}
		} else {
			c = parser->c;
		}
		newData = union_Realloc(union_Default(), s->data, s->length + 1);
		if (newData == NULL) {
			return -1;
		}
		s->data = newData;
		s->data[s->length++] = c;
	}

	parser->value.s = s;
	return 0;
}

static int ReadView(struct parser *parser)
{
	(void) parser;
	return 0;
}

/* ~=~=~=~=~=~=~=~=~=~=~=~=~= */

static int ReadBreak(struct parser *parser)
{
	parser->instruction.instr = INSTR_BREAK;
	return 0;
}

/**
 * 1. for [name] to [instruction] [instruction]
 * 2. for [name] from [instruction] to [instruction] [instruction]
 * 3. for [name] in [instruction] [instruction]
 */
static int ReadFor(struct parser *parser)
{
	char var[MAX_WORD];
	Instruction *from, *to, *in = NULL, *iter;

	if (ReadWord(parser) < 0) {
		return -1;
	}
	strcpy(var, parser->word);

	SkipSpace(parser);
	if (ReadWord(parser) < 0) {
		return -1;
	}
	SkipSpace(parser);
	if (strcmp(parser->word, "from") == 0) {
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		from = union_Alloc(union_Default(), sizeof(*from));
		if (from == NULL) {
			return -1;
		}
		*from = parser->instruction;
		SkipSpace(parser);
		if (ReadWord(parser) < 0) {
			return -1;
		}
	} else if (strcmp(parser->word, "in") == 0) {
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		in = union_Alloc(union_Default(), sizeof(*in));
		if (in == NULL) {
			return -1;
		}
		*in = parser->instruction;
	} else if (strcmp(parser->word, "to") == 0) {
		from = NULL;
	} else {
		return -1;
	}
	SkipSpace(parser);

	if (in == NULL) {
		if (strcmp(parser->word, "to") != 0) {
			return -1;
		}
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		to = union_Alloc(union_Default(), sizeof(*to));
		if (to == NULL) {
			return -1;
		}
		*to = parser->instruction;
		SkipSpace(parser);
	}

	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	iter = union_Alloc(union_Default(), sizeof(*iter));
	if (iter == NULL) {
		return -1;
	}
	*iter = parser->instruction;

	if (in == NULL) {
		parser->instruction.instr = INSTR_FOR;
		strcpy(parser->instruction.forr.variable, var);
		parser->instruction.forr.from = from;
		parser->instruction.forr.to = to;
		parser->instruction.forr.iter = iter;
	} else {
		parser->instruction.instr = INSTR_FORIN;
		strcpy(parser->instruction.forin.variable, var);
		parser->instruction.forin.in = in;
		parser->instruction.forin.iter = iter;
	}
	return 0;
}

/**
 * 1. if [instruction] [instruction]
 * ? else [instruction]
 */
static int ReadIf(struct parser *parser)
{
	char text[sizeof("else")];
	Instruction *iff;
	Instruction *cond;
	Instruction *els = NULL;

	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	cond = union_Alloc(union_Default(), sizeof(*cond));
	if (cond == NULL) {
		return -1;
	}
	*cond = parser->instruction;

	SkipSpace(parser);
	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	iff = union_Alloc(union_Default(), sizeof(*iff));
	if (iff == NULL) {
		return -1;
	}
	*iff = parser->instruction;

	if (LookAhead(parser, text, sizeof(text)) == sizeof(text)) {
		if (memcmp(text, "else", 4) == 0 &&
				!isalnum(text[4]) && text[4] != '_') {
			ReadWord(parser);
			SkipSpace(parser);
			if (ReadExpression(parser, 0) < 0) {
				return -1;
			}
			els = union_Alloc(union_Default(),
					sizeof(*els));
			if (els == NULL) {
				return -1;
			}
			*els = parser->instruction;
		}
	}
	parser->instruction.instr = INSTR_IF;
	parser->instruction.iff.condition = cond;
	parser->instruction.iff.iff = iff;
	parser->instruction.iff.els = els;
	return 0;
}

static int ReadInvoke(struct parser *parser)
{
	char name[256];
	Instruction *args = NULL, *newArgs;
	Uint32 numArgs = 0;

	/* assuming that the caller got the invoke name already
	 * and has skipped the '('
	 */
	strcpy(name, parser->word);
	while (parser->c != ')') {
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		/* NOT parser->uni! */
		newArgs = union_Realloc(union_Default(), args,
				sizeof(*args) * (numArgs + 1));
		if (newArgs == NULL) {
			return -1;
		}
		args = newArgs;
		args[numArgs++] = parser->instruction;
		SkipSpace(parser);
		if (parser->c != ',') {
			break;
		}
		NextChar(parser);
		SkipSpace(parser);
	}
	if (parser->c != ')') {
		return -1;
	}
	NextChar(parser); /* skip ')' */
	strcpy(parser->instruction.invoke.name, name);
	parser->instruction.instr = INSTR_INVOKE;
	parser->instruction.invoke.args = args;
	parser->instruction.invoke.numArgs = numArgs;
	return 0;
}

static int ReadLocal(struct parser *parser)
{
	Instruction *pInstr;

	if (ReadWord(parser) < 0) {
		return -1;
	}
	strcpy(parser->instruction.local.name, parser->word);
	SkipSpace(parser);
	if (parser->c != '=') {
		return -1;
	}
	NextChar(parser); /* skip '=' */
	SkipSpace(parser);

	/* save this because it will be overwritten */
	const Instruction instruction = parser->instruction;
	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	pInstr = union_Alloc(union_Default(), sizeof(*pInstr));
	if (pInstr == NULL) {
		return -1;
	}
	*pInstr = parser->instruction;
	parser->instruction = instruction;
	parser->instruction.instr = INSTR_LOCAL;
	parser->instruction.local.value = pInstr;
	return 0;
}

static int ReadReturn(struct parser *parser)
{
	Instruction *instr;

	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	instr = union_Alloc(union_Default(), sizeof(*instr));
	if (instr == NULL) {
		return -1;
	}
	*instr = parser->instruction;
	parser->instruction.instr = INSTR_RETURN;
	parser->instruction.ret.value = instr;
	return 0;
}

static int ReadSwitch(struct parser *parser)
{
	char cs[5];
	Instruction *instr;
	Instruction *instructions = NULL, *newInstructions;
	Uint32 numInstructions = 0;
	Instruction *conditions = NULL, *newConditions;
	Uint32 *jumps = NULL, *newJumps;
	Uint32 numJumps = 0;

	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	instr = union_Alloc(union_Default(), sizeof(*instr));
	if (instr == NULL) {
		return -1;
	}
	*instr = parser->instruction;
	SkipSpace(parser);
	if (parser->c != '{') {
		return -1;
	}
	NextChar(parser); /* skip '{' */
	while (SkipSpace(parser), parser->c != '}') {
		if (LookAhead(parser, cs, sizeof(cs)) == sizeof(cs)) {
			if (memcmp(cs, "case", 4) == 0 &&
					!isalnum(cs[4]) && cs[4] != '_') {
				ReadWord(parser); /* skip "case" */
				SkipSpace(parser);
				if (ReadExpression(parser, 0) < 0) {
					return -1;
				}

				newConditions = union_Realloc(union_Default(),
						conditions,
						sizeof(*conditions) *
						(numJumps + 1));
				if (newConditions == NULL) {
					return -1;
				}
				conditions = newConditions;

				newJumps = union_Realloc(union_Default(), jumps,
						sizeof(*jumps) *
						(numJumps + 1));
				if (newJumps == NULL) {
					return -1;
				}
				jumps = newJumps;

				conditions[numJumps] = parser->instruction;
				jumps[numJumps] = numInstructions;
				numJumps++;
				continue;
			}
		}
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		newInstructions = union_Realloc(union_Default(), instructions,
				sizeof(*instructions) * (numInstructions + 1));
		if (newInstructions == NULL) {
			return -1;
		}
		instructions = newInstructions;
		instructions[numInstructions++] = parser->instruction;
	}
	NextChar(parser); /* skip '}' */

	parser->instruction.instr = INSTR_SWITCH;
	parser->instruction.switchh.value = instr;
	parser->instruction.switchh.instructions = instructions;
	parser->instruction.switchh.numInstructions = numInstructions;
	parser->instruction.switchh.jumps = jumps;
	parser->instruction.switchh.conditions = conditions;
	parser->instruction.switchh.numJumps = numJumps;
	return 0;
}

static int ReadThis(struct parser *parser)
{
	parser->instruction.instr = INSTR_THIS;
	return 0;
}

static int ReadTrigger(struct parser *parser)
{
	Instruction *args;
	Uint32 numArgs;

	if (ReadWord(parser) < 0) {
		return -1;
	}
	SkipSpace(parser);
	if (parser->c == '(') {
		NextChar(parser);
		SkipSpace(parser);
		if (ReadInvoke(parser) < 0) {
			return -1;
		}
		args = parser->instruction.invoke.args;
		numArgs = parser->instruction.invoke.numArgs;
	}
	parser->instruction.instr = INSTR_TRIGGER;
	strcpy(parser->instruction.trigger.name, parser->word);
	parser->instruction.trigger.args = args;
	parser->instruction.trigger.numArgs = numArgs;
	return 0;
}

static int ReadWhile(struct parser *parser)
{
	Instruction *cond;
	Instruction *iter;

	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	cond = union_Alloc(union_Default(), sizeof(*cond));
	if (cond == NULL) {
		return -1;
	}
	*cond = parser->instruction;

	SkipSpace(parser);
	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	iter = union_Alloc(union_Default(), sizeof(*iter));
	if (iter == NULL) {
		return -1;
	}
	*iter = parser->instruction;

	parser->instruction.instr = INSTR_WHILE;
	parser->instruction.whilee.condition = cond;
	parser->instruction.whilee.iter = iter;
	return 0;
}

static int _ReadValue(struct parser *parser, type_t type)
{
	static int (*const reads[])(struct parser *parser) = {
		[TYPE_ARRAY] = ReadArray,
		[TYPE_BOOL] = ReadBool,
		[TYPE_COLOR] = ReadColor,
		[TYPE_EVENT] = ReadEvent,
		[TYPE_FLOAT] = ReadFloat,
		[TYPE_FUNCTION] = ReadFunction,
		[TYPE_INTEGER] = ReadInt,
		[TYPE_POINT] = SkipSpace, /* these two have been replaced.. */
		[TYPE_RECT] = SkipSpace, /* ..by system functions */
		[TYPE_STRING] = ReadString,
		[TYPE_SUCCESS] = SkipSpace, /* no specific read function */
		[TYPE_VIEW] = ReadView
	};
	if (type == TYPE_NULL) {
		return -1;
	}
	return reads[type](parser);
}

static int ReadValue(struct parser *parser)
{
	struct int_or_float iof;
	type_t type;

	if (isalpha(parser->c)) {
		if (ReadWord(parser) < 0) {
			return -1;
		}
		type = CheckType(parser);
		if (type == TYPE_NULL) {
			return -1;
		}
	} else if (parser->c == '[') {
		type = TYPE_ARRAY;
	} else if (parser->c == '\"') {
		type = TYPE_STRING;
	} else if (ReadIntOrFloat(parser, &iof) < 0) {
		return -1;
	} else {
		iof_AsPrecise(&iof, &parser->value);
		return 0;
	}
	parser->value.type = type;
	return _ReadValue(parser, type);
}

static int ResolveConstant(struct parser *parser)
{
	static const struct {
		const char *word;
		Value value;
	} constants[] = {
		{ "EVENT_CREATE", { TYPE_INTEGER, .i = EVENT_CREATE } },
		{ "EVENT_DESTROY", { TYPE_INTEGER, .i = EVENT_DESTROY } },
		{ "EVENT_TIMER", { TYPE_INTEGER, .i = EVENT_TIMER } },
		{ "EVENT_SETFOCUS", { TYPE_INTEGER, .i = EVENT_SETFOCUS } },
		{ "EVENT_KILLFOCUS", { TYPE_INTEGER, .i = EVENT_KILLFOCUS } },
		{ "EVENT_PAINT", { TYPE_INTEGER, .i = EVENT_PAINT } },
		{ "EVENT_SIZE", { TYPE_INTEGER, .i = EVENT_SIZE } },
		{ "EVENT_KEYDOWN", { TYPE_INTEGER, .i = EVENT_KEYDOWN } },
		{ "EVENT_CHAR", { TYPE_INTEGER, .i = EVENT_CHAR } },
		{ "EVENT_KEYUP", { TYPE_INTEGER, .i = EVENT_KEYUP } },
		{ "EVENT_BUTTONDOWN", { TYPE_INTEGER, .i = EVENT_BUTTONDOWN } },
		{ "EVENT_BUTTONUP", { TYPE_INTEGER, .i = EVENT_BUTTONUP } },
		{ "EVENT_MOUSEMOVE", { TYPE_INTEGER, .i = EVENT_MOUSEMOVE } },
		{ "EVENT_MOUSEWHEEL", { TYPE_INTEGER, .i = EVENT_MOUSEWHEEL } },
		{ "EVENT_TEXTINPUT", { TYPE_INTEGER, .i = EVENT_TEXTINPUT } },

		{ "BUTTON_LEFT", { TYPE_INTEGER, .i = SDL_BUTTON_LEFT } },
		{ "BUTTON_MIDDLE", { TYPE_INTEGER, .i = SDL_BUTTON_MIDDLE } },
		{ "BUTTON_RIGHT", { TYPE_INTEGER, .i = SDL_BUTTON_RIGHT } },

		{ "KEY_RETURN", { TYPE_INTEGER, .i = SDLK_RETURN } },
		{ "KEY_ESCAPE", { TYPE_INTEGER, .i = SDLK_ESCAPE } },
		{ "KEY_BACKSPACE", { TYPE_INTEGER, .i = SDLK_BACKSPACE } },
		{ "KEY_TAB", { TYPE_INTEGER, .i = SDLK_TAB } },
		{ "KEY_SPACE", { TYPE_INTEGER, .i = SDLK_SPACE } },
		{ "KEY_EXCLAIM", { TYPE_INTEGER, .i = SDLK_EXCLAIM } },
		{ "KEY_QUOTEDBL", { TYPE_INTEGER, .i = SDLK_QUOTEDBL } },
		{ "KEY_HASH", { TYPE_INTEGER, .i = SDLK_HASH } },
		{ "KEY_PERCENT", { TYPE_INTEGER, .i = SDLK_PERCENT } },
		{ "KEY_DOLLAR", { TYPE_INTEGER, .i = SDLK_DOLLAR } },
		{ "KEY_AMPERSAND", { TYPE_INTEGER, .i = SDLK_AMPERSAND } },
		{ "KEY_QUOTE", { TYPE_INTEGER, .i = SDLK_QUOTE } },
		{ "KEY_LEFTPAREN", { TYPE_INTEGER, .i = SDLK_LEFTPAREN } },
		{ "KEY_RIGHTPAREN", { TYPE_INTEGER, .i = SDLK_RIGHTPAREN } },
		{ "KEY_ASTERISK", { TYPE_INTEGER, .i = SDLK_ASTERISK } },
		{ "KEY_PLUS", { TYPE_INTEGER, .i = SDLK_PLUS } },
		{ "KEY_COMMA", { TYPE_INTEGER, .i = SDLK_COMMA } },
		{ "KEY_MINUS", { TYPE_INTEGER, .i = SDLK_MINUS } },
		{ "KEY_PERIOD", { TYPE_INTEGER, .i = SDLK_PERIOD } },
		{ "KEY_SLASH", { TYPE_INTEGER, .i = SDLK_SLASH } },
		{ "KEY_0", { TYPE_INTEGER, .i = SDLK_0 } },
		{ "KEY_1", { TYPE_INTEGER, .i = SDLK_1 } },
		{ "KEY_2", { TYPE_INTEGER, .i = SDLK_2 } },
		{ "KEY_3", { TYPE_INTEGER, .i = SDLK_3 } },
		{ "KEY_4", { TYPE_INTEGER, .i = SDLK_4 } },
		{ "KEY_5", { TYPE_INTEGER, .i = SDLK_5 } },
		{ "KEY_6", { TYPE_INTEGER, .i = SDLK_6 } },
		{ "KEY_7", { TYPE_INTEGER, .i = SDLK_7 } },
		{ "KEY_8", { TYPE_INTEGER, .i = SDLK_8 } },
		{ "KEY_9", { TYPE_INTEGER, .i = SDLK_9 } },
		{ "KEY_COLON", { TYPE_INTEGER, .i = SDLK_COLON } },
		{ "KEY_SEMICOLON", { TYPE_INTEGER, .i = SDLK_SEMICOLON } },
		{ "KEY_LESS", { TYPE_INTEGER, .i = SDLK_LESS } },
		{ "KEY_EQUALS", { TYPE_INTEGER, .i = SDLK_EQUALS } },
		{ "KEY_GREATER", { TYPE_INTEGER, .i = SDLK_GREATER } },
		{ "KEY_QUESTION", { TYPE_INTEGER, .i = SDLK_QUESTION } },
		{ "KEY_AT", { TYPE_INTEGER, .i = SDLK_AT } },

		{ "KEY_LEFTBRACKET", { TYPE_INTEGER, .i = SDLK_LEFTBRACKET } },
		{ "KEY_BACKSLASH", { TYPE_INTEGER, .i = SDLK_BACKSLASH } },
		{ "KEY_RIGHTBRACKET", { TYPE_INTEGER, .i = SDLK_RIGHTBRACKET } },
		{ "KEY_CARET", { TYPE_INTEGER, .i = SDLK_CARET } },
		{ "KEY_UNDERSCORE", { TYPE_INTEGER, .i = SDLK_UNDERSCORE } },
		{ "KEY_BACKQUOTE", { TYPE_INTEGER, .i = SDLK_BACKQUOTE } },
		{ "KEY_a", { TYPE_INTEGER, .i = SDLK_a } },
		{ "KEY_b", { TYPE_INTEGER, .i = SDLK_b } },
		{ "KEY_c", { TYPE_INTEGER, .i = SDLK_c } },
		{ "KEY_d", { TYPE_INTEGER, .i = SDLK_d } },
		{ "KEY_e", { TYPE_INTEGER, .i = SDLK_e } },
		{ "KEY_f", { TYPE_INTEGER, .i = SDLK_f } },
		{ "KEY_g", { TYPE_INTEGER, .i = SDLK_g } },
		{ "KEY_h", { TYPE_INTEGER, .i = SDLK_h } },
		{ "KEY_i", { TYPE_INTEGER, .i = SDLK_i } },
		{ "KEY_j", { TYPE_INTEGER, .i = SDLK_j } },
		{ "KEY_k", { TYPE_INTEGER, .i = SDLK_k } },
		{ "KEY_l", { TYPE_INTEGER, .i = SDLK_l } },
		{ "KEY_m", { TYPE_INTEGER, .i = SDLK_m } },
		{ "KEY_n", { TYPE_INTEGER, .i = SDLK_n } },
		{ "KEY_o", { TYPE_INTEGER, .i = SDLK_o } },
		{ "KEY_p", { TYPE_INTEGER, .i = SDLK_p } },
		{ "KEY_q", { TYPE_INTEGER, .i = SDLK_q } },
		{ "KEY_r", { TYPE_INTEGER, .i = SDLK_r } },
		{ "KEY_s", { TYPE_INTEGER, .i = SDLK_s } },
		{ "KEY_t", { TYPE_INTEGER, .i = SDLK_t } },
		{ "KEY_u", { TYPE_INTEGER, .i = SDLK_u } },
		{ "KEY_v", { TYPE_INTEGER, .i = SDLK_v } },
		{ "KEY_w", { TYPE_INTEGER, .i = SDLK_w } },
		{ "KEY_x", { TYPE_INTEGER, .i = SDLK_x } },
		{ "KEY_y", { TYPE_INTEGER, .i = SDLK_y } },
		{ "KEY_z", { TYPE_INTEGER, .i = SDLK_z } },

		{ "KEY_CAPSLOCK", { TYPE_INTEGER, .i = SDLK_CAPSLOCK } },

		{ "KEY_F1", { TYPE_INTEGER, .i = SDLK_F1 } },
		{ "KEY_F2", { TYPE_INTEGER, .i = SDLK_F2 } },
		{ "KEY_F3", { TYPE_INTEGER, .i = SDLK_F3 } },
		{ "KEY_F4", { TYPE_INTEGER, .i = SDLK_F4 } },
		{ "KEY_F5", { TYPE_INTEGER, .i = SDLK_F5 } },
		{ "KEY_F6", { TYPE_INTEGER, .i = SDLK_F6 } },
		{ "KEY_F7", { TYPE_INTEGER, .i = SDLK_F7 } },
		{ "KEY_F8", { TYPE_INTEGER, .i = SDLK_F8 } },
		{ "KEY_F9", { TYPE_INTEGER, .i = SDLK_F9 } },
		{ "KEY_F10", { TYPE_INTEGER, .i = SDLK_F10 } },
		{ "KEY_F11", { TYPE_INTEGER, .i = SDLK_F11 } },
		{ "KEY_F12", { TYPE_INTEGER, .i = SDLK_F12 } },

		{ "KEY_PRINTSCREEN", { TYPE_INTEGER, .i = SDLK_PRINTSCREEN } },
		{ "KEY_SCROLLLOCK", { TYPE_INTEGER, .i = SDLK_SCROLLLOCK } },
		{ "KEY_PAUSE", { TYPE_INTEGER, .i = SDLK_PAUSE } },
		{ "KEY_INSERT", { TYPE_INTEGER, .i = SDLK_INSERT } },
		{ "KEY_HOME", { TYPE_INTEGER, .i = SDLK_HOME } },
		{ "KEY_PAGEUP", { TYPE_INTEGER, .i = SDLK_PAGEUP } },
		{ "KEY_DELETE", { TYPE_INTEGER, .i = SDLK_DELETE } },
		{ "KEY_END", { TYPE_INTEGER, .i = SDLK_END } },
		{ "KEY_PAGEDOWN", { TYPE_INTEGER, .i = SDLK_PAGEDOWN } },
		{ "KEY_RIGHT", { TYPE_INTEGER, .i = SDLK_RIGHT } },
		{ "KEY_LEFT", { TYPE_INTEGER, .i = SDLK_LEFT } },
		{ "KEY_DOWN", { TYPE_INTEGER, .i = SDLK_DOWN } },
		{ "KEY_UP", { TYPE_INTEGER, .i = SDLK_UP } },

		{ "KEY_NUMLOCKCLEAR", { TYPE_INTEGER, .i = SDLK_NUMLOCKCLEAR } },
		{ "KEY_KP_DIVIDE", { TYPE_INTEGER, .i = SDLK_KP_DIVIDE } },
		{ "KEY_KP_MULTIPLY", { TYPE_INTEGER, .i = SDLK_KP_MULTIPLY } },
		{ "KEY_KP_MINUS", { TYPE_INTEGER, .i = SDLK_KP_MINUS } },
		{ "KEY_KP_PLUS", { TYPE_INTEGER, .i = SDLK_KP_PLUS } },
		{ "KEY_KP_ENTER", { TYPE_INTEGER, .i = SDLK_KP_ENTER } },
		{ "KEY_KP_1", { TYPE_INTEGER, .i = SDLK_KP_1 } },
		{ "KEY_KP_2", { TYPE_INTEGER, .i = SDLK_KP_2 } },
		{ "KEY_KP_3", { TYPE_INTEGER, .i = SDLK_KP_3 } },
		{ "KEY_KP_4", { TYPE_INTEGER, .i = SDLK_KP_4 } },
		{ "KEY_KP_5", { TYPE_INTEGER, .i = SDLK_KP_5 } },
		{ "KEY_KP_6", { TYPE_INTEGER, .i = SDLK_KP_6 } },
		{ "KEY_KP_7", { TYPE_INTEGER, .i = SDLK_KP_7 } },
		{ "KEY_KP_8", { TYPE_INTEGER, .i = SDLK_KP_8 } },
		{ "KEY_KP_9", { TYPE_INTEGER, .i = SDLK_KP_9 } },
		{ "KEY_KP_0", { TYPE_INTEGER, .i = SDLK_KP_0 } },
		{ "KEY_KP_PERIOD", { TYPE_INTEGER, .i = SDLK_KP_PERIOD } },

		{ "KEY_APPLICATION", { TYPE_INTEGER, .i = SDLK_APPLICATION } },
		{ "KEY_POWER", { TYPE_INTEGER, .i = SDLK_POWER } },
		{ "KEY_KP_EQUALS", { TYPE_INTEGER, .i = SDLK_KP_EQUALS } },
		{ "KEY_F13", { TYPE_INTEGER, .i = SDLK_F13 } },
		{ "KEY_F14", { TYPE_INTEGER, .i = SDLK_F14 } },
		{ "KEY_F15", { TYPE_INTEGER, .i = SDLK_F15 } },
		{ "KEY_F16", { TYPE_INTEGER, .i = SDLK_F16 } },
		{ "KEY_F17", { TYPE_INTEGER, .i = SDLK_F17 } },
		{ "KEY_F18", { TYPE_INTEGER, .i = SDLK_F18 } },
		{ "KEY_F19", { TYPE_INTEGER, .i = SDLK_F19 } },
		{ "KEY_F20", { TYPE_INTEGER, .i = SDLK_F20 } },
		{ "KEY_F21", { TYPE_INTEGER, .i = SDLK_F21 } },
		{ "KEY_F22", { TYPE_INTEGER, .i = SDLK_F22 } },
		{ "KEY_F23", { TYPE_INTEGER, .i = SDLK_F23 } },
		{ "KEY_F24", { TYPE_INTEGER, .i = SDLK_F24 } },
		{ "KEY_EXECUTE", { TYPE_INTEGER, .i = SDLK_EXECUTE } },
		{ "KEY_HELP", { TYPE_INTEGER, .i = SDLK_HELP } },
		{ "KEY_MENU", { TYPE_INTEGER, .i = SDLK_MENU } },
		{ "KEY_SELECT", { TYPE_INTEGER, .i = SDLK_SELECT } },
		{ "KEY_STOP", { TYPE_INTEGER, .i = SDLK_STOP } },
		{ "KEY_AGAIN", { TYPE_INTEGER, .i = SDLK_AGAIN } },
		{ "KEY_UNDO", { TYPE_INTEGER, .i = SDLK_UNDO } },
		{ "KEY_CUT", { TYPE_INTEGER, .i = SDLK_CUT } },
		{ "KEY_COPY", { TYPE_INTEGER, .i = SDLK_COPY } },
		{ "KEY_PASTE", { TYPE_INTEGER, .i = SDLK_PASTE } },
		{ "KEY_FIND", { TYPE_INTEGER, .i = SDLK_FIND } },
		{ "KEY_MUTE", { TYPE_INTEGER, .i = SDLK_MUTE } },
		{ "KEY_VOLUMEUP", { TYPE_INTEGER, .i = SDLK_VOLUMEUP } },
		{ "KEY_VOLUMEDOWN", { TYPE_INTEGER, .i = SDLK_VOLUMEDOWN } },
		{ "KEY_KP_COMMA", { TYPE_INTEGER, .i = SDLK_KP_COMMA } },
		{ "KEY_KP_EQUALSAS400", { TYPE_INTEGER, .i = SDLK_KP_EQUALSAS400 } },

		{ "KEY_ALTERASE", { TYPE_INTEGER, .i = SDLK_ALTERASE } },
		{ "KEY_SYSREQ", { TYPE_INTEGER, .i = SDLK_SYSREQ } },
		{ "KEY_CANCEL", { TYPE_INTEGER, .i = SDLK_CANCEL } },
		{ "KEY_CLEAR", { TYPE_INTEGER, .i = SDLK_CLEAR } },
		{ "KEY_PRIOR", { TYPE_INTEGER, .i = SDLK_PRIOR } },
		{ "KEY_RETURN2", { TYPE_INTEGER, .i = SDLK_RETURN2 } },
		{ "KEY_SEPARATOR", { TYPE_INTEGER, .i = SDLK_SEPARATOR } },
		{ "KEY_OUT", { TYPE_INTEGER, .i = SDLK_OUT } },
		{ "KEY_OPER", { TYPE_INTEGER, .i = SDLK_OPER } },
		{ "KEY_CLEARAGAIN", { TYPE_INTEGER, .i = SDLK_CLEARAGAIN } },
		{ "KEY_CRSEL", { TYPE_INTEGER, .i = SDLK_CRSEL } },
		{ "KEY_EXSEL", { TYPE_INTEGER, .i = SDLK_EXSEL } },

		{ "KEY_KP_00", { TYPE_INTEGER, .i = SDLK_KP_00 } },
		{ "KEY_KP_000", { TYPE_INTEGER, .i = SDLK_KP_000 } },
		{ "KEY_THOUSANDSSEPARATOR", { TYPE_INTEGER, .i = SDLK_THOUSANDSSEPARATOR } },
		{ "KEY_DECIMALSEPARATOR", { TYPE_INTEGER, .i = SDLK_DECIMALSEPARATOR } },
		{ "KEY_CURRENCYUNIT", { TYPE_INTEGER, .i = SDLK_CURRENCYUNIT } },
		{ "KEY_CURRENCYSUBUNIT", { TYPE_INTEGER, .i = SDLK_CURRENCYSUBUNIT } },
		{ "KEY_KP_LEFTPAREN", { TYPE_INTEGER, .i = SDLK_KP_LEFTPAREN } },
		{ "KEY_KP_RIGHTPAREN", { TYPE_INTEGER, .i = SDLK_KP_RIGHTPAREN } },
		{ "KEY_KP_LEFTBRACE", { TYPE_INTEGER, .i = SDLK_KP_LEFTBRACE } },
		{ "KEY_KP_RIGHTBRACE", { TYPE_INTEGER, .i = SDLK_KP_RIGHTBRACE } },
		{ "KEY_KP_TAB", { TYPE_INTEGER, .i = SDLK_KP_TAB } },
		{ "KEY_KP_BACKSPACE", { TYPE_INTEGER, .i = SDLK_KP_BACKSPACE } },
		{ "KEY_KP_A", { TYPE_INTEGER, .i = SDLK_KP_A } },
		{ "KEY_KP_B", { TYPE_INTEGER, .i = SDLK_KP_B } },
		{ "KEY_KP_C", { TYPE_INTEGER, .i = SDLK_KP_C } },
		{ "KEY_KP_D", { TYPE_INTEGER, .i = SDLK_KP_D } },
		{ "KEY_KP_E", { TYPE_INTEGER, .i = SDLK_KP_E } },
		{ "KEY_KP_F", { TYPE_INTEGER, .i = SDLK_KP_F } },
		{ "KEY_KP_XOR", { TYPE_INTEGER, .i = SDLK_KP_XOR } },
		{ "KEY_KP_POWER", { TYPE_INTEGER, .i = SDLK_KP_POWER } },
		{ "KEY_KP_PERCENT", { TYPE_INTEGER, .i = SDLK_KP_PERCENT } },
		{ "KEY_KP_LESS", { TYPE_INTEGER, .i = SDLK_KP_LESS } },
		{ "KEY_KP_GREATER", { TYPE_INTEGER, .i = SDLK_KP_GREATER } },
		{ "KEY_KP_AMPERSAND", { TYPE_INTEGER, .i = SDLK_KP_AMPERSAND } },
		{ "KEY_KP_DBLAMPERSAND", { TYPE_INTEGER, .i = SDLK_KP_DBLAMPERSAND } },
		{ "KEY_KP_VERTICALBAR", { TYPE_INTEGER, .i = SDLK_KP_VERTICALBAR } },
		{ "KEY_KP_DBLVERTICALBAR", { TYPE_INTEGER, .i = SDLK_KP_DBLVERTICALBAR } },
		{ "KEY_KP_COLON", { TYPE_INTEGER, .i = SDLK_KP_COLON } },
		{ "KEY_KP_HASH", { TYPE_INTEGER, .i = SDLK_KP_HASH } },
		{ "KEY_KP_SPACE", { TYPE_INTEGER, .i = SDLK_KP_SPACE } },
		{ "KEY_KP_AT", { TYPE_INTEGER, .i = SDLK_KP_AT } },
		{ "KEY_KP_EXCLAM", { TYPE_INTEGER, .i = SDLK_KP_EXCLAM } },
		{ "KEY_KP_MEMSTORE", { TYPE_INTEGER, .i = SDLK_KP_MEMSTORE } },
		{ "KEY_KP_MEMRECALL", { TYPE_INTEGER, .i = SDLK_KP_MEMRECALL } },
		{ "KEY_KP_MEMCLEAR", { TYPE_INTEGER, .i = SDLK_KP_MEMCLEAR } },
		{ "KEY_KP_MEMADD", { TYPE_INTEGER, .i = SDLK_KP_MEMADD } },
		{ "KEY_KP_MEMSUBTRACT", { TYPE_INTEGER, .i = SDLK_KP_MEMSUBTRACT } },
		{ "KEY_KP_MEMMULTIPLY", { TYPE_INTEGER, .i = SDLK_KP_MEMMULTIPLY } },
		{ "KEY_KP_MEMDIVIDE", { TYPE_INTEGER, .i = SDLK_KP_MEMDIVIDE } },
		{ "KEY_KP_PLUSMINUS", { TYPE_INTEGER, .i = SDLK_KP_PLUSMINUS } },
		{ "KEY_KP_CLEAR", { TYPE_INTEGER, .i = SDLK_KP_CLEAR } },
		{ "KEY_KP_CLEARENTRY", { TYPE_INTEGER, .i = SDLK_KP_CLEARENTRY } },
		{ "KEY_KP_BINARY", { TYPE_INTEGER, .i = SDLK_KP_BINARY } },
		{ "KEY_KP_OCTAL", { TYPE_INTEGER, .i = SDLK_KP_OCTAL } },
		{ "KEY_KP_DECIMAL", { TYPE_INTEGER, .i = SDLK_KP_DECIMAL } },
		{ "KEY_KP_HEXADECIMAL", { TYPE_INTEGER, .i = SDLK_KP_HEXADECIMAL } },

		{ "KEY_LCTRL", { TYPE_INTEGER, .i = SDLK_LCTRL } },
		{ "KEY_LSHIFT", { TYPE_INTEGER, .i = SDLK_LSHIFT } },
		{ "KEY_LALT", { TYPE_INTEGER, .i = SDLK_LALT } },
		{ "KEY_LGUI", { TYPE_INTEGER, .i = SDLK_LGUI } },
		{ "KEY_RCTRL", { TYPE_INTEGER, .i = SDLK_RCTRL } },
		{ "KEY_RSHIFT", { TYPE_INTEGER, .i = SDLK_RSHIFT } },
		{ "KEY_RALT", { TYPE_INTEGER, .i = SDLK_RALT } },
		{ "KEY_RGUI", { TYPE_INTEGER, .i = SDLK_RGUI } },

		{ "KEY_MODE", { TYPE_INTEGER, .i = SDLK_MODE } },

		{ "KEY_AUDIONEXT", { TYPE_INTEGER, .i = SDLK_AUDIONEXT } },
		{ "KEY_AUDIOPREV", { TYPE_INTEGER, .i = SDLK_AUDIOPREV } },
		{ "KEY_AUDIOSTOP", { TYPE_INTEGER, .i = SDLK_AUDIOSTOP } },
		{ "KEY_AUDIOPLAY", { TYPE_INTEGER, .i = SDLK_AUDIOPLAY } },
		{ "KEY_AUDIOMUTE", { TYPE_INTEGER, .i = SDLK_AUDIOMUTE } },
		{ "KEY_MEDIASELECT", { TYPE_INTEGER, .i = SDLK_MEDIASELECT } },
		{ "KEY_WWW", { TYPE_INTEGER, .i = SDLK_WWW } },
		{ "KEY_MAIL", { TYPE_INTEGER, .i = SDLK_MAIL } },
		{ "KEY_CALCULATOR", { TYPE_INTEGER, .i = SDLK_CALCULATOR } },
		{ "KEY_COMPUTER", { TYPE_INTEGER, .i = SDLK_COMPUTER } },
		{ "KEY_AC_SEARCH", { TYPE_INTEGER, .i = SDLK_AC_SEARCH } },
		{ "KEY_AC_HOME", { TYPE_INTEGER, .i = SDLK_AC_HOME } },
		{ "KEY_AC_BACK", { TYPE_INTEGER, .i = SDLK_AC_BACK } },
		{ "KEY_AC_FORWARD", { TYPE_INTEGER, .i = SDLK_AC_FORWARD } },
		{ "KEY_AC_STOP", { TYPE_INTEGER, .i = SDLK_AC_STOP } },
		{ "KEY_AC_REFRESH", { TYPE_INTEGER, .i = SDLK_AC_REFRESH } },
		{ "KEY_AC_BOOKMARKS", { TYPE_INTEGER, .i = SDLK_AC_BOOKMARKS } },

		{ "KEY_BRIGHTNESSDOWN", { TYPE_INTEGER, .i = SDLK_BRIGHTNESSDOWN } },
		{ "KEY_BRIGHTNESSUP", { TYPE_INTEGER, .i = SDLK_BRIGHTNESSUP } },
		{ "KEY_DISPLAYSWITCH", { TYPE_INTEGER, .i = SDLK_DISPLAYSWITCH } },
		{ "KEY_KBDILLUMTOGGLE", { TYPE_INTEGER, .i = SDLK_KBDILLUMTOGGLE } },
		{ "KEY_KBDILLUMDOWN", { TYPE_INTEGER, .i = SDLK_KBDILLUMDOWN } },
		{ "KEY_KBDILLUMUP", { TYPE_INTEGER, .i = SDLK_KBDILLUMUP } },
		{ "KEY_EJECT", { TYPE_INTEGER, .i = SDLK_EJECT } },
		{ "KEY_SLEEP", { TYPE_INTEGER, .i = SDLK_SLEEP } },
		{ "KEY_APP1", { TYPE_INTEGER, .i = SDLK_APP1 } },
		{ "KEY_APP2", { TYPE_INTEGER, .i = SDLK_APP2 } },

		{ "KEY_AUDIOREWIND", { TYPE_INTEGER, .i = SDLK_AUDIOREWIND } },
		{ "KEY_AUDIOFASTFORWARD", { TYPE_INTEGER, .i = SDLK_AUDIOFASTFORWARD } },

		{ "KEY_SOFTLEFT", { TYPE_INTEGER, .i = SDLK_SOFTLEFT } },
		{ "KEY_SOFTRIGHT", { TYPE_INTEGER, .i = SDLK_SOFTRIGHT } },
		{ "KEY_CALL", { TYPE_INTEGER, .i = SDLK_CALL } },
		{ "KEY_ENDCALL", { TYPE_INTEGER, .i = SDLK_ENDCALL } }
	};

	for (Uint32 i = 0; i < ARRLEN(constants); i++) {
		if (strcmp(constants[i].word, parser->word) == 0) {
			parser->value = constants[i].value;
			return 0;
		}
	}
	return -1;
}

static const struct keyword {
	const char *word;
	int (*read)(struct parser *parser);
} parser_keywords[] = {
	{ "break", ReadBreak },
	{ "for", ReadFor },
	{ "if", ReadIf },
	{ "local", ReadLocal },
	{ "return", ReadReturn },
	{ "switch", ReadSwitch },
	{ "this", ReadThis },
	{ "trigger", ReadTrigger },
	{ "while", ReadWhile },
};

const struct keyword *GetKeyword(const char *word)
{
	for (Uint32 i = 0; i < ARRLEN(parser_keywords); i++) {
		if (strcmp(parser_keywords[i].word, word) == 0) {
			return &parser_keywords[i];
		}
	}
	return NULL;
}

static int ReadExpression(struct parser *parser, int precedence)
{
	struct {
		char ch;
		const char *sys;
		int precedence;
	} prefixes[] = {
		{ '+', NULL, 4 },
		{ '-', "neg", 4 },
		{ '!', "not", 6 }
	};
	struct {
		char ch;
		char ext;
		const char *sys;
		int precedence;
	} infixes[] = {
		{ '&', '&', "and", 2 },
		{ '|', '|', "or", 2 },

		/* this need to come before '=', '\0' so that they are not NOT
		 * checked */
		{ '!', '=', "notequals", 3 },
		{ '=', '=', "equals", 3 },

		{ '=', '\0', "=", 1 },

		{ '>', '=', "geq", 3 },
		{ '>', '\0', "gtr", 3 },
		{ '<', '=', "leq", 3 },
		{ '<', '\0', "lss", 3 },

		{ '+', '\0', "sum", 4 },
		{ '-', '\0', "sub", 4 },

		{ '*', '\0', "mul", 5 },
		{ '/', '\0', "div", 5 },
		{ '%', '\0', "mod", 5 },

		{ '.', '\0', ".", 7 }
	};

	Instruction instr;
	char lhb[2];
	char lh;
	Instruction opr;

	for (Uint32 i = 0; i < ARRLEN(prefixes); i++) {
		if (prefixes[i].ch == parser->c) {
			NextChar(parser);
			SkipSpace(parser);
			if (ReadExpression(parser,
						prefixes[i].precedence) < 0) {
				return -1;
			}
			SkipSpace(parser);
			if (prefixes[i].sys == NULL) {
				instr = parser->instruction;
				goto next_infix;
			}
			instr.instr = INSTR_INVOKESYS;
			strcpy(instr.invoke.name, prefixes[i].sys);
			instr.invoke.args = union_Alloc(union_Default(),
					sizeof(*instr.invoke.args));
			if (instr.invoke.args == NULL) {
				return -1;
			}
			instr.invoke.args[0] = parser->instruction;
			instr.invoke.numArgs = 1;
			goto next_infix;
		}
	}

	if (parser->c == '{') {
		if (precedence != 0) {
			return -1;
		}
		if (ReadBody(parser) < 0) {
			return -1;
		}
		instr.instr = INSTR_GROUP;
		instr.group.instructions = parser->instructions;
		instr.group.numInstructions = parser->numInstructions;
		/* there can't be any operators after {} */
		goto end;
	} else if (parser->c == '(') {
		NextChar(parser); /* skip '(' */
		SkipSpace(parser);
		if (ReadExpression(parser, 0) < 0) {
			return -1;
		}
		instr = parser->instruction;
		if (parser->c != ')') {
			return -1;
		}
		NextChar(parser); /* skip ')' */
	} else if (parser->c == '[') {
		if (ReadArray(parser) < 0) {
			return -1;
		}
		parser->value.type = TYPE_ARRAY;
		instr.instr = INSTR_VALUE;
		instr.value.value.type = parser->value.type;
		instr.value.value = parser->value;
	} else if (parser->c == '\"') {
		if (ReadString(parser) < 0) {
			return -1;
		}
		parser->value.type = TYPE_STRING;
		instr.instr = INSTR_VALUE;
		instr.value.value.type = parser->value.type;
		instr.value.value.s = parser->value.s;
	} else if (!isalpha(parser->c) && parser->c != '_') {
		struct int_or_float iof;

		if (ReadIntOrFloat(parser, &iof) < 0) {
			return -1;
		}
		iof_AsPrecise(&iof, &parser->value);
		instr.instr = INSTR_VALUE;
		instr.value.value = parser->value;
	} else if (ReadWord(parser) < 0) {
		return -1;
	} else {
		const struct keyword *keyword;
		type_t type;

		SkipSpace(parser);

		if ((keyword = GetKeyword(parser->word)) != NULL) {
			return keyword->read(parser);
		} else if (parser->c == '(') {
			NextChar(parser); /* skip '(' */
			SkipSpace(parser);
			if (ReadInvoke(parser) < 0) {
				return -1;
			}
			instr = parser->instruction;
		} else if ((type = CheckType(parser)) != TYPE_NULL) {
			if (_ReadValue(parser, type) < 0) {
				return -1;
			}
			parser->value.type = type;
			instr.instr = INSTR_VALUE;
			instr.value.value = parser->value;
		} else if (strcmp(parser->word, "const") == 0) {
			if (ReadWord(parser) < 0) {
				return -1;
			}
			if (ResolveConstant(parser) < 0) {
				return -1;
			}
			instr.instr = INSTR_VALUE;
			instr.value.value = parser->value;
		} else {
			instr.instr = INSTR_VARIABLE;
			strcpy(instr.variable.name, parser->word);
		}
	}

next_infix:
	SkipSpace(parser);
	if (LookAhead(parser, lhb, 2) != 2) {
		lh = 0;
	} else {
		lh = lhb[1];
	}

	for (Uint32 i = 0; i < ARRLEN(infixes); i++) {
		if (infixes[i].ch != parser->c) {
			continue;
		}
		if (infixes[i].ext != '\0' && infixes[i].ext != lh) {
			continue;
		}
		if (infixes[i].precedence <= precedence) {
			/* the higher function will take care of
			 * this operator */
			goto end;
		}
		if (infixes[i].ext != '\0') {
			NextChar(parser);
		}

		/* these have special handles */
		switch (infixes[i].sys[0]) {
		case '=':
			if (instr.instr != INSTR_VARIABLE && instr.instr !=
					INSTR_SUBVARIABLE) {
				return -1;
			}
			opr.instr = INSTR_SET;
			opr.set.dest = union_Alloc(union_Default(),
					sizeof(*opr.set.dest));
			if (opr.set.dest == NULL) {
				return -1;
			}
			*opr.set.dest = instr;
			opr.set.src = union_Alloc(union_Default(),
					sizeof(*opr.set.src));
			if (opr.set.src == NULL) {
				return -1;
			}
			NextChar(parser); /* skip '=' */
			SkipSpace(parser);
			if (ReadExpression(parser, infixes[i].precedence) < 0) {
				return -1;
			}
			*opr.set.src = parser->instruction;
			instr = opr;
			goto next_infix;

		case '.': {
			char sub[MAX_WORD];
			Instruction *from;

			from = union_Alloc(union_Default(),
					sizeof(*from));
			if (from == NULL) {
				return -1;
			}
			*from = instr;

			NextChar(parser); /* skip '.' */
			SkipSpace(parser);
			if (ReadWord(parser) < 0) {
				return -1;
			}
			strcpy(sub, parser->word);

			SkipSpace(parser);
			if (parser->c == '(') {
				Instruction *args;
				Uint32 numArgs;

				NextChar(parser); /* skip '(' */
				SkipSpace(parser);
				if (ReadInvoke(parser) < 0) {
					return -1;
				}
				args = parser->instruction.invoke.args;
				numArgs = parser->instruction.invoke.numArgs;

				opr.instr = INSTR_INVOKESUB;
				opr.invokesub.from = from;
				strcpy(opr.invokesub.sub, sub);
				opr.invokesub.args = args;
				opr.invokesub.numArgs = numArgs;
			} else {
				opr.instr = INSTR_SUBVARIABLE;
				opr.subvariable.from = from;
				strcpy(opr.subvariable.name, sub);
			}
			instr = opr;
			goto next_infix;
		}
		}

		NextChar(parser);
		SkipSpace(parser);
		if (ReadExpression(parser, infixes[i].precedence) < 0) {
			return -1;
		}
		opr.invoke.args = union_Alloc(union_Default(),
				sizeof(*instr.invoke.args) * 2);
		if (opr.invoke.args == NULL) {
			return -1;
		}
		opr.invoke.numArgs = 2;
		opr.invoke.args[0] = instr;
		opr.invoke.args[1] = parser->instruction;
		opr.instr = INSTR_INVOKESYS;
		strcpy(opr.invoke.name, infixes[i].sys);
		instr = opr;
		goto next_infix;
	}
end:
	parser->instruction = instr;
	return 0;
}

static int ReadProperty(struct parser *parser)
{
	if (parser->c != ':') {
		return -1;
	}
	NextChar(parser); /* skip ':' */
	if (ReadWord(parser) < 0) {
		return -1;
	}
	strcpy(parser->property.name, parser->word);
	SkipSpace(parser);
	if (parser->c != '=') {
		return -1;
	}
	NextChar(parser); /* skip '=' */
	SkipSpace(parser);
	if (ReadExpression(parser, 0) < 0) {
		return -1;
	}
	parser->property.instruction = parser->instruction;
	return 0;
}

static int wrapper_AddProperty(Union *uni, RawWrapper *wrapper,
		const RawProperty *property)
{
	RawProperty *newProperties;

	newProperties = union_Realloc(uni, wrapper->properties,
			sizeof(*wrapper->properties) *
			(wrapper->numProperties + 1));
	if (newProperties == NULL) {
		return -1;
	}
	wrapper->properties = newProperties;
	wrapper->properties[wrapper->numProperties++] = *property;
	return 0;
}

static inline Uint32 FindWrapper(const RawWrapper *wrappers,
		Uint32 numWrappers,
		const char *name)
{
	/* we search in reverse to maybe reduce lookup times */
	for (Uint32 i = numWrappers; i > 0; ) {
		i--;
		if (strcmp(wrappers[i].label, name) == 0) {
			return i;
		}
	}
	return UINT32_MAX;
}

int prop_ParseString(const char *str, Union *uni, RawWrapper **pWrappers,
		Uint32 *pNumWrappers)
{
	Union *defUni;
	Uint32 numPtrs;
	struct parser parser;
	RawWrapper *wrappers, *newWrappers;
	Uint32 numWrappers, curWrapper;

	/* this is for convenience:
	 * all parser functions allocate memory using the default union
	 * but all has to be deallocated when an error occurs which is
	 * annoying so that is why we just store the starting point
	 * of all the pointers we allocate
	 */
	defUni = union_Default();
	numPtrs = defUni->numPointers;

	memset(&parser, 0, sizeof(parser));

	union_Init(&parser.uni, SIZE_MAX);

	wrappers = union_Alloc(&parser.uni, sizeof(*wrappers));
	if (wrappers == NULL) {
		return -1;
	}
	curWrapper = 0;
	numWrappers = 1;
	memset(wrappers, 0, sizeof(*wrappers));

	parser.source = str;
	parser.sourceLength = strlen(str);

	NextChar(&parser);

	while (SkipSpace(&parser), parser.c != EOF) {
		if (parser.c == ':') {
			if (numWrappers == 1) {
				goto fail;
			}
			if (ReadProperty(&parser) < 0) {
				goto fail;
			}
			if (wrapper_AddProperty(&parser.uni,
						&wrappers[numWrappers - 1],
						&parser.property) < 0) {
				goto fail;
			}
			continue;
		}

		if (ReadWord(&parser) < 0) {
			goto fail;
		}
		if (GetKeyword(parser.word) != NULL) {
			goto fail;
		}
		SkipSpace(&parser);
		if (parser.c == ':') {
			NextChar(&parser);
			curWrapper = FindWrapper(wrappers, numWrappers,
					parser.word);
			if (curWrapper != UINT32_MAX) {
				continue;
			}

			newWrappers = union_Realloc(&parser.uni, wrappers,
					sizeof(*wrappers) * (numWrappers + 1));
			if (newWrappers == NULL) {
				goto fail;
			}
			wrappers = newWrappers;
			strcpy(wrappers[numWrappers].label, parser.word);
			wrappers[numWrappers].properties = NULL;
			wrappers[numWrappers].numProperties = 0;
			curWrapper = numWrappers;
			numWrappers++;
		} else if (parser.c == '=') {
			strcpy(parser.property.name, parser.word);
			NextChar(&parser); /* skip '=' */
			SkipSpace(&parser);
			if (ReadExpression(&parser, 0) < 0) {
				goto fail;
			}
			parser.property.instruction = parser.instruction;
			if (wrapper_AddProperty(&parser.uni,
						&wrappers[0],
						&parser.property) < 0) {
				goto fail;
			}
			continue;
		}
	}
	*uni = parser.uni;
	*pWrappers = wrappers;
	*pNumWrappers = numWrappers;
	return 0;

fail:
	union_FreeAll(&parser.uni);
	union_Trim(defUni, numPtrs);
	fprintf(stderr, "parser error at line %ld\n> ", parser.line + 1);
	if (parser.column > 0) {
		for (int i = 0; i < parser.column - 1; i++) {
			fputc(parser.buffer[i], stderr);
		}
		fprintf(stderr, "\033[31m");
		fputc(parser.buffer[parser.column], stderr);
		fprintf(stderr, "\033[0m");
		NextChar(&parser);
	}
	for (int i = 0; i < 20; i++) {
		const int c = NextChar(&parser);
		if (c == EOF || c == '\n') {
			break;
		}
		fputc(c, stderr);
	}
	fputc('\n', stderr);
	return -1;
}

int prop_ParseFile(FILE *file, Union *uni, RawWrapper **pWrappers,
		Uint32 *pNumWrappers)
{
	Union *defUni;
	Uint32 numPtrs;
	struct parser parser;
	RawWrapper *wrappers, *newWrappers;
	Uint32 numWrappers, curWrapper;

	/* this is for convenience:
	 * all parser functions allocate memory using the default union
	 * but all has to be deallocated when an error occurs which is
	 * annoying so that is why we just store the starting point
	 * of all the pointers we allocate
	 */
	defUni = union_Default();
	numPtrs = defUni->numPointers;

	memset(&parser, 0, sizeof(parser));

	union_Init(&parser.uni, SIZE_MAX);

	wrappers = union_Alloc(&parser.uni, sizeof(*wrappers));
	if (wrappers == NULL) {
		return -1;
	}
	curWrapper = 0;
	numWrappers = 1;
	memset(wrappers, 0, sizeof(*wrappers));

	/* we buffer ourselves */
	setvbuf(file, NULL, _IONBF, 0);
	parser.file = file;

	NextChar(&parser);

	while (SkipSpace(&parser), parser.c != EOF) {
		if (parser.c == ':') {
			if (numWrappers == 1) {
				goto fail;
			}
			if (ReadProperty(&parser) < 0) {
				goto fail;
			}
			if (wrapper_AddProperty(&parser.uni,
						&wrappers[numWrappers - 1],
						&parser.property) < 0) {
				goto fail;
			}
			continue;
		}

		if (ReadWord(&parser) < 0) {
			goto fail;
		}
		if (GetKeyword(parser.word) != NULL) {
			goto fail;
		}
		SkipSpace(&parser);
		if (parser.c == ':') {
			NextChar(&parser);
			curWrapper = FindWrapper(wrappers, numWrappers,
					parser.word);
			if (curWrapper != UINT32_MAX) {
				continue;
			}

			newWrappers = union_Realloc(&parser.uni, wrappers,
					sizeof(*wrappers) * (numWrappers + 1));
			if (newWrappers == NULL) {
				goto fail;
			}
			wrappers = newWrappers;
			strcpy(wrappers[numWrappers].label, parser.word);
			wrappers[numWrappers].properties = NULL;
			wrappers[numWrappers].numProperties = 0;
			curWrapper = numWrappers;
			numWrappers++;
		} else if (parser.c == '=') {
			strcpy(parser.property.name, parser.word);
			NextChar(&parser); /* skip '=' */
			SkipSpace(&parser);
			if (ReadExpression(&parser, 0) < 0) {
				goto fail;
			}
			parser.property.instruction = parser.instruction;
			if (wrapper_AddProperty(&parser.uni,
						&wrappers[0],
						&parser.property) < 0) {
				goto fail;
			}
			continue;
		}
	}
	*uni = parser.uni;
	*pWrappers = wrappers;
	*pNumWrappers = numWrappers;
	return 0;

fail:
	union_FreeAll(&parser.uni);
	union_Trim(defUni, numPtrs);
	fprintf(stderr, "parser error at line %ld\n> ", parser.line + 1);
	Uint32 p = parser.iRead, i, n = 0;
	for (i = 0; i < (Uint32) parser.column; i++) {
		if (p == 0) {
			p = sizeof(parser.buffer) - 1;
		} else {
			p--;
		}
		if (p == parser.iWrite) {
			break;
		}
		n++;
	}
	for (i = 0; i < n; i++) {
		if (!isspace(parser.buffer[p])) {
			break;
		}
		if (p == sizeof(parser.buffer) - 1) {
			p = 0;
		} else {
			p++;
		}
	}
	if (n > 0) {
		for (i = 0; i < n - 1; i++) {
			fputc(parser.buffer[p], stderr);
			if (p == sizeof(parser.buffer) - 1) {
				p = 0;
			} else {
				p++;
			}
		}
		fprintf(stderr, "\033[31m");
		fputc(parser.buffer[p], stderr);
		fprintf(stderr, "\033[0m");
		NextChar(&parser);
	}
	for (i = 0; i < 20; i++) {
		const int c = NextChar(&parser);
		if (c == EOF || c == '\n') {
			break;
		}
		fputc(c, stderr);
	}
	fputc('\n', stderr);
	return -1;
}

Instruction *parse_Expression(const char *str, Uint32 length)
{
	struct parser parser;
	Instruction *instr;

	memset(&parser, 0, sizeof(parser));
	parser.source = str;
	parser.sourceLength = length;
	NextChar(&parser);
	SkipSpace(&parser);
	if (ReadExpression(&parser, 0) < 0) {
		return NULL;
	}
	instr = union_Alloc(union_Default(), sizeof(*instr));
	if (instr == NULL) {
		return NULL;
	}
	*instr = parser.instruction;
	return instr;
}
