/*
 *  gstvaapipluginbase.c - Base GStreamer VA-API Plugin element
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include "gstvaapipluginbase.h"
#include "gstvaapipluginutil.h"

/* Default debug category is from the subclass */
#define GST_CAT_DEFAULT (plugin->debug_category)

/* GstImplementsInterface interface */
#if !GST_CHECK_VERSION(1,0,0)
static gboolean
implements_interface_supported (GstImplementsInterface * iface, GType type)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (iface);

  return GST_VAAPI_PLUGIN_BASE_GET_CLASS (plugin)->has_interface (plugin, type);
}

static void
implements_interface_init (GstImplementsInterfaceClass * iface)
{
  iface->supported = implements_interface_supported;
}
#endif

void
gst_vaapi_plugin_base_init_interfaces (GType g_define_type_id)
{
#if !GST_CHECK_VERSION(1,0,0)
  G_IMPLEMENT_INTERFACE (GST_TYPE_IMPLEMENTS_INTERFACE,
      implements_interface_init);
#endif
}

static gboolean
default_has_interface (GstVaapiPluginBase * plugin, GType type)
{
  return FALSE;
}

static void
default_display_changed (GstVaapiPluginBase * plugin)
{
}

void
gst_vaapi_plugin_base_class_init (GstVaapiPluginBaseClass * klass)
{
  klass->has_interface = default_has_interface;
  klass->display_changed = default_display_changed;
}

void
gst_vaapi_plugin_base_init (GstVaapiPluginBase * plugin,
    GstDebugCategory * debug_category)
{
  plugin->debug_category = debug_category;
  plugin->display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
  plugin->display_type_req = GST_VAAPI_DISPLAY_TYPE_ANY;
}

void
gst_vaapi_plugin_base_finalize (GstVaapiPluginBase * plugin)
{
  gst_vaapi_plugin_base_close (plugin);

  gst_debug_category_free (plugin->debug_category);
}

/**
 * gst_vaapi_plugin_base_open:
 * @plugin: a #GstVaapiPluginBase
 *
 * Allocates any internal resources needed for correct operation from
 * the subclass.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_open (GstVaapiPluginBase * plugin)
{
  return TRUE;
}

/**
 * gst_vaapi_plugin_base_close:
 * @plugin: a #GstVaapiPluginBase
 *
 * Deallocates all internal resources that were allocated so
 * far. i.e. put the base plugin object into a clean state.
 */
void
gst_vaapi_plugin_base_close (GstVaapiPluginBase * plugin)
{
  gst_vaapi_display_replace (&plugin->display, NULL);
}

/**
 * gst_vaapi_plugin_base_set_display_type:
 * @plugin: a #GstVaapiPluginBase
 * @display_type: the new request #GstVaapiDisplayType
 *
 * Requests a new display type. The change is effective at the next
 * call to gst_vaapi_plugin_base_ensure_display().
 */
void
gst_vaapi_plugin_base_set_display_type (GstVaapiPluginBase * plugin,
    GstVaapiDisplayType display_type)
{
  plugin->display_type_req = display_type;
}

/**
 * gst_vaapi_plugin_base_ensure_display:
 * @plugin: a #GstVaapiPluginBase
 *
 * Ensures the display stored in @plugin complies with the requested
 * display type constraints.
 *
 * Returns: %TRUE if the display was created to match the requested
 *   type, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_ensure_display (GstVaapiPluginBase * plugin)
{
  if (plugin->display
      && gst_vaapi_display_type_is_compatible (plugin->display_type,
          plugin->display_type_req))
    return TRUE;
  gst_vaapi_display_replace (&plugin->display, NULL);

  if (!gst_vaapi_ensure_display (plugin, plugin->display_type_req))
    return FALSE;
  plugin->display_type = gst_vaapi_display_get_display_type (plugin->display);

  GST_VAAPI_PLUGIN_BASE_GET_CLASS (plugin)->display_changed (plugin);
  return TRUE;
}
