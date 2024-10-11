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

G_GNUC_INTERNAL
guint           convert_cea708_cc_data_to_cdp  (GstObject * dbg_obj,
                                                GstCCCDPMode cdp_mode,
                                                guint16 cdp_hdr_sequence_cntr,
                                                const guint8 * cc_data,
                                                guint cc_data_len,
                                                guint8 * cdp,
                                                guint cdp_len,
                                                const GstVideoTimeCode * tc,
                                                const struct cdp_fps_entry *fps_entry);

G_GNUC_INTERNAL
guint           convert_cea708_cdp_to_cc_data  (GstObject * dbg_obj,
                                                const guint8 * cdp,
                                                guint cdp_len,
                                                guint8 *cc_data,
                                                GstVideoTimeCode * tc,
                                                const struct cdp_fps_entry **out_fps_entry);
G_GNUC_INTERNAL
gint           drop_ccp_from_cc_data           (guint8 * cc_data,
                                                 guint cc_data_len);

#define MAX_CDP_PACKET_LEN 256
#define MAX_CEA608_LEN 32

/**
 * GstCCBufferCea608PaddingStrategy:
 * @CC_BUFFER_CEA608_PADDING_STRATEGY_INPUT_REMOVE: Keep whatever padding was provided on the input.
 *     Do not add, remove, or modify, any padding bytes.
 * @CC_BUFFER_CEA608_PADDING_STRATEGY_VALID: Always modify any padding data to become valid.
 *     This may cause a stream to show as a CEA-608 caption stream with no contents.
 *
 * Since: 1.26
 */
/**
 * @CC_BUFFER_CEA608_PADDING_STRATEGY_INPUT_REMOVE:
 * Since: 1.26
 */
/**
 * @CC_BUFFER_CEA608_PADDING_STRATEGY_VALID:
 * Since: 1.26
 */
typedef enum {
   CC_BUFFER_CEA608_PADDING_STRATEGY_INPUT_REMOVE = (1 << 0),
   CC_BUFFER_CEA608_PADDING_STRATEGY_VALID        = (1 << 1),
} CCBufferCea608PaddingStrategy;

#define GST_TYPE_CC_BUFFER_CEA608_PADDING_STRATEGY (gst_cc_buffer_cea608_padding_strategy_get_type())
GType gst_cc_buffer_cea608_padding_strategy_get_type (void);

G_DECLARE_FINAL_TYPE (CCBuffer, cc_buffer, GST, CC_BUFFER, GObject);

G_GNUC_INTERNAL
CCBuffer *      cc_buffer_new                   (void);
G_GNUC_INTERNAL
void            cc_buffer_get_stored_size       (CCBuffer * buf,
                                                 guint * cea608_1_len,
                                                 guint * cea608_2_len,
                                                 guint * cc_data_len);
G_GNUC_INTERNAL
gboolean        cc_buffer_push_separated        (CCBuffer * buf,
                                                 const guint8 * cea608_1,
                                                 guint cea608_1_len,
                                                 const guint8 * cea608_2,
                                                 guint cea608_2_len,
                                                 const guint8 * cc_data,
                                                 guint cc_data_len);
G_GNUC_INTERNAL
gboolean        cc_buffer_push_cc_data          (CCBuffer * buf,
                                                 const guint8 * cc_data,
                                                 guint cc_data_len);
G_GNUC_INTERNAL
void            cc_buffer_take_cc_data          (CCBuffer * buf,
                                                 const struct cdp_fps_entry * fps_entry,
                                                 guint8 * cc_data,
                                                 guint * cc_data_len);
G_GNUC_INTERNAL
void            cc_buffer_take_separated        (CCBuffer * buf,
                                                 const struct cdp_fps_entry * fps_entry,
                                                 guint8 * cea608_1,
                                                 guint * cea608_1_len,
                                                 guint8 * cea608_2,
                                                 guint * cea608_2_len,
                                                 guint8 * cc_data,
                                                 guint * cc_data_len);
G_GNUC_INTERNAL
void            cc_buffer_take_cea608_field1    (CCBuffer * buf,
                                                 const struct cdp_fps_entry * fps_entry,
                                                 guint8 * cea608_1,
                                                 guint * cea608_1_len);
G_GNUC_INTERNAL
void            cc_buffer_take_cea608_field2    (CCBuffer * buf,
                                                 const struct cdp_fps_entry * fps_entry,
                                                 guint8 * cea608_2,
                                                 guint * cea608_2_len);
G_GNUC_INTERNAL
gboolean        cc_buffer_is_empty              (CCBuffer * buf);
G_GNUC_INTERNAL
void            cc_buffer_discard               (CCBuffer * buf);
G_GNUC_INTERNAL
void            cc_buffer_set_max_buffer_time   (CCBuffer * buf,
                                                 GstClockTime max_time);
G_GNUC_INTERNAL
void            cc_buffer_set_output_padding    (CCBuffer * buf,
                                                 gboolean output_padding,
                                                 gboolean output_ccp_padding);
G_GNUC_INTERNAL
void            cc_buffer_set_cea608_padding_strategy(CCBuffer * buf,
                                                 CCBufferCea608PaddingStrategy padding_strategy);
G_GNUC_INTERNAL
void           cc_buffer_set_cea608_valid_timeout (CCBuffer * buf, GstClockTime valid_timeout);

G_END_DECLS

#endif
