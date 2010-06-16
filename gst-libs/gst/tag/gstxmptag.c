/* GStreamer
 * Copyright (C) 2010 Stefan Kost <stefan.kost@nokia.com>
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
 *
 * gstxmptag.c: library for reading / modifying xmp tags
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
 * SECTION:gsttagxmp
 * @short_description: tag mappings and support functions for plugins
 *                     dealing with xmp packets
 * @see_also: #GstTagList
 *
 * Contains various utility functions for plugins to parse or create
 * xmp packets and map them to and from #GstTagList<!-- -->s.
 *
 * Please note that the xmp parser is very lightweight and not strict at all.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gsttagsetter.h>
#include "gsttageditingprivate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Serializes a GValue into a string.
 */
typedef gchar *(*XmpSerializationFunc) (const GValue * value);

/*
 * Deserializes @str that is the gstreamer tag @gst_tag represented in
 * XMP as the @xmp_tag and adds the result to the @taglist.
 *
 * @pending_tags is passed so that compound xmp tags can search for its
 * complements on the list and use them. Note that used complements should
 * be freed and removed from the list.
 * The list is of PendingXmpTag
 */
typedef void (*XmpDeserializationFunc) (GstTagList * taglist,
    const gchar * gst_tag, const gchar * xmp_tag,
    const gchar * str, GSList ** pending_tags);

struct _XmpTag
{
  const gchar *tag_name;
  XmpSerializationFunc serialize;
  XmpDeserializationFunc deserialize;
};
typedef struct _XmpTag XmpTag;

struct _PendingXmpTag
{
  const gchar *gst_tag;
  XmpTag *xmp_tag;
  gchar *str;
};
typedef struct _PendingXmpTag PendingXmpTag;

/*
 * Mappings from gstreamer tags to xmp tags
 *
 * The mapping here are from a gstreamer tag (as a GQuark)
 * into a GSList of GArray of XmpTag.
 *
 * There might be multiple xmp tags that a single gstreamer tag can be
 * mapped to. For example, GST_TAG_DATE might be mapped into dc:date
 * or exif:DateTimeOriginal, hence the first list, to be able to store
 * alternative mappings of the same gstreamer tag.
 *
 * Some other tags, like GST_TAG_GEO_LOCATION_ELEVATION needs to be
 * mapped into 2 complementary tags in the exif's schema. One of them
 * stores the absolute elevation, and the other one stores if it is
 * above of below sea level. That's why we need a GArray as the item
 * of each GSList in the mapping.
 */
static GHashTable *__xmp_tag_map;
static GMutex *__xmp_tag_map_mutex;

#define XMP_TAG_MAP_LOCK g_mutex_lock (__xmp_tag_map_mutex)
#define XMP_TAG_MAP_UNLOCK g_mutex_unlock (__xmp_tag_map_mutex)

static void
_xmp_tag_add_mapping (const gchar * gst_tag, GPtrArray * array)
{
  GQuark key;
  GSList *list = NULL;

  key = g_quark_from_string (gst_tag);

  XMP_TAG_MAP_LOCK;
  list = g_hash_table_lookup (__xmp_tag_map, GUINT_TO_POINTER (key));
  list = g_slist_append (list, (gpointer) array);
  g_hash_table_insert (__xmp_tag_map, GUINT_TO_POINTER (key), list);
  XMP_TAG_MAP_UNLOCK;
}

static void
_xmp_tag_add_simple_mapping (const gchar * gst_tag, const gchar * xmp_tag,
    XmpSerializationFunc serialization_func,
    XmpDeserializationFunc deserialization_func)
{
  XmpTag *xmpinfo;
  GPtrArray *array;

  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = xmp_tag;
  xmpinfo->serialize = serialization_func;
  xmpinfo->deserialize = deserialization_func;

  array = g_ptr_array_sized_new (1);
  g_ptr_array_add (array, xmpinfo);

  _xmp_tag_add_mapping (gst_tag, array);
}

/*
 * We do not return a copy here because elements are
 * appended, and the API is not public, so we shouldn't
 * have our lists modified during usage
 */
static GSList *
_xmp_tag_get_mapping (const gchar * gst_tag)
{
  GSList *ret = NULL;
  GQuark key = g_quark_from_string (gst_tag);

  XMP_TAG_MAP_LOCK;
  ret = (GSList *) g_hash_table_lookup (__xmp_tag_map, GUINT_TO_POINTER (key));
  XMP_TAG_MAP_UNLOCK;

  return ret;
}

