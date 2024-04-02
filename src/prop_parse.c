#include "gui.h"

#define PARSER_BUFFER 1024

struct parser {
	Union uni;
	FILE *file;
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
		if (parser->iRead < parser->iWrite) {
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) -
					parser->iWrite, parser->file);
			if (parser->iRead > 0) {
				parser->iWrite = fread(&parser->buffer[0], 1,
						parser->iRead - 1,
						parser->file);
			} else {
				parser->iWrite += n;
			}
		} else {
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) - 1 -
					nWritten, parser->file);
			parser->iWrite += n;
		}
	}
}

static int NextChar(struct parser *parser)
{
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
	} else if (parser->c != EOF) {
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

	if (nBuf == 0 || parser->c == EOF ||
			nBuf >= sizeof(parser->buffer) / 2) {
		return 0;
	}

	buf[0] = parser->c;
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

static int ReadInts(struct parser *parser, Sint64 *ints, Uint32 maxNumInts,
		Uint32 *pNumInts)
{
	Uint32 i = 0;
	Value v;

	if (parser->c != '(') {
		return -1;
	}
	NextChar(parser); /* skip '(' */
	SkipSpace(parser);
	while (parser->c != ')') {
		if (i == maxNumInts) {
			return -1;
		}
		if (ReadValue(parser) < 0) {
			return -1;
		}
		SkipSpace(parser);
		if (parser->c == ',') {
			NextChar(parser); /* skip ',' */
			SkipSpace(parser);
		} else if (parser->c != ')') {
			return -1;
		}
		if (value_Cast(&parser->value, TYPE_INTEGER, &v) < 0) {
			return -1;
		}
		ints[i++] = v.i;
	}
	NextChar(parser); /* skip ')' */
	*pNumInts = i;
	return 0;
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

	Sint64 argb[4];
	Uint32 num;
	char *w;

	if (isalpha(parser->c)) {
		if (ReadWord(parser) < 0) {
			return -1;
		}
		w = parser->word;
		if (w[0] == 'a') {
			w++;
		}
		if (strcmp(w, "rgb") == 0) {
			if (ReadInts(parser, argb, ARRLEN(argb), &num) < 0) {
				return -1;
			}
			switch (num) {
			case 0:
				parser->value.c = 0;
				break;
			case 1: {
				const Uint8 gray = argb[0];
				parser->value.c = 0xff000000 |
					(gray << 16) | (gray << 8) | gray;
				break;
			}
			case 3: {
				const Uint8 r = argb[0], g = argb[1],
					b = argb[2];
				parser->value.c = 0xff000000 |
					(r << 16) | (g << 8) | b;
				break;
			}
			case 4: {
				const Uint8 a = argb[0], r = argb[1],
					g = argb[2], b = argb[3];
				parser->value.c = (a << 24) |
					(r << 16) | (g << 8) | b;
				break;
			}
			default:
				return -1;
			}
			return 0;
		}
		for (size_t i = 0; i < ARRLEN(colors); i++) {
			if (strcmp(colors[i].name, parser->word) == 0) {
				parser->value.c = colors[i].color;
				return 0;
			}
		}
		return -1;
	} else {
		struct int_or_float iof;

		if (ReadIntOrFloat(parser, &iof) < 0) {
			return -1;
		}
		parser->value.c = iof_AsInt(&iof);
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

static int ReadPoint(struct parser *parser)
{
	Sint64 nums[2];
	Uint32 num;

	if (ReadInts(parser, nums, ARRLEN(nums), &num) < 0) {
		return -1;
	}
	switch (num) {
	case 0:
		parser->value.p = (Point) { 0, 0 };
		break;
	case 2:
		parser->value.p = (Point) { nums[0], nums[1] };
		break;
	default:
		return -1;
	}
	return 0;
}

static int ReadRect(struct parser *parser)
{
	Sint64 nums[4];
	Uint32 num;

	if (ReadInts(parser, nums, ARRLEN(nums), &num) < 0) {
		return -1;
	}
	switch (num) {
	case 0:
		parser->value.r = (Rect) { 0, 0, 0, 0 };
		break;
	case 2:
		parser->value.r = (Rect) { .w = nums[0], .h = nums[1] };
		break;
	case 4:
		parser->value.r = (Rect) { nums[0], nums[1], nums[2], nums[3] };
		break;
	default:
		return -1;
	}
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

static int ReadThis(struct parser *parser)
{
	parser->instruction.instr = INSTR_THIS;
	return 0;
}

static int ReadTrigger(struct parser *parser)
{
	if (ReadWord(parser) < 0) {
		return -1;
	}
	parser->instruction.instr = INSTR_TRIGGER;
	strcpy(parser->instruction.trigger.name, parser->word);
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
		[TYPE_POINT] = ReadPoint,
		[TYPE_RECT] = ReadRect,
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

		{ "BUTTON_LEFT", { TYPE_INTEGER, .i = SDL_BUTTON_LEFT } },
		{ "BUTTON_MIDDLE", { TYPE_INTEGER, .i = SDL_BUTTON_MIDDLE } },
		{ "BUTTON_RIGHT", { TYPE_INTEGER, .i = SDL_BUTTON_RIGHT } },
	};

	for (Uint32 i = 0; i < ARRLEN(constants); i++) {
		if (strcmp(constants[i].word, parser->word) == 0) {
			parser->value = constants[i].value;
			return 0;
		}
	}
	return -1;
}

static int ReadExpression(struct parser *parser, int precedence)
{
	static const struct {
		const char *word;
		int (*read)(struct parser *parser);
	} keywords[] = {
		{ "break", ReadBreak },
		{ "for", ReadFor },
		{ "if", ReadIf },
		{ "local", ReadLocal },
		{ "return", ReadReturn },
		{ "this", ReadThis },
		{ "trigger", ReadTrigger },
		{ "while", ReadWhile },
	};

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
		/* this need to come before '=', '\0' so that they are not NOT
		 * checked */
		{ '!', '=', "notequals", 2 },
		{ '=', '=', "equals", 2 },

		{ '=', '\0', "=", 1 },

		{ '>', '=', "geq", 2 },
		{ '>', '\0', "gtr", 2 },
		{ '<', '=', "leq", 2 },
		{ '<', '\0', "lss", 2 },

		{ '&', '&', "and", 3 },
		{ '|', '|', "or", 3 },

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
		SkipSpace(parser);

		const type_t type = CheckType(parser);
		if (type != TYPE_NULL) {
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
			if (precedence == 0) {
				for (size_t i = 0; i < ARRLEN(keywords); i++) {
					if (strcmp(keywords[i].word,
								parser->word) ==
							0) {
						/* there can't be any operators
						 * after a keyword */
						return keywords[i].read(parser);
					}
				}
			}

			if (parser->c == '(') {
				NextChar(parser); /* skip '(' */
				SkipSpace(parser);
				if (ReadInvoke(parser) < 0) {
					return -1;
				}
				instr = parser->instruction;
			} else {
				instr.instr = INSTR_VARIABLE;
				strcpy(instr.variable.name, parser->word);
			}
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

int prop_Parse(FILE *file, Union *uni, RawWrapper **pWrappers,
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
