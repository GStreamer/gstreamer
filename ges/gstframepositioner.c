/* GStreamer
 * Copyright (C) 2013 Mathieu Duponchelle <mduponchelle1@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstframepositioner.h"

/* We  need to define a max number of pixel so we can interpolate them */
#define MAX_PIXELS 100000
#define MIN_PIXELS -100000

static void gst_frame_positioner_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_frame_positioner_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_frame_positioner_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);

static gboolean
gst_frame_positioner_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer);
static gboolean gst_frame_positioner_meta_transform (GstBuffer * dest,
    GstMeta * meta, GstBuffer * buffer, GQuark type, gpointer data);

enum
{
  PROP_0,
  PROP_ALPHA,
  PROP_POSX,
  PROP_POSY,
  PROP_ZORDER,
  PROP_WIDTH,
  PROP_HEIGHT
};

static GstStaticPadTemplate gst_frame_positioner_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate gst_frame_positioner_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

G_DEFINE_TYPE (GstFramePositioner, gst_frame_positioner,
    GST_TYPE_BASE_TRANSFORM);

static void
_weak_notify_cb (GstFramePositioner * pos, GObject * old)
{
  pos->current_track = NULL;
}

static void
gst_frame_positioner_update_properties (GstFramePositioner * pos,
    gboolean track_mixing, gint old_track_width, gint old_track_height)
{
  GstCaps *caps;

  if (pos->capsfilter == NULL)
    return;

  if (pos->track_width && pos->track_height &&
      (!track_mixing || !pos->scale_in_compositor)) {
    caps =
        gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
        pos->track_width, "height", G_TYPE_INT, pos->track_height, NULL);
  } else {
    caps = gst_caps_new_empty_simple ("video/x-raw");
  }

  if (pos->fps_n != -1)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, pos->fps_n,
        pos->fps_d, NULL);

  if (old_track_width && pos->width == old_track_width &&
      old_track_height && pos->height == old_track_height &&
      ((float) old_track_width / (float) old_track_height) ==
      ((float) pos->track_width / (float) pos->track_height)) {

    GST_DEBUG_OBJECT (pos, "Following track size width old_track: %d -- pos: %d"
        " || height, old_track %d -- pos: %d",
        old_track_width, pos->width, old_track_height, pos->height);

    pos->width = pos->track_width;
    pos->height = pos->track_height;
  }

  GST_DEBUG_OBJECT (caps, "setting caps");

  g_object_set (pos->capsfilter, "caps", caps, NULL);

  gst_caps_unref (caps);
}

static void
sync_properties_from_track (GstFramePositioner * pos, GESTrack * track)
{
  gint width, height;
  gint old_track_width, old_track_height;
  GstCaps *caps;

  g_object_get (track, "restriction-caps", &caps, NULL);

  width = height = 0;

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *structure;

    structure = gst_caps_get_structure (caps, 0);
    if (!gst_structure_get_int (structure, "width", &width))
      width = 0;
    if (!gst_structure_get_int (structure, "height", &height))
      height = 0;
    if (!gst_structure_get_fraction (structure, "framerate", &(pos->fps_n),
            &(pos->fps_d)))
      pos->fps_n = -1;
  }

  old_track_width = pos->track_width;
  old_track_height = pos->track_height;

  pos->track_width = width;
  pos->track_height = height;

  GST_DEBUG_OBJECT (pos, "syncing framerate from caps : %d/%d", pos->fps_n,
      pos->fps_d);

  gst_frame_positioner_update_properties (pos, ges_track_get_mixing (track),
      old_track_width, old_track_height);
}

static void
_track_restriction_changed_cb (GESTrack * track, GParamSpec * arg G_GNUC_UNUSED,
    GstFramePositioner * pos)
{
  sync_properties_from_track (pos, track);
}

static void
set_track (GstFramePositioner * pos)
{
  GESTrack *new_track;

  if (pos->current_track) {
    g_signal_handlers_disconnect_by_func (pos->current_track,
        (GCallback) _track_restriction_changed_cb, pos);
    g_object_weak_unref (G_OBJECT (pos->current_track),
        (GWeakNotify) _weak_notify_cb, pos);
  }

  new_track = ges_track_element_get_track (pos->track_source);
  if (new_track) {
    pos->current_track = new_track;
    g_object_weak_ref (G_OBJECT (new_track), (GWeakNotify) _weak_notify_cb,
        pos);
    GST_DEBUG_OBJECT (pos, "connecting to track : %p", pos->current_track);

    g_signal_connect (pos->current_track, "notify::restriction-caps",
        (GCallback) _track_restriction_changed_cb, pos);
    sync_properties_from_track (pos, pos->current_track);
  } else {
    pos->current_track = NULL;
  }
}

