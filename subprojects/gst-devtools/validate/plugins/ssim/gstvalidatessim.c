/* GStreamer
 *
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gstvalidatessim.c: GstValidateActionTypes to use with ssim applications
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
#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cairo.h>

#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "../../gst-libs/gst/video/gstvalidatessim.h"
#include "../../gst/validate/gst-validate-report.h"
#include "../../gst/validate/gst-validate-pad-monitor.h"
#include "../../gst/validate/gst-validate-reporter.h"
#include "../../gst/validate/validate.h"
#include "../../gst/validate/gst-validate-scenario.h"
#include "../../gst/validate/gst-validate-utils.h"

#define SSIM_WRONG_FORMAT g_quark_from_static_string ("validatessim::wrong-format")
#define SSIM_CONVERSION_ERROR g_quark_from_static_string ("validatessim::conversion-error")
#define SSIM_SAVING_ERROR g_quark_from_static_string ("validatessim::saving-error")
#define MONITOR_DATA g_quark_from_static_string ("validate-ssim-monitor-data")
#define NOT_ATTACHED g_quark_from_static_string ("validatessim::not-attached")

typedef struct _ValidateSsimOverridePrivate ValidateSsimOverridePrivate;

typedef struct
{
  GstValidateOverride parent;

  ValidateSsimOverridePrivate *priv;

} ValidateSsimOverride;

typedef struct
{
  GstValidateOverrideClass parent;

} ValidateSsimOverrideClass;

typedef struct
{
  gchar *path;
  GstClockTime position;
  guint width, height;
} Frame;

static void
free_frame (Frame * frame)
{
  g_free (frame->path);
}

struct _ValidateSsimOverridePrivate
{
  gchar *outdir;
  gchar *result_outdir;
  GstStructure *config;

  gboolean is_attached;

  GstVideoConverter *converter;
  GstCaps *last_caps;
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  GArray *frames;
  GstClockTime recurrence;
  GstClockTime last_dump_position;

  /* Always used in the streaming thread */
  gboolean needs_reconfigure;
  GstVideoFormat save_format;
  const gchar *ext;
  GstVideoFormat ref_format;
  const gchar *ref_ext;
};



static GType validate_ssim_override_get_type (void);

#define VALIDATE_SSIM_OVERRIDE_TYPE (validate_ssim_override_get_type ())
#define VALIDATE_SSIM_OVERRIDE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), VALIDATE_SSIM_OVERRIDE_TYPE, ValidateSsimOverride))
#define VALIDATE_SSIM_OVERRIDE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), VALIDATE_SSIM_OVERRIDE_TYPE, ValidateSsimOverrideClass))
#define IS_VALIDATE_SSIM_OVERRIDE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), VALIDATE_SSIM_OVERRIDE_TYPE))
#define IS_VALIDATE_SSIM_OVERRIDE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), VALIDATE_SSIM_OVERRIDE_TYPE))
#define VALIDATE_SSIM_OVERRIDE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), VALIDATE_SSIM_OVERRIDE_TYPE, ValidateSsimOverrideClass))

/*  *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (ValidateSsimOverride, validate_ssim_override,
    GST_TYPE_VALIDATE_OVERRIDE)
/*  *INDENT-ON* */

