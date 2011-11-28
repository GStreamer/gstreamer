/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 Tim-Philipp Müller <tim centricular net>
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

/* TODO:
 *
 *  - in ::start(), we want to post a tags message with an array or a list
 *    of tagslists of all tracks, so that applications know at least the
 *    number of tracks and all track durations immediately without having
 *    to do any querying. We have to decide what type and name to use for
 *    this array of track taglists.
 *
 *  - FIX cddb discid calculation algorithm for mixed mode CDs - do we use
 *    offsets and duration of ALL tracks (data + audio) for the CDDB ID
 *    calculation, or only audio tracks?
 *
 *  - Do we really need properties for the TOC bias/offset stuff? Wouldn't
 *    environment variables make much more sense? Do we need this at all
 *    (does it only affect ancient hardware?)
 */

/**
 * SECTION:gstcddabasesrc
 * @short_description: Base class for CD digital audio (CDDA) sources
 *
 * <refsect2>
 * <para>
 * Provides a base class for CDDA sources, which handles things like seeking,
 * querying, discid calculation, tags, and buffer timestamping.
 * </para>
 * <title>Using GstCddaBaseSrc-based elements in applications</title>
 * <para>
 * GstCddaBaseSrc registers two #GstFormat<!-- -->s of its own, namely
 * the "track" format and the "sector" format. Applications will usually
 * only find the "track" format interesting. You can retrieve that #GstFormat
 * for use in seek events or queries with gst_format_get_by_nick("track").
 * </para>
 * <para>
 * In order to query the number of tracks, for example, an application would
 * set the CDDA source element to READY or PAUSED state and then query the
 * the number of tracks via gst_element_query_duration() using the track
 * format acquired above. Applications can query the currently playing track
 * in the same way.
 * </para>
 * <para>
 * Alternatively, applications may retrieve the currently playing track and
 * the total number of tracks from the taglist that will posted on the bus
 * whenever the CD is opened or the currently playing track changes. The
 * taglist will contain GST_TAG_TRACK_NUMBER and GST_TAG_TRACK_COUNT tags.
 * </para>
 * <para>
 * Applications playing back CD audio using playbin and cdda://n URIs should
 * issue a seek command in track format to change between tracks, rather than
 * setting a new cdda://n+1 URI on playbin (as setting a new URI on playbin
 * involves closing and re-opening the CD device, which is much much slower).
 * </para>
 * <title>Tags and meta-information</title>
 * <para>
 * CDDA sources will automatically emit a number of tags, details about which
 * can be found in the libgsttag documentation. Those tags are:
 * #GST_TAG_CDDA_CDDB_DISCID, #GST_TAG_CDDA_CDDB_DISCID_FULL,
 * #GST_TAG_CDDA_MUSICBRAINZ_DISCID, #GST_TAG_CDDA_MUSICBRAINZ_DISCID_FULL,
 * among others.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>             /* for strtol */

#include "gstcddabasesrc.h"
#include "gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY_STATIC (gst_cdda_base_src_debug);
#define GST_CAT_DEFAULT gst_cdda_base_src_debug

#define DEFAULT_DEVICE                       "/dev/cdrom"

#define CD_FRAMESIZE_RAW                     (2352)

#define SECTORS_PER_SECOND                   (75)
#define SECTORS_PER_MINUTE                   (75*60)
#define SAMPLES_PER_SECTOR                   (CD_FRAMESIZE_RAW >> 2)
#define TIME_INTERVAL_FROM_SECTORS(sectors)  ((SAMPLES_PER_SECTOR * sectors * GST_SECOND) / 44100)
#define SECTORS_FROM_TIME_INTERVAL(dtime)    (dtime * 44100 / (SAMPLES_PER_SECTOR * GST_SECOND))

enum
{
  ARG_0,
  ARG_MODE,
  ARG_DEVICE,
  ARG_TRACK,
  ARG_TOC_OFFSET,
  ARG_TOC_BIAS
};

static void gst_cdda_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_cdda_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cdda_base_src_finalize (GObject * obj);
static const GstQueryType *gst_cdda_base_src_get_query_types (GstPad * pad);
static gboolean gst_cdda_base_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_cdda_base_src_handle_event (GstBaseSrc * basesrc,
    GstEvent * event);
static gboolean gst_cdda_base_src_do_seek (GstBaseSrc * basesrc,
    GstSegment * segment);
static void gst_cdda_base_src_setup_interfaces (GType type);
static gboolean gst_cdda_base_src_start (GstBaseSrc * basesrc);
static gboolean gst_cdda_base_src_stop (GstBaseSrc * basesrc);
static GstFlowReturn gst_cdda_base_src_create (GstPushSrc * pushsrc,
    GstBuffer ** buf);
static gboolean gst_cdda_base_src_is_seekable (GstBaseSrc * basesrc);
static void gst_cdda_base_src_update_duration (GstCddaBaseSrc * src);
static void gst_cdda_base_src_set_index (GstElement * src, GstIndex * index);
static GstIndex *gst_cdda_base_src_get_index (GstElement * src);

GST_BOILERPLATE_FULL (GstCddaBaseSrc, gst_cdda_base_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_cdda_base_src_setup_interfaces);

#define SRC_CAPS \
  "audio/x-raw-int, "               \
  "endianness = (int) BYTE_ORDER, " \
  "signed = (boolean) true, "       \
  "width = (int) 16, "              \
  "depth = (int) 16, "              \
  "rate = (int) 44100, "            \
  "channels = (int) 2"              \

static GstStaticPadTemplate gst_cdda_base_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS)
    );

/* our two formats */
static GstFormat track_format;
static GstFormat sector_format;

GType
gst_cdda_base_src_mode_get_type (void)
{
  static GType mode_type;       /* 0 */
  static const GEnumValue modes[] = {
    {GST_CDDA_BASE_SRC_MODE_NORMAL, "Stream consists of a single track",
        "normal"},
    {GST_CDDA_BASE_SRC_MODE_CONTINUOUS, "Stream consists of the whole disc",
        "continuous"},
    {0, NULL, NULL}
  };

  if (mode_type == 0)
    mode_type = g_enum_register_static ("GstCddaBaseSrcMode", modes);

  return mode_type;
}

static void
gst_cdda_base_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_cdda_base_src_src_template);

  /* our very own formats */
  track_format = gst_format_register ("track", "CD track");
  sector_format = gst_format_register ("sector", "CD sector");

  /* register CDDA tags */
  gst_tag_register_musicbrainz_tags ();

