
/* Generated data (by glib-mkenums) */

#include "gst_private.h"
#include <gst/gst.h>

/* enumerations from "gstobject.h" */
static void
register_gst_object_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_OBJECT_DISPOSING, "GST_OBJECT_DISPOSING", "disposing"},
    {GST_OBJECT_FLOATING, "GST_OBJECT_FLOATING", "floating"},
    {GST_OBJECT_FLAG_LAST, "GST_OBJECT_FLAG_LAST", "flag-last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstObjectFlags", values);
}

GType
gst_object_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_object_flags, &id);
  return id;
}

/* enumerations from "gstbin.h" */
static void
register_gst_bin_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_BIN_FLAG_LAST, "GST_BIN_FLAG_LAST", "last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstBinFlags", values);
}

GType
gst_bin_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_bin_flags, &id);
  return id;
}

/* enumerations from "gstbuffer.h" */
static void
register_gst_buffer_flag (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_BUFFER_FLAG_READONLY, "GST_BUFFER_FLAG_READONLY", "readonly"},
    {GST_BUFFER_FLAG_PREROLL, "GST_BUFFER_FLAG_PREROLL", "preroll"},
    {GST_BUFFER_FLAG_DISCONT, "GST_BUFFER_FLAG_DISCONT", "discont"},
    {GST_BUFFER_FLAG_IN_CAPS, "GST_BUFFER_FLAG_IN_CAPS", "in-caps"},
    {GST_BUFFER_FLAG_GAP, "GST_BUFFER_FLAG_GAP", "gap"},
    {GST_BUFFER_FLAG_DELTA_UNIT, "GST_BUFFER_FLAG_DELTA_UNIT", "delta-unit"},
    {GST_BUFFER_FLAG_LAST, "GST_BUFFER_FLAG_LAST", "last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstBufferFlag", values);
}

GType
gst_buffer_flag_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_buffer_flag, &id);
  return id;
}

/* enumerations from "gstbus.h" */
static void
register_gst_bus_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_BUS_FLUSHING, "GST_BUS_FLUSHING", "flushing"},
    {GST_BUS_FLAG_LAST, "GST_BUS_FLAG_LAST", "flag-last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstBusFlags", values);
}

GType
gst_bus_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_bus_flags, &id);
  return id;
}
static void
register_gst_bus_sync_reply (GType * id)
{
  static const GEnumValue values[] = {
    {GST_BUS_DROP, "GST_BUS_DROP", "drop"},
    {GST_BUS_PASS, "GST_BUS_PASS", "pass"},
    {GST_BUS_ASYNC, "GST_BUS_ASYNC", "async"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstBusSyncReply", values);
}

GType
gst_bus_sync_reply_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_bus_sync_reply, &id);
  return id;
}

/* enumerations from "gstcaps.h" */
static void
register_gst_caps_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_CAPS_FLAGS_ANY, "GST_CAPS_FLAGS_ANY", "any"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstCapsFlags", values);
}

GType
gst_caps_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_caps_flags, &id);
  return id;
}

/* enumerations from "gstclock.h" */
static void
register_gst_clock_return (GType * id)
{
  static const GEnumValue values[] = {
    {GST_CLOCK_OK, "GST_CLOCK_OK", "ok"},
    {GST_CLOCK_EARLY, "GST_CLOCK_EARLY", "early"},
    {GST_CLOCK_UNSCHEDULED, "GST_CLOCK_UNSCHEDULED", "unscheduled"},
    {GST_CLOCK_BUSY, "GST_CLOCK_BUSY", "busy"},
    {GST_CLOCK_BADTIME, "GST_CLOCK_BADTIME", "badtime"},
    {GST_CLOCK_ERROR, "GST_CLOCK_ERROR", "error"},
    {GST_CLOCK_UNSUPPORTED, "GST_CLOCK_UNSUPPORTED", "unsupported"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstClockReturn", values);
}

GType
gst_clock_return_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_clock_return, &id);
  return id;
}
static void
register_gst_clock_entry_type (GType * id)
{
  static const GEnumValue values[] = {
    {GST_CLOCK_ENTRY_SINGLE, "GST_CLOCK_ENTRY_SINGLE", "single"},
    {GST_CLOCK_ENTRY_PERIODIC, "GST_CLOCK_ENTRY_PERIODIC", "periodic"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstClockEntryType", values);
}

GType
gst_clock_entry_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_clock_entry_type, &id);
  return id;
}
static void
register_gst_clock_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC, "GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC",
          "can-do-single-sync"},
    {GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC, "GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC",
          "can-do-single-async"},
    {GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC, "GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC",
          "can-do-periodic-sync"},
    {GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC,
          "GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC", "can-do-periodic-async"},
    {GST_CLOCK_FLAG_CAN_SET_RESOLUTION, "GST_CLOCK_FLAG_CAN_SET_RESOLUTION",
          "can-set-resolution"},
    {GST_CLOCK_FLAG_CAN_SET_MASTER, "GST_CLOCK_FLAG_CAN_SET_MASTER",
          "can-set-master"},
    {GST_CLOCK_FLAG_LAST, "GST_CLOCK_FLAG_LAST", "last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstClockFlags", values);
}

GType
gst_clock_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_clock_flags, &id);
  return id;
}

