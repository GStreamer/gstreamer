/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <tsaunier@gnome.org>
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
 * SECTION:ges-track-effect
 * @short_description: adds an effect to a stream in a #GESTimelineSource or a
 * #GESTimelineLayer
 *
 * Since: 0.10.2
 */

#include <glib/gprintf.h>
#include <string.h>

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-effect.h"

G_DEFINE_ABSTRACT_TYPE (GESTrackEffect, ges_track_effect,
    GES_TYPE_TRACK_OPERATION);

static GHashTable *ges_track_effect_get_props_hashtable (GESTrackObject * self);
guint pspec_hash (gconstpointer key_spec);
static gboolean pspec_equal (gconstpointer key_spec_1,
    gconstpointer key_spec_2);

struct _GESTrackEffectPrivate
{
  void *nothing;
};

static void
ges_track_effect_class_init (GESTrackEffectClass * klass)
{
  GESTrackObjectClass *obj_bg_class = GES_TRACK_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackEffectPrivate));

  obj_bg_class->get_props_hastable = ges_track_effect_get_props_hashtable;
}

static void
ges_track_effect_init (GESTrackEffect * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GES_TYPE_TRACK_EFFECT,
      GESTrackEffectPrivate);
}

static gboolean
pspec_equal (gconstpointer key_spec_1, gconstpointer key_spec_2)
{
  const GParamSpec *key1 = key_spec_1;
  const GParamSpec *key2 = key_spec_2;

  return (key1->owner_type == key2->owner_type &&
      strcmp (key1->name, key2->name) == 0);
}

guint
pspec_hash (gconstpointer key_spec)
{
  const GParamSpec *key = key_spec;
  const gchar *p;
  guint h = key->owner_type;

  for (p = key->name; *p; p++)
    h = (h << 5) - h + *p;

  return h;
}

/*  Virtual methods */
static GHashTable *
ges_track_effect_get_props_hashtable (GESTrackObject * self)
{
  gpointer data;
  GstIterator *it;
  GParamSpec **parray;
  GObjectClass *class;
  guint i, nb_specs;
  const gchar *klass;
  GstElementFactory *factory;
  GstElement *child, *element;
  gchar **categories, *categorie;

  gboolean done = FALSE;
  GHashTable *ret = NULL;

  element = ges_track_object_get_element (self);
  if (!element) {
    GST_DEBUG
        ("Can't build the property hashtable until the gnlobject is created");
    return NULL;
  }

  ret = g_hash_table_new_full ((GHashFunc) pspec_hash, pspec_equal,
      (GDestroyNotify) g_param_spec_unref, g_object_unref);

  /*  We go over child elements recursivly, and add writable properties to the
   *  hashtable
   *  FIXME: Add a blacklist of properties */
  it = gst_bin_iterate_recurse (GST_BIN (element));
  while (!done) {
    switch (gst_iterator_next (it, &data)) {
      case GST_ITERATOR_OK:
        child = GST_ELEMENT_CAST (data);
        factory = gst_element_get_factory (child);
        klass = gst_element_factory_get_klass (factory);
        categories = g_strsplit (klass, "/", 0);

        i = 0;
        for (categorie = categories[0]; categorie;) {
          if (g_strcmp0 (categorie, "Effect") == 0) {

            class = G_OBJECT_GET_CLASS (child);
            parray = g_object_class_list_properties (class, &nb_specs);
            for (i = 0; i < nb_specs; i++) {
              if (parray[i]->flags & G_PARAM_WRITABLE) {
                g_hash_table_insert (ret, g_param_spec_ref (parray[i]),
                    g_object_ref (child));
              }
            }
            g_free (parray);

            GST_DEBUG ("%i configurable properties added to %p", child,
                nb_specs);
            gst_object_unref (child);
            break;
          }
          i++;
          categorie = categories[i];
        }
        g_strfreev (categories);
        break;

      case GST_ITERATOR_RESYNC:
        GST_DEBUG ("iterator resync");
        gst_iterator_resync (it);
        break;

      case GST_ITERATOR_DONE:
        GST_DEBUG ("iterator done");
        done = TRUE;
        break;

      default:
        break;
    }
  }
  gst_iterator_free (it);

  return ret;
}
