
/* Generated data (by glib-mkenums) */

#include <gst/gst.h>

/* enumerations from "gstobject.h" */
GType
gst_object_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_DESTROYED, "GST_DESTROYED", "destroyed"},
      {GST_FLOATING, "GST_FLOATING", "floating"},
      {GST_OBJECT_FLAG_LAST, "GST_OBJECT_FLAG_LAST", "object-flag-last"},
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
      {GST_BIN_FLAG_MANAGER, "GST_BIN_FLAG_MANAGER", "flag-manager"},
      {GST_BIN_SELF_SCHEDULABLE, "GST_BIN_SELF_SCHEDULABLE",
            "self-schedulable"},
      {GST_BIN_FLAG_PREFER_COTHREADS, "GST_BIN_FLAG_PREFER_COTHREADS",
            "flag-prefer-cothreads"},
      {GST_BIN_FLAG_FIXED_CLOCK, "GST_BIN_FLAG_FIXED_CLOCK",
            "flag-fixed-clock"},
      {GST_BIN_FLAG_LAST, "GST_BIN_FLAG_LAST", "flag-last"},
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
    static const GEnumValue values[] = {
      {GST_BUFFER_READONLY, "GST_BUFFER_READONLY", "readonly"},
      {GST_BUFFER_SUBBUFFER, "GST_BUFFER_SUBBUFFER", "subbuffer"},
      {GST_BUFFER_ORIGINAL, "GST_BUFFER_ORIGINAL", "original"},
      {GST_BUFFER_DONTFREE, "GST_BUFFER_DONTFREE", "dontfree"},
      {GST_BUFFER_KEY_UNIT, "GST_BUFFER_KEY_UNIT", "key-unit"},
      {GST_BUFFER_DONTKEEP, "GST_BUFFER_DONTKEEP", "dontkeep"},
      {GST_BUFFER_FLAG_LAST, "GST_BUFFER_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstBufferFlag", values);
  }
  return etype;
}


/* enumerations from "gstclock.h" */
GType
gst_clock_entry_status_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_CLOCK_ENTRY_OK, "GST_CLOCK_ENTRY_OK", "ok"},
      {GST_CLOCK_ENTRY_EARLY, "GST_CLOCK_ENTRY_EARLY", "early"},
      {GST_CLOCK_ENTRY_RESTART, "GST_CLOCK_ENTRY_RESTART", "restart"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstClockEntryStatus", values);
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
gst_clock_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_CLOCK_STOPPED, "GST_CLOCK_STOPPED", "stopped"},
      {GST_CLOCK_TIMEOUT, "GST_CLOCK_TIMEOUT", "timeout"},
      {GST_CLOCK_EARLY, "GST_CLOCK_EARLY", "early"},
      {GST_CLOCK_ERROR, "GST_CLOCK_ERROR", "error"},
      {GST_CLOCK_UNSUPPORTED, "GST_CLOCK_UNSUPPORTED", "unsupported"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstClockReturn", values);
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
      {GST_CLOCK_FLAG_CAN_SET_SPEED, "GST_CLOCK_FLAG_CAN_SET_SPEED",
            "set-speed"},
      {0, NULL, NULL}
    };

    etype = g_flags_register_static ("GstClockFlags", values);
  }
  return etype;
}


/* enumerations from "gstcpu.h" */
GType
gst_cpu_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_CPU_FLAG_MMX, "GST_CPU_FLAG_MMX", "mmx"},
      {GST_CPU_FLAG_SSE, "GST_CPU_FLAG_SSE", "sse"},
      {GST_CPU_FLAG_MMXEXT, "GST_CPU_FLAG_MMXEXT", "mmxext"},
      {GST_CPU_FLAG_3DNOW, "GST_CPU_FLAG_3DNOW", "3dnow"},
      {0, NULL, NULL}
    };

    etype = g_flags_register_static ("GstCPUFlags", values);
  }
  return etype;
}


/* enumerations from "gstdata.h" */
GType
gst_data_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_DATA_READONLY, "GST_DATA_READONLY", "readonly"},
      {GST_DATA_FLAG_LAST, "GST_DATA_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstDataFlags", values);
  }
  return etype;
}


