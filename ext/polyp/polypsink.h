#ifndef __GST_POLYPSINK_H__
#define __GST_POLYPSINK_H__

#include <gst/gst.h>

#include <polyp/polyplib-context.h>
#include <polyp/polyplib-stream.h>

G_BEGIN_DECLS

#define GST_TYPE_POLYPSINK \
  (gst_polypsink_get_type())
#define GST_POLYPSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_POLYPSINK,GstPolypSink))
#define GST_POLYPSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_POLYPSINK,GstPolypSinkClass))
#define GST_IS_POLYPSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_POLYPSINK))
#define GST_IS_POLYPSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_POLYPSINK))

typedef struct _GstPolypSink GstPolypSink;
typedef struct _GstPolypSinkClass GstPolypSinkClass;

struct _GstPolypSink {
    GstElement element;

    GstPad *sinkpad;

    char *server, *sink;

    struct pa_mainloop *mainloop;
    struct pa_mainloop_api *mainloop_api;
    struct pa_context *context;
    struct pa_stream *stream;
    struct pa_sample_spec sample_spec;

    int negotiated;

    GstBuffer *buffer;
    size_t  buffer_index;

    size_t counter;
    pa_usec_t latency;

    gboolean caching;
    char *cache_id;
};

struct _GstPolypSinkClass {
    GstElementClass parent_class;
};

GType gst_polypsink_get_type (void);

G_END_DECLS

#endif /* __GST_POLYPSINK_H__ */
