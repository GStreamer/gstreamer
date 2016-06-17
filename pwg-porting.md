---
title: Porting 0.8 plug-ins to 0.10
...

# Porting 0.8 plug-ins to 0.10

This section of the appendix will discuss shortly what changes to
plugins will be needed to quickly and conveniently port most
applications from GStreamer-0.8 to GStreamer-0.10, with references to
the relevant sections in this Plugin Writer's Guide where needed. With
this list, it should be possible to port most plugins to GStreamer-0.10
in less than a day. Exceptions are elements that will require a base
class in 0.10 (sources, sinks), in which case it may take a lot longer,
depending on the coder's skills (however, when using the `GstBaseSink`
and `GstBaseSrc` base-classes, it shouldn't be all too bad), and
elements requiring the deprecated bytestream interface, which should
take 1-2 days with random access. The scheduling parts of muxers will
also need a rewrite, which will take about the same amount of time.

## List of changes

  - Discont events have been replaced by newsegment events. In 0.10, it
    is essential that you send a newsegment event downstream before you
    send your first buffer (in 0.8 the scheduler would invent discont
    events if you forgot them, in 0.10 this is no longer the case).

  - In 0.10, buffers have caps attached to them. Elements should
    allocate new buffers with `gst_pad_alloc_buffer ()`. See [Caps
    negotiation](pwg-negotiation.md) for more details.

  - Most functions returning an object or an object property have been
    changed to return its own reference rather than a constant reference
    of the one owned by the object itself. The reason for this change is
    primarily thread-safety. This means effectively that return values
    of functions such as `gst_element_get_pad ()`, `gst_pad_get_name
    ()`, `gst_pad_get_parent ()`, `gst_object_get_parent ()`, and many
    more like these have to be free'ed or unreferenced after use. Check
    the API references of each function to know for sure whether return
    values should be free'ed or not.

  - In 0.8, scheduling could happen in any way. Source elements could be
    `_get ()`-based or `_loop
                                            ()`-based, and any other element could be `_chain
                                            ()`-based or `_loop ()`-based, with no limitations. Scheduling in
    0.10 is simpler for the scheduler, and the element is expected to do
    some more work. Pads get assigned a scheduling mode, based on which
    they can either operate in random access-mode, in pipeline driving
    mode or in push-mode. all this is documented in detail in [Different
    scheduling modes](pwg-scheduling.md). As a result of this, the
    bytestream object no longer exists. Elements requiring byte-level
    access should now use random access on their sinkpads.

  - Negotiation is asynchronous. This means that downstream negotiation
    is done as data comes in and upstream negotiation is done whenever
    renegotiation is required. All details are described in [Caps
    negotiation](pwg-negotiation.md).

  - For as far as possible, elements should try to use existing base
    classes in 0.10. Sink and source elements, for example, could derive
    from `GstBaseSrc` and `GstBaseSink`. Audio sinks or sources could
    even derive from audio-specific base classes. All existing base
    classes have been discussed in [Pre-made base
    classes](pwg-other-base.md) and the next few chapters.

  - In 0.10, event handling and buffers are separated once again. This
    means that in order to receive events, one no longer has to set the
    `GST_FLAG_EVENT_AWARE` flag, but can simply set an event handling
    function on the element's sinkpad(s), using the function
    `gst_pad_set_event_function ()`. The `_chain ()`-function will only
    receive buffers.

  - Although core will wrap most threading-related locking for you (e.g.
    it takes the stream lock before calling your data handling
    functions), you are still responsible for locking around certain
    functions, e.g. object properties. Be sure to lock properly here,
    since applications will change those properties in a different
    thread than the thread which does the actual data passing\! You can
    use the `GST_OBJECT_LOCK ()` and `GST_OBJECT_UNLOCK
                                            ()` helpers in most cases, fortunately, which grabs the default
    property lock of the element.

  - `GstValueFixedList` and all `*_fixed_list_* ()` functions were
    renamed to `GstValueArray` and `*_array_*
                                            ()`.

  - The semantics of `GST_STATE_PAUSED` and `GST_STATE_PLAYING` have
    changed for elements that are not sink elements. Non-sink elements
    need to be able to accept and process data already in the
    `GST_STATE_PAUSED` state now (i.e. when prerolling the pipeline).
    More details can be found in [What are
    states?](pwg-statemanage-states.md).

  - If your plugin's state change function hasn't been superseded by
    virtual start() and stop() methods of one of the new base classes,
    then your plugin's state change functions may need to be changed in
    order to safely handle concurrent access by multiple threads. Your
    typical state change function will now first handle upwards state
    changes, then chain up to the state change function of the parent
    class (usually GstElementClass in these cases), and only then handle
    downwards state changes. See the vorbis decoder plugin in
    gst-plugins-base for an example.
    
    The reason for this is that in the case of downwards state changes
    you don't want to destroy allocated resources while your plugin's
    chain function (for example) is still accessing those resources in
    another thread. Whether your chain function might be running or not
    depends on the state of your plugin's pads, and the state of those
    pads is closely linked to the state of the element. Pad states are
    handled in the GstElement class's state change function, including
    proper locking, that's why it is essential to chain up before
    destroying allocated resources.
    
    As already mentioned above, you should really rewrite your plugin to
    derive from one of the new base classes though, so you don't have to
    worry about these things, as the base class will handle it for you.
    There are no base classes for decoders and encoders yet, so the
    above paragraphs about state changes definitively apply if your
    plugin is a decoder or an encoder.

  - `gst_pad_set_link_function ()`, which used to set a function that
    would be called when a format was negotiated between two `GstPad`s,
    now sets a function that is called when two elements are linked
    together in an application. For all practical purposes, you most
    likely want to use the function `gst_pad_set_setcaps_function ()`,
    nowadays, which sets a function that is called when the format
    streaming over a pad changes (so similar to `_set_link_function ()`
    in GStreamer-0.8).
    
    If the element is derived from a `GstBase` class, then override the
    `set_caps ()`.

  - `gst_pad_use_explicit_caps ()` has been replaced by
    `gst_pad_use_fixed_caps ()`. You can then set the fixed caps to use
    on a pad with `gst_pad_set_caps ()`.

