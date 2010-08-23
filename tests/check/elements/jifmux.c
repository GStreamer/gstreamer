/* GStreamer
 *
 * unit test for jifmux
 *
 * Copyright (C) <2010> Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/tag/tag.h>

#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>

typedef struct
{
  gboolean result;
  const GstTagList *taglist;
  const gchar *gst_tag;
} ExifTagCheckData;

/* taken from the exif helper lib in -base */
/* Exif tag types */
#define EXIF_TYPE_BYTE       1
#define EXIF_TYPE_ASCII      2
#define EXIF_TYPE_SHORT      3
#define EXIF_TYPE_LONG       4
#define EXIF_TYPE_RATIONAL   5
#define EXIF_TYPE_UNDEFINED  7
#define EXIF_TYPE_SLONG      9
#define EXIF_TYPE_SRATIONAL 10

/* exif tags */
#define GST_EXIF_TAG_GPS_LATITUDE_REF 0x1
#define GST_EXIF_TAG_GPS_LATITUDE 0x2
#define GST_EXIF_TAG_GPS_LONGITUDE_REF 0x3
#define GST_EXIF_TAG_GPS_LONGITUDE 0x4
#define GST_EXIF_TAG_GPS_ALTITUDE_REF 0x5
#define GST_EXIF_TAG_GPS_ALTITUDE 0x6
#define GST_EXIF_TAG_GPS_SPEED_REF 0xC
#define GST_EXIF_TAG_GPS_SPEED 0xD
#define GST_EXIF_TAG_GPS_TRACK_REF 0xE
#define GST_EXIF_TAG_GPS_TRACK 0xF
#define GST_EXIF_TAG_GPS_IMAGE_DIRECTION_REF 0x10
#define GST_EXIF_TAG_GPS_IMAGE_DIRECTION 0x11
#define GST_EXIF_TAG_IMAGE_DESCRIPTION 0x10E
#define GST_EXIF_TAG_MAKE 0x10F
#define GST_EXIF_TAG_MODEL 0x110
#define GST_EXIF_TAG_ORIENTATION 0x112
#define GST_EXIF_TAG_SOFTWARE 0x131
#define GST_EXIF_TAG_DATE_TIME 0x132
#define GST_EXIF_TAG_ARTIST 0x13B
#define GST_EXIF_TAG_COPYRIGHT 0x8298
#define GST_EXIF_TAG_EXPOSURE_TIME 0x829A
#define GST_EXIF_TAG_F_NUMBER 0x829D
#define GST_EXIF_TAG_EXPOSURE_PROGRAM 0x8822
#define GST_EXIF_TAG_PHOTOGRAPHIC_SENSITIVITY 0x8827
#define GST_EXIF_TAG_SENSITIVITY_TYPE 0x8830
#define GST_EXIF_TAG_ISO_SPEED 0x8833
#define GST_EXIF_TAG_DATE_TIME_ORIGINAL 0x9003
#define GST_EXIF_TAG_DATE_TIME_DIGITIZED 0x9004
#define GST_EXIF_TAG_SHUTTER_SPEED_VALUE 0x9201
#define GST_EXIF_TAG_APERTURE_VALUE 0x9202
#define GST_EXIF_TAG_FLASH 0x9209
#define GST_EXIF_TAG_FOCAL_LENGTH 0x920A
#define GST_EXIF_TAG_MAKER_NOTE 0x927C
#define GST_EXIF_TAG_EXPOSURE_MODE 0xA402
#define GST_EXIF_TAG_WHITE_BALANCE 0xA403
#define GST_EXIF_TAG_DIGITAL_ZOOM_RATIO 0xA404
#define GST_EXIF_TAG_SCENE_CAPTURE_TYPE 0xA406
#define GST_EXIF_TAG_GAIN_CONTROL 0xA407
#define GST_EXIF_TAG_CONTRAST 0xA408
#define GST_EXIF_TAG_SATURATION 0xA409

/* IFD pointer tags */
#define EXIF_IFD_TAG 0x8769
#define EXIF_GPS_IFD_TAG 0x8825

/* version tags */
#define EXIF_VERSION_TAG 0x9000
#define EXIF_FLASHPIX_VERSION_TAG 0xA000


typedef struct _GstExifTagMatch GstExifTagMatch;
typedef void (*CompareFunc) (ExifEntry * entry, ExifTagCheckData * testdata);

struct _GstExifTagMatch
{
  const gchar *gst_tag;
  guint16 exif_tag;
  guint16 exif_type;
  guint16 complementary_tag;
  CompareFunc compare_func;
};

/* compare funcs */

