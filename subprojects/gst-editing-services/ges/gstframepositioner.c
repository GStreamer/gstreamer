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

#include <math.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstframepositioner.h"
#include "ges-frame-composition-meta.h"
#include "ges-internal.h"

GST_DEBUG_CATEGORY_STATIC (_framepositioner);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT _framepositioner

/* We  need to define a max number of pixel so we can interpolate them */
#define MAX_PIXELS 100000
#define MIN_PIXELS -100000

static void gst_frame_positioner_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_frame_positioner_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_frame_positioner_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);

enum
{
  PROP_0,
  PROP_ALPHA,
  PROP_POSX,
  PROP_POSY,
  PROP_ZORDER,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_OPERATOR,
  PROP_LAST,
};

static GParamSpec *properties[PROP_LAST];

static GstStaticPadTemplate gst_frame_positioner_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate gst_frame_positioner_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

G_DEFINE_TYPE (GstFramePositioner, gst_frame_positioner,
    GST_TYPE_BASE_TRANSFORM);

GType
gst_compositor_operator_get_type_and_default_value (int *default_operator_value)
{
  static gsize _init = 0;
  static int operator_value = 0;
  static GType operator_gtype = G_TYPE_NONE;

  if (g_once_init_enter (&_init)) {
    GstElement *compositor =
        gst_element_factory_create (ges_get_compositor_factory (), NULL);

    GstPad *compositorPad =
        gst_element_request_pad_simple (compositor, "sink_%u");

    GParamSpec *pspec =
        g_object_class_find_property (G_OBJECT_GET_CLASS (compositorPad),
        "operator");

    if (pspec) {
      operator_value =
          g_value_get_enum (g_param_spec_get_default_value (pspec));
      operator_gtype = pspec->value_type;
    }

    gst_element_release_request_pad (compositor, compositorPad);
    gst_object_unref (compositorPad);
    gst_object_unref (compositor);

    g_once_init_leave (&_init, 1);
  }

  if (default_operator_value)
    *default_operator_value = operator_value;

  return operator_gtype;
}

static void
_weak_notify_cb (GstFramePositioner * pos, GObject * old)
{
  pos->current_track = NULL;
}

