/* GStreamer
 * Copyright (C) <2007> Stefan Kost <ensonic@users.sf.net>
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

/**
 * SECTION:element-equalizer-10bands
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * The 10 band equalizer element changes the frequency spectrum of the audio data.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=song.ogg ! oggdemux ! vorbisdec ! audioconvert ! equalizer-10bands band2=-1.0 ! alsasink
 * </programlisting>
 * This lowers the volume of the 3rd band which is at 93 Hz by FIXME db.
 * </para>
 * </refsect2>
 */

/*
 *
 * gst-launch filesrc location=song.ogg ! oggdemux ! vorbisdec ! audioconvert ! equalizer-10bands band1=-1.0 ! alsasink
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstiirequalizer.h"
#include "gstiirequalizer10bands.h"


enum
{
  ARG_BAND0 = 1,
  ARG_BAND1,
  ARG_BAND2,
  ARG_BAND3,
  ARG_BAND4,
  ARG_BAND5,
  ARG_BAND6,
  ARG_BAND7,
  ARG_BAND8,
  ARG_BAND9,
};

static void gst_iir_equalizer_10bands_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iir_equalizer_10bands_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GST_DEBUG_CATEGORY_EXTERN (equalizer_debug);
#define GST_CAT_DEFAULT equalizer_debug

GST_BOILERPLATE (GstIirEqualizer10Bands, gst_iir_equalizer_10bands,
    GstIirEqualizer, GST_TYPE_IIR_EQUALIZER);

/* equalizer implementation */

static void
gst_iir_equalizer_10bands_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  const GstElementDetails iir_equalizer_details =
      GST_ELEMENT_DETAILS ("10 Band Equalizer",
      "Filter/Effect/Audio",
      "Direct Form 10 band IIR equalizer",
      "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_set_details (element_class, &iir_equalizer_details);
}

static void
gst_iir_equalizer_10bands_class_init (GstIirEqualizer10BandsClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_iir_equalizer_10bands_set_property;
  gobject_class->get_property = gst_iir_equalizer_10bands_get_property;

  g_object_class_install_property (gobject_class, ARG_BAND0,
      g_param_spec_double ("band0", "20 Hz",
          "gain for the frequency band 20 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND1,
      g_param_spec_double ("band1", "43 Hz",
          "gain for the frequency band 43 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND2,
      g_param_spec_double ("band2", "93 Hz",
          "gain for the frequency band 93 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND3,
      g_param_spec_double ("band3", "200 Hz",
          "gain for the frequency band 200 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND4,
      g_param_spec_double ("band4", "430 Hz",
          "gain for the frequency band 430 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND5,
      g_param_spec_double ("band5", "928 Hz",
          "gain for the frequency band 928 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND6,
      g_param_spec_double ("band6", "2000 Hz",
          "gain for the frequency band 2000 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND7,
      g_param_spec_double ("band7", "4308 Hz",
          "gain for the frequency band 4308 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND8,
      g_param_spec_double ("band8", "9283 Hz",
          "gain for the frequency band 9283 Hz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, ARG_BAND9,
      g_param_spec_double ("band9", "20 kHz",
          "gain for the frequency band 20 kHz, ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
}

static void
gst_iir_equalizer_10bands_init (GstIirEqualizer10Bands * equ_n,
    GstIirEqualizer10BandsClass * g_class)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (equ_n);

  gst_iir_equalizer_compute_frequencies (equ, 10);
}

static void
gst_iir_equalizer_10bands_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  switch (prop_id) {
    case ARG_BAND0:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band0::gain", value);
      break;
    case ARG_BAND1:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band1::gain", value);
      break;
    case ARG_BAND2:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band2::gain", value);
      break;
    case ARG_BAND3:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band3::gain", value);
      break;
    case ARG_BAND4:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band4::gain", value);
      break;
    case ARG_BAND5:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band5::gain", value);
      break;
    case ARG_BAND6:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band6::gain", value);
      break;
    case ARG_BAND7:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band7::gain", value);
      break;
    case ARG_BAND8:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band8::gain", value);
      break;
    case ARG_BAND9:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band9::gain", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_iir_equalizer_10bands_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  switch (prop_id) {
    case ARG_BAND0:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band0::gain", value);
      break;
    case ARG_BAND1:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band1::gain", value);
      break;
    case ARG_BAND2:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band2::gain", value);
      break;
    case ARG_BAND3:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band3::gain", value);
      break;
    case ARG_BAND4:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band4::gain", value);
      break;
    case ARG_BAND5:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band5::gain", value);
      break;
    case ARG_BAND6:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band6::gain", value);
      break;
    case ARG_BAND7:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band7::gain", value);
      break;
    case ARG_BAND8:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band8::gain", value);
      break;
    case ARG_BAND9:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band9::gain", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
