/*
 * Copyright (C) 2007 Haakon Sporsheim <hakon.sporsheim@tandberg.com>
 *               2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

#include "ksvideohelpers.h"

#include <uuids.h>
#include "kshelpers.h"

GST_DEBUG_CATEGORY_EXTERN (gst_ks_debug);
#define GST_CAT_DEFAULT gst_ks_debug

static const GUID MEDIASUBTYPE_FOURCC =
    { 0x0 /* FourCC here */ , 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00,
    0x38, 0x9B, 0x71}
};

extern const GUID MEDIASUBTYPE_I420 =
    { 0x30323449, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B,
    0x71}
};

static GstStructure *
ks_video_format_to_structure (GUID subtype_guid, GUID format_guid)
{
  GstStructure *structure = NULL;

  if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_MJPG) || IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_TVMJ) ||     /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_WAKE) ||        /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_CFCC) ||        /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_IJPG)) {        /* FIXME: NOT tested */
    structure = gst_structure_new ("image/jpeg", NULL);
  } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB555) ||       /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB565) ||      /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB24) || IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB32) ||   /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_ARGB1555) ||    /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_ARGB32) ||      /* FIXME: NOT tested */
      IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_ARGB4444)) {    /* FIXME: NOT tested */
    guint depth = 0, bpp = 0;
    gint endianness = 0;
    guint32 r_mask = 0, b_mask = 0, g_mask = 0;

    if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB555)) {
      bpp = 16;
      depth = 15;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x7c00;
      g_mask = 0x03e0;
      b_mask = 0x001f;
    } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB565)) {
      bpp = depth = 16;
      endianness = G_BIG_ENDIAN;
      r_mask = 0xf800;
      g_mask = 0x07e0;
      b_mask = 0x001f;
    } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB24)) {
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x0000ff;
      g_mask = 0x00ff00;
      b_mask = 0xff0000;
    } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_RGB32)) {
      bpp = 32;
      depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x000000ff;
      g_mask = 0x0000ff00;
      b_mask = 0x00ff0000;
      /* FIXME: check
       *r_mask = 0xff000000;
       *g_mask = 0x00ff0000;
       *b_mask = 0x0000ff00;
       */
    } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_ARGB1555)) {
      bpp = 16;
      depth = 15;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x7c00;
      g_mask = 0x03e0;
      b_mask = 0x001f;
    } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_ARGB32)) {
      bpp = depth = 32;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x000000ff;
      g_mask = 0x0000ff00;
      b_mask = 0x00ff0000;
      /* FIXME: check
       *r_mask = 0xff000000;
       *g_mask = 0x00ff0000;
       *b_mask = 0x0000ff00;
       */
    } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_ARGB4444)) {
      bpp = 16;
      depth = 12;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x0f00;
      g_mask = 0x00f0;
      b_mask = 0x000f;
      //r_mask = 0x000f;
      //g_mask = 0x00f0;
      //b_mask = 0x0f00;
    } else {
      g_assert_not_reached ();
    }

    structure = gst_structure_new ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, bpp,
        "depth", G_TYPE_INT, depth,
        "red_mask", G_TYPE_INT, r_mask,
        "green_mask", G_TYPE_INT, g_mask,
        "blue_mask", G_TYPE_INT, b_mask,
        "endianness", G_TYPE_INT, endianness, NULL);
  } else if (IsEqualGUID (&subtype_guid, &MEDIASUBTYPE_dvsd)) {
    if (IsEqualGUID (&format_guid, &FORMAT_DvInfo)) {
      structure = gst_structure_new ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
    } else if (IsEqualGUID (&format_guid, &FORMAT_VideoInfo)) {
      structure = gst_structure_new ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('d', 'v', 's', 'd'),
          NULL);
    }
  } else if (memcmp (&subtype_guid.Data2, &MEDIASUBTYPE_FOURCC.Data2,
          sizeof (subtype_guid) - sizeof (subtype_guid.Data1)) == 0) {
    guint8 *p = (guint8 *) & subtype_guid.Data1;

    structure = gst_structure_new ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC (p[0], p[1], p[2], p[3]),
        NULL);
  }

  if (!structure) {
    GST_DEBUG ("Unknown DirectShow Video GUID %08x-%04x-%04x-%04x-%08x%04x",
        subtype_guid.Data1, subtype_guid.Data2, subtype_guid.Data3,
        *(WORD *) subtype_guid.Data4, *(DWORD *) & subtype_guid.Data4[2],
        *(WORD *) & subtype_guid.Data4[6]);
  }

  return structure;
}

