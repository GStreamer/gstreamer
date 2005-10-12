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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <gst/gst-i18n-plugin.h>
#include <gst/gst.h>

#include <dvdnav/dvdnav.h>
#include <dvdnav/nav_print.h>


GST_DEBUG_CATEGORY_STATIC (dvdnavsrc_debug);
#define GST_CAT_DEFAULT (dvdnavsrc_debug)

/* Size of a DVD sector, used for sector-byte format conversions */
#define DVD_SECTOR_SIZE 2048

#define CLOCK_BASE 9LL
#define CLOCK_FREQ CLOCK_BASE * 10000

#define MPEGTIME_TO_GSTTIME(time) (((time) * (GST_MSECOND/10)) / CLOCK_BASE)
#define GSTTIME_TO_MPEGTIME(time) (((time) * CLOCK_BASE) / (GST_MSECOND/10))

/* Call a dvdnav function and, it it fails, report an error an execute
   the code in the 'action' parameter. */
#define DVDNAV_RAWCALL(func, params, elem, action) \
  if (func params != DVDNAV_STATUS_OK) { \
    GST_ELEMENT_ERROR (elem, LIBRARY, FAILED, \
                       (_("Error invoking \"%s\": %s."), \
                        #func, dvdnav_err_to_string ((elem)->dvdnav)), \
                       GST_ERROR_SYSTEM); \
    action \
  }

/* Call a dvdnav function and, it it fails, report an error and return
   from the current procedure. */
#define DVDNAV_CALL(func, params, elem) \
  DVDNAV_RAWCALL (func, params, elem, return;)

/* Call a dvdnav function and, it it fails, report an error and return
   from the current procedure with the value 'retval'. */
#define DVDNAV_CALLVAL(func, params, elem, retval) \
  DVDNAV_RAWCALL (func, params, elem, return (retval);)


/* The maxinum number of audio and SPU streams in a DVD. */
#define DVDNAVSRC_MAX_AUDIO_STREAMS 8
#define DVDNAVSRC_MAX_SPU_STREAMS 32

#define GST_TYPE_DVDNAVSRC \
  (dvdnavsrc_get_type())
#define DVDNAVSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVDNAVSRC,DVDNavSrc))
#define DVDNAVSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVDNAVSRC,DVDNavSrcClass))
#define GST_IS_DVDNAVSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVDNAVSRC))
#define GST_IS_DVDNAVSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVDNAVSRC))

typedef struct _DVDNavSrc DVDNavSrc;
typedef struct _DVDNavSrcClass DVDNavSrcClass;

/* The pause modes to handle still frames. */
typedef enum
{
  DVDNAVSRC_PAUSE_OFF,          /* No pause active. */
  DVDNAVSRC_PAUSE_LIMITED,      /* A time limited pause is active. */
  DVDNAVSRC_PAUSE_UNLIMITED     /* An time unlimited pause is active. */
}
DVDNavSrcPauseMode;

/* Interval of time to sleep during pauses. */
#define DVDNAVSRC_PAUSE_INTERVAL (GST_SECOND / 20)

/* The DVD domain types. */
typedef enum
{
  DVDNAVSRC_DOMAIN_UNKNOWN,     /* Unknown domain.  */
  DVDNAVSRC_DOMAIN_FP,          /* First Play domain. */
  DVDNAVSRC_DOMAIN_VMGM,        /* Video Management Menu domain */
  DVDNAVSRC_DOMAIN_VTSM,        /* Video Title Menu domain. */
  DVDNAVSRC_DOMAIN_VTS          /* Video Title domain. */
}
DVDNavSrcDomainType;

struct _DVDNavSrc
{
  GstElement element;

  /* Pads */
  GstPad *srcpad;
  GstCaps *streaminfo;

  /* Location */
  gchar *location;

  gboolean did_seek;
  gboolean need_flush;
  gboolean need_newmedia;

  /* Timing */
  GstClock *clock;              /* The clock for this element. */

  /* Pause handling */
  DVDNavSrcPauseMode pause_mode;        /* The current pause mode. */
  GstClockTime pause_end;       /* The clock time for the end of the
                                   pause. */

  /* Highligh handling */
  int button;                   /* The currently highlighted button
                                   number (0 if no highlight). */
  dvdnav_highlight_area_t area; /* The area corresponding to the
                                   currently highlighted button. */

  /* State handling */
  DVDNavSrcDomainType domain;   /* The current DVD domain. */

  int title, chapter, angle;

  int audio_phys, audio_log;    /* The current audio streams. */
  int subp_phys, subp_log;      /* The current subpicture streams. */

  dvdnav_t *dvdnav;             /* The libdvdnav handle. */

  GstCaps *buttoninfo;

  GstBuffer *cur_buf;           /* Current output buffer. See
                                   dvdnavsrc_get. */
};

struct _DVDNavSrcClass
{
  GstElementClass parent_class;

  void (*user_op) (DVDNavSrc * src, int op);
};

/* elementfactory information */
GstElementDetails dvdnavsrc_details = {
  "DVD Source",
  "Source/File/DVD",
  "Access a DVD with navigation features using libdvdnav",
  "David I. Lehn <dlehn@users.sourceforge.net>",
};


/* DVDNavSrc signals and  args */
enum
{
  USER_OP_SIGNAL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_DEVICE,
  ARG_STREAMINFO,
  ARG_BUTTONINFO,
  ARG_TITLE_STRING,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE,
  ARG_AUDIO_LANGS,
  ARG_AUDIO_LANG,
  ARG_SPU_LANGS,
  ARG_SPU_LANG
};

typedef enum
{
  DVDNAVSRC_OPEN = GST_ELEMENT_FLAG_LAST,

  DVDNAVSRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2
}
DVDNavSrcFlags;


GType dvdnavsrc_get_type (void);
static void dvdnavsrc_base_init (gpointer g_class);
static void dvdnavsrc_class_init (DVDNavSrcClass * klass);
static void dvdnavsrc_init (DVDNavSrc * dvdnavsrc);
static void dvdnavsrc_finalize (GObject * object);

static void dvdnavsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void dvdnavsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void dvdnavsrc_set_clock (GstElement * element, GstClock * clock);

static GstEvent *dvdnavsrc_make_dvd_event
    (DVDNavSrc * src, const gchar * event_name, const gchar * firstfield, ...);
static GstEvent *dvdnavsrc_make_dvd_nav_packet_event
    (DVDNavSrc * src, const pci_t * pci);
static GstEvent *dvdnavsrc_make_clut_change_event
    (DVDNavSrc * src, const guint * clut);

static void dvdnavsrc_loop (GstElement * element);
static gboolean dvdnavsrc_event (GstPad * pad, GstEvent * event);
static const GstEventMask *dvdnavsrc_get_event_mask (GstPad * pad);
static const GstFormat *dvdnavsrc_get_formats (GstPad * pad);
static gboolean dvdnavsrc_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);
static const GstQueryType *dvdnavsrc_get_query_types (GstPad * pad);
static gboolean dvdnavsrc_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static gboolean dvdnavsrc_close (DVDNavSrc * src);
static gboolean dvdnavsrc_open (DVDNavSrc * src);
static gboolean dvdnavsrc_is_open (DVDNavSrc * src);

