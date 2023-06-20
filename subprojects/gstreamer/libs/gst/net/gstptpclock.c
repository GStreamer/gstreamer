/* GStreamer
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
 *
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
/**
 * SECTION:gstptpclock
 * @title: GstPtpClock
 * @short_description: Special clock that synchronizes to a remote time
 *                     provider via PTP (IEEE1588:2008).
 * @see_also: #GstClock, #GstNetClientClock, #GstPipeline
 *
 * GstPtpClock implements a PTP (IEEE1588:2008) ordinary clock in slave-only
 * mode, that allows a GStreamer pipeline to synchronize to a PTP network
 * clock in some specific domain.
 *
 * The PTP subsystem can be initialized with gst_ptp_init(), which then starts
 * a helper process to do the actual communication via the PTP ports. This is
 * required as PTP listens on ports < 1024 and thus requires special
 * privileges. Once this helper process is started, the main process will
 * synchronize to all PTP domains that are detected on the selected
 * interfaces.
 *
 * gst_ptp_clock_new() then allows to create a GstClock that provides the PTP
 * time from a master clock inside a specific PTP domain. This clock will only
 * return valid timestamps once the timestamps in the PTP domain are known. To
 * check this, you can use gst_clock_wait_for_sync(), the GstClock::synced
 * signal and gst_clock_is_synced().
 *
 * To gather statistics about the PTP clock synchronization,
 * gst_ptp_statistics_callback_add() can be used. This gives the application
 * the possibility to collect all kinds of statistics from the clock
 * synchronization.
 *
 * Since: 1.6
 *
 */
#define _GNU_SOURCE 1
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstptpclock.h"

#include <gio/gio.h>

#include <gst/base/base.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <processthreadsapi.h>  /* GetCurrentProcessId */
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef G_OS_WIN32
#include <windows.h>
static HMODULE gstnet_dll_handle = NULL;
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

GST_DEBUG_CATEGORY_STATIC (ptp_debug);
#define GST_CAT_DEFAULT (ptp_debug)

/* IEEE 1588 7.7.3.1 */
#define PTP_ANNOUNCE_RECEIPT_TIMEOUT 4

/* Use a running average for calculating the mean path delay instead
 * of just using the last measurement. Enabling this helps in unreliable
 * networks, like wifi, with often changing delays
 *
 * Undef for following IEEE1588-2008 by the letter
 */
#define USE_RUNNING_AVERAGE_DELAY 1

/* Filter out any measurements that are above a certain threshold compared to
 * previous measurements. Enabling this helps filtering out outliers that
 * happen fairly often in unreliable networks, like wifi.
 *
 * Undef for following IEEE1588-2008 by the letter
 */
#define USE_MEASUREMENT_FILTERING 1

/* Select the first clock from which we capture a SYNC message as the master
 * clock of the domain until we are ready to run the best master clock
 * algorithm. This allows faster syncing but might mean a change of the master
 * clock in the beginning. As all clocks in a domain are supposed to use the
 * same time, this shouldn't be much of a problem.
 *
 * Undef for following IEEE1588-2008 by the letter
 */
#define USE_OPPORTUNISTIC_CLOCK_SELECTION 1

/* Only consider SYNC messages for which we are allowed to send a DELAY_REQ
 * afterwards. This allows better synchronization in networks with varying
 * delays, as for every other SYNC message we would have to assume that it's
 * the average of what we saw before. But that might be completely off
 */
#define USE_ONLY_SYNC_WITH_DELAY 1

/* Filter out delay measurements that are too far away from the median of the
 * last delay measurements, currently those that are more than 2 times as big.
 * This increases accuracy a lot on wifi.
 */
#define USE_MEDIAN_PRE_FILTERING 1
#define MEDIAN_PRE_FILTERING_WINDOW 9

/* How many updates should be skipped at maximum when using USE_MEASUREMENT_FILTERING */
#define MAX_SKIPPED_UPDATES 5

typedef enum
{
  PTP_MESSAGE_TYPE_SYNC = 0x0,
  PTP_MESSAGE_TYPE_DELAY_REQ = 0x1,
  PTP_MESSAGE_TYPE_PDELAY_REQ = 0x2,
  PTP_MESSAGE_TYPE_PDELAY_RESP = 0x3,
  PTP_MESSAGE_TYPE_FOLLOW_UP = 0x8,
  PTP_MESSAGE_TYPE_DELAY_RESP = 0x9,
  PTP_MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP = 0xA,
  PTP_MESSAGE_TYPE_ANNOUNCE = 0xB,
  PTP_MESSAGE_TYPE_SIGNALING = 0xC,
  PTP_MESSAGE_TYPE_MANAGEMENT = 0xD
} PtpMessageType;

typedef struct
{
  guint64 seconds_field;        /* 48 bits valid */
  guint32 nanoseconds_field;
} PtpTimestamp;

#define PTP_TIMESTAMP_TO_GST_CLOCK_TIME(ptp) (ptp.seconds_field * GST_SECOND + ptp.nanoseconds_field)
#define GST_CLOCK_TIME_TO_PTP_TIMESTAMP_SECONDS(gst) (((GstClockTime) gst) / GST_SECOND)
#define GST_CLOCK_TIME_TO_PTP_TIMESTAMP_NANOSECONDS(gst) (((GstClockTime) gst) % GST_SECOND)

typedef struct
{
  guint64 clock_identity;
  guint16 port_number;
} PtpClockIdentity;

static gint
compare_clock_identity (const PtpClockIdentity * a, const PtpClockIdentity * b)
{
  if (a->clock_identity < b->clock_identity)
    return -1;
  else if (a->clock_identity > b->clock_identity)
    return 1;

  if (a->port_number < b->port_number)
    return -1;
  else if (a->port_number > b->port_number)
    return 1;

  return 0;
}

typedef struct
{
  guint8 clock_class;
  guint8 clock_accuracy;
  guint16 offset_scaled_log_variance;
} PtpClockQuality;

typedef struct
{
  guint8 transport_specific;
  PtpMessageType message_type;
  /* guint8 reserved; */
  guint8 version_ptp;
  guint16 message_length;
  guint8 domain_number;
  /* guint8 reserved; */
  guint16 flag_field;
  gint64 correction_field;      /* 48.16 fixed point nanoseconds */
  /* guint32 reserved; */
  PtpClockIdentity source_port_identity;
  guint16 sequence_id;
  guint8 control_field;
  gint8 log_message_interval;

  union
  {
    struct
    {
      PtpTimestamp origin_timestamp;
      gint16 current_utc_offset;
      /* guint8 reserved; */
      guint8 grandmaster_priority_1;
      PtpClockQuality grandmaster_clock_quality;
      guint8 grandmaster_priority_2;
      guint64 grandmaster_identity;
      guint16 steps_removed;
      guint8 time_source;
    } announce;

    struct
    {
      PtpTimestamp origin_timestamp;
    } sync;

    struct
    {
      PtpTimestamp precise_origin_timestamp;
    } follow_up;

    struct
    {
      PtpTimestamp origin_timestamp;
    } delay_req;

    struct
    {
      PtpTimestamp receive_timestamp;
      PtpClockIdentity requesting_port_identity;
    } delay_resp;

  } message_specific;
} PtpMessage;

typedef enum
{
  TYPE_EVENT = 0,               /* 64-bit monotonic clock time and PTP message is payload */
  TYPE_GENERAL = 1,             /* 64-bit monotonic clock time and PTP message is payload */
  TYPE_CLOCK_ID = 2,            /* 64-bit clock ID is payload */
  TYPE_SEND_TIME_ACK = 3,       /* 64-bit monotonic clock time, 8-bit message type, 8-bit domain number and 16-bit sequence number is payload */
} StdIOMessageType;

/* 2 byte BE payload size plus 1 byte message type */
#define STDIO_MESSAGE_HEADER_SIZE (3)

/* 2 byte BE payload size. Payload format:
 * - 1 byte GstDebugLevel
 * - 2 byte BE filename length
 * - filename UTF-8 string
 * - 2 byte BE module path length
 * - module path UTF-8 string
 * - 4 byte BE line number
 * - remainder is UTF-8 string
 */
#define STDERR_MESSAGE_HEADER_SIZE (2)

static GMutex ptp_lock;
static GCond ptp_cond;
static gboolean initted = FALSE;
#ifdef HAVE_PTP
static gboolean supported = TRUE;
#else
static gboolean supported = FALSE;
#endif
static GSubprocess *ptp_helper_process;
static GInputStream *stdout_pipe;
static GInputStream *stderr_pipe;
static GOutputStream *stdin_pipe;
static guint8 stdio_header[STDIO_MESSAGE_HEADER_SIZE];  /* buffer for reading the message header */
static guint8 stdout_buffer[8192];      /* buffer for reading the message payload */
static guint8 stderr_header[STDERR_MESSAGE_HEADER_SIZE];        /* buffer for reading the message header */
static guint8 stderr_buffer[8192];      /* buffer for reading the message payload */
static GThread *ptp_helper_thread;
static GMainContext *main_context;
static GMainLoop *main_loop;
static GRand *delay_req_rand;
static GstClock *observation_system_clock;
static PtpClockIdentity ptp_clock_id = { GST_PTP_CLOCK_ID_NONE, 0 };

#define CUR_STDIO_HEADER_SIZE (GST_READ_UINT16_BE (stdio_header))
#define CUR_STDIO_HEADER_TYPE ((StdIOMessageType) stdio_header[2])

#define CUR_STDERR_HEADER_SIZE (GST_READ_UINT16_BE (stderr_header))

typedef struct
{
  GstClockTime receive_time;

  PtpClockIdentity master_clock_identity;

  guint8 grandmaster_priority_1;
  PtpClockQuality grandmaster_clock_quality;
  guint8 grandmaster_priority_2;
  guint64 grandmaster_identity;
  guint16 steps_removed;
  guint8 time_source;

  guint16 sequence_id;
} PtpAnnounceMessage;

typedef struct
{
  PtpClockIdentity master_clock_identity;

  GstClockTime announce_interval;       /* last interval we received */
  GQueue announce_messages;
} PtpAnnounceSender;

typedef struct
{
  guint domain;
  PtpClockIdentity master_clock_identity;

  guint16 sync_seqnum;
  GstClockTime sync_recv_time_local;    /* t2 */
  GstClockTime sync_send_time_remote;   /* t1, might be -1 if FOLLOW_UP pending */
  GstClockTime follow_up_recv_time_local;

  GSource *timeout_source;
  guint16 delay_req_seqnum;
  GstClockTime delay_req_send_time_local;       /* t3, -1 if we wait for FOLLOW_UP */
  GstClockTime delay_req_recv_time_remote;      /* t4, -1 if we wait */
  GstClockTime delay_resp_recv_time_local;

  gint64 correction_field_sync; /* sum of the correction fields of SYNC/FOLLOW_UP */
  gint64 correction_field_delay;        /* sum of the correction fields of DELAY_RESP */
} PtpPendingSync;

static void
ptp_pending_sync_free (PtpPendingSync * sync)
{
  if (sync->timeout_source) {
    g_source_destroy (sync->timeout_source);
    g_source_unref (sync->timeout_source);
  }
  g_free (sync);
}

typedef struct
{
  guint domain;

  GstClockTime last_ptp_time;
  GstClockTime last_local_time;
  gint skipped_updates;

  /* Used for selecting the master/grandmaster */
  GList *announce_senders;

  /* Last selected master clock */
  gboolean have_master_clock;
  PtpClockIdentity master_clock_identity;
  guint64 grandmaster_identity;

  /* Last SYNC or FOLLOW_UP timestamp we received */
  GstClockTime last_ptp_sync_time;
  GstClockTime sync_interval;

  GstClockTime mean_path_delay;
  GstClockTime last_delay_req, min_delay_req_interval;
  guint16 last_delay_req_seqnum;

  GstClockTime last_path_delays[MEDIAN_PRE_FILTERING_WINDOW];
  gint last_path_delays_missing;

  GQueue pending_syncs;

  GstClock *domain_clock;
} PtpDomainData;

static GList *domain_data;
static GMutex domain_clocks_lock;
static GList *domain_clocks;

/* Protected by PTP lock */
static void emit_ptp_statistics (guint8 domain, const GstStructure * stats);
static GHookList domain_stats_hooks;
static gint domain_stats_n_hooks;
static gboolean domain_stats_hooks_initted = FALSE;

/* Only ever accessed from the PTP thread */
/* PTPD in hybrid mode (default) sends multicast PTP messages with an invalid
 * logMessageInterval. We work around this here and warn once */
static gboolean ptpd_hybrid_workaround_warned_once = FALSE;

/* Converts log2 seconds to GstClockTime */
static GstClockTime
log2_to_clock_time (gint l)
{
  if (l < 0)
    return GST_SECOND >> (-l);
  else
    return GST_SECOND << l;
}

