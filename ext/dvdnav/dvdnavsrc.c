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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>

#include <dvdnavsrc.h>

#include "config.h"

#include <dvdnav/dvdnav.h>

struct _DVDNavSrcPrivate {
  GstElement element;
  /* pads */
  GstPad *srcpad;

  /* location */
  gchar *location;

  gboolean new_seek;

  int title, chapter, angle;
  dvdnav_t *dvdnav;
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


static void 		dvdnavsrc_class_init	(DVDNavSrcClass *klass);
static void 		dvdnavsrc_init		(DVDNavSrc *dvdnavsrc);

static void 		dvdnavsrc_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void 		dvdnavsrc_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/*static GstBuffer *	dvdnavsrc_get		(GstPad *pad); */
static void     	dvdnavsrc_loop		(GstElement *element);
/*static GstBuffer *	dvdnavsrc_get_region	(GstPad *pad,gulong offset,gulong size); */

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
  dvdnavsrc->priv = g_new(DVDNavSrcPrivate, 1);
  dvdnavsrc->priv->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (dvdnavsrc), dvdnavsrc->priv->srcpad);
  gst_element_set_loop_function (GST_ELEMENT(dvdnavsrc), GST_DEBUG_FUNCPTR(dvdnavsrc_loop));

  dvdnavsrc->priv->location = g_strdup("/dev/dvd");
  dvdnavsrc->priv->new_seek = FALSE;
  dvdnavsrc->priv->title = 1;
  dvdnavsrc->priv->chapter = 1;
  dvdnavsrc->priv->angle = 1;
}

/* FIXME: this code is not being used */
#ifdef PLEASEFIXTHISCODE
static void
dvdnavsrc_destroy (DVDNavSrc *dvdnavsrc)
{
  /* FIXME */
  g_print("FIXME\n");
  g_free(dvdnavsrc->priv);
}
#endif

