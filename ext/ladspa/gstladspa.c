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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-ladspa
 * @short_description: bridge for ladspa (Linux Audio Developer's Simple Plugin API)
 * 
 * The ladspa (Linux Audio Developer's Simple Plugin API) element is a bridge
 * for plugins using the <ulink url="http://www.ladspa.org/">ladspa</ulink> API.
 * It scans all installed ladspa plugins and registers them as gstreamer
 * elements. If available it can also parse lrdf files and use the metadata for
 * element classification.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>
#include <gst/audio/audio.h>
#include <gst/controller/gstcontroller.h>

#include "gstladspa.h"
#include <ladspa.h>             /* main ladspa sdk include file */
#ifdef HAVE_LRDF
#include <lrdf.h>
#endif

/* 1.0 and the 1.1 preliminary headers don't define a version, but 1.1 final
   does */
#ifndef LADSPA_VERSION
#define LADSPA_VERSION "1.0"
#endif

#define GST_LADSPA_DEFAULT_PATH \
  "/usr/lib/ladspa" G_SEARCHPATH_SEPARATOR_S \
  "/usr/local/lib/ladspa" G_SEARCHPATH_SEPARATOR_S \
  LIBDIR "/ladspa"

static void gst_ladspa_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ladspa_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_ladspa_setup (GstSignalProcessor * sigproc, GstCaps * caps);
static gboolean gst_ladspa_start (GstSignalProcessor * sigproc);
static void gst_ladspa_stop (GstSignalProcessor * sigproc);
static void gst_ladspa_cleanup (GstSignalProcessor * sigproc);
static void gst_ladspa_process (GstSignalProcessor * sigproc, guint nframes);

static GstSignalProcessorClass *parent_class;

static GstPlugin *ladspa_plugin;

GST_DEBUG_CATEGORY_STATIC (ladspa_debug);
#define GST_CAT_DEFAULT ladspa_debug

static GQuark descriptor_quark = 0;


static void
gst_ladspa_base_init (gpointer g_class)
{
  GstLADSPAClass *klass = (GstLADSPAClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstSignalProcessorClass *gsp_class = GST_SIGNAL_PROCESSOR_CLASS (g_class);
  LADSPA_Descriptor *desc;
  guint j, audio_in_count, audio_out_count, control_in_count, control_out_count;
  const gchar *klass_tags;
  gchar *longname, *author;
#ifdef HAVE_LRDF
  gchar *uri;
#endif
  gchar *extra_klass_tags = NULL;

  GST_DEBUG ("base_init %p", g_class);

  desc = (LADSPA_Descriptor *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      descriptor_quark);
  g_assert (desc);
  klass->descriptor = desc;

  /* pad templates */
  gsp_class->num_audio_in = 0;
  gsp_class->num_audio_out = 0;
  /* properties */
  gsp_class->num_control_in = 0;
  gsp_class->num_control_out = 0;

  for (j = 0; j < desc->PortCount; j++) {
    LADSPA_PortDescriptor p = desc->PortDescriptors[j];

    if (LADSPA_IS_PORT_AUDIO (p)) {
      gchar *name = g_strdup ((gchar *) desc->PortNames[j]);

      /* FIXME: group stereo pairs into a stereo pad
       * ladspa-fx have "XXX (Left)" and "XXX (Right)"
       * where XXX={In,Input,Out,Output}
       */

      GST_DEBUG ("LADSPA port name: \"%s\"", name);
      /* replaces all spaces with underscores, and then remaining special chars
       * with '-'
       * FIXME: why, pads can have any name
       */
      g_strdelimit (name, " ", '_');
      g_strcanon (name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "_-><=", '-');
      GST_DEBUG ("GStreamer pad name: \"%s\"", name);

      if (LADSPA_IS_PORT_INPUT (p))
        gst_signal_processor_class_add_pad_template (gsp_class, name,
            GST_PAD_SINK, gsp_class->num_audio_in++, 1);
      else
        gst_signal_processor_class_add_pad_template (gsp_class, name,
            GST_PAD_SRC, gsp_class->num_audio_out++, 1);

      g_free (name);
    } else if (LADSPA_IS_PORT_CONTROL (p)) {
      if (LADSPA_IS_PORT_INPUT (p))
        gsp_class->num_control_in++;
      else
        gsp_class->num_control_out++;
    }
  }

  longname = g_locale_to_utf8 (desc->Name, -1, NULL, NULL, NULL);
  if (!longname)
    longname = g_strdup ("no description available");
  author = g_locale_to_utf8 (desc->Maker, -1, NULL, NULL, NULL);
  if (!author)
    author = g_strdup ("no author available");

#ifdef HAVE_LRDF
  /* libldrf support, we want to get extra class information here */
  uri = g_strdup_printf (LADSPA_BASE "%ld", desc->UniqueID);
  if (uri) {
    lrdf_statement query = { 0, };
    lrdf_uris *uris;
    gchar *str, *base_type = NULL;

    GST_DEBUG ("uri (id=%d) : %s", desc->UniqueID, uri);
    /* we can take this directly from 'desc', keep this example for future
       attributes. 
       if ((str = lrdf_get_setting_metadata (uri, "title"))) {
       GST_DEBUG ("title : %s", str);
       }
       if ((str = lrdf_get_setting_metadata (uri, "creator"))) {
       GST_DEBUG ("creator : %s", str);
       }
     */

    /* get the rdf:type for this plugin */
    query.subject = uri;
    query.predicate = (char *) RDF_BASE "type";
    query.object = (char *) "?";
    query.next = NULL;
    uris = lrdf_match_multi (&query);
    if (uris) {
      if (uris->count == 1) {
        base_type = g_strdup (uris->items[0]);
        GST_DEBUG ("base_type :  %s", base_type);
      }
      lrdf_free_uris (uris);
    }

    /* query taxonomy */
    if (base_type) {
      uris = lrdf_get_all_superclasses (base_type);
      if (uris) {
        guint32 j;

        for (j = 0; j < uris->count; j++) {
          GST_LOG ("parent_type_uri : %s", uris->items[j]);
          if ((str = lrdf_get_label (uris->items[j]))) {
            GST_DEBUG ("parent_type_label : %s", str);
            if (extra_klass_tags) {
              gchar *old_tags = extra_klass_tags;
              extra_klass_tags = g_strconcat (extra_klass_tags, "/", str, NULL);
              g_free (old_tags);
            } else {
              extra_klass_tags = g_strconcat ("/", str, NULL);
            }
          }
        }
        lrdf_free_uris (uris);
      }
      g_free (base_type);
    }

    /* we can use this for the presets
       uris = lrdf_get_setting_uris (desc->UniqueID);
       if (uris) {
       guint32 j;

       for (j = 0; j < uris->count; j++) {
       GST_INFO ("setting_uri : %s", uris->items[j]);
       if ((str = lrdf_get_label (uris->items[j]))) {
       GST_INFO ("setting_label : %s", str);
       }
       }
       lrdf_free_uris (uris);
       }
     */

  }
  g_free (uri);
#endif

  if (gsp_class->num_audio_in == 0)
    klass_tags = "Source/Audio/LADSPA";
  else if (gsp_class->num_audio_out == 0) {
    if (gsp_class->num_control_out == 0)
      klass_tags = "Sink/Audio/LADSPA";
    else
      klass_tags = "Sink/Analyzer/Audio/LADSPA";
  } else
    klass_tags = "Filter/Effect/Audio/LADSPA";

#ifdef HAVE_LRDF
  if (extra_klass_tags) {
    char *s = g_strconcat (klass_tags, extra_klass_tags, NULL);
    g_free (extra_klass_tags);
    extra_klass_tags = s;
  }
#endif
  GST_INFO ("tags : %s", klass_tags);
  gst_element_class_set_details_simple (element_class, longname,
      extra_klass_tags ? extra_klass_tags : klass_tags, longname, author);
  g_free (longname);
  g_free (author);
  g_free (extra_klass_tags);

  klass->audio_in_portnums = g_new0 (gint, gsp_class->num_audio_in);
  klass->audio_out_portnums = g_new0 (gint, gsp_class->num_audio_out);
  klass->control_in_portnums = g_new0 (gint, gsp_class->num_control_in);
  klass->control_out_portnums = g_new0 (gint, gsp_class->num_control_out);

  audio_in_count = audio_out_count = control_in_count = control_out_count = 0;

  for (j = 0; j < desc->PortCount; j++) {
    LADSPA_PortDescriptor p = desc->PortDescriptors[j];

    if (LADSPA_IS_PORT_AUDIO (p)) {
      if (LADSPA_IS_PORT_INPUT (p))
        klass->audio_in_portnums[audio_in_count++] = j;
      else
        klass->audio_out_portnums[audio_out_count++] = j;
    } else if (LADSPA_IS_PORT_CONTROL (p)) {
      if (LADSPA_IS_PORT_INPUT (p))
        klass->control_in_portnums[control_in_count++] = j;
      else
        klass->control_out_portnums[control_out_count++] = j;
    }
  }

  g_assert (audio_in_count == gsp_class->num_audio_in);
  g_assert (audio_out_count == gsp_class->num_audio_out);
  g_assert (control_in_count == gsp_class->num_control_in);
  g_assert (control_out_count == gsp_class->num_control_out);

  if (!LADSPA_IS_INPLACE_BROKEN (desc->Properties))
    GST_SIGNAL_PROCESSOR_CLASS_SET_CAN_PROCESS_IN_PLACE (klass);

  klass->descriptor = desc;
}

static gchar *
gst_ladspa_class_get_param_name (GstLADSPAClass * klass, gint portnum)
{
  LADSPA_Descriptor *desc;
  gchar *ret, *paren;

  desc = klass->descriptor;

  ret = g_strdup (desc->PortNames[portnum]);

  paren = g_strrstr (ret, " (");
  if (paren != NULL)
    *paren = '\0';

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
      desc->PortNames[portnum]);

  return ret;
}

static GParamSpec *
gst_ladspa_class_get_param_spec (GstLADSPAClass * klass, gint portnum)
{
  LADSPA_Descriptor *desc;
  GParamSpec *ret;
  gchar *name;
  gint hintdesc, perms;
  gfloat lower, upper, def;

  desc = klass->descriptor;

  name = gst_ladspa_class_get_param_name (klass, portnum);
  perms = G_PARAM_READABLE;
  if (LADSPA_IS_PORT_INPUT (desc->PortDescriptors[portnum]))
    perms |= G_PARAM_WRITABLE | G_PARAM_CONSTRUCT;
  if (LADSPA_IS_PORT_CONTROL (desc->PortDescriptors[portnum]))
    perms |= GST_PARAM_CONTROLLABLE;

  /* short name for hint descriptor */
  hintdesc = desc->PortRangeHints[portnum].HintDescriptor;

  if (LADSPA_IS_HINT_TOGGLED (hintdesc)) {
    ret = g_param_spec_boolean (name, name, name, FALSE, perms);
    g_free (name);
    return ret;
  }

  if (LADSPA_IS_HINT_BOUNDED_BELOW (hintdesc))
    lower = desc->PortRangeHints[portnum].LowerBound;
  else
    lower = -G_MAXFLOAT;

  if (LADSPA_IS_HINT_BOUNDED_ABOVE (hintdesc))
    upper = desc->PortRangeHints[portnum].UpperBound;
  else
    upper = G_MAXFLOAT;

  if (LADSPA_IS_HINT_SAMPLE_RATE (hintdesc)) {
    /* FIXME! */
    lower *= 44100;
    upper *= 44100;
  }

  if (LADSPA_IS_HINT_INTEGER (hintdesc)) {
    lower = CLAMP (lower, G_MININT, G_MAXINT);
    upper = CLAMP (upper, G_MININT, G_MAXINT);
  }

  /* default to lower bound */
  def = lower;

#ifdef LADSPA_IS_HINT_HAS_DEFAULT
  if (LADSPA_IS_HINT_HAS_DEFAULT (hintdesc)) {
    if (LADSPA_IS_HINT_DEFAULT_0 (hintdesc))
      def = 0.0;
    else if (LADSPA_IS_HINT_DEFAULT_1 (hintdesc))
      def = 1.0;
    else if (LADSPA_IS_HINT_DEFAULT_100 (hintdesc))
      def = 100.0;
    else if (LADSPA_IS_HINT_DEFAULT_440 (hintdesc))
      def = 440.0;
    if (LADSPA_IS_HINT_DEFAULT_MINIMUM (hintdesc))
      def = lower;
    else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM (hintdesc))
      def = upper;
    else if (LADSPA_IS_HINT_LOGARITHMIC (hintdesc)) {
      if (LADSPA_IS_HINT_DEFAULT_LOW (hintdesc))
        def = exp (0.75 * log (lower) + 0.25 * log (upper));
      else if (LADSPA_IS_HINT_DEFAULT_MIDDLE (hintdesc))
        def = exp (0.5 * log (lower) + 0.5 * log (upper));
      else if (LADSPA_IS_HINT_DEFAULT_HIGH (hintdesc))
        def = exp (0.25 * log (lower) + 0.75 * log (upper));
    } else {
      if (LADSPA_IS_HINT_DEFAULT_LOW (hintdesc))
        def = 0.75 * lower + 0.25 * upper;
      else if (LADSPA_IS_HINT_DEFAULT_MIDDLE (hintdesc))
        def = 0.5 * lower + 0.5 * upper;
      else if (LADSPA_IS_HINT_DEFAULT_HIGH (hintdesc))
        def = 0.25 * lower + 0.75 * upper;
    }
  }
#endif /* LADSPA_IS_HINT_HAS_DEFAULT */

  if (lower > upper) {
    gfloat tmp;

    /* silently swap */
    tmp = lower;
    lower = upper;
    upper = tmp;
  }

  def = CLAMP (def, lower, upper);

  if (LADSPA_IS_HINT_INTEGER (hintdesc)) {
    ret = g_param_spec_int (name, name, name, lower, upper, def, perms);
  } else {
    ret = g_param_spec_float (name, name, name, lower, upper, def, perms);
  }

  g_free (name);

  return ret;
}

static void
gst_ladspa_class_init (GstLADSPAClass * klass, LADSPA_Descriptor * desc)
{
  GObjectClass *gobject_class;
  GstSignalProcessorClass *gsp_class;
  GParamSpec *p;
  gint i, ix;

  GST_DEBUG ("class_init %p", klass);

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_ladspa_set_property;
  gobject_class->get_property = gst_ladspa_get_property;

  gsp_class = GST_SIGNAL_PROCESSOR_CLASS (klass);
  gsp_class->setup = gst_ladspa_setup;
  gsp_class->start = gst_ladspa_start;
  gsp_class->stop = gst_ladspa_stop;
  gsp_class->cleanup = gst_ladspa_cleanup;
  gsp_class->process = gst_ladspa_process;

  /* properties have an offset of 1 */
  ix = 1;

  /* register properties */

  for (i = 0; i < gsp_class->num_control_in; i++, ix++) {
    p = gst_ladspa_class_get_param_spec (klass, klass->control_in_portnums[i]);
    g_object_class_install_property (gobject_class, ix, p);
  }

  for (i = 0; i < gsp_class->num_control_out; i++, ix++) {
    p = gst_ladspa_class_get_param_spec (klass, klass->control_out_portnums[i]);
    g_object_class_install_property (gobject_class, ix, p);
  }
}

static void
gst_ladspa_init (GstLADSPA * ladspa, GstLADSPAClass * klass)
{
  ladspa->descriptor = klass->descriptor;
  ladspa->activated = FALSE;
  ladspa->inplace_broken =
      LADSPA_IS_INPLACE_BROKEN (ladspa->descriptor->Properties);
}

static void
gst_ladspa_set_property (GObject * object, guint prop_id, const GValue * value,
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
      gsp->control_in[prop_id] = g_value_get_boolean (value) ? 1.f : 0.f;
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
gst_ladspa_get_property (GObject * object, guint prop_id, GValue * value,
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
      g_value_set_boolean (value, controls[prop_id] > 0.5);
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
gst_ladspa_setup (GstSignalProcessor * gsp, GstCaps * caps)
{
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;
  GstSignalProcessorClass *gsp_class;
  LADSPA_Descriptor *desc;
  gint i;

  gsp_class = GST_SIGNAL_PROCESSOR_GET_CLASS (gsp);
  ladspa = (GstLADSPA *) gsp;
  oclass = (GstLADSPAClass *) gsp_class;
  desc = ladspa->descriptor;

  g_return_val_if_fail (ladspa->handle == NULL, FALSE);
  g_return_val_if_fail (ladspa->activated == FALSE, FALSE);

  GST_DEBUG_OBJECT (ladspa, "instantiating the plugin at %d Hz",
      gsp->sample_rate);

  if (!(ladspa->handle = desc->instantiate (desc, gsp->sample_rate)))
    goto no_instance;

  /* connect the control ports */
  for (i = 0; i < gsp_class->num_control_in; i++)
    desc->connect_port (ladspa->handle,
        oclass->control_in_portnums[i], &(gsp->control_in[i]));
  for (i = 0; i < gsp_class->num_control_out; i++)
    desc->connect_port (ladspa->handle,
        oclass->control_out_portnums[i], &(gsp->control_out[i]));

  return TRUE;

no_instance:
  {
    GST_WARNING_OBJECT (gsp, "could not create instance");
    return FALSE;
  }
}

static gboolean
gst_ladspa_start (GstSignalProcessor * gsp)
{
  GstLADSPA *ladspa;
  LADSPA_Descriptor *desc;

  ladspa = (GstLADSPA *) gsp;
  desc = ladspa->descriptor;

  g_return_val_if_fail (ladspa->activated == FALSE, FALSE);
  g_return_val_if_fail (ladspa->handle != NULL, FALSE);

  GST_DEBUG_OBJECT (ladspa, "activating");

  if (desc->activate)
    desc->activate (ladspa->handle);

  ladspa->activated = TRUE;

  return TRUE;
}

static void
gst_ladspa_stop (GstSignalProcessor * gsp)
{
  GstLADSPA *ladspa;
  LADSPA_Descriptor *desc;

  ladspa = (GstLADSPA *) gsp;
  desc = ladspa->descriptor;

  g_return_if_fail (ladspa->activated == TRUE);
  g_return_if_fail (ladspa->handle != NULL);

  GST_DEBUG_OBJECT (ladspa, "deactivating");

  if (desc->activate)
    desc->activate (ladspa->handle);

  ladspa->activated = FALSE;
}

static void
gst_ladspa_cleanup (GstSignalProcessor * gsp)
{
  GstLADSPA *ladspa;
  LADSPA_Descriptor *desc;

  ladspa = (GstLADSPA *) gsp;
  desc = ladspa->descriptor;

  g_return_if_fail (ladspa->activated == FALSE);
  g_return_if_fail (ladspa->handle != NULL);

  GST_DEBUG_OBJECT (ladspa, "cleaning up");

  if (desc->cleanup)
    desc->cleanup (ladspa->handle);

  ladspa->handle = NULL;
}

static void
gst_ladspa_process (GstSignalProcessor * gsp, guint nframes)
{
  GstSignalProcessorClass *gsp_class;
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;
  LADSPA_Descriptor *desc;
  guint i;

  gsp_class = GST_SIGNAL_PROCESSOR_GET_CLASS (gsp);
  ladspa = (GstLADSPA *) gsp;
  oclass = (GstLADSPAClass *) gsp_class;
  desc = ladspa->descriptor;

  for (i = 0; i < gsp_class->num_audio_in; i++)
    desc->connect_port (ladspa->handle, oclass->audio_in_portnums[i],
        gsp->audio_in[i]);
  for (i = 0; i < gsp_class->num_audio_out; i++)
    desc->connect_port (ladspa->handle, oclass->audio_out_portnums[i],
        gsp->audio_out[i]);

  desc->run (ladspa->handle, nframes);
}

static void
ladspa_describe_plugin (LADSPA_Descriptor_Function descriptor_function)
{
  const LADSPA_Descriptor *desc;
  gint i;

  /* walk through all the plugins in this pluginlibrary */
  i = 0;
  while ((desc = descriptor_function (i++))) {
    gchar *type_name;
    GTypeInfo typeinfo = {
      sizeof (GstLADSPAClass),
      (GBaseInitFunc) gst_ladspa_base_init,
      NULL,
      (GClassInitFunc) gst_ladspa_class_init,
      NULL,
      desc,
      sizeof (GstLADSPA),
      0,
      (GInstanceInitFunc) gst_ladspa_init,
    };
    GType type;
    /* construct the type */
    type_name = g_strdup_printf ("ladspa-%s", desc->Label);
    g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');
    /* if it's already registered, drop it */
    if (g_type_from_name (type_name))
      goto next;

    /* create the type now */
    type =
        g_type_register_static (GST_TYPE_SIGNAL_PROCESSOR, type_name, &typeinfo,
        0);
    /* FIXME: not needed anymore when we can add pad templates, etc in class_init
     * as class_data contains the LADSPA_Descriptor too */
    g_type_set_qdata (type, descriptor_quark, (gpointer) desc);

    if (!gst_element_register (ladspa_plugin, type_name, GST_RANK_NONE, type))
      goto next;

  next:
    g_free (type_name);
  }
}

#ifdef HAVE_LRDF
static gboolean
ladspa_rdf_directory_search (const char *dir_name)
{
  GDir *dir;
  gchar *file_name, *file_uri;
  const gchar *entry_name;
  gint ok;

  GST_INFO ("scanning directory for rdfs \"%s\"", dir_name);

  dir = g_dir_open (dir_name, 0, NULL);
  if (!dir)
    return FALSE;

  while ((entry_name = g_dir_read_name (dir))) {
    file_name = g_build_filename (dir_name, entry_name, NULL);
    file_uri = g_strconcat ("file://", file_name, NULL);
    ok = lrdf_read_file (file_uri);
    GST_INFO ("read %s : %d", file_uri, ok);
    g_free (file_uri);
    g_free (file_name);
  }
  g_dir_close (dir);

  return TRUE;
}

#endif

/* search just the one directory.
 */
static gboolean
ladspa_plugin_directory_search (const char *dir_name)
{
  GDir *dir;
  gchar *file_name;
  const gchar *entry_name;
  LADSPA_Descriptor_Function descriptor_function;
  GModule *plugin;
  gboolean ok = FALSE;

  GST_INFO ("scanning directory for plugins \"%s\"", dir_name);

  dir = g_dir_open (dir_name, 0, NULL);
  if (!dir)
    return FALSE;

  while ((entry_name = g_dir_read_name (dir))) {
    file_name = g_build_filename (dir_name, entry_name, NULL);
    plugin =
        g_module_open (file_name, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (plugin) {
      /* the file is a shared library */
      if (g_module_symbol (plugin, "ladspa_descriptor",
              (gpointer *) & descriptor_function)) {
        /* we've found a ladspa_descriptor function, now introspect it. */
        GST_INFO ("describe %s", file_name);
        ladspa_describe_plugin (descriptor_function);
        ok = TRUE;
      } else {
        /* it was a library, but not a LADSPA one. Unload it. */
        g_module_close (plugin);
      }
    }
    g_free (file_name);
  }
  g_dir_close (dir);

  return ok;
}

/* search the plugin path
 */
static gboolean
ladspa_plugin_path_search (void)
{
  const gchar *search_path;
  gchar *ladspa_path;
  gchar **paths;
  gint i, j, path_entries;
  gboolean res = FALSE, skip;
#ifdef HAVE_LRDF
  gchar *pos, *prefix, *rdf_path;
#endif

  search_path = g_getenv ("LADSPA_PATH");
  if (search_path) {
    ladspa_path =
        g_strdup_printf ("%s" G_SEARCHPATH_SEPARATOR_S GST_LADSPA_DEFAULT_PATH,
        search_path);
  } else {
    ladspa_path = g_strdup (GST_LADSPA_DEFAULT_PATH);
  }

  paths = g_strsplit (ladspa_path, G_SEARCHPATH_SEPARATOR_S, 0);
  path_entries = g_strv_length (paths);
  GST_INFO ("%d dirs in search paths \"%s\"", path_entries, ladspa_path);

#ifdef HAVE_LRDF
  for (i = 0; i < path_entries; i++) {
    skip = FALSE;
    for (j = 0; j < i; j++) {
      if (!strcmp (paths[i], paths[j])) {
        skip = TRUE;
        break;
      }
    }
    if (skip)
      break;
    /* transform path: /usr/lib/ladspa -> /usr/share/ladspa/rdf/
     * yes, this is ugly, but lrdf has not searchpath
     */
    if ((pos = strstr (paths[i], "/lib/ladspa"))) {
      prefix = g_strndup (paths[i], (pos - paths[i]));
      rdf_path = g_build_filename (prefix, "share", "ladspa", "rdf", NULL);
      ladspa_rdf_directory_search (rdf_path);
      g_free (rdf_path);
      g_free (prefix);
    }
  }
#endif

  for (i = 0; i < path_entries; i++) {
    skip = FALSE;
    for (j = 0; j < i; j++) {
      if (!strcmp (paths[i], paths[j])) {
        skip = TRUE;
        break;
      }
    }
    if (skip)
      break;
    res |= ladspa_plugin_directory_search (paths[i]);
  }
  g_strfreev (paths);

  g_free (ladspa_path);

  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ladspa_debug, "ladspa",
      GST_DEBUG_FG_GREEN | GST_DEBUG_BG_BLACK | GST_DEBUG_BOLD, "LADSPA");

  /* initialize gst controller library */
  gst_controller_init (NULL, NULL);

  gst_plugin_add_dependency_simple (plugin,
      "LADSPA_PATH",
      GST_LADSPA_DEFAULT_PATH, NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);

#ifdef HAVE_LRDF
  lrdf_init ();
#endif

  parent_class = g_type_class_ref (GST_TYPE_SIGNAL_PROCESSOR);

  ladspa_plugin = plugin;
  descriptor_quark = g_quark_from_static_string ("ladspa-descriptor");

  if (!ladspa_plugin_path_search ()) {
    GST_WARNING ("no ladspa plugins found, check LADSPA_PATH");
  }

  /* we don't want to fail, even if there are no elements registered */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ladspa",
    "All LADSPA plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
