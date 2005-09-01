
/* Generated data (by glib-mkenums) */

#include "gst_private.h"
#include <gst/gst.h>

/* enumerations from "gstobject.h" */
GType
gst_object_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_OBJECT_DISPOSING, "GST_OBJECT_DISPOSING", "disposing"},
      {GST_OBJECT_DESTROYED, "GST_OBJECT_DESTROYED", "destroyed"},
      {GST_OBJECT_FLOATING, "GST_OBJECT_FLOATING", "floating"},
      {GST_OBJECT_FLAG_LAST, "GST_OBJECT_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstObjectFlags", values);
  }
  return etype;
}

/* enumerations from "gstbin.h" */
GType
gst_bin_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_BIN_FLAG_LAST, "GST_BIN_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstBinFlags", values);
  }
  return etype;
}

/* enumerations from "gstbuffer.h" */
GType
gst_buffer_flag_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_BUFFER_FLAG_READONLY, "GST_BUFFER_FLAG_READONLY", "readonly"},
      {GST_BUFFER_FLAG_ORIGINAL, "GST_BUFFER_FLAG_ORIGINAL", "original"},
      {GST_BUFFER_FLAG_PREROLL, "GST_BUFFER_FLAG_PREROLL", "preroll"},
      {GST_BUFFER_FLAG_DISCONT, "GST_BUFFER_FLAG_DISCONT", "discont"},
      {GST_BUFFER_FLAG_IN_CAPS, "GST_BUFFER_FLAG_IN_CAPS", "in-caps"},
      {GST_BUFFER_FLAG_GAP, "GST_BUFFER_FLAG_GAP", "gap"},
      {GST_BUFFER_FLAG_DELTA_UNIT, "GST_BUFFER_FLAG_DELTA_UNIT", "delta-unit"},
      {GST_BUFFER_FLAG_LAST, "GST_BUFFER_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstBufferFlag", values);
  }
  return etype;
}

/* enumerations from "gstbus.h" */
GType
gst_bus_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_BUS_FLUSHING, "GST_BUS_FLUSHING", "flushing"},
      {GST_BUS_FLAG_LAST, "GST_BUS_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstBusFlags", values);
  }
  return etype;
}

GType
gst_bus_sync_reply_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_BUS_DROP, "GST_BUS_DROP", "drop"},
      {GST_BUS_PASS, "GST_BUS_PASS", "pass"},
      {GST_BUS_ASYNC, "GST_BUS_ASYNC", "async"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstBusSyncReply", values);
  }
  return etype;
}

/* enumerations from "gstclock.h" */
GType
gst_clock_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstClockReturn", values);
  }
  return etype;
}

GType
gst_clock_entry_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_CLOCK_ENTRY_SINGLE, "GST_CLOCK_ENTRY_SINGLE", "single"},
      {GST_CLOCK_ENTRY_PERIODIC, "GST_CLOCK_ENTRY_PERIODIC", "periodic"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstClockEntryType", values);
  }
  return etype;
}

GType
gst_clock_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC, "GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC",
            "do-single-sync"},
      {GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC, "GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC",
            "do-single-async"},
      {GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC,
            "GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC", "do-periodic-sync"},
      {GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC,
            "GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC", "do-periodic-async"},
      {GST_CLOCK_FLAG_CAN_SET_RESOLUTION, "GST_CLOCK_FLAG_CAN_SET_RESOLUTION",
            "set-resolution"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstClockFlags", values);
  }
  return etype;
}

/* enumerations from "gstelement.h" */
GType
gst_element_state_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_STATE_VOID_PENDING, "GST_STATE_VOID_PENDING", "state-void-pending"},
      {GST_STATE_NULL, "GST_STATE_NULL", "state-null"},
      {GST_STATE_READY, "GST_STATE_READY", "state-ready"},
      {GST_STATE_PAUSED, "GST_STATE_PAUSED", "state-paused"},
      {GST_STATE_PLAYING, "GST_STATE_PLAYING", "state-playing"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstElementState", values);
  }
  return etype;
}

GType
gst_element_state_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_STATE_FAILURE, "GST_STATE_FAILURE", "failure"},
      {GST_STATE_SUCCESS, "GST_STATE_SUCCESS", "success"},
      {GST_STATE_ASYNC, "GST_STATE_ASYNC", "async"},
      {GST_STATE_NO_PREROLL, "GST_STATE_NO_PREROLL", "no-preroll"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstElementStateReturn", values);
  }
  return etype;
}

