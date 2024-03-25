#include "gui.h"

static void *event_constructor(Property *args, Uint32 numArgs);
static void *point_constructor(Property *args, Uint32 numArgs);
static void *rect_constructor(Property *args, Uint32 numArgs);
static void *view_constructor(Property *args, Uint32 numArgs);

struct object_class object_classes[] = {
	{ "Event", event_constructor, sizeof(Event) },
	{ "Point", point_constructor, sizeof(Point) },
	{ "Rect", rect_constructor, sizeof(Rect) },
	{ "View", view_constructor, sizeof(View) }
};

static struct environment {
	Label *label; /* first label in the linked list */
	Label *cur; /* selected label */
	View *view; /* current view */
	Property *stack;
	Uint32 numStack;
	struct memo {
		bool inUse;
		Uint32 size;
		void *sys;
	} *objects;
	Uint32 numObjects;
} environment;

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Property *result);
int environment_ExecuteFunction(Function *func,
		Instruction *args, Uint32 numArgs, Property *result);
static int EvaluateInstruction(Instruction *instr, Property *prop);
static int ExecuteInstructions(Instruction *instrs,
		Uint32 num, Property *prop);

static char *WordTerminate(struct value_string *s)
{
	static char word[MAX_WORD];

	if (s->length >= MAX_WORD) {
		return NULL;
	}
	memcpy(word, s->data, s->length);
	word[s->length] = '\0';
	return word;
}

struct object_class *environment_FindClass(const char *name)
{
	for (Uint32 i = 0; i < ARRLEN(object_classes); i++) {
		if (strcmp(object_classes[i].name, name) == 0) {
			return &object_classes[i];
		}
	}
	return NULL;
}

static struct memo *_AllocObject(Uint32 size)
{
	struct memo o, *newObjects, *po;
	void *data;

	newObjects = union_Realloc(union_Default(), environment.objects,
			sizeof(*newObjects) * (environment.numObjects + 1));
	if (newObjects == NULL) {
		return NULL;
	}
	environment.objects = newObjects;

	data = union_Alloc(union_Default(), size);
	if (data == NULL) {
		return NULL;
	}
	o.inUse = false;
	o.size = size;
	o.sys = data;
	po = &environment.objects[environment.numObjects];
	*po = o;
	return po;
}

static void *AllocObject(struct object_class *class)
{
	struct memo *const o = _AllocObject(class->size);
	if (o == NULL) {
		return NULL;
	}
	return o->sys;
}

static struct memo *LocateObject(void *sys)
{
	for (Uint32 i = 0; i < environment.numObjects; i++) {
		if (environment.objects[i].sys == sys) {
			return &environment.objects[i];
		}
	}
	return NULL;
}

static void *DuplicateObject(void *sys)
{
	struct memo *dup;

	for (Uint32 i = 0; i < environment.numObjects; i++) {
		if (environment.objects[i].sys == sys) {
			dup = _AllocObject(environment.objects[i].size);
			if (dup == NULL) {
				return NULL;
			}
			memcpy(dup->sys, sys, dup->size);
			return dup->sys;
		}
	}
	return NULL;
}

static int CastProperty(const Property *in, type_t type, Property *out)
{
	if (in->type == type) {
		*out = *in;
		return 0;
	}
	switch (type) {
	case TYPE_INTEGER:
		if (in->type == TYPE_COLOR) {
			out->value.i = in->value.color;
		} else if (in->type == TYPE_FLOAT) {
			out->value.i = in->value.f;
		} else {
			return -1;
		}
		break;
	case TYPE_FLOAT:
		if (in->type == TYPE_COLOR) {
			out->value.f = in->value.color;
		} else if (in->type == TYPE_INTEGER) {
			out->value.f = in->value.i;
		} else {
			return -1;
		}
		break;
	case TYPE_COLOR:
		if (in->type == TYPE_FLOAT) {
			out->value.color = in->value.f;
		} else if (in->type == TYPE_INTEGER) {
			out->value.color = in->value.i;
		} else {
			return -1;
		}
		break;
	default:
		return -1;
	}
	out->type = type;
	return 0;
}

