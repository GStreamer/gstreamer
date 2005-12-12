/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2005> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include "gst/gst-i18n-plugin.h"
#include <gst/gst.h>

/* taken from linux/cdrom.h */
#define CD_MSF_OFFSET       150 /* MSF numbering offset of first frame */
#define CD_SECS              60 /* seconds per minute */
#define CD_FRAMES            75 /* frames per second */

#include "gstcdparanoia.h"

GST_DEBUG_CATEGORY_STATIC (cdparanoia_debug);
#define GST_CAT_DEFAULT cdparanoia_debug

static GstElementDetails cdparanoia_details = {
  "CD Audio (cdda) Source, Paranoia IV",
  "Source/File",
  "Read audio from CD in paranoid mode",
  "Erik Walthinsen <omega@cse.ogi.edu>, " "Wim Taymans <wim@fluendo.com>",
};

/* we only have one possible caps */
static GstStaticPadTemplate cdparanoia_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 44100, " "channels = (int) 2")
    );


/* Define useful types for non-programmatic interfaces **********/
#define GST_TYPE_PARANOIA_MODE (gst_paranoia_mode_get_type())
static GType
gst_paranoia_mode_get_type (void)
{
  /* FIXME, these are flags and a few of them are missing. */
  static GType paranoia_mode_type = 0;
  static GEnumValue paranoia_modes[] = {
    {PARANOIA_MODE_DISABLE, "Disable paranoid checking", "disable"},
    {PARANOIA_MODE_OVERLAP, "cdda2wav-style overlap checking", "overlap"},
    {PARANOIA_MODE_FULL, "Full paranoia", "full"},
    {0, NULL, NULL},
  };

  if (!paranoia_mode_type) {
    paranoia_mode_type =
        g_enum_register_static ("GstParanoiaMode", paranoia_modes);
  }
  return paranoia_mode_type;
}

typedef enum
{
  GST_PARANOIA_LE = 0,
  GST_PARANOIA_BE = 1,
} GstParanoiaEndian;

#define GST_TYPE_PARANOIA_ENDIAN (gst_paranoia_endian_get_type())
static GType
gst_paranoia_endian_get_type (void)
{
  static GType paranoia_endian_type = 0;
  static GEnumValue paranoia_endians[] = {
    {GST_PARANOIA_LE, "treat drive as little endian", "little-endian"},
    {GST_PARANOIA_BE, "treat drive as big endian", "big-endian"},
    {0, NULL, NULL},
  };

  if (!paranoia_endian_type) {
    paranoia_endian_type =
        g_enum_register_static ("GstParanoiaEndian", paranoia_endians);
  }
  return paranoia_endian_type;
}

/* Standard stuff for signals and arguments **********/
/* CDParanoia signals and args */
enum
{
  SMILIE_CHANGE,
  TRANSPORT_ERROR,
  UNCORRECTED_ERROR,
  LAST_SIGNAL
};

#define DEFAULT_DEVICE			"/dev/cdrom"
#define DEFAULT_GENERIC_DEVICE		NULL
#define DEFAULT_DEFAULT_SECTORS		-1
#define DEFAULT_SEARCH_OVERLAP		-1
#define DEFAULT_ENDIAN			GST_PARANOIA_LE
#define DEFAULT_READ_SPEED		-1
#define DEFAULT_TOC_OFFSET		0
#define DEFAULT_TOC_BIAS		FALSE
#define DEFAULT_NEVER_SKIP		FALSE
#define DEFAULT_ABORT_ON_SKIP		FALSE
#define DEFAULT_PARANOIA_MODE		2
#define DEFAULT_SMILIE
#define DEFAULT_DISCID			NULL

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_GENERIC_DEVICE,
  PROP_DEFAULT_SECTORS,
  PROP_SEARCH_OVERLAP,
  PROP_ENDIAN,
  PROP_READ_SPEED,
  PROP_TOC_OFFSET,
  PROP_TOC_BIAS,
  PROP_NEVER_SKIP,
  PROP_ABORT_ON_SKIP,
  PROP_PARANOIA_MODE,
  PROP_SMILIE,
  PROP_DISCID
};

static void cdparanoia_finalize (GObject * obj);

static void cdparanoia_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void cdparanoia_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean cdparanoia_start (GstBaseSrc * bsrc);
static gboolean cdparanoia_stop (GstBaseSrc * bsrc);

