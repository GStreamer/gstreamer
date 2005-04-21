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
  GST_CAT_INFO (GST_CAT_GST_INIT, "init messages");

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

  if (message->structure) {
    copy->structure = gst_structure_copy (message->structure);
    gst_structure_set_parent_refcount (copy->structure,
        &GST_DATA_REFCOUNT (message));
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

  if (message->structure) {
    gst_structure_set_parent_refcount (message->structure, NULL);
    gst_structure_free (message->structure);
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
    GST_MESSAGE_SRC (message) = gst_object_ref (src);
  } else {
    GST_MESSAGE_SRC (message) = NULL;
  }
  message->structure = NULL;

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
 * @src: The object originating the message.
 * @error: The GError for this message.
 * @debug: A debugging string for something or other.
 *
 * Create a new error message. The message will take ownership of @error and
 * @debug.
 *
 * Returns: The new error message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_error (GstObject * src, GError * error, gchar * debug)
{
  GstMessage *message;
  GstStructure *s;

  message = gst_message_new (GST_MESSAGE_ERROR, src);
  s = gst_structure_new ("GstMessageError", "gerror", G_TYPE_POINTER, error,
      "debug", G_TYPE_STRING, debug, NULL);
  gst_structure_set_parent_refcount (s, &GST_DATA_REFCOUNT (message));
  message->structure = s;

  return message;
}

/**
 * gst_message_new_warning:
 * @src: The object originating the message.
 * @error: The GError for this message.
 * @debug: A debugging string for something or other.
 *
 * Create a new warning message. The message will take ownership of @error and
 * @debug.
 *
 * Returns: The new warning message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_warning (GstObject * src, GError * error, gchar * debug)
{
  GstMessage *message;
  GstStructure *s;

  message = gst_message_new (GST_MESSAGE_WARNING, src);
  s = gst_structure_new ("GstMessageWarning", "gerror", G_TYPE_POINTER, error,
      "debug", G_TYPE_STRING, debug, NULL);
  gst_structure_set_parent_refcount (s, &GST_DATA_REFCOUNT (message));
  message->structure = s;

  return message;
}

/**
 * gst_message_new_tag:
 * @src: The object originating the message.
 * @tag_list: The tag list for the message.
 *
 * Create a new tag message. The message will take ownership of the tag list.
 *
 * Returns: The new tag message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_tag (GstObject * src, GstTagList * tag_list)
{
  GstMessage *message;

  g_return_val_if_fail (GST_IS_STRUCTURE (tag_list), NULL);

  message = gst_message_new (GST_MESSAGE_TAG, src);
  gst_structure_set_parent_refcount (tag_list, &GST_DATA_REFCOUNT (message));
  message->structure = tag_list;

  return message;
}

/**
 * gst_message_new_state_change:
 * @src: The object originating the message.
 * @old: The previous state.
 * @new: The new (current) state.
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
  GstStructure *s;

  message = gst_message_new (GST_MESSAGE_STATE_CHANGED, src);

  s = gst_structure_new ("GstMessageError", "old-state", G_TYPE_INT, (gint) old,
      "new-state", G_TYPE_INT, (gint) new, NULL);
  gst_structure_set_parent_refcount (s, &GST_DATA_REFCOUNT (message));
  message->structure = s;

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

  g_return_val_if_fail (GST_IS_STRUCTURE (structure), NULL);

  message = gst_message_new (GST_MESSAGE_APPLICATION, NULL);
  gst_structure_set_parent_refcount (structure, &GST_DATA_REFCOUNT (message));
  message->structure = structure;

  return message;
}

/**
 * gst_message_get_structure:
 * @message: The #GstMessage.
 *
 * Access the structure of the message.
 *
 * Returns: The structure of the message. The structure is still
 * owned by the message, which means that you should not free it and 
 * that the pointer becomes invalid when you free the message.
 *
 * MT safe.
 */
const GstStructure *
gst_message_get_structure (GstMessage * message)
{
  g_return_val_if_fail (GST_IS_MESSAGE (message), NULL);

  return message->structure;
}

/**
 * gst_message_parse_tag:
 * @message: A valid #GstMessage of type GST_MESSAGE_TAG.
 *
 * Extracts the tag list from the GstMessage. The tag list returned in the
 * output argument is a copy; the caller must free it when done.
 *
 * MT safe.
 */
void
gst_message_parse_tag (GstMessage * message, GstTagList ** tag_list)
{
  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_TAG);

  *tag_list = (GstTagList *) gst_structure_copy (message->structure);
}

/**
 * gst_message_parse_tag:
 * @message: A valid #GstMessage of type GST_MESSAGE_STATE_CHANGED.
 *
 * Extracts the old and new states from the GstMessage.
 *
 * MT safe.
 */
void
gst_message_parse_state_changed (GstMessage * message, GstElementState * old,
    GstElementState * new)
{
  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STATE_CHANGED);

  if (!gst_structure_get_int (message->structure, "old-state", (gint *) old))
    g_assert_not_reached ();
  if (!gst_structure_get_int (message->structure, "new-state", (gint *) new))
    g_assert_not_reached ();
}

/**
 * gst_message_parse_error:
 * @message: A valid #GstMessage of type GST_MESSAGE_ERROR.
 *
 * Extracts the GError and debug string from the GstMessage. The values returned
 * in the output arguments are copies; the caller must free them when done.
 *
 * MT safe.
 */
void
gst_message_parse_error (GstMessage * message, GError ** gerror, gchar ** debug)
{
  const GValue *error_gvalue;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR);

  error_gvalue = gst_structure_get_value (message->structure, "gerror");
  g_return_if_fail (error_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (error_gvalue) == G_TYPE_POINTER);

  *gerror = g_error_copy (g_value_get_pointer (error_gvalue));
  *debug = g_strdup (gst_structure_get_string (message->structure, "debug"));
}

/**
 * gst_message_parse_warning:
 * @message: A valid #GstMessage of type GST_MESSAGE_WARNING.
 *
 * Extracts the GError and debug string from the GstMessage. The values returned
 * in the output arguments are copies; the caller must free them when done.
 *
 * MT safe.
 */
void
gst_message_parse_warning (GstMessage * message, GError ** gerror,
    gchar ** debug)
{
  const GValue *error_gvalue;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_WARNING);

  error_gvalue = gst_structure_get_value (message->structure, "gerror");
  g_return_if_fail (error_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (error_gvalue) == G_TYPE_POINTER);

  *gerror = g_error_copy (g_value_get_pointer (error_gvalue));
  *debug = g_strdup (gst_structure_get_string (message->structure, "debug"));
}