/* enumerations from "gstelement.h" */
static void
register_gst_state (GType * id)
{
  static const GEnumValue values[] = {
    {GST_STATE_VOID_PENDING, "GST_STATE_VOID_PENDING", "void-pending"},
    {GST_STATE_NULL, "GST_STATE_NULL", "null"},
    {GST_STATE_READY, "GST_STATE_READY", "ready"},
    {GST_STATE_PAUSED, "GST_STATE_PAUSED", "paused"},
    {GST_STATE_PLAYING, "GST_STATE_PLAYING", "playing"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstState", values);
}

GType
gst_state_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_state, &id);
  return id;
}
static void
register_gst_state_change_return (GType * id)
{
  static const GEnumValue values[] = {
    {GST_STATE_CHANGE_FAILURE, "GST_STATE_CHANGE_FAILURE", "failure"},
    {GST_STATE_CHANGE_SUCCESS, "GST_STATE_CHANGE_SUCCESS", "success"},
    {GST_STATE_CHANGE_ASYNC, "GST_STATE_CHANGE_ASYNC", "async"},
    {GST_STATE_CHANGE_NO_PREROLL, "GST_STATE_CHANGE_NO_PREROLL", "no-preroll"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstStateChangeReturn", values);
}

GType
gst_state_change_return_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_state_change_return, &id);
  return id;
}
static void
register_gst_state_change (GType * id)
{
  static const GEnumValue values[] = {
    {GST_STATE_CHANGE_NULL_TO_READY, "GST_STATE_CHANGE_NULL_TO_READY",
          "null-to-ready"},
    {GST_STATE_CHANGE_READY_TO_PAUSED, "GST_STATE_CHANGE_READY_TO_PAUSED",
          "ready-to-paused"},
    {GST_STATE_CHANGE_PAUSED_TO_PLAYING, "GST_STATE_CHANGE_PAUSED_TO_PLAYING",
          "paused-to-playing"},
    {GST_STATE_CHANGE_PLAYING_TO_PAUSED, "GST_STATE_CHANGE_PLAYING_TO_PAUSED",
          "playing-to-paused"},
    {GST_STATE_CHANGE_PAUSED_TO_READY, "GST_STATE_CHANGE_PAUSED_TO_READY",
          "paused-to-ready"},
    {GST_STATE_CHANGE_READY_TO_NULL, "GST_STATE_CHANGE_READY_TO_NULL",
          "ready-to-null"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstStateChange", values);
}

GType
gst_state_change_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_state_change, &id);
  return id;
}
static void
register_gst_element_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_ELEMENT_LOCKED_STATE, "GST_ELEMENT_LOCKED_STATE", "locked-state"},
    {GST_ELEMENT_IS_SINK, "GST_ELEMENT_IS_SINK", "is-sink"},
    {GST_ELEMENT_UNPARENTING, "GST_ELEMENT_UNPARENTING", "unparenting"},
    {GST_ELEMENT_FLAG_LAST, "GST_ELEMENT_FLAG_LAST", "flag-last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstElementFlags", values);
}

GType
gst_element_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_element_flags, &id);
  return id;
}