/* Copied over from gst-libs/gst/tag/gsttagedittingprivate.c from -base */
static gint
__exif_tag_image_orientation_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "rotate-0") == 0)
    return 1;
  else if (strcmp (str, "flip-rotate-0") == 0)
    return 2;
  else if (strcmp (str, "rotate-180") == 0)
    return 3;
  else if (strcmp (str, "flip-rotate-180") == 0)
    return 4;
  else if (strcmp (str, "flip-rotate-270") == 0)
    return 5;
  else if (strcmp (str, "rotate-90") == 0)
    return 6;
  else if (strcmp (str, "flip-rotate-90") == 0)
    return 7;
  else if (strcmp (str, "rotate-270") == 0)
    return 8;

end:
  GST_WARNING ("Invalid image orientation tag: %s", str);
  return -1;
}

static gint
__exif_tag_capture_exposure_program_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "undefined") == 0)
    return 0;
  else if (strcmp (str, "manual") == 0)
    return 1;
  else if (strcmp (str, "normal") == 0)
    return 2;
  else if (strcmp (str, "aperture-priority") == 0)
    return 3;
  else if (strcmp (str, "shutter-priority") == 0)
    return 4;
  else if (strcmp (str, "creative") == 0)
    return 5;
  else if (strcmp (str, "action") == 0)
    return 6;
  else if (strcmp (str, "portrait") == 0)
    return 7;
  else if (strcmp (str, "landscape") == 0)
    return 8;

end:
  GST_WARNING ("Invalid capture exposure program tag: %s", str);
  return -1;
}

static gint
__exif_tag_capture_exposure_mode_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "auto-exposure") == 0)
    return 0;
  else if (strcmp (str, "manual-exposure") == 0)
    return 1;
  else if (strcmp (str, "auto-bracket") == 0)
    return 2;

end:
  GST_WARNING ("Invalid capture exposure mode tag: %s", str);
  return -1;
}

static gint
__exif_tag_capture_scene_capture_type_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "standard") == 0)
    return 0;
  else if (strcmp (str, "landscape") == 0)
    return 1;
  else if (strcmp (str, "portrait") == 0)
    return 2;
  else if (strcmp (str, "night-scene") == 0)
    return 3;

end:
  GST_WARNING ("Invalid capture scene capture type: %s", str);
  return -1;
}

static gint
__exif_tag_capture_gain_adjustment_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "none") == 0)
    return 0;
  else if (strcmp (str, "low-gain-up") == 0)
    return 1;
  else if (strcmp (str, "high-gain-up") == 0)
    return 2;
  else if (strcmp (str, "low-gain-down") == 0)
    return 3;
  else if (strcmp (str, "high-gain-down") == 0)
    return 4;

end:
  GST_WARNING ("Invalid capture gain adjustment type: %s", str);
  return -1;
}

static gint
__exif_tag_capture_white_balance_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "auto") == 0)
    return 0;
  else                          /* everything else is just manual */
    return 1;

end:
  GST_WARNING ("Invalid white balance: %s", str);
  return -1;
}

static gint
__exif_tag_capture_contrast_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "normal") == 0)
    return 0;
  else if (strcmp (str, "soft") == 0)
    return 1;
  else if (strcmp (str, "hard") == 0)
    return 2;

end:
  GST_WARNING ("Invalid contrast type: %s", str);
  return -1;
}

static gint
__exif_tag_capture_saturation_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "normal") == 0)
    return 0;
  else if (strcmp (str, "low-saturation") == 0)
    return 1;
  else if (strcmp (str, "high-saturation") == 0)
    return 2;

end:
  GST_WARNING ("Invalid saturation type: %s", str);
  return -1;
}

#define GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC(gst_tag,name)           \
static void                                                                   \
compare_ ## name (ExifEntry * entry, ExifTagCheckData * testdata)             \
{                                                                             \
  gchar *str_tag = NULL;                                                      \
  gint exif_value;                                                            \
  gint value;                                                                 \
                                                                              \
  if (!gst_tag_list_get_string_index (testdata->taglist,                      \
          gst_tag, 0, &str_tag)) {                                            \
    /* fail the test if we can't get the tag */                               \
    GST_WARNING ("Failed to get %s from taglist", gst_tag);                   \
    fail ();                                                                  \
  }                                                                           \
                                                                              \
  value = __exif_tag_ ## name ## _to_exif_value (str_tag);                    \
                                                                              \
  if (value == -1) {                                                          \
    GST_WARNING ("Invalid %s tag value: %s", gst_tag, str_tag);               \
    fail ();                                                                  \
  }                                                                           \
                                                                              \
  exif_value = (gint) exif_get_short (entry->data,                            \
      exif_data_get_byte_order (entry->parent->parent));                      \
                                                                              \
  if (value != exif_value) {                                                  \
    GST_WARNING ("Gstreamer tag value (%d) is different from libexif (%d)",   \
        value, exif_value);                                                   \
    fail ();                                                                  \
  }                                                                           \
                                                                              \
  testdata->result = TRUE;                                                    \
                                                                              \
  g_free (str_tag);                                                           \
}

GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC (GST_TAG_IMAGE_ORIENTATION,
    image_orientation);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC
    (GST_TAG_CAPTURING_EXPOSURE_PROGRAM, capture_exposure_program);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC (GST_TAG_CAPTURING_EXPOSURE_MODE,
    capture_exposure_mode);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC (GST_TAG_CAPTURING_WHITE_BALANCE,
    capture_white_balance);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC (GST_TAG_CAPTURING_CONTRAST,
    capture_contrast);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC
    (GST_TAG_CAPTURING_GAIN_ADJUSTMENT, capture_gain_adjustment);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC (GST_TAG_CAPTURING_SATURATION,
    capture_saturation);
GST_COMPARE_GST_STRING_TAG_TO_EXIF_SHORT_FUNC
    (GST_TAG_CAPTURING_SCENE_CAPTURE_TYPE, capture_scene_capture_type);

static void
compare_sensitivity_type (ExifEntry * entry, ExifTagCheckData * testdata)
{
  gint exif_value;

  exif_value = (gint) exif_get_short (entry->data,
      exif_data_get_byte_order (entry->parent->parent));
  if (exif_value != 3) {
    GST_WARNING ("Gstreamer should always write SensitivityType=3 for now");
    fail ();
  }
  testdata->result = TRUE;
}

static void
compare_date_time (ExifEntry * entry, ExifTagCheckData * testdata)
{
  gint year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0;
  GstDateTime *exif_datetime;
  GstDateTime *datetime;
  const gchar *str;

  if (!gst_tag_list_get_date_time_index (testdata->taglist, GST_TAG_DATE_TIME,
          0, &datetime)) {
    GST_WARNING ("Failed to get datetime from taglist");
    return;
  }

  str = (gchar *) entry->data;

  sscanf (str, "%04d:%02d:%02d %02d:%02d:%02d", &year, &month, &day,
      &hour, &minute, &second);
  exif_datetime = gst_date_time_new_local_time (year, month, day, hour, minute,
      second, 0);
  fail_if (exif_datetime == NULL);

  fail_unless (gst_date_time_get_year (datetime) ==
      gst_date_time_get_year (exif_datetime)
      && gst_date_time_get_month (datetime) ==
      gst_date_time_get_month (exif_datetime)
      && gst_date_time_get_day (datetime) ==
      gst_date_time_get_day (exif_datetime)
      && gst_date_time_get_hour (datetime) ==
      gst_date_time_get_hour (exif_datetime)
      && gst_date_time_get_minute (datetime) ==
      gst_date_time_get_minute (exif_datetime)
      && gst_date_time_get_second (datetime) ==
      gst_date_time_get_second (exif_datetime));

  gst_date_time_unref (exif_datetime);
  gst_date_time_unref (datetime);

  testdata->result = TRUE;
}

static void
compare_shutter_speed (ExifEntry * entry, ExifTagCheckData * testdata)
{
  gdouble gst_value, exif_value;
  ExifSRational rational;
  GValue value = { 0 };

  gst_value = gst_tag_list_get_value_index (testdata->taglist,
      GST_TAG_CAPTURING_SHUTTER_SPEED, 0);
  if (gst_value == NULL) {
    GST_WARNING ("Failed to get shutter-speed from taglist");
    return;
  }

  rational = exif_get_srational (entry->data,
      exif_data_get_byte_order (entry->parent->parent));

  g_value_init (&value, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value, rational.numerator, rational.denominator);
  gst_util_fraction_to_double (gst_value_get_fraction_numerator (&value),
      gst_value_get_fraction_denominator (&value), &exif_value);
  g_value_unset (&value);

  exif_value = -pow (2, -exif_value);

  fail_unless (gst_value == exif_value);
  testdata->result = TRUE;
}

static void
compare_aperture_value (ExifEntry * entry, ExifTagCheckData * testdata)
{
  gdouble gst_value, exif_value;
  ExifSRational rational;
  GValue value = { 0 };

  if (!gst_tag_list_get_double_index (testdata->taglist,
          GST_TAG_CAPTURING_FOCAL_RATIO, 0, &gst_value)) {
    GST_WARNING ("Failed to get focal ratio from taglist");
    return;
  }

  rational = exif_get_srational (entry->data,
      exif_data_get_byte_order (entry->parent->parent));

  g_value_init (&value, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value, rational.numerator, rational.denominator);
  gst_util_fraction_to_double (gst_value_get_fraction_numerator (&value),
      gst_value_get_fraction_denominator (&value), &exif_value);
  g_value_unset (&value);

  exif_value = -pow (2, exif_value / 2);

  fail_unless (gst_value == exif_value);
  testdata->result = TRUE;
}

