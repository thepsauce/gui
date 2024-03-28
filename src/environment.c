#include "gui.h"

Label global_label;

static struct environment {
	Label *label; /* first label in the linked list */
	Label *cur; /* selected label */
	View *view; /* current view */
	Property *stack;
	Uint32 numStack;
} environment = {
	.label = &global_label
};

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Value *result);
int function_Execute(Function *func,
		Instruction *args, Uint32 numArgs, Value *result);
static int EvaluateInstruction(Instruction *instr, Value *value);
static int ExecuteInstructions(Instruction *instrs,
		Uint32 num, Value *value);

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

int value_Cast(const Value *in, type_t type, Value *out)
{
	if (in->type == type) {
		*out = *in;
		return 0;
	}
	switch (type) {
	case TYPE_INTEGER:
		if (in->type == TYPE_COLOR) {
			out->i = in->c;
		} else if (in->type == TYPE_FLOAT) {
			out->i = in->f;
		} else {
			return -1;
		}
		break;
	case TYPE_FLOAT:
		if (in->type == TYPE_COLOR) {
			out->f = in->c;
		} else if (in->type == TYPE_INTEGER) {
			out->f = in->i;
		} else {
			return -1;
		}
		break;
	case TYPE_COLOR:
		if (in->type == TYPE_FLOAT) {
			out->c = in->f;
		} else if (in->type == TYPE_INTEGER) {
			out->c = in->i;
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
	Property *prop;

	for (Uint32 i = environment.numStack; i > 0; ) {
		i--;
		prop = &environment.stack[i];
		if (strcmp(prop->name, name) == 0) {
			if (pValue != NULL) {
				*pValue = &environment.stack[i].value;
			}
			return prop;
		}
	}

	prop = _SearchVariable(environment.view, name, pValue);
	if (prop != NULL) {
		return prop;
	}

	for (Uint32 i = 0; i < global_label.numProperties; i++) {
		prop = &global_label.properties[i];
		if (strcmp(prop->name, name) == 0) {
			if (pValue != NULL) {
				*pValue = &prop->value;
			}
			return prop;
		}
	}
	return NULL;
}

static int StandardProc(View *view, event_t event, EventInfo *info)
{
	View *prev;
	Value *value;
	Value v;
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
		function_Execute(value->func, NULL, 0, &v);
		break;
	case EVENT_PAINT:
		value = view_GetProperty(view, TYPE_FUNCTION, "draw");
		if (value == NULL) {
			break;
		}
		function_Execute(value->func, NULL, 0, &v);
		break;
	default:
		value = view_GetProperty(view, TYPE_FUNCTION, "event");
		if (value == NULL) {
			break;
		}
		i.instr = INSTR_VALUE;
		v.type = TYPE_EVENT;
		v.e.event = event;
		v.e.info = *info;
		i.value.value = v;
		function_Execute(value->func, &i, 1, &v);
		break;
	}
	environment.view = prev;
	return 0;
}

int function_Execute(Function *func, Instruction *args, Uint32 numArgs,
		Value *result)
{
	Property *newStack;
	Value value;
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
		if (EvaluateInstruction(&args[i], &s->value) < 0) {
			return -1;
		}
		if (func->params[i].type != s->value.type) {
			return -1;
		}
		strcpy(s->name, func->params[i].name);
		environment.numStack++;
	}
	r = ExecuteInstructions(func->instructions, func->numInstructions,
			&value);
	if (r < 0) {
		return -1;
	}
	if (r == 1) {
		*result = value;
	}
	environment.numStack = oldNumStack;
	return 0;
}

