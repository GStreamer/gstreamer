/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION: metadataexif
 * @short_description: This module provides functions to extract tags from
 * EXIF metadata chunks and create EXIF chunks from metadata tags.
 * @see_also: #metadatatags.[c/h]
 *
 * If libexif isn't available at compilation time, only the whole chunk
 * (#METADATA_TAG_MAP_WHOLECHUNK) tags is created. It means that individual
 * tags aren't mapped.
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadataexif.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

/*
 * defines
 */

GST_DEBUG_CATEGORY (gst_metadata_exif_debug);
#define GST_CAT_DEFAULT gst_metadata_exif_debug

GType
gst_meta_exif_byte_order_get_type (void)
{
  static GType meta_exif_byte_order_type = 0;
  static const GEnumValue meta_exif_byte_order[] = {
    {GST_META_EXIF_BYTE_ORDER_MOTOROLA, "Motorola byte-order", "Motorola"},
    {GST_META_EXIF_BYTE_ORDER_INTEL, "Intel byte-order", "Intel"},
    {0, NULL, NULL},
  };

  if (!meta_exif_byte_order_type) {
    meta_exif_byte_order_type =
        g_enum_register_static ("MetaExifByteOrder", meta_exif_byte_order);
  }
  return meta_exif_byte_order_type;
}

/*
 * Implementation when libexif isn't available at compilation time
 */

#ifndef HAVE_EXIF

/*
 * extern functions implementations
 */


void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK) {
    GST_LOG ("EXIF not defined, sending just one tag as whole chunk");
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_EXIF,
        adapter);
  }

}

void
metadatamux_exif_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist, const MetaExifWriteOptions * opts)
{
  /* do nothing */
}

#else /* ifndef HAVE_EXIF */

/*
 * Implementation when libexif is available at compilation time
 */

/*
 * includes
 */

#include <libexif/exif-data.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/*
 * enum and types
 */

typedef struct _tag_MEUserData
{
  GstTagList *taglist;
  GstTagMergeMode mode;
  ExifShort resolution_unit;    /* 2- inches (default), 3- cm */
  int altitude_ref;             /* -1- not specified, 0- above sea, 1- bellow sea */
  gchar latitude_ref;           /* k- not specified, 'N'- north, 'S'- south */
  gchar longitude_ref;          /* k- not specified, 'E'- north, 'W'- south */
} MEUserData;

typedef struct _tag_MapIntStr
{
  ExifTag exif;
  ExifIfd ifd;
  const gchar *str;
} MapIntStr;

/*
 * defines and static global vars
 */

