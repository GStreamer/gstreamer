/* gstcdplay
 * Copyright (c) 2002 Charles Schmidt <cbschmid@uiuc.edu> 
 
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
#include "gstcdplayer.h"

/* props */
enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_NUM_TRACKS,
  ARG_START_TRACK,
  ARG_END_TRACK,
  ARG_CURRENT_TRACK,
  ARG_CDDB_DISCID
};

/* signals */
enum
{
  TRACK_CHANGE,
  LAST_SIGNAL
};

static void cdplayer_base_init (gpointer g_class);
static void cdplayer_class_init (CDPlayerClass * klass);
static void cdplayer_init (CDPlayer * cdp);
static void cdplayer_dispose (GObject * object);

static void cdplayer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void cdplayer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static gboolean cdplayer_iterate (GstBin * bin);

static GstElementStateReturn cdplayer_change_state (GstElement * element);

static GstElementClass *parent_class;
static guint cdplayer_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails cdplayer_details = GST_ELEMENT_DETAILS ("CD Player",
    "Generic/Bin",
    "Play CD audio through the CD Drive",
    "Charles Schmidt <cbschmid@uiuc.edu>, "
    "Wim Taymans <wim.taymans@chello.be>");


GType
cdplayer_get_type (void)
{
  static GType cdplayer_type = 0;

  if (!cdplayer_type) {
    static const GTypeInfo cdplayer_info = {
      sizeof (CDPlayerClass),
      cdplayer_base_init,
      NULL,
      (GClassInitFunc) cdplayer_class_init,
      NULL,
      NULL,
      sizeof (CDPlayer),
      0,
      (GInstanceInitFunc) cdplayer_init,
      NULL
    };

    cdplayer_type =
        g_type_register_static (GST_TYPE_BIN, "CDPlayer", &cdplayer_info, 0);
  }

  return cdplayer_type;
}

