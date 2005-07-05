/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2001 Billy Biggs <vektor@dumbterm.net>.
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
//#include <linux/cdrom.h>
#include <assert.h>

#include "dvdreadsrc.h"
#include "stream_labels.h"

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_types.h>
#include <dvdread/ifo_read.h>
#include <dvdread/nav_read.h>
#include <dvdread/nav_print.h>

struct _DVDReadSrcPrivate
{
  /* pads */
  GstPad *srcpad;

  /* location */
  gchar *location;
  gchar *last_uri;

  gboolean new_seek;

  gboolean new_cell;

  int title, chapter, angle;
  int pgc_id, start_cell, cur_cell, cur_pack;
  int last_cell;
  int ttn, pgn, next_cell;
  dvd_reader_t *dvd;
  dvd_file_t *dvd_title;
  ifo_handle_t *vmg_file;
  tt_srpt_t *tt_srpt;
  ifo_handle_t *vts_file;
  vts_ptt_srpt_t *vts_ptt_srpt;
  pgc_t *cur_pgc;

  /* where we are */
  gboolean seek_pend, flush_pend;
  GstFormat seek_pend_fmt;
};

GST_DEBUG_CATEGORY_STATIC (gstdvdreadsrc_debug);
#define GST_CAT_DEFAULT (gstdvdreadsrc_debug)

GstElementDetails dvdreadsrc_details = {
  "DVD Source",
  "Source/File/DVD",
  "Access a DVD title/chapter/angle using libdvdread",
  "Erik Walthinsen <omega@cse.ogi.edu>",
};


/* DVDReadSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_DEVICE,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE
};

static void dvdreadsrc_base_init (gpointer g_class);
static void dvdreadsrc_class_init (DVDReadSrcClass * klass);
static void dvdreadsrc_init (DVDReadSrc * dvdreadsrc);
static void dvdreadsrc_finalize (GObject * object);

static void dvdreadsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void dvdreadsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static const GstEventMask *dvdreadsrc_get_event_mask (GstPad * pad);
static const GstQueryType *dvdreadsrc_get_query_types (GstPad * pad);
static const GstFormat *dvdreadsrc_get_formats (GstPad * pad);
static gboolean dvdreadsrc_srcpad_event (GstPad * pad, GstEvent * event);
static gboolean dvdreadsrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);

static GstData *dvdreadsrc_get (GstPad * pad);
static GstElementStateReturn dvdreadsrc_change_state (GstElement * element);

static void dvdreadsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);


static GstElementClass *parent_class = NULL;

static GstFormat sector_format, angle_format, title_format, chapter_format;

/*static guint dvdreadsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
dvdreadsrc_get_type (void)
{
  static GType dvdreadsrc_type = 0;

  if (!dvdreadsrc_type) {
    static const GTypeInfo dvdreadsrc_info = {
      sizeof (DVDReadSrcClass),
      dvdreadsrc_base_init,
      NULL,
      (GClassInitFunc) dvdreadsrc_class_init,
      NULL,
      NULL,
      sizeof (DVDReadSrc),
      0,
      (GInstanceInitFunc) dvdreadsrc_init,
    };
    static const GInterfaceInfo urihandler_info = {
      dvdreadsrc_uri_handler_init,
      NULL,
      NULL
    };

    sector_format = gst_format_register ("sector", "DVD sector");
    title_format = gst_format_register ("title", "DVD title");
    chapter_format = gst_format_register ("chapter", "DVD chapter");
    angle_format = gst_format_register ("angle", "DVD angle");

    dvdreadsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "DVDReadSrc",
        &dvdreadsrc_info, 0);
    g_type_add_interface_static (dvdreadsrc_type,
        GST_TYPE_URI_HANDLER, &urihandler_info);
  }

  return dvdreadsrc_type;
}

static void
dvdreadsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &dvdreadsrc_details);
}

static void
dvdreadsrc_class_init (DVDReadSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION,
      g_param_spec_string ("location", "Location",
          "DVD device location (deprecated; use device)",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "DVD device location", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TITLE,
      g_param_spec_int ("title", "title", "title",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHAPTER,
      g_param_spec_int ("chapter", "chapter", "chapter",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ANGLE,
      g_param_spec_int ("angle", "angle", "angle",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR (dvdreadsrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (dvdreadsrc_get_property);

  gobject_class->finalize = dvdreadsrc_finalize;

  gstelement_class->change_state = dvdreadsrc_change_state;

  GST_DEBUG_CATEGORY_INIT (gstdvdreadsrc_debug, "dvdreadsrc", 0,
      "DVD reader element based on dvdreadsrc");
}

static void
dvdreadsrc_init (DVDReadSrc * dvdreadsrc)
{
  dvdreadsrc->priv = g_new (DVDReadSrcPrivate, 1);
  dvdreadsrc->priv->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (dvdreadsrc->priv->srcpad, dvdreadsrc_get);
  gst_pad_set_event_function (dvdreadsrc->priv->srcpad,
      dvdreadsrc_srcpad_event);
  gst_pad_set_event_mask_function (dvdreadsrc->priv->srcpad,
      dvdreadsrc_get_event_mask);
  gst_pad_set_query_function (dvdreadsrc->priv->srcpad,
      dvdreadsrc_srcpad_query);
  gst_pad_set_query_type_function (dvdreadsrc->priv->srcpad,
      dvdreadsrc_get_query_types);
  gst_pad_set_formats_function (dvdreadsrc->priv->srcpad,
      dvdreadsrc_get_formats);
  gst_element_add_pad (GST_ELEMENT (dvdreadsrc), dvdreadsrc->priv->srcpad);

  dvdreadsrc->priv->dvd = NULL;
  dvdreadsrc->priv->vts_file = NULL;
  dvdreadsrc->priv->vmg_file = NULL;
  dvdreadsrc->priv->dvd_title = NULL;

  dvdreadsrc->priv->location = g_strdup ("/dev/dvd");
  dvdreadsrc->priv->last_uri = NULL;
  dvdreadsrc->priv->new_seek = TRUE;
  dvdreadsrc->priv->new_cell = TRUE;
  dvdreadsrc->priv->title = 0;
  dvdreadsrc->priv->chapter = 0;
  dvdreadsrc->priv->angle = 0;

  dvdreadsrc->priv->seek_pend = FALSE;
  dvdreadsrc->priv->flush_pend = FALSE;
  dvdreadsrc->priv->seek_pend_fmt = GST_FORMAT_UNDEFINED;
}

static void
dvdreadsrc_finalize (GObject * object)
{
  DVDReadSrc *dvdreadsrc = DVDREADSRC (object);

  if (dvdreadsrc->priv) {
    g_free (dvdreadsrc->priv->location);
    g_free (dvdreadsrc->priv->last_uri);
    g_free (dvdreadsrc->priv);
    dvdreadsrc->priv = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
dvdreadsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  DVDReadSrc *src;
  DVDReadSrcPrivate *priv;

  g_return_if_fail (GST_IS_DVDREADSRC (object));

  src = DVDREADSRC (object);
  priv = src->priv;

  switch (prop_id) {
    case ARG_LOCATION:
    case ARG_DEVICE:
      /* the element must be stopped in order to do this */
      /*g_return_if_fail(!GST_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      g_free (priv->location);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        priv->location = g_strdup ("/dev/dvd");
      /* otherwise set the new filename */
      else
        priv->location = g_strdup (g_value_get_string (value));
      break;
    case ARG_TITLE:
      priv->title = g_value_get_int (value);
      priv->new_seek = TRUE;
      break;
    case ARG_CHAPTER:
      priv->chapter = g_value_get_int (value);
      priv->new_seek = TRUE;
      break;
    case ARG_ANGLE:
      priv->angle = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
dvdreadsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  DVDReadSrc *src;
  DVDReadSrcPrivate *priv;

  g_return_if_fail (GST_IS_DVDREADSRC (object));

  src = DVDREADSRC (object);
  priv = src->priv;

  switch (prop_id) {
    case ARG_DEVICE:
    case ARG_LOCATION:
      g_value_set_string (value, priv->location);
      break;
    case ARG_TITLE:
      g_value_set_int (value, priv->title);
      break;
    case ARG_CHAPTER:
      g_value_set_int (value, priv->chapter);
      break;
    case ARG_ANGLE:
      g_value_set_int (value, priv->angle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
 * Querying and seeking.
 */

static const GstEventMask *
dvdreadsrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_CUR |
          GST_SEEK_METHOD_SET | GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH},
    {0, 0}
  };

  return masks;
}