#ifndef GST_DISABLE_GST_DEBUG
static void dvdnavsrc_print_event (DVDNavSrc * src,
    guint8 * data, int event, int len);
#else
#define dvdnavsrc_print_event(src, data, event, len) ((void) 0)
#endif /* GST_DISABLE_GST_DEBUG */
static void dvdnavsrc_update_streaminfo (DVDNavSrc * src);
static void dvdnavsrc_set_domain (DVDNavSrc * src);
static void dvdnavsrc_update_highlight (DVDNavSrc * src);
static void dvdnavsrc_user_op (DVDNavSrc * src, int op);
static GstStateChangeReturn dvdnavsrc_change_state (GstElement * element,
    GstStateChange transition);

static void dvdnavsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);

static GstElementClass *parent_class = NULL;
static guint dvdnavsrc_signals[LAST_SIGNAL] = { 0 };

static GstFormat sector_format;
static GstFormat title_format;
static GstFormat chapter_format;
static GstFormat angle_format;

GType
dvdnavsrc_get_type (void)
{
  static GType dvdnavsrc_type = 0;

  if (!dvdnavsrc_type) {
    static const GTypeInfo dvdnavsrc_info = {
      sizeof (DVDNavSrcClass),
      dvdnavsrc_base_init,
      NULL,
      (GClassInitFunc) dvdnavsrc_class_init,
      NULL,
      NULL,
      sizeof (DVDNavSrc),
      0,
      (GInstanceInitFunc) dvdnavsrc_init,
    };
    static const GInterfaceInfo urihandler_info = {
      dvdnavsrc_uri_handler_init,
      NULL,
      NULL
    };

    dvdnavsrc_type = g_type_register_static (GST_TYPE_ELEMENT,
        "DVDNavSrc", &dvdnavsrc_info, 0);
    g_type_add_interface_static (dvdnavsrc_type,
        GST_TYPE_URI_HANDLER, &urihandler_info);

    sector_format = gst_format_register ("sector", "DVD sector");
    title_format = gst_format_register ("title", "DVD title");
    chapter_format = gst_format_register ("chapter", "DVD chapter");
    angle_format = gst_format_register ("angle", "DVD angle");

    GST_DEBUG_CATEGORY_INIT (dvdnavsrc_debug, "dvdnavsrc", 0,
        "DVD navigation element");
  }
  return dvdnavsrc_type;
}

static void
dvdnavsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &dvdnavsrc_details);
}

