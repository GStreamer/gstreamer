/* Small helper element for format conversion
 * Copyright (C) 2005 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

#include <gst/gst.h>
#include <glib-object.h>
#include <string.h>
#include <gst/video/video.h>
#include "ges-screenshot.h"
#include "ges-internal.h"

GstBuffer *
ges_play_sink_convert_frame (GstElement * playsink, GstCaps * caps)
{
  GstBuffer *result = NULL;

  g_object_get (G_OBJECT (playsink), "frame", (GstMiniObject *) & result, NULL);

  GST_DEBUG ("got buffer %p from playsink", result);

  if (result != NULL && caps != NULL) {
    GstBuffer *temp;

    temp = gst_video_convert_frame (result, caps, 25 * GST_SECOND, NULL);
    gst_buffer_unref (result);
    result = temp;
  }
  return result;
}