/* *INDENT-OFF* */
/* When changing this table, update 'metadata_mapping.htm' file too. */
static MapIntStr mappedTags[] = {
  {EXIF_TAG_APERTURE_VALUE,              /*RATIONAL,*/ EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_APERTURE              /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_BRIGHTNESS_VALUE,            /*SRATIONAL,*/ EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_BRIGHTNESS            /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_COLOR_SPACE,                 /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_COLOR_SPACE           /*G_TYPE_UINT*/},

  {EXIF_TAG_CONTRAST,                    /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_CONTRAST              /*G_TYPE_INT*/},

  {EXIF_TAG_CUSTOM_RENDERED,             /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_CUSTOM_RENDERED       /*G_TYPE_UINT*/},

  {EXIF_TAG_DIGITAL_ZOOM_RATIO,          /*RATIONAL,*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_DIGITAL_ZOOM          /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_EXPOSURE_PROGRAM,            /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_EXPOSURE_PROGRAM      /*G_TYPE_UINT*/},

  {EXIF_TAG_EXPOSURE_MODE,               /*SHORT,*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_EXPOSURE_MODE         /*G_TYPE_UINT*/},

  {EXIF_TAG_EXPOSURE_TIME,               /*RATIONAL,*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_EXPOSURE_TIME         /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_FLASH,                       /*SHORT*/      EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_FLASH                 /*G_TYPE_UINT*/},

  {EXIF_TAG_FNUMBER,                     /*RATIONAL,*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_FNUMBER               /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_FOCAL_LENGTH,                /*SRATIONAL*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_FOCAL_LEN             /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_GAIN_CONTROL,                /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_GAIN                  /*G_TYPE_UINT*/},

  {EXIF_TAG_ISO_SPEED_RATINGS,           /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_ISO_SPEED_RATINGS     /*G_TYPE_INT*/},

  {EXIF_TAG_LIGHT_SOURCE,                /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_LIGHT_SOURCE          /*G_TYPE_UINT*/},

  {EXIF_TAG_ORIENTATION,                 /*SHORT,*/     EXIF_IFD_0,
   GST_TAG_CAPTURE_ORIENTATION           /*G_TYPE_UINT*/},

  {EXIF_TAG_SATURATION,                  /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_SATURATION            /*G_TYPE_INT*/},

  {EXIF_TAG_SCENE_CAPTURE_TYPE,          /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_SCENE_CAPTURE_TYPE    /*G_TYPE_UINT*/},

  {EXIF_TAG_SHUTTER_SPEED_VALUE,         /*SRATIONAL,*/ EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_SHUTTER_SPEED         /*GST_TYPE_FRACTION*/},

  {EXIF_TAG_WHITE_BALANCE,               /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_WHITE_BALANCE         /*G_TYPE_UINT*/},

  {EXIF_TAG_SOFTWARE,                    /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_CREATOR_TOOL                  /*G_TYPE_STRING*/},

  {EXIF_TAG_DATE_TIME_DIGITIZED,         /*ASCII,*/     EXIF_IFD_EXIF,
   GST_TAG_DATE_TIME_DIGITIZED           /*G_TYPE_STRING*/},

  {EXIF_TAG_DATE_TIME,                   /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_DATE_TIME_MODIFIED            /*G_TYPE_STRING*/},

  {EXIF_TAG_DATE_TIME_ORIGINAL,          /*ASCII,*/     EXIF_IFD_EXIF,
   GST_TAG_DATE_TIME_ORIGINAL            /*G_TYPE_STRING*/},

  {EXIF_TAG_IMAGE_DESCRIPTION,           /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_DESCRIPTION                   /*G_TYPE_STRING*/},

  {EXIF_TAG_MAKE,                        /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_DEVICE_MAKE                   /*G_TYPE_STRING*/},

  {EXIF_TAG_MODEL,                       /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_DEVICE_MODEL                  /*G_TYPE_STRING*/},

  {EXIF_TAG_MAKER_NOTE,                  /*UNDEFINED(size any)*/ EXIF_IFD_EXIF,
   GST_TAG_EXIF_MAKER_NOTE               /*GST_TYPE_BUFFER*/},

  {EXIF_TAG_PIXEL_Y_DIMENSION,           /*LONG,*/      EXIF_IFD_EXIF,
   GST_TAG_IMAGE_HEIGHT                  /*G_TYPE_INT*/},

  {EXIF_TAG_PIXEL_X_DIMENSION,           /*LONG,*/      EXIF_IFD_EXIF,
   GST_TAG_IMAGE_WIDTH                   /*G_TYPE_INT*/},

  {EXIF_TAG_X_RESOLUTION,                /*RATIONAL,*/  EXIF_IFD_0,
   GST_TAG_IMAGE_XRESOLUTION             /*GST_TYPE_FRACTION*/},   /* inches */

  {EXIF_TAG_Y_RESOLUTION,                /*RATIONAL,*/  EXIF_IFD_0,
   GST_TAG_IMAGE_YRESOLUTION             /*GST_TYPE_FRACTION*/},   /* inches */

  {EXIF_TAG_GPS_ALTITUDE,                /*RATIONAL,*/  EXIF_IFD_GPS,
   GST_TAG_GEO_LOCATION_ELEVATION        /*G_TYPE_DOUBLE*/},

  {EXIF_TAG_GPS_LATITUDE,                /*RATIONAL(3),*/  EXIF_IFD_GPS,
   GST_TAG_GEO_LOCATION_LATITUDE         /*G_TYPE_DOUBLE*/},

  {EXIF_TAG_GPS_LONGITUDE,               /*RATIONAL(3),*/  EXIF_IFD_GPS,
   GST_TAG_GEO_LOCATION_LONGITUDE        /*G_TYPE_DOUBLE*/},

  {0, EXIF_IFD_COUNT, NULL}
};
/* *INDENT-ON* */

#define IS_NUMBER(n) ('0' <= (n) && (n) <= '9')
#define IS_FRACT_POSITIVE(n,d) \
  ( ! ( ((n) >> (sizeof(n)*8-1)) ^ ((d) >> (sizeof(d)*8-1)) ) )
#define CHAR_TO_INT(c) ((c) - '0')

/*
 * static helper functions declaration
 */

static const gchar *metadataparse_exif_get_tag_from_exif (ExifTag exif,
    GType * type);

static ExifTag
metadatamux_exif_get_exif_from_tag (const gchar * tag, GType * type,
    ExifIfd * ifd);

static void
metadataparse_exif_data_foreach_content_func (ExifContent * content,
    void *callback_data);

static void
metadataparse_exif_content_foreach_entry_func (ExifEntry * entry,
    void *user_data);

static gboolean
metadataparse_handle_unit_tags (ExifEntry * entry, MEUserData * meudata,
    const ExifByteOrder byte_order);

static void
metadatamux_exif_for_each_tag_in_list (const GstTagList * list,
    const gchar * tag, gpointer user_data);

static gboolean metadataparse_exif_convert_to_datetime (GString * dt);

static gboolean metadatamux_exif_convert_from_datetime (GString * dt);

/*
 * extern functions implementations
 */

