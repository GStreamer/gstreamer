/* GStreamer
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-cdiocddasrc
 * @title: cdiocddasrc
 * @see_also: GstCdParanoiaSrc, GstAudioCdSrc
 *
 * cdiocddasrc reads and extracts raw audio from Audio CDs. It can operate
 * in one of two modes:
 *
 *  * treat each track as a separate stream, counting time from the start
 *    of the track to the end of the track and posting EOS at the end of
 *    a track, or
 *  * treat the entire disc as one stream, counting time from the start of
 *    the first track to the end of the last track, posting EOS only at
 *    the end of the last track.
 *
 * With a recent-enough version of libcdio, the element will extract
 * CD-TEXT if this is supported by the CD-drive and CD-TEXT information
 * is available on the CD. The information will be posted on the bus in
 * form of a tag message.
 *
 * When opened, the element will also calculate a CDDB disc ID and a
 * MusicBrainz disc ID, which applications can use to query online
 * databases for artist/title information. These disc IDs will also be
 * posted on the bus as part of the tag messages.
 *
 * cdiocddasrc supports the GstUriHandler interface, so applications can use
 * playbin with cdda://&lt;track-number&gt; URIs for playback (they will have
 * to connect to playbin's notify::source signal and set the device on the
 * cd source in the notify callback if they want to set the device property).
 * Applications should use seeks in "track" format to switch between different
 * tracks of the same CD (passing a new cdda:// URI to playbin involves opening
 * and closing the CD device, which is much slower).
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 cdiocddasrc track=5 device=/dev/cdrom ! audioconvert ! vorbisenc ! oggmux ! filesink location=track5.ogg
 * ]|
 * This pipeline extracts track 5 of the audio CD and encodes it into an
 * Ogg/Vorbis file.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcdio.h"
#include "gstcdiocddasrc.h"

#include <gst/gst.h>
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define SAMPLES_PER_SECTOR (CDIO_CD_FRAMESIZE_RAW / sizeof (gint16))

#define DEFAULT_READ_SPEED   -1

enum
{
  PROP_0 = 0,
  PROP_READ_SPEED
};

GST_DEBUG_CATEGORY (gst_cdio_debug);


G_DEFINE_TYPE (GstCdioCddaSrc, gst_cdio_cdda_src, GST_TYPE_AUDIO_CD_SRC);
#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_cdio_debug, "cdio", 0, "libcdio elements"); \
    cdio_log_set_handler (gst_cdio_log_handler);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (cdiocddasrc, "cdiocddasrc",
    GST_RANK_SECONDARY - 1, GST_TYPE_CDIO_CDDA_SRC, _do_init);

static void gst_cdio_cdda_src_finalize (GObject * obj);
static void gst_cdio_cdda_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cdio_cdda_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstBuffer *gst_cdio_cdda_src_read_sector (GstAudioCdSrc * src,
    gint sector);
static gboolean gst_cdio_cdda_src_open (GstAudioCdSrc * src,
    const gchar * device);
static void gst_cdio_cdda_src_close (GstAudioCdSrc * src);

#if 0
static gchar *
gst_cdio_cdda_src_get_default_device (GstAudioCdSrc * audiocdsrc)
{
  GstCdioCddaSrc *src;
  gchar *default_device, *ret;

  src = GST_CDIO_CDDA_SRC (audiocdsrc);

  /* src->cdio may be NULL here */
  default_device = cdio_get_default_device (src->cdio);

  ret = g_strdup (default_device);
  free (default_device);

  GST_LOG_OBJECT (src, "returning default device: %s", GST_STR_NULL (ret));

  return ret;
}