static Property *_SearchVariable(View *view, const char *name, Value **pValue)
{
	Label *l;

	if (view == NULL) {
		l = environment.cur;
	} else {
		l = view->label;
	}
	for (Uint32 i = 0; i < l->numProperties; i++) {
		if (strcmp(l->properties[i].name, name) == 0) {
			if (pValue != NULL) {
				if (view == NULL) {
					*pValue = NULL;
				} else {
					*pValue = &view->values[i];
				}
			}
			return &l->properties[i];
		}
	}
	return NULL;
}

static Property *SearchVariable(const char *name, Value **pValue)
{
	for (Uint32 i = environment.numStack; i > 0; ) {
		i--;
		if (strcmp(environment.stack[i].name, name) == 0) {
			if (pValue != NULL) {
				*pValue = &environment.stack[i].value;
			}
			return &environment.stack[i];
		}
	}
	return _SearchVariable(environment.view, name, pValue);
}

static int StandardProc(View *view, event_t event, EventInfo *info)
{
	View *prev;
	Value *value;
	Property prop;
	Event e;
	Instruction i;

	(void) info;
	prev = environment.view;
	environment.view = view;
	switch (event) {
	case EVENT_CREATE:
		value = view_GetProperty(view, TYPE_FUNCTION, "init");
		if (value == NULL) {
			break;
		}
		environment_ExecuteFunction(value->func, NULL, 0, &prop);
		break;
	case EVENT_PAINT:
		value = view_GetProperty(view, TYPE_FUNCTION, "draw");
		if (value == NULL) {
			break;
		}
		environment_ExecuteFunction(value->func, NULL, 0, &prop);
		break;
	default:
		value = view_GetProperty(view, TYPE_FUNCTION, "event");
		if (value == NULL) {
			break;
		}
		e.type = event;
		e.info = *info;
		i.instr = INSTR_VALUE;
		i.type = TYPE_OBJECT;
		i.value.value.object.class = environment_FindClass("Event");
		i.value.value.object.data = &e;
		environment_ExecuteFunction(value->func, &i, 1, &prop);
		break;
	}
	environment.view = prev;
	return 0;
}

static void *CallConstructor(struct object_class *class, Instruction *instrs,
		Uint32 numInstrs)
{
	Property args[numInstrs];
	for (Uint32 i = 0; i < numInstrs; i++) {
		if (EvaluateInstruction(&instrs[i], &args[i]) < 0) {
			return NULL;
		}
	}
	return class->constructor(args, numInstrs);
}

static bool IsPropertyObject(const Property *prop, const char *class)
{
	if (prop->type != TYPE_OBJECT) {
		return false;
	}
	return prop->value.object.class == environment_FindClass(class);
}

static int args_GetRect(Property *args, Uint32 numArgs, Rect *r)
{
	Property prop;

	if (numArgs == 4) {
		Sint32 nums[4];

		for (Uint32 i = 0; i < numArgs; i++) {
			if (CastProperty(&args[i], TYPE_INTEGER, &prop) < 0) {
				return -1;
			}
			nums[i] = prop.value.i;
		}
		*r = (Rect) { nums[0], nums[1], nums[2], nums[3] };
	} else if (numArgs == 1) {
		if (!IsPropertyObject(&args[0], "Rect")) {
			return -1;
		}
		*r = *(Rect*) args[0].value.object.data;
	} else {
		return -1;
	}
	return 0;
}


static void *event_constructor(Property *args, Uint32 numArgs)
{
	/* events shall not be constructed */
	(void) args;
	(void) numArgs;
	return NULL;
}

static void *point_constructor(Property *args, Uint32 numArgs)
{
	struct memo *o;
	Point *p;

	o = _AllocObject(sizeof(*p));
	if (o == NULL) {
		return NULL;
	}
	p = o->sys;
	if (numArgs == 0) {
		p->x = 0;
		p->y = 0;
	} else if (numArgs == 2) {
		Sint32 nums[2];
		Property prop;

		for (Uint32 i = 0; i < numArgs; i++) {
			if (CastProperty(&args[i], TYPE_INTEGER, &prop) < 0) {
				return NULL;
			}
			nums[i] = prop.value.i;
		}
		p->x = nums[0];
		p->y = nums[1];
	} else {
		return NULL;
	}
	return p;
}

