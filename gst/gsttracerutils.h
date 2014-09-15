/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerutils.h: tracing subsystem
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


#ifndef __GST_TRACER_UTILS_H__
#define __GST_TRACER_UTILS_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gstconfig.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

#ifndef GST_DISABLE_GST_DEBUG

/* tracing hooks */

void _priv_gst_tracer_init (void);
void _priv_gst_tracer_deinit (void);

/* tracing modules */

gboolean gst_tracer_register (GstPlugin * plugin, const gchar * name, GType type);

/* tracing helpers */

void gst_tracer_dispatch (GQuark detail, ...);

/* tracer quarks */

/* These enums need to match the number and order
 * of strings declared in _quark_table, in gsttracerutils.c */
typedef enum _GstTracerQuarkId
{
  GST_TRACER_QUARK_HOOK_PAD_PUSH_PRE = 0,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_POST,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_LIST_PRE,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_LIST_POST,
  GST_TRACER_QUARK_HOOK_PAD_PULL_RANGE_PRE,
  GST_TRACER_QUARK_HOOK_PAD_PULL_RANGE_POST,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_EVENT_PRE ,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_EVENT_POST,
  GST_TRACER_QUARK_HOOK_ELEMENT_POST_MESSAGE_PRE,
  GST_TRACER_QUARK_HOOK_ELEMENT_POST_MESSAGE_POST,
  GST_TRACER_QUARK_HOOK_ELEMENT_QUERY_PRE,
  GST_TRACER_QUARK_HOOK_ELEMENT_QUERY_POST,
  GST_TRACER_QUARK_MAX
} GstTracerQuarkId;

extern GQuark _priv_gst_tracer_quark_table[GST_TRACER_QUARK_MAX];

#define GST_TRACER_QUARK(q) _priv_gst_tracer_quark_table[GST_TRACER_QUARK_##q]

/* tracing module helpers */

extern gboolean _priv_tracer_enabled;
extern GHashTable *_priv_tracers;

#define GST_TRACER_IS_ENABLED(id) \
  (_priv_tracer_enabled && \
      (g_hash_table_contains (_priv_tracers, GINT_TO_POINTER(id))))

#define GST_TRACER_TS \
  GST_CLOCK_DIFF (_priv_gst_info_start_time, gst_util_get_timestamp ())

/* tracing hooks */

#define GST_TRACER_PAD_PUSH_PRE(pad, buffer) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PUSH_PRE))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PUSH_PRE), \
        GST_TRACER_TS, \
        pad, buffer); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_POST(pad, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PUSH_POST))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PUSH_POST), \
        GST_TRACER_TS, \
        pad, res); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_LIST_PRE(pad, list) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PUSH_LIST_PRE))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PUSH_LIST_PRE), \
        GST_TRACER_TS, \
        pad, list); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_LIST_POST(pad, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PUSH_LIST_POST))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PUSH_LIST_POST), \
        GST_TRACER_TS, \
        pad, res); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PULL_RANGE_PRE(pad, offset, size) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PULL_RANGE_PRE))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PULL_RANGE_PRE), \
        GST_TRACER_TS, \
        pad, offset, size); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PULL_RANGE_POST(pad, buffer, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PULL_RANGE_POST))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PULL_RANGE_POST), \
        GST_TRACER_TS, \
        pad, buffer, res); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_EVENT_PRE(pad, event) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PUSH_EVENT_PRE))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PUSH_EVENT_PRE), \
        GST_TRACER_TS, \
        pad, event); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_EVENT_POST(pad, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_PAD_PUSH_EVENT_POST))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_PAD_PUSH_EVENT_POST), \
        GST_TRACER_TS, \
        pad, res); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_POST_MESSAGE_PRE(element, message) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_ELEMENT_POST_MESSAGE_PRE))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_ELEMENT_POST_MESSAGE_PRE), \
        GST_TRACER_TS, \
        element, message); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_POST_MESSAGE_POST(element, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_ELEMENT_POST_MESSAGE_POST))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_ELEMENT_POST_MESSAGE_POST), \
        GST_TRACER_TS, \
        element, res); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_QUERY_PRE(element, query) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_ELEMENT_QUERY_PRE))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_ELEMENT_QUERY_PRE), \
        GST_TRACER_TS, \
        element, query); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_QUERY_POST(element, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_QUARK(HOOK_ELEMENT_QUERY_POST))) { \
    gst_tracer_dispatch (GST_TRACER_QUARK(HOOK_ELEMENT_QUERY_POST), \
        GST_TRACER_TS, \
        element, res); \
  } \
}G_STMT_END

#else /* !GST_DISABLE_GST_DEBUG */

#define GST_TRACER_PAD_PUSH_PRE(pad, buffer)
#define GST_TRACER_PAD_PUSH_POST(pad, res)
#define GST_TRACER_PAD_PUSH_LIST_PRE(pad, list)
#define GST_TRACER_PAD_PUSH_LIST_POST(pad, res)
#define GST_TRACER_PAD_PULL_RANGE_PRE(pad, offset, size)
#define GST_TRACER_PAD_PULL_RANGE_POST(pad, buffer, res)
#define GST_TRACER_PAD_PUSH_EVENT_PRE(pad, event)
#define GST_TRACER_PAD_PUSH_EVENT_POST(pad, res)
#define GST_TRACER_ELEMENT_POST_MESSAGE_PRE(element, message)
#define GST_TRACER_ELEMENT_POST_MESSAGE_POST(element, res)
#define GST_TRACER_ELEMENT_QUERY_PRE(element, query)
#define GST_TRACER_ELEMENT_QUERY_POST(element, res)

#endif /* GST_DISABLE_GST_DEBUG */

G_END_DECLS

#endif /* __GST_TRACER_UTILS_H__ */

