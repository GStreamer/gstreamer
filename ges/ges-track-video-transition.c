/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

/**
 * SECTION:ges-track-video-transition
 * @short_description: implements video crossfade transition
 */

#include <ges/ges.h>
#include "ges-internal.h"

G_DEFINE_TYPE (GESTrackVideoTransition, ges_track_video_transition,
    GES_TYPE_TRACK_TRANSITION);

static inline void
ges_track_video_transition_set_border_internal (GESTrackVideoTransition * self,
    guint border);
static inline void
ges_track_video_transition_set_inverted_internal (GESTrackVideoTransition *
    self, gboolean inverted);
static inline gboolean
ges_track_video_transition_set_transition_type_internal (GESTrackVideoTransition
    * self, GESVideoStandardTransitionType type);
struct _GESTrackVideoTransitionPrivate
{
  GESVideoStandardTransitionType type;

  /* prevents cases where the transition has not been changed yet */
  GESVideoStandardTransitionType pending_type;

  /* these enable video interpolation */
  GstController *controller;
  GstInterpolationControlSource *control_source;

  /* so we can support changing between wipes */
  GstElement *topbin;
  GstElement *smpte;
  GstElement *mixer;
  GstPad *sinka;
  GstPad *sinkb;

  /* these will be different depending on whether smptealpha or alpha element
   * is used */
  gdouble start_value;
  gdouble end_value;
  guint64 dur;

  /* This is in case the smpte doesn't exist yet */
  gint pending_border_value;
  gboolean pending_inverted;
};

enum
{
  PROP_0,
  PROP_BORDER,
  PROP_TRANSITION_TYPE,
  PROP_INVERT,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

#define fast_element_link(a,b) gst_element_link_pads_full((a),"src",(b),"sink",GST_PAD_LINK_CHECK_NOTHING)

static GObject *link_element_to_mixer (GstElement * element,
    GstElement * mixer);

static GObject *link_element_to_mixer_with_smpte (GstBin * bin,
    GstElement * element, GstElement * mixer, gint type,
    GstElement ** smpteref);

static void
ges_track_video_transition_duration_changed (GESTrackObject * self,
    guint64 duration);

static GstElement *ges_track_video_transition_create_element (GESTrackObject
    * self);

static void ges_track_video_transition_dispose (GObject * object);

static void ges_track_video_transition_finalize (GObject * object);

static void ges_track_video_transition_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_video_transition_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static void
ges_track_video_transition_class_init (GESTrackVideoTransitionClass * klass)
{
  GObjectClass *object_class;
  GESTrackObjectClass *toclass;

  g_type_class_add_private (klass, sizeof (GESTrackVideoTransitionPrivate));

  object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_video_transition_get_property;
  object_class->set_property = ges_track_video_transition_set_property;
  object_class->dispose = ges_track_video_transition_dispose;
  object_class->finalize = ges_track_video_transition_finalize;

  /**
   * GESTrackVideoTransition:border
   *
   * This value represents the border width of the transition.
   *
   */
  properties[PROP_BORDER] =
      g_param_spec_uint ("border", "Border", "The border width", 0,
      G_MAXUINT, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_BORDER,
      properties[PROP_BORDER]);

  /**
   * GESTrackVideoTransition:type
   *
   * The #GESVideoStandardTransitionType currently applied on the object
   *
   */
  properties[PROP_TRANSITION_TYPE] =
      g_param_spec_enum ("transition-type", "Transition type",
      "The type of the transition", GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE,
      GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_TRANSITION_TYPE,
      properties[PROP_TRANSITION_TYPE]);

  /**
   * GESTrackVideoTransition:invert
   *
   * This value represents the direction of the transition.
   *
   */
  properties[PROP_INVERT] =
      g_param_spec_boolean ("invert", "Invert",
      "Whether the transition is inverted", FALSE, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_INVERT,
      properties[PROP_INVERT]);

  toclass = GES_TRACK_OBJECT_CLASS (klass);
  toclass->duration_changed = ges_track_video_transition_duration_changed;
  toclass->create_element = ges_track_video_transition_create_element;
}

static void
ges_track_video_transition_init (GESTrackVideoTransition * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_VIDEO_TRANSITION, GESTrackVideoTransitionPrivate);

