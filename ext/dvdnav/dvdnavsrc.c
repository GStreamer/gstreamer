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

  /* location */
  gchar *location;
  gboolean new_seek;
  GstBufferPool *bufferpool;

  int title, chapter, angle;
  dvdnav_t *dvdnav;
};

struct _DVDNavSrcClass {
  GstElementClass parent_class;
};

GstElementDetails dvdnavsrc_details = {
  "DVD Source",
  "Source/File/DVD",
  "Access a DVD with navigation features using libdvdnav",
  VERSION,
  "David I. Lehn <dlehn@users.sourceforge.net>",
  "(C) 2002",
};


/* DVDNavSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
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

/*static GstBuffer *	dvdnavsrc_get		(GstPad *pad); */
static void     	dvdnavsrc_loop		(GstElement *element);
/*static GstBuffer *	dvdnavsrc_get_region	(GstPad *pad,gulong offset,gulong size); */
//static void		dvdnavsrc_event (GstPad *pad, GstEvent *event);
static gboolean     	dvdnavsrc_close		(DVDNavSrc *src);
static gboolean     	dvdnavsrc_open		(DVDNavSrc *src);

static GstElementStateReturn 	dvdnavsrc_change_state 	(GstElement *element);


static GstElementClass *parent_class = NULL;
/*static guint dvdnavsrc_signals[LAST_SIGNAL] = { 0 }; */

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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOCATION,
    g_param_spec_string("location","location","location",
                        NULL, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TITLE,
    g_param_spec_int("title","title","title",
                     1,99,1,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHAPTER,
    g_param_spec_int("chapter","chapter","chapter",
                     1,999,1,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ANGLE,
    g_param_spec_int("angle","angle","angle",
                     1,9,1,G_PARAM_READWRITE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR(dvdnavsrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(dvdnavsrc_get_property);

  gstelement_class->change_state = dvdnavsrc_change_state;
}

static void 
dvdnavsrc_init (DVDNavSrc *dvdnavsrc) 
{
  gst_element_set_loop_function (GST_ELEMENT(dvdnavsrc), GST_DEBUG_FUNCPTR(dvdnavsrc_loop));
  dvdnavsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (dvdnavsrc), dvdnavsrc->srcpad);
  //gst_pad_set_event_function (dvdnavsrc->srcpad, dvdnavsrc_event);

  dvdnavsrc->bufferpool = gst_buffer_pool_get_default (DVD_VIDEO_LB_LEN, 2);

  dvdnavsrc->location = g_strdup("/dev/dvd");
  dvdnavsrc->new_seek = FALSE;
  dvdnavsrc->title = 1;
  dvdnavsrc->chapter = 1;
  dvdnavsrc->angle = 1;
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
      src->new_seek = TRUE;
      break;
    case ARG_CHAPTER:
      src->chapter = g_value_get_int (value);
      src->new_seek = TRUE;
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

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDNAVSRC (object));
  
  src = DVDNAVSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->location);
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
  if (dvdnav_get_number_of_programs (src->dvdnav, &programs) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_get_number_of_programs error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
  }
  fprintf (stderr, "There are %d chapters in this title.\n", programs);
  if (chapter < 1 || chapter > programs) {
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
  if (dvdnav_part_play (src->dvdnav, title, chapter) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_part_play error: %s\n", dvdnav_err_to_string(src->dvdnav));
    return FALSE;
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

  return TRUE;
}

/*
static void
dvdnavsrc_event (GstPad *pad, GstElement *element)
{
}
*/

#if 0
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
    case DVDNAV_SEEK_DONE: return "DVDNAV_SEEK_DONE"; break;
    case DVDNAV_HOP_CHANNEL: return "DVDNAV_HOP_CHANNEL"; break;
  }
  return "UNKNOWN";
}
#endif

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
dvdnavsrc_loop (GstElement *element) 
{
  DVDNavSrc *src;
  int done;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_DVDNAVSRC (element));

  src = DVDNAVSRC (element);
  g_return_if_fail (dvdnavsrc_is_open (src));

  done = 0;

  while (!done) {
    int event, len;
    GstBuffer *buf;
    guint8 *data;

    /* allocate a pool for the buffer data */
    /* FIXME: mem leak on non BLOCK_OK events */
    buf = gst_buffer_new_from_pool (src->bufferpool, DVD_VIDEO_LB_LEN, 0);
    if (!buf) {
      gst_element_error (GST_ELEMENT(src),
          "Failed to create a new GstBuffer");
      return;
    }
    data = GST_BUFFER_DATA(buf);

    if (dvdnav_get_next_block (src->dvdnav, data, &event, &len) != DVDNAV_STATUS_OK) {
      fprintf (stderr, "dvdnav_get_next_block error: %s\n", dvdnav_err_to_string(src->dvdnav));
      return;
    }

    switch (event) {
      case DVDNAV_BLOCK_OK:
        g_return_if_fail (GST_BUFFER_DATA(buf) != NULL);
        g_return_if_fail (GST_BUFFER_SIZE(buf) == DVD_VIDEO_LB_LEN);
        gst_pad_push (src->srcpad, buf);
        break;
      case DVDNAV_NOP:
        printf("dvdnav: received NOP event\n");
        break;
      case DVDNAV_STILL_FRAME:
        /* FIXME: we should pause for event->length seconds before dvdnav_still_skip */
        {
          dvdnav_still_event_t *event = (dvdnav_still_event_t *)data;
          fprintf (stderr, "dvdnav: still frame: %d seconds\n", event->length);
          if (dvdnav_still_skip (src->dvdnav) != DVDNAV_STATUS_OK) {
            fprintf (stderr, "dvdnav_still_skip error: %s\n", dvdnav_err_to_string(src->dvdnav));
            /* FIXME: close the stream??? */
          }
        }
        break;
      case DVDNAV_SPU_STREAM_CHANGE:
        {
          dvdnav_spu_stream_change_event_t * event = (dvdnav_spu_stream_change_event_t *)data;
          fprintf (stderr, "dvdnav: spu_stream_change:\n");
          fprintf (stderr, "  physical_wide: %d\n", event->physical_wide);
          fprintf (stderr, "  physical_letterbox: %d\n", event->physical_letterbox);
          fprintf (stderr, "  physical_pan_scan: %d\n", event->physical_pan_scan);
          fprintf (stderr, "  logical: %d\n", event->logical);
        }
        break;
      case DVDNAV_AUDIO_STREAM_CHANGE:
        {
          dvdnav_audio_stream_change_event_t * event = (dvdnav_audio_stream_change_event_t *)data;
          fprintf (stderr, "dvdnav: audio_stream_change:\n");
          fprintf (stderr, "  physical: %d\n", event->physical);
          fprintf (stderr, "  logical: %d\n", event->logical);
        }
        break;
      case DVDNAV_VTS_CHANGE:
        {
          dvdnav_vts_change_event_t *event = (dvdnav_vts_change_event_t *)data;
          fprintf (stderr, "dvdnav: vts_change\n");
          fprintf (stderr, "  old_vtsN: %d\n", event->old_vtsN);
          fprintf (stderr, "  old_domain: %s\n", dvdnav_get_read_domain_name(event->old_domain));
          fprintf (stderr, "  new_vtsN: %d\n", event->new_vtsN);
          fprintf (stderr, "  new_domain: %s\n", dvdnav_get_read_domain_name(event->new_domain));
        }
        break;
      case DVDNAV_CELL_CHANGE:
        {
          dvdnav_cell_change_event_t *event = (dvdnav_cell_change_event_t *)data;
          fprintf (stderr, "dvdnav: cell_change:\n");
          fprintf (stderr, "  old_cell: %p\n", event->old_cell);
          fprintf (stderr, "  new_cell: %p\n", event->new_cell);
        }
        break;
      case DVDNAV_NAV_PACKET:
        {
          dvdnav_nav_packet_event_t *event = (dvdnav_nav_packet_event_t *)data;
          fprintf (stderr, "dvdnav: nav_packet:\n");
          fprintf (stderr, "  pci: %p\n", event->pci);
          fprintf (stderr, "  dsi: %p\n", event->dsi);
        }
        break;
      case DVDNAV_STOP:
        done = 1;
        gst_element_set_eos (GST_ELEMENT (src));
        dvdnavsrc_close(src);
        break;
      case DVDNAV_HIGHLIGHT:
        {
          dvdnav_highlight_event_t *event = (dvdnav_highlight_event_t *)data;
          fprintf (stderr, "dvdnav: highlight:\n");
          fprintf (stderr, "  display: %s\n", 
              event->display == 0 ? "hide" : (event->display == 1 ? "show" : "unknown")
              );
          if (event->display == 1) {
            fprintf (stderr, "  palette: %08x\n", event->palette);
            fprintf (stderr, "  coords (%d, %d) - (%d, %d)\n", event->sx, event->sy, event->ex, event->ey);
            fprintf (stderr, "  pts: %d\n", event->pts);
            fprintf (stderr, "  button: %d\n", event->buttonN);
          }
        }
        break;
      case DVDNAV_SPU_CLUT_CHANGE:
        /* ignore the change events. I'm dont know what I'm meant to do with them */
        /* and there's no struct for it */
        fprintf (stderr, "dvdnav: spu_clut_change\n");
        break;
      case DVDNAV_SEEK_DONE:
        fprintf (stderr, "dvdnav: seek_done\n");
        break;
      case DVDNAV_HOP_CHANNEL:
        fprintf (stderr, "dvdnav: hop_channel\n");
        break;
      default:
        fprintf (stderr, "dvdnavsrc event: %d\n", event);
        break;
    }
  }
}

#if 0
  static GstBuffer *
dvdnavsrc_get (GstPad *pad) 
{
  DVDNavSrc *src;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  src = DVDNAVSRC (gst_pad_get_parent (pad));
  g_return_val_if_fail (gstdvdnav_is_open (src), NULL);

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA (buf) = g_malloc (1024 * DVD_VIDEO_LB_LEN);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  if (src->new_seek) {
    _seek(src, src->titleid, src->chapid, src->angle);
  }

  /* read it in from the file */
  if (_read (src, src->angle, src->new_seek, buf)) {
    gst_element_signal_eos (GST_ELEMENT (src));
    return NULL;
  }

  if (src->new_seek) {
    src->new_seek = FALSE;
  }

  return buf;
}
#endif

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

  if (!dvdnavsrc_tca_seek(src,
      src->title,
      src->chapter,
      src->angle))
    return FALSE;
  
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
