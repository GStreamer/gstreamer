/* gstcdplay
 * Copyright (c) 2002 Charles Schmidt <cbschmid@uiuc.edu> 
 
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

#ifndef __CDPLAYER_H__
#define __CDPLAYER_H__

#include <glib.h>
#include <gst/gst.h>

#include "gstcdplayer_ioctl.h"

#define GST_TYPE_CDPLAYER 		(cdplayer_get_type())
#define CDPLAYER(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDPLAYER,CDPlayer))
#define CDPLAYER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDPLAYER,CDPlayerClass))
#define GST_IS_CDPLAYER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDPLAYER))
#define GST_IS_CDPLAYER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDPLAYER))


typedef struct _CDPlayer CDPlayer;
typedef struct _CDPlayerClass CDPlayerClass;

struct _CDPlayer
{
  GstBin element;

  /* properties */
  gchar *device;
  gint num_tracks;
  gint start_track;
  gint end_track;
  gint current_track;
  guint32 cddb_discid;

  /* private */
  struct cd cd;
  gboolean paused;
};

struct _CDPlayerClass
{
  GstBinClass parent_class;

  /* signal callbacks */
  void (*track_change) (GstElement * element, guint track);
};

GType cdplayer_get_type (void);

#endif