static gboolean
ks_video_append_video_stream_cfg_fields (GstStructure * structure,
    const KS_VIDEO_STREAM_CONFIG_CAPS * vscc)
{
  g_return_val_if_fail (structure, FALSE);
  g_return_val_if_fail (vscc, FALSE);

  /* width */
  if (vscc->MinOutputSize.cx == vscc->MaxOutputSize.cx) {
    gst_structure_set (structure,
        "width", G_TYPE_INT, vscc->MaxOutputSize.cx, NULL);
  } else {
    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE,
        vscc->MinOutputSize.cx, vscc->MaxOutputSize.cx, NULL);
  }

  /* height */
  if (vscc->MinOutputSize.cy == vscc->MaxOutputSize.cy) {
    gst_structure_set (structure,
        "height", G_TYPE_INT, vscc->MaxOutputSize.cy, NULL);
  } else {
    gst_structure_set (structure,
        "height", GST_TYPE_INT_RANGE,
        vscc->MinOutputSize.cy, vscc->MaxOutputSize.cy, NULL);
  }

  /* framerate */
  if (vscc->MinFrameInterval == vscc->MaxFrameInterval) {
    gst_structure_set (structure,
        "framerate", GST_TYPE_FRACTION,
        (gint) (10000000 / vscc->MaxFrameInterval), 1, NULL);
  } else {
    gst_structure_set (structure,
        "framerate", GST_TYPE_FRACTION_RANGE,
        (gint) (10000000 / vscc->MaxFrameInterval), 1,
        (gint) (10000000 / vscc->MinFrameInterval), 1, NULL);
  }

  return TRUE;
}

KsVideoMediaType *
ks_video_media_type_dup (KsVideoMediaType * media_type)
{
  KsVideoMediaType *result = g_new (KsVideoMediaType, 1);

  memcpy (result, media_type, sizeof (KsVideoMediaType));

  result->range = g_malloc (media_type->range->FormatSize);
  memcpy ((gpointer) result->range, media_type->range,
      media_type->range->FormatSize);

  result->format = g_malloc (media_type->format_size);
  memcpy (result->format, media_type->format, media_type->format_size);

  result->translated_caps = gst_caps_ref (media_type->translated_caps);

  return result;
}

void
ks_video_media_type_free (KsVideoMediaType * media_type)
{
  if (media_type == NULL)
    return;

  g_free ((gpointer) media_type->range);

  g_free (media_type->format);

  if (media_type->translated_caps != NULL)
    gst_caps_unref (media_type->translated_caps);

  g_free (media_type);
}

static GList *
ks_video_media_type_list_remove_duplicates (GList * media_types)
{
  GList *master, *duplicates;

  do {
    GList *entry;

    master = duplicates = NULL;

    /* Find the first set of duplicates and their master */
    for (entry = media_types; entry != NULL && duplicates == NULL;
        entry = entry->next) {
      KsVideoMediaType *mt = entry->data;
      GList *other_entry;

      for (other_entry = media_types; other_entry != NULL;
          other_entry = other_entry->next) {
        KsVideoMediaType *other_mt = other_entry->data;

        if (other_mt == mt)
          continue;

        if (gst_caps_is_equal (mt->translated_caps, other_mt->translated_caps))
          duplicates = g_list_prepend (duplicates, other_mt);
      }

      if (duplicates != NULL)
        master = entry;
    }

    if (duplicates != NULL) {
      KsVideoMediaType *selected_mt = master->data;

      /*
       * Pick a FORMAT_VideoInfo2 if present, if not we just stay with the
       * first entry
       */
      for (entry = duplicates; entry != NULL; entry = entry->next) {
        KsVideoMediaType *mt = entry->data;

        if (IsEqualGUID (&mt->range->Specifier, &FORMAT_VideoInfo2)) {
          ks_video_media_type_free (selected_mt);
          selected_mt = mt;
        } else {
          ks_video_media_type_free (mt);
        }

        /* Remove the dupe from the main list */
        media_types = g_list_remove (media_types, mt);
      }

      /* Update master node with the selected MediaType */
      master->data = selected_mt;

      g_list_free (duplicates);
    }
  }
  while (master != NULL);

  return media_types;
}