static void *rect_constructor(Property *args, Uint32 numArgs)
{
	struct memo *o;
	Rect *r;

	o = _AllocObject(sizeof(*r));
	if (o == NULL) {
		return NULL;
	}
	r = o->sys;
	if (numArgs == 0) {
		r->x = 0;
		r->y = 0;
		r->w = 0;
		r->h = 0;
	} else if (numArgs == 4) {
		Sint32 nums[4];
		Property prop;

		for (Uint32 i = 0; i < numArgs; i++) {
			if (CastProperty(&args[i], TYPE_INTEGER, &prop) < 0) {
				return NULL;
			}
			nums[i] = prop.value.i;
		}
		r->x = nums[0];
		r->y = nums[1];
		r->w = nums[2];
		r->h = nums[3];
	} else {
		return NULL;
	}
	return r;
}

static void *view_constructor(Property *args, Uint32 numArgs)
{
	View *v;
	Rect r;
	char *class;

	if (numArgs == 0 || args[0].type != TYPE_STRING) {
		return NULL;
	}
	class = WordTerminate(&args[0].value.s);
	if (class == NULL) {
		return NULL;
	}
	if (numArgs > 1) {
		if (args_GetRect(&args[1], numArgs - 1, &r) < 0) {
			return NULL;
		}
	} else {
		r.x = 0;
		r.y = 0;
		r.w = 0;
		r.h = 0;
	}
	v = view_Create(class, &r);
	if (v == NULL) {
		return NULL;
	}
	return v;
}

int environment_ExecuteFunction(Function *func,
		Instruction *args, Uint32 numArgs, Property *result)
{
	Property *newStack;
	Property prop;
	int r;

	if (func->numParams != numArgs) {
		return -1;
	}

	if (numArgs > 0) {
		newStack = union_Realloc(union_Default(), environment.stack,
				sizeof(*environment.stack) *
				(environment.numStack + numArgs));
		if (newStack == NULL) {
			return -1;
		}
		environment.stack = newStack;
	}

	/* it can happen that the stack grows with local variables,
	 * so we just save this so we can reset the stack to
	 * delete all local variables at once
	 */
	const Uint32 oldNumStack = environment.numStack;

	for (Uint32 i = 0; i < numArgs; i++) {
		Property *const s = &environment.stack[environment.numStack];
		if (EvaluateInstruction(&args[i], s) < 0) {
			return -1;
		}
		if (func->params[i].type != s->type) {
			return -1;
		}
		if (func->params[i].type == TYPE_OBJECT &&
				func->params[i].class !=
				s->value.object.class) {
			return -1;
		}
		strcpy(s->name, func->params[i].name);
		environment.numStack++;
	}
	r = ExecuteInstructions(func->instructions, func->numInstructions,
			&prop);
	if (r < 0) {
		return -1;
	}
	if (r == 1) {
		*result = prop;
	}
	environment.numStack = oldNumStack;
	return 0;
}

