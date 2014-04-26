/* GStreamer
 * Copyright (C) <2009> Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

/**
 * SECTION:element-ssim
 *
 * The ssim calculates SSIM (Structural SIMilarity) index for two or more 
 * streams, for each frame.
 * First stream is the original, other streams are modified (compressed) ones.
 * ssim will calculate SSIM index of each frame of each modified stream, using 
 * original stream as a reference.
 *
 * The ssim accepts only YUV planar top-first data and calculates only Y-SSIM.
 * All streams must have the same width, height and colorspace.
 * Output streams are greyscale video streams, where bright pixels indicate 
 * high SSIM values, dark pixels - low SSIM values.
 * The ssim also calculates mean SSIM index for each frame and emits is as a 
 * message.
 * ssim is intended to be used with videomeasure_collector element to catch the 
 * events (such as mean SSIM index values) and save them into a file.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch ssim name=ssim ssim.src0 ! videoconvert ! glimagesink filesrc
 * location=orig.avi ! decodebin2 ! ssim.original filesrc location=compr.avi !
 * decodebin2 ! ssim.modified0
 * ]| This pipeline produces a video stream that consists of SSIM frames.
 * </refsect2>
 */
/* Element-Checklist-Version: 5 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideomeasure.h"
#include "gstvideomeasure_ssim.h"
#include <gst/audio/audio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GST_CAT_DEFAULT gst_ssim_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* elementfactory information */

#define SINK_CAPS \
  "video/x-raw-yuv, " \
  "format = (fourcc) { I420, YV12, Y41B, Y42B } "


#define SRC_CAPS \
  "video/x-raw-gray, " \
  "width = (int) [ 1, MAX ], " \
  "height = (int) [ 1, MAX ], " \
  "framerate = (fraction) [ 0/1, MAX ], " \
  "bpp = (int) 8, " \
  "depth = (int) 8 "

static GstStaticPadTemplate gst_ssim_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (SRC_CAPS)
    );

static GstStaticPadTemplate gst_ssim_sink_original_template =
GST_STATIC_PAD_TEMPLATE ("original",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static GstStaticPadTemplate gst_ssim_sink_modified_template =
GST_STATIC_PAD_TEMPLATE ("modified_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static void gst_ssim_class_init (GstSSimClass * klass);
static void gst_ssim_init (GstSSim * ssim);
static void gst_ssim_finalize (GObject * object);

static gboolean gst_ssim_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_ssim_query (GstPad * pad, GstQuery * query);
static gboolean gst_ssim_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_ssim_sink_event (GstPad * pad, GstEvent * event);

static GstPad *gst_ssim_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);
static void gst_ssim_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_ssim_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_ssim_collected (GstCollectPads * pads,
    gpointer user_data);

static GstElementClass *parent_class = NULL;

GType
gst_ssim_get_type (void)
{
  static GType ssim_type = 0;

  if (G_UNLIKELY (ssim_type == 0)) {
    static const GTypeInfo ssim_info = {
      sizeof (GstSSimClass), NULL, NULL,
      (GClassInitFunc) gst_ssim_class_init, NULL, NULL,
      sizeof (GstSSim), 0,
      (GInstanceInitFunc) gst_ssim_init,
    };

    ssim_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSSim",
        &ssim_info, 0);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "ssim", 0, "SSIM calculator");
  }
  return ssim_type;
}

static void
gst_ssim_post_message (GstSSim * ssim, GstBuffer * buffer, gfloat mssim,
    gfloat lowest, gfloat highest)
{
  GstMessage *m;
  guint64 offset;

  offset = GST_BUFFER_OFFSET (buffer);

  m = gst_message_new_element (GST_OBJECT_CAST (ssim),
      gst_structure_new ("SSIM",
          "offset", G_TYPE_UINT64, offset,
          "timestamp", GST_TYPE_CLOCK_TIME, GST_BUFFER_TIMESTAMP (buffer),
          "mean", G_TYPE_FLOAT, mssim,
          "lowest", G_TYPE_FLOAT, lowest,
          "highest", G_TYPE_FLOAT, highest, NULL));

  GST_DEBUG_OBJECT (GST_OBJECT (ssim), "Frame %" G_GINT64_FORMAT
      " @ %" GST_TIME_FORMAT " mean SSIM is %f, l-h is %f-%f", offset,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), mssim, lowest, highest);

  gst_element_post_message (GST_ELEMENT_CAST (ssim), m);
}

static GstCaps *
gst_ssim_src_getcaps (GstPad * pad)
{
  GstCaps *result;
  gchar *capstr;

  result = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  capstr = gst_caps_to_string (result);
  GST_DEBUG ("getsrccaps - return static caps: %s", capstr);
  g_free (capstr);
  return result;
}

static GstCaps *
gst_ssim_sink_getcaps (GstPad * pad)
{
  GstCaps *result = NULL;
  GstSSim *ssim;
  gchar *capstr;

  ssim = GST_SSIM (GST_PAD_PARENT (pad));

  GST_OBJECT_LOCK (ssim);

  result = gst_pad_get_fixed_caps_func (pad);
  capstr = gst_caps_to_string (result);
  GST_DEBUG ("getsinkcaps - return caps: %s", capstr);
  g_free (capstr);

  GST_OBJECT_UNLOCK (ssim);

  return result;
}

