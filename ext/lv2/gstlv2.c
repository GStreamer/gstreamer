/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 *               2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *               2003 Andy Wingo <wingo at pobox.com>
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
#include <string.h>
#include <math.h>
#include <glib.h>
#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>

#include "gstlv2.h"
#include <slv2/slv2.h>

#define GST_LV2_DEFAULT_PATH \
  "/usr/lib/lv2" G_SEARCHPATH_SEPARATOR_S \
  "/usr/local/lib/lv2" G_SEARCHPATH_SEPARATOR_S \
  LIBDIR "/lv2"

static void gst_lv2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_lv2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_lv2_setup (GstSignalProcessor * sigproc, GstCaps * caps);
static gboolean gst_lv2_start (GstSignalProcessor * sigproc);
static void gst_lv2_stop (GstSignalProcessor * sigproc);
static void gst_lv2_cleanup (GstSignalProcessor * sigproc);
static void gst_lv2_process (GstSignalProcessor * sigproc, guint nframes);

static SLV2World world;
static SLV2Value audio_class;
static SLV2Value control_class;
static SLV2Value input_class;
static SLV2Value output_class;
static SLV2Value integer_prop;
static SLV2Value toggled_prop;
static SLV2Value in_place_broken_pred;
static SLV2Value in_group_pred;
static SLV2Value has_role_pred;
static SLV2Value lv2_symbol_pred;

static SLV2Value center_role;
static SLV2Value left_role;
static SLV2Value right_role;
static SLV2Value rear_center_role;
static SLV2Value rear_left_role;
static SLV2Value rear_right_role;
static SLV2Value lfe_role;
static SLV2Value center_left_role;
static SLV2Value center_right_role;
static SLV2Value side_left_role;
static SLV2Value side_right_role;

static GstSignalProcessorClass *parent_class;

static GstPlugin *gst_lv2_plugin;

GST_DEBUG_CATEGORY_STATIC (lv2_debug);
#define GST_CAT_DEFAULT lv2_debug

static GQuark descriptor_quark = 0;


/* Convert an LV2 port role to a Gst channel positon
 * WARNING: If the group has only a single port,
 * GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER will be returned for pg:centerRole
 * (which is used by LV2 for mono groups), but this is not correct.  In this
 * case the value must be changed to GST_AUDIO_CHANNEL_POSITION_FRONT_MONO
 * (this can't be done by this function because this information isn't known
 * at the time it is used).
 */
