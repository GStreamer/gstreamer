/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/control/control.h>
#include <gst/mixer/mixer.h>
#include "gstvolume.h"

/* some defines for audio processing */
/* the volume factor is a range from 0.0 to (arbitrary) 4.0
 * we map 1.0 to VOLUME_UNITY_INT
 */
#define VOLUME_UNITY_INT	8192	/* internal int for unity */
#define VOLUME_UNITY_BIT_SHIFT	13	/* number of bits to shift
					   for unity */
#define VOLUME_MAX_DOUBLE	4.0
#define VOLUME_MAX_INT16	32767
#define VOLUME_MIN_INT16	-32768

/* number of steps we use for the mixer interface to go from 0.0 to 1.0 */
# define VOLUME_STEPS		100

static GstElementDetails volume_details = {
  "Volume",
  "Filter/Effect/Audio",
  "Set volume on audio/raw streams",
  "Andy Wingo <apwingo@eos.ncsu.edu>",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SILENT,
  ARG_MUTE,
  ARG_VOLUME
};

static GstStaticPadTemplate volume_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
	"rate = (int) [ 1, MAX ], "
	"channels = (int) [ 1, MAX ], "
	"endianness = (int) BYTE_ORDER, "
	"width = (int) 32, "
	"buffer-frames = (int) [ 1, MAX]; "
	"audio/x-raw-int, "
	"channels = (int) [ 1, MAX ], "
	"rate = (int) [ 1,  MAX ], "
	"endianness = (int) BYTE_ORDER, "
	"width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static GstStaticPadTemplate volume_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
	"rate = (int) [ 1, MAX ], "
	"channels = (int) [ 1, MAX ], "
	"endianness = (int) BYTE_ORDER, "
	"width = (int) 32, "
	"buffer-frames = (int) [ 1, MAX]; "
	"audio/x-raw-int, "
	"channels = (int) [ 1, MAX ], "
	"rate = (int) [ 1,  MAX ], "
	"endianness = (int) BYTE_ORDER, "
	"width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static void volume_base_init (gpointer g_class);
static void volume_class_init (GstVolumeClass * klass);
static void volume_init (GstVolume * filter);

static void volume_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void volume_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void volume_update_volume (const GValue * value, gpointer data);
static void volume_update_mute (const GValue * value, gpointer data);

static gboolean volume_parse_caps (GstVolume * filter,
    GstStructure * structure);

static void volume_chain_float (GstPad * pad, GstData * _data);
static void volume_chain_int16 (GstPad * pad, GstData * _data);

static void gst_volume_interface_init (GstImplementsInterfaceClass * klass);
static void gst_volume_mixer_init (GstMixerClass * iface);

static GstElementClass *parent_class = NULL;

/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

static gboolean
gst_volume_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_MIXER);
  return TRUE;
}

static void
gst_volume_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_volume_interface_supported;
}

static const GList *
gst_volume_list_tracks (GstMixer * mixer)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_val_if_fail (filter != NULL, NULL);
  g_return_val_if_fail (GST_IS_VOLUME (filter), NULL);

  return filter->tracklist;
}

static void
gst_volume_set_volume (GstMixer * mixer, GstMixerTrack * track, gint * volumes)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_VOLUME (filter));

  gst_dpman_bypass_dparam (filter->dpman, "volume");

  filter->volume_f = (gfloat) volumes[0] / VOLUME_STEPS;
  filter->volume_i = filter->volume_f * VOLUME_UNITY_INT;

  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
gst_volume_get_volume (GstMixer * mixer, GstMixerTrack * track, gint * volumes)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_VOLUME (filter));

  volumes[0] = (gint) filter->volume_f * VOLUME_STEPS;
}

static void
gst_volume_set_mute (GstMixer * mixer, GstMixerTrack * track, gboolean mute)
{
  GstVolume *filter = GST_VOLUME (mixer);

  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_VOLUME (filter));

  gst_dpman_bypass_dparam (filter->dpman, "volume");

  filter->mute = mute;

  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
gst_volume_mixer_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_SOFTWARE;

  /* default virtual functions */
  klass->list_tracks = gst_volume_list_tracks;
  klass->set_volume = gst_volume_set_volume;
  klass->get_volume = gst_volume_get_volume;
  klass->set_mute = gst_volume_set_mute;
}