static void
runner_stopping (GstValidateRunner * runner, ValidateSsimOverride * self)
{
  GstValidateSsim *ssim;

  guint i, nfiles;
  gfloat mssim = 0, lowest = 1, highest = -1, total_avg = 0;
  gint npassed = 0, nfailures = 0;
  gdouble min_avg_similarity = 0.95, min_lowest_similarity = -1.0,
      min_avg = 1.0, min_min = 1.0;
  const gchar *compared_files_dir =
      gst_structure_get_string (self->priv->config,
      "reference-images-dir");
  gint fps_n = 0, fps_d = 1;

  if (!self->priv->is_attached) {
    gchar *config_str = gst_structure_to_string (self->priv->config);
    GST_VALIDATE_REPORT (self, NOT_ATTACHED,
        "The test ended without SSIM being attached for config %s", config_str);
    g_free (config_str);
    return;
  }

  if (!compared_files_dir) {
    return;
  }

  gst_validate_printf (self,
      "Running frame comparison between images from '%s' and '%s' %s%s.\n",
      compared_files_dir, self->priv->outdir,
      self->priv->result_outdir ? ". Issues can be visialized in " :
      " (set 'result-output-dir' in the config file to visualize the result)",
      self->priv->result_outdir ? self->priv->result_outdir : "");

  gst_structure_get_double (self->priv->config, "min-avg-priority",
      &min_avg_similarity);
  gst_structure_get_double (self->priv->config, "min-lowest-priority",
      &min_lowest_similarity);

  gst_structure_get_fraction (self->priv->config, "framerate", &fps_n, &fps_d);
  ssim =
      gst_validate_ssim_new (runner, min_avg_similarity, min_lowest_similarity,
      fps_n, fps_d);

  nfiles = self->priv->frames->len;
  for (i = 0; i < nfiles; i++) {
    Frame *frame = &g_array_index (self->priv->frames, Frame, i);
    gchar *refname, *ref_path, *bname = g_path_get_basename (frame->path);

    if (self->priv->ref_format == GST_VIDEO_FORMAT_ENCODED)
      refname = g_strdup_printf ("*.%s", self->priv->ref_ext);
    else
      refname = g_strdup_printf ("*.%dx%d.%s", frame->width, frame->height,
          self->priv->ref_ext);

    ref_path = g_build_path (G_DIR_SEPARATOR_S, compared_files_dir,
        refname, NULL);

    if (!gst_validate_ssim_compare_image_files (ssim, ref_path, frame->path,
            &mssim, &lowest, &highest, self->priv->result_outdir))
      nfailures++;
    else
      npassed++;

    g_free (ref_path);

    min_avg = MIN (min_avg, mssim);
    min_min = MIN (lowest, min_min);
    total_avg += mssim;
    gst_validate_print_position (frame->position, GST_CLOCK_TIME_NONE, 1.0,
        g_strdup_printf (" %d / %d avg: %f min: %f (Passed: %d failed: %d)",
            i + 1, nfiles, mssim, lowest, npassed, nfailures));
    g_free (bname);
  }

  gst_validate_printf (NULL,
      "\nAverage similarity: %f, min_avg: %f, min_min: %f\n",
      total_avg / nfiles, min_avg, min_min);
}

static void
_runner_set (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  ValidateSsimOverride *self = VALIDATE_SSIM_OVERRIDE (object);
  GstValidateRunner *runner =
      gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (self));

  g_signal_connect (runner, "stopping", G_CALLBACK (runner_stopping), self);
  gst_object_unref (runner);
}

static ValidateSsimOverride *
validate_ssim_override_new (GstStructure * config)
{
  const gchar *format;
  ValidateSsimOverride *self = g_object_new (VALIDATE_SSIM_OVERRIDE_TYPE, NULL);

  self->priv->outdir =
      g_strdup (gst_structure_get_string (config, "output-dir"));

  if (self->priv->outdir == NULL) {
    gchar *template = g_build_filename (g_get_tmp_dir (),
        "validatessim-XXXXXX", NULL);
    self->priv->outdir = g_mkdtemp (template);
  }

  if (!g_file_test (self->priv->outdir, G_FILE_TEST_IS_DIR)) {
    if (g_mkdir_with_parents (self->priv->outdir, 0755) != 0) {

      GST_ERROR ("Could not create directory %s", self->priv->outdir);

      g_object_unref (self);

      return NULL;
    }
  }

  self->priv->config = gst_structure_copy (config);
  self->priv->result_outdir =
      g_strdup (gst_structure_get_string (config, "result-output-dir"));

  format = gst_structure_get_string (config, "output-video-format");
  if (!format) {
    self->priv->save_format = GST_VIDEO_FORMAT_ENCODED;
    self->priv->ext = "png";
  } else {
    self->priv->save_format = gst_video_format_from_string (format);
    self->priv->ext = format;
  }

  if (self->priv->save_format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR ("Uknown video format: %s", format);

    gst_object_unref (self);

    return NULL;
  }

  format = gst_structure_get_string (config, "reference-video-format");
  if (!format) {
    self->priv->ref_ext = "png";
    self->priv->ref_format = GST_VIDEO_FORMAT_ENCODED;
  } else {
    self->priv->ref_format = gst_video_format_from_string (format);
    if (self->priv->ref_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR ("Uknown video format: %s", format);

      gst_object_unref (self);

      return NULL;
    }

    self->priv->ref_ext = format;
  }

  gst_validate_utils_get_clocktime (config, "check-recurrence",
      &self->priv->recurrence);

  g_signal_connect (self, "notify::validate-runner", G_CALLBACK (_runner_set),
      NULL);

  return self;
}


