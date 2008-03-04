/* GStreamer
 * Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
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

#include "_stdint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

//#include <gst/gst-i18n-plugin.h>
#define _(s) s                  /* FIXME */

#include "dvdnavsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_dvd_nav_src_debug);
#define GST_CAT_DEFAULT (gst_dvd_nav_src_debug)

/* Size of a DVD sector, used for sector-byte format conversions */
#define DVD_SECTOR_SIZE 2048

#define CLOCK_BASE 9LL
#define CLOCK_FREQ CLOCK_BASE * 10000

#define MPEGTIME_TO_GSTTIME(time) (((time) * (GST_MSECOND/10)) / CLOCK_BASE)
#define GSTTIME_TO_MPEGTIME(time) (((time) * CLOCK_BASE) / (GST_MSECOND/10))


/* The maxinum number of audio and SPU streams in a DVD. */
#define GST_DVD_NAV_SRC_MAX_AUDIO_STREAMS 8
#define GST_DVD_NAV_SRC_MAX_SPU_STREAMS 32


/* Interval of time to sleep during pauses. */
#define GST_DVD_NAV_SRC_PAUSE_INTERVAL (GST_SECOND / 30)


static const GstElementDetails gst_dvd_nav_src_details = {
  "DVD Source",
  "Source/File/DVD",
  "Access a DVD with navigation features using libdvdnav",
  "David I. Lehn <dlehn@users.sourceforge.net>",
};

enum
{
  USER_OP_SIGNAL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE
#if 0
      ARG_STREAMINFO,
  ARG_BUTTONINFO,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE,
  ARG_AUDIO_LANGS,
  ARG_AUDIO_LANG,
  ARG_SPU_LANGS,
  ARG_SPU_LANG
#endif
};

static void gst_dvd_nav_src_do_init (GType dvdnavsrc_type);

static gboolean gst_dvd_nav_src_start (GstBaseSrc * basesrc);
static gboolean gst_dvd_nav_src_stop (GstBaseSrc * basesrc);
static GstEvent *
gst_dvd_nav_src_make_dvd_event (GstDvdNavSrc * src,
    const gchar * event_name, const gchar * firstfield, ...)
    G_GNUC_NULL_TERMINATED;
     static void gst_dvd_nav_src_finalize (GObject * object);
     static void gst_dvd_nav_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
     static void gst_dvd_nav_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

     static void gst_dvd_nav_src_push_dvd_nav_packet_event (GstDvdNavSrc * src,
    const pci_t * pci);
     static void gst_dvd_nav_src_push_clut_change_event (GstDvdNavSrc * src,
    const guint32 * clut);
     static GstFlowReturn gst_dvd_nav_src_create (GstPushSrc * pushsrc,
    GstBuffer ** p_buf);
     static gboolean gst_dvd_nav_src_src_event (GstBaseSrc * basesrc,
    GstEvent * event);
     static gboolean gst_dvd_nav_src_query (GstBaseSrc * basesrc,
    GstQuery * query);
     static gboolean gst_dvd_nav_src_do_seek (GstBaseSrc * src, GstSegment * s);
     static gboolean gst_dvd_nav_src_is_open (GstDvdNavSrc * src);

#if 0
     static void gst_dvd_nav_src_set_clock (GstElement * element,
    GstClock * clock);
#endif

#ifndef GST_DISABLE_GST_DEBUG
     static void gst_dvd_nav_src_print_event (GstDvdNavSrc * src,
    guint8 * data, int event, int len);
#else
#define gst_dvd_nav_src_print_event(src, data, event, len) ((void) 0)
#endif /* GST_DISABLE_GST_DEBUG */
     static void gst_dvd_nav_src_update_streaminfo (GstDvdNavSrc * src);
     static void gst_dvd_nav_src_set_domain (GstDvdNavSrc * src);
     static void gst_dvd_nav_src_update_highlight (GstDvdNavSrc * src,
    gboolean force);
     static void gst_dvd_nav_src_user_op (GstDvdNavSrc * src, gint op);

     static void gst_dvd_nav_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
     static gboolean gst_dvd_nav_src_query_position (GstDvdNavSrc * src,
    GstFormat format, gint64 * p_val);
     static gboolean gst_dvd_nav_src_query_duration (GstDvdNavSrc * src,
    GstFormat format, gint64 * p_val);


     static guint gst_dvd_nav_src_signals[LAST_SIGNAL];

     static GstFormat sector_format;
     static GstFormat title_format;
     static GstFormat chapter_format;
     static GstFormat angle_format;

#define DVD_NAV_SRC_CAPS \
  "video/mpeg, mpegversion=(int)1, systemstream=(boolean)true"

     static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DVD_NAV_SRC_CAPS));


GST_BOILERPLATE_FULL (GstDvdNavSrc, gst_dvd_nav_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_dvd_nav_src_do_init);

     static void gst_dvd_nav_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_dvd_nav_src_details);
}

static gboolean
gst_dvd_nav_src_is_seekable (GstBaseSrc * src)
{
  return TRUE;
}


static void
gst_dvd_nav_src_class_init (GstDvdNavSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gst_dvd_nav_src_signals[USER_OP_SIGNAL] =
      g_signal_new ("user-op",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstDvdNavSrcClass, user_op),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  klass->user_op = gst_dvd_nav_src_user_op;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_dvd_nav_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_dvd_nav_src_get_property);

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "DVD device location", NULL, G_PARAM_READWRITE));
#if 0
  g_object_class_install_property (gobject_class, ARG_TITLE,
      g_param_spec_int ("title", "title", "title",
          0, 99, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CHAPTER,
      g_param_spec_int ("chapter", "chapter", "chapter",
          1, 99, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ANGLE,
      g_param_spec_int ("angle", "angle", "angle", 1, 9, 1, G_PARAM_READWRITE));
  /* FIXME: use tags instead of this? */
/*
  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
      g_param_spec_boxed ("streaminfo", "streaminfo", "streaminfo",
          GST_TYPE_CAPS, G_PARAM_READABLE));
*/
  g_object_class_install_property (gobject_class, ARG_BUTTONINFO,
      g_param_spec_boxed ("buttoninfo", "buttoninfo", "buttoninfo",
          GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_AUDIO_LANGS,
      g_param_spec_string ("audio_languages", "audio_languages",
          "Available audio languages", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_AUDIO_LANG,
      g_param_spec_string ("audio_language", "audio_language",
          "Current audio language", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_SPU_LANGS,
      g_param_spec_string ("spu_languages", "spu_languages",
          "Available SPU languages", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_SPU_LANG,
      g_param_spec_string ("spu_language", "spu_language",
          "Current SPU language", NULL, G_PARAM_READABLE));
#endif

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_stop);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_src_event);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_query);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_create);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_dvd_nav_src_do_seek);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_dvd_nav_src_is_seekable);
#if 0
  gstelement_class->set_clock = gst_dvd_nav_src_set_clock;
#endif
}

static gboolean
gst_dvd_nav_src_check_get_range (GstPad * pad)
{
  return FALSE;
}

static void
gst_dvd_nav_src_init (GstDvdNavSrc * src, GstDvdNavSrcClass * klass)
{
  src->device = g_strdup ("/dev/dvd");
  src->last_uri = NULL;

  src->pending_offset = -1;
  src->new_seek = FALSE;
  src->seek_pending = FALSE;
  src->need_flush = FALSE;
  src->use_tmaps = FALSE;
  src->still_frame = FALSE;

  /* Pause mode is initially inactive. */
  src->pause_mode = GST_DVD_NAV_SRC_PAUSE_OFF;

  /* No highlighted button. */
  src->button = 0;

  /* Domain is unknown at the begining. */
  src->domain = GST_DVD_NAV_SRC_DOMAIN_UNKNOWN;

  src->uri_title = 0;
  src->uri_chapter = 1;
  src->uri_angle = 1;
  src->streaminfo = NULL;
  src->buttoninfo = NULL;

  src->audio_phys = -1;
  src->audio_log = -1;
  src->subp_phys = -1;
  src->subp_log = -1;

  /* No current output buffer. */
  src->cur_buf = NULL;

  src->pgc_length = GST_CLOCK_TIME_NONE;
  src->cell_start = 0;
  src->pg_start = 0;

  src->vmg_file = NULL;
  src->vts_attrs = NULL;
  src->vts_file = NULL;
  src->cur_vts = 0;

  /* avoid unnecessary start/stop in gst_base_src_check_get_range() */
  gst_pad_set_checkgetrange_function (GST_BASE_SRC_PAD (src),
      GST_DEBUG_FUNCPTR (gst_dvd_nav_src_check_get_range));
}

static void
gst_dvd_nav_src_finalize (GObject * object)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (object);

  /* If there's a current output buffer, get rid of it. */
  if (src->cur_buf != NULL) {
    gst_buffer_unref (src->cur_buf);
  }

  g_free (src->last_uri);

  if (src->vts_attrs)
    g_array_free (src->vts_attrs, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dvd_nav_src_is_open (GstDvdNavSrc * src)
{
  return GST_OBJECT_FLAG_IS_SET (GST_OBJECT (src), GST_BASE_SRC_STARTED);
}

static void
gst_dvd_nav_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      /* the element must be stopped in order to do this */
      GST_OBJECT_LOCK (src);

      if (!gst_dvd_nav_src_is_open (src)) {
        g_free (src->device);
        if (g_value_get_string (value) == NULL)
          src->device = g_strdup ("/dev/dvd");
        else
          src->device = g_value_dup_string (value);
      } else {
        g_warning ("dvdnavsrc: cannot change device while running");
      }
      GST_OBJECT_UNLOCK (src);
      break;
#if 0
    case ARG_TITLE:
      src->uri_title = g_value_get_int (value);
      break;
    case ARG_CHAPTER:
      src->uri_chapter = g_value_get_int (value);
      break;
    case ARG_ANGLE:
      src->uri_angle = g_value_get_int (value);
      break;
    case ARG_AUDIO_LANG:
      if (gst_dvd_nav_src_is_open (src)) {
        const gchar *code = g_value_get_string (value);

        if (code != NULL) {
          GST_INFO_OBJECT (src, "setting language %s", code);
          if (dvdnav_audio_language_select (src->dvdnav, (char *) code) !=
              DVDNAV_STATUS_OK) {
            GST_ERROR_OBJECT (src, "setting language: %s",
                dvdnav_err_to_string (src->dvdnav));
          }
        }
      }
      break;
    case ARG_SPU_LANG:
      if (gst_dvd_nav_src_is_open (src)) {
        const gchar *code = g_value_get_string (value);

        if (code != NULL) {
          dvdnav_spu_language_select (src->dvdnav, (char *) code);
        }
      }
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dvd_nav_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, src->device);
      break;
