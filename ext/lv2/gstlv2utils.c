/* GStreamer
 * Copyright (C) 2016 Thibault Saunier <thibault.saunier@collabora.com>
 *               2016 Stefan Sauer <ensonic@users.sf.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "string.h"

#include "gstlv2.h"
#include "gstlv2utils.h"

#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

GST_DEBUG_CATEGORY_EXTERN (lv2_debug);
#define GST_CAT_DEFAULT lv2_debug

/* host features */

/* - log extension */

static int
lv2_log_printf (LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  gst_debug_log_valist (lv2_debug, GST_LEVEL_INFO, "", "", 0, NULL, fmt, ap);
  va_end (ap);
  return 1;
}

static int
lv2_log_vprintf (LV2_Log_Handle handle, LV2_URID type,
    const char *fmt, va_list ap)
{
  gst_debug_log_valist (lv2_debug, GST_LEVEL_INFO, "", "", 0, NULL, fmt, ap);
  return 1;
}

static LV2_Log_Log lv2_log = {
  /* handle = */ NULL, lv2_log_printf, lv2_log_vprintf
};


static const LV2_Feature lv2_log_feature = { LV2_LOG__log, &lv2_log };

/* - urid map/unmap extension */

static LV2_URID
lv2_urid_map (LV2_URID_Map_Handle handle, const char *uri)
{
  return (LV2_URID) g_quark_from_string (uri);
}

static const char *
lv2_urid_unmap (LV2_URID_Unmap_Handle handle, LV2_URID urid)
{
  return g_quark_to_string ((GQuark) urid);
}

static LV2_URID_Map lv2_map = {
  /* handle = */ NULL, lv2_urid_map
};

static LV2_URID_Unmap lv2_unmap = {
  /* handle = */ NULL, lv2_urid_unmap
};

static const LV2_Feature lv2_map_feature = { LV2_URID__map, &lv2_map };
static const LV2_Feature lv2_unmap_feature = { LV2_URID__unmap, &lv2_unmap };

/* feature list */

static const LV2_Feature *lv2_features[] = {
  &lv2_log_feature,
  &lv2_map_feature,
  &lv2_unmap_feature,
  NULL
};

gboolean
gst_lv2_check_required_features (const LilvPlugin * lv2plugin)
{
  LilvNodes *required_features = lilv_plugin_get_required_features (lv2plugin);
  if (required_features) {
    LilvIter *i;
    gint j;
    gboolean missing = FALSE;

    for (i = lilv_nodes_begin (required_features);
        !lilv_nodes_is_end (required_features, i);
        i = lilv_nodes_next (required_features, i)) {
      const LilvNode *required_feature = lilv_nodes_get (required_features, i);
      const char *required_feature_uri = lilv_node_as_uri (required_feature);
      missing = TRUE;

      for (j = 0; lv2_features[j]; j++) {
        if (!strcmp (lv2_features[j]->URI, required_feature_uri)) {
          missing = FALSE;
          break;
        }
      }
      if (missing) {
        GST_FIXME ("lv2 plugin %s needs host feature: %s",
            lilv_node_as_uri (lilv_plugin_get_uri (lv2plugin)),
            required_feature_uri);
        break;
      }
    }
    lilv_nodes_free (required_features);
    return (!missing);
  }
  return TRUE;
}

/* api helpers */

void
gst_lv2_init (GstLV2 * lv2, GstLV2Class * lv2_class)
{
  lv2->klass = lv2_class;

  lv2->instance = NULL;
  lv2->activated = FALSE;

  lv2->ports.control.in = g_new0 (gfloat, lv2_class->control_in_ports->len);
  lv2->ports.control.out = g_new0 (gfloat, lv2_class->control_out_ports->len);
}

void
gst_lv2_finalize (GstLV2 * lv2)
{
  g_free (lv2->ports.control.in);
  g_free (lv2->ports.control.out);
}

gboolean
gst_lv2_setup (GstLV2 * lv2, unsigned long rate)
{
  GstLV2Class *lv2_class = lv2->klass;
  gint i;

  if (lv2->instance)
    lilv_instance_free (lv2->instance);

  if (!(lv2->instance =
          lilv_plugin_instantiate (lv2_class->plugin, rate, lv2_features)))
    return FALSE;

  /* connect the control ports */
  for (i = 0; i < lv2_class->control_in_ports->len; i++)
    lilv_instance_connect_port (lv2->instance,
        g_array_index (lv2_class->control_in_ports, GstLV2Port, i).index,
        &(lv2->ports.control.in[i]));

  for (i = 0; i < lv2_class->control_out_ports->len; i++)
    lilv_instance_connect_port (lv2->instance,
        g_array_index (lv2_class->control_out_ports, GstLV2Port, i).index,
        &(lv2->ports.control.out[i]));

  lilv_instance_activate (lv2->instance);
  lv2->activated = TRUE;

  return TRUE;
}