static void
_track_changed_cb (GESTrackElement * trksrc, GParamSpec * arg G_GNUC_UNUSED,
    GstFramePositioner * pos)
{
  set_track (pos);
}

static void
_trk_element_weak_notify_cb (GstFramePositioner * pos, GObject * old)
{
  pos->track_source = NULL;
}

void
ges_frame_positioner_set_source_and_filter (GstFramePositioner * pos,
    GESTrackElement * trksrc, GstElement * capsfilter)
{
  pos->track_source = trksrc;
  pos->capsfilter = capsfilter;
  pos->current_track = ges_track_element_get_track (trksrc);

  g_object_weak_ref (G_OBJECT (trksrc),
      (GWeakNotify) _trk_element_weak_notify_cb, pos);
  g_signal_connect (trksrc, "notify::track", (GCallback) _track_changed_cb,
      pos);
  set_track (pos);
}

static void
gst_frame_positioner_dispose (GObject * object)
{
  GstFramePositioner *pos = GST_FRAME_POSITIONNER (object);

  if (pos->track_source) {
    g_signal_handlers_disconnect_by_func (pos->track_source, _track_changed_cb,
        pos);
    pos->track_source = NULL;
  }

  if (pos->current_track) {
    g_signal_handlers_disconnect_by_func (pos->current_track,
        _track_restriction_changed_cb, pos);
    g_object_weak_unref (G_OBJECT (pos->current_track),
        (GWeakNotify) _weak_notify_cb, pos);
    pos->current_track = NULL;
  }

  G_OBJECT_CLASS (gst_frame_positioner_parent_class)->dispose (object);
}