/* finds the gst tag that maps to this xmp tag */
static const gchar *
_xmp_tag_get_mapping_reverse (const gchar * xmp_tag, XmpTag ** _xmp_tag)
{
  GHashTableIter iter;
  gpointer key, value;
  const gchar *ret = NULL;
  GSList *walk;
  gint index;

  XMP_TAG_MAP_LOCK;

  /* Iterate over the hashtable */
  g_hash_table_iter_init (&iter, __xmp_tag_map);
  while (!ret && g_hash_table_iter_next (&iter, &key, &value)) {
    GSList *list = (GSList *) value;

    /* each entry might contain multiple mappigns */
    for (walk = list; walk; walk = g_slist_next (walk)) {
      GPtrArray *array = (GPtrArray *) walk->data;

      /* each mapping might contain complementary tags */
      for (index = 0; index < array->len; index++) {
        XmpTag *xmpinfo = (XmpTag *) g_ptr_array_index (array, index);

        if (strcmp (xmpinfo->tag_name, xmp_tag) == 0) {
          *_xmp_tag = xmpinfo;
          ret = g_quark_to_string (GPOINTER_TO_UINT (key));
          goto out;
        }
      }
    }
  }

out:
  XMP_TAG_MAP_UNLOCK;
  return ret;
}

/* utility functions/macros */

#define METERS_PER_SECOND_TO_KILOMETERS_PER_HOUR (3.6)
#define KILOMETERS_PER_HOUR_TO_METERS_PER_SECOND (1/3.6)
#define MILES_PER_HOUR_TO_METERS_PER_SECOND (0.44704)
#define KNOTS_TO_METERS_PER_SECOND (0.514444)

static gchar *
double_to_fraction_string (gdouble num)
{
  gint frac_n;
  gint frac_d;

  gst_util_double_to_fraction (num, &frac_n, &frac_d);
  return g_strdup_printf ("%d/%d", frac_n, frac_d);
}

/* (de)serialize functions */
static gchar *
serialize_exif_gps_coordinate (const GValue * value, gchar pos, gchar neg)
{
  gdouble num;
  gchar c;
  gint integer;
  gchar fraction[G_ASCII_DTOSTR_BUF_SIZE];

  g_return_val_if_fail (G_VALUE_TYPE (value) == G_TYPE_DOUBLE, NULL);

  num = g_value_get_double (value);
  if (num < 0) {
    c = neg;
    num *= -1;
  } else {
    c = pos;
  }
  integer = (gint) num;

  g_ascii_dtostr (fraction, sizeof (fraction), (num - integer) * 60);

  /* FIXME review GPSCoordinate serialization spec for the .mm or ,ss
   * decision. Couldn't understand it clearly */
  return g_strdup_printf ("%d,%s%c", integer, fraction, c);
}

static gchar *
serialize_exif_latitude (const GValue * value)
{
  return serialize_exif_gps_coordinate (value, 'N', 'S');
}

static gchar *
serialize_exif_longitude (const GValue * value)
{
  return serialize_exif_gps_coordinate (value, 'E', 'W');
}

static void
deserialize_exif_gps_coordinate (GstTagList * taglist, const gchar * gst_tag,
    const gchar * str, gchar pos, gchar neg)
{
  gdouble value = 0;
  gint d = 0, m = 0, s = 0;
  gdouble m2 = 0;
  gchar c = 0;
  const gchar *current;

  /* get the degrees */
  if (sscanf (str, "%d", &d) != 1)
    goto error;

  /* find the beginning of the minutes */
  current = strchr (str, ',');
  if (current == NULL)
    goto end;
  current += 1;

  /* check if it uses ,SS or .mm */
  if (strchr (current, ',') != NULL) {
    sscanf (current, "%d,%d%c", &m, &s, &c);
  } else {
    gchar *copy = g_strdup (current);
    gint len = strlen (copy);
    gint i;

    /* check the last letter */
    for (i = len - 1; len >= 0; len--) {
      if (g_ascii_isspace (copy[i]))
        continue;

      if (g_ascii_isalpha (copy[i])) {
        /* found it */
        c = copy[i];
        copy[i] = '\0';
        break;

      } else {
        /* something is wrong */
        g_free (copy);
        goto error;
      }
    }

    /* use a copy so we can change the last letter as E can cause
     * problems here */
    m2 = g_ascii_strtod (copy, NULL);
    g_free (copy);
  }

end:
  /* we can add them all as those that aren't parsed are 0 */
  value = d + (m / 60.0) + (s / (60.0 * 60.0)) + (m2 / 60.0);

  if (c == pos) {
    //NOP
  } else if (c == neg) {
    value *= -1;
  } else {
    goto error;
  }

  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, gst_tag, value, NULL);
  return;

error:
  GST_WARNING ("Failed to deserialize gps coordinate: %s", str);
}

static void
deserialize_exif_latitude (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  deserialize_exif_gps_coordinate (taglist, gst_tag, str, 'N', 'S');
}

static void
deserialize_exif_longitude (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  deserialize_exif_gps_coordinate (taglist, gst_tag, str, 'E', 'W');
}

static gchar *
serialize_exif_altitude (const GValue * value)
{
  gdouble num;

  num = g_value_get_double (value);

  if (num < 0)
    num *= -1;

  return double_to_fraction_string (num);
}

static gchar *
serialize_exif_altituderef (const GValue * value)
{
  gdouble num;

  num = g_value_get_double (value);

  /* 0 means above sea level, 1 means below */
  if (num >= 0)
    return g_strdup ("0");
  return g_strdup ("1");
}