static gchar **
gst_cdio_cdda_src_probe_devices (GstAudioCdSrc * audiocdsrc)
{
  char **devices, **ret, **d;

  /* FIXME: might return the same hardware device twice, e.g.
   * as /dev/cdrom and /dev/dvd - gotta do something more sophisticated */
  devices = cdio_get_devices (DRIVER_DEVICE);

  if (devices == NULL)
    goto no_devices;

  if (*devices == NULL)
    goto empty_devices;

  ret = g_strdupv (devices);
  for (d = devices; *d != NULL; ++d) {
    GST_DEBUG_OBJECT (audiocdsrc, "device: %s", GST_STR_NULL (*d));
    free (*d);
  }
  free (devices);

  return ret;

  /* ERRORS */
no_devices:
  {
    GST_DEBUG_OBJECT (audiocdsrc, "no devices found");
    return NULL;
  }
empty_devices:
  {
    GST_DEBUG_OBJECT (audiocdsrc, "empty device list found");
    free (devices);
    return NULL;
  }
}
#endif

static GstBuffer *
gst_cdio_cdda_src_read_sector (GstAudioCdSrc * audiocdsrc, gint sector)
{
  GstCdioCddaSrc *src;
  guint8 *data;

  src = GST_CDIO_CDDA_SRC (audiocdsrc);

  data = g_malloc (CDIO_CD_FRAMESIZE_RAW);

  /* can't use pad_alloc because we can't return the GstFlowReturn (FIXME 0.11) */
  if (cdio_read_audio_sector (src->cdio, data, sector) != 0)
    goto read_failed;

  if (src->swap_le_be) {
    gint16 *pcm_data = (gint16 *) data;
    gint i;

    for (i = 0; i < SAMPLES_PER_SECTOR; ++i)
      pcm_data[i] = GUINT16_SWAP_LE_BE (pcm_data[i]);
  }

  return gst_buffer_new_wrapped (data, CDIO_CD_FRAMESIZE_RAW);

  /* ERRORS */
read_failed:
  {
    GST_WARNING_OBJECT (src, "read at sector %d failed!", sector);
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (_("Could not read from CD.")),
        ("cdio_read_audio_sector at %d failed: %s", sector,
            g_strerror (errno)));
    g_free (data);
    return NULL;
  }
}

static gboolean
gst_cdio_cdda_src_do_detect_drive_endianness (GstCdioCddaSrc * src, gint from,
    gint to)
{
  gint16 pcm_data[SAMPLES_PER_SECTOR], last_pcm_ne, last_pcm_oe;
  gdouble ne_sumd0, ne_sumd1, ne_factor;
  gdouble oe_sumd0, oe_sumd1, oe_factor;
  gdouble diff;
  gint sector;
  gint i;

  ne_sumd0 = ne_sumd1 = 0.0;
  oe_sumd0 = oe_sumd1 = 0.0;
  last_pcm_ne = 0;
  last_pcm_oe = 0;

  GST_LOG_OBJECT (src, "checking sector %d to %d", from, to);

  for (sector = from; sector < to; ++sector) {
    if (cdio_read_audio_sector (src->cdio, pcm_data, sector) != 0)
      goto read_failed;

    /* only evaluate samples for left channel */
    for (i = 0; i < SAMPLES_PER_SECTOR; i += 2) {
      gint16 pcm;

      /* Native endianness first */
      pcm = pcm_data[i];
      ne_sumd0 += abs (pcm);
      ne_sumd1 += abs (pcm - last_pcm_ne);
      last_pcm_ne = pcm;

      /* other endianness next */
      pcm = GUINT16_SWAP_LE_BE (pcm);
      oe_sumd0 += abs (pcm);
      oe_sumd1 += abs (pcm - last_pcm_oe);
      last_pcm_oe = pcm;
    }

  }

  ne_factor = (ne_sumd1 / ne_sumd0);
  oe_factor = (oe_sumd1 / oe_sumd0);
  diff = ne_factor - oe_factor;

  GST_DEBUG_OBJECT (src, "Native: %.2f, Other: %.2f, diff: %.2f",
      ne_factor, oe_factor, diff);

  if (diff > 0.5) {
    GST_INFO_OBJECT (src, "Drive produces samples in other endianness");
    src->swap_le_be = TRUE;
    return TRUE;
  } else if (diff < -0.5) {
    GST_INFO_OBJECT (src, "Drive produces samples in host endianness");
    src->swap_le_be = FALSE;
    return TRUE;
  } else {
    GST_INFO_OBJECT (src, "Inconclusive, assuming host endianness");
    src->swap_le_be = FALSE;
    return FALSE;
  }

/* ERRORS */
read_failed:
  {
    GST_WARNING_OBJECT (src, "could not read sector %d", sector);
    src->swap_le_be = FALSE;
    return FALSE;
  }
}