static GstFlowReturn cdparanoia_create (GstPushSrc * src, GstBuffer ** buffer);
static gboolean cdparanoia_convert (CDParanoia * src,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean cdparanoia_query (GstBaseSrc * bsrc, GstQuery * query);
static const GstQueryType *cdparanoia_get_query_types (GstPad * pad);
static gboolean cdparanoia_is_seekable (GstBaseSrc * bsrc);
static gboolean cdparanoia_do_seek (GstBaseSrc * bsrc, GstSegment * segment);

static void cdparanoia_set_index (GstElement * element, GstIndex * index);
static GstIndex *cdparanoia_get_index (GstElement * element);

static void cdparanoia_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType cdparanoia_type)
{
  static const GInterfaceInfo urihandler_info = {
    cdparanoia_uri_handler_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (cdparanoia_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (cdparanoia_debug, "cdparanoia", 0,
      "cdparanoia element");
}

GST_BOILERPLATE_FULL (CDParanoia, cdparanoia, GstPushSrc, GST_TYPE_PUSH_SRC,
    _do_init);

static guint cdparanoia_signals[LAST_SIGNAL] = { 0 };

/* our two formats */
static GstFormat track_format;
static GstFormat sector_format;

static void
cdparanoia_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&cdparanoia_src_template));
  gst_element_class_set_details (element_class, &cdparanoia_details);

  /* Register the track and sector format */
  track_format = gst_format_register ("track", "CD track");
  sector_format = gst_format_register ("sector", "CD sector");
}

