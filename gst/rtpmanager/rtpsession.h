/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __RTP_SESSION_H__
#define __RTP_SESSION_H__

#include <gst/gst.h>
#include <gst/netbuffer/gstnetbuffer.h>

#include "rtpsource.h"

typedef struct _RTPSession RTPSession;
typedef struct _RTPSessionClass RTPSessionClass;

#define RTP_TYPE_SESSION             (rtp_session_get_type())
#define RTP_SESSION(sess)            (G_TYPE_CHECK_INSTANCE_CAST((sess),RTP_TYPE_SESSION,RTPSession))
#define RTP_SESSION_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RTP_TYPE_SESSION,RTPSessionClass))
#define RTP_IS_SESSION(sess)         (G_TYPE_CHECK_INSTANCE_TYPE((sess),RTP_TYPE_SESSION))
#define RTP_IS_SESSION_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RTP_TYPE_SESSION))
#define RTP_SESSION_CAST(sess)       ((RTPSession *)(sess))

#define RTP_SESSION_LOCK(sess)     (g_mutex_lock ((sess)->lock))
#define RTP_SESSION_UNLOCK(sess)   (g_mutex_unlock ((sess)->lock))

/**
 * RTPSessionProcessRTP:
 * @sess: an #RTPSession
 * @src: the #RTPSource
 * @buffer: the RTP buffer ready for processing
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess has @buffer ready for further
 * processing. Processing the buffer typically includes decoding and displaying
 * the buffer.
 *
 * Returns: a #GstFlowReturn.
 */
typedef GstFlowReturn (*RTPSessionProcessRTP) (RTPSession *sess, RTPSource *src, GstBuffer *buffer, gpointer user_data);

/**
 * RTPSessionSendRTP:
 * @sess: an #RTPSession
 * @src: the #RTPSource
 * @buffer: the RTP buffer ready for sending
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess has @buffer ready for sending to
 * all listening participants in this session.
 *
 * Returns: a #GstFlowReturn.
 */
typedef GstFlowReturn (*RTPSessionSendRTP) (RTPSession *sess, RTPSource *src, gpointer data, gpointer user_data);

/**
 * RTPSessionSendRTCP:
 * @sess: an #RTPSession
 * @src: the #RTPSource
 * @buffer: the RTCP buffer ready for sending
 * @eos: if an EOS event should be pushed
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess has @buffer ready for sending to
 * all listening participants in this session.
 *
 * Returns: a #GstFlowReturn.
 */
typedef GstFlowReturn (*RTPSessionSendRTCP) (RTPSession *sess, RTPSource *src, GstBuffer *buffer, 
    gboolean eos, gpointer user_data);

/**
 * RTPSessionSyncRTCP:
 * @sess: an #RTPSession
 * @src: the #RTPSource
 * @buffer: the RTCP buffer ready for synchronisation
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess has an SR @buffer ready for doing
 * synchronisation between streams.
 *
 * Returns: a #GstFlowReturn.
 */
typedef GstFlowReturn (*RTPSessionSyncRTCP) (RTPSession *sess, RTPSource *src, GstBuffer *buffer, gpointer user_data);

/**
 * RTPSessionClockRate:
 * @sess: an #RTPSession
 * @payload: the payload
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess needs the clock-rate of @payload.
 *
 * Returns: the clock-rate of @pt.
 */
typedef gint (*RTPSessionClockRate) (RTPSession *sess, guint8 payload, gpointer user_data);

/**
 * RTPSessionReconsider:
 * @sess: an #RTPSession
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess needs to cancel the current timeout. 
 * The currently running timeout should be canceled and a new reporting interval
 * should be requested from @sess.
 */
typedef void (*RTPSessionReconsider) (RTPSession *sess, gpointer user_data);

/**
 * RTPSessionRequestKeyUnit:
 * @sess: an #RTPSession
 * @all_headers: %TRUE if "all-headers" property should be set on the key unit
 *  request
 * @user_data: user data specified when registering
*
 * Asks the encoder to produce a key unit as soon as possibly within the
 * bandwidth constraints
 */
typedef void (*RTPSessionRequestKeyUnit) (RTPSession *sess,
    gboolean all_headers, gpointer user_data);

/**
 * RTPSessionRequestTime:
 * @sess: an #RTPSession
 * @user_data: user data specified when registering
 *
 * This callback will be called when @sess needs the current time. The time
 * should be returned as a #GstClockTime
 */
typedef GstClockTime (*RTPSessionRequestTime) (RTPSession *sess,
    gpointer user_data);

