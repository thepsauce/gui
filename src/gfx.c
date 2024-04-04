#include "gui.h"

#define EPSILON 1.0e-5f

Uint32 RgbToInt(const rgb_t *rgb)
{
	Uint8 a, r, g, b;

	a = rgb->alpha * 255.0f;
	r = rgb->red;
	g = rgb->green;
	b = rgb->blue;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

void IntToRgb(Uint32 color, rgb_t *rgb)
{
	rgb->alpha = (color >> 24) / 255.0f;
	rgb->red = (color >> 16) & 0xff;
	rgb->green = (color >> 8) & 0xff;
	rgb->blue = color & 0xff;
}

void RgbToHsl(const rgb_t *rgb, hsl_t *hsl)
{
	float min, max, delta;

	hsl->alpha = rgb->alpha;
	min = rgb->red < rgb->green ? rgb->red : rgb->green;
	min = min < rgb->blue ? min : rgb->blue;

	max = rgb->red > rgb->green ? rgb->red : rgb->green;
	max = max > rgb->blue ? max : rgb->blue;

	delta = max - min;
	hsl->lightness = delta / 255.0f;
	if (delta < EPSILON) {
		hsl->saturation = 0.0f;
		hsl->hue = 0.0f;
		return;
	}

	if (max > 0.0f) {
		hsl->saturation = delta / max;
	} else {
		hsl->saturation = 0.0f;
		hsl->hue = 0.0f;
		return;
	}

	if (rgb->red == max) {
		hsl->hue = (rgb->green - rgb->blue) / delta;
	} else if(rgb->green == max) {
		hsl->hue = 2.0f + (rgb->blue - rgb->red) / delta;
	} else {
		hsl->hue = 4.0f + (rgb->red - rgb->green) / delta;
	}

	hsl->hue *= 60.0f;
	if (hsl->hue < 0.0f) {
		hsl->hue += 360.0f;
	}
}

void RgbToHsv(const rgb_t *rgb, hsv_t *hsv)
{
	float min, max, delta;

	hsv->alpha = rgb->alpha;
	min = rgb->red < rgb->green ? rgb->red : rgb->green;
	min = min < rgb->blue ? min : rgb->blue;

	max = rgb->red > rgb->green ? rgb->red : rgb->green;
	max = max > rgb->blue ? max : rgb->blue;

	hsv->value = max / 255.0f;
	delta = max - min;
	if (delta <= EPSILON) {
		hsv->saturation = 0.0f;
		hsv->hue = 0.0f;
		return;
	}

	if (max > 0.0f) {
		hsv->saturation = delta / max;
	} else {
		hsv->saturation = 0.0f;
		hsv->hue = 0.0f;
		return;
	}

	if (rgb->red == max) {
		hsv->hue = (rgb->green - rgb->blue) / delta;
	} else if(rgb->green == max) {
		hsv->hue = 2.0f + (rgb->blue - rgb->red) / delta;
	} else {
		hsv->hue = 4.0f + (rgb->red - rgb->green) / delta;
	}

	hsv->hue *= 60.0f;
	if (hsv->hue < 0.0f) {
		hsv->hue += 360.0f;
	}
}

void HsvToRgb(const hsv_t *hsv, rgb_t *rgb)
{
	float hh, p, q, t, ff;
	int i;

	rgb->alpha = hsv->alpha;
	if (hsv->saturation <= EPSILON) {
		rgb->red = hsv->value * 255.0f;
		rgb->green = hsv->value * 255.0f;
		rgb->blue = hsv->value * 255.0f;
		return;
	}

	hh = hsv->hue;
	if(hh >= 360.0f) {
		hh = 0.0f;
	}
	hh /= 60.0f;
	i = (int) hh;
	ff = hh - i;
	p = hsv->value * (1.0f - hsv->saturation);
	q = hsv->value * (1.0f - hsv->saturation * ff);
	t = hsv->value * (1.0f - hsv->saturation * (1.0f - ff));

	switch (i) {
	case 0:
		rgb->red = hsv->value;
		rgb->green = t;
		rgb->blue = p;
		break;
	case 1:
		rgb->red = q;
		rgb->green = hsv->value;
		rgb->blue = p;
		break;
	case 2:
		rgb->red = p;
		rgb->green = hsv->value;
		rgb->blue = t;
		break;
	case 3:
		rgb->red = p;
		rgb->green = q;
		rgb->blue = hsv->value;
		break;
	case 4:
		rgb->red = t;
		rgb->green = p;
		rgb->blue = hsv->value;
		break;
	case 5:
	default:
		rgb->red = hsv->value;
		rgb->green = p;
		rgb->blue = q;
		break;
	}

	rgb->red *= 255.f;
	rgb->green *= 255.0f;
	rgb->blue *= 255.0f;
}

void HsvToHsl(const hsv_t *hsv, hsl_t *hsl)
{
	hsl->alpha = hsv->alpha;
	hsl->hue = hsv->hue;
	hsl->lightness = (2.0f - hsv->saturation) * hsv->value / 2.0f;

	if (hsl->lightness == 0.0f || hsl->lightness == 1.0f) {
		hsl->saturation = 0.0f;
	} else if (hsl->lightness < 0.5f) {
		hsl->saturation = hsv->saturation * hsv->value /
			(hsl->lightness * 2.0f);
	} else {
		hsl->saturation = hsv->saturation * hsv->value /
			(2.0f - hsl->lightness * 2.0f);
	}
}

void HslToHsv(const hsl_t *hsl, hsv_t *hsv)
{
	hsv->alpha = hsl->alpha;
	hsv->hue = hsl->hue;
	if (hsl->lightness <= 0.5f) {
		hsv->saturation = 2.0f * hsl->saturation *
			hsl->lightness / (1.0f + hsl->lightness);
	} else {
		hsv->saturation = 2.0f * hsl->saturation *
			(1.0f - hsl->lightness) / (2.0f - hsl->lightness);
	}
	hsv->value = (hsl->lightness + hsl->saturation *
			fmin(hsl->lightness, 1.0f - hsl->lightness)) / 2.0f;
}

void HslToRgb(const hsl_t *hsl, rgb_t *rgb)
{
	float c, x, m;
	float r, g, b;

	rgb->alpha = hsl->alpha;
	c = (1.0f - fabs(2.0f * hsl->lightness - 1.0f)) * hsl->saturation;
	x = c * (1.0f - fabs(fmod(hsl->hue / 60.0f, 2.0f) - 1.0f));
	m = hsl->lightness - c / 2.0f;

	if (hsl->hue >= 0.0f && hsl->hue < 60.0f) {
		r = c;
		g = x;
		b = 0.0f;
	} else if (hsl->hue >= 60.0f && hsl->hue < 120.0f) {
		r = x;
		g = c;
		b = 0.0f;
	} else if (hsl->hue >= 120.0f && hsl->hue < 180.0f) {
		r = 0.0f;
		g = c;
		b = x;
	} else if (hsl->hue >= 180.0f && hsl->hue < 240.0f) {
		r = 0.0f;
		g = x;
		b = c;
	} else if (hsl->hue >= 240.0f && hsl->hue < 300.0f) {
		r = x;
		g = 0.0f;
		b = c;
	} else {
		r = c;
		g = 0.0f;
		b = x;
	}

	rgb->red = (r + m) * 255.0f;
	rgb->green = (g + m) * 255.0f;
	rgb->blue = (b + m) * 255.0f;
}

int renderer_SetDrawColor(Renderer *renderer, Uint32 color)
{
	return SDL_SetRenderDrawColor(renderer, (color >> 16) & 0xff,
			(color >> 8) & 0xff, color & 0xff, 255);
}

int renderer_SetDrawColorRGB(Renderer *renderer, Uint8 a,
		Uint8 r, Uint8 g, Uint8 b)
{
	return SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

int renderer_DrawRect(Renderer *renderer, Rect *rect)
{
	return SDL_RenderDrawRect(renderer, rect);
}

int renderer_FillRect(Renderer *renderer, Rect *rect)
{
	return SDL_RenderFillRect(renderer, rect);
}

int renderer_DrawLine(Renderer *renderer, Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2)
{
	return SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

int renderer_DrawEllipse(Renderer *renderer, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
	int result;
	Sint32 ix, iy;
	Sint32 l, i, j, k;
	Sint32 ol, oi, oj, ok;
	Sint32 xml, xpl, ypk, ymk;
	Sint32 xmi, xpi, ymj, ypj;
	Sint32 xmj, xpj, ymi, ypi;
	Sint32 xmk, xpk, yml, ypl;

	if (w < 0 || h < 0) {
		return -1;
	}
	if (w == 0) {
		return SDL_RenderDrawLine(renderer, x, y, x, y + h);
	}
	if (h == 0) {
		return SDL_RenderDrawLine(renderer, x, y, x + w, y);
	}

	result = 0;
	ol = oi = oj = ok = INT32_MAX;

	/* Draw */
	if (w > h) {
		ix = 0;
		iy = 32 * w;

		do {
			l = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (h * h) / w;
			k = (i * h) / w;

			if ((ok != k && oj != k) || (oj != j && ok != j) || k != j) {
				xpl = x + l;
				xml = x - l;
				if (k > 0) {
					ypk = y + k;
					ymk = y - k;
					result |= SDL_RenderDrawPoint(renderer, xml, ypk);
					result |= SDL_RenderDrawPoint(renderer, xpl, ypk);
					result |= SDL_RenderDrawPoint(renderer, xml, ymk);
					result |= SDL_RenderDrawPoint(renderer, xpl, ymk);
				} else {
					result |= SDL_RenderDrawPoint(renderer, xml, y);
					result |= SDL_RenderDrawPoint(renderer, xpl, y);
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

			ix = ix + iy / w / 2;
			iy = iy - ix / w / 2;

		} while (i > l);
	} else {
		ix = 0;
		iy = 32 * h;

		do {
			l = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (l * w) / h;
			k = (i * w) / h;

			if (((oi != i) && (ol != i)) || ((ol != l) && (oi != l) && (i != l))) {
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
					ypl = y + l;
					yml = y - l;
					result |= SDL_RenderDrawPoint(renderer, xmk, ypl);
					result |= SDL_RenderDrawPoint(renderer, xpk, ypl);
					result |= SDL_RenderDrawPoint(renderer, xmk, yml);
					result |= SDL_RenderDrawPoint(renderer, xpk, yml);
				} else {
					result |= SDL_RenderDrawPoint(renderer, xmk, y);
					result |= SDL_RenderDrawPoint(renderer, xpk, y);
				}
				ol = l;
			}

			ix = ix + iy / h / 2;
			iy = iy - ix / h / 2;

		} while (i > l);
	}
	return result;
}

int renderer_FillEllipse(SDL_Renderer *renderer, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
	int result;
	Sint32 ix, iy;
	Sint32 l, i, j, k;
	Sint32 ol, oi, oj, ok;
	Sint32 xml, xpl;
	Sint32 xmi, xpi;
	Sint32 xmj, xpj;
	Sint32 xmk, xpk;

	if (w < 0 || h < 0) {
		return -1;
	}
	if (w == 0) {
		return SDL_RenderDrawLine(renderer, x, y, x, y + h);
	}
	if (h == 0) {
		return SDL_RenderDrawLine(renderer, x, y, x + w, y);
	}

	result = 0;

	ol = oi = oj = ok = 0xFFFF;

	/* Draw */
	if (w > h) {
		ix = 0;
		iy = 32 * w;

		do {
			l = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (l * h) / w;
			k = (i * h) / w;

			if ((ok != k) && (oj != k)) {
				xpl = x + l;
				xml = x - l;
				if (k > 0) {
					result |= SDL_RenderDrawLine(renderer, xml, y + k, xpl, y + k);
					result |= SDL_RenderDrawLine(renderer, xml, y - k, xpl, y - k);
				} else {
					result |= SDL_RenderDrawLine(renderer, xml, y, xpl, y);
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

			ix = ix + iy / w / 2;
			iy = iy - ix / w / 2;

		} while (i > l);
	} else {
		ix = 0;
		iy = 32 * h;

		do {
			l = (ix + 32) >> 6;
			i = (iy + 32) >> 6;
			j = (l * w) / h;
			k = (i * w) / h;

			if ((oi != i) && (ol != i)) {
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
			if ((ol != l) && (oi != l) && (i != l)) {
				xmk = x - k;
				xpk = x + k;
				if (l > 0) {
					result |= SDL_RenderDrawLine(renderer, xmk, y + l, xpk, y + l);
					result |= SDL_RenderDrawLine(renderer, xmk, y - l, xpk, y - l);
				} else {
					result |= SDL_RenderDrawLine(renderer, xmk, y, xpk, y);
				}
				ol = l;
			}

			ix = ix + iy / h / 2;
			iy = iy - ix / h / 2;

		} while (i > l);
	}

	return result;
}

struct font *cached_fonts;

Uint32 num_fonts;
Uint32 cur_font;

float tab_multiplier = 4.0f;

static Uint32 AddFont(Font *font)
{
	Uint32 iFont;
	const char *name, *otherName;
	int height, otherHeight;
	Font *other;
	struct font *newFonts;

	name = TTF_FontFaceFamilyName(font);
	height = TTF_FontHeight(font);
	for (iFont = 0; iFont < num_fonts; iFont++) {
		other = cached_fonts[iFont].font;
		otherHeight = TTF_FontHeight(other);
		otherName = TTF_FontFaceFamilyName(other);
		if (height == otherHeight && strcmp(name, otherName) == 0) {
			break;
		}
	}

	if (iFont == num_fonts) {
		newFonts = union_Realloc(union_Default(), cached_fonts,
				sizeof(*newFonts) * (num_fonts + 1));
		if (newFonts == NULL) {
			return UINT32_MAX;
		}
		cached_fonts = newFonts;
		cached_fonts[num_fonts].font = font;
		cached_fonts[num_fonts].cachedWords = NULL;
		cached_fonts[num_fonts].numCachedWords = 0;
		num_fonts++;
	}
	return iFont;
}

int renderer_SetFont(Font *font)
{
	Uint32 iFont;

	iFont = AddFont(font);
	if (iFont == UINT32_MAX) {
		return -1;
	}
	cur_font = iFont;
	return 0;
}

int renderer_SelectFont(Uint32 index)
{
	if (index >= num_fonts) {
		return -1;
	}
	cur_font = index;
	return 0;
}

void renderer_SetTabMultiplier(float multp)
{
	tab_multiplier = multp;
}

Font *renderer_CreateFont(const char *name, int size, Uint32 *pIndex)
{
	Font *font;
	Uint32 index;

	font = TTF_OpenFont(name, size);
	if (font == NULL) {
		return NULL;
	}
	index = AddFont(font);
	if (index == UINT32_MAX) {
		TTF_CloseFont(font);
		return NULL;
	}
	if (pIndex != NULL) {
		*pIndex = index;
	}
	return font;
}

Font *renderer_GetFont(Uint32 index)
{
	if (index >= num_fonts) {
		return NULL;
	}
	return cached_fonts[index].font;
}

static struct word *GetCachedWord(const char *data)
{
	struct font font;

	font = cached_fonts[cur_font];
	for (Uint32 i = 0; i < font.numCachedWords; i++) {
		struct word *const word = &font.cachedWords[i];
		if (strcmp(word->data, data) == 0) {
			return word;
		}
	}
	return NULL;
}

static struct word *CacheWord(Renderer *renderer, const char *data)
{
	const Color white = { 255, 255, 255, 255 };
	const Color black = { 0, 0, 0, 0 };

	struct font *font;

	struct word *newWords;

	SDL_Surface *textSurface;

	struct word word;

	Size len;

	font = &cached_fonts[cur_font];

	newWords = union_Realloc(union_Default(), font->cachedWords,
			sizeof(*newWords) * (font->numCachedWords + 1));
	if (newWords == NULL) {
		return NULL;
	}
	font->cachedWords = newWords;

	textSurface = TTF_RenderUTF8_LCD(font->font, data, white, black);
	if (textSurface == NULL) {
		return NULL;
	}

	word.texture = SDL_CreateTextureFromSurface(renderer, textSurface);
	if (word.texture == NULL) {
		SDL_FreeSurface(textSurface);
		return NULL;
	}

	word.width = textSurface->w;
	word.height = textSurface->h;

	SDL_FreeSurface(textSurface);

	len = strlen(data);
	word.data = union_Alloc(union_Default(), len + 1);
	if (word.data == NULL) {
		SDL_DestroyTexture(word.texture);
		return NULL;
	}
	memcpy(word.data, data, len);
	word.data[len] = '\0';

	font->cachedWords[font->numCachedWords] = word;
	return &font->cachedWords[font->numCachedWords++];
}

int renderer_DrawText(Renderer *renderer, const char *text, Sint32 x, Sint32 y)
{
	struct font *font;
	Uint8 r, g, b, a;
	int advance, tabWidth, height;
	Sint32 cx, cy;
	const char *end;
	char *data = NULL, *newData;
	struct word *word;
	Rect textRect;

	font = &cached_fonts[cur_font];

	SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);

	TTF_GlyphMetrics32(font->font, ' ', NULL, NULL, NULL, NULL, &advance);
	tabWidth = advance * tab_multiplier;
	height = TTF_FontHeight(font->font);

	cx = x;
	cy = y;
	while (*text != '\0') {
		while (*text <= ' ') {
			bool b = false;

			switch (*text) {
			case ' ':
				cx += advance;
				break;
			case '\t':
				cx += tabWidth - (x - cx) % tabWidth;
				break;
			case '\n':
				cx = x;
				cy += height;
				break;
			default:
				b = true;
			}
			if (b) {
				break;
			}
			text++;
		}

		if (*text == '\0') {
			break;
		}

		end = text;
		while ((Uint8) *end > ' ') {
			end++;
		}

		newData = union_Realloc(union_Default(), data, end - text + 1);
		if (newData == NULL) {
			union_Free(union_Default(), data);
			return -1;
		}
		data = newData;

		memcpy(data, text, end - text);
		data[end - text] = '\0';
		word = GetCachedWord(data);

		if (word == NULL) {
			word = CacheWord(renderer, data);
			if (word == NULL) {
				union_Free(union_Default(), data);
				return -1;
			}
		}

		textRect = (Rect) {
			cx, cy, word->width, word->height
		};
		SDL_SetTextureColorMod(word->texture, r, g, b);
		SDL_RenderCopy(renderer, word->texture, NULL, &textRect);
		cx += word->width;
		text = end;
	}
	if (data != NULL) {
		union_Free(union_Default(), data);
	}
	return 0;
}

