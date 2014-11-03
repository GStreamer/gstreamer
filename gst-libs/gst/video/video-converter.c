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

typedef struct _GstLineCache GstLineCache;

struct _GstVideoConverter
{
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
  GstVideoFormat current_format;

  gint in_bits;
  gint out_bits;
  gint cmatrix[4][4];
  guint64 orc_p1;
  guint64 orc_p2;
  guint64 orc_p3;

  GstStructure *config;
  GstVideoDitherMethod dither;

  guint lines;

  guint n_tmplines;
  gpointer *tmplines;
  guint16 *errline;
  guint tmplines_idx;

  gboolean fill_border;
  gpointer borderline;
  guint32 border_argb;

  GstVideoChromaResample *upsample;
  guint up_n_lines;
  gint up_offset;
  GstVideoChromaResample *downsample;
  guint down_n_lines;
  gint down_offset;

  void (*convert) (GstVideoConverter * convert, const GstVideoFrame * src,
      GstVideoFrame * dest);
  void (*matrix) (GstVideoConverter * convert, gpointer pixels);
  void (*dither16) (GstVideoConverter * convert, guint16 * pixels, int j);

  GstLineCache *upsample_lines;
  GstLineCache *hscale_lines;
  GstLineCache *vscale_lines;
  GstLineCache *convert_lines;
  GstLineCache *downsample_lines;
  GstLineCache *pack_lines;

  GstVideoScaler *h_scaler;
  gint h_scale_format;
  GstVideoScaler *v_scaler;
  gint v_scale_width;
  gint v_scale_format;
  gint pack_pstride;

  const GstVideoFrame *src;
  GstVideoFrame *dest;
};

typedef gboolean (*GstLineCacheNeedLineFunc) (GstLineCache * cache, gint idx,
    gpointer user_data);
typedef void (*GstLineCacheFreeLineFunc) (GstLineCache * cache, gint idx,
    gpointer line, gpointer user_data);

struct _GstLineCache
{
  gint first;
  GPtrArray *lines;

  GstLineCacheNeedLineFunc need_line;
  gpointer need_line_data;
  GDestroyNotify need_line_notify;
  GstLineCacheFreeLineFunc free_line;
  gpointer free_line_data;
  GDestroyNotify free_line_notify;
};

static GstLineCache *
gst_line_cache_new ()
{
  GstLineCache *result;

  result = g_slice_new0 (GstLineCache);
  result->lines = g_ptr_array_new ();

  return result;
}

static void
gst_line_cache_clear (GstLineCache * cache)
{
  g_return_if_fail (cache != NULL);

  if (cache->free_line) {
    gint i;
    for (i = 0; i < cache->lines->len; i++)
      cache->free_line (cache, cache->first + i,
          *(cache->lines->pdata + i), cache->free_line_data);
  }
  g_ptr_array_set_size (cache->lines, 0);
  cache->first = 0;
}

static void
gst_line_cache_free (GstLineCache * cache)
{
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
gst_line_cache_set_free_line_func (GstLineCache * cache,
    GstLineCacheFreeLineFunc free_line, gpointer user_data,
    GDestroyNotify notify)
{
  cache->free_line = free_line;
  cache->free_line_data = user_data;
  cache->free_line_notify = notify;
}

static gpointer *
gst_line_cache_get_lines (GstLineCache * cache, gint idx, gint n_lines)
{
  if (cache->first < idx) {
    gint i, to_remove = MIN (idx - cache->first, cache->lines->len);
    if (cache->free_line) {
      for (i = 0; i < to_remove; i++)
        cache->free_line (cache, cache->first + i,
            *(cache->lines->pdata + i), cache->free_line_data);
    }
    if (to_remove > 0)
      g_ptr_array_remove_range (cache->lines, 0, to_remove);
    cache->first = idx;
  } else if (idx < cache->first) {
    gst_line_cache_clear (cache);
    cache->first = idx;
  }

  while (TRUE) {
    if (cache->first <= idx
        && idx + n_lines <= cache->first + (gint) cache->lines->len) {
      return cache->lines->pdata + (idx - cache->first);
    }

    if (cache->need_line == NULL)
      break;

    if (!cache->need_line (cache, cache->first + cache->lines->len,
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

static void video_converter_generic (GstVideoConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest);
static void video_converter_matrix8 (GstVideoConverter * convert,
    gpointer pixels);
static void video_converter_matrix16 (GstVideoConverter * convert,
    gpointer pixels);
static gboolean video_converter_lookup_fastpath (GstVideoConverter * convert);
static void video_converter_compute_matrix (GstVideoConverter * convert);
static void video_converter_compute_resample (GstVideoConverter * convert);

static gboolean do_unpack_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert);
static gboolean do_downsample_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert);
static gboolean do_convert_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert);
static gboolean do_upsample_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert);
static gboolean do_vscale_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert);
static gboolean do_hscale_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert);

static void
free_pack_line (GstLineCache * cache, gint idx,
    gpointer line, gpointer user_data)
{
  GST_DEBUG ("line %d %p", idx, line);
}

static void
alloc_tmplines (GstVideoConverter * convert, guint lines, gint width)
{
  gint i;

  convert->n_tmplines = lines;
  convert->tmplines = g_malloc (lines * sizeof (gpointer));
  for (i = 0; i < lines; i++) {
    convert->tmplines[i] = g_malloc (sizeof (guint16) * (width + 8) * 4);
    if (convert->borderline)
      memcpy (convert->tmplines[i], convert->borderline, width * 8);
  }
  convert->tmplines_idx = 0;
}

