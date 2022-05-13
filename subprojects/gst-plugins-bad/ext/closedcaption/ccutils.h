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
#include <gst/video/video.h>

#ifndef __CCUTILS_H__
#define __CCUTILS_H__

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN(ccutils_debug_cat);

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

typedef enum {
  GST_CC_CDP_MODE_TIME_CODE   = (1<<0),
  GST_CC_CDP_MODE_CC_DATA     = (1<<1),
  GST_CC_CDP_MODE_CC_SVC_INFO = (1<<2)
} GstCCCDPMode;

guint           convert_cea708_cc_data_to_cdp  (GstObject * dbg_obj,
                                                GstCCCDPMode cdp_mode,
                                                guint16 cdp_hdr_sequence_cntr,
                                                const guint8 * cc_data,
                                                guint cc_data_len,
                                                guint8 * cdp,
                                                guint cdp_len,
                                                const GstVideoTimeCode * tc,
                                                const struct cdp_fps_entry *fps_entry);

G_END_DECLS

#endif
