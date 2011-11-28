/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstfrei0r.h"
#include "gstfrei0rsrc.h"

#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_EXTERN (frei0r_debug);
#define GST_CAT_DEFAULT frei0r_debug

typedef struct
{
  f0r_plugin_info_t info;
  GstFrei0rFuncTable ftable;
} GstFrei0rSrcClassData;

static gboolean
gst_frei0r_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (src);

  if (!gst_video_format_parse_caps (caps, &self->fmt, &self->width,
          &self->height)
      || !gst_video_parse_caps_framerate (caps, &self->fps_n, &self->fps_d))
    return FALSE;

  return TRUE;
}

static GstCaps *
gst_frei0r_src_get_caps (GstBaseSrc * src)
{
  if (GST_PAD_CAPS (GST_BASE_SRC_PAD (src)))
    return gst_caps_ref (GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));
  else
    return
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src)));
}

static GstFlowReturn
gst_frei0r_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (src);
  GstFrei0rSrcClass *klass = GST_FREI0R_SRC_GET_CLASS (src);
  guint size, newsize;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = NULL;
  GstClockTime timestamp;
  gdouble time;

  *buf = NULL;

  if (G_UNLIKELY (self->width <= 0 || self->height <= 0))
    return GST_FLOW_NOT_NEGOTIATED;

  if (G_UNLIKELY (!self->f0r_instance)) {
    self->f0r_instance =
        gst_frei0r_instance_construct (klass->ftable, klass->properties,
        klass->n_properties, self->property_cache, self->width, self->height);

    if (G_UNLIKELY (!self->f0r_instance))
      return GST_FLOW_ERROR;
  }

  newsize = gst_video_format_get_size (self->fmt, self->width, self->height);

  ret =
      gst_pad_alloc_buffer_and_set_caps (GST_BASE_SRC_PAD (src),
      GST_BUFFER_OFFSET_NONE, newsize, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)),
      &outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  /* Format might have changed */
  size = GST_BUFFER_SIZE (outbuf);
  newsize = gst_video_format_get_size (self->fmt, self->width, self->height);

  if (size != newsize) {
    gst_buffer_unref (outbuf);
    outbuf = gst_buffer_new_and_alloc (newsize);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));
  }

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp =
      gst_util_uint64_scale (self->n_frames, GST_SECOND * self->fps_d,
      self->fps_n);
  GST_BUFFER_OFFSET (outbuf) = self->n_frames;
  self->n_frames++;
  GST_BUFFER_OFFSET_END (outbuf) = self->n_frames;
  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale (self->n_frames, GST_SECOND * self->fps_d,
      self->fps_n) - GST_BUFFER_TIMESTAMP (outbuf);

  timestamp =
      gst_segment_to_stream_time (&GST_BASE_SRC_CAST (self)->segment,
      GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (self, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (self), timestamp);

  time = ((gdouble) GST_BUFFER_TIMESTAMP (outbuf)) / GST_SECOND;

  GST_OBJECT_LOCK (self);
  if (klass->ftable->update2)
    klass->ftable->update2 (self->f0r_instance, time, NULL, NULL, NULL,
        (guint32 *) GST_BUFFER_DATA (outbuf));
  else
    klass->ftable->update (self->f0r_instance, time, NULL,
        (guint32 *) GST_BUFFER_DATA (outbuf));
  GST_OBJECT_UNLOCK (self);

  *buf = outbuf;

  return GST_FLOW_OK;
}

static gboolean
gst_frei0r_src_start (GstBaseSrc * basesrc)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (basesrc);

  self->n_frames = 0;

  return TRUE;
}

static gboolean
gst_frei0r_src_stop (GstBaseSrc * basesrc)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (basesrc);
  GstFrei0rSrcClass *klass = GST_FREI0R_SRC_GET_CLASS (basesrc);

  if (self->f0r_instance) {
    klass->ftable->destruct (self->f0r_instance);
    self->f0r_instance = NULL;
  }

  self->fmt = GST_VIDEO_FORMAT_UNKNOWN;
  self->width = self->height = 0;
  self->fps_n = self->fps_d = 0;
  self->n_frames = 0;

  return TRUE;
}