#if 0
  ///// FIXME: what type to use here? ///////
  gst_tag_register (GST_TAG_CDDA_TRACK_TAGS, GST_TAG_FLAG_META, GST_TYPE_TAG_LIST, "track-tags", "CDDA taglist for one track", gst_tag_merge_use_first);        ///////////// FIXME: right function??? ///////
#endif

  GST_DEBUG_CATEGORY_INIT (gst_cdda_base_src_debug, "cddabasesrc", 0,
      "CDDA Base Source");
}

static void
gst_cdda_base_src_class_init (GstCddaBaseSrcClass * klass)
{
  GstElementClass *element_class;
  GstPushSrcClass *pushsrc_class;
  GstBaseSrcClass *basesrc_class;
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_cdda_base_src_set_property;
  gobject_class->get_property = gst_cdda_base_src_get_property;
  gobject_class->finalize = gst_cdda_base_src_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "Device", "CD device location",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MODE,
      g_param_spec_enum ("mode", "Mode", "Mode", GST_TYPE_CDDA_BASE_SRC_MODE,
          GST_CDDA_BASE_SRC_MODE_NORMAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TRACK,
      g_param_spec_uint ("track", "Track", "Track", 1, 99, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#if 0
  /* Do we really need this toc adjustment stuff as properties? does the user
   * have a chance to set it in practice, e.g. when using sound-juicer, rb,
   * totem, whatever? Shouldn't we rather use environment variables
   * for this? (tpm) */

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOC_OFFSET,
      g_param_spec_int ("toc-offset", "Table of contents offset",
          "Add <n> sectors to the values reported", G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOC_BIAS,
      g_param_spec_boolean ("toc-bias", "Table of contents bias",
          "Assume that the beginning offset of track 1 as reported in the TOC "
          "will be addressed as LBA 0.  Necessary for some Toshiba drives to "
          "get track boundaries", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  element_class->set_index = GST_DEBUG_FUNCPTR (gst_cdda_base_src_set_index);
  element_class->get_index = GST_DEBUG_FUNCPTR (gst_cdda_base_src_get_index);

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_cdda_base_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_cdda_base_src_stop);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_cdda_base_src_query);
  basesrc_class->event = GST_DEBUG_FUNCPTR (gst_cdda_base_src_handle_event);
  basesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_cdda_base_src_do_seek);
  basesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_cdda_base_src_is_seekable);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_cdda_base_src_create);
}

static void
gst_cdda_base_src_init (GstCddaBaseSrc * src, GstCddaBaseSrcClass * klass)
{
  gst_pad_set_query_type_function (GST_BASE_SRC_PAD (src),
      GST_DEBUG_FUNCPTR (gst_cdda_base_src_get_query_types));

  /* we're not live and we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);

  src->device = NULL;
  src->mode = GST_CDDA_BASE_SRC_MODE_NORMAL;
  src->uri_track = -1;
}

static void
gst_cdda_base_src_finalize (GObject * obj)
{
  GstCddaBaseSrc *cddasrc = GST_CDDA_BASE_SRC (obj);

  g_free (cddasrc->uri);
  g_free (cddasrc->device);

  if (cddasrc->index)
    gst_object_unref (cddasrc->index);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_cdda_base_src_set_device (GstCddaBaseSrc * src, const gchar * device)
{
  if (src->device)
    g_free (src->device);
  src->device = NULL;

  if (!device)
    return;

  /* skip multiple slashes */
  while (*device == '/' && *(device + 1) == '/')
    device++;

#ifdef __sun
  /*
   * On Solaris, /dev/rdsk is used for accessing the CD device, but some
   * applications pass in /dev/dsk, so correct.
   */
  if (strncmp (device, "/dev/dsk", 8) == 0) {
    gchar *rdsk_value;
    rdsk_value = g_strdup_printf ("/dev/rdsk%s", device + 8);
    src->device = g_strdup (rdsk_value);
    g_free (rdsk_value);
  } else {
#endif
    src->device = g_strdup (device);
#ifdef __sun
  }
#endif
}

static void
gst_cdda_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (object);

  GST_OBJECT_LOCK (src);

  switch (prop_id) {
    case ARG_MODE:{
      src->mode = g_value_get_enum (value);
      break;
    }
    case ARG_DEVICE:{
      const gchar *dev = g_value_get_string (value);

      gst_cdda_base_src_set_device (src, dev);
      break;
    }
    case ARG_TRACK:{
      guint track = g_value_get_uint (value);

      if (src->num_tracks > 0 && track > src->num_tracks) {
        g_warning ("Invalid track %u", track);
      } else if (track > 0 && src->tracks != NULL) {
        src->cur_sector = src->tracks[track - 1].start;
        src->uri_track = track;
      } else {
        src->uri_track = track; /* seek will be done in start() */
      }
      break;
    }
    case ARG_TOC_OFFSET:{
      src->toc_offset = g_value_get_int (value);
      break;
    }
    case ARG_TOC_BIAS:{
      src->toc_bias = g_value_get_boolean (value);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }

  GST_OBJECT_UNLOCK (src);
}

static void
gst_cdda_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCddaBaseSrcClass *klass = GST_CDDA_BASE_SRC_GET_CLASS (object);
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (object);

  GST_OBJECT_LOCK (src);

  switch (prop_id) {
    case ARG_MODE:
      g_value_set_enum (value, src->mode);
      break;
    case ARG_DEVICE:{
      if (src->device == NULL && klass->get_default_device != NULL) {
        gchar *d = klass->get_default_device (src);

        if (d != NULL) {
          g_value_set_string (value, DEFAULT_DEVICE);
          g_free (d);
          break;
        }
      }
      if (src->device == NULL)
        g_value_set_string (value, DEFAULT_DEVICE);
      else
        g_value_set_string (value, src->device);
      break;
    }
    case ARG_TRACK:{
      if (src->num_tracks <= 0 && src->uri_track > 0) {
        g_value_set_uint (value, src->uri_track);
      } else {
        g_value_set_uint (value, src->cur_track + 1);
      }
      break;
    }
    case ARG_TOC_OFFSET:
      g_value_set_int (value, src->toc_offset);
      break;
    case ARG_TOC_BIAS:
      g_value_set_boolean (value, src->toc_bias);
      break;
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }

  GST_OBJECT_UNLOCK (src);
}

static gint
gst_cdda_base_src_get_track_from_sector (GstCddaBaseSrc * src, gint sector)
{
  gint i;

  for (i = 0; i < src->num_tracks; ++i) {
    if (sector >= src->tracks[i].start && sector <= src->tracks[i].end)
      return i;
  }
  return -1;
}

