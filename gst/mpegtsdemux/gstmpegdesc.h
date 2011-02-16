/* 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU Lesser General Public License Version 2 or later (the "LGPL"),
 * in which case the provisions of the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of the MPL or the LGPL.
 */

#ifndef __GST_MPEG_DESC_H__
#define __GST_MPEG_DESC_H__

#include <glib.h>
/*
 * descriptor_tag TS  PS                      Identification
 *        0       n/a n/a Reserved
 *        1       n/a n/a Reserved
 *        2        X   X  video_stream_descriptor
 *        3        X   X  audio_stream_descriptor
 *        4        X   X  hierarchy_descriptor
 *        5        X   X  registration_descriptor
 *        6        X   X  data_stream_alignment_descriptor
 *        7        X   X  target_background_grid_descriptor
 *        8        X   X  video_window_descriptor
 *        9        X   X  CA_descriptor
 *       10        X   X  ISO_639_language_descriptor
 *       11        X   X  system_clock_descriptor
 *       12        X   X  multiplex_buffer_utilization_descriptor
 *       13        X   X  copyright_descriptor
 *       14        X      maximum bitrate descriptor
 *       15        X   X  private data indicator descriptor
 *       16        X   X  smoothing buffer descriptor
 *       17        X      STD_descriptor
 *       18        X   X  IBP descriptor
 *      19-63     n/a n/a ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
 *     64-255     n/a n/a User Private
 */
#define DESC_VIDEO_STREAM			2
#define DESC_AUDIO_STREAM			3
#define DESC_HIERARCHY				4
#define DESC_REGISTRATION			5
#define DESC_DATA_STREAM_ALIGNMENT		6
#define DESC_TARGET_BACKGROUND_GRID		7
#define DESC_VIDEO_WINDOW			8
#define DESC_CA					9
#define DESC_ISO_639_LANGUAGE			10
#define DESC_SYSTEM_CLOCK			11
#define DESC_MULTIPLEX_BUFFER_UTILISATION	12
#define DESC_COPYRIGHT				13
#define DESC_MAXIMUM_BITRATE			14
#define DESC_PRIVATE_DATA_INDICATOR		15
#define DESC_SMOOTHING_BUFFER			16
#define DESC_STD				17
#define DESC_IBP				18

#define DESC_DIRAC_TC_PRIVATE			0xAC