/* enumerations from "gsterror.h" */
static void
register_gst_core_error (GType * id)
{
  static const GEnumValue values[] = {
    {GST_CORE_ERROR_FAILED, "GST_CORE_ERROR_FAILED", "failed"},
    {GST_CORE_ERROR_TOO_LAZY, "GST_CORE_ERROR_TOO_LAZY", "too-lazy"},
    {GST_CORE_ERROR_NOT_IMPLEMENTED, "GST_CORE_ERROR_NOT_IMPLEMENTED",
          "not-implemented"},
    {GST_CORE_ERROR_STATE_CHANGE, "GST_CORE_ERROR_STATE_CHANGE",
          "state-change"},
    {GST_CORE_ERROR_PAD, "GST_CORE_ERROR_PAD", "pad"},
    {GST_CORE_ERROR_THREAD, "GST_CORE_ERROR_THREAD", "thread"},
    {GST_CORE_ERROR_NEGOTIATION, "GST_CORE_ERROR_NEGOTIATION", "negotiation"},
    {GST_CORE_ERROR_EVENT, "GST_CORE_ERROR_EVENT", "event"},
    {GST_CORE_ERROR_SEEK, "GST_CORE_ERROR_SEEK", "seek"},
    {GST_CORE_ERROR_CAPS, "GST_CORE_ERROR_CAPS", "caps"},
    {GST_CORE_ERROR_TAG, "GST_CORE_ERROR_TAG", "tag"},
    {GST_CORE_ERROR_MISSING_PLUGIN, "GST_CORE_ERROR_MISSING_PLUGIN",
          "missing-plugin"},
    {GST_CORE_ERROR_CLOCK, "GST_CORE_ERROR_CLOCK", "clock"},
    {GST_CORE_ERROR_NUM_ERRORS, "GST_CORE_ERROR_NUM_ERRORS", "num-errors"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstCoreError", values);
}

GType
gst_core_error_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_core_error, &id);
  return id;
}
static void
register_gst_library_error (GType * id)
{
  static const GEnumValue values[] = {
    {GST_LIBRARY_ERROR_FAILED, "GST_LIBRARY_ERROR_FAILED", "failed"},
    {GST_LIBRARY_ERROR_TOO_LAZY, "GST_LIBRARY_ERROR_TOO_LAZY", "too-lazy"},
    {GST_LIBRARY_ERROR_INIT, "GST_LIBRARY_ERROR_INIT", "init"},
    {GST_LIBRARY_ERROR_SHUTDOWN, "GST_LIBRARY_ERROR_SHUTDOWN", "shutdown"},
    {GST_LIBRARY_ERROR_SETTINGS, "GST_LIBRARY_ERROR_SETTINGS", "settings"},
    {GST_LIBRARY_ERROR_ENCODE, "GST_LIBRARY_ERROR_ENCODE", "encode"},
    {GST_LIBRARY_ERROR_NUM_ERRORS, "GST_LIBRARY_ERROR_NUM_ERRORS",
          "num-errors"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstLibraryError", values);
}

GType
gst_library_error_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_library_error, &id);
  return id;
}
static void
register_gst_resource_error (GType * id)
{
  static const GEnumValue values[] = {
    {GST_RESOURCE_ERROR_FAILED, "GST_RESOURCE_ERROR_FAILED", "failed"},
    {GST_RESOURCE_ERROR_TOO_LAZY, "GST_RESOURCE_ERROR_TOO_LAZY", "too-lazy"},
    {GST_RESOURCE_ERROR_NOT_FOUND, "GST_RESOURCE_ERROR_NOT_FOUND", "not-found"},
    {GST_RESOURCE_ERROR_BUSY, "GST_RESOURCE_ERROR_BUSY", "busy"},
    {GST_RESOURCE_ERROR_OPEN_READ, "GST_RESOURCE_ERROR_OPEN_READ", "open-read"},
    {GST_RESOURCE_ERROR_OPEN_WRITE, "GST_RESOURCE_ERROR_OPEN_WRITE",
          "open-write"},
    {GST_RESOURCE_ERROR_OPEN_READ_WRITE, "GST_RESOURCE_ERROR_OPEN_READ_WRITE",
          "open-read-write"},
    {GST_RESOURCE_ERROR_CLOSE, "GST_RESOURCE_ERROR_CLOSE", "close"},
    {GST_RESOURCE_ERROR_READ, "GST_RESOURCE_ERROR_READ", "read"},
    {GST_RESOURCE_ERROR_WRITE, "GST_RESOURCE_ERROR_WRITE", "write"},
    {GST_RESOURCE_ERROR_SEEK, "GST_RESOURCE_ERROR_SEEK", "seek"},
    {GST_RESOURCE_ERROR_SYNC, "GST_RESOURCE_ERROR_SYNC", "sync"},
    {GST_RESOURCE_ERROR_SETTINGS, "GST_RESOURCE_ERROR_SETTINGS", "settings"},
    {GST_RESOURCE_ERROR_NUM_ERRORS, "GST_RESOURCE_ERROR_NUM_ERRORS",
          "num-errors"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstResourceError", values);
}

GType
gst_resource_error_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_resource_error, &id);
  return id;
}
static void
register_gst_stream_error (GType * id)
{
  static const GEnumValue values[] = {
    {GST_STREAM_ERROR_FAILED, "GST_STREAM_ERROR_FAILED", "failed"},
    {GST_STREAM_ERROR_TOO_LAZY, "GST_STREAM_ERROR_TOO_LAZY", "too-lazy"},
    {GST_STREAM_ERROR_NOT_IMPLEMENTED, "GST_STREAM_ERROR_NOT_IMPLEMENTED",
          "not-implemented"},
    {GST_STREAM_ERROR_TYPE_NOT_FOUND, "GST_STREAM_ERROR_TYPE_NOT_FOUND",
          "type-not-found"},
    {GST_STREAM_ERROR_WRONG_TYPE, "GST_STREAM_ERROR_WRONG_TYPE", "wrong-type"},
    {GST_STREAM_ERROR_CODEC_NOT_FOUND, "GST_STREAM_ERROR_CODEC_NOT_FOUND",
          "codec-not-found"},
    {GST_STREAM_ERROR_DECODE, "GST_STREAM_ERROR_DECODE", "decode"},
    {GST_STREAM_ERROR_ENCODE, "GST_STREAM_ERROR_ENCODE", "encode"},
    {GST_STREAM_ERROR_DEMUX, "GST_STREAM_ERROR_DEMUX", "demux"},
    {GST_STREAM_ERROR_MUX, "GST_STREAM_ERROR_MUX", "mux"},
    {GST_STREAM_ERROR_FORMAT, "GST_STREAM_ERROR_FORMAT", "format"},
    {GST_STREAM_ERROR_NUM_ERRORS, "GST_STREAM_ERROR_NUM_ERRORS", "num-errors"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstStreamError", values);
}

GType
gst_stream_error_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_stream_error, &id);
  return id;
}

/* enumerations from "gstevent.h" */
static void
register_gst_event_type_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_EVENT_TYPE_UPSTREAM, "GST_EVENT_TYPE_UPSTREAM", "upstream"},
    {GST_EVENT_TYPE_DOWNSTREAM, "GST_EVENT_TYPE_DOWNSTREAM", "downstream"},
    {GST_EVENT_TYPE_SERIALIZED, "GST_EVENT_TYPE_SERIALIZED", "serialized"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstEventTypeFlags", values);
}