static void
calculate_mu (GstSSim * ssim, gfloat * outmu, guint8 * buf)
{
  gint oy, ox, iy, ix;

  for (oy = 0; oy < ssim->height; oy++) {
    for (ox = 0; ox < ssim->width; ox++) {
      gfloat mu = 0;
      gfloat elsumm;
      gint weight_y_base, weight_x_base;
      gint weight_offset;
      gint pixel_offset;
      gint winstart_y;
      gint wghstart_y;
      gint winend_y;
      gint winstart_x;
      gint wghstart_x;
      gint winend_x;
      gfloat weight;
      gint source_offset;

      source_offset = oy * ssim->width + ox;

      winstart_x = ssim->windows[source_offset].x_window_start;
      wghstart_x = ssim->windows[source_offset].x_weight_start;
      winend_x = ssim->windows[source_offset].x_window_end;
      winstart_y = ssim->windows[source_offset].y_window_start;
      wghstart_y = ssim->windows[source_offset].y_weight_start;
      winend_y = ssim->windows[source_offset].y_window_end;
      elsumm = ssim->windows[source_offset].element_summ;

      switch (ssim->windowtype) {
        case 0:
          for (iy = winstart_y; iy <= winend_y; iy++) {
            pixel_offset = iy * ssim->width;
            for (ix = winstart_x; ix <= winend_x; ix++)
              mu += buf[pixel_offset + ix];
          }
          mu = mu / elsumm;
          break;
        case 1:

          weight_y_base = wghstart_y - winstart_y;
          weight_x_base = wghstart_x - winstart_x;

          for (iy = winstart_y; iy <= winend_y; iy++) {
            pixel_offset = iy * ssim->width;
            weight_offset = (weight_y_base + iy) * ssim->windowsize +
                weight_x_base;
            for (ix = winstart_x; ix <= winend_x; ix++) {
              weight = ssim->weights[weight_offset + ix];
              mu += weight * buf[pixel_offset + ix];
            }
          }
          mu = mu / elsumm;
          break;
      }
      outmu[oy * ssim->width + ox] = mu;
    }
  }

}

static void
calcssim_without_mu (GstSSim * ssim, guint8 * org, gfloat * orgmu, guint8 * mod,
    guint8 * out, gfloat * mean, gfloat * lowest, gfloat * highest)
{
  gint oy, ox, iy, ix;
  gfloat cumulative_ssim = 0;
  *lowest = G_MAXFLOAT;
  *highest = -G_MAXFLOAT;

  for (oy = 0; oy < ssim->height; oy++) {
    for (ox = 0; ox < ssim->width; ox++) {
      gfloat mu_o = 128, mu_m = 128;
      gdouble sigma_o = 0, sigma_m = 0, sigma_om = 0;
      gfloat tmp1 = 0, tmp2 = 0;
      gfloat elsumm = 0;
      gint weight_y_base, weight_x_base;
      gint weight_offset;
      gint pixel_offset;
      gint winstart_y;
      gint wghstart_y;
      gint winend_y;
      gint winstart_x;
      gint wghstart_x;
      gint winend_x;
      gfloat weight;
      gint source_offset;

      source_offset = oy * ssim->width + ox;

      winstart_x = ssim->windows[source_offset].x_window_start;
      wghstart_x = ssim->windows[source_offset].x_weight_start;
      winend_x = ssim->windows[source_offset].x_window_end;
      winstart_y = ssim->windows[source_offset].y_window_start;
      wghstart_y = ssim->windows[source_offset].y_weight_start;
      winend_y = ssim->windows[source_offset].y_window_end;
      elsumm = ssim->windows[source_offset].element_summ;

      weight_y_base = wghstart_y - winstart_y;
      weight_x_base = wghstart_x - winstart_x;
      switch (ssim->windowtype) {
        case 0:
          for (iy = winstart_y; iy <= winend_y; iy++) {
            guint8 *org_with_offset, *mod_with_offset;
            pixel_offset = iy * ssim->width;
            org_with_offset = &org[pixel_offset];
            mod_with_offset = &mod[pixel_offset];
            for (ix = winstart_x; ix <= winend_x; ix++) {
              tmp1 = org_with_offset[ix] - mu_o;
              sigma_o += tmp1 * tmp1;
              tmp2 = mod_with_offset[ix] - mu_m;
              sigma_m += tmp2 * tmp2;
              sigma_om += tmp1 * tmp2;
            }
          }
          break;
        case 1:

          weight_y_base = wghstart_y - winstart_y;
          weight_x_base = wghstart_x - winstart_x;

          for (iy = winstart_y; iy <= winend_y; iy++) {
            guint8 *org_with_offset, *mod_with_offset;
            gfloat *weights_with_offset;
            gfloat wt1, wt2;
            pixel_offset = iy * ssim->width;
            weight_offset = (weight_y_base + iy) * ssim->windowsize +
                weight_x_base;
            org_with_offset = &org[pixel_offset];
            mod_with_offset = &mod[pixel_offset];
            weights_with_offset = &ssim->weights[weight_offset];
            for (ix = winstart_x; ix <= winend_x; ix++) {
              weight = weights_with_offset[ix];
              tmp1 = org_with_offset[ix] - mu_o;
              tmp2 = mod_with_offset[ix] - mu_m;
              wt1 = weight * tmp1;
              wt2 = weight * tmp2;
              sigma_o += wt1 * tmp1;
              sigma_m += wt2 * tmp2;
              sigma_om += wt1 * tmp2;
            }
          }
          break;
      }
      sigma_o = sqrt (sigma_o / elsumm);
      sigma_m = sqrt (sigma_m / elsumm);
      sigma_om = sigma_om / elsumm;
      tmp1 = (2 * mu_o * mu_m + ssim->const1) * (2 * sigma_om + ssim->const2) /
          ((mu_o * mu_o + mu_m * mu_m + ssim->const1) *
          (sigma_o * sigma_o + sigma_m * sigma_m + ssim->const2));

      /* SSIM can go negative, that's why it is
         127 + index * 128 instead of index * 255 */
      out[oy * ssim->width + ox] = 127 + tmp1 * 128;
      *lowest = MIN (*lowest, tmp1);
      *highest = MAX (*highest, tmp1);
      cumulative_ssim += tmp1;
    }
  }
  *mean = cumulative_ssim / (ssim->width * ssim->height);
}

