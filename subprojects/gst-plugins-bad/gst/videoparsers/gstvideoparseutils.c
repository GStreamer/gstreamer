/* GStreamer
 *  Copyright (2019) Collabora Ltd.
 *  Contact: Aaron Boxer <aaron.boxer@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/base/base.h>
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <gst/video/video-sei.h>
#include <gst/base/gstbitreader.h>
#include <gstvideoparseutils.h>

GST_DEBUG_CATEGORY_EXTERN (videoparseutils_debug);
#define GST_CAT_DEFAULT videoparseutils_debug

static gboolean gst_video_parse_utils_parse_bar (const guint8 * data,
    gsize size, guint field, GstVideoBarData * bar);

static gboolean gst_video_parse_utils_parse_afd (const guint8 data,
    GstVideoAFD * afd, GstVideoAFDSpec spec);


/*
 * gst_video_parse_user_data:
 * @elt: #GstElement that is parsing user data
 * @user_data: #GstVideoParseUserData struct to hold parsed closed caption, bar and AFD data
 * @br: #GstByteReader attached to buffer of user data
 * @field: 0 for progressive or field 1 and 1 for field 2
 * @provider_code: Currently, only (US) ATSC and DirecTV provider codes are supported
 *
 * Parse user data and store in @user_data
 */