static void
dvdnavsrc_class_init (DVDNavSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  dvdnavsrc_signals[USER_OP_SIGNAL] =
      g_signal_new ("user-op",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (DVDNavSrcClass, user_op),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  gobject_class->finalize = dvdnavsrc_finalize;

  klass->user_op = dvdnavsrc_user_op;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location",
          "DVD device location (deprecated; use device)",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "DVD device location", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_TITLE_STRING,
      g_param_spec_string ("title_string", "title string", "DVD title string",
          NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_TITLE,
      g_param_spec_int ("title", "title", "title",
          0, 99, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CHAPTER,
      g_param_spec_int ("chapter", "chapter", "chapter",
          1, 99, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ANGLE,
      g_param_spec_int ("angle", "angle", "angle", 1, 9, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
      g_param_spec_boxed ("streaminfo", "streaminfo", "streaminfo",
          GST_TYPE_CAPS, G_PARAM_READABLE));
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

  gobject_class->set_property = GST_DEBUG_FUNCPTR (dvdnavsrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (dvdnavsrc_get_property);

  gstelement_class->change_state = dvdnavsrc_change_state;
  gstelement_class->set_clock = dvdnavsrc_set_clock;
}

static void
dvdnavsrc_init (DVDNavSrc * src)
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);

  gst_element_set_loop_function (GST_ELEMENT (src), dvdnavsrc_loop);

  gst_pad_set_event_function (src->srcpad, dvdnavsrc_event);
  gst_pad_set_event_mask_function (src->srcpad, dvdnavsrc_get_event_mask);
  gst_pad_set_convert_function (src->srcpad, dvdnavsrc_convert);
  gst_pad_set_query_function (src->srcpad, dvdnavsrc_query);
  gst_pad_set_query_type_function (src->srcpad, dvdnavsrc_get_query_types);
  gst_pad_set_formats_function (src->srcpad, dvdnavsrc_get_formats);

  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

  src->location = g_strdup ("/dev/dvd");

  src->did_seek = FALSE;
  src->need_flush = FALSE;
  src->need_newmedia = TRUE;

  /* Pause mode is initially inactive. */
  src->pause_mode = DVDNAVSRC_PAUSE_OFF;

  /* No highlighted button. */
  src->button = 0;

  /* Domain is unknown at the begining. */
  src->domain = DVDNAVSRC_DOMAIN_UNKNOWN;

  src->title = 0;
  src->chapter = 1;
  src->angle = 1;
  src->streaminfo = NULL;
  src->buttoninfo = NULL;

  src->audio_phys = -1;
  src->audio_log = -1;
  src->subp_phys = -1;
  src->subp_log = -1;

  /* No current output buffer. */
  src->cur_buf = NULL;
}

static void
dvdnavsrc_finalize (GObject * object)
{
  DVDNavSrc *src = DVDNAVSRC (object);

  /* If there's a current output buffer, get rid of it. */
  if (src->cur_buf != NULL) {
    gst_buffer_unref (src->cur_buf);
  }
}

static gboolean
dvdnavsrc_is_open (DVDNavSrc * src)
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC (src), FALSE);

  return GST_OBJECT_FLAG_IS_SET (src, DVDNAVSRC_OPEN);
}

static void
dvdnavsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  DVDNavSrc *src;

  g_return_if_fail (GST_IS_DVDNAVSRC (object));

  src = DVDNAVSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
    case ARG_DEVICE:
      /* the element must be stopped in order to do this */
      /*g_return_if_fail(!GST_OBJECT_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      g_free (src->location);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        src->location = g_strdup ("/dev/dvd");
      /* otherwise set the new filename */
      else
        src->location = g_strdup (g_value_get_string (value));
      break;
    case ARG_TITLE:
      src->title = g_value_get_int (value);
      src->did_seek = TRUE;
      break;
    case ARG_CHAPTER:
      src->chapter = g_value_get_int (value);
      src->did_seek = TRUE;
      break;
    case ARG_ANGLE:
      src->angle = g_value_get_int (value);
      break;
    case ARG_AUDIO_LANG:
      if (dvdnavsrc_is_open (src)) {
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
      if (dvdnavsrc_is_open (src)) {
        const gchar *code = g_value_get_string (value);

        if (code != NULL) {
          dvdnav_spu_language_select (src->dvdnav, (char *) code);
        }
      }
      break;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
dvdnavsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  DVDNavSrc *src;
  const char *title_string;

  g_return_if_fail (GST_IS_DVDNAVSRC (object));

  src = DVDNAVSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
    case ARG_DEVICE:
      g_value_set_string (value, src->location);
      break;
    case ARG_STREAMINFO:
      g_value_set_boxed (value, src->streaminfo);
      break;
    case ARG_BUTTONINFO:
      g_value_set_boxed (value, src->buttoninfo);
      break;
    case ARG_TITLE_STRING:
      if (!dvdnavsrc_is_open (src)) {
        g_value_set_string (value, "");
      } else if (dvdnav_get_title_string (src->dvdnav, &title_string) !=
          DVDNAV_STATUS_OK) {
        g_value_set_string (value, "UNKNOWN");
      } else {
        g_value_set_string (value, title_string);
      }
      break;
    case ARG_TITLE:
      g_value_set_int (value, src->title);
      break;
    case ARG_CHAPTER:
      g_value_set_int (value, src->chapter);
      break;
    case ARG_ANGLE:
      g_value_set_int (value, src->angle);
      break;
    case ARG_AUDIO_LANGS:
      if (!dvdnavsrc_is_open (src)) {
        g_value_set_string (value, "");
      } else {
        uint8_t physical, logical;
        uint16_t lang_int;
        gchar langs[DVDNAVSRC_MAX_AUDIO_STREAMS * 3];
        gchar *lang_ptr = langs;

        for (physical = 0; physical < DVDNAVSRC_MAX_AUDIO_STREAMS; physical++) {
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
      if (!dvdnavsrc_is_open (src)) {
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
      if (!dvdnavsrc_is_open (src)) {
        g_value_set_string (value, "");
      } else {
        uint8_t physical, logical;
        uint16_t lang_int;
        gchar langs[DVDNAVSRC_MAX_SPU_STREAMS * 3];
        gchar *lang_ptr = langs;

        for (physical = 0; physical < DVDNAVSRC_MAX_SPU_STREAMS; physical++) {
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
      if (!dvdnavsrc_is_open (src)) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
dvdnavsrc_set_clock (GstElement * element, GstClock * clock)
{
  DVDNavSrc *src = DVDNAVSRC (element);

  src->clock = clock;
}

static gboolean
dvdnavsrc_tca_seek (DVDNavSrc * src, int title, int chapter, int angle)
{
  int titles, programs, curangle, angles;

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (src->dvdnav != NULL, FALSE);
  g_return_val_if_fail (dvdnavsrc_is_open (src), FALSE);

  /* Dont try to seek to track 0 - First Play program chain */
  g_return_val_if_fail (src->title > 0, FALSE);

  GST_INFO_OBJECT (src, "seeking to %d/%d/%d", title, chapter, angle);
  /*
   * Make sure our title number is valid.
   */
  if (dvdnav_get_number_of_titles (src->dvdnav, &titles) != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_get_number_of_titles: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }
  GST_INFO_OBJECT (src, "there are %d titles on this DVD", titles);
  if (title < 1 || title > titles) {
    GST_ERROR_OBJECT (src, "invalid title %d", title);
    dvdnavsrc_close (src);
    return FALSE;
  }

  /*
   * Before we can get the number of chapters (programs) we need to call
   * dvdnav_title_play so that dvdnav_get_number_of_programs knows which title
   * to operate on (also needed to get the number of angles)
   */
  /* FIXME: This is probably not necessary anymore! */
  if (dvdnav_title_play (src->dvdnav, title) != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_title_play: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }

  /*
   * Make sure the chapter number is valid for this title.
   */
  if (dvdnav_get_number_of_titles (src->dvdnav, &programs)
      != DVDNAV_STATUS_OK) {
    GST_ERROR ("dvdnav_get_number_of_programs: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }
  GST_INFO_OBJECT (src, "there are %d chapters in this title", programs);
  if (chapter < 0 || chapter > programs) {
    GST_ERROR_OBJECT (src, "invalid chapter %d", chapter);
    dvdnavsrc_close (src);
    return FALSE;
  }

  /*
   * Make sure the angle number is valid for this title.
   */
  if (dvdnav_get_angle_info (src->dvdnav, &curangle, &angles)
      != DVDNAV_STATUS_OK) {
    GST_ERROR_OBJECT (src, "dvdnav_get_angle_info: %s",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }
  GST_INFO_OBJECT (src, "there are %d angles in this title", angles);
  if (angle < 1 || angle > angles) {
    GST_ERROR_OBJECT (src, "invalid angle %d", angle);
    dvdnavsrc_close (src);
    return FALSE;
  }

  /*
   * We've got enough info, time to open the title set data.
   */
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

  src->did_seek = TRUE;

  return TRUE;
}

static void
dvdnavsrc_update_streaminfo (DVDNavSrc * src)
{
  GstCaps *caps;
  GstStructure *structure;
  gint64 value;

  caps = gst_caps_new_empty ();
  structure = gst_structure_empty_new ("application/x-gst-streaminfo");
  gst_caps_append_structure (caps, structure);

  if (dvdnavsrc_query (src->srcpad, GST_QUERY_TOTAL, &title_format, &value)) {
    gst_caps_set_simple (caps, "titles", G_TYPE_INT, value, NULL);
  }
  if (dvdnavsrc_query (src->srcpad, GST_QUERY_POSITION, &title_format, &value)) {
    gst_caps_set_simple (caps, "title", G_TYPE_INT, value, NULL);
  }

  if (dvdnavsrc_query (src->srcpad, GST_QUERY_TOTAL, &chapter_format, &value)) {
    gst_caps_set_simple (caps, "chapters", G_TYPE_INT, value, NULL);
  }
  if (dvdnavsrc_query (src->srcpad, GST_QUERY_POSITION,
          &chapter_format, &value)) {
    gst_caps_set_simple (caps, "chapter", G_TYPE_INT, value, NULL);
  }

  if (dvdnavsrc_query (src->srcpad, GST_QUERY_TOTAL, &angle_format, &value)) {
    gst_caps_set_simple (caps, "angles", G_TYPE_INT, value, NULL);
  }
  if (dvdnavsrc_query (src->srcpad, GST_QUERY_POSITION, &angle_format, &value)) {
    gst_caps_set_simple (caps, "angle", G_TYPE_INT, value, NULL);
  }

  if (src->streaminfo) {
    gst_caps_free (src->streaminfo);
  }
  src->streaminfo = caps;
  g_object_notify (G_OBJECT (src), "streaminfo");
}

/*
 * Check for a new DVD domain area, and update the structure if
 * necessary.
 */
static void
dvdnavsrc_set_domain (DVDNavSrc * src)
{
  DVDNavSrcDomainType domain;

  if (dvdnav_is_domain_fp (src->dvdnav)) {
    domain = DVDNAVSRC_DOMAIN_FP;
  } else if (dvdnav_is_domain_vmgm (src->dvdnav)) {
    domain = DVDNAVSRC_DOMAIN_VMGM;
  } else if (dvdnav_is_domain_vtsm (src->dvdnav)) {
    domain = DVDNAVSRC_DOMAIN_VTSM;
  } else if (dvdnav_is_domain_vts (src->dvdnav)) {
    domain = DVDNAVSRC_DOMAIN_VTS;
  } else {
    domain = DVDNAVSRC_DOMAIN_UNKNOWN;
  }

  /* FIXME: We may send a signal if we have a new domain. */
  src->domain = domain;
}

/*
 * Check for a new highlighted area, and send an spu highlight event if
 * necessary.
 */
static void
dvdnavsrc_update_highlight (DVDNavSrc * src)
{
  int button = 0;
  pci_t *pci;
  dvdnav_highlight_area_t area;
  GstEvent *event;

  DVDNAV_CALL (dvdnav_get_current_highlight, (src->dvdnav, &button), src);

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

      event = dvdnavsrc_make_dvd_event (src, "dvd-spu-reset-highlight", NULL);
      gst_pad_push (src->srcpad, GST_DATA (event));
    }
    return;
  }

  DVDNAV_CALL (dvdnav_get_highlight_area, (pci, button, 0, &area), src);

  /* Check if we have a new button number, or a new highlight region. */
  if (button != src->button ||
      memcmp (&area, &(src->area), sizeof (dvdnav_highlight_area_t)) != 0) {
    memcpy (&(src->area), &area, sizeof (dvdnav_highlight_area_t));

    event = dvdnavsrc_make_dvd_event (src,
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
    gst_pad_push (src->srcpad, GST_DATA (event));
  }
}

static void
dvdnavsrc_user_op (DVDNavSrc * src, int op)
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
static gchar *
dvdnav_get_event_name (int event)
{
  switch (event) {
    case DVDNAV_BLOCK_OK:
      return "DVDNAV_BLOCK_OK";
      break;
    case DVDNAV_NOP:
      return "DVDNAV_NOP";
      break;
    case DVDNAV_STILL_FRAME:
      return "DVDNAV_STILL_FRAME";
      break;
    case DVDNAV_WAIT:
      return "DVDNAV_WAIT";
      break;
    case DVDNAV_SPU_STREAM_CHANGE:
      return "DVDNAV_SPU_STREAM_CHANGE";
      break;
    case DVDNAV_AUDIO_STREAM_CHANGE:
      return "DVDNAV_AUDIO_STREAM_CHANGE";
      break;
    case DVDNAV_VTS_CHANGE:
      return "DVDNAV_VTS_CHANGE";
      break;
    case DVDNAV_CELL_CHANGE:
      return "DVDNAV_CELL_CHANGE";
      break;
    case DVDNAV_NAV_PACKET:
      return "DVDNAV_NAV_PACKET";
      break;
    case DVDNAV_STOP:
      return "DVDNAV_STOP";
      break;
    case DVDNAV_HIGHLIGHT:
      return "DVDNAV_HIGHLIGHT";
      break;
    case DVDNAV_SPU_CLUT_CHANGE:
      return "DVDNAV_SPU_CLUT_CHANGE";
      break;
    case DVDNAV_HOP_CHANNEL:
      return "DVDNAV_HOP_CHANNEL";
      break;
  }
  return "UNKNOWN";
}

static gchar *
dvdnav_get_read_domain_name (dvd_read_domain_t domain)
{
  switch (domain) {
    case DVD_READ_INFO_FILE:
      return "DVD_READ_INFO_FILE";
      break;
    case DVD_READ_INFO_BACKUP_FILE:
      return "DVD_READ_INFO_BACKUP_FILE";
      break;
    case DVD_READ_MENU_VOBS:
      return "DVD_READ_MENU_VOBS";
      break;
    case DVD_READ_TITLE_VOBS:
      return "DVD_READ_TITLE_VOBS";
      break;
  }
  return "UNKNOWN";
}

static void
dvdnavsrc_print_event (DVDNavSrc * src, guint8 * data, int event, int len)
{
  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_DVDNAVSRC (src));

  GST_DEBUG_OBJECT (src, "dvdnavsrc (%p): event: %s", src,
      dvdnav_get_event_name (event));
  switch (event) {
    case DVDNAV_BLOCK_OK:
      break;
    case DVDNAV_NOP:
      break;
    case DVDNAV_STILL_FRAME:
    {
      dvdnav_still_event_t *event = (dvdnav_still_event_t *) data;

      GST_DEBUG_OBJECT (src, "  still frame: %d seconds", event->length);
    }
      break;
    case DVDNAV_WAIT:
    {
    }
      break;
    case DVDNAV_SPU_STREAM_CHANGE:
    {
      dvdnav_spu_stream_change_event_t *event =
          (dvdnav_spu_stream_change_event_t *) data;
      GST_DEBUG_OBJECT (src, "  physical_wide: %d", event->physical_wide);
      GST_DEBUG_OBJECT (src, "  physical_letterbox: %d",
          event->physical_letterbox);
      GST_DEBUG_OBJECT (src, "  physical_pan_scan: %d",
          event->physical_pan_scan);
      GST_DEBUG_OBJECT (src, "  logical: %d", event->logical);
    }
      break;
    case DVDNAV_AUDIO_STREAM_CHANGE:
    {
      dvdnav_audio_stream_change_event_t *event =
          (dvdnav_audio_stream_change_event_t *) data;
      GST_DEBUG_OBJECT (src, "  physical: %d", event->physical);
      GST_DEBUG_OBJECT (src, "  logical: %d", event->logical);
    }
      break;
    case DVDNAV_VTS_CHANGE:
    {
      dvdnav_vts_change_event_t *event = (dvdnav_vts_change_event_t *) data;

      GST_DEBUG_OBJECT (src, "  old_vtsN: %d", event->old_vtsN);
      GST_DEBUG_OBJECT (src, "  old_domain: %s",
          dvdnav_get_read_domain_name (event->old_domain));
      GST_DEBUG_OBJECT (src, "  new_vtsN: %d", event->new_vtsN);
      GST_DEBUG_OBJECT (src, "  new_domain: %s",
          dvdnav_get_read_domain_name (event->new_domain));
    }
      break;
    case DVDNAV_CELL_CHANGE:
    {
      /*dvdnav_cell_change_event_t *event =
         (dvdnav_cell_change_event_t *)data; */
      /* FIXME: Print something relevant here. */
    }
      break;
    case DVDNAV_NAV_PACKET:
    {
      /* FIXME: Print something relevant here. */
    }
      break;
    case DVDNAV_STOP:
      break;
    case DVDNAV_HIGHLIGHT:
    {
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
    }
      break;
    case DVDNAV_SPU_CLUT_CHANGE:
      break;
    case DVDNAV_HOP_CHANNEL:
      break;
    default:
      GST_DEBUG_OBJECT (src, "  event id: %d", event);
      break;
  }
}
#endif /* GST_DISABLE_GST_DEBUG */

static GstEvent *
dvdnavsrc_make_dvd_event (DVDNavSrc * src, const gchar * event_name,
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
  event = gst_event_new (GST_EVENT_ANY);
  event->event_data.structure.structure = structure;

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *text = gst_structure_to_string (structure);

    GST_LOG_OBJECT (src, "creating event \"%s\"", text);
    g_free (text);
  }
#endif

  return event;
}

static GstEvent *
dvdnavsrc_make_dvd_nav_packet_event (DVDNavSrc * src, const pci_t * pci)
{
  GstEvent *event;
  GstStructure *structure;
  GValue start_ptm = { 0 }, end_ptm = {
  0};

  /* Store the time values in GValues. */
  g_value_init (&start_ptm, G_TYPE_UINT64);
  g_value_set_uint64 (&start_ptm, MPEGTIME_TO_GSTTIME (pci->pci_gi.vobu_s_ptm));
  g_value_init (&end_ptm, G_TYPE_UINT64);
  g_value_set_uint64 (&end_ptm, MPEGTIME_TO_GSTTIME (pci->pci_gi.vobu_e_ptm));

  /* Add the values to a new structure. */
  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-nav-packet", NULL);
  gst_structure_set_value (structure, "start_ptm", &start_ptm);
  gst_structure_set_value (structure, "end_ptm", &end_ptm);

  /* Create the DVD event and put the structure into it. */
  event = gst_event_new (GST_EVENT_ANY);
  event->event_data.structure.structure = structure;

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *text = gst_structure_to_string (structure);

    GST_LOG_OBJECT (src, "creating event \"%s\"", text);
    g_free (text);
  }
#endif

  return event;
}

static GstEvent *
dvdnavsrc_make_clut_change_event (DVDNavSrc * src, const guint * clut)
{
  GstEvent *event;
  GstStructure *structure;
  guchar name[16];
  int i;

  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-spu-clut-change", NULL);

  /* Create a separate field for each value in the table. */
  for (i = 0; i < 16; i++) {
    sprintf (name, "clut%02d", i);
    gst_structure_set (structure, name, G_TYPE_INT, (int) clut[i], NULL);
  }

  /* Create the DVD event and put the structure into it. */
  event = gst_event_new (GST_EVENT_ANY);
  event->event_data.structure.structure = structure;

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *text = gst_structure_to_string (structure);

    GST_LOG_OBJECT (src, "creating event \"%s\"", text);
    g_free (text);
  }
#endif

  return event;
}

static void
dvdnavsrc_loop (GstElement * element)
{
  DVDNavSrc *src = DVDNAVSRC (element);
  int event, len;
  guint8 *data;
  GstData *send_data;

  g_return_if_fail (dvdnavsrc_is_open (src));

  if (src->did_seek || src->need_newmedia) {
    GstEvent *event;

    src->did_seek = FALSE;
    GST_INFO_OBJECT (src, "sending discont");

    event = gst_event_new_discontinuous (src->need_newmedia, 0);

    src->need_flush = FALSE;
    src->need_newmedia = FALSE;
    gst_pad_push (src->srcpad, GST_DATA (event));
    return;
  }

  if (src->need_flush) {
    src->need_flush = FALSE;
    GST_INFO_OBJECT (src, "sending flush");
    gst_pad_push (src->srcpad, GST_DATA (gst_event_new_flush ()));
    return;
  }

  /* Loop processing blocks until there is data to send. */
  send_data = NULL;
  while (send_data == NULL) {
    if (src->cur_buf == NULL) {
      src->cur_buf = gst_buffer_new_and_alloc (DVD_VIDEO_LB_LEN);
      if (src->cur_buf == NULL) {
        GST_ELEMENT_ERROR (src, CORE, TOO_LAZY, (NULL),
            ("Failed to create a new GstBuffer"));
        return;
      }
    }
    data = GST_BUFFER_DATA (src->cur_buf);

    DVDNAV_CALL (dvdnav_get_next_block, (src->dvdnav, data, &event, &len), src);

    switch (event) {
      case DVDNAV_NOP:
        break;

      case DVDNAV_BLOCK_OK:
        send_data = GST_DATA (src->cur_buf);
        src->cur_buf = NULL;
        break;

      case DVDNAV_STILL_FRAME:
      {
        dvdnav_still_event_t *info = (dvdnav_still_event_t *) data;
        GstClockTime current_time = gst_element_get_time (GST_ELEMENT (src));

        if (src->pause_mode == DVDNAVSRC_PAUSE_OFF) {
          dvdnavsrc_print_event (src, data, event, len);

          /* We just saw a still frame. Start a pause now. */
          if (info->length == 0xff) {
            GST_INFO_OBJECT (src, "starting unlimited pause");
            src->pause_mode = DVDNAVSRC_PAUSE_UNLIMITED;
          } else {
            src->pause_mode = DVDNAVSRC_PAUSE_LIMITED;
            src->pause_end = current_time + info->length * GST_SECOND;
            GST_INFO_OBJECT (src,
                "starting limited pause: %d seconds at %llu until %llu",
                info->length, current_time, src->pause_end);
          }

          /* For the moment, send the first empty event to let
             everyone know that we are displaying a still frame.
             Subsequent calls to this function will take care of
             the rest of the pause. */
          GST_DEBUG_OBJECT (src, "sending still frame event");
          send_data = GST_DATA (dvdnavsrc_make_dvd_event (src,
                  "dvd-spu-still-frame", NULL));
          break;
        }

        if (src->pause_mode == DVDNAVSRC_PAUSE_UNLIMITED ||
            current_time < src->pause_end) {
          GstEvent *event;

          /* We are in pause mode. Make this element sleep for a
             fraction of a second. */
          if (current_time + DVDNAVSRC_PAUSE_INTERVAL > src->pause_end) {
            gst_element_wait (GST_ELEMENT (src), src->pause_end);
          } else {
            gst_element_wait (GST_ELEMENT (src),
                current_time + DVDNAVSRC_PAUSE_INTERVAL);
          }
          /* Send an empty event to keep the pipeline going. */
          /* FIXME: Use an interrupt/filler event here. */
          event = gst_event_new (GST_EVENT_EMPTY);
          send_data = GST_DATA (event);
          GST_EVENT_TIMESTAMP (event) =
              gst_element_get_time (GST_ELEMENT (src));

          break;
        } else {
          /* We reached the end of the pause. */
          src->pause_mode = DVDNAVSRC_PAUSE_OFF;
          DVDNAV_CALL (dvdnav_still_skip, (src->dvdnav), src);
        }
      }
        break;

      case DVDNAV_WAIT:
        /* FIXME: We should really wait here until the fifos are
           empty, but I have no idea how to do that.  In the mean time,
           just clean the wait state. */
        GST_INFO_OBJECT (src, "sending wait");
        DVDNAV_CALL (dvdnav_wait_skip, (src->dvdnav), src);
        break;

      case DVDNAV_STOP:
        GST_INFO_OBJECT (src, "sending eos");
        gst_element_set_eos (GST_ELEMENT (src));
        dvdnavsrc_close (src);

        send_data = GST_DATA (gst_event_new (GST_EVENT_EOS));
        break;

      case DVDNAV_CELL_CHANGE:
        dvdnavsrc_update_streaminfo (src);
        break;

      case DVDNAV_NAV_PACKET:
      {
        pci_t *pci = dvdnav_get_current_nav_pci (src->dvdnav);

        /* Check for forced buttons. */
        if (pci->hli.hl_gi.hli_ss == 1) {
          GST_LOG_OBJECT (src, "menu ahead");
          if (pci->hli.hl_gi.fosl_btnn > 0) {
            GST_DEBUG_OBJECT (src, "forced button");
            dvdnav_button_select (src->dvdnav, pci, pci->hli.hl_gi.fosl_btnn);
          }
        }

        dvdnavsrc_update_highlight (src);

        /* Send a dvd nav packet event. */
        send_data = GST_DATA (dvdnavsrc_make_dvd_nav_packet_event (src, pci));
      }
        break;

      case DVDNAV_SPU_CLUT_CHANGE:
        send_data = GST_DATA (dvdnavsrc_make_clut_change_event (src,
                (guint *) data));
        break;

      case DVDNAV_VTS_CHANGE:
      {
        dvdnavsrc_set_domain (src);

        send_data = GST_DATA (dvdnavsrc_make_dvd_event (src,
                "dvd-vts-change", "domain",
                G_TYPE_INT, (gint) src->domain, NULL));
      }
        break;

      case DVDNAV_AUDIO_STREAM_CHANGE:
      {
        dvdnav_audio_stream_change_event_t *info =
            (dvdnav_audio_stream_change_event_t *) data;
        int phys = info->physical;

        dvdnavsrc_print_event (src, data, event, len);

        if (phys < 0 || phys > DVDNAVSRC_MAX_AUDIO_STREAMS) {
          phys = -1;
        }

        if (phys == src->audio_phys &&
            dvdnav_get_active_audio_stream (src->dvdnav) == src->audio_log) {
          /* Audio state hasn't changed. */
          break;
        }

        src->audio_phys = phys;
        src->audio_log = dvdnav_get_active_audio_stream (src->dvdnav);
        send_data = GST_DATA (dvdnavsrc_make_dvd_event (src,
                "dvd-audio-stream-change",
                "physical", G_TYPE_INT, (gint) src->audio_phys,
                "logical", G_TYPE_INT, (gint) src->audio_log, NULL));
      }
        break;

      case DVDNAV_SPU_STREAM_CHANGE:
      {
        dvdnav_spu_stream_change_event_t *info =
            (dvdnav_spu_stream_change_event_t *) data;
        /* FIXME: Which type of physical stream to use here should
           be configurable through a property. We take widescreen
           for the moment. */
        int phys = info->physical_wide;

        dvdnavsrc_print_event (src, data, event, len);

        if (phys < 0 || phys > DVDNAVSRC_MAX_SPU_STREAMS) {
          phys = -1;
        }

        if (phys == src->subp_phys &&
            dvdnav_get_active_spu_stream (src->dvdnav) == src->subp_log) {
          /* Subpicture state hasn't changed. */
          break;
        }

        src->subp_phys = phys;
        src->subp_log = dvdnav_get_active_spu_stream (src->dvdnav);
        send_data = GST_DATA (dvdnavsrc_make_dvd_event (src,
                "dvd-spu-stream-change",
                "physical", G_TYPE_INT, (gint) phys,
                "logical", G_TYPE_INT,
                (gint) dvdnav_get_active_spu_stream (src->dvdnav), NULL));
      }
        break;

      case DVDNAV_HIGHLIGHT:
        dvdnavsrc_print_event (src, data, event, len);

        dvdnavsrc_update_highlight (src);
        break;

      case DVDNAV_HOP_CHANNEL:
        dvdnavsrc_print_event (src, data, event, len);

        src->button = 0;
        src->pause_mode = DVDNAVSRC_PAUSE_OFF;
        send_data = GST_DATA (gst_event_new_flush ());
        break;

      default:
        g_error ("dvdnavsrc: Unknown dvdnav event %d", event);
        break;
    }
  }

  gst_pad_push (src->srcpad, send_data);
}

/* open the file, necessary to go to RUNNING state */
static gboolean
dvdnavsrc_open (DVDNavSrc * src)
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC (src), FALSE);
  g_return_val_if_fail (!dvdnavsrc_is_open (src), FALSE);
  g_return_val_if_fail (src->location != NULL, FALSE);

  if (dvdnav_open (&src->dvdnav, (char *) src->location) != DVDNAV_STATUS_OK) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED,
        (_("Failed to open DVD device '%s'."), src->location),
        GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_OBJECT_FLAG_SET (src, DVDNAVSRC_OPEN);

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

  if (src->title > 0) {
    unsigned char buf[2048];
    int event, buflen = sizeof (buf);

    DVDNAV_CALLVAL (dvdnav_get_next_block,
        (src->dvdnav, buf, &event, &buflen), src, FALSE);
    dvdnavsrc_print_event (src, buf, event, buflen);

    if (!dvdnavsrc_tca_seek (src, src->title, src->chapter, src->angle)) {
      return FALSE;
    }
  }

  return TRUE;
}

/* close the file */
static gboolean
dvdnavsrc_close (DVDNavSrc * src)
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC (src), FALSE);
  g_return_val_if_fail (dvdnavsrc_is_open (src), FALSE);
  g_return_val_if_fail (src->dvdnav != NULL, FALSE);

  DVDNAV_CALLVAL (dvdnav_close, (src->dvdnav), src, FALSE);

  GST_OBJECT_FLAG_UNSET (src, DVDNAVSRC_OPEN);

  return TRUE;
}

static GstStateChangeReturn
dvdnavsrc_change_state (GstElement * element, GstStateChange transition)
{
  DVDNavSrc *src;

  g_return_val_if_fail (GST_IS_DVDNAVSRC (element), GST_STATE_CHANGE_FAILURE);

  src = DVDNAVSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!dvdnavsrc_is_open (src)) {
        if (!dvdnavsrc_open (src)) {
          return GST_STATE_CHANGE_FAILURE;
        }
      }
      src->streaminfo = NULL;
      src->need_newmedia = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (dvdnavsrc_is_open (src)) {
        if (!dvdnavsrc_close (src)) {
          return GST_STATE_CHANGE_FAILURE;
        }
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static const GstEventMask *
dvdnavsrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET |
          GST_SEEK_METHOD_CUR | GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH},
    {GST_EVENT_FLUSH, 0},
    {GST_EVENT_NAVIGATION, GST_EVENT_FLAG_NONE},
    {0,}
  };

  return masks;
}

