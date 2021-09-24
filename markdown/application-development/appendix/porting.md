---
title: Porting 0.8 applications to 0.10
...

# Porting 0.8 applications to 0.10

This section of the appendix will discuss shortly what changes to
applications will be needed to quickly and conveniently port most
applications from GStreamer-0.8 to GStreamer-0.10, with references to
the relevant sections in this Application Development Manual where
needed. With this list, it should be possible to port simple
applications to GStreamer-0.10 in less than a day.

## List of changes

  - Most functions returning an object or an object property have been
    changed to return its own reference rather than a constant reference
    of the one owned by the object itself. The reason for this change is
    primarily thread safety. This means, effectively, that return values
    of functions such as `gst_element_get_pad ()`, `gst_pad_get_name ()`
    and many more like these have to be free'ed or unreferenced after
    use. Check the API references of each function to know for sure
    whether return values should be free'ed or not. It is important that
    all objects derived from GstObject are ref'ed/unref'ed using
    gst\_object\_ref() and gst\_object\_unref() respectively (instead of
    g\_object\_ref/unref).

  - Applications should no longer use signal handlers to be notified of
    errors, end-of-stream and other similar pipeline events. Instead,
    they should use the `GstBus`, which has been discussed in
    [Bus][bus]. The bus will take care that the messages will
    be delivered in the context of a main loop, which is almost
    certainly the application's main thread. The big advantage of this
    is that applications no longer need to be thread-aware; they don't
    need to use `g_idle_add ()` in the signal handler and do the actual
    real work in the idle-callback. GStreamer now does all that internally.

  - Related to this, `gst_bin_iterate ()` has been removed. Pipelines
    will iterate in their own thread, and applications can simply run a
    `GMainLoop` (or call the mainloop of their UI toolkit, such as
    `gtk_main ()`).

  - State changes can be delayed (ASYNC). Due to the new fully threaded
    nature of GStreamer-0.10, state changes are not always immediate, in
    particular changes including the transition from READY to PAUSED
    state. This means two things in the context of porting applications:
    first of all, it is no longer always possible to do
    `gst_element_set_state ()` and check for a return value of
    GST\_STATE\_CHANGE\_SUCCESS, as the state change might be delayed
    (ASYNC) and the result will not be known until later. You should
    still check for GST\_STATE\_CHANGE\_FAILURE right away, it is just
    no longer possible to assume that everything that is not SUCCESS
    means failure. Secondly, state changes might not be immediate, so
    your code needs to take that into account. You can wait for a state
    change to complete if you use GST\_CLOCK\_TIME\_NONE as timeout
    interval with `gst_element_get_state ()`.

  - In 0.8, events and queries had to manually be sent to sinks in
    pipelines (unless you were using playbin). This is no longer the
    case in 0.10. In 0.10, queries and events can be sent to toplevel
    pipelines, and the pipeline will do the dispatching internally for
    you. This means less bookkeeping in your application. For a short
    code example, see [Position tracking and seeking][queries-and-events].
    Related, seeking is now threadsafe, and your video output will show the new
    video position's frame while seeking, providing a better user experience.

  - The `GstThread` object has been removed. Applications can now simply
    put elements in a pipeline with optionally some “queue” elements in
    between for buffering, and GStreamer will take care of creating
    threads internally. It is still possible to have parts of a pipeline
    run in different threads than others, by using the “queue” element.
    See [Threads][threads] for details.

  - Filtered caps -\> capsfilter element (the pipeline syntax for
    gst-launch has not changed though).

  - libgstgconf-0.10.la does not exist. Use the “gconfvideosink” and
    “gconfaudiosink” elements instead, which will do live-updates and
    require no library linking.

  - The “new-pad” and “state-change” signals on `GstElement` were
    renamed to “pad-added” and “state-changed”.

  - `gst_init_get_popt_table ()` has been removed in favour of the new
    GOption command line option API that was added to GLib 2.6.
    `gst_init_get_option_group ()` is the new GOption-based equivalent
    to `gst_init_get_ptop_table ()`.

[bus]: application-development/basics/bus.md
[threads]: application-development/advanced/threads.md
[queries-and-sevents]: application-development/advanced/queryevents.md