static int EvaluateInstruction(Instruction *instr, Property *prop)
{
	Property *var;
	Value *value;
	void *data;

	switch (instr->instr) {
	case INSTR_EVENT:
	case INSTR_GROUP:
	case INSTR_IF:
	case INSTR_LOCAL:
	case INSTR_RETURN:
	case INSTR_SET:
	case INSTR_TRIGGER:
		return -1;
	case INSTR_INVOKE:
		var = SearchVariable(instr->invoke.name, NULL);
		if (var == NULL || var->type != TYPE_FUNCTION) {
			if (ExecuteSystem(instr->invoke.name,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
				return -1;
			}
		} else if (environment_ExecuteFunction(var->value.func,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
			return -1;
		}
		break;
	case INSTR_NEW:
		data = CallConstructor(instr->new.class, instr->new.args,
				instr->new.numArgs);
		if (data == NULL) {
			return -1;
		}
		prop->type = TYPE_OBJECT;
		prop->value.object.class = instr->new.class;
		prop->value.object.data = data;
		break;
	case INSTR_THIS:
		prop->type = TYPE_OBJECT;
		prop->value.object.class = environment_FindClass("View");
		prop->value.object.data = environment.view;
		break;
	case INSTR_VALUE:
		prop->type = instr->type;
		prop->value = instr->value.value;
		break;
	case INSTR_VARIABLE:
		var = SearchVariable(instr->variable.name, &value);
		if (var == NULL) {
			return -1;
		}
		prop->type = var->type;
		prop->value = *value;
		break;
	}
	return 0;
}

static int ExecuteInstruction(Instruction *instr, Property *prop)
{
	Property *var;
	Value *pValue;
	struct memo *o;
	bool b;
	Property *newStack;
	Property out;

	switch (instr->instr) {
	case INSTR_VALUE:
	case INSTR_VARIABLE:
	case INSTR_NEW:
	case INSTR_THIS:
		break;
	case INSTR_EVENT:
		printf("event: %u\n", instr->event.event);
		break;
	case INSTR_GROUP:
		return ExecuteInstructions(instr->group.instructions,
				instr->group.numInstructions, prop);
	case INSTR_IF:
		if (EvaluateInstruction(instr->iff.condition, prop) < 0) {
			return -1;
		}
		if (prop->type == TYPE_INTEGER) {
			b = !!prop->value.i;
		} else if (prop->type == TYPE_BOOL) {
			b = prop->value.b;
		} else {
			return -1;
		}
		if (b) {
			return ExecuteInstruction(instr->iff.iff, prop);
		} else if (instr->iff.els != NULL) {
			return ExecuteInstruction(instr->iff.els, prop);
		}
		break;
	case INSTR_LOCAL:
		if (EvaluateInstruction(instr->local.value, prop) < 0) {
			return -1;
		}
		newStack = union_Realloc(union_Default(), environment.stack,
				sizeof(*environment.stack) * (environment.numStack + 1));
		if (newStack == NULL) {
			return -1;
		}
		environment.stack = newStack;
		strcpy(prop->name, instr->local.name);
		environment.stack[environment.numStack++] = *prop;
		break;
	case INSTR_RETURN:
		if (EvaluateInstruction(instr->ret.value, prop) < 0) {
			return -1;
		}
		return 1;

	case INSTR_SET:
		var = SearchVariable(instr->set.variable, &pValue);
		if (var == NULL || var->type == TYPE_FUNCTION) {
			return -1;
		}
		if (EvaluateInstruction(instr->set.value, prop) < 0) {
			return -1;
		}
		if (CastProperty(prop, var->type, &out) < 0) {
			return -1;
		}
		if (out.type == TYPE_OBJECT) {
			if (var->value.object.data != NULL) {
				o = LocateObject(var->value.object.data);
				if (o == NULL) {
					return -1;
				}
				o->inUse = false;
			}
			if (out.value.object.data != NULL) {
				o = LocateObject(out.value.object.data);
				if (o == NULL) {
					return -1;
				}
				if (o->inUse) {
					out.value.object.data =
						DuplicateObject(o);
					if (out.value.object.data == NULL) {
						return -1;
					}
				}
			}
		}
		if (pValue != NULL) {
			*pValue = out.value;
		} else {
			var->value = out.value;
		}
		break;
	case INSTR_TRIGGER:
		/* TODO: make trigger system */
		/* triggers are just system functions but defined
		 * by the user in C and installed using
		 * trigger_Install(name, triggerFunc) */
		printf("triggering: %s\n", instr->trigger.name);
		break;
	case INSTR_INVOKE:
		var = SearchVariable(instr->invoke.name, NULL);
		if (var == NULL || var->type != TYPE_FUNCTION) {
			if (ExecuteSystem(instr->invoke.name,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
				return -1;
			}
		} else if (environment_ExecuteFunction(var->value.func,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
			return -1;
		}
		break;
	}
	return 0;
}

static int ExecuteInstructions(Instruction *instrs,
		Uint32 num, Property *prop)
{
	for (Uint32 i = 0; i < num; i++) {
		const int r = ExecuteInstruction(&instrs[i], prop);
		if (r != 0) {
			return r;
		}
	}
	return 0;
}

static int SystemAnd(Property *args, Uint32 numArgs, Property *result)
{
	result->type = TYPE_BOOL;
	result->value.b = true;
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type != TYPE_BOOL &&
				args[i].type != TYPE_INTEGER) {
			return -1;
		}
		if (args[i].type == TYPE_BOOL && !args[i].value.b) {
			result->value.b = false;
			break;
		}
		if (args[i].type == TYPE_INTEGER && args[i].value.i == 0) {
			result->value.b = false;
			break;
		}
	}
	return 0;
}

static int SystemOr(Property *args, Uint32 numArgs, Property *result)
{
	result->type = TYPE_BOOL;
	result->value.b = false;
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type != TYPE_BOOL &&
				args[i].type != TYPE_INTEGER) {
			return -1;
		}
		if (args[i].type == TYPE_BOOL && args[i].value.b) {
			result->value.b = true;
			break;
		}
		if (args[i].type == TYPE_INTEGER && args[i].value.i != 0) {
			result->value.b = true;
			break;
		}
	}
	return 0;
}

