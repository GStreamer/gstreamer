/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org> 
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

#include "gstrtpbuffer.h"

static void gst_rtpbuffer_init (GTypeInstance * instance, gpointer g_class);
static void gst_rtpbuffer_class_init (gpointer g_class, gpointer class_data);
static void gst_rtpbuffer_finalize (GstRTPBuffer * nbuf);
static GstRTPBuffer *gst_rtpbuffer_copy (GstRTPBuffer * nbuf);

static GstBufferClass *parent_class;

GType
gst_rtpbuffer_get_type (void)
{
  static GType _gst_rtpbuffer_type = 0;

  if (G_UNLIKELY (_gst_rtpbuffer_type == 0)) {
    static const GTypeInfo rtpbuffer_info = {
      sizeof (GstRTPBufferClass),
      NULL,
      NULL,
      gst_rtpbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstRTPBuffer),
      0,
      gst_rtpbuffer_init,
      NULL
    };

    _gst_rtpbuffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstRTPBuffer", &rtpbuffer_info, 0);
  }
  return _gst_rtpbuffer_type;
}

static void
gst_rtpbuffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mo_class = GST_MINI_OBJECT_CLASS (g_class);

  parent_class = g_type_class_ref (GST_TYPE_BUFFER);

  mo_class->copy = (GstMiniObjectCopyFunction) gst_rtpbuffer_copy;
  mo_class->finalize = (GstMiniObjectFinalizeFunction) gst_rtpbuffer_finalize;
}

static void
gst_rtpbuffer_init (GTypeInstance * instance, gpointer g_class)
{
}

static void
gst_rtpbuffer_finalize (GstRTPBuffer * nbuf)
{
  GST_MINI_OBJECT_CLASS (parent_class)->finalize (GST_MINI_OBJECT (nbuf));
}

static GstRTPBuffer *
gst_rtpbuffer_copy (GstRTPBuffer * nbuf)
{
  GstRTPBuffer *copy;

  copy =
      (GstRTPBuffer *) GST_MINI_OBJECT_CLASS (parent_class)->
      copy (GST_MINI_OBJECT (nbuf));

  copy->pt = nbuf->pt;
  copy->seqnum = nbuf->seqnum;
  copy->timestamp = nbuf->timestamp;
  copy->timestampinc = nbuf->timestampinc;
  copy->mark = nbuf->mark;

  return copy;
}

GstRTPBuffer *
gst_rtpbuffer_new (void)
{
  GstRTPBuffer *buf;

  buf = (GstRTPBuffer *) gst_mini_object_new (GST_TYPE_RTPBUFFER);

  return buf;
}