GType
gst_event_type_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_event_type_flags, &id);
  return id;
}
static void
register_gst_event_type (GType * id)
{
  static const GEnumValue values[] = {
    {GST_EVENT_UNKNOWN, "GST_EVENT_UNKNOWN", "unknown"},
    {GST_EVENT_FLUSH_START, "GST_EVENT_FLUSH_START", "flush-start"},
    {GST_EVENT_FLUSH_STOP, "GST_EVENT_FLUSH_STOP", "flush-stop"},
    {GST_EVENT_EOS, "GST_EVENT_EOS", "eos"},
    {GST_EVENT_NEWSEGMENT, "GST_EVENT_NEWSEGMENT", "newsegment"},
    {GST_EVENT_TAG, "GST_EVENT_TAG", "tag"},
    {GST_EVENT_BUFFERSIZE, "GST_EVENT_BUFFERSIZE", "buffersize"},
    {GST_EVENT_QOS, "GST_EVENT_QOS", "qos"},
    {GST_EVENT_SEEK, "GST_EVENT_SEEK", "seek"},
    {GST_EVENT_NAVIGATION, "GST_EVENT_NAVIGATION", "navigation"},
    {GST_EVENT_CUSTOM_UPSTREAM, "GST_EVENT_CUSTOM_UPSTREAM", "custom-upstream"},
    {GST_EVENT_CUSTOM_DOWNSTREAM, "GST_EVENT_CUSTOM_DOWNSTREAM",
          "custom-downstream"},
    {GST_EVENT_CUSTOM_DOWNSTREAM_OOB, "GST_EVENT_CUSTOM_DOWNSTREAM_OOB",
          "custom-downstream-oob"},
    {GST_EVENT_CUSTOM_BOTH, "GST_EVENT_CUSTOM_BOTH", "custom-both"},
    {GST_EVENT_CUSTOM_BOTH_OOB, "GST_EVENT_CUSTOM_BOTH_OOB", "custom-both-oob"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstEventType", values);
}

GType
gst_event_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_event_type, &id);
  return id;
}
static void
register_gst_seek_type (GType * id)
{
  static const GEnumValue values[] = {
    {GST_SEEK_TYPE_NONE, "GST_SEEK_TYPE_NONE", "none"},
    {GST_SEEK_TYPE_CUR, "GST_SEEK_TYPE_CUR", "cur"},
    {GST_SEEK_TYPE_SET, "GST_SEEK_TYPE_SET", "set"},
    {GST_SEEK_TYPE_END, "GST_SEEK_TYPE_END", "end"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstSeekType", values);
}

GType
gst_seek_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_seek_type, &id);
  return id;
}
static void
register_gst_seek_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_SEEK_FLAG_NONE, "GST_SEEK_FLAG_NONE", "none"},
    {GST_SEEK_FLAG_FLUSH, "GST_SEEK_FLAG_FLUSH", "flush"},
    {GST_SEEK_FLAG_ACCURATE, "GST_SEEK_FLAG_ACCURATE", "accurate"},
    {GST_SEEK_FLAG_KEY_UNIT, "GST_SEEK_FLAG_KEY_UNIT", "key-unit"},
    {GST_SEEK_FLAG_SEGMENT, "GST_SEEK_FLAG_SEGMENT", "segment"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstSeekFlags", values);
}

GType
gst_seek_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_seek_flags, &id);
  return id;
}

/* enumerations from "gstformat.h" */
static void
register_gst_format (GType * id)
{
  static const GEnumValue values[] = {
    {GST_FORMAT_UNDEFINED, "GST_FORMAT_UNDEFINED", "undefined"},
    {GST_FORMAT_DEFAULT, "GST_FORMAT_DEFAULT", "default"},
    {GST_FORMAT_BYTES, "GST_FORMAT_BYTES", "bytes"},
    {GST_FORMAT_TIME, "GST_FORMAT_TIME", "time"},
    {GST_FORMAT_BUFFERS, "GST_FORMAT_BUFFERS", "buffers"},
    {GST_FORMAT_PERCENT, "GST_FORMAT_PERCENT", "percent"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstFormat", values);
}

GType
gst_format_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_format, &id);
  return id;
}

/* enumerations from "gstindex.h" */
static void
register_gst_index_certainty (GType * id)
{
  static const GEnumValue values[] = {
    {GST_INDEX_UNKNOWN, "GST_INDEX_UNKNOWN", "unknown"},
    {GST_INDEX_CERTAIN, "GST_INDEX_CERTAIN", "certain"},
    {GST_INDEX_FUZZY, "GST_INDEX_FUZZY", "fuzzy"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstIndexCertainty", values);
}

GType
gst_index_certainty_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_index_certainty, &id);
  return id;
}
static void
register_gst_index_entry_type (GType * id)
{
  static const GEnumValue values[] = {
    {GST_INDEX_ENTRY_ID, "GST_INDEX_ENTRY_ID", "id"},
    {GST_INDEX_ENTRY_ASSOCIATION, "GST_INDEX_ENTRY_ASSOCIATION", "association"},
    {GST_INDEX_ENTRY_OBJECT, "GST_INDEX_ENTRY_OBJECT", "object"},
    {GST_INDEX_ENTRY_FORMAT, "GST_INDEX_ENTRY_FORMAT", "format"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstIndexEntryType", values);
}

GType
gst_index_entry_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_index_entry_type, &id);
  return id;
}
static void
register_gst_index_lookup_method (GType * id)
{
  static const GEnumValue values[] = {
    {GST_INDEX_LOOKUP_EXACT, "GST_INDEX_LOOKUP_EXACT", "exact"},
    {GST_INDEX_LOOKUP_BEFORE, "GST_INDEX_LOOKUP_BEFORE", "before"},
    {GST_INDEX_LOOKUP_AFTER, "GST_INDEX_LOOKUP_AFTER", "after"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstIndexLookupMethod", values);
}

GType
gst_index_lookup_method_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_index_lookup_method, &id);
  return id;
}
static void
register_gst_assoc_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_ASSOCIATION_FLAG_NONE, "GST_ASSOCIATION_FLAG_NONE", "none"},
    {GST_ASSOCIATION_FLAG_KEY_UNIT, "GST_ASSOCIATION_FLAG_KEY_UNIT",
          "key-unit"},
    {GST_ASSOCIATION_FLAG_DELTA_UNIT, "GST_ASSOCIATION_FLAG_DELTA_UNIT",
          "delta-unit"},
    {GST_ASSOCIATION_FLAG_LAST, "GST_ASSOCIATION_FLAG_LAST", "last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstAssocFlags", values);
}

