/*
 * GStreamer
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstccconverter.h"

GST_DEBUG_CATEGORY_STATIC (gst_cc_converter_debug);
#define GST_CAT_DEFAULT gst_cc_converter_debug

/* Ordered by the amount of information they can contain */
#define CC_CAPS \
        "closedcaption/x-cea-708,format=(string) cdp; " \
        "closedcaption/x-cea-708,format=(string) cc_data; " \
        "closedcaption/x-cea-608,format=(string) s334-1a; " \
        "closedcaption/x-cea-608,format=(string) raw"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CC_CAPS));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CC_CAPS));

G_DEFINE_TYPE (GstCCConverter, gst_cc_converter, GST_TYPE_BASE_TRANSFORM);
#define parent_class gst_cc_converter_parent_class

static gboolean
gst_cc_converter_transform_size (GstBaseTransform * base,
    GstPadDirection direction,
    GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  /* We can't really convert from an output size to an input size */
  if (direction != GST_PAD_SINK)
    return FALSE;

  /* Assume worst-case here and over-allocate, and in ::transform() we then
   * downsize the buffer as needed. The worst-case is one CDP packet, which
   * can be up to 256 bytes large */

  *othersize = 256;

  return TRUE;
}

static GstCaps *
gst_cc_converter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  static GstStaticCaps non_cdp_caps =
      GST_STATIC_CAPS ("closedcaption/x-cea-708, format=(string)cc_data; "
      "closedcaption/x-cea-608,format=(string) s334-1a; "
      "closedcaption/x-cea-608,format=(string) raw");
  static GstStaticCaps cdp_caps =
      GST_STATIC_CAPS ("closedcaption/x-cea-708, format=(string)cdp");
  static GstStaticCaps cdp_caps_framerate =
      GST_STATIC_CAPS ("closedcaption/x-cea-708, format=(string)cdp, "
      "framerate=(fraction){60/1, 60000/1001, 50/1, 30/1, 30000/1001, 25/1, 24/1, 24000/1001}");

  GstCCConverter *self = GST_CCCONVERTER (base);
  guint i, n;
  GstCaps *res, *templ;

  templ = gst_pad_get_pad_template_caps (base->srcpad);

  res = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    const GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_name (s, "closedcaption/x-cea-608")) {
      const GValue *framerate;

      framerate = gst_structure_get_value (s, "framerate");

      if (direction == GST_PAD_SRC) {
        /* SRC direction: We produce upstream caps
         *
         * Downstream wanted CEA608 caps. If it had a framerate, we
         * also need upstream to provide exactly that same framerate
         * and otherwise we don't care.
         *
         * We can convert everything to CEA608.
         */
        if (framerate) {
          GstCaps *tmp;

          tmp =
              gst_caps_merge (gst_static_caps_get (&cdp_caps),
              gst_static_caps_get (&non_cdp_caps));
          tmp = gst_caps_make_writable (tmp);
          gst_caps_set_value (tmp, "framerate", framerate);
          res = gst_caps_merge (res, tmp);
        } else {
          res = gst_caps_merge (res, gst_static_caps_get (&cdp_caps));
          res = gst_caps_merge (res, gst_static_caps_get (&non_cdp_caps));
        }
      } else {
        /* SINK: We produce downstream caps
         *
         * Upstream provided CEA608 caps. We can convert that to CDP if
         * also a CDP compatible framerate was provided, and we can convert
         * it to anything else regardless.
         *
         * If upstream provided a framerate we can pass that through, possibly
         * filtered for the CDP case.
         */
        if (framerate) {
          GstCaps *tmp;
          GstStructure *t, *u;

          /* Create caps that contain the intersection of all framerates with
           * the CDP allowed framerates */
          tmp =
              gst_caps_make_writable (gst_static_caps_get
              (&cdp_caps_framerate));
          t = gst_caps_get_structure (tmp, 0);
          gst_structure_set_name (t, "closedcaption/x-cea-608");
          gst_structure_remove_field (t, "format");
          u = gst_structure_intersect (s, t);
          gst_caps_unref (tmp);

          if (u) {
            const GValue *cdp_framerate;

            /* There's an intersection between the framerates so we can convert
             * into CDP with exactly those framerates */
            cdp_framerate = gst_structure_get_value (u, "framerate");
            tmp = gst_caps_make_writable (gst_static_caps_get (&cdp_caps));
            gst_caps_set_value (tmp, "framerate", cdp_framerate);
            gst_structure_free (u);

            res = gst_caps_merge (res, tmp);
          }

          /* And we can convert to everything else with the given framerate */
          tmp = gst_caps_make_writable (gst_static_caps_get (&non_cdp_caps));
          gst_caps_set_value (tmp, "framerate", framerate);
          res = gst_caps_merge (res, tmp);
        } else {
          res = gst_caps_merge (res, gst_static_caps_get (&non_cdp_caps));
        }
      }
    } else if (gst_structure_has_name (s, "closedcaption/x-cea-708")) {
      const GValue *framerate;

      framerate = gst_structure_get_value (s, "framerate");

      if (direction == GST_PAD_SRC) {
        /* SRC direction: We produce upstream caps
         *
         * Downstream wanted CEA708 caps. If downstream wants *only* CDP we
         * either need CDP from upstream, or anything else with a CDP
         * framerate.
         * If downstream also wants non-CDP we can accept anything.
         *
         * We pass through any framerate as-is, except for filtering
         * for CDP framerates if downstream wants only CDP.
         */

        if (g_strcmp0 (gst_structure_get_string (s, "format"), "cdp") == 0) {
          /* Downstream wants only CDP */

          /* We need CDP from upstream in that case */
          if (framerate) {
            GstCaps *tmp;

            tmp = gst_caps_make_writable (gst_static_caps_get (&cdp_caps));
            gst_caps_set_value (tmp, "framerate", framerate);
            res = gst_caps_merge (res, tmp);
          } else {
            res = gst_caps_merge (res, gst_static_caps_get (&cdp_caps));
          }

          /* Or anything else with a CDP framerate */
          if (framerate) {
            GstCaps *tmp;
            GstStructure *t, *u;

            /* Create caps that contain the intersection of all framerates with
             * the CDP allowed framerates */
            tmp =
                gst_caps_make_writable (gst_static_caps_get
                (&cdp_caps_framerate));
            t = gst_caps_get_structure (tmp, 0);
            gst_structure_set_name (t, "closedcaption/x-cea-708");
            gst_structure_remove_field (t, "format");
            u = gst_structure_intersect (s, t);
            gst_caps_unref (tmp);

            if (u) {
              const GValue *cdp_framerate;

              /* There's an intersection between the framerates so we can convert
               * into CDP with exactly those framerates from anything else */
              cdp_framerate = gst_structure_get_value (u, "framerate");

              tmp =
                  gst_caps_make_writable (gst_static_caps_get (&non_cdp_caps));
              gst_caps_set_value (tmp, "framerate", cdp_framerate);
              res = gst_caps_merge (res, tmp);
            }
          } else {
            GstCaps *tmp, *cdp_caps;
            const GValue *cdp_framerate;

            /* Get all CDP framerates, we can accept anything that has those
             * framerates */
            cdp_caps = gst_static_caps_get (&cdp_caps_framerate);
            cdp_framerate =
                gst_structure_get_value (gst_caps_get_structure (cdp_caps, 0),
                "framerate");

            tmp = gst_caps_make_writable (gst_static_caps_get (&non_cdp_caps));
            gst_caps_set_value (tmp, "framerate", cdp_framerate);
            gst_caps_unref (cdp_caps);

            res = gst_caps_merge (res, tmp);
          }
        } else {
          /* Downstream wants not only CDP, we can do everything */

          if (framerate) {
            GstCaps *tmp;

            tmp =
                gst_caps_merge (gst_static_caps_get (&cdp_caps),
                gst_static_caps_get (&non_cdp_caps));
            tmp = gst_caps_make_writable (tmp);
            gst_caps_set_value (tmp, "framerate", framerate);
            res = gst_caps_merge (res, tmp);
          } else {
            res = gst_caps_merge (res, gst_static_caps_get (&cdp_caps));
            res = gst_caps_merge (res, gst_static_caps_get (&non_cdp_caps));
          }
        }
      } else {
        GstCaps *tmp;

        /* SINK: We produce downstream caps
         *
         * Upstream provided CEA708 caps. If upstream provided CDP we can
         * output CDP, no matter what (-> passthrough). If upstream did not
         * provide CDP, we can output CDP only if the framerate fits.
         * We can always produce everything else apart from CDP.
         *
         * If upstream provided a framerate we pass that through for non-CDP
         * output, and pass it through filtered for CDP output.
         */

        if (gst_structure_can_intersect (s,
                gst_caps_get_structure (gst_static_caps_get (&cdp_caps), 0))) {
          /* Upstream provided CDP caps, we can do everything independent of
           * framerate */
          if (framerate) {
            tmp = gst_caps_make_writable (gst_static_caps_get (&cdp_caps));
            gst_caps_set_value (tmp, "framerate", framerate);
            res = gst_caps_merge (res, tmp);
          } else {
            res = gst_caps_merge (res, gst_static_caps_get (&cdp_caps));
          }
        } else if (framerate) {
          GstStructure *t, *u;

          /* Upstream did not provide CDP. We can only do CDP if upstream
           * happened to have a CDP framerate */

          /* Create caps that contain the intersection of all framerates with
           * the CDP allowed framerates */
          tmp =
              gst_caps_make_writable (gst_static_caps_get
              (&cdp_caps_framerate));
          t = gst_caps_get_structure (tmp, 0);
          gst_structure_set_name (t, "closedcaption/x-cea-708");
          gst_structure_remove_field (t, "format");
          u = gst_structure_intersect (s, t);
          gst_caps_unref (tmp);

          if (u) {
            const GValue *cdp_framerate;

            /* There's an intersection between the framerates so we can convert
             * into CDP with exactly those framerates */
            cdp_framerate = gst_structure_get_value (u, "framerate");
            tmp = gst_caps_make_writable (gst_static_caps_get (&cdp_caps));
            gst_caps_set_value (tmp, "framerate", cdp_framerate);
            gst_structure_free (u);

            res = gst_caps_merge (res, tmp);
          }
        }

        /* We can always convert CEA708 to all non-CDP formats */
        if (framerate) {
          tmp = gst_caps_make_writable (gst_static_caps_get (&non_cdp_caps));
          gst_caps_set_value (tmp, "framerate", framerate);
          res = gst_caps_merge (res, tmp);
        } else {
          res = gst_caps_merge (res, gst_static_caps_get (&non_cdp_caps));
        }
      }
    } else {
      g_assert_not_reached ();
    }
  }

  /* We can convert anything into anything but it might involve loss of
   * information so always filter according to the order in our template caps
   * in the end */
  if (filter) {
    GstCaps *tmp;
    filter = gst_caps_intersect_full (templ, filter, GST_CAPS_INTERSECT_FIRST);

    tmp = gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    gst_caps_unref (filter);
    res = tmp;
  }

  gst_caps_unref (templ);

  GST_DEBUG_OBJECT (self,
      "Transformed in direction %s caps %" GST_PTR_FORMAT " to %"
      GST_PTR_FORMAT, direction == GST_PAD_SRC ? "src" : "sink", caps, res);

  return res;
}

