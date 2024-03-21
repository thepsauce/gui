#include "gui.h"

#define MAX_WORD 256

struct parser {
	Union uni;
	FILE *file;
	long line;
	int column;
	int c;
	int prev_c;
	char word[MAX_WORD];
	int nWord;
	char label[MAX_WORD];
	Value value;
	RawProperty property;
	Instruction instruction;
	Instruction *instructions;
	Uint32 numInstructions;
};

static int parser_NextChar(struct parser *parser)
{
	parser->prev_c = parser->c;
	parser->c = fgetc(parser->file);
	if (parser->c == '\n') {
		parser->line++;
		parser->column = 0;
	} else if (parser->c != EOF) {
		parser->column++;
	}
	return parser->c;
}

static int parser_SkipSpace(struct parser *parser)
{
	bool isComment = false;

	do {
		if (parser->c == ';') {
			isComment = !isComment;
			parser_NextChar(parser);
			continue;
		}
		if (!isspace(parser->c) && !isComment) {
			break;
		}
	} while (parser_NextChar(parser) != EOF);
	return 0;
}

static int parser_ReadWord(struct parser *parser)
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
		parser_NextChar(parser);
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

static int parser_ReadString(struct parser *parser)
{
	struct value_string s;
	char *newData;

	s.data = NULL;
	s.length = 0;

	if (parser->c != '\"') {
		return -1;
	}
	while (parser_NextChar(parser) != EOF) {
		int c;

		if (parser->c == '\"') {
			parser_NextChar(parser); /* skip '"' */
			break;
		}
		if (parser->c == '\\') {
			parser_NextChar(parser);
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

				d1 = HexToInt(parser_NextChar(parser));
				if (d1 < 0) {
					return -1;
				}
				d2 = HexToInt(parser_NextChar(parser));
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
		newData = union_Realloc(union_Default(), s.data, s.length + 1);
		if (newData == NULL) {
			return -1;
		}
		s.data = newData;
		s.data[s.length++] = c;
	}

	parser->value.s = s;
	return 0;
}

static type_t parser_CheckType(struct parser *parser)
{
	static const char *typeNames[] = {
		[TYPE_BOOL] = "bool",
		[TYPE_COLOR] = "color",
		[TYPE_FLOAT] = "float",
		[TYPE_FONT] = "font",
		[TYPE_FUNCTION] = "function",
		[TYPE_INTEGER] = "int",
		[TYPE_OBJECT] = "object",
		[TYPE_STRING] = "string"
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

static int parser_ReadBool(struct parser *parser)
{
	if (parser_ReadWord(parser) < 0) {
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

static int parser_ReadFont(struct parser *parser)
{
	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	if (!strcmp(parser->word, "default")) {
		parser->value.font = NULL;
		return 0;
	}
	return -1;
}

static int parser_ReadInt(struct parser *parser)
{
	Sint64 sign;
	Sint64 num = 0;

	if (parser->c == '+') {
		sign = 1;
		parser_NextChar(parser);
	} else if (parser->c == '-') {
		sign = -1;
		parser_NextChar(parser);
	} else {
		sign = 1;
	}

	if (!isdigit(parser->c)) {
		return -1;
	}

	if (parser->c == '0') {
		int c;

		c = tolower(parser_NextChar(parser));
		if (c == 'x') {
			while (c = parser_NextChar(parser),
					(c = HexToInt(c)) >= 0) {
				num <<= 4;
				num += c;
			}
		} else if (c == 'o') {
			while (c = parser_NextChar(parser), c >= '0' &&
					c < '8') {
				num <<= 3;
				num += c - '0';
			}
		} else if (c == 'b') {
			while (c = parser_NextChar(parser), c >= '0' &&
					c < '1') {
				num <<= 1;
				num += c - '0';
			}
		}
	} else {
		do {
			num *= 10;
			num += parser->c - '0';
		} while (parser_NextChar(parser), isdigit(parser->c));
	}
	parser->value.i = sign * num;
	return 0;
}

static int parser_ReadFloat(struct parser *parser)
{
	Sint32 sign;
	float front = 0, back = 0;
	Sint32 nFront = 0, nBack = 0;
	Sint32 signExponent = 0;
	Sint32 exponent = 0;

	if (parser->c == '+') {
		sign = 1;
		parser_NextChar(parser);
	} else if (parser->c == '-') {
		sign = -1;
		parser_NextChar(parser);
	} else {
		sign = 1;
	}

	while (isdigit(parser->c)) {
		front *= 10;
		front += parser->c - '0';
		nFront++;
		parser_NextChar(parser);
	}
	if (parser->c == '.') {
		parser_NextChar(parser);
		while (isdigit(parser->c)) {
			back *= 10;
			back += parser->c - '0';
			nBack++;
			parser_NextChar(parser);
		}
	}
	if (nFront == 0 && nBack == 0) {
		return -1;
	}

	if (parser->c == 'e' || parser->c == 'E') {
		parser_NextChar(parser);
		if (parser->c == '+') {
			signExponent = 1;
			parser_NextChar(parser);
		} else if (parser->c == '-') {
			signExponent = -1;
			parser_NextChar(parser);
		} else {
			signExponent = 1;
		}
		while (isdigit(parser->c)) {
			exponent *= 10;
			exponent += parser->c - '0';
			parser_NextChar(parser);
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

static int parser_ReadColor(struct parser *parser)
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
		return parser_ReadInt(parser);
	}
	if (isalpha(parser->c)) {
		if (parser_ReadWord(parser) < 0) {
			return -1;
		}
		for (size_t i = 0; i < ARRLEN(colors); i++) {
			if (strcmp(colors[i].name, parser->word) == 0) {
				parser->value.color = colors[i].color;
				return 0;
			}
		}
	}
	return -1;
}

static int parser_ReadObject(struct parser *parser)
{
	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	parser_SkipSpace(parser);
	if (strcmp(parser->word, "this") == 0) {
		/* TODO: this is quite complex and will be added
		 * when the big View interaction is being added */
	}
	return 0;
}

/* -=-=-=-=-=-=-=-=- Instruction -=-=-=-=-=-=-=-=- */
static int parser_ReadInstruction(struct parser *parser);

static int parser_ReadBody(struct parser *parser)
{
	Instruction *instructions = NULL, *newInstructions;
	Uint32 numInstructions = 0;

	if (parser->c != '{') {
		return -1;
	}
	parser_NextChar(parser); /* skip '{' */
	while (parser_SkipSpace(parser), parser->c != '}') {
		if (parser_ReadInstruction(parser) < 0) {
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
	parser_NextChar(parser); /* skip '}' */
	parser->instructions = instructions;
	parser->numInstructions = numInstructions;
	return 0;
}

static int parser_ReadKeyDown(struct parser *parser)
{
	struct key_info ki;

	/* TODO: handle variations */
	ki.state = 0;
	ki.repeat = 0;
	ki.sym.scancode = 0;
	ki.sym.sym = (SDL_Keycode) parser->c;
	ki.sym.mod = KMOD_NONE;
	parser->instruction.event.info.ki = ki;
	parser_NextChar(parser);
	return 0;
}

static int parser_ReadEvent(struct parser *parser)
{
	static const struct {
		const char *word;
		event_t event;
		int (*read)(struct parser *parser);
	} events[] = {
		{ "keydown", EVENT_KEYDOWN, parser_ReadKeyDown },
		/* TODO: */
	};
	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	parser_SkipSpace(parser);
	for (size_t i = 0; i < ARRLEN(events); i++) {
		if (strcmp(events[i].word, parser->word) == 0) {
			parser->instruction.instr = INSTR_EVENT;
			parser->instruction.event.event = events[i].event;
			events[i].read(parser);
			return 0;
		}
	}
	return -1;
}

static int parser_ReadIf(struct parser *parser)
{
	Instruction *cond;

	if (parser_ReadInstruction(parser) < 0) {
		return -1;
	}
	cond = union_Alloc(union_Default(), sizeof(*cond));
	if (cond == NULL) {
		return -1;
	}
	*cond = parser->instruction;
	parser_SkipSpace(parser);
	if (parser_ReadBody(parser) < 0) {
		return -1;
	}
	parser->instruction.instr = INSTR_IF;
	parser->instruction.iff.condition = cond;
	parser->instruction.iff.instructions = parser->instructions;
	parser->instruction.iff.numInstructions = parser->numInstructions;
	return 0;
}

static int parser_ReadInvoke(struct parser *parser)
{
	char name[256];
	Instruction *args = NULL, *newArgs;
	Uint32 numArgs = 0;

	/* assuming that the caller got the invoke name already
	 * and has skipped the '('
	 */
	strcpy(name, parser->word);
	while (1) {
		if (parser_ReadInstruction(parser) < 0) {
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
		parser_SkipSpace(parser);
		if (parser->c != ',') {
			break;
		}
		parser_NextChar(parser);
		parser_SkipSpace(parser);
	}
	if (parser->c != ')') {
		return -1;
	}
	parser_NextChar(parser); /* skip ')' */
	strcpy(parser->instruction.invoke.name, name);
	parser->instruction.instr = INSTR_INVOKE;
	parser->instruction.invoke.args = args;
	parser->instruction.invoke.numArgs = numArgs;
	return 0;
}

static int parser_ReadLocalOrSet(struct parser *parser, instr_t instr)
{
	Instruction *pInstr;

	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	strcpy(parser->instruction.set.variable, parser->word);
	parser_SkipSpace(parser);
	if (parser->c != '=') {
		return -1;
	}
	parser_NextChar(parser); /* skip '=' */
	parser_SkipSpace(parser);

	/* save this because it will be overwritten */
	const Instruction instruction = parser->instruction;
	if (parser_ReadInstruction(parser) < 0) {
		return -1;
	}
	pInstr = union_Alloc(union_Default(), sizeof(*pInstr));
	if (pInstr == NULL) {
		return -1;
	}
	*pInstr = parser->instruction;
	parser->instruction = instruction;
	parser->instruction.instr = instr;
	parser->instruction.set.value = pInstr;
	return 0;
}

static int parser_ReadLocal(struct parser *parser)
{
	return parser_ReadLocalOrSet(parser, INSTR_LOCAL);
}

static int parser_ReadNew(struct parser *parser)
{
	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	parser->instruction.instr = INSTR_NEW;
	strcpy(parser->instruction.new.class, parser->word);
	return 0;
}

static int parser_ReadReturn(struct parser *parser)
{
	Instruction *instr;

	if (parser_ReadInstruction(parser) < 0) {
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

static int parser_ReadSet(struct parser *parser)
{
	return parser_ReadLocalOrSet(parser, INSTR_SET);
}

static int parser_ReadTrigger(struct parser *parser)
{
	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	parser->instruction.instr = INSTR_TRIGGER;
	strcpy(parser->instruction.trigger.name, parser->word);
	return 0;
}

static int parser_ReadFunction(struct parser *parser);

static int parser_ReadValue(struct parser *parser, type_t type)
{
	static int (*const reads[])(struct parser *parser) = {
		[TYPE_BOOL] = parser_ReadBool,
		[TYPE_COLOR] = parser_ReadColor,
		[TYPE_FLOAT] = parser_ReadFloat,
		[TYPE_FONT] = parser_ReadFont,
		[TYPE_FUNCTION] = parser_ReadFunction,
		[TYPE_INTEGER] = parser_ReadInt,
		[TYPE_OBJECT] = parser_ReadObject,
		[TYPE_STRING] = parser_ReadString,
	};
	if (type == TYPE_NULL) {
		return -1;
	}
	return reads[type](parser);
}

static int parser_ReadInstruction(struct parser *parser)
{
	static const struct {
		const char *word;
		int (*read)(struct parser *parser);
	} keywords[] = {
		{ "event", parser_ReadEvent },
		{ "if", parser_ReadIf },
		{ "local", parser_ReadLocal },
		{ "new", parser_ReadNew },
		{ "return", parser_ReadReturn },
		{ "set", parser_ReadSet },
		{ "trigger", parser_ReadTrigger },
	};

	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	parser_SkipSpace(parser);
	const type_t type = parser_CheckType(parser);
	if (type != TYPE_NULL) {
		if (parser_ReadValue(parser, type) < 0) {
			return -1;
		}
		parser->instruction.type = type;
		parser->instruction.instr = INSTR_VALUE;
		parser->instruction.value.value = parser->value;
		return 0;
	}
	for (size_t i = 0; i < ARRLEN(keywords); i++) {
		if (strcmp(keywords[i].word, parser->word) == 0) {
			keywords[i].read(parser);
			return 0;
		}
	}
	if (parser->c == '(') {
		parser_NextChar(parser); /* skip '(' */
		parser_SkipSpace(parser);
		return parser_ReadInvoke(parser);
	}
	/* assume a variable */
	parser->instruction.instr = INSTR_VARIABLE;
	strcpy(parser->instruction.variable.name, parser->word);
	return 0;
}

static int parser_ReadFunction(struct parser *parser)
{
	Function *func;
	type_t type;
	Parameter *params = NULL, *newParams;
	Uint32 numParams = 0;

	func = union_Alloc(union_Default(), sizeof(*func));
	if (func == NULL) {
		return -1;
	}

	/* read parameters */
	while (parser->c != '{') {
		/* parameter type */
		if (parser_ReadWord(parser) < 0) {
			return -1;
		}
		type = parser_CheckType(parser);
		if (type == TYPE_NULL) {
			return -1;
		}
		/* parameter name */
		parser_SkipSpace(parser);
		if (parser_ReadWord(parser) < 0) {
			return -1;
		}
		/* NOT parser->uni! */
		newParams = union_Realloc(union_Default(), params,
				sizeof(*params) * (numParams + 1));
		if (newParams == NULL) {
			return -1;
		}
		params = newParams;
		params[numParams].type = type;
		strcpy(params[numParams].name, parser->word);
		numParams++;
		parser_SkipSpace(parser);
		if (parser->c != ',' && parser->c != '{') {
			return -1;
		}
		if (parser->c == ',') {
			parser_NextChar(parser); /* skip ',' */
			parser_SkipSpace(parser);
		}
	}

	if (parser_ReadBody(parser) < 0) {
		return -1;
	}
	func->params = params;
	func->numParams = numParams;
	func->instructions = parser->instructions;
	func->numInstructions = parser->numInstructions;
	parser->value.func = func;
	return 0;
}

static int parser_ReadLabel(struct parser *parser)
{
	long prevLine;

	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	prevLine = parser->line;
	parser_SkipSpace(parser);
	if (parser->c != ':' && prevLine == parser->line) {
		return -1;
	}
	parser_NextChar(parser);
	strcpy(parser->label, parser->word);
	return 0;
}

static int parser_ReadProperty(struct parser *parser)
{
	if (parser->c != '.') {
		return -1;
	}
	parser_NextChar(parser); /* skip '.' */
	if (parser_ReadWord(parser) < 0) {
		return -1;
	}
	strcpy(parser->property.name, parser->word);
	parser_SkipSpace(parser);
	if (parser->c != '=') {
		return -1;
	}
	parser_NextChar(parser); /* skip '=' */
	parser_SkipSpace(parser);
	if (parser_ReadInstruction(parser) < 0) {
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
	int tabs = 0;

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
	parser.c = fgetc(file);

	while (parser_SkipSpace(&parser), parser.c != EOF) {
		if (parser.c == '.') {
			if (numWrappers == 0) {
				goto fail;
			}
			if (parser_ReadProperty(&parser) < 0) {
				goto fail;
			}
			if (wrapper_AddProperty(&parser.uni,
						&wrappers[numWrappers - 1],
						&parser.property) < 0) {
				goto fail;
			}
			continue;
		}

		if (parser_ReadLabel(&parser) < 0) {
			goto fail;
		}
		newWrappers = union_Realloc(&parser.uni, wrappers,
				sizeof(*wrappers) * (numWrappers + 1));
		if (newWrappers == NULL) {
			goto fail;
		}
		wrappers = newWrappers;
		strcpy(wrappers[numWrappers].label, parser.label);
		numWrappers++;
	}
	*uni = parser.uni;
	*pWrappers = wrappers;
	*pNumWrappers = numWrappers;
	return 0;

fail:
	union_FreeAll(&parser.uni);
	union_Trim(defUni, numPtrs);
	fprintf(stderr, "parser error at line %ld\n", parser.line + 1);
	fseek(file, -parser.column, SEEK_CUR);
	for (int i = 0; i < parser.column; i++) {
		const int c = fgetc(file);
		fputc(c, stderr);
		if (c == '\t') {
			tabs++;
		}
	}
	fputc('\n', stderr);
	for (int i = 0; i < tabs; i++) {
		fputc('\t', stderr);
	}
	for (int i = 0; i < parser.column - tabs; i++) {
		fputc(' ', stderr);
	}
	fputc('^', stderr);
	return -1;
}
