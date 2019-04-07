/* GStreamer
 * Copyright (C) <2018> Edward Hervey <edward@centricular.com>
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

#ifndef __GST_VIDEO_ANC_H__
#define __GST_VIDEO_ANC_H__

#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>

G_BEGIN_DECLS

typedef struct _GstVideoAncillary GstVideoAncillary;

/**
 * GstVideoAncillary:
 * @DID: The Data Identifier
 * @SDID_block_number: The Secondary Data Identifier (if type 2) or the Data
 *                     Block Number (if type 1)
 * @data_count: The amount of data (in bytes) in @data (max 255 bytes)
 * @data: (array length=data_count): The user data content of the Ancillary packet.
 *    Does not contain the ADF, DID, SDID nor CS.
 *
 * Video Ancillary data, according to SMPTE-291M specification.
 *
 * Note that the contents of the data are always stored as 8bit data (i.e. do not contain
 * the parity check bits).
 *
 * Since: 1.16
 */
struct _GstVideoAncillary {
  guint8 DID;
  guint8 SDID_block_number;
  guint8 data_count;
  guint8 data[256];

  /*< private >*/
  /* Padding for future extension */
  gpointer _gst_reserved[GST_PADDING];
};

typedef enum {
  GST_VIDEO_ANCILLARY_DID_UNDEFINED = 0x00,
  GST_VIDEO_ANCILLARY_DID_DELETION  = 0x80,
  GST_VIDEO_ANCILLARY_DID_HANC_3G_AUDIO_DATA_FIRST = 0xa0,
  GST_VIDEO_ANCILLARY_DID_HANC_3G_AUDIO_DATA_LAST = 0xa7,
  GST_VIDEO_ANCILLARY_DID_HANC_HDTV_AUDIO_DATA_FIRST = 0xe0,
  GST_VIDEO_ANCILLARY_DID_HANC_HDTV_AUDIO_DATA_LAST = 0xe7,
  GST_VIDEO_ANCILLARY_DID_HANC_SDTV_AUDIO_DATA_1_FIRST = 0xec,
  GST_VIDEO_ANCILLARY_DID_HANC_SDTV_AUDIO_DATA_1_LAST = 0xef,
  GST_VIDEO_ANCILLARY_DID_CAMERA_POSITION = 0xf0,
  GST_VIDEO_ANCILLARY_DID_HANC_ERROR_DETECTION = 0xf4,
  GST_VIDEO_ANCILLARY_DID_HANC_SDTV_AUDIO_DATA_2_FIRST = 0xf8,
  GST_VIDEO_ANCILLARY_DID_HANC_SDTV_AUDIO_DATA_2_LAST = 0xff,
} GstVideoAncillaryDID;

/**
 * GST_VIDEO_ANCILLARY_DID16:
 * @anc: a #GstVideoAncillary
 *
 * Returns the #GstVideoAncillaryDID16 of the ancillary data.
 *
 * Since: 1.16
 *
 * Returns: a #GstVideoAncillaryDID16 identifier
 */
#define GST_VIDEO_ANCILLARY_DID16(anc) ((guint16)((anc)->DID) << 8 | (guint16)((anc)->SDID_block_number))

/**
 * GstVideoAncillaryDID16:
 * @GST_VIDEO_ANCILLARY_DID16_S334_EIA_708: CEA 708 Ancillary data according to SMPTE 334
 * @GST_VIDEO_ANCILLARY_DID16_S334_EIA_608: CEA 608 Ancillary data according to SMPTE 334
 *
 * Some know types of Ancillary Data identifiers.
 *
 * Since: 1.16
 */
typedef enum {
  GST_VIDEO_ANCILLARY_DID16_S334_EIA_708	= 0x6101,
  GST_VIDEO_ANCILLARY_DID16_S334_EIA_608	= 0x6102,
} GstVideoAncillaryDID16;

/* Active Format Description (AFD) support
 *
 * A/53 ATSC Digital Television Standard
 *
 * Active Format Description (AFD) should be included in video user data whenever the rectangular
 * picture area containing useful information does not extend to the full height or width of the coded
 * frame. AFD data may also be included in user data when the rectangular picture area containing
 * useful information extends to the full height and width of the coded frame.
 *
 *
 * For more details, please see
 *
 * https://www.atsc.org/wp-content/uploads/2015/03/a_53-Part-4-2009.pdf
 *
 */