gboolean
gst_lv2_cleanup (GstLV2 * lv2, GstObject * obj)
{
  if (lv2->activated == FALSE) {
    GST_ERROR_OBJECT (obj, "Deactivating but LV2 plugin not activated");
    return TRUE;
  }

  if (lv2->instance == NULL) {
    GST_ERROR_OBJECT (obj, "Deactivating but no LV2 plugin set");
    return TRUE;
  }

  GST_DEBUG_OBJECT (obj, "deactivating");

  lilv_instance_deactivate (lv2->instance);

  lv2->activated = FALSE;

  lilv_instance_free (lv2->instance);
  lv2->instance = NULL;

  return TRUE;
}

void
gst_lv2_object_set_property (GstLV2 * lv2, GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  /* remember, properties have an offset */
  prop_id -= lv2->klass->properties;

  /* only input ports */
  g_return_if_fail (prop_id < lv2->klass->control_in_ports->len);

  /* now see what type it is */
  switch (pspec->value_type) {
    case G_TYPE_BOOLEAN:
      lv2->ports.control.in[prop_id] =
          g_value_get_boolean (value) ? 0.0f : 1.0f;
      break;
    case G_TYPE_INT:
      lv2->ports.control.in[prop_id] = g_value_get_int (value);
      break;
    case G_TYPE_FLOAT:
      lv2->ports.control.in[prop_id] = g_value_get_float (value);
      break;
    default:
      GST_WARNING_OBJECT (object, "unhandled type: %s",
          g_type_name (pspec->value_type));
      g_assert_not_reached ();
  }
}

void
gst_lv2_object_get_property (GstLV2 * lv2, GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  gfloat *controls;

  /* remember, properties have an offset */
  prop_id -= lv2->klass->properties;

  if (prop_id < lv2->klass->control_in_ports->len) {
    controls = lv2->ports.control.in;
  } else if (prop_id < lv2->klass->control_in_ports->len +
      lv2->klass->control_out_ports->len) {
    controls = lv2->ports.control.out;
    prop_id -= lv2->klass->control_in_ports->len;
  } else {
    g_return_if_reached ();
  }

  /* now see what type it is */
  switch (pspec->value_type) {
    case G_TYPE_BOOLEAN:
      g_value_set_boolean (value, controls[prop_id] > 0.0f);
      break;
    case G_TYPE_INT:
      g_value_set_int (value, CLAMP (controls[prop_id], G_MININT, G_MAXINT));
      break;
    case G_TYPE_FLOAT:
      g_value_set_float (value, controls[prop_id]);
      break;
    default:
      GST_WARNING_OBJECT (object, "unhandled type: %s",
          g_type_name (pspec->value_type));
      g_return_if_reached ();
  }
}