static gboolean
dvdnav_handle_navigation_event (DVDNavSrc * src, GstEvent * event)
{
  GstStructure *structure = event->event_data.structure.structure;
  const char *event_type = gst_structure_get_string (structure, "event");

  g_return_val_if_fail (event != NULL, FALSE);

  if (strcmp (event_type, "key-press") == 0) {
    const char *key = gst_structure_get_string (structure, "key");

    g_assert (key != NULL);
    GST_DEBUG ("dvdnavsrc got a keypress: %s", key);
  } else if (strcmp (event_type, "mouse-move") == 0) {
    double x, y;

    gst_structure_get_double (structure, "pointer_x", &x);
    gst_structure_get_double (structure, "pointer_y", &y);

    dvdnav_mouse_select (src->dvdnav,
        dvdnav_get_current_nav_pci (src->dvdnav), (int) x, (int) y);

    dvdnavsrc_update_highlight (src);
  } else if (strcmp (event_type, "mouse-button-release") == 0) {
    double x, y;

    gst_structure_get_double (structure, "pointer_x", &x);
    gst_structure_get_double (structure, "pointer_y", &y);

    dvdnav_mouse_activate (src->dvdnav,
        dvdnav_get_current_nav_pci (src->dvdnav), (int) x, (int) y);
  }

  return TRUE;
}