static GstCaps *
gst_cc_converter_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstCCConverter *self = GST_CCCONVERTER (base);
  const GstStructure *s;
  GstStructure *t;
  const GValue *framerate;
  GstCaps *intersection, *templ;

  /* Prefer passthrough if we can */
  if (gst_caps_is_subset (incaps, outcaps)) {
    gst_caps_unref (outcaps);
    return GST_BASE_TRANSFORM_CLASS (parent_class)->fixate_caps (base,
        direction, incaps, gst_caps_ref (incaps));
  }

  /* Otherwise prefer caps in the order of our template caps */
  templ = gst_pad_get_pad_template_caps (base->srcpad);
  intersection =
      gst_caps_intersect_full (templ, outcaps, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (outcaps);
  outcaps = intersection;

  outcaps =
      GST_BASE_TRANSFORM_CLASS (parent_class)->fixate_caps (base, direction,
      incaps, outcaps);

  if (direction == GST_PAD_SRC)
    return outcaps;

  /* if we generate caps for the source pad, pass through any framerate
   * upstream might've given us and remove any framerate that might've
   * been added by basetransform due to intersecting with downstream */
  s = gst_caps_get_structure (incaps, 0);
  framerate = gst_structure_get_value (s, "framerate");
  outcaps = gst_caps_make_writable (outcaps);
  t = gst_caps_get_structure (outcaps, 0);
  if (framerate) {
    gst_structure_set_value (t, "framerate", framerate);
  } else {
    gst_structure_remove_field (t, "framerate");
  }

  GST_DEBUG_OBJECT (self,
      "Fixated caps %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, incaps, outcaps);

  return outcaps;
}

static gboolean
gst_cc_converter_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstCCConverter *self = GST_CCCONVERTER (base);
  const GstStructure *s;
  gboolean passthrough;

  self->input_caption_type = gst_video_caption_type_from_caps (incaps);
  self->output_caption_type = gst_video_caption_type_from_caps (outcaps);

  if (self->input_caption_type == GST_VIDEO_CAPTION_TYPE_UNKNOWN ||
      self->output_caption_type == GST_VIDEO_CAPTION_TYPE_UNKNOWN)
    goto invalid_caps;

  s = gst_caps_get_structure (incaps, 0);
  if (!gst_structure_get_fraction (s, "framerate", &self->fps_n, &self->fps_d))
    self->fps_n = self->fps_d = 0;

  /* Caps can be different but we can passthrough as long as they can
   * intersect, i.e. have same caps name and format */
  passthrough = gst_caps_can_intersect (incaps, outcaps);
  gst_base_transform_set_passthrough (base, passthrough);

  GST_DEBUG_OBJECT (self,
      "Got caps %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT " (passthrough %d)",
      incaps, outcaps, passthrough);

  return TRUE;

invalid_caps:
  {
    GST_ERROR_OBJECT (self,
        "Invalid caps: in %" GST_PTR_FORMAT " out: %" GST_PTR_FORMAT, incaps,
        outcaps);
    return FALSE;
  }
}

