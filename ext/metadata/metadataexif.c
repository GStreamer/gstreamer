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

#include "metadataexif.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

GST_DEBUG_CATEGORY (gst_metadata_exif_debug);
#define GST_CAT_DEFAULT gst_metadata_exif_debug

#ifndef HAVE_EXIF

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK) {
    GST_LOG
        ("EXIF not defined, here I should send just one tag as whole chunk");
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_EXIF,
        adapter);
  }

}

void
metadatamux_exif_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  /* do nothing */
}

#else /* ifndef HAVE_EXIF */

#include <libexif/exif-data.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct _tag_MEUserData
{
  GstTagList *taglist;
  GstTagMergeMode mode;
  ExifShort resolution_unit;    /* 2- inches (default), 3- cm */
} MEUserData;

typedef struct _tag_MapIntStr
{
  ExifTag exif;
  ExifIfd ifd;
  const gchar *str;
} MapIntStr;

static void
exif_data_foreach_content_func (ExifContent * content, void *callback_data);

static void exif_content_foreach_entry_func (ExifEntry * entry, void *);

/* *INDENT-OFF* */
static MapIntStr mappedTags[] = {
  {EXIF_TAG_MAKE,               /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_DEVICE_MAKE,              /*STRING*/},
  {EXIF_TAG_MODEL,              /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_DEVICE_MODEL,             /*STRING*/},
  {EXIF_TAG_SOFTWARE,           /*ASCII,*/     EXIF_IFD_0,
   GST_TAG_CREATOR_TOOL,             /*STRING*/},
  {EXIF_TAG_X_RESOLUTION,       /*RATIONAL,*/  EXIF_IFD_0,
   GST_TAG_IMAGE_XRESOLUTION,        /*FRACTION*/},   /* inches */
  {EXIF_TAG_Y_RESOLUTION,       /*RATIONAL,*/  EXIF_IFD_0,
   GST_TAG_IMAGE_YRESOLUTION,        /*FRACTION*/},   /* inches */
  {EXIF_TAG_EXPOSURE_TIME,      /*RATIONAL,*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_EXPOSURE_TIME,    /*FRACTION*/},
  {EXIF_TAG_FNUMBER,            /*RATIONAL,*/  EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_FNUMBER,          /*FRACTION*/},
  {EXIF_TAG_EXPOSURE_PROGRAM,   /*SHORT,*/     EXIF_IFD_EXIF,
   GST_TAG_CAPTURE_EXPOSURE_PROGRAM, /*UINT*/},
  {EXIF_TAG_BRIGHTNESS_VALUE,   /*SRATIONAL,*/ EXIF_IFD_0,
   GST_TAG_CAPTURE_BRIGHTNESS,       /*FRACTION*/},
  {EXIF_TAG_WHITE_BALANCE,      /*SHORT,*/     EXIF_IFD_0,
   GST_TAG_CAPTURE_WHITE_BALANCE,    /*UINT*/},
  {EXIF_TAG_DIGITAL_ZOOM_RATIO, /*RATIONAL,*/  EXIF_IFD_0,
   GST_TAG_CAPTURE_DIGITAL_ZOOM,     /*FRACTION*/},
  {EXIF_TAG_GAIN_CONTROL,       /*SHORT,*/     EXIF_IFD_0,
   GST_TAG_CAPTURE_GAIN,             /*UINT*/},
  {EXIF_TAG_CONTRAST,           /*SHORT,*/     EXIF_IFD_0,
   GST_TAG_CAPTURE_CONTRAST,         /*INT*/},
  {EXIF_TAG_SATURATION,         /*SHORT,*/     EXIF_IFD_0,
   GST_TAG_CAPTURE_SATURATION,       /*INT*/},
  {0, EXIF_IFD_COUNT, NULL}
};
/* *INDENT-ON* */

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

static ExifTag
metadataparse_exif_get_exif_from_tag (const gchar * tag, GType * type,
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

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{
  const guint8 *buf;
  guint32 size;
  ExifData *exif = NULL;
  MEUserData user_data = { taglist, mode, 2 };

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

  exif_data_foreach_content (exif, exif_data_foreach_content_func,
      (void *) &user_data);

done:

  if (exif)
    exif_data_unref (exif);

  return;

}

static void
exif_data_foreach_content_func (ExifContent * content, void *user_data)
{
  ExifIfd ifd = exif_content_get_ifd (content);

  GST_LOG ("\n  Content %p: %s (ifd=%d)", content, exif_ifd_get_name (ifd),
      ifd);
  exif_content_foreach_entry (content, exif_content_foreach_entry_func,
      user_data);
}

#if 0
static gboolean
exif_fast_mdc (glong n, glong d, gulong * m)
{
  gboolean ret = FALSE;

  static const int a[] =
      { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 39, 41, 43, 47, 49, 53, 0 };
  int i = 0;

  *m = 1;

  while (a[i] <= n && a[i] <= d) {
    while ((n % a[i] == 0) && (d % a[i]) == 0) {
      *m *= a[i];
      ret = TRUE;
    }
    ++i;
  }

  return ret;

}
#endif

static void
exif_content_foreach_entry_func (ExifEntry * entry, void *user_data)
{
  char buf[2048];
  MEUserData *meudata = (MEUserData *) user_data;
  GType type;
  ExifByteOrder byte_order;
  const gchar *tag = metadataparse_exif_get_tag_from_exif (entry->tag, &type);

  /* We need the byte order */
  if (!entry || !entry->parent || !entry->parent->parent)
    return;
  byte_order = exif_data_get_byte_order (entry->parent->parent);

  if (entry->tag == EXIF_TAG_RESOLUTION_UNIT) {
    meudata->resolution_unit = exif_get_short (entry->data, byte_order);
    if (meudata->resolution_unit == 3) {
      /* if [xy]resolution has alredy been add in cm, replace it in inches */
      gfloat value;

      if (gst_tag_list_get_float (meudata->taglist, GST_TAG_IMAGE_XRESOLUTION,
              &value))
        gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_IMAGE_XRESOLUTION, value * 0.4f, NULL);
      if (gst_tag_list_get_float (meudata->taglist, GST_TAG_IMAGE_YRESOLUTION,
              &value))
        gst_tag_list_add (meudata->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_IMAGE_YRESOLUTION, value * 0.4f, NULL);
    }
    goto done;
  }

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

  } else {

    switch (type) {
      case G_TYPE_STRING:
        gst_tag_list_add (meudata->taglist, meudata->mode, tag,
            exif_entry_get_value (entry, buf, sizeof (buf)), NULL);
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
      default:
        break;
    }

  }