static gboolean
dvdnavsrc_event (GstPad * pad, GstEvent * event)
{
  DVDNavSrc *src;
  gboolean res = TRUE;

  src = DVDNAVSRC (gst_pad_get_parent (pad));

  if (!GST_OBJECT_FLAG_IS_SET (src, DVDNAVSRC_OPEN))
    goto error;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 offset;
      gint format;
      int titles, title, new_title;
      int parts, part, new_part;
      int angles, angle, new_angle;
      int origin;

      format = GST_EVENT_SEEK_FORMAT (event);
      offset = GST_EVENT_SEEK_OFFSET (event);

      switch (format) {
        case GST_FORMAT_BYTES:
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
          if (dvdnav_sector_search (src->dvdnav, (offset / DVD_SECTOR_SIZE),
                  origin) != DVDNAV_STATUS_OK) {
            goto error;
          }
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
            if (dvdnav_part_play (src->dvdnav, title, new_part) !=
                DVDNAV_STATUS_OK) {
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
            if (dvdnav_angle_change (src->dvdnav, new_angle) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
          } else {
            goto error;
          }
      }
      src->did_seek = TRUE;
      src->need_flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
      break;
    }
    case GST_EVENT_NAVIGATION:
      res = dvdnav_handle_navigation_event (src, event);
      break;
    case GST_EVENT_FLUSH:
      src->need_flush = TRUE;
      break;
    default:
      goto error;
  }

  if (FALSE) {
  error:
    res = FALSE;
  }
  gst_event_unref (event);

  return res;
}

