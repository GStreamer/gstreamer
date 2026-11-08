/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Copyright (C) 2023 Pengutronix e.K. - www.pengutronix.de
 *
 */

#pragma once

#include "linux/usb/g_uvc.h"
#include "linux/usb/video.h"
#include "linux/videodev2.h"

#include <gst/gst.h>

#include "configfs.h"

G_BEGIN_DECLS GST_DEBUG_CATEGORY_EXTERN (uvcsink_debug);

#define REQUEST_ERROR_CODE_NO_ERROR 0x0
#define REQUEST_ERROR_CODE_INVALID_REQUEST 0x6

#define GST_TYPE_UVCSINK (gst_uvc_sink_get_type())
G_DECLARE_FINAL_TYPE (GstUvcSink, gst_uvc_sink, GST, UVCSINK, GstBin)

GST_ELEMENT_REGISTER_DECLARE (uvcsink);

struct _GstUvcSink
{
  GstBin bin;
  GstElement *fakesink;
  GstElement *v4l2sink;
  GstPad *sinkpad;
  GstPad *fakesinkpad;
  GstPad *v4l2sinkpad;

  GstCaps *probed_caps;

  /* a poll for video_fd */
  GstPoll *poll;
  GstPollFD pollfd;

  struct uvc_function_config *fc;

  struct {
    int bFrameIndex;
    int bFormatIndex;
    unsigned int dwFrameInterval;
  } cur;

  struct uvc_streaming_control probe;
  struct uvc_streaming_control commit;

  int control;

  int request_error_code;

  /* probes */
  int idle_probe_id;

  /* only accessed from the streaming thread */
  gboolean renegotiating;

  GstClock *v4l2_clock;

  /* State shared between the event thread (gst_uvc_sink_task) and the
   * streaming thread, protected by state_lock. The lock is only held to
   * access these fields, never while calling into other elements or pads. */
  GMutex state_lock;
  /* the host is streaming */
  gboolean streaming;
  /* a STREAMON / STREAMOFF from the host is still to be handled */
  gboolean streamon;
  gboolean streamoff;
  /* the format committed by the host differs from the negotiated caps */
  gboolean caps_changed;
  /* the caps selected by the host, or the probed caps before that */
  GstCaps *cur_caps;
};

#define UVCSINK_STATE_LOCK(s) g_mutex_lock (&(s)->state_lock)
#define UVCSINK_STATE_UNLOCK(s) g_mutex_unlock (&(s)->state_lock)

int uvc_events_process_data(GstUvcSink * self,
                                   const struct uvc_request_data *data);
int uvc_events_process_setup(GstUvcSink * self,
                         const struct usb_ctrlrequest *ctrl,
                         struct uvc_request_data *resp);
int uvc_fill_streaming_control(GstUvcSink * self,
                           struct uvc_streaming_control *ctrl,
                           int iframe, int iformat, unsigned int dwival);
G_END_DECLS