/**
 * GstVideoAfd:
 *
 *
 * The various active format descriptions
 *
 *
 * For details, see Table 6.14 Active Format in:
 *
 * ATSC Digital Television Standard:
 * Part 4 – MPEG-2 Video System Characteristics
 *
 * https://www.atsc.org/wp-content/uploads/2015/03/a_53-Part-4-2009.pdf
 *
 *
 * and Active Format Description  in
 *
 * Complete list of AFD codes
 *
 * https://en.wikipedia.org/wiki/Active_Format_Description#Complete_list_of_AFD_codes
 *
 * Note:
 *
 * 0) value 0 is undefined for ATSC, indicating that AFD data is not available,
 *    while 0 is reserved for DVB/ETSI
 * 1) values 1, 5, 6, 7, and 12 are reserved for both ATSC and DVB/ETSI
 * 2) values 2 and 3 are not recommended for ATSC, but are valid for DVB/ETSI
 *
 *
 * Since: 1.16
 */
typedef enum
{
  GST_VIDEO_AFD_ATSC_UNAVAILABLE = 0,
  GST_VIDEO_AFD_ETSI_16_9_ACTIVE_PICTURE = 2,
  GST_VIDEO_AFD_ETSI_14_9_ACTIVE_PICTURE = 3,
  GST_VIDEO_AFD_GREATER_THAN_16_9 = 4,
  GST_VIDEO_AFD_4_3_FULL_16_9_FULL = 8,
  GST_VIDEO_AFD_4_3_FULL_4_3_PILLAR = 9,
  GST_VIDEO_AFD_16_9_LETTER_16_9_FULL = 10,
  GST_VIDEO_AFD_14_9_LETTER_14_9_PILLAR = 11,
  GST_VIDEO_AFD_4_3_FULL_14_9_CENTER = 13,
  GST_VIDEO_AFD_16_9_LETTER_14_9_CENTER = 14,
  GST_VIDEO_AFD_16_9_LETTER_4_3_CENTER = 15
} GstVideoAfd;


/**
 * GstVideoAfdMeta:
 * @meta: parent #GstMeta
 * @afd: #GstVideoAfd
 *
 * Metadata providing active format description (AFD)
 *
 * Since: 1.16
 */
typedef struct
{
  GstMeta meta;

  GstVideoAfd afd;
} GstVideoAfdMeta;


GST_VIDEO_API GType gst_video_afd_meta_api_get_type (void);
#define GST_VIDEO_AFD_META_API_TYPE (gst_video_afd_meta_api_get_type())

GST_VIDEO_API const GstMetaInfo *gst_video_afd_meta_get_info (void);
#define GST_VIDEO_AFD_META_INFO (gst_video_afd_meta_get_info())

/**
 * gst_buffer_get_video_afd_meta:
 * @b: A #GstBuffer
 *
 * Gets the #GstVideoAfdMeta that might be present on @b.
 *
 * Since: 1.16
 *
 * Returns: The first #GstVideoAfdMeta present on @b, or %NULL if
 * no #GstVideoAfdMeta are present
 */
#define gst_buffer_get_video_afd_meta(b) \
        ((GstVideoAfdMeta*)gst_buffer_get_meta((b)))

GST_VIDEO_API
    GstVideoAfdMeta * gst_buffer_add_video_afd_meta (GstBuffer * buffer,
    GstVideoAfd afd);



/* Bar data support
 *
 * A/53 ATSC Digital Television Standard
 *
 * Bar data should be included in video user data
 * whenever the rectangular picture area containing useful information
 * does not extend to the full  height or width of the coded frame
 * and AFD alone is insufficient to describe the extent of the image.
 *
 * For more details, see:
 *
 * https://www.atsc.org/wp-content/uploads/2015/03/a_53-Part-4-2009.pdf
 *
 *
 */

/**
 * GstVideoBarData
 * @top_bar_flag: flag indicating presence of top bar field
 * @bottom_bar_flag: flag indicating presence of bottom bar field
 * @left_bar_flag: flag indicating presence of left bar field
 * @right_bar_flag:  flag indicating presence of right bar field
 * @top_bar: last line of a horizontal letterbox bar area at top of
 * 			reconstructed frame
 * @bottom_bar: first line of a horizontal letterbox bar area at
 * 			bottom of reconstructed frame
 * @left_bar: last horizontal luminance sample of a vertical pillarbox
 * 			 bar area at the left side of the reconstructed frame
 * @right_bar: first horizontal luminance sample of a vertical pillarbox
 * 			 bar area at the right side of the  reconstructed frame
 *
 * Since:  1.16
 *
 *
 */