static const GstFormat *
dvdnavsrc_get_formats (GstPad * pad)
{
  int i;
  static GstFormat formats[] = {
    GST_FORMAT_BYTES,
    /*
       GST_FORMAT_TIME,
       GST_FORMAT_DEFAULT,
     */
    0,                          /* filled later */
    0,                          /* filled later */
    0,                          /* filled later */
    0,                          /* filled later */
    0
  };
  static gboolean format_initialized = FALSE;

  if (!format_initialized) {
    for (i = 0; formats[i] != 0; i++) {
    }
    formats[i++] = sector_format;
    formats[i++] = title_format;
    formats[i++] = chapter_format;
    formats[i++] = angle_format;
    format_initialized = TRUE;
  }

  return formats;
}

static gboolean
dvdnavsrc_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  DVDNavSrc *src;

  src = DVDNAVSRC (gst_pad_get_parent (pad));

  if (!GST_OBJECT_FLAG_IS_SET (src, DVDNAVSRC_OPEN))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      if (*dest_format == sector_format) {
        *dest_value = src_value / DVD_SECTOR_SIZE;
      } else
        return FALSE;
    default:
      if ((src_format == sector_format) && (*dest_format == GST_FORMAT_BYTES)) {
        *dest_value = src_value * DVD_SECTOR_SIZE;
      } else
        return FALSE;
  }

  return TRUE;
}