static void
cdparanoia_class_init (CDParanoiaClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;
  char *success = strerror_tr[0];

  success = NULL;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_PUSH_SRC);

  gobject_class->set_property = cdparanoia_set_property;
  gobject_class->get_property = cdparanoia_get_property;
  gobject_class->finalize = cdparanoia_finalize;

  cdparanoia_signals[SMILIE_CHANGE] =
      g_signal_new ("smilie-change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (CDParanoiaClass, smilie_change), NULL,
      NULL, g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);
  cdparanoia_signals[TRANSPORT_ERROR] =
      g_signal_new ("transport-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (CDParanoiaClass, transport_error),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  cdparanoia_signals[UNCORRECTED_ERROR] =
      g_signal_new ("uncorrected-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (CDParanoiaClass, uncorrected_error),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "CD device location", DEFAULT_DEVICE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_GENERIC_DEVICE,
      g_param_spec_string ("generic_device", "Generic device",
          "Use specified generic scsi device", DEFAULT_GENERIC_DEVICE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEFAULT_SECTORS,
      g_param_spec_int ("default_sectors", "Default sectors",
          "Force default number of sectors in read to n sectors", -1, 100,
          DEFAULT_DEFAULT_SECTORS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEARCH_OVERLAP,
      g_param_spec_int ("search_overlap", "Search overlap",
          "Force minimum overlap search during verification to n sectors", -1,
          75, DEFAULT_SEARCH_OVERLAP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ENDIAN,
      g_param_spec_enum ("endian", "Endian", "Force endian on drive",
          GST_TYPE_PARANOIA_ENDIAN, DEFAULT_ENDIAN, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_READ_SPEED,
      g_param_spec_int ("read_speed", "Read speed",
          "Read from device at specified speed", G_MININT, G_MAXINT,
          DEFAULT_READ_SPEED, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TOC_OFFSET,
      g_param_spec_int ("toc_offset", "TOC offset",
          "Add <n> sectors to the values reported", G_MININT, G_MAXINT,
          DEFAULT_TOC_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TOC_BIAS,
      g_param_spec_boolean ("toc_bias", "TOC bias",
          "Assume that the beginning offset of track 1 as reported in the TOC "
          "will be addressed as LBA 0.  Necessary for some Toshiba drives to "
          "get track boundaries", DEFAULT_TOC_BIAS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_NEVER_SKIP,
      g_param_spec_int ("never_skip", "Never skip",
          "never accept any less than perfect data reconstruction (don't allow "
          "'V's) but if [n] is given, skip after [n] retries without progress.",
          0, G_MAXINT, DEFAULT_NEVER_SKIP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ABORT_ON_SKIP,
      g_param_spec_boolean ("abort_on_skip", "Abort on skip",
          "Abort on imperfect reads/skips", DEFAULT_ABORT_ON_SKIP,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PARANOIA_MODE,
      g_param_spec_enum ("paranoia_mode", "Paranoia mode",
          "Type of checking to perform", GST_TYPE_PARANOIA_MODE, 0,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DISCID,
      g_param_spec_string ("discid", "discid", "The dics id", DEFAULT_DISCID,
          G_PARAM_READABLE));

  /* tags */
  gst_tag_register ("discid", GST_TAG_FLAG_META, G_TYPE_STRING,
      _("discid"), _("CDDA discid for metadata retrieval"),
      gst_tag_merge_use_first);

  gstelement_class->set_index = GST_DEBUG_FUNCPTR (cdparanoia_set_index);
  gstelement_class->get_index = GST_DEBUG_FUNCPTR (cdparanoia_get_index);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (cdparanoia_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (cdparanoia_stop);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (cdparanoia_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (cdparanoia_do_seek);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (cdparanoia_query);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (cdparanoia_create);
}

static void
cdparanoia_init (CDParanoia * cdparanoia, CDParanoiaClass * klass)
{
  gst_pad_set_query_type_function (GST_BASE_SRC_PAD (cdparanoia),
      GST_DEBUG_FUNCPTR (cdparanoia_get_query_types));

  /* we're not live and we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (cdparanoia), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (cdparanoia), FALSE);

  cdparanoia->device = g_strdup (DEFAULT_DEVICE);
  cdparanoia->generic_device = g_strdup (DEFAULT_GENERIC_DEVICE);
  cdparanoia->default_sectors = DEFAULT_DEFAULT_SECTORS;
  cdparanoia->search_overlap = DEFAULT_SEARCH_OVERLAP;
  cdparanoia->endian = DEFAULT_ENDIAN;
  cdparanoia->read_speed = DEFAULT_READ_SPEED;
  cdparanoia->toc_offset = DEFAULT_TOC_OFFSET;
  cdparanoia->toc_bias = DEFAULT_TOC_BIAS;
  cdparanoia->never_skip = DEFAULT_NEVER_SKIP;
  cdparanoia->paranoia_mode = 2;
  cdparanoia->abort_on_skip = DEFAULT_ABORT_ON_SKIP;

  cdparanoia->total_seconds = 0;
  cdparanoia->uri = NULL;
  cdparanoia->uri_track = -1;
  cdparanoia->seek_request = -1;
}

static void
cdparanoia_finalize (GObject * obj)
{
  CDParanoia *cdparanoia = CDPARANOIA (obj);

  g_free (cdparanoia->uri);
  cdparanoia->uri = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
cdparanoia_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  CDParanoia *src;

  g_return_if_fail (GST_IS_CDPARANOIA (object));

  src = CDPARANOIA (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (src->device);
      /* clear the filename if we get a NULL (is that possible?) */
      if (!g_ascii_strcasecmp (g_value_get_string (value), ""))
        src->device = NULL;
      /* otherwise set the new filename */
      else
        src->device = g_strdup (g_value_get_string (value));
      break;
    case PROP_GENERIC_DEVICE:

      if (src->generic_device)
        g_free (src->generic_device);
      /* reset the device if we get a NULL (is that possible?) */
      if (!g_ascii_strcasecmp (g_value_get_string (value), ""))
        src->generic_device = NULL;
      /* otherwise set the new filename */
      else
        src->generic_device = g_strdup (g_value_get_string (value));
      break;
    case PROP_DEFAULT_SECTORS:
      src->default_sectors = g_value_get_int (value);
      break;
    case PROP_SEARCH_OVERLAP:
      src->search_overlap = g_value_get_int (value);
      break;
    case PROP_ENDIAN:
      src->endian = g_value_get_enum (value);
      break;
    case PROP_READ_SPEED:
      src->read_speed = g_value_get_int (value);
      break;
    case PROP_TOC_OFFSET:
      src->toc_offset = g_value_get_int (value);
      break;
    case PROP_TOC_BIAS:
      src->toc_bias = g_value_get_boolean (value);
      break;
    case PROP_NEVER_SKIP:
      src->never_skip = g_value_get_int (value);
      break;
    case PROP_ABORT_ON_SKIP:
      src->abort_on_skip = g_value_get_boolean (value);
      break;
    case PROP_PARANOIA_MODE:
      src->paranoia_mode = g_value_get_enum (value);
      break;
    default:
      break;
  }

}

static void
cdparanoia_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  CDParanoia *src;

  g_return_if_fail (GST_IS_CDPARANOIA (object));

  src = CDPARANOIA (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, src->device);
      break;
    case PROP_GENERIC_DEVICE:
      g_value_set_string (value, src->generic_device);
      break;
    case PROP_DEFAULT_SECTORS:
      g_value_set_int (value, src->default_sectors);
      break;
    case PROP_SEARCH_OVERLAP:
      g_value_set_int (value, src->search_overlap);
      break;
    case PROP_ENDIAN:
      g_value_set_enum (value, src->endian);
      break;
    case PROP_READ_SPEED:
      g_value_set_int (value, src->read_speed);
      break;
    case PROP_TOC_OFFSET:
      g_value_set_int (value, src->toc_offset);
      break;
    case PROP_TOC_BIAS:
      g_value_set_boolean (value, src->toc_bias);
      break;
    case PROP_NEVER_SKIP:
      g_value_set_int (value, src->never_skip);
      break;
    case PROP_ABORT_ON_SKIP:
      g_value_set_boolean (value, src->abort_on_skip);
      break;
    case PROP_PARANOIA_MODE:
      g_value_set_enum (value, src->paranoia_mode);
      break;
    case PROP_DISCID:
      /*
       * Due to possible autocorrections of start sectors of audio tracks on 
       * multisession cds, we can maybe not compute the correct discid.
       * So issue a warning.
       * See cdparanoia/interface/common-interface.c:FixupTOC
       */
      if (src->d && src->d->cd_extra)
        g_warning
            ("DiscID on multisession discs might be broken. Use at own risk.");
      g_value_set_string (value, src->discid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
cdparanoia_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  CDParanoia *src;
  gboolean res;
  GstClockTime time;
  gint64 start_sec, end_sec;

  src = CDPARANOIA (bsrc);

  time = segment->time = segment->start;
  res = cdparanoia_convert (src, GST_FORMAT_TIME, time, &sector_format,
      &start_sec);
  src->segment_start_sector = start_sec;

  if (segment->stop != -1) {
    res &= cdparanoia_convert (src, GST_FORMAT_TIME, segment->stop,
        &sector_format, &end_sec);
    src->segment_end_sector = end_sec;
  } else {
    src->segment_end_sector = src->last_sector;
  }

  src->cur_sector = src->segment_start_sector;
  paranoia_seek (src->p, src->cur_sector, SEEK_SET);
  GST_DEBUG_OBJECT (src, "successfully seek'd to sector %d", src->cur_sector);

  return res;
}

static gboolean
cdparanoia_is_seekable (GstBaseSrc * bsrc)
{
  /* we're seekable */
  return TRUE;
}

static void
cdparanoia_callback (long inpos, int function)
{
}

static GstFlowReturn
cdparanoia_create (GstPushSrc * pushsrc, GstBuffer ** buffer)
{
  CDParanoia *src;
  GstBuffer *buf;
  gint16 *cdda_buf;
  gint64 timestamp;
  GstFormat format;

  src = CDPARANOIA (pushsrc);

  /* stop things apropriatly */
  if (src->cur_sector > src->segment_end_sector)
    goto eos;

  /* convert the sequence sector number to a timestamp */
  format = GST_FORMAT_TIME;
  timestamp = 0LL;
  cdparanoia_convert (src, sector_format, src->cur_sector, &format, &timestamp);

  /* read a sector */
  cdda_buf = paranoia_read (src->p, cdparanoia_callback);

  /* have to copy the buffer for now since we don't own it... */
  /* FIXME must ask monty about allowing ownership transfer */
  buf = gst_buffer_new_and_alloc (CD_FRAMESIZE_RAW);
  memcpy (GST_BUFFER_DATA (buf), cdda_buf, CD_FRAMESIZE_RAW);
  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  cdparanoia_convert (src, GST_FORMAT_BYTES,
      CD_FRAMESIZE_RAW, &format, &timestamp);
  GST_BUFFER_DURATION (buf) = timestamp;
  gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));

  GST_DEBUG_OBJECT (src, "pushing sector %d with timestamp %" GST_TIME_FORMAT,
      src->cur_sector, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  /* update current sector */
  src->cur_sector++;

  *buffer = buf;

  /* we're done, push the buffer off now */
  return GST_FLOW_OK;

eos:
  {
    GST_DEBUG_OBJECT (src, "reached EOS");
    return GST_FLOW_UNEXPECTED;
  }
}

/* need some stuff to get a discid (cdparanoia doesn't do cddb but lets
 * not stop other ppl doing it ;-) */
typedef int byte;

typedef struct
{
  byte m;
  byte s;
  byte f;
}
toc_msf;

/* cdparanoia provides the toc in lba format dang we need it in msf so
 * we have to convert it */
static inline void
lba_to_msf (const gint lba, byte * m, byte * s, byte * f)
{
  gint lba2 = lba;

  lba2 += CD_MSF_OFFSET;
  lba2 &= 0xffffff;
  *m = lba2 / (CD_SECS * CD_FRAMES);
  lba2 %= (CD_SECS * CD_FRAMES);
  *s = lba2 / CD_FRAMES;
  *f = lba2 % CD_FRAMES;
  *f += (*m) * 60 * 75;
  *f += (*s) * 75;
}

static void
lba_toc_to_msf_toc (TOC * lba_toc, toc_msf * msf_toc, gint tracks)
{
  gint i;

  for (i = 0; i <= tracks; i++)
    lba_to_msf (lba_toc[i].dwStartSector, &msf_toc[i].m, &msf_toc[i].s,
        &msf_toc[i].f);
}

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
cddb_discid (gchar * discid, toc_msf * toc, gint tracks)
{
  guint i = 0, t = 0, n = 0;

  while (i < tracks) {
    n = n + cddb_sum ((toc[i].m * 60) + toc[i].s);
    i++;
  }
  t = ((toc[tracks].m * 60) + toc[tracks].s) - ((toc[0].m * 60)
      + toc[0].s);
  sprintf (discid, "%08x", ((n % 0xff) << 24 | t << 8 | tracks));
}

/* get all the cddb info at once */
static void
get_cddb_info (TOC * toc, gint tracks, gchar * discid, gint64 * offsets,
    gint64 * total_seconds)
{
  toc_msf msf_toc[MAXTRK];
  gint i;
  gint64 *p = offsets;

  lba_toc_to_msf_toc (toc, &msf_toc[0], tracks);
  cddb_discid (discid, &msf_toc[0], tracks);

  for (i = 0; i < tracks; i++) {
    *p++ = msf_toc[i].f;
  }

  *total_seconds = msf_toc[tracks].f / 75;

}

static void
add_index_associations (CDParanoia * src)
{
  int i;

  for (i = 0; i < src->d->tracks; i++) {
    gint64 sector;

    sector = cdda_track_firstsector (src->d, i + 1);
    gst_index_add_association (src->index, src->index_id,
        GST_ASSOCIATION_FLAG_KEY_UNIT,
        track_format, i,
        sector_format, sector,
        GST_FORMAT_TIME,
        (gint64) (((CD_FRAMESIZE_RAW >> 2) * sector * GST_SECOND) / 44100),
        GST_FORMAT_BYTES, (gint64) (sector << 2), GST_FORMAT_DEFAULT,
        (gint64) ((CD_FRAMESIZE_RAW >> 2) * sector), NULL);
#if 0
    g_print ("Added association for track %d\n", i + 1);
    g_print ("Sector: %lld\n", sector);
    g_print ("Time: %lld\n",
        (gint64) (((CD_FRAMESIZE_RAW >> 2) * sector * GST_SECOND) / 44100));
    g_print ("Bytes: %lld\n", (gint64) (sector << 2));
    g_print ("Units: %lld\n", (gint64) ((CD_FRAMESIZE_RAW >> 2) * sector));
    g_print ("-----------\n");
#endif
  }
}

/* open the file, necessary to go to RUNNING state */
static gboolean
cdparanoia_start (GstBaseSrc * bsrc)
{
  GstTagList *taglist;
  gint i;
  gint paranoia_mode;
  CDParanoia *src;

  src = CDPARANOIA (bsrc);

  GST_DEBUG_OBJECT (src, "trying to open device...");

  /* find the device */
  if (src->generic_device != NULL) {
    src->d = cdda_identify_scsi (src->generic_device, src->device, FALSE, NULL);
  } else {
    if (src->device != NULL) {
      src->d = cdda_identify (src->device, FALSE, NULL);
    } else {
      src->d = cdda_identify ("/dev/cdrom", FALSE, NULL);
    }
  }

  /* fail if the device couldn't be found */
  if (src->d == NULL)
    goto no_device;

  /* set verbosity mode */
  cdda_verbose_set (src->d, CDDA_MESSAGE_FORGETIT, CDDA_MESSAGE_FORGETIT);

  /* set various other parameters */
  if (src->default_sectors != -1) {
    src->d->nsectors = src->default_sectors;
    src->d->bigbuff = src->default_sectors * CD_FRAMESIZE_RAW;
  }

  /* open the disc */
  if (cdda_open (src->d))
    goto open_failed;

  /* I don't like this here i would prefer it under get_cddb_info but for somereason
   * when leaving the function it clobbers the allocated mem and all is lost bugger
   */
  get_cddb_info (&src->d->disc_toc[0], src->d->tracks, src->discid,
      src->offsets, &src->total_seconds);

  g_object_freeze_notify (G_OBJECT (src));
  g_object_notify (G_OBJECT (src), "discid");
  g_object_thaw_notify (G_OBJECT (src));

  taglist = gst_tag_list_new ();
  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, "discid", src->discid, NULL);
  gst_element_found_tags (GST_ELEMENT (src), taglist);
  /* no need to forward, because discid is useless to other elements */

  if (src->toc_bias) {
    src->toc_offset -= cdda_track_firstsector (src->d, 1);
  }
  for (i = 0; i < src->d->tracks + 1; i++) {
    src->d->disc_toc[i].dwStartSector += src->toc_offset;
  }

  if (src->read_speed != -1) {
    cdda_speed_set (src->d, src->read_speed);
  }

  /* save these ones - skip lead-in */
  src->first_sector = cdda_track_firstsector (src->d, 1);
  src->last_sector = cdda_disc_lastsector (src->d);

  /* this is the default segment we will play */
  src->segment_start_sector = src->first_sector;
  src->segment_end_sector = src->last_sector;

  /* create the paranoia struct and set it up */
  src->p = paranoia_init (src->d);
  if (src->p == NULL)
    goto init_failed;

  paranoia_mode = src->paranoia_mode;
  if (src->never_skip)
    paranoia_mode |= PARANOIA_MODE_NEVERSKIP;

  paranoia_modeset (src->p, paranoia_mode);

  if (src->search_overlap != -1)
    paranoia_overlapset (src->p, src->search_overlap);

  if (src->index && GST_INDEX_IS_WRITABLE (src->index))
    add_index_associations (src);

  {
    GstFormat format;
    gint64 duration;

    format = GST_FORMAT_TIME;

    if (!cdparanoia_convert (src,
            sector_format, src->last_sector + 1 - src->first_sector,
            &format, &duration))
      duration = -1;

    gst_segment_set_duration (&bsrc->segment, format, duration);
  }

  GST_DEBUG_OBJECT (src, "device successfully openend");

  return TRUE;

  /* ERRORS */
no_device:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open CD device for reading.")), ("cdda_identify failed"));
    return FALSE;
  }
open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open CD device for reading.")), ("cdda_open failed"));
    cdda_close (src->d);
    src->d = NULL;
    return FALSE;
  }
init_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("failed to initialize paranoia"), ("failed to initialize paranoia"));
    return FALSE;
  }
}

/* close the file */
static gboolean
cdparanoia_stop (GstBaseSrc * bsrc)
{
  CDParanoia *src;

  src = CDPARANOIA (bsrc);

  /* kill the paranoia state */
  paranoia_free (src->p);
  src->p = NULL;

  src->total_seconds = 0LL;
  /* close the disc */
  cdda_close (src->d);
  src->d = NULL;

  return TRUE;
}

#if 0
static const GstFormat *
cdparanoia_get_formats (GstPad * pad)
{
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0,                          /* filled later */
    0,                          /* filled later */
    0
  };

  formats[3] = track_format;
  formats[4] = sector_format;

  return formats;
}
#endif

static gboolean
cdparanoia_convert (CDParanoia * src,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          src_value <<= 2;      /* 4 bytes per sample */
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value, 44100, GST_SECOND);
          break;
        default:
          if (*dest_format == track_format || *dest_format == sector_format) {
            gint sector = gst_util_uint64_scale (src_value, 44100,
                (CD_FRAMESIZE_RAW >> 2) * GST_SECOND);

            if (*dest_format == sector_format) {
              *dest_value = sector;
            } else {
              *dest_value = cdda_sector_gettrack (src->d, sector) - 1;
            }
          } else
            goto error;
          break;
      }
      break;
    case GST_FORMAT_BYTES:
      src_value >>= 2;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * 4;
          break;
        case GST_FORMAT_TIME:
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, 44100);
          break;
        default:
          if (*dest_format == track_format || *dest_format == sector_format) {
            gint sector = src_value / (CD_FRAMESIZE_RAW >> 2);

            if (*dest_format == track_format) {
              *dest_value = cdda_sector_gettrack (src->d, sector) - 1;
            } else {
              *dest_value = sector;
            }
          } else
            goto error;
          break;
      }
      break;
    default:
    {
      gint64 sector;

      if (src_format == track_format) {
        /* some sanity checks */
        if (src_value < 0 || src_value > src->d->tracks)
          goto error;

        sector = cdda_track_firstsector (src->d, src_value + 1);
      } else if (src_format == sector_format) {
        sector = src_value;
      } else
        goto error;

      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = ((CD_FRAMESIZE_RAW >> 2) * sector * GST_SECOND) / 44100;
          break;
        case GST_FORMAT_BYTES:
          sector <<= 2;
        case GST_FORMAT_DEFAULT:
          *dest_value = (CD_FRAMESIZE_RAW >> 2) * sector;
          break;
        default:
          if (*dest_format == sector_format) {
            *dest_value = sector;
          } else if (*dest_format == track_format) {
            /* if we go past the last sector, make sure to report the last track */
            if (sector > src->last_sector - src->first_sector)
              *dest_value = cdda_sector_gettrack (src->d, src->last_sector);
            else
              *dest_value = cdda_sector_gettrack (src->d,
                  sector + src->first_sector) - 1;
          } else
            goto error;
          break;
      }
      break;
    }
  }
