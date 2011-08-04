
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define COG_ENABLE_UNSTABLE_API 1

#include <cog/cogvirtframe.h>
#include <cog/cog.h>
#include <string.h>
#include <math.h>
#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif
#include <gst/gst.h>

#include "gstcogorc.h"

extern gint8 cog_resample_table_4tap[256][4];

CogFrame *
cog_frame_new_virtual (CogMemoryDomain * domain, CogFrameFormat format,
    int width, int height)
{
  CogFrame *frame = cog_frame_new ();
  int bytes_pp;
  int h_shift, v_shift;
  int chroma_width;
  int chroma_height;
  int i;

  frame->format = format;
  frame->width = width;
  frame->height = height;
  frame->domain = domain;

  if (COG_FRAME_IS_PACKED (format)) {
    frame->components[0].format = format;
    frame->components[0].width = width;
    frame->components[0].height = height;
    if (format == COG_FRAME_FORMAT_AYUV) {
      frame->components[0].stride = width * 4;
    } else if (format == COG_FRAME_FORMAT_v216) {
      frame->components[0].stride = ROUND_UP_POW2 (width, 1) * 4;
    } else if (format == COG_FRAME_FORMAT_v210) {
      frame->components[0].stride = ((width + 47) / 48) * 128;
    } else {
      frame->components[0].stride = ROUND_UP_POW2 (width, 1) * 2;
    }
    frame->components[0].length = frame->components[0].stride * height;

    frame->components[0].data = frame->regions[0];
    frame->components[0].v_shift = 0;
    frame->components[0].h_shift = 0;

    frame->regions[0] =
        g_malloc (frame->components[0].stride * COG_FRAME_CACHE_SIZE);
    for (i = 0; i < COG_FRAME_CACHE_SIZE; i++) {
      frame->cached_lines[0][i] = 0;
    }
    frame->cache_offset[0] = 0;
    frame->is_virtual = TRUE;

    return frame;
  }

  switch (COG_FRAME_FORMAT_DEPTH (format)) {
    case COG_FRAME_FORMAT_DEPTH_U8:
      bytes_pp = 1;
      break;
    case COG_FRAME_FORMAT_DEPTH_S16:
      bytes_pp = 2;
      break;
    case COG_FRAME_FORMAT_DEPTH_S32:
      bytes_pp = 4;
      break;
    default:
      g_return_val_if_reached (NULL);
      bytes_pp = 0;
      break;
  }

  h_shift = COG_FRAME_FORMAT_H_SHIFT (format);
  v_shift = COG_FRAME_FORMAT_V_SHIFT (format);
  chroma_width = ROUND_UP_SHIFT (width, h_shift);
  chroma_height = ROUND_UP_SHIFT (height, v_shift);

  frame->components[0].format = format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_4 (width * bytes_pp);
  frame->components[0].length =
      frame->components[0].stride * frame->components[0].height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  frame->components[1].format = format;
  frame->components[1].width = chroma_width;
  frame->components[1].height = chroma_height;
  frame->components[1].stride = ROUND_UP_4 (chroma_width * bytes_pp);
  frame->components[1].length =
      frame->components[1].stride * frame->components[1].height;
  frame->components[1].v_shift = v_shift;
  frame->components[1].h_shift = h_shift;

  frame->components[2].format = format;
  frame->components[2].width = chroma_width;
  frame->components[2].height = chroma_height;
  frame->components[2].stride = ROUND_UP_4 (chroma_width * bytes_pp);
  frame->components[2].length =
      frame->components[2].stride * frame->components[2].height;
  frame->components[2].v_shift = v_shift;
  frame->components[2].h_shift = h_shift;

  for (i = 0; i < 3; i++) {
    CogFrameData *comp = &frame->components[i];
    int j;

    frame->regions[i] = g_malloc (comp->stride * COG_FRAME_CACHE_SIZE);
    for (j = 0; j < COG_FRAME_CACHE_SIZE; j++) {
      frame->cached_lines[i][j] = 0;
    }
    frame->cache_offset[i] = 0;
  }
  frame->is_virtual = TRUE;

  return frame;
}

void *
cog_virt_frame_get_line (CogFrame * frame, int component, int i)
{
  CogFrameData *comp = &frame->components[component];
  int j;

  g_return_val_if_fail (i >= 0, NULL);
  g_return_val_if_fail (i < comp->height, NULL);

  if (!frame->is_virtual) {
    return COG_FRAME_DATA_GET_LINE (&frame->components[component], i);
  }

  if (i < frame->cache_offset[component]) {
    if (i != 0) {
      g_warning ("cache failure: %d outside [%d,%d]", i,
          frame->cache_offset[component],
          frame->cache_offset[component] + COG_FRAME_CACHE_SIZE - 1);
    }

    frame->cache_offset[component] = i;
    for (j = 0; j < COG_FRAME_CACHE_SIZE; j++) {
      frame->cached_lines[component][j] = 0;
    }
  }

  while (i >= frame->cache_offset[component] + COG_FRAME_CACHE_SIZE) {
    j = frame->cache_offset[component] & (COG_FRAME_CACHE_SIZE - 1);
    frame->cached_lines[component][j] = 0;

    frame->cache_offset[component]++;
  }

  j = i & (COG_FRAME_CACHE_SIZE - 1);
  if (!frame->cached_lines[component][j]) {
    cog_virt_frame_render_line (frame,
        COG_OFFSET (frame->regions[component], comp->stride * j), component, i);
    frame->cached_lines[component][j] = 1;
  }

  return COG_OFFSET (frame->regions[component], comp->stride * j);
}

void
cog_virt_frame_render_line (CogFrame * frame, void *dest, int component, int i)
{
  frame->render_line (frame, dest, component, i);
}

static void
copy (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  src = cog_virt_frame_get_line (frame, component, i);
  switch (COG_FRAME_FORMAT_DEPTH (frame->format)) {
    case COG_FRAME_FORMAT_DEPTH_U8:
      orc_memcpy (dest, src, frame->components[component].width);
      break;
    case COG_FRAME_FORMAT_DEPTH_S16:
      orc_memcpy (dest, src, frame->components[component].width * 2);
      break;
    default:
      g_return_if_reached ();
      break;
  }
}

void
cog_virt_frame_render (CogFrame * frame, CogFrame * dest)
{
  int i, k;

  g_return_if_fail (frame->width == dest->width);
  g_return_if_fail (frame->height >= dest->height);

  if (frame->is_virtual) {
    for (k = 0; k < 3; k++) {
      CogFrameData *comp = dest->components + k;

      for (i = 0; i < dest->components[k].height; i++) {
        cog_virt_frame_render_line (frame,
            COG_FRAME_DATA_GET_LINE (comp, i), k, i);
      }
    }
  } else {
    for (k = 0; k < 3; k++) {
      CogFrameData *comp = dest->components + k;

      for (i = 0; i < dest->components[k].height; i++) {
        copy (frame, COG_FRAME_DATA_GET_LINE (comp, i), k, i);
      }
    }
  }
}

