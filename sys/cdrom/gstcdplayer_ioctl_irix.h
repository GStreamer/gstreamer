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

/* THIS DOES NOT WORK YET */

#define CDPLAYER(x) ((CDPlayer *)x)
#define FD(x) ((int)x)

gboolean cd_init(struct cd *cd,const gchar *device)
{
	CDPLAYER *cdplayer;
	CDSTATUS status;
	CDTRACKINFO info;
	guint i;

	cdplayer = CDOpen(device,"r");

	if (cdplayer == NULL) {
		return FALSE;
	}

	cd->fd = FD(cdplayer);

	if (CDgetstatus(cdplayer,&status) == 0) {
		CDclose(cdplayer);
		cd->fd = 0;
		return FALSE;
	}

	for (i = 1; i < status.last; i++) {
		if (CDgettrackinfo(cdplayer,i,&info) == 0) {
			CDclose(cdplayer);
			cd->fd = 0;
			return FALSE;
		}

		cd->tracks[i].minute = info.start_min;
		cd->tracks[i].second = info.start_sec;
		cd->tracks[i].frame = info.start_frame;

	}

	/* there is no leadout information */
	

	cd->num_tracks = status.last;

	return TRUE;
}

gboolean cd_start(struct cd *cd,gint start_track,gint end_track)
{
	if (cd->fd == 0) {
		return FALSE;
	}

	cd_fix_track_range(cd,&start_track,&end_track);

	

}

gboolean cd_pause(struct cd *cd)
{

}

gboolean cd_resume(struct cd *cd)
{

}

gboolean cd_stop(struct cd *cd)
{

}

/* -1 for error, 0 for not playing, 1 for playing */
CDStatus cd_status(struct cd *cd)
{

}

gint cd_current_track(struct cd *cd)
{

}

gboolean cd_close(struct cd *cd)
{

}