static const GstQueryType *
gst_cdda_base_src_get_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    GST_QUERY_CONVERT,
    0
  };

  return src_query_types;
}

static gboolean
gst_cdda_base_src_convert (GstCddaBaseSrc * src, GstFormat src_format,
    gint64 src_val, GstFormat dest_format, gint64 * dest_val)
{
  gboolean started;

  GST_LOG_OBJECT (src, "converting value %" G_GINT64_FORMAT " from %s into %s",
      src_val, gst_format_get_name (src_format),
      gst_format_get_name (dest_format));

  if (src_format == dest_format) {
    *dest_val = src_val;
    return TRUE;
  }

  started = GST_OBJECT_FLAG_IS_SET (GST_BASE_SRC (src), GST_BASE_SRC_STARTED);

  if (src_format == track_format) {
    if (!started)
      goto not_started;
    if (src_val < 0 || src_val >= src->num_tracks) {
      GST_DEBUG_OBJECT (src, "track number %d out of bounds", (gint) src_val);
      goto wrong_value;
    }
    src_format = GST_FORMAT_DEFAULT;
    src_val = src->tracks[src_val].start * SAMPLES_PER_SECTOR;
  } else if (src_format == sector_format) {
    src_format = GST_FORMAT_DEFAULT;
    src_val = src_val * SAMPLES_PER_SECTOR;
  }

  if (src_format == dest_format) {
    *dest_val = src_val;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      /* convert to samples (4 bytes per sample) */
      src_val = src_val >> 2;
      /* fallthrough */
    case GST_FORMAT_DEFAULT:{
      switch (dest_format) {
        case GST_FORMAT_BYTES:{
          if (src_val < 0) {
            GST_DEBUG_OBJECT (src, "sample source value negative");
            goto wrong_value;
          }
          *dest_val = src_val << 2;     /* 4 bytes per sample */
          break;
        }
        case GST_FORMAT_TIME:{
          *dest_val = gst_util_uint64_scale_int (src_val, GST_SECOND, 44100);
          break;
        }
        default:{
          gint64 sector = src_val / SAMPLES_PER_SECTOR;

          if (dest_format == sector_format) {
            *dest_val = sector;
          } else if (dest_format == track_format) {
            if (!started)
              goto not_started;
            *dest_val = gst_cdda_base_src_get_track_from_sector (src, sector);
          } else {
            goto unknown_format;
          }
          break;
        }
      }
      break;
    }
    case GST_FORMAT_TIME:{
      gint64 sample_offset;

      if (src_val == GST_CLOCK_TIME_NONE) {
        GST_DEBUG_OBJECT (src, "source time value invalid");
        goto wrong_value;
      }

      sample_offset = gst_util_uint64_scale_int (src_val, 44100, GST_SECOND);
      switch (dest_format) {
        case GST_FORMAT_BYTES:{
          *dest_val = sample_offset << 2;       /* 4 bytes per sample */
          break;
        }
        case GST_FORMAT_DEFAULT:{
          *dest_val = sample_offset;
          break;
        }
        default:{
          gint64 sector = sample_offset / SAMPLES_PER_SECTOR;

          if (dest_format == sector_format) {
            *dest_val = sector;
          } else if (dest_format == track_format) {
            if (!started)
              goto not_started;
            *dest_val = gst_cdda_base_src_get_track_from_sector (src, sector);
          } else {
            goto unknown_format;
          }
          break;
        }
      }
      break;
    }
    default:{
      goto unknown_format;
    }
  }

done:
  {
    GST_LOG_OBJECT (src, "returning %" G_GINT64_FORMAT, *dest_val);
    return TRUE;
  }

unknown_format:
  {
    GST_DEBUG_OBJECT (src, "conversion failed: %s", "unsupported format");
    return FALSE;
  }

wrong_value:
  {
    GST_DEBUG_OBJECT (src, "conversion failed: %s",
        "source value not within allowed range");
    return FALSE;
  }

not_started:
  {
    GST_DEBUG_OBJECT (src, "conversion failed: %s",
        "cannot do this conversion, device not open");
    return FALSE;
  }
}

static gboolean
gst_cdda_base_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (basesrc);
  gboolean started;

  started = GST_OBJECT_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED);

  GST_LOG_OBJECT (src, "handling %s query",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      GstFormat dest_format;
      gint64 dest_val;
      guint sectors;

      gst_query_parse_duration (query, &dest_format, NULL);

      if (!started)
        return FALSE;

      g_assert (src->tracks != NULL);

      if (dest_format == track_format) {
        GST_LOG_OBJECT (src, "duration: %d tracks", src->num_tracks);
        gst_query_set_duration (query, track_format, src->num_tracks);
        return TRUE;
      }

      if (src->cur_track < 0 || src->cur_track >= src->num_tracks)
        return FALSE;

      if (src->mode == GST_CDDA_BASE_SRC_MODE_NORMAL) {
        sectors = src->tracks[src->cur_track].end -
            src->tracks[src->cur_track].start + 1;
      } else {
        sectors = src->tracks[src->num_tracks - 1].end -
            src->tracks[0].start + 1;
      }

      /* ... and convert into final format */
      if (!gst_cdda_base_src_convert (src, sector_format, sectors,
              dest_format, &dest_val)) {
        return FALSE;
      }

      gst_query_set_duration (query, dest_format, dest_val);

      GST_LOG ("duration: %u sectors, %" G_GINT64_FORMAT " in format %s",
          sectors, dest_val, gst_format_get_name (dest_format));
      break;
    }
    case GST_QUERY_POSITION:{
      GstFormat dest_format;
      gint64 pos_sector;
      gint64 dest_val;

      gst_query_parse_position (query, &dest_format, NULL);

      if (!started)
        return FALSE;

      g_assert (src->tracks != NULL);

      if (dest_format == track_format) {
        GST_LOG_OBJECT (src, "position: track %d", src->cur_track);
        gst_query_set_position (query, track_format, src->cur_track);
        return TRUE;
      }

      if (src->cur_track < 0 || src->cur_track >= src->num_tracks)
        return FALSE;

      if (src->mode == GST_CDDA_BASE_SRC_MODE_NORMAL) {
        pos_sector = src->cur_sector - src->tracks[src->cur_track].start;
      } else {
        pos_sector = src->cur_sector - src->tracks[0].start;
      }

      if (!gst_cdda_base_src_convert (src, sector_format, pos_sector,
              dest_format, &dest_val)) {
        return FALSE;
      }

      gst_query_set_position (query, dest_format, dest_val);

      GST_LOG ("position: sector %u, %" G_GINT64_FORMAT " in format %s",
          (guint) pos_sector, dest_val, gst_format_get_name (dest_format));
      break;
    }
    case GST_QUERY_CONVERT:{
      GstFormat src_format, dest_format;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_format, &src_val, &dest_format,
          NULL);

      if (!gst_cdda_base_src_convert (src, src_format, src_val, dest_format,
              &dest_val)) {
        return FALSE;
      }

      gst_query_set_convert (query, src_format, src_val, dest_format, dest_val);
      break;
    }
    default:{
      GST_DEBUG_OBJECT (src, "unhandled query, chaining up to parent class");
      return GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
    }
  }

  return TRUE;
}

