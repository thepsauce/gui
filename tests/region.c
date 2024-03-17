#include "../src/gui.h"

void do_stuff(void)
{
	Union uni;
	Region *reg1, *reg2;
	Region *reg;

	union_Init(&uni, SIZE_MAX);

	reg1 = region_Rect_u(&(Rect) { 0, 0, 20, 20 }, &uni);
	reg2 = region_Rect_u(&(Rect) { 10, 10, 30, 30 }, &uni);

	reg = region_Create_u(&uni);
	region_Add(reg, reg1, reg2);

	region_Delete(reg);

	reg = region_Create_u(&uni);
	region_Intersect(reg, reg1, reg2);

	union_FreeAll(&uni);
}

void region_Fill(SDL_Renderer *renderer, Region *region)
{
	for (Uint32 i = 0; i < region->numRects; i++) {
		SDL_RenderDrawRect(renderer, &region->rects[i]);
	}
}

int main(void)
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	const Uint8 *keys;
	bool running;
	Uint64 start, end, ticks;
	SDL_Event event;

	srand(time(NULL));
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("SDL could not be initialized: %s\n", SDL_GetError());
		return 1;
	}

	start = SDL_GetTicks64();
	keys = SDL_GetKeyboardState(NULL);

	window = SDL_CreateWindow(
		"My SDL2 Window",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		640,
		480,
		SDL_WINDOW_SHOWN
	);
	if (window == NULL) {
		printf("SDL window could not be created: %s\n", SDL_GetError());
		return 1;
	}
	renderer = SDL_CreateRenderer(window, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	running = true;

	Region *r1, *r2, *r3 = NULL;
	Region *inverted = NULL;
	r1 = region_Rect(&(Rect) { 18, 18, 100, 100 });
	r2 = region_Rect(&(Rect) { 120, 30, 60, 60 });
	region_AddRect(r2, &(Rect) { 190, 45, 34, 34 });
	region_AddRect(r2, &(Rect) { 230, 54, 19, 19 });
	while (running) {
		if (r3 == NULL) {
			r3 = region_Create();
		} else {
			region_SetEmpty(r3);
		}
		if (inverted == NULL) {
			inverted = region_Create();
		} else {
			region_SetEmpty(inverted);
		}
		r3 = region_Add(r3, r1, r2);
		inverted = region_Invert(inverted, r3);

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);
		end = SDL_GetTicks64();
		ticks = end - start;
		start = end;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_MOUSEMOTION:
				if (event.motion.state & SDL_BUTTON_LMASK) {
					region_MoveBy(r2, event.motion.xrel,
							event.motion.yrel);
				}
				if (event.motion.state & SDL_BUTTON_RMASK) {
					region_MoveBy(r1, event.motion.xrel,
							event.motion.yrel);
				}
				break;
			case SDL_KEYDOWN:
				break;
			}
		}
		(void) ticks;
		(void) keys;
		SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
		region_Fill(renderer, r3);
		SDL_RenderDrawRect(renderer, &r3->bounds);
		SDL_SetRenderDrawColor(renderer, 0, 0, 255, 0);
		region_Fill(renderer, inverted);
		SDL_RenderPresent(renderer);
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