static void
dump_ptp_message (PtpMessage * msg)
{
  GST_TRACE ("PTP message:");
  GST_TRACE ("\ttransport_specific: %u", msg->transport_specific);
  GST_TRACE ("\tmessage_type: 0x%01x", msg->message_type);
  GST_TRACE ("\tversion_ptp: %u", msg->version_ptp);
  GST_TRACE ("\tmessage_length: %u", msg->message_length);
  GST_TRACE ("\tdomain_number: %u", msg->domain_number);
  GST_TRACE ("\tflag_field: 0x%04x", msg->flag_field);
  GST_TRACE ("\tcorrection_field: %" G_GINT64_FORMAT ".%03u",
      (msg->correction_field / 65536),
      (guint) ((msg->correction_field & 0xffff) * 1000) / 65536);
  GST_TRACE ("\tsource_port_identity: 0x%016" G_GINT64_MODIFIER "x %u",
      msg->source_port_identity.clock_identity,
      msg->source_port_identity.port_number);
  GST_TRACE ("\tsequence_id: %u", msg->sequence_id);
  GST_TRACE ("\tcontrol_field: 0x%02x", msg->control_field);
  GST_TRACE ("\tmessage_interval: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (log2_to_clock_time (msg->log_message_interval)));

  switch (msg->message_type) {
    case PTP_MESSAGE_TYPE_ANNOUNCE:
      GST_TRACE ("\tANNOUNCE:");
      GST_TRACE ("\t\torigin_timestamp: %" G_GUINT64_FORMAT ".%09u",
          msg->message_specific.announce.origin_timestamp.seconds_field,
          msg->message_specific.announce.origin_timestamp.nanoseconds_field);
      GST_TRACE ("\t\tcurrent_utc_offset: %d",
          msg->message_specific.announce.current_utc_offset);
      GST_TRACE ("\t\tgrandmaster_priority_1: %u",
          msg->message_specific.announce.grandmaster_priority_1);
      GST_TRACE ("\t\tgrandmaster_clock_quality: 0x%02x 0x%02x %u",
          msg->message_specific.announce.grandmaster_clock_quality.clock_class,
          msg->message_specific.announce.
          grandmaster_clock_quality.clock_accuracy,
          msg->message_specific.announce.
          grandmaster_clock_quality.offset_scaled_log_variance);
      GST_TRACE ("\t\tgrandmaster_priority_2: %u",
          msg->message_specific.announce.grandmaster_priority_2);
      GST_TRACE ("\t\tgrandmaster_identity: 0x%016" G_GINT64_MODIFIER "x",
          msg->message_specific.announce.grandmaster_identity);
      GST_TRACE ("\t\tsteps_removed: %u",
          msg->message_specific.announce.steps_removed);
      GST_TRACE ("\t\ttime_source: 0x%02x",
          msg->message_specific.announce.time_source);
      break;
    case PTP_MESSAGE_TYPE_SYNC:
      GST_TRACE ("\tSYNC:");
      GST_TRACE ("\t\torigin_timestamp: %" G_GUINT64_FORMAT ".%09u",
          msg->message_specific.sync.origin_timestamp.seconds_field,
          msg->message_specific.sync.origin_timestamp.nanoseconds_field);
      break;
    case PTP_MESSAGE_TYPE_FOLLOW_UP:
      GST_TRACE ("\tFOLLOW_UP:");
      GST_TRACE ("\t\tprecise_origin_timestamp: %" G_GUINT64_FORMAT ".%09u",
          msg->message_specific.follow_up.
          precise_origin_timestamp.seconds_field,
          msg->message_specific.follow_up.
          precise_origin_timestamp.nanoseconds_field);
      break;
    case PTP_MESSAGE_TYPE_DELAY_REQ:
      GST_TRACE ("\tDELAY_REQ:");
      GST_TRACE ("\t\torigin_timestamp: %" G_GUINT64_FORMAT ".%09u",
          msg->message_specific.delay_req.origin_timestamp.seconds_field,
          msg->message_specific.delay_req.origin_timestamp.nanoseconds_field);
      break;
    case PTP_MESSAGE_TYPE_DELAY_RESP:
      GST_TRACE ("\tDELAY_RESP:");
      GST_TRACE ("\t\treceive_timestamp: %" G_GUINT64_FORMAT ".%09u",
          msg->message_specific.delay_resp.receive_timestamp.seconds_field,
          msg->message_specific.delay_resp.receive_timestamp.nanoseconds_field);
      GST_TRACE ("\t\trequesting_port_identity: 0x%016" G_GINT64_MODIFIER
          "x %u",
          msg->message_specific.delay_resp.
          requesting_port_identity.clock_identity,
          msg->message_specific.delay_resp.
          requesting_port_identity.port_number);
      break;
    default:
      break;
  }
  GST_TRACE (" ");
}

/* IEEE 1588-2008 5.3.3 */
static gboolean
parse_ptp_timestamp (PtpTimestamp * timestamp, GstByteReader * reader)
{
  g_return_val_if_fail (gst_byte_reader_get_remaining (reader) >= 10, FALSE);

  timestamp->seconds_field =
      (((guint64) gst_byte_reader_get_uint32_be_unchecked (reader)) << 16) |
      gst_byte_reader_get_uint16_be_unchecked (reader);
  timestamp->nanoseconds_field =
      gst_byte_reader_get_uint32_be_unchecked (reader);

  if (timestamp->nanoseconds_field >= 1000000000)
    return FALSE;

  return TRUE;
}

/* IEEE 1588-2008 13.3 */
static gboolean
parse_ptp_message_header (PtpMessage * msg, GstByteReader * reader)
{
  guint8 b;

  g_return_val_if_fail (gst_byte_reader_get_remaining (reader) >= 34, FALSE);

  b = gst_byte_reader_get_uint8_unchecked (reader);
  msg->transport_specific = b >> 4;
  msg->message_type = b & 0x0f;

  b = gst_byte_reader_get_uint8_unchecked (reader);
  msg->version_ptp = b & 0x0f;
  if (msg->version_ptp != 2) {
    GST_WARNING ("Unsupported PTP message version (%u != 2)", msg->version_ptp);
    return FALSE;
  }

  msg->message_length = gst_byte_reader_get_uint16_be_unchecked (reader);
  if (gst_byte_reader_get_remaining (reader) + 4 < msg->message_length) {
    GST_WARNING ("Not enough data (%u < %u)",
        gst_byte_reader_get_remaining (reader) + 4, msg->message_length);
    return FALSE;
  }

  msg->domain_number = gst_byte_reader_get_uint8_unchecked (reader);
  gst_byte_reader_skip_unchecked (reader, 1);

  msg->flag_field = gst_byte_reader_get_uint16_be_unchecked (reader);
  msg->correction_field = gst_byte_reader_get_uint64_be_unchecked (reader);
  gst_byte_reader_skip_unchecked (reader, 4);

  msg->source_port_identity.clock_identity =
      gst_byte_reader_get_uint64_be_unchecked (reader);
  msg->source_port_identity.port_number =
      gst_byte_reader_get_uint16_be_unchecked (reader);

  msg->sequence_id = gst_byte_reader_get_uint16_be_unchecked (reader);
  msg->control_field = gst_byte_reader_get_uint8_unchecked (reader);
  msg->log_message_interval = gst_byte_reader_get_uint8_unchecked (reader);

  return TRUE;
}

/* IEEE 1588-2008 13.5 */
static gboolean
parse_ptp_message_announce (PtpMessage * msg, GstByteReader * reader)
{
  g_return_val_if_fail (msg->message_type == PTP_MESSAGE_TYPE_ANNOUNCE, FALSE);

  if (gst_byte_reader_get_remaining (reader) < 20)
    return FALSE;

  if (!parse_ptp_timestamp (&msg->message_specific.announce.origin_timestamp,
          reader))
    return FALSE;

  msg->message_specific.announce.current_utc_offset =
      gst_byte_reader_get_uint16_be_unchecked (reader);
  gst_byte_reader_skip_unchecked (reader, 1);

  msg->message_specific.announce.grandmaster_priority_1 =
      gst_byte_reader_get_uint8_unchecked (reader);
  msg->message_specific.announce.grandmaster_clock_quality.clock_class =
      gst_byte_reader_get_uint8_unchecked (reader);
  msg->message_specific.announce.grandmaster_clock_quality.clock_accuracy =
      gst_byte_reader_get_uint8_unchecked (reader);
  msg->message_specific.announce.
      grandmaster_clock_quality.offset_scaled_log_variance =
      gst_byte_reader_get_uint16_be_unchecked (reader);
  msg->message_specific.announce.grandmaster_priority_2 =
      gst_byte_reader_get_uint8_unchecked (reader);
  msg->message_specific.announce.grandmaster_identity =
      gst_byte_reader_get_uint64_be_unchecked (reader);
  msg->message_specific.announce.steps_removed =
      gst_byte_reader_get_uint16_be_unchecked (reader);
  msg->message_specific.announce.time_source =
      gst_byte_reader_get_uint8_unchecked (reader);

  return TRUE;
}

/* IEEE 1588-2008 13.6 */
static gboolean
parse_ptp_message_sync (PtpMessage * msg, GstByteReader * reader)
{
  g_return_val_if_fail (msg->message_type == PTP_MESSAGE_TYPE_SYNC, FALSE);

  if (gst_byte_reader_get_remaining (reader) < 10)
    return FALSE;

  if (!parse_ptp_timestamp (&msg->message_specific.sync.origin_timestamp,
          reader))
    return FALSE;

  return TRUE;
}

/* IEEE 1588-2008 13.6 */
static gboolean
parse_ptp_message_delay_req (PtpMessage * msg, GstByteReader * reader)
{
  g_return_val_if_fail (msg->message_type == PTP_MESSAGE_TYPE_DELAY_REQ, FALSE);

  if (gst_byte_reader_get_remaining (reader) < 10)
    return FALSE;

  if (!parse_ptp_timestamp (&msg->message_specific.delay_req.origin_timestamp,
          reader))
    return FALSE;

  return TRUE;
}

/* IEEE 1588-2008 13.7 */
static gboolean
parse_ptp_message_follow_up (PtpMessage * msg, GstByteReader * reader)
{
  g_return_val_if_fail (msg->message_type == PTP_MESSAGE_TYPE_FOLLOW_UP, FALSE);

  if (gst_byte_reader_get_remaining (reader) < 10)
    return FALSE;

  if (!parse_ptp_timestamp (&msg->message_specific.
          follow_up.precise_origin_timestamp, reader))
    return FALSE;

  return TRUE;
}

/* IEEE 1588-2008 13.8 */
static gboolean
parse_ptp_message_delay_resp (PtpMessage * msg, GstByteReader * reader)
{
  g_return_val_if_fail (msg->message_type == PTP_MESSAGE_TYPE_DELAY_RESP,
      FALSE);

  if (gst_byte_reader_get_remaining (reader) < 20)
    return FALSE;

  if (!parse_ptp_timestamp (&msg->message_specific.delay_resp.receive_timestamp,
          reader))
    return FALSE;

  msg->message_specific.delay_resp.requesting_port_identity.clock_identity =
      gst_byte_reader_get_uint64_be_unchecked (reader);
  msg->message_specific.delay_resp.requesting_port_identity.port_number =
      gst_byte_reader_get_uint16_be_unchecked (reader);

  return TRUE;
}

static gboolean
parse_ptp_message (PtpMessage * msg, const guint8 * data, gsize size)
{
  GstByteReader reader;
  gboolean ret = FALSE;

  gst_byte_reader_init (&reader, data, size);

  if (!parse_ptp_message_header (msg, &reader)) {
    GST_WARNING ("Failed to parse PTP message header");
    return FALSE;
  }

  switch (msg->message_type) {
    case PTP_MESSAGE_TYPE_SYNC:
      ret = parse_ptp_message_sync (msg, &reader);
      break;
    case PTP_MESSAGE_TYPE_FOLLOW_UP:
      ret = parse_ptp_message_follow_up (msg, &reader);
      break;
    case PTP_MESSAGE_TYPE_DELAY_REQ:
      ret = parse_ptp_message_delay_req (msg, &reader);
      break;
    case PTP_MESSAGE_TYPE_DELAY_RESP:
      ret = parse_ptp_message_delay_resp (msg, &reader);
      break;
    case PTP_MESSAGE_TYPE_ANNOUNCE:
      ret = parse_ptp_message_announce (msg, &reader);
      break;
    default:
      /* ignore for now */
      break;
  }

  return ret;
}

static gint
compare_announce_message (const PtpAnnounceMessage * a,
    const PtpAnnounceMessage * b)
{
  /* IEEE 1588 Figure 27 */
  if (a->grandmaster_identity == b->grandmaster_identity) {
    if (a->steps_removed + 1 < b->steps_removed)
      return -1;
    else if (a->steps_removed > b->steps_removed + 1)
      return 1;

    /* Error cases are filtered out earlier */
    if (a->steps_removed < b->steps_removed)
      return -1;
    else if (a->steps_removed > b->steps_removed)
      return 1;

    /* Error cases are filtered out earlier */
    if (a->master_clock_identity.clock_identity <
        b->master_clock_identity.clock_identity)
      return -1;
    else if (a->master_clock_identity.clock_identity >
        b->master_clock_identity.clock_identity)
      return 1;

    /* Error cases are filtered out earlier */
    if (a->master_clock_identity.port_number <
        b->master_clock_identity.port_number)
      return -1;
    else if (a->master_clock_identity.port_number >
        b->master_clock_identity.port_number)
      return 1;
    else
      g_assert_not_reached ();

    return 0;
  }

  if (a->grandmaster_priority_1 < b->grandmaster_priority_1)
    return -1;
  else if (a->grandmaster_priority_1 > b->grandmaster_priority_1)
    return 1;

  if (a->grandmaster_clock_quality.clock_class <
      b->grandmaster_clock_quality.clock_class)
    return -1;
  else if (a->grandmaster_clock_quality.clock_class >
      b->grandmaster_clock_quality.clock_class)
    return 1;

  if (a->grandmaster_clock_quality.clock_accuracy <
      b->grandmaster_clock_quality.clock_accuracy)
    return -1;
  else if (a->grandmaster_clock_quality.clock_accuracy >
      b->grandmaster_clock_quality.clock_accuracy)
    return 1;

  if (a->grandmaster_clock_quality.offset_scaled_log_variance <
      b->grandmaster_clock_quality.offset_scaled_log_variance)
    return -1;
  else if (a->grandmaster_clock_quality.offset_scaled_log_variance >
      b->grandmaster_clock_quality.offset_scaled_log_variance)
    return 1;

  if (a->grandmaster_priority_2 < b->grandmaster_priority_2)
    return -1;
  else if (a->grandmaster_priority_2 > b->grandmaster_priority_2)
    return 1;

  if (a->grandmaster_identity < b->grandmaster_identity)
    return -1;
  else if (a->grandmaster_identity > b->grandmaster_identity)
    return 1;
  else
    g_assert_not_reached ();

  return 0;
}

static void
select_best_master_clock (PtpDomainData * domain, GstClockTime now)
{
  GList *qualified_messages = NULL;
  GList *l, *m;
  PtpAnnounceMessage *best = NULL;

  /* IEEE 1588 9.3.2.5 */
  for (l = domain->announce_senders; l; l = l->next) {
    PtpAnnounceSender *sender = l->data;
    GstClockTime window = 4 * sender->announce_interval;
    gint count = 0;

    for (m = sender->announce_messages.head; m; m = m->next) {
      PtpAnnounceMessage *msg = m->data;

      if (now - msg->receive_time <= window)
        count++;
    }

    /* Only include the newest message of announce senders that had at least 2
     * announce messages in the last 4 announce intervals. Which also means
     * that we wait at least 4 announce intervals before we select a master
     * clock. Until then we just report based on the newest SYNC we received
     */
    if (count >= 2) {
      qualified_messages =
          g_list_prepend (qualified_messages,
          g_queue_peek_tail (&sender->announce_messages));
    }
  }

  if (!qualified_messages) {
    GST_DEBUG
        ("No qualified announce messages for domain %u, can't select a master clock",
        domain->domain);
    domain->have_master_clock = FALSE;
    return;
  }

  for (l = qualified_messages; l; l = l->next) {
    PtpAnnounceMessage *msg = l->data;

    if (!best || compare_announce_message (msg, best) < 0)
      best = msg;
  }
  g_clear_pointer (&qualified_messages, g_list_free);

  if (domain->have_master_clock
      && compare_clock_identity (&domain->master_clock_identity,
          &best->master_clock_identity) == 0) {
    GST_DEBUG ("Master clock in domain %u did not change", domain->domain);
  } else {
    GST_DEBUG ("Selected master clock for domain %u: 0x%016" G_GINT64_MODIFIER
        "x %u with grandmaster clock 0x%016" G_GINT64_MODIFIER "x",
        domain->domain, best->master_clock_identity.clock_identity,
        best->master_clock_identity.port_number, best->grandmaster_identity);

    domain->have_master_clock = TRUE;
    domain->grandmaster_identity = best->grandmaster_identity;

    /* Opportunistic master clock selection likely gave us the same master
     * clock before, no need to reset all statistics */
    if (compare_clock_identity (&domain->master_clock_identity,
            &best->master_clock_identity) != 0) {
      memcpy (&domain->master_clock_identity, &best->master_clock_identity,
          sizeof (PtpClockIdentity));
      domain->mean_path_delay = 0;
      domain->last_delay_req = 0;
      domain->last_path_delays_missing = 9;
      domain->min_delay_req_interval = 0;
      domain->sync_interval = 0;
      domain->last_ptp_sync_time = 0;
      domain->skipped_updates = 0;
      g_queue_foreach (&domain->pending_syncs, (GFunc) ptp_pending_sync_free,
          NULL);
      g_queue_clear (&domain->pending_syncs);
    }

    if (g_atomic_int_get (&domain_stats_n_hooks)) {
      GstStructure *stats =
          gst_structure_new (GST_PTP_STATISTICS_BEST_MASTER_CLOCK_SELECTED,
          "domain", G_TYPE_UINT, domain->domain,
          "master-clock-id", G_TYPE_UINT64,
          domain->master_clock_identity.clock_identity,
          "master-clock-port", G_TYPE_UINT,
          domain->master_clock_identity.port_number,
          "grandmaster-clock-id", G_TYPE_UINT64, domain->grandmaster_identity,
          NULL);
      emit_ptp_statistics (domain->domain, stats);
      gst_structure_free (stats);
    }
  }
}

static void
handle_announce_message (PtpMessage * msg, GstClockTime receive_time)
{
  GList *l;
  PtpDomainData *domain = NULL;
  PtpAnnounceSender *sender = NULL;
  PtpAnnounceMessage *announce;

  /* IEEE1588 9.3.2.2 e)
   * Don't consider messages with the alternate master flag set
   */
  if ((msg->flag_field & 0x0100))
    return;

  /* IEEE 1588 9.3.2.5 d)
   * Don't consider announce messages with steps_removed>=255
   */
  if (msg->message_specific.announce.steps_removed >= 255)
    return;

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *tmp = l->data;

    if (tmp->domain == msg->domain_number) {
      domain = tmp;
      break;
    }
  }

  if (!domain) {
    gchar *clock_name;

    domain = g_new0 (PtpDomainData, 1);
    domain->domain = msg->domain_number;
    clock_name = g_strdup_printf ("ptp-clock-%u", domain->domain);
    domain->domain_clock =
        g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", clock_name, NULL);
    gst_object_ref_sink (domain->domain_clock);
    g_free (clock_name);
    g_queue_init (&domain->pending_syncs);
    domain->last_path_delays_missing = 9;
    domain_data = g_list_prepend (domain_data, domain);

    g_mutex_lock (&domain_clocks_lock);
    domain_clocks = g_list_prepend (domain_clocks, domain);
    g_mutex_unlock (&domain_clocks_lock);

    if (g_atomic_int_get (&domain_stats_n_hooks)) {
      GstStructure *stats =
          gst_structure_new (GST_PTP_STATISTICS_NEW_DOMAIN_FOUND, "domain",
          G_TYPE_UINT, domain->domain, "clock", GST_TYPE_CLOCK,
          domain->domain_clock, NULL);
      emit_ptp_statistics (domain->domain, stats);
      gst_structure_free (stats);
    }
  }

  for (l = domain->announce_senders; l; l = l->next) {
    PtpAnnounceSender *tmp = l->data;

    if (compare_clock_identity (&tmp->master_clock_identity,
            &msg->source_port_identity) == 0) {
      sender = tmp;
      break;
    }
  }

  if (!sender) {
    sender = g_new0 (PtpAnnounceSender, 1);

    memcpy (&sender->master_clock_identity, &msg->source_port_identity,
        sizeof (PtpClockIdentity));
    g_queue_init (&sender->announce_messages);
    domain->announce_senders =
        g_list_prepend (domain->announce_senders, sender);
  }

  for (l = sender->announce_messages.head; l; l = l->next) {
    PtpAnnounceMessage *tmp = l->data;

    /* IEEE 1588 9.3.2.5 c)
     * Don't consider identical messages, i.e. duplicates
     */
    if (tmp->sequence_id == msg->sequence_id)
      return;
  }

  if (msg->log_message_interval == 0x7f) {
    sender->announce_interval = 2 * GST_SECOND;

    if (!ptpd_hybrid_workaround_warned_once) {
      GST_WARNING ("Working around ptpd bug: ptpd sends multicast PTP packets "
          "with invalid logMessageInterval");
      ptpd_hybrid_workaround_warned_once = TRUE;
    }
  } else {
    sender->announce_interval = log2_to_clock_time (msg->log_message_interval);
  }

  announce = g_new0 (PtpAnnounceMessage, 1);
  announce->receive_time = receive_time;
  announce->sequence_id = msg->sequence_id;
  memcpy (&announce->master_clock_identity, &msg->source_port_identity,
      sizeof (PtpClockIdentity));
  announce->grandmaster_identity =
      msg->message_specific.announce.grandmaster_identity;
  announce->grandmaster_priority_1 =
      msg->message_specific.announce.grandmaster_priority_1;
  announce->grandmaster_clock_quality.clock_class =
      msg->message_specific.announce.grandmaster_clock_quality.clock_class;
  announce->grandmaster_clock_quality.clock_accuracy =
      msg->message_specific.announce.grandmaster_clock_quality.clock_accuracy;
  announce->grandmaster_clock_quality.offset_scaled_log_variance =
      msg->message_specific.announce.
      grandmaster_clock_quality.offset_scaled_log_variance;
  announce->grandmaster_priority_2 =
      msg->message_specific.announce.grandmaster_priority_2;
  announce->steps_removed = msg->message_specific.announce.steps_removed;
  announce->time_source = msg->message_specific.announce.time_source;
  g_queue_push_tail (&sender->announce_messages, announce);

  select_best_master_clock (domain, receive_time);
}

