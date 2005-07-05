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

#include <string.h>

#define GST_TYPE_VIDEO_CROP \
  (gst_video_crop_get_type())
#define GST_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_CROP,GstVideoCrop))
#define GST_VIDEO_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_CROP,GstVideoCropClass))
#define GST_IS_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_CROP))
#define GST_IS_VIDEO_CROP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_CROP))

typedef struct _GstVideoCrop GstVideoCrop;
typedef struct _GstVideoCropClass GstVideoCropClass;

struct _GstVideoCrop
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* caps */
  gint width, height;
  gint crop_left, crop_right, crop_top, crop_bottom;
  gboolean renegotiate_src_caps;
};

struct _GstVideoCropClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_video_crop_details =
GST_ELEMENT_DETAILS ("video crop filter",
    "Filter/Effect/Video",
    "Crops video into a user defined region",
    "Wim Taymans <wim.taymans@chello.be>");


/* VideoCrop args */
enum
{
  ARG_0,
  ARG_LEFT,
  ARG_RIGHT,
  ARG_TOP,
  ARG_BOTTOM
      /* FILL ME */
};

static GstStaticPadTemplate gst_video_crop_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_video_crop_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


static void gst_video_crop_base_init (gpointer g_class);
static void gst_video_crop_class_init (GstVideoCropClass * klass);
static void gst_video_crop_init (GstVideoCrop * video_crop);

static void gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_video_crop_getcaps (GstPad * pad);

static GstPadLinkReturn
gst_video_crop_link (GstPad * pad, const GstCaps * caps);
static void gst_video_crop_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn gst_video_crop_change_state (GstElement * element);


static GstElementClass *parent_class = NULL;

/* static guint gst_video_crop_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_video_crop_get_type (void)
{
  static GType video_crop_type = 0;

  if (!video_crop_type) {
    static const GTypeInfo video_crop_info = {
      sizeof (GstVideoCropClass),
      gst_video_crop_base_init,
      NULL,
      (GClassInitFunc) gst_video_crop_class_init,
      NULL,
      NULL,
      sizeof (GstVideoCrop),
      0,
      (GInstanceInitFunc) gst_video_crop_init,
    };

    video_crop_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVideoCrop",
        &video_crop_info, 0);
  }
  return video_crop_type;
}

static void
gst_video_crop_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_video_crop_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_crop_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_crop_src_template));
}
static void
gst_video_crop_class_init (GstVideoCropClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEFT,
      g_param_spec_int ("left", "Left", "Pixels to crop at left",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RIGHT,
      g_param_spec_int ("right", "Right", "Pixels to crop at right",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOP,
      g_param_spec_int ("top", "Top", "Pixels to crop at top",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BOTTOM,
      g_param_spec_int ("bottom", "Bottom", "Pixels to crop at bottom",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_video_crop_set_property;
  gobject_class->get_property = gst_video_crop_get_property;

  gstelement_class->change_state = gst_video_crop_change_state;
}

static void
gst_video_crop_init (GstVideoCrop * video_crop)
{
  /* create the sink and src pads */
  video_crop->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_video_crop_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (video_crop), video_crop->sinkpad);
  gst_pad_set_chain_function (video_crop->sinkpad, gst_video_crop_chain);
  gst_pad_set_getcaps_function (video_crop->sinkpad, gst_video_crop_getcaps);
  gst_pad_set_link_function (video_crop->sinkpad, gst_video_crop_link);

  video_crop->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_video_crop_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (video_crop), video_crop->srcpad);
  gst_pad_set_getcaps_function (video_crop->srcpad, gst_video_crop_getcaps);
  gst_pad_set_link_function (video_crop->srcpad, gst_video_crop_link);

  video_crop->crop_right = 0;
  video_crop->crop_left = 0;
  video_crop->crop_top = 0;
  video_crop->crop_bottom = 0;
}

