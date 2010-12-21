/* GStreamer encoding profile registry
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
 *           (C) 2010 Nokia Corporation
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

#include <locale.h>
#include <string.h>
#include "encoding-target.h"

/*
 * File format
 *
 * GKeyFile style.
 *
 * [_gstencodingtarget_]
 * name : <name>
 * category : <category>
 * description : <description> #translatable
 *
 * [profile-<profile1name>]
 * name : <name>
 * description : <description> #optional
 * format : <format>
 * preset : <preset>
 *
 * [streamprofile-<id>]
 * parent : <encodingprofile.name>[,<encodingprofile.name>..]
 * type : <type> # "audio", "video", "text"
 * format : <format>
 * preset : <preset>
 * restriction : <restriction>
 * presence : <presence>
 * pass : <pass>
 * variableframerate : <variableframerate>
 *  */

#define GST_ENCODING_TARGET_HEADER "_gstencodingtarget_"

struct _GstEncodingTarget
{
  GstMiniObject parent;

  gchar *name;
  gchar *category;
  gchar *description;
  GList *profiles;

  /*< private > */
  gchar *keyfile;
};

G_DEFINE_TYPE (GstEncodingTarget, gst_encoding_target, GST_TYPE_MINI_OBJECT);

static void
gst_encoding_target_init (GstEncodingTarget * target)
{
  /* Nothing to initialize */
}

