
#ifndef __YUV_H
#define __YUV_H

#include <stdint.h>
#include "wavelet.h"

extern void rgb24_to_yuv (uint8_t *rgb, Wavelet3DBuf *yuv [], uint32_t frame);
extern void yuv_to_rgb24 (Wavelet3DBuf *yuv [], uint8_t *rgb, uint32_t frame);

extern void rgb32_to_yuv (uint8_t *rgb, Wavelet3DBuf *yuv [], uint32_t frame);
extern void yuv_to_rgb32 (Wavelet3DBuf *yuv [], uint8_t *rgb, uint32_t frame);

extern void rgba_to_yuv (uint8_t *rgba, Wavelet3DBuf *yuva [], uint32_t frame);
extern void yuv_to_rgba (Wavelet3DBuf *yuva [], uint8_t *rgba, uint32_t frame);

extern void grayscale_to_y (uint8_t *rgba, Wavelet3DBuf *y [], uint32_t frame);
extern void y_to_grayscale (Wavelet3DBuf *y [], uint8_t *rgba, uint32_t frame);

#endif

