## tracer: custom spans and events API

New `gst_trace_span_begin()` / `gst_trace_span_end()` and `gst_trace_event()`
API to instrument arbitrary code with custom tracing spans (timed regions) and
point events, each described by a typed `GstTraceFormat`.

This makes it possible to measure how long any operation takes - decoder setup,
a composition restack, a blocking state change - and see it on a profiler
timeline correlated with the rest of the pipeline. The `perfetto` tracer plots
the spans; the `log` tracer renders them to the `GST_TRACER` debug category.

Declare the field layout once, then bracket the region to measure with a
begin/end pair:

    /* GST_DEFINE_TRACE_FORMAT(name, ...) declares a `name ()` getter. */
    GST_DEFINE_TRACE_FORMAT (process_span, "element", STRING, "size", UINT)

    static void
    process (GstElement * element, guint size)
    {
      GST_TRACE_SCOPE_BEGIN (process_span, STRING (GST_ELEMENT_NAME (element)),
          UINT (size));
      ...
      GST_TRACE_SCOPE_END (process_span);
    }

Use `GST_TRACE_EVENT()` for an instantaneous point event.
