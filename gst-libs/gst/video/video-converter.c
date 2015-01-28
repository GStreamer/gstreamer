/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-converter.h"

#include <glib.h>
#include <string.h>
#include <math.h>

#include "video-orc.h"

/**
 * SECTION:videoconverter
 * @short_description: Generic video conversion
 *
 * <refsect2>
 * <para>
 * This object is used to convert video frames from one format to another.
 * The object can perform conversion of:
 * <itemizedlist>
 *  <listitem><para>
 *    video format
 *  </para></listitem>
 *  <listitem><para>
 *    video colorspace
 *  </para></listitem>
 *  <listitem><para>
 *    chroma-siting
 *  </para></listitem>
 *  <listitem><para>
 *    video size (planned)
 *  </para></listitem>
 * </para>
 * </refsect2>
 */

/*
 * (a)  unpack
 * (b)  chroma upsample
 * (c)  (convert Y'CbCr to R'G'B')
 * (d)  gamma decode
 * (e)  downscale
 * (f)  colorspace convert through XYZ
 * (g)  upscale
 * (h)  gamma encode
 * (i)  (convert R'G'B' to Y'CbCr)
 * (j)  chroma downsample
 * (k)  pack
 *
 * quality options
 *
 *  (a) range truncate, range expand
 *  (b) full upsample, 1-1 non-cosited upsample, no upsample
 *  (c) 8 bits, 16 bits
 *  (d)
 *  (e) 8 bits, 16 bits
 *  (f) 8 bits, 16 bits
 *  (g) 8 bits, 16 bits
 *  (h)
 *  (i) 8 bits, 16 bits
 *  (j) 1-1 cosited downsample, no downsample
 *  (k)
 *
 *
 *         1 : a ->   ->   ->   -> e  -> f  -> g  ->   ->   ->   -> k
 *         2 : a ->   ->   ->   -> e  -> f* -> g  ->   ->   ->   -> k
 *         3 : a ->   ->   ->   -> e* -> f* -> g* ->   ->   ->   -> k
 *         4 : a -> b ->   ->   -> e  -> f  -> g  ->   ->   -> j -> k
 *         5 : a -> b ->   ->   -> e* -> f* -> g* ->   ->   -> j -> k
 *         6 : a -> b -> c -> d -> e  -> f  -> g  -> h -> i -> j -> k
 *         7 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 *
 *         8 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 *         9 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 *        10 : a -> b -> c -> d -> e* -> f* -> g* -> h -> i -> j -> k
 */
typedef struct _GstLineCache GstLineCache;

#define SCALE    (8)
#define SCALE_F  ((float) (1 << SCALE))

typedef struct _MatrixData MatrixData;

struct _MatrixData
{
  gdouble dm[4][4];
  gint im[4][4];
  gint width;
  guint64 orc_p1;
  guint64 orc_p2;
  guint64 orc_p3;
  guint64 orc_p4;
  void (*matrix_func) (MatrixData * data, gpointer pixels);
};

typedef struct _GammaData GammaData;

struct _GammaData
{
  gpointer gamma_table;
  gint width;
  void (*gamma_func) (GammaData * data, gpointer dest, gpointer src);
};

typedef struct
{
  guint8 *data;
  guint stride;
  guint n_lines;
  guint idx;
  gpointer user_data;
  GDestroyNotify notify;
} ConverterAlloc;

struct _GstVideoConverter
{
  gint flags;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  gint in_x;
  gint in_y;
  gint in_width;
  gint in_height;
  gint in_maxwidth;
  gint in_maxheight;
  gint out_x;
  gint out_y;
  gint out_width;
  gint out_height;
  gint out_maxwidth;
  gint out_maxheight;

  gint current_pstride;
  gint current_width;
  gint current_height;
  GstVideoFormat current_format;
  gint current_bits;

  GstStructure *config;

  guint16 *tmpline;

  gboolean fill_border;
  gpointer borderline;
  guint32 border_argb;

  void (*convert) (GstVideoConverter * convert, const GstVideoFrame * src,
      GstVideoFrame * dest);

  /* data for unpack */
  GstLineCache *unpack_lines;
  GstVideoFormat unpack_format;
  guint unpack_bits;
  gboolean unpack_rgb;
  gboolean identity_unpack;
  gint unpack_pstride;

  /* chroma upsample */
  GstLineCache *upsample_lines;
  GstVideoChromaResample *upsample;
  GstVideoChromaResample *upsample_p;
  GstVideoChromaResample *upsample_i;
  guint up_n_lines;
  gint up_offset;

  /* to R'G'B */
  GstLineCache *to_RGB_lines;
  MatrixData to_RGB_matrix;
  /* gamma decode */
  GammaData gamma_dec;

  /* scaling */
  GstLineCache *hscale_lines;
  GstVideoScaler *h_scaler;
  gint h_scale_format;
  GstLineCache *vscale_lines;
  GstVideoScaler *v_scaler;
  GstVideoScaler *v_scaler_p;
  GstVideoScaler *v_scaler_i;
  gint v_scale_width;
  gint v_scale_format;

  /* color space conversion */
  GstLineCache *convert_lines;
  MatrixData convert_matrix;
  gint in_bits;
  gint out_bits;

  /* gamma encode */
  GammaData gamma_enc;
  /* to Y'CbCr */
  GstLineCache *to_YUV_lines;
  MatrixData to_YUV_matrix;

  /* chroma downsample */
  GstLineCache *downsample_lines;
  GstVideoChromaResample *downsample;
  GstVideoChromaResample *downsample_p;
  GstVideoChromaResample *downsample_i;
  guint down_n_lines;
  gint down_offset;

  /* dither */
  GstLineCache *dither_lines;
  GstVideoDither *dither;

  /* pack */
  GstLineCache *pack_lines;
  guint pack_nlines;
  GstVideoFormat pack_format;
  guint pack_bits;
  gboolean pack_rgb;
  gboolean identity_pack;
  gint pack_pstride;
  gconstpointer pack_pal;
  gsize pack_palsize;

  const GstVideoFrame *src;
  GstVideoFrame *dest;

  /* fastpath */
  GstFormat fformat;
  GstVideoScaler *fh_scaler[4];
  GstVideoScaler *fv_scaler[4];
  ConverterAlloc *flines;
};

typedef gpointer (*GstLineCacheAllocLineFunc) (GstLineCache * cache, gint idx,
    gpointer user_data);
typedef gboolean (*GstLineCacheNeedLineFunc) (GstLineCache * cache,
    gint out_line, gint in_line, gpointer user_data);

struct _GstLineCache
{
  gint first;
  GPtrArray *lines;

  GstLineCache *prev;
  gboolean write_input;
  gboolean pass_alloc;
  gboolean alloc_writable;

  GstLineCacheNeedLineFunc need_line;
  gpointer need_line_data;
  GDestroyNotify need_line_notify;

  gboolean n_lines;
  guint stride;
  GstLineCacheAllocLineFunc alloc_line;
  gpointer alloc_line_data;
  GDestroyNotify alloc_line_notify;
};

static GstLineCache *
gst_line_cache_new (GstLineCache * prev)
{
  GstLineCache *result;

  result = g_slice_new0 (GstLineCache);
  result->lines = g_ptr_array_new ();
  result->prev = prev;

  return result;
}

static void
gst_line_cache_clear (GstLineCache * cache)
{
  g_return_if_fail (cache != NULL);

  g_ptr_array_set_size (cache->lines, 0);
  cache->first = 0;
}

static void
gst_line_cache_free (GstLineCache * cache)
{
  if (cache->need_line_notify)
    cache->need_line_notify (cache->need_line_data);
  if (cache->alloc_line_notify)
    cache->alloc_line_notify (cache->alloc_line_data);
  gst_line_cache_clear (cache);
  g_ptr_array_unref (cache->lines);
  g_slice_free (GstLineCache, cache);
}

static void
gst_line_cache_set_need_line_func (GstLineCache * cache,
    GstLineCacheNeedLineFunc need_line, gpointer user_data,
    GDestroyNotify notify)
{
  cache->need_line = need_line;
  cache->need_line_data = user_data;
  cache->need_line_notify = notify;
}

static void
gst_line_cache_set_alloc_line_func (GstLineCache * cache,
    GstLineCacheAllocLineFunc alloc_line, gpointer user_data,
    GDestroyNotify notify)
{
  cache->alloc_line = alloc_line;
  cache->alloc_line_data = user_data;
  cache->alloc_line_notify = notify;
}

/* keep this much backlog */
#define BACKLOG 2

static gpointer *
gst_line_cache_get_lines (GstLineCache * cache, gint out_line, gint in_line,
    gint n_lines)
{
  if (cache->first + BACKLOG < in_line) {
    gint to_remove =
        MIN (in_line - (cache->first + BACKLOG), cache->lines->len);
    if (to_remove > 0) {
      g_ptr_array_remove_range (cache->lines, 0, to_remove);
      cache->first += to_remove;
    }
  } else if (in_line < cache->first) {
    gst_line_cache_clear (cache);
    cache->first = in_line;
  }

  while (TRUE) {
    gint oline;

    if (cache->first <= in_line
        && in_line + n_lines <= cache->first + (gint) cache->lines->len) {
      return cache->lines->pdata + (in_line - cache->first);
    }

    if (cache->need_line == NULL)
      break;

    oline = out_line + cache->first + cache->lines->len - in_line;

    if (!cache->need_line (cache, oline, cache->first + cache->lines->len,
            cache->need_line_data))
      break;
  }
  GST_DEBUG ("no lines");
  return NULL;
}

static void
gst_line_cache_add_line (GstLineCache * cache, gint idx, gpointer line)
{
  if (cache->first + cache->lines->len != idx) {
    gst_line_cache_clear (cache);
    cache->first = idx;
  }
  g_ptr_array_add (cache->lines, line);
}

static gpointer
gst_line_cache_alloc_line (GstLineCache * cache, gint idx)
{
  gpointer res;

  if (cache->alloc_line)
    res = cache->alloc_line (cache, idx, cache->alloc_line_data);
  else
    res = NULL;

  return res;
}

static void video_converter_generic (GstVideoConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest);
static gboolean video_converter_lookup_fastpath (GstVideoConverter * convert);
static void video_converter_compute_matrix (GstVideoConverter * convert);
static void video_converter_compute_resample (GstVideoConverter * convert);

static gpointer get_dest_line (GstLineCache * cache, gint idx,
    gpointer user_data);

static gboolean do_unpack_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_downsample_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_convert_to_RGB_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_convert_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_convert_to_YUV_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_upsample_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_vscale_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_hscale_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);
static gboolean do_dither_lines (GstLineCache * cache, gint out_line,
    gint in_line, gpointer user_data);

static ConverterAlloc *
converter_alloc_new (guint stride, guint n_lines, gpointer user_data,
    GDestroyNotify notify)
{
  ConverterAlloc *alloc;

  GST_DEBUG ("stride %d, n_lines %d", stride, n_lines);
  alloc = g_slice_new0 (ConverterAlloc);
  alloc->data = g_malloc (stride * n_lines);
  alloc->stride = stride;
  alloc->n_lines = n_lines;
  alloc->idx = 0;
  alloc->user_data = user_data;
  alloc->notify = notify;

  return alloc;
}

static void
converter_alloc_free (ConverterAlloc * alloc)
{
  if (alloc->notify)
    alloc->notify (alloc->user_data);
  g_free (alloc->data);
  g_slice_free (ConverterAlloc, alloc);
}

static void
setup_border_alloc (GstVideoConverter * convert, ConverterAlloc * alloc)
{
  gint i;

  if (convert->borderline) {
    for (i = 0; i < alloc->n_lines; i++)
      memcpy (&alloc->data[i * alloc->stride], convert->borderline,
          alloc->stride);
  }
}