#if 0
    case ARG_STREAMINFO:
      g_value_set_boxed (value, src->streaminfo);
      break;
    case ARG_BUTTONINFO:
      g_value_set_boxed (value, src->buttoninfo);
      break;
    case ARG_TITLE:
      g_value_set_int (value, src->uri_title);
      break;
    case ARG_CHAPTER:
      g_value_set_int (value, src->uri_chapter);
      break;
    case ARG_ANGLE:
      g_value_set_int (value, src->uri_angle);
      break;
    case ARG_AUDIO_LANGS:
      if (!gst_dvd_nav_src_is_open (src)) {
        g_value_set_string (value, "");
      } else {
        uint8_t physical, logical;
        uint16_t lang_int;
        gchar langs[DVD_NAV_SRC_MAX_AUDIO_STREAMS * 3];
        gchar *lang_ptr = langs;

        for (physical = 0; physical < DVD_NAV_SRC_MAX_AUDIO_STREAMS; physical++) {
          logical = dvdnav_get_audio_logical_stream (src->dvdnav, physical);
          lang_int = dvdnav_audio_stream_to_lang (src->dvdnav, logical);
          if (lang_int != 0xffff) {
            lang_ptr[0] = (lang_int >> 8) & 0xff;
            lang_ptr[1] = lang_int & 0xff;
            lang_ptr[2] = ' ';
            lang_ptr += 3;
          }
        }

        if (lang_ptr > langs) {
          /* Overwrite the space at the end. */
          lang_ptr[-1] = '\0';
        } else {
          langs[0] = '\0';
        }

        g_value_set_string (value, langs);
      }
      break;
    case ARG_AUDIO_LANG:
      if (!gst_dvd_nav_src_is_open (src)) {
        g_value_set_string (value, "");
      } else {
        uint8_t logical;
        uint16_t lang_int;
        gchar lang[3];

        logical = dvdnav_get_active_audio_stream (src->dvdnav);
        lang_int = dvdnav_audio_stream_to_lang (src->dvdnav, logical);
        if (lang_int != 0xffff) {
          lang[0] = (lang_int >> 8) & 0xff;
          lang[1] = lang_int & 0xff;
          lang[2] = '\0';
          g_value_set_string (value, lang);
        } else {
          g_value_set_string (value, "");
        }
      }
      break;
    case ARG_SPU_LANGS:
      if (!gst_dvd_nav_src_is_open (src)) {
        g_value_set_string (value, "");
      } else {
        uint8_t physical, logical;
        uint16_t lang_int;
        gchar langs[DVD_NAV_SRC_MAX_SPU_STREAMS * 3];
        gchar *lang_ptr = langs;

        for (physical = 0; physical < DVD_NAV_SRC_MAX_SPU_STREAMS; physical++) {
          logical = dvdnav_get_spu_logical_stream (src->dvdnav, physical);
          lang_int = dvdnav_spu_stream_to_lang (src->dvdnav, logical);
          if (lang_int != 0xffff) {
            lang_ptr[0] = (lang_int >> 8) & 0xff;
            lang_ptr[1] = lang_int & 0xff;
            lang_ptr[2] = ' ';
            lang_ptr += 3;
          }
        }

        if (lang_ptr > langs) {
          /* Overwrite the space at the end. */
          lang_ptr[-1] = '\0';
        } else {
          langs[0] = '\0';
        }

        g_value_set_string (value, langs);
      }
      break;
    case ARG_SPU_LANG:
      if (!gst_dvd_nav_src_is_open (src)) {
        g_value_set_string (value, "");
      } else {
        uint8_t logical;
        uint16_t lang_int;
        gchar lang[3];

        logical = dvdnav_get_active_spu_stream (src->dvdnav);
        lang_int = dvdnav_spu_stream_to_lang (src->dvdnav, logical);
        if (lang_int != 0xffff) {
          lang[0] = (lang_int >> 8) & 0xff;
          lang[1] = lang_int & 0xff;
          lang[2] = '\0';
          g_value_set_string (value, lang);
        } else {
          g_value_set_string (value, "");
        }
      }
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#if 0
static void
gst_dvd_nav_src_set_clock (GstElement * element, GstClock * clock)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (element);

  src->clock = clock;
}
#endif