static const GstQueryType *
dvdreadsrc_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static const GstFormat *
dvdreadsrc_get_formats (GstPad * pad)
{
  static GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0, 0, 0, 0,                 /* init later */
    0,
  };
  if (formats[1] == 0) {
    formats[1] = sector_format;
    formats[2] = angle_format;
    formats[3] = title_format;
    formats[4] = chapter_format;
  }

  return formats;
}

static gboolean
dvdreadsrc_srcpad_event (GstPad * pad, GstEvent * event)
{
  DVDReadSrc *dvdreadsrc = DVDREADSRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gint64 new_off, total, cur;
      GstFormat fmt;

      /* get requested offset */
      new_off = GST_EVENT_SEEK_OFFSET (event);
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_BYTES:
          new_off /= DVD_VIDEO_LB_LEN;
          fmt = sector_format;
          break;
        default:
          fmt = GST_EVENT_SEEK_FORMAT (event);
          if (fmt == sector_format ||
              fmt == angle_format ||
              fmt == title_format || fmt == chapter_format)
            break;
          GST_LOG ("Unsupported seek format");
          return FALSE;
      }

      /* get current offset and length */
      gst_pad_query (pad, GST_QUERY_TOTAL, &fmt, &total);
      gst_pad_query (pad, GST_QUERY_POSITION, &fmt, &cur);
      if (cur == new_off) {
        GST_LOG ("We're already at that position!");
        return TRUE;
      }

      /* get absolute */
      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          /* no-op */
          break;
        case GST_SEEK_METHOD_CUR:
          new_off += cur;
          break;
        case GST_SEEK_METHOD_END:
          new_off = total - new_off;
          break;
        default:
          GST_LOG ("Unsupported seek method");
          return FALSE;
      }
      if (new_off < 0 || new_off >= total) {
        GST_LOG ("Invalid seek position");
        return FALSE;
      }

      GST_LOG ("Seeking to unit %d in format %d", new_off, fmt);

      if (fmt == sector_format || fmt == chapter_format || fmt == title_format) {
        if (fmt == sector_format) {
          dvdreadsrc->priv->cur_pack = new_off;
        } else if (fmt == chapter_format) {
          dvdreadsrc->priv->cur_pack = 0;
          dvdreadsrc->priv->chapter = new_off;
          dvdreadsrc->priv->seek_pend_fmt = fmt;
        } else if (fmt == title_format) {
          dvdreadsrc->priv->cur_pack = 0;
          dvdreadsrc->priv->title = new_off;
          dvdreadsrc->priv->chapter = 0;
          dvdreadsrc->priv->seek_pend_fmt = fmt;
        }

        /* leave for events */
        dvdreadsrc->priv->seek_pend = TRUE;
        if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH)
          dvdreadsrc->priv->flush_pend = TRUE;
      } else if (fmt == angle_format) {
        dvdreadsrc->priv->angle = new_off;
      }

      break;
    }
    default:
      res = FALSE;
      break;
  }

  gst_event_unref (event);

  return res;
}

static gboolean
dvdreadsrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  DVDReadSrc *dvdreadsrc = DVDREADSRC (gst_pad_get_parent (pad));
  DVDReadSrcPrivate *priv = dvdreadsrc->priv;
  gboolean res = TRUE;

  if (!GST_FLAG_IS_SET (dvdreadsrc, DVDREADSRC_OPEN))
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_BYTES:
          *value = DVDFileSize (priv->dvd_title) * DVD_VIDEO_LB_LEN;
          break;
        default:
          if (*format == sector_format) {
            *value = DVDFileSize (priv->dvd_title);
          } else if (*format == title_format) {
            *value = priv->tt_srpt->nr_of_srpts;
          } else if (*format == chapter_format) {
            *value = priv->tt_srpt->title[priv->title].nr_of_ptts;
          } else if (*format == angle_format) {
            *value = priv->tt_srpt->title[priv->title].nr_of_angles;
          } else {
            GST_LOG ("Unknown format");
            res = FALSE;
          }
          break;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_BYTES:
          *value = priv->cur_pack * DVD_VIDEO_LB_LEN;
          break;
        default:
          if (*format == sector_format) {
            *value = priv->cur_pack;
          } else if (*format == title_format) {
            *value = priv->title;
          } else if (*format == chapter_format) {
            *value = priv->chapter;
          } else if (*format == angle_format) {
            *value = priv->angle;
          } else {
            GST_LOG ("Unknown format");
            res = FALSE;
          }
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

