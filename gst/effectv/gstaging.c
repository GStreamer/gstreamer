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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include "gsteffectv.h"

#define GST_TYPE_AGINGTV \
  (gst_agingtv_get_type())
#define GST_AGINGTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AGINGTV,GstAgingTV))
#define GST_AGINGTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstAgingTV))
#define GST_IS_AGINGTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AGINGTV))
#define GST_IS_AGINGTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AGINGTV))

typedef struct _GstAgingTV GstAgingTV;
typedef struct _GstAgingTVClass GstAgingTVClass;

struct _GstAgingTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint map_width, map_height;
  guint32 *map;
  gint video_width_margin;
};

struct _GstAgingTVClass
{
  GstElementClass parent_class;
};

GstElementDetails gst_agingtv_details = {
  "AgingTV",
  "Filter/Effect",
  "Aply edge detect on video",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2002",
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
};

static void gst_agingtv_class_init (GstAgingTVClass * klass);
static void gst_agingtv_init (GstAgingTV * filter);

static void gst_agingtv_set_property (GObject * object, guint prop_id,
					   const GValue * value, GParamSpec * pspec);
static void gst_agingtv_get_property (GObject * object, guint prop_id,
					   GValue * value, GParamSpec * pspec);

static void gst_agingtv_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

GType gst_agingtv_get_type (void)
{
  static GType agingtv_type = 0;

  if (!agingtv_type) {
    static const GTypeInfo agingtv_info = {
      sizeof (GstAgingTVClass), NULL,
      NULL,
      (GClassInitFunc) gst_agingtv_class_init,
      NULL,
      NULL,
      sizeof (GstAgingTV),
      0,
      (GInstanceInitFunc) gst_agingtv_init,
    };

    agingtv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAgingTV", &agingtv_info, 0);
  }
  return agingtv_type;
}

static void
gst_agingtv_class_init (GstAgingTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_agingtv_set_property;
  gobject_class->get_property = gst_agingtv_get_property;
}

static GstPadConnectReturn
gst_agingtv_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstAgingTV *filter;

  filter = GST_AGINGTV (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  filter->map_width = filter->width / 4;
  filter->map_height = filter->height / 4;
  filter->video_width_margin = filter->width - filter->map_width * 4;

  g_free (filter->map);
  filter->map = (guint32 *)g_malloc (filter->map_width * filter->map_height * sizeof(guint32) * 2);
  bzero(filter->map, filter->map_width * filter->map_height * sizeof(guint32) * 2);

  if (gst_pad_try_set_caps (filter->srcpad, caps)) {
    return GST_PAD_CONNECT_OK;
  }

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_agingtv_init (GstAgingTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_effectv_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_agingtv_chain);
  gst_pad_set_connect_function (filter->sinkpad, gst_agingtv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_effectv_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->map = NULL;
}

static void
gst_agingtv_chain (GstPad * pad, GstBuffer * buf)
{
  GstAgingTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;

  filter = GST_AGINGTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = (filter->width * filter->height * 4);
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  
  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, outbuf);
}

static void
gst_agingtv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAgingTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGINGTV (object));

  filter = GST_AGINGTV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_agingtv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAgingTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGINGTV (object));

  filter = GST_AGINGTV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