/* DVB tags */
#define DESC_DVB_CAROUSEL_IDENTIFIER	0x13
#define DESC_DVB_NETWORK_NAME   0x40
#define DESC_DVB_SERVICE_LIST   0x41
#define DESC_DVB_STUFFING       0x42
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM    0x43
#define DESC_DVB_CABLE_DELIVERY_SYSTEM    0x44
#define DESC_DVB_VBI_DATA       0x45
#define DESC_DVB_VBI_TELETEXT   0x46
#define DESC_DVB_BOUQUET_NAME   0x47
#define DESC_DVB_SERVICE        0x48
#define DESC_DVB_COUNTRY_AVAILABILITY   0x49
#define DESC_DVB_LINKAGE        0x4A
#define DESC_DVB_NVOD_REFERENCE 0x4B
#define DESC_DVB_TIME_SHIFTED_SERVICE   0x4C
#define DESC_DVB_SHORT_EVENT    0x4D
#define DESC_DVB_EXTENDED_EVENT 0x4E
#define DESC_DVB_TIME_SHIFTED_EVENT   0x4F
#define DESC_DVB_COMPONENT      0x50
#define DESC_DVB_MOSAIC         0x51
#define DESC_DVB_STREAM_IDENTIFIER    0x52
#define DESC_DVB_CA_IDENTIFIER  0x53
#define DESC_DVB_CONTENT        0x54
#define DESC_DVB_PARENTAL_RATING    0x55
#define DESC_DVB_TELETEXT       0x56
#define DESC_DVB_TELEPHONE      0x57
#define DESC_DVB_LOCAL_TIME_OFFSET  0x58
#define DESC_DVB_SUBTITLING     0x59
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM  0x5A
#define DESC_DVB_MULTILINGUAL_NETWORK_NAME    0x5B
#define DESC_DVB_MULTILINGUAL_BOUQUET_NAME    0x5C
#define DESC_DVB_MULTILINGUAL_SERVICE_NAME    0x5D
#define DESC_DVB_MULTILINGUAL_COMPONENT   0x5E
#define DESC_DVB_PRIVATE_DATA   0x5F
#define DESC_DVB_SERVICE_MOVE   0x60
#define DESC_DVB_SHORT_SMOOTHING_BUFFER   0x61
#define DESC_DVB_FREQUENCY_LIST 0x62
#define DESC_DVB_PARTIAL_TRANSPORT_STREAM   0x63
#define DESC_DVB_DATA_BROADCAST 0x64
#define DESC_DVB_SCRAMBLING     0x65
#define DESC_DVB_DATA_BROADCAST_ID    0x66
#define DESC_DVB_TRANSPORT_STREAM   0x67
#define DESC_DVB_DSNG           0x68
#define DESC_DVB_PDC            0x69
#define DESC_DVB_AC3            0x6A
#define DESC_DVB_ANCILLARY_DATA 0x6B
#define DESC_DVB_CELL_LIST          0x6C
#define DESC_DVB_CELL_FREQUENCY_LINK    0x6D
#define DESC_DVB_ANNOUNCEMENT_SUPPORT   0x6E
#define DESC_DVB_APPLICATION_SIGNALLING   0x6F
#define DESC_DVB_ADAPTATION_FIELD_DATA    0x70
#define DESC_DVB_SERVICE_IDENTIFIER   0x71
#define DESC_DVB_SERVICE_AVAILABILITY   0x72
#define DESC_DVB_DEFAULT_AUTHORITY    0x73
#define DESC_DVB_RELATED_CONTENT    0x74
#define DESC_DVB_TVA_ID         0x75
#define DESC_DVB_CONTENT_IDENTIFIER   0x76
#define DESC_DVB_TIMESLICE_FEC_IDENTIFIER   0x77
#define DESC_DVB_ECM_REPETITION_RATE    0x78
#define DESC_DVB_S2_SATELLITE_DELIVERY_SYSTEM   0x79
#define DESC_DVB_ENHANCED_AC3   0x7A
#define DESC_DVB_DTS            0x7B
#define DESC_DVB_AAC            0x7C
/* 0x7D and 0x7E are reserved for future use */
#define DESC_DVB_EXTENSION      0x7F
/* 0x80 - 0xFE are user defined */
#define DESC_DTG_LOGICAL_CHANNEL	0x83 /* from DTG D-Book */
/* 0xFF is forbidden */

/* common for all descriptors */
#define DESC_TAG(desc) 		(desc[0])
#define DESC_LENGTH(desc) 	(desc[1])

/* video_stream_descriptor */
#define DESC_VIDEO_STREAM_multiple_framerate_flag(desc) 	(((desc)[2] & 0x80) == 0x80)
#define DESC_VIDEO_STREAM_frame_rate_code(desc) 		(((desc)[2] & 0x38) >> 3)
#define DESC_VIDEO_STREAM_MPEG_1_only_flag(desc) 		(((desc)[2] & 0x04) == 0x04)
#define DESC_VIDEO_STREAM_constrained_parameter_flag(desc) 	(((desc)[2] & 0x02) == 0x02)
#define DESC_VIDEO_STREAM_still_picture_flag(desc) 	 	(((desc)[2] & 0x01) == 0x01)
/* if (MPEG_1_only_flag == 1) */
#define DESC_VIDEO_STREAM_profile_and_level_indication(desc)	((desc)[3])
#define DESC_VIDEO_STREAM_chroma_format(desc)			(((desc)[4] & 0xc0) >> 6)
#define DESC_VIDEO_STREAM_frame_rate_extension_flag(desc)	(((desc)[4] & 0x20) == 0x20)

/* audio_stream_descriptor */
#define DESC_AUDIO_STREAM_free_format_flag(desc) 		(((desc)[2] & 0x80) == 0x80)
#define DESC_AUDIO_STREAM_ID(desc) 				(((desc)[2] & 0x40) == 0x40)
#define DESC_AUDIO_STREAM_layer(desc) 				(((desc)[2] & 0x30) >> 4)
#define DESC_AUDIO_STREAM_variable_rate_audio_indicator(desc) 	(((desc)[2] & 0x08) == 0x08)