static gboolean
send_delay_req_timeout (PtpPendingSync * sync)
{
  guint8 message[STDIO_MESSAGE_HEADER_SIZE + 8 + 44] = { 0, };
  GstByteWriter writer;
  gsize written;
  GError *err = NULL;
  GstClockTime send_time;

  GST_TRACE ("Sending delay_req to domain %u", sync->domain);

  sync->delay_req_send_time_local = send_time =
      gst_clock_get_time (observation_system_clock);

  gst_byte_writer_init_with_data (&writer, message, sizeof (message), FALSE);
  gst_byte_writer_put_uint16_be_unchecked (&writer, 8 + 44);
  gst_byte_writer_put_uint8_unchecked (&writer, TYPE_EVENT);
  gst_byte_writer_put_uint64_be_unchecked (&writer, send_time);
  gst_byte_writer_put_uint8_unchecked (&writer, PTP_MESSAGE_TYPE_DELAY_REQ);
  gst_byte_writer_put_uint8_unchecked (&writer, 2);
  gst_byte_writer_put_uint16_be_unchecked (&writer, 44);
  gst_byte_writer_put_uint8_unchecked (&writer, sync->domain);
  gst_byte_writer_put_uint8_unchecked (&writer, 0);
  gst_byte_writer_put_uint16_be_unchecked (&writer, 0);
  gst_byte_writer_put_uint64_be_unchecked (&writer, 0);
  gst_byte_writer_put_uint32_be_unchecked (&writer, 0);
  gst_byte_writer_put_uint64_be_unchecked (&writer,
      ptp_clock_id.clock_identity);
  gst_byte_writer_put_uint16_be_unchecked (&writer, ptp_clock_id.port_number);
  gst_byte_writer_put_uint16_be_unchecked (&writer, sync->delay_req_seqnum);
  gst_byte_writer_put_uint8_unchecked (&writer, 0x01);
  gst_byte_writer_put_uint8_unchecked (&writer, 0x7f);
  gst_byte_writer_put_uint64_be_unchecked (&writer, 0);
  gst_byte_writer_put_uint16_be_unchecked (&writer, 0);

  if (!g_output_stream_write_all (stdin_pipe, message, sizeof (message),
          &written, NULL, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CLOSED)
        || g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)) {
      GST_ERROR ("Got EOF on stdout");
    } else {
      GST_ERROR ("Failed to write delay-req to stdin: %s", err->message);
    }

    g_message ("EOF on stdout");
    g_main_loop_quit (main_loop);
    return G_SOURCE_REMOVE;
  } else if (written != sizeof (message)) {
    GST_ERROR ("Unexpected write size: %" G_GSIZE_FORMAT, written);
    g_main_loop_quit (main_loop);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_REMOVE;
}