GList *
ks_video_probe_filter_for_caps (HANDLE filter_handle)
{
  GList *ret = NULL;
  gulong pin_count;
  guint pin_id;

  if (!ks_filter_get_pin_property (filter_handle, 0, KSPROPSETID_Pin,
          KSPROPERTY_PIN_CTYPES, &pin_count, sizeof (pin_count)))
    goto beach;

  GST_DEBUG ("pin_count = %d", pin_count);

  for (pin_id = 0; pin_id < pin_count; pin_id++) {
    KSPIN_COMMUNICATION pin_comm;
    KSPIN_DATAFLOW pin_flow;
    GUID pin_cat;

    if (!ks_filter_get_pin_property (filter_handle, pin_id, KSPROPSETID_Pin,
            KSPROPERTY_PIN_COMMUNICATION, &pin_comm, sizeof (pin_comm)))
      continue;

    if (!ks_filter_get_pin_property (filter_handle, pin_id, KSPROPSETID_Pin,
            KSPROPERTY_PIN_DATAFLOW, &pin_flow, sizeof (pin_flow)))
      continue;

    if (!ks_filter_get_pin_property (filter_handle, pin_id, KSPROPSETID_Pin,
            KSPROPERTY_PIN_CATEGORY, &pin_cat, sizeof (pin_cat)))
      continue;

    GST_DEBUG ("pin[%d]: pin_comm=%d, pin_flow=%d", pin_id, pin_comm, pin_flow);

    if (pin_flow == KSPIN_DATAFLOW_OUT &&
        memcmp (&pin_cat, &PINNAME_CAPTURE, sizeof (GUID)) == 0) {
      KSMULTIPLE_ITEM *items;

      if (ks_filter_get_pin_property_multi (filter_handle, pin_id,
              KSPROPSETID_Pin, KSPROPERTY_PIN_DATARANGES, &items)) {
        KSDATARANGE *range = (KSDATARANGE *) (items + 1);
        guint i;

        for (i = 0; i < items->Count; i++) {
          if (IsEqualGUID (&range->MajorFormat, &KSDATAFORMAT_TYPE_VIDEO)) {
            KsVideoMediaType *entry;
            gpointer src_vscc, src_format;
            GstStructure *media_structure;

            entry = g_new0 (KsVideoMediaType, 1);
            entry->pin_id = pin_id;

            entry->range = g_malloc (range->FormatSize);
            memcpy ((gpointer) entry->range, range, range->FormatSize);

            if (IsEqualGUID (&range->Specifier, &FORMAT_VideoInfo)) {
              KS_DATARANGE_VIDEO *vr = (KS_DATARANGE_VIDEO *) entry->range;

              src_vscc = &vr->ConfigCaps;
              src_format = &vr->VideoInfoHeader;

              entry->format_size = sizeof (vr->VideoInfoHeader);
              entry->sample_size = vr->VideoInfoHeader.bmiHeader.biSizeImage;
            } else if (IsEqualGUID (&range->Specifier, &FORMAT_VideoInfo2)) {
              KS_DATARANGE_VIDEO2 *vr = (KS_DATARANGE_VIDEO2 *) entry->range;

              src_vscc = &vr->ConfigCaps;
              src_format = &vr->VideoInfoHeader;

              entry->format_size = sizeof (vr->VideoInfoHeader);
              entry->sample_size = vr->VideoInfoHeader.bmiHeader.biSizeImage;
            } else if (IsEqualGUID (&range->Specifier, &FORMAT_MPEGVideo)) {
              /* Untested and probably wrong... */
              KS_DATARANGE_MPEG1_VIDEO *vr =
                  (KS_DATARANGE_MPEG1_VIDEO *) entry->range;

              src_vscc = &vr->ConfigCaps;
              src_format = &vr->VideoInfoHeader;

              entry->format_size = sizeof (vr->VideoInfoHeader);
              entry->sample_size =
                  vr->VideoInfoHeader.hdr.bmiHeader.biSizeImage;
            } else if (IsEqualGUID (&range->Specifier, &FORMAT_MPEG2Video)) {
              /* Untested and probably wrong... */
              KS_DATARANGE_MPEG2_VIDEO *vr =
                  (KS_DATARANGE_MPEG2_VIDEO *) entry->range;

              src_vscc = &vr->ConfigCaps;
              src_format = &vr->VideoInfoHeader;

              entry->format_size = sizeof (vr->VideoInfoHeader);
              entry->sample_size =
                  vr->VideoInfoHeader.hdr.bmiHeader.biSizeImage;
            } else
              g_assert_not_reached ();

            g_assert (entry->sample_size != 0);

            memcpy ((gpointer) & entry->vscc, src_vscc, sizeof (entry->vscc));

            entry->format = g_malloc (entry->format_size);
            memcpy (entry->format, src_format, entry->format_size);

            media_structure =
                ks_video_format_to_structure (range->SubFormat,
                range->MajorFormat);

            if (media_structure == NULL) {
              g_warning ("ks_video_format_to_structure returned NULL");
              ks_video_media_type_free (entry);
              entry = NULL;
            } else if (ks_video_append_video_stream_cfg_fields (media_structure,
                    &entry->vscc)) {
              entry->translated_caps = gst_caps_new_empty ();
              gst_caps_append_structure (entry->translated_caps,
                  media_structure);
            } else {
              gst_structure_free (media_structure);
              ks_video_media_type_free (entry);
              entry = NULL;
            }

            if (entry != NULL)
              ret = g_list_prepend (ret, entry);
          }

          /* REVISIT: Each KSDATARANGE should start on a 64-bit boundary */
          range = (KSDATARANGE *) (((guchar *) range) + range->FormatSize);
        }

        g_free (items);
      }
    }
  }

  if (ret != NULL) {
    ret = g_list_reverse (ret);
    ret = ks_video_media_type_list_remove_duplicates (ret);
  }

beach:
  return ret;
}

