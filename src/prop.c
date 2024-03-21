#include "gui.h"

struct context {
	Union *uni;
	RawWrapper *wrappers;
	Label *labels;
	Uint32 num, cur;
	bool *evaluated;
	Property *stack;
	Uint32 numStack;
};

static int EvaluateProperty(struct context *ctx, Uint32 index);
static int ExecuteSystem(struct context *ctx, const char *call,
		Instruction *args, Uint32 numArgs, Property *result);
static int ExecuteFunction(struct context *ctx, Function *func,
		Instruction *args, Uint32 numArgs, Property *result);
static int ExecuteInstructions(struct context *ctx, Instruction *instrs,
		Uint32 num, Property *prop);

static Property *SearchVariable(struct context *ctx, const char *name)
{
	Uint32 index;
	Label *label;
	RawWrapper *wrapper;

	for (Uint32 i = ctx->numStack; i > 0; ) {
		i--;
		if (strcmp(ctx->stack[i].name, name) == 0) {
			return &ctx->stack[i];
		}
	}

	wrapper = &ctx->wrappers[ctx->cur];
	for (index = 0; index < wrapper->numProperties; index++) {
		if (strcmp(wrapper->properties[index].name, name) == 0) {
			break;
		}
	}

	if (index == wrapper->numProperties) {
		return NULL;
	}

	if (EvaluateProperty(ctx, index) < 0) {
		return NULL;
	}
	label = &ctx->labels[ctx->cur];
	return &label->properties[index];
}