static gboolean
send_delay_req (PtpDomainData * domain, PtpPendingSync * sync)
{
  GstClockTime now = gst_clock_get_time (observation_system_clock);
  guint timeout;
  GSource *timeout_source;

  if (domain->last_delay_req != 0
      && domain->last_delay_req + domain->min_delay_req_interval > now) {
    GST_TRACE ("Too soon to send new DELAY_REQ");
    return FALSE;
  }

  domain->last_delay_req = now;
  sync->delay_req_seqnum = domain->last_delay_req_seqnum++;

  /* IEEE 1588 9.5.11.2 */
  if (domain->last_delay_req == 0 || domain->min_delay_req_interval == 0)
    timeout = 0;
  else
    timeout =
        g_rand_int_range (delay_req_rand, 0,
        (domain->min_delay_req_interval * 2) / GST_MSECOND);

  sync->timeout_source = timeout_source = g_timeout_source_new (timeout);
  g_source_set_priority (timeout_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (timeout_source, (GSourceFunc) send_delay_req_timeout,
      sync, NULL);
  g_source_attach (timeout_source, main_context);

  return TRUE;
}

/* Filtering of outliers for RTT and time calculations inspired
 * by the code from gstnetclientclock.c
 */
static void
update_ptp_time (PtpDomainData * domain, PtpPendingSync * sync)
{
  GstClockTime internal_time, external_time, rate_num, rate_den;
  GstClockTime corrected_ptp_time, corrected_local_time;
  gdouble r_squared = 0.0;
  gboolean synced;
  GstClockTimeDiff discont = 0;
  GstClockTime estimated_ptp_time = GST_CLOCK_TIME_NONE;
#ifdef USE_MEASUREMENT_FILTERING
  GstClockTime orig_internal_time, orig_external_time, orig_rate_num,
      orig_rate_den;
  GstClockTime new_estimated_ptp_time;
  GstClockTime max_discont, estimated_ptp_time_min, estimated_ptp_time_max;
  gboolean now_synced;
#endif
#ifdef USE_ONLY_SYNC_WITH_DELAY
  GstClockTime mean_path_delay;
#endif

  GST_TRACE ("Updating PTP time");

#ifdef USE_ONLY_SYNC_WITH_DELAY
  if (sync->delay_req_send_time_local == GST_CLOCK_TIME_NONE) {
    GST_TRACE ("Not updating - no delay_req sent");
    return;
  }

  /* IEEE 1588 11.3 */
  mean_path_delay =
      (sync->delay_req_recv_time_remote - sync->sync_send_time_remote +
      sync->sync_recv_time_local - sync->delay_req_send_time_local -
      (sync->correction_field_sync + sync->correction_field_delay +
          32768) / 65536) / 2;
#endif

  /* IEEE 1588 11.2 */
  corrected_ptp_time =
      sync->sync_send_time_remote +
      (sync->correction_field_sync + 32768) / 65536;

#ifdef USE_ONLY_SYNC_WITH_DELAY
  corrected_local_time = sync->sync_recv_time_local - mean_path_delay;
#else
  corrected_local_time = sync->sync_recv_time_local - domain->mean_path_delay;
#endif

#ifdef USE_MEASUREMENT_FILTERING
  /* We check this here and when updating the mean path delay, because
   * we can get here without a delay response too. The tolerance on
   * accepting follow-up after a sync is high, because a PTP server
   * doesn't have to prioritise sending FOLLOW_UP - its purpose is
   * just to give us the accurate timestamp of the preceding SYNC.
   *
   * For that reason also allow at least 100ms delay in case of delays smaller
   * than 5ms. */
  if (sync->follow_up_recv_time_local != GST_CLOCK_TIME_NONE
      && sync->follow_up_recv_time_local >
      sync->sync_recv_time_local + MAX (100 * GST_MSECOND,
          20 * domain->mean_path_delay)) {
    GstClockTimeDiff delay =
        sync->follow_up_recv_time_local - sync->sync_recv_time_local;
    GST_WARNING ("Sync-follow-up delay for domain %u too big: %"
        GST_STIME_FORMAT " > MAX(100ms, 20 * %" GST_TIME_FORMAT ")",
        domain->domain, GST_STIME_ARGS (delay),
        GST_TIME_ARGS (domain->mean_path_delay));
    synced = FALSE;
    gst_clock_get_calibration (GST_CLOCK_CAST (domain->domain_clock),
        &internal_time, &external_time, &rate_num, &rate_den);
    goto out;
  }
#endif

  /* Set an initial local-remote relation */
  if (domain->last_ptp_time == 0)
    gst_clock_set_calibration (domain->domain_clock, corrected_local_time,
        corrected_ptp_time, 1, 1);

#ifdef USE_MEASUREMENT_FILTERING
  /* Check if the corrected PTP time is +/- 3/4 RTT around what we would
   * estimate with our present knowledge about the clock
   */
  /* Store what the clock produced as 'now' before this update */
  gst_clock_get_calibration (GST_CLOCK_CAST (domain->domain_clock),
      &orig_internal_time, &orig_external_time, &orig_rate_num, &orig_rate_den);
  internal_time = orig_internal_time;
  external_time = orig_external_time;
  rate_num = orig_rate_num;
  rate_den = orig_rate_den;

  /* 3/4 RTT window around the estimation */
  max_discont = domain->mean_path_delay * 3 / 2;

  /* Check if the estimated sync time is inside our window */
  estimated_ptp_time_min = corrected_local_time - max_discont;
  estimated_ptp_time_min =
      gst_clock_adjust_with_calibration (GST_CLOCK_CAST (domain->domain_clock),
      estimated_ptp_time_min, internal_time, external_time, rate_num, rate_den);
  estimated_ptp_time_max = corrected_local_time + max_discont;
  estimated_ptp_time_max =
      gst_clock_adjust_with_calibration (GST_CLOCK_CAST (domain->domain_clock),
      estimated_ptp_time_max, internal_time, external_time, rate_num, rate_den);

  synced = (estimated_ptp_time_min < corrected_ptp_time
      && corrected_ptp_time < estimated_ptp_time_max);

  GST_DEBUG ("Adding observation for domain %u: %" GST_TIME_FORMAT " - %"
      GST_TIME_FORMAT, domain->domain,
      GST_TIME_ARGS (corrected_ptp_time), GST_TIME_ARGS (corrected_local_time));

  GST_DEBUG ("Synced %d: %" GST_TIME_FORMAT " < %" GST_TIME_FORMAT " < %"
      GST_TIME_FORMAT, synced, GST_TIME_ARGS (estimated_ptp_time_min),
      GST_TIME_ARGS (corrected_ptp_time),
      GST_TIME_ARGS (estimated_ptp_time_max));

  if (gst_clock_add_observation_unapplied (domain->domain_clock,
          corrected_local_time, corrected_ptp_time, &r_squared,
          &internal_time, &external_time, &rate_num, &rate_den)) {
    GST_DEBUG ("Regression gave r_squared: %f", r_squared);

    /* Old estimated PTP time based on receive time and path delay */
    estimated_ptp_time = corrected_local_time;
    estimated_ptp_time =
        gst_clock_adjust_with_calibration (GST_CLOCK_CAST
        (domain->domain_clock), estimated_ptp_time, orig_internal_time,
        orig_external_time, orig_rate_num, orig_rate_den);

    /* New estimated PTP time based on receive time and path delay */
    new_estimated_ptp_time = corrected_local_time;
    new_estimated_ptp_time =
        gst_clock_adjust_with_calibration (GST_CLOCK_CAST
        (domain->domain_clock), new_estimated_ptp_time, internal_time,
        external_time, rate_num, rate_den);

    discont = GST_CLOCK_DIFF (estimated_ptp_time, new_estimated_ptp_time);
    if (synced && ABS (discont) > max_discont) {
      GstClockTimeDiff offset;
      GST_DEBUG ("Too large a discont %s%" GST_TIME_FORMAT
          ", clamping to 1/4 average RTT = %" GST_TIME_FORMAT,
          (discont < 0 ? "-" : ""), GST_TIME_ARGS (ABS (discont)),
          GST_TIME_ARGS (max_discont));
      if (discont > 0) {        /* Too large a forward step - add a -ve offset */
        offset = max_discont - discont;
        if (-offset > external_time)
          external_time = 0;
        else
          external_time += offset;
      } else {                  /* Too large a backward step - add a +ve offset */
        offset = -(max_discont + discont);
        external_time += offset;
      }

      discont += offset;
    } else {
      GST_DEBUG ("Discont %s%" GST_TIME_FORMAT " (max: %" GST_TIME_FORMAT ")",
          (discont < 0 ? "-" : ""), GST_TIME_ARGS (ABS (discont)),
          GST_TIME_ARGS (max_discont));
    }

    /* Check if the estimated sync time is now (still) inside our window */
    estimated_ptp_time_min = corrected_local_time - max_discont;
    estimated_ptp_time_min =
        gst_clock_adjust_with_calibration (GST_CLOCK_CAST
        (domain->domain_clock), estimated_ptp_time_min, internal_time,
        external_time, rate_num, rate_den);
    estimated_ptp_time_max = corrected_local_time + max_discont;
    estimated_ptp_time_max =
        gst_clock_adjust_with_calibration (GST_CLOCK_CAST
        (domain->domain_clock), estimated_ptp_time_max, internal_time,
        external_time, rate_num, rate_den);

    now_synced = (estimated_ptp_time_min < corrected_ptp_time
        && corrected_ptp_time < estimated_ptp_time_max);

    GST_DEBUG ("Now synced %d: %" GST_TIME_FORMAT " < %" GST_TIME_FORMAT " < %"
        GST_TIME_FORMAT, now_synced, GST_TIME_ARGS (estimated_ptp_time_min),
        GST_TIME_ARGS (corrected_ptp_time),
        GST_TIME_ARGS (estimated_ptp_time_max));

    if (synced || now_synced || domain->skipped_updates > MAX_SKIPPED_UPDATES) {
      gst_clock_set_calibration (GST_CLOCK_CAST (domain->domain_clock),
          internal_time, external_time, rate_num, rate_den);
      domain->skipped_updates = 0;

      domain->last_ptp_time = corrected_ptp_time;
      domain->last_local_time = corrected_local_time;
    } else {
      domain->skipped_updates++;
    }
  } else {
    domain->last_ptp_time = corrected_ptp_time;
    domain->last_local_time = corrected_local_time;
  }

#else
  GST_DEBUG ("Adding observation for domain %u: %" GST_TIME_FORMAT " - %"
      GST_TIME_FORMAT, domain->domain,
      GST_TIME_ARGS (corrected_ptp_time), GST_TIME_ARGS (corrected_local_time));

  gst_clock_get_calibration (GST_CLOCK_CAST (domain->domain_clock),
      &internal_time, &external_time, &rate_num, &rate_den);

  estimated_ptp_time = corrected_local_time;
  estimated_ptp_time =
      gst_clock_adjust_with_calibration (GST_CLOCK_CAST
      (domain->domain_clock), estimated_ptp_time, internal_time,
      external_time, rate_num, rate_den);

  gst_clock_add_observation (domain->domain_clock,
      corrected_local_time, corrected_ptp_time, &r_squared);

  gst_clock_get_calibration (GST_CLOCK_CAST (domain->domain_clock),
      &internal_time, &external_time, &rate_num, &rate_den);

  synced = TRUE;
  domain->last_ptp_time = corrected_ptp_time;
  domain->last_local_time = corrected_local_time;
#endif

#ifdef USE_MEASUREMENT_FILTERING
out:
#endif
  if (g_atomic_int_get (&domain_stats_n_hooks)) {
    GstStructure *stats = gst_structure_new (GST_PTP_STATISTICS_TIME_UPDATED,
        "domain", G_TYPE_UINT, domain->domain,
        "mean-path-delay-avg", GST_TYPE_CLOCK_TIME, domain->mean_path_delay,
        "local-time", GST_TYPE_CLOCK_TIME, corrected_local_time,
        "ptp-time", GST_TYPE_CLOCK_TIME, corrected_ptp_time,
        "estimated-ptp-time", GST_TYPE_CLOCK_TIME, estimated_ptp_time,
        "discontinuity", G_TYPE_INT64, discont,
        "synced", G_TYPE_BOOLEAN, synced,
        "r-squared", G_TYPE_DOUBLE, r_squared,
        "internal-time", GST_TYPE_CLOCK_TIME, internal_time,
        "external-time", GST_TYPE_CLOCK_TIME, external_time,
        "rate-num", G_TYPE_UINT64, rate_num,
        "rate-den", G_TYPE_UINT64, rate_den,
        "rate", G_TYPE_DOUBLE, (gdouble) (rate_num) / rate_den,
        NULL);
    emit_ptp_statistics (domain->domain, stats);
    gst_structure_free (stats);
  }

}

#ifdef USE_MEDIAN_PRE_FILTERING
static gint
compare_clock_time (const GstClockTime * a, const GstClockTime * b)
{
  if (*a < *b)
    return -1;
  else if (*a > *b)
    return 1;
  return 0;
}
#endif

static gboolean
update_mean_path_delay (PtpDomainData * domain, PtpPendingSync * sync)
{
#ifdef USE_MEDIAN_PRE_FILTERING
  GstClockTime last_path_delays[MEDIAN_PRE_FILTERING_WINDOW];
  GstClockTime median;
  gint i;
#endif

  GstClockTime mean_path_delay, delay_req_delay = 0;
  gboolean ret;

  /* IEEE 1588 11.3 */
  mean_path_delay =
      (sync->delay_req_recv_time_remote - sync->sync_send_time_remote +
      sync->sync_recv_time_local - sync->delay_req_send_time_local -
      (sync->correction_field_sync + sync->correction_field_delay +
          32768) / 65536) / 2;

#ifdef USE_MEDIAN_PRE_FILTERING
  for (i = 1; i < MEDIAN_PRE_FILTERING_WINDOW; i++)
    domain->last_path_delays[i - 1] = domain->last_path_delays[i];
  domain->last_path_delays[i - 1] = mean_path_delay;

  if (domain->last_path_delays_missing) {
    domain->last_path_delays_missing--;
  } else {
    memcpy (&last_path_delays, &domain->last_path_delays,
        sizeof (last_path_delays));
    g_qsort_with_data (&last_path_delays,
        MEDIAN_PRE_FILTERING_WINDOW, sizeof (GstClockTime),
        (GCompareDataFunc) compare_clock_time, NULL);

    median = last_path_delays[MEDIAN_PRE_FILTERING_WINDOW / 2];

    /* FIXME: We might want to use something else here, like only allowing
     * things in the interquartile range, or also filtering away delays that
     * are too small compared to the median. This here worked well enough
     * in tests so far.
     */
    if (mean_path_delay > 2 * median) {
      GST_WARNING ("Path delay for domain %u too big compared to median: %"
          GST_TIME_FORMAT " > 2 * %" GST_TIME_FORMAT, domain->domain,
          GST_TIME_ARGS (mean_path_delay), GST_TIME_ARGS (median));
      ret = FALSE;
      goto out;
    }
  }
#endif

#ifdef USE_RUNNING_AVERAGE_DELAY
  /* Track an average round trip time, for a bit of smoothing */
  /* Always update before discarding a sample, so genuine changes in
   * the network get picked up, eventually */
  if (domain->mean_path_delay == 0)
    domain->mean_path_delay = mean_path_delay;
  else if (mean_path_delay < domain->mean_path_delay)   /* Shorter RTTs carry more weight than longer */
    domain->mean_path_delay =
        (3 * domain->mean_path_delay + mean_path_delay) / 4;
  else
    domain->mean_path_delay =
        (15 * domain->mean_path_delay + mean_path_delay) / 16;
#else
  domain->mean_path_delay = mean_path_delay;
#endif

#ifdef USE_MEASUREMENT_FILTERING
  /* The tolerance on accepting follow-up after a sync is high, because
   * a PTP server doesn't have to prioritise sending FOLLOW_UP - its purpose is
   * just to give us the accurate timestamp of the preceding SYNC.
   *
   * For that reason also allow at least 100ms delay in case of delays smaller
   * than 5ms. */
  if (sync->follow_up_recv_time_local != GST_CLOCK_TIME_NONE &&
      domain->mean_path_delay != 0
      && sync->follow_up_recv_time_local >
      sync->sync_recv_time_local + MAX (100 * GST_MSECOND,
          20 * domain->mean_path_delay)) {
    GST_WARNING ("Sync-follow-up delay for domain %u too big: %" GST_TIME_FORMAT
        " > MAX(100ms, 20 * %" GST_TIME_FORMAT ")", domain->domain,
        GST_TIME_ARGS (sync->follow_up_recv_time_local -
            sync->sync_recv_time_local),
        GST_TIME_ARGS (domain->mean_path_delay));
    ret = FALSE;
    goto out;
  }

  if (mean_path_delay > 2 * domain->mean_path_delay) {
    GST_WARNING ("Mean path delay for domain %u too big: %" GST_TIME_FORMAT
        " > 2 * %" GST_TIME_FORMAT, domain->domain,
        GST_TIME_ARGS (mean_path_delay),
        GST_TIME_ARGS (domain->mean_path_delay));
    ret = FALSE;
    goto out;
  }
#endif

  delay_req_delay =
      sync->delay_resp_recv_time_local - sync->delay_req_send_time_local;

#ifdef USE_MEASUREMENT_FILTERING
  /* delay_req_delay is a RTT, so 2 times the path delay is what we'd
   * hope for, but some PTP systems don't prioritise sending DELAY_RESP,
   * but they must still have placed an accurate reception timestamp.
   * That means we should be quite tolerant about late DELAY_RESP, and
   * mostly rely on filtering out jumps in the mean-path-delay elsewhere.
   *
   * For that reason also allow at least 100ms delay in case of delays smaller
   * than 5ms. */
  if (delay_req_delay > MAX (100 * GST_MSECOND, 20 * domain->mean_path_delay)) {
    GST_WARNING ("Delay-request-response delay for domain %u too big: %"
        GST_TIME_FORMAT " > MAX(100ms, 20 * %" GST_TIME_FORMAT ")",
        domain->domain, GST_TIME_ARGS (delay_req_delay),
        GST_TIME_ARGS (domain->mean_path_delay));
    ret = FALSE;
    goto out;
  }
#endif

  ret = TRUE;

  GST_DEBUG ("Got mean path delay for domain %u: %" GST_TIME_FORMAT " (new: %"
      GST_TIME_FORMAT ")", domain->domain,
      GST_TIME_ARGS (domain->mean_path_delay), GST_TIME_ARGS (mean_path_delay));
  GST_DEBUG ("Delay request delay for domain %u: %" GST_TIME_FORMAT,
      domain->domain, GST_TIME_ARGS (delay_req_delay));

#if defined(USE_MEASUREMENT_FILTERING) || defined(USE_MEDIAN_PRE_FILTERING)
out:
#endif
  if (g_atomic_int_get (&domain_stats_n_hooks)) {
    GstStructure *stats =
        gst_structure_new (GST_PTP_STATISTICS_PATH_DELAY_MEASURED,
        "domain", G_TYPE_UINT, domain->domain,
        "mean-path-delay-avg", GST_TYPE_CLOCK_TIME, domain->mean_path_delay,
        "mean-path-delay", GST_TYPE_CLOCK_TIME, mean_path_delay,
        "delay-request-delay", GST_TYPE_CLOCK_TIME, delay_req_delay, NULL);
    emit_ptp_statistics (domain->domain, stats);
    gst_structure_free (stats);
  }

  return ret;
}

static void
handle_sync_message (PtpMessage * msg, GstClockTime receive_time)
{
  GList *l;
  PtpDomainData *domain = NULL;
  PtpPendingSync *sync = NULL;

  /* Don't consider messages with the alternate master flag set */
  if ((msg->flag_field & 0x0100)) {
    GST_TRACE ("Ignoring sync message with alternate-master flag");
    return;
  }

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *tmp = l->data;

    if (msg->domain_number == tmp->domain) {
      domain = tmp;
      break;
    }
  }

  if (!domain) {
    gchar *clock_name;

    domain = g_new0 (PtpDomainData, 1);
    domain->domain = msg->domain_number;
    clock_name = g_strdup_printf ("ptp-clock-%u", domain->domain);
    domain->domain_clock =
        g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", clock_name, NULL);
    gst_object_ref_sink (domain->domain_clock);
    g_free (clock_name);
    g_queue_init (&domain->pending_syncs);
    domain->last_path_delays_missing = 9;
    domain_data = g_list_prepend (domain_data, domain);

    g_mutex_lock (&domain_clocks_lock);
    domain_clocks = g_list_prepend (domain_clocks, domain);
    g_mutex_unlock (&domain_clocks_lock);
  }

  /* If we have a master clock, ignore this message if it's not coming from there */
  if (domain->have_master_clock
      && compare_clock_identity (&domain->master_clock_identity,
          &msg->source_port_identity) != 0)
    return;

#ifdef USE_OPPORTUNISTIC_CLOCK_SELECTION
  /* Opportunistic selection of master clock */
  if (!domain->have_master_clock)
    memcpy (&domain->master_clock_identity, &msg->source_port_identity,
        sizeof (PtpClockIdentity));
#else
  if (!domain->have_master_clock)
    return;
#endif

  if (msg->log_message_interval == 0x7f) {
    domain->sync_interval = GST_SECOND;

    if (!ptpd_hybrid_workaround_warned_once) {
      GST_WARNING ("Working around ptpd bug: ptpd sends multicast PTP packets "
          "with invalid logMessageInterval");
      ptpd_hybrid_workaround_warned_once = TRUE;
    }
  } else {
    domain->sync_interval = log2_to_clock_time (msg->log_message_interval);
  }

  /* Check if duplicated */
  for (l = domain->pending_syncs.head; l; l = l->next) {
    PtpPendingSync *tmp = l->data;

    if (tmp->sync_seqnum == msg->sequence_id)
      return;
  }

  if (msg->message_specific.sync.origin_timestamp.seconds_field >
      GST_CLOCK_TIME_NONE / GST_SECOND) {
    GST_FIXME ("Unsupported sync message seconds field value: %"
        G_GUINT64_FORMAT " > %" G_GUINT64_FORMAT,
        msg->message_specific.sync.origin_timestamp.seconds_field,
        GST_CLOCK_TIME_NONE / GST_SECOND);
    return;
  }

  sync = g_new0 (PtpPendingSync, 1);
  sync->domain = domain->domain;
  sync->sync_seqnum = msg->sequence_id;
  sync->sync_recv_time_local = receive_time;
  sync->sync_send_time_remote = GST_CLOCK_TIME_NONE;
  sync->follow_up_recv_time_local = GST_CLOCK_TIME_NONE;
  sync->delay_req_send_time_local = GST_CLOCK_TIME_NONE;
  sync->delay_req_recv_time_remote = GST_CLOCK_TIME_NONE;
  sync->delay_resp_recv_time_local = GST_CLOCK_TIME_NONE;

  /* 0.5 correction factor for division later */
  sync->correction_field_sync = msg->correction_field;

  if ((msg->flag_field & 0x0200)) {
    /* Wait for FOLLOW_UP */
    GST_TRACE ("Waiting for FOLLOW_UP msg");
  } else {
    sync->sync_send_time_remote =
        PTP_TIMESTAMP_TO_GST_CLOCK_TIME (msg->message_specific.
        sync.origin_timestamp);

    if (domain->last_ptp_sync_time != 0
        && domain->last_ptp_sync_time >= sync->sync_send_time_remote) {
      GST_WARNING ("Backwards PTP times in domain %u: %" GST_TIME_FORMAT " >= %"
          GST_TIME_FORMAT, domain->domain,
          GST_TIME_ARGS (domain->last_ptp_sync_time),
          GST_TIME_ARGS (sync->sync_send_time_remote));
      ptp_pending_sync_free (sync);
      sync = NULL;
      return;
    }
    domain->last_ptp_sync_time = sync->sync_send_time_remote;

    if (send_delay_req (domain, sync)) {
      /* Sent delay request */
    } else {
      update_ptp_time (domain, sync);
      ptp_pending_sync_free (sync);
      sync = NULL;
    }
  }

  if (sync)
    g_queue_push_tail (&domain->pending_syncs, sync);
}

