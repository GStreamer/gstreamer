#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mem.h"
#include <assert.h>
#include "wavelet.h"



static void
fwd_analyze_1 (const TYPE * x, TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  for (i = 0; i < k; i++)
    d[i] = x[(2 * i + 1) * stride] - x[2 * i * stride];
}


static void
fwd_synthesize_1 (const TYPE * x, TYPE * s, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  for (i = 0; i < k; i++)
    s[i * stride] = x[2 * i * stride] + d[i] / 2;
  if (n & 1)
    s[k * stride] = x[2 * k * stride] + d[k - 1] / 2;
}


static void
inv_analyze_1 (TYPE * x, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  for (i = 0; i < k; i++)
    x[(2 * i + 1) * stride] = d[i] + x[2 * i * stride];
}


static void
inv_synthesize_1 (TYPE * x, const TYPE * s, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  for (i = 0; i < k; i++)
    x[2 * i * stride] = s[i] - d[i] / 2;
  if (n & 1)
    x[2 * k * stride] = s[k] - d[k - 1] / 2;
}



static void
fwd_analyze_2 (const TYPE * x, TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  if (n & 1) {
    for (i = 0; i < k; i++)
      d[i] =
	  x[(2 * i + 1) * stride] - (x[2 * i * stride] + x[(2 * i +
		  2) * stride]) / 2;
  } else {
    for (i = 0; i < k - 1; i++)
      d[i] =
	  x[(2 * i + 1) * stride] - (x[2 * i * stride] + x[(2 * i +
		  2) * stride]) / 2;
    d[k - 1] = x[(n - 1) * stride] - x[(n - 2) * stride];
  }
}



static void
fwd_synthesize_2 (const TYPE * x, TYPE * s, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  s[0] = x[0] + d[1] / 2;
  for (i = 1; i < k; i++)
    s[i * stride] = x[2 * i * stride] + (d[i - 1] + d[i]) / 4;
  if (n & 1)
    s[k * stride] = x[2 * k * stride] + d[k - 1] / 2;
}


static inline void
inv_analyze_2 (TYPE * x, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  if (n & 1) {
    for (i = 0; i < k; i++)
      x[(2 * i + 1) * stride] =
	  d[i] + (x[2 * i * stride] + x[(2 * i + 2) * stride]) / 2;
  } else {
    for (i = 0; i < k - 1; i++)
      x[(2 * i + 1) * stride] =
	  d[i] + (x[2 * i * stride] + x[(2 * i + 2) * stride]) / 2;
    x[(n - 1) * stride] = d[k - 1] + x[(n - 2) * stride];
  }
}


static inline void
inv_synthesize_2 (TYPE * x, const TYPE * s, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  x[0] = s[0] - d[1] / 2;
  for (i = 1; i < k; i++)
    x[2 * i * stride] = s[i] - (d[i - 1] + d[i]) / 4;
  if (n & 1)
    x[2 * k * stride] = s[k] - d[k - 1] / 2;
}



static void
fwd_analyze_4 (const TYPE * x, TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  d[0] = x[stride] - (x[0] + x[2 * stride]) / 2;

  if (n & 1) {
    for (i = 1; i < k - 1; i++)
      d[i] = x[(2 * i + 1) * stride]
	  - ((uint32_t) 9 * (x[2 * i * stride] + x[(2 * i + 2) * stride])
	  - (x[(2 * i - 2) * stride] + x[(2 * i + 4) * stride])) / 16;
    if (k > 1)
      d[k - 1] =
	  x[(2 * k - 1) * stride] - (x[(2 * k - 2) * stride] +
	  x[2 * k * stride]) / 2;
  } else {
    for (i = 1; i < k - 2; i++)
      d[i] = x[(2 * i + 1) * stride]
	  - ((uint32_t) 9 * (x[2 * i * stride] + x[(2 * i + 2) * stride])
	  - (x[(2 * i - 2) * stride] + x[(2 * i + 4) * stride])) / 16;
    if (k > 2)
      d[k - 2] = x[(2 * k - 3) * stride] - (x[(2 * k - 4) * stride]
	  + x[(2 * k - 2) * stride]) / 2;
    if (k > 1)
      d[k - 1] = x[(n - 1) * stride] - x[(n - 2) * stride];
  }
}