static gpointer
get_temp_line (GstVideoConverter * convert, gint offset)
{
  gpointer tmpline;

  tmpline = (guint8 *) convert->tmplines[convert->tmplines_idx] +
      (offset * convert->pack_pstride);
  convert->tmplines_idx = (convert->tmplines_idx + 1) % convert->n_tmplines;

  return tmpline;
}

static GstLineCacheNeedLineFunc
chain_unpack_line (GstVideoConverter * convert)
{
  convert->current_format = convert->in_info.finfo->format;
  convert->current_pstride = convert->in_bits >> 1;
  return (GstLineCacheNeedLineFunc) do_unpack_lines;
}

static GstLineCacheNeedLineFunc
chain_upsample (GstVideoConverter * convert, GstLineCacheNeedLineFunc need_line)
{
  video_converter_compute_resample (convert);

  if (convert->upsample) {
    convert->upsample_lines = gst_line_cache_new ();
    gst_line_cache_set_need_line_func (convert->upsample_lines,
        need_line, convert, NULL);
    need_line = (GstLineCacheNeedLineFunc) do_upsample_lines;
  }
  return need_line;
}

static GstLineCacheNeedLineFunc
chain_convert (GstVideoConverter * convert, GstLineCacheNeedLineFunc need_line)
{
  convert->convert_lines = gst_line_cache_new ();
  gst_line_cache_set_need_line_func (convert->convert_lines,
      need_line, convert, NULL);
  convert->current_format = convert->out_info.finfo->format;
  convert->current_pstride = convert->out_bits >> 1;
  return (GstLineCacheNeedLineFunc) do_convert_lines;
}

static GstLineCacheNeedLineFunc
chain_hscale (GstVideoConverter * convert, GstLineCacheNeedLineFunc need_line)
{
  gint method;
  guint taps;

  convert->hscale_lines = gst_line_cache_new ();
  gst_line_cache_set_need_line_func (convert->hscale_lines,
      need_line, convert, NULL);

  if (!gst_structure_get_enum (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, &method))
    method = GST_VIDEO_RESAMPLER_METHOD_CUBIC;
  if (!gst_structure_get_uint (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_TAPS, &taps))
    taps = 0;

  convert->h_scaler =
      gst_video_scaler_new (method, GST_VIDEO_SCALER_FLAG_NONE, taps,
      convert->in_width, convert->out_width, convert->config);

  convert->current_width = convert->out_width;
  convert->h_scale_format = convert->current_format;

  return (GstLineCacheNeedLineFunc) do_hscale_lines;
}

static GstLineCacheNeedLineFunc
chain_vscale (GstVideoConverter * convert, GstLineCacheNeedLineFunc need_line)
{
  GstVideoScalerFlags flags;
  gint method;
  guint taps;

  flags = GST_VIDEO_INFO_IS_INTERLACED (&convert->in_info) ?
      GST_VIDEO_SCALER_FLAG_INTERLACED : 0;

  convert->vscale_lines = gst_line_cache_new ();
  gst_line_cache_set_need_line_func (convert->vscale_lines,
      need_line, convert, NULL);

  if (!gst_structure_get_enum (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, &method))
    method = GST_VIDEO_RESAMPLER_METHOD_CUBIC;
  if (!gst_structure_get_uint (convert->config,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_TAPS, &taps))
    taps = 0;

  convert->v_scaler =
      gst_video_scaler_new (method, flags, taps, convert->in_height,
      convert->out_height, convert->config);
  convert->v_scale_width = convert->current_width;
  convert->v_scale_format = convert->current_format;

  return (GstLineCacheNeedLineFunc) do_vscale_lines;
}

static GstLineCacheNeedLineFunc
chain_downsample (GstVideoConverter * convert,
    GstLineCacheNeedLineFunc need_line)
{
  if (convert->downsample) {
    convert->downsample_lines = gst_line_cache_new ();
    gst_line_cache_set_need_line_func (convert->downsample_lines,
        need_line, convert, NULL);
    need_line = (GstLineCacheNeedLineFunc) do_downsample_lines;
  }
  return need_line;
}