GType
gst_element_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_ELEMENT_LOCKED_STATE, "GST_ELEMENT_LOCKED_STATE", "locked-state"},
      {GST_ELEMENT_IS_SINK, "GST_ELEMENT_IS_SINK", "is-sink"},
      {GST_ELEMENT_UNPARENTING, "GST_ELEMENT_UNPARENTING", "unparenting"},
      {GST_ELEMENT_FLAG_LAST, "GST_ELEMENT_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstElementFlags", values);
  }
  return etype;
}

/* enumerations from "gsterror.h" */
GType
gst_core_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
      {GST_CORE_ERROR_NUM_ERRORS, "GST_CORE_ERROR_NUM_ERRORS", "num-errors"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstCoreError", values);
  }
  return etype;
}

GType
gst_library_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstLibraryError", values);
  }
  return etype;
}

GType
gst_resource_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_RESOURCE_ERROR_FAILED, "GST_RESOURCE_ERROR_FAILED", "failed"},
      {GST_RESOURCE_ERROR_TOO_LAZY, "GST_RESOURCE_ERROR_TOO_LAZY", "too-lazy"},
      {GST_RESOURCE_ERROR_NOT_FOUND, "GST_RESOURCE_ERROR_NOT_FOUND",
            "not-found"},
      {GST_RESOURCE_ERROR_BUSY, "GST_RESOURCE_ERROR_BUSY", "busy"},
      {GST_RESOURCE_ERROR_OPEN_READ, "GST_RESOURCE_ERROR_OPEN_READ",
            "open-read"},
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
    etype = g_enum_register_static ("GstResourceError", values);
  }
  return etype;
}

GType
gst_stream_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_STREAM_ERROR_FAILED, "GST_STREAM_ERROR_FAILED", "failed"},
      {GST_STREAM_ERROR_TOO_LAZY, "GST_STREAM_ERROR_TOO_LAZY", "too-lazy"},
      {GST_STREAM_ERROR_NOT_IMPLEMENTED, "GST_STREAM_ERROR_NOT_IMPLEMENTED",
            "not-implemented"},
      {GST_STREAM_ERROR_TYPE_NOT_FOUND, "GST_STREAM_ERROR_TYPE_NOT_FOUND",
            "type-not-found"},
      {GST_STREAM_ERROR_WRONG_TYPE, "GST_STREAM_ERROR_WRONG_TYPE",
            "wrong-type"},
      {GST_STREAM_ERROR_CODEC_NOT_FOUND, "GST_STREAM_ERROR_CODEC_NOT_FOUND",
            "codec-not-found"},
      {GST_STREAM_ERROR_DECODE, "GST_STREAM_ERROR_DECODE", "decode"},
      {GST_STREAM_ERROR_ENCODE, "GST_STREAM_ERROR_ENCODE", "encode"},
      {GST_STREAM_ERROR_DEMUX, "GST_STREAM_ERROR_DEMUX", "demux"},
      {GST_STREAM_ERROR_MUX, "GST_STREAM_ERROR_MUX", "mux"},
      {GST_STREAM_ERROR_FORMAT, "GST_STREAM_ERROR_FORMAT", "format"},
      {GST_STREAM_ERROR_STOPPED, "GST_STREAM_ERROR_STOPPED", "stopped"},
      {GST_STREAM_ERROR_NUM_ERRORS, "GST_STREAM_ERROR_NUM_ERRORS",
            "num-errors"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstStreamError", values);
  }
  return etype;
}

/* enumerations from "gstevent.h" */
GType
gst_event_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_EVENT_UNKNOWN, "GST_EVENT_UNKNOWN", "unknown"},
      {GST_EVENT_FLUSH_START, "GST_EVENT_FLUSH_START", "flush-start"},
      {GST_EVENT_FLUSH_STOP, "GST_EVENT_FLUSH_STOP", "flush-stop"},
      {GST_EVENT_EOS, "GST_EVENT_EOS", "eos"},
      {GST_EVENT_NEWSEGMENT, "GST_EVENT_NEWSEGMENT", "newsegment"},
      {GST_EVENT_TAG, "GST_EVENT_TAG", "tag"},
      {GST_EVENT_FILLER, "GST_EVENT_FILLER", "filler"},
      {GST_EVENT_QOS, "GST_EVENT_QOS", "qos"},
      {GST_EVENT_SEEK, "GST_EVENT_SEEK", "seek"},
      {GST_EVENT_NAVIGATION, "GST_EVENT_NAVIGATION", "navigation"},
      {GST_EVENT_CUSTOM_UP, "GST_EVENT_CUSTOM_UP", "custom-up"},
      {GST_EVENT_CUSTOM_DS, "GST_EVENT_CUSTOM_DS", "custom-ds"},
      {GST_EVENT_CUSTOM_DS_OOB, "GST_EVENT_CUSTOM_DS_OOB", "custom-ds-oob"},
      {GST_EVENT_CUSTOM_BOTH, "GST_EVENT_CUSTOM_BOTH", "custom-both"},
      {GST_EVENT_CUSTOM_BOTH_OOB, "GST_EVENT_CUSTOM_BOTH_OOB",
            "custom-both-oob"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstEventType", values);
  }
  return etype;
}