static gboolean
gst_frei0r_src_is_seekable (GstBaseSrc * psrc)
{
  return TRUE;
}

static gboolean
gst_frei0r_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstClockTime time;
  GstFrei0rSrc *self = GST_FREI0R_SRC (bsrc);

  segment->time = segment->start;
  time = segment->last_stop;

  /* now move to the time indicated */
  if (self->fps_n) {
    self->n_frames = gst_util_uint64_scale (time,
        self->fps_n, self->fps_d * GST_SECOND);
  } else {
    self->n_frames = 0;
  }

  return TRUE;
}

static gboolean
gst_frei0r_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res;
  GstFrei0rSrc *self = GST_FREI0R_SRC (bsrc);
  GstFrei0rSrcClass *klass = GST_FREI0R_SRC_GET_CLASS (self);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (src_fmt == dest_fmt) {
        dest_val = src_val;
        goto done;
      }

      switch (src_fmt) {
        case GST_FORMAT_DEFAULT:
          switch (dest_fmt) {
            case GST_FORMAT_TIME:
              /* frames to time */
              if (self->fps_n) {
                dest_val = gst_util_uint64_scale (src_val,
                    self->fps_d * GST_SECOND, self->fps_n);
              } else {
                dest_val = 0;
              }
              break;
            default:
              goto error;
          }
          break;
        case GST_FORMAT_TIME:
          switch (dest_fmt) {
            case GST_FORMAT_DEFAULT:
              /* time to frames */
              if (self->fps_n) {
                dest_val = gst_util_uint64_scale (src_val,
                    self->fps_n, self->fps_d * GST_SECOND);
              } else {
                dest_val = 0;
              }
              break;
            default:
              goto error;
          }
          break;
        default:
          goto error;
      }
    done:
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      res = TRUE;
      break;
    }
    default:
      res =
          GST_BASE_SRC_CLASS (g_type_class_peek_parent (klass))->query (bsrc,
          query);
  }
  return res;

  /* ERROR */
error:
  {
    GST_DEBUG_OBJECT (self, "query failed");
    return FALSE;
  }
}

static void
gst_frei0r_src_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
}

static void
gst_frei0r_src_finalize (GObject * object)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (object);
  GstFrei0rSrcClass *klass = GST_FREI0R_SRC_GET_CLASS (object);

  if (self->f0r_instance) {
    klass->ftable->destruct (self->f0r_instance);
    self->f0r_instance = NULL;
  }

  if (self->property_cache)
    gst_frei0r_property_cache_free (klass->properties, self->property_cache,
        klass->n_properties);
  self->property_cache = NULL;

  G_OBJECT_CLASS (g_type_class_peek_parent (klass))->finalize (object);
}