/**
 * Returns true if the pack is a NAV pack.  This check is clearly insufficient,
 * and sometimes we incorrectly think that valid other packs are NAV packs.  I
 * need to make this stronger.
 */
static int
is_nav_pack (unsigned char *buffer)
{
  return (buffer[41] == 0xbf && buffer[1027] == 0xbf);
}

static int
_close (DVDReadSrcPrivate * priv)
{
  ifoClose (priv->vts_file);
  priv->vts_file = NULL;

  ifoClose (priv->vmg_file);
  priv->vmg_file = NULL;

  DVDCloseFile (priv->dvd_title);
  priv->dvd_title = NULL;

  DVDClose (priv->dvd);
  priv->dvd = NULL;

  return 0;
}

static int
_open (DVDReadSrcPrivate * priv, const gchar * location)
{
  g_return_val_if_fail (priv != NULL, -1);
  g_return_val_if_fail (location != NULL, -1);

  /**
   * Open the disc.
   */
  priv->dvd = DVDOpen (location);
  if (!priv->dvd) {
    GST_ERROR ("Couldn't open DVD: %s", location);
    return -1;
  }


  /**
   * Load the video manager to find out the information about the titles on
   * this disc.
   */
  priv->vmg_file = ifoOpen (priv->dvd, 0);
  if (!priv->vmg_file) {
    GST_ERROR ("Can't open VMG info");
    return -1;
  }
  priv->tt_srpt = priv->vmg_file->tt_srpt;

  return 0;
}

static int
_seek_title (DVDReadSrcPrivate * priv, int title, int angle)
{
  GHashTable *languagelist = NULL;

    /**
     * Make sure our title number is valid.
     */
  GST_LOG ("There are %d titles on this DVD", priv->tt_srpt->nr_of_srpts);
  if (title < 0 || title >= priv->tt_srpt->nr_of_srpts) {
    GST_WARNING ("Invalid title %d (only %d available)",
        title, priv->tt_srpt->nr_of_srpts);

    if (title < 0)
      title = 0;
    else
      title = priv->tt_srpt->nr_of_srpts - 1;
  }

  GST_LOG ("There are %d chapters in this title",
      priv->tt_srpt->title[title].nr_of_ptts);

    /**
     * Make sure the angle number is valid for this title.
     */
  GST_LOG ("There are %d angles available in this title",
      priv->tt_srpt->title[title].nr_of_angles);

  if (angle < 0 || angle >= priv->tt_srpt->title[title].nr_of_angles) {
    GST_WARNING ("Invalid angle %d (only %d available)",
        angle, priv->tt_srpt->title[title].nr_of_angles);
    if (angle < 0)
      angle = 0;
    else
      angle = priv->tt_srpt->title[title].nr_of_angles - 1;
  }

    /**
     * Load the VTS information for the title set our title is in.
     */
  priv->vts_file =
      ifoOpen (priv->dvd, priv->tt_srpt->title[title].title_set_nr);
  if (!priv->vts_file) {
    GST_ERROR ("Can't open the info file of title %d",
        priv->tt_srpt->title[title].title_set_nr);
    _close (priv);
    return -1;
  }

  priv->ttn = priv->tt_srpt->title[title].vts_ttn;
  priv->vts_ptt_srpt = priv->vts_file->vts_ptt_srpt;

    /**
     * We've got enough info, time to open the title set data.
     */
  priv->dvd_title =
      DVDOpenFile (priv->dvd, priv->tt_srpt->title[title].title_set_nr,
      DVD_READ_TITLE_VOBS);
  if (!priv->dvd_title) {
    GST_ERROR ("Can't open title VOBS (VTS_%02d_1.VOB)",
        priv->tt_srpt->title[title].title_set_nr);
    _close (priv);
    return -1;
  }

  /* Get stream labels for all audio and subtitle streams */
  languagelist = dvdreadsrc_init_languagelist ();

  dvdreadsrc_get_audio_stream_labels (priv->vts_file, languagelist);
  dvdreadsrc_get_subtitle_stream_labels (priv->vts_file, languagelist);

  g_hash_table_destroy (languagelist);

  GST_LOG ("Opened title %d, angle %d", title, angle);
  priv->title = title;
  priv->angle = angle;

  return 0;
}