GType
gst_seek_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_SEEK_TYPE_NONE, "GST_SEEK_TYPE_NONE", "none"},
      {GST_SEEK_TYPE_CUR, "GST_SEEK_TYPE_CUR", "cur"},
      {GST_SEEK_TYPE_SET, "GST_SEEK_TYPE_SET", "set"},
      {GST_SEEK_TYPE_END, "GST_SEEK_TYPE_END", "end"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstSeekType", values);
  }
  return etype;
}

GType
gst_seek_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_SEEK_FLAG_NONE, "GST_SEEK_FLAG_NONE", "none"},
      {GST_SEEK_FLAG_FLUSH, "GST_SEEK_FLAG_FLUSH", "flush"},
      {GST_SEEK_FLAG_ACCURATE, "GST_SEEK_FLAG_ACCURATE", "accurate"},
      {GST_SEEK_FLAG_KEY_UNIT, "GST_SEEK_FLAG_KEY_UNIT", "key-unit"},
      {GST_SEEK_FLAG_SEGMENT, "GST_SEEK_FLAG_SEGMENT", "segment"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstSeekFlags", values);
  }
  return etype;
}

/* enumerations from "gstformat.h" */
GType
gst_format_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_FORMAT_UNDEFINED, "GST_FORMAT_UNDEFINED", "undefined"},
      {GST_FORMAT_DEFAULT, "GST_FORMAT_DEFAULT", "default"},
      {GST_FORMAT_BYTES, "GST_FORMAT_BYTES", "bytes"},
      {GST_FORMAT_TIME, "GST_FORMAT_TIME", "time"},
      {GST_FORMAT_BUFFERS, "GST_FORMAT_BUFFERS", "buffers"},
      {GST_FORMAT_PERCENT, "GST_FORMAT_PERCENT", "percent"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstFormat", values);
  }
  return etype;
}

/* enumerations from "gstindex.h" */
GType
gst_index_certainty_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_INDEX_UNKNOWN, "GST_INDEX_UNKNOWN", "unknown"},
      {GST_INDEX_CERTAIN, "GST_INDEX_CERTAIN", "certain"},
      {GST_INDEX_FUZZY, "GST_INDEX_FUZZY", "fuzzy"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIndexCertainty", values);
  }
  return etype;
}

GType
gst_index_entry_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_INDEX_ENTRY_ID, "GST_INDEX_ENTRY_ID", "id"},
      {GST_INDEX_ENTRY_ASSOCIATION, "GST_INDEX_ENTRY_ASSOCIATION",
            "association"},
      {GST_INDEX_ENTRY_OBJECT, "GST_INDEX_ENTRY_OBJECT", "object"},
      {GST_INDEX_ENTRY_FORMAT, "GST_INDEX_ENTRY_FORMAT", "format"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIndexEntryType", values);
  }
  return etype;
}

GType
gst_index_lookup_method_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_INDEX_LOOKUP_EXACT, "GST_INDEX_LOOKUP_EXACT", "exact"},
      {GST_INDEX_LOOKUP_BEFORE, "GST_INDEX_LOOKUP_BEFORE", "before"},
      {GST_INDEX_LOOKUP_AFTER, "GST_INDEX_LOOKUP_AFTER", "after"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIndexLookupMethod", values);
  }
  return etype;
}