static GstLineCacheNeedLineFunc
chain_pack (GstVideoConverter * convert, GstLineCacheNeedLineFunc need_line)
{
  convert->pack_lines = gst_line_cache_new ();
  gst_line_cache_set_need_line_func (convert->pack_lines,
      need_line, convert, NULL);
  gst_line_cache_set_free_line_func (convert->pack_lines,
      (GstLineCacheFreeLineFunc) free_pack_line, convert, NULL);
  convert->pack_pstride = convert->current_pstride;
  return NULL;
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
  GstLineCacheNeedLineFunc need_line;
  gint s2, s3;
  const GstVideoFormatInfo *fin, *fout;

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
  convert->config = gst_structure_new ("GstVideoConverter",
      "dither", GST_TYPE_VIDEO_DITHER_METHOD, GST_VIDEO_DITHER_NONE, NULL);
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

  if (video_converter_lookup_fastpath (convert))
    goto done;

  if (in_info->finfo->unpack_func == NULL)
    goto no_unpack_func;

  if (out_info->finfo->pack_func == NULL)
    goto no_pack_func;

  convert->convert = video_converter_generic;

  s2 = convert->in_width * convert->out_height;
  s3 = convert->out_width * convert->in_height;

  video_converter_compute_matrix (convert);

  convert->current_format = GST_VIDEO_INFO_FORMAT (in_info);
  convert->current_width = convert->in_width;

  /* unpack */
  need_line = chain_unpack_line (convert);
  /* upsample chroma */
  need_line = chain_upsample (convert, need_line);

  if (s3 <= s2) {
    /* horizontal scaling first produces less pixels */
    if (convert->in_width <= convert->out_width) {
      /* upscaling, first convert then */
      need_line = chain_convert (convert, need_line);
    }
    if (convert->in_width != convert->out_width) {
      need_line = chain_hscale (convert, need_line);
    }
    if (convert->in_width > convert->out_width) {
      /* downscaling, convert after scaling then */
      need_line = chain_convert (convert, need_line);
    }
  }
  /* v-scale */
  if (convert->in_height != convert->out_height) {
    need_line = chain_vscale (convert, need_line);
  }
  if (s3 > s2) {
    /* vertical scaling first produced less pixels */
    if (convert->in_width <= convert->out_width) {
      /* upscaling, first convert then */
      need_line = chain_convert (convert, need_line);
    }
    if (convert->in_width != convert->out_width) {
      need_line = chain_hscale (convert, need_line);
    }
    if (convert->in_width > convert->out_width) {
      /* downscaling, convert after scaling then */
      need_line = chain_convert (convert, need_line);
    }
  }
  /* downsample chroma */
  need_line = chain_downsample (convert, need_line);
  /* pack into final format */
  need_line = chain_pack (convert, need_line);

  convert->lines = out_info->finfo->pack_lines;
  width = MAX (convert->in_maxwidth, convert->out_maxwidth);
  width += convert->out_x;
  convert->errline = g_malloc0 (sizeof (guint16) * width * 4);

  if (convert->fill_border && (convert->out_height < convert->out_maxheight ||
          convert->out_width < convert->out_maxwidth)) {
    gint i;
    guint8 *b;
    guint64 border_val, v;

    b = convert->borderline = g_malloc0 (sizeof (guint16) * width * 4);

    if (GST_VIDEO_INFO_IS_YUV (&convert->out_info)) {
      /* FIXME, convert to AYUV, just black for now */
      border_val = 0x00007f7f;
    } else {
      border_val = convert->border_argb;
    }

    if (convert->out_bits == 8)
      v = (border_val << 32) | border_val;
    else {
      guint64 c;

      c = (border_val >> 24) & 0xff;
      v = (c << 56) | (c << 48);
      c = (border_val >> 16) & 0xff;
      v |= (c << 40) | (c << 32);
      c = (border_val >> 8) & 0xff;
      v |= (c << 24) | (c << 16);
      c = (border_val) & 0xff;
      v |= (c << 8) | c;
    }
    v = GINT64_TO_BE (v);
    for (i = 0; i < width; i++) {
      memcpy (b, &v, 8);
      b += 8;
    }
  } else {
    convert->borderline = NULL;
  }

  /* FIXME */
  alloc_tmplines (convert, 64, width);

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

  if (convert->upsample)
    gst_video_chroma_resample_free (convert->upsample);
  if (convert->downsample)
    gst_video_chroma_resample_free (convert->downsample);
  if (convert->v_scaler)
    gst_video_scaler_free (convert->v_scaler);
  if (convert->h_scaler)
    gst_video_scaler_free (convert->h_scaler);

  if (convert->upsample_lines)
    gst_line_cache_free (convert->upsample_lines);
  if (convert->hscale_lines)
    gst_line_cache_free (convert->hscale_lines);
  if (convert->vscale_lines)
    gst_line_cache_free (convert->vscale_lines);
  if (convert->convert_lines)
    gst_line_cache_free (convert->convert_lines);
  if (convert->downsample_lines)
    gst_line_cache_free (convert->downsample_lines);
  if (convert->pack_lines)
    gst_line_cache_free (convert->pack_lines);

  for (i = 0; i < convert->n_tmplines; i++)
    g_free (convert->tmplines[i]);
  g_free (convert->tmplines);
  g_free (convert->errline);
  g_free (convert->borderline);

  if (convert->config)
    gst_structure_free (convert->config);

  g_slice_free (GstVideoConverter, convert);
}

static void
video_dither_verterr (GstVideoConverter * convert, guint16 * pixels, int j)
{
  int i;
  guint16 *errline = convert->errline;
  unsigned int mask = 0xff;

  for (i = 0; i < 4 * convert->in_width; i++) {
    int x = pixels[i] + errline[i];
    if (x > 65535)
      x = 65535;
    pixels[i] = x;
    errline[i] = x & mask;
  }
}