static void
handle_follow_up_message (PtpMessage * msg, GstClockTime receive_time)
{
  GList *l;
  PtpDomainData *domain = NULL;
  PtpPendingSync *sync = NULL;

  GST_TRACE ("Processing FOLLOW_UP message");

  /* Don't consider messages with the alternate master flag set */
  if ((msg->flag_field & 0x0100)) {
    GST_TRACE ("Ignoring FOLLOW_UP with alternate-master flag");
    return;
  }

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *tmp = l->data;

    if (msg->domain_number == tmp->domain) {
      domain = tmp;
      break;
    }
  }

  if (!domain) {
    GST_TRACE ("No domain match for FOLLOW_UP msg");
    return;
  }

  /* If we have a master clock, ignore this message if it's not coming from there */
  if (domain->have_master_clock
      && compare_clock_identity (&domain->master_clock_identity,
          &msg->source_port_identity) != 0) {
    GST_TRACE ("FOLLOW_UP msg not from current clock master. Ignoring");
    return;
  }

  /* Check if we know about this one */
  for (l = domain->pending_syncs.head; l; l = l->next) {
    PtpPendingSync *tmp = l->data;

    if (tmp->sync_seqnum == msg->sequence_id) {
      sync = tmp;
      break;
    }
  }

  if (!sync) {
    GST_TRACE ("Ignoring FOLLOW_UP with no pending SYNC");
    return;
  }

  /* Got a FOLLOW_UP for this already */
  if (sync->sync_send_time_remote != GST_CLOCK_TIME_NONE) {
    GST_TRACE ("Got repeat FOLLOW_UP. Ignoring");
    return;
  }

  if (sync->sync_recv_time_local >= receive_time) {
    GST_ERROR ("Got bogus follow up in domain %u: %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, domain->domain,
        GST_TIME_ARGS (sync->sync_recv_time_local),
        GST_TIME_ARGS (receive_time));
    g_queue_remove (&domain->pending_syncs, sync);
    ptp_pending_sync_free (sync);
    return;
  }

  sync->correction_field_sync += msg->correction_field;
  sync->sync_send_time_remote =
      PTP_TIMESTAMP_TO_GST_CLOCK_TIME (msg->message_specific.
      follow_up.precise_origin_timestamp);
  sync->follow_up_recv_time_local = receive_time;

  if (domain->last_ptp_sync_time >= sync->sync_send_time_remote) {
    GST_WARNING ("Backwards PTP times in domain %u: %" GST_TIME_FORMAT " >= %"
        GST_TIME_FORMAT, domain->domain,
        GST_TIME_ARGS (domain->last_ptp_sync_time),
        GST_TIME_ARGS (sync->sync_send_time_remote));
    g_queue_remove (&domain->pending_syncs, sync);
    ptp_pending_sync_free (sync);
    sync = NULL;
    return;
  }
  domain->last_ptp_sync_time = sync->sync_send_time_remote;

  if (send_delay_req (domain, sync)) {
    /* Sent delay request */
  } else {
    update_ptp_time (domain, sync);
    g_queue_remove (&domain->pending_syncs, sync);
    ptp_pending_sync_free (sync);
    sync = NULL;
  }
}

static void
handle_delay_resp_message (PtpMessage * msg, GstClockTime receive_time)
{
  GList *l;
  PtpDomainData *domain = NULL;
  PtpPendingSync *sync = NULL;

  /* Not for us */
  if (msg->message_specific.delay_resp.
      requesting_port_identity.clock_identity != ptp_clock_id.clock_identity
      || msg->message_specific.delay_resp.
      requesting_port_identity.port_number != ptp_clock_id.port_number)
    return;

  /* Don't consider messages with the alternate master flag set */
  if ((msg->flag_field & 0x0100))
    return;

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *tmp = l->data;

    if (msg->domain_number == tmp->domain) {
      domain = tmp;
      break;
    }
  }

  if (!domain)
    return;

  /* If we have a master clock, ignore this message if it's not coming from there */
  if (domain->have_master_clock
      && compare_clock_identity (&domain->master_clock_identity,
          &msg->source_port_identity) != 0)
    return;

  if (msg->log_message_interval == 0x7f) {
    domain->min_delay_req_interval = GST_SECOND;

    if (!ptpd_hybrid_workaround_warned_once) {
      GST_WARNING ("Working around ptpd bug: ptpd sends multicast PTP packets "
          "with invalid logMessageInterval");
      ptpd_hybrid_workaround_warned_once = TRUE;
    }
  } else {
    domain->min_delay_req_interval =
        log2_to_clock_time (msg->log_message_interval);
  }

  /* Check if we know about this one */
  for (l = domain->pending_syncs.head; l; l = l->next) {
    PtpPendingSync *tmp = l->data;

    if (tmp->delay_req_seqnum == msg->sequence_id) {
      sync = tmp;
      break;
    }
  }

  if (!sync)
    return;

  /* Got a DELAY_RESP for this already */
  if (sync->delay_req_recv_time_remote != GST_CLOCK_TIME_NONE)
    return;

  if (sync->delay_req_send_time_local > receive_time) {
    GST_ERROR ("Got bogus delay response in domain %u: %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, domain->domain,
        GST_TIME_ARGS (sync->delay_req_send_time_local),
        GST_TIME_ARGS (receive_time));
    g_queue_remove (&domain->pending_syncs, sync);
    ptp_pending_sync_free (sync);
    return;
  }

  sync->correction_field_delay = msg->correction_field;

  sync->delay_req_recv_time_remote =
      PTP_TIMESTAMP_TO_GST_CLOCK_TIME (msg->message_specific.
      delay_resp.receive_timestamp);
  sync->delay_resp_recv_time_local = receive_time;

  if (domain->mean_path_delay != 0
      && sync->sync_send_time_remote > sync->delay_req_recv_time_remote) {
    GST_WARNING ("Sync send time after delay req receive time for domain %u: %"
        GST_TIME_FORMAT " > %" GST_TIME_FORMAT, domain->domain,
        GST_TIME_ARGS (sync->sync_send_time_remote),
        GST_TIME_ARGS (sync->delay_req_recv_time_remote));
    g_queue_remove (&domain->pending_syncs, sync);
    ptp_pending_sync_free (sync);
    return;
  }

  if (update_mean_path_delay (domain, sync))
    update_ptp_time (domain, sync);
  g_queue_remove (&domain->pending_syncs, sync);
  ptp_pending_sync_free (sync);
}

static void
handle_ptp_message (PtpMessage * msg, GstClockTime receive_time)
{
  /* Ignore our own messages */
  if (msg->source_port_identity.clock_identity == ptp_clock_id.clock_identity &&
      msg->source_port_identity.port_number == ptp_clock_id.port_number) {
    GST_TRACE ("Ignoring our own message");
    return;
  }

  GST_TRACE ("Message type %d receive_time %" GST_TIME_FORMAT,
      msg->message_type, GST_TIME_ARGS (receive_time));
  switch (msg->message_type) {
    case PTP_MESSAGE_TYPE_ANNOUNCE:
      handle_announce_message (msg, receive_time);
      break;
    case PTP_MESSAGE_TYPE_SYNC:
      handle_sync_message (msg, receive_time);
      break;
    case PTP_MESSAGE_TYPE_FOLLOW_UP:
      handle_follow_up_message (msg, receive_time);
      break;
    case PTP_MESSAGE_TYPE_DELAY_RESP:
      handle_delay_resp_message (msg, receive_time);
      break;
    default:
      break;
  }
}