GType
gst_assoc_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_ASSOCIATION_FLAG_NONE, "GST_ASSOCIATION_FLAG_NONE", "none"},
      {GST_ASSOCIATION_FLAG_KEY_UNIT, "GST_ASSOCIATION_FLAG_KEY_UNIT",
            "key-unit"},
      {GST_ASSOCIATION_FLAG_DELTA_UNIT, "GST_ASSOCIATION_FLAG_DELTA_UNIT",
            "delta-unit"},
      {GST_ASSOCIATION_FLAG_LAST, "GST_ASSOCIATION_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstAssocFlags", values);
  }
  return etype;
}

GType
gst_index_resolver_method_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_INDEX_RESOLVER_CUSTOM, "GST_INDEX_RESOLVER_CUSTOM", "custom"},
      {GST_INDEX_RESOLVER_GTYPE, "GST_INDEX_RESOLVER_GTYPE", "gtype"},
      {GST_INDEX_RESOLVER_PATH, "GST_INDEX_RESOLVER_PATH", "path"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIndexResolverMethod", values);
  }
  return etype;
}

GType
gst_index_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_INDEX_WRITABLE, "GST_INDEX_WRITABLE", "writable"},
      {GST_INDEX_READABLE, "GST_INDEX_READABLE", "readable"},
      {GST_INDEX_FLAG_LAST, "GST_INDEX_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIndexFlags", values);
  }
  return etype;
}

/* enumerations from "gstinfo.h" */
GType
gst_debug_level_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstDebugLevel", values);
  }
  return etype;
}

GType
gst_debug_color_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstDebugColorFlags", values);
  }
  return etype;
}

/* enumerations from "gstiterator.h" */
GType
gst_iterator_result_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_ITERATOR_DONE, "GST_ITERATOR_DONE", "done"},
      {GST_ITERATOR_OK, "GST_ITERATOR_OK", "ok"},
      {GST_ITERATOR_RESYNC, "GST_ITERATOR_RESYNC", "resync"},
      {GST_ITERATOR_ERROR, "GST_ITERATOR_ERROR", "error"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIteratorResult", values);
  }
  return etype;
}

GType
gst_iterator_item_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_ITERATOR_ITEM_SKIP, "GST_ITERATOR_ITEM_SKIP", "skip"},
      {GST_ITERATOR_ITEM_PASS, "GST_ITERATOR_ITEM_PASS", "pass"},
      {GST_ITERATOR_ITEM_END, "GST_ITERATOR_ITEM_END", "end"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstIteratorItem", values);
  }
  return etype;
}

/* enumerations from "gstmessage.h" */
GType
gst_message_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_MESSAGE_UNKNOWN, "GST_MESSAGE_UNKNOWN", "unknown"},
      {GST_MESSAGE_EOS, "GST_MESSAGE_EOS", "eos"},
      {GST_MESSAGE_ERROR, "GST_MESSAGE_ERROR", "error"},
      {GST_MESSAGE_WARNING, "GST_MESSAGE_WARNING", "warning"},
      {GST_MESSAGE_INFO, "GST_MESSAGE_INFO", "info"},
      {GST_MESSAGE_TAG, "GST_MESSAGE_TAG", "tag"},
      {GST_MESSAGE_BUFFERING, "GST_MESSAGE_BUFFERING", "buffering"},
      {GST_MESSAGE_STATE_CHANGED, "GST_MESSAGE_STATE_CHANGED", "state-changed"},
      {GST_MESSAGE_STEP_DONE, "GST_MESSAGE_STEP_DONE", "step-done"},
      {GST_MESSAGE_NEW_CLOCK, "GST_MESSAGE_NEW_CLOCK", "new-clock"},
      {GST_MESSAGE_STRUCTURE_CHANGE, "GST_MESSAGE_STRUCTURE_CHANGE",
            "structure-change"},
      {GST_MESSAGE_STREAM_STATUS, "GST_MESSAGE_STREAM_STATUS", "stream-status"},
      {GST_MESSAGE_APPLICATION, "GST_MESSAGE_APPLICATION", "application"},
      {GST_MESSAGE_SEGMENT_START, "GST_MESSAGE_SEGMENT_START", "segment-start"},
      {GST_MESSAGE_SEGMENT_DONE, "GST_MESSAGE_SEGMENT_DONE", "segment-done"},
      {GST_MESSAGE_ANY, "GST_MESSAGE_ANY", "any"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstMessageType", values);
  }
  return etype;
}

/* enumerations from "gstminiobject.h" */
GType
gst_mini_object_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_MINI_OBJECT_FLAG_READONLY, "GST_MINI_OBJECT_FLAG_READONLY",
            "readonly"},
      {GST_MINI_OBJECT_FLAG_STATIC, "GST_MINI_OBJECT_FLAG_STATIC", "static"},
      {GST_MINI_OBJECT_FLAG_LAST, "GST_MINI_OBJECT_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstMiniObjectFlags", values);
  }
  return etype;
}