/* hierarchy_descriptor */
#define DESC_HIERARCHY_hierarchy_type(desc)			(((desc)[2] & 0x0f))
#define DESC_HIERARCHY_hierarchy_layer_index(desc)		(((desc)[3] & 0x3f))
#define DESC_HIERARCHY_hierarchy_embedded_layer_index(desc)	(((desc)[4] & 0x3f))
#define DESC_HIERARCHY_hierarchy_channel(desc)			(((desc)[5] & 0x3f))

/* registration_descriptor */
#define DESC_REGISTRATION_format_identifier(desc)		(GST_READ_UINT32_BE ((desc)+2))
#define DESC_REGISTRATION_additional_ident_info_len(desc)	((desc)[1] - 4)
#define DESC_REGISTRATION_additional_ident_info(desc)		(&(desc)[6])

/* data_stream_alignment_descriptor */
#define DESC_DATA_STREAM_ALIGNMENT_alignment_type(desc)		((desc)[2])

/* target_background_grid_descriptor */
#define DESC_TARGET_BACKGROUND_GRID_horizontal_size(desc)	(GST_READ_UINT16_BE ((desc)+2) >> 2)
#define DESC_TARGET_BACKGROUND_GRID_vertical_size(desc)		((GST_READ_UINT32_BE ((desc)+2) & 0x0003fff0) >> 4)
#define DESC_TARGET_BACKGROUND_GRID_aspect_ratio_information(desc) ((desc)[5] & 0x0f)

/* video_window_descriptor */
#define DESC_VIDEO_WINDOW_horizontal_offset(desc)		(GST_READ_UINT16_BE ((desc)+2) >> 2)
#define DESC_VIDEO_WINDOW_vertical_offset(desc)			((GST_READ_UINT32_BE ((desc)+2) & 0x0003fff0) >> 4)
#define DESC_VIDEO_WINDOW_window_priority(desc)	 		((desc)[5] & 0x0f)

/* CA_descriptor */
#define DESC_CA_system_ID(desc)					(GST_READ_UINT16_BE ((desc)+2))
#define DESC_CA_PID(desc)					(GST_READ_UINT16_BE ((desc)+2) & 0x1fff)

/* ISO_639_language_descriptor */
#define DESC_ISO_639_LANGUAGE_codes_n(desc)			((desc[1]) >> 2)
#define DESC_ISO_639_LANGUAGE_language_code_nth(desc,i)		(&(desc[2 + (4*i)]))
#define DESC_ISO_639_LANGUAGE_audio_type_nth(desc,i)		((desc)[5 + (4*i)])

/* system_clock_descriptor */ 
#define DESC_SYSTEM_CLOCK_external_clock_reference_indicator(desc) (((desc)[2] & 0x80) == 0x80)
#define DESC_SYSTEM_CLOCK_clock_accuracy_integer(desc)		((desc)[2] & 0x3f)
#define DESC_SYSTEM_CLOCK_clock_accuracy_exponent(desc)		(((desc)[3] & 0xe0) >> 5)

/* multiplex_buffer_utilization_descriptor */
#define DESC_MULTIPLEX_BUFFER_UTILISATION_bound_valid_flag(desc)	(((desc)[2] & 0x80) == 0x80)
#define DESC_MULTIPLEX_BUFFER_UTILISATION_LTW_offset_lower_bound(desc)	(GST_READ_UINT16_BE ((desc)+2) & 0x7fff)
#define DESC_MULTIPLEX_BUFFER_UTILISATION_LTW_offset_upper_bound(desc)	(GST_READ_UINT16_BE ((desc)+4) & 0x7fff)

/* copyright_descriptor */
#define DESC_COPYRIGHT_copyright_identifier(desc)		(GST_READ_UINT32_BE ((desc)+2))
#define DESC_COPYRIGHT_additional_copyright_info_len(desc)	((desc)[1] - 4)
#define DESC_COPYRIGHT_additional_copyright_info(desc)		(&(desc)[6])

/* maximum_bitrate_descriptor */
#define DESC_MAXIMUM_BITRAT_maximum_bitrate(desc)		(((((guint32)desc[2]) & 0x3f) << 16) | \
								 GST_READ_UINT16_BE ((desc)+3)) 

/* private_data_indicator_descriptor */
#define DESC_PRIVATE_DATA_INDICATOR_indicator(desc)		(GST_READ_UINT32_BE(&desc[2]))

