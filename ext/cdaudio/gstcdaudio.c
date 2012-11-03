/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <cdaudio.h>

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (gst_cdaudio_debug);
#define GST_CAT_DEFAULT gst_cdaudio_debug

#define GST_TYPE_CDAUDIO                (gst_cdaudio_get_type())
#define GST_CDAUDIO(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDAUDIO,GstCDAudio))
#define GST_CDAUDIO_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDAUDIO,GstCDAudioClass))
#define GST_IS_CDAUDIO(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDAUDIO))
#define GST_IS_CDAUDIO_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDAUDIO))

typedef struct _GstCDAudio GstCDAudio;
typedef struct _GstCDAudioClass GstCDAudioClass;

struct _GstCDAudio
{
  GstElement parent;

  /* properties */
  gchar *device;
  gchar *uri;

  gint cd_desc;
  gulong discid;

  gboolean was_playing;

  struct disc_info info;
  struct disc_volume volume;

  GTimer *timer;
};

struct _GstCDAudioClass
{
  GstElementClass parent_class;

  void (*close_tray) (GstElement * element);
  /* signal callbacks */
  void (*track_change) (GstElement * element, guint track);
};

#define DEFAULT_DEVICE     "/dev/cdrom"
#define DEFAULT_VOLUME_FR  255
#define DEFAULT_VOLUME_FL  255
#define DEFAULT_VOLUME_BR  255
#define DEFAULT_VOLUME_BL  255

/* props */
enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_VOLUME_FR,
  PROP_VOLUME_FL,
  PROP_VOLUME_BR,
  PROP_VOLUME_BL,
  PROP_LAST
};

/* signals */
enum
{
  TRACK_CHANGE,
  LAST_SIGNAL
};

static void gst_cdaudio_finalize (GObject * object);

static void gst_cdaudio_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_cdaudio_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstStateChangeReturn gst_cdaudio_change_state (GstElement * element,
    GstStateChange transition);

static const GstQueryType *gst_cdaudio_get_query_types (GstElement * element);
static gboolean gst_cdaudio_query (GstElement * element, GstQuery * query);

static gboolean gst_cdaudio_send_event (GstElement * element, GstEvent * event);

static void cdaudio_uri_handler_init (gpointer g_iface, gpointer iface_data);

static GstFormat track_format;
static GstFormat sector_format;

static GstElementClass *parent_class;
static guint gst_cdaudio_signals[LAST_SIGNAL] = { 0 };