static gboolean
is_user_positionned (GstFramePositioner * self)
{
  gint i;
  GParamSpec *positioning_props[] = {
    properties[PROP_WIDTH],
    properties[PROP_HEIGHT],
    properties[PROP_POSX],
    properties[PROP_POSY],
  };

  if (self->user_positioned)
    return TRUE;

  for (i = 0; i < G_N_ELEMENTS (positioning_props); i++) {
    GstControlBinding *b = gst_object_get_control_binding (GST_OBJECT (self),
        positioning_props[i]->name);

    if (b) {
      gst_object_unref (b);
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
auto_position (GstFramePositioner * self)
{
  gdouble scaled_width = -1, scaled_height = -1, x, y;

  if (is_user_positionned (self)) {
    GST_DEBUG_OBJECT (self, "Was positioned by the user, not auto positioning");
    return FALSE;
  }

  if (!self->natural_width || !self->natural_height)
    return FALSE;

  if (self->track_width == self->natural_width &&
      self->track_height == self->natural_height)
    return TRUE;

  scaled_height =
      gst_util_uint64_scale_int (self->natural_height, self->track_width,
      self->natural_width);
  scaled_width = self->track_width;
  if (scaled_height > self->track_height) {
    scaled_height = self->track_height;
    scaled_width =
        gst_util_uint64_scale_int (self->natural_width, self->track_height,
        self->natural_height);
  }

  x = MAX (0, (self->track_width - scaled_width) / 2.f);
  y = MAX (0, (self->track_height - scaled_height) / 2.f);

  GST_INFO_OBJECT (self, "Scalling video to match track size from "
      "%dx%d to %fx%f",
      self->natural_width, self->natural_height, scaled_width, scaled_height);
  self->width = scaled_width;
  self->height = scaled_height;
  self->posx = x;
  self->posy = y;

  return TRUE;
}

typedef struct
{
  gdouble *value;
  gint old_track_value;
  gint track_value;
  GParamSpec *pspec;
} RepositionPropertyData;

static void
reposition_properties (GstFramePositioner * pos, gint old_track_width,
    gint old_track_height)
{
  gint i;
  RepositionPropertyData props_data[] = {
    {&pos->width, old_track_width, pos->track_width, properties[PROP_WIDTH]},
    {&pos->height, old_track_height, pos->track_height,
        properties[PROP_HEIGHT]},
    {&pos->posx, old_track_width, pos->track_width, properties[PROP_POSX]},
    {&pos->posy, old_track_height, pos->track_height, properties[PROP_POSY]},
  };

  for (i = 0; i < G_N_ELEMENTS (props_data); i++) {
    GList *values, *tmp;
    gboolean absolute;
    GstTimedValueControlSource *source = NULL;

    RepositionPropertyData d = props_data[i];
    GstControlBinding *binding =
        gst_object_get_control_binding (GST_OBJECT (pos), d.pspec->name);

    *(d.value) =
        *(d.value) * (gdouble) d.track_value / (gdouble) d.old_track_value;

    if (!binding)
      continue;

    if (!GST_IS_DIRECT_CONTROL_BINDING (binding)) {
      GST_FIXME_OBJECT (pos, "Implement support for control binding type: %s",
          G_OBJECT_TYPE_NAME (binding));

      goto next;
    }

    g_object_get (binding, "control_source", &source, NULL);
    if (!source || !GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)) {
      GST_FIXME_OBJECT (pos, "Implement support for control source type: %s",
          source ? G_OBJECT_TYPE_NAME (source) : "NULL");

      goto next;
    }

    values =
        gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
        (source));

    if (!values)
      goto next;

    g_object_get (binding, "absolute", &absolute, NULL);
    for (tmp = values; tmp; tmp = tmp->next) {
      GstTimedValue *value = tmp->data;

      gst_timed_value_control_source_set (source, value->timestamp,
          value->value * d.track_value / d.old_track_value);
    }

    g_list_free (values);

  next:
    gst_clear_object (&source);
    gst_object_unref (binding);
  }
}

static void
gst_frame_positioner_update_properties (GstFramePositioner * pos,
    gboolean track_mixing, gint old_track_width, gint old_track_height)
{
  GstCaps *caps;

  if (pos->capsfilter == NULL)
    return;

  caps = gst_caps_from_string ("video/x-raw(ANY)");

  if (pos->track_width && pos->track_height &&
      (!track_mixing || !pos->scale_in_compositor)) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT,
        pos->track_width, "height", G_TYPE_INT, pos->track_height, NULL);
  }

  if (pos->fps_n != -1)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, pos->fps_n,
        pos->fps_d, NULL);

  if (pos->par_n != -1)
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        pos->par_n, pos->par_d, NULL);

  if (!pos->track_width || !pos->track_height) {
    GST_INFO_OBJECT (pos, "Track doesn't have a proper size, not "
        "positioning the source");
    goto done;
  } else if (auto_position (pos))
    goto done;

  if (!old_track_height || !old_track_width) {
    GST_DEBUG_OBJECT (pos, "No old track size, can not properly reposition");
    goto done;
  }

  if ((!pos->natural_width || !pos->natural_height) &&
      (!pos->width || !pos->height)) {
    GST_DEBUG_OBJECT (pos, "No natural aspect ratio and no user set "
        " image size, can't not reposition.");
    goto done;
  }

  if (gst_util_fraction_compare (old_track_width, old_track_height,
          pos->track_width, pos->track_height)) {
    GST_INFO_OBJECT (pos, "Not repositioning as track size change didn't"
        " keep the same aspect ratio (previous %dx%d("
        "ratio=%f), new: %dx%d(ratio=%f)",
        old_track_width, old_track_height,
        (gdouble) old_track_width / (gdouble) old_track_height,
        pos->track_width, pos->track_height,
        (gdouble) pos->track_width / (gdouble) pos->track_height);
    goto done;
  }

  reposition_properties (pos, old_track_width, old_track_height);

done:
  GST_DEBUG_OBJECT (pos, "setting caps %" GST_PTR_FORMAT, caps);

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

    if (!gst_structure_get_fraction (structure, "pixel-aspect-ratio",
            &(pos->par_n), &(pos->par_d)))
      pos->par_n = -1;
  }

  old_track_width = pos->track_width;
  old_track_height = pos->track_height;

  pos->track_width = width;
  pos->track_height = height;

  GST_DEBUG_OBJECT (pos, "syncing framerate from caps : %d/%d", pos->fps_n,
      pos->fps_d);
  if (caps)
    gst_caps_unref (caps);

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
  gst_object_unref (pos);
}

