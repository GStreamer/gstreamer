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

#include "gstcdplayer_ioctl.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <linux/cdrom.h>


gboolean cd_init(struct cd *cd,const gchar *device)
{
	struct cdrom_tochdr toc_header;
	struct cdrom_tocentry toc_entry;
	guint i;

	cd->fd = open(device,O_RDONLY | O_NONBLOCK);

	if (cd->fd == -1) {
		return FALSE;
	}

	/* get the toc header information */
	if (ioctl(cd->fd,CDROMREADTOCHDR,&toc_header) != 0) {
		return FALSE;
	}

	/* read each entry in the toc header */
	for (i = 1; i < toc_header.cdth_trk1; i++) {
		toc_entry.cdte_format = CDROM_MSF;
		toc_entry.cdte_track = i;

		if (ioctl(cd->fd,CDROMREADTOCENTRY,&toc_entry) != 0) {
			return FALSE;
		}

		cd->tracks[i].minute = toc_entry.cdte_addr.msf.minute;
		cd->tracks[i].second = toc_entry.cdte_addr.msf.second;
		cd->tracks[i].frame = toc_entry.cdte_addr.msf.frame;
		cd->tracks[i].data_track = (toc_entry.cdte_ctrl == CDROM_DATA_TRACK);
	}

	/* read the leadout */
	toc_entry.cdte_track = CDROM_LEADOUT;
	toc_entry.cdte_format = CDROM_MSF;
	if (ioctl(cd->fd,CDROMREADTOCENTRY,&toc_entry) != 0) {
		return FALSE;
	}
	cd->tracks[LEADOUT].minute = toc_entry.cdte_addr.msf.minute;
	cd->tracks[LEADOUT].second = toc_entry.cdte_addr.msf.second;
	cd->tracks[LEADOUT].frame = toc_entry.cdte_addr.msf.frame;

	cd->num_tracks = toc_header.cdth_trk1;

	return TRUE;
}

gboolean cd_start(struct cd *cd,guint start_track)
{
	struct cdrom_msf msf;

	if (cd->fd == -1) {
		return FALSE;
	}

	if (start_track <= 0) {
		start_track = 1;
	}

	if (start_track > cd->num_tracks) {
		start_track = cd->num_tracks;
	}

	msf.cdmsf_min0 = cd->tracks[start_track].minute;
	msf.cdmsf_sec0 = cd->tracks[start_track].second;
	msf.cdmsf_frame0 = cd->tracks[start_track].frame;

	msf.cdmsf_min1 = cd->tracks[LEADOUT].minute;
	msf.cdmsf_sec1 = cd->tracks[LEADOUT].second;
	msf.cdmsf_frame1 = cd->tracks[LEADOUT].frame;

	if (ioctl(cd->fd,CDROMPLAYMSF,&msf) != 0) {
		return FALSE;
	}

	return TRUE;
}

gboolean cd_pause(struct cd *cd)
{
	if (cd->fd == -1) {
		return FALSE;
	}

	if (ioctl(cd->fd,CDROMPAUSE,NULL) != 0) {
		return FALSE;
	}

	return TRUE;
}

gboolean cd_resume(struct cd *cd)
{
	if (cd->fd == -1) {
		return FALSE;
	}

	if (ioctl(cd->fd,CDROMRESUME,NULL) != 0) {
		return FALSE;
	}

	return TRUE;
}

gboolean cd_stop(struct cd *cd)
{
	if (cd->fd == -1) {
		return FALSE;
	}

	if (ioctl(cd->fd,CDROMSTOP,NULL) != 0) {
		return FALSE;
	}

	return TRUE;
}


gboolean cd_close(struct cd *cd)
{
	if (cd->fd == -1) {
		return TRUE;
	}

	if (close(cd->fd) != 0) {
		return FALSE;
	}

	return TRUE;
}