static int EvaluateInstruction(Instruction *instr, Value *result)
{
	Property *var;
	Value *value;

	switch (instr->instr) {
	case INSTR_BREAK:
	case INSTR_FOR:
	case INSTR_FORIN:
	case INSTR_GROUP:
	case INSTR_IF:
	case INSTR_LOCAL:
	case INSTR_RETURN:
	case INSTR_SET:
	case INSTR_TRIGGER:
	case INSTR_WHILE:
		return -1;
	case INSTR_INVOKE:
		var = SearchVariable(instr->invoke.name, NULL);
		if (var == NULL || var->value.type != TYPE_FUNCTION) {
			if (ExecuteSystem(instr->invoke.name,
					instr->invoke.args,
					instr->invoke.numArgs, result) < 0) {
				return -1;
			}
		} else if (function_Execute(var->value.func,
					instr->invoke.args,
					instr->invoke.numArgs, result) < 0) {
			return -1;
		}
		break;
	case INSTR_THIS:
		result->type = TYPE_VIEW;
		result->v = environment.view;
		break;
	case INSTR_VALUE:
		*result = instr->value.value;
		break;
	case INSTR_VARIABLE:
		var = SearchVariable(instr->variable.name, &value);
		if (var == NULL) {
			return -1;
		}
		*result = *value;
		break;
	}
	return 0;
}

