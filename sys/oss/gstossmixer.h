/* GStreamer OSS Mixer implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstossmixer.h: mixer interface implementation for OSS
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

#ifndef __GST_OSS_MIXER_H__
#define __GST_OSS_MIXER_H__

#include <gst/gst.h>
#include <gst/mixer/mixer.h>
#include "gstosselement.h"

G_BEGIN_DECLS
#define GST_TYPE_OSSMIXER_TRACK \
  (gst_ossmixer_track_get_type ())
#define GST_OSSMIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OSSMIXER_TRACK, \
			       GstOssMixerTrack))
#define GST_OSSMIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_OSSMIXER_TRACK, \
			    GstOssMixerTrackClass))
#define GST_IS_OSSMIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OSSMIXER_TRACK))
#define GST_IS_OSSMIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_OSSMIXER_TRACK))
    typedef struct _GstOssMixerTrack
{
  GstMixerTrack parent;

  gint lvol, rvol;
  gint track_num;
} GstOssMixerTrack;

typedef struct _GstOssMixerTrackClass
{
  GstMixerTrackClass parent;
} GstOssMixerTrackClass;

GType gst_ossmixer_track_get_type (void);

void gst_ossmixer_interface_init (GstMixerClass * klass);
void gst_oss_interface_init (GstImplementsInterfaceClass * klass);
void gst_ossmixer_build_list (GstOssElement * oss);
void gst_ossmixer_free_list (GstOssElement * oss);

G_END_DECLS
#endif /* __GST_OSS_MIXER_H__ */
