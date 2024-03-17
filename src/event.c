#include "gui.h"

View *focus_view;
View *mouse_view;

int view_SetFocus(View *view)
{
	focus_view = view;
	return 0;
}

void view_ReleaseMouse(void)
{
	mouse_view = NULL;
}

int view_SetMouse(View *view)
{
	if (mouse_view != NULL) {
		return -1;
	}
	mouse_view = view;
	return 0;
}