static gpointer
get_temp_line (GstLineCache * cache, gint idx, gpointer user_data)
{
  ConverterAlloc *alloc = user_data;
  gpointer tmpline;

  GST_DEBUG ("get temp line %d (%p %d)", idx, alloc, alloc->idx);
  tmpline = &alloc->data[alloc->stride * alloc->idx];
  alloc->idx = (alloc->idx + 1) % alloc->n_lines;

  return tmpline;
}

static gpointer
get_border_temp_line (GstLineCache * cache, gint idx, gpointer user_data)
{
  ConverterAlloc *alloc = user_data;
  GstVideoConverter *convert = alloc->user_data;
  gpointer tmpline;

  GST_DEBUG ("get temp line %d (%p %d)", idx, alloc, alloc->idx);
  tmpline = &alloc->data[alloc->stride * alloc->idx] +
      (convert->out_x * convert->pack_pstride);
  alloc->idx = (alloc->idx + 1) % alloc->n_lines;

  return tmpline;
}

static gint
get_opt_int (GstVideoConverter * convert, const gchar * opt, gint def)
{
  gint res;
  if (!gst_structure_get_int (convert->config, opt, &res))
    res = def;
  return res;
}

static guint
get_opt_uint (GstVideoConverter * convert, const gchar * opt, guint def)
{
  guint res;
  if (!gst_structure_get_uint (convert->config, opt, &res))
    res = def;
  return res;
}

static gboolean
get_opt_bool (GstVideoConverter * convert, const gchar * opt, gboolean def)
{
  gboolean res;
  if (!gst_structure_get_boolean (convert->config, opt, &res))
    res = def;
  return res;
}

static gint
get_opt_enum (GstVideoConverter * convert, const gchar * opt, GType type,
    gint def)
{
  gint res;
  if (!gst_structure_get_enum (convert->config, opt, type, &res))
    res = def;
  return res;
}

static const gchar *
get_opt_str (GstVideoConverter * convert, const gchar * opt, const gchar * def)
{
  const gchar *res;
  if (!(res = gst_structure_get_string (convert->config, opt)))
    res = def;
  return res;
}

#define DEFAULT_OPT_FILL_BORDER TRUE
#define DEFAULT_OPT_BORDER_ARGB 0x00000000
/* options full, input-only, output-only, none */
#define DEFAULT_OPT_MATRIX_MODE "full"
/* none, remap */
#define DEFAULT_OPT_GAMMA_MODE "none"
/* none, merge-only, fast */
#define DEFAULT_OPT_PRIMARIES_MODE "none"
/* options full, upsample-only, downsample-only, none */
#define DEFAULT_OPT_CHROMA_MODE "full"
#define DEFAULT_OPT_RESAMPLER_METHOD GST_VIDEO_RESAMPLER_METHOD_CUBIC
#define DEFAULT_OPT_RESAMPLER_TAPS 0
#define DEFAULT_OPT_DITHER_METHOD GST_VIDEO_DITHER_BAYER
#define DEFAULT_OPT_DITHER_QUANTIZATION 1

#define GET_OPT_FILL_BORDER(c) get_opt_bool(c, \
    GST_VIDEO_CONVERTER_OPT_FILL_BORDER, DEFAULT_OPT_FILL_BORDER)
#define GET_OPT_BORDER_ARGB(c) get_opt_uint(c, \
    GST_VIDEO_CONVERTER_OPT_BORDER_ARGB, DEFAULT_OPT_BORDER_ARGB)
#define GET_OPT_MATRIX_MODE(c) get_opt_str(c, \
    GST_VIDEO_CONVERTER_OPT_MATRIX_MODE, DEFAULT_OPT_MATRIX_MODE)
#define GET_OPT_GAMMA_MODE(c) get_opt_str(c, \
    GST_VIDEO_CONVERTER_OPT_GAMMA_MODE, DEFAULT_OPT_GAMMA_MODE)
#define GET_OPT_PRIMARIES_MODE(c) get_opt_str(c, \
    GST_VIDEO_CONVERTER_OPT_PRIMARIES_MODE, DEFAULT_OPT_PRIMARIES_MODE)
#define GET_OPT_CHROMA_MODE(c) get_opt_str(c, \
    GST_VIDEO_CONVERTER_OPT_CHROMA_MODE, DEFAULT_OPT_CHROMA_MODE)
#define GET_OPT_RESAMPLER_METHOD(c) get_opt_enum(c, \
    GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD, GST_TYPE_VIDEO_RESAMPLER_METHOD, \
    DEFAULT_OPT_RESAMPLER_METHOD)
#define GET_OPT_RESAMPLER_TAPS(c) get_opt_uint(c, \
    GST_VIDEO_CONVERTER_OPT_RESAMPLER_TAPS, DEFAULT_OPT_RESAMPLER_TAPS)
#define GET_OPT_DITHER_METHOD(c) get_opt_enum(c, \
    GST_VIDEO_CONVERTER_OPT_DITHER_METHOD, GST_TYPE_VIDEO_DITHER_METHOD, \
    DEFAULT_OPT_DITHER_METHOD)
#define GET_OPT_DITHER_QUANTIZATION(c) get_opt_uint(c, \
    GST_VIDEO_CONVERTER_OPT_DITHER_QUANTIZATION, DEFAULT_OPT_DITHER_QUANTIZATION)

#define CHECK_MATRIX_FULL(c) (!g_strcmp0(GET_OPT_MATRIX_MODE(c), "full"))
#define CHECK_MATRIX_INPUT(c) (!g_strcmp0(GET_OPT_MATRIX_MODE(c), "input-only"))
#define CHECK_MATRIX_OUTPUT(c) (!g_strcmp0(GET_OPT_MATRIX_MODE(c), "output-only"))
#define CHECK_MATRIX_NONE(c) (!g_strcmp0(GET_OPT_MATRIX_MODE(c), "none"))

#define CHECK_GAMMA_NONE(c) (!g_strcmp0(GET_OPT_GAMMA_MODE(c), "none"))
#define CHECK_GAMMA_REMAP(c) (!g_strcmp0(GET_OPT_GAMMA_MODE(c), "remap"))

#define CHECK_PRIMARIES_NONE(c) (!g_strcmp0(GET_OPT_PRIMARIES_MODE(c), "none"))
#define CHECK_PRIMARIES_MERGE(c) (!g_strcmp0(GET_OPT_PRIMARIES_MODE(c), "merge-only"))
#define CHECK_PRIMARIES_FAST(c) (!g_strcmp0(GET_OPT_PRIMARIES_MODE(c), "fast"))

#define CHECK_CHROMA_FULL(c) (!g_strcmp0(GET_OPT_CHROMA_MODE(c), "full"))
#define CHECK_CHROMA_UPSAMPLE(c) (!g_strcmp0(GET_OPT_CHROMA_MODE(c), "upsample-only"))
#define CHECK_CHROMA_DOWNSAMPLE(c) (!g_strcmp0(GET_OPT_CHROMA_MODE(c), "downsample-only"))
#define CHECK_CHROMA_NONE(c) (!g_strcmp0(GET_OPT_CHROMA_MODE(c), "none"))

static GstLineCache *
chain_unpack_line (GstVideoConverter * convert)
{
  GstLineCache *prev;
  GstVideoInfo *info;

  info = &convert->in_info;

  convert->current_format = convert->unpack_format;
  convert->current_bits = convert->unpack_bits;
  convert->current_pstride = convert->current_bits >> 1;

  convert->unpack_pstride = convert->current_pstride;
  convert->identity_unpack = (convert->current_format == info->finfo->format);

  GST_DEBUG ("chain unpack line format %s, pstride %d, identity_unpack %d",
      gst_video_format_to_string (convert->current_format),
      convert->current_pstride, convert->identity_unpack);

  prev = convert->unpack_lines = gst_line_cache_new (NULL);
  prev->write_input = FALSE;
  prev->pass_alloc = FALSE;
  prev->n_lines = 1;
  prev->stride = convert->current_pstride * convert->current_width;
  gst_line_cache_set_need_line_func (convert->unpack_lines,
      do_unpack_lines, convert, NULL);

  return convert->unpack_lines;
}

static GstLineCache *
chain_upsample (GstVideoConverter * convert, GstLineCache * prev)
{
  video_converter_compute_resample (convert);

  if (convert->upsample_p || convert->upsample_i) {
    GST_DEBUG ("chain upsample");
    prev = convert->upsample_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = TRUE;
    prev->n_lines = 4;
    prev->stride = convert->current_pstride * convert->current_width;
    gst_line_cache_set_need_line_func (convert->upsample_lines,
        do_upsample_lines, convert, NULL);
  }
  return prev;
}

static void
color_matrix_set_identity (MatrixData * m)
{
  int i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      m->dm[i][j] = (i == j);
    }
  }
}

static void
color_matrix_copy (MatrixData * d, const MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      d->dm[i][j] = s->dm[i][j];
}

/* Perform 4x4 matrix multiplication:
 *  - @dst@ = @a@ * @b@
 *  - @dst@ may be a pointer to @a@ andor @b@
 */
static void
color_matrix_multiply (MatrixData * dst, MatrixData * a, MatrixData * b)
{
  MatrixData tmp;
  int i, j, k;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      double x = 0;
      for (k = 0; k < 4; k++) {
        x += a->dm[i][k] * b->dm[k][j];
      }
      tmp.dm[i][j] = x;
    }
  }
  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_invert (MatrixData * d, MatrixData * s)
{
  MatrixData tmp;
  int i, j;
  double det;

  color_matrix_set_identity (&tmp);
  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      tmp.dm[j][i] =
          s->dm[(i + 1) % 3][(j + 1) % 3] * s->dm[(i + 2) % 3][(j + 2) % 3] -
          s->dm[(i + 1) % 3][(j + 2) % 3] * s->dm[(i + 2) % 3][(j + 1) % 3];
    }
  }
  det =
      tmp.dm[0][0] * s->dm[0][0] + tmp.dm[0][1] * s->dm[1][0] +
      tmp.dm[0][2] * s->dm[2][0];
  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      tmp.dm[i][j] /= det;
    }
  }
  color_matrix_copy (d, &tmp);
}