/*
 * metadataparse_exif_tag_list_add:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @adapter: contains the EXIF metadata chunk
 * @mapping: if is to extract individual tags and/or the whole chunk.
 * 
 * This function gets a EXIF chunk (@adapter) and extract tags from it 
 * and the add to @taglist.
 * Note: The EXIF chunk (@adapetr) must NOT be wrapped by any bytes specific
 * to any file format
 *
 * Returns: nothing
 */

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{
  const guint8 *buf;
  guint32 size;
  ExifData *exif = NULL;
  MEUserData user_data = { taglist, mode, 2, -1, 'k', 'k' };

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_EXIF,
        adapter);

  if (!(mapping & METADATA_TAG_MAP_INDIVIDUALS))
    goto done;

  buf = gst_adapter_peek (adapter, size);

  exif = exif_data_new_from_data (buf, size);
  if (exif == NULL) {
    goto done;
  }

  exif_data_foreach_content (exif,
      metadataparse_exif_data_foreach_content_func, (void *) &user_data);

done:

  if (exif)
    exif_data_unref (exif);

  return;

}

/*
 * metadatamux_exif_create_chunk_from_tag_list:
 * @buf: buffer that will have the created EXIF chunk
 * @size: size of the buffer that will be created
 * @taglist: list of tags to be added to EXIF chunk
 * @opts: write options for exif metadata
 *
 * Get tags from @taglist, create a EXIF chunk based on it and save to @buf.
 * Note: The EXIF chunk is NOT wrapped by any bytes specific to any file format
 *
 * Returns: nothing
 */

void
metadatamux_exif_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist, const MetaExifWriteOptions * opts)
{
  ExifData *ed = NULL;
  GstBuffer *exif_chunk = NULL;
  const GValue *val = NULL;

  if (!(buf && size))
    goto done;
  if (*buf) {
    g_free (*buf);
    *buf = NULL;
  }
  *size = 0;

  val = gst_tag_list_get_value_index (taglist, GST_TAG_EXIF, 0);
  if (val) {
    exif_chunk = gst_value_get_buffer (val);
    if (exif_chunk) {
      ed = exif_data_new_from_data (GST_BUFFER_DATA (exif_chunk),
          GST_BUFFER_SIZE (exif_chunk));
    }
  }

  if (!ed) {
    ed = exif_data_new ();
    GST_DEBUG ("setting byteorder %d", opts->byteorder);
    switch (opts->byteorder) {
      case GST_META_EXIF_BYTE_ORDER_MOTOROLA:
        exif_data_set_byte_order (ed, EXIF_BYTE_ORDER_MOTOROLA);
        break;
      case GST_META_EXIF_BYTE_ORDER_INTEL:
        exif_data_set_byte_order (ed, EXIF_BYTE_ORDER_INTEL);
        break;
      default:
        break;
    }
    exif_data_set_data_type (ed, EXIF_DATA_TYPE_COMPRESSED);
    exif_data_fix (ed);
  }

  gst_tag_list_foreach (taglist, metadatamux_exif_for_each_tag_in_list, ed);

  exif_data_save_data (ed, buf, size);


done:

  if (ed)
    exif_data_unref (ed);

  return;
}


/*
 * static helper functions implementation
 */

/*
 * metadataparse_exif_get_tag_from_exif:
 * @exif: EXIF tag to look for
 * @type: the type of the GStreamer tag mapped to @exif
 *
 * This returns the GStreamer tag mapped to an EXIF tag.
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>The GStreamer tag mapped to the @exif
 * </para></listitem>
 * <listitem><para>%NULL if there is no mapped GST tag for @exif
 * </para></listitem>
 * </itemizedlist>
 */

static const gchar *
metadataparse_exif_get_tag_from_exif (ExifTag exif, GType * type)
{
  int i = 0;

  while (mappedTags[i].exif) {
    if (exif == mappedTags[i].exif) {
      *type = gst_tag_get_type (mappedTags[i].str);
      break;
    }
    ++i;
  }

  return mappedTags[i].str;

}

/*
 * metadatamux_exif_get_exif_from_tag:
 * @tag: GST tag to look for
 * @type: the type of the GStreamer @tag
 * @ifd: the place into EXIF chunk @exif belongs to.
 *
 * This returns thet EXIF tag mapped to an GStreamer @tag.
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>The EXIF tag mapped to the GST @tag
 * </para></listitem>
 * <listitem><para>0 if there is no mapped EXIF tag for GST @tag
 * </para></listitem>
 * </itemizedlist>
 */

static ExifTag
metadatamux_exif_get_exif_from_tag (const gchar * tag, GType * type,
    ExifIfd * ifd)
{
  int i = 0;

  while (mappedTags[i].exif) {
    if (0 == strcmp (mappedTags[i].str, tag)) {
      *type = gst_tag_get_type (tag);
      *ifd = mappedTags[i].ifd;
      break;
    }
    ++i;
  }

  return mappedTags[i].exif;

}

/*
 * metadataparse_exif_data_foreach_content_func:
 * @content: EXIF structure from libexif containg a IFD
 * @user_data: pointer to #MEUserData
 *
 * This function designed to be called for each EXIF IFD in a EXIF chunk. This
 * function gets calls another function for each tag into @content. Then all
 * tags into a EXIF chunk can be extracted to a tag list in @user_data.
 * @see_also: #metadataparse_exif_tag_list_add
 * #metadataparse_exif_content_foreach_entry_func
 *
 * Returns: nothing
 */