typedef struct
{
  gboolean top_bar_flag;
  gboolean bottom_bar_flag;
  gboolean left_bar_flag;
  gboolean right_bar_flag;

  guint16 top_bar;
  guint16 bottom_bar;
  guint16 left_bar;
  guint16 right_bar;

} GstVideoBarData;


/**
 * GstVideoBarMeta:
 * @meta: parent #GstMeta
 * @bar_data: #GstVideoBarData
 *
 * Metadata providing bar data
 *
 * Since: 1.16
 */
typedef struct
{
  GstMeta meta;

  GstVideoBarData bar_data;
} GstVideoBarMeta;



GST_VIDEO_API GType gst_video_bar_meta_api_get_type (void);
#define GST_VIDEO_BAR_META_API_TYPE (gst_video_bar_meta_api_get_type())

GST_VIDEO_API const GstMetaInfo *gst_video_bar_meta_get_info (void);
#define GST_VIDEO_BAR_META_INFO (gst_video_bar_meta_get_info())

/**
 * gst_buffer_get_video_bar_meta:
 * @b: A #GstBuffer
 *
 * Gets the #GstVideoBarMeta that might be present on @b.
 *
 * Since: 1.16
 *
 * Returns: The first #GstVideoBarMeta present on @b, or %NULL if
 * no #GstVideoBarMeta are present
 */
#define gst_buffer_get_video_bar_meta(b) \
        ((GstVideoBarMeta*)gst_buffer_get_meta((b)))

GST_VIDEO_API
    GstVideoBarMeta * gst_buffer_add_video_bar_meta (GstBuffer * buffer,
    GstVideoBarData * bar_data);



/* Closed Caption support */
/**
 * GstVideoCaptionType:
 * @GST_VIDEO_CAPTION_TYPE_UNKNOWN: Unknown type of CC
 * @GST_VIDEO_CAPTION_TYPE_CEA608_RAW: CEA-608 as byte pairs. Note that
 *      this format is not recommended since is does not specify to
 *      which field the caption comes from and therefore assumes
 *      it comes from the first field (and that there is no information
 *      on the second field). Use @GST_VIDEO_CAPTION_TYPE_CEA708_RAW
 *      if you wish to store CEA-608 from two fields and prefix each byte pair
 *      with 0xFC for the first field and 0xFD for the second field.
 * @GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A: CEA-608 as byte triplets as defined
 *      in SMPTE S334-1 Annex A. The second and third byte of the byte triplet
 *      is the raw CEA608 data, the first byte is a bitfield: The top/7th bit is
 *      0 for the second field, 1 for the first field, bit 6 and 5 are 0 and
 *      bits 4 to 0 are a 5 bit unsigned integer that represents the line
 *      offset relative to the base-line of the original image format (line 9
 *      for 525-line field 1, line 272 for 525-line field 2, line 5 for
 *      625-line field 1 and line 318 for 625-line field 2).
 * @GST_VIDEO_CAPTION_TYPE_CEA708_RAW: CEA-708 as cc_data byte triplets. They
 *      can also contain 608-in-708 and the first byte of each triplet has to
 *      be inspected for detecting the type.
 * @GST_VIDEO_CAPTION_TYPE_CEA708_CDP: CEA-708 (and optionally CEA-608) in
 *      a CDP (Caption Distribution Packet) defined by SMPTE S-334-2.
 *      Contains the whole CDP (starting with 0x9669).
 *
 * The various known types of Closed Caption (CC).
 *
 * Since: 1.16
 */
typedef enum {
  GST_VIDEO_CAPTION_TYPE_UNKNOWN                = 0,
  GST_VIDEO_CAPTION_TYPE_CEA608_RAW		= 1,
  GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A		= 2,
  GST_VIDEO_CAPTION_TYPE_CEA708_RAW		= 3,
  GST_VIDEO_CAPTION_TYPE_CEA708_CDP		= 4
} GstVideoCaptionType;

GST_VIDEO_API
GstVideoCaptionType
gst_video_caption_type_from_caps (const GstCaps *caps);

GST_VIDEO_API
GstCaps *
gst_video_caption_type_to_caps (GstVideoCaptionType type);

/**
 * GstVideoCaptionMeta:
 * @meta: parent #GstMeta
 * @caption_type: The type of Closed Caption contained in the meta.
 * @data: (array length=size): The Closed Caption data.
 * @size: The size in bytes of @data
 *
 * Extra buffer metadata providing Closed Caption.
 *
 * Since: 1.16
 */
