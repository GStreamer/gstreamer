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
#include <gst/gst.h>

#include "config.h"

#include <dvdnav/dvdnav.h>
#include <dvdread/nav_print.h>

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

struct _DVDNavSrc {
  GstElement element;

  /* pads */
  GstPad *srcpad;
  GstCaps *streaminfo;

  /* location */
  gchar *location;

  gboolean did_seek;
  gboolean need_flush;
  GstBufferPool *bufferpool;

  int title, chapter, angle;
  dvdnav_t *dvdnav;

  GstCaps *buttoninfo;
};

struct _DVDNavSrcClass {
  GstElementClass parent_class;

  void (*button_pressed) (DVDNavSrc *src, int button);
  void (*pointer_select) (DVDNavSrc *src, int x, int y);
  void (*pointer_activate) (DVDNavSrc *src, int x, int y);
  void (*user_op) (DVDNavSrc *src, int op);
};

/* elementfactory information */
GstElementDetails dvdnavsrc_details = {
  "DVD Source",
  "Source/File/DVD",
  "GPL",
  "Access a DVD with navigation features using libdvdnav",
  VERSION,
  "David I. Lehn <dlehn@users.sourceforge.net>",
  "(C) 2002",
};


/* DVDNavSrc signals and args */
enum {
  BUTTON_PRESSED_SIGNAL,
  POINTER_SELECT_SIGNAL,
  POINTER_ACTIVATE_SIGNAL,
  USER_OP_SIGNAL,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_STREAMINFO,
  ARG_BUTTONINFO,
  ARG_TITLE_STRING,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE
};

typedef enum {
  DVDNAVSRC_OPEN		= GST_ELEMENT_FLAG_LAST,

  DVDNAVSRC_FLAG_LAST		= GST_ELEMENT_FLAG_LAST+2,
} DVDNavSrcFlags;


GType			dvdnavsrc_get_type	(void);
static void 		dvdnavsrc_class_init	(DVDNavSrcClass *klass);
static void 		dvdnavsrc_init		(DVDNavSrc *dvdnavsrc);

static void 		dvdnavsrc_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void 		dvdnavsrc_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstData *	dvdnavsrc_get		(GstPad *pad);
/*static GstBuffer *	dvdnavsrc_get_region	(GstPad *pad,gulong offset,gulong size); */
static gboolean 	dvdnavsrc_event 		(GstPad *pad, GstEvent *event);
static const GstEventMask*
			dvdnavsrc_get_event_mask 	(GstPad *pad);
static const GstFormat*
			dvdnavsrc_get_formats 		(GstPad *pad);
/*static gboolean 	dvdnavsrc_convert 		(GstPad *pad,
				    			 GstFormat src_format,
				    			 gint64 src_value, 
							 GstFormat *dest_format, 
							 gint64 *dest_value);*/
static gboolean 	dvdnavsrc_query 		(GstPad *pad, GstQueryType type,
		     					 GstFormat *format, gint64 *value);
static const GstQueryType*
			dvdnavsrc_get_query_types 	(GstPad *pad);

static gboolean		dvdnavsrc_close		(DVDNavSrc *src);
static gboolean		dvdnavsrc_open		(DVDNavSrc *src);
static gboolean		dvdnavsrc_is_open	(DVDNavSrc *src);
static void		dvdnavsrc_print_event	(DVDNavSrc *src, guint8 *data, int event, int len);
static void		dvdnavsrc_update_streaminfo (DVDNavSrc *src);
static void		dvdnavsrc_update_buttoninfo (DVDNavSrc *src);
static void		dvdnavsrc_button_pressed (DVDNavSrc *src, int button);
static void		dvdnavsrc_pointer_select (DVDNavSrc *src, int x, int y);
static void		dvdnavsrc_pointer_activate (DVDNavSrc *src, int x, int y);
static void		dvdnavsrc_user_op (DVDNavSrc *src, int op);

static GstElementStateReturn 	dvdnavsrc_change_state 	(GstElement *element);


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
      sizeof(DVDNavSrcClass),      NULL,
      NULL,
      (GClassInitFunc)dvdnavsrc_class_init,
      NULL,
      NULL,
      sizeof(DVDNavSrc),
      0,
      (GInstanceInitFunc)dvdnavsrc_init,
    };
    dvdnavsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "DVDNavSrc", &dvdnavsrc_info, 0);

    sector_format = gst_format_register ("sector", "DVD sector");
    title_format = gst_format_register ("title", "DVD title");
    chapter_format = gst_format_register ("chapter", "DVD chapter");
    angle_format = gst_format_register ("angle", "DVD angle");
  }
  return dvdnavsrc_type;
}

