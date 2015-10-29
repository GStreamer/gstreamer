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

void _priv_gst_tracing_init (void);
void _priv_gst_tracing_deinit (void);

/* tracer quarks */

/* These enums need to match the number and order
 * of strings declared in _quark_table, in gsttracerutils.c */
typedef enum /*< skip >*/
{
  GST_TRACER_QUARK_HOOK_PAD_PUSH_PRE = 0,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_POST,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_LIST_PRE,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_LIST_POST,
  GST_TRACER_QUARK_HOOK_PAD_PULL_RANGE_PRE,
  GST_TRACER_QUARK_HOOK_PAD_PULL_RANGE_POST,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_EVENT_PRE ,
  GST_TRACER_QUARK_HOOK_PAD_PUSH_EVENT_POST,
  GST_TRACER_QUARK_HOOK_PAD_QUERY_PRE ,
  GST_TRACER_QUARK_HOOK_PAD_QUERY_POST,
  GST_TRACER_QUARK_HOOK_ELEMENT_POST_MESSAGE_PRE,
  GST_TRACER_QUARK_HOOK_ELEMENT_POST_MESSAGE_POST,
  GST_TRACER_QUARK_HOOK_ELEMENT_QUERY_PRE,
  GST_TRACER_QUARK_HOOK_ELEMENT_QUERY_POST,
  GST_TRACER_QUARK_HOOK_ELEMENT_NEW,
  GST_TRACER_QUARK_HOOK_ELEMENT_ADD_PAD,
  GST_TRACER_QUARK_HOOK_ELEMENT_REMOVE_PAD,
  GST_TRACER_QUARK_HOOK_BIN_ADD_PRE,
  GST_TRACER_QUARK_HOOK_BIN_ADD_POST,
  GST_TRACER_QUARK_HOOK_BIN_REMOVE_PRE,
  GST_TRACER_QUARK_HOOK_BIN_REMOVE_POST,
  GST_TRACER_QUARK_HOOK_PAD_LINK_PRE,
  GST_TRACER_QUARK_HOOK_PAD_LINK_POST,
  GST_TRACER_QUARK_HOOK_PAD_UNLINK_PRE,
  GST_TRACER_QUARK_HOOK_PAD_UNLINK_POST,
  GST_TRACER_QUARK_HOOK_ELEMENT_CHANGE_STATE_PRE,
  GST_TRACER_QUARK_HOOK_ELEMENT_CHANGE_STATE_POST,
  GST_TRACER_QUARK_MAX
} GstTracerQuarkId;

extern GQuark _priv_gst_tracer_quark_table[GST_TRACER_QUARK_MAX];

#define GST_TRACER_QUARK(q) _priv_gst_tracer_quark_table[GST_TRACER_QUARK_##q]

/* tracing module helpers */

typedef struct {
  GObject *tracer;
  GCallback func;
} GstTracerHook;

extern gboolean _priv_tracer_enabled;
/* key are hook-id quarks, values are GstTracerHook */
extern GHashTable *_priv_tracers; 

#define GST_TRACER_IS_ENABLED (_priv_tracer_enabled)

#define GST_TRACER_TS \
  GST_CLOCK_DIFF (_priv_gst_info_start_time, gst_util_get_timestamp ())

/* tracing hooks */

#define GST_TRACER_ARGS h->tracer, ts
#define GST_TRACER_DISPATCH(key,type,args) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED) {                                         \
    GstClockTime ts = GST_TRACER_TS;                                   \
    GList *__l, *__n;                                                  \
    GstTracerHook *h;                                                  \
    __l = g_hash_table_lookup (_priv_tracers, GINT_TO_POINTER (key));  \
    for (__n = __l; __n; __n = g_list_next (__n)) {                    \
      h = (GstTracerHook *) __n->data;                                 \
      ((type)(h->func)) args;                                          \
    }                                                                  \
    __l = g_hash_table_lookup (_priv_tracers, NULL);                   \
    for (__n = __l; __n; __n = g_list_next (__n)) {                    \
      h = (GstTracerHook *) __n->data;                                 \
      ((type)(h->func)) args;                                          \
    }                                                                  \
  }                                                                    \
}G_STMT_END