/* smoothing_buffer_descriptor */
#define DESC_SMOOTHING_BUFFER_sb_leak_rate(desc)		(((((guint32)desc[2]) & 0x3f) << 16) | \
                                                                 GST_READ_UINT16_BE ((desc)+3))
#define DESC_SMOOTHING_BUFFER_sb_size(desc)			(((((guint32)desc[5]) & 0x3f) << 16) | \
                                                                 GST_READ_UINT16_BE ((desc)+6))
/* STD_descriptor */
#define DESC_STD_leak_valid_flag(desc)				(((desc)[2] & 0x01) == 0x01)

/* ibp_descriptor */
#define DESC_IBP_closed_gop_flag(desc)				(((desc)[2] & 0x80) == 0x80)
#define DESC_IBP_identical_gop_flag(desc)			(((desc)[2] & 0x40) == 0x40)
#define DESC_IBP_max_gop_length(desc)				(GST_READ_UINT16_BE ((desc)+6) & 0x3fff)

/* time_code descriptor */
#define DESC_TIMECODE_video_pid(desc)                           (GST_READ_UINT16_BE ((desc) + 2) & 0x1fff)

/* Stream identifier descriptor */
#define DESC_DVB_STREAM_IDENTIFIER_component_tag(desc)  (desc[2])

/* DVB Network Name descriptor */
#define DESC_DVB_NETWORK_NAME_length(desc)  (GST_READ_UINT8((desc)+1))
#define DESC_DVB_NETWORK_NAME_text(desc)    (desc+2)

/* DVB Service Descriptor */
#define DESC_DVB_SERVICE_type(desc) (desc[2])
#define DESC_DVB_SERVICE_provider_name_length(desc) (desc[3])
#define DESC_DVB_SERVICE_provider_name_text(desc)   (desc+4)
#define DESC_DVB_SERVICE_name_length(desc)  (desc[4 + DESC_DVB_SERVICE_provider_name_length(desc)])
#define DESC_DVB_SERVICE_name_text(desc)    (desc + 5 + DESC_DVB_SERVICE_provider_name_length(desc))

/* DVB Component Descriptor */
#define DESC_DVB_COMPONENT_stream_content(desc) (desc[2] & 0x0F)
#define DESC_DVB_COMPONENT_type(desc)   (desc[3])
#define DESC_DVB_COMPONENT_tag(desc)    (desc[4])
#define DESC_DVB_COMPONENT_language(desc)   (desc + 5)

/* DVB Bouquet Name Descriptor */
#define DESC_DVB_BOUQUET_NAME_text(desc)    (desc + 2)

/* DVB Short Event Descriptor */
#define DESC_DVB_SHORT_EVENT_name_text(desc)	(desc + 6)
#define DESC_DVB_SHORT_EVENT_name_length(desc)	(desc[5])
#define DESC_DVB_SHORT_EVENT_description_text(desc) (desc + 6 + DESC_DVB_SHORT_EVENT_name_length(desc) + 1)
#define DESC_DVB_SHORT_EVENT_description_length(desc)	(desc[6 + DESC_DVB_SHORT_EVENT_name_length(desc)])

/* DVB Extended Event Descriptor */
#define DESC_DVB_EXTENDED_EVENT_descriptor_number(desc) ((desc[2] & 0xF0) >> 4)
#define DESC_DVB_EXTENDED_EVENT_last_descriptor_number(desc) (desc[2] & 0x0F)
#define DESC_DVB_EXTENDED_EVENT_iso639_language_code(desc) (desc + 3)
#define DESC_DVB_EXTENDED_EVENT_items_length(desc) (desc[6])
#define DESC_DVB_EXTENDED_EVENT_items(desc) (desc + 7) 
#define DESC_DVB_EXTENDED_EVENT_text_length(desc) (desc[7 + DESC_DVB_EXTENDED_EVENT_items_length(desc)])
#define DESC_DVB_EXTENDED_EVENT_text(desc) (desc + 7 + DESC_DVB_EXTENDED_EVENT_items_length(desc) + 1)

