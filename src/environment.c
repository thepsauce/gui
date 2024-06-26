#include "gui.h"

int BaseProc(View *view, event_t type, EventInfo *info);

Union environment_union = { .limit = SIZE_MAX };
Label global_label = { .proc = BaseProc };
View base_view = { .label = &global_label };

View *view_Default(void)
{
	return &base_view;
}

static struct environment {
	Union *uni;
	Label *label; /* first label in the linked list */
	Label *cur; /* selected label */
	View *view; /* current view */
	Property *stack;
	Uint32 numStack;
} environment = {
	.uni = &environment_union,
	.label = &global_label,
	.cur = &global_label,
};

struct trigger *installed_triggers;
Uint32 num_installed_triggers;

int trigger_Install(const struct trigger *trigger)
{
	struct trigger *newTriggers;

	/* let them seg fault if trigger is null */
	if (trigger->trigger == NULL) {
		return -1;
	}
	newTriggers = union_Realloc(union_Default(), installed_triggers,
			sizeof(*installed_triggers) * (num_installed_triggers + 1));
	if (newTriggers == NULL) {
		return -1;
	}
	installed_triggers = newTriggers;
	installed_triggers[num_installed_triggers++] = *trigger;
	return 0;
}

struct trigger *trigger_Get(const char *name)
{
	for (Uint32 i = 0; i < num_installed_triggers; i++) {
		if (strcmp(name, installed_triggers[i].name) == 0) {
			return &installed_triggers[i];
		}
	}
	return NULL;
}

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Value *result);
int function_Execute(Function *func,
		Instruction *args, Uint32 numArgs, Value *result);
static int EvaluateInstruction(Instruction *instr, Value *value);
static int ExecuteInstructions(Instruction *instrs,
		Uint32 num, Value *value);

/*static void environment_GC(void)
{
	* free unused pointers *
	Union *const uni = environment.uni;
	for (Uint32 i = 0; uni->numPointers; i++) {
		struct mem_ptr *const ptr = &uni->pointers[i];
		if ((type_t) ptr->flags == TYPE_NULL) {
			continue;
		}

	}
}*/

Uint32 utf8_Next(const char *str, Uint32 length, Uint32 index)
{
	Uint32 n;

	if (index != length) {
		if (!(str[index] & 0x80)) {
			return index + 1;
		}
		n = length - index;
		while (index++, (--n) != 0) {
			if ((str[index] & 0xc0) == 0x80) {
				break;
			}
		}
	}
	return index;
}

