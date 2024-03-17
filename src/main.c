#include "gui.h"

int container_Proc(View *view, event_t event, EventInfo *info)
{
	(void) view; (void) info;
	switch (event) {
	case EVENT_CREATE:
		break;
	default:
	}
	return 0;
}

int main(void)
{
	View *cont, *view;

	if (gui_Init(GUI_INIT_CLASSES) < 0) {
		return 0;
	}

	class_Create("Container", container_Proc);

	cont = view_Create("Container", &(Rect) { 0, 0, 300, 300 });

	view = view_Create("Button", &(Rect) { 5, 5, 100, 50 });
	view_SetParent(cont, view);

	view = view_Create("Button", &(Rect) { 110, 5, 40, 40 });
	view_SetParent(cont, view);

	view = view_Create("Button", &(Rect) { 5, 60, 80, 30 });
	view_SetParent(cont, view);

	view = view_Create("Button", &(Rect) { 65, 95, 20, 20 });
	view_SetParent(cont, view);

	view_SetParent(view_Default(), cont);

	fprintf(stderr, "Memory: %zu, %u\n", view->uni->allocated,
			view->uni->numPointers);

	return gui_Run();
}
