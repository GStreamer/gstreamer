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

/**
 * SECTION:gstmessage
 * @short_description: Lightweight objects to signal the application of
 *                     pipeline events
 * @see_also: #GstBus, #GstMiniObject, #GstElement
 *
 * Messages are implemented as a subclass of #GstMiniObject with a generic
 * #GstStructure as the content. This allows for writing custom messages without
 * requiring an API change while allowing a wide range of different types
 * of messages.
 *
 * Messages are posted by objects in the pipeline and are passed to the
 * application using the #GstBus.

 * The basic use pattern of posting a message on a #GstBus is as follows:
 *
 * <example>
 * <title>Posting a #GstMessage</title>
 *   <programlisting>
 *    gst_bus_post (bus, gst_message_new_eos());
 *   </programlisting>
 * </example>
 *
 * A #GstElement usually posts messages on the bus provided by the parent
 * container using gst_element_post_message().
 *
 * Last reviewed on 2005-11-09 (0.9.4)
 */

#include <string.h>             /* memcpy */

#include "gst_private.h"
#include "gsterror.h"
#include "gstenumtypes.h"
#include "gstinfo.h"
#include "gstmessage.h"
#include "gsttaglist.h"
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

typedef struct
{
  gint type;
  gchar *name;
  GQuark quark;
} GstMessageQuarks;

static GstMessageQuarks message_quarks[] = {
  {GST_MESSAGE_UNKNOWN, "unknown", 0},
  {GST_MESSAGE_EOS, "eos", 0},
  {GST_MESSAGE_ERROR, "error", 0},
  {GST_MESSAGE_WARNING, "warning", 0},
  {GST_MESSAGE_INFO, "info", 0},
  {GST_MESSAGE_TAG, "tag", 0},
  {GST_MESSAGE_BUFFERING, "buffering", 0},
  {GST_MESSAGE_STATE_CHANGED, "state-changed", 0},
  {GST_MESSAGE_STATE_DIRTY, "state-dirty", 0},
  {GST_MESSAGE_STEP_DONE, "step-done", 0},
  {GST_MESSAGE_CLOCK_PROVIDE, "clock-provide", 0},
  {GST_MESSAGE_CLOCK_LOST, "clock-lost", 0},
  {GST_MESSAGE_NEW_CLOCK, "new-clock", 0},
  {GST_MESSAGE_STRUCTURE_CHANGE, "structure-change", 0},
  {GST_MESSAGE_STREAM_STATUS, "stream-status", 0},
  {GST_MESSAGE_APPLICATION, "application", 0},
  {GST_MESSAGE_ELEMENT, "element", 0},
  {GST_MESSAGE_SEGMENT_START, "segment-start", 0},
  {GST_MESSAGE_SEGMENT_DONE, "segment-done", 0},
  {GST_MESSAGE_DURATION, "duration", 0},
  {0, NULL, 0}
};

/**
 * gst_message_type_get_name:
 * @type: the message type
 *
 * Get a printable name for the given message type. Do not modify or free.
 *
 * Returns: a reference to the static name of the message.
 */
const gchar *
gst_message_type_get_name (GstMessageType type)
{
  gint i;

  for (i = 0; message_quarks[i].name; i++) {
    if (type == message_quarks[i].type)
      return message_quarks[i].name;
  }
  return "unknown";
}

/**
 * gst_message_type_to_quark:
 * @type: the message type
 *
 * Get the unique quark for the given message type.
 *
 * Returns: the quark associated with the message type
 */
GQuark
gst_message_type_to_quark (GstMessageType type)
{
  gint i;

  for (i = 0; message_quarks[i].name; i++) {
    if (type == message_quarks[i].type)
      return message_quarks[i].quark;
  }
  return 0;
}