static void
compare_flash (ExifEntry * entry, ExifTagCheckData * testdata)
{
  guint16 flags;
  gboolean flash_fired;
  const gchar *flash_mode;

  flags = (gint) exif_get_short (entry->data,
      exif_data_get_byte_order (entry->parent->parent));

  if (!gst_tag_list_get_boolean_index (testdata->taglist,
          GST_TAG_CAPTURING_FLASH_FIRED, 0, &flash_fired)) {
    GST_WARNING ("Failed to get %s tag", GST_TAG_CAPTURING_FLASH_FIRED);
    return;
  }
  if (!gst_tag_list_peek_string_index (testdata->taglist,
          GST_TAG_CAPTURING_FLASH_MODE, 0, &flash_mode)) {
    GST_WARNING ("Failed to get %s tag", GST_TAG_CAPTURING_FLASH_MODE);
    return;
  }

  if (flash_fired)
    fail_unless ((flags & 1) == 1);
  else
    fail_unless ((flags & 1) == 0);

  if (strcmp (flash_mode, "auto") == 0) {
    fail_unless (((flags >> 3) & 0x3) == 3);
  } else if (strcmp (flash_mode, "always") == 0) {
    fail_unless (((flags >> 3) & 0x3) == 1);
  } else if (strcmp (flash_mode, "never") == 0) {
    fail_unless (((flags >> 3) & 0x3) == 2);
  } else {
    fail ();
  }
  testdata->result = TRUE;
}

static const GstExifTagMatch tag_map[] = {
  {GST_TAG_DESCRIPTION, GST_EXIF_TAG_IMAGE_DESCRIPTION, EXIF_TYPE_ASCII, 0,
      NULL},
  {GST_TAG_DEVICE_MANUFACTURER, GST_EXIF_TAG_MAKE, EXIF_TYPE_ASCII, 0,
      NULL},
  {GST_TAG_DEVICE_MODEL, GST_EXIF_TAG_MODEL, EXIF_TYPE_ASCII, 0, NULL},
  {GST_TAG_IMAGE_ORIENTATION, GST_EXIF_TAG_ORIENTATION, EXIF_TYPE_SHORT, 0,
      compare_image_orientation},
  {GST_TAG_APPLICATION_NAME, GST_EXIF_TAG_SOFTWARE, EXIF_TYPE_ASCII, 0,
      NULL},
  {GST_TAG_DATE_TIME, GST_EXIF_TAG_DATE_TIME, EXIF_TYPE_ASCII, 0,
      compare_date_time},
  {GST_TAG_ARTIST, EXIF_TAG_ARTIST, EXIF_TYPE_ASCII, NULL},
  {GST_TAG_COPYRIGHT, EXIF_TAG_COPYRIGHT, EXIF_TYPE_ASCII, NULL},
  {GST_TAG_CAPTURING_SHUTTER_SPEED, EXIF_TAG_EXPOSURE_TIME,
      EXIF_TYPE_RATIONAL, NULL},
  {GST_TAG_CAPTURING_FOCAL_RATIO, EXIF_TAG_FNUMBER, EXIF_TYPE_RATIONAL,
      NULL},
  {GST_TAG_CAPTURING_EXPOSURE_PROGRAM, EXIF_TAG_EXPOSURE_PROGRAM,
      EXIF_TYPE_SHORT, compare_capture_exposure_program},

  /* This is called PhotographicSensitivity in 2.3 */
  {GST_TAG_CAPTURING_ISO_SPEED, EXIF_TAG_ISO_SPEED_RATINGS,
      EXIF_TYPE_SHORT, NULL},

  {GST_TAG_CAPTURING_SHUTTER_SPEED, EXIF_TAG_SHUTTER_SPEED_VALUE,
      EXIF_TYPE_SRATIONAL, compare_shutter_speed},
  {GST_TAG_CAPTURING_FOCAL_RATIO, EXIF_TAG_APERTURE_VALUE, EXIF_TYPE_RATIONAL,
      compare_aperture_value},
  {GST_TAG_CAPTURING_FLASH_FIRED, EXIF_TAG_FLASH, EXIF_TYPE_SHORT,
      compare_flash},
  {GST_TAG_CAPTURING_FLASH_MODE, EXIF_TAG_FLASH, EXIF_TYPE_SHORT,
      compare_flash},
  {GST_TAG_CAPTURING_FOCAL_LENGTH, EXIF_TAG_FOCAL_LENGTH, EXIF_TYPE_RATIONAL,
      NULL},
  {GST_TAG_APPLICATION_DATA, EXIF_TAG_MAKER_NOTE, EXIF_TYPE_UNDEFINED, NULL},
  {GST_TAG_CAPTURING_EXPOSURE_MODE, EXIF_TAG_EXPOSURE_MODE, EXIF_TYPE_SHORT,
      compare_capture_exposure_mode},
  {GST_TAG_CAPTURING_WHITE_BALANCE, EXIF_TAG_WHITE_BALANCE, EXIF_TYPE_SHORT,
      compare_capture_white_balance},
  {GST_TAG_CAPTURING_DIGITAL_ZOOM_RATIO, EXIF_TAG_DIGITAL_ZOOM_RATIO,
      EXIF_TYPE_RATIONAL, NULL},
  {GST_TAG_CAPTURING_SCENE_CAPTURE_TYPE, EXIF_TAG_SCENE_CAPTURE_TYPE,
      EXIF_TYPE_SHORT, compare_capture_scene_capture_type},
  {GST_TAG_CAPTURING_GAIN_ADJUSTMENT, EXIF_TAG_GAIN_CONTROL,
      EXIF_TYPE_SHORT, compare_capture_gain_adjustment},
  {GST_TAG_CAPTURING_CONTRAST, EXIF_TAG_CONTRAST, EXIF_TYPE_SHORT,
      compare_capture_contrast},
  {GST_TAG_CAPTURING_SATURATION, EXIF_TAG_SATURATION, EXIF_TYPE_SHORT,
      compare_capture_saturation},

  /* gps tags */
  {GST_TAG_GEO_LOCATION_LATITUDE, EXIF_TAG_GPS_LATITUDE, EXIF_TYPE_RATIONAL,
      compare_geo_coordinate},
  {GST_TAG_GEO_LOCATION_LATITUDE, EXIF_TAG_GPS_LATITUDE_REF, EXIF_TYPE_ASCII,
      compare_geo_coordinate_ref},
  {GST_TAG_GEO_LOCATION_LONGITUDE, EXIF_TAG_GPS_LONGITUDE, EXIF_TYPE_RATIONAL,
      compare_geo_coordinate},
  {GST_TAG_GEO_LOCATION_LONGITUDE, EXIF_TAG_GPS_LONGITUDE_REF, EXIF_TYPE_ASCII,
      compare_geo_coordinate_ref},
  {GST_TAG_GEO_LOCATION_ELEVATION, EXIF_TAG_GPS_ALTITUDE, EXIF_TYPE_RATIONAL,
      compare_geo_elevation},
  {GST_TAG_GEO_LOCATION_ELEVATION, EXIF_TAG_GPS_ALTITUDE_REF, EXIF_TYPE_BYTE,
      compare_geo_elevation_ref},
  {GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, EXIF_TAG_GPS_SPEED, EXIF_TYPE_RATIONAL,
      compare_speed},
  {GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, EXIF_TAG_GPS_SPEED_REF, EXIF_TYPE_ASCII,
      compare_speed_ref},
  {GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, EXIF_TAG_GPS_TRACK,
      EXIF_TYPE_RATIONAL, compare_geo_direction},
  {GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, EXIF_TAG_GPS_TRACK_REF,
      EXIF_TYPE_ASCII, compare_geo_direction_ref},
  {GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, EXIF_TAG_GPS_IMG_DIRECTION,
      EXIF_TYPE_RATIONAL, compare_geo_direction},
  {GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, EXIF_TAG_GPS_IMG_DIRECTION_REF,
      EXIF_TYPE_ASCII, compare_geo_direction_ref}

/*
 * libexif doesn't have these tags
 *  {GST_TAG_CAPTURING_ISO_SPEED, EXIF_TAG_ISO_SPEED, EXIF_TYPE_LONG, NULL},
 *  {GST_TAG_CAPTURING_ISO_SPEED, EXIF_TAG_SENSITIVITY_TYPE,
 *     EXIF_TYPE_SHORT, compare_sensitivity_type},
 */
};

