/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
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

/*
 * this library handles interaction with GConf
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gconf.h"

#ifndef GST_GCONF_DIR
#error "GST_GCONF_DIR is not defined !"
#endif

static GConfClient *_gst_gconf_client = NULL;   /* GConf connection */


/* internal functions */

static GConfClient *
gst_gconf_get_client (void)
{
  if (!_gst_gconf_client)
    _gst_gconf_client = gconf_client_get_default ();

  return _gst_gconf_client;
}

/* external functions */

/**
 * gst_gconf_get_string:
 * @key: a #gchar corresponding to the key you want to get.
 *
 * Get GConf key @key's string value.
 *
 * Returns: a newly allocated #gchar string containing @key's value,
 * or NULL in the case of an error..
 */
gchar *
gst_gconf_get_string (const gchar * key)
{
  GError *error = NULL;
  gchar *value = NULL;
  gchar *full_key = g_strdup_printf ("%s/%s", GST_GCONF_DIR, key);


  value = gconf_client_get_string (gst_gconf_get_client (), full_key, &error);
  g_free (full_key);

  if (error) {
    g_warning ("gst_gconf_get_string: error: %s\n", error->message);
    g_error_free (error);
    return NULL;
  }

  return value;
}

/**
 * gst_gconf_set_string:
 * @key: a #gchar corresponding to the key you want to set.
 * @value: a #gchar containing key value.
 *
 * Set GConf key @key to string value @value.
 */
void
gst_gconf_set_string (const gchar * key, const gchar * value)
{
  GError *error = NULL;
  gchar *full_key = g_strdup_printf ("%s/%s", GST_GCONF_DIR, key);

  gconf_client_set_string (gst_gconf_get_client (), full_key, value, &error);
  if (error) {
    GST_ERROR ("gst_gconf_set_string: error: %s\n", error->message);
    g_error_free (error);
  }
  g_free (full_key);
}

/**
 * gst_gconf_render_bin_from_key:
 * @key: a #gchar string corresponding to a GConf key.
 *
 * Render bin from GConf key @key.
 *
 * Returns: a #GstElement containing the rendered bin.
 */
GstElement *
gst_gconf_render_bin_from_key (const gchar * key)
{
  GstElement *bin = NULL;
  gchar *value;

  value = gst_gconf_get_string (key);
  if (value) {
    GError *err = NULL;

    bin = gst_parse_bin_from_description (value, TRUE, &err);
    if (err) {
      GST_ERROR ("gconf: error creating bin '%s': %s", value, err->message);
      g_error_free (err);
    }

    g_free (value);
  }
  return bin;
}

/**
 * gst_gconf_get_default_audio_sink:
 *
 * Render audio output bin from GStreamer GConf key : "default/audiosink".
 * If key is invalid, the default audio sink for the platform is used
 * (typically osssink or sunaudiosink).
 *
 * Returns: a #GstElement containing the audio output bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_audio_sink (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/audiosink");

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_AUDIOSINK, NULL);

    if (!ret)
      g_warning ("No GConf default audio sink key and %s doesn't work",
          DEFAULT_AUDIOSINK);
  }

  return ret;
}

/**
 * gst_gconf_get_default_video_sink:
 *
 * Render video output bin from GStreamer GConf key : "default/videosink".
 * If key is invalid, the default video sink for the platform is used
 * (typically xvimagesink or ximagesink).
 *
 * Returns: a #GstElement containing the video output bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_video_sink (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/videosink");

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_VIDEOSINK, NULL);

    if (!ret)
      g_warning ("No GConf default video sink key and %s doesn't work",
          DEFAULT_VIDEOSINK);
  }

  return ret;
}

/**
 * gst_gconf_get_default_audio_src:
 *
 * Render audio acquisition bin from GStreamer GConf key : "default/audiosrc".
 * If key is invalid, the default audio source for the plaform is used.
 * (typically osssrc or sunaudiosrc).
 *
 * Returns: a #GstElement containing the audio source bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_audio_src (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/audiosrc");

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_AUDIOSRC, NULL);

    if (!ret)
      g_warning ("No GConf default audio src key and %s doesn't work",
          DEFAULT_AUDIOSRC);
  }

  return ret;
}

/**
 * gst_gconf_get_default_video_src:
 *
 * Render video acquisition bin from GStreamer GConf key :
 * "default/videosrc". If key is invalid, the default video source
 * for the platform is used (typically videotestsrc).
 *
 * Returns: a #GstElement containing the video source bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_video_src (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/videosrc");

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_VIDEOSRC, NULL);

    if (!ret)
      g_warning ("No GConf default video src key and %s doesn't work",
          DEFAULT_VIDEOSRC);
  }

  return ret;
}

/**
 * gst_gconf_get_default_visualization_element:
 *
 * Render visualization bin from GStreamer GConf key : "default/visualization".
 * If key is invalid, the default visualization element is used.
 *
 * Returns: a #GstElement containing the visualization bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_visualization_element (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/visualization");

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_VISUALIZER, NULL);

    if (!ret)
      g_warning
          ("No GConf default visualization plugin key and %s doesn't work",
          DEFAULT_VISUALIZER);
  }

  return ret;
}