static void
metadataparse_exif_data_foreach_content_func (ExifContent * content,
    void *user_data)
{
  ExifIfd ifd = exif_content_get_ifd (content);

  if (ifd == EXIF_IFD_0 || ifd == EXIF_IFD_EXIF || ifd == EXIF_IFD_GPS) {

    GST_LOG ("\n  Content %p: %s (ifd=%d)", content, exif_ifd_get_name (ifd),
        ifd);
    exif_content_foreach_entry (content,
        metadataparse_exif_content_foreach_entry_func, user_data);
  }
}

/*
 * metadataparse_exif_content_foreach_entry_func:
 * @entry: EXIF structure from libexif having a EXIF tag
 * @user_data: pointer to #MEUserData
 *
 * This function designed to be called for each EXIF tag in a EXIF IFD. This
 * function gets the EXIF tag from @entry and then add to the tag list
 * in @user_data by using a merge mode also specified in @user_data
 * @see_also: #metadataparse_exif_data_foreach_content_func
 *
 * Returns: nothing
 */

static void
metadataparse_exif_content_foreach_entry_func (ExifEntry * entry,
    void *user_data)
{
  MEUserData *meudata = (MEUserData *) user_data;
  GType type = G_TYPE_NONE;
  ExifByteOrder byte_order;
  const gchar *tag;

  /* We need the byte order */
  if (!entry || !entry->parent || !entry->parent->parent)
    return;

  tag = metadataparse_exif_get_tag_from_exif (entry->tag, &type);
  byte_order = exif_data_get_byte_order (entry->parent->parent);

  if (metadataparse_handle_unit_tags (entry, meudata, byte_order))
    goto done;

  if (!tag)
    goto done;

  if (type == GST_TYPE_FRACTION) {
    gint numerator = 0;
    gint denominator = 1;

    switch (entry->format) {
      case EXIF_FORMAT_SRATIONAL:
      {
        ExifSRational v_srat;

        v_srat = exif_get_srational (entry->data, byte_order);
        if (v_srat.denominator) {
          numerator = (gint) v_srat.numerator;
          denominator = (gint) v_srat.denominator;
        }
      }
        break;
      case EXIF_FORMAT_RATIONAL:
      {
        ExifRational v_rat;

        v_rat = exif_get_rational (entry->data, byte_order);
        if (v_rat.denominator) {
          numerator = (gint) v_rat.numerator;
          denominator = (gint) v_rat.denominator;
        }
        if (meudata->resolution_unit == 3) {
          /* converts from cm to inches */
          if (entry->tag == EXIF_TAG_X_RESOLUTION
              || entry->tag == EXIF_TAG_Y_RESOLUTION) {
            numerator *= 2;
            denominator *= 5;
          }
        }
      }
        break;
      default:
        GST_ERROR ("Unexpected Tag Type");
        goto done;
        break;
    }
    gst_tag_list_add (meudata->taglist, meudata->mode, tag, numerator,
        denominator, NULL);

  } else if (type == GST_TYPE_BUFFER) {
    GstBuffer *buf = gst_buffer_new_and_alloc (entry->components);

    memcpy (GST_BUFFER_DATA (buf), entry->data, entry->components);
    gst_tag_list_add (meudata->taglist, meudata->mode, tag, buf, NULL);
    gst_buffer_unref (buf);
  } else {
    switch (type) {
      case G_TYPE_STRING:
      {
        char buf[2048];
        const gchar *str = exif_entry_get_value (entry, buf, sizeof (buf));
        GString *value = NULL;

        if (entry->tag == EXIF_TAG_DATE_TIME_DIGITIZED
            || entry->tag == EXIF_TAG_DATE_TIME
            || entry->tag == EXIF_TAG_DATE_TIME_ORIGINAL) {
          value = g_string_new_len (str, 20);
          /* 20 is enough memory to hold "YYYY-MM-DDTHH:MM:SS" */

          if (metadataparse_exif_convert_to_datetime (value)) {
            str = value->str;
          } else {
            GST_ERROR ("Unexpected date & time format for %s", tag);
            str = NULL;
          }

        }
        if (str)
          gst_tag_list_add (meudata->taglist, meudata->mode, tag, str, NULL);
        if (value)
          g_string_free (value, TRUE);
      }
        break;
      case G_TYPE_INT:
        /* fall through */
      case G_TYPE_UINT:
      {
        gint value;

        switch (entry->format) {
          case EXIF_FORMAT_SHORT:
            value = exif_get_short (entry->data, byte_order);
            break;
          case EXIF_FORMAT_LONG:
            value = exif_get_long (entry->data, byte_order);
            break;
          default:
            GST_ERROR ("Unexpected Exif Tag Type (%s - %s)",
                tag, exif_format_get_name (entry->format));
            goto done;
            break;
        }
        if (entry->tag == EXIF_TAG_CONTRAST ||
            entry->tag == EXIF_TAG_SATURATION) {
          switch (value) {
            case 0:
              break;
            case 1:
              value = -67;      /* -100-34 /2 */
              break;
            case 2:
              value = 67;       /* 100+34 /2 */
              break;
            default:
              GST_ERROR ("Unexpected value");
              break;
          }
        }
        gst_tag_list_add (meudata->taglist, meudata->mode, tag, value, NULL);
      }
        break;
      case G_TYPE_DOUBLE:
      {
        gdouble value = 0.0;

        if (entry->tag == EXIF_TAG_GPS_LATITUDE
            || entry->tag == EXIF_TAG_GPS_LONGITUDE) {
          ExifRational *rt = (ExifRational *) entry->data;

          /* DDD - degrees */
          value = (gdouble) rt->numerator / (gdouble) rt->denominator;
          GST_DEBUG ("deg: %lu / %lu", (gulong) rt->numerator,
              (gulong) rt->denominator);
          rt++;

          /* MM - minutes and SS - seconds */
          GST_DEBUG ("min: %lu / %lu", (gulong) rt->numerator,
              (gulong) rt->denominator);
          value += (gdouble) rt->numerator / ((gdouble) rt->denominator * 60.0);
          rt++;
          GST_DEBUG ("sec: %lu / %lu", (gulong) rt->numerator,
              (gulong) rt->denominator);
          value +=
              (gdouble) rt->numerator / ((gdouble) rt->denominator * 3600.0);

          /* apply sign */
          if (entry->tag == EXIF_TAG_GPS_LATITUDE) {
            if (((meudata->latitude_ref == 'S') && (value > 0.0)) ||
                ((meudata->latitude_ref == 'N') && (value < 0.0))) {
              value = -value;
            }
          } else {
            if (((meudata->longitude_ref == 'W') && (value > 0.0)) ||
                ((meudata->longitude_ref == 'E') && (value < 0.0))) {
              value = -value;
            }
          }
          GST_DEBUG ("long/lat : %lf", value);
        }
        if (entry->tag == EXIF_TAG_GPS_ALTITUDE) {
          ExifRational v_rat = exif_get_rational (entry->data, byte_order);
          value = (gdouble) v_rat.numerator / (gdouble) v_rat.denominator;
          if (((meudata->altitude_ref == 1) && (value > 0.0)) ||
              ((meudata->altitude_ref == 0) && (value < 0.0))) {
            value = -value;
          }
          GST_DEBUG ("altitude = %lf", value);
        }
        gst_tag_list_add (meudata->taglist, meudata->mode, tag, value, NULL);
      }
        break;
      default:
        break;
    }
  }


done:
  {
#ifndef GST_DISABLE_GST_DEBUG
    char buf[2048];
    GST_LOG ("\n    Entry %p: %s (%s)\n"
        "      Size, Comps: %d, %d\n"
        "      Value: %s\n"
        "      Title: %s\n"
        "      Description: %s\n",
        entry,
        exif_tag_get_name (entry->tag),
        exif_format_get_name (entry->format),
        entry->size,
        (int) (entry->components),
        exif_entry_get_value (entry, buf, sizeof (buf)),
        exif_tag_get_title (entry->tag), exif_tag_get_description (entry->tag));
#endif
  }
  return;

}