static int
_seek_chapter (DVDReadSrcPrivate * priv, int chapter)
{
  int i;

    /**
     * Make sure the chapter number is valid for this title.
     */
  if (chapter < 0 || chapter >= priv->tt_srpt->title[priv->title].nr_of_ptts) {
    GST_WARNING ("Invalid chapter %d (only %d available)",
        chapter, priv->tt_srpt->title[priv->title].nr_of_ptts);
    if (chapter < 0)
      chapter = 0;
    chapter = priv->tt_srpt->title[priv->title].nr_of_ptts - 1;
  }

    /**
     * Determine which program chain we want to watch.  This is based on the
     * chapter number.
     */
  priv->pgc_id = priv->vts_ptt_srpt->title[priv->ttn - 1].ptt[chapter].pgcn;
  priv->pgn = priv->vts_ptt_srpt->title[priv->ttn - 1].ptt[chapter].pgn;
  priv->cur_pgc = priv->vts_file->vts_pgcit->pgci_srp[priv->pgc_id - 1].pgc;
  priv->start_cell = priv->cur_pgc->program_map[priv->pgn - 1] - 1;

  if (chapter + 1 == priv->tt_srpt->title[priv->title].nr_of_ptts) {
    priv->last_cell = priv->cur_pgc->nr_of_cells;
  } else {
    priv->last_cell =
        priv->cur_pgc->program_map[(priv->vts_ptt_srpt->title[priv->ttn -
                1].ptt[chapter + 1].pgn) - 1] - 1;
  }

  GST_LOG ("Opened chapter %d - cell %d-%d",
      chapter, priv->start_cell, priv->last_cell);

  /* retrieve position */
  priv->cur_pack = 0;
  for (i = 0; i < chapter; i++) {
    gint c1, c2;

    c1 = priv->cur_pgc->program_map[(priv->vts_ptt_srpt->title[priv->ttn -
                1].ptt[i].pgn) - 1] - 1;
    if (i + 1 == priv->tt_srpt->title[priv->title].nr_of_ptts) {
      c2 = priv->cur_pgc->nr_of_cells;
    } else {
      c2 = priv->cur_pgc->program_map[(priv->vts_ptt_srpt->title[priv->ttn -
                  1].ptt[i + 1].pgn) - 1] - 1;
    }

    for (; c1 < c2; c1++) {
      priv->cur_pack +=
          priv->cur_pgc->cell_playback[c1].last_sector -
          priv->cur_pgc->cell_playback[c1].first_sector;
    }
  }

  /* prepare reading for new cell */
  priv->new_cell = TRUE;
  priv->next_cell = priv->start_cell;

  priv->chapter = chapter;
  return 0;
}

static int
get_next_cell_for (DVDReadSrcPrivate * priv, int cell)
{
  /* Check if we're entering an angle block. */
  if (priv->cur_pgc->cell_playback[cell].block_type == BLOCK_TYPE_ANGLE_BLOCK) {
    int i;

    for (i = 0;; ++i) {
      if (priv->cur_pgc->cell_playback[cell + i].block_mode
          == BLOCK_MODE_LAST_CELL) {
        return cell + i + 1;
      }
    }

    /* not reached */
  }

  return cell + 1;
}

/*
 * Read function.
 * -1: error, -2: eos, -3: try again, 0: ok.
 */

