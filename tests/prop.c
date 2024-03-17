#include "test.h"

void type_Print(type_t type, FILE *fp)
{
	const char *words[] = {
		[TYPE_BOOL] = "bool",
		[TYPE_COLOR] = "color",
		[TYPE_FLOAT] = "float",
		[TYPE_FONT] = "font",
		[TYPE_FUNCTION] = "function",
		[TYPE_INTEGER] = "int",
	};
	fprintf(fp, "%s", words[type]);
}

void function_Print(const Function *function, FILE *fp)
{
	for (Uint32 i = 0; i < function->numParams; i++) {
		if (i > 0) {
			fprintf(fp, ", ");
		}
		type_Print(function->params[i].type, fp);
		fprintf(fp, " %s", function->params[i].name);
	}
	if (function->numParams > 0) {
		fputc(' ', fp);
	}
	fprintf(fp, "{ %u }", function->numInstructions);
}

void property_Print(const Property *property, FILE *fp)
{
	fprintf(fp, "  .%s = ", property->name);
	type_Print(property->type, fp);
	fputc(' ', fp);
	switch (property->type) {
	case TYPE_NULL:
		break;
	case TYPE_BOOL:
		fprintf(fp, "%s", property->value.b ?
				"true" : "false");
		break;
	case TYPE_COLOR:
		fprintf(fp, "%#x", property->value.color);
		break;
	case TYPE_FLOAT:
		fprintf(fp, "%f", property->value.f);
		break;
	case TYPE_FONT:
		fprintf(fp, "%p", property->value.font);
		break;
	case TYPE_FUNCTION:
		function_Print(property->value.func, fp);
		break;
	case TYPE_INTEGER:
		fprintf(fp, "%ld", property->value.i);
		break;
	}
	fputc('\n', fp);
}

void label_Print(const Label *label, FILE *fp)
{
	fprintf(fp, "%s:\n", label->name);
	for (Uint32 i = 0; i < label->numProperties; i++) {
		property_Print(&label->properties[i], fp);
	}
}

int main(void)
{
	FILE *fp;
	Union uni;
	RawWrapper *wrappers;
	Uint32 numWrappers;
	Label *labels;
	Uint32 numLabels;
	View *view;

	if (gui_Init(GUI_INIT_CLASSES) < 0) {
		return 1;
	}

	fp = fopen("test.prop", "r");
	if (prop_Parse(fp, &uni, &wrappers, &numWrappers) == 0) {
		if (prop_Evaluate(&uni, wrappers, numWrappers,
					&labels, &numLabels) == 0) {
			if (prop_Digest(labels, numLabels) < 0) {
				fprintf(stdout, "could not digest\n");
			}
			for (Uint32 i = 0; i < numLabels; i++) {
				label_Print(&labels[i], stdout);
			}
		} else {
			fprintf(stdout, "could not evaluate\n");
		}
		union_FreeAll(&uni);
	}
	fclose(fp);

	view = view_Create("Button", &(Rect) { 10, 10, 80, 30 });
	view_SetParent(view_Default(), view);
	return gui_Run();
}
