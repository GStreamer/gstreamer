#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "yuv.h"

/*#define TARKIN_YUV_EXACT*/
/*#define TARKIN_YUV_LXY*/


static inline uint8_t
CLAMP (int16_t x)
{
  return ((x > 255) ? 255 : (x < 0) ? 0 : x);
}



void
rgb24_to_yuv (uint8_t * rgb, Wavelet3DBuf * yuv[], uint32_t frame)
{
  int count = yuv[0]->width * yuv[0]->height;
  int16_t *y = yuv[0]->data + frame * count;
  int16_t *u = yuv[1]->data + frame * count;
  int16_t *v = yuv[2]->data + frame * count;
  int i;


#if defined(TARKIN_YUV_EXACT)
  for (i = 0; i < count; i++, rgb += 3) {
    y[i] = ((int16_t) 77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) / 256;
    u[i] = ((int16_t) - 44 * rgb[0] - 87 * rgb[1] + 131 * rgb[2]) / 256;
    v[i] = ((int16_t) 131 * rgb[0] - 110 * rgb[1] - 21 * rgb[2]) / 256;
  }
#elif defined(TARKIN_YUV_LXY)
  for (i = 0; i < count; i++, rgb += 3) {
    y[i] = ((int16_t) 54 * rgb[0] + 182 * rgb[1] + 18 * rgb[2]) / 256;
    u[i] = rgb[0] - y[i];
    v[i] = rgb[2] - y[i];
  }
#else
  for (i = 0; i < count; i++, rgb += 3) {
    v[i] = rgb[0] - rgb[1];
    u[i] = rgb[2] - rgb[1];
    y[i] = rgb[1] + (u[i] + v[i]) / 4;
  }
#endif
}


void
yuv_to_rgb24 (Wavelet3DBuf * yuv[], uint8_t * rgb, uint32_t frame)
{
  int count = yuv[0]->width * yuv[0]->height;
  int16_t *y = yuv[0]->data + frame * count;
  int16_t *u = yuv[1]->data + frame * count;
  int16_t *v = yuv[2]->data + frame * count;
  int i;

#if defined(TARKIN_YUV_EXACT)
  for (i = 0; i < count; i++, rgb += 3) {
    rgb[0] = CLAMP (y[i] + 1.371 * v[i]);
    rgb[1] = CLAMP (y[i] - 0.698 * v[i] - 0.336 * u[i]);
    rgb[2] = CLAMP (y[i] + 1.732 * u[i]);
  }
#elif defined(TARKIN_YUV_LXY)
  for (i = 0; i < count; i++, rgb += 3) {
    rgb[1] = CLAMP (y[i] - (76 * u[i] - 26 * v[i]) / 256);
    rgb[0] = CLAMP (y[i] + u[i]);
    rgb[2] = CLAMP (y[i] + v[i]);
  }
#else
  for (i = 0; i < count; i++, rgb += 3) {
    rgb[1] = CLAMP (y[i] - (u[i] + v[i]) / 4);
    rgb[2] = CLAMP (u[i] + rgb[1]);
    rgb[0] = CLAMP (v[i] + rgb[1]);
  }
#endif
}


void
rgb32_to_yuv (uint8_t * rgb, Wavelet3DBuf * yuv[], uint32_t frame)
{
  int count = yuv[0]->width * yuv[0]->height;
  int16_t *y = yuv[0]->data + frame * count;
  int16_t *u = yuv[1]->data + frame * count;
  int16_t *v = yuv[2]->data + frame * count;
  int i;

#if defined(TARKIN_YUV_EXACT)
  for (i = 0; i < count; i++, rgb += 4) {
    y[i] = ((int16_t) 77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) / 256;
    u[i] = ((int16_t) - 44 * rgb[0] - 87 * rgb[1] + 131 * rgb[2]) / 256;
    v[i] = ((int16_t) 131 * rgb[0] - 110 * rgb[1] - 21 * rgb[2]) / 256;
  }
#elif defined(TARKIN_YUV_LXY)
  for (i = 0; i < count; i++, rgb += 4) {
    y[i] = ((int16_t) 54 * rgb[0] + 182 * rgb[1] + 18 * rgb[2]) / 256;
    u[i] = rgb[0] - y[i];
    v[i] = rgb[2] - y[i];
  }
#else
  for (i = 0; i < count; i++, rgb += 4) {
    v[i] = rgb[0] - rgb[1];
    u[i] = rgb[2] - rgb[1];
    y[i] = rgb[1] + (u[i] + v[i]) / 4;
  }
#endif
}