static void
color_matrix_offset_components (MatrixData * m, double a1, double a2, double a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][3] = a1;
  a.dm[1][3] = a2;
  a.dm[2][3] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_scale_components (MatrixData * m, double a1, double a2, double a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][0] = a1;
  a.dm[1][1] = a2;
  a.dm[2][2] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_debug (const MatrixData * s)
{
  GST_DEBUG ("[%f %f %f %f]", s->dm[0][0], s->dm[0][1], s->dm[0][2],
      s->dm[0][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[1][0], s->dm[1][1], s->dm[1][2],
      s->dm[1][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[2][0], s->dm[2][1], s->dm[2][2],
      s->dm[2][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[3][0], s->dm[3][1], s->dm[3][2],
      s->dm[3][3]);
}

static void
color_matrix_convert (MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      s->im[i][j] = rint (s->dm[i][j]);

  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[0][0], s->im[0][1], s->im[0][2],
      s->im[0][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[1][0], s->im[1][1], s->im[1][2],
      s->im[1][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[2][0], s->im[2][1], s->im[2][2],
      s->im[2][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", s->im[3][0], s->im[3][1], s->im[3][2],
      s->im[3][3]);
}

static void
color_matrix_YCbCr_to_RGB (MatrixData * m, double Kr, double Kb)
{
  double Kg = 1.0 - Kr - Kb;
  MatrixData k = {
    {
          {1., 0., 2 * (1 - Kr), 0.},
          {1., -2 * Kb * (1 - Kb) / Kg, -2 * Kr * (1 - Kr) / Kg, 0.},
          {1., 2 * (1 - Kb), 0., 0.},
          {0., 0., 0., 1.},
        }
  };

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_YCbCr (MatrixData * m, double Kr, double Kb)
{
  double Kg = 1.0 - Kr - Kb;
  MatrixData k;
  double x;

  k.dm[0][0] = Kr;
  k.dm[0][1] = Kg;
  k.dm[0][2] = Kb;
  k.dm[0][3] = 0;

  x = 1 / (2 * (1 - Kb));
  k.dm[1][0] = -x * Kr;
  k.dm[1][1] = -x * Kg;
  k.dm[1][2] = x * (1 - Kb);
  k.dm[1][3] = 0;

  x = 1 / (2 * (1 - Kr));
  k.dm[2][0] = x * (1 - Kr);
  k.dm[2][1] = -x * Kg;
  k.dm[2][2] = -x * Kb;
  k.dm[2][3] = 0;

  k.dm[3][0] = 0;
  k.dm[3][1] = 0;
  k.dm[3][2] = 0;
  k.dm[3][3] = 1;

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_XYZ (MatrixData * dst, double Rx, double Ry, double Gx,
    double Gy, double Bx, double By, double Wx, double Wy)
{
  MatrixData m, im;
  double sx, sy, sz;
  double wx, wy, wz;

  color_matrix_set_identity (&m);

  m.dm[0][0] = Rx;
  m.dm[1][0] = Ry;
  m.dm[2][0] = (1.0 - Rx - Ry);
  m.dm[0][1] = Gx;
  m.dm[1][1] = Gy;
  m.dm[2][1] = (1.0 - Gx - Gy);
  m.dm[0][2] = Bx;
  m.dm[1][2] = By;
  m.dm[2][2] = (1.0 - Bx - By);

  color_matrix_invert (&im, &m);

  wx = Wx / Wy;
  wy = 1.0;
  wz = (1.0 - Wx - Wy) / Wy;

  sx = im.dm[0][0] * wx + im.dm[0][1] * wy + im.dm[0][2] * wz;
  sy = im.dm[1][0] * wx + im.dm[1][1] * wy + im.dm[1][2] * wz;
  sz = im.dm[2][0] * wx + im.dm[2][1] * wy + im.dm[2][2] * wz;

  m.dm[0][0] *= sx;
  m.dm[1][0] *= sx;
  m.dm[2][0] *= sx;
  m.dm[0][1] *= sy;
  m.dm[1][1] *= sy;
  m.dm[2][1] *= sy;
  m.dm[0][2] *= sz;
  m.dm[1][2] *= sz;
  m.dm[2][2] *= sz;

  color_matrix_copy (dst, &m);
}

void
_custom_video_orc_matrix8 (guint8 * ORC_RESTRICT d1,
    const guint8 * ORC_RESTRICT s1, orc_int64 p1, orc_int64 p2, orc_int64 p3,
    orc_int64 p4, int n)
{
  gint i;
  gint r, g, b;
  gint y, u, v;
  gint a00, a01, a02, a03;
  gint a10, a11, a12, a13;
  gint a20, a21, a22, a23;

  a00 = (gint16) (p1 >> 16);
  a01 = (gint16) (p2 >> 16);
  a02 = (gint16) (p3 >> 16);
  a03 = (gint16) (p4 >> 16);
  a10 = (gint16) (p1 >> 32);
  a11 = (gint16) (p2 >> 32);
  a12 = (gint16) (p3 >> 32);
  a13 = (gint16) (p4 >> 32);
  a20 = (gint16) (p1 >> 48);
  a21 = (gint16) (p2 >> 48);
  a22 = (gint16) (p3 >> 48);
  a23 = (gint16) (p4 >> 48);

  for (i = 0; i < n; i++) {
    r = s1[i * 4 + 1];
    g = s1[i * 4 + 2];
    b = s1[i * 4 + 3];

    y = ((a00 * r + a01 * g + a02 * b) >> SCALE) + a03;
    u = ((a10 * r + a11 * g + a12 * b) >> SCALE) + a13;
    v = ((a20 * r + a21 * g + a22 * b) >> SCALE) + a23;

    d1[i * 4 + 1] = CLAMP (y, 0, 255);
    d1[i * 4 + 2] = CLAMP (u, 0, 255);
    d1[i * 4 + 3] = CLAMP (v, 0, 255);
  }
}

static void
video_converter_matrix8 (MatrixData * data, gpointer pixels)
{
  video_orc_matrix8 (pixels, pixels, data->orc_p1, data->orc_p2,
      data->orc_p3, data->orc_p4, data->width);
}

static void
video_converter_matrix8_AYUV_ARGB (MatrixData * data, gpointer pixels)
{
  video_orc_convert_AYUV_ARGB (pixels, 0, pixels, 0,
      data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], data->width, 1);
}

static gboolean
is_ayuv_to_rgb_matrix (MatrixData * data)
{
  if (data->im[0][0] != data->im[1][0] || data->im[1][0] != data->im[2][0])
    return FALSE;

  if (data->im[0][1] != 0 || data->im[2][2] != 0)
    return FALSE;

  return TRUE;
}

static void
video_converter_matrix16 (MatrixData * data, gpointer pixels)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *p = pixels;
  gint width = data->width;

  for (i = 0; i < width; i++) {
    r = p[i * 4 + 1];
    g = p[i * 4 + 2];
    b = p[i * 4 + 3];

    y = (data->im[0][0] * r + data->im[0][1] * g +
        data->im[0][2] * b + data->im[0][3]) >> SCALE;
    u = (data->im[1][0] * r + data->im[1][1] * g +
        data->im[1][2] * b + data->im[1][3]) >> SCALE;
    v = (data->im[2][0] * r + data->im[2][1] * g +
        data->im[2][2] * b + data->im[2][3]) >> SCALE;

    p[i * 4 + 1] = CLAMP (y, 0, 65535);
    p[i * 4 + 2] = CLAMP (u, 0, 65535);
    p[i * 4 + 3] = CLAMP (v, 0, 65535);
  }
}


static void
prepare_matrix (GstVideoConverter * convert, MatrixData * data)
{
  color_matrix_scale_components (data, SCALE_F, SCALE_F, SCALE_F);
  color_matrix_convert (data);

  data->width = convert->current_width;

  if (convert->current_bits == 8) {
    if (!convert->unpack_rgb && convert->pack_rgb
        && is_ayuv_to_rgb_matrix (data)) {
      GST_DEBUG ("use fast AYUV -> RGB matrix");
      data->matrix_func = video_converter_matrix8_AYUV_ARGB;
    } else {
      gint a03, a13, a23;

      GST_DEBUG ("use 8bit matrix");
      data->matrix_func = video_converter_matrix8;

      data->orc_p1 = (((guint64) (guint16) data->im[2][0]) << 48) |
          (((guint64) (guint16) data->im[1][0]) << 32) |
          (((guint64) (guint16) data->im[0][0]) << 16);
      data->orc_p2 = (((guint64) (guint16) data->im[2][1]) << 48) |
          (((guint64) (guint16) data->im[1][1]) << 32) |
          (((guint64) (guint16) data->im[0][1]) << 16);
      data->orc_p3 = (((guint64) (guint16) data->im[2][2]) << 48) |
          (((guint64) (guint16) data->im[1][2]) << 32) |
          (((guint64) (guint16) data->im[0][2]) << 16);

      a03 = data->im[0][3] >> SCALE;
      a13 = data->im[1][3] >> SCALE;
      a23 = data->im[2][3] >> SCALE;

      data->orc_p4 = (((guint64) (guint16) a23) << 48) |
          (((guint64) (guint16) a13) << 32) | (((guint64) (guint16) a03) << 16);
    }
  } else {
    GST_DEBUG ("use 16bit matrix");
    data->matrix_func = video_converter_matrix16;
  }
}

static void
compute_matrix_to_RGB (GstVideoConverter * convert, MatrixData * data)
{
  GstVideoInfo *info;
  gdouble Kr = 0, Kb = 0;

  info = &convert->in_info;

  {
    const GstVideoFormatInfo *uinfo;
    gint offset[4], scale[4];

    uinfo = gst_video_format_get_info (convert->unpack_format);

    /* bring color components to [0..1.0] range */
    gst_video_color_range_offsets (info->colorimetry.range, uinfo, offset,
        scale);

    color_matrix_offset_components (data, -offset[0], -offset[1], -offset[2]);
    color_matrix_scale_components (data, 1 / ((float) scale[0]),
        1 / ((float) scale[1]), 1 / ((float) scale[2]));
  }

  if (!CHECK_MATRIX_NONE (convert)) {
    if (CHECK_MATRIX_OUTPUT (convert))
      info = &convert->out_info;

    /* bring components to R'G'B' space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_YCbCr_to_RGB (data, Kr, Kb);
  }

  color_matrix_debug (data);
}

static void
compute_matrix_to_YUV (GstVideoConverter * convert, MatrixData * data)
{
  GstVideoInfo *info;
  gdouble Kr = 0, Kb = 0;

  if (!CHECK_MATRIX_NONE (convert)) {
    if (CHECK_MATRIX_INPUT (convert))
      info = &convert->in_info;
    else
      info = &convert->out_info;

    /* bring components to YCbCr space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_RGB_to_YCbCr (data, Kr, Kb);
  }

  info = &convert->out_info;

  {
    const GstVideoFormatInfo *uinfo;
    gint offset[4], scale[4];

    uinfo = gst_video_format_get_info (convert->pack_format);

    /* bring color components to nominal range */
    gst_video_color_range_offsets (info->colorimetry.range, uinfo, offset,
        scale);

    color_matrix_scale_components (data, (float) scale[0], (float) scale[1],
        (float) scale[2]);
    color_matrix_offset_components (data, offset[0], offset[1], offset[2]);
  }

  color_matrix_debug (data);
}


static void
gamma_convert_u8_u16 (GammaData * data, gpointer dest, gpointer src)
{
  gint i;
  guint8 *s = src;
  guint16 *d = dest;
  guint16 *table = data->gamma_table;
  gint width = data->width * 4;

  for (i = 0; i < width; i++)
    d[i] = table[s[i]];
}

static void
gamma_convert_u16_u8 (GammaData * data, gpointer dest, gpointer src)
{
  gint i;
  guint16 *s = src;
  guint8 *d = dest;
  guint8 *table = data->gamma_table;
  gint width = data->width * 4;

  for (i = 0; i < width; i++)
    d[i] = table[s[i]];
}

static void
gamma_convert_u16_u16 (GammaData * data, gpointer dest, gpointer src)
{
  gint i;
  guint16 *s = src;
  guint16 *d = dest;
  guint16 *table = data->gamma_table;
  gint width = data->width * 4;

  for (i = 0; i < width; i++)
    d[i] = table[s[i]];
}

static void
setup_gamma_decode (GstVideoConverter * convert)
{
  GstVideoTransferFunction func;
  guint16 *t;
  gint i;

  func = convert->in_info.colorimetry.transfer;

  convert->gamma_dec.width = convert->current_width;
  if (convert->current_bits == 8) {
    GST_DEBUG ("gamma decode 8->16: %d", func);
    convert->gamma_dec.gamma_func = gamma_convert_u8_u16;
    t = convert->gamma_dec.gamma_table = g_malloc (sizeof (guint16) * 256);

    for (i = 0; i < 256; i++)
      t[i] = rint (gst_video_color_transfer_decode (func, i / 255.0) * 65535.0);
  } else {
    GST_DEBUG ("gamma decode 16->16: %d", func);
    convert->gamma_dec.gamma_func = gamma_convert_u16_u16;
    t = convert->gamma_dec.gamma_table = g_malloc (sizeof (guint16) * 65536);

    for (i = 0; i < 65536; i++)
      t[i] =
          rint (gst_video_color_transfer_decode (func, i / 65535.0) * 65535.0);
  }
  convert->current_bits = 16;
  convert->current_pstride = 8;
  convert->current_format = GST_VIDEO_FORMAT_ARGB64;
}

static void
setup_gamma_encode (GstVideoConverter * convert, gint target_bits)
{
  GstVideoTransferFunction func;
  gint i;

  func = convert->out_info.colorimetry.transfer;

  convert->gamma_enc.width = convert->current_width;
  if (target_bits == 8) {
    guint8 *t;

    GST_DEBUG ("gamma encode 16->8: %d", func);
    convert->gamma_enc.gamma_func = gamma_convert_u16_u8;
    t = convert->gamma_enc.gamma_table = g_malloc (sizeof (guint8) * 65536);

    for (i = 0; i < 65536; i++)
      t[i] = rint (gst_video_color_transfer_encode (func, i / 65535.0) * 255.0);
  } else {
    guint16 *t;

    GST_DEBUG ("gamma encode 16->16: %d", func);
    convert->gamma_enc.gamma_func = gamma_convert_u16_u16;
    t = convert->gamma_enc.gamma_table = g_malloc (sizeof (guint16) * 65536);

    for (i = 0; i < 65536; i++)
      t[i] =
          rint (gst_video_color_transfer_encode (func, i / 65535.0) * 65535.0);
  }
}

static GstLineCache *
chain_convert_to_RGB (GstVideoConverter * convert, GstLineCache * prev)
{
  gboolean do_gamma;

  do_gamma = CHECK_GAMMA_REMAP (convert);

  if (do_gamma) {
    gint scale;

    if (!convert->unpack_rgb) {
      color_matrix_set_identity (&convert->to_RGB_matrix);
      compute_matrix_to_RGB (convert, &convert->to_RGB_matrix);

      /* matrix is in 0..1 range, scale to current bits */
      GST_DEBUG ("chain RGB convert");
      scale = 1 << convert->current_bits;
      color_matrix_scale_components (&convert->to_RGB_matrix,
          (float) scale, (float) scale, (float) scale);

      prepare_matrix (convert, &convert->to_RGB_matrix);

      if (convert->current_bits == 8)
        convert->current_format = GST_VIDEO_FORMAT_ARGB;
      else
        convert->current_format = GST_VIDEO_FORMAT_ARGB64;
    }

    prev = convert->to_RGB_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = FALSE;
    prev->n_lines = 1;
    prev->stride = convert->current_pstride * convert->current_width;
    gst_line_cache_set_need_line_func (convert->to_RGB_lines,
        do_convert_to_RGB_lines, convert, NULL);

    GST_DEBUG ("chain gamma decode");
    setup_gamma_decode (convert);
  }
  return prev;
}

static GstLineCache *
chain_hscale (GstVideoConverter * convert, GstLineCache * prev)
{
  gint method;
  guint taps;

  method = GET_OPT_RESAMPLER_METHOD (convert);
  taps = GET_OPT_RESAMPLER_TAPS (convert);

  convert->h_scaler =
      gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
      convert->in_width, convert->out_width, convert->config);

  gst_video_scaler_get_coeff (convert->h_scaler, 0, NULL, &taps);

  GST_DEBUG ("chain hscale %d->%d, taps %d, method %d",
      convert->in_width, convert->out_width, taps, method);

  convert->current_width = convert->out_width;
  convert->h_scale_format = convert->current_format;

  prev = convert->hscale_lines = gst_line_cache_new (prev);
  prev->write_input = FALSE;
  prev->pass_alloc = FALSE;
  prev->n_lines = 1;
  prev->stride = convert->current_pstride * convert->current_width;
  gst_line_cache_set_need_line_func (convert->hscale_lines,
      do_hscale_lines, convert, NULL);

  return prev;
}

static GstLineCache *
chain_vscale (GstVideoConverter * convert, GstLineCache * prev)
{
  gint method;
  guint taps, taps_i = 0;

  method = GET_OPT_RESAMPLER_METHOD (convert);
  taps = GET_OPT_RESAMPLER_TAPS (convert);

  if (GST_VIDEO_INFO_IS_INTERLACED (&convert->in_info)) {
    convert->v_scaler_i =
        gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_INTERLACED,
        taps, convert->in_height, convert->out_height, convert->config);

    gst_video_scaler_get_coeff (convert->v_scaler_i, 0, NULL, &taps_i);
  }
  convert->v_scaler_p =
      gst_video_scaler_new (method, 0, taps, convert->in_height,
      convert->out_height, convert->config);
  convert->v_scale_width = convert->current_width;
  convert->v_scale_format = convert->current_format;
  convert->current_height = convert->out_height;

  gst_video_scaler_get_coeff (convert->v_scaler_p, 0, NULL, &taps);

  GST_DEBUG ("chain vscale %d->%d, taps %d, method %d",
      convert->in_height, convert->out_height, taps, method);

  prev = convert->vscale_lines = gst_line_cache_new (prev);
  prev->pass_alloc = (taps == 1);
  prev->write_input = FALSE;
  prev->n_lines = MAX (taps_i, taps);
  prev->stride = convert->current_pstride * convert->current_width;
  gst_line_cache_set_need_line_func (convert->vscale_lines,
      do_vscale_lines, convert, NULL);

  return prev;
}

static GstLineCache *
chain_scale (GstVideoConverter * convert, GstLineCache * prev, gboolean force)
{
  gint s0, s1, s2, s3;

  s0 = convert->current_width * convert->current_height;
  s3 = convert->out_width * convert->out_height;

  GST_DEBUG ("%d <> %d", s0, s3);

  if (s3 <= s0 || force) {
    /* we are making the image smaller or are forced to resample */
    s1 = convert->out_width * convert->current_height;
    s2 = convert->current_width * convert->out_height;

    GST_DEBUG ("%d <> %d", s1, s2);

    if (s1 <= s2) {
      /* h scaling first produces less pixels */
      if (convert->current_width != convert->out_width)
        prev = chain_hscale (convert, prev);
      if (convert->current_height != convert->out_height)
        prev = chain_vscale (convert, prev);
    } else {
      /* v scaling first produces less pixels */
      if (convert->current_height != convert->out_height)
        prev = chain_vscale (convert, prev);
      if (convert->current_width != convert->out_width)
        prev = chain_hscale (convert, prev);
    }
  }
  return prev;
}

static GstLineCache *
chain_convert (GstVideoConverter * convert, GstLineCache * prev)
{
  gboolean do_gamma, do_conversion, pass_alloc = FALSE;
  gboolean same_matrix, same_primaries, same_bits;
  MatrixData p1, p2;

  same_bits = convert->unpack_bits == convert->pack_bits;
  if (CHECK_MATRIX_NONE (convert)) {
    same_matrix = TRUE;
  } else {
    same_matrix =
        convert->in_info.colorimetry.matrix ==
        convert->out_info.colorimetry.matrix;
  }

  if (CHECK_PRIMARIES_NONE (convert)) {
    same_primaries = TRUE;
  } else {
    same_primaries =
        convert->in_info.colorimetry.primaries ==
        convert->out_info.colorimetry.primaries;
  }

  GST_DEBUG ("matrix %d -> %d (%d)", convert->in_info.colorimetry.matrix,
      convert->out_info.colorimetry.matrix, same_matrix);
  GST_DEBUG ("bits %d -> %d (%d)", convert->unpack_bits, convert->pack_bits,
      same_bits);
  GST_DEBUG ("primaries %d -> %d (%d)", convert->in_info.colorimetry.primaries,
      convert->out_info.colorimetry.primaries, same_primaries);

  color_matrix_set_identity (&convert->convert_matrix);

  if (!same_primaries) {
    const GstVideoColorPrimariesInfo *pi;

    pi = gst_video_color_primaries_get_info (convert->in_info.colorimetry.
        primaries);
    color_matrix_RGB_to_XYZ (&p1, pi->Rx, pi->Ry, pi->Gx, pi->Gy, pi->Bx,
        pi->By, pi->Wx, pi->Wy);
    GST_DEBUG ("to XYZ matrix");
    color_matrix_debug (&p1);
    GST_DEBUG ("current matrix");
    color_matrix_multiply (&convert->convert_matrix, &convert->convert_matrix,
        &p1);
    color_matrix_debug (&convert->convert_matrix);

    pi = gst_video_color_primaries_get_info (convert->out_info.colorimetry.
        primaries);
    color_matrix_RGB_to_XYZ (&p2, pi->Rx, pi->Ry, pi->Gx, pi->Gy, pi->Bx,
        pi->By, pi->Wx, pi->Wy);
    color_matrix_invert (&p2, &p2);
    GST_DEBUG ("to RGB matrix");
    color_matrix_debug (&p2);
    color_matrix_multiply (&convert->convert_matrix, &convert->convert_matrix,
        &p2);
    GST_DEBUG ("current matrix");
    color_matrix_debug (&convert->convert_matrix);
  }

  do_gamma = CHECK_GAMMA_REMAP (convert);
  if (!do_gamma) {

    convert->in_bits = convert->unpack_bits;
    convert->out_bits = convert->pack_bits;

    if (!same_bits || !same_matrix || !same_primaries) {
      /* no gamma, combine all conversions into 1 */
      if (convert->in_bits < convert->out_bits) {
        gint scale = 1 << (convert->out_bits - convert->in_bits);
        color_matrix_scale_components (&convert->convert_matrix,
            1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
      }
      GST_DEBUG ("to RGB matrix");
      compute_matrix_to_RGB (convert, &convert->convert_matrix);
      GST_DEBUG ("current matrix");
      color_matrix_debug (&convert->convert_matrix);

      GST_DEBUG ("to YUV matrix");
      compute_matrix_to_YUV (convert, &convert->convert_matrix);
      GST_DEBUG ("current matrix");
      color_matrix_debug (&convert->convert_matrix);
      if (convert->in_bits > convert->out_bits) {
        gint scale = 1 << (convert->in_bits - convert->out_bits);
        color_matrix_scale_components (&convert->convert_matrix,
            (float) scale, (float) scale, (float) scale);
      }
      convert->current_bits = MAX (convert->in_bits, convert->out_bits);

      do_conversion = TRUE;
      if (!same_matrix || !same_primaries)
        prepare_matrix (convert, &convert->convert_matrix);
      if (convert->in_bits == convert->out_bits)
        pass_alloc = TRUE;
    } else
      do_conversion = FALSE;

    convert->current_bits = convert->pack_bits;
    convert->current_format = convert->pack_format;
    convert->current_pstride = convert->current_bits >> 1;
  } else {
    /* we did gamma, just do colorspace conversion if needed */
    if (same_primaries) {
      do_conversion = FALSE;
    } else {
      prepare_matrix (convert, &convert->convert_matrix);
      convert->in_bits = convert->out_bits = 16;
      pass_alloc = TRUE;
      do_conversion = TRUE;
    }
  }

  if (do_conversion) {
    GST_DEBUG ("chain conversion");
    prev = convert->convert_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = pass_alloc;
    prev->n_lines = 1;
    prev->stride = convert->current_pstride * convert->current_width;
    gst_line_cache_set_need_line_func (convert->convert_lines,
        do_convert_lines, convert, NULL);
  }
  return prev;
}

static GstLineCache *
chain_convert_to_YUV (GstVideoConverter * convert, GstLineCache * prev)
{
  gboolean do_gamma;

  do_gamma = CHECK_GAMMA_REMAP (convert);

  if (do_gamma) {
    gint scale;

    GST_DEBUG ("chain gamma encode");
    setup_gamma_encode (convert, convert->pack_bits);

    convert->current_bits = convert->pack_bits;
    convert->current_pstride = convert->current_bits >> 1;

    if (!convert->pack_rgb) {
      color_matrix_set_identity (&convert->to_YUV_matrix);
      compute_matrix_to_YUV (convert, &convert->to_YUV_matrix);

      /* matrix is in 0..255 range, scale to pack bits */
      GST_DEBUG ("chain YUV convert");
      scale = 1 << convert->pack_bits;
      color_matrix_scale_components (&convert->to_YUV_matrix,
          1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
      prepare_matrix (convert, &convert->to_YUV_matrix);
    }
    convert->current_format = convert->pack_format;

    prev = convert->to_YUV_lines = gst_line_cache_new (prev);
    prev->write_input = FALSE;
    prev->pass_alloc = FALSE;
    prev->n_lines = 1;
    prev->stride = convert->current_pstride * convert->current_width;
    gst_line_cache_set_need_line_func (convert->to_YUV_lines,
        do_convert_to_YUV_lines, convert, NULL);
  }

  return prev;
}

static GstLineCache *
chain_downsample (GstVideoConverter * convert, GstLineCache * prev)
{
  if (convert->downsample_p || convert->downsample_i) {
    GST_DEBUG ("chain downsample");
    prev = convert->downsample_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = TRUE;
    prev->n_lines = 4;
    prev->stride = convert->current_pstride * convert->current_width;
    gst_line_cache_set_need_line_func (convert->downsample_lines,
        do_downsample_lines, convert, NULL);
  }
  return prev;
}

static GstLineCache *
chain_dither (GstVideoConverter * convert, GstLineCache * prev)
{
  gint i;
  gboolean do_dither = FALSE;
  GstVideoDitherFlags flags = 0;
  GstVideoDitherMethod method;
  guint quant[4], target_quant;

  method = GET_OPT_DITHER_METHOD (convert);
  if (method == GST_VIDEO_DITHER_NONE)
    return prev;

  target_quant = GET_OPT_DITHER_QUANTIZATION (convert);
  GST_DEBUG ("method %d, target-quantization %d", method, target_quant);

  if (convert->pack_pal) {
    quant[0] = 47;
    quant[1] = 47;
    quant[2] = 47;
    quant[3] = 1;
    do_dither = TRUE;
  } else {
    for (i = 0; i < GST_VIDEO_MAX_COMPONENTS; i++) {
      gint depth;

      depth = convert->out_info.finfo->depth[i];

      if (depth == 0) {
        quant[i] = 0;
        continue;
      }

      if (convert->current_bits >= depth) {
        quant[i] = 1 << (convert->current_bits - depth);
        if (target_quant > quant[i]) {
          flags |= GST_VIDEO_DITHER_FLAG_QUANTIZE;
          quant[i] = target_quant;
        }
      } else {
        quant[i] = 0;
      }
      if (quant[i] > 1)
        do_dither = TRUE;
    }
  }

  if (do_dither) {
    GST_DEBUG ("chain dither");

    convert->dither = gst_video_dither_new (method,
        flags, convert->pack_format, quant, convert->current_width);

    prev = convert->dither_lines = gst_line_cache_new (prev);
    prev->write_input = TRUE;
    prev->pass_alloc = TRUE;
    prev->n_lines = 1;
    prev->stride = convert->current_pstride * convert->current_width;
    gst_line_cache_set_need_line_func (prev, do_dither_lines, convert, NULL);
  }
  return prev;
}

static GstLineCache *
chain_pack (GstVideoConverter * convert, GstLineCache * prev)
{
  convert->pack_nlines = convert->out_info.finfo->pack_lines;
  convert->pack_pstride = convert->current_pstride;
  convert->identity_pack =
      (convert->out_info.finfo->format ==
      convert->out_info.finfo->unpack_format);
  GST_DEBUG ("chain pack line format %s, pstride %d, identity_pack %d (%d %d)",
      gst_video_format_to_string (convert->current_format),
      convert->current_pstride, convert->identity_pack,
      convert->out_info.finfo->format, convert->out_info.finfo->unpack_format);

  return prev;
}

static void
setup_allocators (GstVideoConverter * convert)
{
  GstLineCache *cache;
  GstLineCacheAllocLineFunc alloc_line;
  gboolean alloc_writable;
  gpointer user_data;
  GDestroyNotify notify;
  gint width, n_lines;

  width = MAX (convert->in_maxwidth, convert->out_maxwidth);
  width += convert->out_x;

  n_lines = 1;

  /* start with using dest lines if we can directly write into it */
  if (convert->identity_pack) {
    alloc_line = get_dest_line;
    alloc_writable = TRUE;
    user_data = convert;
    notify = NULL;
  } else {
    user_data =
        converter_alloc_new (sizeof (guint16) * width * 4, 4 + BACKLOG, convert,
        NULL);
    setup_border_alloc (convert, user_data);
    notify = (GDestroyNotify) converter_alloc_free;
    alloc_line = get_border_temp_line;
    alloc_writable = FALSE;
  }

  /* now walk backwards, we try to write into the dest lines directly
   * and keep track if the source needs to be writable */
  for (cache = convert->pack_lines; cache; cache = cache->prev) {
    gst_line_cache_set_alloc_line_func (cache, alloc_line, user_data, notify);
    cache->alloc_writable = alloc_writable;
    n_lines = MAX (n_lines, cache->n_lines);

    /* make sure only one cache frees the allocator */
    notify = NULL;

    if (!cache->pass_alloc) {
      /* can't pass allocator, make new temp line allocator */
      user_data =
          converter_alloc_new (sizeof (guint16) * width * 4, n_lines + BACKLOG,
          convert, NULL);
      notify = (GDestroyNotify) converter_alloc_free;
      alloc_line = get_temp_line;
      alloc_writable = FALSE;
      n_lines = cache->n_lines;
    }
    /* if someone writes to the input, we need a writable line from the
     * previous cache */
    if (cache->write_input)
      alloc_writable = TRUE;
  }
  /* free leftover allocator */
  if (notify)
    notify (user_data);
}

/**
 * gst_video_converter_new:
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @config: a #GstStructure with configuration options
 *
 * Create a new converter object to convert between @in_info and @out_info
 * with @config.
 *
 * Returns: a #GstVideoConverter or %NULL if conversion is not possible.
 *
 * Since: 1.6
 */
GstVideoConverter *
gst_video_converter_new (GstVideoInfo * in_info, GstVideoInfo * out_info,
    GstStructure * config)
{
  GstVideoConverter *convert;
  gint width;
  GstLineCache *prev;
  const GstVideoFormatInfo *fin, *fout, *finfo;

  g_return_val_if_fail (in_info != NULL, NULL);
  g_return_val_if_fail (out_info != NULL, NULL);
  /* we won't ever do framerate conversion */
  g_return_val_if_fail (in_info->fps_n == out_info->fps_n, NULL);
  g_return_val_if_fail (in_info->fps_d == out_info->fps_d, NULL);
  /* we won't ever do deinterlace */
  g_return_val_if_fail (in_info->interlace_mode == out_info->interlace_mode,
      NULL);

  convert = g_slice_new0 (GstVideoConverter);

  fin = in_info->finfo;
  fout = out_info->finfo;

  convert->in_info = *in_info;
  convert->out_info = *out_info;

  /* default config */
  convert->config = gst_structure_new_empty ("GstVideoConverter");
  if (config)
    gst_video_converter_set_config (convert, config);

  convert->in_maxwidth = GST_VIDEO_INFO_WIDTH (in_info);
  convert->in_maxheight = GST_VIDEO_INFO_HEIGHT (in_info);
  convert->out_maxwidth = GST_VIDEO_INFO_WIDTH (out_info);
  convert->out_maxheight = GST_VIDEO_INFO_HEIGHT (out_info);

  convert->in_x = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_SRC_X, 0);
  convert->in_y = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_SRC_Y, 0);
  convert->in_width = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_SRC_WIDTH, convert->in_maxwidth);
  convert->in_height = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_SRC_HEIGHT, convert->in_maxheight);

  convert->in_x &= ~((1 << fin->w_sub[1]) - 1);
  convert->in_y &= ~((1 << fin->h_sub[1]) - 1);

  convert->out_x = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_DEST_X, 0);
  convert->out_y = get_opt_int (convert, GST_VIDEO_CONVERTER_OPT_DEST_Y, 0);
  convert->out_width = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_DEST_WIDTH, convert->out_maxwidth);
  convert->out_height = get_opt_int (convert,
      GST_VIDEO_CONVERTER_OPT_DEST_HEIGHT, convert->out_maxheight);

  convert->out_x &= ~((1 << fout->w_sub[1]) - 1);
  convert->out_y &= ~((1 << fout->h_sub[1]) - 1);

  convert->fill_border = get_opt_bool (convert,
      GST_VIDEO_CONVERTER_OPT_FILL_BORDER, TRUE);
  convert->border_argb = get_opt_uint (convert,
      GST_VIDEO_CONVERTER_OPT_BORDER_ARGB, 0x00000000);

  convert->unpack_format = in_info->finfo->unpack_format;
  finfo = gst_video_format_get_info (convert->unpack_format);
  convert->unpack_bits = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, 0);
  convert->unpack_rgb = GST_VIDEO_FORMAT_INFO_IS_RGB (finfo);

  convert->pack_format = out_info->finfo->unpack_format;
  finfo = gst_video_format_get_info (convert->pack_format);
  convert->pack_bits = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, 0);
  convert->pack_rgb = GST_VIDEO_FORMAT_INFO_IS_RGB (finfo);
  convert->pack_pal =
      gst_video_format_get_palette (GST_VIDEO_INFO_FORMAT (out_info),
      &convert->pack_palsize);

  if (video_converter_lookup_fastpath (convert))
    goto done;

  if (in_info->finfo->unpack_func == NULL)
    goto no_unpack_func;

  if (out_info->finfo->pack_func == NULL)
    goto no_pack_func;

  convert->convert = video_converter_generic;

  convert->current_format = GST_VIDEO_INFO_FORMAT (in_info);
  convert->current_width = convert->in_width;
  convert->current_height = convert->in_height;

  /* unpack */
  prev = chain_unpack_line (convert);
  /* upsample chroma */
  prev = chain_upsample (convert, prev);
  /* convert to gamma decoded RGB */
  prev = chain_convert_to_RGB (convert, prev);
  /* do all downscaling */
  prev = chain_scale (convert, prev, FALSE);
  /* do conversion between color spaces */
  prev = chain_convert (convert, prev);
  /* do all remaining (up)scaling */
  prev = chain_scale (convert, prev, TRUE);
  /* convert to gamma encoded Y'Cb'Cr' */
  prev = chain_convert_to_YUV (convert, prev);
  /* downsample chroma */
  prev = chain_downsample (convert, prev);
  /* dither */
  prev = chain_dither (convert, prev);
  /* pack into final format */
  convert->pack_lines = chain_pack (convert, prev);

  width = MAX (convert->in_maxwidth, convert->out_maxwidth);
  width += convert->out_x;

  if (convert->fill_border && (convert->out_height < convert->out_maxheight ||
          convert->out_width < convert->out_maxwidth)) {
    guint32 border_val;

    convert->borderline = g_malloc0 (sizeof (guint16) * width * 4);

    if (GST_VIDEO_INFO_IS_YUV (&convert->out_info)) {
      /* FIXME, convert to AYUV, just black for now */
      border_val = GINT32_FROM_BE (0x00007f7f);
    } else {
      border_val = GINT32_FROM_BE (convert->border_argb);
    }
    if (convert->pack_bits == 8)
      video_orc_splat_u32 (convert->borderline, border_val, width);
    else
      video_orc_splat_u64 (convert->borderline, border_val, width);
  } else {
    convert->borderline = NULL;
  }

  /* now figure out allocators */
  setup_allocators (convert);