static void
cog_virt_frame_render_downsample_horiz_cosite_3tap (CogFrame * frame,
    void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int n_src;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
  n_src = frame->virt_frame1->components[component].width;

  cogorc_downsample_horiz_cosite_3tap (dest + 1,
      (uint16_t *) (src + 1), (uint16_t *) (src + 3),
      frame->components[component].width - 1);

  {
    int j;
    int x;

    j = 0;
    x = 1 * src[CLAMP (j * 2 - 1, 0, n_src - 1)];
    x += 2 * src[CLAMP (j * 2 + 0, 0, n_src - 1)];
    x += 1 * src[CLAMP (j * 2 + 1, 0, n_src - 1)];
    dest[j] = CLAMP ((x + 2) >> 2, 0, 255);

#if 0
    j = frame->components[component].width - 1;
    x = 1 * src[CLAMP (j * 2 - 1, 0, n_src - 1)];
    x += 2 * src[CLAMP (j * 2 + 0, 0, n_src - 1)];
    x += 1 * src[CLAMP (j * 2 + 1, 0, n_src - 1)];
    dest[j] = CLAMP ((x + 2) >> 2, 0, 255);
#endif
  }
}

static void
cog_virt_frame_render_downsample_horiz_halfsite (CogFrame * frame,
    void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int j;
  int n_src;
  int taps = 4;
  int k;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
  n_src = frame->virt_frame1->components[component].width;

  switch (taps) {
    case 4:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        x += 6 * src[CLAMP (j * 2 - 1, 0, n_src - 1)];
        x += 26 * src[CLAMP (j * 2 + 0, 0, n_src - 1)];
        x += 26 * src[CLAMP (j * 2 + 1, 0, n_src - 1)];
        x += 6 * src[CLAMP (j * 2 + 2, 0, n_src - 1)];
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    case 6:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        x += -3 * src[CLAMP (j * 2 - 2, 0, n_src - 1)];
        x += 8 * src[CLAMP (j * 2 - 1, 0, n_src - 1)];
        x += 27 * src[CLAMP (j * 2 + 0, 0, n_src - 1)];
        x += 27 * src[CLAMP (j * 2 + 1, 0, n_src - 1)];
        x += 8 * src[CLAMP (j * 2 + 2, 0, n_src - 1)];
        x += -3 * src[CLAMP (j * 2 + 3, 0, n_src - 1)];
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
    case 8:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        const int taps8[8] = { -2, -4, 9, 29, 29, 9, -4, -2 };
        for (k = 0; k < 8; k++) {
          x += taps8[k] * src[CLAMP (j * 2 - 3 + k, 0, n_src - 1)];
        }
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    case 10:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        const int taps10[10] = { 1, -2, -5, 9, 29, 29, 9, -5, -2, 1 };
        for (k = 0; k < 10; k++) {
          x += taps10[k] * src[CLAMP (j * 2 - 4 + k, 0, n_src - 1)];
        }
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    default:
      break;
  }
}

CogFrame *
cog_virt_frame_new_horiz_downsample (CogFrame * vf, int n_taps)
{
  CogFrame *virt_frame;

  virt_frame =
      cog_frame_new_virtual (NULL, vf->format, vf->width / 2, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->param1 = n_taps;
  switch (n_taps) {
    case 3:
      virt_frame->render_line =
          cog_virt_frame_render_downsample_horiz_cosite_3tap;
      break;
    case 4:
    case 6:
    case 8:
    case 10:
      virt_frame->render_line = cog_virt_frame_render_downsample_horiz_halfsite;
      break;
    default:
      g_return_val_if_reached (NULL);
  }

  return virt_frame;
}

static void
cog_virt_frame_render_downsample_vert_cosite (CogFrame * frame,
    void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  int n_src;

  n_src = frame->virt_frame1->components[component].height;
  src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 - 1, 0, n_src - 1));
  src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 + 0, 0, n_src - 1));
  src3 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 + 1, 0, n_src - 1));

  cogorc_downsample_vert_cosite_3tap (dest, src1, src2, src3,
      frame->components[component].width);
}

static void
cog_virt_frame_render_downsample_vert_halfsite_2tap (CogFrame * frame,
    void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  int n_src;

  n_src = frame->virt_frame1->components[component].height;
  src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2, 0, n_src - 1));
  src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 + 1, 0, n_src - 1));

  cogorc_downsample_vert_halfsite_2tap (dest, src1, src2,
      frame->components[component].width);
}

static void
cog_virt_frame_render_downsample_vert_halfsite_4tap (CogFrame * frame,
    void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  uint8_t *src4;
  int n_src;

  n_src = frame->virt_frame1->components[component].height;
  src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 - 1, 0, n_src - 1));
  src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2, 0, n_src - 1));
  src3 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 + 1, 0, n_src - 1));
  src4 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (i * 2 + 2, 0, n_src - 1));

  cogorc_downsample_vert_halfsite_4tap (dest, src1, src2, src3, src4,
      frame->components[component].width);
}


static void
cog_virt_frame_render_downsample_vert_halfsite (CogFrame * frame,
    void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src[10];
  int j;
  int n_src;
  int taps = frame->param1;
  int k;

  n_src = frame->virt_frame1->components[component].height;
  for (j = 0; j < taps; j++) {
    src[j] = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 - (taps - 2) / 2 + j, 0, n_src - 1));
  }

  switch (taps) {
    case 4:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        x += 6 * src[0][j];
        x += 26 * src[1][j];
        x += 26 * src[2][j];
        x += 6 * src[3][j];
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    case 6:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        x += -3 * src[0][j];
        x += 8 * src[1][j];
        x += 27 * src[2][j];
        x += 27 * src[3][j];
        x += 8 * src[4][j];
        x += -3 * src[5][j];
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    case 8:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        const int taps8[8] = { -2, -4, 9, 29, 29, 9, -4, -2 };
        for (k = 0; k < 8; k++) {
          x += taps8[k] * src[k][j];
        }
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    case 10:
      for (j = 0; j < frame->components[component].width; j++) {
        int x = 0;
        const int taps10[10] = { 1, -2, -5, 9, 29, 29, 9, -5, -2, 1 };
        for (k = 0; k < 10; k++) {
          x += taps10[k] * src[k][j];
        }
        dest[j] = CLAMP ((x + 32) >> 6, 0, 255);
      }
      break;
    default:
      g_return_if_reached ();
      break;
  }
}

CogFrame *
cog_virt_frame_new_vert_downsample (CogFrame * vf, int n_taps)
{
  CogFrame *virt_frame;

  virt_frame =
      cog_frame_new_virtual (NULL, vf->format, vf->width, vf->height / 2);
  virt_frame->virt_frame1 = vf;
  virt_frame->param1 = n_taps;
  switch (n_taps) {
    case 2:
      virt_frame->render_line =
          cog_virt_frame_render_downsample_vert_halfsite_2tap;
      break;
    case 3:
      virt_frame->render_line = cog_virt_frame_render_downsample_vert_cosite;
      break;
    case 4:
      virt_frame->render_line =
          cog_virt_frame_render_downsample_vert_halfsite_4tap;
      break;
    default:
      virt_frame->render_line = cog_virt_frame_render_downsample_vert_halfsite;
      break;
  }

  return virt_frame;
}

static void
cog_virt_frame_render_resample_vert_1tap (CogFrame * frame, void *_dest,
    int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  int n_src;
  int scale = frame->param1;
  int acc;
  int src_i;

  acc = scale * i;
  src_i = acc >> 8;
  /* x = acc & 0xff; */

  n_src = frame->virt_frame1->components[component].height;
  src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (src_i + 0, 0, n_src - 1));

  orc_memcpy (dest, src1, frame->components[component].width);
}