static gchar *
gst_lv2_class_get_param_name (GstLV2Class * klass, GObjectClass * object_class,
    const LilvPort * port)
{
  const LilvPlugin *lv2plugin = klass->plugin;
  gchar *ret;

  ret = g_strdup (lilv_node_as_string (lilv_port_get_symbol (lv2plugin, port)));

  /* this is the same thing that param_spec_* will do */
  g_strcanon (ret, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
  /* satisfy glib2 (argname[0] must be [A-Za-z]) */
  if (!((ret[0] >= 'a' && ret[0] <= 'z') || (ret[0] >= 'A' && ret[0] <= 'Z'))) {
    gchar *tempstr = ret;

    ret = g_strconcat ("param-", ret, NULL);
    g_free (tempstr);
  }

  /* check for duplicate property names */
  if (g_object_class_find_property (object_class, ret)) {
    gint n = 1;
    gchar *nret = g_strdup_printf ("%s-%d", ret, n++);

    while (g_object_class_find_property (object_class, nret)) {
      g_free (nret);
      nret = g_strdup_printf ("%s-%d", ret, n++);
    }
    g_free (ret);
    ret = nret;
  }

  GST_DEBUG ("built property name '%s' from port name '%s'", ret,
      lilv_node_as_string (lilv_port_get_symbol (lv2plugin, port)));

  return ret;
}

static gchar *
gst_lv2_class_get_param_nick (GstLV2Class * klass, const LilvPort * port)
{
  const LilvPlugin *lv2plugin = klass->plugin;

  return g_strdup (lilv_node_as_string (lilv_port_get_name (lv2plugin, port)));
}

static GParamSpec *
gst_lv2_class_get_param_spec (GstLV2Class * klass, GObjectClass * object_class,
    gint portnum)
{
  const LilvPlugin *lv2plugin = klass->plugin;
  const LilvPort *port = lilv_plugin_get_port_by_index (lv2plugin, portnum);
  LilvNode *lv2def, *lv2min, *lv2max;
  GParamSpec *ret;
  gchar *name, *nick;
  gint perms;
  gfloat lower = 0.0f, upper = 1.0f, def = 0.0f;

  nick = gst_lv2_class_get_param_nick (klass, port);
  name = gst_lv2_class_get_param_name (klass, object_class, port);

  GST_DEBUG ("%s trying port %s : %s",
      lilv_node_as_string (lilv_plugin_get_uri (lv2plugin)), name, nick);

  perms = G_PARAM_READABLE;
  if (lilv_port_is_a (lv2plugin, port, input_class))
    perms |= G_PARAM_WRITABLE | G_PARAM_CONSTRUCT;
  if (lilv_port_is_a (lv2plugin, port, control_class))
    perms |= GST_PARAM_CONTROLLABLE;

  if (lilv_port_has_property (lv2plugin, port, toggled_prop)) {
    ret = g_param_spec_boolean (name, nick, nick, FALSE, perms);
    goto done;
  }

  lilv_port_get_range (lv2plugin, port, &lv2def, &lv2min, &lv2max);

  if (lv2def)
    def = lilv_node_as_float (lv2def);
  if (lv2min)
    lower = lilv_node_as_float (lv2min);
  if (lv2max)
    upper = lilv_node_as_float (lv2max);

  lilv_node_free (lv2def);
  lilv_node_free (lv2min);
  lilv_node_free (lv2max);

  if (def < lower) {
    if (lv2def && lv2min) {
      GST_WARNING ("%s has lower bound %f > default %f",
          lilv_node_as_string (lilv_plugin_get_uri (lv2plugin)), lower, def);
    }
    lower = def;
  }

  if (def > upper) {
    if (lv2def && lv2max) {
      GST_WARNING ("%s has upper bound %f < default %f",
          lilv_node_as_string (lilv_plugin_get_uri (lv2plugin)), upper, def);
    }
    upper = def;
  }

  if (lilv_port_has_property (lv2plugin, port, integer_prop))
    ret = g_param_spec_int (name, nick, nick, lower, upper, def, perms);
  else
    ret = g_param_spec_float (name, nick, nick, lower, upper, def, perms);

done:
  g_free (name);
  g_free (nick);

  return ret;
}

void
gst_lv2_class_install_properties (GstLV2Class * lv2_class,
    GObjectClass * object_class, guint offset)
{
  GParamSpec *p;
  guint i;

  lv2_class->properties = offset;

  for (i = 0; i < lv2_class->control_in_ports->len; i++, offset++) {
    p = gst_lv2_class_get_param_spec (lv2_class, object_class,
        g_array_index (lv2_class->control_in_ports, GstLV2Port, i).index);

    g_object_class_install_property (object_class, offset, p);
  }

  for (i = 0; i < lv2_class->control_out_ports->len; i++, offset++) {
    p = gst_lv2_class_get_param_spec (lv2_class, object_class,
        g_array_index (lv2_class->control_out_ports, GstLV2Port, i).index);

    g_object_class_install_property (object_class, offset, p);
  }
}

void
gst_lv2_element_class_set_metadata (GstLV2Class * lv2_class,
    GstElementClass * elem_class, const gchar * lv2_class_tags)
{
  const LilvPlugin *lv2plugin = lv2_class->plugin;
  LilvNode *val;
  const LilvPluginClass *lv2plugin_class;
  const LilvNode *cval;
  gchar *longname, *author, *class_tags = NULL;

  val = lilv_plugin_get_name (lv2plugin);
  if (val) {
    longname = g_strdup (lilv_node_as_string (val));
    lilv_node_free (val);
  } else {
    longname = g_strdup ("no description available");
  }
  val = lilv_plugin_get_author_name (lv2plugin);
  if (val) {
    author = g_strdup (lilv_node_as_string (val));
    lilv_node_free (val);
  } else {
    author = g_strdup ("no author available");
  }

  lv2plugin_class = lilv_plugin_get_class (lv2plugin);
  cval = lilv_plugin_class_get_label (lv2plugin_class);
  if (cval) {
    class_tags = g_strconcat (lv2_class_tags, "/", lilv_node_as_string (cval),
        NULL);
  }

  gst_element_class_set_metadata (elem_class, longname,
      (class_tags ? class_tags : lv2_class_tags), longname, author);
  g_free (longname);
  g_free (author);
  g_free (class_tags);
}


void
gst_lv2_class_init (GstLV2Class * lv2_class, GType type)
{
  const GValue *value =
      gst_structure_get_value (lv2_meta_all, g_type_name (type));
  GstStructure *lv2_meta = g_value_get_boxed (value);
  const LilvPlugin *lv2plugin;
  /* FIXME Handle channels positionning
   * GstAudioChannelPosition position = GST_AUDIO_CHANNEL_POSITION_INVALID; */
  guint j, in_pad_index = 0, out_pad_index = 0;
  const LilvPlugins *plugins = lilv_world_get_all_plugins (world);
  LilvNode *plugin_uri;
  const gchar *element_uri;

  GST_DEBUG ("LV2 initializing class");

  element_uri = gst_structure_get_string (lv2_meta, "element-uri");
  plugin_uri = lilv_new_uri (world, element_uri);
  g_assert (plugin_uri);
  lv2plugin = lilv_plugins_get_by_uri (plugins, plugin_uri);
  g_assert (lv2plugin);
  lv2_class->plugin = lv2plugin;
  lilv_node_free (plugin_uri);

  lv2_class->in_group.ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
  lv2_class->out_group.ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
  lv2_class->control_in_ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
  lv2_class->control_out_ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));

  /* find ports and groups */
  for (j = 0; j < lilv_plugin_get_num_ports (lv2plugin); j++) {
    const LilvPort *port = lilv_plugin_get_port_by_index (lv2plugin, j);
    const gboolean is_input = lilv_port_is_a (lv2plugin, port, input_class);
    struct _GstLV2Port desc = { j, 0, };
    LilvNodes *lv2group = lilv_port_get (lv2plugin, port, group_pred);

    if (lv2group) {
      /* port is part of a group */
      const gchar *group_uri = lilv_node_as_uri (lv2group);
      GstLV2Group *group = is_input
          ? &lv2_class->in_group : &lv2_class->out_group;

      if (group->uri == NULL) {
        group->uri = g_strdup (group_uri);
        group->pad = is_input ? in_pad_index++ : out_pad_index++;
        group->ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
      }

      /* FIXME Handle channels positionning
         position = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
         sub_values = lilv_port_get_value (lv2plugin, port, has_role_pred);
         if (lilv_nodes_size (sub_values) > 0) {
         LilvNode *role = lilv_nodes_get_at (sub_values, 0);
         position = gst_lv2_filter_role_to_position (role);
         }
         lilv_nodes_free (sub_values);

         if (position != GST_AUDIO_CHANNEL_POSITION_INVALID) {
         desc.position = position;
         } */

      g_array_append_val (group->ports, desc);
    } else {
      /* port is not part of a group, or it is part of a group but that group
       * is illegal so we just ignore it */
      if (lilv_port_is_a (lv2plugin, port, audio_class)) {
        desc.pad = is_input ? in_pad_index++ : out_pad_index++;
        if (is_input)
          g_array_append_val (lv2_class->in_group.ports, desc);
        else
          g_array_append_val (lv2_class->out_group.ports, desc);
      } else if (lilv_port_is_a (lv2plugin, port, control_class)) {
        if (is_input)
          g_array_append_val (lv2_class->control_in_ports, desc);
        else
          g_array_append_val (lv2_class->control_out_ports, desc);
      } else {
        /* unknown port type */
        GST_INFO ("unhandled port %d: %s", j,
            lilv_node_as_string (lilv_port_get_symbol (lv2plugin, port)));
        continue;
      }
    }
  }
}

void
gst_lv2_class_finalize (GstLV2Class * lv2_class)
{
  GST_DEBUG ("LV2 finalizing class");

  g_array_free (lv2_class->in_group.ports, TRUE);
  lv2_class->in_group.ports = NULL;
  g_array_free (lv2_class->out_group.ports, TRUE);
  lv2_class->out_group.ports = NULL;
  g_array_free (lv2_class->control_in_ports, TRUE);
  lv2_class->control_in_ports = NULL;
  g_array_free (lv2_class->control_out_ports, TRUE);
  lv2_class->control_out_ports = NULL;
}

void
gst_lv2_register_element (GstPlugin * plugin, GType parent_type,
    const GTypeInfo * info, GstStructure * lv2_meta)
{
  const gchar *type_name =
      gst_structure_get_string (lv2_meta, "element-type-name");

  gst_element_register (plugin, type_name, GST_RANK_NONE,
      g_type_register_static (parent_type, type_name, info, 0));
}
