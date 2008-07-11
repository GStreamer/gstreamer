/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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

#ifndef __RSN_WRAPPERBUFFER_H__
#define __RSN_WRAPPERBUFFER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define RSN_TYPE_WRAPPEDBUFFER (rsn_wrappedbuffer_get_type())
#define RSN_WRAPPEDBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RSN_TYPE_WRAPPEDBUFFER, \
   RsnWrappedBuffer))
#define RSN_WRAPPEDBUFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RSN_TYPE_WRAPPEDBUFFER, \
   RsnWrappedBufferClass))
#define RSN_IS_WRAPPEDBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RSN_TYPE_WRAPPEDBUFFER))
#define RSN_IS_WRAPPEDBUFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RSN_TYPE_WRAPPEDBUFFER))

typedef struct _RsnWrappedBuffer RsnWrappedBuffer;
typedef struct _RsnWrappedBufferClass RsnWrappedBufferClass;

typedef gboolean (*RsnWrappedBufferReleaseFunc)(GstElement *owner,
    RsnWrappedBuffer *buf);

struct _RsnWrappedBuffer {
  GstBuffer     buffer;
  GstBuffer    *wrapped_buffer;

  GstElement   *owner;
  RsnWrappedBufferReleaseFunc  release;
};

struct _RsnWrappedBufferClass 
{
  GstBufferClass parent_class;
};

RsnWrappedBuffer *rsn_wrapped_buffer_new (GstBuffer *buf_to_wrap);
GstBuffer *rsn_wrappedbuffer_unwrap_and_unref (RsnWrappedBuffer *wrap_buf);
void rsn_wrapped_buffer_set_owner (RsnWrappedBuffer *wrapped_buf,
    GstElement *owner);
void rsn_wrapped_buffer_set_releasefunc (RsnWrappedBuffer *wrapped_buf,
    RsnWrappedBufferReleaseFunc release_func);

GType rsn_wrappedbuffer_get_type (void);

G_END_DECLS

#endif