static void
gst_frei0r_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (object);
  GstFrei0rSrcClass *klass = GST_FREI0R_SRC_GET_CLASS (object);

  GST_OBJECT_LOCK (self);
  if (!gst_frei0r_get_property (self->f0r_instance, klass->ftable,
          klass->properties, klass->n_properties, self->property_cache, prop_id,
          value))
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_frei0r_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFrei0rSrc *self = GST_FREI0R_SRC (object);
  GstFrei0rSrcClass *klass = GST_FREI0R_SRC_GET_CLASS (object);

  GST_OBJECT_LOCK (self);
  if (!gst_frei0r_set_property (self->f0r_instance, klass->ftable,
          klass->properties, klass->n_properties, self->property_cache, prop_id,
          value))
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_frei0r_src_class_init (GstFrei0rSrcClass * klass,
    GstFrei0rSrcClassData * class_data)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstPushSrcClass *gstpushsrc_class = (GstPushSrcClass *) klass;
  GstBaseSrcClass *gstbasesrc_class = (GstBaseSrcClass *) klass;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar *author;

  klass->ftable = &class_data->ftable;
  klass->info = &class_data->info;

  gobject_class->finalize = gst_frei0r_src_finalize;
  gobject_class->set_property = gst_frei0r_src_set_property;
  gobject_class->get_property = gst_frei0r_src_get_property;

  klass->n_properties = klass->info->num_params;
  klass->properties = g_new0 (GstFrei0rProperty, klass->n_properties);

  gst_frei0r_klass_install_properties (gobject_class, klass->ftable,
      klass->properties, klass->n_properties);

  author =
      g_strdup_printf
      ("Sebastian Dröge <sebastian.droege@collabora.co.uk>, %s",
      class_data->info.author);
  gst_element_class_set_details_simple (gstelement_class, class_data->info.name,
      "Src/Video", class_data->info.explanation, author);
  g_free (author);

  caps = gst_frei0r_caps_from_color_model (class_data->info.color_model);

  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, templ);
  gst_object_unref (templ);

  gstbasesrc_class->set_caps = gst_frei0r_src_set_caps;
  gstbasesrc_class->get_caps = gst_frei0r_src_get_caps;
  gstbasesrc_class->is_seekable = gst_frei0r_src_is_seekable;
  gstbasesrc_class->do_seek = gst_frei0r_src_do_seek;
  gstbasesrc_class->query = gst_frei0r_src_query;
  gstbasesrc_class->start = gst_frei0r_src_start;
  gstbasesrc_class->stop = gst_frei0r_src_stop;

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_frei0r_src_create);
}

static void
gst_frei0r_src_init (GstFrei0rSrc * self, GstFrei0rSrcClass * klass)
{
  GstPad *pad = GST_BASE_SRC_PAD (self);

  self->property_cache =
      gst_frei0r_property_cache_init (klass->properties, klass->n_properties);

  gst_pad_set_fixatecaps_function (pad, gst_frei0r_src_src_fixate);

  gst_base_src_set_format (GST_BASE_SRC_CAST (self), GST_FORMAT_TIME);
}

GstFrei0rPluginRegisterReturn
gst_frei0r_src_register (GstPlugin * plugin, const gchar * vendor,
    const f0r_plugin_info_t * info, const GstFrei0rFuncTable * ftable)
{
  GTypeInfo typeinfo = {
    sizeof (GstFrei0rSrcClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_frei0r_src_class_init,
    NULL,
    NULL,
    sizeof (GstFrei0rSrc),
    0,
    (GInstanceInitFunc) gst_frei0r_src_init
  };
  GType type;
  gchar *type_name, *tmp;
  GstFrei0rSrcClassData *class_data;
  GstFrei0rPluginRegisterReturn ret = GST_FREI0R_PLUGIN_REGISTER_RETURN_FAILED;

  if (vendor)
    tmp = g_strdup_printf ("frei0r-src-%s-%s", vendor, info->name);
  else
    tmp = g_strdup_printf ("frei0r-src-%s", info->name);
  type_name = g_ascii_strdown (tmp, -1);
  g_free (tmp);
  g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');

  if (g_type_from_name (type_name)) {
    GST_DEBUG ("Type '%s' already exists", type_name);
    return GST_FREI0R_PLUGIN_REGISTER_RETURN_ALREADY_REGISTERED;
  }

  class_data = g_new0 (GstFrei0rSrcClassData, 1);
  memcpy (&class_data->info, info, sizeof (f0r_plugin_info_t));
  memcpy (&class_data->ftable, ftable, sizeof (GstFrei0rFuncTable));
  typeinfo.class_data = class_data;

  type = g_type_register_static (GST_TYPE_PUSH_SRC, type_name, &typeinfo, 0);
  if (gst_element_register (plugin, type_name, GST_RANK_NONE, type))
    ret = GST_FREI0R_PLUGIN_REGISTER_RETURN_OK;

  g_free (type_name);
  return ret;
}