static int
_read (DVDReadSrcPrivate * priv, int angle, int new_seek, GstBuffer * buf)
{
  unsigned char *data, static_data[DVD_VIDEO_LB_LEN];

  if (buf) {
    data = GST_BUFFER_DATA (buf);
  } else {
    data = static_data;
  }

    /**
     * Playback by cell in this pgc, starting at the cell for our chapter.
     */
  if (new_seek)
    priv->cur_cell = priv->start_cell;

again:

  if (priv->cur_cell < priv->last_cell) {
    if (priv->new_cell || new_seek) {
      if (!new_seek) {
        priv->cur_cell = priv->next_cell;
        if (priv->cur_cell >= priv->last_cell) {
          GST_LOG ("last cell in chapter");
          goto again;
        }
      }

      /* take angle into account */
      if (priv->cur_pgc->cell_playback[priv->cur_cell].block_type
          == BLOCK_TYPE_ANGLE_BLOCK)
        priv->cur_cell += angle;

      /* calculate next cell */
      priv->next_cell = get_next_cell_for (priv, priv->cur_cell);

      /**
       * We loop until we're out of this cell.
       */
      priv->cur_pack =
          priv->cur_pgc->cell_playback[priv->cur_cell].first_sector;
      priv->new_cell = FALSE;
    }

    if (priv->cur_pack <
        priv->cur_pgc->cell_playback[priv->cur_cell].last_sector) {
      dsi_t dsi_pack;
      unsigned int next_vobu, next_ilvu_start, cur_output_size;
      int len;

            /**
             * Read NAV packet.
             */
    nav_retry:

      len = DVDReadBlocks (priv->dvd_title, priv->cur_pack, 1, data);
      if (len == 0) {
        GST_ERROR ("Read failed for block %d", priv->cur_pack);
        return -1;
      }

      if (!is_nav_pack (data)) {
        priv->cur_pack++;
        goto nav_retry;
      }


            /**
             * Parse the contained dsi packet.
             */
      navRead_DSI (&dsi_pack, &(data[DSI_START_BYTE]));
      assert (priv->cur_pack == dsi_pack.dsi_gi.nv_pck_lbn);


            /**
             * Determine where we go next.  These values are the ones we mostly
             * care about.
             */
      next_ilvu_start = priv->cur_pack + dsi_pack.sml_agli.data[angle].address;
      cur_output_size = dsi_pack.dsi_gi.vobu_ea;


            /**
             * If we're not at the end of this cell, we can determine the next
             * VOBU to display using the VOBU_SRI information section of the
             * DSI.  Using this value correctly follows the current angle,
             * avoiding the doubled scenes in The Matrix, and makes our life
             * really happy.
             *
             * Otherwise, we set our next address past the end of this cell to
             * force the code above to go to the next cell in the program.
             */
      if (dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL) {
        next_vobu = priv->cur_pack + (dsi_pack.vobu_sri.next_vobu & 0x7fffffff);
      } else {
        next_vobu = priv->cur_pack + cur_output_size + 1;
      }

      assert (cur_output_size < 1024);
      priv->cur_pack++;

      if (buf) {
            /**
             * Read in and output cursize packs.
             */
        len =
            DVDReadBlocks (priv->dvd_title, priv->cur_pack, cur_output_size,
            data);
        if (len != cur_output_size) {
          GST_ERROR ("Read failed for %d blocks at %d",
              cur_output_size, priv->cur_pack);
          return -1;
        }

        GST_BUFFER_SIZE (buf) = cur_output_size * DVD_VIDEO_LB_LEN;
        //GST_BUFFER_OFFSET (buf) = priv->cur_pack * DVD_VIDEO_LB_LEN;
      }

      priv->cur_pack = next_vobu;

      GST_LOG ("done reading data - %u sectors", cur_output_size);

      return 0;
    } else {
      priv->new_cell = TRUE;
    }
  } else {
    /* swap to next chapter */
    if (priv->chapter + 1 == priv->tt_srpt->title[priv->title].nr_of_ptts) {
      GST_LOG ("last chapter done - eos");
      return -2;
    }

    GST_LOG ("end-of-chapter, switch to next");

    priv->chapter++;
    _seek_chapter (priv, priv->chapter);
  }

  /* again */
  GST_LOG ("Need another try");

  return -3;
}

