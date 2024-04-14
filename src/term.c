#include "gui.h"

struct term {
	char **sections;
	Uint32 numSections;
	Uint32 capSections;
	Uint32 index, vct;
	Uint32 lenSection;
	Uint32 capSection;
	Uint32 histIndex;
};

static bool term_DoesExecute(struct term *term)
{
	static const struct {
		char start, end;
	} brackets[] = {
		{ '(', ')' },
		{ '[', ']' },
		{ '{', '}' },
	};

	enum state {
		NONE, COMMENT, QUOTE, SINGLE
	};
	static const struct {
		char ch;
		bool cont;
		enum state state;
	} regions[] = {
		{ ';', false, COMMENT },
		{ '\"', true, QUOTE },
		{ '\'', true, SINGLE },
	};

	char *buf;
	Uint32 counts[ARRLEN(brackets)];
	enum state state = NONE;

	memset(counts, 0, sizeof(counts));

	buf = term->sections[term->numSections - 1];
	for (Uint32 i = 0; i < term->lenSection; i++) {
		if (buf[i] == '\\'  && state != COMMENT &&
				i + 1 < term->lenSection) {
			i++;
			continue;
		}

		for (Uint32 b = 0; b < ARRLEN(brackets); b++) {
			if (buf[i] == brackets[b].start) {
				counts[b]++;
			} if (buf[i] == brackets[b].end) {
				if (counts[b] == 0) {
					return true;
				}
				counts[b]--;
			}
		}

		for (Uint32 r = 0; r < ARRLEN(regions); r++) {
			if (buf[i] == regions[r].ch) {
				if (state != regions[r].state) {
					break;
				}
				if (state == NONE) {
					state = regions[r].state;
				} else {
					state = NONE;
				}
			}
		}
	}

	for (Uint32 i = 0; i < ARRLEN(counts); i++) {
		if (counts[i] > 0) {
			return false;
		}
	}
	return state != QUOTE && state != SINGLE;
}

static int term_Append(struct term *term, const char *str, Uint32 lenStr)
{
	char *buf;

	if (term->index + lenStr >= term->capSection) {
		term->capSection *= 2;
		term->capSection += lenStr + 1;
		buf = union_Realloc(union_Default(),
				term->sections[term->numSections - 1],
				term->capSection);
		if (buf == NULL) {
			return -1;
		}
		term->sections[term->numSections - 1] = buf;
	} else {
		buf = term->sections[term->numSections - 1];
	}
	memmove(&buf[term->index + lenStr], &buf[term->index],
			term->lenSection - term->index);
	memcpy(&buf[term->index], str, lenStr);
	term->lenSection += lenStr;
	term->index += lenStr;
	term->vct = term->index;
	buf[term->lenSection] = '\0';
	/* reset history index */
	term->histIndex = term->numSections - 1;
	return 0;
}

static int term_NextSection(struct term *term)
{
	char *buf;
	Uint32 len;
	char **newSections;
	Instruction *instr;
	Value val;

	if (!term_DoesExecute(term)) {
		return term_Append(term, "\n", 1);
	}

	buf = term->sections[term->numSections - 1];
	len = term->lenSection;

	if (term->numSections == term->capSections) {
		term->capSections *= 2;
		term->capSections++;
		newSections = union_Realloc(union_Default(), term->sections,
				sizeof(*term->sections) * term->capSections);
		if (newSections == NULL) {
			return -1;
		}
		term->sections = newSections;
	}
	term->sections[term->numSections] = NULL;
	term->numSections++;

	term->lenSection = 0;
	term->capSection = 0;
	term->index = 0;
	term->vct = 0;
	term->histIndex = term->numSections - 1;

	instr = parse_Expression(buf, len);
	if (instr != NULL) {
		instruction_Execute(instr, &val);
	}
	return 0;
}