GType
gst_assoc_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_assoc_flags, &id);
  return id;
}
static void
register_gst_index_resolver_method (GType * id)
{
  static const GEnumValue values[] = {
    {GST_INDEX_RESOLVER_CUSTOM, "GST_INDEX_RESOLVER_CUSTOM", "custom"},
    {GST_INDEX_RESOLVER_GTYPE, "GST_INDEX_RESOLVER_GTYPE", "gtype"},
    {GST_INDEX_RESOLVER_PATH, "GST_INDEX_RESOLVER_PATH", "path"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstIndexResolverMethod", values);
}

GType
gst_index_resolver_method_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_index_resolver_method, &id);
  return id;
}
static void
register_gst_index_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_INDEX_WRITABLE, "GST_INDEX_WRITABLE", "writable"},
    {GST_INDEX_READABLE, "GST_INDEX_READABLE", "readable"},
    {GST_INDEX_FLAG_LAST, "GST_INDEX_FLAG_LAST", "flag-last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstIndexFlags", values);
}

GType
gst_index_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_index_flags, &id);
  return id;
}

/* enumerations from "gstinfo.h" */
static void
register_gst_debug_level (GType * id)
{
  static const GEnumValue values[] = {
    {GST_LEVEL_NONE, "GST_LEVEL_NONE", "none"},
    {GST_LEVEL_ERROR, "GST_LEVEL_ERROR", "error"},
    {GST_LEVEL_WARNING, "GST_LEVEL_WARNING", "warning"},
    {GST_LEVEL_INFO, "GST_LEVEL_INFO", "info"},
    {GST_LEVEL_DEBUG, "GST_LEVEL_DEBUG", "debug"},
    {GST_LEVEL_LOG, "GST_LEVEL_LOG", "log"},
    {GST_LEVEL_COUNT, "GST_LEVEL_COUNT", "count"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstDebugLevel", values);
}

GType
gst_debug_level_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_debug_level, &id);
  return id;
}
static void
register_gst_debug_color_flags (GType * id)
{
  static const GEnumValue values[] = {
    {GST_DEBUG_FG_BLACK, "GST_DEBUG_FG_BLACK", "fg-black"},
    {GST_DEBUG_FG_RED, "GST_DEBUG_FG_RED", "fg-red"},
    {GST_DEBUG_FG_GREEN, "GST_DEBUG_FG_GREEN", "fg-green"},
    {GST_DEBUG_FG_YELLOW, "GST_DEBUG_FG_YELLOW", "fg-yellow"},
    {GST_DEBUG_FG_BLUE, "GST_DEBUG_FG_BLUE", "fg-blue"},
    {GST_DEBUG_FG_MAGENTA, "GST_DEBUG_FG_MAGENTA", "fg-magenta"},
    {GST_DEBUG_FG_CYAN, "GST_DEBUG_FG_CYAN", "fg-cyan"},
    {GST_DEBUG_FG_WHITE, "GST_DEBUG_FG_WHITE", "fg-white"},
    {GST_DEBUG_BG_BLACK, "GST_DEBUG_BG_BLACK", "bg-black"},
    {GST_DEBUG_BG_RED, "GST_DEBUG_BG_RED", "bg-red"},
    {GST_DEBUG_BG_GREEN, "GST_DEBUG_BG_GREEN", "bg-green"},
    {GST_DEBUG_BG_YELLOW, "GST_DEBUG_BG_YELLOW", "bg-yellow"},
    {GST_DEBUG_BG_BLUE, "GST_DEBUG_BG_BLUE", "bg-blue"},
    {GST_DEBUG_BG_MAGENTA, "GST_DEBUG_BG_MAGENTA", "bg-magenta"},
    {GST_DEBUG_BG_CYAN, "GST_DEBUG_BG_CYAN", "bg-cyan"},
    {GST_DEBUG_BG_WHITE, "GST_DEBUG_BG_WHITE", "bg-white"},
    {GST_DEBUG_BOLD, "GST_DEBUG_BOLD", "bold"},
    {GST_DEBUG_UNDERLINE, "GST_DEBUG_UNDERLINE", "underline"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstDebugColorFlags", values);
}

GType
gst_debug_color_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_debug_color_flags, &id);
  return id;
}

/* enumerations from "gstiterator.h" */
static void
register_gst_iterator_result (GType * id)
{
  static const GEnumValue values[] = {
    {GST_ITERATOR_DONE, "GST_ITERATOR_DONE", "done"},
    {GST_ITERATOR_OK, "GST_ITERATOR_OK", "ok"},
    {GST_ITERATOR_RESYNC, "GST_ITERATOR_RESYNC", "resync"},
    {GST_ITERATOR_ERROR, "GST_ITERATOR_ERROR", "error"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstIteratorResult", values);
}

GType
gst_iterator_result_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_iterator_result, &id);
  return id;
}
static void
register_gst_iterator_item (GType * id)
{
  static const GEnumValue values[] = {
    {GST_ITERATOR_ITEM_SKIP, "GST_ITERATOR_ITEM_SKIP", "skip"},
    {GST_ITERATOR_ITEM_PASS, "GST_ITERATOR_ITEM_PASS", "pass"},
    {GST_ITERATOR_ITEM_END, "GST_ITERATOR_ITEM_END", "end"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstIteratorItem", values);
}

GType
gst_iterator_item_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_iterator_item, &id);
  return id;
}

