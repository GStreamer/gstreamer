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

#ifndef __CDPLAYER_LL_H__
#define __CDPLAYER_LL_H__

#include <glib.h>

#define LEADOUT 0

#define CDPLAYER_CD(cdp)  (&(cdp->cd))

#define CDPLAYER_MAX_TRACKS 128

typedef enum {
	CD_PLAYING,
	CD_COMPLETED,
	CD_ERROR
} CDStatus;

struct cd_msf {
	guint8 minute;
	guint8 second;
	guint8 frame;

	gboolean data_track;
};

struct cd {
	gint fd;
	gint num_tracks;
	struct cd_msf tracks[CDPLAYER_MAX_TRACKS];
};


/* these are defined by the different cdrom type header files */
gboolean cd_init(struct cd *cd,const gchar *device);
gboolean cd_start(struct cd *cd,gint start_track,gint end_track);
gboolean cd_pause(struct cd *cd);
gboolean cd_resume(struct cd *cd);
gboolean cd_stop(struct cd *cd);
CDStatus cd_status(struct cd *cd);
gint cd_current_track(struct cd *cd);
gboolean cd_close(struct cd *cd);

guint32 cd_cddb_discid(struct cd *cd);

#endif
