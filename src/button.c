#include "gui.h"

#define V_EXTRA 1

#define F_HOVERED 1
#define F_PRESSED 2

struct button {
	char text[256];
};

void button_Render(View *view)
{
	Renderer *renderer;
	bool circular;
	Uint32 bg, away, shadow, outline, text, press, hover;

	struct button *const b = union_Mask(view->uni, V_EXTRA, NULL);
	(void) b;

	bg = view_GetColorProperty(view, "background");
	away = view_GetColorProperty(view, "away");
	shadow = view_GetColorProperty(view, "shadow");
	outline = view_GetColorProperty(view, "outline");
	text = view_GetColorProperty(view, "text");
	press = view_GetColorProperty(view, "press");
	hover = view_GetColorProperty(view, "hover");
	circular = view_GetBoolProperty(view, "circular");

	/* TODO: */
	(void) text;
	(void) outline;
	(void) shadow;

	renderer = renderer_Default();
	if (view->flags & F_PRESSED) {
		if (view->flags & F_HOVERED) {
			renderer_SetDrawColor(renderer, press);
		} else {
			renderer_SetDrawColor(renderer, away);
		}
	} else if (view->flags & F_HOVERED) {
		renderer_SetDrawColor(renderer, hover);
	} else {
		renderer_SetDrawColor(renderer, bg);
	}
	if (circular) {
		const Rect r = view->rect;
		renderer_FillEllipse(renderer, r.x + r.w / 2, r.y + r.h / 2,
				r.w / 2, r.h / 2);
	} else {
		renderer_FillRect(renderer, &view->rect);
	}
}

int button_Proc(View *view, event_t event, EventInfo *info)
{
	Point p;

	(void) info;
	switch (event) {
	case EVENT_CREATE:
		union_Allocf(view->uni, sizeof(struct button), V_EXTRA);
		break;
	case EVENT_PAINT:
		button_Render(view);
		break;
	case EVENT_MOUSEMOVE:
		p = (Point) {
			info->mmi.x,
			info->mmi.y
		};
		if (((view->flags & F_PRESSED) || info->mmi.state == 0) &&
				rect_Contains(&view->rect, &p)) {
			view->flags |= F_HOVERED;
		} else {
			view->flags &= ~F_HOVERED;
		}
		break;
	case EVENT_LBUTTONDOWN:
		if ((view->flags & F_HOVERED)) {
			view->flags |= F_PRESSED;
		}
		break;
	case EVENT_LBUTTONUP:
		if ((view->flags & (F_HOVERED | F_PRESSED)) ==
				(F_HOVERED | F_PRESSED)) {
			printf("button pressed\n");
		}
		view->flags &= ~F_PRESSED;
		break;
	default:
	}
	return 0;
}