static void
gst_frame_positioner_class_init (GstFramePositionerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_frame_positioner_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_frame_positioner_sink_template);

  gobject_class->set_property = gst_frame_positioner_set_property;
  gobject_class->get_property = gst_frame_positioner_get_property;
  gobject_class->dispose = gst_frame_positioner_dispose;
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_frame_positioner_transform_ip);

  /**
   * gstframepositioner:alpha:
   *
   * The desired alpha for the stream.
   */
  g_object_class_install_property (gobject_class, PROP_ALPHA,
      g_param_spec_double ("alpha", "alpha", "alpha of the stream",
          0.0, 1.0, 1.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /**
   * gstframepositioner:posx:
   *
   * The desired x position for the stream.
   */
  g_object_class_install_property (gobject_class, PROP_POSX,
      g_param_spec_int ("posx", "posx", "x position of the stream",
          MIN_PIXELS, MAX_PIXELS, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));


  /**
   * gstframepositioner:posy:
   *
   * The desired y position for the stream.
   */
  g_object_class_install_property (gobject_class, PROP_POSY,
      g_param_spec_int ("posy", "posy", "y position of the stream",
          MIN_PIXELS, MAX_PIXELS, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /**
   * gstframepositioner:zorder:
   *
   * The desired z order for the stream.
   */
  g_object_class_install_property (gobject_class, PROP_ZORDER,
      g_param_spec_uint ("zorder", "zorder", "z order of the stream",
          0, G_MAXUINT, 0, G_PARAM_READWRITE));

  /**
   * gesframepositioner:width:
   *
   * The desired width for that source.
   * Set to 0 if size is not mandatory, will be set to width of the current track.
   */
  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "width", "width of the source",
          0, MAX_PIXELS, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /**
   * gesframepositioner:height:
   *
   * The desired height for that source.
   * Set to 0 if size is not mandatory, will be set to height of the current track.
   */
  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_int ("height", "height", "height of the source",
          0, MAX_PIXELS, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "frame positioner", "Metadata",
      "This element provides with tagging facilities",
      "mduponchelle1@gmail.com");
}

static void
gst_frame_positioner_init (GstFramePositioner * framepositioner)
{
  framepositioner->alpha = 1.0;
  framepositioner->posx = 0.0;
  framepositioner->posy = 0.0;
  framepositioner->zorder = 0;
  framepositioner->width = 0;
  framepositioner->height = 0;
  framepositioner->fps_n = -1;
  framepositioner->fps_d = -1;
  framepositioner->track_width = 0;
  framepositioner->track_height = 0;
  framepositioner->capsfilter = NULL;
  framepositioner->track_source = NULL;
  framepositioner->current_track = NULL;
  framepositioner->scale_in_compositor = TRUE;
}

void
gst_frame_positioner_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFramePositioner *framepositioner = GST_FRAME_POSITIONNER (object);
  gboolean track_mixing = TRUE;

  if (framepositioner->current_track)
    track_mixing = ges_track_get_mixing (framepositioner->current_track);


  GST_OBJECT_LOCK (framepositioner);
  switch (property_id) {
    case PROP_ALPHA:
      framepositioner->alpha = g_value_get_double (value);
      break;
    case PROP_POSX:
      framepositioner->posx = g_value_get_int (value);
      break;
    case PROP_POSY:
      framepositioner->posy = g_value_get_int (value);
      break;
    case PROP_ZORDER:
      framepositioner->zorder = g_value_get_uint (value);
      break;
    case PROP_WIDTH:
      framepositioner->width = g_value_get_int (value);
      gst_frame_positioner_update_properties (framepositioner, track_mixing,
          0, 0);
      break;
    case PROP_HEIGHT:
      framepositioner->height = g_value_get_int (value);
      gst_frame_positioner_update_properties (framepositioner, track_mixing,
          0, 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (framepositioner);
}

void
gst_frame_positioner_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstFramePositioner *pos = GST_FRAME_POSITIONNER (object);
  gint real_width, real_height;

  GST_DEBUG_OBJECT (pos, "get_property");

  switch (property_id) {
    case PROP_ALPHA:
      g_value_set_double (value, pos->alpha);
      break;
    case PROP_POSX:
      g_value_set_int (value, pos->posx);
      break;
    case PROP_POSY:
      g_value_set_int (value, pos->posy);
      break;
    case PROP_ZORDER:
      g_value_set_uint (value, pos->zorder);
      break;
    case PROP_WIDTH:
      real_width = (pos->width > 0) ? pos->width : pos->track_width;
      g_value_set_int (value, real_width);
      break;
    case PROP_HEIGHT:
      real_height = (pos->height > 0) ? pos->height : pos->track_height;
      g_value_set_int (value, real_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

GType
gst_frame_positioner_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "video", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstFramePositionerApi", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static const GstMetaInfo *
gst_frame_positioner_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_frame_positioner_meta_api_get_type (),
        "GstFramePositionerMeta",
        sizeof (GstFramePositionerMeta), gst_frame_positioner_meta_init,
        NULL,
        gst_frame_positioner_meta_transform);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

static gboolean
gst_frame_positioner_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstFramePositionerMeta *smeta;

  smeta = (GstFramePositionerMeta *) meta;

  smeta->alpha = 0.0;
  smeta->posx = smeta->posy = smeta->height = smeta->width = 0;
  smeta->zorder = 0;

  return TRUE;
}

static gboolean
gst_frame_positioner_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstFramePositionerMeta *dmeta, *smeta;

  smeta = (GstFramePositionerMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    /* only copy if the complete data is copied as well */
    dmeta =
        (GstFramePositionerMeta *) gst_buffer_add_meta (dest,
        gst_frame_positioner_get_info (), NULL);
    dmeta->alpha = smeta->alpha;
    dmeta->posx = smeta->posx;
    dmeta->posy = smeta->posy;
    dmeta->width = smeta->width;
    dmeta->height = smeta->height;
    dmeta->zorder = smeta->zorder;
  }

  return TRUE;
}

static GstFlowReturn
gst_frame_positioner_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstFramePositionerMeta *meta;
  GstFramePositioner *framepositioner = GST_FRAME_POSITIONNER (trans);
  GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    gst_object_sync_values (GST_OBJECT (trans), timestamp);
  }

  meta =
      (GstFramePositionerMeta *) gst_buffer_add_meta (buf,
      gst_frame_positioner_get_info (), NULL);

  GST_OBJECT_LOCK (framepositioner);
  meta->alpha = framepositioner->alpha;
  meta->posx = framepositioner->posx;
  meta->posy = framepositioner->posy;
  meta->width = framepositioner->width;
  meta->height = framepositioner->height;
  meta->zorder = framepositioner->zorder;
  GST_OBJECT_UNLOCK (framepositioner);

  return GST_FLOW_OK;
}
