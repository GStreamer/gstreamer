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


extern GstColorSpaceConvertFunction gst_colorspace_rgb2rgb_get_converter(GstColorSpaceConverter *space, GstColorSpaceType srcspace, 
		                                                      GstColorSpaceType destspace);
extern GstColorSpaceConvertFunction gst_colorspace_yuv2rgb_get_converter(GstColorSpaceConverter *space, GstColorSpaceType srcspace, 
		                                                      GstColorSpaceType destspace);
extern GstColorSpaceConvertFunction gst_colorspace_rgb2yuv_get_converter(GstColorSpaceConverter *space, GstColorSpaceType srcspace, 
		                                                      GstColorSpaceType destspace);
extern GstColorSpaceConvertFunction gst_colorspace_yuv2yuv_get_converter(GstColorSpaceConverter *space, GstColorSpaceType srcspace, 
		                                                      GstColorSpaceType destspace);

GstColorSpaceConverter *gst_colorspace_converter_new(gint width, gint height, GstColorSpaceType srcspace, 
		GstColorSpaceType destspace, GdkVisual *destvisual) 
{

  GstColorSpaceConverter *new = g_malloc(sizeof(GstColorSpaceConverter));

  new->width = width;
  new->height = height;
  new->srcspace = srcspace;
  new->destspace = destspace;
  new->visual = destvisual;
  new->color_tables = NULL;
  new->convert = NULL;

  DEBUG("gst_colorspace: new\n");
  if (GST_COLORSPACE_IS_RGB_TYPE(srcspace)) {
    if (GST_COLORSPACE_IS_RGB_TYPE(destspace)) {
      new->convert =  gst_colorspace_rgb2rgb_get_converter(new, srcspace, destspace);
    }
    else {
      //return gst_colorspace_rgb2yuv_get_converter(srcspace, destspace);
    }
  }
  else if (GST_COLORSPACE_IS_YUV_TYPE(srcspace)) {
    if (GST_COLORSPACE_IS_RGB_TYPE(destspace)) {
      new->convert =  gst_colorspace_yuv2rgb_get_converter(new, srcspace, destspace);
    }
    else {
      //return gst_colorspace_yuv2yuv_get_converter(srcspace, destspace);
    }
  }
  if (new->convert == NULL) {
    g_print("gst_colorspace: conversion not implemented\n");
    g_free(new);
    new = NULL;
  }
  return new;
}

void gst_colorspace_destroy(GstColorSpaceConverter *space) 
{
  if (space->color_tables) g_free(space->color_tables);
  g_free(space);
}