static void
handle_send_time_ack (const guint8 * data, gsize size,
    GstClockTime receive_time)
{
  GstByteReader breader;
  GstClockTime helper_send_time;
  guint8 message_type;
  guint8 domain_number;
  guint16 seqnum;
  GList *l;
  PtpDomainData *domain = NULL;
  PtpPendingSync *sync = NULL;

  gst_byte_reader_init (&breader, data, size);
  helper_send_time = gst_byte_reader_get_uint64_be_unchecked (&breader);
  message_type = gst_byte_reader_get_uint8_unchecked (&breader);
  domain_number = gst_byte_reader_get_uint8_unchecked (&breader);
  seqnum = gst_byte_reader_get_uint16_be_unchecked (&breader);

  GST_TRACE
      ("Received SEND_TIME_ACK for message type %d, domain number %d, seqnum %d with send time %"
      GST_TIME_FORMAT " at receive_time %" GST_TIME_FORMAT, message_type,
      domain_number, seqnum, GST_TIME_ARGS (helper_send_time),
      GST_TIME_ARGS (receive_time));

  if (message_type != PTP_MESSAGE_TYPE_DELAY_REQ)
    return;

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *tmp = l->data;

    if (domain_number == tmp->domain) {
      domain = tmp;
      break;
    }
  }

  if (!domain)
    return;

  /* Check if we know about this one */
  for (l = domain->pending_syncs.head; l; l = l->next) {
    PtpPendingSync *tmp = l->data;

    if (tmp->delay_req_seqnum == seqnum) {
      sync = tmp;
      break;
    }
  }

  if (!sync)
    return;

  /* Got a DELAY_RESP for this already */
  if (sync->delay_req_recv_time_remote != GST_CLOCK_TIME_NONE)
    return;

  if (helper_send_time != 0) {
    GST_TRACE ("DELAY_REQ message took %" GST_STIME_FORMAT
        " to helper process, SEND_TIME_ACK took %" GST_STIME_FORMAT
        " from helper process",
        GST_STIME_ARGS ((GstClockTimeDiff) (helper_send_time -
                sync->delay_req_send_time_local)),
        GST_STIME_ARGS (receive_time - helper_send_time));
    sync->delay_req_send_time_local = helper_send_time;
  }
}

static void have_stdout_header (GInputStream * stdout_pipe, GAsyncResult * res,
    gpointer user_data);

static void
have_stdout_body (GInputStream * stdout_pipe, GAsyncResult * res,
    gpointer user_data)
{
  GError *err = NULL;
  gsize read;

  /* Finish reading the body */
  if (!g_input_stream_read_all_finish (stdout_pipe, res, &read, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
        g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)) {
      GST_ERROR ("Got EOF on stdout");
    } else {
      GST_ERROR ("Failed to read header from stdout: %s", err->message);
    }
    g_clear_error (&err);
    g_main_loop_quit (main_loop);
    return;
  } else if (read == 0) {
    GST_ERROR ("Got EOF on stdin");
    g_main_loop_quit (main_loop);
    return;
  } else if (read != CUR_STDIO_HEADER_SIZE) {
    GST_ERROR ("Unexpected read size: %" G_GSIZE_FORMAT, read);
    g_main_loop_quit (main_loop);
    return;
  }

  switch (CUR_STDIO_HEADER_TYPE) {
    case TYPE_EVENT:
    case TYPE_GENERAL:{
      GstClockTime receive_time = gst_clock_get_time (observation_system_clock);
      GstClockTime helper_receive_time;
      PtpMessage msg;

      helper_receive_time = GST_READ_UINT64_BE (stdout_buffer);

      if (parse_ptp_message (&msg, (const guint8 *) stdout_buffer + 8,
              CUR_STDIO_HEADER_SIZE)) {
        dump_ptp_message (&msg);
        if (helper_receive_time != 0) {
          GST_TRACE ("Message took %" GST_STIME_FORMAT " from helper process",
              GST_STIME_ARGS ((GstClockTimeDiff) (receive_time -
                      helper_receive_time)));
          receive_time = helper_receive_time;
        }
        handle_ptp_message (&msg, receive_time);
      }
      break;
    }
    case TYPE_CLOCK_ID:{
      if (CUR_STDIO_HEADER_SIZE != 8) {
        GST_ERROR ("Unexpected clock id size (%u != 8)", CUR_STDIO_HEADER_SIZE);
        g_main_loop_quit (main_loop);
        return;
      }
      g_mutex_lock (&ptp_lock);
      ptp_clock_id.clock_identity = GST_READ_UINT64_BE (stdout_buffer);
#ifdef G_OS_WIN32
      ptp_clock_id.port_number = (guint16) GetCurrentProcessId ();
#else
      ptp_clock_id.port_number = getpid ();
#endif
      GST_DEBUG ("Got clock id 0x%016" G_GINT64_MODIFIER "x %u",
          ptp_clock_id.clock_identity, ptp_clock_id.port_number);
      g_cond_signal (&ptp_cond);
      g_mutex_unlock (&ptp_lock);
      break;
    }
    case TYPE_SEND_TIME_ACK:{
      GstClockTime receive_time = gst_clock_get_time (observation_system_clock);

      if (CUR_STDIO_HEADER_SIZE != 12) {
        GST_ERROR ("Unexpected send time ack size (%u != 12)",
            CUR_STDIO_HEADER_SIZE);
        g_main_loop_quit (main_loop);
        return;
      }

      handle_send_time_ack (stdout_buffer, CUR_STDIO_HEADER_SIZE, receive_time);
      break;
    }
    default:
      break;
  }

  /* And read the next header */
  memset (&stdio_header, 0, STDIO_MESSAGE_HEADER_SIZE);
  g_input_stream_read_all_async (stdout_pipe, stdio_header,
      STDIO_MESSAGE_HEADER_SIZE, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) have_stdout_header, NULL);
}

static void
have_stdout_header (GInputStream * stdout_pipe, GAsyncResult * res,
    gpointer user_data)
{
  GError *err = NULL;
  gsize read;

  /* Finish reading the header */
  if (!g_input_stream_read_all_finish (stdout_pipe, res, &read, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
        g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)) {
      GST_ERROR ("Got EOF on stdout");
    } else {
      GST_ERROR ("Failed to read header from stdout: %s", err->message);
    }
    g_clear_error (&err);
    g_main_loop_quit (main_loop);
    return;
  } else if (read == 0) {
    GST_ERROR ("Got EOF on stdin");
    return;
  } else if (read != STDIO_MESSAGE_HEADER_SIZE) {
    GST_ERROR ("Unexpected read size: %" G_GSIZE_FORMAT, read);
    g_main_loop_quit (main_loop);
    return;
  } else if (CUR_STDIO_HEADER_SIZE > 8192) {
    GST_ERROR ("Unexpected size: %u", CUR_STDIO_HEADER_SIZE);
    g_main_loop_quit (main_loop);
    return;
  }

  /* And now read the body */
  g_input_stream_read_all_async (stdout_pipe, stdout_buffer,
      CUR_STDIO_HEADER_SIZE, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) have_stdout_body, NULL);
}

static void have_stderr_header (GInputStream * stderr_pipe, GAsyncResult * res,
    gpointer user_data);

static void
have_stderr_body (GInputStream * stderr_pipe, GAsyncResult * res,
    gpointer user_data)
{
  GError *err = NULL;
  gsize read;
#ifndef GST_DISABLE_GST_DEBUG
  GstByteReader breader;
  GstDebugLevel level;
  guint16 filename_length;
  gchar *filename = NULL;
  guint16 module_path_length;
  gchar *module_path = NULL;
  guint32 line_number;
  gchar *message = NULL;
  guint16 message_length;
  guint8 b;
#endif

  /* Finish reading the body */
  if (!g_input_stream_read_all_finish (stderr_pipe, res, &read, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
        g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)) {
      GST_ERROR ("Got EOF on stderr");
    } else {
      GST_ERROR ("Failed to read header from stderr: %s", err->message);
    }
    g_clear_error (&err);
    g_main_loop_quit (main_loop);
    return;
  } else if (read == 0) {
    GST_ERROR ("Got EOF on stderr");
    g_main_loop_quit (main_loop);
    return;
  } else if (read != CUR_STDERR_HEADER_SIZE) {
    GST_ERROR ("Unexpected read size: %" G_GSIZE_FORMAT, read);
    g_main_loop_quit (main_loop);
    return;
  }

#ifndef GST_DISABLE_GST_DEBUG
  gst_byte_reader_init (&breader, stderr_buffer, CUR_STDERR_HEADER_SIZE);

  if (!gst_byte_reader_get_uint8 (&breader, &b) || b > GST_LEVEL_MAX)
    goto err;
  level = (GstDebugLevel) b;
  if (!gst_byte_reader_get_uint16_be (&breader, &filename_length)
      || filename_length > gst_byte_reader_get_remaining (&breader))
    goto err;
  filename =
      g_strndup ((const gchar *) gst_byte_reader_get_data_unchecked (&breader,
          filename_length), filename_length);

  if (!gst_byte_reader_get_uint16_be (&breader, &module_path_length)
      || module_path_length > gst_byte_reader_get_remaining (&breader))
    goto err;
  module_path =
      g_strndup ((const gchar *) gst_byte_reader_get_data_unchecked (&breader,
          module_path_length), module_path_length);

  if (!gst_byte_reader_get_uint32_be (&breader, &line_number))
    goto err;

  message_length = gst_byte_reader_get_remaining (&breader);
  message =
      g_strndup ((const gchar *) gst_byte_reader_get_data_unchecked (&breader,
          message_length), message_length);

  gst_debug_log_literal (GST_CAT_DEFAULT, level, filename, module_path,
      line_number, NULL, message);

  g_clear_pointer (&filename, g_free);
  g_clear_pointer (&module_path, g_free);
  g_clear_pointer (&message, g_free);
#endif

  /* And read the next header */
  memset (&stderr_header, 0, STDERR_MESSAGE_HEADER_SIZE);
  g_input_stream_read_all_async (stderr_pipe, stderr_header,
      STDERR_MESSAGE_HEADER_SIZE, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) have_stderr_header, NULL);
  return;

#ifndef GST_DISABLE_GST_DEBUG
err:
  {
    GST_ERROR ("Unexpected stderr data");
    g_clear_pointer (&filename, g_free);
    g_clear_pointer (&module_path, g_free);
    g_clear_pointer (&message, g_free);
    g_main_loop_quit (main_loop);
    return;
  }
#endif
}

static void
have_stderr_header (GInputStream * stderr_pipe, GAsyncResult * res,
    gpointer user_data)
{
  GError *err = NULL;
  gsize read;

  /* Finish reading the header */
  if (!g_input_stream_read_all_finish (stderr_pipe, res, &read, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
        g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)) {
      GST_ERROR ("Got EOF on stderr");
    } else {
      GST_ERROR ("Failed to read header from stderr: %s", err->message);
    }
    g_clear_error (&err);
    g_main_loop_quit (main_loop);
    return;
  } else if (read == 0) {
    GST_ERROR ("Got EOF on stderr");
    g_main_loop_quit (main_loop);
    return;
  } else if (read != STDERR_MESSAGE_HEADER_SIZE) {
    GST_ERROR ("Unexpected read size: %" G_GSIZE_FORMAT, read);
    g_main_loop_quit (main_loop);
    return;
  } else if (CUR_STDERR_HEADER_SIZE > 8192 || CUR_STDERR_HEADER_SIZE < 9) {
    GST_ERROR ("Unexpected size: %u", CUR_STDERR_HEADER_SIZE);
    g_main_loop_quit (main_loop);
    return;
  }

  /* And now read the body */
  g_input_stream_read_all_async (stderr_pipe, stderr_buffer,
      CUR_STDERR_HEADER_SIZE, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) have_stderr_body, NULL);
}

/* Cleanup all announce messages and announce message senders
 * that are timed out by now, and clean up all pending syncs
 * that are missing their FOLLOW_UP or DELAY_RESP */
static gboolean
cleanup_cb (gpointer data)
{
  GstClockTime now = gst_clock_get_time (observation_system_clock);
  GList *l, *m, *n;

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *domain = l->data;

    for (n = domain->announce_senders; n;) {
      PtpAnnounceSender *sender = n->data;
      gboolean timed_out = TRUE;

      /* Keep only 5 messages per sender around */
      while (g_queue_get_length (&sender->announce_messages) > 5) {
        PtpAnnounceMessage *msg = g_queue_pop_head (&sender->announce_messages);
        g_free (msg);
      }

      for (m = sender->announce_messages.head; m; m = m->next) {
        PtpAnnounceMessage *msg = m->data;

        if (msg->receive_time +
            sender->announce_interval * PTP_ANNOUNCE_RECEIPT_TIMEOUT > now) {
          timed_out = FALSE;
          break;
        }
      }

      if (timed_out) {
        GST_DEBUG ("Announce sender 0x%016" G_GINT64_MODIFIER "x %u timed out",
            sender->master_clock_identity.clock_identity,
            sender->master_clock_identity.port_number);
        g_queue_foreach (&sender->announce_messages, (GFunc) g_free, NULL);
        g_queue_clear (&sender->announce_messages);
      }

      if (g_queue_get_length (&sender->announce_messages) == 0) {
        GList *tmp = n->next;

        if (compare_clock_identity (&sender->master_clock_identity,
                &domain->master_clock_identity) == 0)
          GST_WARNING ("currently selected master clock timed out");
        g_free (sender);
        domain->announce_senders =
            g_list_delete_link (domain->announce_senders, n);
        n = tmp;
      } else {
        n = n->next;
      }
    }
    select_best_master_clock (domain, now);

    /* Clean up any pending syncs */
    for (n = domain->pending_syncs.head; n;) {
      PtpPendingSync *sync = n->data;
      gboolean timed_out = FALSE;

      /* Time out pending syncs after 4 sync intervals or 10 seconds,
       * and pending delay reqs after 4 delay req intervals or 10 seconds
       */
      if (sync->delay_req_send_time_local != GST_CLOCK_TIME_NONE &&
          ((domain->min_delay_req_interval != 0
                  && sync->delay_req_send_time_local +
                  4 * domain->min_delay_req_interval < now)
              || (sync->delay_req_send_time_local + 10 * GST_SECOND < now))) {
        timed_out = TRUE;
      } else if ((domain->sync_interval != 0
              && sync->sync_recv_time_local + 4 * domain->sync_interval < now)
          || (sync->sync_recv_time_local + 10 * GST_SECOND < now)) {
        timed_out = TRUE;
      }

      if (timed_out) {
        GList *tmp = n->next;
        ptp_pending_sync_free (sync);
        g_queue_delete_link (&domain->pending_syncs, n);
        n = tmp;
      } else {
        n = n->next;
      }
    }
  }

  return G_SOURCE_CONTINUE;
}