static gint
get_tag_id_from_gst_tag (const gchar * gst_tag)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (tag_map); i++) {
    if (strcmp (gst_tag, tag_map[i].gst_tag) == 0)
      return i;
  }

  return -1;
}

static void
check_content (ExifContent * content, void *user_data)
{
  ExifTagCheckData *test_data = (ExifTagCheckData *) user_data;
  guint16 tagindex;
  ExifEntry *entry;
  GType gst_tag_type;

  tagindex = get_tag_id_from_gst_tag (test_data->gst_tag);

  GST_DEBUG ("Got tagindex %u for gsttag %s", tagindex, test_data->gst_tag);
  fail_if (tagindex == (guint16) - 1);

  gst_tag_type = gst_tag_get_type (tag_map[tagindex].gst_tag);

  entry = exif_content_get_entry (content, tag_map[tagindex].exif_tag);
  GST_DEBUG ("Entry is at %p", entry);
  if (entry == NULL)
    return;
  fail_unless (entry->format == tag_map[tagindex].exif_type);

  if (tag_map[tagindex].compare_func) {
    tag_map[tagindex].compare_func (entry, test_data);
    return;
  }

  switch (entry->format) {
    case EXIF_TYPE_ASCII:{
      const gchar *str;
      gchar *taglist_str;

      str = (gchar *) entry->data;
      fail_unless (gst_tag_list_get_string (test_data->taglist,
              tag_map[tagindex].gst_tag, &taglist_str));

      fail_unless (strcmp (str, taglist_str) == 0);
      test_data->result = TRUE;
    }
      break;
    case EXIF_TYPE_RATIONAL:{
      ExifRational exif_rational = exif_get_rational (entry->data,
          exif_data_get_byte_order (entry->parent->parent));
      GValue exif_value = { 0 };

      g_value_init (&exif_value, GST_TYPE_FRACTION);
      gst_value_set_fraction (&exif_value, exif_rational.numerator,
          exif_rational.denominator);

      if (gst_tag_type == GST_TYPE_FRACTION) {
        const GValue *value = gst_tag_list_get_value_index (test_data->taglist,
            tag_map[tagindex].gst_tag, 0);

        fail_unless (value != NULL);
        fail_unless (G_VALUE_TYPE (value) == GST_TYPE_FRACTION);

        fail_unless (gst_value_get_fraction_numerator (value) ==
            gst_value_get_fraction_numerator (&exif_value) &&
            gst_value_get_fraction_denominator (value) ==
            gst_value_get_fraction_denominator (&exif_value));

        test_data->result = TRUE;
      } else if (gst_tag_type == G_TYPE_DOUBLE) {
        gdouble gst_num;
        gdouble exif_num;

        gst_util_fraction_to_double (gst_value_get_fraction_numerator
            (&exif_value), gst_value_get_fraction_denominator (&exif_value),
            &exif_num);

        fail_unless (gst_tag_list_get_double_index (test_data->taglist,
                tag_map[tagindex].gst_tag, 0, &gst_num));

        fail_unless (gst_num == exif_num);
        test_data->result = TRUE;
      } else {
        GST_WARNING ("Unhandled type for rational tag(%X): %s",
            entry->tag, g_type_name (gst_tag_type));
      }
      g_value_unset (&exif_value);
    }
      break;
    case EXIF_TYPE_SHORT:
    case EXIF_TYPE_LONG:{
      gint gst_num;
      gint exif_num = -1;

      if (entry->format == EXIF_TYPE_LONG) {
        exif_num = (gint) exif_get_long (entry->data,
            exif_data_get_byte_order (entry->parent->parent));
      } else if (entry->format == EXIF_TYPE_SHORT) {
        exif_num = (gint) exif_get_short (entry->data,
            exif_data_get_byte_order (entry->parent->parent));
      }

      fail_unless (gst_tag_list_get_int_index (test_data->taglist,
              tag_map[tagindex].gst_tag, 0, &gst_num));

      fail_unless (exif_num == gst_num);
      test_data->result = TRUE;
    }
      break;
    case EXIF_TYPE_UNDEFINED:{
      GstBuffer *buf;
      gint i;

      if (!gst_tag_list_get_buffer_index (test_data->taglist,
              tag_map[tagindex].gst_tag, 0, &buf)) {
        return;
      }

      fail_unless (entry->size, GST_BUFFER_SIZE (buf));
      for (i = 0; i < GST_BUFFER_SIZE (buf); i++) {
        fail_unless (GST_BUFFER_DATA (buf)[i] == (guint8) entry->data[i]);
      }

      test_data->result = TRUE;
    }
      break;
    default:
      fail ();
  }
}