static void
cdplayer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &cdplayer_details);
}
static void
cdplayer_class_init (CDPlayerClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (cdplayer_dispose);

  gstelement_klass->change_state = GST_DEBUG_FUNCPTR (cdplayer_change_state);
  gstbin_klass->iterate = GST_DEBUG_FUNCPTR (cdplayer_iterate);

  gobject_klass->set_property = cdplayer_set_property;
  gobject_klass->get_property = cdplayer_get_property;

  g_object_class_install_property (gobject_klass, ARG_DEVICE,
      g_param_spec_string ("device", "device", "CDROM device", NULL,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_NUM_TRACKS,
      g_param_spec_int ("num_tracks", "num_tracks", "Number of Tracks",
          G_MININT, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_START_TRACK,
      g_param_spec_int ("start_track", "start_track",
          "Track to start playback on", 1,
          CDPLAYER_MAX_TRACKS - 1, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_END_TRACK,
      g_param_spec_int ("end_track", "end_track",
          "Track to end playback on", 0,
          CDPLAYER_MAX_TRACKS - 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_CURRENT_TRACK,
      g_param_spec_int ("current_track", "current_track",
          "Current track playing", 1,
          CDPLAYER_MAX_TRACKS - 1, 1, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_CDDB_DISCID,
      g_param_spec_uint ("cddb_discid", "cddb_discid", "CDDB Disc ID",
          0, G_MAXUINT, 1, G_PARAM_READABLE));

  cdplayer_signals[TRACK_CHANGE] =
      g_signal_new ("track-change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (CDPlayerClass, track_change), NULL,
      NULL, gst_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  return;
}

static void
cdplayer_init (CDPlayer * cdp)
{
  cdp->device = g_strdup ("/dev/cdrom");
  cdp->num_tracks = -1;
  cdp->start_track = 1;
  cdp->end_track = 0;
  cdp->current_track = 1;

  cdp->paused = FALSE;

  GST_FLAG_SET (cdp, GST_BIN_FLAG_MANAGER);

  return;
}

static void
cdplayer_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * spec)
{
  CDPlayer *cdp;

  g_return_if_fail (GST_IS_CDPLAYER (object));

  cdp = CDPLAYER (object);

  switch (prop_id) {
    case ARG_DEVICE:
// FIXME prolly should uhh.. stop it or something
      if (cdp->device) {
        g_free (cdp->device);
      }

      cdp->device = g_strdup (g_value_get_string (value));
      break;
    case ARG_START_TRACK:
// FIXME prolly should uhh.. restart play, i guess... or something whatever
// FIXME we should only set current_track if its not playing...
      cdp->current_track = cdp->start_track = g_value_get_int (value);
      break;
    case ARG_END_TRACK:
// FIXME prolly should restart play, maybe, or try to set it without interrupt..
      cdp->end_track = g_value_get_int (value);
      break;
    default:
      break;
  }

  return;
}


static void
cdplayer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * spec)
{
  CDPlayer *cdp;

  g_return_if_fail (GST_IS_CDPLAYER (object));

  cdp = CDPLAYER (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, cdp->device);
      break;
    case ARG_NUM_TRACKS:
      g_value_set_int (value, cdp->num_tracks);
      break;
    case ARG_START_TRACK:
      g_value_set_int (value, cdp->start_track);
      break;
    case ARG_END_TRACK:
      g_value_set_int (value, cdp->end_track);
    case ARG_CURRENT_TRACK:
      g_value_set_int (value, cdp->current_track);
      break;
    case ARG_CDDB_DISCID:
      g_value_set_uint (value, cdp->cddb_discid);
    default:
      break;
  }

  return;
}

static void
cdplayer_dispose (GObject * object)
{
  CDPlayer *cdp;

  g_return_if_fail (GST_IS_CDPLAYER (object));

  cdp = CDPLAYER (object);
  g_free (cdp->device);

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }

  return;
}

static gboolean
cdplayer_iterate (GstBin * bin)
{
  CDPlayer *cdp = CDPLAYER (bin);
  gint current_track;

  switch (cd_status (CDPLAYER_CD (cdp))) {
    case CD_PLAYING:
      current_track = cd_current_track (CDPLAYER_CD (cdp));
      if (current_track > cdp->end_track && cdp->end_track != 0) {
        return FALSE;
      }

      if (current_track != -1 && current_track != cdp->current_track) {
        cdp->current_track = current_track;
        g_signal_emit (G_OBJECT (cdp), cdplayer_signals[TRACK_CHANGE], 0,
            cdp->current_track);
      }

      return TRUE;
      break;
    case CD_ERROR:
      gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
      return FALSE;
      break;
    case CD_COMPLETED:
      gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
      gst_element_set_eos (GST_ELEMENT (bin));
      return FALSE;
      break;
  }

  return FALSE;
}


static GstElementStateReturn
cdplayer_change_state (GstElement * element)
{
  CDPlayer *cdp;
  GstElementState state = GST_STATE (element);
  GstElementState pending = GST_STATE_PENDING (element);

  g_return_val_if_fail (GST_IS_CDPLAYER (element), GST_STATE_FAILURE);

  cdp = CDPLAYER (element);

  switch (pending) {
    case GST_STATE_READY:
      if (state != GST_STATE_PAUSED) {
        if (cd_init (CDPLAYER_CD (cdp), cdp->device) == FALSE) {
          return GST_STATE_FAILURE;
        }
        cdp->num_tracks = cdp->cd.num_tracks;
        cdp->cddb_discid = cd_cddb_discid (CDPLAYER_CD (cdp));
      }
      break;
    case GST_STATE_PAUSED:
      /* ready->paused is not useful */
      if (state != GST_STATE_READY) {
        if (cd_pause (CDPLAYER_CD (cdp)) == FALSE) {
          return GST_STATE_FAILURE;
        }

        cdp->paused = TRUE;
      }

      break;
    case GST_STATE_PLAYING:
      if (cdp->paused == TRUE) {
        if (cd_resume (CDPLAYER_CD (cdp)) == FALSE) {
          return GST_STATE_FAILURE;
        }

        cdp->paused = FALSE;
      } else {
        if (cd_start (CDPLAYER_CD (cdp), cdp->start_track,
                cdp->end_track) == FALSE) {
          return GST_STATE_FAILURE;
        }
      }

      break;
    case GST_STATE_NULL:
      /* stop & close fd */
      if (cd_stop (CDPLAYER_CD (cdp)) == FALSE
          || cd_close (CDPLAYER_CD (cdp)) == FALSE) {
        return GST_STATE_FAILURE;
      }

      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    GST_ELEMENT_CLASS (parent_class)->change_state (element);
  }

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "cdplayer", GST_RANK_NONE,
      GST_TYPE_CDPLAYER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "cdplayer", "CD Player", plugin_init, VERSION, GST_LICENSE,    /* ? */
    GST_PACKAGE, GST_ORIGIN);
