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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#define GST_TYPE_VIDEORATE \
  (gst_videorate_get_type())
#define GST_VIDEORATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEORATE,GstVideorate))
#define GST_VIDEORATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEORATE,GstVideorate))
#define GST_IS_VIDEORATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEORATE))
#define GST_IS_VIDEORATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEORATE))

typedef struct _GstVideorate GstVideorate;
typedef struct _GstVideorateClass GstVideorateClass;

struct _GstVideorate
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* video state */
  gdouble from_fps, to_fps;
  guint64 next_ts;
  GstBuffer *prevbuf;
  guint64 in, out, dup, drop;
};

struct _GstVideorateClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails videorate_details =
GST_ELEMENT_DETAILS ("Video rate adjuster",
    "Filter/Effect/Video",
    "Drops/duplicates/adjusts timestamps on video frames to make a perfect stream",
    "Wim Taymans <wim@fluendo.com>");

/* GstVideorate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_IN,
  ARG_OUT,
  ARG_DUP,
  ARG_DROP,
  /* FILL ME */
};

static GstStaticPadTemplate gst_videorate_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ YUY2, I420, YV12, YUYV, UYVY }")
    )
    );

static GstStaticPadTemplate gst_videorate_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ YUY2, I420, YV12, YUYV, UYVY }")
    )
    );

static void gst_videorate_base_init (gpointer g_class);
static void gst_videorate_class_init (GstVideorateClass * klass);
static void gst_videorate_init (GstVideorate * videorate);
static void gst_videorate_chain (GstPad * pad, GstData * _data);

static void gst_videorate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_videorate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_videorate_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_videorate_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_videorate_get_type (void)
{
  static GType videorate_type = 0;

  if (!videorate_type) {
    static const GTypeInfo videorate_info = {
      sizeof (GstVideorateClass),
      gst_videorate_base_init,
      NULL,
      (GClassInitFunc) gst_videorate_class_init,
      NULL,
      NULL,
      sizeof (GstVideorate),
      0,
      (GInstanceInitFunc) gst_videorate_init,
    };

    videorate_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstVideorate", &videorate_info, 0);
  }

  return videorate_type;
}