typedef struct {
  GstMeta meta;

  GstVideoCaptionType caption_type;
  guint8 *data;
  gsize size;
} GstVideoCaptionMeta;

GST_VIDEO_API
GType	gst_video_caption_meta_api_get_type (void);
#define GST_VIDEO_CAPTION_META_API_TYPE (gst_video_caption_meta_api_get_type())

GST_VIDEO_API
const GstMetaInfo *gst_video_caption_meta_get_info (void);
#define GST_VIDEO_CAPTION_META_INFO (gst_video_caption_meta_get_info())

/**
 * gst_buffer_get_video_caption_meta:
 * @b: A #GstBuffer
 *
 * Gets the #GstVideoCaptionMeta that might be present on @b.
 *
 * Since: 1.16
 *
 * Returns: The first #GstVideoCaptionMeta present on @b, or %NULL if
 * no #GstVideoCaptionMeta are present
 */
#define gst_buffer_get_video_caption_meta(b) \
        ((GstVideoCaptionMeta*)gst_buffer_get_meta((b),GST_VIDEO_CAPTION_META_API_TYPE))

GST_VIDEO_API
GstVideoCaptionMeta *gst_buffer_add_video_caption_meta    (GstBuffer   * buffer,
							   GstVideoCaptionType caption_type,
							   const guint8 *data,
							   gsize size);

/**
 * GstVideoVBIParser:
 *
 * A parser for detecting and extracting @GstVideoAncillary data from
 * Vertical Blanking Interval lines of component signals.
 *
 * Since: 1.16
 */

typedef struct _GstVideoVBIParser GstVideoVBIParser;

GST_VIDEO_API
GType gst_video_vbi_parser_get_type (void);

/**
 * GstVideoVBIParserResult:
 * @GST_VIDEO_VBI_PARSER_RESULT_DONE: No line were provided, or no more Ancillary data was found.
 * @GST_VIDEO_VBI_PARSER_RESULT_OK: A #GstVideoAncillary was found.
 * @GST_VIDEO_VBI_PARSER_RESULT_ERROR: An error occured
 *
 * Return values for #GstVideoVBIParser
 *
 * Since: 1.16
 */
typedef enum {
  GST_VIDEO_VBI_PARSER_RESULT_DONE  = 0,
  GST_VIDEO_VBI_PARSER_RESULT_OK    = 1,
  GST_VIDEO_VBI_PARSER_RESULT_ERROR = 2
} GstVideoVBIParserResult;

GST_VIDEO_API
GstVideoVBIParserResult gst_video_vbi_parser_get_ancillary(GstVideoVBIParser *parser,
							   GstVideoAncillary *anc);

GST_VIDEO_API
GstVideoVBIParser *gst_video_vbi_parser_new (GstVideoFormat format, guint32 pixel_width);

GST_VIDEO_API
GstVideoVBIParser *gst_video_vbi_parser_copy (const GstVideoVBIParser *parser);

GST_VIDEO_API
void               gst_video_vbi_parser_free (GstVideoVBIParser *parser);

GST_VIDEO_API
void		   gst_video_vbi_parser_add_line (GstVideoVBIParser *parser, const guint8 *data);

/**
 * GstVideoVBIEncoder:
 *
 * An encoder for writing ancillary data to the
 * Vertical Blanking Interval lines of component signals.
 *
 * Since: 1.16
 */

typedef struct _GstVideoVBIEncoder GstVideoVBIEncoder;

GST_VIDEO_API
GType gst_video_vbi_encoder_get_type (void);

GST_VIDEO_API
GstVideoVBIEncoder *gst_video_vbi_encoder_new  (GstVideoFormat format, guint32 pixel_width);

GST_VIDEO_API
GstVideoVBIEncoder *gst_video_vbi_encoder_copy (const GstVideoVBIEncoder *encoder);

GST_VIDEO_API
void               gst_video_vbi_encoder_free  (GstVideoVBIEncoder *encoder);

GST_VIDEO_API
gboolean gst_video_vbi_encoder_add_ancillary   (GstVideoVBIEncoder *encoder,
                                                gboolean            composite,
                                                guint8              DID,
                                                guint8              SDID_block_number,
                                                const guint8       *data,
                                                guint               data_count);

GST_VIDEO_API
void gst_video_vbi_encoder_write_line (GstVideoVBIEncoder *encoder, guint8 *data);

G_END_DECLS

#endif /* __GST_VIDEO_ANC_H__ */
