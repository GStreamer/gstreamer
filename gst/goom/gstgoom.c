/* gstgoom.c: implementation of goom drawing element
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
#include "goom_core.h"

#define GST_TYPE_GOOM (gst_goom_get_type())
#define GST_GOOM(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GOOM,GstGOOM))
#define GST_GOOM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GOOM,GstGOOM))
#define GST_IS_GOOM(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GOOM))
#define GST_IS_GOOM_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GOOM))

typedef struct _GstGOOM GstGOOM;
typedef struct _GstGOOMClass GstGOOMClass;

struct _GstGOOM {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* the timestamp of the next frame */
  guint64 next_time;
  gint16 datain[2][512];

  /* video state */
  gfloat fps;
  gint width;
  gint height;
  gint channels;
  gboolean srcnegotiated;
};

struct _GstGOOMClass {
  GstElementClass parent_class;
};

GType gst_goom_get_type(void);


/* elementfactory information */
static GstElementDetails gst_goom_details = {
  "GOOM: what a GOOM!",
  "Visualization",
  "Takes frames of data and outputs video frames using the GOOM filter",
  "Wim Taymans <wim.taymans@chello.be>"
};

/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new (
    "goomsrc",
    "video/x-raw-rgb",
    GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_32
  )
)

GST_PAD_TEMPLATE_FACTORY (sink_template,
  "sink",				/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,			/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "goomsink",				/* the name of the caps */
    "audio/x-raw-int",			/* the mime type of the caps */
       /* Properties follow: */
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (TRUE),
      "width",      GST_PROPS_INT (16),
      "depth",      GST_PROPS_INT (16),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_INT_RANGE (1, 2)
  )
)


static void		gst_goom_class_init	(GstGOOMClass *klass);
static void		gst_goom_base_init	(GstGOOMClass *klass);
static void		gst_goom_init		(GstGOOM *goom);
static void		gst_goom_dispose	(GObject *object);

static GstElementStateReturn
			gst_goom_change_state 	(GstElement *element);

static void		gst_goom_chain		(GstPad *pad, GstData *_data);

static GstPadLinkReturn gst_goom_sinkconnect 	(GstPad *pad, GstCaps *caps);
static GstPadLinkReturn gst_goom_srcconnect 	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;

GType
gst_goom_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstGOOMClass),      
      (GBaseInitFunc) gst_goom_base_init,      
      NULL,      
      (GClassInitFunc) gst_goom_class_init,
      NULL,
      NULL,
      sizeof (GstGOOM),
      0,
      (GInstanceInitFunc) gst_goom_init,
    };
    type = g_type_register_static (GST_TYPE_ELEMENT, "GstGOOM", &info, 0);
  }
  return type;
}

static void
gst_goom_base_init (GstGOOMClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &gst_goom_details);
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (sink_template));
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (src_template));
}

static void
gst_goom_class_init(GstGOOMClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose	= gst_goom_dispose;

  gstelement_class->change_state = gst_goom_change_state;
}

static void
gst_goom_init (GstGOOM *goom)
{
  /* create the sink and src pads */
  goom->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template ), "sink");
  goom->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_template ), "src");
  gst_element_add_pad (GST_ELEMENT (goom), goom->sinkpad);
  gst_element_add_pad (GST_ELEMENT (goom), goom->srcpad);

  GST_FLAG_SET (goom, GST_ELEMENT_EVENT_AWARE);

  gst_pad_set_chain_function (goom->sinkpad, gst_goom_chain);
  gst_pad_set_link_function (goom->sinkpad, gst_goom_sinkconnect);

  gst_pad_set_link_function (goom->srcpad, gst_goom_srcconnect);

  goom->width = 320;
  goom->height = 200;
  goom->fps = 25.; /* desired frame rate */
  goom->channels = 0;
  /* set to something */
  goom_init (50, 50);
}