static gboolean
gst_cdda_base_src_is_seekable (GstBaseSrc * basesrc)
{
  return TRUE;
}

static gboolean
gst_cdda_base_src_do_seek (GstBaseSrc * basesrc, GstSegment * segment)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (basesrc);
  gint64 seek_sector;

  GST_DEBUG_OBJECT (src, "segment %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
      GST_TIME_ARGS (segment->start), GST_TIME_ARGS (segment->stop));

  if (!gst_cdda_base_src_convert (src, GST_FORMAT_TIME, segment->start,
          sector_format, &seek_sector)) {
    GST_WARNING_OBJECT (src, "conversion failed");
    return FALSE;
  }

  /* we should only really be called when open */
  g_assert (src->cur_track >= 0 && src->cur_track < src->num_tracks);

  switch (src->mode) {
    case GST_CDDA_BASE_SRC_MODE_NORMAL:
      seek_sector += src->tracks[src->cur_track].start;
      break;
    case GST_CDDA_BASE_SRC_MODE_CONTINUOUS:
      seek_sector += src->tracks[0].start;
      break;
    default:
      g_return_val_if_reached (FALSE);
  }

  src->cur_sector = (gint) seek_sector;

  GST_DEBUG_OBJECT (src, "seek'd to sector %d", src->cur_sector);

  return TRUE;
}

static gboolean
gst_cdda_base_src_handle_track_seek (GstCddaBaseSrc * src, gdouble rate,
    GstSeekFlags flags, GstSeekType start_type, gint64 start,
    GstSeekType stop_type, gint64 stop)
{
  GstBaseSrc *basesrc = GST_BASE_SRC (src);
  GstEvent *event;

  if ((flags & GST_SEEK_FLAG_SEGMENT) == GST_SEEK_FLAG_SEGMENT) {
    gint64 start_time = -1;
    gint64 stop_time = -1;

    if (src->mode != GST_CDDA_BASE_SRC_MODE_CONTINUOUS) {
      GST_DEBUG_OBJECT (src, "segment seek in track format is only "
          "supported in CONTINUOUS mode, not in mode %d", src->mode);
      return FALSE;
    }

    switch (start_type) {
      case GST_SEEK_TYPE_SET:
        if (!gst_cdda_base_src_convert (src, track_format, start,
                GST_FORMAT_TIME, &start_time)) {
          GST_DEBUG_OBJECT (src, "cannot convert track %d to time",
              (gint) start);
          return FALSE;
        }
        break;
      case GST_SEEK_TYPE_END:
        if (!gst_cdda_base_src_convert (src, track_format,
                src->num_tracks - start - 1, GST_FORMAT_TIME, &start_time)) {
          GST_DEBUG_OBJECT (src, "cannot convert track %d to time",
              (gint) start);
          return FALSE;
        }
        start_type = GST_SEEK_TYPE_SET;
        break;
      case GST_SEEK_TYPE_NONE:
        start_time = -1;
        break;
      default:
        g_return_val_if_reached (FALSE);
    }

    switch (stop_type) {
      case GST_SEEK_TYPE_SET:
        if (!gst_cdda_base_src_convert (src, track_format, stop,
                GST_FORMAT_TIME, &stop_time)) {
          GST_DEBUG_OBJECT (src, "cannot convert track %d to time",
              (gint) stop);
          return FALSE;
        }
        break;
      case GST_SEEK_TYPE_END:
        if (!gst_cdda_base_src_convert (src, track_format,
                src->num_tracks - stop - 1, GST_FORMAT_TIME, &stop_time)) {
          GST_DEBUG_OBJECT (src, "cannot convert track %d to time",
              (gint) stop);
          return FALSE;
        }
        stop_type = GST_SEEK_TYPE_SET;
        break;
      case GST_SEEK_TYPE_NONE:
        stop_time = -1;
        break;
      default:
        g_return_val_if_reached (FALSE);
    }

    GST_LOG_OBJECT (src, "seek segment %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time), GST_TIME_ARGS (stop_time));

    /* send fake segment seek event in TIME format to
     * base class, which will hopefully handle the rest */

    event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags, start_type,
        start_time, stop_type, stop_time);

    return GST_BASE_SRC_CLASS (parent_class)->event (basesrc, event);
  }

  /* not a segment seek */

  if (start_type == GST_SEEK_TYPE_NONE) {
    GST_LOG_OBJECT (src, "start seek type is NONE, nothing to do");
    return TRUE;
  }

  if (stop_type != GST_SEEK_TYPE_NONE) {
    GST_WARNING_OBJECT (src, "ignoring stop seek type (expected NONE)");
  }

  if (start < 0 || start >= src->num_tracks) {
    GST_DEBUG_OBJECT (src, "invalid track %" G_GINT64_FORMAT, start);
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "seeking to track %" G_GINT64_FORMAT, start + 1);

  src->cur_sector = src->tracks[start].start;
  GST_DEBUG_OBJECT (src, "starting at sector %d", src->cur_sector);

  if (src->cur_track != start) {
    src->cur_track = (gint) start;
    src->uri_track = -1;
    src->prev_track = -1;

    gst_cdda_base_src_update_duration (src);
  } else {
    GST_DEBUG_OBJECT (src, "is current track, just seeking back to start");
  }

  /* send fake segment seek event in TIME format to
   * base class (so we get a newsegment etc.) */
  event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, -1);

  return GST_BASE_SRC_CLASS (parent_class)->event (basesrc, event);
}

