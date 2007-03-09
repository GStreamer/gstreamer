/* GStreamer
 * Copyright (C) <2004> Benjamin Otte <otte@gnome.org>
 *               <2007> Stefan Kost <ensonic@users.sf.net>
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

/*
 *
 * gst-launch filesrc location=song.ogg ! oggdemux ! vorbisdec ! audioconvert ! equalizer-nbands rband5::gain=-1.0 ! alsasink
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstiirequalizer.h"
#include "gstiirequalizernbands.h"


enum
{
  ARG_NUM_BANDS = 1
};

static void gst_iir_equalizer_nbands_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iir_equalizer_nbands_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GST_DEBUG_CATEGORY_EXTERN ()(equalizer_debug);
#define GST_CAT_DEFAULT equalizer_debug

GST_BOILERPLATE (GstIirEqualizerNBands, gst_iir_equalizer_nbands,
    GstIirEqualizer, GST_TYPE_IIR_EQUALIZER);

/* equalizer implementation */

static void
gst_iir_equalizer_nbands_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  const GstElementDetails iir_equalizer_details =
      GST_ELEMENT_DETAILS ("N Band Equalizer",
      "Filter/Effect/Audio",
      "Direct Form IIR equalizer",
      "Benjamin Otte <otte@gnome.org>," " Stefan Kost <ensonic@user.sf.net>");

  gst_element_class_set_details (element_class, &iir_equalizer_details);
}

static void
gst_iir_equalizer_nbands_class_init (GstIirEqualizerNBandsClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_iir_equalizer_nbands_set_property;
  gobject_class->get_property = gst_iir_equalizer_nbands_get_property;

  g_object_class_install_property (gobject_class, ARG_NUM_BANDS,
      g_param_spec_uint ("num-bands", "num-bands",
          "number of different bands to use", 2, 64, 10,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gst_iir_equalizer_nbands_init (GstIirEqualizerNBands * equ_n,
    GstIirEqualizerNBandsClass * g_class)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (equ_n);

  gst_iir_equalizer_compute_frequencies (equ, 10);
}

static void
gst_iir_equalizer_nbands_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  GST_EQUALIZER_TRANSFORM_LOCK (equ);
  GST_OBJECT_LOCK (equ);
  switch (prop_id) {
    case ARG_NUM_BANDS:
      gst_iir_equalizer_compute_frequencies (equ, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (equ);
  GST_EQUALIZER_TRANSFORM_UNLOCK (equ);
}

static void
gst_iir_equalizer_nbands_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  switch (prop_id) {
    case ARG_NUM_BANDS:
      g_value_set_uint (value, equ->freq_band_count);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
