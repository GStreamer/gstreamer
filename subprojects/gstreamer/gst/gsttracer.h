/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracer.h: tracer base class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_TRACER_H__
#define __GST_TRACER_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gstobject.h>
#include <gst/gstconfig.h>

G_BEGIN_DECLS

typedef struct _GstTracer GstTracer;
typedef struct _GstTracerPrivate GstTracerPrivate;
typedef struct _GstTracerClass GstTracerClass;

/**
 * GstTraceFormat:
 *
 * Opaque registered format describing the fields of a trace span.
 * Obtain one via gst_trace_format_register() or
 * GST_DEFINE_TRACE_FORMAT() and pass it to
 * gst_trace_span_begin().
 *
 * Since: 1.30
 */
typedef struct _GstTraceFormat GstTraceFormat;

/**
 * GstTraceFormatBuilder:
 *
 * Opaque, chainable builder used to assemble a #GstTraceFormat field by
 * field; consumed by gst_trace_format_builder_register().
 *
 * Since: 1.30
 */
typedef struct _GstTraceFormatBuilder GstTraceFormatBuilder;

/**
 * GstTraceField:
 *
 * Transient field descriptor created by gst_trace_field_new() and added
 * to a #GstTraceFormatBuilder.
 *
 * Since: 1.30
 */
typedef struct _GstTraceField GstTraceField;

/**
 * GstTraceSpanId:
 *
 * Opaque identifier for an in-flight trace span. Returned by
 * gst_trace_span_begin() and passed to
 * gst_trace_span_end().
 *
 * Since: 1.30
 */
typedef guint64 GstTraceSpanId;

/**
 * GST_TRACE_SPAN_ID_NONE:
 *
 * Sentinel #GstTraceSpanId value meaning "no span". Returned by
 * gst_trace_span_begin() when no tracer is listening, and accepted
 * as a no-op by gst_trace_span_end().
 *
 * Since: 1.30
 */
#define GST_TRACE_SPAN_ID_NONE ((GstTraceSpanId) 0)

/**
 * GstTracerFieldType:
 * @GST_TRACER_FIELD_TYPE_BOOLEAN: a #gboolean
 * @GST_TRACER_FIELD_TYPE_INT: a #gint
 * @GST_TRACER_FIELD_TYPE_UINT: a #guint
 * @GST_TRACER_FIELD_TYPE_INT64: a #gint64
 * @GST_TRACER_FIELD_TYPE_UINT64: a #guint64
 * @GST_TRACER_FIELD_TYPE_DOUBLE: a #gdouble
 * @GST_TRACER_FIELD_TYPE_STRING: a borrowed string (may be %NULL)
 * @GST_TRACER_FIELD_TYPE_CLOCK_TIME: a #GstClockTime
 * @GST_TRACER_FIELD_TYPE_STRUCTURE: a borrowed #GstStructure (may be %NULL)
 * @GST_TRACER_FIELD_TYPE_OBJECT: a borrowed #GObject (may be %NULL)
 *
 * The type of a trace format field. This is a deliberately small, self
 * contained set rather than a #GType so the tracer stream stays independent
 * of the GObject type system.
 *
 * Since: 1.30
 */
typedef enum {
  GST_TRACER_FIELD_TYPE_BOOLEAN,
  GST_TRACER_FIELD_TYPE_INT,
  GST_TRACER_FIELD_TYPE_UINT,
  GST_TRACER_FIELD_TYPE_INT64,
  GST_TRACER_FIELD_TYPE_UINT64,
  GST_TRACER_FIELD_TYPE_DOUBLE,
  GST_TRACER_FIELD_TYPE_STRING,
  GST_TRACER_FIELD_TYPE_CLOCK_TIME,
  GST_TRACER_FIELD_TYPE_STRUCTURE,
  GST_TRACER_FIELD_TYPE_OBJECT,
} GstTracerFieldType;

/**
 * GstTraceValue:
 * @v_boolean: a #gboolean
 * @v_int: a #gint
 * @v_uint: a #guint
 * @v_int64: a #gint64
 * @v_uint64: a #guint64 (also carries %GST_TRACER_FIELD_TYPE_CLOCK_TIME)
 * @v_double: a #gdouble
 * @v_string: a borrowed string (may be %NULL)
 * @v_structure: a borrowed #GstStructure (may be %NULL)
 * @v_object: a borrowed #GObject (may be %NULL)
 *
 * A single borrowed value for a trace span row.
 *
 * Values are positional and interpreted according to the format description
 * used when registering the corresponding #GstTraceFormat. The value order
 * matches the field order declared on the builder.
 *
 * Since: 1.30
 */