static void
gst_volume_dispose (GObject * object)
{
  GstVolume *volume;

  volume = GST_VOLUME (object);

  if (volume->tracklist) {
    if (volume->tracklist->data)
      g_object_unref (volume->tracklist->data);
    g_list_free (volume->tracklist);
    volume->tracklist = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPadLinkReturn
volume_connect (GstPad * pad, const GstCaps * caps)
{
  GstVolume *filter;
  GstPad *otherpad;
  gint rate;
  GstPadLinkReturn link_ret;
  GstStructure *structure;

  filter = GST_VOLUME (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_IS_VOLUME (filter), GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "rate", &rate);

  link_ret = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_FAILED (link_ret)) {
    return link_ret;
  }

  if (!volume_parse_caps (filter, structure))
    return GST_PAD_LINK_REFUSED;

  gst_dpman_set_rate (filter->dpman, rate);

  return GST_PAD_LINK_OK;
}

static gboolean
volume_parse_caps (GstVolume * filter, GstStructure * structure)
{
  const gchar *mimetype;

  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (structure != NULL, FALSE);

  mimetype = gst_structure_get_name (structure);

  if (strcmp (mimetype, "audio/x-raw-int") == 0) {
    gst_pad_set_chain_function (filter->sinkpad, volume_chain_int16);
    return TRUE;
  }

  if (strcmp (mimetype, "audio/x-raw-float") == 0) {
    gst_pad_set_chain_function (filter->sinkpad, volume_chain_float);
    return TRUE;
  }

  return FALSE;
}


GType
gst_volume_get_type (void)
{
  static GType volume_type = 0;

  if (!volume_type) {
    static const GTypeInfo volume_info = {
      sizeof (GstVolumeClass),
      volume_base_init,
      NULL,
      (GClassInitFunc) volume_class_init,
      NULL,
      NULL,
      sizeof (GstVolume),
      0,
      (GInstanceInitFunc) volume_init
    };
    static const GInterfaceInfo voliface_info = {
      (GInterfaceInitFunc) gst_volume_interface_init,
      NULL,
      NULL
    };
    static const GInterfaceInfo volmixer_info = {
      (GInterfaceInitFunc) gst_volume_mixer_init,
      NULL,
      NULL
    };

    volume_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstVolume", &volume_info, 0);
    g_type_add_interface_static (volume_type, GST_TYPE_IMPLEMENTS_INTERFACE,
	&voliface_info);
    g_type_add_interface_static (volume_type, GST_TYPE_MIXER, &volmixer_info);
  }
  return volume_type;
}
static void
volume_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&volume_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&volume_sink_factory));
  gst_element_class_set_details (element_class, &volume_details);
}
static void
volume_class_init (GstVolumeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MUTE,
      g_param_spec_boolean ("mute", "mute", "mute", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
	  0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));

  gobject_class->set_property = volume_set_property;
  gobject_class->get_property = volume_get_property;
  gobject_class->dispose = gst_volume_dispose;
}

static void
volume_init (GstVolume * filter)
{
  GstMixerTrack *track = NULL;

  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&volume_sink_factory), "sink");
  gst_pad_set_getcaps_function (filter->sinkpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (filter->sinkpad, volume_connect);

  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&volume_src_factory), "src");
  gst_pad_set_getcaps_function (filter->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (filter->srcpad, volume_connect);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  gst_pad_set_chain_function (filter->sinkpad, volume_chain_int16);

  filter->mute = FALSE;
  filter->volume_i = VOLUME_UNITY_INT;
  filter->volume_f = 1.0;
  filter->real_vol_i = VOLUME_UNITY_INT;
  filter->real_vol_f = 1.0;
  filter->tracklist = NULL;

  filter->dpman = gst_dpman_new ("volume_dpman", GST_ELEMENT (filter));
  gst_dpman_add_required_dparam_callback (filter->dpman,
      g_param_spec_int ("mute", "Mute", "Mute the audio",
	  0, 1, 0, G_PARAM_READWRITE), "int", volume_update_mute, filter);
  gst_dpman_add_required_dparam_callback (filter->dpman,
      g_param_spec_double ("volume", "Volume", "Volume of the audio",
	  0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE),
      "scalar", volume_update_volume, filter);

  track = g_object_new (GST_TYPE_MIXER_TRACK, NULL);

  if (GST_IS_MIXER_TRACK (track)) {
    track->label = g_strdup ("volume");
    track->num_channels = 1;
    track->min_volume = 0;
    track->max_volume = VOLUME_STEPS;
    track->flags = GST_MIXER_TRACK_SOFTWARE;
    filter->tracklist = g_list_append (filter->tracklist, track);
  }
}