  self->priv->controller = NULL;
  self->priv->control_source = NULL;
  self->priv->smpte = NULL;
  self->priv->mixer = NULL;
  self->priv->sinka = NULL;
  self->priv->sinkb = NULL;
  self->priv->topbin = NULL;
  self->priv->type = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
  self->priv->pending_type = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
  self->priv->start_value = 0.0;
  self->priv->end_value = 0.0;
  self->priv->dur = 42;
  self->priv->pending_border_value = -1;
  self->priv->pending_inverted = FALSE;
}

static void
ges_track_video_transition_dispose (GObject * object)
{
  GESTrackVideoTransition *self = GES_TRACK_VIDEO_TRANSITION (object);
  GESTrackVideoTransitionPrivate *priv = self->priv;

  GST_DEBUG ("disposing");
  GST_LOG ("mixer: %p smpte: %p sinka: %p sinkb: %p",
      priv->mixer, priv->smpte, priv->sinka, priv->sinkb);

  if (priv->controller) {
    g_object_unref (priv->controller);
    priv->controller = NULL;
    if (priv->control_source)
      gst_object_unref (priv->control_source);
    priv->control_source = NULL;
  }

  if (priv->sinka && priv->sinkb) {
    GST_DEBUG ("releasing request pads for mixer");
    gst_element_release_request_pad (priv->mixer, priv->sinka);
    gst_element_release_request_pad (priv->mixer, priv->sinkb);
    gst_object_unref (priv->sinka);
    gst_object_unref (priv->sinkb);
    priv->sinka = NULL;
    priv->sinkb = NULL;
  }

  if (priv->mixer) {
    GST_LOG ("unrefing mixer");
    gst_object_unref (priv->mixer);
    priv->mixer = NULL;
  }

  G_OBJECT_CLASS (ges_track_video_transition_parent_class)->dispose (object);
}

static void
ges_track_video_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_video_transition_parent_class)->finalize (object);
}