typedef union _GstTraceValue {
  gboolean     v_boolean;
  gint         v_int;
  guint        v_uint;
  gint64       v_int64;
  guint64      v_uint64;
  gdouble      v_double;
  const gchar *v_string;
  GstStructure *v_structure;
  GObject      *v_object;
  /*< private >*/
  gpointer     _gst_reserved[4];
} GstTraceValue;

/**
 * GST_TRACE_VALUE_BOOLEAN:
 * @x: a #gboolean value
 *
 * Builds a `GstTraceValue` holding @x for use inside GST_TRACE_VALUES().
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_BOOLEAN(x) ((GstTraceValue) { .v_boolean = (x) })
/**
 * GST_TRACE_VALUE_INT:
 * @x: a #gint value
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_INT(x)     ((GstTraceValue) { .v_int     = (x) })
/**
 * GST_TRACE_VALUE_UINT:
 * @x: a #guint value
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_UINT(x)    ((GstTraceValue) { .v_uint    = (x) })
/**
 * GST_TRACE_VALUE_INT64:
 * @x: a #gint64 value
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_INT64(x)   ((GstTraceValue) { .v_int64   = (x) })
/**
 * GST_TRACE_VALUE_UINT64:
 * @x: a #guint64 value
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_UINT64(x)  ((GstTraceValue) { .v_uint64  = (x) })
/**
 * GST_TRACE_VALUE_DOUBLE:
 * @x: a #gdouble value
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_DOUBLE(x)  ((GstTraceValue) { .v_double  = (x) })
/**
 * GST_TRACE_VALUE_STRING:
 * @x: (nullable): a borrowed #gchar pointer; %NULL means the field is unset
 *
 * @x must outlive the trace call. %NULL is passed through unchanged so tracers
 * can distinguish unset from empty strings.
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_STRING(x)  ((GstTraceValue) { .v_string  = (x) })
/**
 * GST_TRACE_VALUE_CLOCK_TIME:
 * @x: a #GstClockTime value
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_CLOCK_TIME(x) ((GstTraceValue) { .v_uint64 = (x) })
/**
 * GST_TRACE_VALUE_STRUCTURE:
 * @x: (nullable): a borrowed #GstStructure
 *
 * @x must outlive the trace call.
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_STRUCTURE(x) ((GstTraceValue) { .v_structure = (x) })
/**
 * GST_TRACE_VALUE_OBJECT:
 * @x: (nullable): a borrowed #GObject
 *
 * @x must outlive the trace call.
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUE_OBJECT(x)  ((GstTraceValue) { .v_object = (GObject *) (x) })

/* Short-form helpers for GST_TRACE_VALUES() / GST_DEFINE_TRACE_FORMAT(). */
#include <gst/gsttracermacros.h>

/**
 * GST_TRACE_VALUES:
 * @...: one or more short value entries, e.g. `STRING (s)`, `UINT (n)`,
 *   `OBJECT (o)`; each names a `GST_TRACE_VALUE_*` constructor without the
 *   `GST_TRACE_VALUE_` prefix
 *
 * Builds a positional `GstTraceValue` array suitable for
 * gst_trace_span_begin(). Values are positional and must match the
 * field order declared on the corresponding #GstTraceFormat.
 *
 * Since: 1.30
 */
#define GST_TRACE_VALUES(...) \
  ((const GstTraceValue[]) { _GST_TRACE_MAP_VALUES (__VA_ARGS__) })

/**
 * GstTracerValueScope:
 * @GST_TRACER_VALUE_SCOPE_PROCESS: the value is related to the process
 * @GST_TRACER_VALUE_SCOPE_THREAD: the value is related to a thread
 * @GST_TRACER_VALUE_SCOPE_ELEMENT: the value is related to an #GstElement
 * @GST_TRACER_VALUE_SCOPE_PAD: the value is related to a #GstPad
 *
 * Tracing record will contain fields that contain a measured value or extra
 * meta-data. One such meta data are values that tell where a measurement was
 * taken. This enumeration declares to which scope such a meta data field
 * relates to. If it is e.g. %GST_TRACER_VALUE_SCOPE_PAD, then each of the log
 * events may contain values for different #GstPads.
 *
 * Since: 1.8
 */
