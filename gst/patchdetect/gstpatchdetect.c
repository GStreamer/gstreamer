/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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
 * SECTION:element-gstpatchdetect
 *
 * The patchdetect element detects color patches from a color
 * calibration chart.  Currently, the patches for the 24-square
 * Munsell ColorChecker are hard-coded into the element.  When
 * a color chart is detected in the video stream, a message is
 * sent to the bus containing the detected color values of each
 * of the patches.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v dv1394src ! dvdemux ! dvdec ! patchdetect ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <math.h>
#include <string.h>
#include "gstpatchdetect.h"

GST_DEBUG_CATEGORY_STATIC (gst_patchdetect_debug_category);
#define GST_CAT_DEFAULT gst_patchdetect_debug_category

/* prototypes */


static void gst_patchdetect_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_patchdetect_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_patchdetect_dispose (GObject * object);
static void gst_patchdetect_finalize (GObject * object);

static gboolean
gst_patchdetect_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size);
static gboolean
gst_patchdetect_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_patchdetect_start (GstBaseTransform * trans);
static gboolean gst_patchdetect_stop (GstBaseTransform * trans);
static gboolean gst_patchdetect_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_patchdetect_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_patchdetect_src_event (GstBaseTransform * trans,
    GstEvent * event);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_patchdetect_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_patchdetect_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_patchdetect_debug_category, "patchdetect", 0, \
      "debug category for patchdetect element");

GST_BOILERPLATE_FULL (GstPatchdetect, gst_patchdetect, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_patchdetect_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_patchdetect_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_patchdetect_src_template));

  gst_element_class_set_static_metadata (element_class, "Color Patch Detector",
      "Video/Analysis", "Detects color patches from a color calibration chart",
      "David Schleef <ds@entropywave.com>");
}

static void
gst_patchdetect_class_init (GstPatchdetectClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_patchdetect_set_property;
  gobject_class->get_property = gst_patchdetect_get_property;
  gobject_class->dispose = gst_patchdetect_dispose;
  gobject_class->finalize = gst_patchdetect_finalize;
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_patchdetect_get_unit_size);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_patchdetect_set_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_patchdetect_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_patchdetect_stop);
  base_transform_class->event = GST_DEBUG_FUNCPTR (gst_patchdetect_event);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_patchdetect_transform_ip);
  base_transform_class->src_event =
      GST_DEBUG_FUNCPTR (gst_patchdetect_src_event);

}

static void
gst_patchdetect_init (GstPatchdetect * patchdetect,
    GstPatchdetectClass * patchdetect_class)
{
}