KSPIN_CONNECT *
ks_video_create_pin_conn_from_media_type (KsVideoMediaType * media_type)
{
  KSPIN_CONNECT *conn = NULL;
  KSDATAFORMAT *format = NULL;
  guint8 *vih;

  conn = g_malloc0 (sizeof (KSPIN_CONNECT) + sizeof (KSDATAFORMAT) +
      media_type->format_size);

  conn->Interface.Set = KSINTERFACESETID_Standard;
  conn->Interface.Id = KSINTERFACE_STANDARD_STREAMING;
  conn->Interface.Flags = 0;

  conn->Medium.Set = KSMEDIUMSETID_Standard;
  conn->Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
  conn->Medium.Flags = 0;

  conn->PinId = media_type->pin_id;
  conn->PinToHandle = NULL;
  conn->Priority.PriorityClass = KSPRIORITY_NORMAL;
  conn->Priority.PrioritySubClass = 1;

  format = (KSDATAFORMAT *) (conn + 1);
  memcpy (format, media_type->range, sizeof (KSDATAFORMAT));
  format->FormatSize = sizeof (KSDATAFORMAT) + media_type->format_size;

  vih = (guint8 *) (format + 1);
  memcpy (vih, media_type->format, media_type->format_size);

  return conn;
}

gboolean
ks_video_fixate_media_type (const KSDATARANGE * range,
    guint8 * format, gint width, gint height, gint fps_n, gint fps_d)
{
  DWORD dwRate = (width * height * fps_n) / fps_d;

  g_return_val_if_fail (format != NULL, FALSE);

  if (IsEqualGUID (&range->Specifier, &FORMAT_VideoInfo)) {
    KS_VIDEOINFOHEADER *vih = (KS_VIDEOINFOHEADER *) format;

    vih->AvgTimePerFrame = gst_util_uint64_scale_int (10000000, fps_d, fps_n);
    vih->dwBitRate = dwRate * vih->bmiHeader.biBitCount;

    g_assert (vih->bmiHeader.biWidth == width);
    g_assert (vih->bmiHeader.biHeight == height);
  } else if (IsEqualGUID (&range->Specifier, &FORMAT_VideoInfo2)) {
    KS_VIDEOINFOHEADER2 *vih = (KS_VIDEOINFOHEADER2 *) format;

    vih->AvgTimePerFrame = gst_util_uint64_scale_int (10000000, fps_d, fps_n);
    vih->dwBitRate = dwRate * vih->bmiHeader.biBitCount;

    g_assert (vih->bmiHeader.biWidth == width);
    g_assert (vih->bmiHeader.biHeight == height);
  } else if (IsEqualGUID (&range->Specifier, &FORMAT_MPEGVideo)) {
    KS_MPEG1VIDEOINFO *vih = (KS_MPEG1VIDEOINFO *) format;

    vih->hdr.AvgTimePerFrame =
        gst_util_uint64_scale_int (10000000, fps_d, fps_n);
    vih->hdr.dwBitRate = dwRate * vih->hdr.bmiHeader.biBitCount;

    /* FIXME: set height and width? */
    g_assert (vih->hdr.bmiHeader.biWidth == width);
    g_assert (vih->hdr.bmiHeader.biHeight == height);
  } else if (IsEqualGUID (&range->Specifier, &FORMAT_MPEG2Video)) {
    KS_MPEGVIDEOINFO2 *vih = (KS_MPEGVIDEOINFO2 *) format;

    vih->hdr.AvgTimePerFrame =
        gst_util_uint64_scale_int (10000000, fps_d, fps_n);
    vih->hdr.dwBitRate = dwRate * vih->hdr.bmiHeader.biBitCount;

    /* FIXME: set height and width? */
    g_assert (vih->hdr.bmiHeader.biWidth == width);
    g_assert (vih->hdr.bmiHeader.biHeight == height);
  } else {
    return FALSE;
  }

  return TRUE;
}