static void
cog_virt_frame_render_resample_vert_2tap (CogFrame * frame, void *_dest,
    int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  int n_src;
  int scale = frame->param1;
  int acc;
  int x;
  int src_i;

  acc = scale * i;
  src_i = acc >> 8;
  x = acc & 0xff;

  n_src = frame->virt_frame1->components[component].height;
  src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (src_i + 0, 0, n_src - 1));
  src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
      CLAMP (src_i + 1, 0, n_src - 1));

  if (x == 0) {
    memcpy (dest, src1, frame->components[component].width);
  } else {
    cogorc_combine2_u8 (dest, src1, src2,
        256 - x, x, frame->components[component].width);
  }
}

static void
cog_virt_frame_render_resample_vert_4tap (CogFrame * frame, void *_dest,
    int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  uint8_t *src4;
  int n_src;
  int scale = frame->param1;
  int acc;
  int x;
  int src_i;

  acc = scale * i;
  src_i = acc >> 8;
  x = acc & 0xff;

  n_src = frame->virt_frame1->components[component].height;
  if (src_i < 1 || src_i >= n_src - 3) {
    src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (src_i - 1, 0, n_src - 1));
    src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (src_i + 0, 0, n_src - 1));
    src3 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (src_i + 1, 0, n_src - 1));
    src4 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (src_i + 2, 0, n_src - 1));
  } else {
    src1 = cog_virt_frame_get_line (frame->virt_frame1, component, src_i - 1);
    src2 = cog_virt_frame_get_line (frame->virt_frame1, component, src_i + 0);
    src3 = cog_virt_frame_get_line (frame->virt_frame1, component, src_i + 1);
    src4 = cog_virt_frame_get_line (frame->virt_frame1, component, src_i + 2);
  }

  cogorc_combine4_u8 (dest, src1, src2, src3, src4,
      cog_resample_table_4tap[x][0],
      cog_resample_table_4tap[x][1],
      cog_resample_table_4tap[x][2],
      cog_resample_table_4tap[x][3], frame->components[component].width);
}

CogFrame *
cog_virt_frame_new_vert_resample (CogFrame * vf, int height, int n_taps)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, vf->format, vf->width, height);
  virt_frame->virt_frame1 = vf;
  if (n_taps == 1) {
    virt_frame->render_line = cog_virt_frame_render_resample_vert_1tap;
  } else if (n_taps == 2) {
    virt_frame->render_line = cog_virt_frame_render_resample_vert_2tap;
  } else {
    virt_frame->render_line = cog_virt_frame_render_resample_vert_4tap;
  }

  virt_frame->param1 = 256 * vf->height / height;

  return virt_frame;
}

static void
cog_virt_frame_render_resample_horiz_1tap (CogFrame * frame, void *_dest,
    int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int scale = frame->param1;

  /* n_src = frame->virt_frame1->components[component].width; */
  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

  cogorc_resample_horiz_1tap (dest, src, 0, scale,
      frame->components[component].width);
}

static void
cog_virt_frame_render_resample_horiz_2tap (CogFrame * frame, void *_dest,
    int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int scale = frame->param1;

  /* n_src = frame->virt_frame1->components[component].width; */
  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

  cogorc_resample_horiz_2tap (dest, src, 0, scale,
      frame->components[component].width);
}

static void
cog_virt_frame_render_resample_horiz_4tap (CogFrame * frame, void *_dest,
    int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int j;
  int n_src;
  int scale = frame->param1;
  int acc;

  n_src = frame->virt_frame1->components[component].width;
  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

  acc = 0;
  for (j = 0; j < 1; j++) {
    int src_i;
    int y;
    int z;

    src_i = acc >> 16;
    y = (acc >> 8) & 255;

    z = 32;
    z += cog_resample_table_4tap[y][0] * src[CLAMP (src_i - 1, 0, n_src - 1)];
    z += cog_resample_table_4tap[y][1] * src[CLAMP (src_i + 0, 0, n_src - 1)];
    z += cog_resample_table_4tap[y][2] * src[CLAMP (src_i + 1, 0, n_src - 1)];
    z += cog_resample_table_4tap[y][3] * src[CLAMP (src_i + 2, 0, n_src - 1)];
    z >>= 6;
    dest[j] = CLAMP (z, 0, 255);
    acc += scale;
  }
  for (; j < frame->components[component].width - 2; j++) {
    int src_i;
    int y;
    int z;

    src_i = acc >> 16;
    y = (acc >> 8) & 255;

    z = 32;
    z += cog_resample_table_4tap[y][0] * src[src_i - 1];
    z += cog_resample_table_4tap[y][1] * src[src_i + 0];
    z += cog_resample_table_4tap[y][2] * src[src_i + 1];
    z += cog_resample_table_4tap[y][3] * src[src_i + 2];
    z >>= 6;
    dest[j] = CLAMP (z, 0, 255);
    acc += scale;
  }
  for (; j < frame->components[component].width; j++) {
    int src_i;
    int y;
    int z;

    src_i = acc >> 16;
    y = (acc >> 8) & 255;

    z = 32;
    z += cog_resample_table_4tap[y][0] * src[CLAMP (src_i - 1, 0, n_src - 1)];
    z += cog_resample_table_4tap[y][1] * src[CLAMP (src_i + 0, 0, n_src - 1)];
    z += cog_resample_table_4tap[y][2] * src[CLAMP (src_i + 1, 0, n_src - 1)];
    z += cog_resample_table_4tap[y][3] * src[CLAMP (src_i + 2, 0, n_src - 1)];
    z >>= 6;
    dest[j] = CLAMP (z, 0, 255);
    acc += scale;
  }
}

CogFrame *
cog_virt_frame_new_horiz_resample (CogFrame * vf, int width, int n_taps)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, vf->format, width, vf->height);
  virt_frame->virt_frame1 = vf;
  if (n_taps == 1) {
    virt_frame->render_line = cog_virt_frame_render_resample_horiz_1tap;
  } else if (n_taps == 2) {
    virt_frame->render_line = cog_virt_frame_render_resample_horiz_2tap;
  } else {
    virt_frame->render_line = cog_virt_frame_render_resample_horiz_4tap;
  }

  virt_frame->param1 = 65536 * vf->width / width;

  return virt_frame;
}

static void
unpack_yuyv (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, 0, i);

  switch (component) {
    case 0:
      orc_unpack_yuyv_y (dest, (void *) src, frame->width);
      break;
    case 1:
      orc_unpack_yuyv_u (dest, (void *) src, frame->width / 2);
      break;
    case 2:
      orc_unpack_yuyv_v (dest, (void *) src, frame->width / 2);
      break;
  }
}

static void
unpack_uyvy (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, 0, i);

  switch (component) {
    case 0:
      orc_unpack_uyvy_y (dest, (void *) src, frame->width);
      break;
    case 1:
      cogorc_unpack_axyz_0 (dest, (void *) src, frame->width / 2);
      break;
    case 2:
      cogorc_unpack_axyz_2 (dest, (void *) src, frame->width / 2);
      break;
  }
}

