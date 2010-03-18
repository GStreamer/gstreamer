/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2005 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-gconfaudiosrc
 * @see_also: #GstAlsaSrc, #GstAutoAudioSrc
 *
 * This element records sound from the audiosink that has been configured in
 * GConf by the user.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch gconfaudiosrc ! audioconvert ! wavenc ! filesink location=record.wav
 * ]| Record from configured audioinput
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstgconfelements.h"
#include "gstgconfaudiosrc.h"

static void gst_gconf_audio_src_dispose (GObject * object);
static void gst_gconf_audio_src_finalize (GstGConfAudioSrc * src);
static void cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);
static GstStateChangeReturn
gst_gconf_audio_src_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstGConfAudioSrc, gst_gconf_audio_src, GstBin, GST_TYPE_BIN);

static void
gst_gconf_audio_src_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details_simple (eklass, "GConf audio source",
      "Source/Audio",
      "Audio source embedding the GConf-settings for audio input",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");
}

static void
gst_gconf_audio_src_class_init (GstGConfAudioSrcClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->dispose = gst_gconf_audio_src_dispose;
  oklass->finalize = (GObjectFinalizeFunc) gst_gconf_audio_src_finalize;
  eklass->change_state = gst_gconf_audio_src_change_state;
}

/*
 * Hack to make negotiation work.
 */

static gboolean
gst_gconf_audio_src_reset (GstGConfAudioSrc * src)
{
  GstPad *targetpad;

  /* fakesrc */
  if (src->kid) {
    gst_element_set_state (src->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), src->kid);
  }
  src->kid = gst_element_factory_make ("fakesrc", "testsrc");
  if (!src->kid) {
    GST_ERROR_OBJECT (src, "Failed to create fakesrc");
    return FALSE;
  }
  gst_bin_add (GST_BIN (src), src->kid);

  targetpad = gst_element_get_static_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);

  g_free (src->gconf_str);
  src->gconf_str = NULL;
  return TRUE;
}

static void
gst_gconf_audio_src_init (GstGConfAudioSrc * src,
    GstGConfAudioSrcClass * g_class)
{
  src->pad = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (src), src->pad);

  gst_gconf_audio_src_reset (src);

  src->client = gconf_client_get_default ();
  gconf_client_add_dir (src->client, GST_GCONF_DIR,
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
  src->gconf_notify_id = gconf_client_notify_add (src->client,
      GST_GCONF_DIR "/" GST_GCONF_AUDIOSRC_KEY,
      cb_toggle_element, src, NULL, NULL);
}

static void
gst_gconf_audio_src_dispose (GObject * object)
{
  GstGConfAudioSrc *src = GST_GCONF_AUDIO_SRC (object);

  if (src->client) {
    if (src->gconf_notify_id) {
      gconf_client_notify_remove (src->client, src->gconf_notify_id);
      src->gconf_notify_id = 0;
    }

    g_object_unref (G_OBJECT (src->client));
    src->client = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_gconf_audio_src_finalize (GstGConfAudioSrc * src)
{
  g_free (src->gconf_str);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (src)));
}

static gboolean
do_toggle_element (GstGConfAudioSrc * src)
{
  GstState cur, next;
  GstPad *targetpad;
  gchar *new_gconf_str;

  new_gconf_str = gst_gconf_get_string (GST_GCONF_AUDIOSRC_KEY);
  if (new_gconf_str != NULL && src->gconf_str != NULL &&
      (strlen (new_gconf_str) == 0 ||
          strcmp (src->gconf_str, new_gconf_str) == 0)) {
    g_free (new_gconf_str);
    GST_DEBUG_OBJECT (src, "GConf key was updated, but it didn't change");
    return TRUE;
  }

  GST_OBJECT_LOCK (src);
  cur = GST_STATE (src);
  next = GST_STATE_PENDING (src);
  GST_OBJECT_UNLOCK (src);

  if (cur >= GST_STATE_READY || next == GST_STATE_PAUSED) {
    GST_DEBUG_OBJECT (src, "already running, ignoring GConf change");
    g_free (new_gconf_str);
    return TRUE;
  }

  GST_DEBUG_OBJECT (src, "GConf key changed: '%s' to '%s'",
      GST_STR_NULL (src->gconf_str), GST_STR_NULL (new_gconf_str));

  g_free (src->gconf_str);
  src->gconf_str = new_gconf_str;

  /* kill old element */
  if (src->kid) {
    GST_DEBUG_OBJECT (src, "Removing old kid");
    gst_element_set_state (src->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), src->kid);
    src->kid = NULL;
  }

  GST_DEBUG_OBJECT (src, "Creating new kid");
  if (!(src->kid = gst_gconf_get_default_audio_src ())) {
    GST_ELEMENT_ERROR (src, LIBRARY, SETTINGS, (NULL),
        ("Failed to render audio source from GConf"));
    g_free (src->gconf_str);
    src->gconf_str = NULL;
    return FALSE;
  }
  gst_element_set_state (src->kid, GST_STATE (src));
  gst_bin_add (GST_BIN (src), src->kid);

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (src, "Creating new ghostpad");
  targetpad = gst_element_get_static_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (src, "done changing gconf audio source");

  return TRUE;
}

static void
cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  do_toggle_element (GST_GCONF_AUDIO_SRC (data));
}

static GstStateChangeReturn
gst_gconf_audio_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGConfAudioSrc *src = GST_GCONF_AUDIO_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_toggle_element (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_gconf_audio_src_reset (src))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}