done:
  return convert;

  /* ERRORS */
no_unpack_func:
  {
    GST_ERROR ("no unpack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
    gst_video_converter_free (convert);
    return NULL;
  }
no_pack_func:
  {
    GST_ERROR ("no pack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    gst_video_converter_free (convert);
    return NULL;
  }
}

/**
 * gst_video_converter_free:
 * @convert: a #GstVideoConverter
 *
 * Free @convert
 *
 * Since: 1.6
 */
void
gst_video_converter_free (GstVideoConverter * convert)
{
  gint i;

  g_return_if_fail (convert != NULL);

  if (convert->upsample_p)
    gst_video_chroma_resample_free (convert->upsample_p);
  if (convert->upsample_i)
    gst_video_chroma_resample_free (convert->upsample_i);
  if (convert->downsample_p)
    gst_video_chroma_resample_free (convert->downsample_p);
  if (convert->downsample_i)
    gst_video_chroma_resample_free (convert->downsample_i);
  if (convert->v_scaler_p)
    gst_video_scaler_free (convert->v_scaler_p);
  if (convert->v_scaler_i)
    gst_video_scaler_free (convert->v_scaler_i);
  if (convert->h_scaler)
    gst_video_scaler_free (convert->h_scaler);

  if (convert->unpack_lines)
    gst_line_cache_free (convert->unpack_lines);
  if (convert->upsample_lines)
    gst_line_cache_free (convert->upsample_lines);
  if (convert->to_RGB_lines)
    gst_line_cache_free (convert->to_RGB_lines);
  if (convert->hscale_lines)
    gst_line_cache_free (convert->hscale_lines);
  if (convert->vscale_lines)
    gst_line_cache_free (convert->vscale_lines);
  if (convert->convert_lines)
    gst_line_cache_free (convert->convert_lines);
  if (convert->to_YUV_lines)
    gst_line_cache_free (convert->to_YUV_lines);
  if (convert->downsample_lines)
    gst_line_cache_free (convert->downsample_lines);
  if (convert->dither_lines)
    gst_line_cache_free (convert->dither_lines);

  if (convert->dither)
    gst_video_dither_free (convert->dither);

  g_free (convert->gamma_dec.gamma_table);
  g_free (convert->gamma_enc.gamma_table);

  g_free (convert->tmpline);
  g_free (convert->borderline);

  if (convert->config)
    gst_structure_free (convert->config);

  for (i = 0; i < 4; i++) {
    if (convert->fv_scaler[i])
      gst_video_scaler_free (convert->fv_scaler[i]);
    if (convert->fh_scaler[i])
      gst_video_scaler_free (convert->fh_scaler[i]);
  }
  if (convert->flines)
    converter_alloc_free (convert->flines);

  g_slice_free (GstVideoConverter, convert);
}

static gboolean
copy_config (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstVideoConverter *convert = user_data;

  gst_structure_id_set_value (convert->config, field_id, value);

  return TRUE;
}

/**
 * gst_video_converter_set_config:
 * @convert: a #GstVideoConverter
 * @config: (transfer full): a #GstStructure
 *
 * Set @config as extra configuraion for @convert.
 *
 * If the parameters in @config can not be set exactly, this function returns
 * %FALSE and will try to update as much state as possible. The new state can
 * then be retrieved and refined with gst_video_converter_get_config().
 *
 * Look at the #GST_VIDEO_CONVERTER_OPT_* fields to check valid configuration
 * option and values.
 *
 * Returns: %TRUE when @config could be set.
 *
 * Since: 1.6
 */
gboolean
gst_video_converter_set_config (GstVideoConverter * convert,
    GstStructure * config)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  gst_structure_foreach (config, copy_config, convert);
  gst_structure_free (config);

  return TRUE;
}

