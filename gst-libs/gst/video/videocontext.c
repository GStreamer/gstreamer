/* GStreamer
 *
 * Copyright (C) 2011 Intel
 * Copyright (C) 2011 Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
 *
 * video-context.c: Video Context interface and helpers
 *
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "videocontext.h"

/**
 * SECTION:gstvideocontext
 * @short_description: Interface to handle video library context
 *
 * The Video Context interface enable sharing video context (such as display
 * name, X11 Display, VA-API Display, etc) between neighboor elements and the
 * application.
 * <note>
 *   The GstVideoContext interface is unstable API and may change in future.
 *   One can define GST_USE_UNSTABLE_API to acknowledge and avoid this warning.
 * </note>
 *
 * <refsect2>
 * <title>For Element</title>
 * <para>
 *   This interface shall be implement by group of elements that need to share
 *   a specific video context (like VDPAU, LibVA, OpenGL elements) or by video
 *   sink in order to let the application select appropriate display information
 *   (like the X11 display name) when those sink are auto-plugged.
 * </para>
 * <para>
 *   Along with implementing the interface, elements will need to query
 *   neighboor elements or send message to the application when preparing
 *   the context (see gst_video_context_prepare()). They also need to reply
 *   to the neighboors element queries, so the context can be shared without
 *   the application help.
 * </para>
 * <para>
 *   Elements that are guarantied to have both upstream and downstream
 *   neighboors element implementing the #GstVideoContext (like the gloverlay
 *   element in gst-plugins-opengl) is not required to also implement the
 *   interface. Relying on neighboors query shall be sufficient (see
 *   gst_video_context_run_query()).
 * </para>
 * <para>
 *   The query is an application query with a structure name set to
 *   "prepare-video-context" and an array of supported video context types set
 *   in the field named "types". This query shall be send downstream and
 *   upstream, iterating the pads in order to find neighboors regardless of a
 *   static (sink to src) or a dynamic (src to sink) activation. Element should
 *   used the helpers method gst_video_context_prepare() (or
 *   gst_video_context_run_query() if no GstVideoContext interface) to
 *   correctly execute the query . The result is set using the query helper
 *   functions, the structures fields name being "video-context-type" as
 *   string and "video-context" as a #GValue.
 * </para>
 * <para>
 *   If the query is not handled by any neighboor, the element should ask the
 *   application using the "prepare-video-context" message. The application
 *   may then use the interface to set the video context information. If no
 *   context was set, the element shall create one using default configuration.
 *   Elements with multiple src or sink pad shall implement proper locking to
 *   prevent the race of parallel queries being replied.
 * </para>
 * <para>
 *   Well known video-context are: "x11-display-name" a string representing the
 *   X11 display to use, "x11-display" the X11 Display structure, "va-display",
 *   the VADisplay structure and more.
 * </para>
 * </refsect2>
 *
 * <refsect2>
 * <title>For Application</title>
 * <para>
 *   In the case there is no neighboor element with video context to share,
 *   the element will first turn toward the application, by sending a
 *   "prepare-video-context" message. This message is sent along with a list
 *   of supported display types. The application can optionally reply to this
 *   message by calling appropriate setter through the #GstVideoContext
 *   interface. If the application supports more then one video context type,
 *   it should choose the first one to occure in the supported list. It's
 *   important to remember that the message is delivered from the streaming
 *   thread, and appropriate locking should be considered. If the application
 *   does not have a video context to share, the element will simply allocate
 *   one base on default settings. Usually, only applications using OpenGL
 *   base sink, or running on special X11 display need to share a video context.
 *   <note>
 *     Applications sharing X11 Display structure should always initialize the
 *     X11 threading support using XInitThreads() as GStreamer will need to
 *     manipulate the display from a separeate threads.
 *   </note>
 * </para>
 * <refsect2>
 * <title>Example using ClutterVideoGstVideoSink</title>
 * <para>
 *   This example is for user of ClutterGstVideoSink element, the
 *   ClutterGstPlayer object transparently handle this.
 *  </para>
 * |[
 * #if CLUTTER_WINDOWING_X11
 * static GstBusSyncReply
 * on_sync_message (GstBus * bus, GstMessage * message, gpointer user_data)
 * {
 *   Display *display = user_data;
 *   GstVideoContext *context;
 *   const gchar **types;
 *
 *   if (gst_video_context_message_parse_prepare (message, &types, &context)) {
 *     gint i;
 *
 *     for (i = 0; types[i]; i++) {
 *
 *       if (!strcmp(types[i], "x11-display")) {
 *         gst_video_context_set_context_pointer (context, "x11-display", display);
 *       }
 *       else if (!strcmp(types[i], "x11-display-name")) {
 *         gst_video_context_set_context_string (context, "x11-display-name",
 *             DisplayString (display));
 *       } else {
 *         continue;
 *       }
 *
 *       gst_message_unref (message);
 *       return GST_BUS_DROP;
 *     }
 *   }
 *
 *   return GST_BUS_PASS;
 * }
 * #endif
 *
 * gint
 * main (gint argc, gchar **argv)
 * {
 *   GstBin *pipeline;
 *   GstBus *bus;
 *
 *   ...
 *
 *   bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
 *
 *   #if CLUTTER_WINDOWING_X11
 *   gst_bus_set_sync_handler (priv->bus, on_sync_message,
 *       clutter_x11_get_default_display ());
 *   #endif
 *
 *   gst_object_unref (GST_OBJECT (priv->bus));
 *
 *   ...
 * }
 * ]|
 * </refsect2>
 * </refsect2>
 */

