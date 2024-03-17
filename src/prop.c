#include "gui.h"

static int EvaluateWrapper(Union *uni, RawWrapper *wrapper, Label *label)
{
	label->properties = union_Alloc(uni, sizeof(*label->properties) *
			wrapper->numProperties);
	if (label->properties == NULL) {
		return -1;
	}
	for (Uint32 i = 0; i < wrapper->numProperties; i++) {
		RawProperty const *raw = &wrapper->properties[i];
		Property *prop = &label->properties[i];
		strcpy(prop->name, raw->name);
		switch (raw->instruction.instr) {
		case INSTR_EVENT:
		case INSTR_SET:
		case INSTR_TRIGGER:
			return -1;
		case INSTR_INVOKE:
			/* TODO: */
			prop->type = TYPE_INTEGER;
			break;
		case INSTR_VALUE:
			prop->type = raw->instruction.type;
			prop->value = raw->instruction.value.value;
			break;
		case INSTR_VARIABLE:
			/* TODO: */
			break;
		}

	}
	strcpy(label->name, wrapper->label);
	label->numProperties = wrapper->numProperties;
	return 0;
}

int prop_Evaluate(Union *uni, RawWrapper *wrappers, Uint32 numWrappers,
		Label **pLabels, Uint32 *pNumLabels)
{
	Label *labels;

	labels = union_Alloc(uni, sizeof(*labels) * numWrappers);
	if (labels == NULL) {
		return -1;
	}
	for (Uint32 i = 0; i < numWrappers; i++) {
		if (EvaluateWrapper(uni, &wrappers[i], &labels[i]) < 0) {
			return -1;
		}
	}
	*pLabels = labels;
	*pNumLabels = numWrappers;
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
