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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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


/* private functions */
static void cd_fix_track_range (struct cd *cd, gint * start_track,
    gint * end_track);
static gint cddb_sum (gint n);

#if defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#elif defined(HAVE_SYS_CDIO_H)
#include <sys/cdio.h>
/*
irix cdaudio works quite a bit differently than ioctl(), so its not ready
#elif defined(HAVE_DMEDIA_CDAUDIO_H)
#include <dmedia/cdaudio.h>
*/
#endif

/* these headers define low level functions:
	gboolean cd_init(struct cd *cd,const gchar *device);
	gboolean cd_start(struct cd *cd,gint start_track,gint end_track);
	gboolean cd_pause(struct cd *cd);
	gboolean cd_resume(struct cd *cd);
	gboolean cd_stop(struct cd *cd);
	CDStatus cd_status(struct cd *cd);
	gint cd_current_track(struct cd *cd);
	gboolean cd_close(struct cd *cd);
*/
#if defined(HAVE_CDROM_SOLARIS)
#include "gstcdplayer_ioctl_solaris.h"
#elif defined(HAVE_CDROM_BSD)
#include "gstcdplayer_ioctl_bsd.h"
/*
#elif defined(HAVE_CDROM_IRIX)
#include "gstcdplayer_ioctl_irix.h"
*/
#endif

static void
cd_fix_track_range (struct cd *cd, gint * start_track, gint * end_track)
{
  if (*start_track <= 0) {
    *start_track = 1;
  }

  if (*start_track > cd->num_tracks) {
    *start_track = cd->num_tracks;
  }

  if (*end_track < *start_track && *end_track != LEADOUT) {
    *end_track = *start_track;
  }

  if (*end_track > cd->num_tracks || *end_track + 1 > cd->num_tracks) {
    *end_track = LEADOUT;
  }

  return;
}

/* this cddb info is from 
   http://www.freedb.org/modules.php?name=Sections&sop=viewarticle&artid=6

   this will probably be of interest to anyone wishing to actually use the discid
   http://www.freedb.org/modules.php?name=Sections&sop=viewarticle&artid=28
*/
static gint
cddb_sum (gint n)
{
  gint ret = 0;

  while (n > 0) {
    ret += n % 10;
    n /= 10;
  }

  return ret;
}

guint32
cd_cddb_discid (struct cd * cd)
{
  guint i;
  guint n = 0;
  guint t;

  for (i = 1; i <= cd->num_tracks; i++) {
    n += cddb_sum (cd->tracks[i].minute * 60 + cd->tracks[i].second);
  }

  t = (cd->tracks[LEADOUT].minute * 60 + cd->tracks[LEADOUT].second) -
      (cd->tracks[1].minute * 60 + cd->tracks[1].second);

  return ((n % 0xff) << 24 | t << 8 | (cd->num_tracks));
}