static void
calcssim_canonical (GstSSim * ssim, guint8 * org, gfloat * orgmu, guint8 * mod,
    guint8 * out, gfloat * mean, gfloat * lowest, gfloat * highest)
{
  gint oy, ox, iy, ix;
  gfloat cumulative_ssim = 0;
  *lowest = G_MAXFLOAT;
  *highest = -G_MAXFLOAT;

  for (oy = 0; oy < ssim->height; oy++) {
    for (ox = 0; ox < ssim->width; ox++) {
      gfloat mu_o = 0, mu_m = 0;
      gdouble sigma_o = 0, sigma_m = 0, sigma_om = 0;
      gfloat tmp1, tmp2;
      gfloat elsumm = 0;
      gint weight_y_base, weight_x_base;
      gint weight_offset;
      gint pixel_offset;
      gint winstart_y;
      gint wghstart_y;
      gint winend_y;
      gint winstart_x;
      gint wghstart_x;
      gint winend_x;
      gfloat weight;
      gint source_offset;

      source_offset = oy * ssim->width + ox;

      winstart_x = ssim->windows[source_offset].x_window_start;
      wghstart_x = ssim->windows[source_offset].x_weight_start;
      winend_x = ssim->windows[source_offset].x_window_end;
      winstart_y = ssim->windows[source_offset].y_window_start;
      wghstart_y = ssim->windows[source_offset].y_weight_start;
      winend_y = ssim->windows[source_offset].y_window_end;
      elsumm = ssim->windows[source_offset].element_summ;

      switch (ssim->windowtype) {
        case 0:
          for (iy = winstart_y; iy <= winend_y; iy++) {
            pixel_offset = iy * ssim->width;
            for (ix = winstart_x; ix <= winend_x; ix++) {
              mu_m += mod[pixel_offset + ix];
            }
          }
          mu_m = mu_m / elsumm;
          mu_o = orgmu[oy * ssim->width + ox];
          for (iy = winstart_y; iy <= winend_y; iy++) {
            pixel_offset = iy * ssim->width;
            for (ix = winstart_x; ix <= winend_x; ix++) {
              tmp1 = org[pixel_offset + ix] - mu_o;
              tmp2 = mod[pixel_offset + ix] - mu_m;
              sigma_o += tmp1 * tmp1;
              sigma_m += tmp2 * tmp2;
              sigma_om += tmp1 * tmp2;
            }
          }
          break;
        case 1:

          weight_y_base = wghstart_y - winstart_y;
          weight_x_base = wghstart_x - winstart_x;

          for (iy = winstart_y; iy <= winend_y; iy++) {
            pixel_offset = iy * ssim->width;
            weight_offset = (weight_y_base + iy) * ssim->windowsize +
                weight_x_base;
            for (ix = winstart_x; ix <= winend_x; ix++) {
              weight = ssim->weights[weight_offset + ix];
              mu_o += weight * org[pixel_offset + ix];
              mu_m += weight * mod[pixel_offset + ix];
            }
          }
          mu_m = mu_m / elsumm;
          mu_o = orgmu[oy * ssim->width + ox];
          for (iy = winstart_y; iy <= winend_y; iy++) {
            gfloat *weights_with_offset;
            guint8 *org_with_offset, *mod_with_offset;
            gfloat wt1, wt2;
            pixel_offset = iy * ssim->width;
            weight_offset = (weight_y_base + iy) * ssim->windowsize +
                weight_x_base;
            weights_with_offset = &ssim->weights[weight_offset];
            org_with_offset = &org[pixel_offset];
            mod_with_offset = &mod[pixel_offset];
            for (ix = winstart_x; ix <= winend_x; ix++) {
              weight = weights_with_offset[ix];
              tmp1 = org_with_offset[ix] - mu_o;
              tmp2 = mod_with_offset[ix] - mu_m;
              wt1 = weight * tmp1;
              wt2 = weight * tmp2;
              sigma_o += wt1 * tmp1;
              sigma_m += wt2 * tmp2;
              sigma_om += wt1 * tmp2;
            }
          }
          break;
      }
      sigma_o = sqrt (sigma_o / elsumm);
      sigma_m = sqrt (sigma_m / elsumm);
      sigma_om = sigma_om / elsumm;
      tmp1 = (2 * mu_o * mu_m + ssim->const1) * (2 * sigma_om + ssim->const2) /
          ((mu_o * mu_o + mu_m * mu_m + ssim->const1) *
          (sigma_o * sigma_o + sigma_m * sigma_m + ssim->const2));

      /* SSIM can go negative, that's why it is
         127 + index * 128 instead of index * 255 */
      out[oy * ssim->width + ox] = 127 + tmp1 * 128;
      *lowest = MIN (*lowest, tmp1);
      *highest = MAX (*highest, tmp1);
      cumulative_ssim += tmp1;
    }
  }
  *mean = cumulative_ssim / (ssim->width * ssim->height);
}


/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only measure streams with the same caps.
 */
