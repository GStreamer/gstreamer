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

static void gst_colorspace_rgb_to_rgb_identity(GstColorSpace *space, unsigned char *src, unsigned char *dest); 
static void gst_colorspace_rgb24_to_bgr24(GstColorSpace *space, unsigned char *src, unsigned char *dest);

GstColorSpaceConverter gst_colorspace_rgb2rgb_get_converter(GstColorSpace *space, GstColorSpaceType src, GstColorSpaceType dest) {
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
    default:
      break;
  }
  g_print("gst_colorspace: conversion not supported\n");
  return NULL;
}

static void gst_colorspace_rgb_to_rgb_identity(GstColorSpace *space, unsigned char *src, unsigned char *dest) 
{
  memcpy(dest, src, space->outsize);
}

static void gst_colorspace_rgb24_to_bgr24(GstColorSpace *space, unsigned char *src, unsigned char *dest) 
{
  gint size;
  gchar temp;

  DEBUG("gst_colorspace_rgb24_to_bgr24\n");

  size = space->outsize;

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
  DEBUG("gst_colorspace_rgb24_to_bgr24 end\n");
}

