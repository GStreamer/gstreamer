/* GStreamer
 * Copyright (C) 2022 GStreamer developers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_MSS_PRELUDE_H__
#define __GST_MSS_PRELUDE_H__

#define gst_mss_manifest_new gst_mss2_manifest_new
#define gst_mss_manifest_free gst_mss2_manifest_free
#define gst_mss_manifest_get_protection_system_id gst_mss2_manifest_get_protection_system_id
#define gst_mss_manifest_get_protection_data gst_mss2_manifest_get_protection_data
#define gst_mss_manifest_get_streams gst_mss2_manifest_get_streams
#define gst_mss_manifest_get_timescale gst_mss2_manifest_get_timescale
#define gst_mss_manifest_get_duration gst_mss2_manifest_get_duration
#define gst_mss_manifest_get_gst_duration gst_mss2_manifest_get_gst_duration
#define gst_mss_manifest_get_min_fragment_duration gst_mss2_manifest_get_min_fragment_duration
#define gst_mss_manifest_seek gst_mss2_manifest_seek
#define gst_mss_manifest_live_adapter_clear gst_mss2_manifest_live_adapter_clear
#define gst_mss_manifest_get_current_bitrate gst_mss2_manifest_get_current_bitrate
#define gst_mss_manifest_is_live gst_mss2_manifest_is_live
#define gst_mss_manifest_reload_fragments gst_mss2_manifest_reload_fragments
#define gst_mss_manifest_change_bitrate gst_mss2_manifest_change_bitrate
#define gst_mss_manifest_get_live_seek_range gst_mss2_manifest_get_live_seek_range
#define gst_mss_manifest_live_adapter_push gst_mss2_manifest_live_adapter_push
#define gst_mss_manifest_live_adapter_available gst_mss2_manifest_live_adapter_available
#define gst_mss_manifest_live_adapter_take_buffer gst_mss2_manifest_live_adapter_take_buffer
#define gst_mss_stream_get_type gst_mss2_stream_get_type
#define gst_mss_stream_set_active gst_mss2_stream_set_active
#define gst_mss_stream_get_timescale gst_mss2_stream_get_timescale
#define gst_mss_stream_get_fragment_gst_duration gst_mss2_stream_get_fragment_gst_duration
#define gst_mss_stream_get_caps gst_mss2_stream_get_caps
#define gst_mss_stream_get_fragment_url gst_mss2_stream_get_fragment_url
#define gst_mss_stream_get_fragment_gst_timestamp gst_mss2_stream_get_fragment_gst_timestamp
#define gst_mss_stream_has_next_fragment gst_mss2_stream_has_next_fragment
#define gst_mss_stream_advance_fragment gst_mss2_stream_advance_fragment
#define gst_mss_stream_type_name gst_mss2_stream_type_name
#define gst_mss_stream_regress_fragment gst_mss2_stream_regress_fragment
#define gst_mss_stream_seek gst_mss2_stream_seek
#define gst_mss_stream_select_bitrate gst_mss2_stream_select_bitrate
#define gst_mss_stream_get_current_bitrate gst_mss2_stream_get_current_bitrate
#define gst_mss_stream_get_lang gst_mss2_stream_get_lang
#define gst_mss_stream_fragment_parsing_needed gst_mss2_stream_fragment_parsing_needed
#define gst_mss_stream_parse_fragment gst_mss2_stream_parse_fragment
#define gst_mss_fragment_parser_add_buffer gst_mss2_fragment_parser_add_buffer
#define gst_mss_fragment_parser_clear gst_mss2_fragment_parser_clear
#define gst_mss_fragment_parser_init gst_mss2_fragment_parser_init

#endif /* __GST_MSS_PRELUDE_H__ */