static int ExecuteInstruction(Instruction *instr, Value *result)
{
	Property *var;
	Value *pValue;
	bool b;
	Property *newStack;
	Value out;
	Value from, to, in;
	Uint32 index;

	switch (instr->instr) {
	case INSTR_VALUE:
	case INSTR_VARIABLE:
	case INSTR_THIS:
		break;
	case INSTR_BREAK:
		return 2;
	case INSTR_FOR:
		if (instr->forr.from == NULL) {
			from.i = 0;
		} else if (EvaluateInstruction(instr->forr.from, &from) < 0) {
			return -1;
		}
		if (EvaluateInstruction(instr->forr.to, &to) < 0) {
			return -1;
		}
		newStack = union_Realloc(union_Default(), environment.stack,
				sizeof(*newStack) *
				(environment.numStack + 1));
		if (newStack == NULL) {
			return -1;
		}
		environment.stack = newStack;
		index = environment.numStack++;

		strcpy(environment.stack[index].name, instr->forr.variable);
		environment.stack[index].value.type = TYPE_INTEGER;
		for (Sint64 i = from.i; i < to.i; i++) {
			environment.stack[index].value.i = i;
			const int r = ExecuteInstruction(instr->forr.iter,
					result);
			if (r == 2) {
				environment.numStack = index;
				return 0;
			}
			if (r != 0) {
				return r;
			}
		}
		environment.numStack = index;
		break;
	case INSTR_FORIN:
		if (EvaluateInstruction(instr->forin.in, &in)) {
			return -1;
		}
		newStack = union_Realloc(union_Default(), environment.stack,
				sizeof(*newStack) *
				(environment.numStack + 1));
		if (newStack == NULL) {
			return -1;
		}
		environment.stack = newStack;
		index = environment.numStack++;

		strcpy(environment.stack[index].name, instr->forin.variable);
		if (in.type == TYPE_ARRAY) {
			for (Sint64 i = 0; i < in.a->numValues; i++) {
				environment.stack[index].value = in.a->values[i];
				const int r = ExecuteInstruction(instr->forin.iter,
						result);
				if (r == 2) {
					environment.numStack = index;
					return 0;
				}
				if (r != 0) {
					return r;
				}
			}
		} else if (in.type == TYPE_STRING) {
			environment.stack[index].value.type = TYPE_INTEGER;
			for (Sint64 i = 0; i < in.s->length; i++) {
				environment.stack[index].value.i = in.s->data[i];
				const int r = ExecuteInstruction(instr->forin.iter,
						result);
				if (r == 2) {
					environment.numStack = index;
					return 0;
				}
				if (r != 0) {
					return r;
				}
			}
		} else {
			return -1;
		}
		environment.numStack = index;
		break;
	case INSTR_GROUP: {
		index = environment.numStack;
		const int r = ExecuteInstructions(instr->group.instructions,
				instr->group.numInstructions, result);
		environment.numStack = index;
		return r;
	  }

	case INSTR_IF:
		if (EvaluateInstruction(instr->iff.condition, result) < 0) {
			return -1;
		}
		if (result->type == TYPE_INTEGER) {
			b = !!result->i;
		} else if (result->type == TYPE_BOOL) {
			b = result->b;
		} else {
			return -1;
		}
		if (b) {
			return ExecuteInstruction(instr->iff.iff, result);
		} else if (instr->iff.els != NULL) {
			return ExecuteInstruction(instr->iff.els, result);
		}
		break;

	case INSTR_LOCAL:
		if (EvaluateInstruction(instr->local.value, result) < 0) {
			return -1;
		}
		newStack = union_Realloc(union_Default(), environment.stack,
				sizeof(*environment.stack) * (environment.numStack + 1));
		if (newStack == NULL) {
			return -1;
		}
		environment.stack = newStack;
		strcpy(environment.stack[environment.numStack].name,
				instr->local.name);
		environment.stack[environment.numStack++].value = *result;
		break;
	case INSTR_RETURN:
		if (EvaluateInstruction(instr->ret.value, result) < 0) {
			return -1;
		}
		return 1;

	case INSTR_SET:
		var = SearchVariable(instr->set.variable, &pValue);
		if (var == NULL || var->value.type == TYPE_FUNCTION) {
			return -1;
		}
		if (EvaluateInstruction(instr->set.value, result) < 0) {
			return -1;
		}
		if (value_Cast(result, var->value.type, &out) < 0) {
			return -1;
		}
		if (pValue != NULL) {
			*pValue = out;
		} else {
			var->value = out;
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
		if (var == NULL || var->value.type != TYPE_FUNCTION) {
			if (ExecuteSystem(instr->invoke.name,
					instr->invoke.args,
					instr->invoke.numArgs, result) < 0) {
				return -1;
			}
		} else if (function_Execute(var->value.func,
					instr->invoke.args,
					instr->invoke.numArgs, result) < 0) {
			return -1;
		}
		break;
	case INSTR_WHILE:
		b = true;
		while (b) {
			if (EvaluateInstruction(instr->whilee.condition,
						result) < 0) {
				return -1;
			}
			if (result->type == TYPE_INTEGER) {
				b = !!result->i;
			} else if (result->type == TYPE_BOOL) {
				b = result->b;
			} else {
				return -1;
			}
			if (!b) {
				break;
			}
			const int r = ExecuteInstruction(instr->whilee.iter, result);
			if (r != 0) {
				return r;
			}
		}
		break;
	}
	return 0;
}

static int ExecuteInstructions(Instruction *instrs,
		Uint32 num, Value *result)
{
	for (Uint32 i = 0; i < num; i++) {
		const int r = ExecuteInstruction(&instrs[i], result);
		if (r != 0) {
			return r;
		}
	}
	return 0;
}

static int SystemAnd(Value *args, Uint32 numArgs, Value *result)
{
	result->type = TYPE_BOOL;
	result->b = true;
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type != TYPE_BOOL &&
				args[i].type != TYPE_INTEGER) {
			return -1;
		}
		if (args[i].type == TYPE_BOOL && !args[i].b) {
			result->b = false;
			break;
		}
		if (args[i].type == TYPE_INTEGER && args[i].i == 0) {
			result->b = false;
			break;
		}
	}
	return 0;
}

static int SystemOr(Value *args, Uint32 numArgs, Value *result)
{
	result->type = TYPE_BOOL;
	result->b = false;
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type != TYPE_BOOL &&
				args[i].type != TYPE_INTEGER) {
			return -1;
		}
		if (args[i].type == TYPE_BOOL && args[i].b) {
			result->b = true;
			break;
		}
		if (args[i].type == TYPE_INTEGER && args[i].i != 0) {
			result->b = true;
			break;
		}
	}
	return 0;
}