/* Converts raw CEA708 cc_data and an optional timecode into CDP */
static guint
convert_cea708_cc_data_cea708_cdp_internal (GstCCConverter * self,
    const guint8 * cc_data, guint cc_data_len, guint8 * cdp, guint cdp_len,
    const GstVideoTimeCodeMeta * tc_meta)
{
  GstByteWriter bw;
  guint8 flags, checksum;
  guint i, len;
  guint cc_count;

  gst_byte_writer_init_with_data (&bw, cdp, cdp_len, FALSE);
  gst_byte_writer_put_uint16_be_unchecked (&bw, 0x9669);
  /* Write a length of 0 for now */
  gst_byte_writer_put_uint8_unchecked (&bw, 0);
  if (self->fps_n == 24000 && self->fps_d == 1001) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x1f);
    cc_count = 25;
  } else if (self->fps_n == 24 && self->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x2f);
    cc_count = 25;
  } else if (self->fps_n == 25 && self->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x3f);
    cc_count = 24;
  } else if (self->fps_n == 30000 && self->fps_d == 1001) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x4f);
    cc_count = 20;
  } else if (self->fps_n == 30 && self->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x5f);
    cc_count = 20;
  } else if (self->fps_n == 50 && self->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x6f);
    cc_count = 12;
  } else if (self->fps_n == 60000 && self->fps_d == 1001) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x7f);
    cc_count = 10;
  } else if (self->fps_n == 60 && self->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x8f);
    cc_count = 10;
  } else {
    g_assert_not_reached ();
  }

  if (cc_data_len / 3 > cc_count) {
    GST_ERROR_OBJECT (self, "Too many cc_data triplet for framerate: %u > %u",
        cc_data_len / 3, cc_count);
    return -1;
  }

  /* ccdata_present | caption_service_active */
  flags = 0x42;

  /* time_code_present */
  if (tc_meta)
    flags |= 0x80;

  /* reserved */
  flags |= 0x01;

  gst_byte_writer_put_uint8_unchecked (&bw, flags);

  gst_byte_writer_put_uint16_be_unchecked (&bw, self->cdp_hdr_sequence_cntr);

  if (tc_meta) {
    const GstVideoTimeCode *tc = &tc_meta->tc;

    gst_byte_writer_put_uint8_unchecked (&bw, 0x71);
    gst_byte_writer_put_uint8_unchecked (&bw, 0xc0 |
        (((tc->hours % 10) & 0x3) << 4) |
        ((tc->hours - (tc->hours % 10)) & 0xf));

    gst_byte_writer_put_uint8_unchecked (&bw, 0x80 |
        (((tc->minutes % 10) & 0x7) << 4) |
        ((tc->minutes - (tc->minutes % 10)) & 0xf));

    gst_byte_writer_put_uint8_unchecked (&bw,
        (tc->field_count <
            2 ? 0x00 : 0x80) | (((tc->seconds %
                    10) & 0x7) << 4) | ((tc->seconds -
                (tc->seconds % 10)) & 0xf));

    gst_byte_writer_put_uint8_unchecked (&bw,
        ((tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME) ? 0x80 :
            0x00) | (((tc->frames % 10) & 0x3) << 4) | ((tc->frames -
                (tc->frames % 10)) & 0xf));
  }

  gst_byte_writer_put_uint8_unchecked (&bw, 0x72);
  gst_byte_writer_put_uint8_unchecked (&bw, 0xe0 | cc_count);
  gst_byte_writer_put_data_unchecked (&bw, cc_data, cc_data_len);
  if (cc_count > cc_data_len / 3) {
    gst_byte_writer_fill (&bw, 0, 3 * cc_count - cc_data_len);
  }

  gst_byte_writer_put_uint8_unchecked (&bw, 0x74);
  gst_byte_writer_put_uint16_be_unchecked (&bw, self->cdp_hdr_sequence_cntr);
  self->cdp_hdr_sequence_cntr++;
  /* We calculate the checksum afterwards */
  gst_byte_writer_put_uint8_unchecked (&bw, 0);

  len = gst_byte_writer_get_pos (&bw);
  gst_byte_writer_set_pos (&bw, 2);
  gst_byte_writer_put_uint8_unchecked (&bw, len);

  checksum = 0;
  for (i = 0; i < len; i++) {
    checksum += cdp[i];
  }
  checksum &= 0xff;
  checksum = 256 - checksum;
  cdp[len - 1] = checksum;

  return len;
}