/* enumerations from "gstmessage.h" */
static void
register_gst_message_type (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_MESSAGE_UNKNOWN, "GST_MESSAGE_UNKNOWN", "unknown"},
    {GST_MESSAGE_EOS, "GST_MESSAGE_EOS", "eos"},
    {GST_MESSAGE_ERROR, "GST_MESSAGE_ERROR", "error"},
    {GST_MESSAGE_WARNING, "GST_MESSAGE_WARNING", "warning"},
    {GST_MESSAGE_INFO, "GST_MESSAGE_INFO", "info"},
    {GST_MESSAGE_TAG, "GST_MESSAGE_TAG", "tag"},
    {GST_MESSAGE_BUFFERING, "GST_MESSAGE_BUFFERING", "buffering"},
    {GST_MESSAGE_STATE_CHANGED, "GST_MESSAGE_STATE_CHANGED", "state-changed"},
    {GST_MESSAGE_STATE_DIRTY, "GST_MESSAGE_STATE_DIRTY", "state-dirty"},
    {GST_MESSAGE_STEP_DONE, "GST_MESSAGE_STEP_DONE", "step-done"},
    {GST_MESSAGE_CLOCK_PROVIDE, "GST_MESSAGE_CLOCK_PROVIDE", "clock-provide"},
    {GST_MESSAGE_CLOCK_LOST, "GST_MESSAGE_CLOCK_LOST", "clock-lost"},
    {GST_MESSAGE_NEW_CLOCK, "GST_MESSAGE_NEW_CLOCK", "new-clock"},
    {GST_MESSAGE_STRUCTURE_CHANGE, "GST_MESSAGE_STRUCTURE_CHANGE",
          "structure-change"},
    {GST_MESSAGE_STREAM_STATUS, "GST_MESSAGE_STREAM_STATUS", "stream-status"},
    {GST_MESSAGE_APPLICATION, "GST_MESSAGE_APPLICATION", "application"},
    {GST_MESSAGE_ELEMENT, "GST_MESSAGE_ELEMENT", "element"},
    {GST_MESSAGE_SEGMENT_START, "GST_MESSAGE_SEGMENT_START", "segment-start"},
    {GST_MESSAGE_SEGMENT_DONE, "GST_MESSAGE_SEGMENT_DONE", "segment-done"},
    {GST_MESSAGE_DURATION, "GST_MESSAGE_DURATION", "duration"},
    {GST_MESSAGE_ANY, "GST_MESSAGE_ANY", "any"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstMessageType", values);
}

GType
gst_message_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_message_type, &id);
  return id;
}

/* enumerations from "gstminiobject.h" */
static void
register_gst_mini_object_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_MINI_OBJECT_FLAG_READONLY, "GST_MINI_OBJECT_FLAG_READONLY",
          "readonly"},
    {GST_MINI_OBJECT_FLAG_LAST, "GST_MINI_OBJECT_FLAG_LAST", "last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstMiniObjectFlags", values);
}

GType
gst_mini_object_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_mini_object_flags, &id);
  return id;
}

/* enumerations from "gstpad.h" */
static void
register_gst_pad_link_return (GType * id)
{
  static const GEnumValue values[] = {
    {GST_PAD_LINK_OK, "GST_PAD_LINK_OK", "ok"},
    {GST_PAD_LINK_WRONG_HIERARCHY, "GST_PAD_LINK_WRONG_HIERARCHY",
          "wrong-hierarchy"},
    {GST_PAD_LINK_WAS_LINKED, "GST_PAD_LINK_WAS_LINKED", "was-linked"},
    {GST_PAD_LINK_WRONG_DIRECTION, "GST_PAD_LINK_WRONG_DIRECTION",
          "wrong-direction"},
    {GST_PAD_LINK_NOFORMAT, "GST_PAD_LINK_NOFORMAT", "noformat"},
    {GST_PAD_LINK_NOSCHED, "GST_PAD_LINK_NOSCHED", "nosched"},
    {GST_PAD_LINK_REFUSED, "GST_PAD_LINK_REFUSED", "refused"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstPadLinkReturn", values);
}

GType
gst_pad_link_return_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_pad_link_return, &id);
  return id;
}
static void
register_gst_flow_return (GType * id)
{
  static const GEnumValue values[] = {
    {GST_FLOW_RESEND, "GST_FLOW_RESEND", "resend"},
    {GST_FLOW_OK, "GST_FLOW_OK", "ok"},
    {GST_FLOW_NOT_LINKED, "GST_FLOW_NOT_LINKED", "not-linked"},
    {GST_FLOW_WRONG_STATE, "GST_FLOW_WRONG_STATE", "wrong-state"},
    {GST_FLOW_UNEXPECTED, "GST_FLOW_UNEXPECTED", "unexpected"},
    {GST_FLOW_NOT_NEGOTIATED, "GST_FLOW_NOT_NEGOTIATED", "not-negotiated"},
    {GST_FLOW_ERROR, "GST_FLOW_ERROR", "error"},
    {GST_FLOW_NOT_SUPPORTED, "GST_FLOW_NOT_SUPPORTED", "not-supported"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstFlowReturn", values);
}

GType
gst_flow_return_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_flow_return, &id);
  return id;
}
static void
register_gst_activate_mode (GType * id)
{
  static const GEnumValue values[] = {
    {GST_ACTIVATE_NONE, "GST_ACTIVATE_NONE", "none"},
    {GST_ACTIVATE_PUSH, "GST_ACTIVATE_PUSH", "push"},
    {GST_ACTIVATE_PULL, "GST_ACTIVATE_PULL", "pull"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstActivateMode", values);
}

GType
gst_activate_mode_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_activate_mode, &id);
  return id;
}
static void
register_gst_pad_direction (GType * id)
{
  static const GEnumValue values[] = {
    {GST_PAD_UNKNOWN, "GST_PAD_UNKNOWN", "unknown"},
    {GST_PAD_SRC, "GST_PAD_SRC", "src"},
    {GST_PAD_SINK, "GST_PAD_SINK", "sink"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstPadDirection", values);
}

GType
gst_pad_direction_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_pad_direction, &id);
  return id;
}
static void
register_gst_pad_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_PAD_BLOCKED, "GST_PAD_BLOCKED", "blocked"},
    {GST_PAD_FLUSHING, "GST_PAD_FLUSHING", "flushing"},
    {GST_PAD_IN_GETCAPS, "GST_PAD_IN_GETCAPS", "in-getcaps"},
    {GST_PAD_IN_SETCAPS, "GST_PAD_IN_SETCAPS", "in-setcaps"},
    {GST_PAD_FLAG_LAST, "GST_PAD_FLAG_LAST", "flag-last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstPadFlags", values);
}