static int SystemNot(Property *args, Uint32 numArgs, Property *result)
{
	if (numArgs != 1) {
		return -1;
	}
	result->type = TYPE_BOOL;
	if (args[0].type == TYPE_BOOL) {
		result->value.b = !args[0].value.b;
	} else if (args[0].type == TYPE_INTEGER) {
		result->value.b = args[0].value.i == 0;
	} else {
		return -1;
	}
	return 0;
}

static int SystemEquals(Property *args, Uint32 numArgs, Property *result)
{
	result->type = TYPE_BOOL;
	result->value.b = true;
	for (Uint32 i = 0; i < numArgs; i++) {
		for (Uint32 j = i + 1; j < numArgs; j++) {
			if (args[i].type != args[j].type) {
				result->value.b = false;
				return 0;
			}
			switch (args[i].type) {
			case TYPE_NULL:
				return -1;
			case TYPE_BOOL:
				if (args[i].value.b != args[j].value.b) {
					result->value.b = false;
					return 0;
				}
				break;
			case TYPE_INTEGER:
				if (args[i].value.i != args[j].value.i) {
					result->value.b = false;
					return 0;
				}
				break;
			case TYPE_COLOR:
				if (args[i].value.color != args[j].value.color) {
					result->value.b = false;
					return 0;
				}
				break;
			case TYPE_FLOAT:
				if (args[i].value.f != args[j].value.f) {
					result->value.b = false;
					return 0;
				}
				break;
			case TYPE_FONT:
				if (args[i].value.font != args[j].value.font) {
					result->value.b = false;
					return 0;
				}
				break;
			case TYPE_OBJECT:
				break;
			case TYPE_FUNCTION:
				if (args[i].value.func != args[j].value.func) {
					result->value.b = false;
					return 0;
				}
				break;
			case TYPE_STRING:
				if (args[i].value.s.length !=
						args[j].value.s.length ||
						memcmp(args[i].value.s.data,
						args[j].value.s.data,
						args[i].value.s.length) != 0) {
					result->value.b = false;
					return 0;
				}
				break;
			}
		}
	}
	return 0;
}

static int SystemPrint(Property *args, Uint32 numArgs, Property *result)
{
	FILE *const fp = stdout;

	for (Uint32 i = 0; i < numArgs; i++) {
		Property *const prop = &args[i];
		switch (prop->type) {
		case TYPE_NULL:
			/* should not happen */
			break;
		case TYPE_BOOL:
			fprintf(fp, "%s", prop->value.b ?
					"true" : "false");
			break;
		case TYPE_COLOR:
			fprintf(fp, "%#x", prop->value.color);
			break;
		case TYPE_FLOAT:
			fprintf(fp, "%f", prop->value.f);
			break;
		case TYPE_FONT:
			fprintf(fp, "%p", prop->value.font);
			break;
		case TYPE_FUNCTION:
			fprintf(fp, "%p", prop->value.func);
			break;
		case TYPE_INTEGER:
			fprintf(fp, "%ld", prop->value.i);
			break;
		case TYPE_OBJECT:
			fprintf(fp, "%s %p",
				prop->value.object.class->name,
				prop->value.object.data);
			break;
		case TYPE_STRING:
			fprintf(fp, "%.*s", prop->value.s.length, prop->value.s.data);
			break;
		}
	}
	(void) result;
	return 0;
}