static gboolean
gst_ssim_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSSim *ssim;
  GList *pads;
  const char *media_type;
  GstStructure *capsstr;
  gint width, height, fps_n, fps_d;
  guint32 fourcc;

  ssim = GST_SSIM (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (ssim, "setting caps on pad %p,%s to %" GST_PTR_FORMAT, pad,
      GST_PAD_NAME (pad), caps);

  capsstr = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (capsstr, "width", &width);
  gst_structure_get_int (capsstr, "height", &height);
  gst_structure_get_fraction (capsstr, "framerate", &fps_n, &fps_d);
  gst_structure_get_fourcc (capsstr, "format", &fourcc);

  GST_OBJECT_LOCK (ssim);

  /* Sink caps are stored only once. At the moment it doesn't feel
   * right to measure streams with variable caps.
   */
  if (G_UNLIKELY (!ssim->sinkcaps)) {
    GstStructure *newstr;
    GValue list = { 0, }
    , fourcc = {
    0,};

    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&fourcc, GST_TYPE_FOURCC);

    gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('I', '4', '2', '0'));
    gst_value_list_append_value (&list, &fourcc);
    gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', 'V', '1', '2'));
    gst_value_list_append_value (&list, &fourcc);
    gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', '4', '1', 'B'));
    gst_value_list_append_value (&list, &fourcc);
    gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', '4', '2', 'B'));
    gst_value_list_append_value (&list, &fourcc);

    newstr = gst_structure_new ("video/x-raw-yuv", NULL);
    gst_structure_set (newstr, "width", G_TYPE_INT, width, NULL);
    gst_structure_set (newstr, "height", G_TYPE_INT, height, NULL);
    gst_structure_set_value (newstr, "format", &list);

    ssim->sinkcaps = gst_caps_new_full (newstr, NULL);

    g_value_unset (&list);
    g_value_unset (&fourcc);
  }

  if (G_UNLIKELY (!ssim->srccaps)) {
    GstStructure *newstr;

    newstr = gst_structure_new ("video/x-raw-gray", NULL);
    gst_structure_set (newstr, "width", G_TYPE_INT, width, NULL);
    gst_structure_set (newstr, "height", G_TYPE_INT, height, NULL);
    gst_structure_set (newstr, "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
        NULL);
    /* Calculates SSIM only for Y channel, hence the output is monochrome.
     * TODO: an option (a mask?) to calculate SSIM for more than one channel,
     * will probably output RGB, one metric per channel...that would
     * look kinda funny :)
     */
    gst_structure_set (newstr, "bpp", G_TYPE_INT, 8, "depth", G_TYPE_INT, 8,
        NULL);

    ssim->srccaps = gst_caps_new_full (newstr, NULL);
  }

  pads = GST_ELEMENT (ssim)->pads;
  while (pads) {
    GstPadDirection direction;
    GstPad *otherpad = GST_PAD (pads->data);
    direction = gst_pad_get_direction (otherpad);

    GST_DEBUG_OBJECT (ssim, "checking caps on pad %p", otherpad);
    if (direction == GST_PAD_SINK) {
      gchar *capstr;
      capstr = gst_caps_to_string (GST_PAD_CAPS (otherpad));
      GST_DEBUG_OBJECT (ssim, "old caps on pad %p,%s were %s", otherpad,
          GST_PAD_NAME (otherpad), capstr);
      g_free (capstr);
      gst_caps_replace (&GST_PAD_CAPS (otherpad), ssim->sinkcaps);
      capstr = gst_caps_to_string (ssim->sinkcaps);
      GST_DEBUG_OBJECT (ssim, "new caps on pad %p,%s are %s", otherpad,
          GST_PAD_NAME (otherpad), capstr);
      g_free (capstr);
    } else if (direction == GST_PAD_SRC) {
      gst_caps_replace (&GST_PAD_CAPS (otherpad), ssim->srccaps);
    }
    pads = g_list_next (pads);
  }

  /* parse caps now */
  media_type = gst_structure_get_name (capsstr);
  GST_DEBUG_OBJECT (ssim, "media type is %s", media_type);
  if (strcmp (media_type, "video/x-raw-yuv") == 0) {
    ssim->width = width;
    ssim->height = height;
    ssim->frame_rate = fps_n;
    ssim->frame_rate_base = fps_d;

    GST_INFO_OBJECT (ssim, "parse_caps sets ssim to yuv format "
        "%d, %dx%d, %d/%d fps", fourcc, ssim->width, ssim->height,
        ssim->frame_rate, ssim->frame_rate_base);

    /* Only planar formats are supported.
     * TODO: implement support for interleaved formats
     * Only YUV formats are supported. There's no sense in calculating the
     * index for R, G or B channels separately.
     */
    switch (fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
      case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
        break;
      default:
        goto not_supported;
    }

  } else {
    goto not_supported;
  }

  GST_OBJECT_UNLOCK (ssim);
  return TRUE;
  /* ERRORS */
not_supported:
  {
    GST_OBJECT_UNLOCK (ssim);
    GST_DEBUG_OBJECT (ssim, "unsupported format set as caps");
    return FALSE;
  }
}

static gboolean
gst_ssim_query_latency (GstSSim * ssim, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;

  res = TRUE;
  done = FALSE;

  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (ssim));
  while (!done) {
    GstIteratorResult ires;

    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);
        GstQuery *peerquery;
        GstClockTime min_cur, max_cur;
        gboolean live_cur;

        peerquery = gst_query_new_latency ();

        /* Ask peer for latency */
        res &= gst_pad_peer_query (pad, peerquery);

        /* take max from all valid return values */
        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* store the results */
    GST_DEBUG_OBJECT (ssim, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_ssim_query_duration (GstSSim * ssim, GstQuery * query)
{
  gint64 max, min;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  min = G_MAXINT64;
  res = TRUE;
  done = FALSE;

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (ssim));
  while (!done) {
    GstIteratorResult ires;

    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);

        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_query_peer_duration (pad, &format, &duration);
        /* take min&max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else {
            if (duration > max)
              max = duration;
            if (duration < min)
              min = duration;
          }
        }
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        min = G_MAXINT64;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (ssim, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (min));
    gst_query_set_duration (query, format, min);
  }

  return res;
}


static gboolean
gst_ssim_query (GstPad * pad, GstQuery * query)
{
  GstSSim *ssim = GST_SSIM (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, ssim->timestamp);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, ssim->offset);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_ssim_query_duration (ssim, query);
      break;
    case GST_QUERY_LATENCY:
      res = gst_ssim_query_latency (ssim, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads
       */
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (ssim);
  return res;
}

