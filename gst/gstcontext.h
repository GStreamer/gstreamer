/* GStreamer
 * Copyright (C) 2011 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstcontext.h: Header for GstContext subsystem
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


#ifndef __GST_CONTEXT_H__
#define __GST_CONTEXT_H__

#include <gst/gstminiobject.h>
#include <gst/gstevent.h>

G_BEGIN_DECLS

#define GST_CONTEXT_TRACE_NAME    "GstContext"

typedef struct _GstContext GstContext;

#define GST_TYPE_CONTEXT                (gst_context_get_type())
#define GST_IS_CONTEXT(obj)             (GST_IS_MINI_OBJECT_TYPE (obj, GST_TYPE_CONTEXT))
#define GST_CONTEXT(obj)                ((GstContext *)(obj))
#define GST_CONTEXT_CAST(obj)           ((GstContext *)(obj))


/**
 * gst_context_is_writable:
 * @ctx: a #GstContext
 *
 * Tests if you can safely update @ctx with new events.
 */
#define         gst_context_is_writable(ctx)     gst_mini_object_is_writable (GST_MINI_OBJECT_CAST (ctx))

/**
 * gst_context_make_writable:
 * @ctx: (transfer full): a #GstContext
 *
 * Makes a writable context from the given context. If the source context is
 * already writable, this will simply return the same context. A copy will
 * otherwise be made using gst_context_copy().
 *
 * Returns: (transfer full): a writable context which may or may not be the
 *     same as @ctx
 */
#define         gst_context_make_writable(ctx)   GST_CONTEXT_CAST (gst_mini_object_make_writable (GST_MINI_OBJECT_CAST (ctx)))

/**
 * gst_context_replace:
 * @old_ctx: (inout) (transfer full): pointer to a pointer to a #GstContext
 *     to be replaced.
 * @new_ctx: (allow-none) (transfer none): pointer to a #GstContext that will
 *     replace the context pointed to by @old_context.
 *
 * Modifies a pointer to a #GstContext to point to a different #GstContext. The
 * modification is done atomically (so this is useful for ensuring thread safety
 * in some cases), and the reference counts are updated appropriately (the old
 * context is unreffed, the new one is reffed).
 *
 * Either @new_context or the #GstContext pointed to by @old_context may be NULL.
 */
#define         gst_context_replace(old_ctx,new_ctx) \
    gst_mini_object_replace ((GstMiniObject **)(old_ctx), GST_MINI_OBJECT_CAST (new_ctx))

GType           gst_context_get_type              (void);

/* refcounting */
/**
 * gst_context_ref:
 * @context: The context to refcount
 *
 * Increase the refcount of this context.
 *
 * Returns: (transfer full): @context (for convenience when doing assignments)
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstContext * gst_context_ref (GstContext * context);
#endif

static inline GstContext *
gst_context_ref (GstContext * context)
{
  return (GstContext *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (context));
}

/**
 * gst_context_unref:
 * @context: (transfer full): the context to refcount
 *
 * Decrease the refcount of an context, freeing it if the refcount reaches 0.
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC void gst_context_unref (GstContext * context);
#endif

static inline void
gst_context_unref (GstContext * context)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (context));
}

/* copy context */
/**
 * gst_context_copy:
 * @context: The context to copy
 *
 * Copy the context using the context specific copy function.
 *
 * Returns: (transfer full): the new context
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstContext * gst_context_copy (const GstContext * context);
#endif

static inline GstContext *
gst_context_copy (const GstContext * context)
{
  return GST_CONTEXT_CAST (gst_mini_object_copy (GST_MINI_OBJECT_CONST_CAST (context)));
}

GstContext *   gst_context_new         (void);

/* updating and setting events */
void           gst_context_update      (GstContext *context, GstEvent *event);
GstEvent *     gst_context_get         (GstContext *context, GstEventType type);

void           gst_context_clear       (GstContext *context);

/* foreach */
void           gst_context_foreach     (GstContext *context, GFunc func, gpointer user_data);

G_END_DECLS

#endif /* __GST_CONTEXT_H__ */
