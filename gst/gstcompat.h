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


/* API compatibility stuff */
#ifndef __GSTCOMPAT_H__
#define __GSTCOMPAT_H__

G_BEGIN_DECLS
#ifndef GST_DISABLE_DEPRECATED
/* 0.5.2 changes */
/* element functions */
#define	gst_element_connect(a,b)	gst_element_link(a,b)
#define	gst_element_connect_pads(a,b,c,d) \
					gst_element_link_pads(a,b,c,d)
#ifdef G_HAVE_ISO_VARARGS
#define	gst_element_connect_many(a,...)	gst_element_link_many(a,__VA_ARGS__)
#else
#define gst_element_connect_many(a,args...) \
					gst_element_link_many(a, ## args)
#endif
#define	gst_element_connect_filtered(a,b,c) \
					gst_element_link_filtered(a,b,c)
#define	gst_element_disconnect(a,b)	gst_element_unlink(a,b)
/* pad functions */
#define gst_pad_connect(a,b)		gst_pad_link(a,b)
#define gst_pad_connect_filtered(a,b,c)	gst_pad_link_filtered(a,b,c)
#define gst_pad_disconnect(a,b)		gst_pad_unlink(a,b)
#define gst_pad_proxy_connect(a,b)	gst_pad_proxy_link(a,b)
#define gst_pad_set_connect_function(a,b) \
					gst_pad_set_link_function(a,b)
/* pad macros */
#define GST_PAD_IS_CONNECTED(a)		GST_PAD_IS_LINKED(a)
/* pad enums */
#define GST_PAD_CONNECT_REFUSED		GST_PAD_LINK_REFUSED
#define GST_PAD_CONNECT_DELAYED		GST_PAD_LINK_DELAYED
#define GST_PAD_CONNECT_OK		GST_PAD_LINK_OK
#define GST_PAD_CONNECT_DONE		GST_PAD_LINK_DONE
typedef GstPadLinkReturn GstPadConnectReturn;

/* pad function types */
typedef GstPadLinkFunction GstPadConnectFunction;

/* probably not used */
/*
 * GST_RPAD_LINKFUNC
 */
#endif /* not GST_DISABLE_DEPRECATED */

G_END_DECLS
#endif /* __GSTCOMPAT_H__ */