/**
 * gst_video_converter_get_config:
 * @@convert: a #GstVideoConverter
 *
 * Get the current configuration of @convert.
 *
 * Returns: a #GstStructure that remains valid for as long as @convert is valid
 *   or until gst_video_converter_set_config() is called.
 */
const GstStructure *
gst_video_converter_get_config (GstVideoConverter * convert)
{
  g_return_val_if_fail (convert != NULL, NULL);

  return convert->config;
}

/**
 * gst_video_converter_frame:
 * @convert: a #GstVideoConverter
 * @dest: a #GstVideoFrame
 * @src: a #GstVideoFrame
 *
 * Convert the pixels of @src into @dest using @convert.
 *
 * Since: 1.6
 */
void
gst_video_converter_frame (GstVideoConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest)
{
  g_return_if_fail (convert != NULL);
  g_return_if_fail (src != NULL);
  g_return_if_fail (dest != NULL);

  convert->convert (convert, src, dest);
}

static void
video_converter_compute_matrix (GstVideoConverter * convert)
{
  MatrixData *dst = &convert->convert_matrix;

  color_matrix_set_identity (dst);
  compute_matrix_to_RGB (convert, dst);
  compute_matrix_to_YUV (convert, dst);

  convert->current_bits = 8;
  prepare_matrix (convert, dst);
}

