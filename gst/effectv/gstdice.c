/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * dice.c: a 'dicing' effect
 *  copyright (c) 2001 Sam Mertens.  This code is subject to the provisions of
 *  the GNU Library Public License.
 *
 * I suppose this looks similar to PuzzleTV, but it's not. The screen is
 * divided into small squares, each of which is rotated either 0, 90, 180 or
 * 270 degrees.  The amount of rotation for each square is chosen at random.
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

/**
 * SECTION:element-dicetv
 *
 * DiceTV 'dices' the screen up into many small squares, each defaulting
 * to a size of 16 pixels by 16 pixels.. Each square is rotated randomly
 * in one of four directions: up (no change), down (180 degrees, or
 * upside down), right (90 degrees clockwise), or left (90 degrees
 * counterclockwise). The direction of each square normally remains
 * consistent between each frame.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! dicetv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of dicetv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstdice.h"
#include "gsteffectv.h"

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

#define DEFAULT_CUBE_BITS   4
#define MAX_CUBE_BITS       5
#define MIN_CUBE_BITS       0

typedef enum _dice_dir
{
  DICE_UP = 0,
  DICE_RIGHT = 1,
  DICE_DOWN = 2,
  DICE_LEFT = 3
} DiceDir;

GST_BOILERPLATE (GstDiceTV, gst_dicetv, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void gst_dicetv_create_map (GstDiceTV * filter);

static GstStaticPadTemplate gst_dicetv_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
    );

static GstStaticPadTemplate gst_dicetv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
    );

enum
{
  PROP_0,
  PROP_CUBE_BITS
};

static gboolean
gst_dicetv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDiceTV *filter = GST_DICETV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    g_free (filter->dicemap);
    filter->dicemap = (guint8 *) g_malloc (filter->height * filter->width);
    gst_dicetv_create_map (filter);
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static GstFlowReturn
gst_dicetv_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstDiceTV *filter = GST_DICETV (trans);
  guint32 *src, *dest;
  gint i, map_x, map_y, map_i, base, dx, dy, di;
  gint video_width, g_cube_bits, g_cube_size;
  gint g_map_height, g_map_width;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime timestamp, stream_time;
  const guint8 *dicemap;

  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (filter, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (filter), stream_time);

  GST_OBJECT_LOCK (filter);
  video_width = filter->width;
  g_cube_bits = filter->g_cube_bits;
  g_cube_size = filter->g_cube_size;
  g_map_height = filter->g_map_height;
  g_map_width = filter->g_map_width;

  dicemap = filter->dicemap;

  map_i = 0;
  for (map_y = 0; map_y < g_map_height; map_y++) {
    for (map_x = 0; map_x < g_map_width; map_x++) {
      base = (map_y << g_cube_bits) * video_width + (map_x << g_cube_bits);

      switch (dicemap[map_i]) {
        case DICE_UP:
          for (dy = 0; dy < g_cube_size; dy++) {
            i = base + dy * video_width;
            for (dx = 0; dx < g_cube_size; dx++) {
              dest[i] = src[i];
              i++;
            }
          }
          break;
        case DICE_LEFT:
          for (dy = 0; dy < g_cube_size; dy++) {
            i = base + dy * video_width;

            for (dx = 0; dx < g_cube_size; dx++) {
              di = base + (dx * video_width) + (g_cube_size - dy - 1);
              dest[di] = src[i];
              i++;
            }
          }
          break;
        case DICE_DOWN:
          for (dy = 0; dy < g_cube_size; dy++) {
            di = base + dy * video_width;
            i = base + (g_cube_size - dy - 1) * video_width + g_cube_size;
            for (dx = 0; dx < g_cube_size; dx++) {
              i--;
              dest[di] = src[i];
              di++;
            }
          }
          break;
        case DICE_RIGHT:
          for (dy = 0; dy < g_cube_size; dy++) {
            i = base + (dy * video_width);
            for (dx = 0; dx < g_cube_size; dx++) {
              di = base + dy + (g_cube_size - dx - 1) * video_width;
              dest[di] = src[i];
              i++;
            }
          }
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      map_i++;
    }
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static void
gst_dicetv_create_map (GstDiceTV * filter)
{
  gint x, y, i;

  if (filter->height <= 0 || filter->width <= 0)
    return;

  filter->g_map_height = filter->height >> filter->g_cube_bits;
  filter->g_map_width = filter->width >> filter->g_cube_bits;
  filter->g_cube_size = 1 << filter->g_cube_bits;

  i = 0;

  for (y = 0; y < filter->g_map_height; y++) {
    for (x = 0; x < filter->g_map_width; x++) {
      // dicemap[i] = ((i + y) & 0x3); /* Up, Down, Left or Right */
      filter->dicemap[i] = (fastrand () >> 24) & 0x03;
      i++;
    }
  }
}

static void
gst_dicetv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstDiceTV *filter = GST_DICETV (object);

  switch (prop_id) {
    case PROP_CUBE_BITS:
      GST_OBJECT_LOCK (filter);
      filter->g_cube_bits = g_value_get_int (value);
      gst_dicetv_create_map (filter);
      GST_OBJECT_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dicetv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDiceTV *filter = GST_DICETV (object);

  switch (prop_id) {
    case PROP_CUBE_BITS:
      g_value_set_int (value, filter->g_cube_bits);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dicetv_finalize (GObject * object)
{
  GstDiceTV *filter = GST_DICETV (object);

  g_free (filter->dicemap);
  filter->dicemap = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dicetv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "DiceTV effect",
      "Filter/Effect/Video",
      "'Dices' the screen up into many small squares",
      "Wim Taymans <wim.taymans@chello.be>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_dicetv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_dicetv_src_template);
}

static void
gst_dicetv_class_init (GstDiceTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_dicetv_set_property;
  gobject_class->get_property = gst_dicetv_get_property;
  gobject_class->finalize = gst_dicetv_finalize;

  g_object_class_install_property (gobject_class, PROP_CUBE_BITS,
      g_param_spec_int ("square-bits", "Square Bits", "The size of the Squares",
          MIN_CUBE_BITS, MAX_CUBE_BITS, DEFAULT_CUBE_BITS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_dicetv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_dicetv_transform);
}

static void
gst_dicetv_init (GstDiceTV * filter, GstDiceTVClass * klass)
{
  filter->dicemap = NULL;
  filter->g_cube_bits = DEFAULT_CUBE_BITS;
  filter->g_cube_size = 0;
  filter->g_map_height = 0;
  filter->g_map_width = 0;
}