/* do we need this function? */
static void
gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoCrop *video_crop;

  g_return_if_fail (GST_IS_VIDEO_CROP (object));

  video_crop = GST_VIDEO_CROP (object);

  switch (prop_id) {
    case ARG_LEFT:
      video_crop->crop_left = g_value_get_int (value);
      break;
    case ARG_RIGHT:
      video_crop->crop_right = g_value_get_int (value);
      break;
    case ARG_TOP:
      video_crop->crop_top = g_value_get_int (value);
      break;
    case ARG_BOTTOM:
      video_crop->crop_bottom = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_video_crop_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoCrop *video_crop;

  g_return_if_fail (GST_IS_VIDEO_CROP (object));

  video_crop = GST_VIDEO_CROP (object);

  switch (prop_id) {
    case ARG_LEFT:
      g_value_set_int (value, video_crop->crop_left);
      break;
    case ARG_RIGHT:
      g_value_set_int (value, video_crop->crop_right);
      break;
    case ARG_TOP:
      g_value_set_int (value, video_crop->crop_top);
      break;
    case ARG_BOTTOM:
      g_value_set_int (value, video_crop->crop_bottom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (gst_pad_is_negotiated (video_crop->srcpad))
    video_crop->renegotiate_src_caps = TRUE;
}

static void
gst_video_crop_add_to_struct_val (GstStructure * s, const gchar * field_name,
    gint addval)
{
  const GValue *val;

  val = gst_structure_get_value (s, field_name);

  if (G_VALUE_HOLDS_INT (val)) {
    gint ival = g_value_get_int (val);

    gst_structure_set (s, field_name, G_TYPE_INT, ival + addval, NULL);
    return;
  }

  if (GST_VALUE_HOLDS_INT_RANGE (val)) {
    gint min = gst_value_get_int_range_min (val);
    gint max = gst_value_get_int_range_max (val);

    gst_structure_set (s, field_name, GST_TYPE_INT_RANGE, min + addval,
        max + addval, NULL);
    return;
  }

  if (GST_VALUE_HOLDS_LIST (val)) {
    GValue newlist = { 0, };
    gint i;

    g_value_init (&newlist, GST_TYPE_LIST);
    for (i = 0; i < gst_value_list_get_size (val); ++i) {
      GValue newval = { 0, };
      g_value_init (&newval, G_VALUE_TYPE (val));
      g_value_copy (val, &newval);
      if (G_VALUE_HOLDS_INT (val)) {
        gint ival = g_value_get_int (val);

        g_value_set_int (&newval, ival + addval);
      } else if (GST_VALUE_HOLDS_INT_RANGE (val)) {
        gint min = gst_value_get_int_range_min (val);
        gint max = gst_value_get_int_range_max (val);

        gst_value_set_int_range (&newval, min + addval, max + addval);
      } else {
        g_return_if_reached ();
      }
      gst_value_list_append_value (&newlist, &newval);
      g_value_unset (&newval);
    }
    gst_structure_set_value (s, field_name, &newlist);
    g_value_unset (&newlist);
    return;
  }

  g_return_if_reached ();
}

static GstCaps *
gst_video_crop_getcaps (GstPad * pad)
{
  GstVideoCrop *vc;
  GstCaps *othercaps, *caps;
  GstPad *otherpad;
  gint i, delta_w, delta_h;

  vc = GST_VIDEO_CROP (gst_pad_get_parent (pad));
  otherpad = (pad == vc->srcpad) ? vc->sinkpad : vc->srcpad;
  othercaps = gst_pad_get_allowed_caps (otherpad);

  GST_DEBUG_OBJECT (pad, "othercaps of otherpad %s:%s are: %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (otherpad), othercaps);

  if (pad == vc->srcpad) {
    delta_w = 0 - vc->crop_left - vc->crop_right;
    delta_h = 0 - vc->crop_top - vc->crop_bottom;
  } else {
    delta_w = vc->crop_left + vc->crop_right;
    delta_h = vc->crop_top + vc->crop_bottom;
  }

  for (i = 0; i < gst_caps_get_size (othercaps); i++) {
    GstStructure *s = gst_caps_get_structure (othercaps, i);

    gst_video_crop_add_to_struct_val (s, "width", delta_w);
    gst_video_crop_add_to_struct_val (s, "height", delta_h);
  }

  caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
  gst_caps_free (othercaps);

  GST_DEBUG_OBJECT (pad, "returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static GstPadLinkReturn
gst_video_crop_link (GstPad * pad, const GstCaps * caps)
{
  GstPadLinkReturn ret;
  GstStructure *structure;
  GstVideoCrop *vc;
  GstCaps *newcaps;
  GstPad *otherpad;
  gint w, h, other_w, other_h;

  vc = GST_VIDEO_CROP (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &w)
      || !gst_structure_get_int (structure, "height", &h))
    return GST_PAD_LINK_DELAYED;

  if (pad == vc->srcpad) {
    other_w = w + vc->crop_left + vc->crop_right;
    other_h = h + vc->crop_top + vc->crop_bottom;
    otherpad = vc->sinkpad;
    vc->width = other_w;
    vc->height = other_h;
  } else {
    other_w = w - vc->crop_left - vc->crop_right;
    other_h = h - vc->crop_top - vc->crop_bottom;
    vc->width = w;
    vc->height = h;
    otherpad = vc->srcpad;
  }

  newcaps = gst_caps_copy (caps);

  gst_caps_set_simple (newcaps,
      "width", G_TYPE_INT, other_w, "height", G_TYPE_INT, other_h, NULL);

  ret = gst_pad_try_set_caps (otherpad, newcaps);
  gst_caps_free (newcaps);

  if (ret == GST_PAD_LINK_REFUSED)
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

/* these macros are adapted from videotestsrc.c, paint_setup_I420() */
#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h) (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*ROUND_UP_2(h)/2))

static void
gst_video_crop_i420 (GstVideoCrop * video_crop, GstBuffer * src_buffer,
    GstBuffer * dest_buffer)
{
  guint8 *src;
  guint8 *dest;
  guint8 *srcY, *srcU, *srcV;
  guint8 *destY, *destU, *destV;
  gint out_width = video_crop->width -
      (video_crop->crop_left + video_crop->crop_right);
  gint out_height = video_crop->height -
      (video_crop->crop_top + video_crop->crop_bottom);
  gint j;

  src = GST_BUFFER_DATA (src_buffer);
  dest = GST_BUFFER_DATA (dest_buffer);

  srcY = src + GST_VIDEO_I420_Y_OFFSET (video_crop->width, video_crop->height);
  destY = dest + GST_VIDEO_I420_Y_OFFSET (out_width, out_height);

  /* copy Y plane first */
  srcY +=
      (GST_VIDEO_I420_Y_ROWSTRIDE (video_crop->width) * video_crop->crop_top) +
      video_crop->crop_left;
  for (j = 0; j < out_height; j++) {
    memcpy (destY, srcY, out_width);
    srcY += GST_VIDEO_I420_Y_ROWSTRIDE (video_crop->width);
    destY += GST_VIDEO_I420_Y_ROWSTRIDE (out_width);
  }

  destU = dest + GST_VIDEO_I420_U_OFFSET (out_width, out_height);
  destV = dest + GST_VIDEO_I420_V_OFFSET (out_width, out_height);

  srcU = src + GST_VIDEO_I420_U_OFFSET (video_crop->width, video_crop->height);
  srcV = src + GST_VIDEO_I420_V_OFFSET (video_crop->width, video_crop->height);

  srcU +=
      (GST_VIDEO_I420_U_ROWSTRIDE (video_crop->width) * (video_crop->crop_top /
          2)) + (video_crop->crop_left / 2);
  srcV +=
      (GST_VIDEO_I420_V_ROWSTRIDE (video_crop->width) * (video_crop->crop_top /
          2)) + (video_crop->crop_left / 2);

  for (j = 0; j < out_height / 2; j++) {
    /* copy U plane */
    memcpy (destU, srcU, out_width / 2);
    srcU += GST_VIDEO_I420_U_ROWSTRIDE (video_crop->width);
    destU += GST_VIDEO_I420_U_ROWSTRIDE (out_width);

    /* copy V plane */
    memcpy (destV, srcV, out_width / 2);
    srcV += GST_VIDEO_I420_V_ROWSTRIDE (video_crop->width);
    destV += GST_VIDEO_I420_V_ROWSTRIDE (out_width);
  }
}

static void
gst_video_crop_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buffer = GST_BUFFER (_data);
  GstVideoCrop *video_crop;
  GstBuffer *outbuf;
  gint new_width, new_height;

  video_crop = GST_VIDEO_CROP (gst_pad_get_parent (pad));

  new_width = video_crop->width -
      (video_crop->crop_left + video_crop->crop_right);
  new_height = video_crop->height -
      (video_crop->crop_top + video_crop->crop_bottom);

  if (video_crop->renegotiate_src_caps || !GST_PAD_CAPS (video_crop->srcpad)) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy (gst_pad_get_negotiated_caps (video_crop->sinkpad));

    gst_caps_set_simple (newcaps,
        "width", G_TYPE_INT, new_width, "height", G_TYPE_INT, new_height, NULL);

    if (GST_PAD_LINK_FAILED (gst_pad_try_set_caps (video_crop->srcpad,
                newcaps))) {
      GST_ELEMENT_ERROR (video_crop, CORE, NEGOTIATION, (NULL), (NULL));
      gst_caps_free (newcaps);
      return;
    }

    gst_caps_free (newcaps);

    video_crop->renegotiate_src_caps = FALSE;
  }

  /* passthrough if nothing to do */
  if (new_width == video_crop->width && new_height == video_crop->height) {
    gst_pad_push (video_crop->srcpad, GST_DATA (buffer));
    return;
  }

  g_return_if_fail (GST_BUFFER_SIZE (buffer) >=
      GST_VIDEO_I420_SIZE (video_crop->width, video_crop->height));

  outbuf = gst_pad_alloc_buffer (video_crop->srcpad, GST_BUFFER_OFFSET (buffer),
      GST_VIDEO_I420_SIZE (new_width, new_height));

  gst_buffer_stamp (outbuf, buffer);

  gst_video_crop_i420 (video_crop, buffer, outbuf);
  gst_buffer_unref (buffer);

  gst_pad_push (video_crop->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_video_crop_change_state (GstElement * element)
{
  GstVideoCrop *video_crop;

  video_crop = GST_VIDEO_CROP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      video_crop->renegotiate_src_caps = TRUE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (parent_class->change_state != NULL)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videocrop", GST_RANK_NONE,
      GST_TYPE_VIDEO_CROP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videocrop",
    "Crops video into a user defined region",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