void
ges_frame_positioner_set_source_and_filter (GstFramePositioner * pos,
    GESTrackElement * trksrc, GstElement * capsfilter)
{
  pos->track_source = trksrc;
  pos->capsfilter = capsfilter;
  pos->current_track = ges_track_element_get_track (trksrc);

  g_object_weak_ref (G_OBJECT (trksrc),
      (GWeakNotify) _trk_element_weak_notify_cb, gst_object_ref (pos));
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
  int default_operator_value = 0;
  GType operator_gtype =
      gst_compositor_operator_get_type_and_default_value
      (&default_operator_value);
  guint n_pspecs = PROP_LAST;

  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (_framepositioner, "framepositioner",
      GST_DEBUG_FG_YELLOW, "ges frame positioner");

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
  properties[PROP_ALPHA] =
      g_param_spec_double ("alpha", "alpha", "alpha of the stream", 0.0, 1.0,
      1.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);

  /**
   * gstframepositioner:posx:
   *
   * The desired x position for the stream.
   */
  properties[PROP_POSX] =
      g_param_spec_int ("posx", "posx", "x position of the stream", MIN_PIXELS,
      MAX_PIXELS, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);


  /**
   * gstframepositioner:posy:
   *
   * The desired y position for the stream.
   */
  properties[PROP_POSY] =
      g_param_spec_int ("posy", "posy", "y position of the stream", MIN_PIXELS,
      MAX_PIXELS, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);

  /**
   * gstframepositioner:zorder:
   *
   * The desired z order for the stream.
   */
  properties[PROP_ZORDER] =
      g_param_spec_uint ("zorder", "zorder", "z order of the stream", 0,
      G_MAXUINT, 0, G_PARAM_READWRITE);

  /**
   * gesframepositioner:width:
   *
   * The desired width for that source.
   * Set to 0 if size is not mandatory, will be set to width of the current track.
   */
  properties[PROP_WIDTH] =
      g_param_spec_int ("width", "width", "width of the source", 0, MAX_PIXELS,
      0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);

  /**
   * gesframepositioner:height:
   *
   * The desired height for that source.
   * Set to 0 if size is not mandatory, will be set to height of the current track.
   */
  properties[PROP_HEIGHT] =
      g_param_spec_int ("height", "height", "height of the source", 0,
      MAX_PIXELS, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);

  /**
   * gesframepositioner:operator:
   *
   * The blending operator for the source.
   */
  if (operator_gtype != G_TYPE_NONE) {
    properties[PROP_OPERATOR] =
        g_param_spec_enum ("operator", "Operator",
        "Blending operator to use for blending this pad over the previous ones",
        operator_gtype, default_operator_value,
        (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
            GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS));
  } else {
    n_pspecs--;
  }

  g_object_class_install_properties (gobject_class, n_pspecs, properties);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "frame positioner", "Metadata",
      "This element provides with tagging facilities",
      "mduponchelle1@gmail.com");
}

static void
gst_frame_positioner_init (GstFramePositioner * framepositioner)
{
  int default_operator_value;
  gst_compositor_operator_get_type_and_default_value (&default_operator_value);

  framepositioner->alpha = 1.0;
  framepositioner->posx = 0.0;
  framepositioner->posy = 0.0;
  framepositioner->zorder = 0;
  framepositioner->width = 0;
  framepositioner->height = 0;
  framepositioner->operator = default_operator_value;
  framepositioner->fps_n = -1;
  framepositioner->fps_d = -1;
  framepositioner->track_width = 0;
  framepositioner->track_height = 0;
  framepositioner->capsfilter = NULL;
  framepositioner->track_source = NULL;
  framepositioner->current_track = NULL;
  framepositioner->scale_in_compositor = TRUE;

  framepositioner->par_n = -1;
  framepositioner->par_d = 1;
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
      framepositioner->user_positioned = TRUE;
      break;
    case PROP_POSY:
      framepositioner->posy = g_value_get_int (value);
      framepositioner->user_positioned = TRUE;
      break;
    case PROP_ZORDER:
      framepositioner->zorder = g_value_get_uint (value);
      break;
    case PROP_WIDTH:
      framepositioner->user_positioned = TRUE;
      framepositioner->width = g_value_get_int (value);
      gst_frame_positioner_update_properties (framepositioner, track_mixing,
          0, 0);
      break;
    case PROP_HEIGHT:
      framepositioner->user_positioned = TRUE;
      framepositioner->height = g_value_get_int (value);
      gst_frame_positioner_update_properties (framepositioner, track_mixing,
          0, 0);
      break;
    case PROP_OPERATOR:
      framepositioner->operator = g_value_get_enum (value);
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

  switch (property_id) {
    case PROP_ALPHA:
      g_value_set_double (value, pos->alpha);
      break;
    case PROP_POSX:
      g_value_set_int (value, round (pos->posx));
      break;
    case PROP_POSY:
      g_value_set_int (value, round (pos->posy));
      break;
    case PROP_ZORDER:
      g_value_set_uint (value, pos->zorder);
      break;
    case PROP_WIDTH:
      if (pos->scale_in_compositor) {
        g_value_set_int (value, round (pos->width));
      } else {
        real_width =
            pos->width > 0 ? round (pos->width) : round (pos->track_width);
        g_value_set_int (value, real_width);
      }
      break;
    case PROP_HEIGHT:
      if (pos->scale_in_compositor) {
        g_value_set_int (value, round (pos->height));
      } else {
        real_height =
            pos->height > 0 ? round (pos->height) : round (pos->track_height);
        g_value_set_int (value, real_height);
      }
      break;
    case PROP_OPERATOR:
      g_value_set_enum (value, pos->operator);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_frame_positioner_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GESFrameCompositionMeta *meta;
  GstFramePositioner *framepositioner = GST_FRAME_POSITIONNER (trans);
  GstClockTime timestamp = GST_BUFFER_PTS (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    gst_object_sync_values (GST_OBJECT (trans), timestamp);
  }

  meta = ges_buffer_add_frame_composition_meta (buf);

  GST_OBJECT_LOCK (framepositioner);
  meta->alpha = framepositioner->alpha;
  meta->posx = round (framepositioner->posx);
  meta->posy = round (framepositioner->posy);
  meta->width = round (framepositioner->width);
  meta->height = round (framepositioner->height);
  meta->zorder = framepositioner->zorder;
  meta->operator = framepositioner->operator;
  GST_OBJECT_UNLOCK (framepositioner);

  return GST_FLOW_OK;
}