static gpointer
ptp_helper_main (gpointer data)
{
  GSource *cleanup_source;

  GST_DEBUG ("Starting PTP helper loop");

  g_main_context_push_thread_default (main_context);

  memset (&stdio_header, 0, STDIO_MESSAGE_HEADER_SIZE);
  g_input_stream_read_all_async (stdout_pipe, stdio_header,
      STDIO_MESSAGE_HEADER_SIZE, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) have_stdout_header, NULL);

  memset (&stderr_header, 0, STDERR_MESSAGE_HEADER_SIZE);
  g_input_stream_read_all_async (stderr_pipe, stderr_header,
      STDERR_MESSAGE_HEADER_SIZE, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) have_stderr_header, NULL);

  /* Check all 5 seconds, if we have to cleanup ANNOUNCE or pending syncs message */
  cleanup_source = g_timeout_source_new_seconds (5);
  g_source_set_priority (cleanup_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (cleanup_source, (GSourceFunc) cleanup_cb, NULL, NULL);
  g_source_attach (cleanup_source, main_context);
  g_source_unref (cleanup_source);

  g_main_loop_run (main_loop);
  GST_DEBUG ("Stopped PTP helper loop");

  g_main_context_pop_thread_default (main_context);

  g_mutex_lock (&ptp_lock);
  ptp_clock_id.clock_identity = GST_PTP_CLOCK_ID_NONE;
  ptp_clock_id.port_number = 0;
  initted = FALSE;
  g_cond_signal (&ptp_cond);
  g_mutex_unlock (&ptp_lock);

  return NULL;
}

/**
 * gst_ptp_is_supported:
 *
 * Check if PTP clocks are generally supported on this system, and if previous
 * initializations did not fail.
 *
 * Returns: %TRUE if PTP clocks are generally supported on this system, and
 * previous initializations did not fail.
 *
 * Since: 1.6
 */
gboolean
gst_ptp_is_supported (void)
{
  return supported;
}

/**
 * gst_ptp_is_initialized:
 *
 * Check if the GStreamer PTP clock subsystem is initialized.
 *
 * Returns: %TRUE if the GStreamer PTP clock subsystem is initialized.
 *
 * Since: 1.6
 */
gboolean
gst_ptp_is_initialized (void)
{
  return initted;
}

#if defined(G_OS_WIN32) && !defined(GST_STATIC_COMPILATION)
/* Note: DllMain is only called when DLLs are loaded or unloaded, so this will
 * never be called if libgstnet-1.0 is linked statically. Do not add any code
 * here to, say, initialize variables or set things up since that will only
 * happen for dynamically-built GStreamer.
 */
BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);
BOOL WINAPI
DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH)
    gstnet_dll_handle = (HMODULE) hinstDLL;
  return TRUE;
}

#endif

static char *
get_relocated_libgstnet (void)
{
  char *dir = NULL;

#ifdef G_OS_WIN32
  {
    char *base_dir;

    GST_DEBUG ("attempting to retrieve libgstnet-1.0 location using "
        "Win32-specific method");

    base_dir =
        g_win32_get_package_installation_directory_of_module
        (gstnet_dll_handle);
    if (!base_dir)
      return NULL;

    dir = g_build_filename (base_dir, GST_PLUGIN_SUBDIR, NULL);
    GST_DEBUG ("using DLL dir %s", dir);

    g_free (base_dir);
  }
#elif defined(HAVE_DLADDR)
  {
    Dl_info info;

    GST_DEBUG ("attempting to retrieve libgstnet-1.0 location using "
        "dladdr()");

    if (dladdr (&gst_ptp_init, &info)) {
      GST_LOG ("dli_fname: %s", info.dli_fname);

      if (!info.dli_fname) {
        return NULL;
      }

      dir = g_path_get_dirname (info.dli_fname);
    } else {
      GST_LOG ("dladdr() failed");
      return NULL;
    }
  }
#else
#warning "Unsupported platform for retrieving the current location of a shared library."
#warning "Relocatable builds will not work."
  GST_WARNING ("Don't know how to retrieve the location of the shared "
      "library libgstnet-" GST_API_VERSION);
#endif

  return dir;
}

static int
count_directories (const char *filepath)
{
  int i = 0;
  char *tmp;
  gsize len;

  g_return_val_if_fail (!g_path_is_absolute (filepath), 0);

  tmp = g_strdup (filepath);
  len = strlen (tmp);

  /* ignore UNC share paths entirely */
  if (len >= 3 && G_IS_DIR_SEPARATOR (tmp[0]) && G_IS_DIR_SEPARATOR (tmp[1])
      && !G_IS_DIR_SEPARATOR (tmp[2])) {
    GST_WARNING ("found a UNC share path, ignoring");
    g_clear_pointer (&tmp, g_free);
    return 0;
  }

  /* remove trailing slashes if they exist */
  while (
      /* don't remove the trailing slash for C:\.
       * UNC paths are at least \\s\s */
      len > 3 && G_IS_DIR_SEPARATOR (tmp[len - 1])) {
    tmp[len - 1] = '\0';
    len--;
  }

  while (tmp) {
    char *dirname, *basename;
    len = strlen (tmp);

    if (g_strcmp0 (tmp, ".") == 0)
      break;
    if (g_strcmp0 (tmp, "/") == 0)
      break;

    /* g_path_get_dirname() may return something of the form 'C:.', where C is
     * a drive letter */
    if (len == 3 && g_ascii_isalpha (tmp[0]) && tmp[1] == ':' && tmp[2] == '.')
      break;

    basename = g_path_get_basename (tmp);
    dirname = g_path_get_dirname (tmp);

    if (g_strcmp0 (basename, "..") == 0) {
      i--;
    } else if (g_strcmp0 (basename, ".") == 0) {
      /* nothing to do */
    } else {
      i++;
    }

    g_clear_pointer (&basename, g_free);
    g_clear_pointer (&tmp, g_free);
    tmp = dirname;
  }

  g_clear_pointer (&tmp, g_free);

  if (i < 0) {
    g_critical ("path counting resulted in a negative directory count!");
    return 0;
  }

  return i;
}


/**
 * gst_ptp_init:
 * @clock_id: PTP clock id of this process' clock or %GST_PTP_CLOCK_ID_NONE
 * @interfaces: (transfer none) (array zero-terminated=1) (allow-none): network interfaces to run the clock on
 *
 * Initialize the GStreamer PTP subsystem and create a PTP ordinary clock in
 * slave-only mode for all domains on the given @interfaces with the
 * given @clock_id.
 *
 * If @clock_id is %GST_PTP_CLOCK_ID_NONE, a clock id is automatically
 * generated from the MAC address of the first network interface.
 *
 * This function is automatically called by gst_ptp_clock_new() with default
 * parameters if it wasn't called before.
 *
 * Returns: %TRUE if the GStreamer PTP clock subsystem could be initialized.
 *
 * Since: 1.6
 */
gboolean
gst_ptp_init (guint64 clock_id, gchar ** interfaces)
{
  gboolean ret;
  const gchar *env;
  gchar **argv = NULL;
  gint argc, argc_c;
  GError *err = NULL;

  GST_DEBUG_CATEGORY_INIT (ptp_debug, "ptp", 0, "PTP clock");

  g_mutex_lock (&ptp_lock);
  if (!supported) {
    GST_ERROR ("PTP not supported");
    ret = FALSE;
    goto done;
  }

  if (initted) {
    GST_DEBUG ("PTP already initialized");
    ret = TRUE;
    goto done;
  }

  if (ptp_helper_process) {
    GST_DEBUG ("PTP currently initializing");
    goto wait;
  }

  if (!domain_stats_hooks_initted) {
    g_hook_list_init (&domain_stats_hooks, sizeof (GHook));
    domain_stats_hooks_initted = TRUE;
  }

  argc = 1;
  if (clock_id != GST_PTP_CLOCK_ID_NONE)
    argc += 2;
  if (interfaces != NULL)
    argc += 2 * g_strv_length (interfaces);

  argv = g_new0 (gchar *, argc + 3);
  argc_c = 0;

  /* Find the gst-ptp-helper */
  env = g_getenv ("GST_PTP_HELPER_1_0");
  if (!env)
    env = g_getenv ("GST_PTP_HELPER");

  if (env && *env != '\0') {
    /* use the env-var if it is set */
    argv[argc_c++] = g_strdup (env);
  } else {
    char *relocated_libgstnet;

    /* use the installed version */
    GST_LOG ("Trying installed PTP helper process");

#define MAX_PATH_DEPTH 64

    relocated_libgstnet = get_relocated_libgstnet ();
    if (relocated_libgstnet) {
      int plugin_subdir_depth = count_directories (GST_PLUGIN_SUBDIR);

      GST_DEBUG ("found libgstnet-" GST_API_VERSION " library "
          "at %s", relocated_libgstnet);

      if (plugin_subdir_depth < MAX_PATH_DEPTH) {
        const char *filenamev[MAX_PATH_DEPTH + 5];
        int i = 0, j;

        filenamev[i++] = relocated_libgstnet;
        for (j = 0; j < plugin_subdir_depth; j++)
          filenamev[i++] = "..";
        filenamev[i++] = GST_PTP_HELPER_SUBDIR;
        filenamev[i++] = "gstreamer-" GST_API_VERSION;
#ifdef G_OS_WIN32
        filenamev[i++] = "gst-ptp-helper.exe";
#else
        filenamev[i++] = "gst-ptp-helper";
#endif
        filenamev[i++] = NULL;
        g_assert (i <= MAX_PATH_DEPTH + 5);

        GST_DEBUG ("constructing path to system PTP helper using "
            "plugin dir: \'%s\', PTP helper dir: \'%s\'",
            GST_PLUGIN_SUBDIR, GST_PTP_HELPER_SUBDIR);

        argv[argc_c++] = g_build_filenamev ((char **) filenamev);
      } else {
        GST_WARNING ("GST_PLUGIN_SUBDIR: \'%s\' has too many path segments",
            GST_PLUGIN_SUBDIR);
        argv[argc_c++] = g_strdup (GST_PTP_HELPER_INSTALLED);
      }
    } else {
      argv[argc_c++] = g_strdup (GST_PTP_HELPER_INSTALLED);
    }
  }

#undef MAX_PATH_DEPTH

  GST_LOG ("Using PTP helper process: %s", argv[argc_c - 1]);

  if (clock_id != GST_PTP_CLOCK_ID_NONE) {
    argv[argc_c++] = g_strdup ("-c");
    argv[argc_c++] = g_strdup_printf ("0x%016" G_GINT64_MODIFIER "x", clock_id);
    GST_LOG ("Using clock ID: %s", argv[argc_c - 1]);
  }

  if (interfaces != NULL) {
    gchar **ptr = interfaces;

    while (*ptr) {
      argv[argc_c++] = g_strdup ("-i");
      argv[argc_c++] = g_strdup (*ptr);
      GST_LOG ("Using interface: %s", argv[argc_c - 1]);
      ptr++;
    }
  }

  /* Check if the helper process should be verbose */
  env = g_getenv ("GST_PTP_HELPER_VERBOSE");
  if (env && g_ascii_strcasecmp (env, "no") != 0) {
    argv[argc_c++] = g_strdup ("-v");
  }

  ptp_helper_process =
      g_subprocess_newv ((const gchar * const *) argv,
      G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
      G_SUBPROCESS_FLAGS_STDERR_PIPE, &err);
  if (!ptp_helper_process) {
    GST_ERROR ("Failed to start ptp helper process: %s", err->message);
    g_clear_error (&err);
    ret = FALSE;
    supported = FALSE;
    goto done;
  }

  stdin_pipe = g_subprocess_get_stdin_pipe (ptp_helper_process);
  if (stdin_pipe)
    g_object_ref (stdin_pipe);
  stdout_pipe = g_subprocess_get_stdout_pipe (ptp_helper_process);
  if (stdout_pipe)
    g_object_ref (stdout_pipe);
  stderr_pipe = g_subprocess_get_stderr_pipe (ptp_helper_process);
  if (stderr_pipe)
    g_object_ref (stderr_pipe);
  if (!stdin_pipe || !stdout_pipe || !stderr_pipe) {
    GST_ERROR ("Failed to get ptp helper process pipes");
    ret = FALSE;
    supported = FALSE;
    goto done;
  }

  delay_req_rand = g_rand_new ();
  observation_system_clock =
      g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", "ptp-observation-clock",
      NULL);
  gst_object_ref_sink (observation_system_clock);

  main_context = g_main_context_new ();
  main_loop = g_main_loop_new (main_context, FALSE);

  ptp_helper_thread =
      g_thread_try_new ("ptp-helper-thread", ptp_helper_main, NULL, &err);
  if (!ptp_helper_thread) {
    GST_ERROR ("Failed to start PTP helper thread: %s", err->message);
    g_clear_error (&err);
    ret = FALSE;
    goto done;
  }

  initted = TRUE;

