/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst.h: Main header for GStreamer, apps should include this
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


#ifndef __GST_H__
#define __GST_H__

#include <glib.h>
#include <popt.h>

#include <gst/gstenumtypes.h>
#include <gst/gsttypes.h>
#include <gst/gstversion.h>

#include <gst/gstbin.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstclock.h>
#include <gst/gstcpu.h>
#include <gst/gstelement.h>
#include <gst/gsterror.h>
#include <gst/gstevent.h>
#include <gst/gstindex.h>
#include <gst/gstinfo.h>
#include <gst/gstinterface.h>
#include <gst/gstmarshal.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstpipeline.h>
#include <gst/gstplugin.h>
#include <gst/gstscheduler.h>
#include <gst/gststructure.h>
#include <gst/gstsystemclock.h>
#include <gst/gsttag.h>
#include <gst/gsttaginterface.h>
#include <gst/gstthread.h>
#include <gst/gsttrace.h>
#include <gst/gsttypefind.h>
#include <gst/gsturi.h>
#include <gst/gsturitype.h>
#include <gst/gstutils.h>
#include <gst/gstvalue.h>
#include <gst/gstxml.h>

#include <gst/gstparse.h>
#include <gst/gstregistry.h>
#include <gst/gstregistrypool.h>

/* API compatibility stuff */
#include <gst/gstcompat.h>

G_BEGIN_DECLS
/* make our own type for poptOption because the struct poptOption
 * definition is iffy */
typedef struct poptOption GstPoptOption;

/* initialize GST */
void gst_init (int *argc, char **argv[]);
gboolean gst_init_check (int *argc, char **argv[]);
void gst_init_with_popt_table (int *argc, char **argv[],
    const GstPoptOption * popt_options);
gboolean gst_init_check_with_popt_table (int *argc, char **argv[],
    const GstPoptOption * popt_options);

const GstPoptOption *gst_init_get_popt_table (void);

void gst_use_threads (gboolean use_threads);
gboolean gst_has_threads (void);

void gst_main (void);
void gst_main_quit (void);

G_END_DECLS
#include <gst/gstlog.h>
#endif /* __GST_H__ */
