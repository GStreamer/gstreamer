/* gstsynaesthesia.c: implementation of synaesthesia drawing element
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
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
#include "synaescope.h"

#define GST_TYPE_SYNAESTHESIA (gst_synaesthesia_get_type())
#define GST_SYNAESTHESIA(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_SYNAESTHESIA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_IS_SYNAESTHESIA(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SYNAESTHESIA))
#define GST_IS_SYNAESTHESIA_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SYNAESTHESIA))

typedef struct _GstSynaesthesia GstSynaesthesia;
typedef struct _GstSynaesthesiaClass GstSynaesthesiaClass;

struct _GstSynaesthesia
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* the timestamp of the next frame */
  guint64 next_time;
  gint16 datain[2][512];

  /* video state */
  gfloat fps;
  gint width;
  gint height;
  gboolean first_buffer;
};

struct _GstSynaesthesiaClass
{
  GstElementClass parent_class;
};

GType gst_synaesthesia_get_type (void);


/* elementfactory information */
static GstElementDetails gst_synaesthesia_details =
GST_ELEMENT_DETAILS ("Synaesthesia",
    "Visualization",
    "Creates video visualizations of audio input, using stereo and pitch information",
    "Richard Boulton <richard@tartarus.org>");

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FPS,
  /* FILL ME */
};

static GstStaticPadTemplate gst_synaesthesia_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate gst_synaesthesia_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );


static void gst_synaesthesia_base_init (gpointer g_class);
static void gst_synaesthesia_class_init (GstSynaesthesiaClass * klass);
static void gst_synaesthesia_init (GstSynaesthesia * synaesthesia);

static void gst_synaesthesia_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_synaesthesia_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_synaesthesia_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn
gst_synaesthesia_change_state (GstElement * element);

static GstPadLinkReturn
gst_synaesthesia_sink_link (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_synaesthesia_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstSynaesthesiaClass),
      gst_synaesthesia_base_init,
      NULL,
      (GClassInitFunc) gst_synaesthesia_class_init,
      NULL,
      NULL,
      sizeof (GstSynaesthesia),
      0,
      (GInstanceInitFunc) gst_synaesthesia_init,
    };

    type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSynaesthesia", &info, 0);
  }
  return type;
}

static void
gst_synaesthesia_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_synaesthesia_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_synaesthesia_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_synaesthesia_sink_template));
}

static void
gst_synaesthesia_class_init (GstSynaesthesiaClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
      g_param_spec_int ("width", "Width", "The Width",
          1, 2048, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
      g_param_spec_int ("height", "Height", "The height",
          1, 2048, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FPS,
      g_param_spec_float ("fps", "FPS", "Frames per second",
          0., G_MAXFLOAT, 25., G_PARAM_READWRITE));

  gobject_class->set_property = gst_synaesthesia_set_property;
  gobject_class->get_property = gst_synaesthesia_get_property;

  gstelement_class->change_state = gst_synaesthesia_change_state;
}

static void
gst_synaesthesia_init (GstSynaesthesia * synaesthesia)
{
  /* create the sink and src pads */
  synaesthesia->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_synaesthesia_sink_template), "sink");
  synaesthesia->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_synaesthesia_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->sinkpad);
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->srcpad);

  gst_pad_set_chain_function (synaesthesia->sinkpad, gst_synaesthesia_chain);
  gst_pad_set_link_function (synaesthesia->sinkpad, gst_synaesthesia_sink_link);

  GST_FLAG_SET (synaesthesia, GST_ELEMENT_EVENT_AWARE);

  /* reset the initial video state */
  synaesthesia->width = 320;
  synaesthesia->height = 200;
  synaesthesia->fps = 25.;      /* desired frame rate */

}

static GstPadLinkReturn
gst_synaesthesia_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));

  return GST_PAD_LINK_OK;
}

static void
gst_synaesthesia_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *bufin = GST_BUFFER (_data);
  GstSynaesthesia *synaesthesia;
  GstBuffer *bufout;
  guint32 samples_in;
  gint16 *data;
  gint i;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));

  GST_DEBUG ("Synaesthesia: chainfunc called");

  if (GST_IS_EVENT (bufin)) {
    GstEvent *event = GST_EVENT (bufin);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
        gint64 value = 0;

        gst_event_discont_get_value (event, GST_FORMAT_TIME, &value);
        synaesthesia->next_time = value;
      }
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  samples_in = GST_BUFFER_SIZE (bufin) / sizeof (gint16);

  GST_DEBUG ("input buffer has %d samples", samples_in);

  /* FIXME: should really select the first 1024 samples after the timestamp. */
  if (GST_BUFFER_TIMESTAMP (bufin) < synaesthesia->next_time
      || samples_in < 1024) {
    GST_DEBUG ("timestamp is %" G_GUINT64_FORMAT ": want >= %" G_GUINT64_FORMAT,
        GST_BUFFER_TIMESTAMP (bufin), synaesthesia->next_time);
    gst_buffer_unref (bufin);
    return;
  }

  data = (gint16 *) GST_BUFFER_DATA (bufin);
  for (i = 0; i < 512; i++) {
    synaesthesia->datain[0][i] = *data++;
    synaesthesia->datain[1][i] = *data++;
  }

  if (synaesthesia->first_buffer) {
    synaesthesia_init (synaesthesia->width, synaesthesia->height);

    synaesthesia->first_buffer = FALSE;
  }

  bufout = gst_buffer_new ();
  GST_BUFFER_SIZE (bufout) = synaesthesia->width * synaesthesia->height * 4;
  GST_BUFFER_DATA (bufout) =
      (guchar *) synaesthesia_update (synaesthesia->datain);
  GST_BUFFER_TIMESTAMP (bufout) = synaesthesia->next_time;
  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_DONTFREE);

  synaesthesia->next_time += GST_SECOND / synaesthesia->fps;

  gst_pad_push (synaesthesia->srcpad, GST_DATA (bufout));

  gst_buffer_unref (bufin);

  GST_DEBUG ("Synaesthesia: exiting chainfunc");

}

static void
gst_synaesthesia_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSynaesthesia *synaesthesia;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SYNAESTHESIA (object));
  synaesthesia = GST_SYNAESTHESIA (object);

  switch (prop_id) {
    case ARG_WIDTH:
      synaesthesia->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      synaesthesia->height = g_value_get_int (value);
      break;
    case ARG_FPS:
      synaesthesia->fps = g_value_get_float (value);
      break;
    default:
      break;
  }
}

static void
gst_synaesthesia_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSynaesthesia *synaesthesia;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SYNAESTHESIA (object));
  synaesthesia = GST_SYNAESTHESIA (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, synaesthesia->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, synaesthesia->height);
      break;
    case ARG_FPS:
      g_value_set_float (value, synaesthesia->fps);
      break;
    default:
      break;
  }
}

static GstElementStateReturn
gst_synaesthesia_change_state (GstElement * element)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      synaesthesia->next_time = 0;
      synaesthesia->first_buffer = TRUE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "synaesthesia", GST_RANK_NONE,
      GST_TYPE_SYNAESTHESIA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "synaesthesia",
    "Creates video visualizations of audio input, using stereo and pitch information",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