static int SystemNot(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1) {
		return -1;
	}
	result->type = TYPE_BOOL;
	if (args[0].type == TYPE_BOOL) {
		result->b = !args[0].b;
	} else if (args[0].type == TYPE_INTEGER) {
		result->b = args[0].i == 0;
	} else {
		return -1;
	}
	return 0;
}

static bool Equals(Value *v1, Value *v2)
{
	switch (v1->type) {
	case TYPE_NULL:
		return false;
	case TYPE_ARRAY:
		if (v1->a->numValues != v2->a->numValues) {
			return false;
		}
		for (Uint32 i = 0; i < v1->a->numValues; i++) {
			if (!Equals(&v1->a->values[i], &v2->a->values[i])) {
				return false;
			}
		}
		break;
	case TYPE_BOOL:
		if (v1->b != v2->b) {
			return false;
		}
		break;
	case TYPE_EVENT:
		/* TODO:? who cares about comparing events? */
		return false;
	case TYPE_INTEGER:
		if (v1->i != v2->i) {
			return false;
		}
		break;
	case TYPE_COLOR:
		if (v1->c != v2->c) {
			return false;
		}
		break;
	case TYPE_FLOAT:
		if (v1->f != v2->f) {
			return false;
		}
		break;
	case TYPE_FONT:
		if (v1->font != v2->font) {
			return false;
		}
		break;
	case TYPE_FUNCTION:
		if (v1->func != v2->func) {
			return false;
		}
		break;
	case TYPE_POINT:
		if (v1->p.x != v2->p.x || v1->p.y != v2->p.y) {
			return false;
		}
		break;
	case TYPE_RECT:
		if (v1->r.x != v2->r.x || v1->r.y != v2->r.y ||
				v1->r.w != v2->r.w || v1->r.h != v2->r.h) {
			return false;
		}
		break;
	case TYPE_STRING:
		if (v1->s->length != v2->s->length ||
				memcmp(v1->s->data, v2->s->data, v1->s->length)
				!= 0) {
			return false;
		}
		break;
	case TYPE_VIEW:
		if (v1->v != v2->v) {
			return false;
		}
		break;
	}
	return true;
}

static int SystemEquals(Value *args, Uint32 numArgs, Value *result)
{
	result->type = TYPE_BOOL;
	result->b = true;
	for (Uint32 i = 0; i < numArgs; i++) {
		for (Uint32 j = i + 1; j < numArgs; j++) {
			if (args[i].type != args[j].type) {
				result->b = false;
				return 0;
			}
			result->b = Equals(&args[i], &args[j]);
			if (!result->b) {
				return 0;
			}
		}
	}
	return 0;
}

static int SystemGet(Value *args, Uint32 numArgs, Value *result)
{
	Value val;

	if (numArgs != 2) {
		return -1;
	}
	if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0 || val.i < 0) {
		return -1;
	}
	if (args[0].type == TYPE_ARRAY) {
		if (val.i >= args[0].a->numValues) {
			return -1;
		}
		*result = args[0].a->values[val.i];
	} else if (args[0].type == TYPE_STRING) {
		if (val.i >= args[0].s->length) {
			return -1;
		}
		result->type = TYPE_INTEGER;
		result->i = args[0].s->data[val.i];
	} else {
		return -1;
	}
	return 0;
}