typedef enum
{
  GST_TRACER_VALUE_SCOPE_PROCESS,
  GST_TRACER_VALUE_SCOPE_THREAD,
  GST_TRACER_VALUE_SCOPE_ELEMENT,
  GST_TRACER_VALUE_SCOPE_PAD
} GstTracerValueScope;

/**
 * GstTracerValueFlags:
 * @GST_TRACER_VALUE_FLAGS_NONE: no flags
 * @GST_TRACER_VALUE_FLAGS_OPTIONAL: the value is optional. When using this flag
 *   one need to have an additional boolean arg before this value in the
 *   var-args list passed to  gst_tracer_record_log().
 * @GST_TRACER_VALUE_FLAGS_AGGREGATED: the value is a combined figure, since the
 *   start of tracing. Examples are averages or accumulated durations.
 *
 * Flag that describe the value. These flags help applications processing the
 * logs to understand the values.
 */
typedef enum
{
  GST_TRACER_VALUE_FLAGS_NONE = 0,
  GST_TRACER_VALUE_FLAGS_OPTIONAL = (1 << 0),
  GST_TRACER_VALUE_FLAGS_AGGREGATED = (1 << 1),
} GstTracerValueFlags;

#define GST_TYPE_TRACER            (gst_tracer_get_type())
#define GST_TRACER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRACER,GstTracer))
#define GST_TRACER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRACER,GstTracerClass))
#define GST_IS_TRACER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRACER))
#define GST_IS_TRACER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRACER))
#define GST_TRACER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_TRACER,GstTracerClass))
#define GST_TRACER_CAST(obj)       ((GstTracer *)(obj))

/**
 * GstTracer:
 *
 * The opaque GstTracer instance structure
 */
