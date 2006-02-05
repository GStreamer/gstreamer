/* GStreamer
 * Copyright (C) 2005 Ross Burton <ross@burtonini.com>
 *
 * tags.c: Non-core tag registration
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>
#include <gst/gst.h>
#include "tag.h"

/**
 * SECTION:gsttag
 * @short_description: additional tag definitions for plugins and applications
 * @see_also: #GstTagList
 * 
 * <refsect2>
 * <para>
 * Contains additional standardized GStreamer tag definitions for plugins
 * and applications, and functions to register them with the GStreamer
 * tag system.
 * </para>
 * </refsect2>
 */


static gpointer
gst_tag_register_musicbrainz_tags_internal (gpointer unused)
{
#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif

  gst_tag_register (GST_TAG_MUSICBRAINZ_TRACKID, GST_TAG_FLAG_META,
      G_TYPE_STRING, _("track ID"), _("MusicBrainz track ID"), NULL);
  gst_tag_register (GST_TAG_MUSICBRAINZ_ARTISTID, GST_TAG_FLAG_META,
      G_TYPE_STRING, _("artist ID"), _("MusicBrainz artist ID"), NULL);
  gst_tag_register (GST_TAG_MUSICBRAINZ_ALBUMID, GST_TAG_FLAG_META,
      G_TYPE_STRING, _("album ID"), _("MusicBrainz album ID"), NULL);
  gst_tag_register (GST_TAG_MUSICBRAINZ_ALBUMARTISTID, GST_TAG_FLAG_META,
      G_TYPE_STRING,
      _("album artist ID"), _("MusicBrainz album artist ID"), NULL);
  gst_tag_register (GST_TAG_MUSICBRAINZ_TRMID, GST_TAG_FLAG_META,
      G_TYPE_STRING, _("track TRM ID"), _("MusicBrainz TRM ID"), NULL);
  gst_tag_register (GST_TAG_MUSICBRAINZ_SORTNAME, GST_TAG_FLAG_META,
      G_TYPE_STRING,
      _("artist sortname"), _("MusicBrainz artist sortname"), NULL);

  return NULL;
}

/**
 * gst_tag_register_musicbrainz_tags
 *
 * Registers additional musicbrainz-specific tags with the GStreamer tag
 * system. Plugins and applications that use these tags should call this
 * function before using them. Can be called multiple times.
 */
void
gst_tag_register_musicbrainz_tags (void)
{
  static GOnce mb_once = G_ONCE_INIT;

  g_once (&mb_once, gst_tag_register_musicbrainz_tags_internal, NULL);
}
