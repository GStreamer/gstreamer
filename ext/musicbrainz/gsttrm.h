/* GStreamer trm plugin
 * Copyright (C) 2004 Jeremy Simon <jsimon13@yahoo.fr>
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


#ifndef __GST_MUSICBRAINZ_H__
#define __GST_MUSICBRAINZ_H__

#include <gst/gst.h>

#include <musicbrainz/mb_c.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_MUSICBRAINZ \
  (gst_musicbrainz_get_type())
#define GST_MUSICBRAINZ(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MUSICBRAINZ,GstMusicBrainz))
#define GST_MUSICBRAINZ_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MUSICBRAINZ,GstMusicBrainz))
#define GST_IS_MUSICBRAINZ(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MUSICBRAINZ))
#define GST_IS_MUSICBRAINZ_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MUSICBRAINZ))


  typedef struct _GstMusicBrainz GstMusicBrainz;
  typedef struct _GstMusicBrainzClass GstMusicBrainzClass;

  struct _GstMusicBrainz
  {
    GstElement element;

    GstPad *sinkpad;
    GstPad *srcpad;
    const GstCaps *caps;

    trm_t trm;
    gchar signature[17];
    gchar ascii_signature[37];

    guint depth;
    guint rate;
    guint channels;
    gboolean linked;
    gboolean data_available;
    gboolean signature_available;
    guint64 total_time;
  };

  struct _GstMusicBrainzClass
  {
    GstElementClass parent_class;

    /* signals */
    void (*signature_available) (GstElement * element);
  };

  GType gst_musicbrainz_get_type (void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_MUSICBRAINZE_H__ */
