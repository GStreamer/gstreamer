/*
 * Copyright (C) 2014 Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef __V4L2_UTILS_H__
#define __V4L2_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstV4l2Iterator GstV4l2Iterator;

struct _GstV4l2Iterator
{
    const gchar *device_path;
    const gchar *device_name;
    const gchar *sys_path;
};

GstV4l2Iterator *  gst_v4l2_iterator_new (void);
gboolean           gst_v4l2_iterator_next (GstV4l2Iterator *it);
void               gst_v4l2_iterator_free (GstV4l2Iterator *it);

const gchar *      gst_v4l2_iterator_get_device_path (GstV4l2Iterator *it);
const gchar *      gst_v4l2_iterator_get_device_name (GstV4l2Iterator *it);
const gchar *      gst_v4l2_iterator_get_sys_path (GstV4l2Iterator *it);

G_END_DECLS

#endif /* __V4L2_UTILS_H__ */


