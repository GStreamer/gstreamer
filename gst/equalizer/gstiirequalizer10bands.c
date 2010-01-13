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
 * The 10 band equalizer element allows to change the gain of 10 equally distributed
 * frequency bands between 30 Hz and 15 kHz.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=song.ogg ! oggdemux ! vorbisdec ! audioconvert ! equalizer-10bands band2=3.0 ! alsasink
 * ]| This raises the volume of the 3rd band which is at 119 Hz by 3 db.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstiirequalizer.h"
#include "gstiirequalizer10bands.h"


enum
{
  PROP_BAND0 = 1,
  PROP_BAND1,
  PROP_BAND2,
  PROP_BAND3,
  PROP_BAND4,
  PROP_BAND5,
  PROP_BAND6,
  PROP_BAND7,
  PROP_BAND8,
  PROP_BAND9,
};

static void gst_iir_equalizer_10bands_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iir_equalizer_10bands_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GST_DEBUG_CATEGORY_EXTERN (equalizer_debug);
#define GST_CAT_DEFAULT equalizer_debug


static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstIirEqualizer10Bands, gst_iir_equalizer_10bands,
    GstIirEqualizer, GST_TYPE_IIR_EQUALIZER, _do_init);

/* equalizer implementation */

static void
gst_iir_equalizer_10bands_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "10 Band Equalizer",
      "Filter/Effect/Audio",
      "Direct Form 10 band IIR equalizer",
      "Stefan Kost <ensonic@users.sf.net>");
}

static void
gst_iir_equalizer_10bands_class_init (GstIirEqualizer10BandsClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_iir_equalizer_10bands_set_property;
  gobject_class->get_property = gst_iir_equalizer_10bands_get_property;

  g_object_class_install_property (gobject_class, PROP_BAND0,
      g_param_spec_double ("band0", "29 Hz",
          "gain for the frequency band 29 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND1,
      g_param_spec_double ("band1", "59 Hz",
          "gain for the frequency band 59 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND2,
      g_param_spec_double ("band2", "119 Hz",
          "gain for the frequency band 119 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND3,
      g_param_spec_double ("band3", "237 Hz",
          "gain for the frequency band 237 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND4,
      g_param_spec_double ("band4", "474 Hz",
          "gain for the frequency band 474 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND5,
      g_param_spec_double ("band5", "947 Hz",
          "gain for the frequency band 947 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND6,
      g_param_spec_double ("band6", "1889 Hz",
          "gain for the frequency band 1889 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND7,
      g_param_spec_double ("band7", "3770 Hz",
          "gain for the frequency band 3770 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND8,
      g_param_spec_double ("band8", "7523 Hz",
          "gain for the frequency band 7523 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BAND9,
      g_param_spec_double ("band9", "15011 Hz",
          "gain for the frequency band 15011 Hz, ranging from -24 dB to +12 dB",
          -24.0, 12.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));
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
    case PROP_BAND0:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band0::gain", value);
      break;
    case PROP_BAND1:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band1::gain", value);
      break;
    case PROP_BAND2:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band2::gain", value);
      break;
    case PROP_BAND3:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band3::gain", value);
      break;
    case PROP_BAND4:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band4::gain", value);
      break;
    case PROP_BAND5:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band5::gain", value);
      break;
    case PROP_BAND6:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band6::gain", value);
      break;
    case PROP_BAND7:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band7::gain", value);
      break;
    case PROP_BAND8:
      gst_child_proxy_set_property (GST_OBJECT (equ), "band8::gain", value);
      break;
    case PROP_BAND9:
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
    case PROP_BAND0:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band0::gain", value);
      break;
    case PROP_BAND1:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band1::gain", value);
      break;
    case PROP_BAND2:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band2::gain", value);
      break;
    case PROP_BAND3:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band3::gain", value);
      break;
    case PROP_BAND4:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band4::gain", value);
      break;
    case PROP_BAND5:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band5::gain", value);
      break;
    case PROP_BAND6:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band6::gain", value);
      break;
    case PROP_BAND7:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band7::gain", value);
      break;
    case PROP_BAND8:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band8::gain", value);
      break;
    case PROP_BAND9:
      gst_child_proxy_get_property (GST_OBJECT (equ), "band9::gain", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
