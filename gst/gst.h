/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <unistd.h>

#include <gtk/gtk.h>

#include <gst/gstlog.h>

#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstbuffer.h>
#include <gst/gstcpu.h>
#include <gst/gstelement.h>
#include <gst/gstextratypes.h>
#include <gst/gstbin.h>
#include <gst/gstpipeline.h>
#include <gst/gstthread.h>
#include <gst/gstsrc.h>
#include <gst/gstfilter.h>
#include <gst/gstsink.h>
#include <gst/gstconnection.h>
#include <gst/gsttype.h>
#include <gst/gstplugin.h>
#include <gst/gstutils.h>
#include <gst/gsttrace.h>
#include <gst/gstxml.h>

#include <gst/gsttee.h>

/* initialize GST */
void gst_init(int *argc,char **argv[]);

void gst_main		(void);
void gst_main_quit	(void);

/* debugging */
#ifndef DEBUG
#ifdef DEBUG_ENABLED
#define DEBUG(format, args...) g_print("DEBUG:(%d) " format, getpid() , ##args)
#else
#define DEBUG(format, args...)
#endif
#endif

#endif /* __GST_H__ */