done:
  return res;

error:
  {
    GST_DEBUG_OBJECT (src, "convert failed");
    res = FALSE;
    goto done;
  }
}

static const GstQueryType *
cdparanoia_get_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    GST_QUERY_SEGMENT,
    0
  };

  return src_query_types;
}

static gboolean
cdparanoia_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res = TRUE;
  CDParanoia *src;

  src = CDPARANOIA (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, NULL);
      res = cdparanoia_convert (src, src_fmt, src_val, &dest_fmt, &dest_val);
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }
  return res;
}

static void
cdparanoia_set_index (GstElement * element, GstIndex * index)
{
  CDParanoia *cdparanoia;

  cdparanoia = CDPARANOIA (element);

  cdparanoia->index = index;

  gst_index_get_writer_id (index, GST_OBJECT (cdparanoia),
      &cdparanoia->index_id);
  gst_index_add_format (index, cdparanoia->index_id, track_format);
  gst_index_add_format (index, cdparanoia->index_id, sector_format);
}

static GstIndex *
cdparanoia_get_index (GstElement * element)
{
  CDParanoia *cdparanoia;

  cdparanoia = CDPARANOIA (element);

  return cdparanoia->index;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cdparanoia", GST_RANK_NONE,
          GST_TYPE_CDPARANOIA))
    return FALSE;

  return TRUE;
}

