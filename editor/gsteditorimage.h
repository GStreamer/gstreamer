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


#ifndef __GST_EDITOR_IMAGE_H__
#define __GST_EDITOR_IMAGE_H__

#include <gst/gst.h>

typedef enum 
{
  GST_EDITOR_IMAGE_BIN,
  GST_EDITOR_IMAGE_PIPELINE,
  GST_EDITOR_IMAGE_THREAD,

  GST_EDITOR_IMAGE_ELEMENT,
  GST_EDITOR_IMAGE_SINK,
  GST_EDITOR_IMAGE_SOURCE,
  GST_EDITOR_IMAGE_CONNECTION,
  GST_EDITOR_IMAGE_FILTER,
  GST_EDITOR_IMAGE_TEE,
} GstEditorImageType;

typedef struct _GstEditorImage GstEditorImage;

struct _GstEditorImage {
  GdkPixmap *pixmap;
  GdkBitmap *bitmap;
};


GstEditorImage *gst_editor_image_get(GstEditorImageType type);
GstEditorImage *gst_editor_image_get_for_type(GtkType type);


#endif /* __GST_EDITOR_IMAGE_H__ */
