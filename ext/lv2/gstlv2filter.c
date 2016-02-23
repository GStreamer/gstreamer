/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 *               2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *               2003 Andy Wingo <wingo at pobox.com>
 *               2016 Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION:element-lv2
 * @short_description: bridge for LV2.
 *
 * LV2 is a standard for plugins and matching host applications,
 * mainly targeted at audio processing and generation.  It is intended as
 * a successor to LADSPA (Linux Audio Developer's Simple Plugin API).
 *
 * The LV2 element is a bridge for plugins using the
 * <ulink url="http://www.lv2plug.in/">LV2</ulink> API.  It scans all
 * installed LV2 plugins and registers them as gstreamer elements.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstlv2.h"

#include <string.h>
#include <math.h>
#include <glib.h>

#include <lilv/lilv.h>

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/audio/audio-channels.h>

GST_DEBUG_CATEGORY_EXTERN (lv2_debug);
#define GST_CAT_DEFAULT lv2_debug

typedef struct _lv2_control_info
{
  gchar *name;
  gchar *param_name;
  gfloat lowerbound, upperbound;
  gfloat def;
  gboolean lower, upper, samplerate;
  gboolean toggled, logarithmic, integer, writable;
} lv2_control_info;


typedef struct _GstLV2Filter GstLV2Filter;
typedef struct _GstLV2FilterClass GstLV2FilterClass;
typedef struct _GstLV2FilterGroup GstLV2FilterGroup;
typedef struct _GstLV2FilterPort GstLV2FilterPort;


struct _GstLV2Filter
{
  GstAudioFilter parent;

  LilvPlugin *plugin;
  LilvInstance *instance;

  gboolean activated;

  /* TODO refactor in the same way as LADSPA plugin */
  struct
  {
    struct
    {
      gfloat *in;
      gfloat *out;
    } control;
  } ports;
};

struct _GstLV2FilterGroup
{
  gchar *uri; /**< RDF resource (URI or blank node) */
  guint pad; /**< Gst pad index */
  gchar *symbol; /**< Gst pad name / LV2 group symbol */
  GArray *ports; /**< Array of GstLV2FilterPort */
  gboolean has_roles; /**< TRUE iff all ports have a known role */
};

struct _GstLV2FilterPort
{
  gint index; /**< LV2 port index (on LV2 plugin) */
  gint pad; /**< Gst pad index (iff not part of a group) */
  LilvNode *role; /**< Channel position / port role */
  GstAudioChannelPosition position; /**< Channel position */
};

struct _GstLV2FilterClass
{
  GstAudioFilterClass parent_class;

  LilvPlugin *plugin;

  GstLV2FilterGroup in_group; /**< Array of GstLV2FilterGroup */
  GstLV2FilterGroup out_group; /**< Array of GstLV2FilterGroup */
  GArray *control_in_ports; /**< Array of GstLV2FilterPort */
  GArray *control_out_ports; /**< Array of GstLV2FilterPort */

};

static GstAudioFilter *parent_class = NULL;

/* GObject vmethods implementation */
static void
gst_lv2_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLV2Filter *self = (GstLV2Filter *) (object);
  GstLV2FilterClass *klass =
      (GstLV2FilterClass *) GST_AUDIO_FILTER_GET_CLASS (object);

  /* remember, properties have an offset of 1 */
  prop_id--;

  /* only input ports */
  g_return_if_fail (prop_id < klass->control_in_ports->len);

  /* now see what type it is */
  switch (pspec->value_type) {
    case G_TYPE_BOOLEAN:
      self->ports.control.in[prop_id] =
          g_value_get_boolean (value) ? 0.0f : 1.0f;
      break;
    case G_TYPE_INT:
      self->ports.control.in[prop_id] = g_value_get_int (value);
      break;
    case G_TYPE_FLOAT:
      self->ports.control.in[prop_id] = g_value_get_float (value);
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_lv2_filter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLV2Filter *self = (GstLV2Filter *) (object);
  GstLV2FilterClass *klass =
      (GstLV2FilterClass *) GST_AUDIO_FILTER_GET_CLASS (object);

  gfloat *controls;

  /* remember, properties have an offset of 1 */
  prop_id--;

  if (prop_id < klass->control_in_ports->len) {
    controls = self->ports.control.in;
  } else if (prop_id < klass->control_in_ports->len +
      klass->control_out_ports->len) {
    controls = self->ports.control.out;
    prop_id -= klass->control_in_ports->len;
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
      g_return_if_reached ();
  }
}

#if 0
/* Convert an LV2 port role to a Gst channel positon
 * WARNING: If the group has only a single port,
 * GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER will be returned for pg:centerRole
 * (which is used by LV2 for mono groups), but this is not correct.  In this
 * case the value must be changed to GST_AUDIO_CHANNEL_POSITION_FRONT_MONO
 * (this can't be done by this function because this information isn't known
 * at the time it is used).
 */
static GstAudioChannelPosition
gst_lv2_filter_role_to_position (LilvNode * role)
{
  /* Front.  Mono and left/right are mututally exclusive */
  if (lilv_node_equals (role, center_role)) {

    return GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
  } else if (lilv_node_equals (role, left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
  } else if (lilv_node_equals (role, right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;

    /* Rear. Left/right and center are mututally exclusive */
  } else if (lilv_node_equals (role, rear_center_role)) {
    return GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
  } else if (lilv_node_equals (role, rear_left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
  } else if (lilv_node_equals (role, rear_right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;

    /* Subwoofer/low-frequency-effects */
  } else if (lilv_node_equals (role, lfe_role)) {
    return GST_AUDIO_CHANNEL_POSITION_LFE1;

    /* Center front speakers. Center and left/right_of_center
     * are mutually exclusive */
  } else if (lilv_node_equals (role, center_left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
  } else if (lilv_node_equals (role, center_right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;

    /* sides */
  } else if (lilv_node_equals (role, side_left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
  } else if (lilv_node_equals (role, side_right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
  }

  return GST_AUDIO_CHANNEL_POSITION_INVALID;
}

static GstAudioChannelPosition *
gst_lv2_filter_build_positions (GstLV2FilterGroup * group)
{
  GstAudioChannelPosition *positions = NULL;

  /* don't do anything for mono */
  if (group->ports->len > 1) {
    gint i;

    positions = g_new (GstAudioChannelPosition, group->ports->len);
    for (i = 0; i < group->ports->len; ++i)
      positions[i] = g_array_index (group->ports, GstLV2FilterPort, i).position;
  }
  return positions;
}
#endif

/* Find and return the group @a uri in @a groups, or NULL if not found */
static void
gst_lv2_filter_type_class_add_pad_templates (GstLV2FilterClass * klass)
{
  GstCaps *srccaps, *sinkcaps;
  GstPadTemplate *pad_template;
  GstElementClass *elem_class = GST_ELEMENT_CLASS (klass);

  gint in_channels = 1, out_channels = 1;

  in_channels = klass->in_group.ports->len;

  out_channels = klass->out_group.ports->len;

  /* FIXME Implement deintereleaved audio support */
  sinkcaps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, GST_AUDIO_NE (F32),
      "channels", G_TYPE_INT, in_channels,
      "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "layout", G_TYPE_STRING, "interleaved", NULL);

  srccaps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, GST_AUDIO_NE (F32),
      "channels", G_TYPE_INT, out_channels,
      "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "layout", G_TYPE_STRING, "interleaved", NULL);

  pad_template =
      gst_pad_template_new (GST_BASE_TRANSFORM_SINK_NAME, GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  gst_element_class_add_pad_template (elem_class, pad_template);

  pad_template =
      gst_pad_template_new (GST_BASE_TRANSFORM_SRC_NAME, GST_PAD_SRC,
      GST_PAD_ALWAYS, srccaps);
  gst_element_class_add_pad_template (elem_class, pad_template);

  gst_caps_unref (sinkcaps);
  gst_caps_unref (srccaps);
}

static gboolean
gst_lv2_filter_setup (GstAudioFilter * gsp, const GstAudioInfo * info)
{
  GstLV2Filter *self;
  GstLV2FilterClass *oclass;
  GstAudioFilterClass *audiofilter_class;
  gint i;

  audiofilter_class = GST_AUDIO_FILTER_GET_CLASS (gsp);
  self = (GstLV2Filter *) gsp;
  oclass = (GstLV2FilterClass *) audiofilter_class;

  g_return_val_if_fail (self->activated == FALSE, FALSE);

  GST_DEBUG_OBJECT (self, "instantiating the plugin at %d Hz",
      GST_AUDIO_INFO_RATE (info));

  if (self->instance)
    lilv_instance_free (self->instance);

  if (!(self->instance =
          lilv_plugin_instantiate (oclass->plugin, GST_AUDIO_INFO_RATE (info),
              NULL)))
    goto no_instance;

  /* connect the control ports */
  for (i = 0; i < oclass->control_in_ports->len; i++)
    lilv_instance_connect_port (self->instance,
        g_array_index (oclass->control_in_ports, GstLV2FilterPort, i).index,
        &(self->ports.control.in[i]));

  for (i = 0; i < oclass->control_out_ports->len; i++)
    lilv_instance_connect_port (self->instance,
        g_array_index (oclass->control_out_ports, GstLV2FilterPort, i).index,
        &(self->ports.control.out[i]));

  /* FIXME Handle audio channel positionning while negotiating CAPS */
#if 0
  /* set input group pad audio channel position */
  for (i = 0; i < oclass->in_groups->len; ++i) {
    group = &g_array_index (oclass->in_groups, GstLV2FilterGroup, i);
    if (group->has_roles) {
      if ((positions = gst_lv2_filter_build_positions (group))) {
        if ((pad = gst_element_get_static_pad (GST_ELEMENT (gsp),
                    lilv_node_as_string (group->symbol)))) {
          GST_INFO_OBJECT (self, "set audio channel positions on sink pad %s",
              lilv_node_as_string (group->symbol));
          s = gst_caps_get_structure (caps, 0);
          gst_audio_set_channel_positions (s, positions);
          gst_object_unref (pad);
        }
        g_free (positions);
        positions = NULL;
      }
    }
  }
  /* set output group pad audio channel position */
  for (i = 0; i < oclass->out_groups->len; ++i) {
    group = &g_array_index (oclass->out_groups, GstLV2FilterGroup, i);
    if (group->has_roles) {
      if ((positions = gst_lv2_filter_build_positions (group))) {
        if ((pad = gst_element_get_static_pad (GST_ELEMENT (gsp),
                    lilv_node_as_string (group->symbol)))) {
          GST_INFO_OBJECT (self, "set audio channel positions on src pad %s",
              lilv_node_as_string (group->symbol));
          s = gst_caps_get_structure (caps, 0);
          gst_audio_set_channel_positions (s, positions);
          gst_object_unref (pad);
        }
        g_free (positions);
        positions = NULL;
      }
    }
  }
#endif

  lilv_instance_activate (self->instance);
  self->activated = TRUE;

  return TRUE;

no_instance:
  {
    GST_ERROR_OBJECT (gsp, "could not create instance");
    return FALSE;
  }
}

static gboolean
gst_lv2_filter_stop (GstBaseTransform * transform)
{
  GstLV2Filter *lv2 = (GstLV2Filter *) transform;

  if (lv2->activated == FALSE) {
      GST_ERROR_OBJECT (transform, "Deactivating but LV2 plugin not activated");

      return TRUE;
  }

  if (lv2->instance == NULL) {
      GST_ERROR_OBJECT (transform, "Deactivating but no LV2 plugin set");

      return TRUE;
  }

  GST_DEBUG_OBJECT (lv2, "deactivating");

  lilv_instance_deactivate (lv2->instance);

  lv2->activated = FALSE;

  lilv_instance_free (lv2->instance);
  lv2->instance = NULL;

  return TRUE;
}

static inline void
gst_lv2_filter_deinterleave_data (guint n_channels, gfloat * outdata,
    guint samples, gfloat * indata)
{
  guint i, j;

  for (i = 0; i < n_channels; i++)
    for (j = 0; j < samples; j++)
      outdata[i * samples + j] = indata[j * n_channels + i];
}

static inline void
gst_lv2_filter_interleave_data (guint n_channels, gfloat * outdata,
    guint samples, gfloat * indata)
{
  guint i, j;

  for (i = 0; i < n_channels; i++)
    for (j = 0; j < samples; j++) {
      outdata[j * n_channels + i] = indata[i * samples + j];
    }
}

static GstFlowReturn
gst_lv2_filter_transform_data (GstLV2Filter * self,
    GstMapInfo * in_map, GstMapInfo * out_map)
{
  GstAudioFilterClass *audiofilter_class;
  GstLV2FilterClass *lv2_class;
  GstLV2FilterGroup *lv2_group;
  GstLV2FilterPort *lv2_port;
  guint j, nframes, samples, out_samples;

  gfloat *in = NULL, *out = NULL;

  nframes = in_map->size / sizeof (float);

  audiofilter_class = GST_AUDIO_FILTER_GET_CLASS (self);
  lv2_class = (GstLV2FilterClass *) audiofilter_class;

  samples = nframes / lv2_class->in_group.ports->len;

  /* multi channel inputs */
  lv2_group = &lv2_class->in_group;

  in = g_new0 (gfloat, nframes);

  if (lv2_group->ports->len > 1)
      gst_lv2_filter_deinterleave_data (lv2_group->ports->len, in,
          samples, (gfloat *) in_map->data);

  for (j = 0; j < lv2_group->ports->len; ++j) {
    lv2_port = &g_array_index (lv2_group->ports, GstLV2FilterPort, j);

    lilv_instance_connect_port (self->instance, lv2_port->index,
        in + (j * samples));
  }

  lv2_group = &lv2_class->out_group;
  out_samples = nframes / lv2_group->ports->len;
  out = g_new0 (gfloat, samples * lv2_group->ports->len);
  for (j = 0; j < lv2_group->ports->len; ++j) {
    lv2_port = &g_array_index (lv2_group->ports, GstLV2FilterPort, j);
    lilv_instance_connect_port (self->instance, lv2_port->index,
        out + (j * out_samples));
  }

  lilv_instance_run (self->instance, samples);

  if (lv2_group->ports->len > 1)
      gst_lv2_filter_interleave_data (lv2_group->ports->len,
          (gfloat *) out_map->data, out_samples, out);
  g_free (out);
  g_free (in);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_lv2_filter_transform_ip (GstBaseTransform * transform, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_READWRITE);

  ret = gst_lv2_filter_transform_data ((GstLV2Filter *) transform, &map, &map);

  gst_buffer_unmap (buf, &map);

  return ret;
}

static GstFlowReturn
gst_lv2_filter_transform (GstBaseTransform * transform,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstMapInfo in_map, out_map;
  GstFlowReturn ret;

  gst_buffer_map (inbuf, &in_map, GST_MAP_READ);
  gst_buffer_map (outbuf, &out_map, GST_MAP_WRITE);

  ret = gst_lv2_filter_transform_data ((GstLV2Filter *) transform,
      &in_map, &out_map);

  gst_buffer_unmap (inbuf, &in_map);
  gst_buffer_unmap (outbuf, &out_map);

  return ret;
}

static void
gst_lv2_filter_base_init (gpointer g_class)
{
  GstLV2FilterClass *klass = (GstLV2FilterClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  LilvPlugin *lv2plugin;
  LilvNode *val;
  /* FIXME Handle channels positionning
   * GstAudioChannelPosition position = GST_AUDIO_CHANNEL_POSITION_INVALID; */
  guint j, in_pad_index = 0, out_pad_index = 0;
  const gchar *klass_tags;
  gchar *longname, *author;

  lv2plugin = (LilvPlugin *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      descriptor_quark);

  g_assert (lv2plugin);

  GST_INFO ("base_init %p, plugin %s", g_class,
      lilv_node_get_turtle_token (lilv_plugin_get_uri (lv2plugin)));

  klass->in_group.ports = g_array_new (FALSE, TRUE, sizeof (GstLV2FilterPort));
  klass->out_group.ports = g_array_new (FALSE, TRUE, sizeof (GstLV2FilterPort));
  klass->control_in_ports =
      g_array_new (FALSE, TRUE, sizeof (GstLV2FilterPort));
  klass->control_out_ports =
      g_array_new (FALSE, TRUE, sizeof (GstLV2FilterPort));

  /* find ports and groups */
  for (j = 0; j < lilv_plugin_get_num_ports (lv2plugin); j++) {
    const LilvPort *port = lilv_plugin_get_port_by_index (lv2plugin, j);
    const gboolean is_input = lilv_port_is_a (lv2plugin, port, input_class);
    struct _GstLV2FilterPort desc = { j, 0, };
    LilvNodes *lv2group = lilv_port_get (lv2plugin, port, group_pred);

    if (lv2group) {
      /* port is part of a group */
      const gchar *group_uri = lilv_node_as_uri (lv2group);
      GstLV2FilterGroup *group =
          is_input ? &klass->in_group : &klass->out_group;

      if (group->uri == NULL) {
        group->uri = g_strdup (group_uri);
        group->pad = is_input ? in_pad_index++ : out_pad_index++;
        group->ports = g_array_new (FALSE, TRUE, sizeof (GstLV2FilterPort));
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
          g_array_append_val (klass->in_group.ports, desc);
        else
          g_array_append_val (klass->out_group.ports, desc);
      } else if (lilv_port_is_a (lv2plugin, port, control_class)) {
        if (is_input)
          g_array_append_val (klass->control_in_ports, desc);
        else
          g_array_append_val (klass->control_out_ports, desc);
      } else {
        /* unknown port type */
        GST_INFO ("unhandled port %d", j);
        continue;
      }
    }
  }
  gst_lv2_filter_type_class_add_pad_templates (klass);

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

  klass_tags = "Filter/Effect/Audio/LV2";

  GST_INFO ("tags : %s", klass_tags);
  gst_element_class_set_metadata (element_class, longname,
      klass_tags, longname, author);
  g_free (longname);
  g_free (author);

  klass->plugin = lv2plugin;
}

static gchar *
gst_lv2_filter_class_get_param_name (GstLV2FilterClass * klass,
    const LilvPort * port)
{
  LilvPlugin *lv2plugin = klass->plugin;
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
  if (g_object_class_find_property (G_OBJECT_CLASS (klass), ret)) {
    gint n = 1;
    gchar *nret = g_strdup_printf ("%s-%d", ret, n++);

    while (g_object_class_find_property (G_OBJECT_CLASS (klass), nret)) {
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
gst_lv2_filter_class_get_param_nick (GstLV2FilterClass * klass,
    const LilvPort * port)
{
  LilvPlugin *lv2plugin = klass->plugin;

  return g_strdup (lilv_node_as_string (lilv_port_get_name (lv2plugin, port)));
}

static GParamSpec *
gst_lv2_filter_class_get_param_spec (GstLV2FilterClass * klass, gint portnum)
{
  LilvPlugin *lv2plugin = klass->plugin;
  const LilvPort *port = lilv_plugin_get_port_by_index (lv2plugin, portnum);
  LilvNode *lv2def, *lv2min, *lv2max;
  GParamSpec *ret;
  gchar *name, *nick;
  gint perms;
  gfloat lower = 0.0f, upper = 1.0f, def = 0.0f;

  nick = gst_lv2_filter_class_get_param_nick (klass, port);
  name = gst_lv2_filter_class_get_param_name (klass, port);

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
    GST_WARNING ("%s has lower bound %f > default %f",
        lilv_node_as_string (lilv_plugin_get_uri (lv2plugin)), lower, def);
    lower = def;
  }

  if (def > upper) {
    GST_WARNING ("%s has upper bound %f < default %f",
        lilv_node_as_string (lilv_plugin_get_uri (lv2plugin)), upper, def);
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

static void
gst_lv2_filter_class_init (GstLV2FilterClass * klass, LilvPlugin * lv2plugin)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *transform_class;
  GstAudioFilterClass *audiofilter_class;
  GParamSpec *p;
  gint i, ix;

  GST_DEBUG ("class_init %p", klass);

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_lv2_filter_set_property;
  gobject_class->get_property = gst_lv2_filter_get_property;

  audiofilter_class = GST_AUDIO_FILTER_CLASS (klass);
  audiofilter_class->setup = gst_lv2_filter_setup;

  transform_class = GST_BASE_TRANSFORM_CLASS (klass);
  transform_class->stop = gst_lv2_filter_stop;
  transform_class->transform = gst_lv2_filter_transform;
  transform_class->transform_ip = gst_lv2_filter_transform_ip;

  klass->plugin = lv2plugin;

  /* properties have an offset of 1 */
  ix = 1;

  /* register properties */

  for (i = 0; i < klass->control_in_ports->len; i++, ix++) {
    p = gst_lv2_filter_class_get_param_spec (klass,
        g_array_index (klass->control_in_ports, GstLV2FilterPort, i).index);

    g_object_class_install_property (gobject_class, ix, p);
  }

  for (i = 0; i < klass->control_out_ports->len; i++, ix++) {
    p = gst_lv2_filter_class_get_param_spec (klass,
        g_array_index (klass->control_out_ports, GstLV2FilterPort, i).index);

    g_object_class_install_property (gobject_class, ix, p);
  }
}

static void
gst_lv2_filter_init (GstLV2Filter * self, GstLV2FilterClass * klass)
{
  self->plugin = klass->plugin;
  self->instance = NULL;
  self->activated = FALSE;

  self->ports.control.in = g_new0 (gfloat, klass->control_in_ports->len);
  self->ports.control.out = g_new0 (gfloat, klass->control_out_ports->len);

  if (!lilv_plugin_has_feature (self->plugin, in_place_broken_pred))
    gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), TRUE);
}

gboolean
gst_lv2_filter_register_element (GstPlugin * plugin, const gchar * type_name,
    gpointer * lv2plugin)
{
  GType type;
  GTypeInfo typeinfo = {
    sizeof (GstLV2FilterClass),
    (GBaseInitFunc) gst_lv2_filter_base_init,
    NULL,
    (GClassInitFunc) gst_lv2_filter_class_init,
    NULL,
    lv2plugin,
    sizeof (GstLV2Filter),
    0,
    (GInstanceInitFunc) gst_lv2_filter_init,
  };

  /* create the type */
  type =
      g_type_register_static (GST_TYPE_AUDIO_FILTER, type_name, &typeinfo, 0);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_AUDIO_FILTER);


  /* FIXME: not needed anymore when we can add pad templates, etc in class_init
   * as class_data contains the LADSPA_Descriptor too */
  g_type_set_qdata (type, descriptor_quark, lv2plugin);

  return gst_element_register (plugin, type_name, GST_RANK_NONE, type);
}