static void
deserialize_exif_altitude (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  const gchar *altitude_str = NULL;
  const gchar *altituderef_str = NULL;
  gint frac_n;
  gint frac_d;
  gdouble value;

  GSList *entry;
  PendingXmpTag *ptag = NULL;

  /* find the other missing part */
  if (strcmp (xmp_tag, "exif:GPSAltitude") == 0) {
    altitude_str = str;

    for (entry = *pending_tags; entry; entry = g_slist_next (entry)) {
      ptag = (PendingXmpTag *) entry->data;

      if (strcmp (ptag->xmp_tag->tag_name, "exif:GPSAltitudeRef") == 0) {
        altituderef_str = ptag->str;
        break;
      }
    }

  } else if (strcmp (xmp_tag, "exif:GPSAltitudeRef") == 0) {
    altituderef_str = str;

    for (entry = *pending_tags; entry; entry = g_slist_next (entry)) {
      ptag = (PendingXmpTag *) entry->data;

      if (strcmp (ptag->xmp_tag->tag_name, "exif:GPSAltitude") == 0) {
        altitude_str = ptag->str;
        break;
      }
    }

  } else {
    GST_WARNING ("Unexpected xmp tag %s", xmp_tag);
    return;
  }

  if (!altitude_str) {
    GST_WARNING ("Missing exif:GPSAltitude tag");
    return;
  }
  if (!altituderef_str) {
    GST_WARNING ("Missing exif:GPSAltitudeRef tag");
    return;
  }

  if (sscanf (altitude_str, "%d/%d", &frac_n, &frac_d) != 2) {
    GST_WARNING ("Failed to parse fraction: %s", altitude_str);
    return;
  }

  gst_util_fraction_to_double (frac_n, frac_d, &value);

  if (altituderef_str[0] == '0') {
    /* nop */
  } else if (altituderef_str[0] == '1') {
    value *= -1;
  } else {
    GST_WARNING ("Unexpected exif:AltitudeRef value: %s", altituderef_str);
    return;
  }

  /* add to the taglist */
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_GEO_LOCATION_ELEVATION, value, NULL);

  /* clean up entry */
  g_free (ptag->str);
  g_slice_free (PendingXmpTag, ptag);
  *pending_tags = g_slist_delete_link (*pending_tags, entry);
}

static gchar *
serialize_exif_gps_speed (const GValue * value)
{
  return double_to_fraction_string (g_value_get_double (value) *
      METERS_PER_SECOND_TO_KILOMETERS_PER_HOUR);
}

static gchar *
serialize_exif_gps_speedref (const GValue * value)
{
  /* we always use km/h */
  return g_strdup ("K");
}

static void
deserialize_exif_gps_speed (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  const gchar *speed_str = NULL;
  const gchar *speedref_str = NULL;
  gint frac_n;
  gint frac_d;
  gdouble value;

  GSList *entry;
  PendingXmpTag *ptag = NULL;

  /* find the other missing part */
  if (strcmp (xmp_tag, "exif:GPSSpeed") == 0) {
    speed_str = str;

    for (entry = *pending_tags; entry; entry = g_slist_next (entry)) {
      ptag = (PendingXmpTag *) entry->data;

      if (strcmp (ptag->xmp_tag->tag_name, "exif:GPSSpeedRef") == 0) {
        speedref_str = ptag->str;
        break;
      }
    }

  } else if (strcmp (xmp_tag, "exif:GPSSpeedRef") == 0) {
    speedref_str = str;

    for (entry = *pending_tags; entry; entry = g_slist_next (entry)) {
      ptag = (PendingXmpTag *) entry->data;

      if (strcmp (ptag->xmp_tag->tag_name, "exif:GPSSpeed") == 0) {
        speed_str = ptag->str;
        break;
      }
    }

  } else {
    GST_WARNING ("Unexpected xmp tag %s", xmp_tag);
    return;
  }

  if (!speed_str) {
    GST_WARNING ("Missing exif:GPSSpeed tag");
    return;
  }
  if (!speedref_str) {
    GST_WARNING ("Missing exif:GPSSpeedRef tag");
    return;
  }

  if (sscanf (speed_str, "%d/%d", &frac_n, &frac_d) != 2) {
    GST_WARNING ("Failed to parse fraction: %s", speed_str);
    return;
  }

  gst_util_fraction_to_double (frac_n, frac_d, &value);

  if (speedref_str[0] == 'K') {
    value *= KILOMETERS_PER_HOUR_TO_METERS_PER_SECOND;
  } else if (speedref_str[0] == 'M') {
    value *= MILES_PER_HOUR_TO_METERS_PER_SECOND;
  } else if (speedref_str[0] == 'N') {
    value *= KNOTS_TO_METERS_PER_SECOND;
  } else {
    GST_WARNING ("Unexpected exif:SpeedRef value: %s", speedref_str);
    return;
  }

  /* add to the taglist */
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, value, NULL);

  /* clean up entry */
  g_free (ptag->str);
  g_slice_free (PendingXmpTag, ptag);
  *pending_tags = g_slist_delete_link (*pending_tags, entry);
}

static gchar *
serialize_exif_gps_direction (const GValue * value)
{
  return double_to_fraction_string (g_value_get_double (value));
}

