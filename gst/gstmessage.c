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
#include "gstinfo.h"
#include "gstmemchunk.h"
#include "gstmessage.h"
#include "gsttag.h"
#include "gstutils.h"


static void gst_message_init (GTypeInstance * instance, gpointer g_class);
static void gst_message_class_init (gpointer g_class, gpointer class_data);
static void gst_message_finalize (GstMessage * message);
static GstMessage *_gst_message_copy (GstMessage * message);


void
_gst_message_initialize (void)
{
  gpointer ptr;

  GST_CAT_INFO (GST_CAT_GST_INIT, "init messages");

  gst_message_get_type ();

  /* the GstMiniObject types need to be class_ref'd once before it can be
   * done from multiple threads;
   * see http://bugzilla.gnome.org/show_bug.cgi?id=304551 */
  ptr = g_type_class_ref (GST_TYPE_MESSAGE);
  g_type_class_unref (ptr);
}

GType
gst_message_get_type (void)
{
  static GType _gst_message_type;

  if (G_UNLIKELY (_gst_message_type == 0)) {
    static const GTypeInfo message_info = {
      sizeof (GstMessageClass),
      NULL,
      NULL,
      gst_message_class_init,
      NULL,
      NULL,
      sizeof (GstMessage),
      0,
      gst_message_init,
      NULL
    };

    _gst_message_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstMessage", &message_info, 0);
  }
  return _gst_message_type;
}

static void
gst_message_class_init (gpointer g_class, gpointer class_data)
{
  GstMessageClass *message_class = GST_MESSAGE_CLASS (g_class);

  message_class->mini_object_class.copy =
      (GstMiniObjectCopyFunction) _gst_message_copy;
  message_class->mini_object_class.finalize =
      (GstMiniObjectFinalizeFunction) gst_message_finalize;
}

static void
gst_message_init (GTypeInstance * instance, gpointer g_class)
{
  GstMessage *message = GST_MESSAGE (instance);

  message->timestamp = GST_CLOCK_TIME_NONE;
}

static void
gst_message_finalize (GstMessage * message)
{
  g_return_if_fail (message != NULL);

  GST_CAT_INFO (GST_CAT_MESSAGE, "finalize message %p", message);

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
}

static GstMessage *
_gst_message_copy (GstMessage * message)
{
  GstMessage *copy;

  GST_CAT_INFO (GST_CAT_MESSAGE, "copy message %p", message);

  copy = (GstMessage *) gst_mini_object_new (GST_TYPE_MESSAGE);

  /* FIXME */
  //memcpy (copy, message, sizeof (GstMessage));

  copy->lock = message->lock;
  copy->cond = message->cond;
  copy->type = message->type;
  copy->timestamp = message->timestamp;

  if (GST_MESSAGE_SRC (message)) {
    GST_MESSAGE_SRC (copy) = gst_object_ref (GST_MESSAGE_SRC (message));
  }

  if (message->structure) {
    copy->structure = gst_structure_copy (message->structure);
    gst_structure_set_parent_refcount (copy->structure,
        &message->mini_object.refcount);
  }

  return copy;
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
static GstMessage *
gst_message_new (GstMessageType type, GstObject * src)
{
  GstMessage *message;

  message = (GstMessage *) gst_mini_object_new (GST_TYPE_MESSAGE);

  GST_CAT_INFO (GST_CAT_MESSAGE, "creating new message %p %d", message, type);

  message->type = type;
  if (src) {
    message->src = gst_object_ref (src);
  } else {
    message->src = NULL;
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
  gst_structure_set_parent_refcount (s, &message->mini_object.refcount);
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
  gst_structure_set_parent_refcount (s, &message->mini_object.refcount);
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
  gst_structure_set_parent_refcount (tag_list, &message->mini_object.refcount);
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
  gst_structure_set_parent_refcount (s, &message->mini_object.refcount);
  message->structure = s;

  return message;
}

/**
 * gst_message_new_application:
 * @src: The object originating the message.
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
gst_message_new_application (GstObject * src, GstStructure * structure)
{
  GstMessage *message;

  g_return_val_if_fail (GST_IS_STRUCTURE (structure), NULL);

  message = gst_message_new (GST_MESSAGE_APPLICATION, src);
  gst_structure_set_parent_refcount (structure, &message->mini_object.refcount);
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
  GError *error_val;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR);

  error_gvalue = gst_structure_get_value (message->structure, "gerror");
  g_return_if_fail (error_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (error_gvalue) == G_TYPE_POINTER);

  error_val = g_value_get_pointer (error_gvalue);
  if (error_val)
    *gerror = g_error_copy (error_val);
  else
    *gerror = NULL;
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
  GError *error_val;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_WARNING);

  error_gvalue = gst_structure_get_value (message->structure, "gerror");
  g_return_if_fail (error_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (error_gvalue) == G_TYPE_POINTER);

  error_val = g_value_get_pointer (error_gvalue);
  if (error_val)
    *gerror = g_error_copy (error_val);
  else
    *gerror = NULL;

  *debug = g_strdup (gst_structure_get_string (message->structure, "debug"));
}