void
gst_video_parse_user_data (GstElement * elt, GstVideoParseUserData * user_data,
    GstByteReader * br, guint8 field, guint16 provider_code)
{

  guint32 user_data_id = 0;
  guint8 user_data_type_code = 0;
  gboolean a53_process_708_cc_data = FALSE;
  gboolean process_708_em_data = FALSE;
  guint8 temp = 0;
  guint8 cc_count;
  guint cc_size;
  guint bar_size = 0;
  const guint8 *data = NULL;
  guint8 directv_size = 0;


  /* https://en.wikipedia.org/wiki/CEA-708#Picture_User_Data */
  switch (provider_code) {
    case ITU_T_T35_MANUFACTURER_US_ATSC:
      if (!gst_byte_reader_peek_uint32_be (br, &user_data_id)) {
        GST_WARNING_OBJECT (elt, "Missing user data id, ignoring");
        return;
      }
      switch (user_data_id) {
        case A53_USER_DATA_ID_DTG1:
        case A53_USER_DATA_ID_GA94:
          /* ANSI/SCTE 128-2010a section 8.1.2 */
          if (!gst_byte_reader_get_uint32_be (br, &user_data_id)) {
            GST_WARNING_OBJECT (elt, "Missing user data id, ignoring");
            return;
          }
          break;
        default:
          /* check for SCTE 20 */
          if (user_data_id >> 24 == A53_USER_DATA_TYPE_CODE_CC_DATA) {
            user_data_id = USER_DATA_ID_SCTE_20_CC;
            gst_byte_reader_skip (br, 1);
          }
          break;
      }
      break;
    case ITU_T_T35_MANUFACTURER_US_DIRECTV:
      user_data_id = USER_DATA_ID_DIRECTV_CC;
      break;
    default:
      GST_LOG_OBJECT (elt, "Unsupported provider code %d", provider_code);
      return;
  }

  switch (user_data_id) {
    case USER_DATA_ID_SCTE_20_CC:
      GST_DEBUG_OBJECT (elt, "Unsupported SCTE 20 closed captions");
      break;
    case A53_USER_DATA_ID_DTG1:
      if (!gst_byte_reader_get_uint8 (br, &temp)) {
        GST_WARNING_OBJECT (elt, "Missing active format flag, ignoring");
        break;
      }

      /* check active format flag for presence of AFD */
      if (temp & 0x40) {
        if (!gst_byte_reader_get_uint8 (br, &temp)) {
          GST_WARNING_OBJECT (elt,
              "Missing active format description, ignoring");
          break;
        }

        GST_LOG_OBJECT (elt, "parsed active format description (AFD): %d",
            temp);
        user_data->afd_spec = GST_VIDEO_AFD_SPEC_ATSC_A53;
        user_data->afd = temp;
        user_data->active_format_flag = TRUE;
      } else {
        user_data->active_format_flag = FALSE;
      }
      user_data->has_afd = TRUE;
      user_data->field = field;
      break;
    case USER_DATA_ID_DIRECTV_CC:
    case A53_USER_DATA_ID_GA94:
      if (!gst_byte_reader_get_uint8 (br, &user_data_type_code)) {
        GST_WARNING_OBJECT (elt, "Missing user data type code, ignoring");
        break;
      }
      if (provider_code == ITU_T_T35_MANUFACTURER_US_DIRECTV) {
        if (!gst_byte_reader_get_uint8 (br, &directv_size)) {
          GST_WARNING_OBJECT (elt, "Missing DirecTV size, ignoring");
          break;
        }
      }
      switch (user_data_type_code) {
        case A53_USER_DATA_TYPE_CODE_CC_DATA:
          /* 1 (cc count byte) +
           * 1 (reserved byte, 0xff) +
           * 1 (marker_bits, 0xff)
           */
          if (gst_byte_reader_get_remaining (br) < 3) {
            GST_WARNING_OBJECT (elt,
                "Closed caption data packet too short, ignoring");
            break;
          }
          if (!gst_byte_reader_get_uint8 (br, &cc_count)) {
            GST_WARNING_OBJECT (elt, "Missing closed caption count, ignoring");
            break;
          }

          /* A53 part 4 closed captions */
          a53_process_708_cc_data =
              (cc_count & CEA_708_PROCESS_CC_DATA_FLAG) != 0;
          if (!a53_process_708_cc_data) {
            GST_WARNING_OBJECT (elt,
                "ignoring closed captions as CEA_708_PROCESS_CC_DATA_FLAG is not set");
          }

          process_708_em_data = (cc_count & CEA_708_PROCESS_EM_DATA_FLAG) != 0;
          if (!process_708_em_data) {
            GST_WARNING_OBJECT (elt,
                "CEA_708_PROCESS_EM_DATA_FLAG flag is not set");
          }
          if (!gst_byte_reader_get_uint8 (br, &temp)) {
            GST_WARNING_OBJECT (elt, "Missing em bits, ignoring");
            break;
          }
          if (temp != 0xff) {
            GST_WARNING_OBJECT (elt, "em data does not equal 0xFF");
          }
          process_708_em_data = process_708_em_data && (temp == 0xff);
          /* ignore process_708_em_data as there is content that doesn't follow spec for this field */

          if (a53_process_708_cc_data) {
            cc_count = cc_count & 0x1f;
            cc_size = cc_count * 3;

            if (cc_size == 0 || cc_size > gst_byte_reader_get_remaining (br)) {
              GST_DEBUG_OBJECT (elt,
                  "ignoring closed captions, not enough data");
              break;
            }

            /* Shouldn't really happen so let's not go out of our way to handle it */
            if (user_data->closedcaptions_size > 0)
              GST_WARNING_OBJECT (elt, "unused pending closed captions!");

            g_assert (cc_size <= sizeof (user_data->closedcaptions));
            if (!gst_byte_reader_get_data (br, cc_size, &data))
              break;
            memcpy (user_data->closedcaptions, data, cc_size);
            user_data->closedcaptions_size = cc_size;
            user_data->closedcaptions_type = GST_VIDEO_CAPTION_TYPE_CEA708_RAW;
            user_data->field = field;
            GST_DEBUG_OBJECT (elt, "CEA-708 closed captions, %u bytes",
                cc_size);
          }
          break;
        case A53_USER_DATA_TYPE_CODE_SCTE_21_EIA_608_CC_DATA:
          GST_DEBUG_OBJECT (elt, "Unsupported SCTE 21 closed captions");
          break;
        case A53_USER_DATA_TYPE_CODE_BAR_DATA:
          bar_size = gst_byte_reader_get_remaining (br);
          if (bar_size == 0) {
            GST_WARNING_OBJECT (elt, "Bar data packet too short, ignoring");
            break;
          }
          if (bar_size > GST_VIDEO_BAR_MAX_BYTES) {
            GST_WARNING_OBJECT (elt,
                "Bar data packet of size %d is too long, ignoring", bar_size);
            break;
          }
          if (!gst_byte_reader_get_data (br, bar_size, &data))
            break;
          memcpy (user_data->bar_data, data, bar_size);
          user_data->bar_data_size = bar_size;
          user_data->has_bar_data = TRUE;
          user_data->field = field;
          GST_DEBUG_OBJECT (elt, "Bar data, %u bytes", bar_size);
          break;
        default:
          GST_DEBUG_OBJECT (elt,
              "Unrecognized user data type code %d of size %d",
              user_data_type_code, gst_byte_reader_get_remaining (br));
          break;
      }
      break;
    default:
      GST_DEBUG_OBJECT (elt,
          "Unrecognized user data id %d of size %d", user_data_id,
          gst_byte_reader_get_remaining (br));
      break;
  }
}

/*
 * gst_video_push_user_data:
 * @elt: #GstElement that is pushing user data
 * @user_data: #GstVideoParseUserData holding parsed closed caption, bar and AFD data
 * @buf: #GstBuffer that receives the parsed data
 *
 * After user data has been parsed, add the data to @buf
 */