static gchar *
serialize_exif_gps_directionref (const GValue * value)
{
  /* T for true geographic direction (M would mean magnetic) */
  return g_strdup ("T");
}

static void
deserialize_exif_gps_direction (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags,
    const gchar * direction_tag, const gchar * directionref_tag)
{
  const gchar *dir_str = NULL;
  const gchar *dirref_str = NULL;
  gint frac_n;
  gint frac_d;
  gdouble value;

  GSList *entry;
  PendingXmpTag *ptag = NULL;

  /* find the other missing part */
  if (strcmp (xmp_tag, direction_tag) == 0) {
    dir_str = str;

    for (entry = *pending_tags; entry; entry = g_slist_next (entry)) {
      ptag = (PendingXmpTag *) entry->data;

      if (strcmp (ptag->xmp_tag->tag_name, directionref_tag) == 0) {
        dirref_str = ptag->str;
        break;
      }
    }

  } else if (strcmp (xmp_tag, directionref_tag) == 0) {
    dirref_str = str;

    for (entry = *pending_tags; entry; entry = g_slist_next (entry)) {
      ptag = (PendingXmpTag *) entry->data;

      if (strcmp (ptag->xmp_tag->tag_name, direction_tag) == 0) {
        dir_str = ptag->str;
        break;
      }
    }

  } else {
    GST_WARNING ("Unexpected xmp tag %s", xmp_tag);
    return;
  }

  if (!dir_str) {
    GST_WARNING ("Missing %s tag", dir_str);
    return;
  }
  if (!dirref_str) {
    GST_WARNING ("Missing %s tag", dirref_str);
    return;
  }

  if (sscanf (dir_str, "%d/%d", &frac_n, &frac_d) != 2) {
    GST_WARNING ("Failed to parse fraction: %s", dir_str);
    return;
  }

  gst_util_fraction_to_double (frac_n, frac_d, &value);

  if (dirref_str[0] == 'T') {
    /* nop */
  } else if (dirref_str[0] == 'M') {
    GST_WARNING ("Magnetic direction tags aren't supported yet");
    return;
  } else {
    GST_WARNING ("Unexpected %s value: %s", directionref_tag, dirref_str);
    return;
  }

  /* add to the taglist */
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, gst_tag, value, NULL);

  /* clean up entry */
  g_free (ptag->str);
  g_slice_free (PendingXmpTag, ptag);
  *pending_tags = g_slist_delete_link (*pending_tags, entry);
}

static void
deserialize_exif_gps_track (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  deserialize_exif_gps_direction (taglist, gst_tag, xmp_tag, str, pending_tags,
      "exif:GPSTrack", "exif:GPSTrackRef");
}

static void
deserialize_exif_gps_img_direction (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  deserialize_exif_gps_direction (taglist, gst_tag, xmp_tag, str, pending_tags,
      "exif:GPSImgDirection", "exif:GPSImgDirectionRef");
}

static void
deserialize_xmp_rating (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  guint value;

  if (sscanf (str, "%u", &value) != 1) {
    GST_WARNING ("Failed to parse xmp:Rating %s", str);
    return;
  }

  if (value < 0 || value > 100) {
    GST_WARNING ("Unsupported Rating tag %u (should be from 0 to 100), "
        "ignoring", value);
    return;
  }

  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, gst_tag, value, NULL);
}

static gchar *
serialize_tiff_orientation (const GValue * value)
{
  const gchar *str;
  gint num;

  str = g_value_get_string (value);
  if (str == NULL) {
    GST_WARNING ("Failed to get image orientation tag value");
    return NULL;
  }

  num = gst_tag_image_orientation_to_exif_value (str);
  if (num == -1)
    return NULL;

  return g_strdup_printf ("%d", num);
}

static void
deserialize_tiff_orientation (GstTagList * taglist, const gchar * gst_tag,
    const gchar * xmp_tag, const gchar * str, GSList ** pending_tags)
{
  guint value;
  const gchar *orientation = NULL;

  if (sscanf (str, "%u", &value) != 1) {
    GST_WARNING ("Failed to parse tiff:Orientation %s", str);
    return;
  }

  if (value < 1 || value > 8) {
    GST_WARNING ("Invalid tiff:Orientation tag %u (should be from 1 to 8), "
        "ignoring", value);
    return;
  }

  orientation = gst_tag_image_orientation_from_exif_value (value);
  if (orientation == NULL)
    return;
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, gst_tag, orientation, NULL);
}


/* look at this page for addtional schemas
 * http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/XMP.html
 */