static void
libexif_check_tag_exists (const GstTagList * taglist, const gchar * gst_tag,
    gpointer data)
{
  ExifData *exif_data = (ExifData *) data;
  ExifTagCheckData test_data;

  test_data.result = FALSE;
  test_data.taglist = taglist;
  test_data.gst_tag = gst_tag;

  exif_data_foreach_content (exif_data, check_content, &test_data);

  fail_unless (test_data.result);
}

static void
generate_jif_file_with_tags_from_taglist (GstTagList * taglist,
    const gchar * filepath)
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;
  gchar *launchline;
  GstElement *jifmux;
  GstTagSetter *setter;

  launchline = g_strdup_printf ("videotestsrc num-buffers=1 ! jpegenc ! "
      "jifmux name=jifmux0 ! filesink location=%s", filepath);

  pipeline = gst_parse_launch (launchline, NULL);
  fail_unless (pipeline != NULL);

  jifmux = gst_bin_get_by_name (GST_BIN (pipeline), "jifmux0");
  fail_unless (jifmux != NULL);
  setter = GST_TAG_SETTER (jifmux);
  gst_tag_setter_merge_tags (setter, taglist, GST_TAG_MERGE_REPLACE);
  gst_object_unref (jifmux);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 5, GST_MESSAGE_EOS |
      GST_MESSAGE_ERROR);
  fail_if (!msg || GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);

  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

static void
generate_jif_file_with_tags (const gchar * tags, const gchar * filepath)
{
  GstTagList *taglist;

  taglist = gst_structure_from_string (tags, NULL);
  generate_jif_file_with_tags_from_taglist (taglist, filepath);

  gst_tag_list_free (taglist);
}