/* enumerations from "gstelement.h" */
GType
gst_element_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_ELEMENT_COMPLEX, "GST_ELEMENT_COMPLEX", "complex"},
      {GST_ELEMENT_DECOUPLED, "GST_ELEMENT_DECOUPLED", "decoupled"},
      {GST_ELEMENT_THREAD_SUGGESTED, "GST_ELEMENT_THREAD_SUGGESTED",
            "thread-suggested"},
      {GST_ELEMENT_INFINITE_LOOP, "GST_ELEMENT_INFINITE_LOOP", "infinite-loop"},
      {GST_ELEMENT_NEW_LOOPFUNC, "GST_ELEMENT_NEW_LOOPFUNC", "new-loopfunc"},
      {GST_ELEMENT_EVENT_AWARE, "GST_ELEMENT_EVENT_AWARE", "event-aware"},
      {GST_ELEMENT_USE_THREADSAFE_PROPERTIES,
            "GST_ELEMENT_USE_THREADSAFE_PROPERTIES",
            "use-threadsafe-properties"},
      {GST_ELEMENT_SCHEDULER_PRIVATE1, "GST_ELEMENT_SCHEDULER_PRIVATE1",
            "scheduler-private1"},
      {GST_ELEMENT_SCHEDULER_PRIVATE2, "GST_ELEMENT_SCHEDULER_PRIVATE2",
            "scheduler-private2"},
      {GST_ELEMENT_LOCKED_STATE, "GST_ELEMENT_LOCKED_STATE", "locked-state"},
      {GST_ELEMENT_IN_ERROR, "GST_ELEMENT_IN_ERROR", "in-error"},
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
      {GST_CORE_ERROR_SCHEDULER, "GST_CORE_ERROR_SCHEDULER", "scheduler"},
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
      {GST_EVENT_EOS, "GST_EVENT_EOS", "eos"},
      {GST_EVENT_FLUSH, "GST_EVENT_FLUSH", "flush"},
      {GST_EVENT_EMPTY, "GST_EVENT_EMPTY", "empty"},
      {GST_EVENT_DISCONTINUOUS, "GST_EVENT_DISCONTINUOUS", "discontinuous"},
      {GST_EVENT_QOS, "GST_EVENT_QOS", "qos"},
      {GST_EVENT_SEEK, "GST_EVENT_SEEK", "seek"},
      {GST_EVENT_SEEK_SEGMENT, "GST_EVENT_SEEK_SEGMENT", "seek-segment"},
      {GST_EVENT_SEGMENT_DONE, "GST_EVENT_SEGMENT_DONE", "segment-done"},
      {GST_EVENT_SIZE, "GST_EVENT_SIZE", "size"},
      {GST_EVENT_RATE, "GST_EVENT_RATE", "rate"},
      {GST_EVENT_FILLER, "GST_EVENT_FILLER", "filler"},
      {GST_EVENT_TS_OFFSET, "GST_EVENT_TS_OFFSET", "ts-offset"},
      {GST_EVENT_INTERRUPT, "GST_EVENT_INTERRUPT", "interrupt"},
      {GST_EVENT_NAVIGATION, "GST_EVENT_NAVIGATION", "navigation"},
      {GST_EVENT_TAG, "GST_EVENT_TAG", "tag"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstEventType", values);
  }
  return etype;
}

GType
gst_event_flag_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_EVENT_FLAG_NONE, "GST_EVENT_FLAG_NONE", "event-flag-none"},
      {GST_RATE_FLAG_NEGATIVE, "GST_RATE_FLAG_NEGATIVE", "rate-flag-negative"},
      {0, NULL, NULL}
    };

    etype = g_flags_register_static ("GstEventFlag", values);
  }
  return etype;
}

GType
gst_seek_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_SEEK_METHOD_CUR, "GST_SEEK_METHOD_CUR", "method-cur"},
      {GST_SEEK_METHOD_SET, "GST_SEEK_METHOD_SET", "method-set"},
      {GST_SEEK_METHOD_END, "GST_SEEK_METHOD_END", "method-end"},
      {GST_SEEK_FLAG_FLUSH, "GST_SEEK_FLAG_FLUSH", "flag-flush"},
      {GST_SEEK_FLAG_ACCURATE, "GST_SEEK_FLAG_ACCURATE", "flag-accurate"},
      {GST_SEEK_FLAG_KEY_UNIT, "GST_SEEK_FLAG_KEY_UNIT", "flag-key-unit"},
      {GST_SEEK_FLAG_SEGMENT_LOOP, "GST_SEEK_FLAG_SEGMENT_LOOP",
            "flag-segment-loop"},
      {0, NULL, NULL}
    };

    etype = g_flags_register_static ("GstSeekType", values);
  }
  return etype;
}

