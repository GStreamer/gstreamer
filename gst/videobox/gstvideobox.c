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

#define GST_TYPE_VIDEO_BOX \
  (gst_video_box_get_type())
#define GST_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_BOX,GstVideoBox))
#define GST_VIDEO_BOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_BOX,GstVideoBoxClass))
#define GST_IS_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_BOX))
#define GST_IS_VIDEO_BOX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_BOX))

typedef struct _GstVideoBox GstVideoBox;
typedef struct _GstVideoBoxClass GstVideoBoxClass;

typedef enum
{
  VIDEO_BOX_FILL_BLACK,
  VIDEO_BOX_FILL_GREEN,
  VIDEO_BOX_FILL_BLUE,
}
GstVideoBoxFill;

struct _GstVideoBox
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* caps */
  gint in_width, in_height;
  gint out_width, out_height;

  gint box_left, box_right, box_top, box_bottom;

  gint border_left, border_right, border_top, border_bottom;
  gint crop_left, crop_right, crop_top, crop_bottom;

  gboolean use_alpha;
  gdouble alpha;
  gdouble border_alpha;

  GstVideoBoxFill fill_type;
};

struct _GstVideoBoxClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_video_box_details =
GST_ELEMENT_DETAILS ("video box filter",
    "Filter/Effect/Video",
    "Resizes a video by adding borders or cropping",
    "Wim Taymans <wim@fluendo.com>");


/* VideoBox signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LEFT      0
#define DEFAULT_RIGHT     0
#define DEFAULT_TOP       0
#define DEFAULT_BOTTOM    0
#define DEFAULT_FILL_TYPE VIDEO_BOX_FILL_BLACK
#define DEFAULT_ALPHA     1.0
#define DEFAULT_BORDER_ALPHA 1.0

enum
{
  ARG_0,
  ARG_LEFT,
  ARG_RIGHT,
  ARG_TOP,
  ARG_BOTTOM,
  ARG_FILL_TYPE,
  ARG_ALPHA,
  ARG_BORDER_ALPHA,
  /* FILL ME */
};

static GstStaticPadTemplate gst_video_box_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, AYUV }"))
    );

static GstStaticPadTemplate gst_video_box_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


static void gst_video_box_base_init (gpointer g_class);
static void gst_video_box_class_init (GstVideoBoxClass * klass);
static void gst_video_box_init (GstVideoBox * video_box);

static void gst_video_box_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_box_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_video_box_sink_link (GstPad * pad, const GstCaps * caps);
static void gst_video_box_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn gst_video_box_change_state (GstElement * element);


static GstElementClass *parent_class = NULL;

#define GST_TYPE_VIDEO_BOX_FILL (gst_video_box_fill_get_type())
static GType
gst_video_box_fill_get_type (void)
{
  static GType video_box_fill_type = 0;
  static GEnumValue video_box_fill[] = {
    {VIDEO_BOX_FILL_BLACK, "0", "Black"},
    {VIDEO_BOX_FILL_GREEN, "1", "Colorkey green"},
    {VIDEO_BOX_FILL_BLUE, "2", "Colorkey blue"},
    {0, NULL, NULL},
  };

  if (!video_box_fill_type) {
    video_box_fill_type =
        g_enum_register_static ("GstVideoBoxFill", video_box_fill);
  }
  return video_box_fill_type;
}