static gboolean
gst_cdda_base_src_handle_event (GstBaseSrc * basesrc, GstEvent * event)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (basesrc);
  gboolean ret = FALSE;

  GST_LOG_OBJECT (src, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstSeekType start_type, stop_type;
      GstSeekFlags flags;
      GstFormat format;
      gdouble rate;
      gint64 start, stop;

      if (!GST_OBJECT_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED)) {
        GST_DEBUG_OBJECT (src, "seek failed: device not open");
        break;
      }

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      if (format == sector_format) {
        GST_DEBUG_OBJECT (src, "seek in sector format not supported");
        break;
      }

      if (format == track_format) {
        ret = gst_cdda_base_src_handle_track_seek (src, rate, flags,
            start_type, start, stop_type, stop);
      } else {
        GST_LOG_OBJECT (src, "let base class handle seek in %s format",
            gst_format_get_name (format));
        event = gst_event_ref (event);
        ret = GST_BASE_SRC_CLASS (parent_class)->event (basesrc, event);
      }
      break;
    }
    default:{
      GST_LOG_OBJECT (src, "let base class handle event");
      ret = GST_BASE_SRC_CLASS (parent_class)->event (basesrc, event);
      break;
    }
  }

  return ret;
}

static GstURIType
gst_cdda_base_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_cdda_base_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "cdda", NULL };

  return protocols;
}

static const gchar *
gst_cdda_base_src_uri_get_uri (GstURIHandler * handler)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (handler);

  GST_OBJECT_LOCK (src);

  g_free (src->uri);

  if (GST_OBJECT_FLAG_IS_SET (GST_BASE_SRC (src), GST_BASE_SRC_STARTED)) {
    src->uri =
        g_strdup_printf ("cdda://%s#%d", src->device,
        (src->uri_track > 0) ? src->uri_track : 1);
  } else {
    src->uri = g_strdup ("cdda://1");
  }

  GST_OBJECT_UNLOCK (src);

  return src->uri;
}

/* Note: gst_element_make_from_uri() might call us with just 'cdda://' as
 * URI and expects us to return TRUE then (and this might be in any state) */

/* We accept URIs of the format cdda://(device#track)|(track) */

static gboolean
gst_cdda_base_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (handler);
  gchar *protocol;
  const gchar *location;
  gchar *track_number;

  GST_OBJECT_LOCK (src);

  protocol = gst_uri_get_protocol (uri);
  if (!protocol || g_ascii_strcasecmp (protocol, "cdda") != 0) {
    g_free (protocol);
    goto failed;
  }
  g_free (protocol);

  location = uri + 7;
  track_number = g_strrstr (location, "#");
  src->uri_track = 0;
  /* FIXME 0.11: ignore URI fragments that look like device paths for
   * the benefit of rhythmbox and possibly other applications.
   */
  if (track_number && track_number[1] != '/') {
    gchar *device, *nuri = g_strdup (uri);

    track_number = nuri + (track_number - uri);
    *track_number = '\0';
    device = gst_uri_get_location (nuri);
    gst_cdda_base_src_set_device (src, device);
    g_free (device);
    src->uri_track = strtol (track_number + 1, NULL, 10);
    g_free (nuri);
  } else {
    if (*location == '\0')
      src->uri_track = 1;
    else
      src->uri_track = strtol (location, NULL, 10);
  }

  if (src->uri_track < 1)
    goto failed;

  if (src->num_tracks > 0
      && src->tracks != NULL && src->uri_track > src->num_tracks)
    goto failed;

  if (src->uri_track > 0 && src->tracks != NULL) {
    GST_OBJECT_UNLOCK (src);

    gst_pad_send_event (GST_BASE_SRC_PAD (src),
        gst_event_new_seek (1.0, track_format, GST_SEEK_FLAG_FLUSH,
            GST_SEEK_TYPE_SET, src->uri_track - 1, GST_SEEK_TYPE_NONE, -1));
  } else {
    /* seek will be done in start() */
    GST_OBJECT_UNLOCK (src);
  }

  GST_LOG_OBJECT (handler, "successfully handled uri '%s'", uri);

  return TRUE;

failed:
  {
    GST_OBJECT_UNLOCK (src);
    GST_DEBUG_OBJECT (src, "cannot handle URI '%s'", uri);
    return FALSE;
  }
}

static void
gst_cdda_base_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_cdda_base_src_uri_get_type;
  iface->get_uri = gst_cdda_base_src_uri_get_uri;
  iface->set_uri = gst_cdda_base_src_uri_set_uri;
  iface->get_protocols = gst_cdda_base_src_uri_get_protocols;
}

static void
gst_cdda_base_src_setup_interfaces (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_cdda_base_src_uri_handler_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);
}

/**
 * gst_cdda_base_src_add_track:
 * @src: a #GstCddaBaseSrc
 * @track: address of #GstCddaBaseSrcTrack to add
 * 
 * CDDA sources use this function from their start vfunc to announce the
 * available data and audio tracks to the base source class. The caller
 * should allocate @track on the stack, the base source will do a shallow
 * copy of the structure (and take ownership of the taglist if there is one).
 *
 * Returns: FALSE on error, otherwise TRUE.
 */