static void
gst_cdio_cdda_src_detect_drive_endianness (GstCdioCddaSrc * src, gint first,
    gint last)
{
  gint from, to;

  GST_INFO ("Detecting drive endianness");

  /* try middle of disc first */
  from = (first + last) / 2;
  to = MIN (from + 10, last);
  if (gst_cdio_cdda_src_do_detect_drive_endianness (src, from, to))
    return;

  /* if that was inconclusive, try other places */
  from = (first + last) / 4;
  to = MIN (from + 10, last);
  if (gst_cdio_cdda_src_do_detect_drive_endianness (src, from, to))
    return;

  from = (first + last) * 3 / 4;
  to = MIN (from + 10, last);
  if (gst_cdio_cdda_src_do_detect_drive_endianness (src, from, to))
    return;

  /* if that's still inconclusive, we give up and assume host endianness */
  return;
}

static gboolean
notcdio_track_is_audio_track (const CdIo * p_cdio, track_t i_track)
{
  return (cdio_get_track_format (p_cdio, i_track) == TRACK_FORMAT_AUDIO);
}

static gboolean
gst_cdio_cdda_src_open (GstAudioCdSrc * audiocdsrc, const gchar * device)
{
  GstCdioCddaSrc *src;
  discmode_t discmode;
  gint first_track, num_tracks, i;
  gint first_audio_sector = 0, last_audio_sector = 0;
#if LIBCDIO_VERSION_NUM > 83 || LIBCDIO_VERSION_NUM < 76
  cdtext_t *cdtext;
#endif

  src = GST_CDIO_CDDA_SRC (audiocdsrc);

  g_assert (device != NULL);
  g_assert (src->cdio == NULL);

  GST_LOG_OBJECT (src, "trying to open device %s", device);

  if (!(src->cdio = cdio_open (device, DRIVER_UNKNOWN)))
    goto open_failed;

  discmode = cdio_get_discmode (src->cdio);
  GST_LOG_OBJECT (src, "discmode: %d", (gint) discmode);

  if (discmode != CDIO_DISC_MODE_CD_DA && discmode != CDIO_DISC_MODE_CD_MIXED)
    goto not_audio;

  first_track = cdio_get_first_track_num (src->cdio);
  num_tracks = cdio_get_num_tracks (src->cdio);

  if (num_tracks <= 0 || first_track < 0)
    return TRUE;                /* base class will generate 'has no tracks' error */

  if (src->read_speed != -1)
    cdio_set_speed (src->cdio, src->read_speed);

#if LIBCDIO_VERSION_NUM > 83 || LIBCDIO_VERSION_NUM < 76
  cdtext = cdio_get_cdtext (src->cdio);

  if (NULL == cdtext)
    GST_DEBUG_OBJECT (src, "no CD-TEXT on disc");
  else
    gst_cdio_add_cdtext_album_tags (GST_OBJECT_CAST (src), cdtext,
        audiocdsrc->tags);
#else
  gst_cdio_add_cdtext_album_tags (GST_OBJECT_CAST (src), src->cdio,
      audiocdsrc->tags);
#endif

  GST_LOG_OBJECT (src, "%u tracks, first track: %d", num_tracks, first_track);

  for (i = 0; i < num_tracks; ++i) {
    GstAudioCdSrcTrack track = { 0, };
    gint len_sectors;

    len_sectors = cdio_get_track_sec_count (src->cdio, i + first_track);

    track.num = i + first_track;
    track.is_audio = notcdio_track_is_audio_track (src->cdio, i + first_track);

    /* Note: LSN/LBA confusion all around us; in any case, this does
     * the right thing here (for cddb id calculations etc. as well) */
    track.start = cdio_get_track_lsn (src->cdio, i + first_track);
    track.end = track.start + len_sectors - 1;  /* -1? */

    if (track.is_audio) {
      first_audio_sector = MIN (first_audio_sector, track.start);
      last_audio_sector = MAX (last_audio_sector, track.end);
    }
#if LIBCDIO_VERSION_NUM > 83 || LIBCDIO_VERSION_NUM < 76
    if (NULL != cdtext)
      track.tags = gst_cdio_get_cdtext (GST_OBJECT (src), cdtext,
          i + first_track);
#else
    track.tags = gst_cdio_get_cdtext (GST_OBJECT (src), src->cdio,
        i + first_track);
#endif

    gst_audio_cd_src_add_track (GST_AUDIO_CD_SRC (src), &track);
  }

  /* Try to detect if we need to byte-order swap the samples coming from the
   * drive, which might be the case if the CD drive operates in a different
   * endianness than the host CPU's endianness (happens on e.g. Powerbook G4) */
  gst_cdio_cdda_src_detect_drive_endianness (src, first_audio_sector,
      last_audio_sector);

  return TRUE;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open CD device for reading.")),
        ("cdio_open() failed: %s", g_strerror (errno)));
    return FALSE;
  }