static gpointer
_init_xmp_tag_map ()
{
  GPtrArray *array;
  XmpTag *xmpinfo;

  __xmp_tag_map_mutex = g_mutex_new ();
  __xmp_tag_map = g_hash_table_new (g_direct_hash, g_direct_equal);

  /* add the maps */
  /* dublic code metadata
   * http://dublincore.org/documents/dces/
   */
  _xmp_tag_add_simple_mapping (GST_TAG_ARTIST, "dc:creator", NULL, NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_COPYRIGHT, "dc:rights", NULL, NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_DATE, "dc:date", NULL, NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_DATE, "exif:DateTimeOriginal", NULL,
      NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_DESCRIPTION, "dc:description", NULL,
      NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_KEYWORDS, "dc:subject", NULL, NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_TITLE, "dc:title", NULL, NULL);
  /* FIXME: we probably want GST_TAG_{,AUDIO_,VIDEO_}MIME_TYPE */
  _xmp_tag_add_simple_mapping (GST_TAG_VIDEO_CODEC, "dc:format", NULL, NULL);

  /* xap (xmp) schema */
  _xmp_tag_add_simple_mapping (GST_TAG_USER_RATING, "xmp:Rating", NULL,
      deserialize_xmp_rating);

  /* tiff */
  _xmp_tag_add_simple_mapping (GST_TAG_DEVICE_MANUFACTURER, "tiff:Make", NULL,
      NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_DEVICE_MODEL, "tiff:Model", NULL, NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_IMAGE_ORIENTATION, "tiff:Orientation",
      serialize_tiff_orientation, deserialize_tiff_orientation);

  /* exif schema */
  _xmp_tag_add_simple_mapping (GST_TAG_GEO_LOCATION_LATITUDE,
      "exif:GPSLatitude", serialize_exif_latitude, deserialize_exif_latitude);
  _xmp_tag_add_simple_mapping (GST_TAG_GEO_LOCATION_LONGITUDE,
      "exif:GPSLongitude", serialize_exif_longitude,
      deserialize_exif_longitude);

  /* compound exif tags */
  array = g_ptr_array_sized_new (2);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSAltitude";
  xmpinfo->serialize = serialize_exif_altitude;
  xmpinfo->deserialize = deserialize_exif_altitude;
  g_ptr_array_add (array, xmpinfo);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSAltitudeRef";
  xmpinfo->serialize = serialize_exif_altituderef;
  xmpinfo->deserialize = deserialize_exif_altitude;
  g_ptr_array_add (array, xmpinfo);
  _xmp_tag_add_mapping (GST_TAG_GEO_LOCATION_ELEVATION, array);

  array = g_ptr_array_sized_new (2);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSSpeed";
  xmpinfo->serialize = serialize_exif_gps_speed;
  xmpinfo->deserialize = deserialize_exif_gps_speed;
  g_ptr_array_add (array, xmpinfo);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSSpeedRef";
  xmpinfo->serialize = serialize_exif_gps_speedref;
  xmpinfo->deserialize = deserialize_exif_gps_speed;
  g_ptr_array_add (array, xmpinfo);
  _xmp_tag_add_mapping (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, array);

  array = g_ptr_array_sized_new (2);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSTrack";
  xmpinfo->serialize = serialize_exif_gps_direction;
  xmpinfo->deserialize = deserialize_exif_gps_track;
  g_ptr_array_add (array, xmpinfo);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSTrackRef";
  xmpinfo->serialize = serialize_exif_gps_directionref;
  xmpinfo->deserialize = deserialize_exif_gps_track;
  g_ptr_array_add (array, xmpinfo);
  _xmp_tag_add_mapping (GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, array);

  array = g_ptr_array_sized_new (2);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSImgDirection";
  xmpinfo->serialize = serialize_exif_gps_direction;
  xmpinfo->deserialize = deserialize_exif_gps_img_direction;
  g_ptr_array_add (array, xmpinfo);
  xmpinfo = g_slice_new (XmpTag);
  xmpinfo->tag_name = "exif:GPSImgDirectionRef";
  xmpinfo->serialize = serialize_exif_gps_directionref;
  xmpinfo->deserialize = deserialize_exif_gps_img_direction;
  g_ptr_array_add (array, xmpinfo);
  _xmp_tag_add_mapping (GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, array);

  /* photoshop schema */
  _xmp_tag_add_simple_mapping (GST_TAG_GEO_LOCATION_COUNTRY,
      "photoshop:Country", NULL, NULL);
  _xmp_tag_add_simple_mapping (GST_TAG_GEO_LOCATION_CITY, "photoshop:City",
      NULL, NULL);

  /* iptc4xmpcore schema */
  _xmp_tag_add_simple_mapping (GST_TAG_GEO_LOCATION_SUBLOCATION,
      "Iptc4xmpCore:Location", NULL, NULL);

  return NULL;
}

static void
xmp_tags_initialize ()
{
  static GOnce my_once = G_ONCE_INIT;
  g_once (&my_once, _init_xmp_tag_map, NULL);
}

typedef struct _GstXmpNamespaceMatch GstXmpNamespaceMatch;
struct _GstXmpNamespaceMatch
{
  const gchar *ns_prefix;
  const gchar *ns_uri;
};

static const GstXmpNamespaceMatch ns_match[] = {
  {"dc", "http://purl.org/dc/elements/1.1/"},
  {"exif", "http://ns.adobe.com/exif/1.0/"},
  {"tiff", "http://ns.adobe.com/tiff/1.0/"},
  {"xap", "http://ns.adobe.com/xap/1.0/"},
  {"photoshop", "http://ns.adobe.com/photoshop/1.0/"},
  {"Iptc4xmpCore", "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"},
  {NULL, NULL}
};