/*
 * metadataparse_handle_unit_tags:
 * @entry: The exif entry that is supposed to have a tag that makes reference
 * to other tag
 * @meudata: contains references that can be used afterwards and the tag list
 * @byte_order: used to read Exif values
 *
 * This function tries to parse Exif tags that are not mapped to Gst tags
 * but makes reference to another Exif-Gst mapped tag. For example,
 * 'resolution-unit' that says the units of 'image-xresolution', so proper
 * conversion is done or a hint to the conversion is stored to @meudata
 * to be used afterwards.
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%TRUE if the @entry->tag was handled
 * </para></listitem>
 * <listitem><para>%FALSE if this function does'n handle @entry->tag
 * </para></listitem>
 * </itemizedlist>
 */

static gboolean
metadataparse_handle_unit_tags (ExifEntry * entry, MEUserData * meudata,
    const ExifByteOrder byte_order)
{
  gboolean ret = TRUE;

  switch (entry->tag) {
    case EXIF_TAG_RESOLUTION_UNIT:
      meudata->resolution_unit = exif_get_short (entry->data, byte_order);
      if (meudata->resolution_unit == 3) {
        /* if [xy]resolution has alredy been add in cm, replace it in inches */
        gfloat value;

        if (gst_tag_list_get_float (meudata->taglist,
                GST_TAG_IMAGE_XRESOLUTION, &value)) {
          gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_IMAGE_XRESOLUTION, value * 0.4f, NULL);
        }
        if (gst_tag_list_get_float (meudata->taglist,
                GST_TAG_IMAGE_YRESOLUTION, &value)) {
          gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_IMAGE_YRESOLUTION, value * 0.4f, NULL);
        }
      }
      break;
    case EXIF_TAG_GPS_ALTITUDE_REF:
    {
      gdouble value;

      meudata->altitude_ref = entry->data[0];
      if (gst_tag_list_get_double (meudata->taglist,
              GST_TAG_GEO_LOCATION_ELEVATION, &value)) {
        GST_DEBUG ("alt-ref: %d", meudata->altitude_ref);
        if (((meudata->altitude_ref == 1) && (value > 0.0)) ||
            ((meudata->altitude_ref == 0) && (value < 0.0))) {
          gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_GEO_LOCATION_ELEVATION, -value, NULL);
        }
      }
    }
      break;
    case EXIF_TAG_GPS_LATITUDE_REF:
    {
      gdouble value;

      meudata->latitude_ref = entry->data[0];
      if (gst_tag_list_get_double (meudata->taglist,
              GST_TAG_GEO_LOCATION_LATITUDE, &value)) {
        GST_DEBUG ("lat-ref: %c", meudata->latitude_ref);
        if (((meudata->latitude_ref == 'S') && (value > 0.0)) ||
            ((meudata->latitude_ref == 'N') && (value < 0.0))) {
          gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_GEO_LOCATION_LATITUDE, -value, NULL);
        }
      }
    }
      break;
    case EXIF_TAG_GPS_LONGITUDE_REF:
    {
      gdouble value;

      meudata->longitude_ref = entry->data[0];
      if (gst_tag_list_get_double (meudata->taglist,
              GST_TAG_GEO_LOCATION_LONGITUDE, &value)) {
        GST_DEBUG ("lon-ref: %c", meudata->longitude_ref);
        if (((meudata->longitude_ref == 'W') && (value > 0.0)) ||
            ((meudata->longitude_ref == 'E') && (value < 0.0))) {
          gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_GEO_LOCATION_LONGITUDE, -value, NULL);
        }
      }
    }
      break;
    default:
      ret = FALSE;
      break;
  }

  return ret;
}