gboolean
gst_cdda_base_src_add_track (GstCddaBaseSrc * src, GstCddaBaseSrcTrack * track)
{
  g_return_val_if_fail (GST_IS_CDDA_BASE_SRC (src), FALSE);
  g_return_val_if_fail (track != NULL, FALSE);
  g_return_val_if_fail (track->num > 0, FALSE);

  GST_DEBUG_OBJECT (src, "adding track %2u (%2u) [%6u-%6u] [%5s], tags: %"
      GST_PTR_FORMAT, src->num_tracks + 1, track->num, track->start,
      track->end, (track->is_audio) ? "AUDIO" : "DATA ", track->tags);

  if (src->num_tracks > 0) {
    guint end_of_previous_track = src->tracks[src->num_tracks - 1].end;

    if (track->start <= end_of_previous_track) {
      GST_WARNING ("track %2u overlaps with previous tracks", track->num);
      return FALSE;
    }
  }

  GST_OBJECT_LOCK (src);

  ++src->num_tracks;
  src->tracks = g_renew (GstCddaBaseSrcTrack, src->tracks, src->num_tracks);
  src->tracks[src->num_tracks - 1] = *track;

  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static void
gst_cdda_base_src_update_duration (GstCddaBaseSrc * src)
{
  GstBaseSrc *basesrc;
  GstFormat format;
  gint64 duration;

  basesrc = GST_BASE_SRC (src);

  format = GST_FORMAT_TIME;
  if (gst_pad_query_duration (GST_BASE_SRC_PAD (src), &format, &duration)) {
    gst_segment_set_duration (&basesrc->segment, GST_FORMAT_TIME, duration);
  } else {
    gst_segment_set_duration (&basesrc->segment, GST_FORMAT_TIME, -1);
    duration = GST_CLOCK_TIME_NONE;
  }

  gst_element_post_message (GST_ELEMENT (src),
      gst_message_new_duration (GST_OBJECT (src), GST_FORMAT_TIME, -1));

  GST_LOG_OBJECT (src, "duration updated to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration));
}

#define CD_MSF_OFFSET 150

/* the cddb hash function */
static guint
cddb_sum (gint n)
{
  guint ret;

  ret = 0;
  while (n > 0) {
    ret += (n % 10);
    n /= 10;
  }
  return ret;
}

static void
gst_cddabasesrc_calculate_musicbrainz_discid (GstCddaBaseSrc * src)
{
  GString *s;
  GChecksum *sha;
  guchar digest[20];
  gchar *ptr;
  gchar tmp[9];
  gulong i;
  guint leadout_sector;
  gsize digest_len;

  s = g_string_new (NULL);

  leadout_sector = src->tracks[src->num_tracks - 1].end + 1 + CD_MSF_OFFSET;

  /* generate SHA digest */
  sha = g_checksum_new (G_CHECKSUM_SHA1);
  g_snprintf (tmp, sizeof (tmp), "%02X", src->tracks[0].num);
  g_string_append_printf (s, "%02X", src->tracks[0].num);
  g_checksum_update (sha, (guchar *) tmp, 2);

  g_snprintf (tmp, sizeof (tmp), "%02X", src->tracks[src->num_tracks - 1].num);
  g_string_append_printf (s, " %02X", src->tracks[src->num_tracks - 1].num);
  g_checksum_update (sha, (guchar *) tmp, 2);

  g_snprintf (tmp, sizeof (tmp), "%08X", leadout_sector);
  g_string_append_printf (s, " %08X", leadout_sector);
  g_checksum_update (sha, (guchar *) tmp, 8);

  for (i = 0; i < 99; i++) {
    if (i < src->num_tracks) {
      guint frame_offset = src->tracks[i].start + CD_MSF_OFFSET;

      g_snprintf (tmp, sizeof (tmp), "%08X", frame_offset);
      g_string_append_printf (s, " %08X", frame_offset);
      g_checksum_update (sha, (guchar *) tmp, 8);
    } else {
      g_checksum_update (sha, (guchar *) "00000000", 8);
    }
  }
  digest_len = 20;
  g_checksum_get_digest (sha, (guint8 *) & digest, &digest_len);

  /* re-encode to base64 */
  ptr = g_base64_encode (digest, digest_len);
  g_checksum_free (sha);
  i = strlen (ptr);

  g_assert (i < sizeof (src->mb_discid) + 1);
  memcpy (src->mb_discid, ptr, i);
  src->mb_discid[i] = '\0';
  free (ptr);

  /* Replace '/', '+' and '=' by '_', '.' and '-' as specified on
   * http://musicbrainz.org/doc/DiscIDCalculation
   */
  for (ptr = src->mb_discid; *ptr != '\0'; ptr++) {
    if (*ptr == '/')
      *ptr = '_';
    else if (*ptr == '+')
      *ptr = '.';
    else if (*ptr == '=')
      *ptr = '-';
  }

  GST_DEBUG_OBJECT (src, "musicbrainz-discid      = %s", src->mb_discid);
  GST_DEBUG_OBJECT (src, "musicbrainz-discid-full = %s", s->str);

  gst_tag_list_add (src->tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_CDDA_MUSICBRAINZ_DISCID, src->mb_discid,
      GST_TAG_CDDA_MUSICBRAINZ_DISCID_FULL, s->str, NULL);

  g_string_free (s, TRUE);
}

static void
lba_to_msf (guint sector, guint * p_m, guint * p_s, guint * p_f, guint * p_secs)
{
  guint m, s, f;

  m = sector / SECTORS_PER_MINUTE;
  sector = sector % SECTORS_PER_MINUTE;
  s = sector / SECTORS_PER_SECOND;
  f = sector % SECTORS_PER_SECOND;

  if (p_m)
    *p_m = m;
  if (p_s)
    *p_s = s;
  if (p_f)
    *p_f = f;
  if (p_secs)
    *p_secs = s + (m * 60);
}

static void
gst_cdda_base_src_calculate_cddb_id (GstCddaBaseSrc * src)
{
  GString *s;
  guint first_sector = 0, last_sector = 0;
  guint start_secs, end_secs, secs, len_secs;
  guint total_secs, num_audio_tracks;
  guint id, t, i;

  id = 0;
  total_secs = 0;
  num_audio_tracks = 0;

  /* FIXME: do we use offsets and duration of ALL tracks (data + audio)
   * for the CDDB ID calculation, or only audio tracks? */
  for (i = 0; i < src->num_tracks; ++i) {
    if (1) {                    /* src->tracks[i].is_audio) { */
      if (num_audio_tracks == 0) {
        first_sector = src->tracks[i].start + CD_MSF_OFFSET;
      }
      last_sector = src->tracks[i].end + CD_MSF_OFFSET + 1;
      ++num_audio_tracks;

      lba_to_msf (src->tracks[i].start + CD_MSF_OFFSET, NULL, NULL, NULL,
          &secs);

      len_secs = (src->tracks[i].end - src->tracks[i].start + 1) / 75;

      GST_DEBUG_OBJECT (src, "track %02u: lsn %6u (%02u:%02u), "
          "length: %u seconds (%02u:%02u)",
          num_audio_tracks, src->tracks[i].start + CD_MSF_OFFSET,
          secs / 60, secs % 60, len_secs, len_secs / 60, len_secs % 60);

      id += cddb_sum (secs);
      total_secs += len_secs;
    }
  }

  /* first_sector = src->tracks[0].start + CD_MSF_OFFSET; */
  lba_to_msf (first_sector, NULL, NULL, NULL, &start_secs);

  /* last_sector = src->tracks[src->num_tracks-1].end + CD_MSF_OFFSET; */
  lba_to_msf (last_sector, NULL, NULL, NULL, &end_secs);

  GST_DEBUG_OBJECT (src, "first_sector = %u = %u secs (%02u:%02u)",
      first_sector, start_secs, start_secs / 60, start_secs % 60);
  GST_DEBUG_OBJECT (src, "last_sector  = %u = %u secs (%02u:%02u)",
      last_sector, end_secs, end_secs / 60, end_secs % 60);

  t = end_secs - start_secs;

  GST_DEBUG_OBJECT (src, "total length = %u secs (%02u:%02u), added title "
      "lengths = %u seconds (%02u:%02u)", t, t / 60, t % 60, total_secs,
      total_secs / 60, total_secs % 60);

  src->discid = ((id % 0xff) << 24 | t << 8 | num_audio_tracks);

  s = g_string_new (NULL);
  g_string_append_printf (s, "%08x", src->discid);

  gst_tag_list_add (src->tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_CDDA_CDDB_DISCID, s->str, NULL);

  g_string_append_printf (s, " %u", src->num_tracks);
  for (i = 0; i < src->num_tracks; ++i) {
    g_string_append_printf (s, " %u", src->tracks[i].start + CD_MSF_OFFSET);
  }
  g_string_append_printf (s, " %u", t);

  gst_tag_list_add (src->tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_CDDA_CDDB_DISCID_FULL, s->str, NULL);

  GST_DEBUG_OBJECT (src, "cddb discid = %s", s->str);

  g_string_free (s, TRUE);
}

static void
gst_cdda_base_src_add_tags (GstCddaBaseSrc * src)
{
  gint i;

  /* fill in details for each track */
  for (i = 0; i < src->num_tracks; ++i) {
    gint64 duration;
    guint num_sectors;

    if (src->tracks[i].tags == NULL)
      src->tracks[i].tags = gst_tag_list_new ();

    num_sectors = src->tracks[i].end - src->tracks[i].start + 1;
    gst_cdda_base_src_convert (src, sector_format, num_sectors,
        GST_FORMAT_TIME, &duration);

    gst_tag_list_add (src->tracks[i].tags,
        GST_TAG_MERGE_REPLACE,
        GST_TAG_TRACK_NUMBER, i + 1,
        GST_TAG_TRACK_COUNT, src->num_tracks, GST_TAG_DURATION, duration, NULL);
  }

  /* now fill in per-album tags and include each track's tags
   * in the album tags, so that interested parties can retrieve
   * the relevant details for each track in one go */

  /* /////////////////////////////// FIXME should we rather insert num_tracks
   * tags by the name of 'track-tags' and have the caller use
   * gst_tag_list_get_value_index() rather than use tag names incl.
   * the track number ?? *////////////////////////////////////////

  gst_tag_list_add (src->tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_TRACK_COUNT, src->num_tracks, NULL);
#if 0
  for (i = 0; i < src->num_tracks; ++i) {
    gst_tag_list_add (src->tags, GST_TAG_MERGE_APPEND,
        GST_TAG_CDDA_TRACK_TAGS, src->tracks[i].tags, NULL);
  }
#endif

  GST_DEBUG ("src->tags = %" GST_PTR_FORMAT, src->tags);
}

static void
gst_cdda_base_src_add_index_associations (GstCddaBaseSrc * src)
{
  gint i;

  for (i = 0; i < src->num_tracks; i++) {
    gint64 sector;

    sector = src->tracks[i].start;
    gst_index_add_association (src->index, src->index_id, GST_ASSOCIATION_FLAG_KEY_UNIT, track_format, i,       /* here we count from 0 */
        sector_format, sector,
        GST_FORMAT_TIME,
        (gint64) (((CD_FRAMESIZE_RAW >> 2) * sector * GST_SECOND) / 44100),
        GST_FORMAT_BYTES, (gint64) (sector << 2), GST_FORMAT_DEFAULT,
        (gint64) ((CD_FRAMESIZE_RAW >> 2) * sector), NULL);
  }
}

static void
gst_cdda_base_src_set_index (GstElement * element, GstIndex * index)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (element);
  GstIndex *old;

  GST_OBJECT_LOCK (element);
  old = src->index;
  if (old == index) {
    GST_OBJECT_UNLOCK (element);
    return;
  }
  if (index)
    gst_object_ref (index);
  src->index = index;
  GST_OBJECT_UNLOCK (element);
  if (old)
    gst_object_unref (old);

  if (index) {
    gst_index_get_writer_id (index, GST_OBJECT (src), &src->index_id);
    gst_index_add_format (index, src->index_id, track_format);
    gst_index_add_format (index, src->index_id, sector_format);
  }
}


static GstIndex *
gst_cdda_base_src_get_index (GstElement * element)
{
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (element);
  GstIndex *index;

  GST_OBJECT_LOCK (element);
  if ((index = src->index))
    gst_object_ref (index);
  GST_OBJECT_UNLOCK (element);

  return index;
}

static gint
gst_cdda_base_src_track_sort_func (gconstpointer a, gconstpointer b,
    gpointer foo)
{
  GstCddaBaseSrcTrack *track_a = ((GstCddaBaseSrcTrack *) a);
  GstCddaBaseSrcTrack *track_b = ((GstCddaBaseSrcTrack *) b);

  /* sort data tracks to the end, and audio tracks by track number */
  if (track_a->is_audio == track_b->is_audio)
    return (gint) track_a->num - (gint) track_b->num;

  if (track_a->is_audio) {
    return -1;
  } else {
    return 1;
  }
}

static gboolean
gst_cdda_base_src_start (GstBaseSrc * basesrc)
{
  GstCddaBaseSrcClass *klass = GST_CDDA_BASE_SRC_GET_CLASS (basesrc);
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (basesrc);
  gboolean ret;
  gchar *device = NULL;

  src->discid = 0;
  src->mb_discid[0] = '\0';

  g_assert (klass->open != NULL);

  if (src->device != NULL) {
    device = g_strdup (src->device);
  } else if (klass->get_default_device != NULL) {
    device = klass->get_default_device (src);
  }

  if (device == NULL)
    device = g_strdup (DEFAULT_DEVICE);

  GST_LOG_OBJECT (basesrc, "opening device %s", device);

  src->tags = gst_tag_list_new ();

  ret = klass->open (src, device);
  g_free (device);
  device = NULL;

  if (!ret) {
    GST_DEBUG_OBJECT (basesrc, "failed to open device");
    /* subclass (should have) posted an error message with the details */
    gst_cdda_base_src_stop (basesrc);
    return FALSE;
  }

  if (src->num_tracks == 0 || src->tracks == NULL) {
    GST_DEBUG_OBJECT (src, "no tracks");
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("This CD has no audio tracks")), (NULL));
    gst_cdda_base_src_stop (basesrc);
    return FALSE;
  }

  /* need to calculate disc IDs before we ditch the data tracks */
  gst_cdda_base_src_calculate_cddb_id (src);
  gst_cddabasesrc_calculate_musicbrainz_discid (src);