typedef struct _GstXmpNamespaceMap GstXmpNamespaceMap;
struct _GstXmpNamespaceMap
{
  const gchar *original_ns;
  gchar *gstreamer_ns;
};
static GstXmpNamespaceMap ns_map[] = {
  {"dc", NULL},
  {"exif", NULL},
  {"tiff", NULL},
  {"xap", NULL},
  {"photoshop", NULL},
  {"Iptc4xmpCore", NULL},
  {NULL, NULL}
};

/* parsing */

static void
read_one_tag (GstTagList * list, const gchar * tag, XmpTag * xmptag,
    const gchar * v, GSList ** pending_tags)
{
  GType tag_type;

  if (xmptag && xmptag->deserialize) {
    xmptag->deserialize (list, tag, xmptag->tag_name, v, pending_tags);
    return;
  }

  tag_type = gst_tag_get_type (tag);

  /* add gstreamer tag depending on type */
  switch (tag_type) {
    case G_TYPE_STRING:{
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag, v, NULL);
      break;
    }
    default:
      if (tag_type == GST_TYPE_DATE) {
        GDate *date;
        gint d, m, y;

        /* this is ISO 8601 Date and Time Format
         * %F     Equivalent to %Y-%m-%d (the ISO 8601 date format). (C99)
         * %T     The time in 24-hour notation (%H:%M:%S). (SU)
         * e.g. 2009-05-30T18:26:14+03:00 */

        /* FIXME: this would be the proper way, but needs
           #define _XOPEN_SOURCE before #include <time.h>

           date = g_date_new ();
           struct tm tm={0,};
           strptime (dts, "%FT%TZ", &tm);
           g_date_set_time_t (date, mktime(&tm));
         */
        /* FIXME: this cannot parse the date
           date = g_date_new ();
           g_date_set_parse (date, v);
           if (g_date_valid (date)) {
           gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag,
           date, NULL);
           } else {
           GST_WARNING ("unparsable date: '%s'", v);
           }
         */
        /* poor mans straw */
        sscanf (v, "%04d-%02d-%02dT", &y, &m, &d);
        date = g_date_new_dmy (d, m, y);
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag, date, NULL);
        g_date_free (date);
      } else {
        GST_WARNING ("unhandled type for %s from xmp", tag);
      }
      break;
  }
}

/**
 * gst_tag_list_from_xmp_buffer:
 * @buffer: buffer
 *
 * Parse a xmp packet into a taglist.
 *
 * Returns: new taglist or %NULL, free the list when done
 *
 * Since: 0.10.29
 */