Uint32 utf8_Prev(const char *str, Uint32 length, Uint32 index)
{
	(void) length;
	while (index > 0 && (str[--index] & 0xc0) == 0x80) {
		if (index > 0 && !(str[index - 1] & 0x80)) {
			break;
		}
	}
	return index;
}

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
			out->i = RgbToInt(&in->c);
		} else if (in->type == TYPE_FLOAT) {
			out->i = in->f;
		} else {
			return -1;
		}
		break;
	case TYPE_FLOAT:
		if (in->type == TYPE_COLOR) {
			out->f = RgbToInt(&in->c);
		} else if (in->type == TYPE_INTEGER) {
			out->f = in->i;
		} else {
			return -1;
		}
		break;
	case TYPE_COLOR:
		if (in->type == TYPE_FLOAT) {
			IntToRgb(in->f, &out->c);
		} else if (in->type == TYPE_INTEGER) {
			IntToRgb(in->i, &out->c);
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

static int GetSubVariable(Value *value, const char *sub, Value *result)
{
	switch (value->type) {
	case TYPE_NULL:
	case TYPE_ARRAY:
	case TYPE_BOOL:
	case TYPE_FLOAT:
	case TYPE_FUNCTION:
	case TYPE_INTEGER:
	case TYPE_STRING:
	case TYPE_SUCCESS:
		return -1;

	case TYPE_COLOR: {
		rgb_t rgb;
		hsl_t hsl;
		hsv_t hsv;

		result->type = TYPE_FLOAT;
		rgb = value->c;
		if (strcmp(sub, "alpha") == 0) {
			result->f = rgb.alpha;
		} else if (strcmp(sub, "red") == 0) {
			result->f = rgb.red;
		} else if (strcmp(sub, "green") == 0) {
			result->f = rgb.green;
		} else if (strcmp(sub, "blue") == 0) {
			result->f = rgb.blue;
		} else if (strcmp(sub, "hue") == 0) {
			RgbToHsl(&rgb, &hsl);
			result->f = hsl.hue;
		} else if (strcmp(sub, "saturation") == 0) {
			RgbToHsl(&rgb, &hsl);
			result->f = hsl.saturation;
		} else if (strcmp(sub, "lightness") == 0) {
			RgbToHsl(&rgb, &hsl);
			result->f = hsl.lightness;
		} else if (strcmp(sub, "saturation2") == 0) {
			RgbToHsv(&rgb, &hsv);
			result->f = hsv.saturation;
		} else if (strcmp(sub, "value") == 0) {
			RgbToHsv(&rgb, &hsv);
			result->f = hsv.value;
		} else {
			return -1;
		}
		break;
	}

	case TYPE_EVENT:
		/* TODO: maybe? */
		return -1;

	case TYPE_POINT:
		result->type = TYPE_INTEGER;
		if (sub[0] == '\0' || sub[1] != '\0') {
			return -1;
		}
		switch (sub[0]) {
		case 'x':
			result->i = value->p.x;
			break;
		case 'y':
			result->i = value->p.y;
			break;
		default:
			return -1;
		}
		break;

	case TYPE_RECT:
		result->type = TYPE_INTEGER;
		if (sub[0] == '\0' || sub[1] != '\0') {
			return -1;
		}
		switch (sub[0]) {
		case 'x':
			result->i = value->r.x;
			break;
		case 'y':
			result->i = value->r.y;
			break;
		case 'w':
			result->i = value->r.w;
			break;
		case 'h':
			result->i = value->r.h;
			break;
		default:
			return -1;
		}
		break;

	case TYPE_VIEW:
		if (_SearchVariable(value->v, sub, &value) == NULL) {
			return -1;
		}
		*result = *value;
		break;
	}
	return 0;
}

static int SetSubVariable(Value *value, const char *sub, Value *result)
{
	Value actual;

	switch (value->type) {
	case TYPE_NULL:
	case TYPE_ARRAY:
	case TYPE_BOOL:
	case TYPE_FLOAT:
	case TYPE_FUNCTION:
	case TYPE_INTEGER:
	case TYPE_STRING:
	case TYPE_SUCCESS:
		return -1;
	case TYPE_EVENT:
		/* TODO: maybe? */
		return -1;

	case TYPE_COLOR: {
		hsl_t hsl;
		hsv_t hsv;

		if (value_Cast(result, TYPE_FLOAT, &actual) < 0) {
			return -1;
		}
		if (strcmp(sub, "alpha") == 0) {
			value->c.alpha = actual.f;
		} else if (strcmp(sub, "red") == 0) {
			value->c.red = actual.f;
		} else if (strcmp(sub, "green") == 0) {
			value->c.green = actual.f;
		} else if (strcmp(sub, "blue") == 0) {
			value->c.blue = actual.f;
		} else if (strcmp(sub, "hue") == 0) {
			RgbToHsl(&value->c, &hsl);
			hsl.hue = actual.f;
			HslToRgb(&hsl, &value->c);
		} else if (strcmp(sub, "saturation") == 0) {
			RgbToHsl(&value->c, &hsl);
			hsl.saturation = actual.f;
			HslToRgb(&hsl, &value->c);
		} else if (strcmp(sub, "lightness") == 0) {
			RgbToHsl(&value->c, &hsl);
			hsl.lightness = actual.f;
			HslToRgb(&hsl, &value->c);
		} else if (strcmp(sub, "saturation2") == 0) {
			RgbToHsv(&value->c, &hsv);
			hsv.saturation = actual.f;
			HsvToRgb(&hsv, &value->c);
		} else if (strcmp(sub, "value") == 0) {
			RgbToHsv(&value->c, &hsv);
			hsv.value = actual.f;
			HsvToRgb(&hsv, &value->c);
		} else {
			return -1;
		}
		break;
	}

	case TYPE_POINT:
		if (value_Cast(result, TYPE_INTEGER, &actual) < 0) {
			return -1;
		}
		if (sub[0] == '\0' || sub[1] != '\0') {
			return -1;
		}
		switch (sub[0]) {
		case 'x':
			value->p.x = actual.i;
			break;
		case 'y':
			value->p.y = actual.i;
			break;
		default:
			return -1;
		}
		break;

	case TYPE_RECT:
		if (value_Cast(result, TYPE_INTEGER, &actual) < 0) {
			return -1;
		}
		if (sub[0] == '\0' || sub[1] != '\0') {
			return -1;
		}
		switch (sub[0]) {
		case 'x':
			value->r.x = actual.i;
			break;
		case 'y':
			value->r.y = actual.i;
			break;
		case 'w':
			value->r.w = actual.i;
			break;
		case 'h':
			value->r.h = actual.i;
			break;
		default:
			return -1;
		}
		break;

	case TYPE_VIEW:
		if (_SearchVariable(value->v, sub, &value) == NULL) {
			return -1;
		}
		if (value_Cast(result, value->type, &actual) < 0) {
			return -1;
		}
		*value = actual;
		break;
	}
	return 0;
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
		newStack = union_Realloc(environment.uni, environment.stack,
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

static bool Equals(const Value *v1, const Value *v2)
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
		if (memcmp(&v1->c, &v2->c, sizeof(v1->c)) != 0) {
			return false;
		}
		break;
	case TYPE_FLOAT:
		if (v1->f != v2->f) {
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
	case TYPE_SUCCESS:
		if (strcmp(v1->succ.id, v2->succ.id) != 0) {
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

static int EvaluateInstruction(Instruction *instr, Value *result)
{
	Property *var;
	Value *value, val;

	switch (instr->instr) {
	case INSTR_BREAK:
	case INSTR_FOR:
	case INSTR_FORIN:
	case INSTR_GROUP:
	case INSTR_IF:
	case INSTR_LOCAL:
	case INSTR_RETURN:
	case INSTR_SWITCH:
	case INSTR_WHILE:
		return -1;
	case INSTR_SET:
	case INSTR_TRIGGER:
		return instruction_Execute(instr, result);
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
	case INSTR_INVOKESUB:
		if (EvaluateInstruction(instr->invokesub.from, result) < 0) {
			return -1;
		}
		if (GetSubVariable(result, instr->invokesub.sub, result) < 0) {
			return -1;
		}
		if (result->type != TYPE_FUNCTION) {
			return -1;
		}
		if (function_Execute(result->func, instr->invokesub.args,
					instr->invokesub.numArgs, result) < 0) {
			return -1;
		}
		break;
	case INSTR_INVOKESYS:
		if (ExecuteSystem(instr->invoke.name,
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
		/* value is NULL means that this is a property of a view
		 * but the current view is NULL (static mode) */
		if (var == NULL || value == NULL) {
			return -1;
		}
		*result = *value;
		break;
	case INSTR_SUBVARIABLE:
		if (EvaluateInstruction(instr->subvariable.from, &val) < 0) {
			return -1;
		}
		if (GetSubVariable(&val, instr->subvariable.name,
					result) < 0) {
			return -1;
		}
		break;
	}
	return 0;
}

int instruction_Execute(Instruction *instr, Value *result)
{
	Property *var;
	Value *pValue, val;
	bool b;
	Property *newStack;
	Value out;
	Value from, to, in;
	Uint32 index;

	switch (instr->instr) {
	case INSTR_SUBVARIABLE:
	case INSTR_THIS:
	case INSTR_VALUE:
	case INSTR_VARIABLE:
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
		newStack = union_Realloc(environment.uni, environment.stack,
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
			const int r = instruction_Execute(instr->forr.iter,
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
		newStack = union_Realloc(environment.uni, environment.stack,
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
				const int r = instruction_Execute(instr->forin.iter,
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
				const int r = instruction_Execute(instr->forin.iter,
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
			return instruction_Execute(instr->iff.iff, result);
		} else if (instr->iff.els != NULL) {
			return instruction_Execute(instr->iff.els, result);
		}
		break;

	case INSTR_INVOKESUB:
		if (EvaluateInstruction(instr->invokesub.from, &val) < 0) {
			return -1;
		}
		if (GetSubVariable(&val, instr->invokesub.sub, result) < 0) {
			return -1;
		}
		if (result->type != TYPE_FUNCTION) {
			return -1;
		}
		if (function_Execute(result->func, instr->invokesub.args,
					instr->invokesub.numArgs, result) < 0) {
			return -1;
		}
		break;

	case INSTR_INVOKESYS:
		if (ExecuteSystem(instr->invoke.name,
				instr->invoke.args,
				instr->invoke.numArgs, result) < 0) {
			return -1;
		}
		break;

	case INSTR_LOCAL:
		if (EvaluateInstruction(instr->local.value, result) < 0) {
			return -1;
		}
		newStack = union_Realloc(environment.uni, environment.stack,
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
		if (EvaluateInstruction(instr->set.src, result) < 0) {
			return -1;
		}
		if (instr->set.dest->instr == INSTR_VARIABLE) {
			var = SearchVariable(instr->set.dest->variable.name,
					&pValue);
			if (var == NULL) {
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
		} else if (instr->set.dest->instr == INSTR_SUBVARIABLE) {
			if (EvaluateInstruction(instr->set.dest->subvariable.from, &val) < 0) {
				return -1;
			}
			if (SetSubVariable(&val, instr->set.dest->subvariable.name,
						result) < 0) {
				return -1;
			}
		} else {
			return -1;
		}
		break;

	case INSTR_SWITCH: {
		Uint32 i = UINT32_MAX;
		int r;

		if (EvaluateInstruction(instr->switchh.value, result) < 0) {
			return -1;
		}
		for (Uint32 j = 0; j < instr->switchh.numJumps; j++) {
			if (EvaluateInstruction(&instr->switchh.conditions[j],
						&val) < 0) {
				return -1;
			}
			if (Equals(&val, result)) {
				i = instr->switchh.jumps[j];
				break;
			}
		}
		for (; i < instr->switchh.numInstructions; i++) {
			r = instruction_Execute(&instr->switchh.instructions[i],
					result);
			if (r == 2) {
				return 0;
			}
			if (r != 0) {
				return r;
			}
		}
		break;
	}

	case INSTR_TRIGGER: {
		struct trigger *trigger;

		/* triggers are just system functions but defined
		 * by the user in C and installed using
		 * trigger_Install(name, triggerFunc) */
		trigger = trigger_Get(instr->trigger.name);
		if (trigger == NULL) {
			return -1;
		}
		Value values[instr->trigger.numArgs];
		for (Uint32 i = 0; i < instr->trigger.numArgs; i++) {
			if (EvaluateInstruction(&instr->trigger.args[i],
						&values[i]) < 0) {
				return -1;
			}
		}
		trigger->trigger(values, instr->trigger.numArgs, result);
		break;
	}

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
			const int r = instruction_Execute(instr->whilee.iter, result);
			if (r == 2) {
				return 0;
			}
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
		const int r = instruction_Execute(&instrs[i], result);
		if (r != 0) {
			return r;
		}
	}
	return 0;
}

static int SystemAnd(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemDiv(const Value *args, Uint32 numArgs, Value *result)
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
			result->i /= val.i;
		} else {
			result->f /= val.f;
		}
	}
	return 0;
}

static int SystemDup(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1) {
		return -1;
	}
	switch (args[0].type) {
	case TYPE_ARRAY: {
		struct value_array *arr;

		arr = union_Alloc(environment.uni, sizeof(*arr));
		if (arr == NULL) {
			return -1;
		}
		arr->numValues = args[0].a->numValues;
		if (arr->numValues != 0) {
			arr->values = union_Alloc(environment.uni,
					sizeof(*arr->values) * arr->numValues);
			if (arr->values == NULL) {
				return -1;
			}
			memcpy(arr->values, args[0].a->values,
					sizeof(*arr->values) * arr->numValues);
		} else {
			arr->values = NULL;
		}
		result->type = TYPE_ARRAY;
		result->a = arr;
		break;
	}
	case TYPE_STRING: {
		struct value_string *str;

		str = union_Alloc(environment.uni, sizeof(*str));
		if (str == NULL) {
			return -1;
		}
		str->length = args[0].s->length;
		if (str->length != 0) {
			str->data = union_Alloc(environment.uni, str->length);
			if (str->data == NULL) {
				return -1;
			}
			memcpy(str->data, args[0].s->data, str->length);
		} else {
			str->data = NULL;
		}
		result->type = TYPE_STRING;
		result->s = str;
		break;
	}
	default:
		*result = args[0];
	}
	return 0;
}

static int SystemEquals(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemExists(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_SUCCESS) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->b = args[0].succ.success;
	return 0;
}

static int SystemFile(const Value *args, Uint32 numArgs, Value *result)
{
	FILE *fp;
	long pos;

	if (numArgs != 1 || args[0].type != TYPE_STRING) {
		return -1;
	}

	result->type = TYPE_SUCCESS;

	char name[args[0].s->length + 1];
	memcpy(name, args[0].s->data, args[0].s->length);
	name[args[0].s->length] = '\0';

	result->succ.id = union_Alloc(environment.uni, sizeof(name));
	if (result->succ.id == NULL) {
		return -1;
	}
	strcpy(result->succ.id, name);

	fp = fopen(name, "r");
	if (fp == NULL) {
		result->succ.success = false;
		return 0;
	}

	fseek(fp, 0, SEEK_END);
	pos = ftell(fp);
	result->succ.content = union_Alloc(environment.uni, pos);
	if (result->succ.content == NULL) {
		return -1;
	}
	fseek(fp, 0, SEEK_SET);
	fread(result->succ.content, 1, pos, fp);

	result->succ.success = true;
	return 0;
}

static int SystemFloat(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1) {
		return -1;
	}
	return value_Cast(&args[0], TYPE_FLOAT, result);
}

static int SystemGet(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemGetTextExtent(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;

	if (numArgs == 0 || args[0].type != TYPE_STRING) {
		return -1;
	}
	if (numArgs == 2) {
		if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0) {
			return -1;
		}
		if (val.i < 0) {
			return -1;
		}
		if (val.i > args[0].s->length) {
			val.i = args[0].s->length;
		}
	} else if (numArgs == 1) {
		val.i = args[0].s->length;
	} else {
		return -1;
	}

	result->type = TYPE_RECT;
	if (renderer_GetTextExtent(args[0].s->data, val.i,
				&result->r) < 0) {
		return -1;
	}
	return 0;
}

static int Compare(const Value *v1, const Value *v2)
{
	Value av1, av2;

	if (v1->type == TYPE_FLOAT || v2->type == TYPE_FLOAT) {
		if (value_Cast(v1, TYPE_FLOAT, &av1) < 0) {
			return -1;
		}
		if (value_Cast(v2, TYPE_FLOAT, &av2) < 0) {
			return -1;
		}
		return av1.f < av2.f ? -1 : av1.f > av2.f ? 1 : 0;
	} else {
		if (value_Cast(v1, TYPE_INTEGER, &av1) < 0) {
			return -1;
		}
		if (value_Cast(v2, TYPE_INTEGER, &av2) < 0) {
			return -1;
		}
		return av1.i < av2.i ? -1 : av1.i > av2.i ? 1 : 0;
	}
}

static int SystemGeq(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->b = Compare(&args[0], &args[1]) >= 0;
	return 0;
}

static int SystemGtr(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->b = Compare(&args[0], &args[1]) > 0;
	return 0;
}

static int args_GetFloats4(const Value *args, Uint32 numArgs, float *values)
{
	Uint32 numFloats = 0;
	Value val;

	if (numArgs == 2 || numArgs > 4) {
		return -1;
	}
	for (Uint32 i = 0; i < numArgs; i++) {
		if (value_Cast(&args[i], TYPE_FLOAT, &val) < 0) {
			return -1;
		}
		values[numFloats++] = val.f;
	}
	return 0;
}

static int SystemHsl(const Value *args, Uint32 numArgs, Value *result)
{
	hsl_t hsl;
	float values[4];

	if (args_GetFloats4(args, numArgs, values) < 0) {
		return -1;
	}

	hsl.alpha = 1.0f;
	switch (numArgs) {
	case 0:
		hsl.hue = 0.0f;
		hsl.saturation = 0.0f;
		hsl.lightness = 0.0f;
		break;
	case 1:
		hsl.hue = 0.0f;
		hsl.saturation = 0.0f;
		hsl.lightness = values[0];
		break;
	case 3:
		hsl.hue = values[0];
		hsl.saturation = values[1];
		hsl.lightness = values[2];
		break;
	case 4:
		hsl.alpha = values[0];
		hsl.hue = values[1];
		hsl.saturation = values[2];
		hsl.lightness = values[3];
		break;
	}

	result->type = TYPE_COLOR;
	HslToRgb(&hsl, &result->c);
	return 0;
}

static int SystemHsv(const Value *args, Uint32 numArgs, Value *result)
{
	hsv_t hsv;
	float values[4];

	if (args_GetFloats4(args, numArgs, values) < 0) {
		return -1;
	}

	hsv.alpha = 1.0f;
	switch (numArgs) {
	case 0:
		hsv.hue = 0.0f;
		hsv.saturation = 0.0f;
		hsv.value = 0.0f;
		break;
	case 1:
		hsv.hue = 0.0f;
		hsv.saturation = 0.0f;
		hsv.value = values[0];
		break;
	case 3:
		hsv.hue = values[0];
		hsv.saturation = values[1];
		hsv.value = values[2];
		break;
	case 4:
		hsv.alpha = values[0];
		hsv.hue = values[1];
		hsv.saturation = values[2];
		hsv.value = values[3];
		break;
	}

	result->type = TYPE_COLOR;
	HsvToRgb(&hsv, &result->c);
	return 0;
}

static int SystemInsert(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;
	Sint64 index;

	if (numArgs < 3) {
		return -1;
	}
	if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0) {
		return -1;
	}
	index = val.i;

	if (args[0].type == TYPE_ARRAY) {
		struct value_array *arr;
		Value *newValues;

		arr = args[0].a;
		if (index < 0) {
			index = arr->numValues + index;
		}
		if (index < 0 || index > arr->numValues) {
			return -1;
		}
		if (arr->values != NULL &&
				!union_HasPointer(environment.uni,
					arr->values)) {
			return -1;
		}

		newValues = union_Realloc(environment.uni, arr->values,
				sizeof(*arr->values) *
				(arr->numValues + numArgs - 2));
		if (newValues == NULL) {
			return -1;
		}
		arr->values = newValues;
		memmove(&arr->values[index + numArgs - 2], &arr->values[index],
				sizeof(*arr->values) *
				(arr->numValues - index));
		memcpy(&arr->values[index], &args[2],
				sizeof(*arr->values) * (numArgs - 2));
		arr->numValues += numArgs - 2;
	} else if (args[0].type == TYPE_STRING) {
		struct value_string *str;
		Uint32 count;
		char *newData;

		str = args[0].s;
		if (index < 0) {
			index = str->length + index;
		}
		if (index < 0 || index > str->length) {
			return -1;
		}
		if (str->data != NULL &&
				!union_HasPointer(environment.uni, str->data)) {
			return -1;
		}

		count = 0;
		for (Uint32 i = 2; i < numArgs; i++) {
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

		if (count == 0) {
			return 0;
		}

		newData = union_Realloc(environment.uni, str->data,
				str->length + count);
		if (newData == NULL) {
			return -1;
		}
		str->data = newData;

		memmove(&str->data[index + count], &str->data[index],
				str->length - index);
		for (Uint32 i = 2; i < numArgs; i++) {
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
		str->length += count;
	} else {
		return -1;
	}
	(void) result;
	return 0;
}

static int SystemInt(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1) {
		return -1;
	}
	return value_Cast(&args[0], TYPE_INTEGER, result);
}

static int SystemLength(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemLeq(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->b = Compare(&args[0], &args[1]) <= 0;
	return 0;
}

static int SystemLss(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2) {
		return -1;
	}
	result->type = TYPE_BOOL;
	result->b = Compare(&args[0], &args[1]) < 0;
	return 0;
}

static int SystemMod(const Value *args, Uint32 numArgs, Value *result)
{
	Value v1, v2;

	if (numArgs != 2) {
		return -1;
	}
	if (args[0].type == TYPE_FLOAT || args[1].type == TYPE_FLOAT) {
		result->type = TYPE_FLOAT;
	} else {
		result->type = TYPE_INTEGER;
	}

	if (value_Cast(&args[0], result->type, &v1) < 0) {
		return -1;
	}
	if (value_Cast(&args[1], result->type, &v2) < 0) {
		return -1;
	}

	if (result->type == TYPE_FLOAT) {
		result->f = fmod(v1.f, v2.f);
	} else {
		result->i = v1.i % v2.i;
	}
	return 0;
}

static int SystemMul(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;

	result->type = TYPE_INTEGER;
	result->i = 1;
	for (Uint32 i = 0; i < numArgs; i++) {
		if (args[i].type == TYPE_FLOAT) {
			result->type = TYPE_FLOAT;
			result->f = 1.0f;
			break;
		}
	}
	for (Uint32 i = 0; i < numArgs; i++) {
		if (value_Cast(&args[i], result->type, &val) < 0) {
			return -1;
		}
		if (result->type == TYPE_INTEGER) {
			result->i *= val.i;
		} else {
			result->f *= val.f;
		}
	}
	return 0;
}

static int SystemName(const Value *args, Uint32 numArgs, Value *result)
{
	struct value_string *s;
	Uint32 len;
	char *data;

	if (numArgs != 1 || args[0].type != TYPE_SUCCESS) {
		return -1;
	}

	s = union_Allocf(environment.uni, sizeof(*s), 1 + TYPE_STRING);
	if (s == NULL) {
		return -1;
	}

	len = strlen(args[0].succ.id);
	data = union_Alloc(environment.uni, len);
	if (data == NULL) {
		return -1;
	}
	memcpy(data, args[0].succ.id, len);

	s->data = data;
	s->length = len;

	result->type = TYPE_STRING;
	result->s = s;
	return 0;
}

static int SystemOr(const Value *args, Uint32 numArgs, Value *result)
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

static int args_GetPoint(const Value *args, Uint32 numArgs, Point *p)
{
	Value v;

	if (numArgs == 2) {
		Sint32 nums[2];

		for (Uint32 i = 0; i < numArgs; i++) {
			if (value_Cast(&args[i], TYPE_INTEGER, &v) < 0) {
				return -1;
			}
			nums[i] = v.i;
		}
		*p = (Point) { nums[0], nums[1] };
	} else if (numArgs == 1) {
		if (args[0].type != TYPE_POINT) {
			return -1;
		}
		*p = args[0].p;
	} else {
		return -1;
	}
	return 0;
}

static int args_GetRect(const Value *args, Uint32 numArgs, Rect *r)
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

static int SystemPoint(const Value *args, Uint32 numArgs, Value *result)
{
	Point p;

	if (numArgs == 0) {
		p.x = 0;
		p.y = 0;
	} else if (args_GetPoint(args, numArgs, &p) < 0) {
		return -1;
	}
	result->type = TYPE_POINT;
	result->p = p;
	return 0;
}

static int SystemNot(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemNotEquals(const Value *args, Uint32 numArgs, Value *result)
{
	if (SystemEquals(args, numArgs, result) < 0) {
		return -1;
	}
	result->b = !result->b;
	return 0;
}

static void PrintValue(const Value *value, FILE *fp)
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
		fprintf(fp, "argb(%f, %f, %f, %f)", value->c.alpha,
				value->c.red, value->c.green, value->c.blue);
		break;
	case TYPE_EVENT:
		break;
	case TYPE_FLOAT:
		fprintf(fp, "%f", value->f);
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
	case TYPE_SUCCESS:
		if (value->succ.success) {
			fprintf(fp, "%s", value->succ.content);
		} else if (value->succ.id != NULL) {
			fprintf(fp, "no success on: %s", value->succ.id);
		} else {
			fprintf(fp, "(null)");
		}
		break;
	case TYPE_VIEW:
		fprintf(fp, "%p", value->v);
		break;
	}
}

static int SystemPrint(const Value *args, Uint32 numArgs, Value *result)
{
	FILE *const fp = stdout;
	for (Uint32 i = 0; i < numArgs; i++) {
		PrintValue(&args[i], fp);
	}
	(void) result;
	return 0;
}

static int SystemRect(const Value *args, Uint32 numArgs, Value *result)
{
	Rect r;

	if (numArgs == 0) {
		r.x = 0;
		r.y = 0;
		r.w = 0;
		r.h = 0;
	} else if (args_GetRect(args, numArgs, &r) < 0) {
		return -1;
	}
	result->type = TYPE_RECT;
	result->r = r;
	return 0;
}

static int SystemRemove(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;
	Uint32 range[2];

	if (numArgs < 2 || numArgs > 3) {
		return -1;
	}

	if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0) {
		return -1;
	}
	if (val.i < 0) {
		return -1;
	}
	range[0] = val.i;

	if (numArgs == 3) {
		if (value_Cast(&args[2], TYPE_INTEGER, &val) < 0) {
			return -1;
		}
		if (val.i < 0) {
			return -1;
		}
	}
	range[1] = val.i;

	if (range[0] > range[1]) {
		return -1;
	}

	switch (args[0].type) {
	case TYPE_ARRAY: {
		struct value_array *arr;

		arr = args[0].a;
		if (range[0] >= arr->numValues || range[1] >= arr->numValues) {
			return -1;
		}
		memmove(&arr->values[range[0]], &arr->values[range[1] + 1],
				sizeof(*arr->values) *
				(arr->numValues - range[1] - 1));
		arr->numValues -= range[1] + 1 - range[0];
		break;
	}
	case TYPE_STRING: {
		struct value_string *str;

		str = args[0].s;
		if (range[0] >= str->length || range[1] >= str->length) {
			return -1;
		}
		memmove(&str->data[range[0]], &str->data[range[1] + 1],
				str->length - range[1] - 1);
		str->length -= range[1] + 1 - range[0];
		break;
	}
	default:
		return -1;
	}
	(void) result;
	return 0;
}

static int SystemSub(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;

	if (numArgs != 2) {
		return -1;
	}
	if (args[0].type == TYPE_FLOAT || args[1].type == TYPE_FLOAT) {
		if (value_Cast(&args[0], TYPE_FLOAT, result) < 0) {
			return -1;
		}
		if (value_Cast(&args[1], TYPE_FLOAT, &val) < 0) {
			return -1;
		}
		result->f -= val.f;
	} else {
		if (value_Cast(&args[0], TYPE_INTEGER, result) < 0) {
			return -1;
		}
		if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0) {
			return -1;
		}
		result->i -= val.i;
	}
	return 0;
}

static int SystemSum(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemRand(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemRgb(const Value *args, Uint32 numArgs, Value *result)
{
	rgb_t rgb;
	float values[4];

	if (args_GetFloats4(args, numArgs, values) < 0) {
		return -1;
	}

	rgb.alpha = 1.0f;
	switch (numArgs) {
	case 0:
		rgb.red = 0.0f;
		rgb.green = 0.0f;
		rgb.blue = 0.0f;
		break;
	case 1:
		rgb.red = 0.0f;
		rgb.green = 0.0f;
		rgb.blue = values[0];
		break;
	case 3:
		rgb.red = values[0];
		rgb.green = values[1];
		rgb.blue = values[2];
		break;
	case 4:
		rgb.alpha = values[0];
		rgb.red = values[1];
		rgb.green = values[2];
		rgb.blue = values[3];
		break;
	}

	result->type = TYPE_COLOR;
	result->c = rgb;
	return 0;
}

static int SystemContains(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemCreateView(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemDefaultView(const Value *args, Uint32 numArgs, Value *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_VIEW;
	result->v = view_Default();
	return 0;
}

static int SystemDrawEllipse(const Value *args, Uint32 numArgs, Value *result)
{
	Rect r;

	if (args_GetRect(&args[0], numArgs, &r) < 0) {
		return -1;
	}
	renderer_DrawEllipse(r.x, r.y, r.w, r.h);
	(void) result;
	return 0;
}

static int SystemDrawRect(const Value *args, Uint32 numArgs, Value *result)
{
	Rect r;

	if (args_GetRect(&args[0], numArgs, &r) < 0) {
		return -1;
	}
	renderer_DrawRect(&r);
	(void) result;
	return 0;
}

static int SystemCreateFont(const Value *args, Uint32 numArgs, Value *result)
{
	char *name;
	Value size;
	Uint32 index;

	if (numArgs != 2 || args[0].type != TYPE_STRING) {
		return -1;
	}
	name = WordTerminate(args[0].s);
	if (name == NULL) {
		return -1;
	}
	if (value_Cast(&args[1], TYPE_INTEGER, &size) < 0) {
		return -1;
	}
	if (renderer_CreateFont(name, size.i, &index) == NULL) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = index;
	return 0;
}

static int SystemDrawText(const Value *args, Uint32 numArgs, Value *result)
{
	Point p;
	Rect r;

	if (numArgs < 3 || args[0].type != TYPE_STRING) {
		return -1;
	}
	if (args_GetPoint(&args[1], numArgs - 1, &p) < 0) {
		return -1;
	}
	r.x = p.x;
	r.y = p.y;
	if (renderer_DrawText(args[0].s->data,
				args[0].s->length, &r) < 0) {
		return -1;
	}
	(void) result;
	return 0;
}

static int SystemGetParent(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_VIEW) {
		return -1;
	}
	result->type = TYPE_VIEW;
	result->v = args[0].v->parent;
	return 0;
}

static int SystemSetDrawColor(const Value *args, Uint32 numArgs, Value *result)
{
	Value value;

	if (numArgs != 1) {
		return -1;
	}

	if (value_Cast(&args[0], TYPE_COLOR, &value) < 0) {
		return -1;
	}

	renderer_SetDrawColorRGB(value.c.alpha * 255.0f,
			value.c.red, value.c.green, value.c.blue);
	(void) result;
	return 0;
}

static int SystemSetFont(const Value *args, Uint32 numArgs, Value *result)
{
	Value index;

	if (numArgs != 1) {
		return -1;
	}
	if (value_Cast(&args[0], TYPE_INTEGER, &index) < 0) {
		return -1;
	}
	if (renderer_SelectFont(index.i) < 0) {
		return -1;
	}
	(void) result;
	return 0;
}

static int SystemSetParent(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 2 || args[0].type != TYPE_VIEW ||
			args[1].type != TYPE_VIEW) {
		return -1;
	}
	view_SetParent(args[0].v, args[1].v);
	(void) result;
	return 0;
}

static int SystemGetProperty(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemSetProperty(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemFillEllipse(const Value *args, Uint32 numArgs, Value *result)
{
	Rect r;

	if (args_GetRect(args, numArgs, &r) < 0) {
		return -1;
	}
	renderer_FillEllipse(r.x, r.y, r.w, r.h);
	(void) result;
	return 0;
}

static int SystemFillRect(const Value *args, Uint32 numArgs, Value *result)
{
	Rect r;

	if (args_GetRect(args, numArgs, &r) < 0) {
		return -1;
	}
	renderer_FillRect(&r);
	(void) result;
	return 0;
}

static int SystemGetRect(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_VIEW) {
		return -1;
	}
	result->type = TYPE_RECT;
	result->r = args[0].v->rect;
	return 0;
}


static int SystemSetRect(const Value *args, Uint32 numArgs, Value *result)
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

static int SystemGetWindowWidth(const Value *args, Uint32 numArgs, Value *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = gui_GetWindowWidth();
	return 0;
}

static int SystemGetWindowHeight(const Value *args, Uint32 numArgs, Value *result)
{
	(void) args;
	if (numArgs != 0) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = gui_GetWindowHeight();
	return 0;
}

static int SystemGetText(const Value *args, Uint32 numArgs, Value *result)
{
	struct value_string *str;
	Uint32 len;
	char *data;

	if (numArgs != 1 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	str = union_Alloc(union_Default(), sizeof(*str));
	if (str == NULL) {
		return -1;
	}

	len = strlen(args[0].e.info.ti.text);
	data = union_Alloc(union_Default(), len + 1);
	if (data == NULL) {
		return -1;
	}
	memcpy(data, args[0].e.info.ti.text, len);
	data[len] = '\0';

	str->data = data;
	str->length = len;

	result->type = TYPE_STRING;
	result->s = str;
	return 0;
}

static int SystemGetType(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = args[0].e.event;
	return 0;
}

static int SystemGetPos(const Value *args, Uint32 numArgs, Value *result)
{
	const EventInfo *info;

	if (numArgs != 1 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	info = &args[0].e.info;
	result->type = TYPE_POINT;
	result->p.x = info->mi.x;
	result->p.y = info->mi.y;
	return 0;
}

static int SystemGetButton(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = args[0].e.info.mi.button;
	return 0;
}

static int SystemGetFontSize(const Value *args, Uint32 numArgs, Value *result)
{
	Font *font;

	if (numArgs != 1 || args[0].type != TYPE_INTEGER) {
		return -1;
	}
	font = renderer_GetFont(args[0].i);
	result->type = TYPE_INTEGER;
	result->i = TTF_FontHeight(font);
	return 0;
}

static int SystemGetKey(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = args[0].e.info.ki.sym.sym;
	return 0;
}

static int SystemGetWheel(const Value *args, Uint32 numArgs, Value *result)
{
	if (numArgs != 1 || args[0].type != TYPE_EVENT) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = args[0].e.info.mwi.y;
	return 0;
}

static int SystemSetTextInputRect(const Value *args, Uint32 numArgs,
		Value *result)
{
	Rect r;

	if (args_GetRect(args, numArgs, &r) < 0) {
		return -1;
	}
	SDL_SetTextInputRect(&r);
	(void) result;
	return 0;
}

static int SystemUtf8Next(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;
	struct value_string *str;

	if (numArgs != 2 || args[0].type != TYPE_STRING) {
		return -1;
	}
	if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0) {
		return -1;
	}
	str = args[0].s;
	if (val.i < 0 || val.i > str->length) {
		return -1;
	}

	result->type = TYPE_INTEGER;
	result->i = utf8_Next(str->data, str->length, val.i);
	return 0;
}

static int SystemUtf8Prev(const Value *args, Uint32 numArgs, Value *result)
{
	Value val;
	struct value_string *str;

	if (numArgs != 2 || args[0].type != TYPE_STRING) {
		return -1;
	}
	if (value_Cast(&args[1], TYPE_INTEGER, &val) < 0) {
		return -1;
	}
	str = args[0].s;
	if (val.i < 0 || val.i > str->length) {
		return -1;
	}
	result->type = TYPE_INTEGER;
	result->i = utf8_Prev(str->data, str->length, val.i);
	return 0;
}

static int ExecuteSystem(const char *call,
		Instruction *args, Uint32 numArgs, Value *result)
{
	static const struct system_function {
		const char *name;
		int (*call)(const Value *args, Uint32 numArgs, Value *result);
	} functions[] = {
		{ "and", SystemAnd },
		{ "div", SystemDiv },
		{ "dup", SystemDup },
		{ "equals", SystemEquals },
		{ "exists", SystemExists },
		{ "file", SystemFile },
		{ "float", SystemFloat },
		{ "get", SystemGet },
		{ "geq", SystemGeq },
		{ "gtr", SystemGtr },
		{ "hsl", SystemHsl },
		{ "hsv", SystemHsv },
		{ "insert", SystemInsert },
		{ "int", SystemInt },
		{ "length", SystemLength },
		{ "leq", SystemLeq },
		{ "lss", SystemLss },
		{ "mod", SystemMod },
		{ "mul", SystemMul },
		{ "name", SystemName },
		{ "not", SystemNot },
		{ "notequals", SystemNotEquals },
		{ "or", SystemOr },
		{ "point", SystemPoint },
		{ "print", SystemPrint },
		{ "rand", SystemRand },
		{ "rect", SystemRect },
		{ "remove", SystemRemove },
		{ "rgb", SystemRgb },
		{ "sub", SystemSub },
		{ "sum", SystemSum },

		{ "Contains", SystemContains },
		{ "CreateFont", SystemCreateFont },
		{ "CreateView", SystemCreateView },
		{ "DefaultView", SystemDefaultView },
		{ "DrawRect", SystemDrawRect },
		{ "DrawEllipse", SystemDrawEllipse },
		{ "DrawText", SystemDrawText },
		{ "FillEllipse", SystemFillEllipse },
		{ "FillRect", SystemFillRect },
		{ "GetButton", SystemGetButton },
		{ "GetFontSize", SystemGetFontSize },
		{ "GetKey", SystemGetKey },
		{ "GetParent", SystemGetParent },
		{ "GetPos", SystemGetPos },
		{ "GetProperty", SystemGetProperty },
		{ "GetRect", SystemGetRect },
		{ "GetText", SystemGetText },
		{ "GetTextExtent", SystemGetTextExtent },
		{ "GetType", SystemGetType },
		{ "GetWheel", SystemGetWheel },
		{ "GetWindowHeight", SystemGetWindowHeight },
		{ "GetWindowWidth", SystemGetWindowWidth },
		{ "SetDrawColor", SystemSetDrawColor },
		{ "SetFont", SystemSetFont },
		{ "SetParent", SystemSetParent },
		{ "SetProperty", SystemSetProperty },
		{ "SetRect", SystemSetRect },
		{ "SetTextInputRect", SystemSetTextInputRect },

		{ "Utf8Next", SystemUtf8Next },
		{ "Utf8Prev", SystemUtf8Prev },
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

	if (wrapper->numProperties == 0) {
		return 0;
	}

	Label *const label = environment.cur;
	const Uint32 num = label->numProperties;
	newProperties = union_Realloc(environment.uni, label->properties,
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

	label = union_Alloc(environment.uni, sizeof(*label));
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