static int SystemSum(Property *args, Uint32 numArgs, Property *result)
{
	result->type = TYPE_INTEGER;
	memset(&result->value, 0, sizeof(result->value));
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type == TYPE_FLOAT) {
			result->type = TYPE_FLOAT;
			break;
		}
	}
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type == TYPE_INTEGER) {
			if (result->type == TYPE_FLOAT) {
				result->value.f += args[i].value.i;
			} else {
				result->value.i += args[i].value.i;
			}
		} else if (args[i].type == TYPE_FLOAT) {
			if (result->type == TYPE_FLOAT) {
				result->value.f += args[i].value.f;
			} else {
				result->value.i += args[i].value.f;
			}
		} else {
			return -1;
		}
	}
	return 0;
}

static int SystemRand(Property *args, Uint32 numArgs, Property *result)
{
	Property prop;
	Sint64 nums[2];
	Sint64 r;

	if (numArgs != 2) {
		return -1;
	}
	for (Uint32 i = 0; i < numArgs; i++) {
		if (CastProperty(&args[i], TYPE_INTEGER, &prop) < 0) {
			return -1;
		}
		nums[i] = prop.value.i;
	}
	if (nums[0] > nums[1]) {
		return -1;
	}
	r = rand();
	r *= nums[1] - nums[0];
	r /= RAND_MAX;
	r += nums[0];
	result->type = TYPE_INTEGER;
	result->value.i = r;
	return 0;
}

static int SystemContains(Property *args, Uint32 numArgs, Property *result)
{
	if (numArgs != 2) {
		return -1;
	}
	if (!IsPropertyObject(&args[0], "Rect") ||
			!IsPropertyObject(&args[1], "Point")) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->value.b = rect_Contains(args[0].value.object.data,
			args[1].value.object.data);
	return 0;
}