struct _GstTracer {
  GstObject        parent;
  /*< private >*/
  GstTracerPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstTracerClass {
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_API
GType gst_tracer_get_type          (void);

GST_API
void gst_tracing_register_hook (GstTracer *tracer, const gchar *detail,
  GCallback func);

/* tracing modules */

GST_API
gboolean gst_tracer_register (GstPlugin * plugin, const gchar * name, GType type);

GST_API
GList* gst_tracing_get_active_tracers (void);

GST_API
GstTraceFormatBuilder * gst_trace_format_builder_new (const gchar * name);

GST_API
GstTraceFormatBuilder * gst_trace_format_builder_set_description (
    GstTraceFormatBuilder * builder, const gchar * description);

GST_API
GstTraceFormatBuilder * gst_trace_format_builder_add_field (
    GstTraceFormatBuilder * builder, const gchar * name,
    GstTracerFieldType type);

GST_API
GstTraceFormatBuilder * gst_trace_format_builder_add_field_full (
    GstTraceFormatBuilder * builder, GstTraceField * field);

GST_API
GstTraceFormat * gst_trace_format_builder_register (
    GstTraceFormatBuilder * builder);

GST_API
void gst_trace_format_builder_free (GstTraceFormatBuilder * builder);

GST_API
GstTraceField * gst_trace_field_new (const gchar * name,
    GstTracerFieldType type);

GST_API
GstTraceField * gst_trace_field_set_description (
    GstTraceField * field, const gchar * description);

GST_API
GstTraceField * gst_trace_field_set_scope (
    GstTraceField * field, GstTracerValueScope scope);

GST_API
GstTraceField * gst_trace_field_set_flags (
    GstTraceField * field, GstTracerValueFlags flags);

GST_API
void gst_trace_field_free (GstTraceField * field);

GST_API
GstTraceFormat * gst_trace_format_register (
    const gchar * name, ...) G_GNUC_NULL_TERMINATED;

/**
 * GST_DEFINE_TRACE_FORMAT:
 * @getter: identifier of the getter function to define; its name is also used
 *   verbatim as the span name surfaced to tracers
 * @...: at least one (field-name, short-field-type) pair, where the type is a
 *   #GstTracerFieldType name without the `GST_TRACER_FIELD_TYPE_` prefix (e.g.
 *   `STRING`, `UINT64`); the %NULL terminator is added automatically
 *
 * Defines a `static` getter that lazily registers the format on first call
 * and caches it for the lifetime of the process. Subsequent calls return the
 * cached #GstTraceFormat.
 *
 * Pass the getter to the scope macros to time a block.
 *
 * ``` c
 * GST_DEFINE_TRACE_FORMAT (decode_frame_span,
 *     "codec", STRING,
 *     "frame", UINT64)
 *
 * static void
 * decode_frame (MyDecoder * self, guint64 frame)
 * {
 *   GST_TRACE_SCOPE_BEGIN (decode_frame_span,
 *       STRING (self->codec),
 *       UINT64 (frame));
 *
 *   // ... decode the frame ...
 *
 *   GST_TRACE_SCOPE_END (decode_frame_span);
 * }
 * ```
 *
 * When the span does not match a lexical scope, hold the #GstTraceSpanId
 * returned by gst_trace_span_begin() and close it later with
 * gst_trace_span_end(). In both cases the field values are only evaluated
 * while a tracer is listening.
 *
 * Since: 1.30
 */
#define GST_DEFINE_TRACE_FORMAT(getter, ...)                             \
  static GstTraceFormat *                                                 \
  getter (void)                                                               \
  {                                                                           \
    static gsize __once = 0;                                                  \
    static GstTraceFormat *__format = NULL;                               \
    if (g_once_init_enter (&__once)) {                                        \
      __format = gst_trace_format_register (#getter,            \
          _GST_TRACE_MAP_FIELDS (__VA_ARGS__), NULL);                                   \
      g_once_init_leave (&__once, 1);                                         \
    }                                                                         \
    return __format;                                                          \
  }

GST_API
const gchar * gst_trace_format_get_name        (GstTraceFormat * format);

GST_API
const gchar * gst_trace_format_get_description (GstTraceFormat * format);

GST_API
guint         gst_trace_format_get_n_fields    (GstTraceFormat * format);

GST_API
const gchar * gst_trace_format_get_field_name  (GstTraceFormat * format,
                                                     guint index);

GST_API
GstTracerFieldType gst_trace_format_get_field_type (GstTraceFormat * format,
                                                     guint index);

GST_API
const gchar * gst_trace_format_get_field_description (
    GstTraceFormat * format, guint index);

GST_API
const GstStructure * gst_trace_format_get_field_structure (
    GstTraceFormat * format, guint index);

GST_API
gboolean      gst_trace_format_is_enabled      (GstTraceFormat * format);

GST_API
GstTraceSpanId gst_trace_span_begin (GstTraceFormat * format,
                                              const GstTraceValue * values);

GST_API
void gst_trace_span_end (GstTraceSpanId span_id);

GST_API
void gst_trace_span_end_and_clear (GstTraceSpanId * span_id);

GST_API
void gst_trace_event (GstTraceFormat * format,
                        const GstTraceValue * values);

/**
 * GST_TRACE_EVENT:
 * @format: a #GstTraceFormat (typically the result of a getter declared
 *   with GST_DEFINE_TRACE_FORMAT())
 * @...: one or more GST_TRACE_VALUE_* entries, in the field order declared
 *   on @format
 *
 * Emits a point event for @format. Unlike GST_TRACE_BEGIN(), an
 * event has no duration and no matching end; it is the structured
 * equivalent of the legacy #GstTracerRecord log entry and is emitted whenever
 * the GST_TRACER debug level is enabled. As with gst_tracer_record_log() the
 * values are always evaluated, so keep them cheap.
 *
 * Since: 1.30
 */
#define GST_TRACE_EVENT(format, ...) \
  gst_trace_event ((format), GST_TRACE_VALUES (__VA_ARGS__))

/**
 * GST_TRACE_BEGIN:
 * @format: a #GstTraceFormat (typically the result of a getter declared
 *   with GST_DEFINE_TRACE_FORMAT())
 * @...: one or more GST_TRACE_VALUE_* entries, in the field order declared
 *   on @format
 *
 * Opens a span and returns its #GstTraceSpanId. When @format has no
 * listening tracer the values are not evaluated and %GST_TRACE_SPAN_ID_NONE
 * is returned. The caller closes the span with gst_trace_span_end()
 * or gst_trace_span_end_and_clear() at the desired point; use this
 * when the span lifetime does not match a lexical scope (see GST_TRACE_FUNC()
 * for the auto-closed variant).
 *
 * Since: 1.30
 */
#define GST_TRACE_BEGIN(format, ...) \
  (G_UNLIKELY (gst_trace_format_is_enabled (format)) ? \
      gst_trace_span_begin ((format), \
          GST_TRACE_VALUES (__VA_ARGS__)) : \
      GST_TRACE_SPAN_ID_NONE)

/* *INDENT-OFF* */

/**
 * _GST_TRACE_SCOPE_VAR: (skip)
 *
 * Internal helper that derives the span-id variable name from a format getter,
 * so a begin/end macro pair can share it without the caller naming a variable.
 *
 * Since: 1.30
 */
#define _GST_TRACE_SCOPE_VAR(format) G_PASTE (_gst_trace_span_id_, format)

/**
 * GST_TRACE_SCOPE_BEGIN:
 * @format: a span format getter declared with GST_DEFINE_TRACE_FORMAT()
 * @...: one or more GST_TRACE_VALUE_* entries, in the field order declared
 *   on @format
 *
 * Opens a span and stores its #GstTraceSpanId in a local variable named after
 * @format, so the matching GST_TRACE_SCOPE_END() can close it without the
 * caller declaring a span-id variable. When no tracer is listening the values
 * are not evaluated and %GST_TRACE_SPAN_ID_NONE is stored.
 *
 * Calling GST_TRACE_SCOPE_BEGIN() twice for the same @format in the same scope
 * is not allowed, since the variable name is derived from @format.
 *
 * The span remains open until the matching GST_TRACE_SCOPE_END() is called
 *
 * Since: 1.30
 */
#define GST_TRACE_SCOPE_BEGIN(format, ...) \
  GstTraceSpanId _GST_TRACE_SCOPE_VAR (format) = \
      (G_UNLIKELY (gst_trace_format_is_enabled (format ())) ? \
          gst_trace_span_begin (format (), GST_TRACE_VALUES (__VA_ARGS__)) : \
          GST_TRACE_SPAN_ID_NONE)

/**
 * GST_TRACE_SCOPE_END:
 * @format: the span format getter passed to the matching
 *   GST_TRACE_SCOPE_BEGIN()
 *
 * Closes the span opened by GST_TRACE_SCOPE_BEGIN() for @format and resets the
 * variable to %GST_TRACE_SPAN_ID_NONE.
 *
 * Since: 1.30
 */
#define GST_TRACE_SCOPE_END(format) \
  gst_trace_span_end_and_clear (&_GST_TRACE_SCOPE_VAR (format))

/* *INDENT-ON* */

/**
 * _GST_TRACE_FUNC_HAS_CLEANUP: (skip)
 *
 * Internal gate for GST_TRACE_FUNC(). Defined when the compiler exposes both
 * `cleanup` and `unused` attributes via `__has_attribute()`.
 *
 * Since: 1.30
 */
#if defined(__has_attribute)
#  if __has_attribute(cleanup) && __has_attribute(unused)
#    define _GST_TRACE_FUNC_HAS_CLEANUP 1
#  endif
#endif

#ifdef _GST_TRACE_FUNC_HAS_CLEANUP
/* *INDENT-OFF* */

static inline void
_gst_trace_span_id_cleanup (GstTraceSpanId * id)
{
  gst_trace_span_end (*id);
}

/**
 * GST_TRACE_FUNC:
 * @format: a span format getter declared with GST_DEFINE_TRACE_FORMAT()
 * @...: one or more GST_TRACE_VALUE_* entries, in the field order declared
 *   on @format
 *
 * Opens a span like GST_TRACE_SCOPE_BEGIN() but closes it automatically when
 * the enclosing block exits via `return`, `goto`, or fall-through, so no
 * GST_TRACE_SCOPE_END() call is needed.
 *
 * Implemented with `__attribute__((cleanup))`. On compilers that do not
 * support the attribute (stock MSVC) this macro expands to nothing and its
 * arguments are not evaluated; use the GST_TRACE_SCOPE_BEGIN() /
 * GST_TRACE_SCOPE_END() pair there instead.
 *
 * Since: 1.30
 */
#define GST_TRACE_FUNC(format, ...) \
  __attribute__((cleanup (_gst_trace_span_id_cleanup), unused)) \
  GST_TRACE_SCOPE_BEGIN (format, __VA_ARGS__)

/* *INDENT-ON* */
#else

#define GST_TRACE_FUNC(format, ...) ((void) 0)

#endif /* _GST_TRACE_FUNC_HAS_CLEANUP */

GST_API
gboolean gst_tracer_class_uses_structure_params  (GstTracerClass *tracer_class);
GST_API
void gst_tracer_class_set_use_structure_params   (GstTracerClass *tracer_class,
                                                  gboolean use_structure_params);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTracer, gst_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTraceFormatBuilder, gst_trace_format_builder_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTraceField, gst_trace_field_free)

G_END_DECLS

#endif /* __GST_TRACER_H__ */
