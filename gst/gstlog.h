/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstlog.h: Header for event logging (depracated?)
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

#ifndef __GST_LOG_H__
#define __GST_LOG_H__

#include <glib.h>

G_BEGIN_DECLS

extern const char             *g_log_domain_gstreamer;

#ifdef G_HAVE_ISO_VARARGS

/* information messages */
#define GST_SHOW_INFO
#ifdef GST_SHOW_INFO
#define gst_info(...) fprintf(stderr, __VA_ARGS__)
#else
#define gst_info(...)
#endif

#elif defined(G_HAVE_GNUC_VARARGS)

/* information messages */
#define GST_SHOW_INFO
#ifdef GST_SHOW_INFO
#define gst_info(format,args...) fprintf(stderr,format,##args)
#else
#define gst_info(format,args...)
#endif
#endif

G_END_DECLS

#endif /* __GST_LOG_H__ */
