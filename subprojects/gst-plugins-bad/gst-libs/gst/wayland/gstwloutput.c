/* GStreamer Wayland Library
 *
 * Copyright (C) 2025 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwloutput.h"
#include "gstwloutput-private.h"

struct _GstWlOutput
{
  GObject parent;

  struct wl_output *output;
  guint32 global_id;

  gchar *name;
  gchar *description;

  gint scale_factor;

  struct
  {
    gint x;
    gint y;
    gint physical_width;
    gint physical_height;
    enum wl_output_subpixel subpixel;
    gchar *make;
    gchar *model;
    enum wl_output_transform transform;
  } geometry;

  struct
  {
    guint flags;
    gint width;
    gint height;
    gint refresh;
  } mode;
};

G_DEFINE_TYPE (GstWlOutput, gst_wl_output, G_TYPE_OBJECT);

static void
gst_wl_output_finalize (GObject * object)
{
  GstWlOutput *self = GST_WL_OUTPUT (object);
  g_free (self->name);
  g_free (self->description);
  g_free (self->geometry.make);
  g_free (self->geometry.model);

  wl_output_destroy (self->output);

  G_OBJECT_CLASS (gst_wl_output_parent_class)->finalize (object);
}

static void
gst_wl_output_init (GstWlOutput * self)
{
}

static void
gst_wl_output_class_init (GstWlOutputClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_output_finalize;
}

/**
 * gst_wl_output_new: (skip):
 * @output: A wl_output proxy
 *
 * Returns: (transfer full): A #GstWlOutput object
 *
 * Since: 1.28
 */
GstWlOutput *
gst_wl_output_new (struct wl_output *output, guint32 global_id)
{
  GstWlOutput *self = GST_WL_OUTPUT (g_object_new (GST_TYPE_WL_OUTPUT, NULL));

  self->output = output;
  self->global_id = global_id;

  return self;
}

/**
 * gst_wl_output_set_name: (skip):
 * @self: the #GstWlOutput
 * @name: the name to set
 *
 * Saves the name of the #GstWlOutput.
 *
 * Since: 1.28
 */
void
gst_wl_output_set_name (GstWlOutput * self, const gchar * name)
{
  g_free (self->name);
  self->name = g_strdup (name);
}

/**
 * gst_wl_output_set_description: (skip):
 * @self: the #GstWlOutput
 * @description: the name to set
 *
 * Saves the description of the #GstWlOutput.
 *
 * Since: 1.28
 */
void
gst_wl_output_set_description (GstWlOutput * self, const gchar * description)
{
  g_free (self->description);
  self->description = g_strdup (description);
}

/**
 * gst_wl_output_set_scale: (skip):
 * @self: the #GstWlOutput
 * @scale: the name to set
 *
 * Saves the scale of the #GstWlOutput.
 *
 * Since: 1.28
 */
void
gst_wl_output_set_scale (GstWlOutput * self, gint scale_factor)
{
  self->scale_factor = scale_factor;
}

/**
 * gst_wl_output_set_geometry: (skip):
 * @self: the #GstWlOutput
 * @x: the x coordinate
 * @y: the y coordinate
 * @physical_width: the width in mm
 * @physical_height: the height in mm
 * @subpixel: type of pixels
 * @make: the brand of the output
 * @model: the specific model
 * @transform: the transform used to render to this output
 *
 * Saves all the parameters that are part of the geometry callback for the
 * output.
 *
 * Since: 1.28
 */
void
gst_wl_output_set_geometry (GstWlOutput * self, gint x, gint y,
    gint physical_width, gint physical_height,
    enum wl_output_subpixel subpixel, const gchar * make, const gchar * model,
    enum wl_output_transform transform)
{
  g_free (self->geometry.make);
  g_free (self->geometry.model);
  self->geometry.x = x;
  self->geometry.y = y;
  self->geometry.physical_width = physical_width;
  self->geometry.physical_height = physical_height;
  self->geometry.subpixel = subpixel;
  self->geometry.make = g_strdup (make);
  self->geometry.model = g_strdup (model);
  self->geometry.transform = transform;
}

/**
* gst_wl_output_set_mode: (skip):
* @self: A #GstWlOutput
* @flags: enum wl_output_mode
* @width: the width in pixels
* @height: the height in pixels
* @refresh: the refresh rate in mHz
*
* Saves all the paramters that are part of the mode callback for the output. The
* compositor may call this multiple times but must send the current mode last.
* Only the last mode is kept.
*
* Since: 1.28
*/
void
gst_wl_output_set_mode (GstWlOutput * self, guint flags, gint width,
    gint height, gint refresh)
{
  self->mode.flags = flags;
  self->mode.width = width;
  self->mode.height = height;
  self->mode.refresh = refresh;
}