not_audio:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Disc is not an Audio CD.")), ("discmode: %d", (gint) discmode));

    cdio_destroy (src->cdio);
    src->cdio = NULL;
    return FALSE;
  }
}

static void
gst_cdio_cdda_src_close (GstAudioCdSrc * audiocdsrc)
{
  GstCdioCddaSrc *src = GST_CDIO_CDDA_SRC (audiocdsrc);

  if (src->cdio) {
    cdio_destroy (src->cdio);
    src->cdio = NULL;
  }
}

static void
gst_cdio_cdda_src_init (GstCdioCddaSrc * src)
{
  src->read_speed = DEFAULT_READ_SPEED; /* don't need atomic access here */
  src->cdio = NULL;
}

static void
gst_cdio_cdda_src_finalize (GObject * obj)
{
  GstCdioCddaSrc *src = GST_CDIO_CDDA_SRC (obj);

  if (src->cdio) {
    cdio_destroy (src->cdio);
    src->cdio = NULL;
  }

  G_OBJECT_CLASS (gst_cdio_cdda_src_parent_class)->finalize (obj);
}

static void
gst_cdio_cdda_src_class_init (GstCdioCddaSrcClass * klass)
{
  GstAudioCdSrcClass *audiocdsrc_class = GST_AUDIO_CD_SRC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_cdio_cdda_src_set_property;
  gobject_class->get_property = gst_cdio_cdda_src_get_property;
  gobject_class->finalize = gst_cdio_cdda_src_finalize;

  audiocdsrc_class->open = gst_cdio_cdda_src_open;
  audiocdsrc_class->close = gst_cdio_cdda_src_close;
  audiocdsrc_class->read_sector = gst_cdio_cdda_src_read_sector;
#if 0
  audiocdsrc_class->probe_devices = gst_cdio_cdda_src_probe_devices;
  audiocdsrc_class->get_default_device = gst_cdio_cdda_src_get_default_device;
#endif

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_READ_SPEED,
      g_param_spec_int ("read-speed", "Read speed",
          "Read from device at the specified speed (-1 = default)", -1, 100,
          DEFAULT_READ_SPEED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "CD audio source (CDDA)", "Source/File",
      "Read audio from CD using libcdio",
      "Tim-Philipp Müller <tim centricular net>");
}

static void
gst_cdio_cdda_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCdioCddaSrc *src = GST_CDIO_CDDA_SRC (object);

  switch (prop_id) {
    case PROP_READ_SPEED:{
      gint speed;

      speed = g_value_get_int (value);
      g_atomic_int_set (&src->read_speed, speed);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cdio_cdda_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCdioCddaSrc *src = GST_CDIO_CDDA_SRC (object);

  switch (prop_id) {
    case PROP_READ_SPEED:{
      gint speed;

      speed = g_atomic_int_get (&src->read_speed);
      g_value_set_int (value, speed);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
