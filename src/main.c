#include "gui.h"

int main(void)
{
	if (gui_Init(GUI_INIT_CLASSES) < 0) {
		return 1;
	}

	return gui_Run();
}
