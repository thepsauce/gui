#include "test.h"

int main(void)
{
	FILE *fp;
	Union uni;
	RawWrapper *wrappers;
	Uint32 numWrappers;

	if (gui_Init(GUI_INIT_CLASSES) < 0) {
		return 1;
	}

	fp = fopen("tests/prop/test.prop", "r");
	if (prop_Parse(fp, &uni, &wrappers, &numWrappers) == 0) {
		if (environment_Digest(wrappers, numWrappers) == 0) {
		} else {
			fprintf(stdout, "could not digest\n");
			return 1;
		}
		union_FreeAll(&uni);
		fclose(fp);
	} else {
		fclose(fp);
		return 1;
	}

	Label *const glob = environment_FindLabel("");
	for (Uint32 i = 0; i < glob->numProperties; i++) {
		Property *const prop = &glob->properties[i];
		if (strcmp(prop->name, "main") == 0 &&
				prop->value.type == TYPE_FUNCTION &&
				prop->value.func->numParams == 0) {
			Value code;

			function_Execute(prop->value.func, NULL, 0, &code);
			break;
		}
	}
	return gui_Run();
}
