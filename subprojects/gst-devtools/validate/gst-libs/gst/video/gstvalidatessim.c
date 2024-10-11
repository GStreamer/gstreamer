/* GStreamer
 *
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <glib-object.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include "gstvalidatessim.h"
#include "gssim.h"

#include <cairo.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#define GST_CAT_DEFAULT gstvalidatessim_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define SIMILARITY_ISSUE_WITH_PREVIOUS g_quark_from_static_string ("ssim::image-not-similar-enough-with-theoretical-reference")
#define SIMILARITY_ISSUE g_quark_from_static_string ("ssim::image-not-similar-enough")
#define GENERAL_INPUT_ERROR g_quark_from_static_string ("ssim::general-file-error")
#define WRONG_FORMAT g_quark_from_static_string ("ssim::wrong-format")

enum
{
  PROP_FIRST_PROP = 1,
  PROP_RUNNER,
  PROP_LAST
};

typedef struct
{
  GstVideoConverter *converter;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
} SSimConverterInfo;

struct _GstValidateSsimPrivate
{
  gint width;
  gint height;

  Gssim *ssim;

  GList *converters;
  GstVideoInfo out_info;

  SSimConverterInfo outconverter_info;

  gfloat min_avg_similarity;
  gfloat min_lowest_similarity;

  GHashTable *ref_frames_cache;
  gint fps_n, fps_d;
};

G_DEFINE_TYPE_WITH_CODE (GstValidateSsim, gst_validate_ssim,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstValidateSsim)
    G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, NULL));

static void
ssim_convert_info_free (SSimConverterInfo * info)
{
  if (info->converter)
    gst_video_converter_free (info->converter);

  g_free (info);
}

static gboolean
gst_validate_ssim_convert (GstValidateSsim * self, SSimConverterInfo * info,
    GstVideoFrame * frame, GstVideoFrame * converted_frame)
{
  gboolean res = TRUE;
  GstBuffer *outbuf = NULL;

  g_return_val_if_fail (info != NULL, FALSE);

  outbuf = gst_buffer_new_allocate (NULL, info->out_info.size, NULL);
  if (!gst_video_frame_map (converted_frame, &info->out_info, outbuf,
          GST_MAP_WRITE)) {
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not map output converted_frame");
    goto fail;
  }

  gst_video_converter_frame (info->converter, frame, converted_frame);

done:
  if (outbuf)
    gst_buffer_unref (outbuf);

  return res;

fail:
  res = FALSE;
  goto done;
}

static gchar *
gst_validate_ssim_save_out (GstValidateSsim * self, GstBuffer * buffer,
    const gchar * ref_file, const gchar * file, const gchar * outfolder)
{
  GstVideoFrame frame, converted;

  if (!g_file_test (outfolder, G_FILE_TEST_IS_DIR)) {
    if (g_mkdir_with_parents (outfolder, 0755) != 0) {

      GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
          "Could not create output directory %s", outfolder);
      return NULL;
    }
  }

  if (self->priv->outconverter_info.converter == NULL ||
      self->priv->width != self->priv->outconverter_info.out_info.width ||
      self->priv->height != self->priv->outconverter_info.out_info.height) {

    if (self->priv->outconverter_info.converter)
      gst_video_converter_free (self->priv->outconverter_info.converter);

    gst_video_info_init (&self->priv->outconverter_info.in_info);
    gst_video_info_set_format (&self->priv->outconverter_info.in_info,
        GST_VIDEO_FORMAT_GRAY8, self->priv->width, self->priv->height);

    gst_video_info_init (&self->priv->outconverter_info.out_info);
    gst_video_info_set_format (&self->priv->outconverter_info.out_info,
        GST_VIDEO_FORMAT_RGBx, self->priv->width, self->priv->height);

    self->priv->outconverter_info.converter =
        gst_video_converter_new (&self->priv->outconverter_info.in_info,
        &self->priv->outconverter_info.out_info, NULL);
  }

  if (!gst_video_frame_map (&frame, &self->priv->outconverter_info.in_info,
          buffer, GST_MAP_READ)) {
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not map output frame");

    return NULL;
  }

  if (gst_validate_ssim_convert (self, &self->priv->outconverter_info,
          &frame, &converted)) {
    cairo_status_t status;
    gchar *bn1 = g_path_get_basename (ref_file);
    gchar *bn2 = g_path_get_basename (file);
    gchar *fname =
        g_strdup_printf ("original_%s.VS.nok_%s.result.png", bn1, bn2);
    gchar *outfile = g_build_path (G_DIR_SEPARATOR_S, outfolder, fname, NULL);
    cairo_surface_t *out_surface = NULL;

    if (g_str_has_suffix (file, ".png")) {
      cairo_t *context;
      cairo_surface_t *ref_surface =
          cairo_image_surface_create_from_png (ref_file);
      cairo_surface_t *nok_surface = cairo_image_surface_create_from_png (file);
      cairo_surface_t *diff_surface =
          cairo_image_surface_create_for_data (GST_VIDEO_FRAME_PLANE_DATA
          (&converted, 0), CAIRO_FORMAT_RGB24,
          GST_VIDEO_FRAME_WIDTH (&converted),
          GST_VIDEO_FRAME_HEIGHT (&converted),
          GST_VIDEO_FRAME_PLANE_STRIDE (&converted, 0));

      out_surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
          GST_VIDEO_FRAME_WIDTH (&frame) * 2,
          GST_VIDEO_FRAME_HEIGHT (&frame) * 2);

      context = cairo_create (out_surface);
      cairo_set_operator (context, CAIRO_OPERATOR_OVER);
      cairo_set_source_surface (context, ref_surface, 0, 0);
      cairo_paint (context);

      cairo_translate (context, GST_VIDEO_FRAME_WIDTH (&converted), 0);
      cairo_set_source_surface (context, nok_surface, 0, 0);
      cairo_paint (context);

      cairo_translate (context, -GST_VIDEO_FRAME_WIDTH (&converted) / 2,
          GST_VIDEO_FRAME_HEIGHT (&converted));
      cairo_set_source_surface (context, diff_surface, 0, 0);
      cairo_paint (context);

      cairo_surface_destroy (ref_surface);
      cairo_surface_destroy (nok_surface);
      cairo_surface_destroy (diff_surface);
    } else {
      out_surface =
          cairo_image_surface_create_for_data (GST_VIDEO_FRAME_PLANE_DATA
          (&converted, 0), CAIRO_FORMAT_RGB24,
          GST_VIDEO_FRAME_WIDTH (&converted),
          GST_VIDEO_FRAME_HEIGHT (&converted),
          GST_VIDEO_FRAME_PLANE_STRIDE (&converted, 0));
    }

    if ((status = cairo_surface_write_to_png (out_surface, outfile)) !=
        CAIRO_STATUS_SUCCESS) {
      GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
          "Could not save '%s', cairo status is '%s'", outfile,
          cairo_status_to_string (status));
    }

    cairo_surface_destroy (out_surface);
    gst_video_frame_unmap (&frame);
    gst_video_frame_unmap (&converted);
    g_free (bn1);
    g_free (bn2);
    g_free (fname);
    return outfile;
  }

  return NULL;
}

static gboolean
gst_validate_ssim_configure (GstValidateSsim * self, gint width, gint height)
{
  if (width == self->priv->width && height == self->priv->height)
    return FALSE;

  gssim_configure (self->priv->ssim, width, height);

  self->priv->width = width;
  self->priv->height = height;

  gst_video_info_init (&self->priv->out_info);
  gst_video_info_set_format (&self->priv->out_info, GST_VIDEO_FORMAT_I420,
      self->priv->width, self->priv->height);

  return TRUE;
}

static void
gst_validate_ssim_configure_converter (GstValidateSsim * self, gint index,
    gboolean force, GstVideoFormat in_format, gint width, gint height)
{
  SSimConverterInfo *info = g_list_nth_data (self->priv->converters, index);

  if (!info) {
    info = g_new0 (SSimConverterInfo, 1);

    self->priv->converters =
        g_list_insert (self->priv->converters, info, index);
  }

  if (force || info->in_info.height != height || info->in_info.width != width ||
      info->in_info.finfo->format != in_format) {
    gst_video_info_init (&info->in_info);
    gst_video_info_set_format (&info->in_info, in_format, width, height);

    if (info->converter)
      gst_video_converter_free (info->converter);

    info->out_info = self->priv->out_info;

    if (gst_video_info_is_equal (&info->in_info, &info->out_info))
      info->converter = NULL;
    else
      info->converter =
          gst_video_converter_new (&info->in_info, &info->out_info, NULL);
  }
}

static GstVideoFormat
_get_format_from_surface (cairo_surface_t * surface)
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
  if (cairo_surface_get_content (surface) == CAIRO_CONTENT_COLOR_ALPHA)
    return GST_VIDEO_FORMAT_BGRA;
  else
    return GST_VIDEO_FORMAT_BGRx;
#else
  if (cairo_surface_get_content (surface) == CAIRO_CONTENT_COLOR_ALPHA)
    return GST_VIDEO_FORMAT_ARGB;
  else
    return GST_VIDEO_FORMAT_RGBx;
#endif
}

void
gst_validate_ssim_compare_frames (GstValidateSsim * self,
    GstVideoFrame * ref_frame, GstVideoFrame * frame, GstBuffer ** outbuf,
    gfloat * mean, gfloat * lowest, gfloat * highest)
{
  gboolean reconf;
  guint8 *outdata = NULL;
  GstMapInfo map1, map2, outmap;

  GstVideoFrame converted_frame1, converted_frame2;
  SSimConverterInfo *convinfo1, *convinfo2;

  reconf =
      gst_validate_ssim_configure (self, ref_frame->info.width,
      ref_frame->info.height);

  gst_validate_ssim_configure_converter (self, 0, reconf,
      ref_frame->info.finfo->format, ref_frame->info.width,
      ref_frame->info.height);

  gst_validate_ssim_configure_converter (self, 1, reconf,
      frame->info.finfo->format, frame->info.width, frame->info.height);

  convinfo1 = (SSimConverterInfo *) g_list_nth_data (self->priv->converters, 0);
  if (convinfo1->converter)
    gst_validate_ssim_convert (self, convinfo1, ref_frame, &converted_frame1);
  else
    converted_frame1 = *ref_frame;

  convinfo2 = (SSimConverterInfo *) g_list_nth_data (self->priv->converters, 1);
  if (convinfo2->converter)
    gst_validate_ssim_convert (self, convinfo2, frame, &converted_frame2);
  else
    converted_frame2 = *frame;

  if (!gst_buffer_map (converted_frame1.buffer, &map1, GST_MAP_READ)) {
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not map reference frame");

    return;
  }

  if (!gst_buffer_map (converted_frame2.buffer, &map2, GST_MAP_READ)) {
    gst_buffer_unmap (converted_frame1.buffer, &map1);
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not map compared frame");

    return;
  }

  if (outbuf) {
    *outbuf = gst_buffer_new_and_alloc (GST_ROUND_UP_4 (self->priv->width) *
        self->priv->height);
    if (!gst_buffer_map (*outbuf, &outmap, GST_MAP_WRITE)) {
      GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
          "Could not map output frame");

      gst_buffer_unref (*outbuf);
      gst_buffer_unmap (converted_frame1.buffer, &map1);
      gst_buffer_unmap (converted_frame2.buffer, &map2);
      *outbuf = NULL;

      return;
    }

    outdata = outmap.data;
  }

  gssim_compare (self->priv->ssim, map1.data, map2.data, outdata, mean,
      lowest, highest);

  gst_buffer_unmap (ref_frame->buffer, &map1);
  gst_buffer_unmap (frame->buffer, &map2);

  if (convinfo1->converter)
    gst_video_frame_unmap (&converted_frame1);
  if (convinfo2->converter)
    gst_video_frame_unmap (&converted_frame2);

  if (outbuf)
    gst_buffer_unmap (*outbuf, &outmap);
}

static gboolean
gst_validate_ssim_get_frame_from_png (GstValidateSsim * self, const char *file,
    GstVideoFrame * frame)
{
  guint8 *data;
  GstBuffer *buf;
  GstVideoInfo info;
  cairo_surface_t *surface = NULL;

  surface = cairo_image_surface_create_from_png (file);
  if (surface == NULL
      || (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)) {
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR, "Could not open %s: %s",
        file, cairo_status_to_string (cairo_surface_status (surface)));

    return FALSE;
  }

  gst_video_info_init (&info);
  gst_video_info_set_format (&info,
      _get_format_from_surface (surface),
      cairo_image_surface_get_width (surface),
      cairo_image_surface_get_height (surface));

  cairo_surface_flush (surface);
  data = cairo_image_surface_get_data (surface);
  buf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      data, info.size, 0, info.size, surface,
      (GDestroyNotify) cairo_surface_destroy);
  if (!gst_video_frame_map (frame, &info, buf, GST_MAP_READ)) {
    gst_buffer_unref (buf);
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not map input frame");

    return FALSE;
  }

  gst_buffer_unref (buf);

  return TRUE;
}

static gboolean
gst_validate_ssim_get_frame_from_file (GstValidateSsim * self, const char *file,
    GstVideoFrame * frame)
{
  gchar *data;
  gsize length;
  GstBuffer *buf;
  GstVideoInfo info;
  GstVideoFormat format;
  gint strv_length, width, height;

  gboolean res = TRUE;
  gchar **splited_name = NULL, **splited_size = NULL, *strformat;

  GError *error = NULL;

  if (g_str_has_suffix (file, ".png")) {
    return gst_validate_ssim_get_frame_from_png (self, file, frame);
  }

  splited_name = g_strsplit (file, ".", -1);
  strv_length = g_strv_length (splited_name);

  strformat = splited_name[strv_length - 1];
  format = gst_video_format_from_string (strformat);
  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_VALIDATE_REPORT (self, WRONG_FORMAT, "Unknown format: %s", strformat);

    goto fail;
  }

  splited_size = g_strsplit (splited_name[strv_length - 2], "x", -1);
  if (g_strv_length (splited_size) != 2) {
    GST_VALIDATE_REPORT (self, WRONG_FORMAT,
        "Can not determine video size from filename: %s ", file);

    goto fail;
  }

  errno = 0;
  width = g_ascii_strtoull (splited_size[0], NULL, 10);
  if (errno) {
    GST_VALIDATE_REPORT (self, WRONG_FORMAT,
        "Can not determine video size from filename: %s ", file);

    goto fail;
  }

  errno = 0;
  height = g_ascii_strtoull (splited_size[1], NULL, 10);
  if (errno) {
    GST_VALIDATE_REPORT (self, WRONG_FORMAT,
        "Can not determine video size from filename: %s ", file);

    goto fail;
  }

  gst_video_info_init (&info);
  gst_video_info_set_format (&info, format, width, height);

  if (!g_file_get_contents (file, &data, &length, &error)) {
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR, "Could not open %s: %s",
        file, error->message);
    g_error_free (error);

    goto fail;
  }

  buf = gst_buffer_new_wrapped (data, length);
  if (!gst_video_frame_map (frame, &info, buf, GST_MAP_READ)) {
    gst_buffer_unref (buf);
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not map input frame");

    goto fail;
  }
  gst_buffer_unref (buf);

done:
  g_strfreev (splited_name);
  g_strfreev (splited_size);

  return res;

fail:
  res = FALSE;

  goto done;
}

static gboolean
_filename_get_timestamp (GstValidateSsim * self, const gchar * filename,
    GstClockTime * ts)
{
  guint h, m, s, ns;
  gchar *bname = g_path_get_basename (filename);
  gchar *other = g_strdup (bname);
  gboolean res = TRUE;

  if (sscanf (bname, "%" GST_VALIDATE_SSIM_TIME_FORMAT "%s", &h, &m, &s, &ns,
          other) < 4) {
    GST_INFO_OBJECT (self, "Can not sscanf %s", bname);

    goto fail;
  }

  *ts = (h * 3600 + m * 60 + s) * GST_SECOND + ns;

done:
  g_free (other);
  g_free (bname);
  return res;

fail:
  res = FALSE;
  goto done;
}

typedef struct
{
  gchar *path;
  GstClockTime ts;
} Frame;

static void
_free_frame (Frame * frame)
{
  g_free (frame->path);
}

static gint
_sort_frames (Frame * a, Frame * b)
{
  if (a->ts < b->ts)
    return -1;

  if (a->ts == b->ts)
    return 0;

  return 1;
}

static Frame *
_find_frame (GstValidateSsim * self, GArray * frames, GstClockTime ts,
    gboolean get_next)
{
  guint i;
  Frame *lframe = &g_array_index (frames, Frame, 0);

  if (self->priv->fps_n) {
    gint64 frame_number = gst_util_uint64_scale (ts, self->priv->fps_n,
        self->priv->fps_d * GST_SECOND);

    if (frames->len < frame_number)
      return NULL;

    return &g_array_index (frames, Frame, frame_number);
  }

  if (frames->len == 1) {
    Frame *iframe = &g_array_index (frames, Frame, 0);

    if (iframe->ts == ts)
      return iframe;

    return NULL;
  }

  for (i = 1; i < frames->len; i++) {
    Frame *iframe = &g_array_index (frames, Frame, i);

    if (ts >= lframe->ts && iframe->ts > ts) {
      if (get_next)
        return iframe;

      return lframe;
    } else if (i + 1 == frames->len) {
      return iframe;
    }

    lframe = iframe;
  }

  return NULL;
}

static GArray *
_get_ref_frame_cache (GstValidateSsim * self, const gchar * ref_file)
{
  GFile *ref_dir_file = NULL;
  GFileInfo *info;
  GFileEnumerator *fenum;
  GArray *frames = NULL;
  gchar *ref_dir = NULL;

  ref_dir = g_path_get_dirname (ref_file);

  frames = g_hash_table_lookup (self->priv->ref_frames_cache, ref_file);
  if (frames)
    goto done;

  ref_dir_file = g_file_new_for_path (ref_dir);
  if (!(fenum = g_file_enumerate_children (ref_dir_file,
              "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL))) {
    GST_INFO ("%s is not a folder", ref_dir);

    goto done;
  }

  for (info = g_file_enumerator_next_file (fenum, NULL, NULL);
      info; info = g_file_enumerator_next_file (fenum, NULL, NULL)) {
    Frame iframe;
    const gchar *display_name = g_file_info_get_display_name (info);

    if (!_filename_get_timestamp (self, display_name, &iframe.ts)) {
      g_object_unref (info);
      continue;
    }

    iframe.path = g_build_path (G_DIR_SEPARATOR_S,
        ref_dir, g_file_info_get_name (info), NULL);

    g_object_unref (info);

    if (!frames) {
      frames = g_array_new (TRUE, TRUE, sizeof (Frame));

      g_array_set_clear_func (frames, (GDestroyNotify) _free_frame);
    }
    g_array_append_val (frames, iframe);
  }
  g_object_unref (fenum);

  if (frames) {
    g_array_sort (frames, (GCompareFunc) _sort_frames);

    g_hash_table_insert (self->priv->ref_frames_cache, g_strdup (ref_dir),
        frames);
  }

done:
  g_clear_object (&ref_dir_file);
  g_free (ref_dir);

  return frames;
}

static gchar *
_get_ref_file_path (GstValidateSsim * self, const gchar * ref_file,
    const gchar * file, gboolean get_next)
{
  Frame *frame;
  GArray *frames;
  gchar *real_ref_file = NULL;
  GstClockTime file_ts;

  if (!g_strrstr (ref_file, "*"))
    return g_strdup (ref_file);

  if (!_filename_get_timestamp (self, file, &file_ts)) {
    goto done;
  }

  frames = _get_ref_frame_cache (self, ref_file);
  if (frames) {
    frame = _find_frame (self, frames, file_ts, get_next);

    if (frame)
      real_ref_file = g_strdup (frame->path);
  }

done:

  return real_ref_file;
}

static gboolean
gst_validate_ssim_compare_image_file (GstValidateSsim * self,
    const gchar * ref_file, const gchar * file, gfloat * mean, gfloat * lowest,
    gfloat * highest, const gchar * outfolder)
{
  GstBuffer *outbuf = NULL, **poutbuf = NULL;
  gboolean res = TRUE;
  GstVideoFrame ref_frame, frame;
  gchar *real_ref_file = NULL;
  gchar *output_failure_image = NULL, *failure_info = NULL;

  real_ref_file = _get_ref_file_path (self, ref_file, file, FALSE);

  if (!real_ref_file) {
    GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
        "Could not find ref file: %s for file: %s", ref_file, file);
    goto fail;
  }

  if (!gst_validate_ssim_get_frame_from_file (self, real_ref_file, &ref_frame))
    goto fail;


  if (!gst_validate_ssim_get_frame_from_file (self, file, &frame)) {
    gst_video_frame_unmap (&ref_frame);

    goto fail;
  }

  if (outfolder) {
    poutbuf = &outbuf;
  }

  gst_validate_ssim_compare_frames (self, &ref_frame, &frame,
      poutbuf, mean, lowest, highest);

  if (*mean < self->priv->min_avg_similarity) {
    GstClockTime ref_ts, f_ts;

    gst_video_frame_unmap (&ref_frame);
    gst_video_frame_unmap (&frame);

    _filename_get_timestamp (self, real_ref_file, &ref_ts);
    _filename_get_timestamp (self, file, &f_ts);

    if (g_strcmp0 (ref_file, real_ref_file) && ref_ts != f_ts) {
      gchar *tmpref = real_ref_file;

      real_ref_file = _get_ref_file_path (self, ref_file, file, TRUE);

      GST_VALIDATE_REPORT (self, SIMILARITY_ISSUE_WITH_PREVIOUS,
          "\nComparing %s with %s failed, (mean %f "
          " min %f), checking next %s\n",
          tmpref, file, *mean, *lowest, real_ref_file);

      g_free (tmpref);

      res = gst_validate_ssim_compare_image_file (self,
          real_ref_file, file, mean, lowest, highest, outfolder);
      goto done;
    }

    if (outbuf)
      output_failure_image =
          gst_validate_ssim_save_out (self, outbuf, real_ref_file, file,
          outfolder);

    if (output_failure_image)
      failure_info =
          g_strdup_printf (" (See %s to check differences in images)",
          output_failure_image);

    GST_VALIDATE_REPORT (self, SIMILARITY_ISSUE,
        "Average similarity '%f' between %s and %s inferior"
        " than the minimum average: %f%s", *mean,
        real_ref_file, file, self->priv->min_avg_similarity, failure_info);

    goto fail;
  }

  if (*lowest < self->priv->min_lowest_similarity) {
    if (outbuf)
      output_failure_image =
          gst_validate_ssim_save_out (self, outbuf, real_ref_file, file,
          outfolder);

    if (output_failure_image)
      failure_info =
          g_strdup_printf (" (See %s to check differences in images)",
          output_failure_image);

    GST_VALIDATE_REPORT (self, SIMILARITY_ISSUE,
        "Lowest similarity '%f' between %s and %s inferior"
        " than the minimum lowest similarity: %f%s", *lowest,
        real_ref_file, file, self->priv->min_lowest_similarity, failure_info);

    gst_video_frame_unmap (&ref_frame);
    gst_video_frame_unmap (&frame);

    goto fail;
  }

  gst_video_frame_unmap (&ref_frame);
  gst_video_frame_unmap (&frame);

done:

  g_free (failure_info);
  g_free (output_failure_image);
  g_free (real_ref_file);
  if (outbuf)
    gst_buffer_unref (outbuf);

  return res;

fail:
  res = FALSE;

  goto done;
}

static gboolean
_check_directory (GstValidateSsim * self, const gchar * ref_dir,
    const gchar * compared_dir, gfloat * mean, gfloat * lowest,
    gfloat * highest, const gchar * outfolder)
{
  gint nfiles = 0, nnotfound = 0, nfailures = 0;
  gboolean res = TRUE;
  GFileInfo *info;
  GFileEnumerator *fenum;
  gfloat min_avg = 1.0, min_min = 1.0, total_avg = 0;
  GFile *file = g_file_new_for_path (ref_dir);

  if (!(fenum = g_file_enumerate_children (file,
              "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL))) {
    GST_INFO ("%s is not a folder", ref_dir);
    res = FALSE;

    goto done;
  }

  for (info = g_file_enumerator_next_file (fenum, NULL, NULL);
      info; info = g_file_enumerator_next_file (fenum, NULL, NULL)) {

    if (g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR ||
        g_file_info_get_file_type (info) == G_FILE_TYPE_SYMBOLIC_LINK) {
      gchar *compared_file = g_build_path (G_DIR_SEPARATOR_S,
          compared_dir, g_file_info_get_name (info), NULL);
      gchar *ref_file = NULL;

      if (!g_file_test (compared_file, G_FILE_TEST_IS_REGULAR)) {
        GST_ERROR_OBJECT (self, "Could not find file %s", compared_file);
        nnotfound++;
        res = FALSE;
      } else {

        ref_file =
            g_build_path (G_DIR_SEPARATOR_S, ref_dir,
            g_file_info_get_name (info), NULL);
        if (!gst_validate_ssim_compare_image_files (self, ref_file,
                compared_file, mean, lowest, highest, outfolder)) {
          nfailures++;
          res = FALSE;
        } else {
          nfiles++;
        }
      }

      min_avg = MIN (min_avg, *mean);
      min_min = MIN (min_min, *lowest);
      total_avg += *mean;
      gst_validate_printf (NULL,
          "<position: %s duration: %" GST_TIME_FORMAT
          " avg: %f min: %f (Passed: %d failed: %d, %d not found)/>\r",
          g_file_info_get_display_name (info),
          GST_TIME_ARGS (GST_CLOCK_TIME_NONE),
          *mean, *lowest, nfiles, nfailures, nnotfound);

      g_free (compared_file);
      g_free (ref_file);
    }

    g_object_unref (info);
  }

  if (nfiles == 0) {
    gst_validate_printf (NULL, "\nNo files to verify.\n");
  } else {
    gst_validate_printf (NULL,
        "\nAverage similarity: %f, min_avg: %f, min_min: %f\n",
        total_avg / nfiles, min_avg, min_min);
  }

done:
  gst_object_unref (file);
  if (fenum)
    gst_object_unref (fenum);

  return res;
}

gboolean
gst_validate_ssim_compare_image_files (GstValidateSsim * self,
    const gchar * ref_file, const gchar * file, gfloat * mean, gfloat * lowest,
    gfloat * highest, const gchar * outfolder)
{
  if (g_file_test (ref_file, G_FILE_TEST_IS_DIR)) {
    if (!g_file_test (file, G_FILE_TEST_IS_DIR)) {
      GST_VALIDATE_REPORT (self, GENERAL_INPUT_ERROR,
          "%s is a directory but %s is not", ref_file, file);

      return FALSE;
    }

    return _check_directory (self, ref_file, file, mean, lowest, highest,
        outfolder);
  } else {
    return gst_validate_ssim_compare_image_file (self, ref_file, file, mean,
        lowest, highest, outfolder);
  }
}

static void
gst_validate_ssim_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      g_value_set_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object)));
      break;
    default:
      break;
  }
}

static void
gst_validate_ssim_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (object),
          g_value_get_object (value));
      break;
    default:
      break;
  }
}

static void
gst_validate_ssim_dispose (GObject * object)
{
  GstValidateSsim *self = GST_VALIDATE_SSIM (object);
  void (*chain_up) (GObject *) =
      ((GObjectClass *) gst_validate_ssim_parent_class)->dispose;

  gst_object_unref (self->priv->ssim);

  chain_up (object);
}

static void
gst_validate_ssim_finalize (GObject * object)
{
  GstValidateSsim *self = GST_VALIDATE_SSIM (object);
  void (*chain_up) (GObject *) =
      ((GObjectClass *) gst_validate_ssim_parent_class)->finalize;

  g_list_free_full (self->priv->converters,
      (GDestroyNotify) ssim_convert_info_free);

  if (self->priv->outconverter_info.converter)
    gst_video_converter_free (self->priv->outconverter_info.converter);
  g_hash_table_unref (self->priv->ref_frames_cache);

  chain_up (object);
}

static gpointer
_register_issues (gpointer data)
{
  gst_validate_issue_register (gst_validate_issue_new_full (SIMILARITY_ISSUE,
          "Compared images were not similar enough",
          "The images checker detected that the images"
          " it is comparing do not have the similarity"
          " level defined with min-avg-similarity or"
          " min-lowest-similarity", GST_VALIDATE_REPORT_LEVEL_CRITICAL,
          GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS |
          GST_VALIDATE_ISSUE_FLAGS_NO_BACKTRACE));

  gst_validate_issue_register (gst_validate_issue_new
      (SIMILARITY_ISSUE_WITH_PREVIOUS,
          "Comparison with theoretical reference image failed",
          " In a case where we have reference frames with the following"
          " timestamps: [0.00, 0.10, 0.20, 0.30], comparing a frame with"
          " 0.05 as a timestamp will be done with the first frame."
          " If this fails, a ssim::image-not-similar-enough-with-theoretical-reference"
          " warning is issued and the system then tries with the second reference frame.",
          GST_VALIDATE_REPORT_LEVEL_WARNING));

  gst_validate_issue_register (gst_validate_issue_new (GENERAL_INPUT_ERROR,
          "Something went wrong handling image files for ssim comparison",
          "An error occurred when working with input files",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  gst_validate_issue_register (gst_validate_issue_new (WRONG_FORMAT,
          "The format or dimensions of the compared images do not match",
          "The format or dimensions of the compared images do not match",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  return NULL;
}

static void
gst_validate_ssim_class_init (GstValidateSsimClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  static GOnce _once = G_ONCE_INIT;

  GST_DEBUG_CATEGORY_INIT (gstvalidatessim_debug, "validatessim", 0,
      "Validate ssim plugin");

  oclass->get_property = gst_validate_ssim_get_property;
  oclass->set_property = gst_validate_ssim_set_property;
  oclass->dispose = gst_validate_ssim_dispose;
  oclass->finalize = gst_validate_ssim_finalize;

  g_once (&_once, _register_issues, NULL);

  g_object_class_install_property (oclass, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
gst_validate_ssim_init (GstValidateSsim * self)
{
  self->priv = gst_validate_ssim_get_instance_private (self);

  self->priv->ssim = gssim_new ();
  self->priv->ref_frames_cache = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, (GDestroyNotify) g_array_unref);
}

GstValidateSsim *
gst_validate_ssim_new (GstValidateRunner * runner,
    gfloat min_avg_similarity, gfloat min_lowest_similarity,
    gint fps_n, gint fps_d)
{
  GstValidateSsim *self =
      g_object_new (GST_VALIDATE_SSIM_TYPE, "validate-runner", runner, NULL);

  self->priv->min_avg_similarity = min_avg_similarity;
  self->priv->min_lowest_similarity = min_lowest_similarity;
  self->priv->fps_n = fps_n;
  self->priv->fps_d = fps_d;

  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (self),
      g_strdup ("gst-validate-images-checker"));

  return self;
}