static void
video_converter_compute_resample (GstVideoConverter * convert)
{
  GstVideoInfo *in_info, *out_info;
  const GstVideoFormatInfo *sfinfo, *dfinfo;

  if (CHECK_CHROMA_NONE (convert))
    return;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  sfinfo = in_info->finfo;
  dfinfo = out_info->finfo;

  GST_DEBUG ("site: %d->%d, w_sub: %d->%d, h_sub: %d->%d", in_info->chroma_site,
      out_info->chroma_site, sfinfo->w_sub[2], dfinfo->w_sub[2],
      sfinfo->h_sub[2], dfinfo->h_sub[2]);

  if (sfinfo->w_sub[2] != dfinfo->w_sub[2] ||
      sfinfo->h_sub[2] != dfinfo->h_sub[2] ||
      in_info->chroma_site != out_info->chroma_site ||
      in_info->width != out_info->width ||
      in_info->height != out_info->height) {
    if (GST_VIDEO_INFO_IS_INTERLACED (in_info)) {
      if (!CHECK_CHROMA_DOWNSAMPLE (convert))
        convert->upsample_i = gst_video_chroma_resample_new (0,
            in_info->chroma_site, GST_VIDEO_CHROMA_FLAG_INTERLACED,
            sfinfo->unpack_format, sfinfo->w_sub[2], sfinfo->h_sub[2]);
      if (!CHECK_CHROMA_UPSAMPLE (convert))
        convert->downsample_i =
            gst_video_chroma_resample_new (0, out_info->chroma_site,
            GST_VIDEO_CHROMA_FLAG_INTERLACED, dfinfo->unpack_format,
            -dfinfo->w_sub[2], -dfinfo->h_sub[2]);
    }
    if (!CHECK_CHROMA_DOWNSAMPLE (convert))
      convert->upsample_p = gst_video_chroma_resample_new (0,
          in_info->chroma_site, 0, sfinfo->unpack_format, sfinfo->w_sub[2],
          sfinfo->h_sub[2]);
    if (!CHECK_CHROMA_UPSAMPLE (convert))
      convert->downsample_p = gst_video_chroma_resample_new (0,
          out_info->chroma_site, 0, dfinfo->unpack_format, -dfinfo->w_sub[2],
          -dfinfo->h_sub[2]);
  }
}

#define FRAME_GET_PLANE_STRIDE(frame, plane) \
  GST_VIDEO_FRAME_PLANE_STRIDE (frame, plane)
#define FRAME_GET_PLANE_LINE(frame, plane, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_PLANE_DATA (frame, plane))) + \
      FRAME_GET_PLANE_STRIDE (frame, plane) * (line))

#define FRAME_GET_COMP_STRIDE(frame, comp) \
  GST_VIDEO_FRAME_COMP_STRIDE (frame, comp)
#define FRAME_GET_COMP_LINE(frame, comp, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_COMP_DATA (frame, comp))) + \
      FRAME_GET_COMP_STRIDE (frame, comp) * (line))

#define FRAME_GET_STRIDE(frame)      FRAME_GET_PLANE_STRIDE (frame, 0)
#define FRAME_GET_LINE(frame,line)   FRAME_GET_PLANE_LINE (frame, 0, line)

#define FRAME_GET_Y_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_Y, line)
#define FRAME_GET_U_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_U, line)
#define FRAME_GET_V_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_V, line)
#define FRAME_GET_A_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_A, line)

#define FRAME_GET_Y_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_Y)
#define FRAME_GET_U_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_U)
#define FRAME_GET_V_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_V)
#define FRAME_GET_A_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_A)


#define UNPACK_FRAME(frame,dest,line,x,width)        \
  frame->info.finfo->unpack_func (frame->info.finfo, \
      (GST_VIDEO_FRAME_IS_INTERLACED (frame) ?       \
        GST_VIDEO_PACK_FLAG_INTERLACED :             \
        GST_VIDEO_PACK_FLAG_NONE),                   \
      dest, frame->data, frame->info.stride, x,      \
      line, width)
#define PACK_FRAME(frame,src,line,width)             \
  frame->info.finfo->pack_func (frame->info.finfo,   \
      (GST_VIDEO_FRAME_IS_INTERLACED (frame) ?       \
        GST_VIDEO_PACK_FLAG_INTERLACED :             \
        GST_VIDEO_PACK_FLAG_NONE),                   \
      src, 0, frame->data, frame->info.stride,       \
      frame->info.chroma_site, line, width);

static gpointer
get_dest_line (GstLineCache * cache, gint idx, gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  guint8 *line;
  gint pstride = convert->pack_pstride;
  gint out_x = convert->out_x;
  guint cline;

  cline = CLAMP (idx, 0, convert->out_maxheight - 1);

  GST_DEBUG ("get dest line %d", cline);
  line = FRAME_GET_LINE (convert->dest, cline);

  if (convert->borderline) {
    gint r_border = (out_x + convert->out_width) * pstride;
    gint rb_width = convert->out_maxwidth * pstride - r_border;
    gint lb_width = out_x * pstride;

    memcpy (line, convert->borderline, lb_width);
    memcpy (line + r_border, convert->borderline, rb_width);
  }
  line += out_x * pstride;

  return line;
}

static gboolean
do_unpack_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer tmpline;
  guint cline;

  cline = CLAMP (in_line + convert->in_y, 0, convert->in_maxheight - 1);

  if (cache->alloc_writable || !convert->identity_unpack) {
    tmpline = gst_line_cache_alloc_line (cache, out_line);
    GST_DEBUG ("unpack line %d (%u) %p", in_line, cline, tmpline);
    UNPACK_FRAME (convert->src, tmpline, cline, convert->in_x,
        convert->in_width);
  } else {
    tmpline = ((guint8 *) FRAME_GET_LINE (convert->src, cline)) +
        convert->in_x * convert->unpack_pstride;
    GST_DEBUG ("get src line %d (%u) %p", in_line, cline, tmpline);
  }
  gst_line_cache_add_line (cache, in_line, tmpline);

  return TRUE;
}

static gboolean
do_upsample_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines;
  gint i, start_line, n_lines;

  n_lines = convert->up_n_lines;
  start_line = in_line;
  if (start_line < n_lines + convert->up_offset)
    start_line += convert->up_offset;

  /* get the lines needed for chroma upsample */
  lines = gst_line_cache_get_lines (cache->prev, out_line, start_line, n_lines);

  if (convert->upsample) {
    GST_DEBUG ("doing upsample %d-%d %p", start_line, start_line + n_lines - 1,
        lines[0]);
    gst_video_chroma_resample (convert->upsample, lines, convert->in_width);
  }

  for (i = 0; i < n_lines; i++)
    gst_line_cache_add_line (cache, start_line + i, lines[i]);

  return TRUE;
}