/* enumerations from "gstpad.h" */
GType
gst_pad_link_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstPadLinkReturn", values);
  }
  return etype;
}

GType
gst_flow_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstFlowReturn", values);
  }
  return etype;
}

GType
gst_activate_mode_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_ACTIVATE_NONE, "GST_ACTIVATE_NONE", "none"},
      {GST_ACTIVATE_PUSH, "GST_ACTIVATE_PUSH", "push"},
      {GST_ACTIVATE_PULL, "GST_ACTIVATE_PULL", "pull"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstActivateMode", values);
  }
  return etype;
}

GType
gst_pad_direction_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PAD_UNKNOWN, "GST_PAD_UNKNOWN", "unknown"},
      {GST_PAD_SRC, "GST_PAD_SRC", "src"},
      {GST_PAD_SINK, "GST_PAD_SINK", "sink"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPadDirection", values);
  }
  return etype;
}

GType
gst_pad_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PAD_BLOCKED, "GST_PAD_BLOCKED", "blocked"},
      {GST_PAD_FLUSHING, "GST_PAD_FLUSHING", "flushing"},
      {GST_PAD_IN_GETCAPS, "GST_PAD_IN_GETCAPS", "in-getcaps"},
      {GST_PAD_IN_SETCAPS, "GST_PAD_IN_SETCAPS", "in-setcaps"},
      {GST_PAD_FLAG_LAST, "GST_PAD_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPadFlags", values);
  }
  return etype;
}

GType
gst_pad_presence_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PAD_ALWAYS, "GST_PAD_ALWAYS", "always"},
      {GST_PAD_SOMETIMES, "GST_PAD_SOMETIMES", "sometimes"},
      {GST_PAD_REQUEST, "GST_PAD_REQUEST", "request"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPadPresence", values);
  }
  return etype;
}

GType
gst_pad_template_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PAD_TEMPLATE_FIXED, "GST_PAD_TEMPLATE_FIXED", "fixed"},
      {GST_PAD_TEMPLATE_FLAG_LAST, "GST_PAD_TEMPLATE_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPadTemplateFlags", values);
  }
  return etype;
}

/* enumerations from "gstpipeline.h" */
GType
gst_pipeline_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PIPELINE_FLAG_FIXED_CLOCK, "GST_PIPELINE_FLAG_FIXED_CLOCK",
            "fixed-clock"},
      {GST_PIPELINE_FLAG_LAST, "GST_PIPELINE_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPipelineFlags", values);
  }
  return etype;
}

/* enumerations from "gstplugin.h" */
GType
gst_plugin_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PLUGIN_ERROR_MODULE, "GST_PLUGIN_ERROR_MODULE", "module"},
      {GST_PLUGIN_ERROR_DEPENDENCIES, "GST_PLUGIN_ERROR_DEPENDENCIES",
            "dependencies"},
      {GST_PLUGIN_ERROR_NAME_MISMATCH, "GST_PLUGIN_ERROR_NAME_MISMATCH",
            "name-mismatch"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPluginError", values);
  }
  return etype;
}

/* enumerations from "gstpluginfeature.h" */
GType
gst_rank_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_RANK_NONE, "GST_RANK_NONE", "none"},
      {GST_RANK_MARGINAL, "GST_RANK_MARGINAL", "marginal"},
      {GST_RANK_SECONDARY, "GST_RANK_SECONDARY", "secondary"},
      {GST_RANK_PRIMARY, "GST_RANK_PRIMARY", "primary"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstRank", values);
  }
  return etype;
}

/* enumerations from "gstquery.h" */
GType
gst_query_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_QUERY_NONE, "GST_QUERY_NONE", "none"},
      {GST_QUERY_POSITION, "GST_QUERY_POSITION", "position"},
      {GST_QUERY_LATENCY, "GST_QUERY_LATENCY", "latency"},
      {GST_QUERY_JITTER, "GST_QUERY_JITTER", "jitter"},
      {GST_QUERY_RATE, "GST_QUERY_RATE", "rate"},
      {GST_QUERY_SEEKING, "GST_QUERY_SEEKING", "seeking"},
      {GST_QUERY_CONVERT, "GST_QUERY_CONVERT", "convert"},
      {GST_QUERY_FORMATS, "GST_QUERY_FORMATS", "formats"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstQueryType", values);
  }
  return etype;
}

