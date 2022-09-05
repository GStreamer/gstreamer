/* GStreamer
 * Copyright (C) 2013 Wim Taymans <wim.taymans at gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:rtsp-context
 * @short_description: A client request context
 * @see_also: #GstRTSPServer, #GstRTSPClient
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtsp-context.h"

G_DEFINE_POINTER_TYPE (GstRTSPContext, gst_rtsp_context);

static GPrivate current_context;

/**
 * gst_rtsp_context_get_current: (skip):
 *
 * Get the current #GstRTSPContext. This object is retrieved from the
 * current thread that is handling the request for a client.
 *
 * Returns: a #GstRTSPContext
 */
GstRTSPContext *
gst_rtsp_context_get_current (void)
{
  GSList *l;

  l = g_private_get (&current_context);
  if (l == NULL)
    return NULL;

  return (GstRTSPContext *) (l->data);

}

/**
 * gst_rtsp_context_push_current:
 * @ctx: a #GstRTSPContext
 *
 * Pushes @ctx onto the context stack. The current
 * context can then be received using gst_rtsp_context_get_current().
 **/
void
gst_rtsp_context_push_current (GstRTSPContext * ctx)
{
  GSList *l;

  g_return_if_fail (ctx != NULL);

  l = g_private_get (&current_context);
  l = g_slist_prepend (l, ctx);
  g_private_set (&current_context, l);
}

/**
 * gst_rtsp_context_pop_current:
 * @ctx: a #GstRTSPContext
 *
 * Pops @ctx off the context stack (verifying that @ctx
 * is on the top of the stack).
 **/
void
gst_rtsp_context_pop_current (GstRTSPContext * ctx)
{
  GSList *l;

  l = g_private_get (&current_context);

  g_return_if_fail (l != NULL);
  g_return_if_fail (l->data == ctx);

  l = g_slist_delete_link (l, l);
  g_private_set (&current_context, l);
}

/**
 * gst_rtsp_context_set_token:
 * @ctx: a #GstRTSPContext
 * @token: a #GstRTSPToken
 *
 * Set the token for @ctx.
 *
 * Since: 1.22
 **/
void
gst_rtsp_context_set_token (GstRTSPContext * ctx, GstRTSPToken * token)
{
  g_return_if_fail (ctx != NULL);
  g_return_if_fail (ctx == gst_rtsp_context_get_current ());
  g_return_if_fail (GST_IS_RTSP_TOKEN (token));

  if (ctx->token != NULL)
    gst_rtsp_token_unref (ctx->token);

  gst_rtsp_token_ref (token);
  ctx->token = token;
}