#if 0
  /* adjust sector offsets if necessary */
  if (src->toc_bias) {
    src->toc_offset -= src->tracks[0].start;
  }
  for (i = 0; i < src->num_tracks; ++i) {
    src->tracks[i].start += src->toc_offset;
    src->tracks[i].end += src->toc_offset;
  }
#endif

  /* now that we calculated the various disc IDs,
   * sort the data tracks to end and ignore them */
  src->num_all_tracks = src->num_tracks;

  g_qsort_with_data (src->tracks, src->num_tracks,
      sizeof (GstCddaBaseSrcTrack), gst_cdda_base_src_track_sort_func, NULL);

  while (src->num_tracks > 0 && !src->tracks[src->num_tracks - 1].is_audio)
    --src->num_tracks;

  if (src->num_tracks == 0) {
    GST_DEBUG_OBJECT (src, "no audio tracks");
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("This CD has no audio tracks")), (NULL));
    gst_cdda_base_src_stop (basesrc);
    return FALSE;
  }

  gst_cdda_base_src_add_tags (src);

  if (src->index && GST_INDEX_IS_WRITABLE (src->index))
    gst_cdda_base_src_add_index_associations (src);

  src->cur_track = 0;
  src->prev_track = -1;

  if (src->uri_track > 0 && src->uri_track <= src->num_tracks) {
    GST_LOG_OBJECT (src, "seek to track %d", src->uri_track);
    src->cur_track = src->uri_track - 1;
    src->uri_track = -1;
    src->mode = GST_CDDA_BASE_SRC_MODE_NORMAL;
  }

  src->cur_sector = src->tracks[src->cur_track].start;
  GST_LOG_OBJECT (src, "starting at sector %d", src->cur_sector);

  gst_cdda_base_src_update_duration (src);

  return TRUE;
}

