#include "gui.h"

struct context {
	Union *uni;
	RawWrapper *wrappers;
	Label *labels;
	Uint32 num, cur;
	bool *evaluated;
};

static int EvaluateProperty(struct context *ctx, Uint32 index);

static Property *SearchVariable(struct context *ctx, const char *name)
{
	Uint32 index;
	Label *label;
	RawWrapper *wrapper;

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

static int ExecuteSystem(struct context *ctx, const char *call,
		Instruction *args, Uint32 numArgs, Property *result)
{
	(void) ctx;
	printf("Exec system: %s[%p (%u)]\n", call, args, numArgs);
	/* TODO: */
	result->type = TYPE_INTEGER;
	return 0;
}

static int ExecuteFunction(struct context *ctx, Function *func,
		Instruction *args, Uint32 numArgs, Property *result)
{
	(void) ctx;
	printf("Exec function: %p[%p (%u)]\n", func, args, numArgs);
	/* TODO: */
	result->type = TYPE_INTEGER;
	return 0;
}

static int EvaluateProperty(struct context *ctx, Uint32 index)
{
	RawWrapper *wrapper;
	RawProperty *raw;
	Label *label;
	Instruction *instr;
	Property *prop, *var;

	if (ctx->evaluated[index]) {
		return 1;
	}

	wrapper = &ctx->wrappers[ctx->cur];
	raw = &wrapper->properties[index];

	label = &ctx->labels[ctx->cur];

	prop = &label->properties[index];
	prop->type = TYPE_NULL;

	instr = &raw->instruction;
	switch (instr->instr) {
	case INSTR_EVENT:
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
