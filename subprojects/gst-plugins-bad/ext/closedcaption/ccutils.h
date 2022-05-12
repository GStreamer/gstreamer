/*
 * GStreamer
 * Copyright (C) 2022 Matthew Waters <matthew@centricular.com>
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

#include <gst/gst.h>

#ifndef __CCUTILS_H__
#define __CCUTILS_H__

G_BEGIN_DECLS

struct cdp_fps_entry
{
  guint8 fps_idx;               /* value stored in cdp */
  guint fps_n, fps_d;
  guint max_cc_count;
  guint max_ccp_count;
  guint max_cea608_count;
};

G_GNUC_INTERNAL
const struct cdp_fps_entry * cdp_fps_entry_from_fps (guint fps_n, guint fps_d);
G_GNUC_INTERNAL
const struct cdp_fps_entry * cdp_fps_entry_from_id  (guint8 id);

extern const struct cdp_fps_entry null_fps_entry;

G_END_DECLS

#endif
