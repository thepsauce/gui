#include "gui.h"

Region *region_Create(void)
{
	return region_Create_u(union_Default());
}

Region *region_Create_u(Union *uni)
{
	Region *reg;

	reg = union_Alloc(uni, sizeof(*reg));
	if (reg == NULL) {
		return NULL;
	}
	reg->uni = uni;
	reg->rects = NULL;
	reg->numRects = 0;
	reg->bounds = (Rect) { 0, 0, 0, 0 };
	return reg;
}

Region *region_SetEmpty(Region *reg)
{
	if (reg->rects != NULL) {
		union_Free(reg->uni, reg->rects);
		reg->rects = NULL;
	}
	reg->numRects = 0;
	reg->bounds = (Rect) { 0, 0, 0, 0 };
	return reg;
}

Region *region_Rect(const Rect *rect)
{
	return region_Rect_u(rect, union_Default());
}

Region *region_Rect_u(const Rect *rect, Union *uni)
{
	Region *reg;

	reg = region_Create_u(uni);
	if (reg == NULL) {
		return NULL;
	}
	reg->rects = union_Alloc(uni, sizeof(*reg->rects));
	if (reg->rects == NULL) {
		union_Free(uni, reg);
		return NULL;
	}
	reg->rects[0] = *rect;
	reg->numRects =  1;
	reg->bounds = *rect;
	return reg;
}

Region *region_SetRect(Region *reg, const Rect *rect)
{
	Rect *newRects;

	if (rect_IsEmpty(rect)) {
		return region_SetEmpty(reg);
	}

	newRects = union_Realloc(reg->uni, reg->rects, sizeof(*reg->rects));
	if (newRects == NULL) {
		return NULL;
	}
	reg->rects = newRects;
	reg->rects[0] = *rect;
	reg->numRects = 1;
	reg->bounds = *rect;
	return reg;
}

Region *region_AddRect(Region *reg, const Rect *rect)
{
	Rect *newRects;

	newRects = union_Realloc(reg->uni, reg->rects, sizeof(*reg->rects) *
			(reg->numRects + 1));
	if (newRects == NULL) {
		return NULL;
	}
	reg->rects = newRects;
	reg->rects[reg->numRects++] = *rect;

	const Sint32 dx1 = rect->x - reg->bounds.x;
	const Sint32 dx2 = reg->bounds.x + reg->bounds.w - (rect->x + rect->w);
	const Sint32 dy1 = rect->y - reg->bounds.y;
	const Sint32 dy2 = reg->bounds.y + reg->bounds.h - (rect->y + rect->h);
	if (dx1 < 0) {
		reg->bounds.x += dx1;
		reg->bounds.w -= dx1;
	}
	if (dx2 < 0) {
		reg->bounds.w -= dx2;
	}
	if (dy1 < 0) {
		reg->bounds.y += dy1;
		reg->bounds.h -= dy1;
	}
	if (dy2 < 0) {
		reg->bounds.h -= dy2;
	}
	return reg;
}

Region *region_Intersect(Region *reg, const Region *reg1, const Region *reg2)
{
	for (Uint32 i = 0; i < reg1->numRects; i++) {
		for (Uint32 j = 0; j < reg2->numRects; j++) {
			Rect res;

			if (rect_Intersect(&reg1->rects[i], &reg1->rects[j],
						&res)) {
				if (region_AddRect(reg, &res) == NULL) {
					region_Delete(reg);
					return NULL;
				}
			}
		}
	}
	return reg;
}

Region *region_Add(Region *reg, const Region *reg1, const Region *reg2)
{
	Rect *newRects;
	Uint32 newSize;

	newSize = reg1->numRects + reg2->numRects;
	newRects = union_Realloc(reg->uni, reg->rects,
			sizeof(*reg->rects) * newSize);
	if (newRects == NULL) {
		return NULL;
	}
	reg->rects = newRects;
	reg->numRects = newSize;
	memcpy(reg->rects, reg1->rects, sizeof(*reg1->rects) * reg1->numRects);
	memcpy(&reg->rects[reg1->numRects],
			reg2->rects, sizeof(*reg2->rects) * reg2->numRects);
	reg->bounds.x = MIN(reg1->bounds.x, reg2->bounds.x);
	reg->bounds.y = MIN(reg1->bounds.y, reg2->bounds.y);
	reg->bounds.w = MAX(reg1->bounds.x + reg1->bounds.w,
			reg2->bounds.x + reg2->bounds.w) - reg->bounds.x;
	reg->bounds.h = MAX(reg1->bounds.y + reg1->bounds.h,
			reg2->bounds.y + reg2->bounds.h) - reg->bounds.y;
	return reg;
}