static GstStructure *
ks_video_append_var_video_fields (GstStructure * structure)
{
  if (structure) {
    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  }

  return structure;
}

GstCaps *
ks_video_get_all_caps (void)
{
  static GstCaps *caps = NULL;

  if (caps == NULL) {
    GstStructure *structure;
    caps = gst_caps_new_empty ();

    /* from Windows SDK 6.0 uuids.h */
    /* RGB formats */
    structure =
        ks_video_append_var_video_fields (ks_video_format_to_structure
        (MEDIASUBTYPE_RGB555, FORMAT_VideoInfo));
    gst_caps_append_structure (caps, structure);

    structure =
        ks_video_append_var_video_fields (ks_video_format_to_structure
        (MEDIASUBTYPE_RGB565, FORMAT_VideoInfo));
    gst_caps_append_structure (caps, structure);

    structure =
        ks_video_append_var_video_fields (ks_video_format_to_structure
        (MEDIASUBTYPE_RGB24, FORMAT_VideoInfo));
    gst_caps_append_structure (caps, structure);

    structure =
        ks_video_append_var_video_fields (ks_video_format_to_structure
        (MEDIASUBTYPE_RGB32, FORMAT_VideoInfo));
    gst_caps_append_structure (caps, structure);

    /* YUV formats */
    structure =
        ks_video_append_var_video_fields (gst_structure_new ("video/x-raw-yuv",
            NULL));
    gst_caps_append_structure (caps, structure);

    /* Other formats */
    structure =
        ks_video_append_var_video_fields (ks_video_format_to_structure
        (MEDIASUBTYPE_MJPG, FORMAT_VideoInfo));
    gst_caps_append_structure (caps, structure);

    structure =
        ks_video_append_var_video_fields (ks_video_format_to_structure
        (MEDIASUBTYPE_dvsd, FORMAT_VideoInfo));
    gst_caps_append_structure (caps, structure);

    structure =                 /* no variable video fields (width, height, framerate) */
        ks_video_format_to_structure (MEDIASUBTYPE_dvsd, FORMAT_DvInfo);
    gst_caps_append_structure (caps, structure);
  }

  return caps;
}
