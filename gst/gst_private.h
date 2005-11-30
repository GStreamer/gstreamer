/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst_private.h: Private header for within libgst
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_PRIVATE_H__
#define __GST_PRIVATE_H__

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* This needs to be before glib.h, since it might be used in inline
 * functions */
extern const char             *g_log_domain_gstreamer;

#include <glib.h>

#include <stdlib.h>
#include <string.h>

G_BEGIN_DECLS

gboolean __gst_in_valgrind (void);

/*** debugging categories *****************************************************/

#ifndef GST_DISABLE_GST_DEBUG

#include <gst/gstinfo.h>

extern GstDebugCategory *GST_CAT_GST_INIT;
extern GstDebugCategory *GST_CAT_AUTOPLUG;
extern GstDebugCategory *GST_CAT_AUTOPLUG_ATTEMPT;
extern GstDebugCategory *GST_CAT_PARENTAGE;
extern GstDebugCategory *GST_CAT_STATES;
extern GstDebugCategory *GST_CAT_SCHEDULING;
extern GstDebugCategory *GST_CAT_BUFFER;
extern GstDebugCategory *GST_CAT_BUS;
extern GstDebugCategory *GST_CAT_CAPS;
extern GstDebugCategory *GST_CAT_CLOCK;
extern GstDebugCategory *GST_CAT_ELEMENT_PADS;
extern GstDebugCategory *GST_CAT_PADS;
extern GstDebugCategory *GST_CAT_PIPELINE;
extern GstDebugCategory *GST_CAT_PLUGIN_LOADING;
extern GstDebugCategory *GST_CAT_PLUGIN_INFO;
extern GstDebugCategory *GST_CAT_PROPERTIES;
extern GstDebugCategory *GST_CAT_XML;
extern GstDebugCategory *GST_CAT_NEGOTIATION;
extern GstDebugCategory *GST_CAT_REFCOUNTING;
extern GstDebugCategory *GST_CAT_ERROR_SYSTEM;
extern GstDebugCategory *GST_CAT_EVENT;
extern GstDebugCategory *GST_CAT_MESSAGE;
extern GstDebugCategory *GST_CAT_PARAMS;
extern GstDebugCategory *GST_CAT_CALL_TRACE;
extern GstDebugCategory *GST_CAT_SIGNAL;
extern GstDebugCategory *GST_CAT_PROBE;
extern GstDebugCategory *GST_CAT_REGISTRY;

#else

#define GST_CAT_GST_INIT         NULL
#define GST_CAT_AUTOPLUG         NULL
#define GST_CAT_AUTOPLUG_ATTEMPT NULL
#define GST_CAT_PARENTAGE        NULL
#define GST_CAT_STATES           NULL
#define GST_CAT_SCHEDULING       NULL
#define GST_CAT_DATAFLOW         NULL
#define GST_CAT_BUFFER           NULL
#define GST_CAT_BUS              NULL
#define GST_CAT_CAPS             NULL
#define GST_CAT_CLOCK            NULL
#define GST_CAT_ELEMENT_PADS     NULL
#define GST_CAT_PADS             NULL
#define GST_CAT_PIPELINE         NULL
#define GST_CAT_PLUGIN_LOADING   NULL
#define GST_CAT_PLUGIN_INFO      NULL
#define GST_CAT_PROPERTIES       NULL
#define GST_CAT_XML              NULL
#define GST_CAT_NEGOTIATION      NULL
#define GST_CAT_REFCOUNTING      NULL
#define GST_CAT_ERROR_SYSTEM     NULL
#define GST_CAT_EVENT            NULL
#define GST_CAT_MESSAGE          NULL
#define GST_CAT_PARAMS           NULL
#define GST_CAT_CALL_TRACE       NULL
#define GST_CAT_SIGNAL           NULL
#define GST_CAT_PROBE            NULL
#define GST_CAT_REGISTRY         NULL

#endif

G_END_DECLS
#endif /* __GST_PRIVATE_H__ */