static gboolean
do_convert_to_RGB_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  MatrixData *data = &convert->to_RGB_matrix;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);
  destline = lines[0];

  if (data->matrix_func) {
    GST_DEBUG ("to RGB line %d %p", in_line, destline);
    data->matrix_func (data, destline);
  }
  if (convert->gamma_dec.gamma_func) {
    destline = gst_line_cache_alloc_line (cache, out_line);

    GST_DEBUG ("gamma decode line %d %p->%p", in_line, lines[0], destline);
    convert->gamma_dec.gamma_func (&convert->gamma_dec, destline, lines[0]);
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_hscale_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);

  destline = gst_line_cache_alloc_line (cache, out_line);

  GST_DEBUG ("hresample line %d %p->%p", in_line, lines[0], destline);
  gst_video_scaler_horizontal (convert->h_scaler, convert->h_scale_format,
      lines[0], destline, 0, convert->out_width);

  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_vscale_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines, destline;
  guint sline, n_lines;
  guint cline;

  cline = CLAMP (in_line, 0, convert->out_height - 1);

  gst_video_scaler_get_coeff (convert->v_scaler, cline, &sline, &n_lines);
  lines = gst_line_cache_get_lines (cache->prev, out_line, sline, n_lines);

  destline = gst_line_cache_alloc_line (cache, out_line);

  GST_DEBUG ("vresample line %d %d-%d %p->%p", in_line, sline,
      sline + n_lines - 1, lines[0], destline);
  gst_video_scaler_vertical (convert->v_scaler, convert->v_scale_format, lines,
      destline, cline, convert->v_scale_width);

  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_convert_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  MatrixData *data = &convert->convert_matrix;
  gpointer *lines, destline;
  guint in_bits, out_bits;
  gint width;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);

  destline = lines[0];

  in_bits = convert->in_bits;
  out_bits = convert->out_bits;

  width = MIN (convert->in_width, convert->out_width);

  if (out_bits == 16 || in_bits == 16) {
    gpointer srcline = lines[0];

    if (out_bits != in_bits)
      destline = gst_line_cache_alloc_line (cache, out_line);

    /* FIXME, we can scale in the conversion matrix */
    if (in_bits == 8) {
      GST_DEBUG ("8->16 line %d %p->%p", in_line, srcline, destline);
      video_orc_convert_u8_to_u16 (destline, srcline, width * 4);
      srcline = destline;
    }

    if (data->matrix_func) {
      GST_DEBUG ("matrix line %d %p", in_line, srcline);
      data->matrix_func (data, srcline);
    }

    /* FIXME, dither here */
    if (out_bits == 8) {
      GST_DEBUG ("16->8 line %d %p->%p", in_line, srcline, destline);
      video_orc_convert_u16_to_u8 (destline, srcline, width * 4);
    }
  } else {
    if (data->matrix_func) {
      GST_DEBUG ("matrix line %d %p", in_line, destline);
      data->matrix_func (data, destline);
    }
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_convert_to_YUV_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  MatrixData *data = &convert->to_YUV_matrix;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);
  destline = lines[0];

  if (convert->gamma_enc.gamma_func) {
    destline = gst_line_cache_alloc_line (cache, out_line);

    GST_DEBUG ("gamma encode line %d %p->%p", in_line, lines[0], destline);
    convert->gamma_enc.gamma_func (&convert->gamma_enc, destline, lines[0]);
  }
  if (data->matrix_func) {
    GST_DEBUG ("to YUV line %d %p", in_line, destline);
    data->matrix_func (data, destline);
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static gboolean
do_downsample_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines;
  gint i, start_line, n_lines;

  n_lines = convert->down_n_lines;
  start_line = in_line;
  if (start_line < n_lines + convert->down_offset)
    start_line += convert->down_offset;

  /* get the lines needed for chroma downsample */
  lines = gst_line_cache_get_lines (cache->prev, out_line, start_line, n_lines);

  if (convert->downsample) {
    GST_DEBUG ("downsample line %d %d-%d %p", in_line, start_line,
        start_line + n_lines - 1, lines[0]);
    gst_video_chroma_resample (convert->downsample, lines, convert->out_width);
  }

  for (i = 0; i < n_lines; i++)
    gst_line_cache_add_line (cache, start_line + i, lines[i]);

  return TRUE;
}

static gboolean
do_dither_lines (GstLineCache * cache, gint out_line, gint in_line,
    gpointer user_data)
{
  GstVideoConverter *convert = user_data;
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (cache->prev, out_line, in_line, 1);
  destline = lines[0];

  if (convert->dither) {
    GST_DEBUG ("Dither line %d %p", in_line, destline);
    gst_video_dither_line (convert->dither, destline, 0, out_line,
        convert->out_width);
  }
  gst_line_cache_add_line (cache, in_line, destline);

  return TRUE;
}

static void
video_converter_generic (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint i;
  gint out_maxwidth, out_maxheight;
  gint out_x, out_y, out_height;
  gint pack_lines, pstride;
  gint lb_width;

  out_height = convert->out_height;
  out_maxwidth = convert->out_maxwidth;
  out_maxheight = convert->out_maxheight;

  out_x = convert->out_x;
  out_y = convert->out_y;

  convert->src = src;
  convert->dest = dest;

  if (GST_VIDEO_FRAME_IS_INTERLACED (src)) {
    GST_DEBUG ("setup interlaced frame");
    convert->upsample = convert->upsample_i;
    convert->downsample = convert->downsample_i;
    convert->v_scaler = convert->v_scaler_i;
  } else {
    GST_DEBUG ("setup progressive frame");
    convert->upsample = convert->upsample_p;
    convert->downsample = convert->downsample_p;
    convert->v_scaler = convert->v_scaler_p;
  }
  if (convert->upsample) {
    gst_video_chroma_resample_get_info (convert->upsample,
        &convert->up_n_lines, &convert->up_offset);
  } else {
    convert->up_n_lines = 1;
    convert->up_offset = 0;
  }
  if (convert->downsample) {
    gst_video_chroma_resample_get_info (convert->downsample,
        &convert->down_n_lines, &convert->down_offset);
  } else {
    convert->down_n_lines = 1;
    convert->down_offset = 0;
  }

  pack_lines = convert->pack_nlines;    /* only 1 for now */
  pstride = convert->pack_pstride;

  lb_width = out_x * pstride;

  if (convert->borderline) {
    /* FIXME we should try to avoid PACK_FRAME */
    for (i = 0; i < out_y; i++)
      PACK_FRAME (dest, convert->borderline, i, out_maxwidth);
  }

  for (i = 0; i < out_height; i += pack_lines) {
    gpointer *lines;

    /* load the lines needed to pack */
    lines = gst_line_cache_get_lines (convert->pack_lines, i + out_y,
        i, pack_lines);

    if (!convert->identity_pack) {
      /* take away the border */
      guint8 *l = ((guint8 *) lines[0]) - lb_width;
      /* and pack into destination */
      GST_DEBUG ("pack line %d %p (%p)", i + out_y, lines[0], l);
      PACK_FRAME (dest, l, i + out_y, out_maxwidth);
    }
  }

  if (convert->borderline) {
    for (i = out_y + out_height; i < out_maxheight; i++)
      PACK_FRAME (dest, convert->borderline, i, out_maxwidth);
  }
  if (convert->pack_pal) {
    memcpy (GST_VIDEO_FRAME_PLANE_DATA (dest, 1), convert->pack_pal,
        convert->pack_palsize);
  }
}

/* Fast paths */

#define GET_LINE_OFFSETS(interlaced,line,l1,l2) \
    if (interlaced) {                           \
      l1 = (line & 2 ? line - 1 : line);        \
      l2 = l1 + 2;                              \
    } else {                                    \
      l1 = line;                                \
      l2 = l1 + 1;                              \
    }

static void
convert_I420_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_I420_YUY2 (FRAME_GET_LINE (dest, l1),
        FRAME_GET_LINE (dest, l2),
        FRAME_GET_Y_LINE (src, l1),
        FRAME_GET_Y_LINE (src, l2),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_I420_UYVY (FRAME_GET_LINE (dest, l1),
        FRAME_GET_LINE (dest, l2),
        FRAME_GET_Y_LINE (src, l1),
        FRAME_GET_Y_LINE (src, l2),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_I420_AYUV (FRAME_GET_LINE (dest, l1),
        FRAME_GET_LINE (dest, l2),
        FRAME_GET_Y_LINE (src, l1),
        FRAME_GET_Y_LINE (src, l2),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1), width);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_420_422 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  video_orc_planar_chroma_420_422 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);
}

static void
convert_I420_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_420_444 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  video_orc_planar_chroma_420_444 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_YUY2_I420 (FRAME_GET_Y_LINE (dest, l1),
        FRAME_GET_Y_LINE (dest, l2),
        FRAME_GET_U_LINE (dest, i >> 1),
        FRAME_GET_V_LINE (dest, i >> 1),
        FRAME_GET_LINE (src, l1), FRAME_GET_LINE (src, l2), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_YUY2_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_YUY2_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_YUY2_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_YUY2_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_YUY2_Y444 (FRAME_GET_COMP_LINE (dest, 0, 0),
      FRAME_GET_COMP_STRIDE (dest, 0), FRAME_GET_COMP_LINE (dest, 1, 0),
      FRAME_GET_COMP_STRIDE (dest, 1), FRAME_GET_COMP_LINE (dest, 2, 0),
      FRAME_GET_COMP_STRIDE (dest, 2), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}


static void
convert_UYVY_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  gboolean interlaced = GST_VIDEO_FRAME_IS_INTERLACED (src);
  gint l1, l2;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    GET_LINE_OFFSETS (interlaced, i, l1, l2);

    video_orc_convert_UYVY_I420 (FRAME_GET_COMP_LINE (dest, 0, l1),
        FRAME_GET_COMP_LINE (dest, 0, l2),
        FRAME_GET_COMP_LINE (dest, 1, i >> 1),
        FRAME_GET_COMP_LINE (dest, 2, i >> 1),
        FRAME_GET_LINE (src, l1), FRAME_GET_LINE (src, l2), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_UYVY_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_UYVY_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_AYUV_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width/height */
  video_orc_convert_AYUV_I420 (FRAME_GET_Y_LINE (dest, 0),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (dest, 1),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      2 * FRAME_GET_STRIDE (src), FRAME_GET_LINE (src, 1),
      2 * FRAME_GET_STRIDE (src), width / 2, height / 2);
}

static void
convert_AYUV_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width */
  video_orc_convert_AYUV_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width */
  video_orc_convert_AYUV_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only works for even width */
  video_orc_convert_AYUV_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_Y42B_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_422_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  video_orc_planar_chroma_422_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_Y42B_Y444 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_422_444 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height);

  video_orc_planar_chroma_422_444 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y42B_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y42B_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  /* only for even width */
  video_orc_convert_Y42B_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_I420 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_444_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), width / 2, height / 2);

  video_orc_planar_chroma_444_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), width / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_Y444_Y42B (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), width, height);

  video_orc_planar_chroma_444_422 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), width / 2, height);

  video_orc_planar_chroma_444_422 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_YUY2 (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y444_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_UYVY (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y444_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width / 2, height);
}

static void
convert_Y444_AYUV (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_Y444_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width, height);
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static void
convert_AYUV_ARGB (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  MatrixData *data = &convert->convert_matrix;
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_ARGB (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_AYUV_BGRA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  video_orc_convert_AYUV_BGRA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_AYUV_ABGR (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  video_orc_convert_AYUV_ABGR (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_AYUV_RGBA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  video_orc_convert_AYUV_RGBA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), data->im[0][0], data->im[0][2],
      data->im[2][1], data->im[1][1], data->im[1][2], width, height);
}

static void
convert_I420_BGRA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;
  MatrixData *data = &convert->convert_matrix;

  for (i = 0; i < height; i++) {
    video_orc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1),
        data->im[0][0], data->im[0][2],
        data->im[2][1], data->im[1][1], data->im[1][2], width);
  }
}
#endif