GType
gst_seek_accuracy_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_SEEK_CERTAIN, "GST_SEEK_CERTAIN", "certain"},
      {GST_SEEK_FUZZY, "GST_SEEK_FUZZY", "fuzzy"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstSeekAccuracy", values);
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


/* enumerations from "gstpad.h" */
GType
gst_pad_link_return_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PAD_LINK_REFUSED, "GST_PAD_LINK_REFUSED", "refused"},
      {GST_PAD_LINK_DELAYED, "GST_PAD_LINK_DELAYED", "delayed"},
      {GST_PAD_LINK_OK, "GST_PAD_LINK_OK", "ok"},
      {GST_PAD_LINK_DONE, "GST_PAD_LINK_DONE", "done"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstPadLinkReturn", values);
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
      {GST_PAD_DISABLED, "GST_PAD_DISABLED", "disabled"},
      {GST_PAD_NEGOTIATING, "GST_PAD_NEGOTIATING", "negotiating"},
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


/* enumerations from "gstquery.h" */
GType
gst_query_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_QUERY_NONE, "GST_QUERY_NONE", "none"},
      {GST_QUERY_TOTAL, "GST_QUERY_TOTAL", "total"},
      {GST_QUERY_POSITION, "GST_QUERY_POSITION", "position"},
      {GST_QUERY_LATENCY, "GST_QUERY_LATENCY", "latency"},
      {GST_QUERY_JITTER, "GST_QUERY_JITTER", "jitter"},
      {GST_QUERY_START, "GST_QUERY_START", "start"},
      {GST_QUERY_SEGMENT_END, "GST_QUERY_SEGMENT_END", "segment-end"},
      {GST_QUERY_RATE, "GST_QUERY_RATE", "rate"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstQueryType", values);
  }
  return etype;
}


/* enumerations from "gstscheduler.h" */
GType
gst_scheduler_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_SCHEDULER_FLAG_FIXED_CLOCK, "GST_SCHEDULER_FLAG_FIXED_CLOCK",
            "fixed-clock"},
      {GST_SCHEDULER_FLAG_NEW_API, "GST_SCHEDULER_FLAG_NEW_API", "new-api"},
      {GST_SCHEDULER_FLAG_LAST, "GST_SCHEDULER_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstSchedulerFlags", values);
  }
  return etype;
}

GType
gst_scheduler_state_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_SCHEDULER_STATE_NONE, "GST_SCHEDULER_STATE_NONE", "none"},
      {GST_SCHEDULER_STATE_RUNNING, "GST_SCHEDULER_STATE_RUNNING", "running"},
      {GST_SCHEDULER_STATE_STOPPED, "GST_SCHEDULER_STATE_STOPPED", "stopped"},
      {GST_SCHEDULER_STATE_ERROR, "GST_SCHEDULER_STATE_ERROR", "error"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstSchedulerState", values);
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


/* enumerations from "gstthread.h" */
GType
gst_thread_state_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_THREAD_STATE_SPINNING, "GST_THREAD_STATE_SPINNING",
            "state-spinning"},
      {GST_THREAD_STATE_REAPING, "GST_THREAD_STATE_REAPING", "state-reaping"},
      {GST_THREAD_MUTEX_LOCKED, "GST_THREAD_MUTEX_LOCKED", "mutex-locked"},
      {GST_THREAD_FLAG_LAST, "GST_THREAD_FLAG_LAST", "flag-last"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstThreadState", values);
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


/* enumerations from "gsttypes.h" */
GType
gst_element_state_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_STATE_VOID_PENDING, "GST_STATE_VOID_PENDING", "void-pending"},
      {GST_STATE_NULL, "GST_STATE_NULL", "null"},
      {GST_STATE_READY, "GST_STATE_READY", "ready"},
      {GST_STATE_PAUSED, "GST_STATE_PAUSED", "paused"},
      {GST_STATE_PLAYING, "GST_STATE_PLAYING", "playing"},
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
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstElementStateReturn", values);
  }
  return etype;
}

GType
gst_result_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_RESULT_OK, "GST_RESULT_OK", "ok"},
      {GST_RESULT_NOK, "GST_RESULT_NOK", "nok"},
      {GST_RESULT_NOT_IMPL, "GST_RESULT_NOT_IMPL", "not-impl"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("GstResult", values);
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