typedef void (*GstTracerHookPadPushPre) (GObject *, GstClockTime, GstPad *, 
    GstBuffer *);
#define GST_TRACER_PAD_PUSH_PRE(pad, buffer) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PUSH_PRE), \
    GstTracerHookPadPushPre, (GST_TRACER_ARGS, pad, buffer)); \
}G_STMT_END

typedef void (*GstTracerHookPadPushPost) (GObject *, GstClockTime, GstPad *, 
    GstFlowReturn);
#define GST_TRACER_PAD_PUSH_POST(pad, res) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PUSH_POST), \
    GstTracerHookPadPushPost, (GST_TRACER_ARGS, pad, res)); \
}G_STMT_END

typedef void (*GstTracerHookPadPushListPre) (GObject *, GstClockTime, GstPad *, 
    GstBufferList *);
#define GST_TRACER_PAD_PUSH_LIST_PRE(pad, list) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PUSH_LIST_PRE), \
    GstTracerHookPadPushListPre, (GST_TRACER_ARGS, pad, list)); \
}G_STMT_END

typedef void (*GstTracerHookPadPushListPost) (GObject *, GstClockTime, GstPad *, 
    GstFlowReturn);
#define GST_TRACER_PAD_PUSH_LIST_POST(pad, res) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PUSH_LIST_POST), \
    GstTracerHookPadPushListPost, (GST_TRACER_ARGS, pad, res)); \
}G_STMT_END

typedef void (*GstTracerHookPadPullRangePre) (GObject *, GstClockTime, GstPad *, 
    guint64, guint);
#define GST_TRACER_PAD_PULL_RANGE_PRE(pad, offset, size) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PULL_RANGE_PRE), \
    GstTracerHookPadPullRangePre, (GST_TRACER_ARGS, pad, offset, size)); \
}G_STMT_END

typedef void (*GstTracerHookPadPullRangePost) (GObject *, GstClockTime,
    GstPad *, GstBuffer *, GstFlowReturn);
#define GST_TRACER_PAD_PULL_RANGE_POST(pad, buffer, res) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PULL_RANGE_POST), \
    GstTracerHookPadPullRangePost, (GST_TRACER_ARGS, pad, buffer, res)); \
}G_STMT_END

typedef void (*GstTracerHookPadPushEventPre) (GObject *, GstClockTime, GstPad *, 
    GstEvent *);
#define GST_TRACER_PAD_PUSH_EVENT_PRE(pad, event) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PUSH_EVENT_PRE), \
    GstTracerHookPadPushEventPre, (GST_TRACER_ARGS, pad, event)); \
}G_STMT_END

typedef void (*GstTracerHookPadPushEventPost) (GObject *, GstClockTime, 
    GstPad *, gboolean);
#define GST_TRACER_PAD_PUSH_EVENT_POST(pad, res) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_PUSH_EVENT_POST), \
    GstTracerHookPadPushEventPost, (GST_TRACER_ARGS, pad, res)); \
}G_STMT_END

typedef void (*GstTracerHookPadQueryPre) (GObject *, GstClockTime, GstPad *,
    GstQuery *);
#define GST_TRACER_PAD_QUERY_PRE(pad, query) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_QUERY_PRE), \
    GstTracerHookPadQueryPre, (GST_TRACER_ARGS, pad, query)); \
}G_STMT_END

typedef void (*GstTracerHookPadQueryPost) (GObject *, GstClockTime,
    GstPad *, gboolean, GstQuery *);
#define GST_TRACER_PAD_QUERY_POST(pad, res, query) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_QUERY_POST), \
    GstTracerHookPadQueryPost, (GST_TRACER_ARGS, pad, res, query)); \
}G_STMT_END

typedef void (*GstTracerHookElementPostMessagePre) (GObject *, GstClockTime,
    GstElement *, GstMessage *);