static void
gst_goom_dispose (GObject *object)
{
  goom_close ();
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPadLinkReturn
gst_goom_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstGOOM *goom;
  goom = GST_GOOM (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_get_int (caps, "channels", &goom->channels);

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_goom_srcconnect (GstPad *pad, GstCaps *caps)
{
  GstGOOM *goom;
  goom = GST_GOOM (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  if (gst_caps_has_property_typed (caps, "width", GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "width", &goom->width);
  }
  if (gst_caps_has_property_typed (caps, "height", GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "height", &goom->height);
  }
  if (gst_caps_has_property_typed (caps, "framerate", GST_PROPS_FLOAT_TYPE)) {
    gst_caps_get_float (caps, "framerate", &goom->fps);
  }

  goom_set_resolution (goom->width, goom->height);
  goom->srcnegotiated = TRUE;

  return GST_PAD_LINK_OK;
}

static gboolean
gst_goom_negotiate_default (GstGOOM *goom)
{
  GstCaps *caps;

  caps = GST_CAPS_NEW (
	     "goomsrc",
	     "video/x-raw-rgb",
	       "format", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")), 
	       "bpp", 		GST_PROPS_INT (32), 
	       "depth", 	GST_PROPS_INT (32), 
	       "endianness", 	GST_PROPS_INT (G_BIG_ENDIAN),
	       "red_mask", 	GST_PROPS_INT (R_MASK_32), 
	       "green_mask", 	GST_PROPS_INT (G_MASK_32), 
	       "blue_mask", 	GST_PROPS_INT (B_MASK_32), 
	       "width", 	GST_PROPS_INT (goom->width), 
	       "height", 	GST_PROPS_INT (goom->height),
	       "framerate",	GST_PROPS_FLOAT (goom->fps)
	   );

  if (gst_pad_try_set_caps (goom->srcpad, caps) <= 0) {
    return FALSE;
  }

  goom_set_resolution (goom->width, goom->height);
  goom->srcnegotiated = TRUE;

  return TRUE;
}

static void
gst_goom_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *bufin = GST_BUFFER (_data);
  GstGOOM *goom;
  GstBuffer *bufout;
  guint32 samples_in;
  gint16 *data;
  gint i;

  goom = GST_GOOM (gst_pad_get_parent (pad));

  GST_DEBUG ("GOOM: chainfunc called");

  if (GST_IS_EVENT (bufin)) {
    GstEvent *event = GST_EVENT (bufin);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
	gint64 value = 0;

	gst_event_discont_get_value (event, GST_FORMAT_TIME, &value);

        goom->next_time = value;
      }
      default:
	gst_pad_event_default (pad, event);
	break;
    }
    return;
  }

  if (goom->channels == 0) {
    gst_element_error (GST_ELEMENT (goom), "sink format not negotiated");
    goto done;
  }

  if (!GST_PAD_IS_USABLE (goom->srcpad))
    goto done;

  if (!goom->srcnegotiated) {
    if (!gst_goom_negotiate_default (goom)) {
      gst_element_error (GST_ELEMENT (goom), "could not negotiate src format");
      goto done;
    }
  }

  samples_in = GST_BUFFER_SIZE (bufin) / (sizeof (gint16) * goom->channels);

  GST_DEBUG ("input buffer has %d samples", samples_in);

  if (GST_BUFFER_TIMESTAMP (bufin) < goom->next_time || samples_in < 512) {
    goto done;
  }

  data = (gint16 *) GST_BUFFER_DATA (bufin);
  if (goom->channels == 2) {
    for (i=0; i < 512; i++) {
      goom->datain[0][i] = *data++;
      goom->datain[1][i] = *data++;
    }
  }
  else {
    for (i=0; i < 512; i++) {
      goom->datain[0][i] = *data;
      goom->datain[1][i] = *data++;
    }
  }

  bufout = gst_buffer_new ();
  GST_BUFFER_SIZE (bufout) = goom->width * goom->height * 4;
  GST_BUFFER_DATA (bufout) = (guchar *) goom_update (goom->datain);
  GST_BUFFER_TIMESTAMP (bufout) = goom->next_time;
  GST_BUFFER_FLAG_SET (bufout, GST_BUFFER_DONTFREE);

  goom->next_time += GST_SECOND / goom->fps;

  gst_pad_push (goom->srcpad, GST_DATA (bufout));

done:
  gst_buffer_unref (bufin);

  GST_DEBUG ("GOOM: exiting chainfunc");
}

static GstElementStateReturn
gst_goom_change_state (GstElement *element)
{ 
  GstGOOM *goom = GST_GOOM (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break; 
    case GST_STATE_READY_TO_PAUSED:
      goom->next_time = 0;
      goom->srcnegotiated = FALSE;
      goom->channels = 0;
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "goom",
			       GST_RANK_NONE, GST_TYPE_GOOM);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "goom",
  "GOOM visualization filter",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
