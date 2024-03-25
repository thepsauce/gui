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

typedef SDL_Renderer Renderer;
typedef SDL_Rect Rect;
typedef SDL_Point Point;

#define GUI_INIT_CLASSES 0x01

int gui_Init(Uint32 flags);
Sint32 gui_GetWindowWidth(void);
Sint32 gui_GetWindowHeight(void);
int gui_Run(void);

Renderer *renderer_Default(void);
int renderer_SetDrawColor(Renderer *renderer, Uint32 color);
int renderer_SetDrawColorRGB(Renderer *renderer, Uint8 r, Uint8 g, Uint8 b);
int renderer_FillRect(Renderer *renderer, Rect *rect);
int renderer_DrawLine(Renderer *renderer, Sint32 x1, Sint32 y1,
		Sint32 x2, Sint32 y2);
int renderer_DrawEllipse(Renderer *renderer, Sint32 x, Sint32 y, Sint32 rx, Sint32 ry);
int renderer_FillEllipse(Renderer *renderer, Sint32 x, Sint32 y, Sint32 rx, Sint32 ry);

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

typedef union event_info {
	struct key_info ki;
	struct mouse_info mi;
	struct mouse_move_info mmi;
	struct mouse_wheel_info mwi;
} EventInfo;

typedef struct event {
	event_t type;
	EventInfo info;
} Event;

struct view;

typedef int (*EventProc)(struct view*, event_t, EventInfo*);

typedef enum type {
	TYPE_NULL = -1,
	TYPE_BOOL = 0,
	TYPE_COLOR,
	TYPE_FLOAT,
	TYPE_FONT,
	TYPE_FUNCTION,
	TYPE_INTEGER,
	TYPE_OBJECT,
	TYPE_STRING,
} type_t;

typedef enum {
	INSTR_EVENT,
	INSTR_GROUP,
	INSTR_IF,
	INSTR_INVOKE,
	INSTR_LOCAL,
	INSTR_NEW,
	INSTR_RETURN,
	INSTR_SET,
	INSTR_THIS,
	INSTR_TRIGGER,
	INSTR_VALUE,
	INSTR_VARIABLE,
} instr_t;

struct instruction;
struct property;
struct object_class;

typedef struct parameter {
	type_t type;
	struct object_class *class;
	char name[MAX_WORD];
} Parameter;

typedef struct function {
	Parameter *params;
	Uint32 numParams;
	struct instruction *instructions;
	Uint32 numInstructions;
} Function;

struct object_class {
	char name[MAX_WORD];
	void *(*constructor)(struct property *args, Uint32 numArgs);
	Size size;
};

struct object_class *environment_FindClass(const char *name);

struct value_object {
	struct object_class *class;
	void *data;
};

struct value_string {
	char *data;
	Uint32 length;
};

typedef union value {
	bool b;
	Uint32 color;
	float f;
	void *font;
	Function *func;
	Sint64 i;
	struct value_object object;
	struct value_string s;
} Value;

struct instr_event {
	event_t event;
	EventInfo info;
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

struct instr_local {
	char name[MAX_WORD];
	struct instruction *value;
};

struct instr_new {
	struct object_class *class;
	struct instruction *args;
	Uint32 numArgs;
};

struct instr_return {
	struct instruction *value;
};

struct instr_set {
	char variable[MAX_WORD];
	struct instruction *value;
};

struct instr_trigger {
	char name[MAX_WORD];
};

struct instr_value {
	Value value;
};

struct instr_variable {
	char name[MAX_WORD];
};

typedef struct instruction {
	instr_t instr;
	type_t type;
	union {
		struct instr_event event;
		struct instr_group group;
		struct instr_if iff;
		struct instr_invoke invoke;
		struct instr_local local;
		struct instr_new new;
		struct instr_return ret;
		struct instr_set set;
		struct instr_trigger trigger;
		struct instr_value value;
		struct instr_variable variable;
	};
} Instruction;

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
	type_t type;
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

int prop_Parse(FILE *file, Union *uni, RawWrapper **pWrappers,
		Uint32 *pNumWrappers);

int environment_ExecuteFunction(Function *func, Instruction *args,
		Uint32 numArgs, Property *result);
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
Uint32 view_GetColorProperty(View *view, const char *name);
int view_SetParent(View *view, View *parent);
void view_Delete(View *view);