/* Converts CDP into raw CEA708 cc_data */
static guint
convert_cea708_cdp_cea708_cc_data_internal (GstCCConverter * self,
    const guint8 * cdp, guint cdp_len, guint8 cc_data[256],
    GstVideoTimeCode * tc)
{
  GstByteReader br;
  guint16 u16;
  guint8 u8;
  guint8 flags;
  gint fps_n, fps_d;
  guint len;

  memset (tc, 0, sizeof (*tc));

  /* Header + footer length */
  if (cdp_len < 11)
    return 0;

  gst_byte_reader_init (&br, cdp, cdp_len);
  u16 = gst_byte_reader_get_uint16_be_unchecked (&br);
  if (u16 != 0x9669)
    return 0;

  u8 = gst_byte_reader_get_uint8_unchecked (&br);
  if (u8 != cdp_len)
    return 0;

  u8 = gst_byte_reader_get_uint8_unchecked (&br);
  switch (u8) {
    case 0x1f:
      fps_n = 24000;
      fps_d = 1001;
      break;
    case 0x2f:
      fps_n = 24;
      fps_d = 1;
      break;
    case 0x3f:
      fps_n = 25;
      fps_d = 1;
      break;
    case 0x4f:
      fps_n = 30000;
      fps_d = 1001;
      break;
    case 0x5f:
      fps_n = 30;
      fps_d = 1;
      break;
    case 0x6f:
      fps_n = 50;
      fps_d = 1;
      break;
    case 0x7f:
      fps_n = 60000;
      fps_d = 1001;
      break;
    case 0x8f:
      fps_n = 60;
      fps_d = 1;
      break;
    default:
      return 0;
  }

  flags = gst_byte_reader_get_uint8_unchecked (&br);
  /* No cc_data? */
  if ((flags & 0x40) == 0)
    return 0;

  /* cdp_hdr_sequence_cntr */
  gst_byte_reader_skip_unchecked (&br, 2);

  /* time_code_present */
  if (flags & 0x80) {
    guint8 hours, minutes, seconds, frames, fields;
    gboolean drop_frame;

    if (gst_byte_reader_get_remaining (&br) < 5)
      return 0;
    if (gst_byte_reader_get_uint8_unchecked (&br) != 0x71)
      return 0;

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if ((u8 & 0xc) != 0xc)
      return 0;

    hours = ((u8 >> 4) & 0x3) * 10 + (u8 & 0xf);

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if ((u8 & 0x80) != 0x80)
      return 0;
    minutes = ((u8 >> 4) & 0x7) * 10 + (u8 & 0xf);

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 & 0x80)
      fields = 2;
    else
      fields = 1;
    seconds = ((u8 >> 4) & 0x7) * 10 + (u8 & 0xf);

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 & 0x40)
      return 0;

    drop_frame = ! !(u8 & 0x80);
    frames = ((u8 >> 4) & 0x3) * 10 + (u8 & 0xf);

    gst_video_time_code_init (tc, fps_n, fps_d, NULL,
        drop_frame ? GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME :
        GST_VIDEO_TIME_CODE_FLAGS_NONE, hours, minutes, seconds, frames,
        fields);
  }

  /* ccdata_present */
  if (flags & 0x40) {
    guint8 cc_count;

    if (gst_byte_reader_get_remaining (&br) < 2)
      return 0;
    if (gst_byte_reader_get_uint8_unchecked (&br) != 0x72)
      return 0;

    cc_count = gst_byte_reader_get_uint8_unchecked (&br);
    if ((cc_count & 0xe0) != 0xe0)
      return 0;
    cc_count &= 0x1f;

    len = 3 * cc_count;
    if (gst_byte_reader_get_remaining (&br) < len)
      return 0;

    memcpy (cc_data, gst_byte_reader_get_data_unchecked (&br, len), len);
  }

  /* skip everything else we don't care about */
  return len;
}


