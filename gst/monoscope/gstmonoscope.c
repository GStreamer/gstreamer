/* gstmonoscope.c: implementation of monoscope drawing element
 * Copyright (C) <2002> Richard Boulton <richard@tartarus.org>
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include "monoscope.h"

#define GST_TYPE_MONOSCOPE (gst_monoscope_get_type())
#define GST_MONOSCOPE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MONOSCOPE,GstMonoscope))
#define GST_MONOSCOPE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MONOSCOPE,GstMonoscope))
#define GST_IS_MONOSCOPE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MONOSCOPE))
#define GST_IS_MONOSCOPE_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MONOSCOPE))

typedef struct _GstMonoscope GstMonoscope;
typedef struct _GstMonoscopeClass GstMonoscopeClass;

struct _GstMonoscope
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* the timestamp of the next frame */
  guint64 next_time;
  gint16 datain[512];

  /* video state */
  gdouble fps;
  gint width;
  gint height;
  gboolean first_buffer;

  /* visualisation state */
  struct monoscope_state *visstate;
};

struct _GstMonoscopeClass
{
  GstElementClass parent_class;
};

GType gst_monoscope_get_type (void);


/* elementfactory information */
static GstElementDetails gst_monoscope_details = {
  "Monoscope",
  "Visualization",
  "Displays a highly stabilised waveform of audio input",
  "Richard Boulton <richard@tartarus.org>"
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

#if G_BYTE_ORDER == G_BIG_ENDIAN
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
        "green_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
        "blue_mask = (int) " GST_VIDEO_BYTE4_MASK_32 ", "
        "width = (int)256, "
        "height = (int)128, " "framerate = " GST_VIDEO_FPS_RANGE)
    );
#else
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
        "green_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
        "blue_mask = (int) " GST_VIDEO_BYTE1_MASK_32 ", "
        "width = (int)256, "
        "height = (int)128, " "framerate = " GST_VIDEO_FPS_RANGE)
    );
#endif

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );


static void gst_monoscope_class_init (GstMonoscopeClass * klass);
static void gst_monoscope_base_init (GstMonoscopeClass * klass);
static void gst_monoscope_init (GstMonoscope * monoscope);

static void gst_monoscope_chain (GstPad * pad, GstData * _data);

static GstPadLinkReturn
gst_monoscope_srcconnect (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_monoscope_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstMonoscopeClass),
      (GBaseInitFunc) gst_monoscope_base_init,
      NULL,
      (GClassInitFunc) gst_monoscope_class_init,
      NULL,
      NULL,
      sizeof (GstMonoscope),
      0,
      (GInstanceInitFunc) gst_monoscope_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstMonoscope", &info, 0);
  }
  return type;
}

static void
gst_monoscope_base_init (GstMonoscopeClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (element_class, &gst_monoscope_details);
}

static void
gst_monoscope_class_init (GstMonoscopeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void
gst_monoscope_init (GstMonoscope * monoscope)
{
  /* create the sink and src pads */
  monoscope->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  monoscope->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (monoscope), monoscope->sinkpad);
  gst_element_add_pad (GST_ELEMENT (monoscope), monoscope->srcpad);

  gst_pad_set_chain_function (monoscope->sinkpad, gst_monoscope_chain);
  gst_pad_set_link_function (monoscope->srcpad, gst_monoscope_srcconnect);

  monoscope->next_time = 0;

  /* reset the initial video state */
  monoscope->first_buffer = TRUE;
  monoscope->width = 256;
  monoscope->height = 128;
  monoscope->fps = 25.;         /* desired frame rate */
}

static GstPadLinkReturn
gst_monoscope_srcconnect (GstPad * pad, const GstCaps * caps)
{
  GstMonoscope *monoscope = GST_MONOSCOPE (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &monoscope->width);
  gst_structure_get_int (structure, "height", &monoscope->height);
  gst_structure_get_double (structure, "framerate", &monoscope->fps);

  return GST_PAD_LINK_OK;
}

static void
gst_monoscope_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *bufin = GST_BUFFER (_data);
  GstMonoscope *monoscope;
  GstBuffer *bufout;
  guint32 samples_in;
  gint16 *data;
  gint i;

  monoscope = GST_MONOSCOPE (gst_pad_get_parent (pad));

  GST_DEBUG ("Monoscope: chainfunc called");

  samples_in = GST_BUFFER_SIZE (bufin) / sizeof (gint16);

  GST_DEBUG ("input buffer has %d samples", samples_in);

  /* FIXME: should really select the first 1024 samples after the timestamp. */
  if (GST_BUFFER_TIMESTAMP (bufin) < monoscope->next_time || samples_in < 1024) {
    GST_DEBUG ("timestamp is %" G_GUINT64_FORMAT ": want >= %" G_GUINT64_FORMAT,
        GST_BUFFER_TIMESTAMP (bufin), monoscope->next_time);
    gst_buffer_unref (bufin);
    return;
  }

  data = (gint16 *) GST_BUFFER_DATA (bufin);
  /* FIXME: Select samples in a better way. */
  for (i = 0; i < 512; i++) {
    monoscope->datain[i] = *data++;
  }

  if (monoscope->first_buffer) {
    monoscope->visstate = monoscope_init (monoscope->width, monoscope->height);
    g_assert (monoscope->visstate != 0);
    GST_DEBUG ("making new pad");
    if (!gst_pad_is_negotiated (monoscope->srcpad)) {
      if (gst_pad_renegotiate (monoscope->srcpad) <= 0) {
        GST_ELEMENT_ERROR (monoscope, CORE, NEGOTIATION, (NULL), (NULL));
        return;
      }
    }
    monoscope->first_buffer = FALSE;
  }

  bufout = gst_buffer_new ();
  GST_BUFFER_SIZE (bufout) = monoscope->width * monoscope->height * 4;
  GST_BUFFER_DATA (bufout) =
      (guchar *) monoscope_update (monoscope->visstate, monoscope->datain);
  GST_BUFFER_TIMESTAMP (bufout) = monoscope->next_time;
  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_DONTFREE);

  monoscope->next_time += GST_SECOND / monoscope->fps;

  gst_pad_push (monoscope->srcpad, GST_DATA (bufout));

  gst_buffer_unref (bufin);

  GST_DEBUG ("Monoscope: exiting chainfunc");

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "monoscope",
      GST_RANK_NONE, GST_TYPE_MONOSCOPE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "monoscope",
    "Monoscope visualization",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