/*
 * metadatamux_exif_for_each_tag_in_list:
 * @list: GStreamer tag list from which @tag belongs to
 * @tag: GStreamer tag to be added to the EXIF chunk
 * @user_data: pointer to #ExifData in which the tag will be added
 *
 * This function designed to be called for each tag in GST tag list. This
 * function adds get the tag value from tag @list and then add it to the EXIF
 * chunk by using #ExifData and related functions from libexif
 * @see_also: #metadatamux_exif_create_chunk_from_tag_list
 *
 * Returns: nothing
 */

static void
metadatamux_exif_for_each_tag_in_list (const GstTagList * list,
    const gchar * tag, gpointer user_data)
{
  ExifData *ed = (ExifData *) user_data;
  ExifTag exif_tag;
  GType type = G_TYPE_INVALID;
  ExifEntry *entry = NULL;
  ExifIfd ifd = EXIF_IFD_COUNT;
  const ExifByteOrder byte_order = exif_data_get_byte_order (ed);

  exif_tag = metadatamux_exif_get_exif_from_tag (tag, &type, &ifd);

  if (!exif_tag)
    goto done;

  entry = exif_data_get_entry (ed, exif_tag);

  if (entry)
    exif_entry_ref (entry);
  else {
    entry = exif_entry_new ();
    exif_content_add_entry (ed->ifd[ifd], entry);
    exif_entry_initialize (entry, exif_tag);
  }

  if (entry->data == NULL) {
    if (entry->tag == EXIF_TAG_GPS_ALTITUDE) {
      entry->format = EXIF_FORMAT_RATIONAL;
      entry->components = 1;
      entry->size = exif_format_get_size (entry->format) * entry->components;
      entry->data = g_malloc (entry->size);
    } else if (entry->tag == EXIF_TAG_GPS_LATITUDE
        || entry->tag == EXIF_TAG_GPS_LONGITUDE) {
      entry->format = EXIF_FORMAT_RATIONAL;
      entry->components = 3;
      entry->size = exif_format_get_size (entry->format) * entry->components;
      entry->data = g_malloc (entry->size);
    }
  }

  if (type == GST_TYPE_FRACTION) {
    const GValue *gvalue = gst_tag_list_get_value_index (list, tag, 0);
    gint numerator = gst_value_get_fraction_numerator (gvalue);
    gint denominator = gst_value_get_fraction_denominator (gvalue);

    switch (entry->format) {
      case EXIF_FORMAT_SRATIONAL:
      {
        ExifSRational sr = { numerator, denominator };

        exif_set_srational (entry->data, byte_order, sr);
      }
        break;
      case EXIF_FORMAT_RATIONAL:
      {
        ExifRational r;

        if (entry->tag == EXIF_TAG_X_RESOLUTION ||
            entry->tag == EXIF_TAG_Y_RESOLUTION) {
          ExifEntry *unit_entry = NULL;

          unit_entry = exif_data_get_entry (ed, EXIF_TAG_RESOLUTION_UNIT);

          if (unit_entry) {
            ExifShort vsh = exif_get_short (unit_entry->data, byte_order);

            if (vsh != 2)       /* inches */
              exif_set_short (unit_entry->data, byte_order, 2);
          }
        }
        r.numerator = numerator;
        r.denominator = denominator;
        if (numerator < 0)
          r.numerator = -numerator;
        if (denominator < 0)
          r.denominator = -denominator;
        exif_set_rational (entry->data, byte_order, r);
      }
        break;
      default:
        break;
    }
  } else if (type == GST_TYPE_BUFFER) {
    const GValue *val = NULL;
    GstBuffer *buf;

    val = gst_tag_list_get_value_index (list, tag, 0);
    buf = gst_value_get_buffer (val);
    entry->components = GST_BUFFER_SIZE (buf);
    entry->size = GST_BUFFER_SIZE (buf);
    entry->data = g_malloc (entry->size);
    memcpy (entry->data, GST_BUFFER_DATA (buf), entry->size);
  } else {
    switch (type) {
      case G_TYPE_STRING:
      {
        gchar *value = NULL;

        if (gst_tag_list_get_string (list, tag, &value)) {
          if (entry->tag == EXIF_TAG_DATE_TIME_DIGITIZED
              || entry->tag == EXIF_TAG_DATE_TIME
              || entry->tag == EXIF_TAG_DATE_TIME_ORIGINAL) {
            GString *datetime = g_string_new_len (value,
                20);            /* enough memory to hold "YYYY:MM:DD HH:MM:SS" */

            if (metadatamux_exif_convert_from_datetime (datetime)) {
            } else {
              GST_ERROR ("Unexpected date & time format for %s", tag);
            }
            g_free (value);
            value = datetime->str;
            g_string_free (datetime, FALSE);
          } else if (value) {
            entry->components = strlen (value) + 1;
            entry->size =
                exif_format_get_size (entry->format) * entry->components;
            entry->data = (guint8 *) value;
            value = NULL;
          }
        }
      }
        break;
      case G_TYPE_UINT:
      case G_TYPE_INT:
      {
        gint value;
        ExifShort v_short;

        if (G_TYPE_UINT == type) {
          gst_tag_list_get_uint (list, tag, (guint *) & value);
        } else {
          gst_tag_list_get_int (list, tag, &value);
        }

        switch (entry->format) {
          case EXIF_FORMAT_SHORT:
            if (entry->tag == EXIF_TAG_CONTRAST
                || entry->tag == EXIF_TAG_SATURATION) {
              if (value < -33)
                value = 1;      /* low */
              else if (value < 34)
                value = 0;      /* normal */
              else
                value = 2;      /* high */
            }
            v_short = value;
            exif_set_short (entry->data, byte_order, v_short);
            break;
          case EXIF_FORMAT_LONG:
            exif_set_long (entry->data, byte_order, value);
            break;
          default:
            break;
        }

      }
        break;
      case G_TYPE_DOUBLE:
      {
        gdouble value;

        gst_tag_list_get_double (list, tag, &value);
        if (entry->tag == EXIF_TAG_GPS_LATITUDE
            || entry->tag == EXIF_TAG_GPS_LONGITUDE) {
          ExifRational *rt = (ExifRational *) entry->data;
          gdouble v = fabs (value);
          ExifEntry *ref_entry = NULL;
          char ref;
          const ExifTag ref_tag = entry->tag == EXIF_TAG_GPS_LATITUDE ?
              EXIF_TAG_GPS_LATITUDE_REF : EXIF_TAG_GPS_LONGITUDE_REF;

          /* DDD - degrees */
          rt->numerator = (gulong) v;
          rt->denominator = 1;
          GST_DEBUG ("deg: %lf : %lu / %lu", v, (gulong) rt->numerator,
              (gulong) rt->denominator);
          v -= rt->numerator;
          rt++;

          /* MM - minutes */
          rt->numerator = (gulong) (v * 60.0);
          rt->denominator = 1;
          GST_DEBUG ("min: %lf : %lu / %lu", v, (gulong) rt->numerator,
              (gulong) rt->denominator);
          v -= ((gdouble) rt->numerator / 60.0);
          rt++;

          /* SS - seconds */
          rt->numerator = (gulong) (0.5 + v * 3600.0);
          rt->denominator = 1;
          GST_DEBUG ("sec: %lf : %lu / %lu", v, (gulong) rt->numerator,
              (gulong) rt->denominator);

          if (entry->tag == EXIF_TAG_GPS_LONGITUDE) {
            GST_DEBUG ("longitude : %lf", value);
            ref = (value < 0.0) ? 'W' : 'E';
          } else {
            GST_DEBUG ("latitude : %lf", value);
            ref = (value < 0.0) ? 'S' : 'N';
          }

          ref_entry = exif_data_get_entry (ed, ref_tag);
          if (ref_entry) {
            exif_entry_ref (ref_entry);
          } else {
            ref_entry = exif_entry_new ();
            exif_content_add_entry (ed->ifd[EXIF_IFD_GPS], ref_entry);
            exif_entry_initialize (ref_entry, ref_tag);
          }
          if (ref_entry->data == NULL) {
            ref_entry->format = EXIF_FORMAT_ASCII;
            ref_entry->components = 2;
            ref_entry->size = 2;
            ref_entry->data = g_malloc (2);
          }
          ref_entry->data[0] = ref;
          ref_entry->data[1] = 0;
          exif_entry_unref (ref_entry);
        } else if (entry->tag == EXIF_TAG_GPS_ALTITUDE) {
          ExifEntry *ref_entry = NULL;
          ExifRational *rt = (ExifRational *) entry->data;

          rt->numerator = (gulong) fabs (10.0 * value);
          rt->denominator = 10;

          GST_DEBUG ("altitude : %lf", value);

          ref_entry = exif_data_get_entry (ed, EXIF_TAG_GPS_ALTITUDE_REF);
          if (ref_entry) {
            exif_entry_ref (ref_entry);
          } else {
            ref_entry = exif_entry_new ();
            exif_content_add_entry (ed->ifd[EXIF_IFD_GPS], ref_entry);
            exif_entry_initialize (ref_entry, EXIF_TAG_GPS_ALTITUDE_REF);
          }
          if (ref_entry->data == NULL) {
            ref_entry->format = EXIF_FORMAT_BYTE;
            ref_entry->components = 1;
            ref_entry->size = 1;
            ref_entry->data = g_malloc (1);
          }
          if (value > 0.0) {
            ref_entry->data[0] = 0;
          } else {
            ref_entry->data[0] = 1;
          }
          exif_entry_unref (ref_entry);
        }
      }
        break;
      default:
        break;
    }
  }

done:

  if (entry)
    exif_entry_unref (entry);

}