static const GstQueryType *
dvdnavsrc_get_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return src_query_types;
}

static gboolean
dvdnavsrc_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = TRUE;
  DVDNavSrc *src;
  int titles, title;
  int parts, part;
  int angles, angle;
  unsigned int pos, len;

  src = DVDNAVSRC (gst_pad_get_parent (pad));

  if (!GST_OBJECT_FLAG_IS_SET (src, DVDNAVSRC_OPEN))
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      if (*format == sector_format) {
        if (dvdnav_get_position (src->dvdnav, &pos, &len)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = len;
      } else if (*format == GST_FORMAT_BYTES) {
        if (dvdnav_get_position (src->dvdnav, &pos, &len) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = len * DVD_SECTOR_SIZE;
      } else if (*format == title_format) {
        if (dvdnav_get_number_of_titles (src->dvdnav, &titles)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = titles;
      } else if (*format == chapter_format) {
        if (dvdnav_get_number_of_titles (src->dvdnav, &parts)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = parts;
      } else if (*format == angle_format) {
        if (dvdnav_get_angle_info (src->dvdnav, &angle, &angles)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = angles;
      } else {
        res = FALSE;
      }
      break;
    case GST_QUERY_POSITION:
      if (*format == sector_format) {
        if (dvdnav_get_position (src->dvdnav, &pos, &len)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = pos;
      } else if (*format == title_format) {
        if (dvdnav_current_title_info (src->dvdnav, &title, &part)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = title;
      } else if (*format == chapter_format) {
        if (dvdnav_current_title_info (src->dvdnav, &title, &part)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = part;
      } else if (*format == angle_format) {
        if (dvdnav_get_angle_info (src->dvdnav, &angle, &angles)
            != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = angle;
      } else {
        res = FALSE;
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

/*
 * URI interface.
 */

static guint
dvdnavsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
dvdnavsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "dvdnav", NULL };

  return protocols;
}

static const gchar *
dvdnavsrc_uri_get_uri (GstURIHandler * handler)
{
  return "dvdnav://";
}

static gboolean
dvdnavsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gboolean ret;
  gchar *protocol = gst_uri_get_protocol (uri);

  ret = (protocol && !strcmp (protocol, "dvdnav")) ? TRUE : FALSE;
  g_free (protocol);

  return ret;
}

static void
dvdnavsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = dvdnavsrc_uri_get_type;
  iface->get_protocols = dvdnavsrc_uri_get_protocols;
  iface->get_uri = dvdnavsrc_uri_get_uri;
  iface->set_uri = dvdnavsrc_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dvdnavsrc", GST_RANK_NONE,
          GST_TYPE_DVDNAVSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvdnavsrc",
    "Access a DVD with navigation features using libdvdnav",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
