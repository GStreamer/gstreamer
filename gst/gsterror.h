/* GStreamer
 * Copyright (C) 2004 Thomas Vander Stichele <thomas at apestaart dot org>
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

#ifndef __GST_ERROR_H__
#define __GST_ERROR_H__

#include <glib.h>
#include <glib-object.h>
#include <errno.h>

G_BEGIN_DECLS
/*
 * we define FIXME error domains:
 * GST_CORE_ERROR
 * GST_LIBRARY_ERROR
 * GST_RESOURCE_ERROR
 * GST_STREAM_ERROR
 *
 * Check GError API docs for rationale for naming.
 */
/* Core errors are anything that can go wrong in or using
 * the core GStreamer library */
/* FIXME: should we divide in numerical blocks so we can easily add
          for example PAD errors later ? */
    typedef enum
{
  GST_CORE_ERROR_FAILED = 1,
  GST_CORE_ERROR_TOO_LAZY,
  GST_CORE_ERROR_NOT_IMPLEMENTED,
  GST_CORE_ERROR_STATE_CHANGE,
  GST_CORE_ERROR_PAD,
  GST_CORE_ERROR_THREAD,
  GST_CORE_ERROR_SCHEDULER,
  GST_CORE_ERROR_NEGOTIATION,
  GST_CORE_ERROR_EVENT,
  GST_CORE_ERROR_SEEK,
  GST_CORE_ERROR_CAPS,
  GST_CORE_ERROR_TAG,
  GST_CORE_ERROR_NUM_ERRORS
}
GstCoreError;

/* Library errors are for errors from the library being used by elements
   initializing, closing, ... */
typedef enum
{
  GST_LIBRARY_ERROR_FAILED = 1,
  GST_LIBRARY_ERROR_TOO_LAZY,
  GST_LIBRARY_ERROR_INIT,
  GST_LIBRARY_ERROR_SHUTDOWN,
  GST_LIBRARY_ERROR_SETTINGS,
  GST_LIBRARY_ERROR_ENCODE,
  GST_LIBRARY_ERROR_NUM_ERRORS
}
GstLibraryError;

/* Resource errors are for anything external used by an element:
   memory, files, network connections, process space, ...
   They're typically used by source and sink elements */
typedef enum
{
  GST_RESOURCE_ERROR_FAILED = 1,
  GST_RESOURCE_ERROR_TOO_LAZY,
  GST_RESOURCE_ERROR_NOT_FOUND,
  GST_RESOURCE_ERROR_BUSY,
  GST_RESOURCE_ERROR_OPEN_READ,
  GST_RESOURCE_ERROR_OPEN_WRITE,
  GST_RESOURCE_ERROR_OPEN_READ_WRITE,
  GST_RESOURCE_ERROR_CLOSE,
  GST_RESOURCE_ERROR_READ,
  GST_RESOURCE_ERROR_WRITE,
  GST_RESOURCE_ERROR_SEEK,
  GST_RESOURCE_ERROR_SYNC,
  GST_RESOURCE_ERROR_SETTINGS,
  GST_RESOURCE_ERROR_NUM_ERRORS
}
GstResourceError;

/* Stream errors are for anything related to the stream being processed:
   format errors, media type errors, ...
   They're typically used by decoders, demuxers, converters, ... */
typedef enum
{
  GST_STREAM_ERROR_FAILED = 1,
  GST_STREAM_ERROR_TOO_LAZY,
  GST_STREAM_ERROR_NOT_IMPLEMENTED,
  GST_STREAM_ERROR_TYPE_NOT_FOUND,
  GST_STREAM_ERROR_WRONG_TYPE,
  GST_STREAM_ERROR_CODEC_NOT_FOUND,
  GST_STREAM_ERROR_DECODE,
  GST_STREAM_ERROR_ENCODE,
  GST_STREAM_ERROR_DEMUX,
  GST_STREAM_ERROR_MUX,
  GST_STREAM_ERROR_FORMAT,
  GST_STREAM_ERROR_NUM_ERRORS
}
GstStreamError;

/* This should go away once we convinced glib people to register GError */
#define GST_TYPE_G_ERROR    (gst_g_error_get_type ())

#define GST_LIBRARY_ERROR   gst_library_error_quark ()
#define GST_RESOURCE_ERROR  gst_resource_error_quark ()
#define GST_CORE_ERROR      gst_core_error_quark ()
#define GST_STREAM_ERROR    gst_stream_error_quark ()

#define GST_ERROR_SYSTEM    ("system error: %s", g_strerror (errno))

GType gst_g_error_get_type (void);
gchar *gst_error_get_message (GQuark domain, gint code);
GQuark gst_stream_error_quark (void);
GQuark gst_core_error_quark (void);
GQuark gst_resource_error_quark (void);
GQuark gst_library_error_quark (void);

G_END_DECLS
#endif /* __GST_ERROR_H__ */
