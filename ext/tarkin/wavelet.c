#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mem.h"
#include "wavelet.h"

/**
 *   (The transform code is in wavelet_xform.c)
 */


#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MAX3(a,b,c) (MAX(a,MAX(b,c)))



Wavelet3DBuf *
wavelet_3d_buf_new (uint32_t width, uint32_t height, uint32_t frames)
{
  Wavelet3DBuf *buf = (Wavelet3DBuf *) MALLOC (sizeof (Wavelet3DBuf));
  uint32_t _w = width;
  uint32_t _h = height;
  uint32_t _f = frames;
  int level;

  if (!buf)
    return NULL;

  buf->data = (TYPE *) MALLOC (width * height * frames * sizeof (TYPE));

  if (!buf->data) {
    wavelet_3d_buf_destroy (buf);
    return NULL;
  }

  buf->width = width;
  buf->height = height;
  buf->frames = frames;
  buf->scales = 1;

  while (_w > 1 || _h > 1 || _f > 1) {
    buf->scales++;
    _w = (_w + 1) / 2;
    _h = (_h + 1) / 2;
    _f = (_f + 1) / 2;
  }

  buf->w = (uint32_t *) MALLOC (buf->scales * sizeof (uint32_t));
  buf->h = (uint32_t *) MALLOC (buf->scales * sizeof (uint32_t));
  buf->f = (uint32_t *) MALLOC (buf->scales * sizeof (uint32_t));
  buf->offset = (uint32_t (*)[8]) MALLOC (8 * buf->scales * sizeof (uint32_t));

  buf->scratchbuf =
      (TYPE *) MALLOC (MAX3 (width, height, frames) * sizeof (TYPE));

  if (!buf->w || !buf->h || !buf->f || !buf->offset || !buf->scratchbuf) {
    wavelet_3d_buf_destroy (buf);
    return NULL;
  }

  buf->w[buf->scales - 1] = width;
  buf->h[buf->scales - 1] = height;
  buf->f[buf->scales - 1] = frames;

  for (level = buf->scales - 2; level >= 0; level--) {
    buf->w[level] = (buf->w[level + 1] + 1) / 2;
    buf->h[level] = (buf->h[level + 1] + 1) / 2;
    buf->f[level] = (buf->f[level + 1] + 1) / 2;
    buf->offset[level][0] = 0;
    buf->offset[level][1] = buf->w[level];
    buf->offset[level][2] = buf->h[level] * width;
    buf->offset[level][3] = buf->f[level] * width * height;
    buf->offset[level][4] = buf->offset[level][2] + buf->w[level];
    buf->offset[level][5] = buf->offset[level][3] + buf->w[level];
    buf->offset[level][6] = buf->offset[level][3] + buf->offset[level][2];
    buf->offset[level][7] = buf->offset[level][6] + buf->w[level];
  }

  return buf;
}


void
wavelet_3d_buf_destroy (Wavelet3DBuf * buf)
{
  if (buf) {
    if (buf->data)
      FREE (buf->data);
    if (buf->w)
      FREE (buf->w);
    if (buf->h)
      FREE (buf->h);
    if (buf->f)
      FREE (buf->f);
    if (buf->offset)
      FREE (buf->offset);
    if (buf->scratchbuf)
      FREE (buf->scratchbuf);
    FREE (buf);
  }
}


#if defined(DBG_XFORM)

#include "pnm.h"

void
wavelet_3d_buf_dump (char *fmt,
    uint32_t first_frame_in_buf,
    uint32_t id, Wavelet3DBuf * buf, int16_t offset)
{
  char fname[256];
  uint32_t f;

  for (f = 0; f < buf->frames; f++) {
    snprintf (fname, 256, fmt, id, first_frame_in_buf + f);

    write_pgm16 (fname, buf->data + f * buf->width * buf->height,
        buf->width, buf->height, offset);
  }
}
#endif