static int CompareRectsX(const void *a, const void *b)
{
	const Rect *const r1 = a, *const r2 = b;
	return r1->x - r2->x;
}

static int CompareRectsY(const void *a, const void *b)
{
	const Rect *const r1 = a, *const r2 = b;
	return r1->y - r2->y;
}

Region *region_Invert(Region *reg, Region *reg1)
{
	Rect r;
	Uint32 ys, yi;

	reg->bounds = reg1->bounds;

	/* sorting at the beginning allows for smaller lookup sizes */
	qsort(reg1->rects, reg1->numRects, sizeof(*reg1->rects), CompareRectsY);
	ys = 0;
	yi = 0;
	r.y = reg1->bounds.y;
	r.h = 0;
	for (Sint32 i = 0; i < reg1->bounds.h; i++) {
		for (; ys < reg1->numRects; ys++) {
			if (reg1->rects[ys].y + reg1->rects[ys].h > r.y) {
				break;
			}
		}

		for (yi = ys; yi < reg1->numRects; yi++) {
			if (reg1->rects[yi].y > r.y + r.h) {
				break;
			}
		}

		if (yi == ys) {
			r.h++;
		} else {
			/* there exist some rectangles interfering with the
			 * scanline
			 */
			if (r.h > 0) {
				r.x = reg1->bounds.x;
				r.w = reg1->bounds.w;
				region_AddRect(reg, &r);
				r.y += r.h;
			}
			Rect rx[yi - ys + 1];
			memcpy(rx, &reg1->rects[ys], sizeof(*rx) * (yi - ys));
			qsort(rx, yi - ys, sizeof(*rx), CompareRectsX);
			rx[yi - ys] = (Rect) { reg1->bounds.x + reg1->bounds.w,
				r.y, 0, 1 };
			r.x = reg1->bounds.x;
			for (Uint32 xi = 0; xi <= yi - ys; xi++) {
				if (rx[xi].y + rx[xi].h <= r.y ||
						rx[xi].x + rx[xi].w <= r.x) {
					continue;
				}
				r.w = rx[xi].x - r.x;
				if (r.w > 0) {
					Uint32 j;

					/* join rects that align perfectly */
					for (j = 0; j < reg->numRects; j++) {
						const Rect ro = reg->rects[j];
						if (ro.y + ro.h != r.y) {
							continue;
						}
						if (ro.x == r.x &&
								ro.w == r.w) {
							reg->rects[j].h++;
							break;
						}
					}
					/* if nothing was joined,
					 * add the rect */
					if (j == reg->numRects) {
						r.h = 1;
						region_AddRect(reg, &r);
					}
				}
				r.x = rx[xi].x + rx[xi].w;
			}
			r.y++;
			r.h = 0;
		}
	}
	if (r.h > 0) {
		r.x = reg1->bounds.x;
		r.w = reg1->bounds.w;
		region_AddRect(reg, &r);
	}
	return reg;
}

Region *region_Subtract(Region *reg, const Region *reg1, const Region *reg2)
{
	Region *tmp;

	reg->numRects = reg1->numRects + reg2->numRects;
	reg->rects = union_Alloc(reg->uni, sizeof(*reg->rects) * reg->numRects);
	if (reg->rects == NULL) {
		union_Free(reg->uni, reg);
		return NULL;
	}
	/* Fact used: Subtracting a region is the same as intersecting with
	 * the inverse of that region.
	 * So: A without B = A intersect ~B
	 * Proof:
	 * Let x in A without B, that is equivalent to
	 * x in A and x not in B which is equivalent to
	 * x in A and x in ~B which is the same as
	 * x in A intersect ~B.
	 */
	tmp = region_Create_u(reg->uni);
	region_Invert(tmp, (Region*) reg2);
	reg = region_Intersect(reg, reg1, tmp);
	region_Delete(tmp);
	return reg;
}

Region *region_MoveBy(Region *reg, Sint32 dx, Sint32 dy)
{
	for (Uint32 i = 0; i < reg->numRects; i++) {
		reg->rects[i].x += dx;
		reg->rects[i].y += dy;
	}
	reg->bounds.x += dx;
	reg->bounds.y += dy;
	return reg;
}

void region_Delete(Region *reg)
{
	union_Free(reg->uni, reg->rects);
	union_Free(reg->uni, reg);
}
