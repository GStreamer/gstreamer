/* GStreamer Editing Services
 *
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
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
#ifndef _GES_VALIDATE_
#define _GES_VALIDATE_

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

gboolean
ges_validate_activate (GstPipeline *pipeline, const gchar *scenario, gboolean *needs_set_state);

gint
ges_validate_clean (GstPipeline *pipeline);

void ges_validate_handle_request_state_change (GstMessage *message, GMainLoop *mainloop);

G_END_DECLS

#endif  /* _GES_VALIDATE */
