/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstmessage.c: GstMessage subsystem
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


#include <string.h>             /* memcpy */

#include "gst_private.h"
#include "gstdata_private.h"
#include "gstinfo.h"
#include "gstmemchunk.h"
#include "gstmessage.h"
#include "gsttag.h"

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE */
#include "gsttrace.h"
static GstAllocTrace *_message_trace;
#endif

static GstMemChunk *chunk;

/* #define MEMPROF */

GType _gst_message_type;

void
_gst_message_initialize (void)
{
  /* register the type */
  _gst_message_type = g_boxed_type_register_static ("GstMessage",
      (GBoxedCopyFunc) gst_data_copy, (GBoxedFreeFunc) gst_data_unref);

#ifndef GST_DISABLE_TRACE
  _message_trace = gst_alloc_trace_register (GST_MESSAGE_TRACE_NAME);
#endif

  chunk = gst_mem_chunk_new ("GstMessageChunk", sizeof (GstMessage),
      sizeof (GstMessage) * 50, 0);
}

static GstMessage *
_gst_message_copy (GstMessage * message)
{
  GstMessage *copy;

  GST_CAT_INFO (GST_CAT_MESSAGE, "copy message %p", message);

  copy = gst_mem_chunk_alloc (chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_message_trace, copy);
#endif

  memcpy (copy, message, sizeof (GstMessage));
  if (GST_MESSAGE_SRC (copy)) {
    gst_object_ref (GST_MESSAGE_SRC (copy));
  }

  /* FIXME copy/ref additional fields */
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_TAG:
      copy->message_data.structure.structure =
          gst_tag_list_copy ((GstTagList *) message->message_data.structure.
          structure);
      break;
    default:
      break;
  }

  return copy;
}

static void
_gst_message_free (GstMessage * message)
{
  GST_CAT_INFO (GST_CAT_MESSAGE, "freeing message %p", message);

  if (GST_MESSAGE_SRC (message)) {
    gst_object_unref (GST_MESSAGE_SRC (message));
  }
  if (message->lock) {
    GST_MESSAGE_LOCK (message);
    GST_MESSAGE_SIGNAL (message);
    GST_MESSAGE_UNLOCK (message);
  }
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_error_free (GST_MESSAGE_ERROR_GERROR (message));
      g_free (GST_MESSAGE_ERROR_DEBUG (message));
      break;
    case GST_MESSAGE_TAG:
      if (GST_IS_TAG_LIST (message->message_data.tag.list)) {
        gst_tag_list_free (message->message_data.tag.list);
      } else {
        g_warning ("tag message %p didn't contain a valid tag list!", message);
        GST_ERROR ("tag message %p didn't contain a valid tag list!", message);
      }
      break;
    case GST_MESSAGE_APPLICATION:
      if (message->message_data.structure.structure) {
        gst_structure_free (message->message_data.structure.structure);
        message->message_data.structure.structure = NULL;
      }
      break;
    default:
      break;
  }
  _GST_DATA_DISPOSE (GST_DATA (message));
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_message_trace, message);
#endif
  gst_mem_chunk_free (chunk, message);
}

GType
gst_message_get_type (void)
{
  return _gst_message_type;
}

/**
 * gst_message_new:
 * @type: The type of the new message
 *
 * Allocate a new message of the given type.
 *
 * Returns: A new message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new (GstMessageType type, GstObject * src)
{
  GstMessage *message;

  message = gst_mem_chunk_alloc0 (chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_message_trace, message);
#endif

  GST_CAT_INFO (GST_CAT_MESSAGE, "creating new message %p %d", message, type);

  _GST_DATA_INIT (GST_DATA (message),
      _gst_message_type,
      0,
      (GstDataFreeFunction) _gst_message_free,
      (GstDataCopyFunction) _gst_message_copy);

  GST_MESSAGE_TYPE (message) = type;
  GST_MESSAGE_TIMESTAMP (message) = G_GINT64_CONSTANT (0);
  if (src) {
    gst_object_ref (src);
    GST_MESSAGE_SRC (message) = src;
  }

  return message;
}


/**
 * gst_message_new_eos:
 *
 * Create a new eos message.
 *
 * Returns: The new eos message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_eos (GstObject * src)
{
  GstMessage *message;

  message = gst_message_new (GST_MESSAGE_EOS, src);

  return message;
}

/**
 * gst_message_new_error:
 *
 * Create a new error message.
 *
 * Returns: The new error message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_error (GstObject * src, GError * error, gchar * debug)
{
  GstMessage *message;

  message = gst_message_new (GST_MESSAGE_ERROR, src);
  GST_MESSAGE_ERROR_GERROR (message) = error;
  GST_MESSAGE_ERROR_DEBUG (message) = debug;

  return message;
}

/**
 * gst_message_new_warning:
 *
 * Create a new warning message.
 *
 * Returns: The new warning message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_warning (GstObject * src, GError * error, gchar * debug)
{
  GstMessage *message;

  message = gst_message_new (GST_MESSAGE_WARNING, src);
  GST_MESSAGE_WARNING_GERROR (message) = error;
  GST_MESSAGE_WARNING_DEBUG (message) = debug;

  return message;
}

/**
 * gst_message_new_tag:
 *
 * Create a new tag message.
 *
 * Returns: The new tag message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_tag (GstObject * src, GstTagList * tag_list)
{
  GstMessage *message;

  message = gst_message_new (GST_MESSAGE_TAG, src);
  GST_MESSAGE_TAG_LIST (message) = tag_list;

  return message;
}

/**
 * gst_message_new_state_change:
 *
 * Create a state change message.
 *
 * Returns: The new state change message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_state_changed (GstObject * src, GstElementState old,
    GstElementState new)
{
  GstMessage *message;

  message = gst_message_new (GST_MESSAGE_STATE_CHANGED, src);
  message->message_data.state_changed.old = old;
  message->message_data.state_changed.new = new;

  return message;
}

/**
 * gst_message_new_application:
 * @structure: The structure for the message. The message will take ownership of
 * the structure.
 *
 * Create a new application-specific message. These messages can be used by
 * application-specific plugins to pass data to the app.
 *
 * Returns: The new message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_application (GstStructure * structure)
{
  GstMessage *message;

  message = gst_message_new (GST_MESSAGE_APPLICATION, NULL);
  message->message_data.structure.structure = structure;

  return message;
}