static void
unpack_axyz (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint32_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, 0, i);

  switch ((frame->param1 >> (12 - component * 4)) & 0xf) {
    case 0:
      cogorc_unpack_axyz_0 (dest, src, frame->width);
      break;
    case 1:
      cogorc_unpack_axyz_1 (dest, src, frame->width);
      break;
    case 2:
      cogorc_unpack_axyz_2 (dest, src, frame->width);
      break;
    case 3:
      cogorc_unpack_axyz_3 (dest, src, frame->width);
      break;
  }
}

static void
unpack_v210 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int j;

  src = cog_virt_frame_get_line (frame->virt_frame1, 0, i);

#define READ_UINT32_LE(a) (((uint8_t *)(a))[0] | (((uint8_t *)(a))[1]<<8) | \
  (((uint8_t *)(a))[2]<<16) | (((uint8_t *)(a))[3]<<24))
  switch (component) {
    case 0:
      for (j = 0; j < frame->width / 6; j++) {
        dest[j * 6 + 0] =
            ((READ_UINT32_LE (src + j * 16 + 0) >> 10) & 0x3ff) >> 2;
        dest[j * 6 + 1] =
            ((READ_UINT32_LE (src + j * 16 + 4) >> 0) & 0x3ff) >> 2;
        dest[j * 6 + 2] =
            ((READ_UINT32_LE (src + j * 16 + 4) >> 20) & 0x3ff) >> 2;
        dest[j * 6 + 3] =
            ((READ_UINT32_LE (src + j * 16 + 8) >> 10) & 0x3ff) >> 2;
        dest[j * 6 + 4] =
            ((READ_UINT32_LE (src + j * 16 + 12) >> 0) & 0x3ff) >> 2;
        dest[j * 6 + 5] =
            ((READ_UINT32_LE (src + j * 16 + 12) >> 20) & 0x3ff) >> 2;
      }
      if (j * 6 + 0 < frame->width) {
        dest[j * 6 + 0] =
            ((READ_UINT32_LE (src + j * 16 + 0) >> 10) & 0x3ff) >> 2;
      }
      if (j * 6 + 1 < frame->width) {
        dest[j * 6 + 1] =
            ((READ_UINT32_LE (src + j * 16 + 4) >> 0) & 0x3ff) >> 2;
      }
      if (j * 6 + 2 < frame->width) {
        dest[j * 6 + 2] =
            ((READ_UINT32_LE (src + j * 16 + 4) >> 20) & 0x3ff) >> 2;
      }
      if (j * 6 + 3 < frame->width) {
        dest[j * 6 + 3] =
            ((READ_UINT32_LE (src + j * 16 + 8) >> 10) & 0x3ff) >> 2;
      }
      if (j * 6 + 4 < frame->width) {
        dest[j * 6 + 4] =
            ((READ_UINT32_LE (src + j * 16 + 12) >> 0) & 0x3ff) >> 2;
      }
      if (j * 6 + 5 < frame->width) {
        dest[j * 6 + 5] =
            ((READ_UINT32_LE (src + j * 16 + 12) >> 20) & 0x3ff) >> 2;
      }
      break;
    case 1:
      for (j = 0; j < frame->width / 6; j++) {
        dest[j * 3 + 0] =
            ((READ_UINT32_LE (src + j * 16 + 0) >> 0) & 0x3ff) >> 2;
        dest[j * 3 + 1] =
            ((READ_UINT32_LE (src + j * 16 + 4) >> 10) & 0x3ff) >> 2;
        dest[j * 3 + 2] =
            ((READ_UINT32_LE (src + j * 16 + 8) >> 20) & 0x3ff) >> 2;
      }
      if (j * 6 + 0 < frame->width) {
        dest[j * 3 + 0] =
            ((READ_UINT32_LE (src + j * 16 + 0) >> 0) & 0x3ff) >> 2;
      }
      if (j * 6 + 2 < frame->width) {
        dest[j * 3 + 1] =
            ((READ_UINT32_LE (src + j * 16 + 4) >> 10) & 0x3ff) >> 2;
      }
      if (j * 6 + 4 < frame->width) {
        dest[j * 3 + 2] =
            ((READ_UINT32_LE (src + j * 16 + 8) >> 20) & 0x3ff) >> 2;
      }
      break;
    case 2:
      for (j = 0; j < frame->width / 6; j++) {
        dest[j * 3 + 0] =
            ((READ_UINT32_LE (src + j * 16 + 0) >> 20) & 0x3ff) >> 2;
        dest[j * 3 + 1] =
            ((READ_UINT32_LE (src + j * 16 + 8) >> 0) & 0x3ff) >> 2;
        dest[j * 3 + 2] =
            ((READ_UINT32_LE (src + j * 16 + 12) >> 10) & 0x3ff) >> 2;
      }
      if (j * 6 + 0 < frame->width) {
        dest[j * 3 + 0] =
            ((READ_UINT32_LE (src + j * 16 + 0) >> 20) & 0x3ff) >> 2;
      }
      if (j * 6 + 2 < frame->width) {
        dest[j * 3 + 1] =
            ((READ_UINT32_LE (src + j * 16 + 8) >> 0) & 0x3ff) >> 2;
      }
      if (j * 6 + 4 < frame->width) {
        dest[j * 3 + 2] =
            ((READ_UINT32_LE (src + j * 16 + 12) >> 10) & 0x3ff) >> 2;
      }
      break;
  }
}

static void
unpack_v216 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int j;

  src = cog_virt_frame_get_line (frame->virt_frame1, 0, i);

  switch (component) {
    case 0:
      for (j = 0; j < frame->width; j++) {
        dest[j] = src[j * 4 + 2 + 1];
      }
      break;
    case 1:
      for (j = 0; j < frame->width / 2; j++) {
        dest[j] = src[j * 8 + 0 + 1];
      }
      break;
    case 2:
      for (j = 0; j < frame->width / 2; j++) {
        dest[j] = src[j * 8 + 4 + 1];
      }
  }
}