static void
gst_cdda_base_src_clear_tracks (GstCddaBaseSrc * src)
{
  if (src->tracks != NULL) {
    gint i;

    for (i = 0; i < src->num_all_tracks; ++i) {
      if (src->tracks[i].tags)
        gst_tag_list_free (src->tracks[i].tags);
    }

    g_free (src->tracks);
    src->tracks = NULL;
  }
  src->num_tracks = 0;
  src->num_all_tracks = 0;
}

static gboolean
gst_cdda_base_src_stop (GstBaseSrc * basesrc)
{
  GstCddaBaseSrcClass *klass = GST_CDDA_BASE_SRC_GET_CLASS (basesrc);
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (basesrc);

  g_assert (klass->close != NULL);

  klass->close (src);

  gst_cdda_base_src_clear_tracks (src);

  if (src->tags) {
    gst_tag_list_free (src->tags);
    src->tags = NULL;
  }

  src->prev_track = -1;
  src->cur_track = -1;

  return TRUE;
}


static GstFlowReturn
gst_cdda_base_src_create (GstPushSrc * pushsrc, GstBuffer ** buffer)
{
  GstCddaBaseSrcClass *klass = GST_CDDA_BASE_SRC_GET_CLASS (pushsrc);
  GstCddaBaseSrc *src = GST_CDDA_BASE_SRC (pushsrc);
  GstBuffer *buf;
  GstFormat format;
  gboolean eos;

  GstClockTime position = GST_CLOCK_TIME_NONE;
  GstClockTime duration = GST_CLOCK_TIME_NONE;
  gint64 qry_position;

  g_assert (klass->read_sector != NULL);

  switch (src->mode) {
    case GST_CDDA_BASE_SRC_MODE_NORMAL:
      eos = (src->cur_sector > src->tracks[src->cur_track].end);
      break;
    case GST_CDDA_BASE_SRC_MODE_CONTINUOUS:
      eos = (src->cur_sector > src->tracks[src->num_tracks - 1].end);
      src->cur_track = gst_cdda_base_src_get_track_from_sector (src,
          src->cur_sector);
      break;
    default:
      g_return_val_if_reached (GST_FLOW_ERROR);
  }

  if (eos) {
    src->prev_track = -1;
    GST_DEBUG_OBJECT (src, "EOS at sector %d, cur_track=%d, mode=%d",
        src->cur_sector, src->cur_track, src->mode);
    /* base class will send EOS for us */
    return GST_FLOW_UNEXPECTED;
  }

  if (src->prev_track != src->cur_track) {
    GstTagList *tags;

    tags = gst_tag_list_merge (src->tags, src->tracks[src->cur_track].tags,
        GST_TAG_MERGE_REPLACE);
    GST_LOG_OBJECT (src, "announcing tags: %" GST_PTR_FORMAT, tags);
    gst_element_found_tags_for_pad (GST_ELEMENT (src),
        GST_BASE_SRC_PAD (src), tags);
    src->prev_track = src->cur_track;

    gst_cdda_base_src_update_duration (src);

    g_object_notify (G_OBJECT (src), "track");
  }

  GST_LOG_OBJECT (src, "asking for sector %u", src->cur_sector);

  buf = klass->read_sector (src, src->cur_sector);

  if (buf == NULL) {
    GST_WARNING_OBJECT (src, "failed to read sector %u", src->cur_sector);
    return GST_FLOW_ERROR;
  }

  if (GST_BUFFER_CAPS (buf) == NULL) {
    gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));
  }

  format = GST_FORMAT_TIME;
  if (gst_pad_query_position (GST_BASE_SRC_PAD (src), &format, &qry_position)) {
    gint64 next_ts = 0;

    position = (GstClockTime) qry_position;

    ++src->cur_sector;
    if (gst_pad_query_position (GST_BASE_SRC_PAD (src), &format, &next_ts)) {
      duration = (GstClockTime) (next_ts - qry_position);
    }
    --src->cur_sector;
  }

  /* fallback duration: 4 bytes per sample, 44100 samples per second */
  if (duration == GST_CLOCK_TIME_NONE) {
    duration = gst_util_uint64_scale_int (GST_BUFFER_SIZE (buf) >> 2,
        GST_SECOND, 44100);
  }

  GST_BUFFER_TIMESTAMP (buf) = position;
  GST_BUFFER_DURATION (buf) = duration;

  GST_LOG_OBJECT (src, "pushing sector %d with timestamp %" GST_TIME_FORMAT,
      src->cur_sector, GST_TIME_ARGS (position));

  ++src->cur_sector;

  *buffer = buf;

  return GST_FLOW_OK;
}