GstTagList *
gst_tag_list_from_xmp_buffer (const GstBuffer * buffer)
{
  GstTagList *list = NULL;
  const gchar *xps, *xp1, *xp2, *xpe, *ns, *ne;
  guint len, max_ft_len;
  gboolean in_tag;
  gchar *part, *pp;
  guint i;
  const gchar *last_tag = NULL;
  XmpTag *last_xmp_tag = NULL;
  GSList *pending_tags = NULL;

  xmp_tags_initialize ();

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) > 0, NULL);

  xps = (const gchar *) GST_BUFFER_DATA (buffer);
  len = GST_BUFFER_SIZE (buffer);
  xpe = &xps[len + 1];

  /* check header and footer */
  xp1 = g_strstr_len (xps, len, "<?xpacket begin");
  if (!xp1)
    goto missing_header;
  xp1 = &xp1[strlen ("<?xpacket begin")];
  while (*xp1 != '>' && *xp1 != '<' && xp1 < xpe)
    xp1++;
  if (*xp1 != '>')
    goto missing_header;

  max_ft_len = 1 + strlen ("<?xpacket end=\".\"?>\n");
  if (len < max_ft_len)
    goto missing_footer;

  GST_DEBUG ("checking footer: [%s]", &xps[len - max_ft_len]);
  xp2 = g_strstr_len (&xps[len - max_ft_len], max_ft_len, "<?xpacket ");
  if (!xp2)
    goto missing_footer;

  GST_INFO ("xmp header okay");

  /* skip > and text until first xml-node */
  xp1++;
  while (*xp1 != '<' && xp1 < xpe)
    xp1++;

  /* no tag can be longer that the whole buffer */
  part = g_malloc (xp2 - xp1);
  list = gst_tag_list_new ();

  /* parse data into a list of nodes */
  /* data is between xp1..xp2 */
  in_tag = TRUE;
  ns = ne = xp1;
  pp = part;
  while (ne < xp2) {
    if (in_tag) {
      ne++;
      while (ne < xp2 && *ne != '>' && *ne != '<') {
        if (*ne == '\n' || *ne == '\t' || *ne == ' ') {
          while (ne < xp2 && (*ne == '\n' || *ne == '\t' || *ne == ' '))
            ne++;
          *pp++ = ' ';
        } else {
          *pp++ = *ne++;
        }
      }
      *pp = '\0';
      if (*ne != '>')
        goto broken_xml;
      /* create node */
      /* {XML, ns, ne-ns} */
      if (ns[0] != '/') {
        gchar *as = strchr (part, ' ');
        /* only log start nodes */
        GST_INFO ("xml: %s", part);

        if (as) {
          gchar *ae, *d;

          /* skip ' ' and scan the attributes */
          as++;
          d = ae = as;

          /* split attr=value pairs */
          while (*ae != '\0') {
            if (*ae == '=') {
              /* attr/value delimmiter */
              d = ae;
            } else if (*ae == '"') {
              /* scan values */
              gchar *v;

              ae++;
              while (*ae != '\0' && *ae != '"')
                ae++;

              *d = *ae = '\0';
              v = &d[2];
              GST_INFO ("   : [%s][%s]", as, v);
              if (!strncmp (as, "xmlns:", 6)) {
                i = 0;
                /* we need to rewrite known namespaces to what we use in
                 * tag_matches */
                while (ns_match[i].ns_prefix) {
                  if (!strcmp (ns_match[i].ns_uri, v))
                    break;
                  i++;
                }
                if (ns_match[i].ns_prefix) {
                  if (strcmp (ns_map[i].original_ns, &as[6])) {
                    ns_map[i].gstreamer_ns = g_strdup (&as[6]);
                  }
                }
              } else {
                const gchar *gst_tag;
                XmpTag *xmp_tag = NULL;
                /* FIXME: eventualy rewrite ns
                 * find ':'
                 * check if ns before ':' is in ns_map and ns_map[i].gstreamer_ns!=NULL
                 * do 2 stage filter in tag_matches
                 */
                gst_tag = _xmp_tag_get_mapping_reverse (as, &xmp_tag);
                if (gst_tag) {
                  PendingXmpTag *ptag;

                  ptag = g_slice_new (PendingXmpTag);
                  ptag->gst_tag = gst_tag;
                  ptag->xmp_tag = xmp_tag;
                  ptag->str = g_strdup (v);

                  pending_tags = g_slist_append (pending_tags, ptag);
                }
              }
              /* restore chars overwritten by '\0' */
              *d = '=';
              *ae = '"';
            } else if (*ae == '\0' || *ae == ' ') {
              /* end of attr/value pair */
              as = &ae[1];
            }
            /* to next char if not eos */
            if (*ae != '\0')
              ae++;
          }
        } else {
          /*
             <dc:type><rdf:Bag><rdf:li>Image</rdf:li></rdf:Bag></dc:type>
             <dc:creator><rdf:Seq><rdf:li/></rdf:Seq></dc:creator>
           */
          /* FIXME: eventualy rewrite ns */

          /* skip rdf tags for now */
          if (strncmp (part, "rdf:", 4)) {
            const gchar *parttag;

            parttag = _xmp_tag_get_mapping_reverse (part, &last_xmp_tag);
            if (parttag) {
              last_tag = parttag;
            }
          }
        }
      }
      /* next cycle */
      ne++;
      if (ne < xp2) {
        if (*ne != '<')
          in_tag = FALSE;
        ns = ne;
        pp = part;
      }
    } else {
      while (ne < xp2 && *ne != '<') {
        *pp++ = *ne;
        ne++;
      }
      *pp = '\0';
      /* create node */
      /* {TXT, ns, (ne-ns)-1} */
      if (ns[0] != '\n' && &ns[1] <= ne) {
        /* only log non-newline nodes, we still have to parse them */
        GST_INFO ("txt: %s", part);
        if (last_tag) {
          PendingXmpTag *ptag;

          ptag = g_slice_new (PendingXmpTag);
          ptag->gst_tag = last_tag;
          ptag->xmp_tag = last_xmp_tag;
          ptag->str = g_strdup (part);

          pending_tags = g_slist_append (pending_tags, ptag);
        }
      }
      /* next cycle */
      in_tag = TRUE;
      ns = ne;
      pp = part;
    }
  }

  while (pending_tags) {
    PendingXmpTag *ptag = (PendingXmpTag *) pending_tags->data;

    pending_tags = g_slist_delete_link (pending_tags, pending_tags);

    read_one_tag (list, ptag->gst_tag, ptag->xmp_tag, ptag->str, &pending_tags);

    g_free (ptag->str);
    g_slice_free (PendingXmpTag, ptag);
  }

  GST_INFO ("xmp packet parsed, %d entries",
      gst_structure_n_fields ((GstStructure *) list));

  /* free resources */
  i = 0;
  while (ns_map[i].original_ns) {
    g_free (ns_map[i].gstreamer_ns);
    i++;
  }
  g_free (part);

  return list;

  /* Errors */
missing_header:
  GST_WARNING ("malformed xmp packet header");
  return NULL;
missing_footer:
  GST_WARNING ("malformed xmp packet footer");
  return NULL;
broken_xml:
  GST_WARNING ("malformed xml tag: %s", part);
  return NULL;
}


/* formatting */

static void
string_open_tag (GString * string, const char *tag)
{
  g_string_append_c (string, '<');
  g_string_append (string, tag);
  g_string_append_c (string, '>');
}

static void
string_close_tag (GString * string, const char *tag)
{
  g_string_append (string, "</");
  g_string_append (string, tag);
  g_string_append (string, ">\n");
}