CogFrame *
cog_virt_frame_new_unpack (CogFrame * vf)
{
  CogFrame *virt_frame;
  CogFrameFormat format;
  CogFrameRenderFunc render_line;
  int param1 = 0;

  if (!COG_FRAME_IS_PACKED (vf->format))
    return vf;

  switch (vf->format) {
    case COG_FRAME_FORMAT_YUYV:
      format = COG_FRAME_FORMAT_U8_422;
      render_line = unpack_yuyv;
      break;
    case COG_FRAME_FORMAT_UYVY:
      format = COG_FRAME_FORMAT_U8_422;
      render_line = unpack_uyvy;
      break;
    case COG_FRAME_FORMAT_v210:
      format = COG_FRAME_FORMAT_U8_422;
      render_line = unpack_v210;
      break;
    case COG_FRAME_FORMAT_v216:
      format = COG_FRAME_FORMAT_U8_422;
      render_line = unpack_v216;
      break;
    case COG_FRAME_FORMAT_RGBx:
    case COG_FRAME_FORMAT_RGBA:
      format = COG_FRAME_FORMAT_U8_444;
      render_line = unpack_axyz;
      param1 = 0x0123;
      break;
    case COG_FRAME_FORMAT_BGRx:
    case COG_FRAME_FORMAT_BGRA:
      format = COG_FRAME_FORMAT_U8_444;
      render_line = unpack_axyz;
      param1 = 0x2103;
      break;
    case COG_FRAME_FORMAT_xRGB:
    case COG_FRAME_FORMAT_ARGB:
    case COG_FRAME_FORMAT_AYUV:
      format = COG_FRAME_FORMAT_U8_444;
      render_line = unpack_axyz;
      param1 = 0x1230;
      break;
    case COG_FRAME_FORMAT_xBGR:
    case COG_FRAME_FORMAT_ABGR:
      format = COG_FRAME_FORMAT_U8_444;
      render_line = unpack_axyz;
      param1 = 0x3210;
      break;
    default:
      g_return_val_if_reached (NULL);
  }

  virt_frame = cog_frame_new_virtual (NULL, format, vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = render_line;
  virt_frame->param1 = param1;

  return virt_frame;
}


static void
pack_yuyv (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_yuyv (dest, src_y, src_u, src_v, frame->width / 2);
}


CogFrame *
cog_virt_frame_new_pack_YUY2 (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_YUYV,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_yuyv;

  return virt_frame;
}

static void
pack_uyvy (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_uyvy (dest, src_y, src_u, src_v, frame->width / 2);
}

CogFrame *
cog_virt_frame_new_pack_UYVY (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_YUYV,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_uyvy;

  return virt_frame;
}

static void
pack_v216 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;
  int j;

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  for (j = 0; j < frame->width / 2; j++) {
    dest[j * 8 + 0] = src_u[j];
    dest[j * 8 + 1] = src_u[j];
    dest[j * 8 + 2] = src_y[j * 2 + 0];
    dest[j * 8 + 3] = src_y[j * 2 + 0];
    dest[j * 8 + 4] = src_v[j];
    dest[j * 8 + 5] = src_v[j];
    dest[j * 8 + 6] = src_y[j * 2 + 1];
    dest[j * 8 + 7] = src_y[j * 2 + 1];
  }
}

CogFrame *
cog_virt_frame_new_pack_v216 (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_v216,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_v216;

  return virt_frame;
}

static void
pack_v210 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;
  int j;
  uint32_t val;

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

#define TO_10(x) (((x)<<2) | ((x)>>6))
#define WRITE_UINT32_LE(a,b) do { \
  ((uint8_t *)(a))[0] = (b)&0xff; \
  ((uint8_t *)(a))[1] = ((b)>>8)&0xff; \
  ((uint8_t *)(a))[2] = ((b)>>16)&0xff; \
  ((uint8_t *)(a))[3] = ((b)>>24)&0xff; \
} while(0)
  for (j = 0; j < frame->width / 6; j++) {
    int y0, y1, y2, y3, y4, y5;
    int cr0, cr1, cr2;
    int cb0, cb1, cb2;

    y0 = TO_10 (src_y[j * 6 + 0]);
    y1 = TO_10 (src_y[j * 6 + 1]);
    y2 = TO_10 (src_y[j * 6 + 2]);
    y3 = TO_10 (src_y[j * 6 + 3]);
    y4 = TO_10 (src_y[j * 6 + 4]);
    y5 = TO_10 (src_y[j * 6 + 5]);
    cb0 = TO_10 (src_u[j * 3 + 0]);
    cb1 = TO_10 (src_u[j * 3 + 1]);
    cb2 = TO_10 (src_u[j * 3 + 2]);
    cr0 = TO_10 (src_v[j * 3 + 0]);
    cr1 = TO_10 (src_v[j * 3 + 1]);
    cr2 = TO_10 (src_v[j * 3 + 2]);

    val = (cr0 << 20) | (y0 << 10) | (cb0);
    WRITE_UINT32_LE (dest + j * 16 + 0, val);

    val = (y2 << 20) | (cb1 << 10) | (y1);
    WRITE_UINT32_LE (dest + j * 16 + 4, val);

    val = (cb2 << 20) | (y3 << 10) | (cr1);
    WRITE_UINT32_LE (dest + j * 16 + 8, val);

    val = (y5 << 20) | (cr2 << 10) | (y4);
    WRITE_UINT32_LE (dest + j * 16 + 12, val);
  }
  if (j * 6 < frame->width) {
    int y0, y1, y2, y3, y4, y5;
    int cr0, cr1, cr2;
    int cb0, cb1, cb2;

    y0 = ((j * 6 + 0) < frame->width) ? TO_10 (src_y[j * 6 + 0]) : 0;
    y1 = ((j * 6 + 1) < frame->width) ? TO_10 (src_y[j * 6 + 1]) : 0;
    y2 = ((j * 6 + 2) < frame->width) ? TO_10 (src_y[j * 6 + 2]) : 0;
    y3 = ((j * 6 + 3) < frame->width) ? TO_10 (src_y[j * 6 + 3]) : 0;
    y4 = ((j * 6 + 4) < frame->width) ? TO_10 (src_y[j * 6 + 4]) : 0;
    y5 = ((j * 6 + 5) < frame->width) ? TO_10 (src_y[j * 6 + 5]) : 0;
    cb0 = ((j * 6 + 0) < frame->width) ? TO_10 (src_u[j * 3 + 0]) : 0;
    cb1 = ((j * 6 + 2) < frame->width) ? TO_10 (src_u[j * 3 + 1]) : 0;
    cb2 = ((j * 6 + 4) < frame->width) ? TO_10 (src_u[j * 3 + 2]) : 0;
    cr0 = ((j * 6 + 0) < frame->width) ? TO_10 (src_v[j * 3 + 0]) : 0;
    cr1 = ((j * 6 + 2) < frame->width) ? TO_10 (src_v[j * 3 + 1]) : 0;
    cr2 = ((j * 6 + 4) < frame->width) ? TO_10 (src_v[j * 3 + 2]) : 0;

    val = (cr0 << 20) | (y0 << 10) | (cb0);
    WRITE_UINT32_LE (dest + j * 16 + 0, val);

    val = (y2 << 20) | (cb1 << 10) | (y1);
    WRITE_UINT32_LE (dest + j * 16 + 4, val);

    val = (cb2 << 20) | (y3 << 10) | (cr1);
    WRITE_UINT32_LE (dest + j * 16 + 8, val);

    val = (y5 << 20) | (cr2 << 10) | (y4);
    WRITE_UINT32_LE (dest + j * 16 + 12, val);
  }

}

CogFrame *
cog_virt_frame_new_pack_v210 (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_v210,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_v210;

  return virt_frame;
}