static GstFlowReturn
convert_cea608_raw_cea608_s334_1a (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n;

  n = gst_buffer_get_size (inbuf);
  if (n & 1) {
    GST_ERROR_OBJECT (self, "Invalid raw CEA608 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 2;

  if (n > 3) {
    GST_ERROR_OBJECT (self, "Too many CEA608 pairs %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_set_size (outbuf, 3 * n);

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  /* We have to assume that each value is from the first field and
   * don't know from which line offset it originally is */
  for (i = 0; i < n; i++) {
    out.data[i * 3] = 0x80;
    out.data[i * 3 + 1] = in.data[i * 2];
    out.data[i * 3 + 2] = in.data[i * 2 + 1];
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea608_raw_cea708_cc_data (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n;

  n = gst_buffer_get_size (inbuf);
  if (n & 1) {
    GST_ERROR_OBJECT (self, "Invalid raw CEA608 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 2;

  if (n > 3) {
    GST_ERROR_OBJECT (self, "Too many CEA608 pairs %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_set_size (outbuf, 3 * n);

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  /* We have to assume that each value is from the first field and
   * don't know from which line offset it originally is */
  for (i = 0; i < n; i++) {
    out.data[i * 3] = 0xfc;
    out.data[i * 3 + 1] = in.data[i * 2];
    out.data[i * 3 + 2] = in.data[i * 2 + 1];
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea608_raw_cea708_cdp (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n, len;
  guint8 cc_data[256];

  n = gst_buffer_get_size (inbuf);
  if (n & 1) {
    GST_ERROR_OBJECT (self, "Invalid raw CEA608 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 2;

  if (n > 3) {
    GST_ERROR_OBJECT (self, "Too many CEA608 pairs %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  for (i = 0; i < n; i++) {
    cc_data[i * 3] = 0xfc;
    cc_data[i * 3 + 1] = in.data[i * 2];
    cc_data[i * 3 + 2] = in.data[i * 2 + 1];
  }

  len =
      convert_cea708_cc_data_cea708_cdp_internal (self, cc_data, n * 3,
      out.data, out.size, gst_buffer_get_video_time_code_meta (inbuf));

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  if (len == -1)
    return GST_FLOW_ERROR;

  gst_buffer_set_size (outbuf, len);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea608_s334_1a_cea608_raw (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n;
  guint cea608 = 0;

  n = gst_buffer_get_size (inbuf);
  if (n % 3 != 0) {
    GST_ERROR_OBJECT (self, "Invalid S334-1A CEA608 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 3;

  if (n > 3) {
    GST_ERROR_OBJECT (self, "Too many S334-1A CEA608 triplets %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  for (i = 0; i < n; i++) {
    if (in.data[i * 3] & 0x80) {
      out.data[i * 2] = in.data[i * 3 + 1];
      out.data[i * 2 + 1] = in.data[i * 3 + 2];
      cea608++;
    }
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  gst_buffer_set_size (outbuf, 2 * cea608);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea608_s334_1a_cea708_cc_data (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n;

  n = gst_buffer_get_size (inbuf);
  if (n % 3 != 0) {
    GST_ERROR_OBJECT (self, "Invalid S334-1A CEA608 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 3;

  if (n > 3) {
    GST_ERROR_OBJECT (self, "Too many S334-1A CEA608 triplets %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_set_size (outbuf, 3 * n);

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  for (i = 0; i < n; i++) {
    out.data[i * 3] = (in.data[i * 3] & 0x80) ? 0xfc : 0xfd;
    out.data[i * 3 + 1] = in.data[i * 3 + 1];
    out.data[i * 3 + 2] = in.data[i * 3 + 2];
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea608_s334_1a_cea708_cdp (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n, len;
  guint8 cc_data[256];

  n = gst_buffer_get_size (inbuf);
  if (n % 3 != 0) {
    GST_ERROR_OBJECT (self, "Invalid S334-1A CEA608 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 3;

  if (n > 3) {
    GST_ERROR_OBJECT (self, "Too many S334-1A CEA608 triplets %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  for (i = 0; i < n; i++) {
    cc_data[i * 3] = (in.data[i * 3] & 0x80) ? 0xfc : 0xfd;
    cc_data[i * 3 + 1] = in.data[i * 3 + 1];
    cc_data[i * 3 + 2] = in.data[i * 3 + 2];
  }

  len =
      convert_cea708_cc_data_cea708_cdp_internal (self, cc_data, n * 3,
      out.data, out.size, gst_buffer_get_video_time_code_meta (inbuf));

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  if (len == -1)
    return GST_FLOW_ERROR;

  gst_buffer_set_size (outbuf, len);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea708_cc_data_cea608_raw (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n;
  guint cea608 = 0;

  n = gst_buffer_get_size (inbuf);
  if (n % 3 != 0) {
    GST_ERROR_OBJECT (self, "Invalid raw CEA708 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 3;

  if (n > 25) {
    GST_ERROR_OBJECT (self, "Too many CEA708 triplets %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  for (i = 0; i < n; i++) {
    /* We can only really copy the first field here as there can't be any
     * signalling in raw CEA608 and we must not mix the streams of different
     * fields
     */
    if (in.data[i * 3] == 0xfc) {
      out.data[cea608 * 2] = in.data[i * 3 + 1];
      out.data[cea608 * 2 + 1] = in.data[i * 3 + 2];
      cea608++;
    }
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  gst_buffer_set_size (outbuf, 2 * cea608);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea708_cc_data_cea608_s334_1a (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i, n;
  guint cea608 = 0;

  n = gst_buffer_get_size (inbuf);
  if (n % 3 != 0) {
    GST_ERROR_OBJECT (self, "Invalid raw CEA708 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 3;

  if (n > 25) {
    GST_ERROR_OBJECT (self, "Too many CEA708 triplets %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  for (i = 0; i < n; i++) {
    if (in.data[i * 3] == 0xfc || in.data[i * 3] == 0xfd) {
      /* We have to assume a line offset of 0 */
      out.data[cea608 * 3] = in.data[i * 3] == 0xfc ? 0x80 : 0x00;
      out.data[cea608 * 3 + 1] = in.data[i * 3 + 1];
      out.data[cea608 * 3 + 2] = in.data[i * 3 + 2];
      cea608++;
    }
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  gst_buffer_set_size (outbuf, 3 * cea608);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea708_cc_data_cea708_cdp (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint n;
  guint len;

  n = gst_buffer_get_size (inbuf);
  if (n % 3 != 0) {
    GST_ERROR_OBJECT (self, "Invalid raw CEA708 buffer size");
    return GST_FLOW_ERROR;
  }

  n /= 3;

  if (n > 25) {
    GST_ERROR_OBJECT (self, "Too many CEA708 triplets %u", n);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  len =
      convert_cea708_cc_data_cea708_cdp_internal (self, in.data, in.size,
      out.data, out.size, gst_buffer_get_video_time_code_meta (inbuf));

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  if (len == -1)
    return GST_FLOW_ERROR;

  gst_buffer_set_size (outbuf, len);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea708_cdp_cea608_raw (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i;
  GstVideoTimeCode tc;
  guint8 cc_data[256];
  guint len, cea608 = 0;

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  len =
      convert_cea708_cdp_cea708_cc_data_internal (self, in.data, in.size,
      cc_data, &tc);
  len /= 3;

  if (len > 25) {
    GST_ERROR_OBJECT (self, "Too many cc_data triples in CDP packet %u", len);
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < len; i++) {
    /* We can only really copy the first field here as there can't be any
     * signalling in raw CEA608 and we must not mix the streams of different
     * fields
     */
    if (cc_data[i * 3] == 0xfc) {
      out.data[cea608 * 2] = cc_data[i * 3 + 1];
      out.data[cea608 * 2 + 1] = cc_data[i * 3 + 2];
      cea608++;
    }
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  gst_buffer_set_size (outbuf, 2 * cea608);

  if (tc.config.fps_n != 0 && !gst_buffer_get_video_time_code_meta (inbuf))
    gst_buffer_add_video_time_code_meta (outbuf, &tc);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea708_cdp_cea608_s334_1a (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  guint i;
  GstVideoTimeCode tc;
  guint8 cc_data[256];
  guint len, cea608 = 0;

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  len =
      convert_cea708_cdp_cea708_cc_data_internal (self, in.data, in.size,
      cc_data, &tc);
  len /= 3;

  if (len > 25) {
    GST_ERROR_OBJECT (self, "Too many cc_data triples in CDP packet %u", len);
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < len; i++) {
    if (cc_data[i * 3] == 0xfc || cc_data[i * 3] == 0xfd) {
      /* We have to assume a line offset of 0 */
      out.data[cea608 * 3] = cc_data[i * 3] == 0xfc ? 0x80 : 0x00;
      out.data[cea608 * 3 + 1] = cc_data[i * 3 + 1];
      out.data[cea608 * 3 + 2] = cc_data[i * 3 + 2];
      cea608++;
    }
  }

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  gst_buffer_set_size (outbuf, 3 * cea608);

  if (tc.config.fps_n != 0 && !gst_buffer_get_video_time_code_meta (inbuf))
    gst_buffer_add_video_time_code_meta (outbuf, &tc);

  return GST_FLOW_OK;
}

static GstFlowReturn
convert_cea708_cdp_cea708_cc_data (GstCCConverter * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo in, out;
  GstVideoTimeCode tc;
  guint len;

  gst_buffer_map (inbuf, &in, GST_MAP_READ);
  gst_buffer_map (outbuf, &out, GST_MAP_WRITE);

  len =
      convert_cea708_cdp_cea708_cc_data_internal (self, in.data, in.size,
      out.data, &tc);

  gst_buffer_unmap (inbuf, &in);
  gst_buffer_unmap (outbuf, &out);

  if (len / 3 > 25) {
    GST_ERROR_OBJECT (self, "Too many cc_data triples in CDP packet %u",
        len / 3);
    return GST_FLOW_ERROR;
  }

  gst_buffer_set_size (outbuf, len);

  if (tc.config.fps_n != 0 && !gst_buffer_get_video_time_code_meta (inbuf))
    gst_buffer_add_video_time_code_meta (outbuf, &tc);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_cc_converter_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstCCConverter *self = GST_CCCONVERTER (base);
  GstVideoTimeCodeMeta *tc_meta = gst_buffer_get_video_time_code_meta (inbuf);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (base, "Converting %" GST_PTR_FORMAT " from %u to %u", inbuf,
      self->input_caption_type, self->output_caption_type);

  switch (self->input_caption_type) {
    case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:

      switch (self->output_caption_type) {
        case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:
          ret = convert_cea608_raw_cea608_s334_1a (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
          ret = convert_cea608_raw_cea708_cc_data (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
          ret = convert_cea608_raw_cea708_cdp (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
        default:
          g_assert_not_reached ();
          break;
      }

      break;
    case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:

      switch (self->output_caption_type) {
        case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
          ret = convert_cea608_s334_1a_cea608_raw (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
          ret = convert_cea608_s334_1a_cea708_cc_data (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
          ret = convert_cea608_s334_1a_cea708_cdp (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:
        default:
          g_assert_not_reached ();
          break;
      }

      break;
    case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:

      switch (self->output_caption_type) {
        case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
          ret = convert_cea708_cc_data_cea608_raw (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:
          ret = convert_cea708_cc_data_cea608_s334_1a (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
          ret = convert_cea708_cc_data_cea708_cdp (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
        default:
          g_assert_not_reached ();
          break;
      }

      break;
    case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:

      switch (self->output_caption_type) {
        case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
          ret = convert_cea708_cdp_cea608_raw (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:
          ret = convert_cea708_cdp_cea608_s334_1a (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
          ret = convert_cea708_cdp_cea708_cc_data (self, inbuf, outbuf);
          break;
        case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
        default:
          g_assert_not_reached ();
          break;
      }

      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (ret != GST_FLOW_OK)
    return ret;

  if (tc_meta)
    gst_buffer_add_video_time_code_meta (outbuf, &tc_meta->tc);

  GST_DEBUG_OBJECT (self, "Converted to %" GST_PTR_FORMAT, outbuf);

  return gst_buffer_get_size (outbuf) >
      0 ? GST_FLOW_OK : GST_BASE_TRANSFORM_FLOW_DROPPED;
}

static gboolean
gst_cc_converter_start (GstBaseTransform * base)
{
  GstCCConverter *self = GST_CCCONVERTER (base);

  /* Resetting this is not really needed but makes debugging easier */
  self->cdp_hdr_sequence_cntr = 0;

  return TRUE;
}

static void
gst_cc_converter_class_init (GstCCConverterClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseTransformClass *basetransform_class;

  gstelement_class = (GstElementClass *) klass;
  basetransform_class = (GstBaseTransformClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Closed Caption Converter",
      "Filter/ClosedCaption",
      "Converts Closed Captions between different formats",
      "Sebastian Dröge <sebastian@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_cc_converter_start);
  basetransform_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_cc_converter_transform_size);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cc_converter_transform_caps);
  basetransform_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_cc_converter_fixate_caps);
  basetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_cc_converter_set_caps);
  basetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_cc_converter_transform);
  basetransform_class->passthrough_on_same_caps = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_cc_converter_debug, "ccconverter",
      0, "Closed Caption converter");
}

static void
gst_cc_converter_init (GstCCConverter * self)
{
}