static void
video_dither_halftone (GstVideoConverter * convert, guint16 * pixels, int j)
{
  int i;
  static guint16 halftone[8][8] = {
    {0, 128, 32, 160, 8, 136, 40, 168},
    {192, 64, 224, 96, 200, 72, 232, 104},
    {48, 176, 16, 144, 56, 184, 24, 152},
    {240, 112, 208, 80, 248, 120, 216, 88},
    {12, 240, 44, 172, 4, 132, 36, 164},
    {204, 76, 236, 108, 196, 68, 228, 100},
    {60, 188, 28, 156, 52, 180, 20, 148},
    {252, 142, 220, 92, 244, 116, 212, 84}
  };

  for (i = 0; i < convert->in_width * 4; i++) {
    int x;
    x = pixels[i] + halftone[(i >> 2) & 7][j & 7];
    if (x > 65535)
      x = 65535;
    pixels[i] = x;
  }
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
 * The config is a GstStructure that can contain the the following fields:
 *
 *  "dither"    GST_TYPE_VIDEO_DITHER_METHOD   The dithering used when reducing
 *                                             colors
 *
 * Returns: %TRUE when @config could be set.
 *
 * Since: 1.6
 */
gboolean
gst_video_converter_set_config (GstVideoConverter * convert,
    GstStructure * config)
{
  gint dither;
  gboolean res = TRUE;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  if (gst_structure_get_enum (config, GST_VIDEO_CONVERTER_OPT_DITHER_METHOD,
          GST_TYPE_VIDEO_DITHER_METHOD, &dither)) {
    gboolean update = TRUE;

    switch (dither) {
      case GST_VIDEO_DITHER_NONE:
        convert->dither16 = NULL;
        break;
      case GST_VIDEO_DITHER_VERTERR:
        convert->dither16 = video_dither_verterr;
        break;
      case GST_VIDEO_DITHER_HALFTONE:
        convert->dither16 = video_dither_halftone;
        break;
      default:
        update = FALSE;
        break;
    }
    if (update)
      gst_structure_set (convert->config, GST_VIDEO_CONVERTER_OPT_DITHER_METHOD,
          GST_TYPE_VIDEO_DITHER_METHOD, dither, NULL);
    else
      res = FALSE;
  }
  if (res)
    gst_structure_foreach (config, copy_config, convert);

  gst_structure_free (config);

  return res;
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

#define SCALE    (8)
#define SCALE_F  ((float) (1 << SCALE))

static void
video_converter_matrix8 (GstVideoConverter * convert, gpointer pixels)
{
  gint width = MIN (convert->in_width, convert->out_width);
#if 1
  video_orc_matrix8 (pixels, pixels, convert->orc_p1, convert->orc_p2,
      convert->orc_p3, width);
#elif 0
  /* FIXME we would like to set this as a backup function, it's faster than the
   * orc generated one */
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *p = pixels;

  for (i = 0; i < convert->in_width; i++) {
    r = p[i * 4 + 1];
    g = p[i * 4 + 2];
    b = p[i * 4 + 3];

    y = (convert->cmatrix[0][0] * r + convert->cmatrix[0][1] * g +
        convert->cmatrix[0][2] * b + convert->cmatrix[0][3]) >> SCALE;
    u = (convert->cmatrix[1][0] * r + convert->cmatrix[1][1] * g +
        convert->cmatrix[1][2] * b + convert->cmatrix[1][3]) >> SCALE;
    v = (convert->cmatrix[2][0] * r + convert->cmatrix[2][1] * g +
        convert->cmatrix[2][2] * b + convert->cmatrix[2][3]) >> SCALE;

    p[i * 4 + 1] = CLAMP (y, 0, 255);
    p[i * 4 + 2] = CLAMP (u, 0, 255);
    p[i * 4 + 3] = CLAMP (v, 0, 255);
  }
#endif
}

static void
video_converter_matrix8_AYUV_ARGB (GstVideoConverter * convert, gpointer pixels)
{
  video_orc_convert_AYUV_ARGB (pixels, 0, pixels, 0,
      convert->cmatrix[0][0], convert->cmatrix[0][2],
      convert->cmatrix[2][1], convert->cmatrix[1][1], convert->cmatrix[1][2],
      convert->in_width, 1);
}

static void
video_converter_matrix16 (GstVideoConverter * convert, gpointer pixels)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *p = pixels;

  for (i = 0; i < convert->in_width; i++) {
    r = p[i * 4 + 1];
    g = p[i * 4 + 2];
    b = p[i * 4 + 3];

    y = (convert->cmatrix[0][0] * r + convert->cmatrix[0][1] * g +
        convert->cmatrix[0][2] * b + convert->cmatrix[0][3]) >> SCALE;
    u = (convert->cmatrix[1][0] * r + convert->cmatrix[1][1] * g +
        convert->cmatrix[1][2] * b + convert->cmatrix[1][3]) >> SCALE;
    v = (convert->cmatrix[2][0] * r + convert->cmatrix[2][1] * g +
        convert->cmatrix[2][2] * b + convert->cmatrix[2][3]) >> SCALE;

    p[i * 4 + 1] = CLAMP (y, 0, 65535);
    p[i * 4 + 2] = CLAMP (u, 0, 65535);
    p[i * 4 + 3] = CLAMP (v, 0, 65535);
  }
}

typedef struct
{
  double m[4][4];
} ColorMatrix;

static void
color_matrix_set_identity (ColorMatrix * m)
{
  int i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      m->m[i][j] = (i == j);
    }
  }
}

/* Perform 4x4 matrix multiplication:
 *  - @dst@ = @a@ * @b@
 *  - @dst@ may be a pointer to @a@ andor @b@
 */
static void
color_matrix_multiply (ColorMatrix * dst, ColorMatrix * a, ColorMatrix * b)
{
  ColorMatrix tmp;
  int i, j, k;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      double x = 0;
      for (k = 0; k < 4; k++) {
        x += a->m[i][k] * b->m[k][j];
      }
      tmp.m[i][j] = x;
    }
  }

  memcpy (dst, &tmp, sizeof (ColorMatrix));
}

