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

#ifdef HAVE_CDROM_BSD_NETBSD	/* net & open */
#ifndef CDIOREADTOCHDR
#define CDIOREADTOCHDR CDIOREADTOCHEADER
#endif
gboolean
cd_init (struct cd *cd, const gchar * device)
{
  struct ioc_toc_header toc_header;
  struct ioc_read_toc_entry toc_entry;
  struct cd_toc_entry toc_entry_data;
  guint i;

  cd->fd = open (device, O_RDONLY | O_NONBLOCK);

  if (cd->fd == -1) {
    return FALSE;
  }

  /* get the toc header information */
  if (ioctl (cd->fd, CDIOREADTOCHDR, &toc_header) != 0) {
    close (cd->fd);
    cd->fd = -1;
    return FALSE;
  }

  /* read each entry in the toc header */
  for (i = 1; i <= toc_header.ending_track; i++) {
    toc_entry.address_format = CD_MSF_FORMAT;
    toc_entry.starting_track = i;
    toc_entry.data = &toc_entry_data;
    toc_entry.data_len = sizeof (toc_entry_data);

    if (ioctl (cd->fd, CDIOREADTOCENTRYS, &toc_entry) != 0) {
      close (cd->fd);
      cd->fd = -1;
      return FALSE;
    }

    cd->tracks[i].minute = toc_entry.data->addr.msf.minute;
    cd->tracks[i].second = toc_entry.data->addr.msf.second;
    cd->tracks[i].frame = toc_entry.data->addr.msf.frame;
    cd->tracks[i].data_track = (toc_entry.data->control & 4) == 4;
  }

  /* read the leadout */
  toc_entry.address_format = CD_MSF_FORMAT;
  toc_entry.starting_track = 0xAA;	/* leadout */
  toc_entry.data = &toc_entry_data;
  toc_entry.data_len = sizeof (toc_entry_data);

  if (ioctl (cd->fd, CDIOREADTOCENTRYS, &toc_entry) != 0) {
    close (cd->fd);
    cd->fd = -1;
    return FALSE;
  }

  cd->tracks[LEADOUT].minute = toc_entry.data->addr.msf.minute;
  cd->tracks[LEADOUT].second = toc_entry.data->addr.msf.second;
  cd->tracks[LEADOUT].frame = toc_entry.data->addr.msf.frame;

  cd->num_tracks = toc_header.ending_track;

  return TRUE;
}
#elif defined HAVE_CDROM_BSD_DARWIN
gboolean
cd_init (struct cd *cd, const gchar * device)
{
  struct ioc_toc_header toc_header;
  struct ioc_read_toc_entry toc_entry;
  guint i;

  cd->fd = open (device, O_RDONLY | O_NONBLOCK);

  if (cd->fd == -1) {
    return FALSE;
  }

  /* get the toc header information */
  if (ioctl (cd->fd, CDIOREADTOCHDR, &toc_header) != 0) {
    close (cd->fd);
    cd->fd = -1;
    return FALSE;
  }

  /* read each entry in the toc header */
  for (i = 1; i <= toc_header.ending_track; i++) {
    toc_entry.address_format = CD_MSF_FORMAT;
    toc_entry.starting_track = i;

    if (ioctl (cd->fd, CDIOREADTOCENTRYS, &toc_entry) != 0) {
      close (cd->fd);
      cd->fd = -1;
      return FALSE;
    }

    cd->tracks[i].minute = toc_entry.data->addr[1];
    cd->tracks[i].second = toc_entry.data->addr[2];
    cd->tracks[i].frame = toc_entry.data->addr[3];
    cd->tracks[i].data_track = (toc_entry.data->control & 4) == 4;
  }

  /* read the leadout */
  toc_entry.address_format = CD_MSF_FORMAT;
  toc_entry.starting_track = 0xAA;	/* leadout */
  toc_entry.data = &toc_entry_data;
  toc_entry.data_len = sizeof (toc_entry_data);

  if (ioctl (cd->fd, CDIOREADTOCENTRYS, &toc_entry) != 0) {
    close (cd->fd);
    cd->fd = -1;
    return FALSE;
  }

  cd->tracks[LEADOUT].minute = toc_entry.data->addr[1];
  cd->tracks[LEADOUT].second = toc_entry.data->addr[2];
  cd->tracks[LEADOUT].frame = toc_entry.data->addr[3];

  cd->num_tracks = toc_header.ending_track;

  return TRUE;
}
#else /* free */
gboolean
cd_init (struct cd *cd, const gchar * device)
{
  struct ioc_toc_header toc_header;
  struct ioc_read_toc_entry toc_entry;
  guint i;

  cd->fd = open (device, O_RDONLY | O_NONBLOCK);

  if (cd->fd == -1) {
    return FALSE;
  }

  /* get the toc header information */
  if (ioctl (cd->fd, CDIOREADTOCHDR, &toc_header) != 0) {
    close (cd->fd);
    cd->fd = -1;
    return FALSE;
  }

  /* read each entry in the toc header */
  for (i = 1; i <= toc_header.ending_track; i++) {
    toc_entry.address_format = CD_MSF_FORMAT;
    toc_entry.starting_track = i;

    if (ioctl (cd->fd, CDIOREADTOCENTRYS, &toc_entry) != 0) {
      close (cd->fd);
      cd->fd = -1;
      return FALSE;
    }

    cd->tracks[i].minute = toc_entry.entry.addr.msf.minute;
    cd->tracks[i].second = toc_entry.entry.addr.msf.second;
    cd->tracks[i].frame = toc_entry.entry.addr.msf.frame;
    cd->tracks[i].data_track = (toc_entry.data->control & 4) == 4;
  }

  /* read the leadout */
  toc_entry.address_format = CD_MSF_FORMAT;
  toc_entry.starting_track = 0xAA;	/* leadout */
  toc_entry.data = &toc_entry_data;
  toc_entry.data_len = sizeof (toc_entry_data);

  if (ioctl (cd->fd, CDIOREADTOCENTRYS, &toc_entry) != 0) {
    close (cd->fd);
    cd->fd = -1;
    return FALSE;
  }

  cd->tracks[LEADOUT].minute = toc_entry.entry.addr.msf.minute;
  cd->tracks[LEADOUT].second = toc_entry.entry.addr.msf.second;
  cd->tracks[LEADOUT].frame = toc_entry.entry.addr.msf.frame;

  cd->num_tracks = toc_header.ending_track;

  return TRUE;
}
#endif