static gboolean
forward_event_func (GstPad * pad, GValue * ret, GstEvent * event)
{
  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  if (!gst_pad_push_event (pad, event)) {
    g_value_set_boolean (ret, FALSE);
    GST_LOG_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
  } else {
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  gst_object_unref (pad);
  return TRUE;
}

/* forwards the event to all sinkpads, takes ownership of the
 * event
 *
 * Returns: TRUE if the event could be forwarded on all
 * sinkpads.
 */
static gboolean
forward_event (GstSSim * ssim, GstEvent * event)
{
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (ssim, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (ssim));
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  return g_value_get_boolean (&vret);
}

static gboolean
gst_ssim_src_event (GstPad * pad, GstEvent * event)
{
  GstSSim *ssim;
  gboolean result;

  ssim = GST_SSIM (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      result = FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      GstSeekType curtype;
      gint64 cur;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &ssim->segment_rate, NULL, &flags, &curtype,
          &cur, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (ssim->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (pad, gst_event_new_flush_start ());
      }
      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_OBJECT_LOCK (ssim->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        ssim->segment_position = cur;
      else
        ssim->segment_position = 0;
      {
        GstSSimOutputContext *c;
        gint i = 0;
        for (i = 0; i < ssim->src->len; i++) {
          c = (GstSSimOutputContext *) g_ptr_array_index (ssim->src, i);
          c->segment_pending = TRUE;
        }
      }
      GST_OBJECT_UNLOCK (ssim->collect);

      result = forward_event (ssim, event);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (ssim, event);
      break;
  }
  gst_object_unref (ssim);

  return result;
}

static gboolean
gst_ssim_sink_event (GstPad * pad, GstEvent * event)
{
  GstSSim *ssim;
  gboolean ret;

  ssim = GST_SSIM (gst_pad_get_parent (pad));

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      gdouble applied_rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;
      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);
      GST_DEBUG ("NEWSEGMENTEVENT: update(%d), rate(%f), app_rate(%f), "
          "format(%d), start(%" GST_TIME_FORMAT ") stop(%" GST_TIME_FORMAT ") "
          "position(%" GST_TIME_FORMAT ")", update, rate, applied_rate, format,
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (position));
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      /* mark a pending new segment. This event is synchronized
       * with the streaming thread so we can safely update the
       * variable without races. It's somewhat weird because we
       * assume the collectpads forwarded the FLUSH_STOP past us
       * and downstream (using our source pad, the bastard!).
       */
    {
      GstSSimOutputContext *c;
      gint i = 0;
      for (i = 0; i < ssim->src->len; i++) {
        c = (GstSSimOutputContext *) g_ptr_array_index (ssim->src, i);
        c->segment_pending = TRUE;
      }
    }
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  GST_DEBUG ("Dispatching %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));
  ret = ssim->collect_event (pad, event);
  GST_DEBUG ("Event %s on pad %s:%s is dispatched", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));
  gst_object_unref (ssim);
  return ret;
}

static void
gst_ssim_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSSim *ssim;

  ssim = GST_SSIM (object);

  switch (prop_id) {
    case PROP_SSIM_TYPE:
      ssim->ssimtype = g_value_get_int (value);
      break;
    case PROP_WINDOW_TYPE:
      ssim->windowtype = g_value_get_int (value);
      g_free (ssim->windows);
      ssim->windows = NULL;
      break;
    case PROP_WINDOW_SIZE:
      ssim->windowsize = g_value_get_int (value);
      g_free (ssim->windows);
      ssim->windows = NULL;
      break;
    case PROP_GAUSS_SIGMA:
      ssim->sigma = g_value_get_float (value);
      g_free (ssim->windows);
      ssim->windows = NULL;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ssim_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSSim *ssim;

  ssim = GST_SSIM (object);

  switch (prop_id) {
    case PROP_SSIM_TYPE:
      g_value_set_int (value, ssim->ssimtype);
      break;
    case PROP_WINDOW_TYPE:
      g_value_set_int (value, ssim->windowtype);
      break;
    case PROP_WINDOW_SIZE:
      g_value_set_int (value, ssim->windowsize);
      break;
    case PROP_GAUSS_SIGMA:
      g_value_set_float (value, ssim->sigma);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_ssim_class_init (GstSSimClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_ssim_set_property;
  gobject_class->get_property = gst_ssim_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ssim_finalize);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSIM_TYPE,
      g_param_spec_int ("ssim-type", "SSIM type",
          "Type of the SSIM metric. 0 - canonical. 1 - with fixed mu "
          "(almost the same results, but roughly 20% faster)",
          0, 1, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW_TYPE,
      g_param_spec_int ("window-type", "Window type",
          "Type of the weighting in the window. "
          "0 - no weighting. 1 - Gaussian weighting (controlled by \"sigma\")",
          0, 1, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW_SIZE,
      g_param_spec_int ("window-size", "Window size",
          "Size of a window.", 1, 22, 11,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_GAUSS_SIGMA,
      g_param_spec_float ("gauss-sigma", "Deviation (for Gauss function)",
          "Used to calculate Gussian weights "
          "(only when using Gaussian window).",
          G_MINFLOAT, 10, 1.5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_ssim_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_ssim_sink_original_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_ssim_sink_modified_template));
  gst_element_class_set_static_metadata (gstelement_class, "SSim",
      "Filter/Analyzer/Video",
      "Calculate Y-SSIM for n+2 YUV video streams",
      "Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>");

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_ssim_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_ssim_release_pad);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_ssim_change_state);
}

static GstPad *
gst_ssim_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * padname)
{
  gchar *name;
  GstSSim *ssim;
  GstPad *newpad;
  GstPad *newsrc;
  gint padcount;
  GstPadTemplate *template;
  guint num = -1;

  if (templ->direction != GST_PAD_SINK)
    goto not_sink;

  ssim = GST_SSIM (element);

  padcount = ssim->padcount;

  GST_DEBUG_OBJECT (ssim, "number of pads = %d", padcount);

  if (padname)
    GST_DEBUG_OBJECT (ssim, "reqested pad %s", padname);
  else
    goto unnamed_pad;

  if (strcmp (padname, "original") == 0) {
    newpad = gst_pad_new_from_template (templ, "original");
    GST_DEBUG_OBJECT (ssim, "request new sink pad original");
    ssim->orig = newpad;
  } else if (strncmp (padname, "modified_", 9) == 0) {
    const gchar *numstr = &padname[9];
    num = strtoul (numstr, NULL, 10);
    if (errno == EINVAL || errno == ERANGE)
      goto bad_name;
    newpad = gst_pad_new_from_template (templ, padname);
    GST_DEBUG_OBJECT (ssim, "request new sink pad %s", padname);
  } else
    goto bad_name;

  gst_pad_set_getcaps_function (newpad,
      GST_DEBUG_FUNCPTR (gst_ssim_sink_getcaps));
  gst_pad_set_setcaps_function (newpad, GST_DEBUG_FUNCPTR (gst_ssim_setcaps));
  gst_collect_pads_add_pad (ssim->collect, newpad, sizeof (GstCollectData),
      NULL, TRUE);

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events
   */
  GST_DEBUG_OBJECT (ssim, "Current collect_event is %p, changing to %p",
      ssim->collect_event, GST_PAD_EVENTFUNC (newpad));
  ssim->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (newpad);
  gst_pad_set_event_function (newpad, GST_DEBUG_FUNCPTR (gst_ssim_sink_event));

  GST_DEBUG_OBJECT (ssim, "Adding a pad...");
  /* takes ownership of the pad */
  if (!gst_element_add_pad (GST_ELEMENT (ssim), newpad))
    goto could_not_add_sink;
  else
    /* increment pad counter */
#if GLIB_CHECK_VERSION(2,29,5)
    padcount = g_atomic_int_add (&ssim->padcount, 1);
#else
    padcount = g_atomic_int_exchange_and_add (&ssim->padcount, 1);
#endif

  if (num != -1) {
    GstSSimOutputContext *c;

    template = gst_static_pad_template_get (&gst_ssim_src_template);
    name = g_strdup_printf ("src_%u", num);
    newsrc = gst_pad_new_from_template (template, name);
    GST_DEBUG_OBJECT (ssim, "creating src pad %s", name);
    g_free (name);
    gst_object_unref (template);

    gst_pad_set_getcaps_function (newsrc,
        GST_DEBUG_FUNCPTR (gst_ssim_src_getcaps));
    gst_pad_set_query_function (newsrc, GST_DEBUG_FUNCPTR (gst_ssim_query));
    gst_pad_set_event_function (newsrc, GST_DEBUG_FUNCPTR (gst_ssim_src_event));

    if (!gst_element_add_pad (GST_ELEMENT (ssim), newsrc))
      goto could_not_add_src;

    c = g_new (GstSSimOutputContext, 1);
    c->pad = newsrc;
    g_object_set_data (G_OBJECT (newpad), "ssim-match-output-context", c);
    g_ptr_array_add (ssim->src, (gpointer) c);
  }

  return newpad;

  /* errors */
bad_name:
  {
    g_warning ("gstssim: request new pad with bad name %s (must be "
        "'modified')\n", padname);
    return NULL;
  }
unnamed_pad:
  {
    g_warning ("gstssim: request new pad without a name (must be "
        "'modified')\n");
    return NULL;
  }
not_sink:
  {
    g_warning ("gstssim: request new pad that is not a SINK pad\n");
    return NULL;
  }
could_not_add_src:
  {
    GST_DEBUG_OBJECT (ssim, "could not add src pad");
    gst_object_unref (newsrc);
  }
could_not_add_sink:
  {
    GST_DEBUG_OBJECT (ssim, "could not add sink pad");
    gst_collect_pads_remove_pad (ssim->collect, newpad);
    gst_object_unref (newpad);
    return NULL;
  }
}

static void
gst_ssim_release_pad (GstElement * element, GstPad * pad)
{
  GstSSim *ssim;

  ssim = GST_SSIM (element);

  GST_DEBUG_OBJECT (ssim, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_collect_pads_remove_pad (ssim->collect, pad);
  gst_element_remove_pad (element, pad);
}


static void
gst_ssim_init (GstSSim * ssim)
{
  ssim->windowsize = 11;
  ssim->windowtype = 1;
  ssim->windows = NULL;
  ssim->sigma = 1.5;
  ssim->ssimtype = 0;
  ssim->src = g_ptr_array_new ();
  ssim->padcount = 0;
  ssim->collect_event = NULL;
  ssim->sinkcaps = NULL;

  /* keep track of the sinkpads requested */
  ssim->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (ssim->collect,
      GST_DEBUG_FUNCPTR (gst_ssim_collected), ssim);
}

static void
gst_ssim_finalize (GObject * object)
{
  GstSSim *ssim = GST_SSIM (object);

  gst_object_unref (ssim->collect);
  ssim->collect = NULL;

  g_free (ssim->windows);
  ssim->windows = NULL;

  g_free (ssim->weights);
  ssim->weights = NULL;

  if (ssim->sinkcaps)
    gst_caps_unref (ssim->sinkcaps);
  if (ssim->srccaps)
    gst_caps_unref (ssim->srccaps);

  g_ptr_array_free (ssim->src, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

typedef gfloat (*GstSSimWeightFunc) (GstSSim * ssim, gint y, gint x);

static gfloat
gst_ssim_weight_func_none (GstSSim * ssim, gint y, gint x)
{
  return 1;
}

static gfloat
gst_ssim_weight_func_gauss (GstSSim * ssim, gint y, gint x)
{
  gfloat coord = sqrt (x * x + y * y);
  return exp (-1 * (coord * coord) / (2 * ssim->sigma * ssim->sigma)) /
      (ssim->sigma * sqrt (2 * G_PI));
}

static gboolean
gst_ssim_regenerate_windows (GstSSim * ssim)
{
  gint windowiseven;
  gint y, x, y2, x2;
  GstSSimWeightFunc func;
  gfloat normal_summ = 0;
  gint normal_count = 0;

  g_free (ssim->weights);

  ssim->weights = g_new (gfloat, ssim->windowsize * ssim->windowsize);

  windowiseven = ((gint) ssim->windowsize / 2) * 2 == ssim->windowsize ? 1 : 0;

  g_free (ssim->windows);

  ssim->windows = g_new (GstSSimWindowCache, ssim->height * ssim->width);

  switch (ssim->windowtype) {
    case 0:
      func = gst_ssim_weight_func_none;
      break;
    case 1:
      func = gst_ssim_weight_func_gauss;
      break;
    default:
      GST_WARNING_OBJECT (ssim, "unknown window type - %d. Defaulting to %d",
          ssim->windowtype, 1);
      ssim->windowtype = 1;
      func = gst_ssim_weight_func_gauss;
  }

  for (y = 0; y < ssim->windowsize; y++) {
    gint yoffset = y * ssim->windowsize;
    for (x = 0; x < ssim->windowsize; x++) {
      ssim->weights[yoffset + x] = func (ssim, x - ssim->windowsize / 2 +
          windowiseven, y - ssim->windowsize / 2 + windowiseven);
      normal_summ += ssim->weights[yoffset + x];
      normal_count++;
    }
  }

  for (y = 0; y < ssim->height; y++) {
    for (x = 0; x < ssim->width; x++) {
      GstSSimWindowCache win;
      gint element_count = 0;

      win.x_window_start = x - ssim->windowsize / 2 + windowiseven;
      win.x_weight_start = 0;
      if (win.x_window_start < 0) {
        win.x_weight_start = -win.x_window_start;
        win.x_window_start = 0;
      }

      win.x_window_end = x + ssim->windowsize / 2;
      if (win.x_window_end >= ssim->width)
        win.x_window_end = ssim->width - 1;

      win.y_window_start = y - ssim->windowsize / 2 + windowiseven;
      win.y_weight_start = 0;
      if (win.y_window_start < 0) {
        win.y_weight_start = -win.y_window_start;
        win.y_window_start = 0;
      }

      win.y_window_end = y + ssim->windowsize / 2;
      if (win.y_window_end >= ssim->height)
        win.y_window_end = ssim->height - 1;

      win.element_summ = 0;
      element_count = (win.y_window_end - win.y_window_start + 1) *
          (win.x_window_end - win.x_window_start + 1);
      if (element_count == normal_count)
        win.element_summ = normal_summ;
      else {
        for (y2 = win.y_weight_start; y2 < ssim->windowsize; y2++) {
          for (x2 = win.x_weight_start; x2 < ssim->windowsize; x2++) {
            win.element_summ += ssim->weights[y2 * ssim->windowsize + x2];
          }
        }
      }
      ssim->windows[(y * ssim->width + x)] = win;
    }
  }

  /* FIXME: while 0.01 and 0.03 are pretty much static, the 255 implies that
   * we're working with 8-bit-per-color-component format, which may not be true
   */
  ssim->const1 = 0.01 * 255 * 0.01 * 255;
  ssim->const2 = 0.03 * 255 * 0.03 * 255;
  return TRUE;
}

static GstFlowReturn
gst_ssim_collected (GstCollectPads * pads, gpointer user_data)
{
  GstSSim *ssim;
  GSList *collected;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *orgbuf = NULL;
  gfloat *orgmu = NULL;
  GstBuffer *outbuf = NULL;
  gpointer outdata = NULL;
  guint outsize = 0;
  gfloat mssim = 0, lowest = 1, highest = -1;
  gboolean ready = TRUE;
  gint padnumber = 0;

  ssim = GST_SSIM (user_data);

  if (G_UNLIKELY (ssim->windows == NULL)) {
    GST_DEBUG_OBJECT (ssim, "Regenerating windows");
    gst_ssim_regenerate_windows (ssim);
  }

  switch (ssim->ssimtype) {
    case 0:
      ssim->func = (GstSSimFunction) calcssim_canonical;
      break;
    case 1:
      ssim->func = (GstSSimFunction) calcssim_without_mu;
      break;
    default:
      return GST_FLOW_ERROR;
  }

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstCollectData *collect_data;
    GstBuffer *inbuf;

    collect_data = (GstCollectData *) collected->data;

    inbuf = gst_collect_pads_peek (pads, collect_data);

    if (inbuf == NULL) {
      GST_LOG_OBJECT (ssim, "channel %p: no bytes available", collect_data);
      ready = FALSE;
    } else
      gst_buffer_unref (inbuf);
  }

  /* if _collected() was called, all pads should have data, but if
   * one of them doesn't, it means that it is EOS and we can't go any further
   *
   * FIXME, shouldn't we do something about pads that DO have data?
   * Flush them or something?
   */
  if (G_UNLIKELY (!ready))
    goto eos;

  /* Mu is just a blur, we can calculate it once */
  if (ssim->ssimtype == 0) {
    orgmu = g_new (gfloat, ssim->width * ssim->height);

    for (collected = pads->data; collected;
        collected = g_slist_next (collected)) {
      GstCollectData *collect_data;

      collect_data = (GstCollectData *) collected->data;

      if (collect_data->pad == ssim->orig) {
        orgbuf = gst_collect_pads_pop (pads, collect_data);;

        GST_DEBUG_OBJECT (ssim, "Original stream - flags(0x%x), timestamp(%"
            GST_TIME_FORMAT "), duration(%" GST_TIME_FORMAT ")",
            GST_BUFFER_FLAGS (orgbuf),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (orgbuf)),
            GST_TIME_ARGS (GST_BUFFER_DURATION (orgbuf)));
        calculate_mu (ssim, orgmu, GST_BUFFER_DATA (orgbuf));

        break;
      }
    }
  }

  GST_LOG_OBJECT (ssim, "starting to cycle through streams");

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstCollectData *collect_data;
    GstBuffer *inbuf;
    guint8 *indata;

    collect_data = (GstCollectData *) collected->data;

    if (collect_data->pad != ssim->orig) {
      inbuf = gst_collect_pads_pop (pads, collect_data);

      indata = GST_BUFFER_DATA (inbuf);

      GST_DEBUG_OBJECT (ssim, "Modified stream - flags(0x%x), timestamp(%"
          GST_TIME_FORMAT "), duration(%" GST_TIME_FORMAT ")",
          GST_BUFFER_FLAGS (inbuf),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)));

      if (!GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
        GstSSimOutputContext *c;
        GstEvent *measured;
        guint64 offset;
        GValue vmean = { 0 }
        , vlowest = {
        0}
        , vhighest = {
        0};

        c = (GstSSimOutputContext *)
            g_object_get_data (G_OBJECT (collect_data->pad),
            "ssim-match-output-context");

        GST_DEBUG_OBJECT (ssim, "Output context is %" GST_PTR_FORMAT
            ", pad will be %" GST_PTR_FORMAT, c, c->pad);

        outsize = GST_ROUND_UP_4 (ssim->width) * ssim->height;
        GST_LOG_OBJECT (ssim, "channel %p: making output buffer of %d bytes",
            collect_data, outsize);

        /* first buffer, alloc outsize.
         * FIXME: we can easily subbuffer and _make_writable.
         * FIXME: only create empty buffer for first non-gap buffer, so that we
         * only use ssim function when really calculating
         */
        outbuf = gst_buffer_new_and_alloc (GST_ROUND_UP_4 (ssim->width) *
            ssim->height);
        outdata = GST_BUFFER_DATA (outbuf);
        gst_buffer_set_caps (outbuf, gst_pad_get_fixed_caps_func (c->pad));

        /* Videos should match, so the output video has the same characteristics
         * as the input video
         */
        /* set timestamps on the output buffer */
        gst_buffer_copy_metadata (outbuf, inbuf, (GstBufferCopyFlags)
            GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

        g_value_init (&vmean, G_TYPE_FLOAT);
        g_value_init (&vlowest, G_TYPE_FLOAT);
        g_value_init (&vhighest, G_TYPE_FLOAT);

        GST_LOG_OBJECT (ssim, "channel %p: calculating SSIM", collect_data);

        ssim->func (ssim, GST_BUFFER_DATA (orgbuf), orgmu, indata, outdata,
            &mssim, &lowest, &highest);

        GST_DEBUG_OBJECT (GST_OBJECT (ssim), "MSSIM is %f, l-h is %f - %f",
            mssim, lowest, highest);

        gst_ssim_post_message (ssim, outbuf, mssim, lowest, highest);

        g_value_set_float (&vmean, mssim);
        g_value_set_float (&vlowest, lowest);
        g_value_set_float (&vhighest, highest);
        offset = GST_BUFFER_OFFSET (inbuf);

        /* our timestamping is very simple, just an ever incrementing
         * counter, the new segment time will take care of their respective
         * stream time.
         */
        if (c->segment_pending) {
          GstEvent *event;

          /* FIXME, use rate/applied_rate as set on all sinkpads.
           * - currently we just set rate as received from last seek-event
           * We could potentially figure out the duration as well using
           * the current segment positions and the stated stop positions.
           * Also we just start from stream time 0 which is rather
           * weird. For non-synchronized mixing, the time should be
           * the min of the stream times of all received segments,
           * rationale being that the duration is at least going to
           * be as long as the earliest stream we start mixing. This
           * would also be correct for synchronized mixing but then
           * the later streams would be delayed until the stream times`
           * match.
           */
          event = gst_event_new_new_segment_full (FALSE, ssim->segment_rate,
              1.0, GST_FORMAT_TIME, ssim->timestamp, -1,
              ssim->segment_position);

          gst_pad_push_event (c->pad, event);
          c->segment_pending = FALSE;
        }

        measured = gst_event_new_measured (offset,
            GST_BUFFER_TIMESTAMP (inbuf), "SSIM", &vmean, &vlowest, &vhighest);
        gst_pad_push_event (c->pad, measured);

        /* send it out */
        GST_DEBUG_OBJECT (ssim, "pushing outbuf, timestamp %" GST_TIME_FORMAT
            ", size %d", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
            GST_BUFFER_SIZE (outbuf));
        ret &= gst_pad_push (c->pad, outbuf);

      } else {
        GST_LOG_OBJECT (ssim, "channel %p: skipping", collect_data);
      }
      gst_buffer_unref (inbuf);
      padnumber++;
    }
  }
  gst_buffer_unref (orgbuf);

  if (ssim->ssimtype == 0)
    g_free (orgmu);

  ssim->segment_position = 0;

  return ret;

  /* ERRORS */
eos:
  {
    gint i;
    GST_DEBUG_OBJECT (ssim, "no data available, must be EOS");
    for (i = 0; i < ssim->src->len; i++) {
      GstSSimOutputContext *c =
          (GstSSimOutputContext *) g_ptr_array_index (ssim->src, i);
      gst_pad_push_event (c->pad, gst_event_new_eos ());
    }

    return GST_FLOW_UNEXPECTED;
  }
}

static GstStateChangeReturn
gst_ssim_change_state (GstElement * element, GstStateChange transition)
{
  GstSSim *ssim;
  GstStateChangeReturn ret;

  ssim = GST_SSIM (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ssim->timestamp = 0;
      ssim->offset = 0;
      {
        GstSSimOutputContext *c;
        gint i = 0;
        for (i = 0; i < ssim->src->len; i++) {
          c = (GstSSimOutputContext *) g_ptr_array_index (ssim->src, i);
          c->segment_pending = TRUE;
        }
      }
      ssim->segment_position = 0;
      ssim->segment_rate = 1.0;
      gst_segment_init (&ssim->segment, GST_FORMAT_UNDEFINED);
      gst_collect_pads_start (ssim->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish
       */
      gst_collect_pads_stop (ssim->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}
