/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * gstfragment.c:
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

#include <glib.h>
#include <gst/base/gstadapter.h>
#include "gstfragmented.h"
#include "gstfragment.h"

#define GST_CAT_DEFAULT fragmented_debug

#define GST_FRAGMENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_FRAGMENT, GstFragmentPrivate))

enum
{
  PROP_0,
  PROP_INDEX,
  PROP_NAME,
  PROP_DURATION,
  PROP_DISCONTINOUS,
  PROP_LAST
};

struct _GstFragmentPrivate
{
  GstAdapter *adapter;
  GstBuffer *buffer;
};

G_DEFINE_TYPE (GstFragment, gst_fragment, G_TYPE_OBJECT);

static void gst_fragment_dispose (GObject * object);
static void gst_fragment_finalize (GObject * object);

static void
gst_fragment_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GstFragment *fragment = GST_FRAGMENT (object);

  switch (property_id) {
    case PROP_INDEX:
      g_value_set_uint (value, fragment->index);
      break;

    case PROP_NAME:
      g_value_set_string (value, fragment->name);
      break;

    case PROP_DURATION:
      g_value_set_uint64 (value, fragment->stop_time - fragment->start_time);
      break;

    case PROP_DISCONTINOUS:
      g_value_set_boolean (value, fragment->discontinuous);
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_fragment_class_init (GstFragmentClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstFragmentPrivate));

  gobject_class->get_property = gst_fragment_get_property;
  gobject_class->dispose = gst_fragment_dispose;
  gobject_class->finalize = gst_fragment_finalize;

  g_object_class_install_property (gobject_class, PROP_INDEX,
      g_param_spec_uint ("index", "Index", "Index of the fragment", 0,
          G_MAXUINT, 0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_NAME,
      g_param_spec_string ("name", "Name",
          "Name of the fragment (eg:fragment-12.ts)", NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_DISCONTINOUS,
      g_param_spec_boolean ("discontinuous", "Discontinous",
          "Whether this fragment has a discontinuity or not",
          FALSE, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_DURATION,
      g_param_spec_uint64 ("duration", "Fragment duration",
          "Duration of the fragment", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
}

static void
gst_fragment_init (GstFragment * fragment)
{
  GstFragmentPrivate *priv;

  fragment->priv = priv = GST_FRAGMENT_GET_PRIVATE (fragment);

  priv->adapter = gst_adapter_new ();
  fragment->download_start_time = gst_util_get_timestamp ();
  fragment->start_time = 0;
  fragment->stop_time = 0;
  fragment->index = 0;
  fragment->name = g_strdup ("");
  fragment->completed = FALSE;
  fragment->discontinuous = FALSE;
}

GstFragment *
gst_fragment_new (void)
{
  return GST_FRAGMENT (g_object_new (GST_TYPE_FRAGMENT, NULL));
}

static void
gst_fragment_finalize (GObject * gobject)
{
  GstFragment *fragment = GST_FRAGMENT (gobject);

  g_free (fragment->name);

  G_OBJECT_CLASS (gst_fragment_parent_class)->finalize (gobject);
}

void
gst_fragment_dispose (GObject * object)
{
  GstFragmentPrivate *priv = GST_FRAGMENT (object)->priv;

  if (priv->adapter) {
    gst_object_unref (priv->adapter);
    priv->adapter = NULL;
  }
  if (priv->buffer) {
    gst_buffer_unref (priv->buffer);
    priv->buffer = NULL;
  }

  G_OBJECT_CLASS (gst_fragment_parent_class)->dispose (object);
}

GstBuffer *
gst_fragment_get_buffer (GstFragment * fragment)
{
  g_return_val_if_fail (fragment != NULL, NULL);

  if (!fragment->completed)
    return NULL;

  if (!fragment->priv->buffer) {
    fragment->priv->buffer = gst_adapter_take_buffer (fragment->priv->adapter,
        gst_adapter_available (fragment->priv->adapter));
  }

  return gst_buffer_ref (fragment->priv->buffer);
}

gboolean
gst_fragment_add_buffer (GstFragment * fragment, GstBuffer * buffer)
{
  g_return_val_if_fail (fragment != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);

  if (fragment->completed) {
    GST_WARNING ("Fragment is completed, could not add more buffers");
    return FALSE;
  }

  GST_DEBUG ("Adding new buffer to the fragment");
  /* We steal the buffers you pass in */
  gst_adapter_push (fragment->priv->adapter, buffer);

  return TRUE;
}

gsize
gst_fragment_get_total_size (GstFragment * fragment)
{
  g_return_val_if_fail (GST_IS_FRAGMENT (fragment), 0);

  if (fragment->priv->buffer)
    return GST_BUFFER_SIZE (fragment->priv->buffer);

  return gst_adapter_available (fragment->priv->adapter);
}
