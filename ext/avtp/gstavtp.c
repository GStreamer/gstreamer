/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/**
 * plugin-avtp:
 *
 * ## Audio Video Transport Protocol (AVTP) Plugin
 *
 * The AVTP plugin implements typical Talker and Listener functionalities that
 * can be leveraged by GStreamer-based applications in order to implement TSN
 * audio/video applications.
 *
 * ### Dependencies
 *
 * The plugin uses libavtp to handle AVTP packetization. Libavtp source code can
 * be found in https://github.com/AVnu/libavtp as well as instructions to build
 * and install it.
 *
 * If libavtp isn't detected by configure, the plugin isn't built.
 *
 * ### The application/x-avtp mime type
 *
 * For valid AVTPDUs encapsulated in GstBuffers, we use the caps with mime type
 * application/x-avtp.
 *
 * AVTP mime type is pretty simple and has no fields.
 *
 * ### PTP Clock
 *
 * The AVTP plugin elements require that GStreamer pipeline clock be in sync
 * with the network generalized PTP clock (gPTP). Applications using the AVTP
 * plugin elements can achieve that by using GstPtpClock as the pipeline clock.
 *
 * Note that GstPtpClock is a UDP slave only clock, meaning that some other
 * endpoint needs to provide the gPTP master clock.
 *
 * One can use, on another endpoint on the network, Linuxptp project ptp4l
 * daemon to provide a gPTP master clock on the network over UDP:
 *
 *     $ ptp4l -i $IFNAME
 *
 * For further information check ptp4l(8).
 *
 * ### FQTSS Setup
 *
 * FQTSS (Forwarding and Queuing Enhancements for Time-Sensitive Streams) can be
 * enabled on Linux with the help of the mqprio and cbs qdiscs provided by the
 * Linux Traffic Control. Below we provide an example to configure those qdiscs
 * in order to transmit a CVF H.264 stream 1280x720@30fps. For further
 * information on how to configure these qdiscs check tc-mqprio(8) and
 * tc-cbs(8) man pages.
 *
 * On the host that will run as AVTP Talker (pipeline that generates the video
 * stream), run the following commands:
 *
 * Configure mpqrio qdisc (replace $HANDLE_ID by an unused handle ID):
 *
 *     $ tc qdisc add dev $IFNAME parent root handle $HANDLE_ID mqprio \
 *         num_tc 3 map 2 2 1 0 2 2 2 2 2 2 2 2 2 2 2 2 \
 *         queues 1@0 1@1 2@2 hw 0
 *
 * Configure cbs qdisc:
 *
 *     $ tc qdisc replace dev $IFNAME parent $HANDLE_ID:1 cbs idleslope 27756 \
 *         sendslope -972244 hicredit 42 locredit -1499 offload 1
 *
 * No FQTSS configuration is required at the host running as AVTP Listener.
 *
 * ### Capabilities
 *
 * The `avtpsink` and `avtpsrc` elements open `AF_PACKET` sockets, which require
 * `CAP_NET_RAW` capability. Therefore, applications must have that capability
 * in order to successfully use this element. For instance, one can use:
 *
 *     $ sudo setcap cap_net_raw+ep <application>
 *
 * Applications can drop this capability after the sockets are open, after
 * `avtpsrc` or `avtpsink` elements transition to PAUSED state. See setcap(8)
 * man page for more information.
 *
 * ### Elements configuration
 *
 * Each element has its own configuration properties, with some being common
 * to several elements. Basic properties are:
 *
 *   * streamid (avtpaafpay, avtpcvfpay, avtpaafdepay, avtpcvfdepay): Stream ID
 *     associated with the stream.
 *
 *   * ifname (avtpsink, avtpsrc): Network interface used to send/receive
 *     AVTP packets.
 *
 *   * dst-macaddr (avtpsink, avtpsrc): Destination MAC address for the stream.
 *
 *   * priority (avtpsink): Priority used by the plugin to transmit AVTP
 *     traffic.
 *
 *   * mtt (avtpaafpay, avtpcvfpay): Maximum Transit Time, in nanoseconds, as
 *     defined in AVTP spec.
 *
 *   * tu (avtpaafpay, avtpcvfpay): Maximum Time Uncertainty, in nanoseconds, as
 *     defined in AVTP spec.
 *
 *   * processing-deadline (avtpaafpay, avtpcvfpay, avtpsink): Maximum amount of
 *     time, in nanoseconds, that the pipeline is expected to process any
 *     buffer. This value should be in sync between the one used on the
 *     payloader and the sink, as this time is also taken into consideration to
 *     define the correct presentation time of the packets on the AVTP listener
 *     side. It should be as low as possible (zero if possible).
 *
 *   * tstamp-mode (avtpaafpay): AAF timestamping mode, as defined in AVTP spec.
 *
 *   * mtu (avtpcvfpay): Maximum Transmit Unit of the underlying network, used
 *     to determine when to fragment a CVF packet and how big it should be.
 *
 * Check each element documentation for more details.
 *
 *
 * ### Running a sample pipeline
 *
 * The following pipelines assume a hypothetical `-k ptp` flag that forces the
 * pipeline clock to be GstPtpClock. A real application would programmatically
 * define GstPtpClock as the pipeline clock (see next section). It is also
 * assumed that `gst-launch-1.0` has CAP_NET_RAW capability.
 *
 * On the AVTP talker, the following pipeline can be used to generate an H.264
 * stream to be sent via network using AVTP:
 *
 *     $ gst-launch-1.0 -k ptp videotestsrc is-live=true ! clockoverlay ! \
 *         x264enc ! avtpcvfpay processing-deadline=20000000 ! \
 *         avtpsink ifname=$IFNAME
 *
 * On the AVTP listener host, the following pipeline can be used to get the
 * AVTP stream, depacketize it and show it on the screen:
 *
 *     $ gst-launch-1.0 -k ptp avtpsrc ifname=$IFNAME ! avtpcvfdepay ! \
 *         vaapih264dec ! videoconvert ! clockoverlay halignment=right ! \
 *         queue ! autovideosink
 *
 * ### Pipeline clock
 *
 * The AVTP plugin elements require that the pipeline clock is in sync with
 * the network PTP clock. As GStreamer has a GstPtpClock, using it should be
 * the simplest way of achieving that.
 *
 * However, as there's no way of forcing a clock to a pipeline using
 * gst-launch-1.0 application, even for quick tests, it's necessary to have
 * an application. One can refer to GStreamer "hello world" application,
 * remembering to set the pipeline clock to GstPtpClock before putting the
 * pipeline on "PLAYING" state. Some code like:
 *
 *     GstClock *clk = gst_ptp_clock_new("ptp-clock", 0);
 *     gst_clock_wait_for_sync(clk, GST_CLOCK_TIME_NONE);
 *     gst_pipeline_use_clock (GST_PIPELINE (pipeline), clk);
 *
 * Would do the trick.
 *
 * ### Disclaimer
 *
 * It's out of scope for the AVTP plugin to verify how it is invoked, should
 * a malicious software do it for Denial of Service attempts, or other
 * compromises attempts.
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

#include "gstavtpaafdepay.h"
#include "gstavtpaafpay.h"
#include "gstavtpcvfdepay.h"
#include "gstavtpcvfpay.h"
#include "gstavtpsink.h"
#include "gstavtpsrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_avtp_aaf_pay_plugin_init (plugin))
    return FALSE;
  if (!gst_avtp_aaf_depay_plugin_init (plugin))
    return FALSE;
  if (!gst_avtp_sink_plugin_init (plugin))
    return FALSE;
  if (!gst_avtp_src_plugin_init (plugin))
    return FALSE;
  if (!gst_avtp_cvf_pay_plugin_init (plugin))
    return FALSE;
  if (!gst_avtp_cvf_depay_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    avtp, "Audio/Video Transport Protocol (AVTP) plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