/* update the time map when the title change */
static gint
gst_dvd_nav_src_update_tmaps (GstDvdNavSrc * src)
{
  int32_t title, part;
  title_info_t *info;
  gint title_set_nr, title_ttn;
  gint i;

  /* Reset stuffs */
  src->use_tmaps = FALSE;
  src->title_tmap = NULL;

  /* FIXME: check if the title really changed, by now it's called on every
   * DVDNAV_CELL_CHANGE event.
   */
  if (dvdnav_current_title_info (src->dvdnav, &title,
          &part) != DVDNAV_STATUS_OK) {
    GST_LOG_OBJECT (src, "Failed getting current title informations!");
    return FALSE;
  }

  GST_LOG_OBJECT (src, "title = %d, part = %d", title, part);

  /* load the VTS information for the title set our title is in */

  /* Close old vts file */
  if (src->vts_file) {
    ifoClose (src->vts_file);
    src->vts_file = NULL;
  }

  /* There isn't a timemap for the menus */
  if (title < 1) {
    GST_LOG_OBJECT (src, "Time map not available for menus");
    return FALSE;
  }

  info = &src->tt_srpt->title[title - 1];
  title_set_nr = info->title_set_nr;
  GST_LOG_OBJECT (src, "title_set_nr = %d", title_set_nr);
  title_ttn = info->vts_ttn;
  GST_LOG_OBJECT (src, "title_ttn= %d", title_ttn);

  src->vts_file = ifoOpen (src->dvd, title_set_nr);
  if (src->vts_file == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD")),
        ("ifoOpen() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  src->vts_tmapt = src->vts_file->vts_tmapt;
  src->vts_ptt_srpt = src->vts_file->vts_ptt_srpt;

  if (src->vts_file->vts_tmapt != NULL) {
    i = title_ttn - 1;
    if (i >= 0 && i < src->vts_tmapt->nr_of_tmaps) {
      src->title_tmap = &src->vts_tmapt->tmap[i];
      GST_LOG_OBJECT (src, "Time map table number: %d", i);
    } else {
      src->title_tmap = NULL;
      GST_LOG_OBJECT (src, "Cannot find table number for title %d", title_ttn);
      return FALSE;
    }
  }

  /* Check that the max map entry sector size is >= then the length reported
   * by dvdnav_get_position, as looks like this function doesn't compute 
   * correctly the pos and length values.
   */
  guint32 pos;
  guint32 dvdnav_length;

  if (dvdnav_get_position (src->dvdnav, &pos,
          &dvdnav_length) != DVDNAV_STATUS_OK) {
    GST_WARNING_OBJECT (src,
        "Cannot get stream length by dvdnav, "
        "won't enable tmaps use as we cannot check if they're interaction with dvdnav is right");
    return FALSE;
  } else {
    if (src->title_tmap) {
      int tmap_length;

      tmap_length =
          (src->title_tmap->map_ent[src->title_tmap->
              nr_of_entries] & 0x7fffffff);
      if (tmap_length >= dvdnav_length) {
        GST_LOG_OBJECT (src, "Using time maps for seeking");
      } else {
        GST_WARNING_OBJECT (src,
            "Time maps has max sector (%d) < than length (%d) reported by dvdnav: "
            "tmap problem or dvdnav bug?", dvdnav_length);
      }
    }
  }

  /* Dump all tmaps for title set for debugging */
  if (src->vts_tmapt) {
    gint i, j;

    GST_LOG_OBJECT (src, "nr_of_tmaps = %d", src->vts_tmapt->nr_of_tmaps);
    for (i = 0; i < src->vts_tmapt->nr_of_tmaps; ++i) {
      GST_LOG_OBJECT (src, "======= Table %d ===================", i);
      GST_LOG_OBJECT (src, "Offset relative to VTS_TMAPTI: %d",
          src->vts_tmapt->tmap_offset[i]);
      GST_LOG_OBJECT (src, "Time unit (seconds)          : %d",
          src->vts_tmapt->tmap[i].tmu);
      GST_LOG_OBJECT (src, "Number of entries            : %d",
          src->vts_tmapt->tmap[i].nr_of_entries);
      for (j = 0; j < src->vts_tmapt->tmap[i].nr_of_entries; j++) {
        guint64 time;

        time = src->vts_tmapt->tmap[i].tmu * (j + 1) * GST_SECOND;
        GST_LOG_OBJECT (src, "Time: %" GST_TIME_FORMAT " VOBU "
            "Sector: %d %s", GST_TIME_ARGS (time),
            src->vts_tmapt->tmap[i].map_ent[j] & 0x7fffffff,
            (src->vts_tmapt->tmap[i].map_ent[j] >> 31) ? "discontinuity" : "");
      }
    }
  } else {
    GST_LOG_OBJECT (src, "no vts_tmapt to dump");
    return FALSE;
  }

  src->use_tmaps = TRUE;
  return TRUE;
}

/* Find sector from time using time map if available */
/* FIXME: do interpolation as in gst_dvd_nav_src_get_time_for_sector_tmap */
static gint
gst_dvd_nav_src_get_sector_from_time_tmap (GstDvdNavSrc * src, GstClockTime ts)
{
  gint sector, i;

  if (src->vts_tmapt == NULL || src->vts_tmapt->nr_of_tmaps == 0)
    return -1;

  sector = 0;
  for (i = 0; i < src->title_tmap->nr_of_entries; ++i) {
    GstClockTime entry_time;

    entry_time = src->title_tmap->tmu * (i + 1) * GST_SECOND;
    if (entry_time <= ts) {
      sector = src->title_tmap->map_ent[i] & 0x7fffffff;
    }
    if (entry_time >= ts) {
      return sector;
    }
  }

  if (ts == 0)
    return 0;

  return -1;
}

/* Find time for sector using time map, does interpolation */
static GstClockTime
gst_dvd_nav_src_get_time_for_sector_tmap (GstDvdNavSrc * src, guint sector)
{
  gint i;
  guint prev_sector = 0;
  guint cur_sector = 0;
  GstClockTime target, diff;

  if (src->vts_tmapt == NULL || src->vts_tmapt->nr_of_tmaps == 0) {
    return GST_CLOCK_TIME_NONE;
  }

  for (i = 0; i < src->title_tmap->nr_of_entries; ++i) {
    prev_sector = cur_sector;
    cur_sector = src->title_tmap->map_ent[i] & 0x7fffffff;
    if (sector >= prev_sector && sector <= cur_sector) {
      target = src->title_tmap->tmu * i * GST_SECOND;
      if (i < src->title_tmap->nr_of_entries - 1) {
        diff =
            src->title_tmap->tmu * GST_SECOND * (sector -
            prev_sector) / (cur_sector - prev_sector);
        target += diff;
      }
      return target;
    }
  }

  if (sector == 0)
    return (GstClockTime) 0;

  return GST_CLOCK_TIME_NONE;
}

/* Find time for sector */
static GstClockTime
gst_dvd_nav_src_get_time_for_sector (GstDvdNavSrc * src, guint sector)
{
  GstClockTime time;

  GST_OBJECT_LOCK (src);
  if (src->use_tmaps) {
    time = gst_dvd_nav_src_get_time_for_sector_tmap (src, sector);
    if (time != GST_CLOCK_TIME_NONE) {
      GST_OBJECT_UNLOCK (src);
      return time;
    }
  }
  /* Fallback to average time calculation */
  time = (GstClockTime) ((float) sector / src->sector_length * src->pgc_length);

  GST_OBJECT_UNLOCK (src);
  return time;
}

/* Find sector from time */
static GstClockTime
gst_dvd_nav_src_get_sector_from_time (GstDvdNavSrc * src, GstClockTime time)
{
  gint sector;

  GST_OBJECT_LOCK (src);
  if (src->use_tmaps) {
    sector = gst_dvd_nav_src_get_sector_from_time_tmap (src, time);
    if (sector >= 0) {
      GST_OBJECT_UNLOCK (src);
      return sector;
    }
  }

  /* Fallback to average sector calculation */
  sector = (GstClockTime) ((float) time / src->pgc_length * src->sector_length);
  GST_OBJECT_UNLOCK (src);

  return sector;
}

static gboolean
gst_dvd_nav_src_do_convert_query (GstDvdNavSrc * src, GstQuery * query)
{
  GstFormat src_format, dest_format;
  gboolean ret = FALSE;
  gint64 src_val, dest_val = -1;

  gst_query_parse_convert (query, &src_format, &src_val, &dest_format, NULL);

  if (src_format == dest_format) {
    dest_val = src_val;
    ret = TRUE;
    goto done;
  }

  /* FIXME: handle also chapter format */
  /* Formats to consider: TIME, DEFAULT, BYTES, title, chapter, sector.
   * Note: title and chapter are counted as starting from 0 here, just like
   * in the context of seek events. Another note: DEFAULT format is undefined */

  if (src_format == GST_FORMAT_BYTES) {
    src_format = sector_format;
    src_val /= DVD_SECTOR_SIZE;
  }

  if (src_format == sector_format) {
    /* SECTOR => xyz */
    if (dest_format == GST_FORMAT_TIME && src_val < G_MAXUINT) {
      dest_val = gst_dvd_nav_src_get_time_for_sector (src, src_val);
      ret = (dest_val >= 0);
    } else if (dest_format == GST_FORMAT_BYTES) {
      dest_val = src_val * DVD_SECTOR_SIZE;
      ret = TRUE;
    } else {
      ret = FALSE;
    }
  } else if (src_format == title_format) {
    /* TITLE => xyz */
    if (dest_format == GST_FORMAT_TIME) {
      /* not really true, but we use this to trick the base source into
       * handling seeks in title-format for us (the source won't know that
       * we changed the title in this case) (changing titles should really
       * be done with an interface rather than a seek, but for now we're
       * stuck with this mechanism. Fix in 0.11) */
      dest_val = (GstClockTime) 0;
      ret = TRUE;
    } else {
      ret = FALSE;
    }
  } else if (src_format == GST_FORMAT_TIME) {
    /* TIME => sectors */
    if (dest_format == sector_format || dest_format == GST_FORMAT_BYTES) {
      dest_val = gst_dvd_nav_src_get_sector_from_time (src, src_val);
      ret = (dest_val >= 0);
      /* TIME => bytes */
      if (dest_format == GST_FORMAT_BYTES) {
        dest_val *= DVD_VIDEO_LB_LEN;
      }
    }
  } else {
    ret = FALSE;
  }

done:

  if (ret) {
    gst_query_set_convert (query, src_format, src_val, dest_format, dest_val);
  }

  return ret;
}

/* FIXME: handle also title, chapter, angle format */
static gboolean
gst_dvd_nav_src_do_seek (GstBaseSrc * basesrc, GstSegment * s)
{
  GstDvdNavSrc *src;

  src = GST_DVD_NAV_SRC (basesrc);

  /* On the first call of do_seek in gstbasesrc during gst_base_src_activate_push the dvdnav vm
   * isn't running. Return TRUE as the seek was done.
   */
  if (src->first_seek == TRUE) {
    src->first_seek = FALSE;
    return TRUE;
  }

  GST_DEBUG_OBJECT (src, "Seeking to %s: %12" G_GINT64_FORMAT,
      gst_format_get_name (s->format), s->last_stop);

  if (s->format == sector_format || s->format == GST_FORMAT_BYTES
      || s->format == GST_FORMAT_TIME) {
    gint sector = 0;

    /* Convert in sector format */
    if (s->format == GST_FORMAT_BYTES) {
      sector = s->last_stop / DVD_SECTOR_SIZE;
    } else if (s->format == GST_FORMAT_TIME) {
      sector = gst_dvd_nav_src_get_sector_from_time (src, s->last_stop);
    }

    if (dvdnav_sector_search (src->dvdnav, sector,
            SEEK_SET) != DVDNAV_STATUS_OK) {
      GST_DEBUG_OBJECT (src, "seek to %s %d failed",
          gst_format_get_name (s->format), s->last_stop);
      return FALSE;
    }
    GST_LOG_OBJECT (src, "seek to %s %d ok", gst_format_get_name (s->format),
        s->last_stop);

  } else {
    g_return_val_if_reached (FALSE);
  }
  return TRUE;
}


static gboolean
gst_dvd_nav_src_tca_seek (GstDvdNavSrc * src, gint title, gint chapter,
    gint angle)
{
  int titles, chapters, curangle, angles;

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (src->dvdnav != NULL, FALSE);
//  g_return_val_if_fail (gst_dvd_nav_src_is_open (src), FALSE);

  /* Dont try to seek to track 0 - First Play program chain */
  g_return_val_if_fail (src->title > 0, FALSE);

  GST_INFO_OBJECT (src, "seeking to %d/%d/%d", title, chapter, angle);
  /* Make sure our title number is valid */
  if (dvdnav_get_number_of_titles (src->dvdnav, &titles) != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_get_number_of_titles: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }
  GST_INFO_OBJECT (src, "there are %d titles on this DVD", titles);
  if (title < 1 || title > titles) {
    GST_ERROR_OBJECT (src, "invalid title %d", title);
    return FALSE;
  }

  /* Before we can get the number of chapters (programs) we need to call
   * dvdnav_title_play so that dvdnav_get_number_of_programs knows which title
   * to operate on (also needed to get the number of angles) */
  /* FIXME: This is probably not necessary anymore! */
  if (dvdnav_title_play (src->dvdnav, title) != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_title_play: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }

  /* Make sure the chapter number is valid for this title */
  if (dvdnav_get_number_of_parts (src->dvdnav, title, &chapters)
      != DVDNAV_STATUS_OK) {
    GST_ERROR ("dvdnav_get_number_of_programs: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }
  GST_INFO_OBJECT (src, "there are %d chapters in this title", chapters);
  if (chapter < 0 || chapter > chapters) {
    GST_ERROR_OBJECT (src, "invalid chapter %d", chapter);
    return FALSE;
  }

  /* Make sure the angle number is valid for this title */
  if (dvdnav_get_angle_info (src->dvdnav, &curangle, &angles)
      != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_get_angle_info: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }
  GST_INFO_OBJECT (src, "there are %d angles in this title", angles);
  if (angle < 1 || angle > angles) {
    GST_ERROR_OBJECT (src, "invalid angle %d", angle);
    return FALSE;
  }

  /* We've got enough info, time to open the title set data */
  if (src->chapter == 0) {
    if (dvdnav_title_play (src->dvdnav, title) != DVDNAV_STATUS_OK) {
      GST_ERROR_OBJECT (src, "dvdnav_title_play: %s",
          dvdnav_err_to_string (src->dvdnav));
      return FALSE;
    }
  } else {
    if (dvdnav_part_play (src->dvdnav, title, chapter) != DVDNAV_STATUS_OK) {
      GST_ERROR_OBJECT (src, "dvdnav_part_play: %s",
          dvdnav_err_to_string (src->dvdnav));
      return FALSE;
    }
  }
  if (dvdnav_angle_change (src->dvdnav, angle) != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_angle_change: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }


  return TRUE;
}

static void
gst_dvd_nav_src_update_streaminfo (GstDvdNavSrc * src)
{
/* FIXME: we should really use TAGS for this stuff, shouldn't we? */
#if 0
  GstBaseSrc *bsrc = GST_BASE_SRC (src);
  GstCaps *caps;
  GstStructure *structure;
  gint64 value;

  caps = gst_caps_new_empty ();
  structure = gst_structure_empty_new ("application/x-gst-streaminfo");
  gst_caps_append_structure (caps, structure);

  if (gst_dvd_nav_src_query_duration (bsrc, title_format, &value)) {
    gst_caps_set_simple (caps, "titles", G_TYPE_INT, value, NULL);
  }
  if (gst_dvd_nav_src_query_position (bsrc, title_format, &value)) {
    gst_caps_set_simple (caps, "title", G_TYPE_INT, value, NULL);
  }

  if (gst_dvd_nav_src_query_duration (bsrc, chapter_format, &value)) {
    gst_caps_set_simple (caps, "chapters", G_TYPE_INT, value, NULL);
  }
  if (gst_dvd_nav_src_query_position (bsrc, chapter_format, &value)) {
    gst_caps_set_simple (caps, "chapter", G_TYPE_INT, value, NULL);
  }

  if (gst_dvd_nav_src_query_duration (bsrc, angle_format, &value)) {
    gst_caps_set_simple (caps, "angles", G_TYPE_INT, value, NULL);
  }
  if (gst_dvd_nav_src_query_position (bsrc, angle_format, &value)) {
    gst_caps_set_simple (caps, "angle", G_TYPE_INT, value, NULL);
  }

  gst_caps_replace (&src->streaminfo, caps);
  gst_caps_unref (caps);

  g_object_notify (G_OBJECT (src), "streaminfo");
#endif
}

/*
 * Check for a new DVD domain area, and update the structure if
 * necessary.
 */
static void
gst_dvd_nav_src_set_domain (GstDvdNavSrc * src)
{
  GstDvdNavSrcDomainType domain;

  if (dvdnav_is_domain_fp (src->dvdnav)) {
    domain = GST_DVD_NAV_SRC_DOMAIN_FP;
  } else if (dvdnav_is_domain_vmgm (src->dvdnav)) {
    domain = GST_DVD_NAV_SRC_DOMAIN_VMGM;
  } else if (dvdnav_is_domain_vtsm (src->dvdnav)) {
    domain = GST_DVD_NAV_SRC_DOMAIN_VTSM;
  } else if (dvdnav_is_domain_vts (src->dvdnav)) {
    domain = GST_DVD_NAV_SRC_DOMAIN_VTS;
  } else {
    domain = GST_DVD_NAV_SRC_DOMAIN_UNKNOWN;
  }

  /* FIXME: We may send a signal if we have a new domain. */
  src->domain = domain;
}

/*
 * Check for a new highlighted area, and send an spu highlight event if
 * necessary.
 */
static void
gst_dvd_nav_src_update_highlight (GstDvdNavSrc * src, gboolean force)
{
  int button = 0;
  pci_t *pci;
  dvdnav_highlight_area_t area;
  GstEvent *event;

  if (dvdnav_get_current_highlight (src->dvdnav, &button) != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED, (NULL),
        ("dvdnav_get_current_highlight: %s",
            dvdnav_err_to_string (src->dvdnav)));
    return;
  }

  pci = dvdnav_get_current_nav_pci (src->dvdnav);
  if ((button > pci->hli.hl_gi.btn_ns) || (button < 0)) {
    /* button is out of the range of possible buttons. */
    button = 0;
  }

  if (!pci->hli.hl_gi.hli_ss) {
    /* Not in menu */
    button = 0;
  }

  if (button == 0) {
    if (src->button != 0) {
      src->button = 0;

      gst_pad_push_event (GST_BASE_SRC_PAD (src),
          gst_dvd_nav_src_make_dvd_event (src, "dvd-spu-reset-highlight",
              NULL, NULL));
    }
    return;
  }

  if (dvdnav_get_highlight_area (pci, button, 0, &area) != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED, (NULL),
        ("dvdnav_get_highlight_area: %s", dvdnav_err_to_string (src->dvdnav)));
    return;
  }

  /* Check if we have a new button number, or a new highlight region. */
  if (button != src->button || force ||
      memcmp (&area, &(src->area), sizeof (dvdnav_highlight_area_t)) != 0) {
    memcpy (&(src->area), &area, sizeof (dvdnav_highlight_area_t));

    event = gst_dvd_nav_src_make_dvd_event (src,
        "dvd-spu-highlight",
        "button", G_TYPE_INT, (gint) button,
        "palette", G_TYPE_INT, (gint) area.palette,
        "sx", G_TYPE_INT, (gint) area.sx,
        "sy", G_TYPE_INT, (gint) area.sy,
        "ex", G_TYPE_INT, (gint) area.ex,
        "ey", G_TYPE_INT, (gint) area.ey, NULL);

    if (src->button == 0) {
      /* When setting the button for the first time, take the
         timestamp into account. */
      GST_EVENT_TIMESTAMP (event) = MPEGTIME_TO_GSTTIME (area.pts);
    }

    src->button = button;

    GST_DEBUG ("Sending dvd-spu-highlight for button %d", button);
    int ret = gst_pad_push_event (GST_BASE_SRC_PAD (src), event);

    GST_DEBUG ("End Sending dvd-spu-highlight for button %d, ret: %d", button,
        ret);
  }
}

static void
gst_dvd_nav_src_user_op (GstDvdNavSrc * src, gint op)
{
  pci_t *pci = dvdnav_get_current_nav_pci (src->dvdnav);

  GST_INFO_OBJECT (src, "user operation %d", op);

  /* Magic user_op ids */
  switch (op) {
    case 0:                    /* None */
      break;
    case 1:                    /* Upper */
      if (dvdnav_upper_button_select (src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 2:                    /* Lower */
      if (dvdnav_lower_button_select (src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 3:                    /* Left */
      if (dvdnav_left_button_select (src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 4:                    /* Right */
      if (dvdnav_right_button_select (src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 5:                    /* Activate */
      if (dvdnav_button_activate (src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 6:                    /* GoUp */
      if (dvdnav_go_up (src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 7:                    /* TopPG */
      if (dvdnav_top_pg_search (src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 8:                    /* PrevPG */
      if (dvdnav_prev_pg_search (src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 9:                    /* NextPG */
      if (dvdnav_next_pg_search (src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 10:                   /* Menu - Title */
      if (dvdnav_menu_call (src->dvdnav, DVD_MENU_Title) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 11:                   /* Menu - Root */
      if (dvdnav_menu_call (src->dvdnav, DVD_MENU_Root) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 12:                   /* Menu - Subpicture */
      if (dvdnav_menu_call (src->dvdnav, DVD_MENU_Subpicture)
          != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 13:                   /* Menu - Audio */
      if (dvdnav_menu_call (src->dvdnav, DVD_MENU_Audio) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 14:                   /* Menu - Angle */
      if (dvdnav_menu_call (src->dvdnav, DVD_MENU_Angle) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 15:                   /* Menu - Part */
      if (dvdnav_menu_call (src->dvdnav, DVD_MENU_Part) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 50:                   /* Select button */
    {
      int32_t button;

      dvdnav_get_current_highlight (src->dvdnav, &button);
      if (button == 0) {
        for (button = 1; button <= 36; button++) {
          if (dvdnav_button_select (src->dvdnav, pci, button) ==
              DVDNAV_STATUS_OK) {
            break;
          }
        }
        dvdnav_get_current_highlight (src->dvdnav, &button);
      }
      GST_INFO_OBJECT (src, "Selected button: %d", button);
    }
      break;
  }
  return;

naverr:
  GST_WARNING_OBJECT (src, "user op %d failure: %s",
      op, dvdnav_err_to_string (src->dvdnav));
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
dvdnav_get_event_name (int event)
{
  switch (event) {
    case DVDNAV_BLOCK_OK:
      return "DVDNAV_BLOCK_OK";
    case DVDNAV_NOP:
      return "DVDNAV_NOP";
    case DVDNAV_STILL_FRAME:
      return "DVDNAV_STILL_FRAME";
    case DVDNAV_WAIT:
      return "DVDNAV_WAIT";
    case DVDNAV_SPU_STREAM_CHANGE:
      return "DVDNAV_SPU_STREAM_CHANGE";
    case DVDNAV_AUDIO_STREAM_CHANGE:
      return "DVDNAV_AUDIO_STREAM_CHANGE";
    case DVDNAV_VTS_CHANGE:
      return "DVDNAV_VTS_CHANGE";
    case DVDNAV_CELL_CHANGE:
      return "DVDNAV_CELL_CHANGE";
    case DVDNAV_NAV_PACKET:
      return "DVDNAV_NAV_PACKET";
    case DVDNAV_STOP:
      return "DVDNAV_STOP";
    case DVDNAV_HIGHLIGHT:
      return "DVDNAV_HIGHLIGHT";
    case DVDNAV_SPU_CLUT_CHANGE:
      return "DVDNAV_SPU_CLUT_CHANGE";
    case DVDNAV_HOP_CHANNEL:
      return "DVDNAV_HOP_CHANNEL";
    default:
      break;
  }
  return "UNKNOWN";
}

static const gchar *
dvdnav_get_read_domain_name (dvd_read_domain_t domain)
{
  switch (domain) {
    case DVD_READ_INFO_FILE:
      return "DVD_READ_INFO_FILE";
    case DVD_READ_INFO_BACKUP_FILE:
      return "DVD_READ_INFO_BACKUP_FILE";
    case DVD_READ_MENU_VOBS:
      return "DVD_READ_MENU_VOBS";
    case DVD_READ_TITLE_VOBS:
      return "DVD_READ_TITLE_VOBS";
    default:
      break;
  }
  return "UNKNOWN";
}

static void
gst_dvd_nav_src_print_event (GstDvdNavSrc * src, guint8 * data, int event,
    int len)
{
  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_DVD_NAV_SRC (src));

  GST_DEBUG_OBJECT (src, "dvdnavsrc (%p): event: %s", src,
      dvdnav_get_event_name (event));
  switch (event) {
    case DVDNAV_BLOCK_OK:
      break;
    case DVDNAV_NOP:
      break;
    case DVDNAV_STILL_FRAME:{
      dvdnav_still_event_t *event = (dvdnav_still_event_t *) data;

      GST_DEBUG_OBJECT (src, "  still frame: %d seconds", event->length);
      break;
    }
    case DVDNAV_WAIT:
      break;
    case DVDNAV_SPU_STREAM_CHANGE:{
      dvdnav_spu_stream_change_event_t *event;

      event = (dvdnav_spu_stream_change_event_t *) data;
      GST_DEBUG_OBJECT (src, "  physical_wide: %d", event->physical_wide);
      GST_DEBUG_OBJECT (src, "  physical_letterbox: %d",
          event->physical_letterbox);
      GST_DEBUG_OBJECT (src, "  physical_pan_scan: %d",
          event->physical_pan_scan);
      GST_DEBUG_OBJECT (src, "  logical: %d", event->logical);
      break;
    }
    case DVDNAV_AUDIO_STREAM_CHANGE:{
      dvdnav_audio_stream_change_event_t *event =
          (dvdnav_audio_stream_change_event_t *) data;
      GST_DEBUG_OBJECT (src, "  physical: %d", event->physical);
      GST_DEBUG_OBJECT (src, "  logical: %d", event->logical);
      break;
    }
    case DVDNAV_VTS_CHANGE:{
      dvdnav_vts_change_event_t *event = (dvdnav_vts_change_event_t *) data;

      GST_DEBUG_OBJECT (src, "  old_vtsN: %d", event->old_vtsN);
      GST_DEBUG_OBJECT (src, "  old_domain: %s",
          dvdnav_get_read_domain_name (event->old_domain));
      GST_DEBUG_OBJECT (src, "  new_vtsN: %d", event->new_vtsN);
      GST_DEBUG_OBJECT (src, "  new_domain: %s",
          dvdnav_get_read_domain_name (event->new_domain));
      break;
    }
    case DVDNAV_CELL_CHANGE:
      break;
    case DVDNAV_NAV_PACKET:{
      /* FIXME: Print something relevant here. */
      break;
    }
    case DVDNAV_STOP:
      break;
    case DVDNAV_HIGHLIGHT:{
      dvdnav_highlight_event_t *event = (dvdnav_highlight_event_t *) data;

      GST_DEBUG_OBJECT (src, "  display: %s",
          event->display == 0 ?
          "hide" : (event->display == 1 ? "show" : "unknown"));
      if (event->display == 1) {
        GST_DEBUG_OBJECT (src, "  palette: %08x", event->palette);
        GST_DEBUG_OBJECT (src, "  coords (%u, %u) - (%u, %u)",
            event->sx, event->sy, event->ex, event->ey);
        GST_DEBUG_OBJECT (src, "  pts: %u", event->pts);
        GST_DEBUG_OBJECT (src, "  button: %u", event->buttonN);
      }
      break;
    }
    case DVDNAV_SPU_CLUT_CHANGE:
      break;
    case DVDNAV_HOP_CHANNEL:
      break;
    default:{
      GST_DEBUG_OBJECT (src, "  event id: %d", event);
      break;
    }
  }
}
#endif /* GST_DISABLE_GST_DEBUG */

static GstEvent *
gst_dvd_nav_src_make_dvd_event (GstDvdNavSrc * src, const gchar * event_name,
    const gchar * firstfield, ...)
{
  GstEvent *event;
  GstStructure *structure;
  va_list varargs;

  g_return_val_if_fail (event_name != NULL, NULL);

  /* Create a structure with the given fields. */
  va_start (varargs, firstfield);
  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, event_name, NULL);
  gst_structure_set_valist (structure, firstfield, varargs);
  va_end (varargs);

  /* Create the DVD event and put the structure into it. */
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure);

  GST_LOG_OBJECT (src, "created event %" GST_PTR_FORMAT, event);

  return event;
}

static void
gst_dvd_nav_src_structure_set_uint64 (GstStructure * structure,
    const gchar * name, guint64 val)
{
  GValue gvalue = { 0, };

  g_value_init (&gvalue, G_TYPE_UINT64);
  g_value_set_uint64 (&gvalue, val);
  gst_structure_set_value (structure, name, &gvalue);
}

static void
gst_dvd_nav_src_push_dvd_nav_packet_event (GstDvdNavSrc * src,
    const pci_t * pci)
{
  GstEvent *event;
  GstStructure *structure;

  /* Build the event structure. */
  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-nav-packet", NULL);

  gst_dvd_nav_src_structure_set_uint64 (structure, "start_ptm",
      MPEGTIME_TO_GSTTIME (pci->pci_gi.vobu_s_ptm));
  gst_dvd_nav_src_structure_set_uint64 (structure, "end_ptm",
      MPEGTIME_TO_GSTTIME (pci->pci_gi.vobu_e_ptm));
  gst_dvd_nav_src_structure_set_uint64 (structure, "cell_start",
      src->cell_start);
  gst_dvd_nav_src_structure_set_uint64 (structure, "pg_start", src->pg_start);

  /* Create the DVD event and put the structure into it. */
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure);

  GST_LOG_OBJECT (src, "pushing nav packet event %" GST_PTR_FORMAT, event);

  gst_pad_push_event (GST_BASE_SRC_PAD (src), event);
}

static void
gst_dvd_nav_src_push_clut_change_event (GstDvdNavSrc * src,
    const guint32 * clut)
{
  GstEvent *event;
  GstStructure *structure;
  gchar name[16];
  int i;

  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-spu-clut-change", NULL);

  /* Create a separate field for each value in the table. */
  for (i = 0; i < 16; i++) {
    sprintf (name, "clut%02d", i);
    gst_structure_set (structure, name, G_TYPE_INT, (int) clut[i], NULL);
  }

  /* Create the DVD event and put the structure into it. */
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure);

  GST_LOG_OBJECT (src, "pushing clut change event %" GST_PTR_FORMAT, event);

  gst_pad_push_event (GST_BASE_SRC_PAD (src), event);
}

/* Use libdvdread to read and cache info from the IFO file about
 * streams in each VTS
 */
static gboolean
read_vts_info (GstDvdNavSrc * src)
{
  dvd_reader_t *dvdi;
  ifo_handle_t *ifo;
  gint i;
  gint n_vts;

  if (src->vts_attrs) {
    g_array_free (src->vts_attrs, TRUE);
    src->vts_attrs = NULL;
  }

  dvdi = DVDOpen (src->device);
  if (!dvdi)
    return FALSE;

  ifo = ifoOpen (dvdi, 0);
  if (!ifo) {
    GST_ERROR ("Can't open VMG info");
    return FALSE;
  }
  n_vts = ifo->vts_atrt->nr_of_vtss;
  memcpy (&src->vmgm_attr, ifo->vmgi_mat, sizeof (vmgi_mat_t));
  ifoClose (ifo);

  GST_DEBUG ("Reading IFO info for %d VTSs", n_vts);
  src->vts_attrs = g_array_sized_new (FALSE, TRUE, sizeof (vtsi_mat_t),
      n_vts + 1);
  if (!src->vts_attrs)
    return FALSE;
  g_array_set_size (src->vts_attrs, n_vts + 1);

  for (i = 1; i <= n_vts; i++) {
    ifo = ifoOpen (dvdi, i);
    if (!ifo) {
      GST_ERROR ("Can't open VTS %d", i);
      return FALSE;
    }

    GST_DEBUG ("VTS %d, Menu has %d audio %d subpictures. "
        "Title has %d and %d", i,
        ifo->vtsi_mat->nr_of_vtsm_audio_streams,
        ifo->vtsi_mat->nr_of_vtsm_subp_streams,
        ifo->vtsi_mat->nr_of_vts_audio_streams,
        ifo->vtsi_mat->nr_of_vts_subp_streams);

    memcpy (&g_array_index (src->vts_attrs, vtsi_mat_t, i),
        ifo->vtsi_mat, sizeof (vtsi_mat_t));

    ifoClose (ifo);
  }
  DVDClose (dvdi);
  return TRUE;
}

static gboolean
gst_dvd_nav_src_push_titlelang_event (GstDvdNavSrc * src)
{
  vtsi_mat_t *vts_attr;
  audio_attr_t *a_attrs;
  subp_attr_t *s_attrs;
  gint n_audio, n_subp;
  GstStructure *s;
  GstEvent *e;
  gint i;
  gchar lang_code[3] = { '\0', '\0', '\0' };
  gchar *t;

  if (src->vts_attrs == NULL || src->cur_vts >= src->vts_attrs->len) {
    if (src->vts_attrs)
      GST_ERROR ("No stream info for VTS %d (have %d)", src->cur_vts,
          src->vts_attrs->len);
    else
      GST_ERROR ("No stream info");
    return FALSE;
  }

  if (src->domain == GST_DVD_NAV_SRC_DOMAIN_VMGM) {
    vts_attr = NULL;
    a_attrs = &src->vmgm_attr.vmgm_audio_attr;
    n_audio = MIN (1, src->vmgm_attr.nr_of_vmgm_audio_streams);
    s_attrs = &src->vmgm_attr.vmgm_subp_attr;
    n_subp = MIN (1, src->vmgm_attr.nr_of_vmgm_subp_streams);
  } else {
    vts_attr = &g_array_index (src->vts_attrs, vtsi_mat_t, src->cur_vts);
    a_attrs = vts_attr->vts_audio_attr;
    n_audio = vts_attr->nr_of_vts_audio_streams;
    s_attrs = vts_attr->vts_subp_attr;
    n_subp = vts_attr->nr_of_vts_subp_streams;
  }

  /* build event */

  s = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-lang-codes", NULL);
  e = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

  /* audio */
  for (i = 0; i < n_audio; i++) {
    const audio_attr_t *a = a_attrs + i;

    t = g_strdup_printf ("audio-%d-format", i);
    gst_structure_set (s, t, G_TYPE_INT, (int) a->audio_format, NULL);
    g_free (t);

    GST_DEBUG ("Audio stream %d is format %d", i, (int) a->audio_format);

    if (a->lang_type) {
      t = g_strdup_printf ("audio-%d-language", i);
      lang_code[0] = (a->lang_code >> 8) & 0xff;
      lang_code[1] = a->lang_code & 0xff;
      gst_structure_set (s, t, G_TYPE_STRING, lang_code, NULL);
      g_free (t);

      GST_DEBUG ("Audio stream %d is language %s", i, lang_code);
    } else
      GST_DEBUG ("Audio stream %d - no language", i, lang_code);
  }

  /* subtitle */
  for (i = 0; i < n_subp; i++) {
    const subp_attr_t *u = s_attrs + i;

    t = g_strdup_printf ("subtitle-%d-language", i);
    if (u->type) {
      lang_code[0] = (u->lang_code >> 8) & 0xff;
      lang_code[1] = u->lang_code & 0xff;
      gst_structure_set (s, t, G_TYPE_STRING, lang_code, NULL);
    } else {
      gst_structure_set (s, t, G_TYPE_STRING, "MENU", NULL);
    }
    g_free (t);

    GST_DEBUG ("Subtitle stream %d is language %s", i,
        lang_code[0] ? lang_code : "NONE");
  }

  gst_pad_push_event (GST_BASE_SRC_PAD (src), e);
  return TRUE;
}

static GstFlowReturn
gst_dvd_nav_src_process_next_block (GstDvdNavSrc * src, GstBuffer ** p_buf)
{
  dvdnav_status_t navret;
  guint8 *data;
  gint event, len;

  if (src->cur_buf == NULL)
    src->cur_buf = gst_buffer_new_and_alloc (DVD_VIDEO_LB_LEN);

  data = GST_BUFFER_DATA (src->cur_buf);
  len = GST_BUFFER_SIZE (src->cur_buf);

  navret = dvdnav_get_next_block (src->dvdnav, data, &event, &len);
  if (navret != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("dvdnav_get_next_block: %s", dvdnav_err_to_string (src->dvdnav)));
    return GST_FLOW_ERROR;
  }

  /* If the next block is not DVDNAV_STILL_FRAME we can seek */
  if (event != DVDNAV_STILL_FRAME) {
    src->still_frame = FALSE;
  }

  switch (event) {
    case DVDNAV_NOP:
      break;
    case DVDNAV_BLOCK_OK:{
      *p_buf = src->cur_buf;
      src->cur_buf = NULL;
      break;
    }
    case DVDNAV_STILL_FRAME:{
      src->still_frame = TRUE;
      dvdnav_still_event_t *info = (dvdnav_still_event_t *) data;

      gst_dvd_nav_src_print_event (src, data, event, len);

      if (src->pause_mode == GST_DVD_NAV_SRC_PAUSE_OFF) {
        gst_dvd_nav_src_print_event (src, data, event, len);

        /* We just saw a still frame. Start a pause now. */
        if (info->length == 0xff) {
          GST_INFO_OBJECT (src, "starting unlimited pause");
          src->pause_mode = GST_DVD_NAV_SRC_PAUSE_UNLIMITED;
          src->pause_remain = -1;
        } else {
          src->pause_mode = GST_DVD_NAV_SRC_PAUSE_LIMITED;
          src->pause_remain = info->length * GST_SECOND;
          gint64 val;

          gst_dvd_nav_src_query_position (src, GST_FORMAT_TIME, &val);
          GST_INFO_OBJECT (src,
              "starting limited pause: %d seconds at %" GST_TIME_FORMAT,
              info->length, GST_TIME_ARGS (val));
        }

        /* For the moment, send the first empty event to let
           everyone know that we are displaying a still frame.
           Subsequent calls to this function will take care of
           the rest of the pause. */
        GST_DEBUG_OBJECT (src, "sending still frame event");
        gst_pad_push_event (GST_BASE_SRC_PAD (src),
            gst_dvd_nav_src_make_dvd_event (src, "dvd-spu-still-frame",
                NULL, NULL));
        break;
      }

      if (src->pause_mode == GST_DVD_NAV_SRC_PAUSE_UNLIMITED ||
          src->pause_remain > 0) {

        GST_DEBUG_OBJECT (src, "sleeping %d during still frame",
            GST_DVD_NAV_SRC_PAUSE_INTERVAL);
        GstClock *system_clock = gst_system_clock_obtain ();
        GstClockTime cur_time = gst_clock_get_internal_time (system_clock);
        GstClockID id =
            gst_clock_new_single_shot_id (system_clock,
            cur_time + GST_DVD_NAV_SRC_PAUSE_INTERVAL);
        gst_clock_id_wait (id, NULL);
        gst_clock_id_unref (id);


        if (src->pause_mode == GST_DVD_NAV_SRC_PAUSE_LIMITED) {
          if (src->pause_remain < GST_DVD_NAV_SRC_PAUSE_INTERVAL)
            src->pause_remain = 0;
          else
            src->pause_remain -= GST_DVD_NAV_SRC_PAUSE_INTERVAL;
        }

        /* If pause isn't finished, discont because time isn't actually
         * advancing */
        if (src->pause_mode == GST_DVD_NAV_SRC_PAUSE_UNLIMITED ||
            src->pause_remain > 0)

          break;
      } else {
        /* We reached the end of the pause. */
        src->pause_mode = GST_DVD_NAV_SRC_PAUSE_OFF;
        if (dvdnav_still_skip (src->dvdnav) != DVDNAV_STATUS_OK) {
          GST_WARNING_OBJECT (src, "dvdnav_still_skip failed: %s",
              dvdnav_err_to_string (src->dvdnav));
        }
        /* Flush to ensure that data after the wait is played correctly */
        src->need_flush = TRUE;
      }
      break;
    }

    case DVDNAV_WAIT:{
/* was commented out already: */
#if 0
      GstEvent *event;

      /* FIXME: We should really wait here until the fifos are
         empty, but I have no idea how to do that.  In the mean time,
         just clean the wait state. */
      GST_INFO_OBJECT (src, "sending wait");

      gst_element_wait (GST_ELEMENT (src),
          gst_element_get_time (GST_ELEMENT (src)) + 1.5 * GST_SECOND);
#endif
      if (dvdnav_wait_skip (src->dvdnav) != DVDNAV_STATUS_OK) {
        GST_WARNING_OBJECT (src, "dvdnav_wait_skip failed: %s",
            dvdnav_err_to_string (src->dvdnav));
      }

/* was commented out already: */
#if 0
      event = gst_event_new_filler ();
      send_data = GST_DATA (event);
#endif
      break;
    }
    case DVDNAV_STOP:{
      GST_INFO_OBJECT (src, "stop - EOS");
      return GST_FLOW_UNEXPECTED;
    }
    case DVDNAV_CELL_CHANGE:{
      dvdnav_cell_change_event_t *event = (dvdnav_cell_change_event_t *) data;

      src->pgc_length = MPEGTIME_TO_GSTTIME (event->pgc_length);
      src->cell_start = MPEGTIME_TO_GSTTIME (event->cell_start);
      src->pg_start = MPEGTIME_TO_GSTTIME (event->pg_start);

      GST_LOG_OBJECT (src, "New cell. PGC Len %" GST_TIME_FORMAT
          " cell_start %" GST_TIME_FORMAT ", pg_start %" GST_TIME_FORMAT,
          GST_TIME_ARGS (src->pgc_length),
          GST_TIME_ARGS (src->cell_start), GST_TIME_ARGS (src->pg_start));

      /* Update the current length in sectors */
      guint32 pos;

      if (dvdnav_get_position (src->dvdnav, &pos,
              &src->sector_length) != DVDNAV_STATUS_OK)
        GST_WARNING_OBJECT (src,
            "Cannot get new stream length after DVDNAV_CELL_CHANGE");

      /* Update the time maps */
      gst_dvd_nav_src_update_tmaps (src);

      gst_dvd_nav_src_update_streaminfo (src);

      break;
    }
    case DVDNAV_NAV_PACKET:{
      pci_t *pci = dvdnav_get_current_nav_pci (src->dvdnav);

      /* Check for forced buttons. */
      if (pci->hli.hl_gi.hli_ss == 1) {
        GST_LOG_OBJECT (src, "menu ahead");
        if (pci->hli.hl_gi.fosl_btnn > 0) {
          GST_DEBUG_OBJECT (src, "forced button");
          dvdnav_button_select (src->dvdnav, pci, pci->hli.hl_gi.fosl_btnn);
        }
      }

      gst_dvd_nav_src_update_highlight (src, FALSE);

      /* Send a dvd nav packet event. */
      gst_dvd_nav_src_push_dvd_nav_packet_event (src, pci);

      /* The NAV packet contains also data, so send it */
      *p_buf = src->cur_buf;
      src->cur_buf = NULL;
      break;
    }
    case DVDNAV_SPU_CLUT_CHANGE:{
      /* Does this work on 64-bit/big-endian machines? (tpm) -
       * Should do! (jan) */
      gst_dvd_nav_src_push_clut_change_event (src, (const guint32 *) data);
      break;
    }
    case DVDNAV_VTS_CHANGE:{
      dvdnav_vts_change_event_t *event = (dvdnav_vts_change_event_t *) data;

      gst_dvd_nav_src_set_domain (src);

      if (src->domain == GST_DVD_NAV_SRC_DOMAIN_VMGM)
        src->cur_vts = 0;
      else
        src->cur_vts = event->new_vtsN;

/* FIXME */
#if 0
      gst_pad_push_event (GST_BASE_SRC_PAD (src),
          gst_event_new_discontinuous (FALSE, GST_FORMAT_UNDEFINED));
#endif

      if (src->domain == GST_DVD_NAV_SRC_DOMAIN_VTSM ||
          src->domain == GST_DVD_NAV_SRC_DOMAIN_VTS ||
          src->domain == GST_DVD_NAV_SRC_DOMAIN_VMGM) {
        if (!gst_dvd_nav_src_push_titlelang_event (src)) {
          GST_ELEMENT_ERROR (src, LIBRARY, FAILED,
              (_("Invalid title information on DVD.")), GST_ERROR_SYSTEM);
        }
      }

      gst_pad_push_event (GST_BASE_SRC_PAD (src),
          gst_dvd_nav_src_make_dvd_event (src, "dvd-vts-change",
              "domain", G_TYPE_INT, (gint) src->domain, NULL));
      break;
    }
    case DVDNAV_AUDIO_STREAM_CHANGE:{
      dvdnav_audio_stream_change_event_t *info;
      gint phys;

      info = (dvdnav_audio_stream_change_event_t *) data;
      phys = info->physical;

      gst_dvd_nav_src_print_event (src, data, event, len);

      if (phys < 0 || phys > GST_DVD_NAV_SRC_MAX_AUDIO_STREAMS) {
        phys = -1;
      }

      if (phys == src->audio_phys &&
          dvdnav_get_active_audio_stream (src->dvdnav) == src->audio_log) {
        /* Audio state hasn't changed. */
        break;
      }

      src->audio_phys = phys;
      src->audio_log = dvdnav_get_active_audio_stream (src->dvdnav);
      gst_pad_push_event (GST_BASE_SRC_PAD (src),
          gst_dvd_nav_src_make_dvd_event (src, "dvd-audio-stream-change",
              "physical", G_TYPE_INT, (gint) src->audio_phys,
              "logical", G_TYPE_INT, (gint) src->audio_log, NULL));
      break;
    }
    case DVDNAV_SPU_STREAM_CHANGE:{
      dvdnav_spu_stream_change_event_t *info;
      gint phys;

      info = (dvdnav_spu_stream_change_event_t *) data;
      /* FIXME: Which type of physical stream to use here should
         be configurable through a property. We take widescreen
         for the moment. */
      phys = info->physical_wide;

      gst_dvd_nav_src_print_event (src, data, event, len);

      if (phys < 0 || phys > GST_DVD_NAV_SRC_MAX_SPU_STREAMS) {
        phys = -1;
      }

      if (phys == src->subp_phys &&
          dvdnav_get_active_spu_stream (src->dvdnav) == src->subp_log) {
        /* Subpicture state hasn't changed. */
        break;
      }

      src->subp_phys = phys;
      src->subp_log = dvdnav_get_active_spu_stream (src->dvdnav);

      gst_pad_push_event (GST_BASE_SRC_PAD (src),
          gst_dvd_nav_src_make_dvd_event (src, "dvd-spu-stream-change",
              "physical", G_TYPE_INT, (gint) phys,
              "logical", G_TYPE_INT,
              (gint) dvdnav_get_active_spu_stream (src->dvdnav), NULL));
      break;
    }
    case DVDNAV_HIGHLIGHT:{
      gst_dvd_nav_src_print_event (src, data, event, len);

      gst_dvd_nav_src_update_highlight (src, FALSE);
      break;
    }
    case DVDNAV_HOP_CHANNEL:{
      gst_dvd_nav_src_print_event (src, data, event, len);

      src->button = 0;
      src->pause_mode = GST_DVD_NAV_SRC_PAUSE_OFF;
      /* If seeking the flush is already sent by the basesrc parent class */
      if (!src->seek_pending)
        src->need_flush = TRUE;
      /* was commented out already: */
      /* send_data = GST_DATA (gst_event_new_flush ()); */
      break;
    }
    default:
      g_error ("dvdnavsrc: Unknown dvdnav event %d", event);
      break;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_dvd_nav_src_create (GstPushSrc * pushsrc, GstBuffer ** p_buf)
{
  GstFlowReturn ret;
  GstDvdNavSrc *src;

  src = GST_DVD_NAV_SRC (pushsrc);

  if (src->new_seek) {
    gst_dvd_nav_src_tca_seek (src, src->title, src->chapter, src->angle);
    src->new_seek = FALSE;
  }

  *p_buf = NULL;

  /* Loop processing blocks until there is data to send. */
  do {
    if (src->need_flush) {
      if (src->pause_mode != GST_DVD_NAV_SRC_PAUSE_OFF) {
        if (dvdnav_still_skip (src->dvdnav) != DVDNAV_STATUS_OK) {
          GST_ELEMENT_ERROR (src, LIBRARY, FAILED, (NULL),
              ("dvdnav_still_skip: %s", dvdnav_err_to_string (src->dvdnav)));
          return GST_FLOW_ERROR;
        }
        src->pause_mode = GST_DVD_NAV_SRC_PAUSE_OFF;
      }

      src->need_flush = FALSE;
      GST_INFO_OBJECT (src, "sending flush");
      gst_pad_push_event (GST_BASE_SRC_PAD (src), gst_event_new_flush_start ());
      gst_pad_push_event (GST_BASE_SRC_PAD (src), gst_event_new_flush_stop ());

      gst_dvd_nav_src_update_highlight (src, TRUE);
    }

    ret = gst_dvd_nav_src_process_next_block (src, p_buf);
  }
  while (ret == GST_FLOW_OK && *p_buf == NULL);

  src->seek_pending = FALSE;
  return ret;
}

static gboolean
gst_dvd_nav_src_start (GstBaseSrc * basesrc)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (basesrc);
  GstTagList *tags;
  const char *title_str;

  if (!read_vts_info (src)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not read title information for DVD.")), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Load the video manager to find out the information about the titles */
  src->dvd = DVDOpen (src->device);
  if (!src->dvd)
    return FALSE;

  GST_DEBUG_OBJECT (src, "Loading VMG info");

  if (!(src->vmg_file = ifoOpen (src->dvd, 0))) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD")),
        ("ifoOpen() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  src->tt_srpt = src->vmg_file->tt_srpt;

  if (dvdnav_open (&src->dvdnav, src->device) != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        (_("Failed to open DVD device '%s'."), src->device));
    return FALSE;
  }

  if (dvdnav_set_PGC_positioning_flag (src->dvdnav, 1) != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED,
        (_("Failed to set PGC based seeking.")), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Read the first block before seeking to force a libdvdnav internal
   * call to vm_start, otherwise it ignores our seek position.
   * This happens because vm_start sets the domain to the first-play (FP)
   * domain, overriding any other title that has been set.
   * Track/chapter setting used to work, but libdvdnav has delayed the call
   * to vm_start from _open, to _get_block.
   * FIXME: But, doing it this way has problems too, as there is no way to
   * get back to the FP domain.
   * Maybe we could title==0 to mean FP domain, and not do this read & seek.
   * If title subsequently gets set to 0, we would need to dvdnav_close
   * followed by dvdnav_open to get back to the FP domain.
   * Since we dont currently support seeking by setting the title/chapter/angle
   * after opening, we'll forget about close/open for now, and just do the
   * title==0 thing.
   */

  src->title = src->uri_title;
  src->chapter = src->uri_chapter;
  src->angle = src->uri_angle;

  if (src->title > 0) {
    dvdnav_status_t ret;
    guint8 buf[DVD_SECTOR_SIZE];
    gint event, buflen = sizeof (buf);

    ret = dvdnav_get_next_block (src->dvdnav, buf, &event, &buflen);
    if (ret != DVDNAV_STATUS_OK) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("dvdnav_get_next_block: %s", dvdnav_err_to_string (src->dvdnav)));
      return FALSE;
    }


    gst_dvd_nav_src_print_event (src, buf, event, buflen);

    if (!gst_dvd_nav_src_tca_seek (src, src->title, src->chapter, src->angle)) {
      return FALSE;
    }
  }

  tags = gst_tag_list_new ();
  if (dvdnav_get_title_string (src->dvdnav, &title_str) == DVDNAV_STATUS_OK) {
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_TITLE, title_str, NULL);
  }

  if (tags && gst_structure_n_fields ((GstStructure *) tags) > 0) {
    gst_element_found_tags (GST_ELEMENT (src), tags);
  }

  src->streaminfo = NULL;
  src->use_tmaps = FALSE;
  src->first_seek = TRUE;

  return TRUE;
}

static gboolean
gst_dvd_nav_src_stop (GstBaseSrc * basesrc)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (basesrc);

  if (src->dvdnav && dvdnav_close (src->dvdnav) != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, (NULL),
        ("dvdnav_close failed: %s", dvdnav_err_to_string (src->dvdnav)));
    return FALSE;
  }

  /* Close old vts file if opened */
  if (src->vts_file)
    ifoClose (src->vts_file);

  /* Close the video manager */
  ifoClose (src->vmg_file);

  DVDClose (src->dvd);

  return TRUE;
}

static gboolean
gst_dvd_nav_src_handle_navigation_event (GstDvdNavSrc * src, GstEvent * event)
{
  const GstStructure *structure;
  const gchar *event_type;

  structure = gst_event_get_structure (event);
  event_type = gst_structure_get_string (structure, "event");

  if (strcmp (event_type, "key-press") == 0) {
    const gchar *key = gst_structure_get_string (structure, "key");

    if (key == NULL)
      return TRUE;

    if (g_str_equal (key, "Return")) {
      dvdnav_button_activate (src->dvdnav,
          dvdnav_get_current_nav_pci (src->dvdnav));
    } else if (g_str_equal (key, "Left")) {
      dvdnav_left_button_select (src->dvdnav,
          dvdnav_get_current_nav_pci (src->dvdnav));
    } else if (g_str_equal (key, "Right")) {
      dvdnav_right_button_select (src->dvdnav,
          dvdnav_get_current_nav_pci (src->dvdnav));
    } else if (g_str_equal (key, "Up")) {
      dvdnav_upper_button_select (src->dvdnav,
          dvdnav_get_current_nav_pci (src->dvdnav));
    } else if (g_str_equal (key, "Down")) {
      dvdnav_lower_button_select (src->dvdnav,
          dvdnav_get_current_nav_pci (src->dvdnav));
    } else if (g_str_equal (key, "m")) {
      dvdnav_menu_call (src->dvdnav, DVD_MENU_Escape);
    } else if (g_str_equal (key, "t")) {
      dvdnav_menu_call (src->dvdnav, DVD_MENU_Title);
    } else if (g_str_equal (key, "r")) {
      dvdnav_menu_call (src->dvdnav, DVD_MENU_Root);
    } else if (g_str_equal (key, "comma")) {
      gint title = 0;
      gint part = 0;

      if (dvdnav_current_title_info (src->dvdnav, &title, &part) && title > 0
          && part > 1) {
        dvdnav_part_play (src->dvdnav, title, part - 1);
      }
    } else if (g_str_equal (key, "period")) {
      gint title = 0;
      gint part = 0;

      if (dvdnav_current_title_info (src->dvdnav, &title, &part) && title > 0) {
        dvdnav_part_play (src->dvdnav, title, part + 1);
      }
    }

    GST_DEBUG ("dvdnavsrc got a keypress: %s", key);
  } else if (strcmp (event_type, "mouse-move") == 0) {
    gdouble x, y;

    gst_structure_get_double (structure, "pointer_x", &x);
    gst_structure_get_double (structure, "pointer_y", &y);

    dvdnav_mouse_select (src->dvdnav,
        dvdnav_get_current_nav_pci (src->dvdnav), (int) x, (int) y);

    gst_dvd_nav_src_update_highlight (src, FALSE);
  } else if (strcmp (event_type, "mouse-button-release") == 0) {
    gdouble x, y;

    gst_structure_get_double (structure, "pointer_x", &x);
    gst_structure_get_double (structure, "pointer_y", &y);
    GST_DEBUG_OBJECT (src, "Got click at %g, %g", x, y);

    dvdnav_mouse_activate (src->dvdnav,
        dvdnav_get_current_nav_pci (src->dvdnav), (int) x, (int) y);
  }

  return TRUE;
}

/* FIXME: handle also angle format */
static gboolean
gst_dvd_nav_src_handle_seek_event (GstDvdNavSrc * src, GstEvent * event)
{
  GstSeekFlags flags;
  GstSeekType cur_type, end_type;
  gint64 new_off;
  GstFormat format;
  gdouble rate;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &new_off,
      &end_type, NULL);

  /* Cannot seek during another seek */
  if (src->seek_pending) {
    GST_WARNING_OBJECT (src, "Cannot seek during another seek");
    return FALSE;
  }

  /* If we are in a still frame we cannot seek */
  if (src->still_frame) {
    GST_WARNING_OBJECT (src, "Cannot seek during a still frame");
    return FALSE;
  }

  if (rate <= 0.0) {
    GST_DEBUG_OBJECT (src, "cannot do backwards playback yet");
    return FALSE;
  }

  if ((flags & GST_SEEK_FLAG_SEGMENT) != 0) {
    GST_DEBUG_OBJECT (src, "segment seek not supported");
    return FALSE;
  }

  if ((flags & GST_SEEK_FLAG_FLUSH) == 0) {
    GST_DEBUG_OBJECT (src, "can only do flushing seeks at the moment");
    return FALSE;
  }

  if (end_type != GST_SEEK_TYPE_NONE) {
    GST_DEBUG_OBJECT (src, "end seek type not supported");
    return FALSE;
  }

  if (cur_type != GST_SEEK_TYPE_SET) {
    GST_DEBUG_OBJECT (src, "only SEEK_TYPE_SET is supported");
    return FALSE;
  }

  if (format != chapter_format && format != title_format &&
      format != GST_FORMAT_BYTES && format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (src, "unsupported seek format %d (%s)", format,
        gst_format_get_name (format));
    return FALSE;
  }

  src->seek_pending = TRUE;

  return GST_BASE_SRC_CLASS (parent_class)->event (GST_BASE_SRC (src), event);
}

/* FIXME: please remove me, I use disk space without a reason. thanks. */
#if 0
static gboolean
gst_dvd_nav_src_handle_seek_event (GstDvdNavSrc * src, GstEvent * event)
{
  gint64 offset;
  gint format;
  int titles, title, new_title;
  int parts, part, new_part;
  int angles, angle, new_angle;
  int origin;
  guint32 curoff, len;

  format = GST_EVENT_SEEK_FORMAT (event);
  offset = GST_EVENT_SEEK_OFFSET (event);

  /* Disallow seek before the DVD VM has started or if we haven't finished a 
   * previous seek yet */
  if (dvdnav_get_position (src->dvdnav, &curoff, &len) != DVDNAV_STATUS_OK
      || src->seek_pending)
    goto error;

  GST_DEBUG_OBJECT (src, "Seeking to %lld, format %d, method %d\n", offset,
      format, GST_EVENT_SEEK_METHOD (event));

  switch (format) {
    case GST_FORMAT_BYTES:
      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          origin = SEEK_SET;
          src->pending_offset = offset;
          break;
        case GST_SEEK_METHOD_CUR:
          origin = SEEK_CUR;
          src->pending_offset = curoff + offset;
          break;
        case GST_SEEK_METHOD_END:
          origin = SEEK_END;
          src->pending_offset = len + offset;
          break;
        default:
          goto error;
      }
      if (dvdnav_sector_search (src->dvdnav, (offset / DVD_SECTOR_SIZE),
              origin) != DVDNAV_STATUS_OK) {
        goto error;
      }
      break;
    default:
      if (format == sector_format) {
        switch (GST_EVENT_SEEK_METHOD (event)) {
          case GST_SEEK_METHOD_SET:
            origin = SEEK_SET;
            break;
          case GST_SEEK_METHOD_CUR:
            origin = SEEK_CUR;
            break;
          case GST_SEEK_METHOD_END:
            origin = SEEK_END;
            break;
          default:
            goto error;
        }
        if (dvdnav_sector_search (src->dvdnav, offset, origin) !=
            DVDNAV_STATUS_OK) {
          goto error;
        }
      } else if (format == title_format) {
        if (dvdnav_current_title_info (src->dvdnav, &title, &part) !=
            DVDNAV_STATUS_OK) {
          goto error;
        }
        switch (GST_EVENT_SEEK_METHOD (event)) {
          case GST_SEEK_METHOD_SET:
            new_title = offset;
            break;
          case GST_SEEK_METHOD_CUR:
            new_title = title + offset;
            break;
          case GST_SEEK_METHOD_END:
            if (dvdnav_get_number_of_titles (src->dvdnav, &titles) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
            new_title = titles + offset;
            break;
          default:
            goto error;
        }
        if (dvdnav_title_play (src->dvdnav, new_title) != DVDNAV_STATUS_OK) {
          goto error;
        }
      } else if (format == chapter_format) {
        if (dvdnav_current_title_info (src->dvdnav, &title, &part) !=
            DVDNAV_STATUS_OK) {
          goto error;
        }
        switch (GST_EVENT_SEEK_METHOD (event)) {
          case GST_SEEK_METHOD_SET:
            new_part = offset;
            break;
          case GST_SEEK_METHOD_CUR:
            new_part = part + offset;
            break;
          case GST_SEEK_METHOD_END:
            if (dvdnav_get_number_of_titles (src->dvdnav, &parts) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
            new_part = parts + offset;
            break;
          default:
            goto error;
        }
        /*if (dvdnav_part_search(src->dvdnav, new_part) != */
        if (dvdnav_part_play (src->dvdnav, title, new_part) != DVDNAV_STATUS_OK) {
          goto error;
        }
      } else if (format == angle_format) {
        if (dvdnav_get_angle_info (src->dvdnav, &angle, &angles) !=
            DVDNAV_STATUS_OK) {
          goto error;
        }
        switch (GST_EVENT_SEEK_METHOD (event)) {
          case GST_SEEK_METHOD_SET:
            new_angle = offset;
            break;
          case GST_SEEK_METHOD_CUR:
            new_angle = angle + offset;
            break;
          case GST_SEEK_METHOD_END:
            new_angle = angles + offset;
            break;
          default:
            goto error;
        }
        if (dvdnav_angle_change (src->dvdnav, new_angle) != DVDNAV_STATUS_OK) {
          goto error;
        }
      } else {
        goto error;
      }
  }
  src->did_seek = TRUE;
  src->seek_pending = TRUE;
  src->need_flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
  return TRUE;

error:

  GST_DEBUG_OBJECT (src, "seek failed");
  return FALSE;
}
#endif

static gboolean
gst_dvd_nav_src_src_event (GstBaseSrc * basesrc, GstEvent * event)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (basesrc);
  gboolean res;

  GST_LOG_OBJECT (src, "handling %s event", GST_EVENT_TYPE_NAME (event));

  if (!gst_dvd_nav_src_is_open (src)) {
    GST_DEBUG_OBJECT (src, "device not open yet");
    return FALSE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_dvd_nav_src_handle_seek_event (src, event);
      break;
    case GST_EVENT_NAVIGATION:
      res = gst_dvd_nav_src_handle_navigation_event (src, event);
      break;
    case GST_EVENT_FLUSH_START:        /* FIXME: hmm? */
      src->need_flush = TRUE;
      /* fall through */
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->event (basesrc, event);
      break;
  }

  return res;
}

static gboolean
gst_dvd_nav_src_query_position (GstDvdNavSrc * src, GstFormat format,
    gint64 * p_val)
{
  guint32 pos, len;
  gint32 title, angle, ch, x;

  *p_val = -1;

  if (format == sector_format) {
    if (dvdnav_get_position (src->dvdnav, &pos, &len) == DVDNAV_STATUS_OK)
      *p_val = pos;
  } else if (format == GST_FORMAT_BYTES) {
    if (dvdnav_get_position (src->dvdnav, &pos, &len) == DVDNAV_STATUS_OK)
      *p_val = (gint64) pos *DVD_SECTOR_SIZE;
  } else if (format == GST_FORMAT_TIME) {
    if (dvdnav_get_position (src->dvdnav, &pos, &len) == DVDNAV_STATUS_OK)
      *p_val = (gint64) gst_dvd_nav_src_get_time_for_sector (src, pos);
    /* libdvnav from svn at at least 20070503 provide this new function
     * dvdnav_get_current_time, I used it just to test that the values
     * provided by the timemap were rights but I won't use it as the values
     * it return changes only every ~0.5 seconds or less depending on how
     * much sectors are contained in a cell.
     *p_val = dvdnav_get_current_time(src->dvdnav) * GST_SECOND / 90000;
     */
  } else if (format == title_format) {
    if (dvdnav_current_title_info (src->dvdnav, &title, &x) == DVDNAV_STATUS_OK)
      *p_val = title;
  } else if (format == chapter_format) {
    if (dvdnav_current_title_info (src->dvdnav, &x, &ch) == DVDNAV_STATUS_OK)
      *p_val = ch;
  } else if (format == angle_format) {
    if (dvdnav_get_angle_info (src->dvdnav, &angle, &x) == DVDNAV_STATUS_OK)
      *p_val = angle;
  }

  return (*p_val != -1);
}

static gboolean
gst_dvd_nav_src_query_duration (GstDvdNavSrc * src, GstFormat format,
    gint64 * p_val)
{
  guint32 pos, len;
  gint32 titles, t, angles, parts, x;

  *p_val = -1;

  if (format == GST_FORMAT_TIME) {
    if (src->pgc_length != GST_CLOCK_TIME_NONE)
      *p_val = src->pgc_length;
  } else if (format == sector_format) {
    if (dvdnav_get_position (src->dvdnav, &pos, &len) == DVDNAV_STATUS_OK)
      *p_val = len;
  } else if (format == GST_FORMAT_BYTES) {
    if (dvdnav_get_position (src->dvdnav, &pos, &len) == DVDNAV_STATUS_OK)
      *p_val = (gint64) len *DVD_SECTOR_SIZE;
  } else if (format == title_format) {
    if (dvdnav_get_number_of_titles (src->dvdnav, &titles) == DVDNAV_STATUS_OK)
      *p_val = titles;
  } else if (format == chapter_format) {
    if (dvdnav_current_title_info (src->dvdnav, &t, &x) == DVDNAV_STATUS_OK &&
        dvdnav_get_number_of_parts (src->dvdnav, t, &parts) == DVDNAV_STATUS_OK)
      *p_val = parts;
  } else if (format == angle_format) {
    if (dvdnav_get_angle_info (src->dvdnav, &x, &angles) == DVDNAV_STATUS_OK)
      *p_val = angles;
  }

  return (*p_val != -1);
}

static gboolean
gst_dvd_nav_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (basesrc);
  GstFormat format;
  gboolean res;
  gint64 val;

  if (!gst_dvd_nav_src_is_open (src)) {
    GST_DEBUG_OBJECT (src, "query failed: device not open yet");
    return FALSE;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      gst_query_parse_duration (query, &format, NULL);
      res = gst_dvd_nav_src_query_duration (src, format, &val);
      if (res) {
        gst_query_set_duration (query, format, val);
      }
      break;
    }
    case GST_QUERY_POSITION:{
      gst_query_parse_position (query, &format, NULL);
      res = gst_dvd_nav_src_query_position (src, format, &val);
      if (res) {
        gst_query_set_position (query, format, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:{
      res = gst_dvd_nav_src_do_convert_query (src, query);
      break;
    }
    default:{
      res = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
      break;
    }
  }

  return res;
}

/* URI interface */

static guint
gst_dvd_nav_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_dvd_nav_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "dvd", "dvdnav", NULL };

  return protocols;
}

static const gchar *
gst_dvd_nav_src_uri_get_uri (GstURIHandler * handler)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (handler);

  g_free (src->last_uri);
  src->last_uri = g_strdup_printf ("dvd://%d,%d,%d", src->uri_title,
      src->uri_chapter, src->uri_angle);

  return src->last_uri;
}

static gboolean
gst_dvd_nav_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstDvdNavSrc *src = GST_DVD_NAV_SRC (handler);
  gboolean ret;
  gchar *protocol = gst_uri_get_protocol (uri);

  ret = (protocol &&
      (!strcmp (protocol, "dvdnav") ||
          !strcmp (protocol, "dvd"))) ? TRUE : FALSE;
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
          if (val != src->uri_title) {
            src->uri_title = val;
            src->new_seek = TRUE;
          }
          break;
        case 1:
          if (val != src->uri_chapter) {
            src->uri_chapter = val;
            src->new_seek = TRUE;
          }
          break;
        case 2:
          src->uri_angle = val;
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
gst_dvd_nav_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_dvd_nav_src_uri_get_type;
  iface->get_protocols = gst_dvd_nav_src_uri_get_protocols;
  iface->get_uri = gst_dvd_nav_src_uri_get_uri;
  iface->set_uri = gst_dvd_nav_src_uri_set_uri;
}

static void
gst_dvd_nav_src_do_init (GType dvdnavsrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_dvd_nav_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (dvdnavsrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  title_format = gst_format_register ("title", "DVD title");
  angle_format = gst_format_register ("angle", "DVD angle");
  sector_format = gst_format_register ("sector", "DVD sector");
  chapter_format = gst_format_register ("chapter", "DVD chapter");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dvdnavsrc", GST_RANK_PRIMARY,
          /* GST_RANK_PRIMARY + 1, */ GST_TYPE_DVD_NAV_SRC)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_dvd_nav_src_debug, "dvdnavsrc", 0,
      "DVD navigation element based on libdvdnav");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvdnav",
    "Access a DVD with navigation features using libdvdnav",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