/*
 * metadataparse_exif_convert_to_datetime:
 * @dt: string containing date_time in format "YYYY:MM:DD HH:MM:SS"
 *
 * This function converts a exif date_and_time string @dt into the format
 * specified by http://www.w3.org/TR/1998/NOTE-datetime-19980827, in which a 
 * subset of ISO RFC 8601 used by XMP.
 * @dt->allocated_len must be not less than 21 to hold enough memory
 * hold "YYYY:MM:DD HH:MM:SS" without additional allocation
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>#TRUE if succeded
 * </para></listitem>
 * <listitem><para>#FALSE if failed
 * </para></listitem>
 * </itemizedlist>
 */

static gboolean
metadataparse_exif_convert_to_datetime (GString * dt)
{

  /* "YYYY:MM:DD HH:MM:SS" */
  /*  012345678901234567890 */

  if (dt->allocated_len < 20)
    return FALSE;

  /* Fix me: Ideally would be good to get the time zone from othe Exif tag
   * for the time being, just use local time as explained in XMP specification
   * (converting from EXIF to XMP date and time) */

  dt->str[4] = '-';
  dt->str[7] = '-';
  dt->str[10] = 'T';
  dt->str[19] = '\0';

  return TRUE;

}


/*
 * metadatamux_exif_convert_from_datetime:
 * @dt: string containing date_time in format specified by
 * http://www.w3.org/TR/1998/NOTE-datetime-19980827
 *
 * This function converts a date_and_time string @dt as specified by
 * http://www.w3.org/TR/1998/NOTE-datetime-19980827, in which a 
 * subset of ISO RFC 8601 used by XMP, into Exif date_time format.
 * @dt->allocated_len must be not less than 20 to hold enough memory
 * hold "YYYY:MM:DD HH:MM:SS" without additional allocation
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>#TRUE if succeded
 * </para></listitem>
 * <listitem><para>#FALSE if failed
 * </para></listitem>
 * </itemizedlist>
 */

