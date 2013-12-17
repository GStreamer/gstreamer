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
#include "gstvaapivideocontext.h"

/* Default debug category is from the subclass */
#define GST_CAT_DEFAULT (plugin->debug_category)

/* GstImplementsInterface interface */
#if !GST_CHECK_VERSION(1,0,0)
static gboolean
implements_interface_supported (GstImplementsInterface * iface, GType type)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (iface);

  if (type == GST_TYPE_VIDEO_CONTEXT)
    return TRUE;
  return GST_VAAPI_PLUGIN_BASE_GET_CLASS (plugin)->has_interface (plugin, type);
}

static void
implements_interface_init (GstImplementsInterfaceClass * iface)
{
  iface->supported = implements_interface_supported;
}
#endif

/* GstVideoContext interface */
#if GST_CHECK_VERSION(1,1,0)
static void
plugin_set_context (GstElement * element, GstContext * context)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);
  GstVaapiDisplay *display = NULL;

  if (gst_vaapi_video_context_get_display (context, &display)) {
    GST_INFO_OBJECT (element, "set display %p", display);
    gst_vaapi_display_replace (&plugin->display, display);
    gst_vaapi_display_unref (display);
  }
}
#else
static void
plugin_set_context (GstVideoContext * context, const gchar * type,
    const GValue * value)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (context);

  gst_vaapi_set_display (type, value, &plugin->display);
}

static void
video_context_interface_init (GstVideoContextInterface * iface)
{
  iface->set_context = plugin_set_context;
}

#define GstVideoContextClass GstVideoContextInterface
#endif

void
gst_vaapi_plugin_base_init_interfaces (GType g_define_type_id)
{
#if !GST_CHECK_VERSION(1,0,0)
  G_IMPLEMENT_INTERFACE (GST_TYPE_IMPLEMENTS_INTERFACE,
      implements_interface_init);
#endif
#if !GST_CHECK_VERSION(1,1,0)
  G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_CONTEXT, video_context_interface_init);
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

#if GST_CHECK_VERSION(1,1,0)
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  element_class->set_context = GST_DEBUG_FUNCPTR (plugin_set_context);
#endif
}

void
gst_vaapi_plugin_base_init (GstVaapiPluginBase * plugin,
    GstDebugCategory * debug_category)
{
  plugin->debug_category = debug_category;
  plugin->display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
  plugin->display_type_req = GST_VAAPI_DISPLAY_TYPE_ANY;

  /* sink pad */
  plugin->sinkpad = gst_element_get_static_pad (GST_ELEMENT (plugin), "sink");
  plugin->sinkpad_query = GST_PAD_QUERYFUNC (plugin->sinkpad);
  gst_video_info_init (&plugin->sinkpad_info);

  /* src pad */
  if (!(GST_OBJECT_FLAGS (plugin) & GST_ELEMENT_FLAG_SINK)) {
    plugin->srcpad = gst_element_get_static_pad (GST_ELEMENT (plugin), "src");
    plugin->srcpad_query = GST_PAD_QUERYFUNC (plugin->srcpad);
  }
  gst_video_info_init (&plugin->srcpad_info);
}

void
gst_vaapi_plugin_base_finalize (GstVaapiPluginBase * plugin)
{
  gst_vaapi_plugin_base_close (plugin);
  if (plugin->sinkpad)
    gst_object_unref (plugin->sinkpad);
  if (plugin->srcpad)
    gst_object_unref (plugin->srcpad);
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

  gst_caps_replace (&plugin->sinkpad_caps, NULL);
  plugin->sinkpad_caps_changed = FALSE;
  gst_video_info_init (&plugin->sinkpad_info);

  gst_caps_replace (&plugin->srcpad_caps, NULL);
  plugin->srcpad_caps_changed = FALSE;
  gst_video_info_init (&plugin->srcpad_info);
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

/**
 * gst_vaapi_plugin_base_set_caps:
 * @plugin: a #GstVaapiPluginBase
 * @incaps: the sink pad (input) caps
 * @outcaps: the src pad (output) caps
 *
 * Notifies the base plugin object of the new input and output caps,
 * obtained from the subclass.
 *
 * Returns: %TRUE if the update of caps was successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_set_caps (GstVaapiPluginBase * plugin, GstCaps * incaps,
    GstCaps * outcaps)
{
  if (incaps && incaps != plugin->sinkpad_caps) {
    gst_caps_replace (&plugin->sinkpad_caps, incaps);
    if (!gst_video_info_from_caps (&plugin->sinkpad_info, incaps))
      return FALSE;
    plugin->sinkpad_caps_changed = TRUE;
  }

  if (outcaps && outcaps != plugin->srcpad_caps) {
    gst_caps_replace (&plugin->srcpad_caps, outcaps);
    if (!gst_video_info_from_caps (&plugin->srcpad_info, outcaps))
      return FALSE;
    plugin->srcpad_caps_changed = TRUE;
  }
  return TRUE;
}