static void
gst_videorate_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videorate_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videorate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videorate_src_template));
}
static void
gst_videorate_class_init (GstVideorateClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (object_class, ARG_IN,
      g_param_spec_uint64 ("in", "In",
          "Number of input frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_OUT,
      g_param_spec_uint64 ("out", "Out",
          "Number of output frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_DUP,
      g_param_spec_uint64 ("duplicate", "Duplicate",
          "Number of duplicated frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_DROP,
      g_param_spec_uint64 ("drop", "Drop",
          "Number of dropped frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));

  object_class->set_property = gst_videorate_set_property;
  object_class->get_property = gst_videorate_get_property;

  element_class->change_state = gst_videorate_change_state;
}

static GstCaps *
gst_videorate_getcaps (GstPad * pad)
{
  GstVideorate *videorate;
  GstPad *otherpad;
  GstCaps *caps, *copy, *copy2 = NULL;
  int i;
  gdouble otherfps;
  GstStructure *structure;
  gboolean negotiated;

  videorate = GST_VIDEORATE (gst_pad_get_parent (pad));

  otherpad = (pad == videorate->srcpad) ? videorate->sinkpad :
      videorate->srcpad;
  negotiated = gst_pad_is_negotiated (otherpad);
  otherfps = (pad == videorate->srcpad) ? videorate->from_fps :
      videorate->to_fps;

  caps = gst_pad_get_allowed_caps (otherpad);
  copy = gst_caps_copy (caps);
  if (negotiated) {
    copy2 = gst_caps_copy (caps);
  }
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
  }
  if ((negotiated)) {
    for (i = 0; i < gst_caps_get_size (copy2); i++) {
      structure = gst_caps_get_structure (copy2, i);

      gst_structure_set (structure, "framerate", G_TYPE_DOUBLE, otherfps, NULL);
    }
    gst_caps_append (copy2, copy);
    copy = copy2;
  }
  gst_caps_append (copy, caps);

  return copy;
}

static GstPadLinkReturn
gst_videorate_link (GstPad * pad, const GstCaps * caps)
{
  GstVideorate *videorate;
  GstStructure *structure;
  gboolean ret;
  double fps;
  GstPad *otherpad;

  videorate = GST_VIDEORATE (gst_pad_get_parent (pad));

  otherpad = (pad == videorate->srcpad) ? videorate->sinkpad :
      videorate->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_double (structure, "framerate", &fps);
  if (!ret)
    return GST_PAD_LINK_REFUSED;

  if (pad == videorate->srcpad) {
    videorate->to_fps = fps;
  } else {
    videorate->from_fps = fps;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    /* 
     * Ensure that the other side talks the format we're trying to set
     */
    GstCaps *newcaps = gst_caps_copy (caps);

    if (pad == videorate->srcpad) {
      gst_caps_set_simple (newcaps,
          "framerate", G_TYPE_DOUBLE, videorate->from_fps, NULL);
    } else {
      gst_caps_set_simple (newcaps,
          "framerate", G_TYPE_DOUBLE, videorate->to_fps, NULL);
    }
    ret = gst_pad_try_set_caps (otherpad, newcaps);
    if (GST_PAD_LINK_FAILED (ret)) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  return GST_PAD_LINK_OK;
}

static void
gst_videorate_init (GstVideorate * videorate)
{
  GST_FLAG_SET (videorate, GST_ELEMENT_EVENT_AWARE);

  GST_DEBUG ("gst_videorate_init");
  videorate->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_videorate_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (videorate), videorate->sinkpad);
  gst_pad_set_chain_function (videorate->sinkpad, gst_videorate_chain);
  gst_pad_set_getcaps_function (videorate->sinkpad, gst_videorate_getcaps);
  gst_pad_set_link_function (videorate->sinkpad, gst_videorate_link);

  videorate->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_videorate_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (videorate), videorate->srcpad);
  gst_pad_set_getcaps_function (videorate->srcpad, gst_videorate_getcaps);
  gst_pad_set_link_function (videorate->srcpad, gst_videorate_link);

  videorate->prevbuf = NULL;
  videorate->in = 0;
  videorate->out = 0;
  videorate->drop = 0;
  videorate->dup = 0;
}

static void
gst_videorate_chain (GstPad * pad, GstData * data)
{
  GstVideorate *videorate = GST_VIDEORATE (gst_pad_get_parent (pad));
  GstBuffer *buf;

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    gst_pad_event_default (pad, event);
    return;
  }

  buf = GST_BUFFER (data);

  /* pull in 2 buffers */
  if (videorate->prevbuf == NULL) {
    videorate->prevbuf = buf;
  } else {
    GstClockTime prevtime, intime;
    gint count = 0;
    gint64 diff1, diff2;

    prevtime = GST_BUFFER_TIMESTAMP (videorate->prevbuf);
    intime = GST_BUFFER_TIMESTAMP (buf);

    videorate->in++;

    /* got 2 buffers, see which one is the best */
    do {
      diff1 = abs (prevtime - videorate->next_ts);
      diff2 = abs (intime - videorate->next_ts);

      /* output first one when its the best */
      if (diff1 <= diff2) {
        GstBuffer *outbuf;

        count++;
        outbuf =
            gst_buffer_create_sub (videorate->prevbuf, 0,
            GST_BUFFER_SIZE (videorate->prevbuf));
        GST_BUFFER_TIMESTAMP (outbuf) = videorate->next_ts;
        videorate->out++;
        videorate->next_ts = videorate->out / videorate->to_fps * GST_SECOND;
        GST_BUFFER_DURATION (outbuf) =
            videorate->next_ts - GST_BUFFER_TIMESTAMP (outbuf);
        gst_pad_push (videorate->srcpad, GST_DATA (outbuf));
      }
      /* continue while the first one was the best */
    }
    while (diff1 <= diff2);

    /* if we outputed the first buffer more then once, we have dups */
    if (count > 1) {
      videorate->dup += count - 1;
      g_object_notify (G_OBJECT (videorate), "duplicate");
    }
    /* if we didn't output the first buffer, we have a drop */
    else if (count == 0) {
      videorate->drop++;
      g_object_notify (G_OBJECT (videorate), "drop");
    }
//    g_print ("swap: diff1 %lld, diff2 %lld, in %d, out %d, drop %d, dup %d\n", diff1, diff2, 
//                  videorate->in, videorate->out, videorate->drop, videorate->dup);

    /* swap in new one when it's the best */
    gst_buffer_unref (videorate->prevbuf);
    videorate->prevbuf = buf;
  }
}

static void
gst_videorate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  //GstVideorate *videorate = GST_VIDEORATE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videorate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideorate *videorate = GST_VIDEORATE (object);

  switch (prop_id) {
    case ARG_IN:
      g_value_set_uint64 (value, videorate->in);
      break;
    case ARG_OUT:
      g_value_set_uint64 (value, videorate->out);
      break;
    case ARG_DUP:
      g_value_set_uint64 (value, videorate->dup);
      break;
    case ARG_DROP:
      g_value_set_uint64 (value, videorate->drop);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_videorate_change_state (GstElement * element)
{
  //GstVideorate *videorate = GST_VIDEORATE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videorate", GST_RANK_NONE,
      GST_TYPE_VIDEORATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videorate",
    "Adjusts video frames",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
