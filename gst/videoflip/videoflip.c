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

#define DEBUG_ENABLED
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <stdlib.h>
#include <math.h>
#include <videoflip.h>
#include <string.h>

#include "gstvideoflip.h"

static void gst_videoflip_planar411 (GstVideoflip * scale, unsigned char *dest,
    unsigned char *src);

static void gst_videoflip_flip (GstVideoflip * videoflip, unsigned char *dest,
    unsigned char *src, int sw, int sh, int dw, int dh);

struct videoflip_format_struct videoflip_formats[] = {
  /* planar */
  {"YV12", 12, gst_videoflip_planar411,},
  {"I420", 12, gst_videoflip_planar411,},
};

int videoflip_n_formats =
    sizeof (videoflip_formats) / sizeof (videoflip_formats[0]);

GstStructure *
videoflip_get_cap (struct videoflip_format_struct *format)
{
  unsigned int fourcc;
  GstStructure *structure;

  if (format->scale == NULL)
    return NULL;

  fourcc =
      GST_MAKE_FOURCC (format->fourcc[0], format->fourcc[1], format->fourcc[2],
      format->fourcc[3]);

  if (format->bpp) {
    structure = gst_structure_new ("video/x-raw-rgb",
        "depth", G_TYPE_INT, format->bpp,
        "bpp", G_TYPE_INT, format->depth,
        "endianness", G_TYPE_INT, format->endianness,
        "red_mask", G_TYPE_INT, format->red_mask,
        "green_mask", G_TYPE_INT, format->green_mask,
        "blue_mask", G_TYPE_INT, format->blue_mask, NULL);
  } else {
    structure = gst_structure_new ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  }

  return structure;
}

struct videoflip_format_struct *
videoflip_find_by_caps (const GstCaps * caps)
{
  int i;

  GST_DEBUG ("finding %p", caps);

  g_return_val_if_fail (caps != NULL, NULL);

  for (i = 0; i < videoflip_n_formats; i++) {
    GstCaps *c;

    c = gst_caps_new_full (videoflip_get_cap (videoflip_formats + i), NULL);
    if (c) {
      if (gst_caps_is_always_compatible (caps, c)) {
        gst_caps_free (c);
        return videoflip_formats + i;
      }
      gst_caps_free (c);
    }
  }

  return NULL;
}

void
gst_videoflip_setup (GstVideoflip * videoflip)
{
  if (videoflip->from_width == 0 || videoflip->from_height == 0) {
    return;
  }

  switch (videoflip->method) {
    case GST_VIDEOFLIP_METHOD_90R:
    case GST_VIDEOFLIP_METHOD_90L:
    case GST_VIDEOFLIP_METHOD_TRANS:
    case GST_VIDEOFLIP_METHOD_OTHER:
      videoflip->to_height = videoflip->from_width;
      videoflip->to_width = videoflip->from_height;
      break;
    case GST_VIDEOFLIP_METHOD_IDENTITY:
    case GST_VIDEOFLIP_METHOD_180:
    case GST_VIDEOFLIP_METHOD_HORIZ:
    case GST_VIDEOFLIP_METHOD_VERT:
      videoflip->to_height = videoflip->from_height;
      videoflip->to_width = videoflip->from_width;
      break;
    default:
      /* FIXME */
      break;
  }

  GST_DEBUG ("format=%p \"%s\" from %dx%d to %dx%d",
      videoflip->format, videoflip->format->fourcc,
      videoflip->from_width, videoflip->from_height,
      videoflip->to_width, videoflip->to_height);

  if (videoflip->method == GST_VIDEOFLIP_METHOD_IDENTITY) {
    GST_DEBUG ("videoflip: using passthru");
    videoflip->passthru = TRUE;
    videoflip->inited = TRUE;
    return;
  }

  videoflip->from_buf_size = (videoflip->from_width * videoflip->from_height
      * videoflip->format->depth) / 8;
  videoflip->to_buf_size = (videoflip->to_width * videoflip->to_height
      * videoflip->format->depth) / 8;

  videoflip->inited = TRUE;
}

static void
gst_videoflip_planar411 (GstVideoflip * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoflip: scaling planar 4:1:1 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoflip_flip (scale, dest, src, sw, sh, dw, dh);

  src += sw * sh;
  dest += dw * dh;

  dh = dh >> 1;
  dw = dw >> 1;
  sh = sh >> 1;
  sw = sw >> 1;

  gst_videoflip_flip (scale, dest, src, sw, sh, dw, dh);

  src += sw * sh;
  dest += dw * dh;

  gst_videoflip_flip (scale, dest, src, sw, sh, dw, dh);
}

static void
gst_videoflip_flip (GstVideoflip * videoflip, unsigned char *dest,
    unsigned char *src, int sw, int sh, int dw, int dh)
{
  int x, y;

  switch (videoflip->method) {
    case GST_VIDEOFLIP_METHOD_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - x) * sw + y];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[x * sw + (sw - 1 - y)];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - y) * sw + (sw - 1 - x)];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[y * sw + (sw - 1 - x)];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - y) * sw + x];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_TRANS:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[x * sw + y];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_OTHER:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - x) * sw + (sw - 1 - y)];
        }
      }
      break;
    default:
      /* FIXME */
      break;
  }
}