static gboolean
metadatamux_exif_convert_from_datetime (GString * dt)
{
  gboolean ret = TRUE;
  char *p = dt->str;

  if (dt->allocated_len < 20)
    goto error;

  /* check YYYY */

  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;

  if (*p == '\0') {
    sprintf (p, ":01:01 00:00:00");
    goto done;
  } else if (*p == '-') {
    *p++ = ':';
  } else
    goto error;

  /* check MM */

  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;

  if (*p == '\0') {
    sprintf (p, ":01 00:00:00");
    goto done;
  } else if (*p == '-') {
    *p++ = ':';
  } else
    goto error;

  /* check DD */

  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;

  if (*p == '\0') {
    sprintf (p, " 00:00:00");
    goto done;
  } else if (*p == 'T') {
    *p++ = ' ';
  } else
    goto error;

  /* check hh */

  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;

  if (*p++ != ':')
    goto error;

  /* check mm */

  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;

  if (*p == ':') {
    p++;
  } else if (*p == 'Z' || *p == '+' || *p == '-') {
    /* FIXME: in case of '+' or '-', it would be better to also fill another
     * EXIF tag in order to save, somehow the time zone info */
    sprintf (p, ":00");
    goto done;
  } else
    goto error;

  /* check ss */

  if (IS_NUMBER (*p))
    p++;
  else
    goto error;
  if (IS_NUMBER (*p))
    p++;
  else
    goto error;

  *p = '\0';

  /* if here, everything is ok */
  goto done;
error:

  ret = FALSE;

done:

  /* FIXME: do we need to check if the date is valid ? */

  if (ret)
    dt->len = 19;
  return ret;

}

#endif /* else (ifndef HAVE_EXIF) */