static void
gst_encoding_target_finalize (GstEncodingTarget * target)
{
  GST_DEBUG ("Finalizing");

  if (target->name)
    g_free (target->name);
  if (target->category)
    g_free (target->category);
  if (target->description)
    g_free (target->description);

  g_list_foreach (target->profiles, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (target->profiles);
}

static void
gst_encoding_target_class_init (GstMiniObjectClass * klass)
{
  klass->finalize =
      (GstMiniObjectFinalizeFunction) gst_encoding_target_finalize;
}

/**
 * gst_encoding_target_get_name:
 * @target: a #GstEncodingTarget
 *
 * Since: 0.10.32
 *
 * Returns: (transfer none): The name of the @target.
 */
const gchar *
gst_encoding_target_get_name (GstEncodingTarget * target)
{
  return target->name;
}

/**
 * gst_encoding_target_get_category:
 * @target: a #GstEncodingTarget
 *
 * Since: 0.10.32
 *
 * Returns: (transfer none): The category of the @target.
 */
const gchar *
gst_encoding_target_get_category (GstEncodingTarget * target)
{
  return target->category;
}

/**
 * gst_encoding_target_get_description:
 * @target: a #GstEncodingTarget
 *
 * Since: 0.10.32
 *
 * Returns: (transfer none): The description of the @target.
 */
const gchar *
gst_encoding_target_get_description (GstEncodingTarget * target)
{
  return target->description;
}

/**
 * gst_encoding_target_get_profiles:
 * @target: a #GstEncodingTarget
 *
 * Since: 0.10.32
 *
 * Returns: (transfer none) (element-type Gst.EncodingProfile): A list of
 * #GstEncodingProfile(s) this @target handles.
 */
const GList *
gst_encoding_target_get_profiles (GstEncodingTarget * target)
{
  return target->profiles;
}

static inline gboolean
validate_name (const gchar * name)
{
  guint i, len;

  len = strlen (name);
  if (len == 0)
    return FALSE;

  /* First character can only be a lower case ASCII character */
  if (!g_ascii_isalpha (name[0]) || !g_ascii_islower (name[0]))
    return FALSE;

  /* All following characters can only by:
   * either a lower case ASCII character
   * or an hyphen
   * or a numeric */
  for (i = 1; i < len; i++) {
    /* if uppercase ASCII letter, return */
    if (g_ascii_isupper (name[i]))
      return FALSE;
    /* if a digit, continue */
    if (g_ascii_isdigit (name[i]))
      continue;
    /* if an hyphen, continue */
    if (name[i] == '-')
      continue;
    /* remaining should only be ascii letters */
    if (!g_ascii_isalpha (name[i]))
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_encoding_target_new:
 * @name: The name of the target.
 * @category: (transfer none): The name of the category to which this @target
 * belongs.
 * @description: (transfer none): A description of #GstEncodingTarget in the
 * current locale.
 * @profiles: (transfer none) (element-type Gst.EncodingProfile): A #GList of
 * #GstEncodingProfile.
 *
 * Creates a new #GstEncodingTarget.
 *
 * The name and category can only consist of lowercase ASCII letters for the
 * first character, followed by either lowercase ASCII letters, digits or
 * hyphens ('-').
 *
 * Since: 0.10.32
 *
 * Returns: (transfer full): The newly created #GstEncodingTarget or %NULL if
 * there was an error.
 */

GstEncodingTarget *
gst_encoding_target_new (const gchar * name, const gchar * category,
    const gchar * description, const GList * profiles)
{
  GstEncodingTarget *res;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (category != NULL, NULL);
  g_return_val_if_fail (description != NULL, NULL);

  /* Validate name */
  if (!validate_name (name))
    goto invalid_name;
  if (!validate_name (category))
    goto invalid_category;

  res = (GstEncodingTarget *) gst_mini_object_new (GST_TYPE_ENCODING_TARGET);
  res->name = g_strdup (name);
  res->category = g_strdup (category);
  res->description = g_strdup (description);

  while (profiles) {
    GstEncodingProfile *prof = (GstEncodingProfile *) profiles->data;

    res->profiles =
        g_list_append (res->profiles, gst_encoding_profile_ref (prof));
    profiles = profiles->next;
  }

  return res;

invalid_name:
  {
    GST_ERROR ("Invalid name for encoding category : '%s'", name);
    return NULL;
  }

invalid_category:
  {
    GST_ERROR ("Invalid category for encoding category : '%s'", category);
    return NULL;
  }
}

/**
 * gst_encoding_target_add_profile:
 * @target: the #GstEncodingTarget to add a profile to
 * @profile: (transfer full): the #GstEncodingProfile to add
 *
 * Adds the given @profile to the @target.
 *
 * The @target will steal a reference to the @profile. If you wish to use
 * the profile after calling this method, you should increase its reference
 * count.
 *
 * Since: 0.10.32
 *
 * Returns: %TRUE if the profile was added, else %FALSE.
 **/

gboolean
gst_encoding_target_add_profile (GstEncodingTarget * target,
    GstEncodingProfile * profile)
{
  GList *tmp;

  g_return_val_if_fail (GST_IS_ENCODING_TARGET (target), FALSE);
  g_return_val_if_fail (GST_IS_ENCODING_PROFILE (profile), FALSE);

  /* Make sure profile isn't already controlled by this target */
  for (tmp = target->profiles; tmp; tmp = tmp->next) {
    GstEncodingProfile *prof = (GstEncodingProfile *) tmp->data;

    if (!g_strcmp0 (gst_encoding_profile_get_name (profile),
            gst_encoding_profile_get_name (prof))) {
      GST_WARNING ("Profile already present in target");
      return FALSE;
    }
  }

  target->profiles = g_list_append (target->profiles, profile);

  return TRUE;
}

static gboolean
serialize_stream_profiles (GKeyFile * out, GstEncodingProfile * sprof,
    const gchar * profilename, guint id)
{
  gchar *sprofgroupname;
  gchar *tmpc;
  const GstCaps *format, *restriction;
  const gchar *preset, *name, *description;

  sprofgroupname = g_strdup_printf ("streamprofile-%s-%d", profilename, id);

  /* Write the parent profile */
  g_key_file_set_value (out, sprofgroupname, "parent", profilename);

  g_key_file_set_value (out, sprofgroupname, "type",
      gst_encoding_profile_get_type_nick (sprof));

  format = gst_encoding_profile_get_format (sprof);
  if (format) {
    tmpc = gst_caps_to_string (format);
    g_key_file_set_value (out, sprofgroupname, "format", tmpc);
    g_free (tmpc);
  }

  name = gst_encoding_profile_get_name (sprof);
  if (name)
    g_key_file_set_string (out, sprofgroupname, "name", name);

  description = gst_encoding_profile_get_description (sprof);
  if (description)
    g_key_file_set_string (out, sprofgroupname, "description", description);

  preset = gst_encoding_profile_get_preset (sprof);
  if (preset)
    g_key_file_set_string (out, sprofgroupname, "preset", preset);

  restriction = gst_encoding_profile_get_restriction (sprof);
  if (restriction) {
    tmpc = gst_caps_to_string (restriction);
    g_key_file_set_value (out, sprofgroupname, "restriction", tmpc);
    g_free (tmpc);
  }
  g_key_file_set_integer (out, sprofgroupname, "presence",
      gst_encoding_profile_get_presence (sprof));

  if (GST_IS_ENCODING_VIDEO_PROFILE (sprof)) {
    GstEncodingVideoProfile *vp = (GstEncodingVideoProfile *) sprof;

    g_key_file_set_integer (out, sprofgroupname, "pass",
        gst_encoding_video_profile_get_pass (vp));
    g_key_file_set_boolean (out, sprofgroupname, "variableframerate",
        gst_encoding_video_profile_get_variableframerate (vp));
  }

  g_free (sprofgroupname);
  return TRUE;
}

/* Serialize the top-level profiles
 * Note: They don't have to be containerprofiles */
static gboolean
serialize_encoding_profile (GKeyFile * out, GstEncodingProfile * prof)
{
  gchar *profgroupname;
  const GList *tmp;
  guint i;
  const gchar *profname, *profdesc, *profpreset, *proftype;
  const GstCaps *profformat, *profrestriction;

  profname = gst_encoding_profile_get_name (prof);
  profdesc = gst_encoding_profile_get_description (prof);
  profformat = gst_encoding_profile_get_format (prof);
  profpreset = gst_encoding_profile_get_preset (prof);
  proftype = gst_encoding_profile_get_type_nick (prof);
  profrestriction = gst_encoding_profile_get_restriction (prof);

  profgroupname = g_strdup_printf ("profile-%s", profname);

  g_key_file_set_string (out, profgroupname, "name", profname);

  g_key_file_set_value (out, profgroupname, "type",
      gst_encoding_profile_get_type_nick (prof));

  if (profdesc)
    g_key_file_set_locale_string (out, profgroupname, "description",
        setlocale (LC_ALL, NULL), profdesc);
  if (profformat) {
    gchar *tmpc = gst_caps_to_string (profformat);
    g_key_file_set_string (out, profgroupname, "format", tmpc);
    g_free (tmpc);
  }
  if (profpreset)
    g_key_file_set_string (out, profgroupname, "preset", profpreset);

  /* stream profiles */
  if (GST_IS_ENCODING_CONTAINER_PROFILE (prof)) {
    for (tmp =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (prof)), i = 0; tmp;
        tmp = tmp->next, i++) {
      GstEncodingProfile *sprof = (GstEncodingProfile *) tmp->data;

      if (!serialize_stream_profiles (out, sprof, profname, i))
        return FALSE;
    }
  }
  g_free (profgroupname);
  return TRUE;
}

static gboolean
serialize_target (GKeyFile * out, GstEncodingTarget * target)
{
  GList *tmp;

  g_key_file_set_string (out, GST_ENCODING_TARGET_HEADER, "name", target->name);
  g_key_file_set_string (out, GST_ENCODING_TARGET_HEADER, "category",
      target->category);
  g_key_file_set_string (out, GST_ENCODING_TARGET_HEADER, "description",
      target->description);

  for (tmp = target->profiles; tmp; tmp = tmp->next) {
    GstEncodingProfile *prof = (GstEncodingProfile *) tmp->data;
    if (!serialize_encoding_profile (out, prof))
      return FALSE;
  }

  return TRUE;
}

/**
 * parse_encoding_profile:
 * @in: a #GKeyFile
 * @parentprofilename: the parent profile name (including 'profile-' or 'streamprofile-' header)
 * @profilename: the profile name group to parse
 * @nbgroups: the number of top-level groups
 * @groups: the top-level groups
 */
static GstEncodingProfile *
parse_encoding_profile (GKeyFile * in, gchar * parentprofilename,
    gchar * profilename, gsize nbgroups, gchar ** groups)
{
  GstEncodingProfile *sprof = NULL;
  gchar **parent;
  gchar *proftype, *format, *preset, *restriction, *pname, *description;
  GstCaps *formatcaps = NULL;
  GstCaps *restrictioncaps = NULL;
  gboolean variableframerate;
  gint pass, presence;
  gsize i, nbencprofiles;

  GST_DEBUG ("parentprofilename : %s , profilename : %s",
      parentprofilename, profilename);

  if (parentprofilename) {
    gboolean found = FALSE;

    parent =
        g_key_file_get_string_list (in, profilename, "parent",
        &nbencprofiles, NULL);
    if (!parent || !nbencprofiles) {
      return NULL;
    }

    /* Check if this streamprofile is used in <profilename> */
    for (i = 0; i < nbencprofiles; i++) {
      if (!g_strcmp0 (parent[i], parentprofilename)) {
        found = TRUE;
        break;
      }
    }
    g_strfreev (parent);

    if (!found) {
      GST_DEBUG ("Stream profile '%s' isn't used in profile '%s'",
          profilename, parentprofilename);
      return NULL;
    }
  }

  pname = g_key_file_get_value (in, profilename, "name", NULL);

  /* First try to get localized description */
  description =
      g_key_file_get_locale_string (in, profilename, "description",
      setlocale (LC_ALL, NULL), NULL);
  if (description == NULL)
    description = g_key_file_get_value (in, profilename, "description", NULL);

  /* Parse the remaining fields */
  proftype = g_key_file_get_value (in, profilename, "type", NULL);
  if (!proftype) {
    GST_WARNING ("Missing 'type' field for streamprofile %s", profilename);
    return NULL;
  }

  format = g_key_file_get_value (in, profilename, "format", NULL);
  if (format) {
    formatcaps = gst_caps_from_string (format);
    g_free (format);
  }

  preset = g_key_file_get_value (in, profilename, "preset", NULL);

  restriction = g_key_file_get_value (in, profilename, "restriction", NULL);
  if (restriction) {
    restrictioncaps = gst_caps_from_string (restriction);
    g_free (restriction);
  }

  presence = g_key_file_get_integer (in, profilename, "presence", NULL);
  pass = g_key_file_get_integer (in, profilename, "pass", NULL);
  variableframerate =
      g_key_file_get_boolean (in, profilename, "variableframerate", NULL);

  /* Build the streamprofile ! */
  if (!g_strcmp0 (proftype, "container")) {
    GstEncodingProfile *pprof;

    sprof =
        (GstEncodingProfile *) gst_encoding_container_profile_new (pname,
        description, formatcaps, preset);
    /* Now look for the stream profiles */
    for (i = 0; i < nbgroups; i++) {
      if (!g_ascii_strncasecmp (groups[i], "streamprofile-", 13)) {
        pprof = parse_encoding_profile (in, pname, groups[i], nbgroups, groups);
        if (pprof) {
          gst_encoding_container_profile_add_profile (
              (GstEncodingContainerProfile *) sprof, pprof);
        }
      }
    }
  } else if (!g_strcmp0 (proftype, "video")) {
    sprof =
        (GstEncodingProfile *) gst_encoding_video_profile_new (formatcaps,
        preset, restrictioncaps, presence);
    gst_encoding_video_profile_set_variableframerate ((GstEncodingVideoProfile
            *) sprof, variableframerate);
    gst_encoding_video_profile_set_pass ((GstEncodingVideoProfile *) sprof,
        pass);
  } else if (!g_strcmp0 (proftype, "audio")) {
    sprof =
        (GstEncodingProfile *) gst_encoding_audio_profile_new (formatcaps,
        preset, restrictioncaps, presence);
  } else
    GST_ERROR ("Unknown profile format '%s'", proftype);

  if (restrictioncaps)
    gst_caps_unref (restrictioncaps);
  if (formatcaps)
    gst_caps_unref (formatcaps);

  if (pname)
    g_free (pname);
  if (description)
    g_free (description);
  if (preset)
    g_free (preset);
  if (proftype)
    g_free (proftype);

  return sprof;
}

static GstEncodingTarget *
parse_keyfile (GKeyFile * in, gchar * targetname, gchar * categoryname,
    gchar * description)
{
  GstEncodingTarget *res = NULL;
  GstEncodingProfile *prof;
  gchar **groups;
  gsize i, nbgroups;

  res = gst_encoding_target_new (targetname, categoryname, description, NULL);

  /* Figure out the various profiles */
  groups = g_key_file_get_groups (in, &nbgroups);
  for (i = 0; i < nbgroups; i++) {
    if (!g_ascii_strncasecmp (groups[i], "profile-", 8)) {
      prof = parse_encoding_profile (in, NULL, groups[i], nbgroups, groups);
      if (prof)
        gst_encoding_target_add_profile (res, prof);
    }
  }

  g_strfreev (groups);

  if (targetname)
    g_free (targetname);
  if (categoryname)
    g_free (categoryname);
  if (description)
    g_free (description);

  return res;
}

static GKeyFile *
load_file_and_read_header (const gchar * path, gchar ** targetname,
    gchar ** categoryname, gchar ** description, GError ** error)
{
  GKeyFile *in;
  gboolean res;

  in = g_key_file_new ();

  GST_DEBUG ("path:%s", path);

  res =
      g_key_file_load_from_file (in, path,
      G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error);
  if (!res || error != NULL)
    goto load_error;

  *targetname =
      g_key_file_get_value (in, GST_ENCODING_TARGET_HEADER, "name", error);
  if (!*targetname)
    goto empty_name;

  *categoryname =
      g_key_file_get_value (in, GST_ENCODING_TARGET_HEADER, "category", NULL);
  *description =
      g_key_file_get_value (in, GST_ENCODING_TARGET_HEADER, "description",
      NULL);

  return in;

load_error:
  {
    GST_WARNING ("Unable to read GstEncodingTarget file %s: %s",
        path, (*error)->message);
    g_key_file_free (in);
    return NULL;
  }

empty_name:
  {
    GST_WARNING ("Wrong header in file %s: %s", path, (*error)->message);
    g_key_file_free (in);
    return NULL;
  }
}

/**
 * gst_encoding_target_load_from:
 * @path: The file to load the #GstEncodingTarget from
 * @error: If an error occured, this field will be filled in.
 *
 * Opens the provided file and returns the contained #GstEncodingTarget.
 *
 * Since: 0.10.32
 *
 * Returns: (transfer full): The #GstEncodingTarget contained in the file, else
 * %NULL
 */

GstEncodingTarget *
gst_encoding_target_load_from (const gchar * path, GError ** error)
{
  GKeyFile *in;
  gchar *targetname, *categoryname, *description;
  GstEncodingTarget *res = NULL;

  in = load_file_and_read_header (path, &targetname, &categoryname,
      &description, error);
  if (!in)
    goto beach;

  res = parse_keyfile (in, targetname, categoryname, description);

  g_key_file_free (in);

beach:
  return res;
}

/**
 * gst_encoding_target_load:
 * @name: the name of the #GstEncodingTarget to load.
 * @error: If an error occured, this field will be filled in.
 *
 * Searches for the #GstEncodingTarget with the given name, loads it
 * and returns it.
 *
 * Warning: NOT IMPLEMENTED.
 *
 * Since: 0.10.32
 *
 * Returns: (transfer full): The #GstEncodingTarget if available, else %NULL.
 */

GstEncodingTarget *
gst_encoding_target_load (const gchar * name, GError ** error)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

/**
 * gst_encoding_target_save:
 * @target: a #GstEncodingTarget
 * @error: If an error occured, this field will be filled in.
 *
 * Saves the @target to the default location.
 *
 * Warning: NOT IMPLEMENTED.
 *
 * Since: 0.10.32
 *
 * Returns: %TRUE if the target was correctly saved, else %FALSE.
 **/

gboolean
gst_encoding_target_save (GstEncodingTarget * target, GError ** error)
{
  g_return_val_if_fail (GST_IS_ENCODING_TARGET (target), FALSE);

  /* FIXME : IMPLEMENT */
  return FALSE;
}

/**
 * gst_encoding_target_save_to:
 * @target: a #GstEncodingTarget
 * @path: the location to store the @target at.
 * @error: If an error occured, this field will be filled in.
 *
 * Saves the @target to the provided location.
 *
 * Since: 0.10.32
 *
 * Returns: %TRUE if the target was correctly saved, else %FALSE.
 **/

gboolean
gst_encoding_target_save_to (GstEncodingTarget * target, const gchar * path,
    GError ** error)
{
  GKeyFile *out;
  gchar *data;
  gsize data_size;

  g_return_val_if_fail (GST_IS_ENCODING_TARGET (target), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);

  /* FIXME : Check path is valid and writable
   * FIXME : Strip out profiles already present in system target */

  /* Get unique name... */

  /* Create output GKeyFile */
  out = g_key_file_new ();

  if (!serialize_target (out, target))
    goto serialize_failure;

  if (!(data = g_key_file_to_data (out, &data_size, error)))
    goto convert_failed;

  if (!g_file_set_contents (path, data, data_size, error))
    goto write_failed;

  g_key_file_free (out);
  g_free (data);

  return TRUE;

serialize_failure:
  {
    GST_ERROR ("Failure serializing target");
    g_key_file_free (out);
    return FALSE;
  }

convert_failed:
  {
    GST_ERROR ("Failure converting keyfile: %s", (*error)->message);
    g_key_file_free (out);
    g_free (data);
    return FALSE;
  }

write_failed:
  {
    GST_ERROR ("Unable to write file %s: %s", path, (*error)->message);
    g_key_file_free (out);
    g_free (data);
    return FALSE;
  }
}