/**
* gst_wl_output_get_wl_output:
* @self: A #GstWlOutput
*
* Returns: the struct wl_output pointer
*
* Since: 1.28
*/
struct wl_output *
gst_wl_output_get_wl_output (GstWlOutput * self)
{
  return self->output;
}

/**
* gst_wl_output_get_id:
* @self: A #GstWlOutput
*
* Returns: the Waylandnd object global id
*
* Since: 1.28
*/
guint32
gst_wl_output_get_id (GstWlOutput * self)
{
  return wl_proxy_get_id ((struct wl_proxy *) self->output);
}

/**
* gst_wl_output_get_name:
* @self: A #GstWlOutput
*
* Returns: the output name
*
* Since: 1.28
*/
const gchar *
gst_wl_output_get_name (GstWlOutput * self)
{
  return self->name;
}

/**
* gst_wl_output_get_description:
* @self: A #GstWlOutput
*
* Returns: the output description
*
* Since: 1.28
*/
const gchar *
gst_wl_output_get_decscription (GstWlOutput * self)
{
  return self->description;
}

/**
* gst_wl_output_get_make:
* @self: A #GstWlOutput
*
* Returns: the output make
*
* Since: 1.28
*/
const gchar *
gst_wl_output_get_make (GstWlOutput * self)
{
  return self->geometry.make;
}

/**
* gst_wl_output_get_model:
* @self: A #GstWlOutput
*
* Returns: the output model
*
* Since: 1.28
*/
const gchar *
gst_wl_output_get_model (GstWlOutput * self)
{
  return self->geometry.model;
}

/**
* gst_wl_output_get_scale:
* @self: A #GstWlOutput
*
* The output scale from the output protocol is rounded up to the next integer
* value. For accurate scaling factor use the fractional scaling protocol, which
* is queried per surface rather then per output.
*
* Returns: the output scale factor
*
* Since: 1.28
*/
gint
gst_wl_output_get_scale (GstWlOutput * self)
{
  return self->scale_factor;
}

/**
* gst_wl_output_get_x:
* @self: A #GstWlOutput
*
* Returns: the output virtual x coordinate
*
* Since: 1.28
*/
gint
gst_wl_output_get_x (GstWlOutput * self)
{
  return self->geometry.x;
}

/**
* gst_wl_output_get_y:
* @self: A #GstWlOutput
*
* Returns: the output virtual y coordinate
*
* Since: 1.28
*/
gint
gst_wl_output_get_y (GstWlOutput * self)
{
  return self->geometry.y;
}

/**
* gst_wl_output_get_physical_width:
* @self: A #GstWlOutput
*
* Returns: the output physical width
*
* Since: 1.28
*/
gint
gst_wl_output_get_physical_width (GstWlOutput * self)
{
  return self->geometry.physical_width;
}

/**
* gst_wl_output_get_physical_height:
* @self: A #GstWlOutput
*
* Returns: the output physical height
*
* Since: 1.28
*/
gint
gst_wl_output_get_physical_height (GstWlOutput * self)
{
  return self->geometry.physical_height;
}

/**
* gst_wl_output_get_subpixel:
* @self: A #GstWlOutput
*
* Returns: the output subpixel type (see enum wl_output_subpixel)
*
* Since: 1.28
*/
enum wl_output_subpixel
gst_wl_output_get_subpixel (GstWlOutput * self)
{
  return self->geometry.subpixel;
}

/**
* gst_wl_output_get_transform:
* @self: A #GstWlOutput
*
* Returns: the output transform (see enum wl_output_transfor)
*
* Since: 1.28
*/
enum wl_output_transform
gst_wl_output_get_transform (GstWlOutput * self)
{
  return self->geometry.transform;
}

/**
* gst_wl_output_get_mode_flags:
* @self: A #GstWlOutput
*
* Returns: the output mode flags (see enum wl_output_mode)
*
* Since: 1.28
*/
guint
gst_wl_output_get_mode_flags (GstWlOutput * self)
{
  return self->mode.flags;
}

/**
* gst_wl_output_get_width:
* @self: A #GstWlOutput
*
* Returns: the output width in pixels
*
* Since: 1.28
*/
gint
gst_wl_output_get_width (GstWlOutput * self)
{
  return self->mode.width;
}

/**
* gst_wl_output_get_height:
* @self: A #GstWlOutput
*
* Returns: the output height in pixels
*
* Since: 1.28
*/
gint
gst_wl_output_get_height (GstWlOutput * self)
{
  return self->mode.height;
}

/**
* gst_wl_output_get_refresh:
* @self: A #GstWlOutput
*
* Returns: the output refresh in mHz
*
* Since: 1.28
*/
gint
gst_wl_output_get_refresh (GstWlOutput * self)
{
  return self->mode.refresh;
}
