/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/* This effect is borrowed from xmms-0.6.1, though I mangled it so badly in
 * the process of copying it over that the xmms people probably won't want
 * any credit for it ;-)
 */
/**
 * SECTION:element-stereo
 *
 * Create a wide stereo effect.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! stereo ! audioconvert ! audioresample ! alsasink
 * ]| Play an Ogg/Vorbis file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gststereo.h"

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

#define ALLOWED_CAPS \
    "audio/x-raw-int,"                                                \
    " depth = (int) 16, "                                             \
    " width = (int) 16, "                                             \
    " endianness = (int) BYTE_ORDER,"                                 \
    " rate = (int) [ 1, MAX ],"                                       \
    " channels = (int) 2, "                                           \
    " signed = (boolean) TRUE"

/* Stereo signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_ACTIVE,
  ARG_STEREO
};

static void gst_stereo_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_stereo_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_stereo_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);

/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

GST_BOILERPLATE (GstStereo, gst_stereo, GstAudioFilter, GST_TYPE_AUDIO_FILTER);

static void
gst_stereo_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_static_metadata (element_class, "Stereo effect",
      "Filter/Effect/Audio",
      "Muck with the stereo signal to enhance its 'stereo-ness'",
      "Erik Walthinsen <omega@cse.ogi.edu>");

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_stereo_class_init (GstStereoClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_stereo_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_stereo_get_property);

  g_object_class_install_property (gobject_class, ARG_ACTIVE,
      g_param_spec_boolean ("active", "active", "active",
          TRUE,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_STEREO,
      g_param_spec_float ("stereo", "stereo", "stereo",
          0.0, 1.0, 0.1,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_stereo_transform_ip);
}

static void
gst_stereo_init (GstStereo * stereo, GstStereoClass * klass)
{
  stereo->active = TRUE;
  stereo->stereo = 0.1;
}

static GstFlowReturn
gst_stereo_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstStereo *stereo = GST_STEREO (base);
  gint16 *data = (gint16 *) GST_BUFFER_DATA (outbuf);
  gint samples = GST_BUFFER_SIZE (outbuf) / 2;
  gint i;
  gdouble avg, ldiff, rdiff, tmp;
  gdouble mul = stereo->stereo;

  if (!gst_buffer_is_writable (outbuf))
    return GST_FLOW_OK;

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (outbuf)))
    gst_object_sync_values (GST_OBJECT (stereo), GST_BUFFER_TIMESTAMP (outbuf));

  if (stereo->active) {
    for (i = 0; i < samples / 2; i += 2) {
      avg = (data[i] + data[i + 1]) / 2;
      ldiff = data[i] - avg;
      rdiff = data[i + 1] - avg;

      tmp = avg + ldiff * mul;
      if (tmp < -32768)
        tmp = -32768;
      if (tmp > 32767)
        tmp = 32767;
      data[i] = tmp;

      tmp = avg + rdiff * mul;
      if (tmp < -32768)
        tmp = -32768;
      if (tmp > 32767)
        tmp = 32767;
      data[i + 1] = tmp;
    }
  }

  return GST_FLOW_OK;
}

static void
gst_stereo_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstStereo *stereo;

  g_return_if_fail (GST_IS_STEREO (object));
  stereo = GST_STEREO (object);

  switch (prop_id) {
    case ARG_ACTIVE:
      stereo->active = g_value_get_boolean (value);
      break;
    case ARG_STEREO:
      stereo->stereo = g_value_get_float (value) * 10.0;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_stereo_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstStereo *stereo;

  g_return_if_fail (GST_IS_STEREO (object));
  stereo = GST_STEREO (object);

  switch (prop_id) {
    case ARG_ACTIVE:
      g_value_set_boolean (value, stereo->active);
      break;
    case ARG_STEREO:
      g_value_set_float (value, stereo->stereo / 10.0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "stereo", GST_RANK_NONE,
      GST_TYPE_STEREO);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    stereo,
    "Muck with the stereo signal, enhance it's 'stereo-ness'",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
