#include "test.h"

#define GRID_SIZE 3

typedef struct ttt {
	char turn;
	char grid[GRID_SIZE * GRID_SIZE];
} TicTacToe;

int ttt_Proc(View *view, event_t event, EventInfo *info)
{
	TicTacToe *ttt;
	Rect r;
	Renderer *renderer;
	Sint32 x, y;

	ttt = view->uni->pointers[1].sys;
	switch (event) {
	case EVENT_CREATE:
		ttt = union_Alloc(view->uni, sizeof(struct ttt));
		ttt->turn = 'X';
		break;
	case EVENT_PAINT:
		r = view->rect;
		renderer = renderer_Default();
		renderer_SetDrawColor(renderer, 255, 0, 0, 255);

		for (Sint32 i = 1; i <= 2; i++) {
			x = r.x + i * r.w / GRID_SIZE;
			y = r.y + i * r.h / GRID_SIZE;
			renderer_DrawLine(renderer, r.x, y, r.x + r.w, y);
			renderer_DrawLine(renderer, x, r.y, x, r.y + r.h);
		}
		for (Sint32 i = 0; i < (Sint32) sizeof(ttt->grid); i++) {
			const char g = ttt->grid[i];
			x = (i % GRID_SIZE) * r.w / GRID_SIZE;
			y = (i / GRID_SIZE) * r.h / GRID_SIZE;
			if (g == 'O') {
				renderer_DrawEllipse(renderer, x + r.w / 6, y + r.h / 6, r.w / 8, r.h / 8);
			} else if (g == 'X') {
				const Sint32 ox = r.w / 16;
				const Sint32 oy = r.h / 16;
				renderer_DrawLine(renderer, x + ox, y + oy,
						x + r.w / 3 - ox, y + r.h / 3 - oy);
				renderer_DrawLine(renderer, x + r.w / 3 - ox, y + oy,
						x + ox, y + r.h / 3 - oy);
			}
		}
		break;
	case EVENT_LBUTTONDOWN: {
		r = view->rect;
		const Sint32 x = info->mi.x * GRID_SIZE / r.w;
		const Sint32 y = info->mi.y * GRID_SIZE / r.h;
		if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
			break;
		}
		printf("%d, %d, %c\n", x, y, ttt->turn);
		ttt->grid[x + y * GRID_SIZE] = ttt->turn;
		ttt->turn ^= 'X' ^ 'O';
		break;
	}
	default:
	}
	return 0;
}

int main(void)
{
	View *ttt;

	if (gui_Init(0) < 0) {
		return 1;
	}
	class_Create("TicTacToe", ttt_Proc);
	ttt = view_Create("TicTacToe", &(Rect) { 0, 0, 300, 300 });
	view_SetParent(view_Default(), ttt);
	return gui_Run();
}
