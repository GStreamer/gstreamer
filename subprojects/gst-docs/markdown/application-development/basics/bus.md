---
title: Bus
...

# Bus

A bus is a simple system that takes care of forwarding messages from the
streaming threads to an application in its own thread context. The
advantage of a bus is that an application does not need to be
thread-aware in order to use GStreamer, even though GStreamer itself is
heavily threaded.

Every pipeline contains a bus by default, so applications do not need to
create a bus or anything. The only thing applications should do is set a
message handler on a bus, which is similar to a signal handler to an
object. When the mainloop is running, the bus will periodically be
checked for new messages, and the callback will be called when any
message is available.

## How to use a bus

There are two different ways to use a bus:

  - Run a GLib/Gtk+ main loop (or iterate the default GLib main context
    yourself regularly) and attach some kind of watch to the bus. This
    way the GLib main loop will check the bus for new messages and
    notify you whenever there are messages.

    Typically you would use `gst_bus_add_watch ()` or
    `gst_bus_add_signal_watch ()` in this case.

    To use a bus, attach a message handler to the bus of a pipeline
    using `gst_bus_add_watch ()`. This handler will be called whenever
    the pipeline emits a message to the bus. In this handler, check the
    signal type (see next section) and do something accordingly. The
    return value of the handler should be `TRUE` to keep the handler
    attached to the bus, return `FALSE` to remove it.

  - Check for messages on the bus yourself. This can be done using
    `gst_bus_peek ()` and/or `gst_bus_poll ()`.


{{ bus_example.c }}

It is important to know that the handler will be called in the thread
context of the mainloop. This means that the interaction between the
pipeline and application over the bus is *asynchronous*, and thus not
suited for some real-time purposes, such as cross-fading between audio
tracks, doing (theoretically) gapless playback or video effects. All
such things should be done in the pipeline context, which is easiest by
writing a GStreamer plug-in. It is very useful for its primary purpose,
though: passing messages from pipeline to application. The advantage of
this approach is that all the threading that GStreamer does internally
is hidden from the application and the application developer does not
have to worry about thread issues at all.

Note that if you're using the default GLib mainloop integration, you
can, instead of attaching a watch, connect to the “message” signal on
the bus. This way you don't have to `switch()` on all possible message
types; just connect to the interesting signals in form of
`message::<type>`, where `<type>` is a specific message type (see
the next section for an explanation of message types).

The above snippet could then also be written as:

``` c
GstBus *bus;

[..]

bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
gst_bus_add_signal_watch (bus);
g_signal_connect (bus, "message::error", G_CALLBACK (cb_message_error), NULL);
g_signal_connect (bus, "message::eos", G_CALLBACK (cb_message_eos), NULL);

[..]

```

If you aren't using GLib mainloop, the asynchronous message signals
won't be available by default. You can however install a custom sync
handler that wakes up the custom mainloop and that uses
`gst_bus_async_signal_func ()` to emit the signals. (see also
[documentation](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstBus.html)
for details)

## Message types

GStreamer has a few pre-defined message types that can be passed over
the bus. The messages are extensible, however. Plug-ins can define
additional messages, and applications can decide to either have specific
code for those or ignore them. All applications are strongly recommended
to at least handle error messages by providing visual feedback to the
user.

All messages have a message source, type and timestamp. The message
source can be used to see which element emitted the message. For some
messages, for example, only the ones emitted by the top-level pipeline
will be interesting to most applications (e.g. for state-change
notifications). Below is a list of all messages and a short explanation
of what they do and how to parse message-specific content.

  - Error, warning and information notifications: those are used by
    elements if a message should be shown to the user about the state of
    the pipeline. Error messages are fatal and terminate the
    data-passing. The error should be repaired to resume pipeline
    activity. Warnings are not fatal, but imply a problem nevertheless.
    Information messages are for non-problem notifications. All those
    messages contain a `GError` with the main error type and message,
    and optionally a debug string. Both can be extracted using
    `gst_message_parse_error()`, `_parse_warning ()` and `_parse_info ()`.
    Both error and debug strings should be freed after use.

  - End-of-stream notification: this is emitted when the stream has
    ended. The state of the pipeline will not change, but further media
    handling will stall. Applications can use this to skip to the next
    song in their playlist. After end-of-stream, it is also possible to
    seek back in the stream. Playback will then continue automatically.
    This message has no specific arguments.

  - Tags: emitted when metadata was found in the stream. This can be
    emitted multiple times for a pipeline (e.g. once for descriptive
    metadata such as artist name or song title, and another one for
    stream-information, such as samplerate and bitrate). Applications
    should cache metadata internally. `gst_message_parse_tag()` should be
    used to parse the taglist, which should be `gst_tag_list_unref ()`'ed
    when no longer needed.

  - State-changes: emitted after a successful state change.
    `gst_message_parse_state_changed ()` can be used to parse the old
    and new state of this transition.

  - Buffering: emitted during caching of network-streams. One can
    manually extract the progress (in percent) from the message by
    extracting the “buffer-percent” property from the structure returned
    by `gst_message_get_structure()`. See also [Buffering][buffering]

  - Element messages: these are special messages that are unique to
    certain elements and usually represent additional features. The
    element's documentation should mention in detail which element
    messages a particular element may send. As an example, the 'qtdemux'
    QuickTime demuxer element may send a 'redirect' element message on
    certain occasions if the stream contains a redirect instruction.

  - Application-specific messages: any information on those can be
    extracted by getting the message structure (see above) and reading
    its fields. Usually these messages can safely be ignored.

    Application messages are primarily meant for internal use in
    applications in case the application needs to marshal information
    from some thread into the main thread. This is particularly useful
    when the application is making use of element signals (as those
    signals will be emitted in the context of the streaming thread).

[buffering]: application-development/advanced/buffering.md
