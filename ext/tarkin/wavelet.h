#ifndef __WAVELET_H
#define __WAVELET_H

#include <stdint.h>


typedef struct {
   TYPE *data;
   uint32_t width;
   uint32_t height;
   uint32_t frames;
   uint32_t scales;
   uint32_t *w;
   uint32_t *h;
   uint32_t *f;
   uint32_t (*offset)[8];
   TYPE *scratchbuf;
} Wavelet3DBuf;


extern Wavelet3DBuf* wavelet_3d_buf_new (uint32_t width, uint32_t height,
                                         uint32_t frames);

extern void wavelet_3d_buf_destroy (Wavelet3DBuf* buf);

/**
 *  transform buf->data
 *  a_moments is the number of vanishing moments of the analyzing
 *  highpass filter,
 *  s_moments the one of the synthesizing lowpass filter.
 */
extern void wavelet_3d_buf_fwd_xform (Wavelet3DBuf* buf,
                                      int a_moments, int s_moments);
extern void wavelet_3d_buf_inv_xform (Wavelet3DBuf* buf,
                                      int a_moments, int s_moments);

extern int  wavelet_3d_buf_encode_coeff (const Wavelet3DBuf* buf,
                                         uint8_t *bitstream,
                                         uint32_t limit);

extern void wavelet_3d_buf_decode_coeff (Wavelet3DBuf* buf,
                                         uint8_t *bitstream,
                                         uint32_t limit);

#if defined(DBG_XFORM)
extern void wavelet_3d_buf_dump (char *fmt,
                                 uint32_t first_frame_in_buf,
                                 uint32_t id,
                                 Wavelet3DBuf* buf,
                                 int16_t offset);
#else
#define wavelet_3d_buf_dump(x...)
#endif

#endif