/**
 * RTPSessionCallbacks:
 * @RTPSessionProcessRTP: callback to process RTP packets
 * @RTPSessionSendRTP: callback for sending RTP packets
 * @RTPSessionSendRTCP: callback for sending RTCP packets
 * @RTPSessionSyncRTCP: callback for handling SR packets
 * @RTPSessionReconsider: callback for reconsidering the timeout
 * @RTPSessionRequestKeyUnit: callback for requesting a new key unit
 *
 * These callbacks can be installed on the session manager to get notification
 * when RTP and RTCP packets are ready for further processing. These callbacks
 * are not implemented with signals for performance reasons.
 */
typedef struct {
  RTPSessionProcessRTP  process_rtp;
  RTPSessionSendRTP     send_rtp;
  RTPSessionSyncRTCP    sync_rtcp;
  RTPSessionSendRTCP    send_rtcp;
  RTPSessionClockRate   clock_rate;
  RTPSessionReconsider  reconsider;
  RTPSessionRequestKeyUnit request_key_unit;
  RTPSessionRequestTime request_time;
} RTPSessionCallbacks;

/**
 * RTPSession:
 * @lock: lock to protect the session
 * @source: the source of this session
 * @ssrcs: Hashtable of sources indexed by SSRC
 * @cnames: Hashtable of sources indexed by CNAME
 * @num_sources: the number of sources
 * @activecount: the number of active sources
 * @callbacks: callbacks
 * @user_data: user data passed in callbacks
 * @stats: session statistics
 *
 * The RTP session manager object
 */
struct _RTPSession {
  GObject       object;

  GMutex       *lock;

  guint         header_len;
  guint         mtu;

  /* bandwidths */
  gboolean     recalc_bandwidth;
  guint        bandwidth;
  gdouble      rtcp_bandwidth;
  guint        rtcp_rr_bandwidth;
  guint        rtcp_rs_bandwidth;

  RTPSource    *source;

  /* for sender/receiver counting */
  guint32       key;
  guint32       mask_idx;
  guint32       mask;
  GHashTable   *ssrcs[32];
  GHashTable   *cnames;
  guint         total_sources;

  GstClockTime  next_rtcp_check_time;
  GstClockTime  last_rtcp_send_time;
  gboolean      first_rtcp;
  gboolean      allow_early;

  GstClockTime  next_early_rtcp_time;

  gchar        *bye_reason;
  gboolean      sent_bye;

  RTPSessionCallbacks   callbacks;
  gpointer              process_rtp_user_data;
  gpointer              send_rtp_user_data;
  gpointer              send_rtcp_user_data;
  gpointer              sync_rtcp_user_data;
  gpointer              clock_rate_user_data;
  gpointer              reconsider_user_data;
  gpointer              request_key_unit_user_data;
  gpointer              request_time_user_data;

  RTPSessionStats stats;

  gboolean      change_ssrc;
  gboolean      favor_new;
  GstClockTime  rtcp_feedback_retention_window;
  guint         rtcp_immediate_feedback_threshold;

  GArray       *rtcp_pli_requests;
  GstClockTime last_keyframe_request;
};

/**
 * RTPSessionClass:
 * @on_new_ssrc: emited when a new source is found
 * @on_bye_ssrc: emited when a source is gone
 *
 * The session class.
 */
struct _RTPSessionClass {
  GObjectClass   parent_class;

  /* action signals */
  RTPSource* (*get_source_by_ssrc) (RTPSession *sess, guint32 ssrc);

  /* signals */
  void (*on_new_ssrc)       (RTPSession *sess, RTPSource *source);
  void (*on_ssrc_collision) (RTPSession *sess, RTPSource *source);
  void (*on_ssrc_validated) (RTPSession *sess, RTPSource *source);
  void (*on_ssrc_active)    (RTPSession *sess, RTPSource *source);
  void (*on_ssrc_sdes)      (RTPSession *sess, RTPSource *source);
  void (*on_bye_ssrc)       (RTPSession *sess, RTPSource *source);
  void (*on_bye_timeout)    (RTPSession *sess, RTPSource *source);
  void (*on_timeout)        (RTPSession *sess, RTPSource *source);
  void (*on_sender_timeout) (RTPSession *sess, RTPSource *source);
  gboolean (*on_sending_rtcp) (RTPSession *sess, GstBuffer *buffer,
      gboolean early);
  void (*on_feedback_rtcp)  (RTPSession *sess, guint type, guint fbtype,
      guint sender_ssrc, guint media_ssrc, GstBuffer *fci);
  void (*send_rtcp)         (RTPSession *sess, GstClockTimeDiff max_delay);
};

