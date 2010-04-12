/* GStreamer
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Collabora Multimedia
 * Copyright (C) 2010 Arun Raghavan <arun.raghavan@collabora.co.uk>
 *
 * gstaacutil.h: collection of AAC helper utilities
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

#ifndef __GST_AAC_UTIL_H__
#define __GST_AAC_UTIL_H__

#include <glib.h>

/* FIXME: This file is duplicated in gst-plugins-* wherever needed, so if you
 * update this file, please find all other instances and update them as well.
 * This less-than-optimal setup is being used till there is a standard location
 * for such common functionality.
 */

G_BEGIN_DECLS

gint    gst_aac_level_from_header (guint profile,
                                   guint sample_freq_idx,
                                   guint channel_config);

G_END_DECLS

#endif /* __GST_AAC_UTIL_H__*/