static char *
gst_value_serialize_xmp (const GValue * value)
{
  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      return g_markup_escape_text (g_value_get_string (value), -1);
    case G_TYPE_INT:
      return g_strdup_printf ("%d", g_value_get_int (value));
    case G_TYPE_UINT:
      return g_strdup_printf ("%u", g_value_get_uint (value));
    default:
      break;
  }
  /* put non-switchable types here */
  if (G_VALUE_TYPE (value) == GST_TYPE_DATE) {
    const GDate *date = gst_value_get_date (value);

    return g_strdup_printf ("%04d-%02d-%02d",
        (gint) g_date_get_year (date), (gint) g_date_get_month (date),
        (gint) g_date_get_day (date));
  } else {
    return NULL;
  }
}

static void
write_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  guint i = 0, ct = gst_tag_list_get_tag_size (list, tag), tag_index;
  GString *data = user_data;
  GPtrArray *xmp_tag_array = NULL;
  char *s;
  GSList *xmptaglist;

  /* map gst-tag to xmp tag */
  xmptaglist = _xmp_tag_get_mapping (tag);
  if (xmptaglist) {
    /* FIXME - we are always chosing the first tag mapped on the list */
    xmp_tag_array = (GPtrArray *) xmptaglist->data;
  }

  if (!xmp_tag_array) {
    GST_WARNING ("no mapping for %s to xmp", tag);
    return;
  }

  for (tag_index = 0; tag_index < xmp_tag_array->len; tag_index++) {
    XmpTag *xmp_tag;

    xmp_tag = g_ptr_array_index (xmp_tag_array, tag_index);
    string_open_tag (data, xmp_tag->tag_name);

    /* fast path for single valued tag */
    if (ct == 1) {
      if (xmp_tag->serialize) {
        s = xmp_tag->serialize (gst_tag_list_get_value_index (list, tag, 0));
      } else {
        s = gst_value_serialize_xmp (gst_tag_list_get_value_index (list, tag,
                0));
      }
      if (s) {
        g_string_append (data, s);
        g_free (s);
      } else {
        GST_WARNING ("unhandled type for %s to xmp", tag);
      }
    } else {
      string_open_tag (data, "rdf:Bag");
      for (i = 0; i < ct; i++) {
        GST_DEBUG ("mapping %s[%u/%u] to xmp", tag, i, ct);
        if (xmp_tag->serialize) {
          s = xmp_tag->serialize (gst_tag_list_get_value_index (list, tag, i));
        } else {
          s = gst_value_serialize_xmp (gst_tag_list_get_value_index (list, tag,
                  i));
        }
        if (s) {
          string_open_tag (data, "rdf:li");
          g_string_append (data, s);
          string_close_tag (data, "rdf:li");
          g_free (s);
        } else {
          GST_WARNING ("unhandled type for %s to xmp", tag);
        }
      }
      string_close_tag (data, "rdf:Bag");
    }

    string_close_tag (data, xmp_tag->tag_name);
  }
}

/**
 * gst_tag_list_to_xmp_buffer:
 * @list: tags
 * @read_only: does the container forbid inplace editing
 *
 * Formats a taglist as a xmp packet.
 *
 * Returns: new buffer or %NULL, unref the buffer when done
 *
 * Since: 0.10.29
 */
GstBuffer *
gst_tag_list_to_xmp_buffer (const GstTagList * list, gboolean read_only)
{
  GstBuffer *buffer = NULL;
  GString *data = g_string_sized_new (4096);
  guint i;

  xmp_tags_initialize ();

  g_return_val_if_fail (GST_IS_TAG_LIST (list), NULL);

  /* xmp header */
  g_string_append (data,
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");
  g_string_append (data,
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"GStreamer\">\n");
  g_string_append (data,
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"");
  i = 0;
  while (ns_match[i].ns_prefix) {
    g_string_append_printf (data, " xmlns:%s=\"%s\"", ns_match[i].ns_prefix,
        ns_match[i].ns_uri);
    i++;
  }
  g_string_append (data, ">\n");
  g_string_append (data, "<rdf:Description rdf:about=\"\">\n");

  /* iterate the taglist */
  gst_tag_list_foreach (list, write_one_tag, data);

  /* xmp footer */
  g_string_append (data, "</rdf:Description>\n");
  g_string_append (data, "</rdf:RDF>\n");
  g_string_append (data, "</x:xmpmeta>\n");

  if (!read_only) {
    /* the xmp spec recommand to add 2-4KB padding for in-place editable xmp */
    guint i;

    for (i = 0; i < 32; i++) {
      g_string_append (data, "                " "                "
          "                " "                " "\n");
    }
  }
  g_string_append_printf (data, "<?xpacket end=\"%c\"?>\n",
      (read_only ? 'r' : 'w'));

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = data->len + 1;
  GST_BUFFER_DATA (buffer) = (guint8 *) g_string_free (data, FALSE);
  GST_BUFFER_MALLOCDATA (buffer) = GST_BUFFER_DATA (buffer);

  return buffer;
}