G_DEFINE_INTERFACE (GstVideoContext, gst_video_context_iface, G_TYPE_INVALID);

static inline GstStructure *
gst_video_context_new_structure (const gchar ** types)
{
  return gst_structure_new ("prepare-video-context",
      "types", G_TYPE_STRV, types, NULL);
}

static gboolean
gst_video_context_pad_query (gpointer item, GValue * value, gpointer user_data)
{
  GstPad *pad = GST_PAD (item);
  GstQuery *query = user_data;
  gboolean res;

  res = gst_pad_peer_query (pad, query);
  gst_object_unref (pad);

  if (res) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  return TRUE;
}

static void
gst_video_context_iface_default_init (GstVideoContextInterface * iface)
{
  /* default virtual functions */
  iface->set_context = NULL;
}

/**
 * gst_video_context_set_context:
 * @context: an element implementing #GstVideoContext
 * @type: the type of display being set
 * @value: a #GValue containing the context
 *
 * This is a wrapper for the set_context() virtual method. It is suggested to
 * use one of the helpers to avoid having to manipulate #GValue
 */
void
gst_video_context_set_context (GstVideoContext * context, const gchar * type,
    const GValue * value)
{
  g_return_if_fail (GST_IS_VIDEO_CONTEXT (context));
  g_return_if_fail (GST_VIDEO_CONTEXT_GET_IFACE (context)->set_context);

  GST_VIDEO_CONTEXT_GET_IFACE (context)->set_context (context, type, value);
}

/**
 * gst_video_context_set_context_string:
 * @context: an element implementing #GstVideoContext
 * @type: the type of display being set
 * @string: a string representing the video context
 *
 * This helper is commonly used for setting video context represented by a
 * string like the X11 display name ("x11-display-name")/
 */
void
gst_video_context_set_context_string (GstVideoContext * context,
    const gchar * type, const gchar * string)
{
  GValue value = { 0 };
  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, string);
  gst_video_context_set_context (context, type, &value);
  g_value_unset (&value);
}

/**
 * gst_video_context_set_context_pointer:
 * @context: an element implementing #GstVideoContext
 * @type: the type of display being set
 * @pointer: a pointer to the video context
 *
 * This helper is used for setting video context using a pointer, typically to
 * a structure like the X11 Display ("x11-display") or the VADisplay
 * ("vaapi-display").
 * <note>
 *   Users of X11 Display should ensure that XInitThreads() was called before
 *   opening the display.
 * </note>
 */
