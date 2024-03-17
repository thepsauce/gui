#include "gui.h"

bool rect_IsEmpty(const Rect *rect)
{
	return rect->w == 0 || rect->h == 0;
}

bool rect_Intersect(const Rect *r1, const Rect *r2, Rect *rect)
{
	return SDL_IntersectRect(r1, r2, rect);
}

int rect_Subtract(const Rect *r1, const Rect *r2, Rect *rects)
{
	int n = 0;
	Sint32 dx1, dx2;

	/* Prioritizing x axis */
	dx1 = r2->x - r1->x;
	dx2 = (r1->x + r1->w) - (r2->x + r2->w);

	const Sint32 dy1 = r2->y - r1->y;
	const Sint32 dy2 = (r1->y + r1->h) - (r2->y + r2->h);

	if (dx1 >= r1->w || dx2 >= r1->w || dy1 >= r1->h || dy2 >= r1->h) {
		rects[0] = *r1;
		return 1;
	}

	if (dx1 > 0) {
		rects[n].x = r1->x;
		rects[n].w = dx1;
		rects[n].y = r1->y;
		rects[n].h = r1->h;
		n++;
	} else {
		dx1 = 0;
	}
	if (dx2 > 0) {
		rects[n].x = r2->x + r2->w;
		rects[n].w = dx2;
		rects[n].y = r1->y;
		rects[n].h = r1->h;
		n++;
	} else {
		dx2 = 0;
	}

	if (dy1 > 0) {
		rects[n].x = r1->x + dx1;
		rects[n].w = r1->w - dx1 - dx2;
		rects[n].y = r1->y;
		rects[n].h = dy1;
		n++;
	}
	if (dy2 > 0) {
		rects[n].x = r1->x + dx1;
		rects[n].w = r1->w - dx1 - dx2;
		rects[n].y = r2->y + r2->h;
		rects[n].h = dy2;
		n++;
	}
	return n;
}

bool rect_Contains(const Rect *r, const Point *p)
{
	return p->x >= r->x && p->y >= r->y &&
		p->x < r->x + r->w && p->y < r->y + r->h;
}
