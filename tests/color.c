#include "test.h"

void rgb_print(const rgb_t *rgb)
{
	printf("rgb(%f, %f, %f)\n", rgb->red, rgb->green, rgb->blue);
}

void hsl_print(const hsl_t *hsl)
{
	printf("hsl(%f, %f, %f)\n", hsl->hue, hsl->saturation, hsl->lightness);
}

void hsv_print(const hsv_t *hsv)
{
	printf("hsv(%f, %f, %f)\n", hsv->hue, hsv->saturation, hsv->value);
}

int main(void)
{
	rgb_t rgb;
	hsv_t hsv;
	hsl_t hsl;

	rgb.alpha = 255;
	rgb.red = 255;
	rgb.green = 0;
	rgb.blue = 128;

	RgbToHsv(&rgb, &hsv);

	hsv_print(&hsv);

	HsvToRgb(&hsv, &rgb);

	rgb_print(&rgb);

	HsvToHsl(&hsv, &hsl);

	hsl_print(&hsl);

	HslToHsv(&hsl, &hsv);

	hsv_print(&hsv);

	HslToRgb(&hsl, &rgb);

	rgb_print(&rgb);

	return 0;
}
