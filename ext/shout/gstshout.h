/* GStreamer
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


#ifndef __GST_ICECASTSEND_H__
#define __GST_ICECASTSEND_H__

#include <gst/gst.h>
#include <shout/shout.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


/* Definition of structure storing data for this element. */
  typedef struct _GstIcecastSend GstIcecastSend;
  struct _GstIcecastSend
  {
    GstElement element;

    GstPad *sinkpad, *srcpad;

    shout_conn_t conn;

    gchar *ip;
    guint port;
    gchar *password;
    gboolean public;
    gchar *name;
    gchar *description;
    gchar *genre;
    gchar *mount;
    gchar *dumpfile;
    gboolean icy;
    gchar *aim;
    gchar *icq;
    gchar *irc;

  };

/* Standard definition defining a class for this element. */
  typedef struct _GstIcecastSendClass GstIcecastSendClass;
  struct _GstIcecastSendClass
  {
    GstElementClass parent_class;
  };

/* Standard macros for defining types for this element.  */
#define GST_TYPE_ICECASTSEND \
  (gst_icecastsend_get_type())
#define GST_ICECASTSEND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ICECASTSEND,GstIcecastSend))
#define GST_ICECASTSEND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ICECASTSEND,GstIcecastSend))
#define GST_IS_ICECASTSEND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ICECASTSEND))
#define GST_IS_ICECASTSEND_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ICECASTSEND))

/* Standard function returning type information. */
  GType gst_icecastsend_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_ICECASTSEND_H__ */