static void
pack_ayuv (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_x123 (dest, src_y, src_u, src_v, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_AYUV (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_AYUV,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_ayuv;

  return virt_frame;
}

static void
pack_rgb (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;
  int j;

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  for (j = 0; j < frame->width; j++) {
    dest[j * 3 + 0] = src_y[j];
    dest[j * 3 + 1] = src_u[j];
    dest[j * 3 + 2] = src_v[j];
  }
}

CogFrame *
cog_virt_frame_new_pack_RGB (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_RGB,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_rgb;

  return virt_frame;
}


static const int cog_rgb_to_ycbcr_matrix_8bit_sdtv[] = {
  66, 129, 25, 4096,
  -38, -74, 112, 32768,
  112, -94, -18, 32768,
};

static const int cog_rgb_to_ycbcr_matrix_8bit_hdtv[] = {
  47, 157, 16, 4096,
  -26, -87, 112, 32768,
  112, -102, -10, 32768,
};

static void
color_matrix_RGB_to_YCbCr (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  int *matrix = frame->virt_priv2;

  src1 = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src2 = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src3 = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  /* for RGB -> YUV */
  switch (component) {
    case 0:
      orc_matrix3_000_u8 (dest, src1, src2, src3,
          matrix[0], matrix[1], matrix[2], (16 << 8) + 128, 8, frame->width);
      break;
    case 1:
      orc_matrix3_000_u8 (dest, src1, src2, src3,
          matrix[4], matrix[5], matrix[6], (128 << 8) + 128, 8, frame->width);
      break;
    case 2:
      orc_matrix3_000_u8 (dest, src1, src2, src3,
          matrix[8], matrix[9], matrix[10], (128 << 8) + 128, 8, frame->width);
      break;
    default:
      break;
  }

}


static const int cog_ycbcr_to_rgb_matrix_6bit_sdtv[] = {
  75, 0, 102, -14267,
  75, -25, -52, 8677,
  75, 129, 0, -17717,
};

static const int cog_ycbcr_to_rgb_matrix_8bit_sdtv[] = {
  42, 0, 153, -57068,
  42, -100, -208, 34707,
  42, 4, 0, -70870,
};

static const int cog_ycbcr_to_rgb_matrix_6bit_hdtv[] = {
  75, 0, 115, -15878,
  75, -14, -34, 4920,
  75, 135, 0, -18497,
};

static const int cog_ycbcr_to_rgb_matrix_8bit_hdtv[] = {
  42, 0, 203, -63514,
  42, -55, -136, 19681,
  42, 29, 0, -73988,
};

static void
color_matrix_YCbCr_to_RGB_6bit (CogFrame * frame, void *_dest, int component,
    int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  int *matrix = frame->virt_priv2;

  src1 = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src2 = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src3 = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  switch (component) {
    case 0:
      orc_matrix2_u8 (dest, src1, src3, matrix[0], matrix[2], matrix[3] + 32,
          frame->width);
      break;
    case 1:
      orc_matrix3_u8 (dest, src1, src2, src3, matrix[4], matrix[5], matrix[6],
          matrix[7] + 32, frame->width);
      break;
    case 2:
      orc_matrix2_u8 (dest, src1, src2,
          matrix[8], matrix[9], matrix[11] + 32, frame->width);
      break;
    default:
      break;
  }
}

static void
color_matrix_YCbCr_to_RGB_8bit (CogFrame * frame, void *_dest, int component,
    int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  int *matrix = frame->virt_priv2;

  src1 = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src2 = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src3 = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  switch (component) {
    case 0:
      orc_matrix2_11_u8 (dest, src1, src3, matrix[0], matrix[2], frame->width);
      break;
    case 1:
      orc_matrix3_100_u8 (dest, src1, src2, src3,
          matrix[4], matrix[5], matrix[6], frame->width);
      break;
    case 2:
      orc_matrix2_12_u8 (dest, src1, src2, matrix[8], matrix[9], frame->width);
      break;
    default:
      break;
  }
}

CogFrame *
cog_virt_frame_new_color_matrix_YCbCr_to_RGB (CogFrame * vf,
    CogColorMatrix color_matrix, int bits)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_U8_444,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  if (bits <= 6) {
    virt_frame->render_line = color_matrix_YCbCr_to_RGB_6bit;
    if (color_matrix == COG_COLOR_MATRIX_HDTV) {
      virt_frame->virt_priv2 = (void *) cog_ycbcr_to_rgb_matrix_6bit_hdtv;
    } else {
      virt_frame->virt_priv2 = (void *) cog_ycbcr_to_rgb_matrix_6bit_sdtv;
    }
  } else {
    virt_frame->render_line = color_matrix_YCbCr_to_RGB_8bit;
    if (color_matrix == COG_COLOR_MATRIX_HDTV) {
      virt_frame->virt_priv2 = (void *) cog_ycbcr_to_rgb_matrix_8bit_hdtv;
    } else {
      virt_frame->virt_priv2 = (void *) cog_ycbcr_to_rgb_matrix_8bit_sdtv;
    }
  }

  return virt_frame;
}

CogFrame *
cog_virt_frame_new_color_matrix_RGB_to_YCbCr (CogFrame * vf,
    CogColorMatrix color_matrix, int coefficient_bits)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_U8_444,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = color_matrix_RGB_to_YCbCr;
  if (color_matrix == COG_COLOR_MATRIX_HDTV) {
    virt_frame->virt_priv2 = (void *) cog_rgb_to_ycbcr_matrix_8bit_hdtv;
  } else {
    virt_frame->virt_priv2 = (void *) cog_rgb_to_ycbcr_matrix_8bit_sdtv;
  }

  return virt_frame;
}

static const int cog_ycbcr_sdtv_to_ycbcr_hdtv_matrix_8bit[] = {
  256, -30, -53, 10600,
  0, 261, 29, -4367,
  0, 19, 262, -3289,
};

static const int cog_ycbcr_hdtv_to_ycbcr_sdtv_matrix_8bit[] = {
  256, 25, 49, -9536,
  0, 253, -28, 3958,
  0, -19, 252, 2918,
};


static void
color_matrix_YCbCr_to_YCbCr (CogFrame * frame, void *_dest, int component,
    int i)
{
  uint8_t *dest = _dest;
  uint8_t *src1;
  uint8_t *src2;
  uint8_t *src3;
  int *matrix = frame->virt_priv2;

  src1 = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src2 = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src3 = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  switch (component) {
    case 0:
      orc_matrix3_100_offset_u8 (dest, src1, src2, src3,
          matrix[0] - 256, matrix[1], matrix[2], matrix[3], 8, frame->width);
      break;
    case 1:
      orc_matrix3_000_u8 (dest, src1, src2, src3,
          matrix[4], matrix[5], matrix[6], matrix[7], 8, frame->width);
      break;
    case 2:
      orc_matrix3_000_u8 (dest, src1, src2, src3,
          matrix[8], matrix[9], matrix[10], matrix[11], 8, frame->width);
      break;
    default:
      break;
  }

}

CogFrame *
cog_virt_frame_new_color_matrix_YCbCr_to_YCbCr (CogFrame * vf,
    CogColorMatrix in_color_matrix, CogColorMatrix out_color_matrix, int bits)
{
  CogFrame *virt_frame;

  if (in_color_matrix == out_color_matrix)
    return vf;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_U8_444,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = color_matrix_YCbCr_to_YCbCr;
  if (in_color_matrix == COG_COLOR_MATRIX_HDTV) {
    virt_frame->virt_priv2 = (void *) cog_ycbcr_hdtv_to_ycbcr_sdtv_matrix_8bit;
  } else {
    virt_frame->virt_priv2 = (void *) cog_ycbcr_sdtv_to_ycbcr_hdtv_matrix_8bit;
  }

  return virt_frame;
}


