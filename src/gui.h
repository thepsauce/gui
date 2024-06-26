#include <errno.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_WORD 256

typedef size_t Size;

#define ARRLEN(a) (sizeof(a)/sizeof*(a))

#define MAX(a, b) ({ \
	__auto_type _a = (a); \
	__auto_type _b = (b); \
	_a > _b ? _a : _b; \
})

#define MIN(a, b) ({ \
	__auto_type _a = (a); \
	__auto_type _b = (b); \
	_a < _b ? _a : _b; \
})

#define PRINT_DEBUG() fprintf(stderr, "error at %s:%d: ", __FILE__, __LINE__)

typedef SDL_Color Color;
typedef SDL_Texture Texture;
typedef TTF_Font Font;
typedef SDL_Renderer Renderer;
typedef SDL_Rect Rect;
typedef SDL_Point Point;

#define GUI_INIT_CLASSES 0x01

int gui_Init(Uint32 flags);
Sint32 gui_GetWindowWidth(void);
Sint32 gui_GetWindowHeight(void);
int gui_Run(void);

typedef struct {
	float alpha;
	float red;
	float green;
	float blue;
} rgb_t;

typedef struct {
	float alpha;
	float hue;
	float saturation;
	float value;
} hsv_t;

typedef struct {
	float alpha;
	float hue;
	float saturation;
	float lightness;
} hsl_t;

Uint32 RgbToInt(const rgb_t *rgb);
void IntToRgb(Uint32 color, rgb_t *rgb);
void RgbToHsl(const rgb_t *rgb, hsl_t *hsl);
void RgbToHsv(const rgb_t *rgb, hsv_t *hsv);
void HsvToRgb(const hsv_t *hsv, rgb_t *rgb);
void HsvToHsl(const hsv_t *hsv, hsl_t *hsl);
void HslToHsv(const hsl_t *hsl, hsv_t *hsv);
void HslToRgb(const hsl_t *hsl, rgb_t *rgb);

Renderer *renderer_Default(void);
int renderer_SetDrawColor(Uint32 color);
int renderer_SetDrawColorRGB(Uint8 a,
		Uint8 r, Uint8 g, Uint8 b);
int renderer_DrawRect(Rect *rect);
int renderer_FillRect(Rect *rect);
int renderer_DrawLine(Sint32 x1, Sint32 y1,
		Sint32 x2, Sint32 y2);
int renderer_DrawEllipse(Sint32 x, Sint32 y, Sint32 rx, Sint32 ry);
int renderer_FillEllipse(Sint32 x, Sint32 y, Sint32 rx, Sint32 ry);

struct font {
	Font *font;
	struct word {
		char *data;
		Sint32 width, height;
		SDL_Texture *texture;
	} *cachedWords;
	Uint32 numCachedWords;
};

Font *renderer_CreateFont(const char *name, int size, Uint32 *pIndex);
Font *renderer_GetFont(Uint32 index);
int renderer_SelectFont(Uint32 index);
int renderer_SetFont(Font *font);
void renderer_SetTabMultiplier(float multp);
int renderer_DrawText(const char *text, Uint32 length,
		Rect *rect);
int renderer_GetTextExtent(const char *text, Uint32 length,
		Rect *rect);
int renderer_LineSkip(void);

bool rect_IsEmpty(const Rect *rect);
bool rect_Intersect(const Rect *r1, const Rect *r2, Rect *rect);
int rect_Subtract(const Rect *r1, const Rect *r2, Rect *rects);
bool rect_Contains(const Rect *r, const Point *p);

typedef struct mem_union {
	struct mem_ptr {
		void *sys;
		Size size;
		Uint64 flags;
	} *pointers;
	Uint32 numPointers;
	Size limit;
	Size allocated;
} Union;

Union *union_Default(void);
void union_Init(Union *uni, Size limit);
void *union_Alloc(Union *uni, Size sz);
void *union_Allocf(Union *uni, Size sz, Uint64 flags);
void *union_Realloc(Union *uni, void *ptr, Size sz);
void *union_Mask(Union *uni, Uint64 flags, const void *ptr);
bool union_HasPointer(Union *uni, void *ptr);
void union_FreeAll(Union *uni);
int union_Free(Union *uni, void *ptr);
void union_Trim(Union *uni, Uint32 numPointers);

typedef struct region {
	Union *uni;
	Rect *rects;
	Uint32 numRects;
	Rect bounds;
} Region;