GType
gst_message_get_type (void)
{
  static GType _gst_message_type;

  if (G_UNLIKELY (_gst_message_type == 0)) {
    gint i;
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

    for (i = 0; message_quarks[i].name; i++) {
      message_quarks[i].quark =
          g_quark_from_static_string (message_quarks[i].name);
    }
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

  GST_CAT_LOG (GST_CAT_MESSAGE, "new message %p", message);
  GST_MESSAGE_TIMESTAMP (message) = GST_CLOCK_TIME_NONE;
}

static void
gst_message_finalize (GstMessage * message)
{
  g_return_if_fail (message != NULL);

  GST_CAT_LOG (GST_CAT_MESSAGE, "finalize message %p", message);

  if (GST_MESSAGE_SRC (message)) {
    gst_object_unref (GST_MESSAGE_SRC (message));
    GST_MESSAGE_SRC (message) = NULL;
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

  GST_CAT_LOG (GST_CAT_MESSAGE, "copy message %p", message);

  copy = (GstMessage *) gst_mini_object_new (GST_TYPE_MESSAGE);

  /* FIXME, need to copy relevant data from the miniobject. */
  //memcpy (copy, message, sizeof (GstMessage));

  GST_MESSAGE_GET_LOCK (copy) = GST_MESSAGE_GET_LOCK (message);
  GST_MESSAGE_COND (copy) = GST_MESSAGE_COND (message);
  GST_MESSAGE_TYPE (copy) = GST_MESSAGE_TYPE (message);
  GST_MESSAGE_TIMESTAMP (copy) = GST_MESSAGE_TIMESTAMP (message);

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
 * gst_message_new_custom:
 * @type: The #GstMessageType to distinguish messages
 * @src: The object originating the message.
 * @structure: The structure for the message. The message will take ownership of
 * the structure.
 *
 * Create a new custom-typed message. This can be used for anything not
 * handled by other message-specific functions to pass a message to the
 * app. The structure field can be NULL.
 *
 * Returns: The new message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_custom (GstMessageType type, GstObject * src,
    GstStructure * structure)
{
  GstMessage *message;

  message = (GstMessage *) gst_mini_object_new (GST_TYPE_MESSAGE);

  GST_CAT_LOG (GST_CAT_MESSAGE, "source %s: creating new message %p %s",
      (src ? GST_OBJECT_NAME (src) : "NULL"), message,
      gst_message_type_get_name (type));

  message->type = type;

  if (src)
    gst_object_ref (src);
  message->src = src;

  if (structure) {
    gst_structure_set_parent_refcount (structure,
        &message->mini_object.refcount);
  }
  message->structure = structure;

  return message;
}

/**
 * gst_message_new_eos:
 * @src: The object originating the message.
 *
 * Create a new eos message. This message is generated and posted in
 * the sink elements of a GstBin. The bin will only forward the EOS
 * message to the application if all sinks have posted an EOS message.
 *
 * Returns: The new eos message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_eos (GstObject * src)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_EOS, src, NULL);

  return message;
}

/**
 * gst_message_new_error:
 * @src: The object originating the message.
 * @error: The GError for this message.
 * @debug: A debugging string for something or other.
 *
 * Create a new error message. The message will copy @error and
 * @debug. This message is posted by element when a fatal event
 * occured. The pipeline will probably (partially) stop.
 *
 * Returns: The new error message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_error (GstObject * src, GError * error, gchar * debug)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_ERROR, src,
      gst_structure_new ("GstMessageError", "gerror", GST_TYPE_G_ERROR, error,
          "debug", G_TYPE_STRING, debug, NULL));

  return message;
}

/**
 * gst_message_new_warning:
 * @src: The object originating the message.
 * @error: The GError for this message.
 * @debug: A debugging string for something or other.
 *
 * Create a new warning message. The message will make copies of @error and
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

  message = gst_message_new_custom (GST_MESSAGE_WARNING, src,
      gst_structure_new ("GstMessageWarning", "gerror", GST_TYPE_G_ERROR, error,
          "debug", G_TYPE_STRING, debug, NULL));

  return message;
}

/**
 * gst_message_new_tag:
 * @src: The object originating the message.
 * @tag_list: The tag list for the message.
 *
 * Create a new tag message. The message will take ownership of the tag list.
 * The message is posted by elements that discovered a new taglist.
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

  message =
      gst_message_new_custom (GST_MESSAGE_TAG, src, (GstStructure *) tag_list);

  return message;
}

/**
 * gst_message_new_state_changed:
 * @src: the object originating the message
 * @oldstate: the previous state
 * @newstate: the new (current) state
 * @pending: the pending (target) state
 *
 * Create a state change message. This message is posted whenever an element
 * changed its state.
 *
 * Returns: The new state change message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_state_changed (GstObject * src,
    GstState oldstate, GstState newstate, GstState pending)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_STATE_CHANGED, src,
      gst_structure_new ("GstMessageState",
          "old-state", GST_TYPE_STATE, (gint) oldstate,
          "new-state", GST_TYPE_STATE, (gint) newstate,
          "pending-state", GST_TYPE_STATE, (gint) pending, NULL));

  return message;
}

/**
 * gst_message_new_state_dirty:
 * @src: the object originating the message
 *
 * Create a state dirty message. This message is posted whenever an element
 * changed its state asynchronously and is used internally to update the
 * states of container objects.
 *
 * Returns: The new state dirty message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_state_dirty (GstObject * src)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_STATE_DIRTY, src, NULL);

  return message;
}

/**
 * gst_message_new_clock_provide:
 * @src: The object originating the message.
 * @clock: The clock it provides
 * @ready: TRUE if the sender can provide a clock
 *
 * Create a clock provide message. This message is posted whenever an
 * element is ready to provide a clock or lost its ability to provide
 * a clock (maybe because it paused or became EOS).
 *
 * This message is mainly used internally to manage the clock
 * selection.
 *
 * Returns: The new provide clock message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_clock_provide (GstObject * src, GstClock * clock,
    gboolean ready)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_CLOCK_PROVIDE, src,
      gst_structure_new ("GstMessageClockProvide",
          "clock", GST_TYPE_CLOCK, clock,
          "ready", G_TYPE_BOOLEAN, ready, NULL));

  return message;
}

/**
 * gst_message_new_clock_lost:
 * @src: The object originating the message.
 * @clock: the clock that was lost
 *
 * Create a clock lost message. This message is posted whenever the
 * clock is not valid anymore.
 *
 * If this message is posted by the pipeline, the pipeline will
 * select a new clock again when it goes to PLAYING. It might therefore
 * be needed to set the pipeline to PAUSED and PLAYING again.
 *
 * Returns: The new clock lost message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_clock_lost (GstObject * src, GstClock * clock)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_CLOCK_LOST, src,
      gst_structure_new ("GstMessageClockLost",
          "clock", GST_TYPE_CLOCK, clock, NULL));

  return message;
}

/**
 * gst_message_new_new_clock:
 * @src: The object originating the message.
 * @clock: the new selected clock
 *
 * Create a new clock message. This message is posted whenever the
 * pipeline selectes a new clock for the pipeline.
 *
 * Returns: The new new clock message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_new_clock (GstObject * src, GstClock * clock)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_NEW_CLOCK, src,
      gst_structure_new ("GstMessageNewClock",
          "clock", GST_TYPE_CLOCK, clock, NULL));

  return message;
}

/**
 * gst_message_new_segment_start:
 * @src: The object originating the message.
 * @format: The format of the position being played
 * @position: The position of the segment being played
 *
 * Create a new segment message. This message is posted by elements that
 * start playback of a segment as a result of a segment seek. This message
 * is not received by the application but is used for maintenance reasons in
 * container elements.
 *
 * Returns: The new segment start message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_segment_start (GstObject * src, GstFormat format,
    gint64 position)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_SEGMENT_START, src,
      gst_structure_new ("GstMessageSegmentStart",
          "format", GST_TYPE_FORMAT, format,
          "position", G_TYPE_INT64, position, NULL));

  return message;
}

/**
 * gst_message_new_segment_done:
 * @src: The object originating the message.
 * @format: The format of the position being done
 * @position: The position of the segment being done
 *
 * Create a new segment done message. This message is posted by elements that
 * finish playback of a segment as a result of a segment seek. This message
 * is received by the application after all elements that posted a segment_start
 * have posted the segment_done.
 *
 * Returns: The new segment done message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_segment_done (GstObject * src, GstFormat format,
    gint64 position)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_SEGMENT_DONE, src,
      gst_structure_new ("GstMessageSegmentDone",
          "format", GST_TYPE_FORMAT, format,
          "position", G_TYPE_INT64, position, NULL));

  return message;
}

/**
 * gst_message_new_application:
 * @src: The object originating the message.
 * @structure: The structure for the message. The message will take ownership of
 * the structure.
 *
 * Create a new application-typed message. GStreamer will never create these
 * messages; they are a gift from us to you. Enjoy.
 *
 * Returns: The new application message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_application (GstObject * src, GstStructure * structure)
{
  return gst_message_new_custom (GST_MESSAGE_APPLICATION, src, structure);
}

/**
 * gst_message_new_element:
 * @src: The object originating the message.
 * @structure: The structure for the message. The message will take ownership of
 * the structure.
 *
 * Create a new element-specific message. This is meant as a generic way of
 * allowing one-way communication from an element to an application, for example
 * "the firewire cable was unplugged". The format of the message should be
 * documented in the element's documentation. The structure field can be NULL.
 *
 * Returns: The new element message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_element (GstObject * src, GstStructure * structure)
{
  return gst_message_new_custom (GST_MESSAGE_ELEMENT, src, structure);
}

/**
 * gst_message_new_duration:
 * @src: The object originating the message.
 * @format: The format of the duration
 * @duration: The new duration 
 *
 * Create a new duration message. This message is posted by elements that
 * know the duration of a stream in a specific format. This message
 * is received by bins and is used to calculate the total duration of a
 * pipeline. Elements may post a duration message with a duration of
 * GST_CLOCK_TIME_NONE to indicate that the duration has changed and the 
 * cached duration should be discarded. The new duration can then be 
 * retrieved via a query.
 *
 * Returns: The new duration message.
 *
 * MT safe.
 */
GstMessage *
gst_message_new_duration (GstObject * src, GstFormat format, gint64 duration)
{
  GstMessage *message;

  message = gst_message_new_custom (GST_MESSAGE_DURATION, src,
      gst_structure_new ("GstMessageDuration",
          "format", GST_TYPE_FORMAT, format,
          "duration", G_TYPE_INT64, duration, NULL));

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
 * @tag_list: Return location for the tag-list.
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
 * gst_message_parse_state_changed:
 * @message: a valid #GstMessage of type GST_MESSAGE_STATE_CHANGED
 * @oldstate: the previous state
 * @newstate: the new (current) state
 * @pending: the pending (target) state
 *
 * Extracts the old and new states from the GstMessage.
 *
 * MT safe.
 */
void
gst_message_parse_state_changed (GstMessage * message,
    GstState * oldstate, GstState * newstate, GstState * pending)
{
  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STATE_CHANGED);

  if (oldstate)
    gst_structure_get_enum (message->structure, "old-state",
        GST_TYPE_STATE, (gint *) oldstate);
  if (newstate)
    gst_structure_get_enum (message->structure, "new-state",
        GST_TYPE_STATE, (gint *) newstate);
  if (pending)
    gst_structure_get_enum (message->structure, "pending-state",
        GST_TYPE_STATE, (gint *) pending);
}

/**
 * gst_message_parse_clock_provide:
 * @message: A valid #GstMessage of type GST_MESSAGE_CLOCK_PROVIDE.
 * @clock: A pointer to  hold a clock object.
 * @ready: A pointer to hold the ready flag.
 *
 * Extracts the clock and ready flag from the GstMessage.
 * The clock object returned remains valid until the message is freed.
 *
 * MT safe.
 */
void
gst_message_parse_clock_provide (GstMessage * message, GstClock ** clock,
    gboolean * ready)
{
  const GValue *clock_gvalue;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_CLOCK_PROVIDE);

  clock_gvalue = gst_structure_get_value (message->structure, "clock");
  g_return_if_fail (clock_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (clock_gvalue) == GST_TYPE_CLOCK);

  if (ready)
    gst_structure_get_boolean (message->structure, "ready", ready);
  if (clock)
    *clock = (GstClock *) g_value_get_object (clock_gvalue);
}

/**
 * gst_message_parse_clock_lost:
 * @message: A valid #GstMessage of type GST_MESSAGE_CLOCK_LOST.
 * @clock: A pointer to hold the lost clock
 *
 * Extracts the lost clock from the GstMessage.
 * The clock object returned remains valid until the message is freed.
 *
 * MT safe.
 */
void
gst_message_parse_clock_lost (GstMessage * message, GstClock ** clock)
{
  const GValue *clock_gvalue;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_CLOCK_LOST);

  clock_gvalue = gst_structure_get_value (message->structure, "clock");
  g_return_if_fail (clock_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (clock_gvalue) == GST_TYPE_CLOCK);

  if (clock)
    *clock = (GstClock *) g_value_get_object (clock_gvalue);
}

/**
 * gst_message_parse_new_clock:
 * @message: A valid #GstMessage of type GST_MESSAGE_NEW_CLOCK.
 * @clock: A pointer to hold the selected new clock
 *
 * Extracts the new clock from the GstMessage.
 * The clock object returned remains valid until the message is freed.
 *
 * MT safe.
 */
void
gst_message_parse_new_clock (GstMessage * message, GstClock ** clock)
{
  const GValue *clock_gvalue;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_NEW_CLOCK);

  clock_gvalue = gst_structure_get_value (message->structure, "clock");
  g_return_if_fail (clock_gvalue != NULL);
  g_return_if_fail (G_VALUE_TYPE (clock_gvalue) == GST_TYPE_CLOCK);

  if (clock)
    *clock = (GstClock *) g_value_get_object (clock_gvalue);
}