static int SystemInsert(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs < 2) {
		return -1;
	}
	if (args[0].type == TYPE_ARRAY) {
		struct value_array *arr;
		Value *newValues;

		arr = args[0].a;
		newValues = union_Realloc(union_Default(), arr->values,
				sizeof(*arr->values) *
				(arr->numValues + numArgs - 1));
		if (newValues == NULL) {
			return -1;
		}
		arr->values = newValues;
		memcpy(&arr->values[arr->numValues], &args[1],
				sizeof(*arr->values) * (numArgs - 1));
		arr->numValues += numArgs - 1;
	} else if (args[0].type == TYPE_STRING) {
		struct value_string *str;
		Uint32 count, index;
		char *newData;

		count = 0;
		for (Uint32 i = 1; i < numArgs; i++) {
			if (args[i].type == TYPE_STRING) {
				count += args[i].s->length;
			} else if (args[i].type == TYPE_INTEGER) {
				Uint64 u;

				u = args[i].i;
				do {
					count++;
					u >>= 8;
				} while (u > 0);
			} else {
				return -1;
			}
		}


		str = args[0].s;
		newData = union_Realloc(union_Default(), str->data,
				str->length + count);
		if (newData == NULL) {
			return -1;
		}
		str->data = newData;

		index = str->length;
		for (Uint32 i = 1; i < numArgs; i++) {
			if (args[i].type == TYPE_STRING) {
				memcpy(&str->data[index], args[i].s->data,
						args[i].s->length);
				index += args[i].s->length;
			} else if (args[i].type == TYPE_INTEGER) {
				Uint64 u;

				u = args[i].i;
				do {
					str->data[index++] = u & 0xff;
					u >>= 8;
				} while (u > 0);
			} else {
				return -1;
			}
		}
		str->length = index;
	} else {
		return -1;
	}
	(void) result;
	return 0;
}

static int SystemLength(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	if (args[0].type == TYPE_ARRAY) {
		result->i = args[0].a->numValues;
	} else if (args[0].type == TYPE_STRING) {
		result->i = args[0].s->length;
	} else {
		return -1;
	}
	return 0;
}

static void PrintValue(Value *value, FILE *fp)
{
	switch (value->type) {
	case TYPE_NULL:
		/* should not happen */
		break;
	case TYPE_ARRAY:
		fputc('[', fp);
		fputc(' ', fp);
		for (Uint32 i = 0; i < value->a->numValues; i++) {
			if (i > 0) {
				fputc(',', fp);
				fputc(' ', fp);
			}
			PrintValue(&value->a->values[i], fp);
		}
		fputc(']', fp);
		break;
	case TYPE_BOOL:
		fprintf(fp, "%s", value->b ?
				"true" : "false");
		break;
	case TYPE_COLOR:
		fprintf(fp, "%#x", value->c);
		break;
	case TYPE_EVENT:
		break;
	case TYPE_FLOAT:
		fprintf(fp, "%f", value->f);
		break;
	case TYPE_FONT:
		fprintf(fp, "%p", value->font);
		break;
	case TYPE_FUNCTION:
		fprintf(fp, "%p", value->func);
		break;
	case TYPE_INTEGER:
		fprintf(fp, "%ld", value->i);
		break;
	case TYPE_POINT:
		fprintf(fp, "%d, %d", value->p.x, value->p.y);
		break;
	case TYPE_RECT:
		fprintf(fp, "%d, %d, %d, %d",
				value->r.x, value->r.y, value->r.w, value->r.h);
		break;
	case TYPE_STRING:
		fprintf(fp, "%.*s", value->s->length, value->s->data);
		break;
	case TYPE_VIEW:
		fprintf(fp, "%p", value->v);
		break;
	}
}

static int SystemPrint(Value *args, Uint32 numArgs, Value *result)
{
	FILE *const fp = stdout;
	for (Uint32 i = 0; i < numArgs; i++) {
		PrintValue(&args[i], fp);
	}
	(void) result;
	return 0;
}

static int SystemSum(Value *args, Uint32 numArgs, Value *result)
{
	Value val;

	result->type = TYPE_INTEGER;
	result->f = 0;
	result->i = 0;
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type == TYPE_FLOAT) {
			result->type = TYPE_FLOAT;
			break;
		}
	}
	for (Uint32 i = 0; i < numArgs; i++) {
		if (value_Cast(&args[i], result->type, &val) < 0) {
			return -1;
		}
		if (result->type == TYPE_INTEGER) {
			result->i += val.i;
		} else {
			result->f += val.f;
		}
	}
	return 0;
}

