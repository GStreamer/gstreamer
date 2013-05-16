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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:ges-base-effect
 * @short_description: adds an effect to a stream in a GESSourceClip or a
 * #GESLayer
 */

#include <glib/gprintf.h>

#include "ges-utils.h"
#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-base-effect.h"

G_DEFINE_ABSTRACT_TYPE (GESBaseEffect, ges_base_effect, GES_TYPE_OPERATION);

static GHashTable *ges_base_effect_get_props_hashtable (GESTrackElement * self);

struct _GESBaseEffectPrivate
{
  void *nothing;
};

static void
ges_base_effect_class_init (GESBaseEffectClass * klass)
{
  GESTrackElementClass *obj_bg_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESBaseEffectPrivate));

  obj_bg_class->get_props_hastable = ges_base_effect_get_props_hashtable;
}

static void
ges_base_effect_init (GESBaseEffect * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GES_TYPE_BASE_EFFECT,
      GESBaseEffectPrivate);
}

/*  Virtual methods */
static GHashTable *
ges_base_effect_get_props_hashtable (GESTrackElement * self)
{
  GValue item = { 0, };
  GstIterator *it;
  GParamSpec **parray;
  GObjectClass *class;
  const gchar *klass;
  GstElementFactory *factory;
  GstElement *element;
  gboolean done = FALSE;
  GHashTable *ret = NULL;

  element = ges_track_element_get_element (self);
  if (!element) {
    GST_DEBUG
        ("Can't build the property hashtable until the gnlobject is created");
    return NULL;
  }

  ret = g_hash_table_new_full ((GHashFunc) pspec_hash, pspec_equal,
      (GDestroyNotify) g_param_spec_unref, gst_object_unref);

  /*  We go over child elements recursivly, and add writable properties to the
   *  hashtable
   *  FIXME: Add a blacklist of properties */
  it = gst_bin_iterate_recurse (GST_BIN (element));

  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
      {
        gchar **categories;
        guint category;
        GstElement *child = g_value_get_object (&item);

        factory = gst_element_get_factory (child);
        klass = gst_element_factory_get_metadata (factory,
            GST_ELEMENT_METADATA_KLASS);

        GST_DEBUG ("Looking at element '%s' of klass '%s'",
            GST_ELEMENT_NAME (child), klass);

        categories = g_strsplit (klass, "/", 0);

        for (category = 0; categories[category]; category++) {
          if (g_strcmp0 (categories[category], "Effect") == 0) {
            guint i, nb_specs;

            class = G_OBJECT_GET_CLASS (child);
            parray = g_object_class_list_properties (class, &nb_specs);
            for (i = 0; i < nb_specs; i++) {
              if (parray[i]->flags & G_PARAM_WRITABLE) {
                g_hash_table_insert (ret, g_param_spec_ref (parray[i]),
                    gst_object_ref (child));
              }
            }
            g_free (parray);

            GST_DEBUG
                ("%d configurable properties of '%s' added to property hashtable",
                nb_specs, GST_ELEMENT_NAME (child));
            break;
          }
        }

        g_strfreev (categories);
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        /* FIXME, properly restart the process */
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
  g_value_unset (&item);
  gst_iterator_free (it);

  return ret;
}