static gboolean
seek_sector (DVDReadSrcPrivate * priv, int angle)
{
  gint seek_to = priv->cur_pack;
  gint chapter, sectors, next, cur, i;

  /* retrieve position */
  priv->cur_pack = 0;
  for (i = 0; i < priv->tt_srpt->title[priv->title].nr_of_ptts; i++) {
    gint c1, c2;

    c1 = priv->cur_pgc->program_map[(priv->vts_ptt_srpt->title[priv->ttn -
                1].ptt[i].pgn) - 1] - 1;
    if (i + 1 == priv->tt_srpt->title[priv->title].nr_of_ptts) {
      c2 = priv->cur_pgc->nr_of_cells;
    } else {
      c2 = priv->cur_pgc->program_map[(priv->vts_ptt_srpt->title[priv->ttn -
                  1].ptt[i + 1].pgn) - 1] - 1;
    }

    for (next = cur = c1; cur < c2;) {
      if (next != cur) {
        sectors =
            priv->cur_pgc->cell_playback[cur].last_sector -
            priv->cur_pgc->cell_playback[cur].first_sector;
        if (priv->cur_pack + sectors > seek_to) {
          chapter = i;
          goto done;
        }
        priv->cur_pack += sectors;
      }
      cur = next;
      if (priv->cur_pgc->cell_playback[cur].block_type
          == BLOCK_TYPE_ANGLE_BLOCK)
        cur += angle;
      next = get_next_cell_for (priv, cur);
    }
  }

  GST_LOG ("Seek to sector %u failed", seek_to);
  return FALSE;

done:
  /* so chapter $chapter and cell $cur contain our sector
   * of interest. Let's go there! */
  GST_LOG ("Seek succeeded, going to chapter %u, cell %u", chapter, cur);

  _seek_chapter (priv, chapter);
  priv->cur_cell = cur;
  priv->next_cell = next;
  priv->new_cell = FALSE;
  priv->cur_pack = seek_to;

  return TRUE;
}

static GstData *
dvdreadsrc_get (GstPad * pad)
{
  gint res;
  DVDReadSrc *dvdreadsrc;
  DVDReadSrcPrivate *priv;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  dvdreadsrc = DVDREADSRC (gst_pad_get_parent (pad));
  priv = dvdreadsrc->priv;
  g_return_val_if_fail (GST_FLAG_IS_SET (dvdreadsrc, DVDREADSRC_OPEN), NULL);

  /* handle vents, if any */
  if (priv->seek_pend) {
    if (priv->flush_pend) {
      priv->flush_pend = FALSE;

      return GST_DATA (gst_event_new (GST_EVENT_FLUSH));
    }

    priv->seek_pend = FALSE;
    if (priv->seek_pend_fmt != GST_FORMAT_UNDEFINED) {
      if (priv->seek_pend_fmt == title_format) {
        _seek_title (priv, priv->title, priv->angle);
      }
      _seek_chapter (priv, priv->chapter);

      priv->seek_pend_fmt = GST_FORMAT_UNDEFINED;
    } else {
      if (!seek_sector (priv, priv->angle)) {
        gst_element_set_eos (GST_ELEMENT (dvdreadsrc));
        return GST_DATA (gst_event_new (GST_EVENT_EOS));
      }
    }

    return GST_DATA (gst_event_new_discontinuous (FALSE,
            GST_FORMAT_BYTES, (gint64) (priv->cur_pack * DVD_VIDEO_LB_LEN),
            GST_FORMAT_UNDEFINED));
  }

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new_and_alloc (1024 * DVD_VIDEO_LB_LEN);

  if (priv->new_seek) {
    _seek_title (priv, priv->title, priv->angle);
    _seek_chapter (priv, priv->chapter);
  }

  /* read it in from the file */
  while ((res = _read (priv, priv->angle, priv->new_seek, buf)) == -3);
  switch (res) {
    case -1:
      GST_ELEMENT_ERROR (dvdreadsrc, RESOURCE, READ, (NULL), (NULL));
      gst_buffer_unref (buf);
      return NULL;
    case -2:
      gst_element_set_eos (GST_ELEMENT (dvdreadsrc));
      gst_buffer_unref (buf);
      return GST_DATA (gst_event_new (GST_EVENT_EOS));
    case 0:
      break;
    default:
      g_assert_not_reached ();
  }

  priv->new_seek = FALSE;

  return GST_DATA (buf);
}