Region *region_Create(void);
Region *region_Create_u(Union *uni);
Region *region_SetEmpty(Region *region);
Region *region_Invert(Region *reg, Region *reg1);
Region *region_Rect(const Rect *rect);
Region *region_Rect_u(const Rect *rect, Union *uni);
Region *region_SetRect(Region *region, const Rect *rect);
Region *region_AddRect(Region *reg, const Rect *rect);
Region *region_Intersect(Region *reg, const Region *reg1, const Region *reg2);
Region *region_Add(Region *reg, const Region *reg1, const Region *reg2);
Region *region_Subtract(Region *reg, const Region *reg1, const Region *reg2);
Region *region_MoveBy(Region *reg, Sint32 dx, Sint32 dy);
void region_Delete(Region *reg);

typedef enum {
	EVENT_NULL,
	// system events
	CEXEC_DESTROY,
	// standard events
	EVENT_SERIALIZE,
	EVENT_DESERIALIZE,
	EVENT_ADDCHILD,
	EVENT_REMCHILD,
	EVENT_ID, // id changes
	EVENT_GETCLIP,
	EVENT_GETCONTAINERCLIP,
	EVENT_CREATE,
	EVENT_COMMAND,
	EVENT_DESTROY,
	EVENT_TIMER,
	EVENT_SETFOCUS,
	EVENT_KILLFOCUS,
	EVENT_PAINT,
	EVENT_SIZE,
	EVENT_KEYDOWN,
	EVENT_CHAR,
	EVENT_KEYUP,
	EVENT_BUTTONDOWN,
	EVENT_BUTTONUP,
	EVENT_MOUSEMOVE,
	EVENT_MOUSEMOVEOUTSIDE,
	EVENT_CAPTUREDMOVE,
	EVENT_SETCURSOR,
	EVENT_MOUSEWHEEL,
	EVENT_TEXTINPUT,
} event_t;

struct key_info {
	Uint8 state;
	Uint8 repeat;
	SDL_Keysym sym;
};

struct mouse_info {
	Uint8 button;
	Uint8 clicks;
	Sint32 x, y;
};

struct mouse_move_info {
	Uint32 state;
	Sint32 x, y;
	Sint32 dx, dy;
};

struct mouse_wheel_info {
	Sint32 x, y;
};

struct text_info {
	char text[32];
};

typedef union event_info {
	struct key_info ki;
	struct mouse_info mi;
	struct mouse_move_info mmi;
	struct mouse_wheel_info mwi;
	struct text_info ti;
} EventInfo;

typedef struct event {
	event_t type;
	EventInfo info;
} Event;

struct view;

typedef int (*EventProc)(struct view*, event_t, EventInfo*);

typedef enum type {
	TYPE_NULL = -1,
	TYPE_ARRAY = 0,
	TYPE_BOOL,
	TYPE_COLOR,
	TYPE_EVENT,
	TYPE_FLOAT,
	TYPE_FUNCTION,
	TYPE_INTEGER,
	TYPE_POINT,
	TYPE_RECT,
	TYPE_STRING,
	TYPE_SUCCESS,
	TYPE_VIEW
} type_t;

typedef enum {
	INSTR_BREAK,
	INSTR_FOR,
	INSTR_FORIN,
	INSTR_GROUP,
	INSTR_IF,
	INSTR_INVOKE,
	INSTR_INVOKESUB,
	/* note that INVOKE can also be a system invoke
	 * but INVOKESYS is explicit about it */
	INSTR_INVOKESYS,
	INSTR_LOCAL,
	INSTR_RETURN,
	INSTR_SET,
	INSTR_SUBVARIABLE,
	INSTR_SWITCH,
	INSTR_THIS,
	INSTR_TRIGGER,
	INSTR_VALUE,
	INSTR_VARIABLE,
	INSTR_WHILE,
} instr_t;

struct instruction;
struct property;

struct value_array {
	struct value *values;
	Uint32 numValues;
};

typedef struct parameter {
	type_t type;
	char name[MAX_WORD];
} Parameter;

typedef struct function {
	Parameter *params;
	Uint32 numParams;
	struct instruction *instructions;
	Uint32 numInstructions;
} Function;

struct value_event {
	event_t event;
	EventInfo info;
};

struct value_string {
	char *data;
	Uint32 length;
};

struct value_success {
	char *id;
	bool success;
	char *content;
};

typedef struct value {
	type_t type;
	union {
		struct value_array *a;
		bool b;
		rgb_t c;
		struct value_event e;
		float f;
		Function *func;
		Sint64 i;
		Point p;
		Rect r;
		struct value_string *s;
		struct value_success succ;
		struct view *v;
	};
} Value;

/* impl: src/environment.c */
int value_Cast(const Value *in, type_t type, Value *out);