done:

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

  return;

}

/*
 *
 */

static ExifRational
float_to_rational (gfloat f)
{
  ExifRational r;
  int i = 6;                    /* precision */

  r.denominator = 1;

  while (i--) {
    if (f == floorf (f)) {
      break;
    }
    f *= 10.0f;
    r.denominator *= 10;
  }

  r.numerator = f;

  if (!(r.numerator & 0x1 || r.denominator & 0x1)) {
    /* divide both by 2 */
    r.numerator >>= 1;
    r.denominator >>= 1;
  }
  if (r.numerator % 5 == 0 && r.denominator % 5 == 0) {
    r.numerator /= 5;
    r.denominator /= 5;
  }

  return r;

}

static ExifSRational
float_to_srational (gfloat f)
{
  ExifSRational sr;
  int i = 6;                    /* precision */

  sr.denominator = 1;

  while (i--) {
    if (f == floorf (f)) {
      break;
    }
    f *= 10.0f;
    sr.denominator *= 10;
  }

  sr.numerator = f;

  if (!(sr.numerator & 0x1 || sr.denominator & 0x1)) {
    /* divide both by 2 */
    sr.numerator >>= 1;
    sr.denominator >>= 1;
  }
  if (sr.numerator % 5 == 0 && sr.denominator % 5 == 0) {
    sr.numerator /= 5;
    sr.denominator /= 5;
  }

  return sr;

}

static void
metadataexif_for_each_tag_in_list (const GstTagList * list, const gchar * tag,
    gpointer user_data)
{
  ExifData *ed = (ExifData *) user_data;
  ExifTag exif_tag;
  GType type;
  ExifEntry *entry = NULL;
  ExifIfd ifd;
  const ExifByteOrder byte_order = exif_data_get_byte_order (ed);

  exif_tag = metadataparse_exif_get_exif_from_tag (tag, &type, &ifd);

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
        ExifRational r = { numerator, denominator };

        exif_set_rational (entry->data, byte_order, r);
        if (entry->tag == EXIF_TAG_X_RESOLUTION ||
            entry->tag == EXIF_TAG_Y_RESOLUTION) {
          ExifEntry *unit_entry = NULL;

          if ((unit_entry = exif_data_get_entry (ed, EXIF_TAG_RESOLUTION_UNIT))) {
            ExifShort vsh = exif_get_short (unit_entry->data, byte_order);

            if (vsh != 2)       /* inches */
              exif_set_short (unit_entry->data, byte_order, 2);
          }
        }
      }
        break;
      default:
        break;
    }
  } else {

    switch (type) {
      case G_TYPE_STRING:
      {
        gchar *value = NULL;

        if (gst_tag_list_get_string (list, tag, &value)) {
          entry->components = strlen (value) + 1;
          entry->size =
              exif_format_get_size (entry->format) * entry->components;
          entry->data = value;
        }
      }
        break;
      case G_TYPE_UINT:
      case G_TYPE_INT:
      {
        gint value;
        ExifShort v_short;

        if (G_TYPE_UINT == type) {
          gst_tag_list_get_uint (list, tag, &value);
        } else {
          gst_tag_list_get_int (list, tag, &value);
        }
        if (entry->tag == EXIF_TAG_CONTRAST
            || entry->tag == EXIF_TAG_SATURATION) {
          if (value < -33)
            value = 1;          /* low */
          else if (value < 34)
            value = 0;          /* normal */
          else
            value = 2;          /* high */
        }
        v_short = value;
        exif_set_short (entry->data, byte_order, v_short);
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

void
metadatamux_exif_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
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
    exif_data_set_data_type (ed, EXIF_DATA_TYPE_COMPRESSED);
    exif_data_fix (ed);
  }

  gst_tag_list_foreach (taglist, metadataexif_for_each_tag_in_list, ed);

  exif_data_save_data (ed, buf, size);


done:

  if (ed)
    exif_data_unref (ed);

  return;
}

#endif /* else (ifndef HAVE_EXIF) */
