/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstspectrascope.c: frequency spectrum scope
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-spectrascope
 * @see_also: goom
 *
 * Spectrascope is a simple spectrum visualisation element. It renders the
 * frequency spectrum as a series of bars.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc ! audioconvert ! spectrascope ! ximagesink
 * ]|
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>

#include "gstspectrascope.h"

static GstStaticPadTemplate gst_spectra_scope_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate gst_spectra_scope_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );


GST_DEBUG_CATEGORY_STATIC (spectra_scope_debug);
#define GST_CAT_DEFAULT spectra_scope_debug

static void gst_spectra_scope_finalize (GObject * object);

static gboolean gst_spectra_scope_setup (GstBaseAudioVisualizer * scope);
static gboolean gst_spectra_scope_render (GstBaseAudioVisualizer * scope,
    GstBuffer * audio, GstBuffer * video);


GST_BOILERPLATE (GstSpectraScope, gst_spectra_scope, GstBaseAudioVisualizer,
    GST_TYPE_BASE_AUDIO_VISUALIZER);

static void
gst_spectra_scope_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Frequency spectrum scope", "Visualization",
      "Simple frequency spectrum scope", "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_spectra_scope_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_spectra_scope_sink_template);
}

static void
gst_spectra_scope_class_init (GstSpectraScopeClass * g_class)
{
  GObjectClass *gobject_class = (GObjectClass *) g_class;
  GstBaseAudioVisualizerClass *scope_class =
      (GstBaseAudioVisualizerClass *) g_class;

  gobject_class->finalize = gst_spectra_scope_finalize;

  scope_class->setup = GST_DEBUG_FUNCPTR (gst_spectra_scope_setup);
  scope_class->render = GST_DEBUG_FUNCPTR (gst_spectra_scope_render);
}

static void
gst_spectra_scope_init (GstSpectraScope * scope, GstSpectraScopeClass * g_class)
{
  /* do nothing */
}

static void
gst_spectra_scope_finalize (GObject * object)
{
  GstSpectraScope *scope = GST_SPECTRA_SCOPE (object);

  if (scope->fft_ctx) {
    gst_fft_s16_free (scope->fft_ctx);
    scope->fft_ctx = NULL;
  }
  if (scope->freq_data) {
    g_free (scope->freq_data);
    scope->freq_data = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_spectra_scope_setup (GstBaseAudioVisualizer * bscope)
{
  GstSpectraScope *scope = GST_SPECTRA_SCOPE (bscope);
  guint num_freq = bscope->width + 1;

  if (scope->fft_ctx)
    gst_fft_s16_free (scope->fft_ctx);
  g_free (scope->freq_data);

  /* we'd need this amount of samples per render() call */
  bscope->req_spf = num_freq * 2 - 2;
  scope->fft_ctx = gst_fft_s16_new (bscope->req_spf, FALSE);
  scope->freq_data = g_new (GstFFTS16Complex, num_freq);

  return TRUE;
}

static inline void
add_pixel (guint32 * _p, guint32 _c)
{
  guint8 *p = (guint8 *) _p;
  guint8 *c = (guint8 *) & _c;

  if (p[0] < 255 - c[0])
    p[0] += c[0];
  else
    p[0] = 255;
  if (p[1] < 255 - c[1])
    p[1] += c[1];
  else
    p[1] = 255;
  if (p[2] < 255 - c[2])
    p[2] += c[2];
  else
    p[2] = 255;
  if (p[3] < 255 - c[3])
    p[3] += c[3];
  else
    p[3] = 255;
}

static gboolean
gst_spectra_scope_render (GstBaseAudioVisualizer * bscope, GstBuffer * audio,
    GstBuffer * video)
{
  GstSpectraScope *scope = GST_SPECTRA_SCOPE (bscope);
  guint32 *vdata = (guint32 *) GST_BUFFER_DATA (video);
  gint16 *adata = (gint16 *) g_memdup (GST_BUFFER_DATA (audio),
      GST_BUFFER_SIZE (audio));
  GstFFTS16Complex *fdata = scope->freq_data;
  guint x, y, off;
  guint l, h = bscope->height - 1;
  gfloat fr, fi;
  guint w = bscope->width;

  if (bscope->channels > 1) {
    guint ch = bscope->channels;
    guint num_samples = GST_BUFFER_SIZE (audio) / (ch * sizeof (gint16));
    guint i, c, v, s = 0;

    /* deinterleave and mixdown adata */
    for (i = 0; i < num_samples; i++) {
      v = 0;
      for (c = 0; c < ch; c++) {
        v += adata[s++];
      }
      adata[i] = v / ch;
    }
  }

  /* run fft */
  gst_fft_s16_window (scope->fft_ctx, adata, GST_FFT_WINDOW_HAMMING);
  gst_fft_s16_fft (scope->fft_ctx, adata, fdata);
  g_free (adata);

  /* draw lines */
  for (x = 0; x < bscope->width; x++) {
    /* figure out the range so that we don't need to clip,
     * or even better do a log mapping? */
    fr = (gfloat) fdata[1 + x].r / 512.0;
    fi = (gfloat) fdata[1 + x].i / 512.0;
    y = (guint) (h * fabs (fr * fr + fi * fi));
    if (y > h)
      y = h;
    y = h - y;
    off = (y * w) + x;
    vdata[off] = 0x00FFFFFF;
    for (l = y + 1; l <= h; l++) {
      off += w;
      add_pixel (&vdata[off], 0x007F7F7F);
    }
  }
  return TRUE;
}

gboolean
gst_spectra_scope_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (spectra_scope_debug, "spectrascope", 0,
      "spectrascope");

  return gst_element_register (plugin, "spectrascope", GST_RANK_NONE,
      GST_TYPE_SPECTRA_SCOPE);
}