static void
convert_444_422 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int n_src;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
  n_src = frame->virt_frame1->components[component].width;

  if (component == 0) {
    orc_memcpy (dest, src, frame->width);
  } else {
    cogorc_downsample_horiz_cosite_1tap (dest + 1,
        (uint16_t *) (src + 2), frame->components[component].width - 1);

    {
      int j;
      int x;

      j = 0;
      x = 1 * src[CLAMP (j * 2 - 1, 0, n_src - 1)];
      x += 2 * src[CLAMP (j * 2 + 0, 0, n_src - 1)];
      x += 1 * src[CLAMP (j * 2 + 1, 0, n_src - 1)];
      dest[j] = CLAMP ((x + 2) >> 2, 0, 255);
    }
  }
}

static void
convert_422_420 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  if (component == 0) {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
    orc_memcpy (dest, src, frame->components[component].width);
  } else {
    uint8_t *dest = _dest;
    uint8_t *src1;
    uint8_t *src2;
    int n_src;

    n_src = frame->virt_frame1->components[component].height;
    src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 + 0, 0, n_src - 1));
    src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 + 1, 0, n_src - 1));

    cogorc_downsample_vert_halfsite_2tap (dest, src1, src2,
        frame->components[component].width);
  }
}

static void
convert_444_420_mpeg2 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  if (component == 0) {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
    orc_memcpy (dest, src, frame->components[component].width);
  } else {
    uint8_t *dest = _dest;
    uint8_t *src1;
    uint8_t *src2;
    int n_src;

    n_src = frame->virt_frame1->components[component].height;
    src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 + 0, 0, n_src - 1));
    src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 + 1, 0, n_src - 1));

#if 0
    cogorc_downsample_420_mpeg2 (dest + 1,
        (uint16_t *) src1, (uint16_t *) (src1 + 2),
        (uint16_t *) src2, (uint16_t *) (src2 + 2),
        frame->components[component].width - 1);
#else
    {
      int j;
      int x;

      for (j = 1; j < frame->components[component].width; j++) {
        x = 1 * src1[j * 2 - 1];
        x += 2 * src1[j * 2 + 0];
        x += 1 * src1[j * 2 + 1];
        x += 1 * src2[j * 2 - 1];
        x += 2 * src2[j * 2 + 0];
        x += 1 * src2[j * 2 + 1];
        dest[j] = CLAMP ((x + 4) >> 3, 0, 255);
      }
    }
#endif
    {
      int j;
      int x;

      j = 0;
      x = 1 * src1[CLAMP (j * 2 - 1, 0, n_src - 1)];
      x += 2 * src1[CLAMP (j * 2 + 0, 0, n_src - 1)];
      x += 1 * src1[CLAMP (j * 2 + 1, 0, n_src - 1)];
      x += 1 * src2[CLAMP (j * 2 - 1, 0, n_src - 1)];
      x += 2 * src2[CLAMP (j * 2 + 0, 0, n_src - 1)];
      x += 1 * src2[CLAMP (j * 2 + 1, 0, n_src - 1)];
      dest[j] = CLAMP ((x + 4) >> 3, 0, 255);
    }
  }
}

static void
convert_444_420_jpeg (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  if (component == 0) {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
    orc_memcpy (dest, src, frame->components[component].width);
  } else {
    uint8_t *dest = _dest;
    uint8_t *src1;
    uint8_t *src2;
    int n_src;

    n_src = frame->virt_frame1->components[component].height;
    src1 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 + 0, 0, n_src - 1));
    src2 = cog_virt_frame_get_line (frame->virt_frame1, component,
        CLAMP (i * 2 + 1, 0, n_src - 1));

    cogorc_downsample_420_jpeg (dest,
        (uint16_t *) src1, (uint16_t *) src2,
        frame->components[component].width);
  }
}

/* up */

static void
convert_420_444_mpeg2 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int n_taps = frame->param1;

  if (component == 0) {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

    orc_memcpy (dest, src, frame->width);
  } else {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i / 2);

    switch (n_taps) {
      default:
      case 2:
        cogorc_upsample_horiz_cosite (dest, src, src + 1,
            frame->components[component].width / 2 - 1);
        break;
    }
    dest[frame->components[component].width - 2] =
        src[frame->components[component].width / 2 - 1];
    dest[frame->components[component].width - 1] =
        src[frame->components[component].width / 2 - 1];
  }
}

static void
convert_420_444_jpeg (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int n_taps = frame->param1;

  if (component == 0) {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

    orc_memcpy (dest, src, frame->width);
  } else {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i / 2);

    switch (n_taps) {
      default:
      case 1:
        cogorc_upsample_horiz_cosite_1tap (dest, src,
            frame->components[component].width / 2 - 1);
        break;
    }
    dest[frame->components[component].width - 2] =
        src[frame->components[component].width / 2 - 1];
    dest[frame->components[component].width - 1] =
        src[frame->components[component].width / 2 - 1];
  }
}

static void
convert_422_444 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int n_taps = frame->param1;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

  if (component == 0) {
    orc_memcpy (dest, src, frame->width);
  } else {
    switch (n_taps) {
      default:
      case 2:
        cogorc_upsample_horiz_cosite (dest, src, src + 1,
            frame->components[component].width / 2 - 1);
        break;
    }
    dest[frame->components[component].width - 2] =
        src[frame->components[component].width / 2 - 1];
    dest[frame->components[component].width - 1] =
        src[frame->components[component].width / 2 - 1];
  }
}

static void
convert_420_422 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int n_taps = frame->param1;

  if (component == 0) {
    src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
    orc_memcpy (dest, src, frame->components[component].width);
  } else {
    switch (n_taps) {
      case 1:
      default:
        src = cog_virt_frame_get_line (frame->virt_frame1, component, i >> 1);
        orc_memcpy (dest, src, frame->components[component].width);
        break;
      case 2:
        if ((i & 1) && i < frame->components[component].height - 1) {
          uint8_t *src2;

          src = cog_virt_frame_get_line (frame->virt_frame1, component, i >> 1);
          src2 = cog_virt_frame_get_line (frame->virt_frame1,
              component, (i >> 1) + 1);
          cogorc_upsample_vert_avgub (dest, src, src2,
              frame->components[component].width);
        } else {
          src = cog_virt_frame_get_line (frame->virt_frame1, component, i >> 1);
          orc_memcpy (dest, src, frame->components[component].width);
        }
        break;
    }
  }
}

CogFrame *
cog_virt_frame_new_subsample (CogFrame * vf, CogFrameFormat format,
    CogChromaSite chroma_site, int n_taps)
{
  CogFrame *virt_frame;
  CogFrameRenderFunc render_line;

  if (vf->format == format) {
    return vf;
  }
  if (vf->format == COG_FRAME_FORMAT_U8_422 &&
      format == COG_FRAME_FORMAT_U8_420) {
    render_line = convert_422_420;
  } else if (vf->format == COG_FRAME_FORMAT_U8_444 &&
      format == COG_FRAME_FORMAT_U8_420) {
    if (chroma_site == COG_CHROMA_SITE_MPEG2) {
      render_line = convert_444_420_mpeg2;
    } else {
      render_line = convert_444_420_jpeg;
    }
  } else if (vf->format == COG_FRAME_FORMAT_U8_444 &&
      format == COG_FRAME_FORMAT_U8_422) {
    render_line = convert_444_422;
  } else if (vf->format == COG_FRAME_FORMAT_U8_420 &&
      format == COG_FRAME_FORMAT_U8_422) {
    render_line = convert_420_422;
  } else if (vf->format == COG_FRAME_FORMAT_U8_420 &&
      format == COG_FRAME_FORMAT_U8_444) {
    if (chroma_site == COG_CHROMA_SITE_MPEG2) {
      render_line = convert_420_444_mpeg2;
    } else {
      render_line = convert_420_444_jpeg;
    }
  } else if (vf->format == COG_FRAME_FORMAT_U8_422 &&
      format == COG_FRAME_FORMAT_U8_444) {
    render_line = convert_422_444;
  } else {
    GST_ERROR ("trying to subsample from %d to %d", vf->format, format);
    g_return_val_if_reached (NULL);
  }
  virt_frame = cog_frame_new_virtual (NULL, format, vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->param1 = n_taps;
  virt_frame->render_line = render_line;

  return virt_frame;
}


static void
convert_u8_s16 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  int16_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
  orc_addc_convert_u8_s16 (dest, src, frame->components[component].width);
}