static int SystemGetClass(Property *args, Uint32 numArgs, Property *result)
{
	if (numArgs != 1 || args[0].type != TYPE_OBJECT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->value.i = (Sint64) args[0].value.object.class;
	return 0;
}

static int SystemGetObject(Property *args, Uint32 numArgs, Property *result)
{
	if (numArgs != 1 || args[0].type != TYPE_STRING) {
		return -1;
	}
	result->type = TYPE_OBJECT;
	result->value.object.class = environment_FindClass("View");
	/* TODO: add id system */
	result->value.object.data = NULL;
	return 0;
}

static int SystemGetParent(Property *args, Uint32 numArgs, Property *result)
{
	View *v;

	if (numArgs != 1 || !IsPropertyObject(&args[0], "View")) {
		return -1;
	}
	v = args[0].value.object.data;
	result->type = TYPE_OBJECT;
	result->value.object.class = environment_FindClass("View");
	result->value.object.data = v->parent;
	return 0;
}

static int SystemSetDrawColor(Property *args, Uint32 numArgs, Property *result)
{
	Property prop;

	if (numArgs != 1) {
		return -1;
	}

	if (CastProperty(&args[0], TYPE_COLOR, &prop) < 0) {
		return -1;
	}

	renderer_SetDrawColor(renderer_Default(), prop.value.color);
	(void) result;
	return 0;
}

static int SystemSetParent(Property *args, Uint32 numArgs, Property *result)
{
	View *p, *v;

	if (numArgs != 2 || !IsPropertyObject(&args[0], "View") ||
			!IsPropertyObject(&args[1], "View")) {
		return -1;
	}
	v = args[0].value.object.data;
	p = args[1].value.object.data;
	view_SetParent(v, p);
	(void) result;
	return 0;
}

static int SystemGetProperty(Property *args, Uint32 numArgs, Property *result)
{
	View *v;
	char *s;
	Property *prop;
	Value *pValue;

	if (numArgs != 2 || !IsPropertyObject(&args[0], "View") ||
			args[1].type != TYPE_STRING) {
		return -1;
	}
	v = args[0].value.object.data;
	s = WordTerminate(&args[1].value.s);
	if (s == NULL) {
		return -1;
	}
	prop = _SearchVariable(v, s, &pValue);
	if (prop == NULL) {
		return -1;
	}
	result->type = prop->type;
	result->value = *pValue;
	return 0;
}

static int SystemSetProperty(Property *args, Uint32 numArgs, Property *result)
{
	View *v;
	char *s;
	Value *pValue;
	Property *prop;
	Property out;

	if (numArgs != 3 || !IsPropertyObject(&args[0], "View") ||
			args[1].type != TYPE_STRING) {
		return -1;
	}
	v = args[0].value.object.data;
	s = WordTerminate(&args[1].value.s);
	if (s == NULL) {
		return -1;
	}
	prop = _SearchVariable(v, s, &pValue);
	if (prop == NULL) {
		return -1;
	}
	if (CastProperty(&args[2], prop->type, &out) < 0) {
		return -1;
	}
	*pValue = out.value;
	(void) result;
	return 0;
}

static int SystemFillRect(Property *args, Uint32 numArgs, Property *result)
{
	Rect r;

	if (args_GetRect(args, numArgs, &r) < 0) {
		return -1;
	}
	renderer_FillRect(renderer_Default(), &r);
	(void) result;
	return 0;
}

static int SystemGetRect(Property *args, Uint32 numArgs, Property *result)
{
	if (numArgs != 1) {
		return -1;
	}

	if (!IsPropertyObject(&args[0], "View")) {
		return -1;
	}

	View *const view = args[0].value.object.data;
	result->type = TYPE_OBJECT;
	result->value.object.class = environment_FindClass("Rect");
	result->value.object.data = AllocObject(result->value.object.class);
	if (result->value.object.data == NULL) {
		return -1;
	}
	memcpy(result->value.object.data, &view->rect, sizeof(view->rect));
	return 0;
}


static int SystemSetRect(Property *args, Uint32 numArgs, Property *result)
{
	Rect r;

	if (numArgs == 0) {
		return -1;
	}

	if (!IsPropertyObject(&args[0], "View")) {
		return -1;
	}

	View *const view = args[0].value.object.data;

	if (args_GetRect(&args[1], numArgs - 1, &r) < 0) {
		return -1;
	}
	view->rect = r;
	(void) result;
	return 0;
}

static int SystemGetWindowWidth(Property *args, Uint32 numArgs, Property *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->value.i = gui_GetWindowWidth();
	return 0;
}

static int SystemGetWindowHeight(Property *args, Uint32 numArgs, Property *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->value.i = gui_GetWindowHeight();
	return 0;
}

static int SystemGetType(Property *args, Uint32 numArgs, Property *result)
{
	Event *e;

	if (numArgs == 0 || !IsPropertyObject(&args[0], "Event")) {
		return -1;
	}
	e = args[0].value.object.data;
	result->type = TYPE_INTEGER;
	result->value.i = e->type;
	return 0;
}

static int SystemGetPos(Property *args, Uint32 numArgs, Property *result)
{
	Event *e;
	Point *p;

	if (numArgs == 0 || !IsPropertyObject(&args[0], "Event")) {
		return -1;
	}
	e = args[0].value.object.data;
	result->type = TYPE_OBJECT;
	result->value.object.class = environment_FindClass("Point");
	p = AllocObject(result->value.object.class);
	if (p == NULL) {
		return -1;
	}
	p->x = e->info.mi.x;
	p->y = e->info.mi.y;
	result->value.object.data = p;
	return 0;
}

static int SystemGetButton(Property *args, Uint32 numArgs, Property *result)
{
	Event *e;

	if (numArgs == 0 || !IsPropertyObject(&args[0], "Event")) {
		return -1;
	}
	e = args[0].value.object.data;
	result->type = TYPE_INTEGER;
	result->value.i = e->info.mi.button;
	return 0;
}

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Property *result)
{
	static const struct system_function {
		const char *name;
		int (*call)(Property *args, Uint32 numArgs, Property *result);
	} functions[] = {
		/* TODO: add more system functions */
		{ "and", SystemAnd },
		{ "equals", SystemEquals },
		{ "not", SystemNot },
		{ "or", SystemOr },
		{ "print", SystemPrint },
		{ "rand", SystemRand },
		{ "sum", SystemSum },

		{ "Contains", SystemContains },
		{ "FillRect", SystemFillRect },
		{ "GetButton", SystemGetButton },
		{ "GetClass", SystemGetClass },
		{ "GetObject", SystemGetObject },
		{ "GetParent", SystemGetParent },
		{ "GetPos", SystemGetPos },
		{ "GetProperty", SystemGetProperty },
		{ "GetRect", SystemGetRect },
		{ "GetType", SystemGetType },
		{ "GetWindowHeight", SystemGetWindowHeight },
		{ "GetWindowWidth", SystemGetWindowWidth },
		{ "SetDrawColor", SystemSetDrawColor },
		{ "SetParent", SystemSetParent },
		{ "SetProperty", SystemSetProperty },
		{ "SetRect", SystemSetRect },
	};

	const struct system_function *sys = NULL;

	for (Uint32 i = 0; i < (Uint32) ARRLEN(functions); i++) {
		if (strcmp(functions[i].name, call) == 0) {
			sys = &functions[i];
			break;
		}
	}
	if (sys == NULL) {
		return -1;
	}

	Property props[numArgs];
	for (Uint32 i = 0; i < numArgs; i++) {
		if (EvaluateInstruction(&args[i], &props[i]) < 0) {
			return -1;
		}
	}
	return sys->call(props, numArgs, result);
}