GType
gst_pad_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_pad_flags, &id);
  return id;
}

/* enumerations from "gstpadtemplate.h" */
static void
register_gst_pad_presence (GType * id)
{
  static const GEnumValue values[] = {
    {GST_PAD_ALWAYS, "GST_PAD_ALWAYS", "always"},
    {GST_PAD_SOMETIMES, "GST_PAD_SOMETIMES", "sometimes"},
    {GST_PAD_REQUEST, "GST_PAD_REQUEST", "request"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstPadPresence", values);
}

GType
gst_pad_presence_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_pad_presence, &id);
  return id;
}
static void
register_gst_pad_template_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_PAD_TEMPLATE_FIXED, "GST_PAD_TEMPLATE_FIXED", "fixed"},
    {GST_PAD_TEMPLATE_FLAG_LAST, "GST_PAD_TEMPLATE_FLAG_LAST", "flag-last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstPadTemplateFlags", values);
}

GType
gst_pad_template_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_pad_template_flags, &id);
  return id;
}

/* enumerations from "gstpipeline.h" */
static void
register_gst_pipeline_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_PIPELINE_FLAG_FIXED_CLOCK, "GST_PIPELINE_FLAG_FIXED_CLOCK",
          "fixed-clock"},
    {GST_PIPELINE_FLAG_LAST, "GST_PIPELINE_FLAG_LAST", "last"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstPipelineFlags", values);
}

GType
gst_pipeline_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_pipeline_flags, &id);
  return id;
}

/* enumerations from "gstplugin.h" */
static void
register_gst_plugin_error (GType * id)
{
  static const GEnumValue values[] = {
    {GST_PLUGIN_ERROR_MODULE, "GST_PLUGIN_ERROR_MODULE", "module"},
    {GST_PLUGIN_ERROR_DEPENDENCIES, "GST_PLUGIN_ERROR_DEPENDENCIES",
          "dependencies"},
    {GST_PLUGIN_ERROR_NAME_MISMATCH, "GST_PLUGIN_ERROR_NAME_MISMATCH",
          "name-mismatch"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstPluginError", values);
}

GType
gst_plugin_error_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_plugin_error, &id);
  return id;
}
static void
register_gst_plugin_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_PLUGIN_FLAG_CACHED, "GST_PLUGIN_FLAG_CACHED", "cached"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstPluginFlags", values);
}

GType
gst_plugin_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_plugin_flags, &id);
  return id;
}

/* enumerations from "gstpluginfeature.h" */
static void
register_gst_rank (GType * id)
{
  static const GEnumValue values[] = {
    {GST_RANK_NONE, "GST_RANK_NONE", "none"},
    {GST_RANK_MARGINAL, "GST_RANK_MARGINAL", "marginal"},
    {GST_RANK_SECONDARY, "GST_RANK_SECONDARY", "secondary"},
    {GST_RANK_PRIMARY, "GST_RANK_PRIMARY", "primary"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstRank", values);
}

GType
gst_rank_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_rank, &id);
  return id;
}

/* enumerations from "gstquery.h" */
static void
register_gst_query_type (GType * id)
{
  static const GEnumValue values[] = {
    {GST_QUERY_NONE, "GST_QUERY_NONE", "none"},
    {GST_QUERY_POSITION, "GST_QUERY_POSITION", "position"},
    {GST_QUERY_DURATION, "GST_QUERY_DURATION", "duration"},
    {GST_QUERY_LATENCY, "GST_QUERY_LATENCY", "latency"},
    {GST_QUERY_JITTER, "GST_QUERY_JITTER", "jitter"},
    {GST_QUERY_RATE, "GST_QUERY_RATE", "rate"},
    {GST_QUERY_SEEKING, "GST_QUERY_SEEKING", "seeking"},
    {GST_QUERY_SEGMENT, "GST_QUERY_SEGMENT", "segment"},
    {GST_QUERY_CONVERT, "GST_QUERY_CONVERT", "convert"},
    {GST_QUERY_FORMATS, "GST_QUERY_FORMATS", "formats"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstQueryType", values);
}

GType
gst_query_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_query_type, &id);
  return id;
}