struct instr_break {
	int nothing;
};

struct instr_for {
	char variable[MAX_WORD];
	/* from can be null, meaning the start is 0 */
	struct instruction *from;
	struct instruction *to;
	struct instruction *iter;
};

struct instr_forin {
	char variable[MAX_WORD];
	struct instruction *in;
	struct instruction *iter;
};

struct instr_getsub {
	char variable[MAX_WORD];
	char sub[MAX_WORD];
};

struct instr_group {
	struct instruction *instructions;
	Uint32 numInstructions;
};

struct instr_if {
	struct instruction *condition;
	struct instruction *iff;
	struct instruction *els;
};

struct instr_invoke {
	char name[MAX_WORD];
	struct instruction *args;
	Uint32 numArgs;
};

struct instr_invokesub {
	struct instruction *from;
	char sub[MAX_WORD];
	struct instruction *args;
	Uint32 numArgs;
};

struct instr_local {
	char name[MAX_WORD];
	struct instruction *value;
};

struct instr_return {
	struct instruction *value;
};

struct instr_set {
	struct instruction *dest;
	struct instruction *src;
};

struct instr_switch {
	struct instruction *value;
	struct instruction *instructions;
	Uint32 numInstructions;
	Uint32 *jumps;
	struct instruction *conditions;
	Uint32 numJumps;
};

struct instr_trigger {
	char name[MAX_WORD];
	struct instruction *args;
	Uint32 numArgs;
};

struct instr_value {
	Value value;
};

struct instr_variable {
	char name[MAX_WORD];
};

struct instr_subvariable {
	struct instruction *from;
	char name[MAX_WORD];
};

struct instr_while {
	struct instruction *condition;
	struct instruction *iter;
};

typedef struct instruction {
	instr_t instr;
	union {
		struct instr_break breakk;
		struct instr_for forr;
		struct instr_forin forin;
		struct instr_getsub getsub;
		struct instr_group group;
		struct instr_if iff;
		struct instr_invoke invoke;
		struct instr_invokesub invokesub;
		struct instr_local local;
		struct instr_return ret;
		struct instr_set set;
		struct instr_subvariable subvariable;
		struct instr_switch switchh;
		struct instr_trigger trigger;
		struct instr_value value;
		struct instr_variable variable;
		struct instr_while whilee;
	};
} Instruction;

struct trigger {
	char name[256];
	int (*trigger)(const Value *args, Uint32 numArgs, Value *result);
};

int trigger_Install(const struct trigger *trigger);
struct trigger *trigger_Get(const char *word);

typedef struct raw_property {
	char name[MAX_WORD];
	Instruction instruction;
} RawProperty;

typedef struct property_wrapper {
	char label[MAX_WORD];
	RawProperty *properties;
	Uint32 numProperties;
} RawWrapper;

typedef struct property {
	char name[MAX_WORD];
	Value value;
} Property;

typedef struct label {
	char name[MAX_WORD];
	Property *properties;
	Uint32 numProperties;
	EventProc proc;
	struct label *next;
} Label;

Uint32 utf8_Next(const char *str, Uint32 length, Uint32 index);
Uint32 utf8_Prev(const char *str, Uint32 length, Uint32 index);
int instruction_Execute(Instruction *instr, Value *value);
int prop_ParseString(const char *str, Union *uni, RawWrapper **pWrappers,
		Uint32 *pNumWrappers);
int prop_ParseFile(FILE *file, Union *uni, RawWrapper **pWrappers,
		Uint32 *pNumWrappers);
Instruction *parse_Expression(const char *str, Uint32 length);

int function_Execute(Function *func, Instruction *args, Uint32 numArgs,
		Value *result);
Label *environment_FindLabel(const char *name);
Label *environment_AddLabel(const char *name);
int environment_Digest(RawWrapper *wrappers, Uint32 numWrappers);

typedef struct view {
	Label *label;
	Union *uni;
	Uint64 flags;
	Rect rect;
	Region *region;
	Value *values;
	struct view *prev, *next;
	struct view *child, *parent;
} View;

View *view_Default(void);
View *view_Create(const char *labelName, const Rect *rect);
int view_SendRecursive(View *view, event_t type, EventInfo *info);
int view_Send(View *view, event_t type, EventInfo *info);
Value *view_GetProperty(View *view, type_t type, const char *name);
bool view_GetBoolProperty(View *view, const char *name);
int view_GetColorProperty(View *view, const char *name, rgb_t *rgb);
int view_SetParent(View *view, View *parent);
void view_Delete(View *view);