#define GET_TMP_LINE(fl,idx) &fl->data[fl->stride * ((idx) % fl->n_lines)]

static void
convert_scale_planes (GstVideoConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest)
{
  int k, n_planes;
  GstFormat format = convert->fformat;

  n_planes = GST_VIDEO_FRAME_N_PLANES (src);

  for (k = 0; k < n_planes; k++) {
    gint i, tmp_in, out_w, out_h;
    GstVideoScaler *v_scaler, *h_scaler;
    ConverterAlloc *alloc;

    v_scaler = convert->fv_scaler[k];
    h_scaler = convert->fh_scaler[k];

    alloc = convert->flines;

    /* FIXME. assumes subsampling of component N is the same as plane N, which is
     * currently true for all formats we have but it might not be in the future. */
    out_w = GST_VIDEO_FRAME_COMP_WIDTH (dest, k);
    out_h = GST_VIDEO_FRAME_COMP_HEIGHT (dest, k);

    tmp_in = 0;
    for (i = 0; i < out_h; i++) {
      guint8 *d, *s;
      guint in, n_taps, j;
      gpointer lines[32];

      gst_video_scaler_get_coeff (v_scaler, i, &in, &n_taps);

      while (tmp_in < in + n_taps) {
        s = FRAME_GET_PLANE_LINE (src, k, tmp_in);
        gst_video_scaler_horizontal (h_scaler, format,
            s, GET_TMP_LINE (alloc, tmp_in), 0, out_w);
        tmp_in++;
      }
      for (j = 0; j < n_taps; j++)
        lines[j] = GET_TMP_LINE (alloc, in + j);

      d = FRAME_GET_PLANE_LINE (dest, k, i);
      gst_video_scaler_vertical (v_scaler, format, lines, d, i, out_w);
    }
  }
}

static void
setup_scale (GstVideoConverter * convert)
{
  int i, n_planes;
  gint method, stride = 0;
  guint taps, max_taps = 0;
  GstVideoInfo *in_info, *out_info;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  n_planes = GST_VIDEO_INFO_N_PLANES (in_info);

  method = GET_OPT_RESAMPLER_METHOD (convert);
  taps = GET_OPT_RESAMPLER_TAPS (convert);

  if (n_planes == 1) {
    if (GST_VIDEO_INFO_IS_YUV (in_info)) {
      GstVideoScaler *y_scaler, *uv_scaler;

      y_scaler = gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
          GST_VIDEO_INFO_COMP_WIDTH (in_info, GST_VIDEO_COMP_Y),
          GST_VIDEO_INFO_COMP_WIDTH (out_info, GST_VIDEO_COMP_Y),
          convert->config);
      uv_scaler =
          gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
          GST_VIDEO_INFO_COMP_WIDTH (in_info, GST_VIDEO_COMP_U),
          GST_VIDEO_INFO_COMP_WIDTH (out_info, GST_VIDEO_COMP_U),
          convert->config);

      convert->fh_scaler[0] =
          gst_video_scaler_combine_packed_YUV (y_scaler, uv_scaler,
          GST_VIDEO_INFO_FORMAT (in_info), GST_VIDEO_INFO_FORMAT (out_info));

      gst_video_scaler_free (y_scaler);
      gst_video_scaler_free (uv_scaler);
    } else {
      convert->fh_scaler[0] =
          gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
          GST_VIDEO_INFO_WIDTH (in_info), GST_VIDEO_INFO_WIDTH (out_info),
          convert->config);
    }
    stride = MAX (stride, GST_VIDEO_INFO_PLANE_STRIDE (in_info, 0));
    stride = MAX (stride, GST_VIDEO_INFO_PLANE_STRIDE (out_info, 0));

    convert->fv_scaler[0] =
        gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
        GST_VIDEO_INFO_HEIGHT (in_info), GST_VIDEO_INFO_HEIGHT (out_info),
        convert->config);

    gst_video_scaler_get_coeff (convert->fv_scaler[0], 0, NULL, &max_taps);
    convert->fformat = GST_VIDEO_INFO_FORMAT (in_info);
  } else {
    for (i = 0; i < n_planes; i++) {
      guint n_taps;

      stride = MAX (stride, GST_VIDEO_INFO_COMP_STRIDE (in_info, i));
      stride = MAX (stride, GST_VIDEO_INFO_COMP_STRIDE (out_info, i));

      convert->fh_scaler[i] =
          gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
          GST_VIDEO_INFO_COMP_WIDTH (in_info, i),
          GST_VIDEO_INFO_COMP_WIDTH (out_info, i), convert->config);
      convert->fv_scaler[i] =
          gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
          GST_VIDEO_INFO_COMP_HEIGHT (in_info, i),
          GST_VIDEO_INFO_COMP_HEIGHT (out_info, i), convert->config);

      gst_video_scaler_get_coeff (convert->fv_scaler[i], 0, NULL, &n_taps);
      max_taps = MAX (max_taps, n_taps);
    }
    convert->fformat = GST_VIDEO_FORMAT_GRAY8;
  }
  convert->flines =
      converter_alloc_new (stride, max_taps + BACKLOG, NULL, NULL);
}

/* Fast paths */

typedef struct
{
  GstVideoFormat in_format;
  GstVideoFormat out_format;
  gboolean keeps_interlaced;
  gboolean needs_color_matrix;
  gboolean keeps_size;
  gint width_align, height_align;
  void (*convert) (GstVideoConverter * convert, const GstVideoFrame * src,
      GstVideoFrame * dest);
} VideoTransform;

static const VideoTransform transforms[] = {
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_YUY2},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_UYVY},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_AYUV},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_Y42B, FALSE, FALSE, TRUE, 0, 0,
      convert_I420_Y42B},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_Y444, FALSE, FALSE, TRUE, 0, 0,
      convert_I420_Y444},

  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_YUY2},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_UYVY},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_AYUV},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_Y42B, FALSE, FALSE, TRUE, 0, 0,
      convert_I420_Y42B},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_Y444, FALSE, FALSE, TRUE, 0, 0,
      convert_I420_Y444},

  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_I420, TRUE, FALSE, TRUE, 0, 0,
      convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_YV12, TRUE, FALSE, TRUE, 0, 0,
      convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_YUY2},       /* alias */
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, TRUE, 0, 0,
      convert_YUY2_AYUV},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, TRUE, 0, 0,
      convert_YUY2_Y42B},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, TRUE, 0, 0,
      convert_YUY2_Y444},

  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_I420, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_YV12, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_YUY2},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_AYUV},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_Y42B},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, TRUE, 0, 0,
      convert_UYVY_Y444},

  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_I420, FALSE, FALSE, TRUE, 1, 1,
      convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_YV12, FALSE, FALSE, TRUE, 1, 1,
      convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, TRUE, 1, 0,
      convert_AYUV_YUY2},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, TRUE, 1, 0,
      convert_AYUV_UYVY},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, TRUE, 1, 0,
      convert_AYUV_Y42B},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, TRUE, 0, 0,
      convert_AYUV_Y444},

  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_I420, FALSE, FALSE, TRUE, 0, 0,
      convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_YV12, FALSE, FALSE, TRUE, 0, 0,
      convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, TRUE, 0, 0,
      convert_Y42B_YUY2},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, TRUE, 0, 0,
      convert_Y42B_UYVY},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, TRUE, 1, 0,
      convert_Y42B_AYUV},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_Y444, TRUE, FALSE, TRUE, 0, 0,
      convert_Y42B_Y444},

  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_I420, FALSE, FALSE, TRUE, 1, 0,
      convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_YV12, FALSE, FALSE, TRUE, 1, 0,
      convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, TRUE, 1, 0,
      convert_Y444_YUY2},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, TRUE, 1, 0,
      convert_Y444_UYVY},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_AYUV, TRUE, FALSE, TRUE, 0, 0,
      convert_Y444_AYUV},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, TRUE, 1, 0,
      convert_Y444_Y42B},

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_ARGB, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_ARGB},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_BGRA, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_BGRA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_xRGB, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_ARGB},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_BGRx, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_BGRA},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_ABGR, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_ABGR},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGBA, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_RGBA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_xBGR, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_ABGR},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGBx, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_RGBA},       /* alias */

  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_BGRA, FALSE, TRUE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_BGRx, FALSE, TRUE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_BGRA, FALSE, TRUE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_BGRx, FALSE, TRUE, TRUE, 0, 0,
      convert_I420_BGRA},
#endif

  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_I420, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_YV12, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_Y41B, GST_VIDEO_FORMAT_Y41B, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_Y42B, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_A420, GST_VIDEO_FORMAT_A420, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_YUV9, GST_VIDEO_FORMAT_YUV9, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_YVU9, GST_VIDEO_FORMAT_YVU9, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},

  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_YUY2, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_UYVY, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
  {GST_VIDEO_FORMAT_YVYU, GST_VIDEO_FORMAT_YVYU, TRUE, FALSE, FALSE, 0, 0,
      convert_scale_planes},
};

static gboolean
video_converter_lookup_fastpath (GstVideoConverter * convert)
{
  int i;
  GstVideoFormat in_format, out_format;
  GstVideoTransferFunction in_transf, out_transf;
  gboolean interlaced, same_matrix, same_primaries, same_size;
  gint width, height;

  width = GST_VIDEO_INFO_WIDTH (&convert->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&convert->in_info);

  if (GET_OPT_DITHER_QUANTIZATION (convert) != 1)
    return FALSE;

  /* we don't do gamma conversion in fastpath */
  in_transf = convert->in_info.colorimetry.transfer;
  out_transf = convert->out_info.colorimetry.transfer;
  if (CHECK_GAMMA_REMAP (convert) && in_transf != out_transf)
    return FALSE;

  same_size = (width == convert->out_width && height == convert->out_height);

  in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);

  if (CHECK_MATRIX_NONE (convert)) {
    same_matrix = TRUE;
  } else {
    GstVideoColorMatrix in_matrix, out_matrix;

    in_matrix = convert->in_info.colorimetry.matrix;
    out_matrix = convert->out_info.colorimetry.matrix;
    same_matrix = in_matrix == out_matrix;
  }

  if (CHECK_PRIMARIES_NONE (convert)) {
    same_primaries = TRUE;
  } else {
    GstVideoColorPrimaries in_primaries, out_primaries;

    in_primaries = convert->in_info.colorimetry.primaries;
    out_primaries = convert->out_info.colorimetry.primaries;
    same_primaries = in_primaries == out_primaries;
  }

  interlaced = GST_VIDEO_INFO_IS_INTERLACED (&convert->in_info);
  interlaced |= GST_VIDEO_INFO_IS_INTERLACED (&convert->out_info);

  for (i = 0; i < sizeof (transforms) / sizeof (transforms[0]); i++) {
    if (transforms[i].in_format == in_format &&
        transforms[i].out_format == out_format &&
        (transforms[i].keeps_interlaced || !interlaced) &&
        (transforms[i].needs_color_matrix || (same_matrix && same_primaries))
        && (transforms[i].keeps_size || !same_size)
        && (transforms[i].width_align & width) == 0
        && (transforms[i].height_align & height) == 0) {
      GST_DEBUG ("using fastpath");
      if (transforms[i].needs_color_matrix)
        video_converter_compute_matrix (convert);
      convert->convert = transforms[i].convert;
      convert->tmpline = g_malloc0 (sizeof (guint16) * (width + 8) * 4);
      if (!transforms[i].keeps_size)
        setup_scale (convert);
      return TRUE;
    }
  }
  GST_DEBUG ("no fastpath found");
  return FALSE;
}
