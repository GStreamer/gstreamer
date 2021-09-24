/* GStreamer encoding profiles library
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *           (C) 2009 Nokia Corporation
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

#ifndef __GST_PROFILE_H__
#define __GST_PROFILE_H__

#include <gst/gst.h>

typedef enum {
  GST_ENCODING_PROFILE_UNKNOWN,
  GST_ENCODING_PROFILE_VIDEO,
  GST_ENCODING_PROFILE_AUDIO,
  GST_ENCODING_PROFILE_TEXT
  /* Room for extenstion */
} GstEncodingProfileType;

typedef struct _GstEncodingTarget GstEncodingTarget;
typedef struct _GstEncodingProfile GstEncodingProfile;
typedef struct _GstStreamEncodingProfile GstStreamEncodingProfile;
typedef struct _GstVideoEncodingProfile GstVideoEncodingProfile;

/* FIXME/UNKNOWNS
 *
 * Should encoding categories be well-known strings/quarks ?
 *
 */

/**
 * GstEncodingTarget:
 * @name: The name of the target profile.
 * @category: The target category (device, service, use-case).
 * @profiles: A list of #GstProfile this device supports.
 *
 */
struct _GstEncodingTarget {
  gchar     *name;
  gchar     *category;
  GList     *profiles;
}

/**
 * GstEncodingProfile:
 * @name: The name of the profile
 * @format: The GStreamer mime type corresponding to the muxing format.
 * @preset: The name of the #GstPreset(s) to be used on the muxer. This is optional.
 * @multipass: Whether this profile is a multi-pass profile or not.
 * @encodingprofiles: A list of #GstStreamEncodingProfile for the various streams.
 *
 */

struct _GstEncodingProfile {
  gchar	        *name;
  gchar         *format;
  gchar         *preset;
  gboolean       multipass;
  GList         *encodingprofiles;
};

/**
 * GstStreamEncodingProfile:
 * @type: Type of profile
 * @format: The GStreamer mime type corresponding to the encoding format.
 * @preset: The name of the #GstPreset to be used on the encoder. This is optional.
 * @restriction: The #GstCaps restricting the input. This is optional.
 * @presence: The number of streams that can be created. 0 => any.
 */
struct _GstStreamEncodingProfile {
  GstEncodingProfileType   type;
  gchar                   *format;
  gchar                   *preset;
  GstCaps                 *restriction;
  guint                    presence;
};

/**
 * GstVideoEncodingProfile:
 * @profile: common #GstEncodingProfile part.
 * @pass: The pass number if this is part of a multi-pass profile. Starts at 1
 * for multi-pass. Set to 0 if this is not part of a multi-pass profile.
 * @variable_framerate: Do not enforce framerate on incoming raw stream. Default
 * is FALSE.
 */
struct _GstVideoEncodingProfile {
  GstStreamEncodingProfile      profile;
  guint                         pass;
  gboolean                      variable_framerate;
};

/* Generic helper API */
/**
 * gst_encoding_category_list_target:
 * @category: a profile target category name. Can be NULL.
 * 
 * Returns the list of all available #GstProfileTarget for the given @category.
 * If @category is #NULL, then all available #GstProfileTarget are returned.
 */
GList *gst_encoding_category_list_target (gchar *category);

/**
 * list available profile target categories
 */
GList *gst_profile_list_target_categories ();

gboolean gst_profile_target_save (GstProfileTarget *target);

/**
 * gst_encoding_profile_get_input_caps:
 * @profile: a #GstEncodingProfile
 *
 * Returns: the list of all caps the profile can accept. Caller must call
 * gst_cap_unref on all unwanted caps once it is done with the list.
 */
GList * gst_profile_get_input_caps (GstEncodingProfile *profile);

/*
 * Application convenience methods (possibly to be added in gst-pb-utils)
 */

/**
 * gst_pb_utils_create_encoder:
 * @caps: The #GstCaps corresponding to a codec format
 * @preset: The name of a preset
 * @name: The name to give to the returned instance, can be #NULL.
 *
 * Creates an encoder which can output the given @caps. If several encoders can
 * output the given @caps, then the one with the highest rank will be picked.
 * If a @preset is specified, it will be applied to the created encoder before
 * returning it.
 * If a @preset is specified, then the highest-ranked encoder that can accept
 * the givein preset will be returned.
 *
 * Returns: The encoder instance with the preset applied if it is available.
 * #NULL if no encoder is available.
 */
GstElement *gst_pb_utils_create_encoder(GstCaps *caps, gchar *preset, gchar *name);
/**
 * gst_pb_utils_create_encoder_format:
 *
 * Convenience version of @gst_pb_utils_create_encoder except one does not need
 * to create a #GstCaps.
 */
GstElement *gst_pb_utils_create_encoder_format(gchar *format, gchar *preset,
					       gchar *name);

/**
 * gst_pb_utils_create_muxer:
 * @caps: The #GstCaps corresponding to a codec format
 * @preset: The name of a preset
 *
 * Creates an muxer which can output the given @caps. If several muxers can
 * output the given @caps, then the one with the highest rank will be picked.
 * If a @preset is specified, it will be applied to the created muxer before
 * returning it.
 * If a @preset is specified, then the highest-ranked muxer that can accept
 * the givein preset will be returned.
 *
 * Returns: The muxer instance with the preset applied if it is available.
 * #NULL if no muxer is available.
 */
GstElement *gst_pb_utils_create_muxer(GstCaps *caps, gchar *preset);
/**
 * gst_pb_utils_create_muxer_format:
 *
 * Convenience version of @gst_pb_utils_create_muxer except one does not need
 * to create a #GstCaps.
 */
GstElement *gst_pb_utils_create_muxer_format(gchar *format, gchar *preset,
					       gchar *name);

/**
 * gst_pb_utils_encoders_compatible_with_muxer:
 * @muxer: a muxer instance
 *
 * Finds a list of available encoders whose output can be fed to the given
 * @muxer.
 *
 * Returns: A list of compatible encoders, or #NULL if none can be found.
 */
GList *gst_pb_utils_encoders_compatible_with_muxer(GstElement *muxer);

GList *gst_pb_utils_muxers_compatible_with_encoder(GstElement *encoder);


/*
 * GstPreset modifications
 */

/**
 * gst_preset_create:
 * @preset: The #GstPreset on which to create the preset
 * @name: A name for the preset
 * @properties: The properties
 *
 * Creates a new preset with the given properties. This preset will only
 * exist during the lifetime of the process.
 * If you wish to use it after the lifetime of the process, you must call
 * @gst_preset_save_preset.
 *
 * Returns: #TRUE if the preset could be created, else #FALSE.
 */
gboolean gst_preset_create (GstPreset *preset, gchar *name,
			    GstStructure *properties);

/**
 * gst_preset_reset:
 * @preset: a #GstPreset
 *
 * Sets all the properties of the element back to their default values.
 */
/* FIXME : This could actually be put at the GstObject level, or maybe even
 * at the GObject level. */
void gst_preset_reset (GstPreset *preset);

#endif /* __GST_PROFILE_H__ */
