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
extern const char             g_log_domain_gstreamer[];

#include <glib.h>

#include <stdlib.h>
#include <string.h>

/* Needed for GstRegistry * */
#include "gstregistry.h"
#include "gststructure.h"

G_BEGIN_DECLS

gboolean _priv_gst_in_valgrind (void);

/* Initialize GStreamer private quark storage */
void _priv_gst_quarks_initialize (void);

/* Other init functions called from gst_init().
 * FIXME 0.11: rename to _priv_gst_foo_init() so they don't get exported
 * (can't do this now because these functions used to be in our public
 * headers, so at least the symbols need to continue to be available unless
 * we want enterprise edition packagers dancing on our heads) */
void  _gst_buffer_initialize (void);
void  _gst_event_initialize (void);
void  _gst_format_initialize (void);
void  _gst_message_initialize (void);
void  _gst_plugin_initialize (void);
void  _gst_query_initialize (void);
void  _gst_tag_initialize (void);
void  _gst_value_initialize (void);

/* Private registry functions */
gboolean _priv_gst_registry_remove_cache_plugins (GstRegistry *registry);
void _priv_gst_registry_cleanup (void);

/* used in both gststructure.c and gstcaps.c; numbers are completely made up */
#define STRUCTURE_ESTIMATED_STRING_LEN(s) (16 + (s)->fields->len * 22)

gboolean  priv_gst_structure_append_to_gstring (const GstStructure * structure,
                                                GString            * s);


/*** debugging categories *****************************************************/

#ifndef GST_DISABLE_GST_DEBUG

#ifndef _MSC_VER
#define IMPORT_SYMBOL
#else /* _MSC_VER */
#ifndef LIBGSTREAMER_EXPORTS
#define IMPORT_SYMBOL __declspec(dllimport)
#else
#define IMPORT_SYMBOL 
#endif
#endif

#include <gst/gstinfo.h>

extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_GST_INIT;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_AUTOPLUG;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_AUTOPLUG_ATTEMPT;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PARENTAGE;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_STATES;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_SCHEDULING;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_BUFFER;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_BUS;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_CAPS;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_CLOCK;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_ELEMENT_PADS;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PADS;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PIPELINE;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PLUGIN_LOADING;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PLUGIN_INFO;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PROPERTIES;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_XML;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_NEGOTIATION;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_REFCOUNTING;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_ERROR_SYSTEM;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_EVENT;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_MESSAGE;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PARAMS;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_CALL_TRACE;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_SIGNAL;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_PROBE;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_REGISTRY;
extern IMPORT_SYMBOL GstDebugCategory *GST_CAT_QOS;

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
#define GST_CAT_QOS              NULL

#endif

G_END_DECLS
#endif /* __GST_PRIVATE_H__ */