void
yuv_to_rgb32 (Wavelet3DBuf * yuv[], uint8_t * rgb, uint32_t frame)
{
  int count = yuv[0]->width * yuv[0]->height;
  int16_t *y = yuv[0]->data + frame * count;
  int16_t *u = yuv[1]->data + frame * count;
  int16_t *v = yuv[2]->data + frame * count;
  int i;

#if defined(TARKIN_YUV_EXACT)
  for (i = 0; i < count; i++, rgb += 4) {
    rgb[0] = CLAMP (y[i] + 1.371 * v[i]);
    rgb[1] = CLAMP (y[i] - 0.698 * v[i] - 0.336 * u[i]);
    rgb[2] = CLAMP (y[i] + 1.732 * u[i]);
  }
#elif defined(TARKIN_YUV_LXY)
  for (i = 0; i < count; i++, rgb += 4) {
    rgb[1] = CLAMP (y[i] - (76 * u[i] - 26 * v[i]) / 256);
    rgb[0] = CLAMP (y[i] + u[i]);
    rgb[2] = CLAMP (y[i] + v[i]);
  }
#else
  for (i = 0; i < count; i++, rgb += 4) {
    rgb[1] = CLAMP (y[i] - (u[i] + v[i]) / 4);
    rgb[2] = CLAMP (u[i] + rgb[1]);
    rgb[0] = CLAMP (v[i] + rgb[1]);
  }
#endif
}


void
rgba_to_yuv (uint8_t * rgba, Wavelet3DBuf * yuva[], uint32_t frame)
{
  int count = yuva[0]->width * yuva[0]->height;
  int16_t *y = yuva[0]->data + frame * count;
  int16_t *u = yuva[1]->data + frame * count;
  int16_t *v = yuva[2]->data + frame * count;
  int16_t *a = yuva[3]->data + frame * count;
  int i;

#if defined(TARKIN_YUV_EXACT)
  for (i = 0; i < count; i++, rgba += 4) {
    y[i] = ((int16_t) 77 * rgba[0] + 150 * rgba[1] + 29 * rgba[2]) / 256;
    u[i] = ((int16_t) - 44 * rgba[0] - 87 * rgba[1] + 131 * rgba[2]) / 256;
    v[i] = ((int16_t) 131 * rgba[0] - 110 * rgba[1] - 21 * rgba[2]) / 256;
    a[i] = rgba[3];
  }
#elif defined(TARKIN_YUV_LXY)
  for (i = 0; i < count; i++, rgba += 4) {
    y[i] = ((int16_t) 54 * rgba[0] + 182 * rgba[1] + 18 * rgba[2]) / 256;
    u[i] = rgba[0] - y[i];
    v[i] = rgba[2] - y[i];
    a[i] = rgba[3];
  }
#else
  for (i = 0; i < count; i++, rgba += 4) {
    v[i] = rgba[0] - rgba[1];
    u[i] = rgba[2] - rgba[1];
    y[i] = rgba[1] + (u[i] + v[i]) / 4;
    a[i] = rgba[3];
  }
#endif
}


void
yuv_to_rgba (Wavelet3DBuf * yuva[], uint8_t * rgba, uint32_t frame)
{
  int count = yuva[0]->width * yuva[0]->height;
  int16_t *y = yuva[0]->data + frame * count;
  int16_t *u = yuva[1]->data + frame * count;
  int16_t *v = yuva[2]->data + frame * count;
  int16_t *a = yuva[3]->data + frame * count;
  int i;

#if defined(TARKIN_YUV_EXACT)
  for (i = 0; i < count; i++, rgba += 4) {
    rgba[0] = CLAMP (y[i] + 1.371 * v[i]);
    rgba[1] = CLAMP (y[i] - 0.698 * v[i] - 0.336 * u[i]);
    rgba[2] = CLAMP (y[i] + 1.732 * u[i]);
    rgba[3] = a[i];
  }
#elif defined(TARKIN_YUV_LXY)
  for (i = 0; i < count; i++, rgba += 4) {
    rgba[1] = CLAMP (y[i] - (76 * u[i] - 26 * v[i]) / 256);
    rgba[0] = CLAMP (y[i] + u[i]);
    rgba[2] = CLAMP (y[i] + v[i]);
    rgba[3] = a[i];
  }
#else
  for (i = 0; i < count; i++, rgba += 4) {
    rgba[1] = CLAMP (y[i] - (u[i] + v[i]) / 4);
    rgba[2] = CLAMP (u[i] + rgba[1]);
    rgba[0] = CLAMP (v[i] + rgba[1]);
    rgba[3] = a[i];
  }
#endif
}

void
grayscale_to_y (uint8_t * rgba, Wavelet3DBuf * y[], uint32_t frame)
{
  int count = y[0]->width * y[0]->height;
  int16_t *_y = y[0]->data + frame * count;
  int i;

  for (i = 0; i < count; i++)
    _y[i] = rgba[i];
}


void
y_to_grayscale (Wavelet3DBuf * y[], uint8_t * rgba, uint32_t frame)
{
  int count = y[0]->width * y[0]->height;
  int16_t *_y = y[0]->data + frame * count;
  int i;

  for (i = 0; i < count; i++)
    rgba[i] = CLAMP (_y[i]);
}