/**
 * gst_message_parse_error:
 * @message: A valid #GstMessage of type GST_MESSAGE_ERROR.
 * @gerror: Location for the GError
 * @debug: Location for the debug message
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
  g_return_if_fail (G_VALUE_TYPE (error_gvalue) == GST_TYPE_G_ERROR);

  error_val = (GError *) g_value_get_boxed (error_gvalue);
  if (error_val)
    *gerror = g_error_copy (error_val);
  else
    *gerror = NULL;
  *debug = g_strdup (gst_structure_get_string (message->structure, "debug"));
}

/**
 * gst_message_parse_warning:
 * @message: A valid #GstMessage of type GST_MESSAGE_WARNING.
 * @gerror: Location for the GError
 * @debug: Location for the debug message
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
  g_return_if_fail (G_VALUE_TYPE (error_gvalue) == GST_TYPE_G_ERROR);

  error_val = (GError *) g_value_get_boxed (error_gvalue);
  if (error_val)
    *gerror = g_error_copy (error_val);
  else
    *gerror = NULL;

  *debug = g_strdup (gst_structure_get_string (message->structure, "debug"));
}

/**
 * gst_message_parse_segment_start:
 * @message: A valid #GstMessage of type GST_MESSAGE_SEGMENT_START.
 * @format: Result location for the format
 * @position: Result location for the position
 *
 * Extracts the position and format from the segment start message.
 *
 * MT safe.
 */