void
gst_video_push_user_data (GstElement * elt, GstVideoParseUserData * user_data,
    GstBuffer * buf)
{

  /* 1. handle closed captions */
  if (user_data->closedcaptions_size > 0) {
    if (!gst_buffer_get_meta (buf, GST_VIDEO_CAPTION_META_API_TYPE)) {
      gst_buffer_add_video_caption_meta (buf,
          user_data->closedcaptions_type, user_data->closedcaptions,
          user_data->closedcaptions_size);
    } else {
      GST_DEBUG_OBJECT (elt, "Closed caption data already found on buffer, "
          "discarding to avoid duplication");
    }

    user_data->closedcaptions_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
    user_data->closedcaptions_size = 0;
  }

  /* 2. handle AFD */
  if (user_data->has_afd) {
    GstVideoAFD afd;
    afd.field = 0;
    afd.aspect_ratio = GST_VIDEO_AFD_ASPECT_RATIO_UNDEFINED;
    afd.spec = GST_VIDEO_AFD_SPEC_ATSC_A53;
    afd.afd = GST_VIDEO_AFD_UNAVAILABLE;
    if (gst_video_parse_utils_parse_afd (user_data->afd, &afd, afd.spec)) {
      gst_buffer_add_video_afd_meta (buf, afd.field, afd.spec, afd.afd);
    } else {
      GST_WARNING_OBJECT (elt, "Invalid AFD value %d", user_data->afd);
    }
  } else if (user_data->active_format_flag) {
    /* AFD was present, but now it is no longer present */
    GST_DEBUG_OBJECT (elt,
        "AFD was present in previous frame, now no longer present");
    user_data->active_format_flag = 0;
  }
  user_data->has_afd = FALSE;

  /* 3. handle Bar data */
  if (user_data->has_bar_data) {
    GstVideoBarData data;
    if (gst_video_parse_utils_parse_bar (user_data->bar_data,
            user_data->bar_data_size, user_data->field, &data)) {
      gst_buffer_add_video_bar_meta (buf, data.field, data.is_letterbox,
          data.bar_data[0], data.bar_data[1]);
    } else {
      GST_WARNING_OBJECT (elt, "Invalid Bar data");
    }
  } else if (user_data->bar_data_size) {
    /* bar data was present, but now it is no longer present */
    GST_DEBUG_OBJECT (elt,
        "Bar data was present in previous frame, now no longer present");
    user_data->bar_data_size = 0;
  }
  user_data->has_bar_data = FALSE;
}


/*
 * gst_video_parse_utils_parse_bar:
 * @data: bar data array
 * @size:size of bar data array
 * @bar: #GstVideoBarData structure
 *
 * Parse bar data bytes into #GstVideoBarData structure
 *
 * See Table in https://www.atsc.org/wp-content/uploads/2015/03/a_53-Part-4-2009.pdf
 *
 * Returns: TRUE if parsing was successful, otherwise FALSE
 */
static gboolean
gst_video_parse_utils_parse_bar (const guint8 * data, gsize size,
    guint field, GstVideoBarData * bar)
{
  guint8 temp;
  int i = 0;
  guint8 bar_flags[4];
  guint16 bar_vals[4] = { 0, 0, 0, 0 };
  GstBitReader bar_tender;

  /* there must be at least one byte, and not more than GST_VIDEO_BAR_MAX_BYTES bytes */
  if (!bar || size == 0 || size > GST_VIDEO_BAR_MAX_BYTES)
    return FALSE;

  gst_bit_reader_init (&bar_tender, data, size);


  /* parse bar flags */
  for (i = 0; i < 4; ++i) {
    if (!gst_bit_reader_get_bits_uint8 (&bar_tender, bar_flags + i, 1))
      return FALSE;
  }

  /* the next four bits must equal 1111 */
  if (!gst_bit_reader_get_bits_uint8 (&bar_tender, &temp, 4) || temp != 0xF)
    return FALSE;

  /* parse bar values */
  for (i = 0; i < 4; ++i) {
    if (bar_flags[i]) {
      /* the next two bits must equal 11 */
      if (!gst_bit_reader_get_bits_uint8 (&bar_tender, &temp, 2) || temp != 0x3)
        return FALSE;

      if (!gst_bit_reader_get_bits_uint16 (&bar_tender, bar_vals + i, 14))
        return FALSE;
    }
  }

  /* bars are signaled in pairs: either top/bottom or left/right, but not both */
  if ((bar_flags[0] != bar_flags[1]) || (bar_flags[2] != bar_flags[3]))
    return FALSE;
  if (bar_flags[0] && bar_flags[2])
    return FALSE;

  bar->is_letterbox = bar_flags[0];
  if (bar->is_letterbox) {
    bar->bar_data[0] = bar_vals[0];
    bar->bar_data[1] = bar_vals[1];
  } else {
    bar->bar_data[0] = bar_vals[2];
    bar->bar_data[1] = bar_vals[3];
  }
  bar->field = field;

  return TRUE;
}