wait:
  GST_DEBUG ("Waiting for PTP to be initialized");

  while (ptp_clock_id.clock_identity == GST_PTP_CLOCK_ID_NONE && initted)
    g_cond_wait (&ptp_cond, &ptp_lock);

  ret = initted;
  if (ret) {
    GST_DEBUG ("Initialized and got clock id 0x%016" G_GINT64_MODIFIER "x %u",
        ptp_clock_id.clock_identity, ptp_clock_id.port_number);
  } else {
    GST_ERROR ("Failed to initialize");
    supported = FALSE;
  }

done:
  g_strfreev (argv);

  if (!ret) {
    if (ptp_helper_process) {
      g_clear_object (&stdin_pipe);
      g_clear_object (&stdout_pipe);
      g_clear_object (&stderr_pipe);
      g_subprocess_force_exit (ptp_helper_process);
      g_clear_object (&ptp_helper_process);
    }

    if (main_loop && ptp_helper_thread) {
      g_main_loop_quit (main_loop);
      g_thread_join (ptp_helper_thread);
    }
    ptp_helper_thread = NULL;
    if (main_loop)
      g_main_loop_unref (main_loop);
    main_loop = NULL;
    if (main_context)
      g_main_context_unref (main_context);
    main_context = NULL;

    if (delay_req_rand)
      g_rand_free (delay_req_rand);
    delay_req_rand = NULL;

    if (observation_system_clock)
      gst_object_unref (observation_system_clock);
    observation_system_clock = NULL;
  }

  g_mutex_unlock (&ptp_lock);

  return ret;
}

/**
 * gst_ptp_deinit:
 *
 * Deinitialize the GStreamer PTP subsystem and stop the PTP clock. If there
 * are any remaining GstPtpClock instances, they won't be further synchronized
 * to the PTP network clock.
 *
 * Since: 1.6
 */
void
gst_ptp_deinit (void)
{
  GList *l, *m;

  g_mutex_lock (&ptp_lock);

  if (ptp_helper_process) {
    g_clear_object (&stdin_pipe);
    g_clear_object (&stdout_pipe);
    g_clear_object (&stderr_pipe);
    g_subprocess_force_exit (ptp_helper_process);
    g_clear_object (&ptp_helper_process);
  }

  if (main_loop && ptp_helper_thread) {
    GThread *tmp = ptp_helper_thread;
    ptp_helper_thread = NULL;
    g_mutex_unlock (&ptp_lock);
    g_main_loop_quit (main_loop);
    g_thread_join (tmp);
    g_mutex_lock (&ptp_lock);
  }
  if (main_loop)
    g_main_loop_unref (main_loop);
  main_loop = NULL;
  if (main_context)
    g_main_context_unref (main_context);
  main_context = NULL;

  if (delay_req_rand)
    g_rand_free (delay_req_rand);
  delay_req_rand = NULL;
  if (observation_system_clock)
    gst_object_unref (observation_system_clock);
  observation_system_clock = NULL;

  for (l = domain_data; l; l = l->next) {
    PtpDomainData *domain = l->data;

    for (m = domain->announce_senders; m; m = m->next) {
      PtpAnnounceSender *sender = m->data;

      g_queue_foreach (&sender->announce_messages, (GFunc) g_free, NULL);
      g_queue_clear (&sender->announce_messages);
      g_free (sender);
    }
    g_list_free (domain->announce_senders);

    g_queue_foreach (&domain->pending_syncs, (GFunc) ptp_pending_sync_free,
        NULL);
    g_queue_clear (&domain->pending_syncs);
    gst_object_unref (domain->domain_clock);
    g_free (domain);
  }
  g_list_free (domain_data);
  domain_data = NULL;
  g_list_foreach (domain_clocks, (GFunc) g_free, NULL);
  g_list_free (domain_clocks);
  domain_clocks = NULL;

  ptp_clock_id.clock_identity = GST_PTP_CLOCK_ID_NONE;
  ptp_clock_id.port_number = 0;

  initted = FALSE;

  g_mutex_unlock (&ptp_lock);
}

#define DEFAULT_DOMAIN 0

enum
{
  PROP_0,
  PROP_DOMAIN,
  PROP_INTERNAL_CLOCK,
  PROP_MASTER_CLOCK_ID,
  PROP_GRANDMASTER_CLOCK_ID
};

struct _GstPtpClockPrivate
{
  guint domain;
  GstClock *domain_clock;
  gulong domain_stats_id;
};

#define gst_ptp_clock_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstPtpClock, gst_ptp_clock, GST_TYPE_SYSTEM_CLOCK);

static void gst_ptp_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ptp_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ptp_clock_finalize (GObject * object);

static GstClockTime gst_ptp_clock_get_internal_time (GstClock * clock);

static void
gst_ptp_clock_class_init (GstPtpClockClass * klass)
{
  GObjectClass *gobject_class;
  GstClockClass *clock_class;

  gobject_class = G_OBJECT_CLASS (klass);
  clock_class = GST_CLOCK_CLASS (klass);

  gobject_class->finalize = gst_ptp_clock_finalize;
  gobject_class->get_property = gst_ptp_clock_get_property;
  gobject_class->set_property = gst_ptp_clock_set_property;

  g_object_class_install_property (gobject_class, PROP_DOMAIN,
      g_param_spec_uint ("domain", "Domain",
          "The PTP domain", 0, G_MAXUINT8,
          DEFAULT_DOMAIN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INTERNAL_CLOCK,
      g_param_spec_object ("internal-clock", "Internal Clock",
          "Internal clock", GST_TYPE_CLOCK,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MASTER_CLOCK_ID,
      g_param_spec_uint64 ("master-clock-id", "Master Clock ID",
          "Master Clock ID", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GRANDMASTER_CLOCK_ID,
      g_param_spec_uint64 ("grandmaster-clock-id", "Grand Master Clock ID",
          "Grand Master Clock ID", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  clock_class->get_internal_time = gst_ptp_clock_get_internal_time;
}

static void
gst_ptp_clock_init (GstPtpClock * self)
{
  GstPtpClockPrivate *priv;

  self->priv = priv = gst_ptp_clock_get_instance_private (self);

  GST_OBJECT_FLAG_SET (self, GST_CLOCK_FLAG_CAN_SET_MASTER);
  GST_OBJECT_FLAG_SET (self, GST_CLOCK_FLAG_NEEDS_STARTUP_SYNC);

  priv->domain = DEFAULT_DOMAIN;
}

static gboolean
gst_ptp_clock_ensure_domain_clock (GstPtpClock * self)
{
  gboolean got_clock = TRUE;

  if (G_UNLIKELY (!self->priv->domain_clock)) {
    g_mutex_lock (&domain_clocks_lock);
    if (!self->priv->domain_clock) {
      GList *l;

      got_clock = FALSE;

      for (l = domain_clocks; l; l = l->next) {
        PtpDomainData *clock_data = l->data;

        if (clock_data->domain == self->priv->domain &&
            clock_data->have_master_clock && clock_data->last_ptp_time != 0) {
          GST_DEBUG ("Switching domain clock on domain %d", clock_data->domain);
          self->priv->domain_clock = clock_data->domain_clock;
          got_clock = TRUE;
          break;
        }
      }
    }
    g_mutex_unlock (&domain_clocks_lock);
    if (got_clock) {
      g_object_notify (G_OBJECT (self), "internal-clock");
      gst_clock_set_synced (GST_CLOCK (self), TRUE);
    }
  }

  return got_clock;
}

static gboolean
gst_ptp_clock_stats_callback (guint8 domain, const GstStructure * stats,
    gpointer user_data)
{
  GstPtpClock *self = user_data;

  if (domain != self->priv->domain
      || !gst_structure_has_name (stats, GST_PTP_STATISTICS_TIME_UPDATED))
    return TRUE;

  /* Let's set our internal clock */
  if (!gst_ptp_clock_ensure_domain_clock (self))
    return TRUE;

  self->priv->domain_stats_id = 0;

  return FALSE;
}

static void
gst_ptp_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPtpClock *self = GST_PTP_CLOCK (object);

  switch (prop_id) {
    case PROP_DOMAIN:
      self->priv->domain = g_value_get_uint (value);
      gst_ptp_clock_ensure_domain_clock (self);
      if (!self->priv->domain_clock)
        self->priv->domain_stats_id =
            gst_ptp_statistics_callback_add (gst_ptp_clock_stats_callback, self,
            NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ptp_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPtpClock *self = GST_PTP_CLOCK (object);

  switch (prop_id) {
    case PROP_DOMAIN:
      g_value_set_uint (value, self->priv->domain);
      break;
    case PROP_INTERNAL_CLOCK:
      gst_ptp_clock_ensure_domain_clock (self);
      g_value_set_object (value, self->priv->domain_clock);
      break;
    case PROP_MASTER_CLOCK_ID:
    case PROP_GRANDMASTER_CLOCK_ID:{
      GList *l;

      g_mutex_lock (&domain_clocks_lock);
      g_value_set_uint64 (value, 0);

      for (l = domain_clocks; l; l = l->next) {
        PtpDomainData *clock_data = l->data;

        if (clock_data->domain == self->priv->domain) {
          if (prop_id == PROP_MASTER_CLOCK_ID)
            g_value_set_uint64 (value,
                clock_data->master_clock_identity.clock_identity);
          else
            g_value_set_uint64 (value, clock_data->grandmaster_identity);
          break;
        }
      }
      g_mutex_unlock (&domain_clocks_lock);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ptp_clock_finalize (GObject * object)
{
  GstPtpClock *self = GST_PTP_CLOCK (object);

  if (self->priv->domain_stats_id)
    gst_ptp_statistics_callback_remove (self->priv->domain_stats_id);

  G_OBJECT_CLASS (gst_ptp_clock_parent_class)->finalize (object);
}

static GstClockTime
gst_ptp_clock_get_internal_time (GstClock * clock)
{
  GstPtpClock *self = GST_PTP_CLOCK (clock);

  gst_ptp_clock_ensure_domain_clock (self);

  if (!self->priv->domain_clock) {
    GST_ERROR_OBJECT (self, "Domain %u has no clock yet and is not synced",
        self->priv->domain);
    return GST_CLOCK_TIME_NONE;
  }

  return gst_clock_get_time (self->priv->domain_clock);
}

/**
 * gst_ptp_clock_new:
 * @name: Name of the clock
 * @domain: PTP domain
 *
 * Creates a new PTP clock instance that exports the PTP time of the master
 * clock in @domain. This clock can be slaved to other clocks as needed.
 *
 * If gst_ptp_init() was not called before, this will call gst_ptp_init() with
 * default parameters.
 *
 * This clock only returns valid timestamps after it received the first
 * times from the PTP master clock on the network. Once this happens the
 * GstPtpClock::internal-clock property will become non-NULL. You can
 * check this with gst_clock_wait_for_sync(), the GstClock::synced signal and
 * gst_clock_is_synced().
 *
 * Returns: (transfer full): A new #GstClock
 *
 * Since: 1.6
 */
GstClock *
gst_ptp_clock_new (const gchar * name, guint domain)
{
  GstClock *clock;

  g_return_val_if_fail (domain <= G_MAXUINT8, NULL);

  if (!initted && !gst_ptp_init (GST_PTP_CLOCK_ID_NONE, NULL)) {
    GST_ERROR ("Failed to initialize PTP");
    return NULL;
  }

  clock = g_object_new (GST_TYPE_PTP_CLOCK, "name", name, "domain", domain,
      NULL);

  /* Clear floating flag */
  gst_object_ref_sink (clock);

  return clock;
}

typedef struct
{
  guint8 domain;
  const GstStructure *stats;
} DomainStatsMarshalData;

static void
domain_stats_marshaller (GHook * hook, DomainStatsMarshalData * data)
{
  GstPtpStatisticsCallback callback = (GstPtpStatisticsCallback) hook->func;

  if (!callback (data->domain, data->stats, hook->data))
    g_hook_destroy (&domain_stats_hooks, hook->hook_id);
}

static void
emit_ptp_statistics (guint8 domain, const GstStructure * stats)
{
  DomainStatsMarshalData data = { domain, stats };

  g_mutex_lock (&ptp_lock);
  g_hook_list_marshal (&domain_stats_hooks, TRUE,
      (GHookMarshaller) domain_stats_marshaller, &data);
  g_mutex_unlock (&ptp_lock);
}

/**
 * gst_ptp_statistics_callback_add:
 * @callback: GstPtpStatisticsCallback to call
 * @user_data: Data to pass to the callback
 * @destroy_data: GDestroyNotify to destroy the data
 *
 * Installs a new statistics callback for gathering PTP statistics. See
 * GstPtpStatisticsCallback for a list of statistics that are provided.
 *
 * Returns: Id for the callback that can be passed to
 * gst_ptp_statistics_callback_remove()
 *
 * Since: 1.6
 */
gulong
gst_ptp_statistics_callback_add (GstPtpStatisticsCallback callback,
    gpointer user_data, GDestroyNotify destroy_data)
{
  GHook *hook;

  g_mutex_lock (&ptp_lock);

  if (!domain_stats_hooks_initted) {
    g_hook_list_init (&domain_stats_hooks, sizeof (GHook));
    domain_stats_hooks_initted = TRUE;
  }

  hook = g_hook_alloc (&domain_stats_hooks);
  hook->func = callback;
  hook->data = user_data;
  hook->destroy = destroy_data;
  g_hook_prepend (&domain_stats_hooks, hook);
  g_atomic_int_add (&domain_stats_n_hooks, 1);

  g_mutex_unlock (&ptp_lock);

  return hook->hook_id;
}

/**
 * gst_ptp_statistics_callback_remove:
 * @id: Callback id to remove
 *
 * Removes a PTP statistics callback that was previously added with
 * gst_ptp_statistics_callback_add().
 *
 * Since: 1.6
 */
void
gst_ptp_statistics_callback_remove (gulong id)
{
  g_mutex_lock (&ptp_lock);
  if (g_hook_destroy (&domain_stats_hooks, id))
    g_atomic_int_add (&domain_stats_n_hooks, -1);
  g_mutex_unlock (&ptp_lock);
}