static void
libexif_check_tags_from_taglist (GstTagList * taglist, const gchar * filepath)
{
  ExifData *exif_data;

  fail_unless (taglist != NULL);

  exif_data = exif_data_new_from_file (filepath);

  gst_tag_list_foreach (taglist, libexif_check_tag_exists, exif_data);

  exif_data_unref (exif_data);
}

static void
libexif_check_tags (const gchar * tags, const gchar * filepath)
{
  GstTagList *taglist;

  taglist = gst_structure_from_string (tags, NULL);
  fail_unless (taglist != NULL);

  libexif_check_tags_from_taglist (taglist, filepath);

  gst_tag_list_free (taglist);
}

GST_START_TEST (test_jifmux_tags)
{
  gchar *tmpfile;
  gchar *tmp;
  GstTagList *taglist;
  GstDateTime *datetime;
  GstBuffer *buffer;
  gint i;

  gst_tag_register_musicbrainz_tags ();

  tmp = g_strdup_printf ("%s%d", "gst-check-xmp-test-", g_random_int ());
  tmpfile = g_build_filename (g_get_tmp_dir (), tmp, NULL);
  g_free (tmp);

/*
  GST_TAG_CAPTURE_FLASH_FIRED
*/
  datetime = gst_date_time_new_local_time (2000, 10, 5, 8, 45, 13, 0);
  buffer = gst_buffer_new_and_alloc (100);
  for (i = 0; i < 100; i++) {
    GST_BUFFER_DATA (buffer)[i] = i;
  }
  taglist = gst_tag_list_new_full (GST_TAG_ARTIST, "some artist",
      GST_TAG_COPYRIGHT, "My copyright notice",
      GST_TAG_DEVICE_MANUFACTURER, "MyFavoriteBrand",
      GST_TAG_DEVICE_MODEL, "123v42.1",
      GST_TAG_DESCRIPTION, "some description",
      GST_TAG_APPLICATION_NAME, "jifmux-test v1.2b",
      GST_TAG_CAPTURING_SHUTTER_SPEED, 1, 30,
      GST_TAG_CAPTURING_FOCAL_RATIO, 2.0,
      GST_TAG_CAPTURING_ISO_SPEED, 800, GST_TAG_DATE_TIME, datetime,
      GST_TAG_CAPTURING_FOCAL_LENGTH, 22.5,
      GST_TAG_CAPTURING_DIGITAL_ZOOM_RATIO, 5.25,
      GST_TAG_APPLICATION_DATA, buffer,
      GST_TAG_CAPTURING_FLASH_FIRED, TRUE,
      GST_TAG_CAPTURING_FLASH_MODE, "auto", NULL);
  gst_date_time_unref (datetime);
  gst_buffer_unref (buffer);
  generate_jif_file_with_tags_from_taglist (taglist, tmpfile);
  libexif_check_tags_from_taglist (taglist, tmpfile);
  gst_tag_list_free (taglist);

#define IMAGE_ORIENTATION_TAG(t) "taglist," GST_TAG_IMAGE_ORIENTATION "=" t
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG ("rotate-0"), tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("rotate-0"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG ("flip-rotate-0"),
      tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("flip-rotate-0"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG ("rotate-180"), tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("rotate-180"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG ("flip-rotate-180"),
      tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("flip-rotate-180"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG
      ("flip-rotate-270"), tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("flip-rotate-270"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG ("rotate-90"), tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("rotate-90"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG
      ("flip-rotate-90"), tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("flip-rotate-90"), tmpfile);
  generate_jif_file_with_tags (IMAGE_ORIENTATION_TAG ("rotate-270"), tmpfile);
  libexif_check_tags (IMAGE_ORIENTATION_TAG ("rotate-270"), tmpfile);

#define EXPOSURE_PROGRAM_TAG(t) "taglist," GST_TAG_CAPTURING_EXPOSURE_PROGRAM \
    "=" t
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("undefined"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("undefined"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("manual"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("manual"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("normal"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("normal"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("aperture-priority"),
      tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("aperture-priority"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("shutter-priority"),
      tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("shutter-priority"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("creative"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("creative"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("action"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("action"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("portrait"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("portrait"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_PROGRAM_TAG ("landscape"), tmpfile);
  libexif_check_tags (EXPOSURE_PROGRAM_TAG ("landscape"), tmpfile);

#define EXPOSURE_MODE_TAG(t) "taglist," GST_TAG_CAPTURING_EXPOSURE_MODE "=" t
  generate_jif_file_with_tags (EXPOSURE_MODE_TAG ("auto-exposure"), tmpfile);
  libexif_check_tags (EXPOSURE_MODE_TAG ("auto-exposure"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_MODE_TAG ("manual-exposure"), tmpfile);
  libexif_check_tags (EXPOSURE_MODE_TAG ("manual-exposure"), tmpfile);
  generate_jif_file_with_tags (EXPOSURE_MODE_TAG ("auto-bracket"), tmpfile);
  libexif_check_tags (EXPOSURE_MODE_TAG ("auto-bracket"), tmpfile);

#define SCENE_CAPTURE_TYPE_TAG(t) "taglist," GST_TAG_CAPTURING_SCENE_CAPTURE_TYPE\
    "=" t
  generate_jif_file_with_tags (SCENE_CAPTURE_TYPE_TAG ("standard"), tmpfile);
  libexif_check_tags (SCENE_CAPTURE_TYPE_TAG ("standard"), tmpfile);
  generate_jif_file_with_tags (SCENE_CAPTURE_TYPE_TAG ("landscape"), tmpfile);
  libexif_check_tags (SCENE_CAPTURE_TYPE_TAG ("landscape"), tmpfile);
  generate_jif_file_with_tags (SCENE_CAPTURE_TYPE_TAG ("portrait"), tmpfile);
  libexif_check_tags (SCENE_CAPTURE_TYPE_TAG ("portrait"), tmpfile);
  generate_jif_file_with_tags (SCENE_CAPTURE_TYPE_TAG ("night-scene"), tmpfile);
  libexif_check_tags (SCENE_CAPTURE_TYPE_TAG ("night-scene"), tmpfile);

#define WHITE_BALANCE_TAG(t) "taglist," GST_TAG_CAPTURING_WHITE_BALANCE "=" t
  generate_jif_file_with_tags (WHITE_BALANCE_TAG ("auto"), tmpfile);
  libexif_check_tags (WHITE_BALANCE_TAG ("auto"), tmpfile);
  generate_jif_file_with_tags (WHITE_BALANCE_TAG ("manual"), tmpfile);
  libexif_check_tags (WHITE_BALANCE_TAG ("manual"), tmpfile);

#define GAIN_ADJUSTMENT_TAG(t) "taglist," GST_TAG_CAPTURING_GAIN_ADJUSTMENT "=" t
  generate_jif_file_with_tags (GAIN_ADJUSTMENT_TAG ("none"), tmpfile);
  libexif_check_tags (GAIN_ADJUSTMENT_TAG ("none"), tmpfile);
  generate_jif_file_with_tags (GAIN_ADJUSTMENT_TAG ("high-gain-up"), tmpfile);
  libexif_check_tags (GAIN_ADJUSTMENT_TAG ("high-gain-up"), tmpfile);
  generate_jif_file_with_tags (GAIN_ADJUSTMENT_TAG ("low-gain-up"), tmpfile);
  libexif_check_tags (GAIN_ADJUSTMENT_TAG ("low-gain-up"), tmpfile);
  generate_jif_file_with_tags (GAIN_ADJUSTMENT_TAG ("high-gain-down"), tmpfile);
  libexif_check_tags (GAIN_ADJUSTMENT_TAG ("high-gain-down"), tmpfile);
  generate_jif_file_with_tags (GAIN_ADJUSTMENT_TAG ("low-gain-down"), tmpfile);
  libexif_check_tags (GAIN_ADJUSTMENT_TAG ("low-gain-down"), tmpfile);

#define CONTRAST_TAG(t) "taglist," GST_TAG_CAPTURING_CONTRAST "=" t
  generate_jif_file_with_tags (CONTRAST_TAG ("normal"), tmpfile);
  libexif_check_tags (CONTRAST_TAG ("normal"), tmpfile);
  generate_jif_file_with_tags (CONTRAST_TAG ("soft"), tmpfile);
  libexif_check_tags (CONTRAST_TAG ("soft"), tmpfile);
  generate_jif_file_with_tags (CONTRAST_TAG ("hard"), tmpfile);
  libexif_check_tags (CONTRAST_TAG ("hard"), tmpfile);

#define SATURATION_TAG(t) "taglist," GST_TAG_CAPTURING_SATURATION "=" t
  generate_jif_file_with_tags (SATURATION_TAG ("normal"), tmpfile);
  libexif_check_tags (SATURATION_TAG ("normal"), tmpfile);
  generate_jif_file_with_tags (SATURATION_TAG ("low-saturation"), tmpfile);
  libexif_check_tags (SATURATION_TAG ("low-saturation"), tmpfile);
  generate_jif_file_with_tags (SATURATION_TAG ("high-saturation"), tmpfile);
  libexif_check_tags (SATURATION_TAG ("high-saturation"), tmpfile);

  g_free (tmpfile);
}

GST_END_TEST;

#define HAVE_ELEMENT(name) \
  gst_default_registry_check_feature_version (name,\
      GST_VERSION_MAJOR, GST_VERSION_MINOR, 0)

static Suite *
jifmux_suite (void)
{
  Suite *s = suite_create ("jifmux");
  TCase *tc_chain = tcase_create ("general");

  if (HAVE_ELEMENT ("taginject") && HAVE_ELEMENT ("jpegenc")) {
    tcase_add_test (tc_chain, test_jifmux_tags);
  } else {
    GST_WARNING ("jpegenc or taginject element not available, skipping tests");
  }

  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (jifmux);