static int MergeWithLabel(const RawWrapper *wrapper)
{
	Property *newProperties;
	RawProperty *raw;
	Property prop;

	Label *const label = environment.label;
	const Uint32 num = label->numProperties;
	newProperties = union_Realloc(union_Default(), label->properties,
			sizeof(*label->properties) *
			(label->numProperties + wrapper->numProperties));
	if (newProperties == NULL) {
		return -1;
	}
	label->properties = newProperties;

	for (Uint32 i = 0, j; i < wrapper->numProperties; i++) {
		raw = &wrapper->properties[i];
		if (EvaluateInstruction(&raw->instruction, &prop) < 0) {
			return -1;
		}

		for (j = 0; j < num; j++) {
			Property *const labelProp = &label->properties[j];
			if (strcmp(raw->name, labelProp->name) == 0) {
				if (prop.type != labelProp->type) {
					return -1;
				}
			}
			labelProp->value = prop.value;
			break;
		}
		if (j == num) {
			strcpy(prop.name, raw->name);
			label->properties[label->numProperties++] = prop;
		}
	}
	return 0;
}

Label *environment_FindLabel(const char *name)
{
	Label *label;

	for (label = environment.label; label != NULL; label = label->next) {
		if (strcmp(label->name, name) == 0) {
			return label;
		}
	}
	return NULL;
}

Label *environment_AddLabel(const char *name)
{
	Label *label;
	Label *last;

	label = union_Alloc(union_Default(), sizeof(*label));
	if (label == NULL) {
		return NULL;
	}
	memset(label, 0, sizeof(*label));
	strcpy(label->name, name);
	last = environment.label;
	if (last == NULL) {
		environment.label = label;
		environment.cur = label;
	} else {
		for (last = environment.cur; last->next != NULL; ) {
			last = last->next;
		}
	}
	label->proc = StandardProc;
	return label;
}

int environment_Digest(RawWrapper *wrappers, Uint32 numWrappers)
{
	for (Uint32 i = 0; i < numWrappers; i++) {
		Label *label;

		label = environment_FindLabel(wrappers[i].label);
		if (label == NULL) {
			label = environment_AddLabel(wrappers[i].label);
			if (label == NULL) {
				return -1;
			}
		}
		environment.cur = label;
		if (MergeWithLabel(&wrappers[i]) < 0) {
			return -1;
		}
	}
	return 0;
}