static int SystemRand(Value *args, Uint32 numArgs, Value *result)
{
	Value val;
	Sint64 nums[2];
	Sint64 r;

	if (numArgs != 2) {
		return -1;
	}
	for (Uint32 i = 0; i < numArgs; i++) {
		if (value_Cast(&args[i], TYPE_INTEGER, &val) < 0) {
			return -1;
		}
		nums[i] = val.i;
	}
	if (nums[0] > nums[1]) {
		return -1;
	}
	r = rand();
	r *= nums[1] - nums[0];
	r /= RAND_MAX;
	r += nums[0];
	result->type = TYPE_INTEGER;
	result->i = r;
	return 0;
}

static int SystemContains(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2) {
		return -1;
	}
	if (args[0].type != TYPE_RECT || args[1].type != TYPE_POINT) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->b = rect_Contains(&args[0].r, &args[1].p);
	return 0;
}

static int args_GetRect(Value *args, Uint32 numArgs, Rect *r)
{
	Value v;

	if (numArgs == 4) {
		Sint32 nums[4];

		for (Uint32 i = 0; i < numArgs; i++) {
			if (value_Cast(&args[i], TYPE_INTEGER, &v) < 0) {
				return -1;
			}
			nums[i] = v.i;
		}
		*r = (Rect) { nums[0], nums[1], nums[2], nums[3] };
	} else if (numArgs == 1) {
		if (args[0].type != TYPE_RECT) {
			return -1;
		}
		*r = args[0].r;
	} else {
		return -1;
	}
	return 0;
}

static int SystemCreateView(Value *args, Uint32 numArgs, Value *result)
{
	Rect r;
	char *class;
	View *view;

	if (numArgs == 0 || args[0].type != TYPE_STRING) {
		return -1;
	}
	class = WordTerminate(args[0].s);
	if (class == NULL) {
		return -1;
	}
	if (numArgs > 1) {
		if (args_GetRect(&args[1], numArgs - 1, &r) < 0) {
			return -1;
		}
	} else {
		r.x = 0;
		r.y = 0;
		r.w = 0;
		r.h = 0;
	}
	view = view_Create(class, &r);
	if (view == NULL) {
		return -1;
	}
	result->type = TYPE_VIEW;
	result->v = view;
	return 0;
}

static int SystemGetParent(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_VIEW) {
		return -1;
	}
	result->type = TYPE_VIEW;
	result->v = args[0].v->parent;
	return 0;
}

static int SystemSetDrawColor(Value *args, Uint32 numArgs, Value *result)
{
	Value value;

	if (numArgs != 1) {
		return -1;
	}

	if (value_Cast(&args[0], TYPE_COLOR, &value) < 0) {
		return -1;
	}

	renderer_SetDrawColor(renderer_Default(), value.c);
	(void) result;
	return 0;
}

static int SystemSetParent(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2 || args[0].type != TYPE_VIEW ||
			args[1].type != TYPE_VIEW) {
		return -1;
	}
	view_SetParent(args[0].v, args[1].v);
	(void) result;
	return 0;
}

static int SystemGetProperty(Value *args, Uint32 numArgs, Value *result)
{
	View *view;
	char *str;
	Value *pValue;

	if (numArgs != 2 || args[0].type != TYPE_VIEW ||
			args[1].type != TYPE_STRING) {
		return -1;
	}
	view = args[0].v;
	str = WordTerminate(args[1].s);
	if (str == NULL) {
		return -1;
	}
	if (_SearchVariable(view, str, &pValue) == NULL) {
		return -1;
	}
	*result = *pValue;
	return 0;
}