/* static guint gst_video_box_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_video_box_get_type (void)
{
  static GType video_box_type = 0;

  if (!video_box_type) {
    static const GTypeInfo video_box_info = {
      sizeof (GstVideoBoxClass),
      gst_video_box_base_init,
      NULL,
      (GClassInitFunc) gst_video_box_class_init,
      NULL,
      NULL,
      sizeof (GstVideoBox),
      0,
      (GInstanceInitFunc) gst_video_box_init,
    };

    video_box_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVideoBox",
        &video_box_info, 0);
  }
  return video_box_type;
}

static void
gst_video_box_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_video_box_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_box_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_box_src_template));
}
static void
gst_video_box_class_init (GstVideoBoxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILL_TYPE,
      g_param_spec_enum ("fill", "Fill", "How to fill the borders",
          GST_TYPE_VIDEO_BOX_FILL, DEFAULT_FILL_TYPE,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEFT,
      g_param_spec_int ("left", "Left", "Pixels to box at left",
          G_MININT, G_MAXINT, DEFAULT_LEFT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RIGHT,
      g_param_spec_int ("right", "Right", "Pixels to box at right",
          G_MININT, G_MAXINT, DEFAULT_RIGHT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOP,
      g_param_spec_int ("top", "Top", "Pixels to box at top",
          G_MININT, G_MAXINT, DEFAULT_TOP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BOTTOM,
      g_param_spec_int ("bottom", "Bottom", "Pixels to box at bottom",
          G_MININT, G_MAXINT, DEFAULT_BOTTOM, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha value picture",
          0.0, 1.0, DEFAULT_ALPHA, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BORDER_ALPHA,
      g_param_spec_double ("border_alpha", "Border Alpha",
          "Alpha value of the border", 0.0, 1.0, DEFAULT_BORDER_ALPHA,
          G_PARAM_READWRITE));

  gobject_class->set_property = gst_video_box_set_property;
  gobject_class->get_property = gst_video_box_get_property;

  gstelement_class->change_state = gst_video_box_change_state;
}

static void
gst_video_box_init (GstVideoBox * video_box)
{
  /* create the sink and src pads */
  video_box->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_video_box_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (video_box), video_box->sinkpad);
  gst_pad_set_chain_function (video_box->sinkpad, gst_video_box_chain);
  gst_pad_set_link_function (video_box->sinkpad, gst_video_box_sink_link);

  video_box->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_video_box_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (video_box), video_box->srcpad);

  video_box->box_right = DEFAULT_RIGHT;
  video_box->box_left = DEFAULT_LEFT;
  video_box->box_top = DEFAULT_TOP;
  video_box->box_bottom = DEFAULT_BOTTOM;
  video_box->fill_type = DEFAULT_FILL_TYPE;
  video_box->alpha = DEFAULT_ALPHA;
  video_box->border_alpha = DEFAULT_BORDER_ALPHA;

  GST_FLAG_SET (video_box, GST_ELEMENT_EVENT_AWARE);
}