/*
 * gst_video_parse_utils_parse_afd:
 * @data: bar byte
 * @afd: pointer to #GstVideoAFD struct
 * @spec : #GstVideoAFDSpec indicating specification that applies to AFD byte
 *
 * Parse afd byte into #GstVideoAFD struct
 *
 * See:
 *
 * https://www.atsc.org/wp-content/uploads/2015/03/a_53-Part-4-2009.pdf
 *
 * https://en.wikipedia.org/wiki/Active_Format_Description#Complete_list_of_AFD_codes
 *
 * and
 *
 * SMPTE ST2016-1
 *
 * Returns: TRUE if parsing was successful, otherwise FALSE
 */
static gboolean
gst_video_parse_utils_parse_afd (const guint8 data, GstVideoAFD * afd,
    GstVideoAFDSpec spec)
{
  guint8 afd_data;
  g_return_val_if_fail (afd != NULL, FALSE);
  g_return_val_if_fail ((guint8) spec <= 2, FALSE);
  switch (spec) {
    case GST_VIDEO_AFD_SPEC_DVB_ETSI:
    case GST_VIDEO_AFD_SPEC_ATSC_A53:
      if ((data & 0x40) == 0)
        return FALSE;
      afd_data = data & 0xF;
      break;
    case GST_VIDEO_AFD_SPEC_SMPTE_ST2016_1:
      if ((data & 0x80) || (data & 0x3))
        return FALSE;
      afd_data = data >> 3;
      afd->aspect_ratio = (GstVideoAFDAspectRatio) (((data >> 2) & 1) + 1);
      break;
    default:
      return FALSE;
  }

  /* AFD is stored in a nybble */
  g_return_val_if_fail (afd_data <= 0xF, FALSE);
  /* reserved values for all specifications */
  g_return_val_if_fail (afd_data != 1 && (afd_data < 5 || afd_data > 7)
      && afd_data != 12, FALSE);
  /* reserved for DVB/ETSI */
  g_return_val_if_fail ((spec != GST_VIDEO_AFD_SPEC_DVB_ETSI)
      || (afd_data != 0), FALSE);

  afd->spec = spec;
  afd->afd = (GstVideoAFDValue) afd_data;
  return TRUE;
}

/*
 * gst_video_parse_user_data_unregistered:
 * @elt: #GstElement that is parsing user data
 * @user_data: #GstVideoParseUserDataUnregistered struct to hold parsed data
 * @br: #GstByteReader attached to buffer of user data
 * @uuid: User Data Unregistered UUID
 *
 * Parse user data and store in @user_data
 */
void
gst_video_parse_user_data_unregistered (GstElement * elt,
    GstVideoParseUserDataUnregistered * user_data,
    GstByteReader * br, guint8 uuid[16])
{
  gst_video_user_data_unregistered_clear (user_data);

  memcpy (&user_data->uuid, uuid, 16);
  user_data->size = gst_byte_reader_get_size (br);
  gst_byte_reader_dup_data (br, user_data->size, &user_data->data);
}

/*
 * gst_video_user_data_unregistered_clear:
 * @user_data: #GstVideoParseUserDataUnregistered holding SEI User Data Unregistered
 *
 * Clears the user data unregistered
 */
void
gst_video_user_data_unregistered_clear (GstVideoParseUserDataUnregistered *
    user_data)
{
  g_free (user_data->data);
  user_data->data = NULL;
  user_data->size = 0;
}

/*
 * gst_video_push_user_data_unregistered:
 * @elt: #GstElement that is pushing user data
 * @user_data: #GstVideoParseUserDataUnregistered holding SEI User Data Unregistered
 * @buf: #GstBuffer that receives the parsed data
 *
 * After user data has been parsed, add the data to @buf
 */
void
gst_video_push_user_data_unregistered (GstElement * elt,
    GstVideoParseUserDataUnregistered * user_data, GstBuffer * buf)
{
  if (user_data->data != NULL) {
    gst_buffer_add_video_sei_user_data_unregistered_meta (buf, user_data->uuid,
        user_data->data, user_data->size);
    gst_video_user_data_unregistered_clear (user_data);
  }
}