static int SystemSetProperty(Value *args, Uint32 numArgs, Value *result)
{
	View *view;
	char *str;
	Value *pValue;
	Value out;

	if (numArgs != 3 || args[0].type != TYPE_VIEW ||
			args[1].type != TYPE_STRING) {
		return -1;
	}
	view = args[0].v;
	str = WordTerminate(args[1].s);
	if (str == NULL) {
		return -1;
	}
	if (_SearchVariable(view, str, &pValue) == NULL) {
		return -1;
	}
	if (value_Cast(&args[2], pValue->type, &out) < 0) {
		return -1;
	}
	*pValue = out;
	(void) result;
	return 0;
}

static int SystemFillRect(Value *args, Uint32 numArgs, Value *result)
{
	Rect r;

	if (args_GetRect(args, numArgs, &r) < 0) {
		return -1;
	}
	renderer_FillRect(renderer_Default(), &r);
	(void) result;
	return 0;
}

static int SystemGetRect(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_VIEW) {
		return -1;
	}
	result->type = TYPE_RECT;
	result->r = args[0].v->rect;
	return 0;
}


static int SystemSetRect(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs == 0 || args[0].type != TYPE_VIEW) {
		return -1;
	}
	if (args_GetRect(&args[1], numArgs - 1, &args[0].v->rect) < 0) {
		return -1;
	}
	(void) result;
	return 0;
}

static int SystemGetWindowWidth(Value *args, Uint32 numArgs, Value *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = gui_GetWindowWidth();
	return 0;
}

static int SystemGetWindowHeight(Value *args, Uint32 numArgs, Value *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = gui_GetWindowHeight();
	return 0;
}

static int SystemGetType(Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs == 0 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = args[0].e.event;
	return 0;
}

static int SystemGetPos(Value *args, Uint32 numArgs, Value *result)
{
	EventInfo *info;

	if (numArgs == 0 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	info = &args[0].e.info;
	result->type = TYPE_POINT;
	result->p.x = info->mi.x;
	result->p.y = info->mi.y;
	return 0;
}

static int SystemGetButton(Value *args, Uint32 numArgs, Value *result)
{

	if (numArgs == 0 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = args[0].e.info.mi.button;
	return 0;
}

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Value *result)
{
	static const struct system_function {
		const char *name;
		int (*call)(Value *args, Uint32 numArgs, Value *result);
	} functions[] = {
		/* TODO: add more system functions */
		{ "and", SystemAnd },
		{ "equals", SystemEquals },
		{ "get", SystemGet },
		{ "insert", SystemInsert },
		{ "length", SystemLength },
		{ "not", SystemNot },
		{ "or", SystemOr },
		{ "print", SystemPrint },
		{ "rand", SystemRand },
		{ "sum", SystemSum },

		{ "Contains", SystemContains },
		{ "CreateView", SystemCreateView },
		{ "FillRect", SystemFillRect },
		{ "GetButton", SystemGetButton },
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

	Value values[numArgs];
	for (Uint32 i = 0; i < numArgs; i++) {
		if (EvaluateInstruction(&args[i], &values[i]) < 0) {
			return -1;
		}
	}
	return sys->call(values, numArgs, result);
}

static int MergeWithLabel(const RawWrapper *wrapper)
{
	Property *newProperties;
	RawProperty *raw;
	Value val;

	Label *const label = environment.cur;
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
		if (EvaluateInstruction(&raw->instruction, &val) < 0) {
			return -1;
		}

		for (j = 0; j < num; j++) {
			Property *const labelProp = &label->properties[j];
			if (strcmp(raw->name, labelProp->name) == 0) {
				if (val.type != labelProp->value.type) {
					return -1;
				}
			}
			labelProp->value = val;
			break;
		}
		if (j == num) {
			Property prop;

			strcpy(prop.name, raw->name);
			prop.value = val;
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
	for (last = environment.label; last->next != NULL; ) {
		last = last->next;
	}
	last->next = label;
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