static void
fwd_synthesize_4 (const TYPE * x, TYPE * s, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  s[0] = x[0] + d[1] / 2;
  if (k > 1)
    s[stride] = x[2 * stride] + (d[0] + d[1]) / 4;
  for (i = 2; i < k - 1; i++)
    s[i * stride] = x[2 * i * stride]
	+ ((uint32_t) 9 * (d[i - 1] + d[i]) - (d[i - 2] + d[i + 1])) / 32;
  if (k > 2)
    s[(k - 1) * stride] = x[(2 * k - 2) * stride] + (d[k - 2] + d[k - 1]) / 4;
  if (n & 1)
    s[k * stride] = x[2 * k * stride] + d[k - 1] / 2;
}


static void
inv_analyze_4 (TYPE * x, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  x[stride] = d[0] + (x[0] + x[2 * stride]) / 2;

  if (n & 1) {
    for (i = 1; i < k - 1; i++)
      x[(2 * i + 1) * stride] = d[i]
	  + ((uint32_t) 9 * (x[2 * i * stride] + x[(2 * i + 2) * stride])
	  - (x[(2 * i - 2) * stride] + x[(2 * i + 4) * stride])) / 16;
    if (k > 1)
      x[(2 * k - 1) * stride] =
	  d[k - 1] + (x[(2 * k - 2) * stride] + x[2 * k * stride]) / 2;
  } else {
    for (i = 1; i < k - 2; i++)
      x[(2 * i + 1) * stride] = d[i]
	  + (9 * (x[2 * i * stride] + x[(2 * i + 2) * stride])
	  - (x[(2 * i - 2) * stride] + x[(2 * i + 4) * stride])) / 16;
    if (k > 2)
      x[(2 * k - 3) * stride] = d[k - 2] + (x[(2 * k - 4) * stride]
	  + x[(2 * k - 2) * stride]) / 2;
    if (k > 1)
      x[(n - 1) * stride] = d[k - 1] + x[(n - 2) * stride];
  }
}


static void
inv_synthesize_4 (TYPE * x, const TYPE * s, const TYPE * d, int stride, int n)
{
  int i, k = n / 2;

  x[0] = s[0] - d[1] / 2;
  if (k > 1)
    x[2 * stride] = s[1] - (d[0] + d[1]) / 4;
  for (i = 2; i < k - 1; i++)
    x[2 * i * stride] = s[i] - ((uint32_t) 9 * (d[i - 1] + d[i])
	- (d[i - 2] + d[i + 1])) / 32;
  if (k > 2)
    x[(2 * k - 2) * stride] = s[k - 1] - (d[k - 2] + d[k - 1]) / 4;
  if (n & 1)
    x[2 * k * stride] = s[k] - d[k - 1] / 2;
}


static inline void
copyback_d (TYPE * x, const TYPE * d, int stride, int n)
{
  int i, j, k = n / 2;

  for (i = n - k, j = 0; i < n; i++, j++)
    x[i * stride] = d[j];
}


static inline void
copy_s_d (const TYPE * x, TYPE * s_d, int stride, int n)
{
  int i;

  for (i = 0; i < n; i++)
    s_d[i] = x[i * stride];
}



typedef
    void (*FwdSFnc) (const TYPE * x, TYPE * s, const TYPE * d, int stride,
    int n);

typedef void (*FwdAFnc) (const TYPE * x, TYPE * d, int stride, int n);

typedef
    void (*InvSFnc) (TYPE * x, const TYPE * s, const TYPE * d, int stride,
    int n);

typedef void (*InvAFnc) (TYPE * x, const TYPE * d, int stride, int n);



static FwdSFnc fwd_synthesize[] = { NULL, fwd_synthesize_1, fwd_synthesize_2,
  NULL, fwd_synthesize_4
};

static FwdAFnc fwd_analyze[] = { NULL, fwd_analyze_1, fwd_analyze_2,
  NULL, fwd_analyze_4
};

static InvSFnc inv_synthesize[] = { NULL, inv_synthesize_1, inv_synthesize_2,
  NULL, inv_synthesize_4
};

static InvAFnc inv_analyze[] = { NULL, inv_analyze_1, inv_analyze_2,
  NULL, inv_analyze_4
};