void
gst_message_parse_segment_start (GstMessage * message, GstFormat * format,
    gint64 * position)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_SEGMENT_START);

  structure = gst_message_get_structure (message);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (position)
    *position =
        g_value_get_int64 (gst_structure_get_value (structure, "position"));
}

/**
 * gst_message_parse_segment_done:
 * @message: A valid #GstMessage of type GST_MESSAGE_SEGMENT_DONE.
 * @format: Result location for the format
 * @position: Result location for the position
 *
 * Extracts the position and format from the segment start message.
 *
 * MT safe.
 */
void
gst_message_parse_segment_done (GstMessage * message, GstFormat * format,
    gint64 * position)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_SEGMENT_DONE);

  structure = gst_message_get_structure (message);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (position)
    *position =
        g_value_get_int64 (gst_structure_get_value (structure, "position"));
}

/**
 * gst_message_parse_duration:
 * @message: A valid #GstMessage of type GST_MESSAGE_DURATION.
 * @format: Result location for the format
 * @duration: Result location for the duration
 *
 * Extracts the duration and format from the duration message. The duration
 * might be GST_CLOCK_TIME_NONE, which indicates that the duration has
 * changed. Applications should always use a query to retrieve the duration
 * of a pipeline.
 *
 * MT safe.
 */
void
gst_message_parse_duration (GstMessage * message, GstFormat * format,
    gint64 * duration)
{
  const GstStructure *structure;

  g_return_if_fail (GST_IS_MESSAGE (message));
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_DURATION);

  structure = gst_message_get_structure (message);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (duration)
    *duration =
        g_value_get_int64 (gst_structure_get_value (structure, "duration"));
}
