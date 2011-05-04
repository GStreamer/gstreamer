/* GStreamer
 * Copyright (C) 2011 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstcontext.c: GstContext subsystem
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
 * SECTION:gstcontext
 * @short_description: Structure containing events describing the context
 *                     for buffers in a pipeline
 * @see_also: #GstPad, #GstBuffer
 *
 * Last reviewed on 2011-05-4 (0.11.0)
 */


#include "gst_private.h"

#include "gstinfo.h"
#include "gstcontext.h"
#include "gstutils.h"
#include "gstquark.h"

struct _GstContext
{
  GstMiniObject mini_object;

  /*< private > */
  GstEvent *events[GST_EVENT_MAX_STICKY];

  gpointer _gst_reserved[GST_PADDING];
};

static GType _gst_context_type = 0;

GType
gst_context_get_type (void)
{
  if (G_UNLIKELY (_gst_context_type == 0)) {
    _gst_context_type = gst_mini_object_register ("GstContext");
  }
  return _gst_context_type;
}

static void
_gst_context_free (GstContext * context)
{
  GST_LOG ("freeing context %p", context);

  g_return_if_fail (context != NULL);
  g_return_if_fail (GST_IS_CONTEXT (context));

  gst_context_clear (context);

  g_slice_free1 (GST_MINI_OBJECT_SIZE (context), context);
}

static void gst_context_init (GstContext * context, gsize size);

static GstContext *
_gst_context_copy (GstContext * context)
{
  GstContext *copy;
  guint i;

  copy = g_slice_new0 (GstContext);

  gst_context_init (copy, sizeof (GstContext));

  for (i = 0; i < GST_EVENT_MAX_STICKY; i++)
    gst_event_replace (&copy->events[i], context->events[i]);

  return copy;
}

static void
gst_context_init (GstContext * context, gsize size)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (context), _gst_context_type,
      size);

  context->mini_object.copy = (GstMiniObjectCopyFunction) _gst_context_copy;
  context->mini_object.free = (GstMiniObjectFreeFunction) _gst_context_free;
}

/**
 * gst_context_new:
 *
 * Create a new #GstContext object that can be used to manage events.
 *
 * Returns: (transfer full): a new #GstContext
 */
GstContext *
gst_context_new (void)
{
  GstContext *context;

  context = g_slice_new0 (GstContext);

  GST_DEBUG ("creating new context %p", context);

  gst_context_init (context, sizeof (GstContext));

  return context;
}

/**
 * gst_context_update:
 * @context: a #GstContext
 * @event: a #GstEvent
 *
 * Update @context with @event. The context must be writable.
 */
void
gst_context_update (GstContext * context, GstEvent * event)
{
  guint idx;

  g_return_if_fail (context != NULL);
  g_return_if_fail (gst_context_is_writable (context));

  idx = GST_EVENT_STICKY_IDX (event);

  GST_LOG ("storing event %s at index %u", GST_EVENT_TYPE_NAME (event), idx);

  gst_event_replace (&context->events[idx], event);
}

/**
 * gst_context_get:
 * @context: a #GstContext
 * @type: a #GstEventType
 *
 * Get the event of @type from @context.
 *
 * Returns: the last #GstEvent of @type that was updated on @context. This
 * function returns NULL when there is no event with the given type.
 */
GstEvent *
gst_context_get (GstContext * context, GstEventType type)
{
  guint idx;
  GstEvent *event = NULL;

  g_return_val_if_fail (context != NULL, NULL);

  idx = GST_EVENT_STICKY_IDX_TYPE (type);

  if ((event = context->events[idx]))
    gst_event_ref (event);

  return event;
}

/**
 * gst_context_clear:
 * @context: a #GstContext
 *
 * Clear all stored events in @context
 */
void
gst_context_clear (GstContext * context)
{
  guint i;

  for (i = 0; i < GST_EVENT_MAX_STICKY; i++)
    gst_event_replace (&context->events[i], NULL);
}

/**
 * gst_context_foreach:
 * @context: a #GstContext
 * @func: a #GFunc
 * @user_data: user data
 *
 * Call @func with the non NULL event and @user_data.
 */
void
gst_context_foreach (GstContext * context, GFunc func, gpointer user_data)
{
  guint i;
  GstEvent *event;

  for (i = 0; i < GST_EVENT_MAX_STICKY; i++)
    if ((event = context->events[i]))
      func (event, user_data);
}
