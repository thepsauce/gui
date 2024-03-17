#include "gui.h"

int renderer_SetDrawColor(Renderer *renderer, Uint32 color)
{
	return SDL_SetRenderDrawColor(renderer, color & 0xff,
			(color >> 8) & 0xff, (color >> 16) & 0xff, 255);
}

int renderer_SetDrawColorRGB(Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
	return SDL_SetRenderDrawColor(renderer, r, g, b, 255);
}

int renderer_FillRect(Renderer *renderer, Rect *rect)
{
	return SDL_RenderFillRect(renderer, rect);
}

int renderer_DrawLine(Renderer *renderer, Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2)
{
	return SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

int renderer_DrawEllipse(Renderer *renderer, Sint32 x, Sint32 y, Sint32 rx, Sint32 ry)
{
	int result;
	Sint32 ix, iy;
	Sint32 h, i, j, k;
	Sint32 oh, oi, oj, ok;
	Sint32 xmh, xph, ypk, ymk;
	Sint32 xmi, xpi, ymj, ypj;
	Sint32 xmj, xpj, ymi, ypi;
	Sint32 xmk, xpk, ymh, yph;

	if (rx < 0 || ry < 0) {
		return -1;
	}
	if (rx == 0) {
		return SDL_RenderDrawLine(renderer, x, y - ry, x, y + ry);
	}
	if (ry == 0) {
		return SDL_RenderDrawLine(renderer, x - rx, y, x + rx, y);
	}

	result = 0;
	oh = oi = oj = ok = 0xFFFF;

	/* Draw */
	if (rx > ry) {
		ix = 0;
		iy = rx * 64;

		do {
			h = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (h * ry) / rx;
			k = (i * ry) / rx;

			if ((ok != k && oj != k) || (oj != j && ok != j) || k != j) {
				xph = x + h;
				xmh = x - h;
				if (k > 0) {
					ypk = y + k;
					ymk = y - k;
					result |= SDL_RenderDrawPoint(renderer, xmh, ypk);
					result |= SDL_RenderDrawPoint(renderer, xph, ypk);
					result |= SDL_RenderDrawPoint(renderer, xmh, ymk);
					result |= SDL_RenderDrawPoint(renderer, xph, ymk);
				} else {
					result |= SDL_RenderDrawPoint(renderer, xmh, y);
					result |= SDL_RenderDrawPoint(renderer, xph, y);
				}
				ok = k;
				xpi = x + i;
				xmi = x - i;
				if (j > 0) {
					ypj = y + j;
					ymj = y - j;
					result |= SDL_RenderDrawPoint(renderer, xmi, ypj);
					result |= SDL_RenderDrawPoint(renderer, xpi, ypj);
					result |= SDL_RenderDrawPoint(renderer, xmi, ymj);
					result |= SDL_RenderDrawPoint(renderer, xpi, ymj);
				} else {
					result |= SDL_RenderDrawPoint(renderer, xmi, y);
					result |= SDL_RenderDrawPoint(renderer, xpi, y);
				}
				oj = j;
			}

			ix = ix + iy / rx;
			iy = iy - ix / rx;

		} while (i > h);
	} else {
		ix = 0;
		iy = ry * 64;

		do {
			h = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (h * rx) / ry;
			k = (i * rx) / ry;

			if (((oi != i) && (oh != i)) || ((oh != h) && (oi != h) && (i != h))) {
				xmj = x - j;
				xpj = x + j;
				if (i > 0) {
					ypi = y + i;
					ymi = y - i;
					result |= SDL_RenderDrawPoint(renderer, xmj, ypi);
					result |= SDL_RenderDrawPoint(renderer, xpj, ypi);
					result |= SDL_RenderDrawPoint(renderer, xmj, ymi);
					result |= SDL_RenderDrawPoint(renderer, xpj, ymi);
				} else {
					result |= SDL_RenderDrawPoint(renderer, xmj, y);
					result |= SDL_RenderDrawPoint(renderer, xpj, y);
				}
				oi = i;
				xmk = x - k;
				xpk = x + k;
				if (h > 0) {
					yph = y + h;
					ymh = y - h;
					result |= SDL_RenderDrawPoint(renderer, xmk, yph);
					result |= SDL_RenderDrawPoint(renderer, xpk, yph);
					result |= SDL_RenderDrawPoint(renderer, xmk, ymh);
					result |= SDL_RenderDrawPoint(renderer, xpk, ymh);
				} else {
					result |= SDL_RenderDrawPoint(renderer, xmk, y);
					result |= SDL_RenderDrawPoint(renderer, xpk, y);
				}
				oh = h;
			}

			ix = ix + iy / ry;
			iy = iy - ix / ry;

		} while (i > h);
	}
	return result;
}

int renderer_FillEllipse(SDL_Renderer *renderer, Sint32 x, Sint32 y, Sint32 rx, Sint32 ry)
{
	int result;
	Sint32 ix, iy;
	Sint32 h, i, j, k;
	Sint32 oh, oi, oj, ok;
	Sint32 xmh, xph;
	Sint32 xmi, xpi;
	Sint32 xmj, xpj;
	Sint32 xmk, xpk;

	if (rx < 0 || ry < 0) {
		return -1;
	}
	if (rx == 0) {
		return SDL_RenderDrawLine(renderer, x, y - ry, x, y + ry);
	}
	if (ry == 0) {
		return SDL_RenderDrawLine(renderer, x - rx, y, x + rx, y);
	}

	result = 0;

	oh = oi = oj = ok = 0xFFFF;

	/* Draw */
	if (rx > ry) {
		ix = 0;
		iy = rx * 64;

		do {
			h = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (h * ry) / rx;
			k = (i * ry) / rx;

			if ((ok != k) && (oj != k)) {
				xph = x + h;
				xmh = x - h;
				if (k > 0) {
					result |= SDL_RenderDrawLine(renderer, xmh, y + k, xph, y + k);
					result |= SDL_RenderDrawLine(renderer, xmh, y - k, xph, y - k);
				} else {
					result |= SDL_RenderDrawLine(renderer, xmh, y, xph, y);
				}
				ok = k;
			}
			if ((oj != j) && (ok != j) && (k != j)) {
				xmi = x - i;
				xpi = x + i;
				if (j > 0) {
					result |= SDL_RenderDrawLine(renderer, xmi, y + j, xpi, y + j);
					result |= SDL_RenderDrawLine(renderer, xmi, y - j, xpi, y - j);
				} else {
					result |= SDL_RenderDrawLine(renderer, xmi, y, xpi, y);
				}
				oj = j;
			}

			ix = ix + iy / rx;
			iy = iy - ix / rx;

		} while (i > h);
	} else {
		ix = 0;
		iy = ry * 64;

		do {
			h = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (h * rx) / ry;
			k = (i * rx) / ry;

			if ((oi != i) && (oh != i)) {
				xmj = x - j;
				xpj = x + j;
				if (i > 0) {
					result |= SDL_RenderDrawLine(renderer, xmj, y + i, xpj, y + i);
					result |= SDL_RenderDrawLine(renderer, xmj, y - i, xpj, y - i);
				} else {
					result |= SDL_RenderDrawLine(renderer, xmj, y, xpj, y);
				}
				oi = i;
			}
			if ((oh != h) && (oi != h) && (i != h)) {
				xmk = x - k;
				xpk = x + k;
				if (h > 0) {
					result |= SDL_RenderDrawLine(renderer, xmk, y + h, xpk, y + h);
					result |= SDL_RenderDrawLine(renderer, xmk, y - h, xpk, y - h);
				} else {
					result |= SDL_RenderDrawLine(renderer, xmk, y, xpk, y);
				}
				oh = h;
			}

			ix = ix + iy / ry;
			iy = iy - ix / ry;

		} while (i > h);
	}

	return (result);
}