static void
volume_chain_float (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVolume *filter;
  GstBuffer *out_buf;
  gfloat *data;
  gint i, num_samples;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_VOLUME (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_VOLUME (filter));

  out_buf = gst_buffer_copy_on_write (buf);

  data = (gfloat *) GST_BUFFER_DATA (out_buf);
  num_samples = GST_BUFFER_SIZE (out_buf) / sizeof (gfloat);
  GST_DPMAN_PREPROCESS (filter->dpman, num_samples,
      GST_BUFFER_TIMESTAMP (out_buf));
  i = 0;

  while (GST_DPMAN_PROCESS (filter->dpman, i)) {
    data[i++] *= filter->real_vol_f;
  }

  gst_pad_push (filter->srcpad, GST_DATA (out_buf));

}

static void
volume_chain_int16 (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVolume *filter;
  GstBuffer *out_buf;
  gint16 *data;
  gint i, num_samples;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_VOLUME (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_VOLUME (filter));

  out_buf = gst_buffer_copy_on_write (buf);

  data = (gint16 *) GST_BUFFER_DATA (out_buf);
  g_assert (data);
  num_samples = GST_BUFFER_SIZE (out_buf) / sizeof (gint16);
  GST_DPMAN_PREPROCESS (filter->dpman, num_samples,
      GST_BUFFER_TIMESTAMP (out_buf));
  i = 0;

  while (GST_DPMAN_PROCESS (filter->dpman, i)) {
    /* only clamp if the gain is greater than 1.0 */
    if (filter->real_vol_i > VOLUME_UNITY_INT) {
      while (i < GST_DPMAN_NEXT_UPDATE_FRAME (filter->dpman)) {
	/* we use bitshifting instead of dividing by UNITY_INT for speed */
	data[i] =
	    (gint16) CLAMP ((filter->real_vol_i *
		(gint) data[i]) >> VOLUME_UNITY_BIT_SHIFT, VOLUME_MIN_INT16,
	    VOLUME_MAX_INT16);
	i++;
      }
    } else {
      while (i < GST_DPMAN_NEXT_UPDATE_FRAME (filter->dpman)) {
	/* we use bitshifting instead of dividing by UNITY_INT for speed */
	data[i] =
	    (gint16) ((filter->real_vol_i *
		(gint) data[i]) >> VOLUME_UNITY_BIT_SHIFT);
	i++;
      }
    }
  }

  gst_pad_push (filter->srcpad, GST_DATA (out_buf));

}

static void
volume_update_mute (const GValue * value, gpointer data)
{
  GstVolume *filter = (GstVolume *) data;

  g_return_if_fail (GST_IS_VOLUME (filter));

  if (G_VALUE_HOLDS_BOOLEAN (value)) {
    filter->mute = g_value_get_boolean (value);
  } else if (G_VALUE_HOLDS_INT (value)) {
    filter->mute = (g_value_get_int (value) == 1);
  }

  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
volume_update_volume (const GValue * value, gpointer data)
{
  GstVolume *filter = (GstVolume *) data;

  g_return_if_fail (GST_IS_VOLUME (filter));

  filter->volume_f = g_value_get_double (value);
  filter->volume_i = filter->volume_f * VOLUME_UNITY_INT;
  if (filter->mute) {
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  } else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
volume_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVolume *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VOLUME (object));
  filter = GST_VOLUME (object);

  switch (prop_id) {
    case ARG_MUTE:
      gst_dpman_bypass_dparam (filter->dpman, "mute");
      volume_update_mute (value, filter);
      break;
    case ARG_VOLUME:
      gst_dpman_bypass_dparam (filter->dpman, "volume");
      volume_update_volume (value, filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
volume_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVolume *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VOLUME (object));
  filter = GST_VOLUME (object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, filter->mute);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, filter->volume_f);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_control_init (NULL, NULL);

  return gst_element_register (plugin, "volume", GST_RANK_NONE,
      GST_TYPE_VOLUME);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "volume",
    "element for controlling audio volume",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