static int term_SetSection(struct term *term, const char *str, Uint32 lenStr)
{
	char *buf;

	if (lenStr >= term->capSection) {
		term->capSection = lenStr + 1;
		buf = union_Realloc(union_Default(),
				term->sections[term->numSections - 1],
				term->capSection);
		if (buf == NULL) {
			return -1;
		}
		term->sections[term->numSections - 1] = buf;
	} else {
		buf = term->sections[term->numSections - 1];
	}
	memcpy(buf, str, lenStr);
	term->lenSection = lenStr;
	term->index = lenStr;
	term->vct = lenStr;
	buf[lenStr] = '\0';
	return 0;
}

static void term_Delete(struct term *term, Uint32 from, Uint32 to)
{
	char *buf;

	buf = term->sections[term->numSections - 1];
	memmove(&buf[from], &buf[to], term->lenSection - to);
	term->lenSection -= to - from;
	buf[term->lenSection] = '\0';
}

int BaseProc(View *view, event_t type, EventInfo *info)
{
	static struct term term;

	Rect r, ext;
	Uint32 index;
	int lineSkip;

	(void) view;

	if (term.sections == NULL) {
		term.sections = union_Alloc(union_Default(),
				sizeof(*term.sections));
		term.sections[0] = NULL;
		term.numSections = 1;
		term.capSections = 1;
	}

	switch (type) {
	case EVENT_PAINT:
		renderer_SetDrawColor(0xffffffff);
		r.x = 0;
		r.y = 0;
		lineSkip = renderer_LineSkip();
		for (Uint32 i = 0; i < term.numSections - 1; i++) {
			if (term.sections[i] == NULL) {
				r.y += lineSkip;
				continue;
			}
			renderer_DrawText(term.sections[i],
					strlen(term.sections[i]), &r);
			r.y += r.h;
		}
		renderer_DrawText(term.sections[term.numSections - 1],
				term.lenSection, &r);
		renderer_GetTextExtent(term.sections[term.numSections - 1],
				term.index, &ext);
		ext.y += r.y;
		ext.w = 2;
		renderer_FillRect(&ext);
		break;

	case EVENT_KEYDOWN:
		switch (info->ki.sym.sym) {
		case SDLK_LEFT:
			term.index = utf8_Prev(term.sections[term.numSections - 1],
					term.lenSection, term.index);
			term.vct = term.index;
			break;
		case SDLK_RIGHT:
			term.index = utf8_Next(term.sections[term.numSections - 1],
					term.lenSection, term.index);
			term.vct = term.index;
			break;
		case SDLK_HOME:
			term.index = 0;
			term.vct = term.index;
			break;
		case SDLK_END:
			term.index = term.lenSection;
			term.vct = term.index;
			break;
		case SDLK_UP: again_up:
			if (term.histIndex == 0) {
				break;
			}
			term.histIndex--;
			if (term.sections[term.histIndex] == NULL) {
				goto again_up;
			}
			term_SetSection(&term, term.sections[term.histIndex],
					strlen(term.sections[term.histIndex]));
			break;
		case SDLK_DOWN: again_down:
			if (term.histIndex + 2 == term.numSections) {
				term.histIndex++;
				term_SetSection(&term, "", 0);
				break;
			}
			if (term.histIndex + 1 == term.numSections) {
				term_SetSection(&term, "", 0);
				break;
			}
			term.histIndex++;
			if (term.sections[term.histIndex] == NULL) {
				goto again_down;
			}
			term_SetSection(&term, term.sections[term.histIndex],
					strlen(term.sections[term.histIndex]));
			break;

		case SDLK_BACKSPACE:
			index = utf8_Prev(term.sections[term.numSections - 1],
					term.lenSection, term.index);
			if (index != term.index) {
				term_Delete(&term, index, term.index);
				term.index = index;
			}
			break;
		case SDLK_DELETE:
			index = utf8_Next(term.sections[term.numSections - 1],
					term.lenSection, term.index);
			if (index != term.index) {
				term_Delete(&term, term.index, index);
			}
			break;
		case SDLK_RETURN:
			term_NextSection(&term);
			break;
		}
		break;

	case EVENT_TEXTINPUT:
		term_Append(&term, info->ti.text, strlen(info->ti.text));
		break;
	default:
	}
	return 0;
}

