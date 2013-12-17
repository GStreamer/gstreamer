/* GStreamer libsndfile plugin
 * Copyright (C) 2003 Andy Wingo <wingo at pobox dot com>
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
 */


#ifndef __GST_SFSINK_H__
#define __GST_SFSINK_H__


#include <gst/gst.h>
#include <sndfile.h>


G_BEGIN_DECLS

GstCaps *gst_sf_create_audio_template_caps (void);

#define GST_TYPE_SF_MAJOR_TYPES (gst_sf_major_types_get_type())
#define GST_TYPE_SF_MINOR_TYPES (gst_sf_minor_types_get_type())

GType gst_sf_major_types_get_type (void);
GType gst_sf_minor_types_get_type (void);

GType gst_sf_dec_get_type (void);

G_END_DECLS


#endif /* __GST_SFSINK_H__ */
