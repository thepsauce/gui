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
	char label[MAX_WORD];
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
		[TYPE_FONT] = "font",
		[TYPE_FUNCTION] = "function",
		[TYPE_INTEGER] = "int",
		[TYPE_POINT] = "point",
		[TYPE_RECT] = "rect",
		[TYPE_STRING] = "string",
		[TYPE_VIEW] = "view",
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
		{ "black", 0x000000 },
		{ "white", 0xffffff },
		{ "red", 0xff0000 },
		{ "green", 0x00ff00 },
		{ "blue", 0x0000ff },
		{ "yellow", 0xffff00 },
		{ "cyan", 0x00ffff },
		{ "magenta", 0xff00ff },
		{ "gray", 0x808080 },
		{ "orange", 0xffa500 },
		{ "purple", 0x800080 },
		{ "brown", 0xa52a2a },
		{ "pink", 0xffc0cb },
		{ "olive", 0x808000 },
		{ "teal", 0x008080 },
		{ "navy", 0x000080 }
	};

	if (isdigit(parser->c)) {
		return ReadInt(parser);
	}
	if (isalpha(parser->c)) {
		if (ReadWord(parser) < 0) {
			return -1;
		}
		for (size_t i = 0; i < ARRLEN(colors); i++) {
			if (strcmp(colors[i].name, parser->word) == 0) {
				parser->value.c = colors[i].color;
				return 0;
			}
		}
	}
	return -1;
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
	Sint32 sign;
	float front = 0, back = 0;
	Sint32 nFront = 0, nBack = 0;
	Sint32 signExponent = 0;
	Sint32 exponent = 0;

	if (parser->c == '+') {
		sign = 1;
		NextChar(parser);
	} else if (parser->c == '-') {
		sign = -1;
		NextChar(parser);
	} else {
		sign = 1;
	}

	while (isdigit(parser->c)) {
		front *= 10;
		front += parser->c - '0';
		nFront++;
		NextChar(parser);
	}
	if (parser->c == '.') {
		NextChar(parser);
		while (isdigit(parser->c)) {
			back *= 10;
			back += parser->c - '0';
			nBack++;
			NextChar(parser);
		}
	}
	if (nFront == 0 && nBack == 0) {
		return -1;
	}

	if (parser->c == 'e' || parser->c == 'E') {
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
	}

	exponent *= signExponent;
	if (nBack > exponent) {
		for (Sint32 i = exponent; i < nBack; i++) {
			back /= 10;
		}
	} else {
		for (Sint32 i = nBack; i < exponent; i++) {
			back *= 10;
		}
	}
	if (exponent < 0) {
		for (Sint32 i = exponent; i < 0; i++) {
			front /= 10;
		}
	} else {
		for (Sint32 i = 0; i < exponent; i++) {
			front *= 10;
		}
	}
	parser->value.f = sign * (front + back);
	return 0;
}

static int ReadInstruction(struct parser *parser);

