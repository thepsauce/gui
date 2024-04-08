#include "gui.h"

int main(void)
{
	Uint32 index;

	if (gui_Init(GUI_INIT_CLASSES) < 0) {
		return 1;
	}

	renderer_CreateFont("FiraCode-Regular.ttf", 14, &index);
	renderer_SelectFont(index);

	return gui_Run();
}
