/*
 * WebRTC Audio Processing Elements
 *
 *  Copyright 2016 Collabora Ltd
 *    @author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_WEBRTC_ECHO_PROBE_H__
#define __GST_WEBRTC_ECHO_PROBE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_WEBRTC_ECHO_PROBE            (gst_webrtc_echo_probe_get_type())
#define GST_WEBRTC_ECHO_PROBE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_ECHO_PROBE,GstWebrtcEchoProbe))
#define GST_IS_WEBRTC_ECHO_PROBE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_ECHO_PROBE))
#define GST_WEBRTC_ECHO_PROBE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_ECHO_PROBE,GstWebrtcEchoProbeClass))
#define GST_IS_WEBRTC_ECHO_PROBE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_ECHO_PROBE))
#define GST_WEBRTC_ECHO_PROBE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_ECHO_PROBE,GstWebrtcEchoProbeClass))

#define GST_WEBRTC_ECHO_PROBE_LOCK(obj) g_mutex_lock (&GST_WEBRTC_ECHO_PROBE (obj)->lock)
#define GST_WEBRTC_ECHO_PROBE_UNLOCK(obj) g_mutex_unlock (&GST_WEBRTC_ECHO_PROBE (obj)->lock)

typedef struct _GstWebrtcEchoProbe GstWebrtcEchoProbe;
typedef struct _GstWebrtcEchoProbeClass GstWebrtcEchoProbeClass;

/**
 * GstWebrtcEchoProbe:
 *
 * The adder object structure.
 */
struct _GstWebrtcEchoProbe
{
  GstAudioFilter parent;

  /* This lock is required as the DSP may need to lock itself using it's
   * object lock and also lock the probe. The natural order for the DSP is
   * to lock the DSP and then the echo probe. If we where using the probe
   * object lock, we'd be racing with GstBin which will lock sink to src,
   * and may accidently reverse the order. */
  GMutex lock;

  /* Protected by the lock */
  GstAudioInfo info;
  guint period_size;
  GstClockTime latency;
  gint delay;

  GstSegment segment;
  GstAdapter *adapter;

  /* Private */
  gboolean acquired;
};

struct _GstWebrtcEchoProbeClass
{
  GstAudioFilterClass parent_class;
};

GType gst_webrtc_echo_probe_get_type (void);

GstWebrtcEchoProbe *gst_webrtc_acquire_echo_probe (const gchar * name);
void gst_webrtc_release_echo_probe (GstWebrtcEchoProbe * probe);
gint gst_webrtc_echo_probe_read (GstWebrtcEchoProbe * self,
    GstClockTime rec_time, gpointer frame);

G_END_DECLS
#endif /* __GST_WEBRTC_ECHO_PROBE_H__ */