static void
color_matrix_offset_components (ColorMatrix * m, double a1, double a2,
    double a3)
{
  ColorMatrix a;

  color_matrix_set_identity (&a);
  a.m[0][3] = a1;
  a.m[1][3] = a2;
  a.m[2][3] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_scale_components (ColorMatrix * m, double a1, double a2, double a3)
{
  ColorMatrix a;

  color_matrix_set_identity (&a);
  a.m[0][0] = a1;
  a.m[1][1] = a2;
  a.m[2][2] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_YCbCr_to_RGB (ColorMatrix * m, double Kr, double Kb)
{
  double Kg = 1.0 - Kr - Kb;
  ColorMatrix k = {
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
color_matrix_RGB_to_YCbCr (ColorMatrix * m, double Kr, double Kb)
{
  double Kg = 1.0 - Kr - Kb;
  ColorMatrix k;
  double x;

  k.m[0][0] = Kr;
  k.m[0][1] = Kg;
  k.m[0][2] = Kb;
  k.m[0][3] = 0;

  x = 1 / (2 * (1 - Kb));
  k.m[1][0] = -x * Kr;
  k.m[1][1] = -x * Kg;
  k.m[1][2] = x * (1 - Kb);
  k.m[1][3] = 0;

  x = 1 / (2 * (1 - Kr));
  k.m[2][0] = x * (1 - Kr);
  k.m[2][1] = -x * Kg;
  k.m[2][2] = -x * Kb;
  k.m[2][3] = 0;

  k.m[3][0] = 0;
  k.m[3][1] = 0;
  k.m[3][2] = 0;
  k.m[3][3] = 1;

  color_matrix_multiply (m, &k, m);
}

static void
video_converter_compute_matrix (GstVideoConverter * convert)
{
  GstVideoInfo *in_info, *out_info;
  ColorMatrix dst;
  gint i, j;
  const GstVideoFormatInfo *sfinfo, *dfinfo;
  const GstVideoFormatInfo *suinfo, *duinfo;
  gint offset[4], scale[4];
  gdouble Kr = 0, Kb = 0;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  sfinfo = in_info->finfo;
  dfinfo = out_info->finfo;

  suinfo = gst_video_format_get_info (sfinfo->unpack_format);
  duinfo = gst_video_format_get_info (dfinfo->unpack_format);

  convert->in_bits = GST_VIDEO_FORMAT_INFO_DEPTH (suinfo, 0);
  convert->out_bits = GST_VIDEO_FORMAT_INFO_DEPTH (duinfo, 0);

  GST_DEBUG ("in bits %d, out bits %d", convert->in_bits, convert->out_bits);

  if (in_info->colorimetry.range == out_info->colorimetry.range &&
      in_info->colorimetry.matrix == out_info->colorimetry.matrix) {
    GST_DEBUG ("using identity color transform");
    convert->matrix = NULL;
    return;
  }

  /* calculate intermediate format for the matrix. When unpacking, we expand
   * input to 16 when one of the inputs is 16 bits */
  if (convert->in_bits == 16 || convert->out_bits == 16) {
    convert->matrix = video_converter_matrix16;

    if (GST_VIDEO_FORMAT_INFO_IS_RGB (suinfo))
      suinfo = gst_video_format_get_info (GST_VIDEO_FORMAT_ARGB64);
    else
      suinfo = gst_video_format_get_info (GST_VIDEO_FORMAT_AYUV64);

    if (GST_VIDEO_FORMAT_INFO_IS_RGB (duinfo))
      duinfo = gst_video_format_get_info (GST_VIDEO_FORMAT_ARGB64);
    else
      duinfo = gst_video_format_get_info (GST_VIDEO_FORMAT_AYUV64);
  } else {
    if (GST_VIDEO_FORMAT_INFO_IS_YUV (suinfo)
        && GST_VIDEO_FORMAT_INFO_IS_RGB (duinfo))
      convert->matrix = video_converter_matrix8_AYUV_ARGB;
    else
      convert->matrix = video_converter_matrix8;
  }

  color_matrix_set_identity (&dst);

  /* 1, bring color components to [0..1.0] range */
  gst_video_color_range_offsets (in_info->colorimetry.range, suinfo, offset,
      scale);

  color_matrix_offset_components (&dst, -offset[0], -offset[1], -offset[2]);

  color_matrix_scale_components (&dst, 1 / ((float) scale[0]),
      1 / ((float) scale[1]), 1 / ((float) scale[2]));

  /* 2. bring components to R'G'B' space */
  if (gst_video_color_matrix_get_Kr_Kb (in_info->colorimetry.matrix, &Kr, &Kb))
    color_matrix_YCbCr_to_RGB (&dst, Kr, Kb);

  /* 3. inverse transfer function. R'G'B' to linear RGB */

  /* 4. from RGB to XYZ using the primaries */

  /* 5. from XYZ to RGB using the primaries */

  /* 6. transfer function. linear RGB to R'G'B' */

  /* 7. bring components to YCbCr space */
  if (gst_video_color_matrix_get_Kr_Kb (out_info->colorimetry.matrix, &Kr, &Kb))
    color_matrix_RGB_to_YCbCr (&dst, Kr, Kb);

  /* 8, bring color components to nominal range */
  gst_video_color_range_offsets (out_info->colorimetry.range, duinfo, offset,
      scale);

  color_matrix_scale_components (&dst, (float) scale[0], (float) scale[1],
      (float) scale[2]);

  color_matrix_offset_components (&dst, offset[0], offset[1], offset[2]);

  /* because we're doing fixed point matrix coefficients */
  color_matrix_scale_components (&dst, SCALE_F, SCALE_F, SCALE_F);

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      convert->cmatrix[i][j] = rint (dst.m[i][j]);

  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[0][0],
      convert->cmatrix[0][1], convert->cmatrix[0][2], convert->cmatrix[0][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[1][0],
      convert->cmatrix[1][1], convert->cmatrix[1][2], convert->cmatrix[1][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[2][0],
      convert->cmatrix[2][1], convert->cmatrix[2][2], convert->cmatrix[2][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[3][0],
      convert->cmatrix[3][1], convert->cmatrix[3][2], convert->cmatrix[3][3]);

  convert->orc_p1 = (((guint64) (guint16) convert->cmatrix[2][0]) << 48) |
      (((guint64) (guint16) convert->cmatrix[1][0]) << 32) |
      (((guint64) (guint16) convert->cmatrix[0][0]) << 16);
  convert->orc_p2 = (((guint64) (guint16) convert->cmatrix[2][1]) << 48) |
      (((guint64) (guint16) convert->cmatrix[1][1]) << 32) |
      (((guint64) (guint16) convert->cmatrix[0][1]) << 16);
  convert->orc_p3 = (((guint64) (guint16) convert->cmatrix[2][2]) << 48) |
      (((guint64) (guint16) convert->cmatrix[1][2]) << 32) |
      (((guint64) (guint16) convert->cmatrix[0][2]) << 16);
}

static void
video_converter_compute_resample (GstVideoConverter * convert)
{
  GstVideoInfo *in_info, *out_info;
  const GstVideoFormatInfo *sfinfo, *dfinfo;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  sfinfo = in_info->finfo;
  dfinfo = out_info->finfo;

  if (sfinfo->w_sub[2] != dfinfo->w_sub[2] ||
      sfinfo->h_sub[2] != dfinfo->h_sub[2] ||
      in_info->chroma_site != out_info->chroma_site ||
      in_info->width != out_info->width ||
      in_info->height != out_info->height) {
    GstVideoChromaFlags flags = (GST_VIDEO_INFO_IS_INTERLACED (in_info) ?
        GST_VIDEO_CHROMA_FLAG_INTERLACED : 0);

    convert->upsample = gst_video_chroma_resample_new (0,
        in_info->chroma_site, flags, sfinfo->unpack_format, sfinfo->w_sub[2],
        sfinfo->h_sub[2]);

    convert->downsample = gst_video_chroma_resample_new (0,
        out_info->chroma_site, flags, dfinfo->unpack_format, -dfinfo->w_sub[2],
        -dfinfo->h_sub[2]);

  } else {
    convert->upsample = NULL;
    convert->downsample = NULL;
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
  GST_DEBUG ("upsample: %p, site: %d, offset %d, n_lines %d", convert->upsample,
      in_info->chroma_site, convert->up_offset, convert->up_n_lines);
  GST_DEBUG ("downsample: %p, site: %d, offset %d, n_lines %d",
      convert->downsample, out_info->chroma_site, convert->down_offset,
      convert->down_n_lines);
}

#define TO_16(x) (((x)<<8) | (x))

static void
convert_to16 (gpointer line, gint width)
{
  guint8 *line8 = line;
  guint16 *line16 = line;
  gint i;

  for (i = (width - 1) * 4; i >= 0; i--)
    line16[i] = TO_16 (line8[i]);
}

static void
convert_to8 (gpointer line, gint width)
{
  guint8 *line8 = line;
  guint16 *line16 = line;
  gint i;

  for (i = 0; i < width * 4; i++)
    line8[i] = line16[i] >> 8;
}

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

static gboolean
do_unpack_lines (GstLineCache * cache, gint line, GstVideoConverter * convert)
{
  gpointer tmpline;
  guint cline;

  cline = CLAMP (line + convert->in_y, 0, convert->in_maxheight - 1);

  /* FIXME we should be able to use the input line without unpack if the
   * format is already suitable. When we do this, we should be careful not to
   * do in-place modifications. */
  GST_DEBUG ("unpack line %d (%u)", line, cline);
  tmpline = get_temp_line (convert, convert->out_x);

  UNPACK_FRAME (convert->src, tmpline, cline, convert->in_x, convert->in_width);

  gst_line_cache_add_line (cache, line, tmpline);

  return TRUE;
}

static gboolean
do_upsample_lines (GstLineCache * cache, gint line, GstVideoConverter * convert)
{
  gpointer *lines;
  gint i, start_line, n_lines;

  n_lines = convert->up_n_lines;
  start_line = line;
  if (start_line < n_lines + convert->up_offset)
    start_line += convert->up_offset;

  /* get the lines needed for chroma upsample */
  lines =
      gst_line_cache_get_lines (convert->upsample_lines, start_line, n_lines);

  GST_DEBUG ("doing upsample %d-%d", start_line, start_line + n_lines - 1);
  gst_video_chroma_resample (convert->upsample, lines, convert->in_width);

  for (i = 0; i < n_lines; i++)
    gst_line_cache_add_line (cache, start_line + i, lines[i]);

  return TRUE;
}

static gboolean
do_hscale_lines (GstLineCache * cache, gint line, GstVideoConverter * convert)
{
  gpointer *lines, destline;

  lines = gst_line_cache_get_lines (convert->hscale_lines, line, 1);

  destline = get_temp_line (convert, convert->out_x);

  GST_DEBUG ("hresample line %d", line);
  gst_video_scaler_horizontal (convert->h_scaler, convert->h_scale_format,
      lines[0], destline, 0, convert->out_width);

  gst_line_cache_add_line (cache, line, destline);

  return TRUE;
}

static gboolean
do_vscale_lines (GstLineCache * cache, gint line, GstVideoConverter * convert)
{
  gpointer *lines, destline;
  guint sline, n_lines;
  guint cline;

  cline = CLAMP (line, 0, convert->out_height - 1);

  gst_video_scaler_get_coeff (convert->v_scaler, cline, &sline, &n_lines);
  lines = gst_line_cache_get_lines (convert->vscale_lines, sline, n_lines);

  destline = get_temp_line (convert, convert->out_x);

  /* FIXME with 1-tap (nearest), we can simply copy the input line. We need
   * to be careful to not do in-place modifications later */
  GST_DEBUG ("vresample line %d %d-%d", line, sline, sline + n_lines - 1);
  gst_video_scaler_vertical (convert->v_scaler, convert->v_scale_format,
      lines, destline, cline, convert->v_scale_width);

  gst_line_cache_add_line (cache, line, destline);

  return TRUE;
}

static gboolean
do_convert_lines (GstLineCache * cache, gint line, GstVideoConverter * convert)
{
  gpointer *lines;
  guint in_bits, out_bits;
  gint width;

  lines = gst_line_cache_get_lines (convert->convert_lines, line, 1);

  in_bits = convert->in_bits;
  out_bits = convert->out_bits;

  width = MIN (convert->in_width, convert->out_width);

  GST_DEBUG ("convert line %d", line);
  if (out_bits == 16 || in_bits == 16) {
    /* FIXME, we can scale in the conversion matrix */
    if (in_bits == 8)
      convert_to16 (lines[0], width);

    if (convert->matrix)
      convert->matrix (convert, lines[0]);
    if (convert->dither16)
      convert->dither16 (convert, lines[0], line);

    if (out_bits == 8)
      convert_to8 (lines[0], width);
  } else {
    if (convert->matrix)
      convert->matrix (convert, lines[0]);
  }
  gst_line_cache_add_line (cache, line, lines[0]);

  return TRUE;
}

static gboolean
do_downsample_lines (GstLineCache * cache, gint line,
    GstVideoConverter * convert)
{
  gpointer *lines;
  gint i, start_line, n_lines;

  n_lines = convert->down_n_lines;
  start_line = line;
  if (start_line < n_lines + convert->down_offset)
    start_line += convert->down_offset;

  /* get the lines needed for chroma downsample */
  lines =
      gst_line_cache_get_lines (convert->downsample_lines, start_line, n_lines);

  GST_DEBUG ("downsample line %d %d-%d", line, start_line,
      start_line + n_lines - 1);
  gst_video_chroma_resample (convert->downsample, lines, convert->out_width);

  for (i = 0; i < n_lines; i++)
    gst_line_cache_add_line (cache, start_line + i, lines[i]);

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
  gint r_border, out_width, lb_width, rb_width;

  out_width = convert->out_width;
  out_height = convert->out_height;
  out_maxwidth = convert->out_maxwidth;
  out_maxheight = convert->out_maxheight;

  out_x = convert->out_x;
  out_y = convert->out_y;

  convert->src = src;
  convert->dest = dest;

  pack_lines = convert->lines;  /* only 1 for now */
  pstride = convert->pack_pstride;

  r_border = (out_x + out_width) * pstride;
  rb_width = out_maxwidth * pstride - r_border;
  lb_width = out_x * pstride;

  if (convert->borderline) {
    /* FIXME we should try to avoid PACK_FRAME */
    for (i = 0; i < out_y; i++)
      PACK_FRAME (dest, convert->borderline, i, out_maxwidth);
  }

  for (i = 0; i < out_height; i += pack_lines) {
    gpointer *lines;
    guint8 *l;

    /* load the lines needed to pack */
    lines = gst_line_cache_get_lines (convert->pack_lines, i, pack_lines);

    /* take away the border */
    l = ((guint8 *) lines[0]) - lb_width;

    if (convert->borderline) {
      /* FIXME this can be optimized if we make separate temp lines with
       * border for the output lines */
      memcpy (l, convert->borderline, lb_width);
      memcpy (l + r_border, convert->borderline, rb_width);
    }
    /* and pack into destination */
    /* FIXME, we should be able to convert directly into the destination line
     * when the output format is in the right format */
    GST_DEBUG ("pack line %d", i + out_y);
    PACK_FRAME (dest, l, i + out_y, out_maxwidth);
  }

  if (convert->borderline) {
    for (i = out_y + out_height; i < out_maxheight; i++)
      PACK_FRAME (dest, convert->borderline, i, out_maxwidth);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
    UNPACK_FRAME (src, convert->tmplines[0], height - 1, convert->in_x, width);
    PACK_FRAME (dest, convert->tmplines[0], height - 1, width);
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
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_ARGB (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), convert->cmatrix[0][0], convert->cmatrix[0][2],
      convert->cmatrix[2][1], convert->cmatrix[1][1], convert->cmatrix[1][2],
      width, height);
}

static void
convert_AYUV_BGRA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_BGRA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), convert->cmatrix[0][0], convert->cmatrix[0][2],
      convert->cmatrix[2][1], convert->cmatrix[1][1], convert->cmatrix[1][2],
      width, height);
}

static void
convert_AYUV_ABGR (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_ABGR (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), convert->cmatrix[0][0], convert->cmatrix[0][2],
      convert->cmatrix[2][1], convert->cmatrix[1][1], convert->cmatrix[1][2],
      width, height);
}

static void
convert_AYUV_RGBA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  gint width = convert->in_width;
  gint height = convert->in_height;

  video_orc_convert_AYUV_RGBA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), convert->cmatrix[0][0], convert->cmatrix[0][2],
      convert->cmatrix[2][1], convert->cmatrix[1][1], convert->cmatrix[1][2],
      width, height);
}