static void
ges_track_video_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTrackVideoTransition *tr = GES_TRACK_VIDEO_TRANSITION (object);

  switch (property_id) {
    case PROP_BORDER:
      g_value_set_uint (value, ges_track_video_transition_get_border (tr));
      break;
    case PROP_TRANSITION_TYPE:
      g_value_set_enum (value,
          ges_track_video_transition_get_transition_type (tr));
      break;
    case PROP_INVERT:
      g_value_set_boolean (value, ges_track_video_transition_is_inverted (tr));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_video_transition_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESTrackVideoTransition *tr = GES_TRACK_VIDEO_TRANSITION (object);

  switch (property_id) {
    case PROP_BORDER:
      ges_track_video_transition_set_border_internal (tr,
          g_value_get_uint (value));
      break;
    case PROP_TRANSITION_TYPE:
      ges_track_video_transition_set_transition_type_internal (tr,
          g_value_get_enum (value));
      break;
    case PROP_INVERT:
      ges_track_video_transition_set_inverted_internal (tr,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
on_caps_set (GstPad * srca_pad, GParamSpec * pspec, GstElement * capsfilt)
{
  gint width, height;
  const GstStructure *str;
  GstCaps *size_caps = NULL;

  if (GST_PAD_CAPS (srca_pad)) {
    /* Get width and height of first video */
    str = gst_caps_get_structure (GST_PAD_CAPS (srca_pad), 0);
    gst_structure_get_int (str, "width", &width);
    gst_structure_get_int (str, "height", &height);

    /* Set capsfilter to the size of the first video */
    size_caps =
        gst_caps_new_simple ("video/x-raw-yuv",
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
    g_object_set (capsfilt, "caps", size_caps, NULL);
  }
}

static GstElement *
create_mixer (GstElement * topbin)
{
  GstElement *mixer = NULL;

  /* Prefer videomixer2 to videomixer */
  mixer = gst_element_factory_make ("videomixer2", NULL);
  if (mixer == NULL)
    mixer = gst_element_factory_make ("videomixer", NULL);
  g_object_set (G_OBJECT (mixer), "background", 1, NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  return (mixer);
}

static void
set_interpolation (GObject * element, GESTrackVideoTransitionPrivate * priv,
    const gchar * propname)
{
  g_object_set (element, propname, (gfloat) 0.0, NULL);

  if (priv->controller)
    g_object_unref (priv->controller);

  priv->controller =
      gst_object_control_properties (G_OBJECT (element), propname, NULL);

  if (priv->control_source) {
    gst_interpolation_control_source_unset_all (priv->control_source);
    gst_object_unref (priv->control_source);
  }

  priv->control_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (priv->controller,
      propname, GST_CONTROL_SOURCE (priv->control_source));
  gst_interpolation_control_source_set_interpolation_mode
      (priv->control_source, GST_INTERPOLATE_LINEAR);
}

static GstElement *
ges_track_video_transition_create_element (GESTrackObject * object)
{
  GstElement *topbin, *iconva, *iconvb, *scalea, *scaleb, *capsfilt, *oconv;
  GObject *target = NULL;
  const gchar *propname = NULL;
  GstElement *mixer = NULL;
  GstPad *sinka_target, *sinkb_target, *src_target, *sinka, *sinkb, *src,
      *srca_pad;
  GESTrackVideoTransition *self;
  GESTrackVideoTransitionPrivate *priv;

  self = GES_TRACK_VIDEO_TRANSITION (object);
  priv = self->priv;

  GST_LOG ("creating a video bin");

  topbin = gst_bin_new ("transition-bin");
  iconva = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-a");
  iconvb = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-b");
  scalea = gst_element_factory_make ("videoscale", "vs-a");
  scaleb = gst_element_factory_make ("videoscale", "vs-b");
  capsfilt = gst_element_factory_make ("capsfilter", "capsfilt");
  oconv = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, scalea, scaleb, capsfilt,
      oconv, NULL);

  mixer = create_mixer (topbin);

  if (priv->pending_type != GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE) {
    priv->sinka =
        (GstPad *) link_element_to_mixer_with_smpte (GST_BIN (topbin), iconva,
        mixer, priv->pending_type, NULL);
    priv->sinkb =
        (GstPad *) link_element_to_mixer_with_smpte (GST_BIN (topbin), iconvb,
        mixer, priv->pending_type, &priv->smpte);
    target = (GObject *) priv->smpte;
    propname = "position";
    priv->start_value = 1.0;
    priv->end_value = 0.0;
  } else {
    gst_element_link_pads_full (iconva, "src", scalea, "sink",
        GST_PAD_LINK_CHECK_NOTHING);
    gst_element_link_pads_full (iconvb, "src", scaleb, "sink",
        GST_PAD_LINK_CHECK_NOTHING);
    gst_element_link_pads_full (scaleb, "src", capsfilt, "sink",
        GST_PAD_LINK_CHECK_NOTHING);

    priv->sinka = (GstPad *) link_element_to_mixer (scalea, mixer);
    priv->sinkb = (GstPad *) link_element_to_mixer (capsfilt, mixer);
    target = (GObject *) priv->sinkb;
    propname = "alpha";
    priv->start_value = 0.0;
    priv->end_value = 1.0;
  }

  priv->mixer = gst_object_ref (mixer);

  fast_element_link (mixer, oconv);

  sinka_target = gst_element_get_static_pad (iconva, "sink");
  sinkb_target = gst_element_get_static_pad (iconvb, "sink");
  src_target = gst_element_get_static_pad (oconv, "src");

  sinka = gst_ghost_pad_new ("sinka", sinka_target);
  sinkb = gst_ghost_pad_new ("sinkb", sinkb_target);
  src = gst_ghost_pad_new ("src", src_target);

  gst_element_add_pad (topbin, src);
  gst_element_add_pad (topbin, sinka);
  gst_element_add_pad (topbin, sinkb);

  srca_pad = gst_element_get_static_pad (scalea, "src");
  g_signal_connect (srca_pad, "notify::caps", G_CALLBACK (on_caps_set),
      (GstElement *) capsfilt);

  gst_object_unref (sinka_target);
  gst_object_unref (sinkb_target);
  gst_object_unref (src_target);
  gst_object_unref (srca_pad);

  /* set up interpolation */

  set_interpolation (target, priv, propname);

  priv->topbin = topbin;
  priv->type = priv->pending_type;

  return topbin;
}

static void
unblock_pad_cb (GstPad * sink, gboolean blocked, void *nil_ptr)
{
  /*Dummy function to make sure the unblocking is async */
}

static void
add_smpte_to_bin (GstPad * sink, GstElement * smptealpha,
    GESTrackVideoTransitionPrivate * priv)
{
  GstPad *peer, *sinkpad;

  g_object_set (smptealpha,
      "type", (gint) priv->pending_type, "invert", (gboolean) TRUE, NULL);
  gst_bin_add (GST_BIN (priv->topbin), smptealpha);
  gst_element_sync_state_with_parent (smptealpha);

  sinkpad = gst_element_get_static_pad (smptealpha, "sink");
  peer = gst_pad_get_peer (sink);
  gst_pad_unlink (peer, sink);

  gst_pad_link_full (peer, sinkpad, GST_PAD_LINK_CHECK_NOTHING);

  gst_object_unref (sinkpad);
  gst_object_unref (peer);
}

static void
replace_mixer (GESTrackVideoTransitionPrivate * priv)
{
  GstPad *mixer_src_pad, *color_sink_pad;

  mixer_src_pad = gst_element_get_static_pad (priv->mixer, "src");
  color_sink_pad = gst_pad_get_peer (mixer_src_pad);

  gst_element_set_state (priv->mixer, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (priv->topbin), priv->mixer);

  gst_object_unref (priv->mixer);

  priv->mixer = gst_object_ref (create_mixer (priv->topbin));

  gst_element_sync_state_with_parent (priv->mixer);

  gst_object_unref (mixer_src_pad);

  mixer_src_pad = gst_element_get_static_pad (priv->mixer, "src");
  gst_pad_link (mixer_src_pad, color_sink_pad);

  gst_object_unref (mixer_src_pad);
  gst_object_unref (color_sink_pad);

}

static void
switch_to_smpte_cb (GstPad * sink, gboolean blocked,
    GESTrackVideoTransition * transition)
{
  GstElement *smptealpha = gst_element_factory_make ("smptealpha", NULL);
  GstElement *smptealphab = gst_element_factory_make ("smptealpha", NULL);
  GESTrackVideoTransitionPrivate *priv = transition->priv;

  if (priv->pending_type == GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE)
    goto beach;

  GST_INFO ("Bin %p switching from crossfade to smpte", priv->topbin);

  add_smpte_to_bin (priv->sinka, smptealpha, priv);
  add_smpte_to_bin (priv->sinkb, smptealphab, priv);

  if (priv->pending_border_value != -1) {
    g_object_set (smptealphab, "border", priv->pending_border_value, NULL);
    priv->pending_border_value = -1;
  }

  if (priv->pending_inverted) {
    g_object_set (smptealphab, "invert", priv->pending_inverted, NULL);
    priv->pending_inverted = FALSE;
  }

  replace_mixer (priv);

  priv->start_value = 1.0;
  priv->end_value = 0.0;

  set_interpolation (G_OBJECT (smptealphab), priv, (gchar *) "position");
  ges_track_video_transition_duration_changed (GES_TRACK_OBJECT (transition),
      priv->dur);


  priv->sinka = (GstPad *) link_element_to_mixer (smptealpha, priv->mixer);
  priv->sinkb = (GstPad *) link_element_to_mixer (smptealphab, priv->mixer);

  priv->smpte = smptealphab;

  priv->type = priv->pending_type;

  GST_INFO ("Bin %p switched from crossfade to smpte", priv->topbin);

beach:
  priv->pending_type = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
  gst_pad_set_blocked_async (sink,
      FALSE, (GstPadBlockCallback) unblock_pad_cb, NULL);
}

static GstElement *
remove_smpte_from_bin (GESTrackVideoTransitionPrivate * priv, GstPad * sink)
{
  GstPad *smpte_src, *peer_src, *smpte_sink;
  GstElement *smpte, *peer;

  smpte_src = gst_pad_get_peer (sink);
  smpte = gst_pad_get_parent_element (smpte_src);

  if (smpte == NULL) {
    gst_object_unref (smpte_src);
    GST_ERROR ("The pad %p has no parent element. This should not happen");
    return (NULL);
  }

  smpte_sink = gst_element_get_static_pad (smpte, "sink");
  peer_src = gst_pad_get_peer (smpte_sink);
  peer = gst_pad_get_parent_element (peer_src);

  gst_pad_unlink (peer_src, smpte_sink);
  gst_pad_unlink (smpte_src, sink);

  gst_element_set_state (smpte, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (priv->topbin), smpte);

  gst_object_unref (smpte);
  gst_object_unref (smpte_sink);
  gst_object_unref (smpte_src);
  gst_object_unref (peer_src);
  return (peer);
}

static void
switch_to_crossfade_cb (GstPad * sink, gboolean blocked,
    GESTrackVideoTransition * transition)
{
  GstElement *peera;
  GstElement *peerb;
  GESTrackVideoTransitionPrivate *priv = transition->priv;

  GST_INFO ("Bin %p switching from smpte to crossfade", priv->topbin);

  if (priv->pending_type != GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE)
    goto beach;

  peera = remove_smpte_from_bin (priv, priv->sinka);
  peerb = remove_smpte_from_bin (priv, priv->sinkb);
  if (!peera || !peerb)
    goto beach;

  replace_mixer (priv);

  priv->sinka = (GstPad *) link_element_to_mixer (peera, priv->mixer);
  priv->sinkb = (GstPad *) link_element_to_mixer (peerb, priv->mixer);

  priv->start_value = 0.0;
  priv->end_value = 1.0;
  set_interpolation (G_OBJECT (priv->sinkb), priv, (gchar *) "alpha");
  ges_track_video_transition_duration_changed (GES_TRACK_OBJECT (transition),
      priv->dur);

  priv->smpte = NULL;

  gst_object_unref (peera);
  gst_object_unref (peerb);

  priv->type = priv->pending_type;

  GST_INFO ("Bin %p switched from smpte to crossfade", priv->topbin);

beach:
  priv->pending_type = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
  gst_pad_set_blocked_async (sink, FALSE,
      (GstPadBlockCallback) unblock_pad_cb, NULL);
}

static GObject *
link_element_to_mixer (GstElement * element, GstElement * mixer)
{
  GstPad *sinkpad = gst_element_get_request_pad (mixer, "sink_%d");
  GstPad *srcpad = gst_element_get_static_pad (element, "src");

  gst_pad_link_full (srcpad, sinkpad, GST_PAD_LINK_CHECK_NOTHING);
  gst_object_unref (srcpad);

  return G_OBJECT (sinkpad);
}

static GObject *
link_element_to_mixer_with_smpte (GstBin * bin, GstElement * element,
    GstElement * mixer, gint type, GstElement ** smpteref)
{
  GstPad *srcpad, *sinkpad;
  GstElement *smptealpha = gst_element_factory_make ("smptealpha", NULL);

  g_object_set (G_OBJECT (smptealpha),
      "type", (gint) type, "invert", (gboolean) TRUE, NULL);
  gst_bin_add (bin, smptealpha);

  fast_element_link (element, smptealpha);

  /* crack */
  if (smpteref) {
    *smpteref = smptealpha;
  }

  srcpad = gst_element_get_static_pad (smptealpha, "src");
  sinkpad = gst_element_get_request_pad (mixer, "sink_%d");
  gst_pad_link_full (srcpad, sinkpad, GST_PAD_LINK_CHECK_NOTHING);
  gst_object_unref (srcpad);

  return G_OBJECT (sinkpad);
}

static void
ges_track_video_transition_duration_changed (GESTrackObject * object,
    guint64 duration)
{
  GValue start_value = { 0, };
  GValue end_value = { 0, };
  GstElement *gnlobj = ges_track_object_get_gnlobject (object);
  GESTrackVideoTransition *self = GES_TRACK_VIDEO_TRANSITION (object);
  GESTrackVideoTransitionPrivate *priv = self->priv;

  GST_LOG ("updating controller");

  if (G_UNLIKELY (!gnlobj || !priv->control_source))
    return;

  GST_INFO ("duration: %" G_GUINT64_FORMAT, duration);
  g_value_init (&start_value, G_TYPE_DOUBLE);
  g_value_init (&end_value, G_TYPE_DOUBLE);
  g_value_set_double (&start_value, priv->start_value);
  g_value_set_double (&end_value, priv->end_value);

  GST_LOG ("setting values on controller");

  gst_interpolation_control_source_unset_all (priv->control_source);
  gst_interpolation_control_source_set (priv->control_source, 0, &start_value);
  gst_interpolation_control_source_set (priv->control_source,
      duration, &end_value);

  priv->dur = duration;
  GST_LOG ("done updating controller");
}

static inline void
ges_track_video_transition_set_border_internal (GESTrackVideoTransition * self,
    guint value)
{
  GESTrackVideoTransitionPrivate *priv = self->priv;

  if (!priv->smpte) {
    priv->pending_border_value = value;
    return;
  }
  g_object_set (priv->smpte, "border", value, NULL);
}

static inline void
ges_track_video_transition_set_inverted_internal (GESTrackVideoTransition *
    self, gboolean inverted)
{
  GESTrackVideoTransitionPrivate *priv = self->priv;

  if (!priv->smpte) {
    priv->pending_inverted = !inverted;
    return;
  }
  g_object_set (priv->smpte, "invert", !inverted, NULL);
}


static inline gboolean
ges_track_video_transition_set_transition_type_internal (GESTrackVideoTransition
    * self, GESVideoStandardTransitionType type)
{
  GESTrackVideoTransitionPrivate *priv = self->priv;

  GST_LOG ("%p %d => %d", self, priv->type, type);

  if (type == priv->type && !priv->pending_type) {
    GST_INFO ("This type is already set on this transition\n");
    return TRUE;
  }
  if (type == priv->pending_type) {
    GST_INFO ("This type is already pending for this transition\n");
    return TRUE;
  }
  if (priv->type &&
      ((priv->type != type) || (priv->type != priv->pending_type)) &&
      ((type == GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE) ||
          (priv->type == GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE))) {
    priv->pending_type = type;
    if (type != GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE) {
      if (!priv->topbin)
        return FALSE;
      priv->smpte = NULL;
      gst_pad_set_blocked_async (gst_element_get_static_pad (priv->topbin,
              "sinka"), TRUE, (GstPadBlockCallback) switch_to_smpte_cb, self);
    } else {
      if (!priv->topbin)
        return FALSE;
      priv->start_value = 1.0;
      priv->end_value = 0.0;
      gst_pad_set_blocked_async (gst_element_get_static_pad (priv->topbin,
              "sinka"), TRUE, (GstPadBlockCallback) switch_to_crossfade_cb,
          self);
    }
    return TRUE;
  }
  priv->pending_type = type;
  if (priv->smpte && (type != GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE)) {
    g_object_set (priv->smpte, "type", (gint) type, NULL);
  }
  return TRUE;
}

/**
 * ges_track_video_transition_set_border:
 * @self: The #GESTrackVideoTransition to set the border to
 * @value: The value of the border to set on @object
 *
 * Set the border property of @self, this value represents
 * the border width of the transition. In case this value does
 * not make sense for the current transition type, it is cached
 * for later use.
 */
void
ges_track_video_transition_set_border (GESTrackVideoTransition * self,
    guint value)
{
  ges_track_video_transition_set_border_internal (self, value);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER]);
}

/**
 * ges_track_video_transition_get_border:
 * @self: The #GESTrackVideoTransition to get the border from
 *
 * Get the border property of @self, this value represents
 * the border width of the transition.
 *
 * Returns: The border values of @self or -1 if not meaningful
 * (this will happen when not using a smpte transition).
 */
gint
ges_track_video_transition_get_border (GESTrackVideoTransition * self)
{
  gint value;

  if (!self->priv->smpte) {
    return -1;
  }

  g_object_get (self->priv->smpte, "border", &value, NULL);

  return value;
}

/**
 * ges_track_video_transition_set_inverted:
 * @self: The #GESTrackVideoTransition to set invert on
 * @inverted: %TRUE to invert the transition %FALSE otherwise
 *
 * Set the invert property of @self, this value represents
 * the direction of the transition. In case this value does
 * not make sense for the current transition type, it is cached
 * for later use.
 */
void
ges_track_video_transition_set_inverted (GESTrackVideoTransition * self,
    gboolean inverted)
{
  ges_track_video_transition_set_inverted_internal (self, inverted);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INVERT]);
}