/* DVB Satellite Delivery System Descriptor */
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_frequency(desc)	(desc + 2)
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_orbital_position(desc)	(desc + 6)
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_west_east_flag(desc)	((desc[8] & 0x80) == 0x80)
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_polarization(desc)	((desc[8] >> 5) & 0x3)
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_modulation(desc)	(desc[8] & 0x1F)
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_symbol_rate(desc)	(desc + 9)
#define DESC_DVB_SATELLITE_DELIVERY_SYSTEM_fec_inner(desc)	(desc[12] & 0x0F)

/* DVB Terrestrial Delivery System Descriptor */
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_frequency(desc)	(GST_READ_UINT32_BE((desc) + 2))
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_bandwidth(desc)	((desc[6] >> 5) & 0x7)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_constellation(desc)	((desc[7] >> 6) & 0x3)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_hierarchy(desc)	((desc[7] >> 3) & 0x7)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_code_rate_hp(desc)	(desc[7] & 0x7)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_code_rate_lp(desc)	((desc[8] >> 5) & 0x7)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_guard_interval(desc)	((desc[8] >> 3) & 0x3)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_transmission_mode(desc)	((desc[8] >> 1) & 0x3)
#define DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_other_frequency(desc)	((desc[8] & 0x01) == 0x01)

/* DVB Cable Delivery System Descriptor */
#define DESC_DVB_CABLE_DELIVERY_SYSTEM_frequency(desc)		(desc + 2)
#define DESC_DVB_CABLE_DELIVERY_SYSTEM_fec_outer(desc)		(desc[7] & 0x0F)
#define DESC_DVB_CABLE_DELIVERY_SYSTEM_modulation(desc)		(desc[8])
#define DESC_DVB_CABLE_DELIVERY_SYSTEM_symbol_rate(desc)	(desc + 9)
#define DESC_DVB_CABLE_DELIVERY_SYSTEM_fec_inner(desc)		(desc[12] & 0x0F)

/* DVB Data Broadcast Descriptor */
#define DESC_DVB_DATA_BROADCAST_data_broadcast_id(desc)		(GST_READ_UINT16_BE((desc) + 2))
#define DESC_DVB_DATA_BROADCAST_component_tag(desc)		(desc[4])
#define DESC_DVB_DATA_BROADCAST_selector_length(desc)		(desc[5])
#define DESC_DVB_DATA_BROADCAST_selector(desc)			(desc + 6)
#define DESC_DVB_DATA_BROADCAST_iso639_language_code(desc)	(desc + 6 + DESC_DVB_DATA_BROADCAST_selector_length(desc))
#define DESC_DVB_DATA_BROADCAST_text_length(desc)		(desc + 9 + DESC_DVB_DATA_BROADCAST_selector_length(desc))
#define DESC_DVB_DATA_BROADCAST_text(desc)			(desc + 10 + DESC_DVB_DATA_BROADCAST_selector_length(desc))

/* DVB Data Broadcast Id Descriptor */
#define DESC_DVB_DATA_BROADCAST_ID_data_broadcast_id(desc)	(GST_READ_UINT16_BE((desc) + 2))
#define DESC_DVB_DATA_BROADCAST_ID_id_selector_byte(desc)	(desc + 4)

/* DVB Carousel Identifier Descriptor */
#define DESC_DVB_CAROUSEL_IDENTIFIER_carousel_id(desc)		(GST_READ_UINT32_BE((desc) + 2))

/* registration_descriptor format IDs */
#define DRF_ID_HDMV       0x48444d56
#define DRF_ID_VC1        0x56432D31   /* defined in RP227 */

typedef struct {
  guint    n_desc;
  guint8   data_length;
  guint8  *data;
} GstMPEGDescriptor;

void gst_mpegtsdesc_init_debug (void);
GstMPEGDescriptor* 	gst_mpeg_descriptor_parse 	(guint8 *data, guint size);
void		 	gst_mpeg_descriptor_free 	(GstMPEGDescriptor *desc);

guint 			gst_mpeg_descriptor_n_desc	(GstMPEGDescriptor *desc);
guint8*			gst_mpeg_descriptor_find	(GstMPEGDescriptor *desc, gint tag);
GArray*         gst_mpeg_descriptor_find_all (GstMPEGDescriptor * desc, gint tag);

guint8*			gst_mpeg_descriptor_nth		(GstMPEGDescriptor *desc, guint i);

#endif /* __GST_MPEG_DESC_H__ */