void
gst_video_context_set_context_pointer (GstVideoContext * context,
    const gchar * type, gpointer pointer)
{
  GValue value = { 0 };
  g_value_init (&value, G_TYPE_POINTER);
  g_value_set_pointer (&value, pointer);
  gst_video_context_set_context (context, type, &value);
  g_value_unset (&value);
}

/**
 * gst_video_context_set_context_object:
 * @context: an element implementing #GstVideoContext
 * @type: the type of display being set
 * @object: a #GObject resenting the display
 *
 * This is for video context that are #GObject, this helper allow taking
 * benifit of the #GObject refcounting. It is particularly handy for element
 * to have refcounting as the order in which element will stop using the
 * display is not defined.
 */
void
gst_video_context_set_context_object (GstVideoContext * context,
    const gchar * type, GObject * object)
{
  GValue value = { 0 };
  g_return_if_fail (G_IS_OBJECT (object));
  g_value_init (&value, G_TYPE_OBJECT);
  g_value_set_object (&value, object);
  gst_video_context_set_context (context, type, &value);
  g_value_unset (&value);
}

/**
 * gst_video_context_prepare:
 * @context: an element implementing #GstVideoContext interface
 * @types: an array of supported types, prefered first
 *
 * This method run "prepare-video-context" custom query dowstream, and
 * upstream. If * the query has a reply, it sets the context value using
 * gst_video_context_set_context(). Otherwise, it sends a
 * "prepare-video-context" message to the application. The element can then
 * continue video context initialization.
 */
void
gst_video_context_prepare (GstVideoContext * context, const gchar ** types)
{
  GstQuery *query = gst_video_context_query_new (types);

  /* Check neighborhood, if found call GstVideoContext */
  if (gst_video_context_run_query (GST_ELEMENT (context), query)) {
    const gchar *type = NULL;
    const GValue *value;
    gst_video_context_query_parse_value (query, &type, &value);
    gst_video_context_set_context (context, type, value);
  } else {
    /* If no neighbor replyed, query the application */
    GstMessage *message;
    GstStructure *structure;

    structure = gst_video_context_new_structure (types);
    message = gst_message_new_element (GST_OBJECT (context), structure);
    gst_element_post_message (GST_ELEMENT (context), message);
  }

  gst_query_unref (query);
}

/**
 * gst_video_context_message_parse_prepare:
 * @message: a #GstMessage
 * @types: return value for supported types
 * @context: return value for the element the implements #GstVideoContext
 *
 * This helper shall be used by application to simply handling of the
 * "prepare-video-context" message.
 *
 * Rerturns: #FALSE is the message was not valid "prepare-video-context"
 *           element message, otherwise #TRUE with @types and @context set.
 */
gboolean
gst_video_context_message_parse_prepare (GstMessage * message,
    const gchar *** types, GstVideoContext ** context)
{
  GstObject *src = GST_MESSAGE_SRC (message);
  const GstStructure *structure;
  const GValue *value;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return FALSE;

  if (!gst_structure_has_name (message->structure, "prepare-video-context"))
    return FALSE;

  if (!GST_IS_VIDEO_CONTEXT (src))
    return FALSE;

  structure = gst_message_get_structure (message);
  value = gst_structure_get_value (structure, "types");

  if (!G_VALUE_HOLDS (value, G_TYPE_STRV))
    return FALSE;

  if (types)
    *types = g_value_get_boxed (value);

  if (context)
    *context = GST_VIDEO_CONTEXT (src);

  return TRUE;
}

/**
 * gst_video_context_query_new:
 * @types: a string array of video context types
 *
 * Create a new custom #GstQuery with structure name "prepare-video-context".
 */
GstQuery *
gst_video_context_query_new (const gchar ** types)
{
  GstStructure *structure = gst_video_context_new_structure (types);
  return gst_query_new_application (GST_QUERY_CUSTOM, structure);
}

/**
 * gst_video_context_run_query:
 * @element: a #GstElement
 * @query: a #GstQuery
 *
 * This helper runs the query on each downstream, then upstream pads in an
 * element. This is called by gst_video_context_prepare(). This method is only
 * used directly within elements that are required to have two neighboors
 * elements with appropriate video context. This would be the case of
 * specialized filters that only manipulate non-raw buffers (e.g.
 * gldeinterlace). Those elements do not have to implement #GstVideoContext
 * interface.
 */
