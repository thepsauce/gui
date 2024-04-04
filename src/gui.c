#include "gui.h"

SDL_Window *gui_window;
SDL_Renderer *gui_renderer;
const Uint8 *gui_keys;
bool gui_running;

int button_Proc(View *view, event_t event, EventInfo *info);

Renderer *renderer_Default(void)
{
	return gui_renderer;
}

Sint32 gui_GetWindowWidth(void)
{
	int w, h;

	SDL_GetWindowSize(gui_window, &w, &h);
	return w;
}

Sint32 gui_GetWindowHeight(void)
{
	int w, h;

	SDL_GetWindowSize(gui_window, &w, &h);
	return h;
}

int gui_Init(Uint32 flags)
{
	(void) flags;

	srand(time(NULL));
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("SDL could not be initialized: %s\n", SDL_GetError());
		return 1;
	}

	if (TTF_Init() < 0) {
		printf("SDL_ttf could not be initialized: %s\n", TTF_GetError());
		return 1;
	}

	gui_keys = SDL_GetKeyboardState(NULL);

	gui_window = SDL_CreateWindow(
		"My SDL2 Window",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		640,
		480,
		SDL_WINDOW_SHOWN
	);
	if (gui_window == NULL) {
		printf("SDL gui_window could not be created: %s\n", SDL_GetError());
		return 1;
	}
	gui_renderer = SDL_CreateRenderer(gui_window, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	return 0;
}

static int TranslateEvent(const SDL_Event *event,
		event_t *type, EventInfo *info)
{
	switch (event->type) {
	case SDL_KEYDOWN:
		*type = EVENT_KEYDOWN;
		info->ki.state = event->key.state;
		info->ki.repeat = event->key.repeat;
		info->ki.sym = event->key.keysym;
		break;
	case SDL_KEYUP:
		*type = EVENT_KEYUP;
		info->ki.state = event->key.state;
		info->ki.repeat = event->key.repeat;
		info->ki.sym = event->key.keysym;
		break;
	case SDL_MOUSEMOTION:
		*type = EVENT_MOUSEMOVE;
		info->mmi.state = event->motion.state;
		info->mmi.x = event->motion.x;
		info->mmi.y = event->motion.y;
		info->mmi.dx = event->motion.xrel;
		info->mmi.dy = event->motion.yrel;
		break;
	case SDL_MOUSEBUTTONDOWN:
		*type = EVENT_BUTTONDOWN;
		info->mi.button = event->button.button;
		info->mi.clicks = event->button.clicks;
		info->mi.x = event->button.x;
		info->mi.y = event->button.y;
		break;
	case SDL_MOUSEBUTTONUP:
		*type = EVENT_BUTTONUP;
		info->mi.button = event->button.button;
		info->mi.clicks = event->button.clicks;
		info->mi.x = event->button.x;
		info->mi.y = event->button.y;
		break;
	case SDL_MOUSEWHEEL:
		*type = EVENT_MOUSEWHEEL;
		info->mwi.x = event->wheel.x;
		info->mwi.y = event->wheel.y;
		break;
	case SDL_TEXTINPUT:
		*type = EVENT_TEXTINPUT;
		strcpy(info->ti.text, event->text.text);
		break;
	case SDL_TEXTEDITING:
		break;
	case SDL_QUIT:
		gui_running = false;
		/* fall through */
	default:
		return 1;
	}
	return 0;
}

int gui_Run(void)
{
	Uint64 start, end, ticks;
	SDL_Event event;
	event_t type;
	EventInfo info;

	SDL_StartTextInput();
	start = SDL_GetTicks64();
	gui_running = true;
	while (gui_running) {
		SDL_SetRenderDrawColor(gui_renderer, 0, 0, 0, 0);
		SDL_RenderClear(gui_renderer);
		end = SDL_GetTicks64();
		ticks = end - start;
		start = end;

		type = EVENT_NULL;

		while (SDL_PollEvent(&event)) {
			if (TranslateEvent(&event, &type, &info) == 0) {
				view_SendRecursive(view_Default(), type, &info);
			}
		}

		(void) ticks;
		view_SendRecursive(view_Default(), EVENT_PAINT, NULL);

		SDL_RenderPresent(gui_renderer);
	}
	SDL_DestroyRenderer(gui_renderer);
	SDL_DestroyWindow(gui_window);
	SDL_Quit();
	return 0;
}

