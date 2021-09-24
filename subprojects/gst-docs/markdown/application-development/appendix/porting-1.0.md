---
title: Porting 0.10 applications to 1.0
...

# Porting 0.10 applications to 1.0

This section outlines some of the changes necessary to port applications
from GStreamer-0.10 to GStreamer-1.0. For a comprehensive and up-to-date
list, see the separate [Porting
to 1.0](https://gitlab.freedesktop.org/gstreamer/gstreamer/tree/master/docs/random/porting-to-1.0.txt)
document.

It should be possible to port simple applications to GStreamer-1.0 in
less than a day.

## List of changes

  - All deprecated methods were removed. Recompile against 0.10 with
    GST\_DISABLE\_DEPRECATED defined (such as by adding
    -DGST\_DISABLE\_DEPRECATED to the compiler flags) and fix issues
    before attempting to port to 1.0.

  - "playbin2" has been renamed to "playbin", with similar API

  - "decodebin2" has been renamed to "decodebin", with similar API. Note
    that there is no longer a "new-decoded-pad" signal, just use
    GstElement's "pad-added" signal instead (but don't forget to remove
    the 'gboolean last' argument from your old signal callback functino
    signature).

  - the names of some "formatted" pad templates has been changed from
    e.g. "src%d" to "src%u" or "src\_%u" or similar, since we don't want
    to see negative numbers in pad names. This mostly affects
    applications that create request pads from elements.

  - some elements that used to have a single dynamic source pad have a
    source pad now. Example: wavparse, id3demux, iceydemux, apedemux.
    (This does not affect applications using decodebin or playbin).

  - playbin now proxies the GstVideoOverlay (former GstXOverlay)
    interface, so most applications can just remove the sync bus handler
    where they would set the window ID, and instead just set the window
    ID on playbin from the application thread before starting playback.

    playbin also proxies the GstColorBalance and GstNavigation
    interfaces, so applications that use this don't need to go fishing
    for elements that may implement those any more, but can just use on
    playbin unconditionally.

  - multifdsink, tcpclientsink, tcpclientsrc, tcpserversrc the protocol
    property is removed, use gdppay and gdpdepay.

  - XML serialization was removed.

  - Probes and pad blocking was merged into new pad probes.

  - Position, duration and convert functions no longer use an inout
    parameter for the destination format.

  - Video and audio caps were simplified. audio/x-raw-int and
    audio/x-raw-float are now all under the audio/x-raw media type.
    Similarly, video/x-raw-rgb and video/x-raw-yuv are now video/x-raw.

  - ffmpegcolorspace was removed and replaced with videoconvert.

  - GstMixerInterface / GstTunerInterface were removed without
    replacement.

  - The GstXOverlay interface was renamed to GstVideoOverlay, and now
    part of the video library in gst-plugins-base, as the interfaces
    library no longer exists.

    The name of the GstXOverlay "prepare-xwindow-id" message has changed
    to "prepare-window-handle" (and GstXOverlay has been renamed to
    GstVideoOverlay). Code that checks for the string directly should be
    changed to use
    gst\_is\_video\_overlay\_prepare\_window\_handle\_message(message)
    instead.

  - The GstPropertyProbe interface was removed. There is no replacement
    for it in GStreamer 1.0.x and 1.2.x, but since version 1.4 there is
    a more featureful replacement for device discovery and feature
    querying provided by GstDeviceMonitor, GstDevice, and friends. See
    the ["GStreamer Device Discovery and Device Probing"
    documentation](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-device-probing.html).

  - gst\_uri\_handler\_get\_uri() and the get\_uri vfunc now return a
    copy of the URI string

    gst\_uri\_handler\_set\_uri() and the set\_uri vfunc now take an
    additional GError argument so the handler can notify the caller why
    it didn't accept a particular URI.

    gst\_uri\_handler\_set\_uri() now checks if the protocol of the URI
    passed is one of the protocols advertised by the uri handler, so
    set\_uri vfunc implementations no longer need to check that as well.

  - GstTagList is now an opaque mini object instead of being typedefed
    to a GstStructure. While it was previously okay (and in some cases
    required because of missing taglist API) to cast a GstTagList to a
    GstStructure or use gst\_structure\_\* API on taglists, you can no
    longer do that. Doing so will cause crashes.

    Also, tag lists are refcounted now, and can therefore not be freely
    modified any longer. Make sure to call
    gst\_tag\_list\_make\_writable (taglist) before adding, removing or
    changing tags in the taglist.

    GST\_TAG\_IMAGE, GST\_TAG\_PREVIEW\_IMAGE, GST\_TAG\_ATTACHMENT:
    many tags that used to be of type GstBuffer are now of type
    GstSample (which is basically a struct containing a buffer alongside
    caps and some other info).

  - GstController has now been merged into GstObject. It does not exists
    as an individual object anymore. In addition core contains a
    GstControlSource base class and the GstControlBinding. The actual
    control sources are in the controller library as before. The 2nd big
    change is that control sources generate a sequence of gdouble values
    and those are mapped to the property type and value range by
    GstControlBindings.

    The whole gst\_controller\_\* API is gone and now available in
    simplified form under gst\_object\_\*. ControlSources are now
    attached via GstControlBinding to properties. There are no GValue
    arguments used anymore when programming control sources.