static int ReadBody(struct parser *parser)
{
	Instruction *instructions = NULL, *newInstructions;
	Uint32 numInstructions = 0;

	if (parser->c != '{') {
		return -1;
	}
	NextChar(parser); /* skip '{' */
	while (SkipSpace(parser), parser->c != '}') {
		if (ReadInstruction(parser) < 0) {
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

static int ReadFont(struct parser *parser)
{
	if (ReadWord(parser) < 0) {
		return -1;
	}
	if (!strcmp(parser->word, "default")) {
		parser->value.font = NULL;
		return 0;
	}
	return -1;
}

static int ReadChar(struct parser *parser)
{
	int c;

	if (parser->c != '\'') {
		return -1;
	}
	NextChar(parser);
	if(parser->c == '\\') {
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
	parser->value.i = c;
	return 0;
}

static int ReadInt(struct parser *parser)
{
	Sint64 sign;
	Sint64 num = 0;

	if (parser->c == '\'') {
		return ReadChar(parser);
	}

	if (parser->c == '+') {
		sign = 1;
		NextChar(parser);
	} else if (parser->c == '-') {
		sign = -1;
		NextChar(parser);
	} else {
		sign = 1;
	}

	if (!isdigit(parser->c)) {
		return -1;
	}

	if (parser->c == '0') {
		int c;

		c = tolower(NextChar(parser));
		if (c == 'x') {
			while (c = NextChar(parser),
					(c = HexToInt(c)) >= 0) {
				num <<= 4;
				num += c;
			}
		} else if (c == 'o') {
			while (c = NextChar(parser), c >= '0' &&
					c < '8') {
				num <<= 3;
				num += c - '0';
			}
		} else if (c == 'b') {
			while (c = NextChar(parser), c >= '0' &&
					c < '1') {
				num <<= 1;
				num += c - '0';
			}
		}
	} else {
		do {
			num *= 10;
			num += parser->c - '0';
		} while (NextChar(parser), isdigit(parser->c));
	}
	parser->value.i = sign * num;
	return 0;
}

static int ReadInts(struct parser *parser, Sint64 *ints, Uint32 numInts)
{
	Uint32 i = 0;
	Value v;

	if (parser->c != '(') {
		return -1;
	}
	NextChar(parser); /* skip '(' */
	SkipSpace(parser);
	memset(ints, 0, sizeof(*ints) * numInts);
	while (parser->c != ')') {
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
		if (i == numInts) {
			break;
		}
	}
	NextChar(parser); /* skip ')' */
	return 0;
}

static int ReadPoint(struct parser *parser)
{
	Sint64 nums[2];

	if (ReadInts(parser, nums, ARRLEN(nums)) < 0) {
		return -1;
	}
	parser->value.p = (Point) { nums[0], nums[1] };
	return 0;
}

static int ReadRect(struct parser *parser)
{
	Sint64 nums[4];

	if (ReadInts(parser, nums, ARRLEN(nums)) < 0) {
		return -1;
	}
	parser->value.r = (Rect) { nums[0], nums[1], nums[2], nums[3] };
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
		if (ReadInstruction(parser) < 0) {
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
		if (ReadInstruction(parser) < 0) {
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
		if (ReadInstruction(parser) < 0) {
			return -1;
		}
		to = union_Alloc(union_Default(), sizeof(*to));
		if (to == NULL) {
			return -1;
		}
		*to = parser->instruction;
		SkipSpace(parser);
	}

	if (ReadInstruction(parser) < 0) {
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
	char text[5];
	Instruction *iff;
	Instruction *cond;
	Instruction *els = NULL;

	if (ReadInstruction(parser) < 0) {
		return -1;
	}
	cond = union_Alloc(union_Default(), sizeof(*cond));
	if (cond == NULL) {
		return -1;
	}
	*cond = parser->instruction;

	SkipSpace(parser);
	if (ReadInstruction(parser) < 0) {
		return -1;
	}
	iff = union_Alloc(union_Default(), sizeof(*iff));
	if (iff == NULL) {
		return -1;
	}
	*iff = parser->instruction;

	if (LookAhead(parser, text, 5) == 5) {
		if (memcmp(text, "else", 4) == 0 &&
				!isalnum(text[4]) && text[4] != '_') {
			ReadWord(parser);
			SkipSpace(parser);
			if (ReadInstruction(parser) < 0) {
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
		if (ReadInstruction(parser) < 0) {
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
	strcpy(parser->instruction.set.variable, parser->word);
	SkipSpace(parser);
	if (parser->c != '=') {
		return -1;
	}
	NextChar(parser); /* skip '=' */
	SkipSpace(parser);

	/* save this because it will be overwritten */
	const Instruction instruction = parser->instruction;
	if (ReadInstruction(parser) < 0) {
		return -1;
	}
	pInstr = union_Alloc(union_Default(), sizeof(*pInstr));
	if (pInstr == NULL) {
		return -1;
	}
	*pInstr = parser->instruction;
	parser->instruction = instruction;
	parser->instruction.instr = INSTR_LOCAL;
	parser->instruction.set.value = pInstr;
	return 0;
}

static int ReadReturn(struct parser *parser)
{
	Instruction *instr;

	if (ReadInstruction(parser) < 0) {
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

static int _ReadValue(struct parser *parser, type_t type)
{
	static int (*const reads[])(struct parser *parser) = {
		[TYPE_ARRAY] = ReadArray,
		[TYPE_BOOL] = ReadBool,
		[TYPE_COLOR] = ReadColor,
		[TYPE_EVENT] = ReadEvent,
		[TYPE_FLOAT] = ReadFloat,
		[TYPE_FONT] = ReadFont,
		[TYPE_FUNCTION] = ReadFunction,
		[TYPE_INTEGER] = ReadInt,
		[TYPE_POINT] = ReadPoint,
		[TYPE_RECT] = ReadRect,
		[TYPE_STRING] = ReadString,
		[TYPE_VIEW] = ReadView
	};
	if (type == TYPE_NULL) {
		return -1;
	}
	return reads[type](parser);
}

static int ReadValue(struct parser *parser)
{
	type_t type;

	if (isalpha(parser->c)) {
		if (ReadWord(parser) < 0) {
			return -1;
		}
		type = CheckType(parser);
		if (type == TYPE_NULL) {
			return -1;
		}
	} else if (isdigit(parser->c)) {
		type = TYPE_INTEGER;
	} else if (parser->c == '\"') {
		type = TYPE_STRING;
	} else if (parser->c == '[') {
		type = TYPE_ARRAY;
	} else {
		return -1;
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

static int ReadInstruction(struct parser *parser)
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
	};

	if (parser->c == '{') {
		if (ReadBody(parser) < 0) {
			return -1;
		}
		parser->instruction.instr = INSTR_GROUP;
		parser->instruction.group.instructions = parser->instructions;
		parser->instruction.group.numInstructions =
			parser->numInstructions;
		return 0;
	}

	/* implicit array type */
	if (parser->c == '[') {
		if (ReadArray(parser) < 0) {
			return -1;
		}
		parser->value.type = TYPE_ARRAY;
		parser->instruction.instr = INSTR_VALUE;
		parser->instruction.value.value.type = parser->value.type;
		parser->instruction.value.value = parser->value;
		return 0;
	}

	/* implicit int type */
	if (isdigit(parser->c) || parser->c == '\'') {
		if (ReadInt(parser) < 0) {
			return -1;
		}
		parser->value.type = TYPE_INTEGER;
		parser->instruction.instr = INSTR_VALUE;
		parser->instruction.value.value.type = parser->value.type;
		parser->instruction.value.value.i = parser->value.i;
		return 0;
	}

	/* implicit string type */
	if (parser->c == '\"') {
		if (ReadString(parser) < 0) {
			return -1;
		}
		parser->value.type = TYPE_STRING;
		parser->instruction.instr = INSTR_VALUE;
		parser->instruction.value.value.type = parser->value.type;
		parser->instruction.value.value.s = parser->value.s;
		return 0;
	}

	if (ReadWord(parser) < 0) {
		return -1;
	}
	SkipSpace(parser);
	const type_t type = CheckType(parser);
	if (type != TYPE_NULL) {
		if (_ReadValue(parser, type) < 0) {
			return -1;
		}
		parser->value.type = type;

		parser->instruction.instr = INSTR_VALUE;
		parser->instruction.value.value = parser->value;
		return 0;
	}
	if (strcmp(parser->word, "const") == 0) {
		if (ReadWord(parser) < 0) {
			return -1;
		}
		if (ResolveConstant(parser) < 0) {
			return -1;
		}
		parser->instruction.instr = INSTR_VALUE;
		parser->instruction.value.value = parser->value;
		return 0;
	}
	for (size_t i = 0; i < ARRLEN(keywords); i++) {
		if (strcmp(keywords[i].word, parser->word) == 0) {
			return keywords[i].read(parser);
		}
	}
	if (parser->c == '(') {
		NextChar(parser); /* skip '(' */
		SkipSpace(parser);
		return ReadInvoke(parser);
	}
	if (parser->c == '=') {
		Instruction *pInstr;
		char var[MAX_WORD];

		strcpy(var, parser->word);

		NextChar(parser); /* skip '=' */
		SkipSpace(parser);
		if (ReadInstruction(parser) < 0) {
			return -1;
		}
		pInstr = union_Alloc(union_Default(), sizeof(*pInstr));
		if (pInstr == NULL) {
			return -1;
		}
		*pInstr = parser->instruction;
		parser->instruction.instr = INSTR_SET;
		strcpy(parser->instruction.set.variable, var);
		parser->instruction.set.value = pInstr;
		return 0;
	}
	/* assume a variable */
	parser->instruction.instr = INSTR_VARIABLE;
	strcpy(parser->instruction.variable.name, parser->word);
	return 0;
}

static int ReadLabel(struct parser *parser)
{
	long prevLine;

	if (ReadWord(parser) < 0) {
		return -1;
	}
	prevLine = parser->line;
	SkipSpace(parser);
	if (parser->c != ':' && prevLine == parser->line) {
		return -1;
	}
	NextChar(parser);
	strcpy(parser->label, parser->word);
	return 0;
}

static int ReadProperty(struct parser *parser)
{
	if (parser->c != '.') {
		return -1;
	}
	NextChar(parser); /* skip '.' */
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
	if (ReadInstruction(parser) < 0) {
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

int prop_Parse(FILE *file, Union *uni, RawWrapper **pWrappers,
		Uint32 *pNumWrappers)
{
	Union *defUni;
	Uint32 numPtrs;
	struct parser parser;
	RawWrapper *wrappers = NULL, *newWrappers;
	Uint32 numWrappers = 0;

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
	parser.file = file;
	/* we buffer ourselves */
	setvbuf(file, NULL, _IONBF, 0);

	NextChar(&parser);

	while (SkipSpace(&parser), parser.c != EOF) {
		if (parser.c == '.') {
			if (numWrappers == 0) {
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

		if (ReadLabel(&parser) < 0) {
			goto fail;
		}
		newWrappers = union_Realloc(&parser.uni, wrappers,
				sizeof(*wrappers) * (numWrappers + 1));
		if (newWrappers == NULL) {
			goto fail;
		}
		wrappers = newWrappers;
		strcpy(wrappers[numWrappers].label, parser.label);
		wrappers[numWrappers].properties = NULL;
		wrappers[numWrappers].numProperties = 0;
		numWrappers++;
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
	for (i = 0; i < n; i++) {
		fputc(parser.buffer[p], stderr);
		if (p == sizeof(parser.buffer) - 1) {
			p = 0;
		} else {
			p++;
		}
	}
	fputc('\n', stderr);
	return -1;
}