/* GSTURIHANDLER INTERFACE *************************************************/

static guint
cdparanoia_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
cdparanoia_uri_get_protocols (void)
{
  static gchar *protocols[] = { "cdda", NULL };

  return protocols;
}
static const gchar *
cdparanoia_uri_get_uri (GstURIHandler * handler)
{
  CDParanoia *cdparanoia = CDPARANOIA (handler);

  return cdparanoia->uri;
}

static gboolean
cdparanoia_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;

  CDParanoia *cdparanoia = CDPARANOIA (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "cdda") != 0)
    goto invalid_proto;
  g_free (protocol);

  location = gst_uri_get_location (uri);
  cdparanoia->uri_track = strtol (location, NULL, 10);
  if (cdparanoia->uri_track > 0) {
    cdparanoia->seek_request = cdparanoia->uri_track;
  }
  g_free (location);

  return TRUE;

  /* ERRORS */
invalid_proto:
  {
    g_free (protocol);
    return FALSE;
  }
}

static void
cdparanoia_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = cdparanoia_uri_get_type;
  iface->get_protocols = cdparanoia_uri_get_protocols;
  iface->get_uri = cdparanoia_uri_get_uri;
  iface->set_uri = cdparanoia_uri_set_uri;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cdparanoia",
    "Read audio from CD in paranoid mode",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