static void
convert_I420_BGRA (GstVideoConverter * convert, const GstVideoFrame * src,
    GstVideoFrame * dest)
{
  int i;
  gint width = convert->in_width;
  gint height = convert->in_height;

  for (i = 0; i < height; i++) {
    video_orc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1),
        convert->cmatrix[0][0], convert->cmatrix[0][2],
        convert->cmatrix[2][1], convert->cmatrix[1][1], convert->cmatrix[1][2],
        width);
  }
}
#endif



/* Fast paths */

typedef struct
{
  GstVideoFormat in_format;
  GstVideoColorMatrix in_matrix;
  GstVideoFormat out_format;
  GstVideoColorMatrix out_matrix;
  gboolean keeps_color_matrix;
  gboolean keeps_interlaced;
  gboolean needs_color_matrix;
  gint width_align, height_align;
  void (*convert) (GstVideoConverter * convert, const GstVideoFrame * src,
      GstVideoFrame * dest);
} VideoTransform;

static const VideoTransform transforms[] = {
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_I420_YUY2},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_I420_UYVY},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_I420_AYUV},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 0, 0,
      convert_I420_Y42B},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 0, 0,
      convert_I420_Y444},

  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_I420_YUY2},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_I420_UYVY},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_I420_AYUV},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 0, 0,
      convert_I420_Y42B},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 0, 0,
      convert_I420_Y444},

  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YV12,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0, convert_UYVY_YUY2},      /* alias */
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_YUY2_AYUV},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_YUY2_Y42B},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_YUY2_Y444},

  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YV12,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_UYVY_YUY2},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_UYVY_AYUV},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_UYVY_Y42B},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_UYVY_Y444},

  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 1, 1,
      convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YV12,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 1, 1,
      convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_AYUV_YUY2},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_AYUV_UYVY},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_AYUV_Y42B},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_AYUV_Y444},

  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 0, 0,
      convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YV12,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 0, 0,
      convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_Y42B_YUY2},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_Y42B_UYVY},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_Y42B_AYUV},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_Y42B_Y444},

  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 1, 0,
      convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YV12,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, FALSE, 1, 0,
      convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_Y444_YUY2},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_Y444_UYVY},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 0, 0,
      convert_Y444_AYUV},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, FALSE, 1, 0,
      convert_Y444_Y42B},

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_ARGB,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_ARGB},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_BGRA,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_BGRA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_xRGB,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0, convert_AYUV_ARGB},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_BGRx,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0, convert_AYUV_BGRA},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_ABGR,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_ABGR},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_RGBA,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0,
      convert_AYUV_RGBA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_xBGR,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0, convert_AYUV_ABGR},       /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_RGBx,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, TRUE, TRUE, 0, 0, convert_AYUV_RGBA},       /* alias */

  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_BGRA,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_BGRx,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_BGRA,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
  {GST_VIDEO_FORMAT_YV12, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_BGRx,
        GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, FALSE, TRUE, 0, 0,
      convert_I420_BGRA},