static GstAudioChannelPosition
gst_lv2_role_to_position (SLV2Value role)
{
  /* Front.  Mono and left/right are mututally exclusive */
  if (slv2_value_equals (role, center_role)) {

    return GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
  } else if (slv2_value_equals (role, left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
  } else if (slv2_value_equals (role, right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;

    /* Rear. Left/right and center are mututally exclusive */
  } else if (slv2_value_equals (role, rear_center_role)) {
    return GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
  } else if (slv2_value_equals (role, rear_left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
  } else if (slv2_value_equals (role, rear_right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;

    /* Subwoofer/low-frequency-effects */
  } else if (slv2_value_equals (role, lfe_role)) {
    return GST_AUDIO_CHANNEL_POSITION_LFE;

    /* Center front speakers. Center and left/right_of_center
     * are mutually exclusive */
  } else if (slv2_value_equals (role, center_left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
  } else if (slv2_value_equals (role, center_right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;

    /* sides */
  } else if (slv2_value_equals (role, side_left_role)) {
    return GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
  } else if (slv2_value_equals (role, side_right_role)) {
    return GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
  }

  return GST_AUDIO_CHANNEL_POSITION_INVALID;
}

/* Find and return the group @a uri in @a groups, or NULL if not found */
static GstLV2Group *
gst_lv2_class_find_group (GArray * groups, SLV2Value uri)
{
  int i = 0;
  for (; i < groups->len; ++i)
    if (slv2_value_equals (g_array_index (groups, GstLV2Group, i).uri, uri))
      return &g_array_index (groups, GstLV2Group, i);
  return NULL;
}

static GstAudioChannelPosition *
gst_lv2_build_positions (GstLV2Group * group)
{
  GstAudioChannelPosition *positions = NULL;

  /* don't do anything for mono */
  if (group->ports->len > 1) {
    gint i;

    positions = g_new (GstAudioChannelPosition, group->ports->len);
    for (i = 0; i < group->ports->len; ++i)
      positions[i] = g_array_index (group->ports, GstLV2Port, i).position;
  }
  return positions;
}

static void
gst_lv2_base_init (gpointer g_class)
{
  GstLV2Class *klass = (GstLV2Class *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstSignalProcessorClass *gsp_class = GST_SIGNAL_PROCESSOR_CLASS (g_class);
  SLV2Plugin lv2plugin;
  SLV2Value val;
  SLV2Values values, sub_values;
  GstLV2Group *group = NULL;
  GstAudioChannelPosition position = GST_AUDIO_CHANNEL_POSITION_INVALID;
  guint j, in_pad_index = 0, out_pad_index = 0;
  const gchar *klass_tags;
  gchar *longname, *author;

  lv2plugin = (SLV2Plugin) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      descriptor_quark);

  g_assert (lv2plugin);

  GST_DEBUG ("base_init %p, plugin %s", g_class,
      slv2_value_as_string (slv2_plugin_get_uri (lv2plugin)));

  gsp_class->num_group_in = 0;
  gsp_class->num_group_out = 0;
  gsp_class->num_audio_in = 0;
  gsp_class->num_audio_out = 0;
  gsp_class->num_control_in = 0;
  gsp_class->num_control_out = 0;

  klass->in_groups = g_array_new (FALSE, TRUE, sizeof (GstLV2Group));
  klass->out_groups = g_array_new (FALSE, TRUE, sizeof (GstLV2Group));
  klass->audio_in_ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
  klass->audio_out_ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
  klass->control_in_ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
  klass->control_out_ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));

  /* find ports and groups */
  for (j = 0; j < slv2_plugin_get_num_ports (lv2plugin); j++) {
    const SLV2Port port = slv2_plugin_get_port_by_index (lv2plugin, j);
    const gboolean is_input = slv2_port_is_a (lv2plugin, port, input_class);
    gboolean in_group = FALSE;
    struct _GstLV2Port desc = { j, 0, };
    values = slv2_port_get_value (lv2plugin, port, in_group_pred);

    if (slv2_values_size (values) > 0) {
      /* port is part of a group */
      SLV2Value group_uri = slv2_values_get_at (values, 0);
      GArray *groups = is_input ? klass->in_groups : klass->out_groups;
      GstLV2Group *group = gst_lv2_class_find_group (groups, group_uri);
      in_group = TRUE;
      if (group == NULL) {
        GstLV2Group g;
        g.uri = slv2_value_duplicate (group_uri);
        g.pad = is_input ? in_pad_index++ : out_pad_index++;
        g.ports = g_array_new (FALSE, TRUE, sizeof (GstLV2Port));
        g.has_roles = TRUE;
        g.symbol = NULL;
        sub_values = slv2_plugin_get_value_for_subject (lv2plugin, group_uri,
            lv2_symbol_pred);
        /* symbol is mandatory */
        if (slv2_values_size (sub_values) > 0) {
          g.symbol = slv2_value_duplicate (slv2_values_get_at (sub_values, 0));
          if (!gst_element_class_get_pad_template (element_class,
                  slv2_value_as_string (g.symbol))) {
            g_array_append_val (groups, g);
            group = &g_array_index (groups, GstLV2Group, groups->len - 1);
            assert (group);
            assert (slv2_value_equals (group->uri, group_uri));
          } else {
            GST_WARNING ("plugin %s has duplicate group symbol '%s'",
                slv2_value_as_string (slv2_plugin_get_uri (lv2plugin)),
                slv2_value_as_string (g.symbol));
            in_group = FALSE;
          }
        } else {
          GST_WARNING ("plugin %s has illegal group with no symbol",
              slv2_value_as_string (slv2_plugin_get_uri (lv2plugin)));
          in_group = FALSE;
        }
      }

      if (in_group) {
        position = GST_AUDIO_CHANNEL_POSITION_INVALID;
        sub_values = slv2_port_get_value (lv2plugin, port, has_role_pred);
        if (slv2_values_size (sub_values) > 0) {
          SLV2Value role = slv2_values_get_at (sub_values, 0);
          position = gst_lv2_role_to_position (role);
        }
        slv2_values_free (sub_values);
        if (position != GST_AUDIO_CHANNEL_POSITION_INVALID) {
          desc.position = position;
          g_array_append_val (group->ports, desc);
        } else {
          in_group = FALSE;
        }
      }
    }

    if (!in_group) {
      /* port is not part of a group, or it is part of a group but that group
       * is illegal so we just ignore it */
      if (slv2_port_is_a (lv2plugin, port, audio_class)) {
        desc.pad = is_input ? in_pad_index++ : out_pad_index++;
        if (is_input)
          g_array_append_val (klass->audio_in_ports, desc);
        else
          g_array_append_val (klass->audio_out_ports, desc);
      } else if (slv2_port_is_a (lv2plugin, port, control_class)) {
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
    slv2_values_free (values);
  }

  gsp_class->num_group_in = klass->in_groups->len;
  gsp_class->num_group_out = klass->out_groups->len;
  gsp_class->num_audio_in = klass->audio_in_ports->len;
  gsp_class->num_audio_out = klass->audio_out_ports->len;
  gsp_class->num_control_in = klass->control_in_ports->len;
  gsp_class->num_control_out = klass->control_out_ports->len;

  /* add input group pad templates */
  for (j = 0; j < gsp_class->num_group_in; ++j) {
    group = &g_array_index (klass->in_groups, GstLV2Group, j);

    gst_signal_processor_class_add_pad_template (gsp_class,
        slv2_value_as_string (group->symbol), GST_PAD_SINK, j,
        group->ports->len);
  }

  /* add output group pad templates */
  for (j = 0; j < gsp_class->num_group_out; ++j) {
    group = &g_array_index (klass->out_groups, GstLV2Group, j);

    gst_signal_processor_class_add_pad_template (gsp_class,
        slv2_value_as_string (group->symbol), GST_PAD_SRC, j,
        group->ports->len);
  }

  /* add non-grouped input port pad templates */
  for (j = 0; j < gsp_class->num_audio_in; ++j) {
    struct _GstLV2Port *desc =
        &g_array_index (klass->audio_in_ports, GstLV2Port, j);
    SLV2Port port = slv2_plugin_get_port_by_index (lv2plugin, desc->index);
    const gchar *name =
        slv2_value_as_string (slv2_port_get_symbol (lv2plugin, port));
    gst_signal_processor_class_add_pad_template (gsp_class, name, GST_PAD_SINK,
        j, 1);
  }

  /* add non-grouped output port pad templates */
  for (j = 0; j < gsp_class->num_audio_out; ++j) {
    struct _GstLV2Port *desc =
        &g_array_index (klass->audio_out_ports, GstLV2Port, j);
    SLV2Port port = slv2_plugin_get_port_by_index (lv2plugin, desc->index);
    const gchar *name =
        slv2_value_as_string (slv2_port_get_symbol (lv2plugin, port));
    gst_signal_processor_class_add_pad_template (gsp_class, name, GST_PAD_SRC,
        j, 1);
  }

  val = slv2_plugin_get_name (lv2plugin);
  if (val) {
    longname = g_strdup (slv2_value_as_string (val));
    slv2_value_free (val);
  } else {
    longname = g_strdup ("no description available");
  }
  val = slv2_plugin_get_author_name (lv2plugin);
  if (val) {
    author = g_strdup (slv2_value_as_string (val));
    slv2_value_free (val);
  } else {
    author = g_strdup ("no author available");
  }

  if (gsp_class->num_audio_in == 0)
    klass_tags = "Source/Audio/LV2";
  else if (gsp_class->num_audio_out == 0) {
    if (gsp_class->num_control_out == 0)
      klass_tags = "Sink/Audio/LV2";
    else
      klass_tags = "Sink/Analyzer/Audio/LV2";
  } else
    klass_tags = "Filter/Effect/Audio/LV2";

  GST_INFO ("tags : %s", klass_tags);
  gst_element_class_set_metadata (element_class, longname,
      klass_tags, longname, author);
  g_free (longname);
  g_free (author);

  if (!slv2_plugin_has_feature (lv2plugin, in_place_broken_pred))
    GST_SIGNAL_PROCESSOR_CLASS_SET_CAN_PROCESS_IN_PLACE (klass);

  klass->plugin = lv2plugin;
}

static gchar *
gst_lv2_class_get_param_name (GstLV2Class * klass, SLV2Port port)
{
  SLV2Plugin lv2plugin = klass->plugin;
  gchar *ret;

  ret = g_strdup (slv2_value_as_string (slv2_port_get_symbol (lv2plugin,
              port)));

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
      slv2_value_as_string (slv2_port_get_symbol (lv2plugin, port)));

  return ret;
}

static gchar *
gst_lv2_class_get_param_nick (GstLV2Class * klass, SLV2Port port)
{
  SLV2Plugin lv2plugin = klass->plugin;

  return g_strdup (slv2_value_as_string (slv2_port_get_name (lv2plugin, port)));
}

static GParamSpec *
gst_lv2_class_get_param_spec (GstLV2Class * klass, gint portnum)
{
  SLV2Plugin lv2plugin = klass->plugin;
  SLV2Port port = slv2_plugin_get_port_by_index (lv2plugin, portnum);
  SLV2Value lv2def, lv2min, lv2max;
  GParamSpec *ret;
  gchar *name, *nick;
  gint perms;
  gfloat lower = 0.0f, upper = 1.0f, def = 0.0f;

  nick = gst_lv2_class_get_param_nick (klass, port);
  name = gst_lv2_class_get_param_name (klass, port);

  GST_DEBUG ("%s trying port %s : %s",
      slv2_value_as_string (slv2_plugin_get_uri (lv2plugin)), name, nick);

  perms = G_PARAM_READABLE;
  if (slv2_port_is_a (lv2plugin, port, input_class))
    perms |= G_PARAM_WRITABLE | G_PARAM_CONSTRUCT;
  if (slv2_port_is_a (lv2plugin, port, control_class))
    perms |= GST_PARAM_CONTROLLABLE;

  if (slv2_port_has_property (lv2plugin, port, toggled_prop)) {
    ret = g_param_spec_boolean (name, nick, nick, FALSE, perms);
    goto done;
  }

  slv2_port_get_range (lv2plugin, port, &lv2def, &lv2min, &lv2max);

  if (lv2def)
    def = slv2_value_as_float (lv2def);
  if (lv2min)
    lower = slv2_value_as_float (lv2min);
  if (lv2max)
    upper = slv2_value_as_float (lv2max);

  slv2_value_free (lv2def);
  slv2_value_free (lv2min);
  slv2_value_free (lv2max);

  if (def < lower) {
    GST_WARNING ("%s has lower bound %f > default %f",
        slv2_value_as_string (slv2_plugin_get_uri (lv2plugin)), lower, def);
    lower = def;
  }

  if (def > upper) {
    GST_WARNING ("%s has upper bound %f < default %f",
        slv2_value_as_string (slv2_plugin_get_uri (lv2plugin)), upper, def);
    upper = def;
  }

  if (slv2_port_has_property (lv2plugin, port, integer_prop))
    ret = g_param_spec_int (name, nick, nick, lower, upper, def, perms);
  else
    ret = g_param_spec_float (name, nick, nick, lower, upper, def, perms);

done:
  g_free (name);
  g_free (nick);

  return ret;
}

static void
gst_lv2_class_init (GstLV2Class * klass, SLV2Plugin lv2plugin)
{
  GObjectClass *gobject_class;
  GstSignalProcessorClass *gsp_class;
  GParamSpec *p;
  gint i, ix;

  GST_DEBUG ("class_init %p", klass);

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_lv2_set_property;
  gobject_class->get_property = gst_lv2_get_property;

  gsp_class = GST_SIGNAL_PROCESSOR_CLASS (klass);
  gsp_class->setup = gst_lv2_setup;
  gsp_class->start = gst_lv2_start;
  gsp_class->stop = gst_lv2_stop;
  gsp_class->cleanup = gst_lv2_cleanup;
  gsp_class->process = gst_lv2_process;

  klass->plugin = lv2plugin;

  /* properties have an offset of 1 */
  ix = 1;

  /* register properties */

  for (i = 0; i < gsp_class->num_control_in; i++, ix++) {
    p = gst_lv2_class_get_param_spec (klass,
        g_array_index (klass->control_in_ports, GstLV2Port, i).index);

    g_object_class_install_property (gobject_class, ix, p);
  }

  for (i = 0; i < gsp_class->num_control_out; i++, ix++) {
    p = gst_lv2_class_get_param_spec (klass,
        g_array_index (klass->control_out_ports, GstLV2Port, i).index);

    g_object_class_install_property (gobject_class, ix, p);
  }
}

static void
gst_lv2_init (GstLV2 * lv2, GstLV2Class * klass)
{
  lv2->plugin = klass->plugin;
  lv2->instance = NULL;
  lv2->activated = FALSE;
}

static void
gst_lv2_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSignalProcessor *gsp;
  GstSignalProcessorClass *gsp_class;

  gsp = GST_SIGNAL_PROCESSOR (object);
  gsp_class = GST_SIGNAL_PROCESSOR_GET_CLASS (object);

  /* remember, properties have an offset of 1 */
  prop_id--;

  /* only input ports */
  g_return_if_fail (prop_id < gsp_class->num_control_in);

  /* now see what type it is */
  switch (pspec->value_type) {
    case G_TYPE_BOOLEAN:
      gsp->control_in[prop_id] = g_value_get_boolean (value) ? 0.0f : 1.0f;
      break;
    case G_TYPE_INT:
      gsp->control_in[prop_id] = g_value_get_int (value);
      break;
    case G_TYPE_FLOAT:
      gsp->control_in[prop_id] = g_value_get_float (value);
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_lv2_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSignalProcessor *gsp;
  GstSignalProcessorClass *gsp_class;
  gfloat *controls;

  gsp = GST_SIGNAL_PROCESSOR (object);
  gsp_class = GST_SIGNAL_PROCESSOR_GET_CLASS (object);

  /* remember, properties have an offset of 1 */
  prop_id--;

  if (prop_id < gsp_class->num_control_in) {
    controls = gsp->control_in;
  } else if (prop_id < gsp_class->num_control_in + gsp_class->num_control_out) {
    controls = gsp->control_out;
    prop_id -= gsp_class->num_control_in;
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

static gboolean
gst_lv2_setup (GstSignalProcessor * gsp, GstCaps * caps)
{
  GstLV2 *lv2;
  GstLV2Class *oclass;
  GstSignalProcessorClass *gsp_class;
  GstStructure *s;
  gint i;
  GstLV2Group *group = NULL;
  GstAudioChannelPosition *positions = NULL;
  GstPad *pad;

  gsp_class = GST_SIGNAL_PROCESSOR_GET_CLASS (gsp);
  lv2 = (GstLV2 *) gsp;
  oclass = (GstLV2Class *) gsp_class;

  g_return_val_if_fail (lv2->activated == FALSE, FALSE);

  GST_DEBUG_OBJECT (lv2, "instantiating the plugin at %d Hz", gsp->sample_rate);

  if (!(lv2->instance =
          slv2_plugin_instantiate (oclass->plugin, gsp->sample_rate, NULL)))
    goto no_instance;

  /* connect the control ports */
  for (i = 0; i < gsp_class->num_control_in; i++)
    slv2_instance_connect_port (lv2->instance,
        g_array_index (oclass->control_in_ports, GstLV2Port, i).index,
        &(gsp->control_in[i]));
  for (i = 0; i < gsp_class->num_control_out; i++)
    slv2_instance_connect_port (lv2->instance,
        g_array_index (oclass->control_out_ports, GstLV2Port, i).index,
        &(gsp->control_out[i]));

  /* set input group pad audio channel position */
  for (i = 0; i < gsp_class->num_group_in; ++i) {
    group = &g_array_index (oclass->in_groups, GstLV2Group, i);
    if (group->has_roles) {
      if ((positions = gst_lv2_build_positions (group))) {
        if ((pad = gst_element_get_static_pad (GST_ELEMENT (gsp),
                    slv2_value_as_string (group->symbol)))) {
          GST_INFO_OBJECT (lv2, "set audio channel positions on sink pad %s",
              slv2_value_as_string (group->symbol));
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
  for (i = 0; i < gsp_class->num_group_out; ++i) {
    group = &g_array_index (oclass->out_groups, GstLV2Group, i);
    if (group->has_roles) {
      if ((positions = gst_lv2_build_positions (group))) {
        if ((pad = gst_element_get_static_pad (GST_ELEMENT (gsp),
                    slv2_value_as_string (group->symbol)))) {
          GST_INFO_OBJECT (lv2, "set audio channel positions on src pad %s",
              slv2_value_as_string (group->symbol));
          s = gst_caps_get_structure (caps, 0);
          gst_audio_set_channel_positions (s, positions);
          gst_object_unref (pad);
        }
        g_free (positions);
        positions = NULL;
      }
    }
  }
  return TRUE;

no_instance:
  {
    GST_WARNING_OBJECT (gsp, "could not create instance");
    return FALSE;
  }
}

static gboolean
gst_lv2_start (GstSignalProcessor * gsp)
{
  GstLV2 *lv2 = (GstLV2 *) gsp;

  g_return_val_if_fail (lv2->activated == FALSE, FALSE);
  g_return_val_if_fail (lv2->instance != NULL, FALSE);

  GST_DEBUG_OBJECT (lv2, "activating");

  slv2_instance_activate (lv2->instance);

  lv2->activated = TRUE;

  return TRUE;
}

static void
gst_lv2_stop (GstSignalProcessor * gsp)
{
  GstLV2 *lv2 = (GstLV2 *) gsp;

  g_return_if_fail (lv2->activated == TRUE);
  g_return_if_fail (lv2->instance != NULL);

  GST_DEBUG_OBJECT (lv2, "deactivating");

  slv2_instance_deactivate (lv2->instance);

  lv2->activated = FALSE;
}

static void
gst_lv2_cleanup (GstSignalProcessor * gsp)
{
  GstLV2 *lv2 = (GstLV2 *) gsp;

  g_return_if_fail (lv2->activated == FALSE);
  g_return_if_fail (lv2->instance != NULL);

  GST_DEBUG_OBJECT (lv2, "cleaning up");

  slv2_instance_free (lv2->instance);

  lv2->instance = NULL;
}

static void
gst_lv2_process (GstSignalProcessor * gsp, guint nframes)
{
  GstSignalProcessorClass *gsp_class;
  GstLV2 *lv2;
  GstLV2Class *lv2_class;
  GstLV2Group *lv2_group;
  GstLV2Port *lv2_port;
  GstSignalProcessorGroup *gst_group;
  guint i, j;

  gsp_class = GST_SIGNAL_PROCESSOR_GET_CLASS (gsp);
  lv2 = (GstLV2 *) gsp;
  lv2_class = (GstLV2Class *) gsp_class;

  /* multi channel inputs */
  for (i = 0; i < gsp_class->num_group_in; i++) {
    lv2_group = &g_array_index (lv2_class->in_groups, GstLV2Group, i);
    gst_group = &gsp->group_in[i];
    for (j = 0; j < lv2_group->ports->len; ++j) {
      lv2_port = &g_array_index (lv2_group->ports, GstLV2Port, j);
      slv2_instance_connect_port (lv2->instance, lv2_port->index,
          gst_group->buffer + (j * nframes));
    }
  }
  /* mono inputs */
  for (i = 0; i < gsp_class->num_audio_in; i++) {
    lv2_port = &g_array_index (lv2_class->audio_in_ports, GstLV2Port, i);
    slv2_instance_connect_port (lv2->instance, lv2_port->index,
        gsp->audio_in[i]);
  }
  /* multi channel outputs */
  for (i = 0; i < gsp_class->num_group_out; i++) {
    lv2_group = &g_array_index (lv2_class->out_groups, GstLV2Group, i);
    gst_group = &gsp->group_out[i];
    for (j = 0; j < lv2_group->ports->len; ++j) {
      lv2_port = &g_array_index (lv2_group->ports, GstLV2Port, j);
      slv2_instance_connect_port (lv2->instance, lv2_port->index,
          gst_group->buffer + (j * nframes));
    }
  }
  /* mono outputs */
  for (i = 0; i < gsp_class->num_audio_out; i++) {
    lv2_port = &g_array_index (lv2_class->audio_out_ports, GstLV2Port, i);
    slv2_instance_connect_port (lv2->instance, lv2_port->index,
        gsp->audio_out[i]);
  }

  slv2_instance_run (lv2->instance, nframes);
}

/* search the plugin path
 */
static gboolean
lv2_plugin_discover (void)
{
  guint i, j;
  SLV2Plugins plugins = slv2_world_get_all_plugins (world);

  for (i = 0; i < slv2_plugins_size (plugins); ++i) {
    SLV2Plugin lv2plugin = slv2_plugins_get_at (plugins, i);
    gint num_audio_ports = 0;
    const gchar *plugin_uri, *p;
    gchar *type_name;
    GTypeInfo typeinfo = {
      sizeof (GstLV2Class),
      (GBaseInitFunc) gst_lv2_base_init,
      NULL,
      (GClassInitFunc) gst_lv2_class_init,
      NULL,
      lv2plugin,
      sizeof (GstLV2),
      0,
      (GInstanceInitFunc) gst_lv2_init,
    };
    GType type;

    plugin_uri = slv2_value_as_uri (slv2_plugin_get_uri (lv2plugin));
    /* construct the type name from plugin URI */
    if ((p = strstr (plugin_uri, "://"))) {
      /* cut off the protocol (e.g. http://) */
      type_name = g_strdup (&p[3]);
    } else {
      type_name = g_strdup (plugin_uri);
    }
    g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name))
      goto next;

    /* check if this has any audio ports */
    for (j = 0; j < slv2_plugin_get_num_ports (lv2plugin); j++) {
      const SLV2Port port = slv2_plugin_get_port_by_index (lv2plugin, j);
      if (slv2_port_is_a (lv2plugin, port, audio_class)) {
        num_audio_ports++;
      }
    }
    if (!num_audio_ports) {
      GST_INFO ("plugin %s has no audio ports", type_name);
      goto next;
    }

    /* create the type */
    type =
        g_type_register_static (GST_TYPE_SIGNAL_PROCESSOR, type_name, &typeinfo,
        0);

    /* FIXME: not needed anymore when we can add pad templates, etc in class_init
     * as class_data contains the LADSPA_Descriptor too */
    g_type_set_qdata (type, descriptor_quark, (gpointer) lv2plugin);

    if (!gst_element_register (gst_lv2_plugin, type_name, GST_RANK_NONE, type))
      goto next;

  next:
    g_free (type_name);
  }
  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (lv2_debug, "lv2",
      GST_DEBUG_FG_GREEN | GST_DEBUG_BG_BLACK | GST_DEBUG_BOLD, "LV2");

  world = slv2_world_new ();
  slv2_world_load_all (world);

  audio_class = slv2_value_new_uri (world, SLV2_PORT_CLASS_AUDIO);
  control_class = slv2_value_new_uri (world, SLV2_PORT_CLASS_CONTROL);
  input_class = slv2_value_new_uri (world, SLV2_PORT_CLASS_INPUT);
  output_class = slv2_value_new_uri (world, SLV2_PORT_CLASS_OUTPUT);

#define NS_LV2 "http://lv2plug.in/ns/lv2core#"
#define NS_PG  "http://lv2plug.in/ns/ext/port-groups"

  integer_prop = slv2_value_new_uri (world, NS_LV2 "integer");
  toggled_prop = slv2_value_new_uri (world, NS_LV2 "toggled");
  in_place_broken_pred = slv2_value_new_uri (world, NS_LV2 "inPlaceBroken");
  in_group_pred = slv2_value_new_uri (world, NS_PG "inGroup");
  has_role_pred = slv2_value_new_uri (world, NS_PG "role");
  lv2_symbol_pred = slv2_value_new_string (world, NS_LV2 "symbol");

  center_role = slv2_value_new_uri (world, NS_PG "centerChannel");
  left_role = slv2_value_new_uri (world, NS_PG "leftChannel");
  right_role = slv2_value_new_uri (world, NS_PG "rightChannel");
  rear_center_role = slv2_value_new_uri (world, NS_PG "rearCenterChannel");
  rear_left_role = slv2_value_new_uri (world, NS_PG "rearLeftChannel");
  rear_right_role = slv2_value_new_uri (world, NS_PG "rearRightChannel");
  lfe_role = slv2_value_new_uri (world, NS_PG "lfeChannel");
  center_left_role = slv2_value_new_uri (world, NS_PG "centerLeftChannel");
  center_right_role = slv2_value_new_uri (world, NS_PG "centerRightChannel");
  side_left_role = slv2_value_new_uri (world, NS_PG "sideLeftChannel");
  side_right_role = slv2_value_new_uri (world, NS_PG "sideRightChannel");

  gst_plugin_add_dependency_simple (plugin,
      "LV2_PATH", GST_LV2_DEFAULT_PATH, NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  parent_class = g_type_class_ref (GST_TYPE_SIGNAL_PROCESSOR);

  gst_lv2_plugin = plugin;
  descriptor_quark = g_quark_from_static_string ("slv2-plugin");

  /* ensure GstAudioChannelPosition type is registered */
  if (!gst_audio_channel_position_get_type ())
    return FALSE;

  if (!lv2_plugin_discover ()) {
    GST_WARNING ("no lv2 plugins found, check LV2_PATH");
  }

  /* we don't want to fail, even if there are no elements registered */
  return TRUE;
}

#ifdef __GNUC__
__attribute__ ((destructor))
#endif
     static void plugin_cleanup (GstPlugin * plugin)
{
  slv2_value_free (audio_class);
  slv2_value_free (control_class);
  slv2_value_free (input_class);
  slv2_value_free (output_class);

  slv2_value_free (integer_prop);
  slv2_value_free (toggled_prop);
  slv2_value_free (in_place_broken_pred);
  slv2_value_free (in_group_pred);
  slv2_value_free (has_role_pred);
  slv2_value_free (lv2_symbol_pred);

  slv2_value_free (center_role);
  slv2_value_free (left_role);
  slv2_value_free (right_role);
  slv2_value_free (rear_center_role);
  slv2_value_free (rear_left_role);
  slv2_value_free (rear_right_role);
  slv2_value_free (lfe_role);
  slv2_value_free (center_left_role);
  slv2_value_free (center_right_role);
  slv2_value_free (side_left_role);
  slv2_value_free (side_right_role);

  slv2_world_free (world);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    lv2,
    "All LV2 plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