static gboolean
_can_attach (GstValidateOverride * override, GstValidateMonitor * monitor)
{
  guint i;
  GstPad *pad = NULL;
  GstCaps *template_caps;
  GstElement *element = NULL;
  GstStructure *structure;
  gboolean res = FALSE;
  ValidateSsimOverride *self = VALIDATE_SSIM_OVERRIDE (override);

  if (self->priv->is_attached) {
    GST_ERROR_OBJECT (override, "Already attached");

    goto fail;
  }

  if (!GST_IS_VALIDATE_PAD_MONITOR (monitor)) {
    goto fail;
  }

  pad = GST_PAD (gst_validate_monitor_get_target (monitor));
  element = gst_validate_monitor_get_element (monitor);

  if (!element) {
    GST_INFO_OBJECT (monitor, "Not in an element yet, can't attach");
    goto fail;
  }

  if ((gst_validate_element_has_klass (element, "Converter") ||
          gst_validate_element_has_klass (element, "Filter") ||
          gst_validate_element_has_klass (element, "Decoder")) &&
      GST_PAD_IS_SINK (pad)) {
    GST_INFO_OBJECT (override, "Not attaching on filter or decoder sinkpads");

    goto fail;
  }

  if (!GST_PAD_PAD_TEMPLATE (pad)) {
    GST_INFO_OBJECT (pad,
        "Doesn't have template, can't use it %" GST_PTR_FORMAT,
        gst_pad_query_caps (pad, NULL));
    return FALSE;
  }

  template_caps = GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad));
  if (gst_caps_is_any (template_caps)) {
    res = TRUE;
    goto done;
  }

  for (i = 0; i < gst_caps_get_size (template_caps); i++) {
    structure = gst_caps_get_structure (template_caps, i);
    if (gst_structure_has_name (structure, "video/x-raw")) {
      res = TRUE;
      break;
    }
  }

done:
  if (res) {
    gchar *path = gst_object_get_path_string (GST_OBJECT (pad));
    GST_INFO_OBJECT (override, "Wrapping %" GST_PTR_FORMAT, pad);

    gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (override),
        g_strdup_printf ("ssim-override-%s", path));
    gst_validate_printf (self, "Using %s as output directory\n",
        self->priv->outdir);
    g_free (path);
  }

  if (pad)
    gst_object_unref (pad);
  if (element)
    gst_object_unref (element);
  return res;

fail:
  res = FALSE;
  goto done;
}

static void
validate_ssim_override_attached (GstValidateOverride * override)
{
  ValidateSsimOverride *self = VALIDATE_SSIM_OVERRIDE (override);

  self->priv->is_attached = TRUE;
}

static void
_finalize (GObject * object)
{
  ValidateSsimOverridePrivate *priv = VALIDATE_SSIM_OVERRIDE (object)->priv;
  GDir *outdir_handle = NULL;
  const gchar *filename = NULL;
  GError *error = NULL;

  if (priv->converter)
    gst_video_converter_free (priv->converter);

  if (priv->last_caps)
    gst_caps_unref (priv->last_caps);

  if (priv->config && !gst_structure_has_field (priv->config, "output-dir")) {
    /* Remove temporary directory contents (expected to be files, no sub-directories). */
    outdir_handle = g_dir_open (priv->outdir, 0, &error);
    if (outdir_handle != NULL) {
      while ((filename = g_dir_read_name (outdir_handle))) {
        gchar *path =
            g_build_path (G_DIR_SEPARATOR_S, priv->outdir, filename, NULL);
        g_remove (path);
        g_free (path);
      }
      g_dir_close (outdir_handle);
    } else {
      GST_ERROR ("Unable to cleanup temporary directory %s: %s", priv->outdir,
          error->message);
      g_error_free (error);
    }
    g_rmdir (priv->outdir);
  }

  g_free (priv->outdir);
  g_free (priv->result_outdir);
  g_array_unref (priv->frames);

  if (priv->config)
    gst_structure_free (priv->config);
}