#endif
};

static gboolean
video_converter_lookup_fastpath (GstVideoConverter * convert)
{
  int i;
  GstVideoFormat in_format, out_format;
  GstVideoColorMatrix in_matrix, out_matrix;
  gboolean interlaced;
  gint width, height;

  width = GST_VIDEO_INFO_WIDTH (&convert->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&convert->in_info);

  if (width != convert->out_width || height != convert->out_height)
    return FALSE;

  in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);

  in_matrix = convert->in_info.colorimetry.matrix;
  out_matrix = convert->out_info.colorimetry.matrix;

  interlaced = GST_VIDEO_INFO_IS_INTERLACED (&convert->in_info);
  interlaced |= GST_VIDEO_INFO_IS_INTERLACED (&convert->out_info);

  for (i = 0; i < sizeof (transforms) / sizeof (transforms[0]); i++) {
    if (transforms[i].in_format == in_format &&
        transforms[i].out_format == out_format &&
        (transforms[i].keeps_color_matrix ||
            (transforms[i].in_matrix == in_matrix &&
                transforms[i].out_matrix == out_matrix)) &&
        (transforms[i].keeps_interlaced || !interlaced) &&
        (transforms[i].width_align & width) == 0 &&
        (transforms[i].height_align & height) == 0) {
      GST_DEBUG ("using fastpath");
      if (transforms[i].needs_color_matrix)
        video_converter_compute_matrix (convert);
      convert->convert = transforms[i].convert;
      alloc_tmplines (convert, 1, GST_VIDEO_INFO_WIDTH (&convert->in_info));
      return TRUE;
    }
  }
  GST_DEBUG ("no fastpath found");
  return FALSE;
}