#define GST_TRACER_ELEMENT_POST_MESSAGE_PRE(element, message) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_POST_MESSAGE_PRE), \
    GstTracerHookElementPostMessagePre, (GST_TRACER_ARGS, element, message)); \
}G_STMT_END

typedef void (*GstTracerHookElementPostMessagePost) (GObject *, GstClockTime,
    GstElement *, gboolean);
#define GST_TRACER_ELEMENT_POST_MESSAGE_POST(element, res) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_POST_MESSAGE_POST), \
    GstTracerHookElementPostMessagePost, (GST_TRACER_ARGS, element, res)); \
}G_STMT_END

typedef void (*GstTracerHookElementQueryPre) (GObject *, GstClockTime,
    GstElement *, GstQuery *);
#define GST_TRACER_ELEMENT_QUERY_PRE(element, query) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_QUERY_PRE), \
    GstTracerHookElementQueryPre, (GST_TRACER_ARGS, element, query)); \
}G_STMT_END

typedef void (*GstTracerHookElementQueryPost) (GObject *, GstClockTime,
    GstElement *, gboolean);
#define GST_TRACER_ELEMENT_QUERY_POST(element, res) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_QUERY_POST), \
    GstTracerHookElementQueryPost, (GST_TRACER_ARGS, element, res)); \
}G_STMT_END

typedef void (*GstTracerHookElementNew) (GObject *, GstClockTime,
    GstElement *);
#define GST_TRACER_ELEMENT_NEW(element) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_NEW), \
    GstTracerHookElementNew, (GST_TRACER_ARGS, element)); \
}G_STMT_END

typedef void (*GstTracerHookElementAddPad) (GObject *, GstClockTime,
    GstElement *, GstPad *);
#define GST_TRACER_ELEMENT_ADD_PAD(element, pad) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_ADD_PAD), \
    GstTracerHookElementAddPad, (GST_TRACER_ARGS, element, pad)); \
}G_STMT_END

typedef void (*GstTracerHookElementRemovePad) (GObject *, GstClockTime,
    GstElement *, GstPad *);
#define GST_TRACER_ELEMENT_REMOVE_PAD(element, pad) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_REMOVE_PAD), \
    GstTracerHookElementRemovePad, (GST_TRACER_ARGS, element, pad)); \
}G_STMT_END

typedef void (*GstTracerHookElementChangeStatePre) (GObject *, GstClockTime,
    GstElement *, GstStateChange);
#define GST_TRACER_ELEMENT_CHANGE_STATE_PRE(element, transition) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_CHANGE_STATE_PRE), \
    GstTracerHookElementChangeStatePre, (GST_TRACER_ARGS, element, transition)); \
}G_STMT_END

typedef void (*GstTracerHookElementChangeStatePost) (GObject *, GstClockTime,
    GstElement *, GstStateChange, GstStateChangeReturn);
#define GST_TRACER_ELEMENT_CHANGE_STATE_POST(element, transition, result) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_ELEMENT_CHANGE_STATE_POST), \
    GstTracerHookElementChangeStatePost, (GST_TRACER_ARGS, element, transition, result)); \
}G_STMT_END

typedef void (*GstTracerHookBinAddPre) (GObject *, GstClockTime,
    GstBin *, GstElement *);
#define GST_TRACER_BIN_ADD_PRE(bin, element) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_BIN_ADD_PRE), \
    GstTracerHookBinAddPre, (GST_TRACER_ARGS, bin, element)); \
}G_STMT_END

typedef void (*GstTracerHookBinAddPost) (GObject *, GstClockTime,
    GstBin *, GstElement *, gboolean);
#define GST_TRACER_BIN_ADD_POST(bin, element, result) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_BIN_ADD_POST), \
    GstTracerHookBinAddPost, (GST_TRACER_ARGS, bin, element, result)); \
}G_STMT_END

typedef void (*GstTracerHookBinRemovePre) (GObject *, GstClockTime,
    GstBin *, GstElement *);