/* do we need this function? */
static void
gst_video_box_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoBox *video_box;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEO_BOX (object));

  video_box = GST_VIDEO_BOX (object);

  switch (prop_id) {
    case ARG_LEFT:
      video_box->box_left = g_value_get_int (value);
      if (video_box->box_left < 0) {
        video_box->border_left = -video_box->box_left;
        video_box->crop_left = 0;
      } else {
        video_box->border_left = 0;
        video_box->crop_left = video_box->box_left;
      }
      break;
    case ARG_RIGHT:
      video_box->box_right = g_value_get_int (value);
      if (video_box->box_right < 0) {
        video_box->border_right = -video_box->box_right;
        video_box->crop_right = 0;
      } else {
        video_box->border_right = 0;
        video_box->crop_right = video_box->box_right;
      }
      break;
    case ARG_TOP:
      video_box->box_top = g_value_get_int (value);
      if (video_box->box_top < 0) {
        video_box->border_top = -video_box->box_top;
        video_box->crop_top = 0;
      } else {
        video_box->border_top = 0;
        video_box->crop_top = video_box->box_top;
      }
      break;
    case ARG_BOTTOM:
      video_box->box_bottom = g_value_get_int (value);
      if (video_box->box_bottom < 0) {
        video_box->border_bottom = -video_box->box_bottom;
        video_box->crop_bottom = 0;
      } else {
        video_box->border_bottom = 0;
        video_box->crop_bottom = video_box->box_bottom;
      }
      break;
    case ARG_FILL_TYPE:
      video_box->fill_type = g_value_get_enum (value);
      break;
    case ARG_ALPHA:
      video_box->alpha = g_value_get_double (value);
      break;
    case ARG_BORDER_ALPHA:
      video_box->border_alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_video_box_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoBox *video_box;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEO_BOX (object));

  video_box = GST_VIDEO_BOX (object);

  switch (prop_id) {
    case ARG_LEFT:
      g_value_set_int (value, video_box->box_left);
      break;
    case ARG_RIGHT:
      g_value_set_int (value, video_box->box_right);
      break;
    case ARG_TOP:
      g_value_set_int (value, video_box->box_top);
      break;
    case ARG_BOTTOM:
      g_value_set_int (value, video_box->box_bottom);
      break;
    case ARG_FILL_TYPE:
      g_value_set_enum (value, video_box->fill_type);
      break;
    case ARG_ALPHA:
      g_value_set_double (value, video_box->alpha);
      break;
    case ARG_BORDER_ALPHA:
      g_value_set_double (value, video_box->border_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPadLinkReturn
gst_video_box_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstVideoBox *video_box;
  GstStructure *structure;
  gboolean ret;

  video_box = GST_VIDEO_BOX (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", &video_box->in_width);
  ret &= gst_structure_get_int (structure, "height", &video_box->in_height);

  return GST_PAD_LINK_OK;
}

#define GST_VIDEO_I420_Y_OFFSET(width,height) (0)
#define GST_VIDEO_I420_U_OFFSET(width,height) ((width)*(height))
#define GST_VIDEO_I420_V_OFFSET(width,height) ((width)*(height) + ((width/2)*(height/2)))

#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (width)
#define GST_VIDEO_I420_U_ROWSTRIDE(width) ((width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((width)/2)

static int yuv_colors_Y[] = { 16, 150, 29 };
static int yuv_colors_U[] = { 128, 46, 255 };
static int yuv_colors_V[] = { 128, 21, 107 };

static void
gst_video_box_i420 (GstVideoBox * video_box, guint8 * src, guint8 * dest)
{
  guint8 *srcY, *srcU, *srcV;
  guint8 *destY, *destU, *destV;
  gint crop_width, crop_height;
  gint out_width, out_height;
  gint src_stride;
  gint br, bl, bt, bb;
  gint j;
  gint color1, color2;

  br = video_box->border_right;
  bl = video_box->border_left;
  bt = video_box->border_top;
  bb = video_box->border_bottom;

  out_width = video_box->out_width;
  out_height = video_box->out_height;

  destY = dest + GST_VIDEO_I420_Y_OFFSET (out_width, out_height);

  srcY =
      src + GST_VIDEO_I420_Y_OFFSET (video_box->in_width, video_box->in_height);
  src_stride = GST_VIDEO_I420_Y_ROWSTRIDE (video_box->in_width);

  crop_width =
      video_box->in_width - (video_box->crop_left + video_box->crop_right);
  crop_height =
      video_box->in_height - (video_box->crop_top + video_box->crop_bottom);

  srcY += src_stride * video_box->crop_top + video_box->crop_left;

  color1 = yuv_colors_Y[video_box->fill_type];

  /* copy Y plane first */
  for (j = 0; j < bt; j++) {
    memset (destY, color1, out_width);
    destY += out_width;
  }
  for (j = 0; j < crop_height; j++) {
    memset (destY, color1, bl);
    destY += bl;
    memcpy (destY, srcY, crop_width);
    destY += crop_width;
    memset (destY, color1, br);
    destY += br;
    srcY += src_stride;
  }
  for (j = 0; j < bb; j++) {
    memset (destY, color1, out_width);
    destY += out_width;
  }

  src_stride = GST_VIDEO_I420_U_ROWSTRIDE (video_box->in_width);

  destU = dest + GST_VIDEO_I420_U_OFFSET (out_width, out_height);
  destV = dest + GST_VIDEO_I420_V_OFFSET (out_width, out_height);

  crop_width /= 2;
  crop_height /= 2;
  out_width /= 2;
  out_height /= 2;
  bb /= 2;
  bt /= 2;
  br /= 2;
  bl /= 2;

  srcU =
      src + GST_VIDEO_I420_U_OFFSET (video_box->in_width, video_box->in_height);
  srcV =
      src + GST_VIDEO_I420_V_OFFSET (video_box->in_width, video_box->in_height);
  srcU += src_stride * (video_box->crop_top / 2) + (video_box->crop_left / 2);
  srcV += src_stride * (video_box->crop_top / 2) + (video_box->crop_left / 2);

  color1 = yuv_colors_U[video_box->fill_type];
  color2 = yuv_colors_V[video_box->fill_type];

  for (j = 0; j < bt; j++) {
    memset (destU, color1, out_width);
    memset (destV, color2, out_width);
    destU += out_width;
    destV += out_width;
  }
  for (j = 0; j < crop_height; j++) {
    memset (destU, color1, bl);
    destU += bl;
    /* copy U plane */
    memcpy (destU, srcU, crop_width);
    destU += crop_width;
    memset (destU, color1, br);
    destU += br;
    srcU += src_stride;

    memset (destV, color2, bl);
    destV += bl;
    /* copy V plane */
    memcpy (destV, srcV, crop_width);
    destV += crop_width;
    memset (destV, color2, br);
    destV += br;
    srcV += src_stride;
  }
  for (j = 0; j < bb; j++) {
    memset (destU, color1, out_width);
    memset (destV, color2, out_width);
    destU += out_width;
    destV += out_width;
  }
}

static void
gst_video_box_ayuv (GstVideoBox * video_box, guint8 * src, guint8 * dest)
{
  guint8 *srcY, *srcU, *srcV;
  gint crop_width, crop_width2, crop_height;
  gint out_width, out_height;
  gint src_stride, src_stride2;
  gint br, bl, bt, bb;
  gint colorY, colorU, colorV;
  gint i, j;
  guint8 b_alpha = (guint8) (video_box->border_alpha * 255);
  guint8 i_alpha = (guint8) (video_box->alpha * 255);
  guint32 *destp = (guint32 *) dest;
  guint32 ayuv;

  br = video_box->border_right;
  bl = video_box->border_left;
  bt = video_box->border_top;
  bb = video_box->border_bottom;

  out_width = video_box->out_width;
  out_height = video_box->out_height;

  src_stride = GST_VIDEO_I420_Y_ROWSTRIDE (video_box->in_width);
  src_stride2 = src_stride / 2;

  crop_width =
      video_box->in_width - (video_box->crop_left + video_box->crop_right);
  crop_width2 = crop_width / 2;
  crop_height =
      video_box->in_height - (video_box->crop_top + video_box->crop_bottom);

  srcY =
      src + GST_VIDEO_I420_Y_OFFSET (video_box->in_width, video_box->in_height);
  srcY += src_stride * video_box->crop_top + video_box->crop_left;
  srcU =
      src + GST_VIDEO_I420_U_OFFSET (video_box->in_width, video_box->in_height);
  srcU += src_stride2 * (video_box->crop_top / 2) + (video_box->crop_left / 2);
  srcV =
      src + GST_VIDEO_I420_V_OFFSET (video_box->in_width, video_box->in_height);
  srcV += src_stride2 * (video_box->crop_top / 2) + (video_box->crop_left / 2);

  colorY = yuv_colors_Y[video_box->fill_type];
  colorU = yuv_colors_U[video_box->fill_type];
  colorV = yuv_colors_V[video_box->fill_type];

  ayuv =
      GUINT32_FROM_BE ((b_alpha << 24) | (colorY << 16) | (colorU << 8) |
      colorV);

  /* top border */
  for (i = 0; i < bt; i++) {
    for (j = 0; j < out_width; j++) {
      *destp++ = ayuv;
    }
  }
  for (i = 0; i < crop_height; i++) {
    /* left border */
    for (j = 0; j < bl; j++) {
      *destp++ = ayuv;
    }
    dest = (guint8 *) destp;
    /* center */
    for (j = 0; j < crop_width2; j++) {
      *dest++ = i_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU;
      *dest++ = *srcV;
      *dest++ = i_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU++;
      *dest++ = *srcV++;
    }
    if (i % 2 == 0) {
      srcU -= crop_width2;
      srcV -= crop_width2;
    } else {
      srcU += src_stride2 - crop_width2;
      srcV += src_stride2 - crop_width2;
    }
    srcY += src_stride - crop_width;

    destp = (guint32 *) dest;
    /* right border */
    for (j = 0; j < br; j++) {
      *destp++ = ayuv;
    }
  }
  /* bottom border */
  for (i = 0; i < bb; i++) {
    for (j = 0; j < out_width; j++) {
      *destp++ = ayuv;
    }
  }
}

static void
gst_video_box_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buffer;
  GstVideoBox *video_box;
  GstBuffer *outbuf;
  gint new_width, new_height;

  video_box = GST_VIDEO_BOX (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (_data)) {
    GstEvent *event = GST_EVENT (_data);

    switch (GST_EVENT_TYPE (event)) {
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  buffer = GST_BUFFER (_data);

  new_width =
      video_box->in_width - (video_box->box_left + video_box->box_right);
  new_height =
      video_box->in_height - (video_box->box_top + video_box->box_bottom);

  if (new_width != video_box->out_width ||
      new_height != video_box->out_height ||
      !GST_PAD_CAPS (video_box->srcpad)) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy (gst_pad_get_negotiated_caps (video_box->sinkpad));

    video_box->use_alpha = TRUE;

    /* try AYUV first */
    gst_caps_set_simple (newcaps,
        "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("AYUV"),
        "width", G_TYPE_INT, new_width, "height", G_TYPE_INT, new_height, NULL);

    if (GST_PAD_LINK_FAILED (gst_pad_try_set_caps (video_box->srcpad, newcaps))) {
      video_box->use_alpha = FALSE;
      newcaps =
          gst_caps_copy (gst_pad_get_negotiated_caps (video_box->sinkpad));
      gst_caps_set_simple (newcaps, "format", GST_TYPE_FOURCC,
          GST_STR_FOURCC ("I420"), "width", G_TYPE_INT, new_width, "height",
          G_TYPE_INT, new_height, NULL);

      if (GST_PAD_LINK_FAILED (gst_pad_try_set_caps (video_box->srcpad,
                  newcaps))) {
        GST_ELEMENT_ERROR (video_box, CORE, NEGOTIATION, (NULL), (NULL));
        return;
      }
    }

    video_box->out_width = new_width;
    video_box->out_height = new_height;
  }

  if (video_box->use_alpha) {
    outbuf = gst_buffer_new_and_alloc (new_width * new_height * 4);

    gst_video_box_ayuv (video_box,
        GST_BUFFER_DATA (buffer), GST_BUFFER_DATA (outbuf));
  } else {
    outbuf = gst_buffer_new_and_alloc ((new_width * new_height * 3) / 2);

    gst_video_box_i420 (video_box,
        GST_BUFFER_DATA (buffer), GST_BUFFER_DATA (outbuf));
  }
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);


  gst_buffer_unref (buffer);

  gst_pad_push (video_box->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_video_box_change_state (GstElement * element)
{
  GstVideoBox *video_box;

  video_box = GST_VIDEO_BOX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
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

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videobox", GST_RANK_NONE,
      GST_TYPE_VIDEO_BOX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videobox",
    "resizes a video by adding borders or cropping",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
