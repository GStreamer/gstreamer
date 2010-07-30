/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvdpbuffer.h"

static GObjectClass *gst_vdp_buffer_parent_class;

void
gst_vdp_buffer_set_buffer_pool (GstVdpBuffer * buffer, GstVdpBufferPool * bpool)
{
  g_return_if_fail (GST_IS_VDP_BUFFER (buffer));

  if (bpool) {
    g_return_if_fail (GST_IS_VDP_BUFFER_POOL (bpool));
    g_object_add_weak_pointer (G_OBJECT (bpool), (void **) &buffer->bpool);
  }

  buffer->bpool = bpool;
}

gboolean
gst_vdp_buffer_revive (GstVdpBuffer * buffer)
{
  if (buffer->bpool)
    return gst_vdp_buffer_pool_put_buffer (buffer->bpool, buffer);

  return FALSE;
}

static void
gst_vdp_buffer_init (GstVdpBuffer * buffer, gpointer g_class)
{
  buffer->bpool = NULL;
}

static void
gst_vdp_buffer_class_init (gpointer g_class, gpointer class_data)
{
  gst_vdp_buffer_parent_class = g_type_class_peek_parent (g_class);
}


GType
gst_vdp_buffer_get_type (void)
{
  static GType _gst_vdp_buffer_type;

  if (G_UNLIKELY (_gst_vdp_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_vdp_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstVdpBuffer),
      0,
      (GInstanceInitFunc) gst_vdp_buffer_init,
      NULL
    };
    _gst_vdp_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstVdpBuffer", &info, 0);
  }
  return _gst_vdp_buffer_type;
}
