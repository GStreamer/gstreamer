/* Gnome-Streamer
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

//#define DEBUG_ENABLED

#include <gst/gst.h>
#include <gstcolorspace.h>

static void gst_colorspace_rgb_to_rgb_identity(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest); 
static void gst_colorspace_rgb24_to_bgr24(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_rgb32_to_bgr32(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest); 
static void gst_colorspace_rgb555_to_rgb565(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest); 

GstColorSpaceConvertFunction gst_colorspace_rgb2rgb_get_converter(GstColorSpaceConverter *space, GstColorSpaceType src, GstColorSpaceType dest) {
  switch(src) {
    case GST_COLORSPACE_RGB24:
      space->insize = space->width*space->height*3;
      switch(dest) {
        case GST_COLORSPACE_RGB24:
	  space->outsize = space->width*space->height*3;
          return gst_colorspace_rgb_to_rgb_identity;
        case GST_COLORSPACE_BGR24:
	  space->outsize = space->width*space->height*3;
          return gst_colorspace_rgb24_to_bgr24;
	default:
	  break;
      }
      break;
    case GST_COLORSPACE_BGR24:
      space->insize = space->width*space->height*3;
      switch(dest) {
        case GST_COLORSPACE_RGB24:
	  space->outsize = space->width*space->height*3;
          return gst_colorspace_rgb24_to_bgr24;
        case GST_COLORSPACE_BGR24:
	  space->outsize = space->width*space->height*3;
          return gst_colorspace_rgb_to_rgb_identity;
	default:
	  break;
      }
      break;
    case GST_COLORSPACE_RGB32:
      space->insize = space->width*space->height*4;
      switch(dest) {
        case GST_COLORSPACE_BGR32:
	  space->outsize = space->width*space->height*4;
          return gst_colorspace_rgb32_to_bgr32;
        case GST_COLORSPACE_RGB32:
	  space->outsize = space->width*space->height*4;
          return gst_colorspace_rgb_to_rgb_identity;
	default:
	  break;
      }
      break;
    case GST_COLORSPACE_BGR32:
      space->insize = space->width*space->height*4;
      switch(dest) {
        case GST_COLORSPACE_RGB32:
	  space->outsize = space->width*space->height*4;
          return gst_colorspace_rgb32_to_bgr32;
        case GST_COLORSPACE_BGR32:
	  space->outsize = space->width*space->height*4;
          return gst_colorspace_rgb_to_rgb_identity;
	default:
	  break;
      }
      break;
    case GST_COLORSPACE_BGR555:
      space->insize = space->width*space->height*2;
      switch(dest) {
        case GST_COLORSPACE_RGB555:
	  space->outsize = space->width*space->height*2;
          return gst_colorspace_rgb32_to_bgr32;
        case GST_COLORSPACE_BGR565:
	  space->outsize = space->width*space->height*2;
          return gst_colorspace_rgb555_to_rgb565;
	default:
	  break;
      }
    default:
      break;
  }
  g_print("gst_colorspace: conversion not supported %d %d\n", src, dest);
  return NULL;
}

static void gst_colorspace_rgb_to_rgb_identity(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) 
{
  memcpy(dest, src, space->outsize);
}

static void gst_colorspace_rgb24_to_bgr24(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) 
{
  gint size;
  gchar temp;

  GST_DEBUG (0,"gst_colorspace_rgb24_to_bgr24 %p %p %d\n", src, dest, space->outsize);

  size = space->outsize/3;

  if (src == dest) {
    while (size--) {
      temp = src[0];
      src[0] = src[2];
      src[2] = temp;
      src+=3;
    }
  }
  else {
    while (size--) {
      *dest++ = src[2];
      *dest++ = src[1];
      *dest++ = src[0];
      src+=3;
    }
  }
  GST_DEBUG (0,"gst_colorspace_rgb24_to_bgr24 end\n");
}

static void gst_colorspace_rgb32_to_bgr32(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) 
{
  gint size;
  gchar temp;

  GST_DEBUG (0,"gst_colorspace_rgb32_to_bgr32 %p %p %d\n", src, dest, space->outsize);

  size = space->outsize/4;

  if (src == dest) {
    while (size--) {
      temp = src[0];
      src[0] = src[2];
      src[2] = temp;
      src+=4;
    }
  }
  else {
    while (size--) {
      *dest++ = src[2];
      *dest++ = src[1];
      *dest++ = src[0];
      dest++;
      src+=4;
    }
  }
  GST_DEBUG (0,"gst_colorspace_rgb32_to_bgr32 end\n");
}

static void gst_colorspace_rgb555_to_rgb565(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) 
{
  gint size;
  guint32 *srcptr = (guint32 *) src;
  guint32 *destptr = (guint32 *) dest;

  GST_DEBUG (0,"gst_colorspace_rgb555_to_rgb565 %p %p %d\n", src, dest, space->outsize);

  size = space->outsize/4;

  if (src == dest) {
    while (size--) {
      *srcptr += (*srcptr++)&0xFFE0FFE0;
    }
  }
  else {
    while (size--) {
      *destptr++ = *srcptr + ((*srcptr++)&0xFFE0FFE0);
    }
  }
}