/* enumerations from "gsttag.h" */
GType
gst_tag_merge_mode_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstTagMergeMode", values);
  }
  return etype;
}

GType
gst_tag_flag_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_TAG_FLAG_UNDEFINED, "GST_TAG_FLAG_UNDEFINED", "undefined"},
      {GST_TAG_FLAG_META, "GST_TAG_FLAG_META", "meta"},
      {GST_TAG_FLAG_ENCODED, "GST_TAG_FLAG_ENCODED", "encoded"},
      {GST_TAG_FLAG_DECODED, "GST_TAG_FLAG_DECODED", "decoded"},
      {GST_TAG_FLAG_COUNT, "GST_TAG_FLAG_COUNT", "count"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstTagFlag", values);
  }
  return etype;
}

/* enumerations from "gsttask.h" */
GType
gst_task_state_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_TASK_STARTED, "GST_TASK_STARTED", "started"},
      {GST_TASK_STOPPED, "GST_TASK_STOPPED", "stopped"},
      {GST_TASK_PAUSED, "GST_TASK_PAUSED", "paused"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstTaskState", values);
  }
  return etype;
}

/* enumerations from "gsttrace.h" */
GType
gst_alloc_trace_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_ALLOC_TRACE_LIVE, "GST_ALLOC_TRACE_LIVE", "live"},
      {GST_ALLOC_TRACE_MEM_LIVE, "GST_ALLOC_TRACE_MEM_LIVE", "mem-live"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstAllocTraceFlags", values);
  }
  return etype;
}

/* enumerations from "gsttypefind.h" */
GType
gst_type_find_probability_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_TYPE_FIND_MINIMUM, "GST_TYPE_FIND_MINIMUM", "minimum"},
      {GST_TYPE_FIND_POSSIBLE, "GST_TYPE_FIND_POSSIBLE", "possible"},
      {GST_TYPE_FIND_LIKELY, "GST_TYPE_FIND_LIKELY", "likely"},
      {GST_TYPE_FIND_NEARLY_CERTAIN, "GST_TYPE_FIND_NEARLY_CERTAIN",
            "nearly-certain"},
      {GST_TYPE_FIND_MAXIMUM, "GST_TYPE_FIND_MAXIMUM", "maximum"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstTypeFindProbability", values);
  }
  return etype;
}

/* enumerations from "gsturi.h" */
GType
gst_uri_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_URI_UNKNOWN, "GST_URI_UNKNOWN", "unknown"},
      {GST_URI_SINK, "GST_URI_SINK", "sink"},
      {GST_URI_SRC, "GST_URI_SRC", "src"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstURIType", values);
  }
  return etype;
}

/* enumerations from "gstregistry.h" */
GType
gst_registry_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_REGISTRY_OK, "GST_REGISTRY_OK", "ok"},
      {GST_REGISTRY_LOAD_ERROR, "GST_REGISTRY_LOAD_ERROR", "load-error"},
      {GST_REGISTRY_SAVE_ERROR, "GST_REGISTRY_SAVE_ERROR", "save-error"},
      {GST_REGISTRY_PLUGIN_LOAD_ERROR, "GST_REGISTRY_PLUGIN_LOAD_ERROR",
            "plugin-load-error"},
      {GST_REGISTRY_PLUGIN_SIGNATURE_ERROR,
            "GST_REGISTRY_PLUGIN_SIGNATURE_ERROR", "plugin-signature-error"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstRegistryReturn", values);
  }
  return etype;
}

GType
gst_registry_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_REGISTRY_READABLE, "GST_REGISTRY_READABLE", "readable"},
      {GST_REGISTRY_WRITABLE, "GST_REGISTRY_WRITABLE", "writable"},
      {GST_REGISTRY_EXISTS, "GST_REGISTRY_EXISTS", "exists"},
      {GST_REGISTRY_REMOTE, "GST_REGISTRY_REMOTE", "remote"},
      {GST_REGISTRY_DELAYED_LOADING, "GST_REGISTRY_DELAYED_LOADING",
            "delayed-loading"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstRegistryFlags", values);
  }
  return etype;
}

/* enumerations from "gstparse.h" */
GType
gst_parse_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
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
    etype = g_enum_register_static ("GstParseError", values);
  }
  return etype;
}

/* Generated data ends here */