/* enumerations from "gsttaglist.h" */
static void
register_gst_tag_merge_mode (GType * id)
{
  static const GEnumValue values[] = {
    {GST_TAG_MERGE_UNDEFINED, "GST_TAG_MERGE_UNDEFINED", "undefined"},
    {GST_TAG_MERGE_REPLACE_ALL, "GST_TAG_MERGE_REPLACE_ALL", "replace-all"},
    {GST_TAG_MERGE_REPLACE, "GST_TAG_MERGE_REPLACE", "replace"},
    {GST_TAG_MERGE_APPEND, "GST_TAG_MERGE_APPEND", "append"},
    {GST_TAG_MERGE_PREPEND, "GST_TAG_MERGE_PREPEND", "prepend"},
    {GST_TAG_MERGE_KEEP, "GST_TAG_MERGE_KEEP", "keep"},
    {GST_TAG_MERGE_KEEP_ALL, "GST_TAG_MERGE_KEEP_ALL", "keep-all"},
    {GST_TAG_MERGE_COUNT, "GST_TAG_MERGE_COUNT", "count"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstTagMergeMode", values);
}

GType
gst_tag_merge_mode_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_tag_merge_mode, &id);
  return id;
}
static void
register_gst_tag_flag (GType * id)
{
  static const GEnumValue values[] = {
    {GST_TAG_FLAG_UNDEFINED, "GST_TAG_FLAG_UNDEFINED", "undefined"},
    {GST_TAG_FLAG_META, "GST_TAG_FLAG_META", "meta"},
    {GST_TAG_FLAG_ENCODED, "GST_TAG_FLAG_ENCODED", "encoded"},
    {GST_TAG_FLAG_DECODED, "GST_TAG_FLAG_DECODED", "decoded"},
    {GST_TAG_FLAG_COUNT, "GST_TAG_FLAG_COUNT", "count"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstTagFlag", values);
}

GType
gst_tag_flag_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_tag_flag, &id);
  return id;
}

/* enumerations from "gsttask.h" */
static void
register_gst_task_state (GType * id)
{
  static const GEnumValue values[] = {
    {GST_TASK_STARTED, "GST_TASK_STARTED", "started"},
    {GST_TASK_STOPPED, "GST_TASK_STOPPED", "stopped"},
    {GST_TASK_PAUSED, "GST_TASK_PAUSED", "paused"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstTaskState", values);
}

GType
gst_task_state_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_task_state, &id);
  return id;
}

/* enumerations from "gsttrace.h" */
static void
register_gst_alloc_trace_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {GST_ALLOC_TRACE_LIVE, "GST_ALLOC_TRACE_LIVE", "live"},
    {GST_ALLOC_TRACE_MEM_LIVE, "GST_ALLOC_TRACE_MEM_LIVE", "mem-live"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstAllocTraceFlags", values);
}

GType
gst_alloc_trace_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_alloc_trace_flags, &id);
  return id;
}

/* enumerations from "gsttypefind.h" */
static void
register_gst_type_find_probability (GType * id)
{
  static const GEnumValue values[] = {
    {GST_TYPE_FIND_MINIMUM, "GST_TYPE_FIND_MINIMUM", "minimum"},
    {GST_TYPE_FIND_POSSIBLE, "GST_TYPE_FIND_POSSIBLE", "possible"},
    {GST_TYPE_FIND_LIKELY, "GST_TYPE_FIND_LIKELY", "likely"},
    {GST_TYPE_FIND_NEARLY_CERTAIN, "GST_TYPE_FIND_NEARLY_CERTAIN",
          "nearly-certain"},
    {GST_TYPE_FIND_MAXIMUM, "GST_TYPE_FIND_MAXIMUM", "maximum"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstTypeFindProbability", values);
}

GType
gst_type_find_probability_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_type_find_probability, &id);
  return id;
}

/* enumerations from "gsturi.h" */
static void
register_gst_uri_type (GType * id)
{
  static const GEnumValue values[] = {
    {GST_URI_UNKNOWN, "GST_URI_UNKNOWN", "unknown"},
    {GST_URI_SINK, "GST_URI_SINK", "sink"},
    {GST_URI_SRC, "GST_URI_SRC", "src"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstURIType", values);
}

GType
gst_uri_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_uri_type, &id);
  return id;
}

/* enumerations from "gstparse.h" */
static void
register_gst_parse_error (GType * id)
{
  static const GEnumValue values[] = {
    {GST_PARSE_ERROR_SYNTAX, "GST_PARSE_ERROR_SYNTAX", "syntax"},
    {GST_PARSE_ERROR_NO_SUCH_ELEMENT, "GST_PARSE_ERROR_NO_SUCH_ELEMENT",
          "no-such-element"},
    {GST_PARSE_ERROR_NO_SUCH_PROPERTY, "GST_PARSE_ERROR_NO_SUCH_PROPERTY",
          "no-such-property"},
    {GST_PARSE_ERROR_LINK, "GST_PARSE_ERROR_LINK", "link"},
    {GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY,
          "GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY", "could-not-set-property"},
    {GST_PARSE_ERROR_EMPTY_BIN, "GST_PARSE_ERROR_EMPTY_BIN", "empty-bin"},
    {GST_PARSE_ERROR_EMPTY, "GST_PARSE_ERROR_EMPTY", "empty"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstParseError", values);
}

GType
gst_parse_error_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_parse_error, &id);
  return id;
}

/* Generated data ends here */