static void
validate_ssim_override_class_init (ValidateSsimOverrideClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstValidateOverrideClass *override_class =
      GST_VALIDATE_OVERRIDE_CLASS (klass);

  gobject_class->finalize = _finalize;
  override_class->attached = validate_ssim_override_attached;

  if (!gst_validate_is_initialized ())
    return;

  GST_VALIDATE_OVERRIDE_CLASS (klass)->can_attach = _can_attach;

  gst_validate_issue_register (gst_validate_issue_new (SSIM_WRONG_FORMAT,
          "The ValidateSSim plugin can not work with a video format",
          "The GstValidate ssim plugin was not able to work"
          " with a video format that flowed in the pipeline."
          " Make sure you properly configured the plugin",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  gst_validate_issue_register (gst_validate_issue_new (SSIM_CONVERSION_ERROR,
          "The ValidateSSim plugin could not convert a frame in the needed format",
          "The GstValidate ssim plugin needs to convert the frame in a colorspace"
          " it can handle, but it was not possible.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  gst_validate_issue_register (gst_validate_issue_new (SSIM_SAVING_ERROR,
          "The ValidateSSim plugin could not save PNG file",
          "The ValidateSSim plugin could not save PNG file",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  gst_validate_issue_register (gst_validate_issue_new
      (NOT_ATTACHED,
          "The ssim override was never attached.",
          "The ssim override was never attached.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));
}

static void
validate_ssim_override_init (ValidateSsimOverride * self)
{
  self->priv = validate_ssim_override_get_instance_private (self);

  self->priv->needs_reconfigure = TRUE;
  self->priv->frames = g_array_new (TRUE, TRUE, sizeof (Frame));
  g_array_set_clear_func (self->priv->frames, (GDestroyNotify) free_frame);
}

static gboolean
_set_videoconvert (ValidateSsimOverride * o,
    GstValidatePadMonitor * pad_monitor)
{
  GstCaps *caps;
  GstVideoFormat format;
  ValidateSsimOverridePrivate *priv = o->priv;
  GstPad *pad =
      GST_PAD (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR
          (pad_monitor)));

  caps = gst_pad_get_current_caps (pad);
  gst_object_unref (pad);
  gst_caps_replace (&priv->last_caps, caps);

  gst_video_info_init (&priv->in_info);
  gst_video_info_init (&priv->out_info);
  if (priv->converter) {
    gst_video_converter_free (priv->converter);
    priv->converter = NULL;
  }

  if (!gst_video_info_from_caps (&priv->in_info, priv->last_caps)) {
    GST_VALIDATE_REPORT (o, SSIM_WRONG_FORMAT,
        "The format %" GST_PTR_FORMAT " is not supported"
        " by the plugin", pad_monitor->last_caps);

    return FALSE;
  }

  if (GST_VIDEO_INFO_HAS_ALPHA (&priv->in_info))
    format = GST_VIDEO_FORMAT_BGRA;
  else
    format = GST_VIDEO_FORMAT_BGRx;

  if (priv->in_info.finfo->format == format) {
    GST_INFO_OBJECT (o, "No conversion needed");

    return TRUE;
  }

  if (priv->save_format != GST_VIDEO_FORMAT_ENCODED)
    format = priv->save_format;

  gst_video_info_set_format (&priv->out_info, format,
      priv->in_info.width, priv->in_info.height);
  priv->out_info.fps_d = priv->in_info.fps_d;
  priv->out_info.fps_n = priv->in_info.fps_n;

  priv->converter = gst_video_converter_new (&priv->in_info,
      &priv->out_info, NULL);

  return TRUE;

}

static gboolean
has_frame (ValidateSsimOverride * self, gchar * name)
{
  guint i;
  GArray *frames = self->priv->frames;

  for (i = 0; i < frames->len; ++i) {
    if (g_strcmp0 (g_array_index (frames, Frame, i).path, name) == 0)
      return TRUE;
  }

  return FALSE;
}


static gchar *
_get_filename (ValidateSsimOverride * self, GstValidatePadMonitor * monitor,
    GstClockTime position)
{
  gint i = 0;
  gchar *outname = NULL, *s;

  if (self->priv->save_format == GST_VIDEO_FORMAT_ENCODED)
    s = g_strdup_printf ("%" GST_VALIDATE_SSIM_TIME_FORMAT ".%s",
        GST_TIME_ARGS (position), self->priv->ext);
  else
    s = g_strdup_printf ("%" GST_VALIDATE_SSIM_TIME_FORMAT ".%dx%d.%s",
        GST_TIME_ARGS (position),
        self->priv->out_info.width,
        self->priv->out_info.height, self->priv->ext);

  outname = g_build_path (G_DIR_SEPARATOR_S, self->priv->outdir, s, NULL);

  g_free (s);
  while (has_frame (self, outname)) {
    g_free (outname);
    if (self->priv->save_format == GST_VIDEO_FORMAT_ENCODED)
      s = g_strdup_printf ("%" GST_VALIDATE_SSIM_TIME_FORMAT "-%d.%s",
          GST_TIME_ARGS (position), i++, self->priv->ext);
    else
      s = g_strdup_printf ("%" GST_VALIDATE_SSIM_TIME_FORMAT "-%d.%dx%d.%s",
          GST_TIME_ARGS (position), i++, self->priv->out_info.width,
          self->priv->out_info.height, self->priv->ext);

    outname = g_build_path (G_DIR_SEPARATOR_S, self->priv->outdir, s, NULL);
    g_free (s);
  }

  return outname;
}

static gboolean
_should_dump_buffer (ValidateSsimOverride * self,
    GstValidatePadMonitor * pad_monitor, GstClockTime position)
{
  ValidateSsimOverridePrivate *priv = self->priv;

  if (!GST_CLOCK_TIME_IS_VALID (priv->recurrence))
    return TRUE;

  if (priv->needs_reconfigure)
    return TRUE;

  /* recurence 0 means, dump exclusively on reconfiguration */
  if (priv->recurrence == 0)
    return FALSE;

  if (GST_CLOCK_DIFF (priv->last_dump_position, position) >= priv->recurrence)
    return TRUE;

  return FALSE;
}

static gboolean
_save_frame (ValidateSsimOverride * self, GstVideoFrame * frame,
    const gchar * outname)
{
  gboolean res = TRUE;
  cairo_status_t status;
  cairo_surface_t *surface;
  GError *error = NULL;

  if (self->priv->save_format == GST_VIDEO_FORMAT_ENCODED) {
    surface =
        cairo_image_surface_create_for_data (GST_VIDEO_FRAME_PLANE_DATA (frame,
            0), CAIRO_FORMAT_RGB24, GST_VIDEO_FRAME_WIDTH (frame),
        GST_VIDEO_FRAME_HEIGHT (frame), GST_VIDEO_FRAME_PLANE_STRIDE (frame,
            0));

    if ((status = cairo_surface_write_to_png (surface, outname)) !=
        CAIRO_STATUS_SUCCESS) {
      GST_VALIDATE_REPORT (self, SSIM_SAVING_ERROR,
          "Could not save '%s', cairo status is '%s'", outname,
          cairo_status_to_string (status));

      res = FALSE;
    }

    cairo_surface_destroy (surface);

    return res;
  }

  if (!g_file_set_contents (outname,
          GST_VIDEO_FRAME_PLANE_DATA (frame, 0),
          GST_VIDEO_FRAME_SIZE (frame), &error)) {
    GST_VALIDATE_REPORT (self, SSIM_SAVING_ERROR,
        "Could not save %s error: %s", outname, error->message);
    res = FALSE;
  }

  return res;
}

static void
_handle_buffer (GstValidateOverride * override,
    GstValidatePadMonitor * pad_monitor, GstBuffer * buffer)
{
  gchar *outname = NULL;
  GstVideoFrame frame;
  Frame iframe;

  ValidateSsimOverride *o = VALIDATE_SSIM_OVERRIDE (override);
  ValidateSsimOverridePrivate *priv = o->priv;

  GstClockTime position;

  position = gst_segment_to_stream_time (&pad_monitor->segment,
      GST_FORMAT_TIME, GST_BUFFER_PTS (buffer));

  if (!_should_dump_buffer (o, pad_monitor, position)) {
    GST_LOG_OBJECT (override, "Not dumping buffer: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));

    return;
  }

  if (priv->needs_reconfigure) {
    priv->needs_reconfigure = !_set_videoconvert (o, pad_monitor);
  }

  if (priv->converter) {
    GstVideoFrame inframe;
    GstBuffer *outbuf;

    if (!gst_video_frame_map (&inframe, &priv->in_info, buffer, GST_MAP_READ)) {
      GST_VALIDATE_REPORT (o, SSIM_CONVERSION_ERROR,
          "Could not map the videoframe %p", buffer);

      return;
    }

    outbuf = gst_buffer_new_allocate (NULL, priv->out_info.size, NULL);
    if (!gst_video_frame_map (&frame, &priv->out_info, outbuf, GST_MAP_WRITE)) {
      GST_VALIDATE_REPORT (o, SSIM_CONVERSION_ERROR,
          "Could not map the outbuffer %p", outbuf);

      gst_buffer_unref (outbuf);
      return;
    }
    gst_buffer_unref (outbuf);
    gst_video_converter_frame (priv->converter, &inframe, &frame);
    gst_video_frame_unmap (&inframe);
  } else {
    if (!gst_video_frame_map (&frame, &priv->in_info, buffer, GST_MAP_WRITE)) {
      GST_VALIDATE_REPORT (o, SSIM_CONVERSION_ERROR,
          "Could not map the buffer %p", buffer);
      return;
    }
  }

  outname = _get_filename (o, pad_monitor, position);
  if (_save_frame (o, &frame, outname)) {
    priv->last_dump_position = position;

    iframe.position = position;
    iframe.path = outname;
    iframe.width = priv->in_info.width;
    iframe.height = priv->in_info.height;
    g_array_append_val (priv->frames, iframe);
  }

  gst_video_frame_unmap (&frame);
}

static void
_handle_event (GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstEvent * event)
{
  ValidateSsimOverride *self = VALIDATE_SSIM_OVERRIDE (override);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      self->priv->needs_reconfigure = TRUE;
      break;
    case GST_EVENT_CAPS:
      self->priv->needs_reconfigure = TRUE;
      break;
    default:
      break;
  }
}

static gboolean
_map_confg (GQuark field_id, GValue * value, GstStructure * structure)
{
  if (!gst_structure_id_has_field (structure, field_id))
    gst_structure_id_set_value (structure, field_id, value);

  return TRUE;
}

static gboolean
gst_validate_ssim_init (GstPlugin * plugin)
{
  GList *tmp, *config;
  GstStructure *config_structure = NULL;

  if (!gst_validate_is_initialized ())
    return FALSE;

  config = gst_validate_plugin_get_config (plugin);
  for (tmp = config; tmp; tmp = tmp->next) {
    gboolean is_config;

    if (gst_structure_get_boolean (tmp->data, "is-config", &is_config)) {
      if (is_config) {
        config_structure = tmp->data;
        break;
      }
    }
  }

  for (tmp = config; tmp; tmp = tmp->next) {
    const gchar *name = gst_structure_get_string (tmp->data, "element-name");
    const gchar *target_element_classification =
        gst_structure_get_string (tmp->data, "element-classification");

    if (tmp->data == config_structure)
      continue;

    if (config_structure) {
      gst_structure_map_in_place (config_structure,
          (GstStructureMapFunc) _map_confg, tmp->data);
    }
    if ((name || target_element_classification)) {
      GstValidateOverride *override =
          GST_VALIDATE_OVERRIDE (validate_ssim_override_new (tmp->data));

      if (override == NULL) {
        GST_ERROR ("Could not create override with config %"
            GST_PTR_FORMAT, tmp->data);

        continue;
      }

      override->buffer_probe_handler =
          (GstValidateOverrideBufferHandler) _handle_buffer;
      override->buffer_handler =
          (GstValidateOverrideBufferHandler) _handle_buffer;
      override->event_handler = (GstValidateOverrideEventHandler) _handle_event;

      if (target_element_classification)
        gst_validate_override_register_by_klass (target_element_classification,
            override);
      else if (name)
        gst_validate_override_register_by_name (name, override);
      else
        g_assert_not_reached ();

      g_object_unref (override);
    } else {
      GST_ERROR ("Wrong configuration '%" GST_PTR_FORMAT
          "'element-classification' and output-dir are mandatory fields",
          tmp->data);
      g_assert_not_reached ();
    }
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    validatessim,
    "GstValidate plugin to run the ssim algorithm on raw"
    " video buffers. It allows you to generate png files"
    "\n                          "
    " and then check them against pre generated, reference images."
    "\n                          "
    " The following parameters can be passed in the configuration file:"
    "\n                          "
    "   'element-classification': The target element classification as define in gst_element_class_set_metadata"
    "\n                          "
    "   'output-dir': The directory in which the image files will be saved'"
    "\n",
    gst_validate_ssim_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