static void
dvdnavsrc_class_init (DVDNavSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  dvdnavsrc_signals[BUTTON_PRESSED_SIGNAL] =
    g_signal_new ("button_pressed",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (DVDNavSrcClass, button_pressed),
        NULL, NULL,
        gst_marshal_VOID__INT,
        G_TYPE_NONE, 1,
        G_TYPE_INT);

  dvdnavsrc_signals[POINTER_SELECT_SIGNAL] =
    g_signal_new ("pointer_select",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (DVDNavSrcClass, pointer_select),
        NULL, NULL,
        gst_marshal_VOID__INT_INT,
        G_TYPE_NONE, 2,
        G_TYPE_INT, G_TYPE_INT);

  dvdnavsrc_signals[POINTER_ACTIVATE_SIGNAL] =
    g_signal_new ("pointer_activate",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (DVDNavSrcClass, pointer_activate),
        NULL, NULL,
        gst_marshal_VOID__INT_INT,
        G_TYPE_NONE, 2,
        G_TYPE_INT, G_TYPE_INT);

  dvdnavsrc_signals[USER_OP_SIGNAL] =
    g_signal_new ("user_op",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (DVDNavSrcClass, user_op),
        NULL, NULL,
        gst_marshal_VOID__INT,
        G_TYPE_NONE, 1,
        G_TYPE_INT);

  klass->button_pressed = dvdnavsrc_button_pressed;
  klass->pointer_select = dvdnavsrc_pointer_select;
  klass->pointer_activate = dvdnavsrc_pointer_activate;
  klass->user_op = dvdnavsrc_user_op;
    
  g_object_class_install_property(gobject_class, ARG_LOCATION,
    g_param_spec_string("location", "location", "location",
                        NULL, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_TITLE_STRING,
    g_param_spec_string("title_string", "title string", "DVD title string",
                        NULL, G_PARAM_READABLE));
  g_object_class_install_property(gobject_class, ARG_TITLE,
    g_param_spec_int("title", "title", "title",
                     0,99,1,G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_CHAPTER,
    g_param_spec_int("chapter", "chapter", "chapter",
                     1,99,1,G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_ANGLE,
    g_param_spec_int("angle", "angle", "angle",
                     1,9,1,G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_STREAMINFO,
    g_param_spec_boxed("streaminfo", "streaminfo", "streaminfo",
                       GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property(gobject_class, ARG_BUTTONINFO,
    g_param_spec_boxed("buttoninfo", "buttoninfo", "buttoninfo",
                       GST_TYPE_CAPS, G_PARAM_READABLE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR(dvdnavsrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(dvdnavsrc_get_property);

  gstelement_class->change_state = dvdnavsrc_change_state;
}

static void 
dvdnavsrc_init (DVDNavSrc *src) 
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);

  gst_pad_set_get_function (src->srcpad, dvdnavsrc_get);
  gst_pad_set_event_function (src->srcpad, dvdnavsrc_event);
  gst_pad_set_event_mask_function (src->srcpad, dvdnavsrc_get_event_mask);
  /*gst_pad_set_convert_function (src->srcpad, dvdnavsrc_convert);*/
  gst_pad_set_query_function (src->srcpad, dvdnavsrc_query);
  gst_pad_set_query_type_function (src->srcpad, dvdnavsrc_get_query_types);
  gst_pad_set_formats_function (src->srcpad, dvdnavsrc_get_formats);

  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

  src->bufferpool = gst_buffer_pool_get_default (DVD_VIDEO_LB_LEN, 2);

  src->location = g_strdup("/dev/dvd");
  src->did_seek = FALSE;
  src->need_flush = FALSE;
  src->title = 0;
  src->chapter = 0;
  src->angle = 1;
  src->streaminfo = NULL;
  src->buttoninfo = NULL;
}

/* FIXME: this code is not being used */
#ifdef PLEASEFIXTHISCODE
static void
dvdnavsrc_destroy (DVDNavSrc *dvdnavsrc)
{
  /* FIXME */
  g_print("FIXME\n");
  gst_buffer_pool_destroy (dvdnavsrc->bufferpool);
}
#endif

static gboolean 
dvdnavsrc_is_open (DVDNavSrc *src)
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC (src), FALSE);

  return GST_FLAG_IS_SET (src, DVDNAVSRC_OPEN);
}

static void 
dvdnavsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  DVDNavSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDNAVSRC (object));
  
  src = DVDNAVSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
      /*g_return_if_fail(!GST_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      if (src->location)
        g_free (src->location);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        src->location = g_strdup("/dev/dvd");
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void 
dvdnavsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  DVDNavSrc *src;
  const char *title_string;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDNAVSRC (object));
  
  src = DVDNAVSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case ARG_STREAMINFO:
      g_value_set_boxed (value, src->streaminfo);
      break;
    case ARG_BUTTONINFO:
      g_value_set_boxed (value, src->buttoninfo);
      break;
    case ARG_TITLE_STRING:
      if (!dvdnavsrc_is_open(src)) {
        g_value_set_string (value, "");
      } else if (dvdnav_get_title_string(src->dvdnav, &title_string) !=
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
dvdnavsrc_tca_seek(DVDNavSrc *src, int title, int chapter, int angle)
{
  int titles, programs, curangle, angles;

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (src->dvdnav != NULL, FALSE);
  g_return_val_if_fail (dvdnavsrc_is_open (src), FALSE);

  /* Dont try to seek to track 0 - First Play program chain */
  g_return_val_if_fail (src->title > 0, FALSE);

  fprintf (stderr, "dvdnav: seeking to %d/%d/%d\n", title, chapter, angle);
  /**
   * Make sure our title number is valid.
   */
  if (dvdnav_get_number_of_titles (src->dvdnav, &titles) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_get_number_of_titles error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }
  fprintf (stderr, "There are %d titles on this DVD.\n", titles);
  if (title < 1 || title > titles) {
    fprintf (stderr, "Invalid title %d.\n", title);
    dvdnavsrc_close (src);
    return FALSE;
  }

  /**
   * Before we can get the number of chapters (programs) we need to call
   * dvdnav_title_play so that dvdnav_get_number_of_programs knows which title
   * to operate on (also needed to get the number of angles)
   */
  if (dvdnav_title_play (src->dvdnav, title) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_title_play error: %s\n",
        dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }

  /**
   * Make sure the chapter number is valid for this title.
   */
  if (dvdnav_get_number_of_titles (src->dvdnav, &programs) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_get_number_of_programs error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }
  fprintf (stderr, "There are %d chapters in this title.\n", programs);
  if (chapter < 0 || chapter > programs) {
    fprintf (stderr, "Invalid chapter %d\n", chapter);
    dvdnavsrc_close (src);
    return FALSE;
  }

  /**
   * Make sure the angle number is valid for this title.
   */
  if (dvdnav_get_angle_info (src->dvdnav, &curangle, &angles) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_get_angle_info error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }
  fprintf (stderr, "There are %d angles in this title.\n", angles);
  if( angle < 1 || angle > angles) {
    fprintf (stderr, "Invalid angle %d\n", angle);
    dvdnavsrc_close (src);
    return FALSE;
  }

  /**
   * We've got enough info, time to open the title set data.
   */
  if (src->chapter == 0) {
    if (dvdnav_title_play (src->dvdnav, title) != DVDNAV_STATUS_OK) {
      fprintf (stderr, "dvdnav_title_play error: %s\n", dvdnav_err_to_string(src->dvdnav));
      return FALSE;
    }
  } else {
    if (dvdnav_part_play (src->dvdnav, title, chapter) != DVDNAV_STATUS_OK) {
      fprintf (stderr, "dvdnav_part_play error: %s\n", dvdnav_err_to_string(src->dvdnav));
      return FALSE;
    }
  }
  if (dvdnav_angle_change (src->dvdnav, angle) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_angle_change error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }

  /*
  if (dvdnav_physical_audio_stream_change (src->dvdnav, 0) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_physical_audio_stream_change error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }
  if (dvdnav_logical_audio_stream_change (src->dvdnav, 0) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_logical_audio_stream_change error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }
  */

  src->did_seek = TRUE;

  return TRUE;
}

static void
dvdnavsrc_update_streaminfo (DVDNavSrc *src)
{
  GstCaps *caps;
  GstProps *props;
  GstPropsEntry *entry;
  gint64 value;

  props = gst_props_empty_new ();

  /*
  entry = gst_props_entry_new ("title_string", GST_PROPS_STRING (""));
  gst_props_add_entry (props, entry);
  */

  if (dvdnavsrc_query(src->srcpad, GST_QUERY_TOTAL, &title_format, &value)) {
    entry = gst_props_entry_new ("titles", GST_PROPS_INT (value));
    gst_props_add_entry (props, entry);
  }
  if (dvdnavsrc_query(src->srcpad, GST_QUERY_POSITION, &title_format, &value)) {
    entry = gst_props_entry_new ("title", GST_PROPS_INT (value));
    gst_props_add_entry (props, entry);
  }

  if (dvdnavsrc_query(src->srcpad, GST_QUERY_TOTAL, &chapter_format, &value)) {
    entry = gst_props_entry_new ("chapters", GST_PROPS_INT (value));
    gst_props_add_entry (props, entry);
  }
  if (dvdnavsrc_query(src->srcpad, GST_QUERY_POSITION, &chapter_format, &value)) {
    entry = gst_props_entry_new ("chapter", GST_PROPS_INT (value));
    gst_props_add_entry (props, entry);
  }

  if (dvdnavsrc_query(src->srcpad, GST_QUERY_TOTAL, &angle_format, &value)) {
    entry = gst_props_entry_new ("angles", GST_PROPS_INT (value));
    gst_props_add_entry (props, entry);
  }
  if (dvdnavsrc_query(src->srcpad, GST_QUERY_POSITION, &angle_format, &value)) {
    entry = gst_props_entry_new ("angle", GST_PROPS_INT (value));
    gst_props_add_entry (props, entry);
  }

  caps = gst_caps_new ("dvdnavsrc_streaminfo",
      "application/x-gst-streaminfo",
      props);
  if (src->streaminfo) {
    gst_caps_unref (src->streaminfo);
  }
  src->streaminfo = caps;
  g_object_notify (G_OBJECT (src), "streaminfo");
}

static void
dvdnavsrc_update_buttoninfo (DVDNavSrc *src)
{
  GstCaps *caps;
  GstProps *props;
  GstPropsEntry *entry;
  pci_t *pci;

  pci = dvdnav_get_current_nav_pci(src->dvdnav);
  fprintf(stderr, "update button info total:%d\n", pci->hli.hl_gi.btn_ns);

  props = gst_props_empty_new ();

  entry = gst_props_entry_new ("total", GST_PROPS_INT (pci->hli.hl_gi.btn_ns));
  gst_props_add_entry (props, entry);

  caps = gst_caps_new ("dvdnavsrc_buttoninfo",
      "application/x-gst-dvdnavsrc-buttoninfo",
      props);
  if (src->buttoninfo) {
    gst_caps_unref (src->buttoninfo);
  }
  src->buttoninfo = caps;
  g_object_notify (G_OBJECT (src), "buttoninfo");
}

static void
dvdnavsrc_button_pressed (DVDNavSrc *src, int button)
{
}

static void
dvdnavsrc_pointer_select (DVDNavSrc *src, int x, int y)
{
  dvdnav_mouse_select(src->dvdnav,
                      dvdnav_get_current_nav_pci(src->dvdnav),
                      x, y);
}

static void
dvdnavsrc_pointer_activate (DVDNavSrc *src, int x, int y)
{
  dvdnav_mouse_activate(src->dvdnav,
                        dvdnav_get_current_nav_pci(src->dvdnav),
                        x, y);
}

static void
dvdnavsrc_user_op (DVDNavSrc *src, int op)
{
  pci_t *pci = dvdnav_get_current_nav_pci(src->dvdnav);

  fprintf (stderr, "user_op %d\n", op);
  /* Magic user_op ids */
  switch (op) {
    case 0: /* None */
      break;
    case 1: /* Upper */
      if (dvdnav_upper_button_select(src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 2: /* Lower */
      if (dvdnav_lower_button_select(src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 3: /* Left */
      if (dvdnav_left_button_select(src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 4: /* Right */
      if (dvdnav_right_button_select(src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 5: /* Activate */
      if (dvdnav_button_activate(src->dvdnav, pci) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 6: /* GoUp */
      if (dvdnav_go_up(src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 7: /* TopPG */
      if (dvdnav_top_pg_search(src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 8: /* PrevPG */
      if (dvdnav_prev_pg_search(src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 9: /* NextPG */
      if (dvdnav_next_pg_search(src->dvdnav) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 10: /* Menu - Title */
      if (dvdnav_menu_call(src->dvdnav, DVD_MENU_Title) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 11: /* Menu - Root */
      if (dvdnav_menu_call(src->dvdnav, DVD_MENU_Root) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 12: /* Menu - Subpicture */
      if (dvdnav_menu_call(src->dvdnav, DVD_MENU_Subpicture) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 13: /* Menu - Audio */
      if (dvdnav_menu_call(src->dvdnav, DVD_MENU_Audio) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 14: /* Menu - Angle */
      if (dvdnav_menu_call(src->dvdnav, DVD_MENU_Angle) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
    case 15: /* Menu - Part */
      if (dvdnav_menu_call(src->dvdnav, DVD_MENU_Part) != DVDNAV_STATUS_OK) {
        goto naverr;
      }
      break;
  }
  return;
naverr:
  gst_element_error(GST_ELEMENT(src), "user op %d failure: %d",
      op, dvdnav_err_to_string(src->dvdnav));

}

static gchar *
dvdnav_get_event_name(int event)
{
  switch (event) {
    case DVDNAV_BLOCK_OK: return "DVDNAV_BLOCK_OK"; break;
    case DVDNAV_NOP: return "DVDNAV_NOP"; break;
    case DVDNAV_STILL_FRAME: return "DVDNAV_STILL_FRAME"; break;
    case DVDNAV_SPU_STREAM_CHANGE: return "DVDNAV_SPU_STREAM_CHANGE"; break;
    case DVDNAV_AUDIO_STREAM_CHANGE: return "DVDNAV_AUDIO_STREAM_CHANGE"; break;
    case DVDNAV_VTS_CHANGE: return "DVDNAV_VTS_CHANGE"; break;
    case DVDNAV_CELL_CHANGE: return "DVDNAV_CELL_CHANGE"; break;
    case DVDNAV_NAV_PACKET: return "DVDNAV_NAV_PACKET"; break;
    case DVDNAV_STOP: return "DVDNAV_STOP"; break;
    case DVDNAV_HIGHLIGHT: return "DVDNAV_HIGHLIGHT"; break;
    case DVDNAV_SPU_CLUT_CHANGE: return "DVDNAV_SPU_CLUT_CHANGE"; break;
    case DVDNAV_HOP_CHANNEL: return "DVDNAV_HOP_CHANNEL"; break;
    case DVDNAV_WAIT: return "DVDNAV_WAIT"; break;
  }
  return "UNKNOWN";
}

static gchar *
dvdnav_get_read_domain_name(dvd_read_domain_t domain)
{
  switch (domain) {
    case DVD_READ_INFO_FILE: return "DVD_READ_INFO_FILE"; break;
    case DVD_READ_INFO_BACKUP_FILE: return "DVD_READ_INFO_BACKUP_FILE"; break;
    case DVD_READ_MENU_VOBS: return "DVD_READ_MENU_VOBS"; break;
    case DVD_READ_TITLE_VOBS: return "DVD_READ_TITLE_VOBS"; break;
  }
  return "UNKNOWN";
}

static void
dvdnavsrc_print_event (DVDNavSrc *src, guint8 *data, int event, int len)
{
  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_DVDNAVSRC (src));

  fprintf (stderr, "dvdnavsrc (%p): event: %s\n", src, dvdnav_get_event_name(event));
  switch (event) {
    case DVDNAV_BLOCK_OK:
      break;
    case DVDNAV_NOP:
      break;
    case DVDNAV_STILL_FRAME:
      {
        dvdnav_still_event_t *event = (dvdnav_still_event_t *)data;
        fprintf (stderr, "  still frame: %d seconds\n", event->length);
      }
      break;
    case DVDNAV_SPU_STREAM_CHANGE:
      {
        dvdnav_spu_stream_change_event_t * event = (dvdnav_spu_stream_change_event_t *)data;
        fprintf (stderr, "  physical_wide: %d\n", event->physical_wide);
        fprintf (stderr, "  physical_letterbox: %d\n", event->physical_letterbox);
        fprintf (stderr, "  physical_pan_scan: %d\n", event->physical_pan_scan);
        fprintf (stderr, "  logical: %d\n", event->logical);
      }
      break;
    case DVDNAV_AUDIO_STREAM_CHANGE:
      {
        dvdnav_audio_stream_change_event_t * event = (dvdnav_audio_stream_change_event_t *)data;
        fprintf (stderr, "  physical: %d\n", event->physical);
        fprintf (stderr, "  logical: %d\n", event->logical);
      }
      break;
    case DVDNAV_VTS_CHANGE:
      {
        dvdnav_vts_change_event_t *event = (dvdnav_vts_change_event_t *)data;
        fprintf (stderr, "  old_vtsN: %d\n", event->old_vtsN);
        fprintf (stderr, "  old_domain: %s\n", dvdnav_get_read_domain_name(event->old_domain));
        fprintf (stderr, "  new_vtsN: %d\n", event->new_vtsN);
        fprintf (stderr, "  new_domain: %s\n", dvdnav_get_read_domain_name(event->new_domain));
      }
      break;
    case DVDNAV_CELL_CHANGE:
      {
        /*dvdnav_cell_change_event_t *event = (dvdnav_cell_change_event_t *)data;*/
        /*fprintf (stderr, "  old_cell: %p\n", event->old_cell);*/
        /*fprintf (stderr, "  new_cell: %p\n", event->new_cell);*/
      }
      break;
    case DVDNAV_NAV_PACKET:
      {
/*
        dvdnav_nav_packet_event_t *event = (dvdnav_nav_packet_event_t *)data;
        pci_t *pci;
        dsi_t *dsi;

        pci = event->pci;
        dsi = event->dsi;

        pci = dvdnav_get_current_nav_pci(src->dvdnav);
        dsi = dvdnav_get_current_nav_dsi(src->dvdnav);
        fprintf (stderr, "  pci: %p\n", event->pci);
        fprintf (stderr, "  dsi: %p\n", event->dsi);

        navPrint_PCI(pci);
        navPrint_DSI(dsi);
*/
      }
      break;
    case DVDNAV_STOP:
      break;
    case DVDNAV_HIGHLIGHT:
      {
        dvdnav_highlight_event_t *event = (dvdnav_highlight_event_t *)data;
        fprintf (stderr, "  display: %s\n", 
            event->display == 0 ? "hide" : (event->display == 1 ? "show" : "unknown")
            );
        if (event->display == 1) {
          fprintf (stderr, "  palette: %08x\n", event->palette);
          fprintf (stderr, "  coords (%u, %u) - (%u, %u)\n", event->sx, event->sy, event->ex, event->ey);
          fprintf (stderr, "  pts: %u\n", event->pts);
          fprintf (stderr, "  button: %u\n", event->buttonN);
        }
      }
      break;
    case DVDNAV_SPU_CLUT_CHANGE:
      break;
    case DVDNAV_HOP_CHANNEL:
      break;
    case DVDNAV_WAIT:
      break;
    default:
      fprintf (stderr, "  event id: %d\n", event);
      break;
  }
}

static GstData *
dvdnavsrc_get (GstPad *pad) 
{
  DVDNavSrc *src;
  int event, len;
  GstBuffer *buf;
  guint8 *data;
  gboolean have_buf;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  src = DVDNAVSRC (gst_pad_get_parent (pad));
  g_return_val_if_fail (dvdnavsrc_is_open (src), NULL);

  if (src->did_seek) {
    GstEvent *event;

    src->did_seek = FALSE;
    GST_DEBUG ("dvdnavsrc sending discont");
    event = gst_event_new_discontinuous (FALSE, 0);
    src->need_flush = FALSE;
    return GST_DATA (event);
  }
  if (src->need_flush) {
    src->need_flush = FALSE;
    GST_DEBUG ("dvdnavsrc sending flush");
    return GST_DATA (gst_event_new_flush());
  }

  /* loop processing blocks until data is pushed */
  have_buf = FALSE;
  while (!have_buf) {
    /* allocate a pool for the buffer data */
    /* FIXME: mem leak on non BLOCK_OK events */
    buf = gst_buffer_new_from_pool (src->bufferpool, DVD_VIDEO_LB_LEN, 0);
    if (!buf) {
      gst_element_error (GST_ELEMENT (src), "Failed to create a new GstBuffer");
      return NULL;
    }
    data = GST_BUFFER_DATA(buf);

    if (dvdnav_get_next_block (src->dvdnav, data, &event, &len) !=
        DVDNAV_STATUS_OK) {
      gst_element_error (GST_ELEMENT (src), "dvdnav_get_next_block error: %s\n",
          dvdnav_err_to_string(src->dvdnav));
      return NULL;
    }

    switch (event) {
      case DVDNAV_NOP:
        break;
      case DVDNAV_BLOCK_OK:
        g_return_val_if_fail (GST_BUFFER_DATA(buf) != NULL, NULL);
        g_return_val_if_fail (GST_BUFFER_SIZE(buf) == DVD_VIDEO_LB_LEN, NULL);
        have_buf = TRUE;
        break;
      case DVDNAV_STILL_FRAME:
        /* FIXME: we should pause for event->length seconds before
         * dvdnav_still_skip */
        dvdnavsrc_print_event (src, data, event, len);
        if (dvdnav_still_skip (src->dvdnav) != DVDNAV_STATUS_OK) {
          gst_element_error (GST_ELEMENT (src), "dvdnav_still_skip error: %s\n",
              dvdnav_err_to_string(src->dvdnav));
          /* FIXME: close the stream??? */
        }
        break;
      case DVDNAV_STOP:
        GST_DEBUG ("dvdnavsrc sending eos");
        gst_element_set_eos (GST_ELEMENT (src));
        dvdnavsrc_close(src);
        buf = GST_BUFFER (gst_event_new (GST_EVENT_EOS));
        have_buf = TRUE;
        break;
      case DVDNAV_CELL_CHANGE:
        dvdnavsrc_update_streaminfo (src);
        break;
      case DVDNAV_NAV_PACKET:
        if (0) dvdnavsrc_update_buttoninfo (src);
        break;
      case DVDNAV_WAIT:
	/* FIXME: supposed to make sure all the data has made 
	 * it to the sinks before skipping the wait
	 */
        dvdnav_wait_skip(src->dvdnav);
      case DVDNAV_VTS_CHANGE:
      case DVDNAV_SPU_STREAM_CHANGE:
      case DVDNAV_AUDIO_STREAM_CHANGE:
      case DVDNAV_HIGHLIGHT:
      case DVDNAV_SPU_CLUT_CHANGE:
      case DVDNAV_HOP_CHANNEL:
      default:
        dvdnavsrc_print_event (src, data, event, len);
        break;
    }
  }
  return GST_DATA(buf);
}

/* open the file, necessary to go to RUNNING state */
static gboolean 
dvdnavsrc_open (DVDNavSrc *src) 
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC(src), FALSE);
  g_return_val_if_fail (!dvdnavsrc_is_open (src), FALSE);
  g_return_val_if_fail (src->location != NULL, FALSE);

  if (dvdnav_open (&src->dvdnav, (char*)src->location) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_open error: %s location: %s\n", dvdnav_err_to_string(src->dvdnav), src->location);
    return FALSE;
  }

  GST_FLAG_SET (src, DVDNAVSRC_OPEN);

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
    int event, buflen = sizeof(buf);
    fprintf(stderr, "+XXX\n");
    if (dvdnav_get_next_block(src->dvdnav, buf, &event, &buflen) != DVDNAV_STATUS_OK) {
      fprintf(stderr, "pre seek dvdnav_get_next_block error: %s\n", dvdnav_err_to_string(src->dvdnav));
      return FALSE;
    }
    dvdnavsrc_print_event (src, buf, event, buflen);
    /*
    while (dvdnav_get_next_block(src->dvdnav, buf, &event, &buflen) == DVDNAV_STATUS_OK) {
      if (event != DVDNAV_BLOCK_OK)
        dvdnavsrc_print_event (src, buf, event, buflen);
    }
    */
    fprintf(stderr, "pre seek dvdnav_get_next_block error: %s\n", dvdnav_err_to_string(src->dvdnav));
    fprintf(stderr, "-XXX\n");

    if (!dvdnavsrc_tca_seek(src,
        src->title,
        src->chapter,
        src->angle))
      return FALSE;
  }
  
  return TRUE;
}

/* close the file */
static gboolean
dvdnavsrc_close (DVDNavSrc *src) 
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC(src), FALSE);
  g_return_val_if_fail (dvdnavsrc_is_open (src), FALSE);
  g_return_val_if_fail (src->dvdnav != NULL, FALSE);

  if (dvdnav_close (src->dvdnav) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_close error: %s\n",
        dvdnav_err_to_string (src->dvdnav));
    return FALSE;
  }

  GST_FLAG_UNSET (src, DVDNAVSRC_OPEN);

  return TRUE;
}

static GstElementStateReturn
dvdnavsrc_change_state (GstElement *element)
{
  DVDNavSrc *src;

  g_return_val_if_fail (GST_IS_DVDNAVSRC (element), GST_STATE_FAILURE);

  src = DVDNAVSRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (!dvdnavsrc_is_open (src)) {
        if (!dvdnavsrc_open (src)) {
          return GST_STATE_FAILURE;
        }
      }
      src->streaminfo = NULL;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (dvdnavsrc_is_open (src)) {
        if (!dvdnavsrc_close (src)) {
          return GST_STATE_FAILURE;
        }
      }
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static const GstEventMask *
dvdnavsrc_get_event_mask (GstPad *pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK,         GST_SEEK_METHOD_SET | 
	                     GST_SEEK_METHOD_CUR | 
	                     GST_SEEK_METHOD_END | 
		             GST_SEEK_FLAG_FLUSH },
                             /*
    {GST_EVENT_SEEK_SEGMENT, GST_SEEK_METHOD_SET | 
	                     GST_SEEK_METHOD_CUR | 
	                     GST_SEEK_METHOD_END | 
		             GST_SEEK_FLAG_FLUSH },
                             */
    {0,}
  };

  return masks;
}

static gboolean
dvdnavsrc_event (GstPad *pad, GstEvent *event)
{
  DVDNavSrc *src;
  gboolean res = TRUE;

  src = DVDNAVSRC (gst_pad_get_parent (pad));

  if (!GST_FLAG_IS_SET (src, DVDNAVSRC_OPEN))
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
	    
      format    = GST_EVENT_SEEK_FORMAT (event);
      offset    = GST_EVENT_SEEK_OFFSET (event);

      switch (format) {
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
            if (dvdnav_sector_search(src->dvdnav, offset, origin) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
          } else if (format == title_format) {
            if (dvdnav_current_title_info(src->dvdnav, &title, &part) !=
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
                if (dvdnav_get_number_of_titles(src->dvdnav, &titles) !=
                    DVDNAV_STATUS_OK) {
                  goto error;
                }
                new_title = titles + offset;
                break;
              default:
                goto error;
            }
            if (dvdnav_title_play(src->dvdnav, new_title) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
          } else if (format == chapter_format) {
            if (dvdnav_current_title_info(src->dvdnav, &title, &part) !=
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
                if (dvdnav_get_number_of_titles(src->dvdnav, &parts) !=
                    DVDNAV_STATUS_OK) {
                  goto error;
                }
                new_part = parts + offset;
                break;
              default:
                goto error;
            }
            /*if (dvdnav_part_search(src->dvdnav, new_part) !=*/
            if (dvdnav_part_play(src->dvdnav, title, new_part) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
          } else if (format == angle_format) {
            if (dvdnav_get_angle_info(src->dvdnav, &angle, &angles) !=
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
            if (dvdnav_angle_change(src->dvdnav, new_angle) !=
                DVDNAV_STATUS_OK) {
              goto error;
            }
          } else {
            goto error;
          }
      }
      src->did_seek = TRUE;
      src->need_flush = GST_EVENT_SEEK_FLAGS(event) & GST_SEEK_FLAG_FLUSH;
      break;
    }
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
dvdnavsrc_get_formats (GstPad *pad)
{
  int i;
  static GstFormat formats[] = {
    /*
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    */
    0,	/* filled later */
    0,	/* filled later */
    0,	/* filled later */
    0,	/* filled later */
    0
  };
  static gboolean format_initialized = FALSE;

  if (!format_initialized) {
    for (i=0; formats[i] != 0; i++) {
    }
    formats[i++] = sector_format;
    formats[i++] = title_format;
    formats[i++] = chapter_format;
    formats[i++] = angle_format;
    format_initialized = TRUE;
  }

  return formats;
}

#if 0
static gboolean
dvdnavsrc_convert (GstPad *pad,
		    GstFormat src_format, gint64 src_value, 
		    GstFormat *dest_format, gint64 *dest_value)
{
  DVDNavSrc *src;

  src = DVDNAVSRC (gst_pad_get_parent (pad));

  if (!GST_FLAG_IS_SET (src, DVDNAVSRC_OPEN))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          src_value <<= 2;	/* 4 bytes per sample */
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value * 44100 / GST_SECOND;
	  break;
	default:
          if (*dest_format == track_format || *dest_format == sector_format) {
	    gint sector = (src_value * 44100) / ((CD_FRAMESIZE_RAW >> 2) * GST_SECOND);

	    if (*dest_format == sector_format) {
	      *dest_value = sector;
	    }
	    else {
	      *dest_value = cdda_sector_gettrack (src->d, sector) - 1;
	    }
	  }
          else 
	    return FALSE;
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
          *dest_value = src_value * GST_SECOND / 44100;
	  break;
	default:
          if (*dest_format == track_format || *dest_format == sector_format) {
            gint sector = src_value / (CD_FRAMESIZE_RAW >> 2);

            if (*dest_format == track_format) {
	      *dest_value = cdda_sector_gettrack (src->d, sector) - 1;
	    }
	    else {
	      *dest_value = sector;
	    }
	  }
          else 
	    return FALSE;
	  break;
      }
      break;
    default:
    {
      gint sector;

      if (src_format == track_format) {
	/* some sanity checks */
	if (src_value < 0 || src_value > src->d->tracks)
          return FALSE;

	sector = cdda_track_firstsector (src->d, src_value + 1);
      }
      else if (src_format == sector_format) {
	sector = src_value;
      }
      else
        return FALSE;

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
	  }
	  else if (*dest_format == track_format) {
	    /* if we go past the last sector, make sure to report the last track */
	    if (sector > src->last_sector)
	      *dest_value = cdda_sector_gettrack (src->d, src->last_sector);
	    else 
	      *dest_value = cdda_sector_gettrack (src->d, sector) - 1;
	  }
          else 
            return FALSE;
	  break;
      }
      break;
    }
  }

  return TRUE;
}
#endif

static const GstQueryType*
dvdnavsrc_get_query_types (GstPad *pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return src_query_types;
}

static gboolean
dvdnavsrc_query (GstPad *pad, GstQueryType type,
		  GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  DVDNavSrc *src;
  int titles, title;
  int parts, part;
  int angles, angle;
  unsigned int pos, len;

  src = DVDNAVSRC (gst_pad_get_parent (pad));

  if (!GST_FLAG_IS_SET (src, DVDNAVSRC_OPEN))
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      if (*format == sector_format) {
        if (dvdnav_get_position(src->dvdnav, &pos, &len) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = len;
      } else if (*format == title_format) {
        if (dvdnav_get_number_of_titles(src->dvdnav, &titles) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = titles;
      } else if (*format == chapter_format) {
        if (dvdnav_get_number_of_titles(src->dvdnav, &parts) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = parts;
      } else if (*format == angle_format) {
        if (dvdnav_get_angle_info(src->dvdnav, &angle, &angles) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = angles;
      } else {
        res = FALSE;
      }
      break;
    case GST_QUERY_POSITION:
      if (*format == sector_format) {
        if (dvdnav_get_position(src->dvdnav, &pos, &len) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = pos;
      } else if (*format == title_format) {
        if (dvdnav_current_title_info(src->dvdnav, &title, &part) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = title;
      } else if (*format == chapter_format) {
        if (dvdnav_current_title_info(src->dvdnav, &title, &part) != DVDNAV_STATUS_OK) {
          res = FALSE;
        }
        *value = part;
      } else if (*format == angle_format) {
        if (dvdnav_get_angle_info(src->dvdnav, &angle, &angles) != DVDNAV_STATUS_OK) {
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

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the dvdnavsrc element */
  factory = gst_element_factory_new ("dvdnavsrc", GST_TYPE_DVDNAVSRC,
                                    &dvdnavsrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dvdnavsrc",
  plugin_init
};