gboolean
cd_start (struct cd * cd, gint start_track, gint end_track)
{
  struct ioc_play_msf msf;

  if (cd->fd == -1) {
    return FALSE;
  }

  cd_fix_track_range (cd, &start_track, &end_track);

  msf.start_m = cd->tracks[start_track].minute;
  msf.start_s = cd->tracks[start_track].second;
  msf.start_f = cd->tracks[start_track].frame;

  if (end_track == LEADOUT) {
    msf.end_m = cd->tracks[end_track].minute;
    msf.end_s = cd->tracks[end_track].second;
    msf.end_f = cd->tracks[end_track].frame;
  } else {
    msf.end_m = cd->tracks[end_track + 1].minute;
    msf.end_s = cd->tracks[end_track + 1].second;
    msf.end_f = cd->tracks[end_track + 1].frame;
  }

  if (ioctl (cd->fd, CDIOCPLAYMSF, &msf) != 0) {
    return FALSE;
  }

}

gboolean
cd_pause (struct cd * cd)
{
  if (cd->fd == -1) {
    return FALSE;
  }

  if (ioctl (cd->fd, CDIOCPAUSE, NULL) != 0) {
    return FALSE;
  }

  return TRUE;
}

gboolean
cd_resume (struct cd * cd)
{
  if (cd->fd == -1) {
    return FALSE;
  }

  if (ioctl (cd->fd, CDIOCRESUME, NULL) != 0) {
    return FALSE;
  }

  return TRUE;
}

gboolean
cd_stop (struct cd * cd)
{
  if (cd->fd == -1) {
    return FALSE;
  }

  if (ioctl (cd->fd, CDIOCSTOP, NULL) != 0) {
    return FALSE;
  }

  return TRUE;
}

/* -1 for error, 0 for not playing, 1 for playing */
CDStatus
cd_status (struct cd * cd)
{
  struct ioc_read_subchannel sub_channel;
  struct cd_sub_channel_info sub_channel_info;

  if (cd->fd == -1) {
    return -1;
  }

  sub_channel.address_format = CD_MSF_FORMAT;
  sub_channel.data_format = CD_CURRENT_POSITION;
  sub_channel.track = 0;
  sub_channel.data = &sub_channel_info;
  sub_channel.data_len = sizeof (sub_channel_info);

  if (ioctl (cd->fd, CDIOCREADSUBCHANNEL, &sub_channel) != 0) {
    return FALSE;
  }

  switch (sub_channel.data->header.audio_status) {
    case CD_AS_PLAY_IN_PROGRESS:
    case CD_AS_PLAY_PAUSED:
      return CD_PLAYING;
      break;
    case CD_AS_PLAY_COMPLETED:
      return CD_COMPLETED;
      break;
    case CD_AS_AUDIO_INVALID:
    case CD_AS_PLAY_ERROR:
    default:
      return CD_ERROR;
      break;

  }
}

gint
cd_current_track (struct cd *cd)
{
  struct ioc_read_subchannel sub_channel;
  struct cd_sub_channel_info sub_channel_info;

  if (cd->fd == -1) {
    return -1;
  }

  sub_channel.address_format = CD_MSF_FORMAT;
  sub_channel.data_format = CD_TRACK_INFO;
  sub_channel.track = 0;
  sub_channel.data = &sub_channel_info;
  sub_channel.data_len = sizeof (sub_channel_info);

  if (ioctl (cd->fd, CDIOCREADSUBCHANNEL, &sub_channel) != 0) {
    return -1;
  }
#ifdef __NetBSD__
  return sub_channel.data->what.track_info.track_number;
#else
  return sub_channel.data->track_number;
#endif
}

gboolean
cd_close (struct cd * cd)
{
  if (cd->fd == -1) {
    return TRUE;
  }

  if (close (cd->fd) != 0) {
    return FALSE;
  }

  cd->fd = -1;

  return TRUE;
}
