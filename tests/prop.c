#include "test.h"

int main(void)
{
	FILE *fp;
	Union uni;
	RawWrapper *wrappers;
	Uint32 numWrappers;
	View *view;

	if (gui_Init(GUI_INIT_CLASSES) < 0) {
		return 1;
	}

	fp = fopen("button.prop", "r");
	if (prop_Parse(fp, &uni, &wrappers, &numWrappers) == 0) {
		if (environment_Digest(wrappers, numWrappers) == 0) {
		} else {
			fprintf(stdout, "could not digest\n");
		}
		union_FreeAll(&uni);
	}
	fclose(fp);

	view = view_Create("Button", &(Rect) { 10, 10, 80, 30 });
	if (view == NULL) {
		return 1;
	}
	view_SetParent(view_Default(), view);
	return gui_Run();
}
