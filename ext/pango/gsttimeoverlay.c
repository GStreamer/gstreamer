/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Tim-Philipp Müller <tim@centricular.net>
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
 * SECTION:element-timeoverlay
 * @see_also: #GstBaseTextOverlay, #GstClockOverlay
 *
 * This element overlays the buffer time stamps of a video stream on
 * top of itself. You can position the text and configure the font details
 * using the properties of the #GstBaseTextOverlay class. By default, the
 * time stamp is displayed in the top left corner of the picture, with some
 * padding to the left and to the top.
 *
 * <refsect2>
 * |[
 * gst-launch -v videotestsrc ! timeoverlay ! xvimagesink
 * ]| Display the time stamps in the top left
 * corner of the video picture.
 * |[
 * gst-launch -v videotestsrc ! timeoverlay halign=right valign=bottom text="Stream time:" shaded-background=true ! xvimagesink
 * ]| Another pipeline that displays the time stamps with some leading
 * text in the bottom right corner of the video picture, with the background
 * of the text being shaded in order to make it more legible on top of a
 * bright video background.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>

#include "gsttimeoverlay.h"

#define gst_time_overlay_parent_class parent_class
G_DEFINE_TYPE (GstTimeOverlay, gst_time_overlay, GST_TYPE_BASE_TEXT_OVERLAY);

static gchar *
gst_time_overlay_render_time (GstTimeOverlay * overlay, GstClockTime time)
{
  guint hours, mins, secs, msecs;

  if (!GST_CLOCK_TIME_IS_VALID (time))
    return g_strdup ("");

  hours = (guint) (time / (GST_SECOND * 60 * 60));
  mins = (guint) ((time / (GST_SECOND * 60)) % 60);
  secs = (guint) ((time / GST_SECOND) % 60);
  msecs = (guint) ((time % GST_SECOND) / (1000 * 1000));

  return g_strdup_printf ("%u:%02u:%02u.%03u", hours, mins, secs, msecs);
}

/* Called with lock held */
static gchar *
gst_time_overlay_get_text (GstBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  GstClockTime time = GST_BUFFER_TIMESTAMP (video_frame);
  gchar *time_str, *txt, *ret;

  overlay->need_render = TRUE;

  if (!GST_CLOCK_TIME_IS_VALID (time)) {
    GST_DEBUG ("buffer without valid timestamp");
    return g_strdup ("");
  }

  GST_DEBUG ("buffer with timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS (time));

  txt = g_strdup (overlay->default_text);

  time_str = gst_time_overlay_render_time (GST_TIME_OVERLAY (overlay), time);
  if (txt != NULL && *txt != '\0') {
    ret = g_strdup_printf ("%s %s", txt, time_str);
  } else {
    ret = time_str;
    time_str = NULL;
  }

  g_free (txt);
  g_free (time_str);

  return ret;
}

static void
gst_time_overlay_class_init (GstTimeOverlayClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseTextOverlayClass *gsttextoverlay_class;
  PangoContext *context;
  PangoFontDescription *font_description;

  gsttextoverlay_class = (GstBaseTextOverlayClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class, "Time overlay",
      "Filter/Editor/Video",
      "Overlays buffer time stamps on a video stream",
      "Tim-Philipp Müller <tim@centricular.net>");

  gsttextoverlay_class->get_text = gst_time_overlay_get_text;

  g_mutex_lock (gsttextoverlay_class->pango_lock);
  context = gsttextoverlay_class->pango_context;

  pango_context_set_language (context, pango_language_from_string ("en_US"));
  pango_context_set_base_dir (context, PANGO_DIRECTION_LTR);

  font_description = pango_font_description_new ();
  pango_font_description_set_family_static (font_description, "Monospace");
  pango_font_description_set_style (font_description, PANGO_STYLE_NORMAL);
  pango_font_description_set_variant (font_description, PANGO_VARIANT_NORMAL);
  pango_font_description_set_weight (font_description, PANGO_WEIGHT_NORMAL);
  pango_font_description_set_stretch (font_description, PANGO_STRETCH_NORMAL);
  pango_font_description_set_size (font_description, 18 * PANGO_SCALE);
  pango_context_set_font_description (context, font_description);
  pango_font_description_free (font_description);
  g_mutex_unlock (gsttextoverlay_class->pango_lock);
}

static void
gst_time_overlay_init (GstTimeOverlay * overlay)
{
  GstBaseTextOverlay *textoverlay;

  textoverlay = GST_BASE_TEXT_OVERLAY (overlay);

  textoverlay->valign = GST_BASE_TEXT_OVERLAY_VALIGN_TOP;
  textoverlay->halign = GST_BASE_TEXT_OVERLAY_HALIGN_LEFT;
}