/* open the file, necessary to go to RUNNING state */
static gboolean
dvdreadsrc_open_file (DVDReadSrc * src)
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDREADSRC (src), FALSE);
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, DVDREADSRC_OPEN), FALSE);

  if (_open (src->priv, src->priv->location)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), (NULL));
    return FALSE;
  }
  src->priv->seek_pend_fmt = title_format;
  src->priv->seek_pend = TRUE;

  GST_FLAG_SET (src, DVDREADSRC_OPEN);

  return TRUE;
}

/* close the file */
static void
dvdreadsrc_close_file (DVDReadSrc * src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, DVDREADSRC_OPEN));

  _close (src->priv);

  GST_FLAG_UNSET (src, DVDREADSRC_OPEN);
}

static GstElementStateReturn
dvdreadsrc_change_state (GstElement * element)
{
  DVDReadSrc *dvdreadsrc = DVDREADSRC (element);

  g_return_val_if_fail (GST_IS_DVDREADSRC (element), GST_STATE_FAILURE);

  GST_DEBUG ("gstdvdreadsrc: state pending %d", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!dvdreadsrc_open_file (DVDREADSRC (element)))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      dvdreadsrc->priv->new_cell = TRUE;
      dvdreadsrc->priv->new_seek = TRUE;
      dvdreadsrc->priv->chapter = 0;
      dvdreadsrc->priv->title = 0;
      dvdreadsrc->priv->flush_pend = FALSE;
      dvdreadsrc->priv->seek_pend = FALSE;
      dvdreadsrc->priv->seek_pend_fmt = GST_FORMAT_UNDEFINED;
      break;
    case GST_STATE_READY_TO_NULL:
      dvdreadsrc_close_file (DVDREADSRC (element));
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*
 * URI interface.
 */

static GstURIType
dvdreadsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
dvdreadsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "dvd", NULL };

  return protocols;
}

static const gchar *
dvdreadsrc_uri_get_uri (GstURIHandler * handler)
{
  DVDReadSrc *dvdreadsrc = DVDREADSRC (handler);

  g_free (dvdreadsrc->priv->last_uri);
  dvdreadsrc->priv->last_uri =
      g_strdup_printf ("dvd://%d,%d,%d", dvdreadsrc->priv->title,
      dvdreadsrc->priv->chapter, dvdreadsrc->priv->angle);

  return dvdreadsrc->priv->last_uri;
}

static gboolean
dvdreadsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  DVDReadSrc *dvdreadsrc = DVDREADSRC (handler);
  gboolean ret;
  gchar *protocol = gst_uri_get_protocol (uri);

  ret = (protocol && !strcmp (protocol, "dvd")) ? TRUE : FALSE;
  g_free (protocol);
  protocol = NULL;

  if (!ret)
    return ret;

  /*
   * Parse out the new t/c/a and seek to them
   */
  {
    gchar *location = NULL;
    gchar **strs;
    gchar **strcur;
    gint pos = 0;

    location = gst_uri_get_location (uri);

    if (!location)
      return ret;

    strcur = strs = g_strsplit (location, ",", 0);
    while (strcur && *strcur) {
      gint val;

      if (!sscanf (*strcur, "%d", &val))
        break;

      switch (pos) {
        case 0:
          if (val != dvdreadsrc->priv->title) {
            dvdreadsrc->priv->title = val;
            dvdreadsrc->priv->new_seek = TRUE;
          }
          break;
        case 1:
          if (val != dvdreadsrc->priv->chapter) {
            dvdreadsrc->priv->chapter = val;
            dvdreadsrc->priv->new_seek = TRUE;
          }
          break;
        case 2:
          dvdreadsrc->priv->angle = val;
          break;
      }

      strcur++;
      pos++;
    }

    g_strfreev (strs);
    g_free (location);
  }

  return ret;
}

static void
dvdreadsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = dvdreadsrc_uri_get_type;
  iface->get_protocols = dvdreadsrc_uri_get_protocols;
  iface->get_uri = dvdreadsrc_uri_get_uri;
  iface->set_uri = dvdreadsrc_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dvdreadsrc", GST_RANK_NONE,
          GST_TYPE_DVDREADSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvdreadsrc",
    "Access a DVD with dvdread",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
