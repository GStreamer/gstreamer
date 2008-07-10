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
 * SECTION:element-clockoverlay
 * @see_also: #GstTextOverlay, #GstTimeOverlay
 *
 * This element overlays the current clock time on top of a video
 * stream. You can position the text and configure the font details
 * using the properties of the #GstTextOverlay class. By default, the
 * time is displayed in the top left corner of the picture, with some
 * padding to the left and to the top.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v videotestsrc ! clockoverlay ! xvimagesink
 * ]| Display the current time in the top left corner of the video picture
 * |[
 * gst-launch -v videotestsrc ! clockoverlay halign=right valign=bottom text="Edge City" shaded-background=true ! ffmpegcolorspace ! ximagesink
 * ]| Another pipeline that displays the current time with some leading
 * text in the bottom right corner of the video picture, with the background
 * of the text being shaded in order to make it more legible on top of a
 * bright video background.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gstclockoverlay.h>
#include <gst/video/video.h>
#include <time.h>

static const GstElementDetails clock_overlay_details =
GST_ELEMENT_DETAILS ("Clock overlay",
    "Filter/Editor/Video",
    "Overlays the current clock time on a video stream",
    "Tim-Philipp Müller <tim@centricular.net>");

GST_BOILERPLATE (GstClockOverlay, gst_clock_overlay, GstTextOverlay,
    GST_TYPE_TEXT_OVERLAY)

     static void gst_clock_overlay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &clock_overlay_details);
}

static gchar *
gst_clock_overlay_render_time (GstClockOverlay * overlay)
{
  struct tm *t;
  time_t now;

#ifdef HAVE_LOCALTIME_R
  struct tm dummy;
#endif

  now = time (NULL);

#ifdef HAVE_LOCALTIME_R
  t = localtime_r (&now, &dummy);
#else
  /* on win32 this apparently returns a per-thread struct which would be fine */
  t = localtime (&now);
#endif

  if (t == NULL)
    return g_strdup ("--:--:--");

  return g_strdup_printf ("%02u:%02u:%02u", t->tm_hour, t->tm_min, t->tm_sec);
}

/* Called with lock held */
static gchar *
gst_clock_overlay_get_text (GstTextOverlay * overlay, GstBuffer * video_frame)
{
  gchar *time_str, *txt, *ret;

  overlay->need_render = TRUE;

  txt = g_strdup (overlay->default_text);

  time_str = gst_clock_overlay_render_time (GST_CLOCK_OVERLAY (overlay));
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
gst_clock_overlay_class_init (GstClockOverlayClass * klass)
{
  GstTextOverlayClass *gsttextoverlay_class;

  gsttextoverlay_class = (GstTextOverlayClass *) klass;

  gsttextoverlay_class->get_text = gst_clock_overlay_get_text;
}

static void
gst_clock_overlay_init (GstClockOverlay * overlay, GstClockOverlayClass * klass)
{
  PangoFontDescription *font_description;
  GstTextOverlay *textoverlay;
  PangoContext *context;

  textoverlay = GST_TEXT_OVERLAY (overlay);

  context = GST_TEXT_OVERLAY_CLASS (klass)->pango_context;

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

  textoverlay->valign = GST_TEXT_OVERLAY_VALIGN_TOP;
  textoverlay->halign = GST_TEXT_OVERLAY_HALIGN_LEFT;
}