static void
_do_init (GType cdaudio_type)
{
  static const GInterfaceInfo urihandler_info = {
    cdaudio_uri_handler_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (cdaudio_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
}

GType gst_cdaudio_get_type (void);
GST_BOILERPLATE_FULL (GstCDAudio, gst_cdaudio, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_cdaudio_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class, "CD player",
      "Generic/Bin",
      "Play CD audio through the CD Drive", "Wim Taymans <wim@fluendo.com>");

  /* Register the track and sector format */
  track_format = gst_format_register ("track", "CD track");
  sector_format = gst_format_register ("sector", "CD sector");
}

static void
gst_cdaudio_class_init (GstCDAudioClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_cdaudio_set_property;
  gobject_klass->get_property = gst_cdaudio_get_property;

  g_object_class_install_property (gobject_klass, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "CDROM device",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_VOLUME_FL,
      g_param_spec_int ("volume-fl", "Volume fl", "Front left volume",
          0, 255, 255, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_VOLUME_FR,
      g_param_spec_int ("volume-fr", "Volume fr", "Front right volume",
          0, 255, 255, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_VOLUME_BL,
      g_param_spec_int ("volume-bl", "Volume bl", "Back left volume",
          0, 255, 255, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_VOLUME_BR,
      g_param_spec_int ("volume-br", "Volume br", "Back right volume",
          0, 255, 255, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_cdaudio_signals[TRACK_CHANGE] =
      g_signal_new ("track-change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstCDAudioClass, track_change), NULL,
      NULL, gst_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_cdaudio_finalize);

  gstelement_klass->change_state = GST_DEBUG_FUNCPTR (gst_cdaudio_change_state);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_cdaudio_send_event);
  gstelement_klass->get_query_types =
      GST_DEBUG_FUNCPTR (gst_cdaudio_get_query_types);
  gstelement_klass->query = GST_DEBUG_FUNCPTR (gst_cdaudio_query);

  GST_DEBUG_CATEGORY_INIT (gst_cdaudio_debug, "cdaudio", 0, "CDAudio Element");
}

static void
gst_cdaudio_init (GstCDAudio * cdaudio, GstCDAudioClass * g_class)
{
  cdaudio->device = g_strdup (DEFAULT_DEVICE);
  cdaudio->volume.vol_front.right = DEFAULT_VOLUME_FR;
  cdaudio->volume.vol_front.left = DEFAULT_VOLUME_FL;
  cdaudio->volume.vol_back.right = DEFAULT_VOLUME_BR;
  cdaudio->volume.vol_back.left = DEFAULT_VOLUME_BL;

  cdaudio->was_playing = FALSE;
  cdaudio->timer = g_timer_new ();

  GST_OBJECT_FLAG_SET (cdaudio, GST_ELEMENT_IS_SINK);
}

static void
gst_cdaudio_finalize (GObject * object)
{
  GstCDAudio *cdaudio = GST_CDAUDIO (object);

  g_timer_destroy (cdaudio->timer);
  g_free (cdaudio->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cdaudio_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * spec)
{
  GstCDAudio *cdaudio;

  cdaudio = GST_CDAUDIO (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (cdaudio->device);
      cdaudio->device = g_value_dup_string (value);
      break;
    case PROP_VOLUME_FR:
      cdaudio->volume.vol_front.right = g_value_get_int (value);
      break;
    case PROP_VOLUME_FL:
      cdaudio->volume.vol_front.left = g_value_get_int (value);
      break;
    case PROP_VOLUME_BR:
      cdaudio->volume.vol_back.right = g_value_get_int (value);
      break;
    case PROP_VOLUME_BL:
      cdaudio->volume.vol_back.left = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_cdaudio_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * spec)
{
  GstCDAudio *cdaudio;

  cdaudio = GST_CDAUDIO (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, cdaudio->device);
      break;
    case PROP_VOLUME_FR:
      g_value_set_int (value, cdaudio->volume.vol_front.right);
      break;
    case PROP_VOLUME_FL:
      g_value_set_int (value, cdaudio->volume.vol_front.left);
      break;
    case PROP_VOLUME_BR:
      g_value_set_int (value, cdaudio->volume.vol_back.right);
      break;
    case PROP_VOLUME_BL:
      g_value_set_int (value, cdaudio->volume.vol_back.left);
      break;
    default:
      break;
  }
}

static void
debug_track_info (GstCDAudio * cdaudio)
{
  gint i;

  for (i = 0; i < cdaudio->info.disc_total_tracks; i++) {
    GST_DEBUG_OBJECT (cdaudio, "%d %d %d %d:%02d", i,
        cdaudio->info.disc_track[i].track_length.frames,
        cdaudio->info.disc_track[i].track_pos.frames,
        cdaudio->info.disc_track[i].track_length.minutes,
        cdaudio->info.disc_track[i].track_length.seconds);
  }
}

static GstStateChangeReturn
gst_cdaudio_change_state (GstElement * element, GstStateChange transition)
{
  GstCDAudio *cdaudio;
  GstStateChangeReturn ret;
  gint res;

  cdaudio = GST_CDAUDIO (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if ((res = cd_init_device (cdaudio->device)) < 0)
        goto init_failed;

      cdaudio->cd_desc = res;

      /* close tray */
      if ((res = cd_close (cdaudio->cd_desc)) < 0)
        goto close_failed;

      if ((res = cd_stat (cdaudio->cd_desc, &cdaudio->info)) < 0) {
        /* we just give a warning here */
        GST_ELEMENT_WARNING (cdaudio, LIBRARY, INIT,
            ("Could not retrieve CD track info."), (NULL));
      } else {
        debug_track_info (cdaudio);
        cdaudio->discid = cddb_discid (cdaudio->cd_desc);
        /* FIXME, post message with discid */
      }
      cdaudio->was_playing = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      if (cdaudio->was_playing)
        res = cd_resume (cdaudio->cd_desc);
      else
        res = cd_play (cdaudio->cd_desc, 1);

      if (res < 0)
        goto play_failed;

      cdaudio->was_playing = TRUE;
      g_timer_start (cdaudio->timer);
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if ((res = cd_pause (cdaudio->cd_desc)) < 0)
        goto pause_failed;
      g_timer_stop (cdaudio->timer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if ((res = cd_stop (cdaudio->cd_desc)) < 0)
        goto stop_failed;
      if ((res = cd_finish (cdaudio->cd_desc)) < 0)
        goto finish_failed;
      cdaudio->cd_desc = -1;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (cdaudio, LIBRARY, INIT,
        ("Could not init CD device %s. (%d)", cdaudio->device, res), (NULL));
    cdaudio->cd_desc = -1;
    return GST_STATE_CHANGE_FAILURE;
  }
close_failed:
  {
    GST_ELEMENT_ERROR (cdaudio, LIBRARY, INIT,
        ("Could not close CD tray for device %s. (%d)", cdaudio->device, res),
        (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
play_failed:
  {
    GST_ELEMENT_ERROR (cdaudio, LIBRARY, INIT,
        ("Could not play CD device %s. (%d)", cdaudio->device, res), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
pause_failed:
  {
    GST_ELEMENT_ERROR (cdaudio, LIBRARY, INIT,
        ("Could not pause CD device %s. (%d)", cdaudio->device, res), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
stop_failed:
  {
    GST_ELEMENT_ERROR (cdaudio, LIBRARY, INIT,
        ("Could not stop CD device %s. (%d)", cdaudio->device, res), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
finish_failed:
  {
    GST_ELEMENT_ERROR (cdaudio, LIBRARY, INIT,
        ("Could not finish CD device %s. (%d)", cdaudio->device, res), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static gboolean
gst_cdaudio_send_event (GstElement * element, GstEvent * event)
{
  GstCDAudio *cdaudio;
  gboolean res = TRUE;

  cdaudio = GST_CDAUDIO (element);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      gint ret;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      /* FIXME, implement more formats */
      if (format != GST_FORMAT_TIME)
        goto wrong_format;

      if (rate != 1.0)
        goto wrong_rate;

      if (start_type != GST_SEEK_TYPE_SET)
        goto unsupported;

      ret = cd_play_pos (cdaudio->cd_desc, 1, start / GST_SECOND);
      if (ret < 0)
        goto seek_failed;
    }
    default:
      res = FALSE;
      break;
  }

done:
  gst_event_unref (event);

  return res;

  /* ERRORS */
wrong_format:
  {
    GST_DEBUG_OBJECT (cdaudio, "only seek in TIME is supported");
    res = FALSE;
    goto done;
  }
wrong_rate:
  {
    GST_DEBUG_OBJECT (cdaudio, "only seek with 1.0 rate is supported");
    res = FALSE;
    goto done;
  }
unsupported:
  {
    GST_DEBUG_OBJECT (cdaudio, "only seek SET is supported");
    res = FALSE;
    goto done;
  }
seek_failed:
  {
    GST_DEBUG_OBJECT (cdaudio, "seek failed");
    res = FALSE;
    goto done;
  }
}

static const GstQueryType *
gst_cdaudio_get_query_types (GstElement * element)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    0
  };

  return query_types;
}

static gboolean
gst_cdaudio_query (GstElement * element, GstQuery * query)
{
  GstCDAudio *cdaudio;
  gboolean res = TRUE;
  gulong micros;
  gdouble seconds;

  cdaudio = GST_CDAUDIO (element);

  GST_LOG_OBJECT (element, "handling %s query",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  /* take new snapshot every 1000 miliseconds */
  seconds = g_timer_elapsed (cdaudio->timer, &micros);
  if (micros > 1000 || seconds > 1) {
    cd_stat (cdaudio->cd_desc, &cdaudio->info);
    g_timer_start (cdaudio->timer);
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat dest_format;
      gint64 dest_val;

      gst_query_parse_duration (query, &dest_format, NULL);

      switch (dest_format) {
        case GST_FORMAT_TIME:
          dest_val = (cdaudio->info.disc_length.minutes * 60 +
              cdaudio->info.disc_length.seconds) * GST_SECOND;
          break;
        default:
        {
          if (dest_format == track_format) {
            dest_val = cdaudio->info.disc_total_tracks;
          } else {
            res = FALSE;
          }
          break;
        }
      }
      if (res)
        gst_query_set_duration (query, dest_format, dest_val);
      break;
    }
    case GST_QUERY_POSITION:
    {
      GstFormat dest_format;
      gint64 dest_val;

      gst_query_parse_position (query, &dest_format, NULL);

      switch (dest_format) {
        case GST_FORMAT_TIME:
          dest_val = (cdaudio->info.disc_time.minutes * 60 +
              cdaudio->info.disc_time.seconds) * GST_SECOND;
          break;
        default:
        {
          if (dest_format == track_format) {
            dest_val = cdaudio->info.disc_current_track;
          } else {
            res = FALSE;
          }
          break;
        }
      }
      if (res)
        gst_query_set_position (query, dest_format, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cdaudio", GST_RANK_NONE,
          GST_TYPE_CDAUDIO))
    return FALSE;

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
cdaudio_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
cdaudio_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "cd", NULL };

  return protocols;
}

static const gchar *
cdaudio_uri_get_uri (GstURIHandler * handler)
{
  GstCDAudio *cdaudio = GST_CDAUDIO (handler);

  return cdaudio->uri;
}

static gboolean
cdaudio_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;
  gboolean ret;

  ret = TRUE;

  //GstCDAudio *cdaudio = GST_CDAUDIO(handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "cd") != 0)
    goto wrong_protocol;

  g_free (protocol);

  location = gst_uri_get_location (uri);
  /*
     cdaudio->uri_track = strtol(location,NULL,10);
     if (cdaudio->uri_track > 0) {
     cdaudio->seek_request = cdaudio->uri_track;
     }
   */
  g_free (location);

  return ret;

  /* ERRORS */
wrong_protocol:
  {
    g_free (protocol);
    return FALSE;
  }
}

static void
cdaudio_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = cdaudio_uri_get_type;
  iface->get_protocols = cdaudio_uri_get_protocols;
  iface->get_uri = cdaudio_uri_get_uri;
  iface->set_uri = cdaudio_uri_set_uri;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    cdaudio,
    "Play CD audio through the CD Drive",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