#define GST_TRACER_BIN_REMOVE_PRE(bin, element) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_BIN_REMOVE_PRE), \
    GstTracerHookBinRemovePre, (GST_TRACER_ARGS, bin, element)); \
}G_STMT_END

typedef void (*GstTracerHookBinRemovePost) (GObject *, GstClockTime,
    GstBin *, gboolean);
#define GST_TRACER_BIN_REMOVE_POST(bin, result) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_BIN_REMOVE_POST), \
    GstTracerHookBinRemovePost, (GST_TRACER_ARGS, bin, result)); \
}G_STMT_END

typedef void (*GstTracerHookPadLinkPre) (GObject *, GstClockTime,
    GstPad *, GstPad *);
#define GST_TRACER_PAD_LINK_PRE(srcpad, sinkpad) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_LINK_PRE), \
    GstTracerHookPadLinkPre, (GST_TRACER_ARGS, srcpad, sinkpad)); \
}G_STMT_END

typedef void (*GstTracerHookPadLinkPost) (GObject *, GstClockTime,
    GstPad *, GstPad *, GstPadLinkReturn);
#define GST_TRACER_PAD_LINK_POST(srcpad, sinkpad, result) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_LINK_POST), \
    GstTracerHookPadLinkPost, (GST_TRACER_ARGS, srcpad, sinkpad, result)); \
}G_STMT_END

typedef void (*GstTracerHookPadUnlinkPre) (GObject *, GstClockTime,
    GstPad *, GstPad *);
#define GST_TRACER_PAD_UNLINK_PRE(srcpad, sinkpad) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_UNLINK_PRE), \
    GstTracerHookPadUnlinkPre, (GST_TRACER_ARGS, srcpad, sinkpad)); \
}G_STMT_END

typedef void (*GstTracerHookPadUnlinkPost) (GObject *, GstClockTime,
    GstPad *, GstPad *, gboolean);
#define GST_TRACER_PAD_UNLINK_POST(srcpad, sinkpad, result) G_STMT_START{ \
  GST_TRACER_DISPATCH(GST_TRACER_QUARK(HOOK_PAD_UNLINK_POST), \
    GstTracerHookPadUnlinkPost, (GST_TRACER_ARGS, srcpad, sinkpad, result)); \
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
#define GST_TRACER_PAD_QUERY_PRE(pad, query)
#define GST_TRACER_PAD_QUERY_POST(pad, res, query)
#define GST_TRACER_ELEMENT_POST_MESSAGE_PRE(element, message)
#define GST_TRACER_ELEMENT_POST_MESSAGE_POST(element, res)
#define GST_TRACER_ELEMENT_QUERY_PRE(element, query)
#define GST_TRACER_ELEMENT_QUERY_POST(element, res)
#define GST_TRACER_ELEMENT_NEW(element)
#define GST_TRACER_ELEMENT_ADD_PAD(element, pad)
#define GST_TRACER_ELEMENT_REMOVE_PAD(element, pad)
#define GST_TRACER_ELEMENT_CHANGE_STATE_PRE(element, transition)
#define GST_TRACER_ELEMENT_CHANGE_STATE_POST(element, transition, res)
#define GST_TRACER_BIN_ADD_PRE(bin, element)
#define GST_TRACER_BIN_ADD_POST(bin, element, res)
#define GST_TRACER_BIN_REMOVE_PRE(bin, element)
#define GST_TRACER_BIN_REMOVE_POST(bin, res)
#define GST_TRACER_PAD_LINK_PRE(srcpad, sinkpad)
#define GST_TRACER_PAD_LINK_POST(srcpad, sinkpad, res)
#define GST_TRACER_PAD_UNLINK_PRE(srcpad, sinkpad)
#define GST_TRACER_PAD_UNLINK_POST(srcpad, sinkpad, res)

#endif /* GST_DISABLE_GST_DEBUG */

G_END_DECLS

#endif /* __GST_TRACER_UTILS_H__ */

