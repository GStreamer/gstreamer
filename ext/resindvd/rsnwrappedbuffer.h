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

typedef struct _RsnMetaWrapped RsnMetaWrapped;

struct _RsnMetaWrapped {
  GstMeta       meta;
  GstBuffer    *wrapped_buffer;

  GstElement   *owner;
};

GstBuffer *rsn_wrapped_buffer_new (GstBuffer *buf_to_wrap, GstElement *owner);

GstBuffer *rsn_meta_wrapped_unwrap_and_unref (GstBuffer *wrap_buf, RsnMetaWrapped *meta);

void rsn_meta_wrapped_set_owner (RsnMetaWrapped *meta, GstElement *owner);

const GstMetaInfo * rsn_meta_wrapped_get_info (void);

#define RSN_META_WRAPPED_GET(buf,create) ((RsnMetaWrapped *)gst_buffer_get_meta(buf,rsn_meta_wrapped_get_info(),create))

G_END_DECLS

#endif