CogFrame *
cog_virt_frame_new_convert_u8 (CogFrame * vf)
{
  CogFrame *virt_frame;
  CogFrameFormat format;

  format = (vf->format & 3) | COG_FRAME_FORMAT_U8_444;

  virt_frame = cog_frame_new_virtual (NULL, format, vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = convert_u8_s16;

  return virt_frame;
}

static void
convert_s16_u8 (CogFrame * frame, void *_dest, int component, int i)
{
  int16_t *dest = _dest;
  uint8_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);

  orc_subc_convert_s16_u8 (dest, src, frame->components[component].width);
}

CogFrame *
cog_virt_frame_new_convert_s16 (CogFrame * vf)
{
  CogFrame *virt_frame;
  CogFrameFormat format;

  format = (vf->format & 3) | COG_FRAME_FORMAT_S16_444;

  virt_frame = cog_frame_new_virtual (NULL, format, vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = convert_s16_u8;

  return virt_frame;
}

static void
crop_u8 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
  orc_memcpy (dest, src, frame->components[component].width);
}

static void
crop_s16 (CogFrame * frame, void *_dest, int component, int i)
{
  int16_t *dest = _dest;
  int16_t *src;

  src = cog_virt_frame_get_line (frame->virt_frame1, component, i);
  orc_memcpy (dest, src, frame->components[component].width * sizeof (int16_t));
}

CogFrame *
cog_virt_frame_new_crop (CogFrame * vf, int width, int height)
{
  CogFrame *virt_frame;

  if (width == vf->width && height == vf->height)
    return vf;

  g_return_val_if_fail (width <= vf->width, NULL);
  g_return_val_if_fail (height <= vf->height, NULL);

  virt_frame = cog_frame_new_virtual (NULL, vf->format, width, height);
  virt_frame->virt_frame1 = vf;
  switch (COG_FRAME_FORMAT_DEPTH (vf->format)) {
    case COG_FRAME_FORMAT_DEPTH_U8:
      virt_frame->render_line = crop_u8;
      break;
    case COG_FRAME_FORMAT_DEPTH_S16:
      virt_frame->render_line = crop_s16;
      break;
    default:
      g_return_val_if_reached (NULL);
      break;
  }

  return virt_frame;
}

static void
edge_extend_u8 (CogFrame * frame, void *_dest, int component, int i)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  CogFrame *srcframe = frame->virt_frame1;

  src = cog_virt_frame_get_line (frame->virt_frame1, component,
      MIN (i, srcframe->components[component].height - 1));
  orc_memcpy (dest, src, srcframe->components[component].width);
  orc_splat_u8_ns (dest + srcframe->components[component].width,
      dest[srcframe->components[component].width - 1],
      frame->components[component].width -
      srcframe->components[component].width);
}

static void
edge_extend_s16 (CogFrame * frame, void *_dest, int component, int i)
{
  int16_t *dest = _dest;
  int16_t *src;
  CogFrame *srcframe = frame->virt_frame1;

  src = cog_virt_frame_get_line (frame->virt_frame1, component,
      MIN (i, srcframe->components[component].height - 1));
  orc_memcpy (dest, src,
      srcframe->components[component].width * sizeof (int16_t));
  orc_splat_s16_ns (dest + srcframe->components[component].width,
      dest[srcframe->components[component].width - 1],
      frame->components[component].width -
      srcframe->components[component].width);
}

CogFrame *
cog_virt_frame_new_edgeextend (CogFrame * vf, int width, int height)
{
  CogFrame *virt_frame;

  if (width == vf->width && height == vf->height)
    return vf;

  g_return_val_if_fail (width >= vf->width, NULL);
  g_return_val_if_fail (height >= vf->height, NULL);

  virt_frame = cog_frame_new_virtual (NULL, vf->format, width, height);
  virt_frame->virt_frame1 = vf;
  switch (COG_FRAME_FORMAT_DEPTH (vf->format)) {
    case COG_FRAME_FORMAT_DEPTH_U8:
      virt_frame->render_line = edge_extend_u8;
      break;
    case COG_FRAME_FORMAT_DEPTH_S16:
      virt_frame->render_line = edge_extend_s16;
      break;
    default:
      g_return_val_if_reached (NULL);
      break;
  }

  return virt_frame;
}



static void
pack_RGBx (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_123x (dest, src_r, src_g, src_b, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_RGBx (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_RGBx,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_RGBx;

  return virt_frame;
}

static void
pack_xRGB (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_x123 (dest, src_r, src_g, src_b, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_xRGB (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_xRGB,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_xRGB;

  return virt_frame;
}

static void
pack_BGRx (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_123x (dest, src_b, src_g, src_r, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_BGRx (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_BGRx,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_BGRx;

  return virt_frame;
}

static void
pack_xBGR (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_x123 (dest, src_b, src_g, src_r, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_xBGR (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_xBGR,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_xBGR;

  return virt_frame;
}

static void
pack_RGBA (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_123x (dest, src_r, src_g, src_b, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_RGBA (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_RGBA,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_RGBA;

  return virt_frame;
}

static void
pack_ARGB (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_x123 (dest, src_r, src_g, src_b, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_ARGB (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_ARGB,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_ARGB;

  return virt_frame;
}

static void
pack_BGRA (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_123x (dest, src_b, src_g, src_r, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_BGRA (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_BGRA,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_BGRA;

  return virt_frame;
}

static void
pack_ABGR (CogFrame * frame, void *_dest, int component, int i)
{
  uint32_t *dest = _dest;
  uint8_t *src_r;
  uint8_t *src_g;
  uint8_t *src_b;

  src_r = cog_virt_frame_get_line (frame->virt_frame1, 0, i);
  src_g = cog_virt_frame_get_line (frame->virt_frame1, 1, i);
  src_b = cog_virt_frame_get_line (frame->virt_frame1, 2, i);

  orc_pack_x123 (dest, src_b, src_g, src_r, 0xff, frame->width);
}

CogFrame *
cog_virt_frame_new_pack_ABGR (CogFrame * vf)
{
  CogFrame *virt_frame;

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_ABGR,
      vf->width, vf->height);
  virt_frame->virt_frame1 = vf;
  virt_frame->render_line = pack_ABGR;

  return virt_frame;
}
