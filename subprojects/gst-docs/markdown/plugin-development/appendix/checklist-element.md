---
title: Things to check when writing an element
...

# Things to check when writing an element

This chapter contains a fairly random selection of things to take care
of when writing an element. It's up to you how far you're going to stick
to those guidelines. However, keep in mind that when you're writing an
element and hope for it to be included in the mainstream GStreamer
distribution, it *has to* meet those requirements. As far as possible,
we will try to explain why those requirements are set.

## About states

  - Make sure the state of an element gets reset when going to `NULL`.
    Ideally, this should set all object properties to their original
    state. This function should also be called from \_init.

  - Make sure an element forgets *everything* about its contained stream
    when going from `PAUSED` to `READY`. In `READY`, all stream states
    are reset. An element that goes from `PAUSED` to `READY` and back to
    `PAUSED` should start reading the stream from the start again.

  - People that use `gst-launch` for testing have the tendency to not
    care about cleaning up. This is *wrong*. An element should be tested
    using various applications, where testing not only means to “make
    sure it doesn't crash”, but also to test for memory leaks using
    tools such as `valgrind`. Elements have to be reusable in a pipeline
    after having been reset.

## Debugging

  - Elements should *never* use their standard output for debugging
    (using functions such as `printf
                                            ()` or `g_print ()`). Instead, elements should use the logging
    functions provided by GStreamer, named `GST_DEBUG ()`, `GST_LOG ()`,
    `GST_INFO ()`, `GST_WARNING ()` and `GST_ERROR ()`. The various
    logging levels can be turned on and off at runtime and can thus be
    used for solving issues as they turn up. Instead of `GST_LOG ()` (as
    an example), you can also use `GST_LOG_OBJECT
                                            ()` to print the object that you're logging output for.

  - Ideally, elements should use their own debugging category. Most
    elements use the following code to do that:

    ``` c
    GST_DEBUG_CATEGORY_STATIC (myelement_debug);
    #define GST_CAT_DEFAULT myelement_debug

    [..]

    static void
    gst_myelement_class_init (GstMyelementClass *klass)
    {
    [..]
      GST_DEBUG_CATEGORY_INIT (myelement_debug, "myelement",
                   0, "My own element");
    }

    ```

    At runtime, you can turn on debugging using the commandline option
    `--gst-debug=myelement:5`.

  - Elements should use GST\_DEBUG\_FUNCPTR when setting pad functions
    or overriding element class methods, for example:

    ``` c
    gst_pad_set_event_func (myelement->srcpad,
        GST_DEBUG_FUNCPTR (my_element_src_event));

    ```

    This makes debug output much easier to read later on.

  - Elements that are aimed for inclusion into one of the GStreamer
    modules should ensure consistent naming of the element name,
    structures and function names. For example, if the element type is
    GstYellowFooDec, functions should be prefixed with
    gst\_yellow\_foo\_dec\_ and the element should be registered as
    'yellowfoodec'. Separate words should be separate in this scheme, so
    it should be GstFooDec and gst\_foo\_dec, and not GstFoodec and
    gst\_foodec.

## Querying, events and the like

  - All elements to which it applies (sources, sinks, demuxers) should
    implement query functions on their pads, so that applications and
    neighbour elements can request the current position, the stream
    length (if known) and so on.

  - Elements should make sure they forward events they do not handle
    with gst\_pad\_event\_default (pad, parent, event) instead of just
    dropping them. Events should never be dropped unless specifically
    intended.

  - Elements should make sure they forward queries they do not handle
    with gst\_pad\_query\_default (pad, parent, query) instead of just
    dropping them.

## Testing your element

  - `gst-launch` is *not* a good tool to show that your element is
    finished. Applications such as Rhythmbox and Totem (for GNOME) or
    AmaroK (for KDE) *are*. `gst-launch` will not test various things
    such as proper clean-up on reset, event handling, querying and so
    on.

  - Parsers and demuxers should make sure to check their input. Input
    cannot be trusted. Prevent possible buffer overflows and the like.
    Feel free to error out on unrecoverable stream errors. Test your
    demuxer using stream corruption elements such as `breakmydata`
    (included in gst-plugins). It will randomly insert, delete and
    modify bytes in a stream, and is therefore a good test for
    robustness. If your element crashes when adding this element, your
    element needs fixing. If it errors out properly, it's good enough.
    Ideally, it'd just continue to work and forward data as much as
    possible.

  - Demuxers should not assume that seeking works. Be prepared to work
    with unseekable input streams (e.g. network sources) as well.

  - Sources and sinks should be prepared to be assigned another clock
    than the one they expose themselves. Always use the provided clock
    for synchronization, else you'll get A/V sync issues.
