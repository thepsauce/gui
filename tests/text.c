#include "test.h"

int TextProc(View *view, event_t type, EventInfo *info)
{
	Renderer *renderer;
	(void) view;
	(void) info;
	switch (type) {
	case EVENT_PAINT:
		renderer = renderer_Default();
		renderer_DrawText(renderer, "Hello", 30, 30);
		break;
	default:
	}
	return 0;
}

int main(void)
{
	Uint32 index;
	Label *label;
	View *view;

	if (gui_Init(0) < 0) {
		printf("failed gui_init()\n");
		return 1;
	}

	if (renderer_CreateFont("FiraCode-Regular.ttf", 24, &index) == NULL) {
		return 1;
	}
	renderer_SelectFont(index);

	label = environment_AddLabel("Test");
	if (label == NULL) {
		printf("failed environment_AddLabel()\n");
		return 1;
	}
	label->proc = TextProc;
	view = view_Create("Test", &(Rect) { 0, 0, 1000, 1000 });
	if (view == NULL) {
		printf("failed view_Create()\n");
		return 1;
	}
	view_SetParent(view, view_Default());
	return gui_Run();
}