GType rtp_session_get_type (void);

/* create and configure */
RTPSession*     rtp_session_new           (void);
void            rtp_session_set_callbacks          (RTPSession *sess,
		                                    RTPSessionCallbacks *callbacks,
                                                    gpointer user_data);
void            rtp_session_set_process_rtp_callback   (RTPSession * sess,
                                                    RTPSessionProcessRTP callback,
                                                    gpointer user_data);
void            rtp_session_set_send_rtp_callback  (RTPSession * sess,
                                                    RTPSessionSendRTP callback,
                                                    gpointer user_data);
void            rtp_session_set_send_rtcp_callback   (RTPSession * sess,
                                                    RTPSessionSendRTCP callback,
                                                    gpointer user_data);
void            rtp_session_set_sync_rtcp_callback   (RTPSession * sess,
                                                    RTPSessionSyncRTCP callback,
                                                    gpointer user_data);
void            rtp_session_set_clock_rate_callback   (RTPSession * sess,
                                                    RTPSessionClockRate callback,
                                                    gpointer user_data);
void            rtp_session_set_reconsider_callback (RTPSession * sess,
                                                    RTPSessionReconsider callback,
                                                    gpointer user_data);
void            rtp_session_set_request_time_callback (RTPSession * sess,
                                                    RTPSessionRequestTime callback,
                                                    gpointer user_data);

void            rtp_session_set_bandwidth          (RTPSession *sess, gdouble bandwidth);
gdouble         rtp_session_get_bandwidth          (RTPSession *sess);
void            rtp_session_set_rtcp_fraction      (RTPSession *sess, gdouble fraction);
gdouble         rtp_session_get_rtcp_fraction      (RTPSession *sess);

gboolean        rtp_session_set_sdes_string        (RTPSession *sess, GstRTCPSDESType type,
                                                    const gchar *cname);
gchar*          rtp_session_get_sdes_string        (RTPSession *sess, GstRTCPSDESType type);

GstStructure *  rtp_session_get_sdes_struct        (RTPSession *sess);
void            rtp_session_set_sdes_struct        (RTPSession *sess, const GstStructure *sdes);

/* handling sources */
RTPSource*      rtp_session_get_internal_source    (RTPSession *sess);

void            rtp_session_set_internal_ssrc      (RTPSession *sess, guint32 ssrc);
guint32         rtp_session_get_internal_ssrc      (RTPSession *sess);

gboolean        rtp_session_add_source             (RTPSession *sess, RTPSource *src);
guint           rtp_session_get_num_sources        (RTPSession *sess);
guint           rtp_session_get_num_active_sources (RTPSession *sess);
RTPSource*      rtp_session_get_source_by_ssrc     (RTPSession *sess, guint32 ssrc);
RTPSource*      rtp_session_get_source_by_cname    (RTPSession *sess, const gchar *cname);
RTPSource*      rtp_session_create_source          (RTPSession *sess);

/* processing packets from receivers */
GstFlowReturn   rtp_session_process_rtp            (RTPSession *sess, GstBuffer *buffer,
                                                    GstClockTime current_time,
						    GstClockTime running_time);
GstFlowReturn   rtp_session_process_rtcp           (RTPSession *sess, GstBuffer *buffer,
                                                    GstClockTime current_time,
                                                    guint64 ntpnstime);

/* processing packets for sending */
GstFlowReturn   rtp_session_send_rtp               (RTPSession *sess, gpointer data, gboolean is_list,
                                                    GstClockTime current_time, GstClockTime running_time);

/* stopping the session */
GstFlowReturn   rtp_session_schedule_bye           (RTPSession *sess, const gchar *reason,
                                                    GstClockTime current_time);

/* get interval for next RTCP interval */
GstClockTime    rtp_session_next_timeout           (RTPSession *sess, GstClockTime current_time);
GstFlowReturn   rtp_session_on_timeout             (RTPSession *sess, GstClockTime current_time,
                                                    guint64 ntpnstime, GstClockTime running_time);

/* request the transmittion of an early RTCP packet */
void            rtp_session_request_early_rtcp     (RTPSession * sess, GstClockTime current_time,
                                                    GstClockTimeDiff max_delay);

/* Notify session of a request for a new key unit */
void            rtp_session_request_key_unit       (RTPSession * sess,
                                                    guint32 ssrc);

#endif /* __RTP_SESSION_H__ */
