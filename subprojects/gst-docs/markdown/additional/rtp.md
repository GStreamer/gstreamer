# RTP and RTSP support

GStreamer has excellent support for both RTP and RTSP, and its RTP/RTSP
stack has proved itself over years of being widely used in production use
in a variety of mission-critical and low-latency scenarios, from small
embedded devices to large-scale videoconferencing and command-and-control
systems.

## GStreamer RTSP Server

GStreamer's RTSP server (gst-rtsp-server) is a featureful and easy-to-use
library that allows applications to implement a complete RTSP server with
just a couple of lines of code.

It is multi-threaded, scalable and flexible, and provides support for
static or dynamic mount points, authentication, retransmission (rtx),
encryption (srtp, secure RTP), UDP unicast and multicast as well as
TCP interleaving, seeking, and optionally also cgroup integration for
advanced resource management and control. It can also distribute a
GStreamer net client clock to GStreamer RTSP clients to facilitate
multi-device synchronization.


## GStreamer RTSP Client

The GStreamer <tt>rtspsrc</tt> element from gst-plugins-good is GStreamer's
high-level RTSP client abstraction. It can be used as a standalone element
directly, or can be used via <tt>playbin</tt> by passing an rtsp:// URI to
playbin. <tt>rtspsrc</tt> features a number of GObject properties that allow
you to configure it in all kinds of different ways, most notably a
<tt>"latency"</tt> property to configure the default jitterbuffer latency,
which you may want to configure to a lower value to achieve lower latency.


## RTP components

Most of GStreamer's key RTP components live in gst-plugins-good:
* The <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-rtpmanager.html">rtpmanager</a></tt>
   plugin contains elements like
    <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpbin.html">rtpbin</a></tt> and
    <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpjitterbuffer.html">rtpjitterbuffer</a></tt>
* The <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-rtp.html">rtp</a></tt> plugin
      contains RTP payloading and depayloading elements for many different
      codecs and container formats

with some lower-level libraries in gst-plugins-base:

  *   The <a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gstreamer-rtp.html">GStreamer RTP library</a>
      contains things such as RTP payloader/depayloader base classes and functions to handle RTP and RTCP buffers
  *   The <a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gstreamer-mikey.html">GStreamer MIKEY library</a>
      contains helper functions to deal with MIKEY messages for secure RTP
  *   The <a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gstreamer-rtsp.html">GStreamer RTSP library</a>
      contains low-level RTSP functionality used by gst-rtsp-server and higher-level objects such as rtspsrc.
  *   The <a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gstreamer-sdp.html">GStreamer SDP library</a>
      contains utility functions for SDP message parsing and creation.


Some of the main components are:

* <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpbin.html">rtpbin</a></tt>
  is the high-level RTP component and supports sending
  and receiving, just sending or just receiving data, with and without RTCP
  support. This is the bin that does it all: it adapts dynamically to your
  needs based on the requested pads; it also contains an rtpjitterbuffer.
* <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpjitterbuffer.html">rtpjitterbuffer</a></tt>
  is an RTP buffer that controls network jitter and reorders packets. It also
  dumps packets that arrive too late, handles packet retransmission and lost
  packet notification and adjusts for sender-receiver clock drift.
* <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpptdemux.html">rtpptdemux</a></tt>
  is an element that usually sits on the rtpbin src
  pad and will detect any new payload types that arrive in the RTP stream.
  It will then create a pad for that new payload and you can connect a
  depayloader/decoder pipeline to that pad.
* <tt><a href="/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpssrcdemux.html">rtpssrcdemux</a></tt>
  is an element that usually sits on the rtpbin src
  pad and will detect any new SSRCs that arrive in the RTP stream.
  It will then create a pad for that new SSRC and you can connect a
  depayloader/decoder pipeline to that pad.
* <tt><a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-gstrtpbasedepayload.html">GstRTPBaseDepayload</a></tt>
  is a base class for RTP depayloaders
* <tt><a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-gstrtpbasepayload.html">GstRTPBasePayload</a></tt>
  is a base class for RTP payloaders
* <tt><a href="/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-gstrtpbaseaudiopayload.html">GstRTPBaseAudioPayload</a>
  is a base class for audio RTP payloaders



Note that many RTP elements assume they receive RTP buffers with
<a href="/data/doc/gstreamer/head/gstreamer-libs/html/gstreamer-libs-GstNetAddressMeta.html">GstNetAddressMeta</a>
meta data set on them (as udpsrc will produce).
