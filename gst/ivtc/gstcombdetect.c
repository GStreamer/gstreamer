/* GStreamer
 * Copyright (C) 2013 Rdio, Inc. <ingestions@rdio.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstcombdetect
 *
 * The combdetect element detects if combing artifacts are present in
 * a raw video stream, and if so, marks them with an annoying and
 * highly visible color.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! combdetect ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "gstcombdetect.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_comb_detect_debug_category);
#define GST_CAT_DEFAULT gst_comb_detect_debug_category

/* prototypes */


static void gst_comb_detect_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_comb_detect_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_comb_detect_dispose (GObject * object);
static void gst_comb_detect_finalize (GObject * object);

static gboolean gst_comb_detect_start (GstBaseTransform * trans);
static gboolean gst_comb_detect_stop (GstBaseTransform * trans);
static gboolean gst_comb_detect_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_comb_detect_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * inframe, GstVideoFrame * outframe);

enum
{
  PROP_0
};

/* pad templates */

#define VIDEO_SINK_CAPS \
    GST_VIDEO_CAPS_MAKE("{ I420, Y444, Y42B }")

static GstStaticPadTemplate gst_comb_detect_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_SINK_CAPS)
    );

#define VIDEO_SRC_CAPS \
    GST_VIDEO_CAPS_MAKE("{ I420, Y444, Y42B }")

static GstStaticPadTemplate gst_comb_detect_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_SRC_CAPS)
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstCombDetect, gst_comb_detect, GST_TYPE_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_comb_detect_debug_category, "combdetect", 0,
        "debug category for combdetect element"));

static void
gst_comb_detect_class_init (GstCombDetectClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_comb_detect_sink_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_comb_detect_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Comb Detect", "Video/Filter", "Detect combing artifacts in video stream",
      "David Schleef <ds@schleef.org>");

  gobject_class->set_property = gst_comb_detect_set_property;
  gobject_class->get_property = gst_comb_detect_get_property;
  gobject_class->dispose = gst_comb_detect_dispose;
  gobject_class->finalize = gst_comb_detect_finalize;
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_comb_detect_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_comb_detect_stop);
  video_filter_class->set_info = GST_DEBUG_FUNCPTR (gst_comb_detect_set_info);
  video_filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_comb_detect_transform_frame);

}

static void
gst_comb_detect_init (GstCombDetect * combdetect)
{
  combdetect->sinkpad =
      gst_pad_new_from_static_template (&gst_comb_detect_sink_template, "sink");
  combdetect->srcpad =
      gst_pad_new_from_static_template (&gst_comb_detect_src_template, "src");
}

void
gst_comb_detect_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstCombDetect *combdetect = GST_COMB_DETECT (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_comb_detect_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstCombDetect *combdetect = GST_COMB_DETECT (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_comb_detect_dispose (GObject * object)
{
  /* GstCombDetect *combdetect = GST_COMB_DETECT (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_comb_detect_parent_class)->dispose (object);
}

void
gst_comb_detect_finalize (GObject * object)
{
  /* GstCombDetect *combdetect = GST_COMB_DETECT (object); */

  /* clean up object here */

  G_OBJECT_CLASS (gst_comb_detect_parent_class)->finalize (object);
}


static gboolean
gst_comb_detect_start (GstBaseTransform * trans)
{
  /* GstCombDetect *combdetect = GST_COMB_DETECT (trans); */

  /* initialize processing */
  return TRUE;
}

static gboolean
gst_comb_detect_stop (GstBaseTransform * trans)
{
  /* GstCombDetect *combdetect = GST_COMB_DETECT (trans); */

  /* finalize processing */
  return TRUE;
}

static gboolean
gst_comb_detect_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstCombDetect *combdetect = GST_COMB_DETECT (filter);

  memcpy (&combdetect->vinfo, in_info, sizeof (GstVideoInfo));

  return TRUE;
}

static GstFlowReturn
gst_comb_detect_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  int k;
  int height;
  int width;

#define GET_LINE(frame,comp,line) (((unsigned char *)(frame)->data[k]) + \
      (line) * GST_VIDEO_FRAME_COMP_STRIDE((frame), (comp)))

  for (k = 1; k < 3; k++) {
    int i;
    height = GST_VIDEO_FRAME_COMP_HEIGHT (outframe, k);
    width = GST_VIDEO_FRAME_COMP_WIDTH (outframe, k);
    for (i = 0; i < height; i++) {
      memcpy (GET_LINE (outframe, k, i), GET_LINE (inframe, k, i), width);
    }
  }

  {
    int j;
#define MAXWIDTH 2048
    int thisline[MAXWIDTH];
    int score = 0;

    height = GST_VIDEO_FRAME_COMP_HEIGHT (outframe, 0);
    width = GST_VIDEO_FRAME_COMP_WIDTH (outframe, 0);

    memset (thisline, 0, sizeof (thisline));

    k = 0;
    for (j = 0; j < height; j++) {
      int i;
      if (j < 2 || j >= height - 2) {
        guint8 *dest = GET_LINE (outframe, 0, j);
        guint8 *src = GET_LINE (inframe, 0, j);
        for (i = 0; i < width; i++) {
          dest[i] = src[i] / 2;
        }
      } else {
        guint8 *dest = GET_LINE (outframe, 0, j);
        guint8 *src1 = GET_LINE (inframe, 0, j - 1);
        guint8 *src2 = GET_LINE (inframe, 0, j);
        guint8 *src3 = GET_LINE (inframe, 0, j + 1);

        for (i = 0; i < width; i++) {
          if (src2[i] < MIN (src1[i], src3[i]) - 5 ||
              src2[i] > MAX (src1[i], src3[i]) + 5) {
            if (i > 0) {
              thisline[i] += thisline[i - 1];
            }
            thisline[i]++;
            if (thisline[i] > 1000)
              thisline[i] = 1000;
          } else {
            thisline[i] = 0;
          }
          if (thisline[i] > 100) {
            dest[i] = 255;
            score++;
          } else {
            dest[i] = src2[i] / 2;
          }
        }
      }
    }

    if (score > 10)
      GST_DEBUG ("score %d", score);
  }

  return GST_FLOW_OK;
}
