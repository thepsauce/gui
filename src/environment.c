#include "gui.h"

struct object_function rect_class_functions[] = {
};

struct object_class rect_class = {
	"Rect",
	sizeof(Sint32) * 4,
	NULL
};

struct object_class view_class = {
	"View",
	sizeof(struct view),
	&rect_class
};

static struct environment {
	Union *uni;
	struct object_class *class;
	Label *label; /* first label in the linked list */
	Label *cur; /* selected label */
	View *view; /* current view */
	Property *stack;
	Uint32 numStack;
} environment = {
	.class = &view_class
};

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Property *result);
int environment_ExecuteFunction(Function *func,
		Instruction *args, Uint32 numArgs, Property *result);
static int EvaluateInstruction(Instruction *instr, Property *prop);
static int ExecuteInstructions(Instruction *instrs,
		Uint32 num, Property *prop);

static struct object_class *FindClass(const char *name)
{
	struct object_class *class;

	for (class = environment.class; class != NULL; class = class->next) {
		if (strcmp(class->name, name) == 0) {
			return class;
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
		} else if (in->type != TYPE_FLOAT) {
			out->value.i = in->value.f;
		} else {
			return -1;
		}
		break;
	case TYPE_FLOAT:
		if (in->type == TYPE_COLOR) {
			out->value.f = in->value.color;
		} else if (in->type != TYPE_INTEGER) {
			out->value.f = in->value.i;
		} else {
			return -1;
		}
		break;
	case TYPE_COLOR:
		if (in->type == TYPE_FLOAT) {
			out->value.color = in->value.f;
		} else if (in->type != TYPE_INTEGER) {
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

static Property *SearchVariable(const char *name)
{
	for (Uint32 i = environment.numStack; i > 0; ) {
		i--;
		if (strcmp(environment.stack[i].name, name) == 0) {
			return &environment.stack[i];
		}
	}

	for (Uint32 i = 0; i < environment.cur->numProperties; i++) {
		if (strcmp(environment.cur->properties[i].name, name) == 0) {
			return &environment.cur->properties[i];
		}
	}
	return NULL;
}

static int StandardProc(View *view, event_t event, EventInfo *info)
{
	Property *prop;

	(void) info;
	switch (event) {
	case EVENT_CREATE:
		environment.cur = view->label;
		environment.view = view;
		prop = SearchVariable("init");
		if (prop == NULL || prop->type != TYPE_FUNCTION) {
			break;
		}
		environment_ExecuteFunction(prop->value.func, NULL, 0, prop);
		break;
	case EVENT_PAINT:
		environment.cur = view->label;
		environment.view = view;
		prop = SearchVariable("draw");
		if (prop == NULL || prop->type != TYPE_FUNCTION) {
			break;
		}
		environment_ExecuteFunction(prop->value.func, NULL, 0, prop);
		break;
	default:
	}
	return 0;
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
		if (EvaluateInstruction(&args[i],
				&environment.stack[environment.numStack]) < 0) {
			return -1;
		}
		if (func->params[i].type !=
				environment.stack[environment.numStack].type) {
			return -1;
		}
		strcpy(environment.stack[environment.numStack].name,
				func->params[i].name);
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
	struct object_class *class;
	void *data;

	switch (instr->instr) {
	case INSTR_EVENT:
	case INSTR_IF:
	case INSTR_LOCAL:
	case INSTR_RETURN:
	case INSTR_SET:
	case INSTR_TRIGGER:
		return -1;
	case INSTR_INVOKE:
		var = SearchVariable(instr->invoke.name);
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
		class = FindClass(instr->new.class);
		data = union_Alloc(union_Default(), class->size);
		if (data == NULL) {
			return -1;
		}
		memset(data, 0, class->size);
		prop->type = TYPE_OBJECT;
		prop->value.object.class = class;
		prop->value.object.data = data;
		break;
	case INSTR_THIS:
		prop->type = TYPE_OBJECT;
		prop->value.object.class = FindClass("View");
		prop->value.object.data = environment.view;
		break;
	case INSTR_VALUE:
		prop->type = instr->type;
		prop->value = instr->value.value;
		break;
	case INSTR_VARIABLE:
		var = SearchVariable(instr->variable.name);
		if (var == NULL) {
			return -1;
		}
		*prop = *var;
		break;
	}
	return 0;
}

static int ExecuteInstruction(Instruction *instr, Property *prop)
{
	Property *var;
	bool b;
	Property *newStack;

	switch (instr->instr) {
	case INSTR_VALUE:
	case INSTR_VARIABLE:
	case INSTR_NEW:
	case INSTR_THIS:
		break;
	case INSTR_EVENT:
		printf("event: %u\n", instr->event.event);
		break;
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
			return ExecuteInstructions(instr->iff.instructions,
					instr->iff.numInstructions, prop);
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
		var = SearchVariable(instr->set.variable);
		if (var == NULL || var->type == TYPE_FUNCTION) {
			return -1;
		}
		if (EvaluateInstruction(instr->set.value, prop) < 0) {
			return -1;
		}
		if (prop->type != var->type) {
			return -1;
		}
		var->value = prop->value;
		break;
	case INSTR_TRIGGER:
		printf("triggering: %s\n", instr->trigger.name);
		break;
	case INSTR_INVOKE:
		var = SearchVariable(instr->invoke.name);
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
			if (prop->value.object.class == NULL) {
				fprintf(fp, "null");
			} else {
				fprintf(fp, "%s %p",
					prop->value.object.class->name,
					prop->value.object.data);
			}
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

static bool IsPropertyObject(const Property *prop, const char *class)
{
	if (prop->type != TYPE_OBJECT) {
		return false;
	}
	return strcmp(prop->value.object.class->name, class) == 0;
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
	result->value.object.class = FindClass("Rect");
	result->value.object.data = &view->rect;
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

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Property *result)
{
	static const struct system_function {
		const char *name;
		int (*call)(Property *args, Uint32 numArgs, Property *result);
	} functions[] = {
		/* TODO: add more system functions */
		{ "equals", SystemEquals },
		{ "print", SystemPrint },
		{ "sum", SystemSum },
		{ "SetDrawColor", SystemSetDrawColor },
		{ "FillRect", SystemFillRect },
		{ "GetRect", SystemGetRect },
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
