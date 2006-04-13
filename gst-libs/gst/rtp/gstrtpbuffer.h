/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
 *               <2005> Wim Taymans <wim@fluendo.com>
 *
 * gstrtpbuffer.h: various helper functions to manipulate buffers
 *     with RTP payload.
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

#ifndef __GST_RTPBUFFER_H__
#define __GST_RTPBUFFER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_RTP_VERSION 2

typedef enum
{
  /* Audio: */
  GST_RTP_PAYLOAD_PCMU = 0,             /* ITU-T G.711. mu-law audio (RFC 3551) */
  GST_RTP_PAYLOAD_GSM = 3,
  GST_RTP_PAYLOAD_PCMA = 8,             /* ITU-T G.711 A-law audio (RFC 3551) */
  GST_RTP_PAYLOAD_L16_STEREO = 10,
  GST_RTP_PAYLOAD_L16_MONO = 11,
  GST_RTP_PAYLOAD_MPA = 14,             /* Audio MPEG 1-3 */
  GST_RTP_PAYLOAD_G723_63 = 16,         /* Not standard */
  GST_RTP_PAYLOAD_G723_53 = 17,         /* Not standard */
  GST_RTP_PAYLOAD_TS48 = 18,            /* Not standard */
  GST_RTP_PAYLOAD_TS41 = 19,            /* Not standard */
  GST_RTP_PAYLOAD_G728 = 20,            /* Not standard */
  GST_RTP_PAYLOAD_G729 = 21,            /* Not standard */

  /* Video: */
  GST_RTP_PAYLOAD_MPV = 32,             /* Video MPEG 1 & 2 */
  GST_RTP_PAYLOAD_H263 = 34,

  /* BOTH */
} GstRTPPayload;

/* Defining the above as strings, to make the declaration of pad_templates
 * easier. So if please keep these synchronized with the above.
 */
#define GST_RTP_PAYLOAD_PCMU_STRING "0"
#define GST_RTP_PAYLOAD_GSM_STRING "3"
#define GST_RTP_PAYLOAD_PCMA_STRING "8"
#define GST_RTP_PAYLOAD_L16_STEREO_STRING "10"
#define GST_RTP_PAYLOAD_L16_MONO_STRING "11"
#define GST_RTP_PAYLOAD_MPA_STRING "14"
#define GST_RTP_PAYLOAD_G723_63_STRING "16"
#define GST_RTP_PAYLOAD_G723_53_STRING "17"
#define GST_RTP_PAYLOAD_TS48_STRING "18"
#define GST_RTP_PAYLOAD_TS41_STRING "19"
#define GST_RTP_PAYLOAD_G728_STRING "20"
#define GST_RTP_PAYLOAD_G729_STRING "21"
#define GST_RTP_PAYLOAD_MPV_STRING "32"
#define GST_RTP_PAYLOAD_H263_STRING "34"

/* creating buffers */
GstBuffer*      gst_rtp_buffer_new              (void);
void            gst_rtp_buffer_allocate_data    (GstBuffer *buffer, guint payload_len, 
                                                 guint8 pad_len, guint8 csrc_count);

GstBuffer*      gst_rtp_buffer_new_take_data    (gpointer data, guint len);
GstBuffer*      gst_rtp_buffer_new_copy_data    (gpointer data, guint len);
GstBuffer*      gst_rtp_buffer_new_allocate     (guint payload_len, guint8 pad_len, guint8 csrc_count);
GstBuffer*      gst_rtp_buffer_new_allocate_len (guint packet_len, guint8 pad_len, guint8 csrc_count);

guint           gst_rtp_buffer_calc_header_len  (guint8 csrc_count);
guint           gst_rtp_buffer_calc_packet_len  (guint payload_len, guint8 pad_len, guint8 csrc_count);
guint           gst_rtp_buffer_calc_payload_len (guint packet_len, guint8 pad_len, guint8 csrc_count);

gboolean        gst_rtp_buffer_validate_data    (guint8 *data, guint len);
gboolean        gst_rtp_buffer_validate         (GstBuffer *buffer);

void            gst_rtp_buffer_set_packet_len   (GstBuffer *buffer, guint len);
guint           gst_rtp_buffer_get_packet_len   (GstBuffer *buffer);

guint8          gst_rtp_buffer_get_version      (GstBuffer *buffer);
void            gst_rtp_buffer_set_version      (GstBuffer *buffer, guint8 version);

gboolean        gst_rtp_buffer_get_padding      (GstBuffer *buffer);
void            gst_rtp_buffer_set_padding      (GstBuffer *buffer, gboolean padding);
void            gst_rtp_buffer_pad_to           (GstBuffer *buffer, guint len);

gboolean        gst_rtp_buffer_get_extension    (GstBuffer *buffer);
void            gst_rtp_buffer_set_extension    (GstBuffer *buffer, gboolean extension);

guint32         gst_rtp_buffer_get_ssrc         (GstBuffer *buffer);
void            gst_rtp_buffer_set_ssrc         (GstBuffer *buffer, guint32 ssrc);

guint8          gst_rtp_buffer_get_csrc_count   (GstBuffer *buffer);
guint32         gst_rtp_buffer_get_csrc         (GstBuffer *buffer, guint8 idx);
void            gst_rtp_buffer_set_csrc         (GstBuffer *buffer, guint8 idx, guint32 csrc);

gboolean        gst_rtp_buffer_get_marker       (GstBuffer *buffer);
void            gst_rtp_buffer_set_marker       (GstBuffer *buffer, gboolean marker);

guint8          gst_rtp_buffer_get_payload_type (GstBuffer *buffer);
void            gst_rtp_buffer_set_payload_type (GstBuffer *buffer, guint8 payload_type);

guint16         gst_rtp_buffer_get_seq          (GstBuffer *buffer);
void            gst_rtp_buffer_set_seq          (GstBuffer *buffer, guint16 seq);

guint32         gst_rtp_buffer_get_timestamp    (GstBuffer *buffer);
void            gst_rtp_buffer_set_timestamp    (GstBuffer *buffer, guint32 timestamp);

GstBuffer* gst_rtp_buffer_get_payload_buffer (GstBuffer *buffer);
guint           gst_rtp_buffer_get_payload_len  (GstBuffer *buffer);
gpointer        gst_rtp_buffer_get_payload      (GstBuffer *buffer);

G_END_DECLS

#endif /* __GST_RTPBUFFER_H__ */