static int EvaluateInstruction(struct context *ctx, Instruction *instr, Property *prop)
{
	Property *var;

	switch (instr->instr) {
	case INSTR_EVENT:
	case INSTR_IF:
	case INSTR_LOCAL:
	case INSTR_RETURN:
	case INSTR_SET:
	case INSTR_TRIGGER:
		return -1;
	case INSTR_INVOKE:
		var = SearchVariable(ctx, instr->invoke.name);
		if (var == NULL || var->type != TYPE_FUNCTION) {
			if (ExecuteSystem(ctx, instr->invoke.name,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
				return -1;
			}
		} else if (ExecuteFunction(ctx, var->value.func,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
			return -1;
		}
		break;
	case INSTR_NEW:
		/* TODO: */
		/* objects can be of type View or Rect or Point etc. */
		break;
	case INSTR_VALUE:
		prop->type = instr->type;
		prop->value = instr->value.value;
		break;
	case INSTR_VARIABLE:
		var = SearchVariable(ctx, instr->variable.name);
		if (var == NULL) {
			return -1;
		}
		*prop = *var;
		break;
	}
	return 0;
}

static int ExecuteInstruction(struct context *ctx, Instruction *instr, Property *prop)
{
	Property *var;
	bool b;
	Property *newStack;

	switch (instr->instr) {
	case INSTR_EVENT:
		printf("event: %u\n", instr->event.event);
		break;
	case INSTR_IF:
		if (EvaluateInstruction(ctx, instr->iff.condition, prop) < 0) {
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
			return ExecuteInstructions(ctx, instr->iff.instructions,
					instr->iff.numInstructions, prop);
		}
		break;
	case INSTR_LOCAL:
		if (EvaluateInstruction(ctx, instr->local.value, prop) < 0) {
			return -1;
		}
		newStack = union_Realloc(union_Default(), ctx->stack,
				sizeof(*ctx->stack) * (ctx->numStack + 1));
		if (newStack == NULL) {
			return -1;
		}
		ctx->stack = newStack;
		strcpy(prop->name, instr->local.name);
		ctx->stack[ctx->numStack++] = *prop;
		break;
	case INSTR_RETURN:
		if (EvaluateInstruction(ctx, instr->ret.value, prop) < 0) {
			return -1;
		}
		return 1;
	case INSTR_SET:
		var = SearchVariable(ctx, instr->set.variable);
		if (var == NULL || var->type == TYPE_FUNCTION) {
			return -1;
		}
		if (EvaluateInstruction(ctx, instr->set.value, prop) < 0) {
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
		var = SearchVariable(ctx, instr->invoke.name);
		if (var == NULL || var->type != TYPE_FUNCTION) {
			if (ExecuteSystem(ctx, instr->invoke.name,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
				return -1;
			}
		} else if (ExecuteFunction(ctx, var->value.func,
					instr->invoke.args,
					instr->invoke.numArgs, prop) < 0) {
			return -1;
		}
		break;
	case INSTR_VALUE:
	case INSTR_VARIABLE:
	case INSTR_NEW:
		break;
	}
	return 0;
}

static int ExecuteInstructions(struct context *ctx, Instruction *instrs,
		Uint32 num, Property *prop)
{
	for (Uint32 i = 0; i < num; i++) {
		const int r = ExecuteInstruction(ctx, &instrs[i], prop);
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

static int ExecuteSystem(struct context *ctx, const char *call,
		Instruction *args, Uint32 numArgs, Property *result)
{
	static const struct system_function {
		const char *name;
		int (*call)(Property *args, Uint32 numArgs, Property *result);
	} functions[] = {
		/* TODO: add more system functions */
		{ "equals", SystemEquals },
		{ "sum", SystemSum },
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
		if (EvaluateInstruction(ctx, &args[i], &props[i]) < 0) {
			return -1;
		}
	}
	return sys->call(props, numArgs, result);
}

static int ExecuteFunction(struct context *ctx, Function *func,
		Instruction *args, Uint32 numArgs, Property *result)
{
	Property *newStack;
	Property prop;
	int r;

	if (func->numParams != numArgs) {
		return -1;
	}

	newStack = union_Realloc(union_Default(), ctx->stack,
			sizeof(*ctx->stack) * (ctx->numStack + numArgs));
	if (newStack == NULL) {
		return -1;
	}
	ctx->stack = newStack;

	/* it can happen that the stack grows with local variables,
	 * so we just save this so we can reset the stack to
	 * delete all local variables at once
	 */
	const Uint32 oldNumStack = ctx->numStack;

	for (Uint32 i = 0; i < numArgs; i++) {
		if (EvaluateInstruction(ctx, &args[i],
					&ctx->stack[ctx->numStack]) < 0) {
			return -1;
		}
		if (func->params[i].type != ctx->stack[ctx->numStack].type) {
			return -1;
		}
		strcpy(ctx->stack[ctx->numStack].name, func->params[i].name);
		ctx->numStack++;
	}
	r = ExecuteInstructions(ctx, func->instructions, func->numInstructions,
			&prop);
	if (r < 0) {
		return -1;
	}
	if (r == 1) {
		*result = prop;
	}
	ctx->numStack = oldNumStack;
	return 0;
}

static int EvaluateProperty(struct context *ctx, Uint32 index)
{
	RawWrapper *wrapper;
	RawProperty *raw;
	Label *label;
	Property *prop;

	if (ctx->evaluated[index]) {
		return 1;
	}

	wrapper = &ctx->wrappers[ctx->cur];
	raw = &wrapper->properties[index];

	label = &ctx->labels[ctx->cur];

	prop = &label->properties[index];
	prop->type = TYPE_NULL;

	if (EvaluateInstruction(ctx, &raw->instruction, prop) < 0) {
		return -1;
	}
	if (prop->type == TYPE_NULL) {
		return -1;
	}
	strcpy(prop->name, raw->name);
	ctx->evaluated[index] = true;
	return 0;
}

static int EvaluateNext(struct context *ctx)
{
	Label *label;
	RawWrapper *wrapper;

	label = &ctx->labels[ctx->cur];
	wrapper = &ctx->wrappers[ctx->cur];
	label->properties = union_Alloc(ctx->uni, sizeof(*label->properties) *
			wrapper->numProperties);
	if (label->properties == NULL) {
		return -1;
	}
	ctx->evaluated = union_Alloc(ctx->uni, wrapper->numProperties);
	if (ctx->evaluated == NULL) {
		return -1;
	}
	memset(ctx->evaluated, 0, wrapper->numProperties);
	for (Uint32 i = 0; i < wrapper->numProperties; i++) {
		printf("property %u: %s\n", i, wrapper->properties[i].name);
		if (EvaluateProperty(ctx, i) < 0) {
			return -1;
		}
	}
	union_Free(ctx->uni, ctx->evaluated);
	strcpy(label->name, wrapper->label);
	label->numProperties = wrapper->numProperties;
	return 0;
}

int prop_Evaluate(Union *uni, RawWrapper *wrappers, Uint32 numWrappers,
		Label **pLabels, Uint32 *pNumLabels)
{
	struct context ctx;
	Label *labels;

	memset(&ctx, 0, sizeof(ctx));
	ctx.uni = uni;
	ctx.wrappers = wrappers;
	ctx.labels = union_Alloc(uni, sizeof(*labels) * numWrappers);
	ctx.num = numWrappers;
	if (ctx.labels == NULL) {
		return -1;
	}
	for (Uint32 i = 0; i < numWrappers; i++) {
		ctx.cur = i;
		if (EvaluateNext(&ctx) < 0) {
			return -1;
		}
	}
	*pLabels = ctx.labels;
	*pNumLabels = ctx.num;
	return 0;
}

static int MergeProperties(Class *class, const Label *label)
{
	Property *newProperties;
	const Uint32 num = class->numProperties;

	newProperties = realloc(class->properties, sizeof(*class->properties) *
			(class->numProperties + label->numProperties));
	if (newProperties == NULL) {
		return -1;
	}
	class->properties = newProperties;

	for (Uint32 i = 0, j; i < label->numProperties; i++) {
		const Property *const prop = &label->properties[i];

		for (j = 0; j < num; j++) {
			Property *const classProp = &class->properties[j];
			if (strcmp(prop->name, classProp->name) == 0) {
				if (prop->type != classProp->type) {
					return -1;
				}
			}
			classProp->value = prop->value;
			break;
		}
		if (j == num) {
			class->properties[class->numProperties++] = *prop;
		}
	}
	return 0;
}

int prop_Digest(const Label *label, Uint32 numWrappers)
{
	Class *c;

	for (Uint32 i = 0; i < numWrappers; i++) {
		c = class_Find(label[i].name);
		if (c == NULL) {
			return -1;
		}
		if (MergeProperties(c, &label[i]) < 0) {
			return -1;
		}
	}
	return 0;
}