gboolean
gst_video_context_run_query (GstElement * element, GstQuery * query)
{
  GstIterator *it;
  GstIteratorFoldFunction func = gst_video_context_pad_query;
  GValue res = { 0 };

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask downstream neighbor (mainly static pipeline case) */
  it = gst_element_iterate_src_pads (element);

  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);

  /* If none, ask upstream neighbor (auto-plugged case) */
  if (!g_value_get_boolean (&res)) {
    it = gst_element_iterate_sink_pads (element);

    while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
      gst_iterator_resync (it);

    gst_iterator_free (it);
  }

  return g_value_get_boolean (&res);
}

/**
 * gst_video_context_query_get_supported_types:
 * @query: a #GstQuery
 *
 * Returns: An array of supported video context types
 */
const gchar **
gst_video_context_query_get_supported_types (GstQuery * query)
{
  GstStructure *structure = gst_query_get_structure (query);
  const GValue *value = gst_structure_get_value (structure, "types");

  if (G_VALUE_HOLDS (value, G_TYPE_STRV))
    return g_value_get_boxed (value);

  return NULL;
}

/**
 * gst_video_context_query_parse_value:
 * @query: a #GstQuery
 * @type: return video context type
 * @value: return video context #GValue
 *
 * Helper to extract the video context type and value from a #GstQuery.
 */
void
gst_video_context_query_parse_value (GstQuery * query, const gchar ** type,
    const GValue ** value)
{
  GstStructure *structure = gst_query_get_structure (query);

  if (type)
    *type = gst_structure_get_string (structure, "video-context-type");

  if (value)
    *value = gst_structure_get_value (structure, "video-context");
}

/**
 * gst_video_context_query_set_value:
 * @query: a #GstQuery
 * @type: the video context type
 * @value: a #GValue set with video context
 *
 * Helper to set the video context as a #GValue inside the #GstQuery.
 */
void
gst_video_context_query_set_value (GstQuery * query, const gchar * type,
    GValue * value)
{
  GstStructure *structure = gst_query_get_structure (query);
  gst_structure_set (structure, "video-context-type", G_TYPE_STRING, type,
      "video-context", G_TYPE_VALUE, value, NULL);
}

/**
 * gst_video_context_query_set_string:
 * @query: a #GstQuery
 * @type: the video context type
 * @value: a string representing the video context
 *
 * Helper to set the video context as a string inside the #GstQuery.
 */
void
gst_video_context_query_set_string (GstQuery * query, const gchar * type,
    const gchar * value)
{
  GstStructure *structure = gst_query_get_structure (query);
  gst_structure_set (structure, "video-context-type", G_TYPE_STRING, type,
      "video-context", G_TYPE_STRING, value, NULL);
}

/**
 * gst_video_context_query_set_pointer:
 * @query: a #GstQuery
 * @type: the video context type
 * @value: a #gpointer representing the video context
 *
 * Helper to set the video context as a #gpointer inside the #GstQuery.
 */
void
gst_video_context_query_set_pointer (GstQuery * query, const gchar * type,
    gpointer value)
{
  GstStructure *structure = gst_query_get_structure (query);
  gst_structure_set (structure, "video-context-type", G_TYPE_STRING, type,
      "video-context", G_TYPE_POINTER, value, NULL);
}

/**
 * gst_video_context_query_set_object:
 * @query: a #GstQuery
 * @type: the video context type
 * @value: a #GObject representing the video context
 *
 * Helper to set the video context as a #GObject inside the #GstQuery.
 */
void
gst_video_context_query_set_object (GstQuery * query, const gchar * type,
    GObject * value)
{
  GstStructure *structure = gst_query_get_structure (query);
  gst_structure_set (structure, "video-context-type", G_TYPE_STRING, type,
      "video-context", G_TYPE_OBJECT, value, NULL);
}