static void 
dvdnavsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  DVDNavSrc *src;
  DVDNavSrcPrivate *priv;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDNAVSRC (object));
  
  src = DVDNAVSRC (object);
  priv = src->priv;

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
      /*g_return_if_fail(!GST_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      if (priv->location)
        g_free (priv->location);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        priv->location = g_strdup("/dev/dvd");
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
dvdnavsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  DVDNavSrc *src;
  DVDNavSrcPrivate *priv;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDNAVSRC (object));
  
  src = DVDNAVSRC (object);
  priv = src->priv;

  switch (prop_id) {
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

static int
_open(DVDNavSrcPrivate *priv, const gchar *location)
{
  g_return_val_if_fail(priv != NULL, -1);
  g_return_val_if_fail(location != NULL, -1);

  if (dvdnav_open ( &priv->dvdnav, (char*)location ) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_open error: %s location: %s\n", dvdnav_err_to_string(priv->dvdnav), location);
    return -1;
  }

  return 0;
}

static int
_close(DVDNavSrcPrivate *priv)
{
  g_return_val_if_fail(priv != NULL, -1);
  g_return_val_if_fail(priv->dvdnav != NULL, -1);

  if (dvdnav_close ( priv->dvdnav ) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_close error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  return 0;
}

static int
_seek(DVDNavSrcPrivate *priv, int title, int chapter, int angle)
{
  int titles, programs, curangle, angles;

  g_return_val_if_fail(priv != NULL, -1);
  g_return_val_if_fail(priv->dvdnav != NULL, -1);

 /**
  * Make sure our title number is valid.
  */
  if (dvdnav_get_number_of_titles (priv->dvdnav, &titles) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_get_number_of_titles error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  fprintf (stderr, "There are %d titles on this DVD.\n", titles);
  if (title < 0 || title >= titles) {
    fprintf (stderr, "Invalid title %d.\n", title + 1);
    _close (priv);
    return -1;
  }

  /**
   * Make sure the chapter number is valid for this title.
   */
  if (dvdnav_get_number_of_programs (priv->dvdnav, &programs) != DVDNAV_STATUS_OK) {
    fprintf( stderr, "dvdnav_get_number_of_programs error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  fprintf (stderr, "There are %d chapters in this title.\n", programs);
  if (chapter < 0 || chapter >= programs) {
    fprintf (stderr, "Invalid chapter %d\n", chapter + 1);
    _close (priv);
    return -1;
  }

  /**
   * Make sure the angle number is valid for this title.
   */
  if (dvdnav_get_angle_info (priv->dvdnav, &curangle, &angles) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_get_angle_info error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  fprintf (stderr, "There are %d angles in this title.\n", angles);
  if( angle < 0 || angle >= angles) {
    fprintf (stderr, "Invalid angle %d\n", angle + 1);
    _close (priv);
    return -1;
  }

  /**
   * We've got enough info, time to open the title set data.
   */
  if (dvdnav_part_play (priv->dvdnav, title, chapter) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_part_play error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  if (dvdnav_angle_change (priv->dvdnav, angle) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_angle_change error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }

  /*
  if (dvdnav_physical_audio_stream_change (priv->dvdnav, 0) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_physical_audio_stream_change error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  if (dvdnav_logical_audio_stream_change (priv->dvdnav, 0) != DVDNAV_STATUS_OK) {
    fprintf (stderr, "dvdnav_logical_audio_stream_change error: %s\n", dvdnav_err_to_string(priv->dvdnav));
    return -1;
  }
  */

  return 0;
}

static void
dvdnavsrc_loop (GstElement *element) 
{
  DVDNavSrc *dvdnavsrc;
  DVDNavSrcPrivate *priv;
  int done;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_DVDNAVSRC (element));

  dvdnavsrc = DVDNAVSRC (element);
  priv = dvdnavsrc->priv;
  g_return_if_fail (GST_FLAG_IS_SET (dvdnavsrc, DVDNAVSRC_OPEN));

  done = 0;

  while (!done) {
    int event, len;
    GstBuffer *buf;
    unsigned char *data;

    /* allocate the space for the buffer data */
    data = g_malloc (DVD_VIDEO_LB_LEN);

    if (dvdnav_get_next_block (priv->dvdnav, data, &event, &len) != DVDNAV_STATUS_OK) {
      fprintf (stderr, "dvdnav_get_next_block error: %s\n", dvdnav_err_to_string(priv->dvdnav));
      return;
    }

    switch (event) {
      case DVDNAV_BLOCK_OK:
        /* create the buffer */
        /* FIXME: should eventually use a bufferpool for this */
        buf = gst_buffer_new ();
        g_return_if_fail (buf);

        GST_BUFFER_DATA (buf) = data;
        GST_BUFFER_SIZE (buf) = DVD_VIDEO_LB_LEN;
        gst_pad_push(priv->srcpad, buf);
        break;
      case DVDNAV_STOP:
        done = 1;
        gst_element_set_eos (GST_ELEMENT (dvdnavsrc));
        _close(priv);
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
  DVDNavSrc *dvdnavsrc;
  DVDNavSrcPrivate *priv;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  dvdnavsrc = DVDNAVSRC (gst_pad_get_parent (pad));
  priv = dvdnavsrc->priv;
  g_return_val_if_fail (GST_FLAG_IS_SET (dvdnavsrc, DVDNAVSRC_OPEN),NULL);

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA (buf) = g_malloc (1024 * DVD_VIDEO_LB_LEN);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  if (priv->new_seek) {
    _seek(priv, priv->titleid, priv->chapid, priv->angle);
  }

  /* read it in from the file */
  if (_read (priv, priv->angle, priv->new_seek, buf)) {
    gst_element_signal_eos (GST_ELEMENT (dvdnavsrc));
    return NULL;
  }

  if (priv->new_seek) {
    priv->new_seek = FALSE;
  }

  return buf;
}
#endif

/* open the file, necessary to go to RUNNING state */
static gboolean 
dvdnavsrc_open_file (DVDNavSrc *src) 
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDNAVSRC(src), FALSE);
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, DVDNAVSRC_OPEN), FALSE);

  if (_open(src->priv, src->priv->location))
    return FALSE;
  if (_seek(src->priv,
      src->priv->title,
      src->priv->chapter,
      src->priv->angle))
    return FALSE;

  GST_FLAG_SET (src, DVDNAVSRC_OPEN);
  
  return TRUE;
}

/* close the file */
static void 
dvdnavsrc_close_file (DVDNavSrc *src) 
{
  g_return_if_fail (GST_FLAG_IS_SET (src, DVDNAVSRC_OPEN));

  _close(src->priv);

  GST_FLAG_UNSET (src, DVDNAVSRC_OPEN);
}

static GstElementStateReturn
dvdnavsrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_DVDNAVSRC (element), GST_STATE_FAILURE);

  GST_DEBUG (0,"gstdvdnavsrc: state pending %d", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, DVDNAVSRC_OPEN))
      dvdnavsrc_close_file (DVDNAVSRC (element));
  /* otherwise (READY or higher) we need to open the file */
  } else {
    if (!GST_FLAG_IS_SET (element, DVDNAVSRC_OPEN)) {
      if (!dvdnavsrc_open_file (DVDNAVSRC (element)))
        return GST_STATE_FAILURE;
    }
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