void
gst_patchdetect_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PATCHDETECT (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_patchdetect_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PATCHDETECT (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_patchdetect_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_PATCHDETECT (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_patchdetect_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_PATCHDETECT (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_patchdetect_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  int width, height;
  GstVideoFormat format;
  gboolean ret;

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  *size = gst_video_format_get_size (format, width, height);

  return ret;
}

static gboolean
gst_patchdetect_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstPatchdetect *patchdetect = GST_PATCHDETECT (trans);
  int width, height;
  GstVideoFormat format;
  gboolean ret;

  ret = gst_video_format_parse_caps (incaps, &format, &width, &height);
  if (ret) {
    patchdetect->format = format;
    patchdetect->width = width;
    patchdetect->height = height;
  }

  return ret;
}

static gboolean
gst_patchdetect_start (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_patchdetect_stop (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_patchdetect_event (GstBaseTransform * trans, GstEvent * event)
{

  return TRUE;
}

typedef struct
{
  guint8 *y;
  int ystride;
  guint8 *u;
  int ustride;
  guint8 *v;
  int vstride;
  int width;
  int height;
  int t;
} Frame;

typedef struct
{
  int y, u, v;
  int diff_y, diff_u, diff_v;
  gboolean match;
  int patch_block;
  int color;
  int count;
  int sum_x;
  int sum_y;
} Stats;

typedef struct
{
  int r, g, b;
  int y, u, v;
} Color;

typedef struct
{
  int x, y;
  int patch1, patch2;
  gboolean valid;
} Point;

typedef struct
{
  int xmin, xmax;
  int ymin, ymax;
  int val;
  int y, u, v;
  int count;
  int cen_x, cen_y;
  gboolean valid;
} Patch;

static Color patch_colors[24] = {
  {115, 82, 68, 92, 119, 143},
  {194, 150, 130, 152, 115, 148},
  {98, 122, 157, 119, 146, 116},
  {87, 108, 67, 102, 112, 120},
  {133, 128, 177, 130, 149, 128},
  {103, 189, 170, 161, 128, 91},
  {214, 126, 44, 135, 83, 170},
  {80, 91, 166, 97, 162, 120},
  {193, 90, 99, 113, 122, 173},
  {94, 60, 108, 77, 146, 141},
  {157, 188, 64, 164, 77, 119},
  {224, 163, 46, 160, 70, 160},
  {56, 61, 150, 73, 168, 122},
  {70, 148, 73, 124, 103, 97},
  {175, 54, 60, 85, 118, 181},
  {231, 199, 31, 182, 51, 149},
  {187, 86, 149, 112, 146, 170},
  {8, 133, 161, 109, 153, 72},
  {243, 243, 243, 225, 128, 128},
  {200, 200, 200, 188, 128, 128},
  {160, 160, 160, 153, 128, 128},
  {122, 122, 122, 121, 128, 128},
  {85, 85, 85, 89, 128, 128},
  {52, 52, 52, 61, 128, 128}
};

static void
get_block_stats (Frame * frame, int x, int y, Stats * stats)
{
  int i, j;
  guint8 *data;
  int max;
  int min;
  int sum;

  max = 0;
  min = 255;
  sum = 0;
  for (j = 0; j < 8; j++) {
    data = frame->y + frame->ystride * (j + y) + x;
    for (i = 0; i < 8; i++) {
      max = MAX (max, data[i]);
      min = MIN (min, data[i]);
      sum += data[i];
    }
  }
  stats->y = sum / 64;
  stats->diff_y = MAX (max - stats->y, stats->y - min);

  max = 0;
  min = 255;
  sum = 0;
  for (j = 0; j < 4; j++) {
    data = frame->u + frame->ustride * (j + y / 2) + x / 2;
    for (i = 0; i < 4; i++) {
      max = MAX (max, data[i]);
      min = MIN (min, data[i]);
      sum += data[i];
    }
  }
  stats->u = sum / 16;
  stats->diff_u = MAX (max - stats->u, stats->u - min);

  max = 0;
  min = 255;
  sum = 0;
  for (j = 0; j < 4; j++) {
    data = frame->v + frame->vstride * (j + y / 2) + x / 2;
    for (i = 0; i < 4; i++) {
      max = MAX (max, data[i]);
      min = MIN (min, data[i]);
      sum += data[i];
    }
  }
  stats->v = sum / 16;
  stats->diff_v = MAX (max - stats->v, stats->v - min);

  stats->patch_block = -1;
  stats->match = FALSE;
#define MATCH 15
  if (stats->diff_y < MATCH && stats->diff_u < MATCH && stats->diff_v < MATCH) {
    stats->match = TRUE;
  }
}

static void
paint_block (Frame * frame, int x, int y, int value)
{
  int i, j;
  guint8 *data;

  for (j = 0; j < 8; j++) {
    data = frame->y + frame->ystride * (j + y) + x;
    for (i = 0; i < 8; i++) {
      data[i] = value;
    }
  }

  for (j = 0; j < 4; j++) {
    data = frame->u + frame->ustride * (j + y / 2) + x / 2;
    for (i = 0; i < 4; i++) {
      data[i] = 128;
    }
  }

  for (j = 0; j < 4; j++) {
    data = frame->v + frame->vstride * (j + y / 2) + x / 2;
    for (i = 0; i < 4; i++) {
      data[i] = 128;
    }
  }
}

static gboolean
patch_check (Frame * frame, guint8 * patchpix, int x, int y, int w, int h)
{
  int i, j;

  for (j = y; j < y + h; j++) {
    for (i = x; i < x + w; i++) {
      if (patchpix[j * frame->width + i] != 0)
        return FALSE;
    }
  }

  return TRUE;
}

static void
patch_start (Frame * frame, guint8 * patchpix, Patch * patch, int x, int y,
    int w, int h)
{
  int i, j;

  for (j = y; j < y + h; j++) {
    for (i = x; i < x + w; i++) {
      patchpix[j * frame->width + i] = patch->val;
    }
  }
  patch->xmin = MAX (1, x - 1);
  patch->xmax = MIN (x + w + 1, frame->width - 1);
  patch->ymin = MAX (1, y - 1);
  patch->ymax = MIN (y + h + 1, frame->height - 1);
  patch->count = w * h;

}

static void
patch_grow (Frame * frame, guint8 * patchpix, Patch * patch)
{
  gboolean growmore = FALSE;
  guint8 *ydata, *udata, *vdata;
  int i, j;
  int count = 5;

#define MAXDIFF 15
  do {
    for (j = patch->ymin; j < patch->ymax; j++) {
      ydata = frame->y + frame->ystride * j;
      udata = frame->u + frame->ustride * (j / 2);
      vdata = frame->v + frame->vstride * (j / 2);
      for (i = patch->xmin; i < patch->xmax; i++) {
        if (patchpix[j * frame->width + i] != 0)
          continue;

        if (patchpix[(j + 1) * frame->width + i] == patch->val ||
            patchpix[(j - 1) * frame->width + i] == patch->val ||
            patchpix[j * frame->width + i + 1] == patch->val ||
            patchpix[j * frame->width + i - 1] == patch->val) {
          int diff = ABS (ydata[i] - patch->y) +
              ABS (udata[i / 2] - patch->u) + ABS (vdata[i / 2] - patch->v);

          if (diff < MAXDIFF) {
            patchpix[j * frame->width + i] = patch->val;
            patch->xmin = MIN (patch->xmin, MAX (i - 1, 1));
            patch->xmax = MAX (patch->xmax, MIN (i + 2, frame->width - 1));
            patch->ymin = MIN (patch->ymin, MAX (j - 1, 1));
            patch->ymax = MAX (patch->ymax, MIN (j + 2, frame->height - 1));
            patch->count++;
            growmore = TRUE;
          }
        }
      }
    }
    for (j = patch->ymax - 1; j >= patch->ymin; j--) {
      ydata = frame->y + frame->ystride * j;
      udata = frame->u + frame->ustride * (j / 2);
      vdata = frame->v + frame->vstride * (j / 2);
      for (i = patch->xmax - 1; i >= patch->xmin; i--) {
        if (patchpix[j * frame->width + i] != 0)
          continue;

        if (patchpix[(j + 1) * frame->width + i] == patch->val ||
            patchpix[(j - 1) * frame->width + i] == patch->val ||
            patchpix[j * frame->width + i + 1] == patch->val ||
            patchpix[j * frame->width + i - 1] == patch->val) {
          int diff = ABS (ydata[i] - patch->y) +
              ABS (udata[i / 2] - patch->u) + ABS (vdata[i / 2] - patch->v);

          if (diff < MAXDIFF) {
            patchpix[j * frame->width + i] = patch->val;
            patch->xmin = MIN (patch->xmin, MAX (i - 1, 1));
            patch->xmax = MAX (patch->xmax, MIN (i + 2, frame->width - 1));
            patch->ymin = MIN (patch->ymin, MAX (j - 1, 1));
            patch->ymax = MAX (patch->ymax, MIN (j + 2, frame->height - 1));
            patch->count++;
            growmore = TRUE;
          }
        }
      }
    }

    count--;
  } while (growmore && count > 0);

#if 0
  for (j = patch->ymin; j < patch->ymax; j++) {
    guint8 *data;
    data = frame->y + frame->ystride * j;
    for (i = patch->xmin; i < patch->xmax; i++) {
      if (patchpix[j * frame->width + i] != patch->val)
        continue;
      if ((i + j + frame->t) & 0x4) {
        data[i] = 16;
      }
    }
  }
#endif

}

#if 0
static void
find_cluster (Point * points, int n_points, int *result_x, int *result_y)
{
  int dist;
  int ave_x, ave_y;
  int i;

  for (dist = 50; dist >= 10; dist -= 5) {
    int sum_x, sum_y;
    int n_valid;

    sum_x = 0;
    sum_y = 0;
    n_valid = 0;
    for (i = 0; i < n_points; i++) {
      if (!points[i].valid)
        continue;
      sum_x += points[i].x;
      sum_y += points[i].y;
      n_valid++;
    }
    ave_x = sum_x / n_valid;
    ave_y = sum_y / n_valid;

    for (i = 0; i < n_points; i++) {
      int d;
      if (!points[i].valid)
        continue;
      d = (points[i].x - ave_x) * (points[i].x - ave_x);
      d += (points[i].y - ave_y) * (points[i].y - ave_y);
      if (d > dist * dist)
        points[i].valid = FALSE;
    }
  }
  *result_x = ave_x;
  *result_y = ave_y;
}
#endif

typedef struct _Matrix Matrix;
struct _Matrix
{
  double m[4][4];
};

#if 0
static void
dump_4x4 (double a[4][4], double b[4][4])
{
  int j;
  int i;

  for (j = 0; j < 4; j++) {
    g_print ("[ ");
    for (i = 0; i < 4; i++) {
      g_print ("%8.2g", a[i][j]);
      if (i != 4 - 1)
        g_print (", ");
    }
    g_print ("|");
    for (i = 0; i < 4; i++) {
      g_print ("%8.2g", b[i][j]);
      if (i != 4 - 1)
        g_print (", ");
    }
    g_print ("]\n");
  }
  g_print ("\n");

}
#endif

static void
invert_matrix (double m[10][10], int n)
{
  int i, j, k;
  double tmp[10][10] = { {0} };
  double x;

  for (i = 0; i < n; i++) {
    tmp[i][i] = 1;
  }

  for (j = 0; j < n; j++) {
    for (k = 0; k < n; k++) {
      if (k == j)
        continue;

      x = m[j][k] / m[j][j];
      for (i = 0; i < n; i++) {
        m[i][k] -= x * m[i][j];
        tmp[i][k] -= x * tmp[i][j];
      }
    }

    x = m[j][j];
    for (i = 0; i < n; i++) {
      m[i][j] /= x;
      tmp[i][j] /= x;
    }
  }

  memcpy (m, tmp, sizeof (tmp));
}

static GstFlowReturn
gst_patchdetect_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstPatchdetect *patchdetect = GST_PATCHDETECT (trans);
  Frame frame;
  Point *points;
  int i, j;
  int blocks_x, blocks_y;
  int n_points;
  int n_patches;
  Patch *patches;
  guint8 *patchpix;
  int vec1_x, vec1_y;
  int vec2_x, vec2_y;
  Color detected_colors[24];
  gboolean detected = FALSE;

  frame.y = GST_BUFFER_DATA (buf);
  frame.ystride = gst_video_format_get_row_stride (patchdetect->format,
      0, patchdetect->width);
  frame.u =
      frame.y + gst_video_format_get_component_offset (patchdetect->format, 1,
      patchdetect->width, patchdetect->height);
  frame.ustride =
      gst_video_format_get_row_stride (patchdetect->format, 1,
      patchdetect->width);
  frame.v =
      frame.y + gst_video_format_get_component_offset (patchdetect->format, 2,
      patchdetect->width, patchdetect->height);
  frame.vstride =
      gst_video_format_get_row_stride (patchdetect->format, 2,
      patchdetect->width);
  frame.width = patchdetect->width;
  frame.height = patchdetect->height;
  frame.t = patchdetect->t;
  patchdetect->t++;

  blocks_y = (patchdetect->height & (~7)) / 8;
  blocks_x = (patchdetect->width & (~7)) / 8;

  patchpix = g_malloc0 (patchdetect->width * patchdetect->height);
  patches = g_malloc0 (sizeof (Patch) * 256);

  n_patches = 0;
  for (j = 0; j < blocks_y; j += 4) {
    for (i = 0; i < blocks_x; i += 4) {
      Stats block = { 0 };

      get_block_stats (&frame, i * 8, j * 8, &block);

      patches[n_patches].val = n_patches + 2;
      if (block.match) {
        if (patch_check (&frame, patchpix, i * 8, j * 8, 8, 8)) {
          patch_start (&frame, patchpix, patches + n_patches, i * 8, j * 8, 8,
              8);

          patches[n_patches].y = block.y;
          patches[n_patches].u = block.u;
          patches[n_patches].v = block.v;

          patch_grow (&frame, patchpix, patches + n_patches);
          n_patches++;
          g_assert (n_patches < 256);
        }
      }
    }
  }

  {
    int n;

    for (n = 0; n < n_patches; n++) {
      Patch *patch = &patches[n];
      int xsum;
      int ysum;

      if (patch->count > 10000)
        continue;
      patch->valid = TRUE;

      xsum = 0;
      ysum = 0;
      for (j = patch->ymin; j < patch->ymax; j++) {
        for (i = patch->xmin; i < patch->xmax; i++) {
          if (patchpix[j * frame.width + i] != patch->val)
            continue;
          xsum += i;
          ysum += j;
        }
      }

      patch->cen_x = xsum / patch->count;
      patch->cen_y = ysum / patch->count;
    }

  }

  points = g_malloc0 (sizeof (Point) * 1000);
  n_points = 0;

  for (i = 0; i < n_patches; i++) {
    for (j = i + 1; j < n_patches; j++) {
      int dist_x, dist_y;

      if (i == j)
        continue;

      dist_x = patches[i].cen_x - patches[j].cen_x;
      dist_y = patches[i].cen_y - patches[j].cen_y;

      if (dist_x < 0) {
        dist_x = -dist_x;
        dist_y = -dist_y;
      }
      if (ABS (2 * dist_y) < dist_x && dist_x < 100) {
        points[n_points].x = dist_x;
        points[n_points].y = dist_y;
        points[n_points].valid = TRUE;
        points[n_points].patch1 = i;
        points[n_points].patch2 = j;
        n_points++;
        g_assert (n_points < 1000);
      }
    }
  }

  {
    int dist;
    int ave_x = 0, ave_y = 0;
    for (dist = 50; dist >= 10; dist -= 5) {
      int sum_x, sum_y;
      int n_valid;

      sum_x = 0;
      sum_y = 0;
      n_valid = 0;
      for (i = 0; i < n_points; i++) {
        if (!points[i].valid)
          continue;
        sum_x += points[i].x;
        sum_y += points[i].y;
        n_valid++;
      }
      if (n_valid == 0)
        continue;
      ave_x = sum_x / n_valid;
      ave_y = sum_y / n_valid;

      for (i = 0; i < n_points; i++) {
        int d;
        if (!points[i].valid)
          continue;
        d = (points[i].x - ave_x) * (points[i].x - ave_x);
        d += (points[i].y - ave_y) * (points[i].y - ave_y);
        if (d > dist * dist)
          points[i].valid = FALSE;
      }
    }
    vec1_x = ave_x;
    vec1_y = ave_y;
  }

  n_points = 0;
  for (i = 0; i < n_patches; i++) {
    for (j = i + 1; j < n_patches; j++) {
      int dist_x, dist_y;

      if (i == j)
        continue;

      dist_x = patches[i].cen_x - patches[j].cen_x;
      dist_y = patches[i].cen_y - patches[j].cen_y;

      if (dist_y < 0) {
        dist_x = -dist_x;
        dist_y = -dist_y;
      }
      if (ABS (2 * dist_x) < dist_y && dist_y < 100) {
        points[n_points].x = dist_x;
        points[n_points].y = dist_y;
        points[n_points].valid = TRUE;
        points[n_points].patch1 = i;
        points[n_points].patch2 = j;
        n_points++;
        g_assert (n_points < 1000);
      }
    }
  }

  {
    int dist;
    int ave_x = 0, ave_y = 0;
    for (dist = 50; dist >= 10; dist -= 5) {
      int sum_x, sum_y;
      int n_valid;

      sum_x = 0;
      sum_y = 0;
      n_valid = 0;
      for (i = 0; i < n_points; i++) {
        if (!points[i].valid)
          continue;
        sum_x += points[i].x;
        sum_y += points[i].y;
        n_valid++;
      }
      if (n_valid == 0)
        continue;
      ave_x = sum_x / n_valid;
      ave_y = sum_y / n_valid;

      for (i = 0; i < n_points; i++) {
        int d;
        if (!points[i].valid)
          continue;
        d = (points[i].x - ave_x) * (points[i].x - ave_x);
        d += (points[i].y - ave_y) * (points[i].y - ave_y);
        if (d > dist * dist)
          points[i].valid = FALSE;
      }
    }
    vec2_x = ave_x;
    vec2_y = ave_y;
  }

#if 0
  for (i = 0; i < n_points; i++) {
    if (!points[i].valid)
      continue;
    paint_block (&frame, 4 * points[i].x, 240 + 4 * points[i].y, 16);
  }
#endif
#if 0
  paint_block (&frame, 360, 240, 16);
  paint_block (&frame, 360 + vec1_x, 240 + vec1_y, 16);
  paint_block (&frame, 360 + vec2_x, 240 + vec2_y, 16);
#endif

  {
    double m00, m01, m10, m11;
    double det;
    double v1, v2;
    double ave_v1 = 0, ave_v2 = 0;

    det = vec1_x * vec2_y - vec1_y * vec2_x;
    m00 = vec2_y / det;
    m01 = -vec2_x / det;
    m10 = -vec1_y / det;
    m11 = vec1_x / det;

    for (i = 0; i < n_patches - 1; i++) {
      int count = 0;
      double sum_v1 = 0;
      double sum_v2 = 0;

      if (!patches[i].valid)
        continue;

      n_points = 0;
      for (j = i + 1; j < n_patches; j++) {
        int diff_x = patches[j].cen_x - patches[i].cen_x;
        int diff_y = patches[j].cen_y - patches[i].cen_y;

        if (!patches[j].valid)
          continue;

        v1 = diff_x * m00 + diff_y * m01;
        v2 = diff_x * m10 + diff_y * m11;

        if (v1 > -0.5 && v1 < 5.5 && v2 > -0.5 && v2 < 3.5 &&
            ABS (v1 - rint (v1)) < 0.1 && ABS (v2 - rint (v2)) < 0.1) {
          sum_v1 += v1 - rint (v1);
          sum_v2 += v2 - rint (v2);
          count++;
        }
      }
      ave_v1 = sum_v1 / count;
      ave_v2 = sum_v2 / count;

      if (count > 20) {
        int k;
        for (j = 0; j < 4; j++) {
          for (k = 0; k < 6; k++) {
            Stats block;

            int xx;
            int yy;
            xx = patches[i].cen_x + (ave_v1 + k) * vec1_x + (ave_v2 +
                j) * vec2_x;
            yy = patches[i].cen_y + (ave_v1 + k) * vec1_y + (ave_v2 +
                j) * vec2_y;

            get_block_stats (&frame, xx - 4, yy - 4, &block);
            //GST_ERROR("%d %d: %d %d %d", k, j, block.y, block.u, block.v);

            detected_colors[k + j * 6].y = block.y;
            detected_colors[k + j * 6].u = block.u;
            detected_colors[k + j * 6].v = block.v;

            paint_block (&frame, xx - 4, yy - 4, 16);
          }
        }

        detected = TRUE;

#if 0
        for (j = i + 1; j < n_patches; j++) {
          int diff_x = patches[j].cen_x - patches[i].cen_x;
          int diff_y = patches[j].cen_y - patches[i].cen_y;
          int xx;
          int yy;

          if (!patches[j].valid)
            continue;

          v1 = diff_x * m00 + diff_y * m01;
          v2 = diff_x * m10 + diff_y * m11;

          if (v1 > -0.5 && v1 < 5.5 && v2 > -0.5 && v2 < 3.5 &&
              ABS (v1 - rint (v1)) < 0.1 && ABS (v2 - rint (v2)) < 0.1) {
            v1 = rint (v1);
            v2 = rint (v2);
            xx = patches[i].cen_x + (ave_v1 + v1) * vec1_x + (ave_v2 +
                v2) * vec2_x;
            yy = patches[i].cen_y + (ave_v1 + v1) * vec1_y + (ave_v2 +
                v2) * vec2_y;

            paint_block (&frame, patches[j].cen_x, patches[j].cen_y, 128);
            paint_block (&frame, xx, yy, 16);
          }
        }
        paint_block (&frame, patches[i].cen_x, patches[i].cen_y, 240);
#endif
        break;
      }
    }
  }

#define N 10
  if (detected) {
    int i, j, k;
    int n = N;
    double diff = 0;
    double matrix[10][10] = { {0} };
    double vy[10] = { 0 };
    double vu[10] = { 0 };
    double vv[10] = { 0 };
    double *by = patchdetect->by;
    double *bu = patchdetect->bu;
    double *bv = patchdetect->bv;
    double flip_diff = 0;

    for (i = 0; i < 24; i++) {
      diff += ABS (detected_colors[i].y - patch_colors[i].y);
      diff += ABS (detected_colors[i].u - patch_colors[i].u);
      diff += ABS (detected_colors[i].v - patch_colors[i].v);

      flip_diff += ABS (detected_colors[23 - i].y - patch_colors[i].y);
      flip_diff += ABS (detected_colors[23 - i].u - patch_colors[i].u);
      flip_diff += ABS (detected_colors[23 - i].v - patch_colors[i].v);
    }
    GST_ERROR ("uncorrected error %g (flipped %g)", diff / 24.0,
        flip_diff / 24.0);
    if (flip_diff < diff) {
      for (i = 0; i < 12; i++) {
        Color tmp;
        tmp = detected_colors[i];
        detected_colors[i] = detected_colors[23 - i];
        detected_colors[23 - i] = tmp;
      }
    }

    for (i = 0; i < 24; i++) {
      int dy = detected_colors[i].y - patch_colors[i].y;
      int du = detected_colors[i].u - patch_colors[i].u;
      int dv = detected_colors[i].v - patch_colors[i].v;
      int py = detected_colors[i].y - 128;
      int pu = detected_colors[i].u - 128;
      int pv = detected_colors[i].v - 128;
      int w = (i < 18) ? 1 : 2;
      double z[10];

      diff += ABS (dy) + ABS (du) + ABS (dv);

      z[0] = 1;
      z[1] = py;
      z[2] = pu;
      z[3] = pv;
      z[4] = py * py;
      z[5] = py * pu;
      z[6] = py * pv;
      z[7] = pu * pu;
      z[8] = pu * pv;
      z[9] = pv * pv;

      for (j = 0; j < n; j++) {
        for (k = 0; k < n; k++) {
          matrix[j][k] += w * z[j] * z[k];
        }

        vy[j] += w * dy * z[j];
        vu[j] += w * du * z[j];
        vv[j] += w * dv * z[j];
      }
    }

    invert_matrix (matrix, n);

    for (i = 0; i < n; i++) {
      by[i] = 0;
      bu[i] = 0;
      bv[i] = 0;
      for (j = 0; j < n; j++) {
        by[i] += matrix[i][j] * vy[j];
        bu[i] += matrix[i][j] * vu[j];
        bv[i] += matrix[i][j] * vv[j];
      }
    }

    //GST_ERROR("a %g %g %g b %g %g %g", ay, au, av, by, bu, bv);

    diff = 0;
    for (i = 0; i < 24; i++) {
      double cy, cu, cv;
      double z[10];
      int py = detected_colors[i].y - 128;
      int pu = detected_colors[i].u - 128;
      int pv = detected_colors[i].v - 128;

      z[0] = 1;
      z[1] = py;
      z[2] = pu;
      z[3] = pv;
      z[4] = py * py;
      z[5] = py * pu;
      z[6] = py * pv;
      z[7] = pu * pu;
      z[8] = pu * pv;
      z[9] = pv * pv;

      cy = 0;
      cu = 0;
      cv = 0;
      for (j = 0; j < n; j++) {
        cy += by[j] * z[j];
        cu += bu[j] * z[j];
        cv += bv[j] * z[j];
      }

      diff += fabs (patch_colors[i].y - (128 + py - cy));
      diff += fabs (patch_colors[i].u - (128 + pu - cu));
      diff += fabs (patch_colors[i].v - (128 + pv - cv));
    }
    GST_ERROR ("average error %g", diff / 24.0);
    patchdetect->valid = 3000;
  }

  if (patchdetect->valid > 0) {
    int n = N;
    guint8 *u1, *u2;
    guint8 *v1, *v2;
    double *by = patchdetect->by;
    double *bu = patchdetect->bu;
    double *bv = patchdetect->bv;

    patchdetect->valid--;
    u1 = g_malloc (frame.width);
    u2 = g_malloc (frame.width);
    v1 = g_malloc (frame.width);
    v2 = g_malloc (frame.width);

    for (j = 0; j < frame.height; j += 2) {
      for (i = 0; i < frame.width / 2; i++) {
        u1[2 * i + 0] = frame.u[(j / 2) * frame.ustride + i];
        u1[2 * i + 1] = u1[2 * i + 0];
        u2[2 * i + 0] = u1[2 * i + 0];
        u2[2 * i + 1] = u1[2 * i + 0];
        v1[2 * i + 0] = frame.v[(j / 2) * frame.vstride + i];
        v1[2 * i + 1] = v1[2 * i + 0];
        v2[2 * i + 0] = v1[2 * i + 0];
        v2[2 * i + 1] = v1[2 * i + 0];
      }
      for (i = 0; i < frame.width; i++) {
        int k;
        double z[10];
        double cy, cu, cv;
        int y, u, v;
        int py, pu, pv;

        y = frame.y[(j + 0) * frame.ystride + i];
        u = u1[i];
        v = v1[i];

        py = y - 128;
        pu = u - 128;
        pv = v - 128;

        z[0] = 1;
        z[1] = py;
        z[2] = pu;
        z[3] = pv;
        z[4] = py * py;
        z[5] = py * pu;
        z[6] = py * pv;
        z[7] = pu * pu;
        z[8] = pu * pv;
        z[9] = pv * pv;

        cy = 0;
        cu = 0;
        cv = 0;
        for (k = 0; k < n; k++) {
          cy += by[k] * z[k];
          cu += bu[k] * z[k];
          cv += bv[k] * z[k];
        }

        frame.y[(j + 0) * frame.ystride + i] = CLAMP (rint (y - cy), 0, 255);
        u1[i] = CLAMP (rint (u - cu), 0, 255);
        v1[i] = CLAMP (rint (v - cv), 0, 255);

        y = frame.y[(j + 1) * frame.ystride + i];
        u = u2[i];
        v = v2[i];

        py = y - 128;
        pu = u - 128;
        pv = v - 128;

        z[0] = 1;
        z[1] = py;
        z[2] = pu;
        z[3] = pv;
        z[4] = py * py;
        z[5] = py * pu;
        z[6] = py * pv;
        z[7] = pu * pu;
        z[8] = pu * pv;
        z[9] = pv * pv;

        cy = 0;
        cu = 0;
        cv = 0;
        for (k = 0; k < n; k++) {
          cy += by[k] * z[k];
          cu += bu[k] * z[k];
          cv += bv[k] * z[k];
        }

        frame.y[(j + 1) * frame.ystride + i] = CLAMP (rint (y - cy), 0, 255);
        u2[i] = CLAMP (rint (u - cu), 0, 255);
        v2[i] = CLAMP (rint (v - cv), 0, 255);
      }
      for (i = 0; i < frame.width / 2; i++) {
        frame.u[(j / 2) * frame.ustride + i] = (u1[2 * i + 0] +
            u1[2 * i + 1] + u2[2 * i + 0] + u2[2 * i + 1] + 2) >> 2;
        frame.v[(j / 2) * frame.vstride + i] = (v1[2 * i + 0] +
            v1[2 * i + 1] + v2[2 * i + 0] + v2[2 * i + 1] + 2) >> 2;
      }
    }

    g_free (u1);
    g_free (u2);
    g_free (v1);
    g_free (v2);
  }

  g_free (points);
  g_free (patches);
  g_free (patchpix);

  return GST_FLOW_OK;
}

static gboolean
gst_patchdetect_src_event (GstBaseTransform * trans, GstEvent * event)
{

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "patchdetect", GST_RANK_NONE,
      gst_patchdetect_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    patchdetect,
    "patchdetect element",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