static inline void
fwd_xform (TYPE * scratchbuf, TYPE * data, int stride, int n,
    int a_moments, int s_moments)
{
  TYPE *x = data;
  TYPE *d = scratchbuf;
  TYPE *s = data;

  assert (a_moments == 1 || a_moments == 2 || a_moments == 4);
  assert (s_moments == 1 || s_moments == 2 || s_moments == 4);

  /*  XXX FIXME: Ugly hack to work around  */
  /*             the short-row bug in high */
  /*             order xform functions     */
  if (n < 9)
    a_moments = s_moments = 2;
  if (n < 5)
    a_moments = s_moments = 1;

  fwd_analyze[a_moments] (x, d, stride, n);
  fwd_synthesize[s_moments] (x, s, d, stride, n);
  copyback_d (x, d, stride, n);
}


static inline void
inv_xform (TYPE * scratchbuf, TYPE * data, int stride, int n,
    int a_moments, int s_moments)
{
  int k = n / 2;
  TYPE *x = data;
  TYPE *s = scratchbuf;
  TYPE *d = scratchbuf + n - k;

  assert (a_moments == 1 || a_moments == 2 || a_moments == 4);
  assert (s_moments == 1 || s_moments == 2 || s_moments == 4);

  /*  XXX FIXME: Ugly hack to work around  */
  /*             the short-row bug in high */
  /*             order xform functions     */
  if (n < 9)
    a_moments = s_moments = 2;
  if (n < 5)
    a_moments = s_moments = 1;

  copy_s_d (data, scratchbuf, stride, n);
  inv_synthesize[s_moments] (x, s, d, stride, n);
  inv_analyze[a_moments] (x, d, stride, n);
}



void
wavelet_3d_buf_fwd_xform (Wavelet3DBuf * buf, int a_moments, int s_moments)
{
  int level;

  for (level = buf->scales - 1; level > 0; level--) {
    uint32_t w = buf->w[level];
    uint32_t h = buf->h[level];
    uint32_t f = buf->f[level];

    if (w > 1) {
      int row, frame;

      for (frame = 0; frame < f; frame++) {
	for (row = 0; row < h; row++) {
	  TYPE *data = buf->data + (frame * buf->height + row) * buf->width;

	  fwd_xform (buf->scratchbuf, data, 1, w, a_moments, s_moments);
	}
      }
    }

    if (h > 1) {
      int col, frame;

      for (frame = 0; frame < f; frame++) {
	for (col = 0; col < w; col++) {
	  TYPE *data = buf->data + frame * buf->width * buf->height + col;

	  fwd_xform (buf->scratchbuf, data, buf->width, h,
	      a_moments, s_moments);
	}
      }
    }

    if (f > 1) {
      int i, j;

      for (j = 0; j < h; j++) {
	for (i = 0; i < w; i++) {
	  TYPE *data = buf->data + j * buf->width + i;

	  fwd_xform (buf->scratchbuf, data, buf->width * buf->height, f,
	      a_moments, s_moments);
	}
      }
    }
  }
}


void
wavelet_3d_buf_inv_xform (Wavelet3DBuf * buf, int a_moments, int s_moments)
{
  int level;

  for (level = 1; level < buf->scales; level++) {
    uint32_t w = buf->w[level];
    uint32_t h = buf->h[level];
    uint32_t f = buf->f[level];

    if (f > 1) {
      int i, j;

      for (j = 0; j < h; j++) {
	for (i = 0; i < w; i++) {
	  TYPE *data = buf->data + j * buf->width + i;

	  inv_xform (buf->scratchbuf, data, buf->width * buf->height, f,
	      a_moments, s_moments);
	}
      }
    }

    if (h > 1) {
      int col, frame;

      for (frame = 0; frame < f; frame++) {
	for (col = 0; col < w; col++) {
	  TYPE *data = buf->data + frame * buf->width * buf->height + col;

	  inv_xform (buf->scratchbuf, data, buf->width, h,
	      a_moments, s_moments);
	}
      }
    }

    if (w > 1) {
      int row, frame;

      for (frame = 0; frame < f; frame++) {
	for (row = 0; row < h; row++) {
	  TYPE *data = buf->data + (frame * buf->height + row) * buf->width;

	  inv_xform (buf->scratchbuf, data, 1, w, a_moments, s_moments);
	}
      }
    }
  }
}