/**
 * ges_track_video_transition_is_inverted:
 * @self: The #GESTrackVideoTransition to get the inversion from
 *
 * Get the invert property of @self, this value represents
 * the direction of the transition.
 *
 * Returns: The invert value of @self
 */
gboolean
ges_track_video_transition_is_inverted (GESTrackVideoTransition * self)
{
  gboolean inverted;

  if (!self->priv->smpte) {
    return FALSE;
  }

  g_object_get (self->priv->smpte, "invert", &inverted, NULL);

  return !inverted;
}

/**
 * ges_track_video_transition_set_transition_type:
 * @self: a #GESTrackVideoTransition
 * @type: a #GESVideoStandardTransitionType
 *
 * Sets the transition being used to @type.
 *
 * Returns: %TRUE if the transition type was properly changed, else %FALSE.
 */
gboolean
ges_track_video_transition_set_transition_type (GESTrackVideoTransition * self,
    GESVideoStandardTransitionType type)
{
  gboolean ret =
      ges_track_video_transition_set_transition_type_internal (self, type);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSITION_TYPE]);

  return ret;
}

/**
 * ges_track_video_transition_get_transition_type:
 * @trans: a #GESTrackVideoTransition
 *
 * Get the transition type used by @trans.
 *
 * Returns: The transition type used by @trans.
 */
GESVideoStandardTransitionType
ges_track_video_transition_get_transition_type (GESTrackVideoTransition * trans)
{
  if (trans->priv->pending_type)
    return trans->priv->pending_type;
  return trans->priv->type;
}

/**
 * ges_track_video_transition_new:
 *
 * Creates a new #GESTrackVideoTransition.
 *
 * Returns: The newly created #GESTrackVideoTransition, or %NULL if there was an
 * error.
 */
GESTrackVideoTransition *
ges_track_video_transition_new (void)
{
  return g_object_new (GES_TYPE_TRACK_VIDEO_TRANSITION, NULL);
}
