/* GStreamer H.263 Parser
 * Copyright (C) <2010> Arun Raghavan <arun.raghavan@collabora.co.uk>
 * Copyright (C) <2010> Edward Hervey <edward.hervey@collabora.co.uk>
 * Copyright (C) <2010> Collabora Multimedia
 * Copyright (C) <2010> Nokia Corporation
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
#  include "config.h"
#endif

#include <gst/base/gstbitreader.h>
#include "gsth263parse.h"

/* H263 Optional Features */
typedef enum
{
  /* Optional Unrestricted Motion Vector (UMV) mode (see Annex D) */
  H263_OPTION_UMV_MODE = 1 << 0,
  /* Optional Syntax-based Arithmetic Coding (SAC) mode (see Annex E) */
  H263_OPTION_SAC_MODE = 1 << 1,
  /* Optional Advanced Prediction mode (AP) (see Annex F) */
  H263_OPTION_AP_MODE = 1 << 2,
  /* Optional PB-frames mode (see Annex G) */
  H263_OPTION_PB_MODE = 1 << 3,
  /* Optional Advanced INTRA Coding (AIC) mode (see Annex I) */
  H263_OPTION_AIC_MODE = 1 << 4,
  /* Optional Deblocking Filter (DF) mode (see Annex J) */
  H263_OPTION_DF_MODE = 1 << 5,
  /* Optional Slice Structured (SS) mode (see Annex K) */
  H263_OPTION_SS_MODE = 1 << 6,
  /* Optional Reference Picture Selection (RPS) mode (see Annex N) */
  H263_OPTION_RPS_MODE = 1 << 7,
  /* Optional Independent Segment Decoding (ISD) mode (see Annex R) */
  H263_OPTION_ISD_MODE = 1 << 8,
  /* Optional Alternative INTER VLC (AIV) mode (see Annex S) */
  H263_OPTION_AIV_MODE = 1 << 9,
  /* Optional Modified Quantization (MQ) mode (see Annex T) */
  H263_OPTION_MQ_MODE = 1 << 10,
  /* Optional Reference Picture Resampling (RPR) mode (see Annex P) */
  H263_OPTION_RPR_MODE = 1 << 11,
  /* Optional Reduced-Resolution Update (RRU) mode (see Annex Q) */
  H263_OPTION_RRU_MODE = 1 << 12,
  /* Optional Enhanced Reference Picture Selection (ERPS) mode (see Annex U) */
  H263_OPTION_ERPS_MODE = 1 << 13,
  /* Optional Data Partitioned Slices (DPS) mode (see Annex V) */
  H263_OPTION_DPS_MODE = 1 << 14
} H263OptionalFeatures;

/* H263 Picture Types */
typedef enum
{
  PICTURE_I = 0,                /* I-picture (INTRA) Baseline */
  PICTURE_P,                    /* P-picture (INTER) Baseline */
  PICTURE_IMPROVED_PB,          /* Improved PB-frame (Annex M) */
  PICTURE_B,                    /* B-picture (Annex O) */
  PICTURE_EI,                   /* EI-picture (Annex O) */
  PICTURE_EP,                   /* EP-picture (Annex O) */
  PICTURE_RESERVED1,
  PICTURE_RESERVED2,
  PICTURE_PB                    /* PB-frame (See Annex G) */
} H263PictureType;

/* H263 Picture Format */
typedef enum
{
  PICTURE_FMT_FORBIDDEN_0 = 0,
  PICTURE_FMT_SUB_QCIF,
  PICTURE_FMT_QCIF,
  PICTURE_FMT_CIF,
  PICTURE_FMT_4CIF,
  PICTURE_FMT_16CIF,
  PICTURE_FMT_RESERVED1,
  PICTURE_FMT_EXTENDEDPTYPE
} H263PictureFormat;

typedef enum
{
  UUI_ABSENT = 0,
  UUI_IS_1,
  UUI_IS_01,
} H263UUI;

struct _H263Params
{
  guint32 temporal_ref;

  H263OptionalFeatures features;

  gboolean splitscreen;
  gboolean documentcamera;
  gboolean fullpicturefreezerelease;
  gboolean custompcfpresent;
  H263UUI uui;
  guint8 sss;

  H263PictureFormat format;

  H263PictureType type;

  guint32 width;
  guint32 height;
  guint8 parnum, pardenom;
  gint32 pcfnum, pcfdenom;
};


gboolean
gst_h263_parse_is_delta_unit (H263Params * params)
{
  return (params->type == PICTURE_I);
}

/* Reads adapter and tries to populate params. 'fast' mode can be used to
 * extract a subset of the data (for now, it quits once we have the picture
 * type. */
GstFlowReturn
gst_h263_parse_get_params (GstH263Parse * h263parse, GstBuffer * buffer,
    H263Params ** params_p, gboolean fast)
{
  static const guint8 partable[6][2] = {
    {1, 0},
    {1, 1},
    {12, 11},
    {10, 11},
    {16, 11},
    {40, 33}
  };

  static const guint16 sizetable[8][2] = {
    {0, 0},
    {128, 96},
    {176, 144},
    {352, 288},
    {704, 576},
    {1408, 1152}
  };

  static const gchar *source_format_name[] = {
    "Forbidden",
    "sub-QCIF",
    "QCIF",
    "CIF",
    "4CIF",
    "16CIF",
    "Reserved",
    "Extended PType"
  };

  H263Params *params;
  GstBitReader br;
  guint8 tr;
  guint32 psc, temp32;
  guint8 temp8, pquant;
  gboolean hasplusptype;

  *params_p = g_new0 (H263Params, 1);
  params = *params_p;

  /* FIXME: we can optimise a little by checking the value of available
   * instead of calling using the bit reader's get_bits_* functions. */
  gst_bit_reader_init_from_buffer (&br, buffer);

  /* Default PCF is CIF PCF = 30000/1001 */
  params->pcfnum = 30000;
  params->pcfdenom = 1001;

  GST_DEBUG ("NEW BUFFER");
  if (!gst_bit_reader_get_bits_uint32 (&br, &psc, 22) ||
      !gst_bit_reader_get_bits_uint8 (&br, &tr, 8) ||
      !gst_bit_reader_get_bits_uint8 (&br, &temp8, 8))
    goto more;

  /* PSC   : Picture Start Code                 22 bits
   * TR    : Temporal Reference                 8  bits
   * PTYPE : Type Information                   variable
   *  bit 1 : Always "1"
   *  bit 2 : Always "0"
   *  bit 3 : Split Screen Indicator
   *  bit 4 : Document Camera Indicator
   *  bit 6-8 : Source Format
   *            if 111 : extended PTYPE is present */

  /* 5.1.1 PSC : Picture Start Code (0x0020)    22 bits */
  /* FIXME : Scan for the PSC instead of assuming it's always present
   * and at the beginning. */
  if (G_UNLIKELY (psc != 0x0020)) {
    GST_WARNING ("Invalid PSC");
    goto beach;
  }

  /* 5.1.2 TR : Temporal Reference              8 bits */
  GST_DEBUG (" Temporal Reference : %d", tr);
  params->temporal_ref = tr;

  if ((temp8 >> 6) != 0x2) {
    GST_WARNING ("Invalid PTYPE");
    goto beach;
  }

  /* 5.1.3 PTYPE : Type Information             variable length */
  params->splitscreen = (temp8 & 0x20) == 0x20;
  params->documentcamera = (temp8 & 0x10) == 0x10;
  params->fullpicturefreezerelease = (temp8 & 0x08) == 0x08;
  params->format = temp8 & 0x07;

  hasplusptype = (temp8 & 0x07) == 0x07;

  GST_DEBUG (" Split Screen Indicator : %s",
      params->splitscreen ? "on" : "off");
  GST_DEBUG (" Document camera indicator : %s",
      params->documentcamera ? "on" : "off");
  GST_DEBUG (" Full Picture Freeze Release : %s",
      params->fullpicturefreezerelease ? "on" : "off");
  GST_DEBUG (" Source format 0x%x (%s)", params->format,
      source_format_name[params->format]);

  if (!hasplusptype) {
    guint8 ptype2;

    /* Fill in width/height based on format */
    params->width = sizetable[params->format][0];
    params->height = sizetable[params->format][1];

    /* Default PAR is 12/11 */
    params->parnum = 12;
    params->pardenom = 11;

    /* 5.1.3 : Remainder of PTYPE                5 bits */
    if (!gst_bit_reader_get_bits_uint8 (&br, &ptype2, 5))
      goto more;

    params->type = (ptype2 & 0x10) == 0x10;
    if ((ptype2 & 0x08) == 0x08)
      params->features |= H263_OPTION_UMV_MODE;
    if ((ptype2 & 0x04) == 0x04)
      params->features |= H263_OPTION_SAC_MODE;
    if ((ptype2 & 0x02) == 0x02)
      params->features |= H263_OPTION_AP_MODE;
    if ((ptype2 & 0x01) == 0x01) {
      params->features |= H263_OPTION_PB_MODE;
      params->type = PICTURE_PB;
    }

    GST_DEBUG (" Picture Coding Type : %s",
        (ptype2 & 0x10) == 0x10 ? "INTER (P-picture)" : "INTRA (I-picture)");
    GST_DEBUG (" Unrestricted Motion Vector mode (Annex D) : %s",
        (ptype2 & 0x08) == 0x08 ? "on" : "off");
    GST_DEBUG (" Syntax-basex Arithmetic Coding mode (Annex E) : %s",
        (ptype2 & 0x04) == 0x04 ? "on" : "off");
    GST_DEBUG (" Advanced Prediction mode (Annex F) : %s",
        (ptype2 & 0x02) == 0x02 ? "on" : "off");
    GST_DEBUG (" PB Frames mode (Annex G) : %s",
        (ptype2 & 0x01) == 0x01 ? "on" : "off");

    if (fast)
      goto done;
  }

  if (hasplusptype) {
    guint8 ufep;
    guint8 cpm;
    guint32 opptype, mpptype;

    /* 5.1.4 PLUSPTYPE */

    /* 5.1.4.1 UFEP : Update Full Extended PTYPE (3 bits) */
    if (!gst_bit_reader_get_bits_uint8 (&br, &ufep, 3))
      goto more;
    GST_DEBUG (" UFEP 0x%x", ufep);

    if (ufep == 1) {
      /* 5.1.4.2 OPPTYPE : The Optional Part of PLUSPTYPE (OPPTYPE) (18 bits) */
      if (!gst_bit_reader_get_bits_uint32 (&br, &opptype, 18))
        goto more;

      /* Last 4 bits are always "1000" */
      if ((opptype & 0xf) != 0x8) {
        GST_WARNING ("Corrupted OPTTYPE");
        goto beach;
      }
      params->format = opptype >> 15;
      params->custompcfpresent = (opptype & 0x4000) == 0x4000;
      if (opptype & 0x2000)
        params->features |= H263_OPTION_UMV_MODE;
      if (opptype & 0x1000)
        params->features |= H263_OPTION_SAC_MODE;
      if (opptype & 0x0800)
        params->features |= H263_OPTION_AP_MODE;
      if (opptype & 0x0400)
        params->features |= H263_OPTION_AIC_MODE;
      if (opptype & 0x0200)
        params->features |= H263_OPTION_DF_MODE;
      if (opptype & 0x0100)
        params->features |= H263_OPTION_SS_MODE;
      if (opptype & 0x0080)
        params->features |= H263_OPTION_RPS_MODE;
      if (opptype & 0x0040)
        params->features |= H263_OPTION_ISD_MODE;
      if (opptype & 0x0020)
        params->features |= H263_OPTION_AIV_MODE;
      if (opptype & 0x0010)
        params->features |= H263_OPTION_MQ_MODE;
      /* Bit 15 is set to 1 to avoid looking like a start code */
      if (opptype & 0x0004)
        params->features |= H263_OPTION_ERPS_MODE;
      if (opptype & 0x0002)
        params->features |= H263_OPTION_DPS_MODE;
    }

    /* 5.1.4.3 MPPTYPE : The mandatory part of PLUSPTYPE (9 bits) */
    if (!gst_bit_reader_get_bits_uint32 (&br, &mpptype, 9))
      goto more;

    /* Last 3 bits are always "001" */
    if ((mpptype & 0x7) != 1) {
      GST_WARNING ("Corrupted MPPTYPE");
      goto beach;
    }

    params->type = mpptype >> 6;
    GST_DEBUG (" Picture Coding Type : %d", params->type);

    if (fast)
      goto done;

    if (mpptype & 0x2000)
      params->features |= H263_OPTION_RPR_MODE;
    if (mpptype & 0x1000)
      params->features |= H263_OPTION_RRU_MODE;

    /* 5.1.20 CPM : Continuous Presence Multipoint and Video Multiplex (1 bit) */
    if (!gst_bit_reader_get_bits_uint8 (&br, &cpm, 1))
      goto more;
    GST_DEBUG (" Continuous Presence Multipoint and Video Multiplex : %d", cpm);

    if (cpm) {
      /* 5.1.21 PSBI : Picture Sub-Bitstream Indicator (2 bits) */
      guint8 psbi;
      if (!gst_bit_reader_get_bits_uint8 (&br, &psbi, 2))
        goto more;
      GST_DEBUG (" Picture Sub-Bitstream Indicator (PSBI):%d", psbi);
    }

    if (ufep == 1) {
      guint32 cpfmt;

      /* 5.1.5 CPFMT : Custom Picture Format (23 bits) */
      if (!gst_bit_reader_get_bits_uint32 (&br, &cpfmt, 23))
        goto more;
      if (!(cpfmt & 0x200)) {
        GST_WARNING ("Corrupted CPFMT (0x%x)", cpfmt);
        goto beach;
      }
      temp8 = cpfmt >> 19;
      params->width = (((cpfmt >> 10) & 0x1f) + 1) * 4;
      params->height = (cpfmt & 0x1f) * 4;

      if (temp8 == 0xf) {
        guint32 epar;
        /* 5.1.6 EPAR : Extended Pixel Aspect Ratio (16bits) */
        if (!gst_bit_reader_get_bits_uint32 (&br, &epar, 16))
          goto more;
        params->parnum = epar >> 8;
        params->pardenom = epar & 0xf;
      } else {
        params->parnum = partable[temp8][0];
        params->pardenom = partable[temp8][1];
      }

      if (params->custompcfpresent) {
        /* 5.1.7 CPCFC : Custom Picture Clock Frequency Code (8bits) */
        /* (we store this as a frame rate) */
        if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 8))
          goto more;
        GST_DEBUG ("  Custom PCF is present (%d)", (int) temp8);
        params->pcfnum = gst_util_uint64_scale_int (1800000, 1, temp8 & 0x7f);
        params->pcfdenom = (temp8 & 0x80) ? 1001 : 1000;
        /* 5.1.8 ETR : Extended Temp8oral Reference (2bits) */
        if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 2))
          goto more;
        params->temporal_ref |= temp8 << 8;
      }

      if (params->features & H263_OPTION_UMV_MODE) {
        guint8 i;
        /* 5.1.9 UUI : Unlimited Unrestricted Motion Vectors Indicator (variable length) */
        if (!gst_bit_reader_get_bits_uint8 (&br, &i, 1))
          goto more;
        if (i == 0) {
          if (!gst_bit_reader_get_bits_uint8 (&br, &i, 1))
            goto more;
          if (i != 1) {
            GST_WARNING ("Corrupted UUI (0%u)", (guint) i);
            goto beach;
          }
          params->uui = UUI_IS_01;
        } else {
          params->uui = UUI_IS_1;
        }
      }

      if (params->features & H263_OPTION_SS_MODE) {
        /* 5.1.10 SSS : Slice Structured Submode bits (2bits) */
        if (!gst_bit_reader_get_bits_uint8 (&br, &params->sss, 2))
          goto more;
      }

      /* WE DO NOT SUPPORT optional Temporal, SNR, and Spatial Scalability mode */
      /* 5.1.11 ELNUM : Enhancement Layer Number (4bits) */
      /* 5.1.12 RLNUM : Reference Layer Number (4bits) */

      if (params->features & H263_OPTION_RPS_MODE) {
        /* 5.1.13 RPSMF : Reference Picture Selection Mode Flags (3bits) */
        /* FIXME : We just swallow the bits */
        if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 3))
          goto more;

        /* 5.1.14 TRPI : Temporal Reference for Prediction Indication (1bit) */
        if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 1))
          goto more;

        if (temp8) {
          /* 5.1.15 TRP : Temporal Reference for Prediction (10bits) */
          /* FIXME : We just swallow the bits */
          if (!gst_bit_reader_get_bits_uint32 (&br, &temp32, 10))
            goto more;
        }

        /* 5.1.16 BCI Back-Channel message Indication (variable length) */
        if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 1))
          goto more;
        if (temp8 == 1) {
          /* 5.1.17 BCM Back-Channel Message (variable length) */
          GST_ERROR ("We won't support Back-Channel Message (BCM)");
          goto beach;
        } else {
          if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 1))
            goto more;
          if (temp8 != 1) {
            GST_WARNING ("Corrupted BCI");
            goto beach;
          }
        }
      }                         /* END H263_OPTION_RPS_MODE */
    }

    GST_DEBUG (" Advanced INTRA Coding mode (Annex I) : %s",
        (params->features & H263_OPTION_AIC_MODE ? "on" : "off"));
    GST_DEBUG (" Deblocking Filter mode (Annex J) : %s",
        (params->features & H263_OPTION_DF_MODE ? "on" : "off"));
    GST_DEBUG (" Slice Structured mode (Annex K) : %s",
        (params->features & H263_OPTION_SS_MODE ? "on" : "off"));
    GST_DEBUG (" Reference Picture Selection mode (Annex N) : %s",
        (params->features & H263_OPTION_RPS_MODE ? "on" : "off"));
    GST_DEBUG (" Independent Segment Decoding mode (Annex R) : %s",
        (params->features & H263_OPTION_ISD_MODE ? "on" : "off"));
    GST_DEBUG (" Alternative INTER VLC mode (Annex S) : %s",
        (params->features & H263_OPTION_AIV_MODE ? "on" : "off"));
    GST_DEBUG (" Modified Quantization mode (Annex T) : %s",
        (params->features & H263_OPTION_MQ_MODE ? "on" : "off"));
    GST_DEBUG (" Enhanced Reference Picture Selection mode (Annex U) : %s",
        (params->features & H263_OPTION_ERPS_MODE ? "on" : "off"));
    GST_DEBUG (" Enhanced Data Partitioned Slices mode (Annex V) : %s",
        (params->features & H263_OPTION_DPS_MODE ? "on" : "off"));

    /* END ufep == 1 */
    /* WE DO NOT SUPPORT optional Reference Picture Resampling mode */
    /* 5.1.18 RPRP : Reference Picture Resampling Parameters (variable length) */
  }

  /* END hasplusptype */
  /* 5.1.19 PQUANT : Quantizer Information (5 bits) */
  if (!gst_bit_reader_get_bits_uint8 (&br, &pquant, 5))
    goto more;
  GST_DEBUG (" PQUANT : 0x%x", pquant);

  if (!hasplusptype) {
    guint8 cpm;
    /* 5.1.20 CPM : Continuous Presence Multipoint and Video Multiplex (1 bit) */
    if (!gst_bit_reader_get_bits_uint8 (&br, &cpm, 1))
      goto more;
    GST_DEBUG (" Continuous Presence Multipoint and Video Multiplex : %d", cpm);

    if (cpm) {
      /* 5.1.21 PSBI : Picture Sub-Bitstream Indicator (2 bits) */
      guint8 psbi;
      if (!gst_bit_reader_get_bits_uint8 (&br, &psbi, 2))
        goto more;
      GST_DEBUG (" Picture Sub-Bitstream Indicator (PSBI):%d", psbi);
    }
  }

  if (params->type & (PICTURE_PB | PICTURE_IMPROVED_PB)) {
    /* 5.1.22 TRb : Temporal Reference for B-pictures in PB-frames (3/5bits) */
    /* FIXME : We just swallow the bits */
    if (!gst_bit_reader_get_bits_uint8 (&br, &temp8,
            params->custompcfpresent ? 5 : 3))
      goto more;

    /* 5.1.23 DBQUANT : Quantization information for B-pictures in PB-frames (2bits) */
    if (!gst_bit_reader_get_bits_uint8 (&br, &temp8, 2))
      goto more;
  }

  GST_DEBUG (" Framerate defined by the stream is %d/%d",
      params->pcfnum, params->pcfdenom);

  /* We ignore the PEI and PSUPP - these may occur in any frame, and can be
   * ignored by decoders that don't support them, except for bits of Annex W */

  /* FIXME: Annex H (Forward Error Correction) requires that we poke into the
   * stream data. */

  /* FIXME: Annex P (Reference Picture Resampling) can be signaled implicitly
   * as well as in the header. Should we set the field to false in caps if it
   * is not specfied by the header? */

  /* FIXME: Annex U (Enhanced Reference Picture Selection) poses a problem - we
   * have no means of specifying what sub-modes, if any, are used. */

done:
  h263parse->state = GOT_HEADER;
more:
  return GST_FLOW_OK;

beach:
  h263parse->state = PASSTHROUGH;
  return GST_FLOW_OK;
}

gint
gst_h263_parse_get_profile (H263Params * params)
{
  gboolean c, d, d1, d21, d22, e, f, f2, g, h, i, j, k, k0, k1, k2, l, m, n, o,
      p, q, r, s, t, u, v, w;

  /* FIXME: some parts of Annex C can be discovered, others can not */
  c = FALSE;
  d = (params->features & H263_OPTION_UMV_MODE) != 0;
  /* d1: Annex D.1; d21: Annex D.2 with UUI=1; d22: Annex D.2 with UUI=01 */
  d1 = (d && params->uui == UUI_ABSENT);
  d21 = (d && params->uui == UUI_IS_1);
  d22 = (d && params->uui == UUI_IS_01);
  e = (params->features & H263_OPTION_SAC_MODE) != 0;
  /* f:Annex  F.2 or F.3 may be used; f2: only Annex F.2 is used (we have no
   * way of detecting this right now */
  f = (params->features & H263_OPTION_AP_MODE) != 0;
  f2 = FALSE;
  g = (params->features & H263_OPTION_PB_MODE) != 0;
  h = FALSE;
  i = (params->features & H263_OPTION_AIC_MODE) != 0;
  j = (params->features & H263_OPTION_DF_MODE) != 0;
  k = (params->features & H263_OPTION_SS_MODE) != 0;
  /* k0: Annex K without submodes; k1: Annex K with ASO; k2: Annex K with RS */
  k0 = (k && params->sss == 0x0);
  k1 = (k && params->sss == 0x2);
  k2 = (k && params->sss == 0x1);
  l = FALSE;
  m = (params->type == PICTURE_IMPROVED_PB);
  n = (params->features & H263_OPTION_RPS_MODE) != 0;
  o = FALSE;
  p = FALSE;
  q = (params->features & H263_OPTION_RRU_MODE) != 0;
  r = (params->features & H263_OPTION_ISD_MODE) != 0;
  s = (params->features & H263_OPTION_AIV_MODE) != 0;
  t = (params->features & H263_OPTION_MQ_MODE) != 0;
  u = (params->features & H263_OPTION_ERPS_MODE) != 0;
  v = (params->features & H263_OPTION_DPS_MODE) != 0;
  w = FALSE;

  /* FIXME: The use of UUI in Annex D seems to be in contradiction with the
   * profile definition in Annex X. Afaict, D.2 with UUI not present is not a
   * meaningful state. */

  /* FIXME: We have no way to distinguish between the use of section F.2 (four
   * motion vectors per macroblock) and F.3 (overlapped block motion
   * compensation), so we assume that they are either both present else neither
   * is. This means if a profile supports only F.2 and not F.3, but we see that
   * Advanced Prediction mode (Annex F) is used, we assume this profile does
   * not apply. */

  /* FIXME: We assume there is no error correction (Annex H) to avoid having to
   * parse the stream to look for its existence. */

  /* FIXME: Profiles 1 and 5-8 need the detection of Annex L.4 which can happen
   * anywhere in the stream, so we just assume it doesn't exist and hope for
   * the best. */

  /* FIXME: Annex O support is TBD. */

  /* FIXME: see note for Annex P elsewhere in this file. */

  /* FIXME: Annex W.6.3.{8,11} suffer the same fate as Annex L.4 above. */

  /* FIXME: We have no way of figuring out submodes when Annex U is used. Here
   * we always assume no submode is used. */

  if (!c && !d && !e && !f && !g && !h && !i && !j && !k && !l && !m && !n &&
      !o && !p && !q && !r && !s && !t && !u && !v && !w)
    return 0;
  if (!c && (!d || d1) && !e && (!f || f2) && !g && !h && !k && !l && !m &&
      !n && !o && !p && !q && !r && !s && !u && !v && !w)
    return 1;
  if (!c && (!d || d1) && !e && !g && !h && !i && !j && !k && !l && !m && !n &&
      !o && !p && !q && !r && !s && !t && !u && !v && !w)
    return 2;
  if (!c && (!d || d1) && !e && (!f || f2) && !g && !h && (!k || k0) && !l &&
      !m && !n && !o && !p && !q && !r && !s && !u && !v && !w)
    return 3;
  if (!c && (!d || d1) && !e && (!f || f2) && !g && !h && (!k || k0) && !l &&
      !m && !n && !o && !p && !q && !r && !s && !u && !w)
    return 4;
  if (!c && (!d || d1 || d21) && !e && !g && !h && !k && !l && !m && !n &&
      !o && !p && !q && !r && !s && !v && !w)
    return 5;
  if (!c && (!d || d1 || d21) && !e && !g && !h && (!k || k0 || k1) && !l &&
      !m && !n && !o && !p && !q && !r && !s && !v && !w)
    return 6;
  if (!c && (!d || d1 || d21) && !e && !g && !h && !k && !l && !m && !n &&
      !o && !p && !q && !r && !s && !v && !w)
    return 7;
  if (!c && (!d || d1 || d21) && !e && !g && !h && (!k || k0 || k1) && !l &&
      !m && !n && !o && !p && !q && !r && !s && !v && !w)
    /* FIXME: needs Annex O and Annex P support */
    return 8;

  return -1;
}

#define H263_PROFILE_NOT_0_2(profile) \
  ((profile) != -1 && (profile) != 0 && (profile) != 2)

#define H263_FMT_UPTO_QCIF(params) \
  ((params)->format == PICTURE_FMT_SUB_QCIF || \
   (params)->format == PICTURE_FMT_QCIF)
#define H263_FMT_UPTO_CIF(params) \
  ((params)->format == PICTURE_FMT_SUB_QCIF || \
   (params)->format == PICTURE_FMT_QCIF || \
   (params)->format == PICTURE_FMT_CIF)
#define H263_FMT_CUSTOM_UPTO_QCIF(params) \
   ((params)->format == PICTURE_FMT_RESERVED1 && \
    (params)->height <= 144 && \
    (params)->width <= 176)
#define H263_FMT_CUSTOM_UPTO_CIF(params) \
   ((params)->format == PICTURE_FMT_RESERVED1 && \
    (params)->height <= 288 && \
    (params)->width <= 352)

#define GST_FRACTION_LE(f1, f2) \
  ((gst_value_compare (&(f1), &(f2)) == GST_VALUE_LESS_THAN) || \
   (gst_value_compare (&(f1), &(f2)) == GST_VALUE_EQUAL))

gint
gst_h263_parse_get_level (H263Params * params, gint profile,
    guint bitrate, gint fps_num, gint fps_denom)
{
  GValue fps15 = { 0, };
  GValue fps30 = { 0, };
  GValue fps50 = { 0, };
  GValue fps60 = { 0, };
  GValue fps = { 0, };

  if (bitrate == 0) {
    GST_DEBUG ("Can't calculate level since bitrate is unknown");
    return -1;
  }

  g_value_init (&fps15, GST_TYPE_FRACTION);
  g_value_init (&fps30, GST_TYPE_FRACTION);
  g_value_init (&fps50, GST_TYPE_FRACTION);
  g_value_init (&fps60, GST_TYPE_FRACTION);
  g_value_init (&fps, GST_TYPE_FRACTION);

  gst_value_set_fraction (&fps15, 15000, 1001);
  gst_value_set_fraction (&fps30, 30000, 1001);
  gst_value_set_fraction (&fps50, 50, 1);
  gst_value_set_fraction (&fps60, 60000, 1001);

  gst_value_set_fraction (&fps, fps_num, fps_denom);

  /* Level 10 */
  if (H263_FMT_UPTO_QCIF (params) && GST_FRACTION_LE (fps, fps15) &&
      bitrate <= 64000)
    return 10;

  /* Level 20 */
  if (((H263_FMT_UPTO_QCIF (params) && GST_FRACTION_LE (fps, fps30)) ||
          (params->format == PICTURE_FMT_CIF && GST_FRACTION_LE (fps, fps15)))
      && bitrate <= 128000)
    return 20;

  /* Level 30 */
  if (H263_FMT_UPTO_CIF (params) && GST_FRACTION_LE (fps, fps30) &&
      bitrate <= 384000)
    return 30;

  /* Level 40 */
  if (H263_FMT_UPTO_CIF (params) && GST_FRACTION_LE (fps, fps30) &&
      bitrate <= 2048000)
    return 40;

  /* Level 45 */
  if ((H263_FMT_UPTO_QCIF (params) || (H263_FMT_CUSTOM_UPTO_QCIF (params) &&
              H263_PROFILE_NOT_0_2 (profile))) &&
      GST_FRACTION_LE (fps, fps15) &&
      /* (!h263parse->custompcfpresent || H263_PROFILE_NOT_0_2(profile)) && */
      bitrate <= 128000)
    return 45;

  /* Level 50 */
  if ((H263_FMT_UPTO_CIF (params) || H263_FMT_CUSTOM_UPTO_CIF (params)) &&
      (GST_FRACTION_LE (fps, fps50) ||
          (params->width <= 352 && params->height <= 240 &&
              GST_FRACTION_LE (fps, fps60))) && (bitrate <= 4096000))
    return 50;

  /* Level 60 */
  if (((params->width <= 720 && params->height <= 288 &&
              GST_FRACTION_LE (fps, fps50)) ||
          (params->width <= 720 && params->height <= 240 &&
              GST_FRACTION_LE (fps, fps60))) && (bitrate <= 8192000))
    return 60;

  /* Level 70 */
  if (((params->width <= 720 && params->height <= 576 &&
              GST_FRACTION_LE (fps, fps50)) ||
          (params->width <= 720 && params->height <= 480 &&
              GST_FRACTION_LE (fps, fps60))) && (bitrate <= 16384000))
    return 70;

  GST_DEBUG ("Weird - didn't match any profile!");
  return -1;
}

void
gst_h263_parse_get_framerate (GstCaps * sink_caps, H263Params * params,
    gint * num, gint * denom)
{
  GstStructure *st;

  st = gst_caps_get_structure (sink_caps, 0);

  if (gst_structure_get_fraction (st, "framerate", num, denom)) {
    /* Got it in caps - nothing more to do */
    GST_DEBUG ("Sink caps override framerate from headers");
    return;
  }

  /* Caps didn't have the framerate - get it from params */
  *num = params->pcfnum;
  *denom = params->pcfdenom;
}

void
gst_h263_parse_set_src_caps (GstH263Parse * h263parse, H263Params * params)
{
  GstCaps *caps;
  gint fr_num, fr_denom;

  g_assert (h263parse->state == PASSTHROUGH || h263parse->state == GOT_HEADER);

  caps = GST_PAD_CAPS (GST_BASE_PARSE_SINK_PAD (GST_BASE_PARSE (h263parse)));
  if (caps) {
    caps = gst_caps_copy (caps);
  } else {
    caps = gst_caps_new_simple ("video/x-h263",
        "variant", G_TYPE_STRING, "itu", NULL);
  }
  gst_caps_set_simple (caps, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  gst_h263_parse_get_framerate (caps, params, &fr_num, &fr_denom);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, fr_num, fr_denom,
      NULL);

  if (h263parse->state == GOT_HEADER) {
    gst_caps_set_simple (caps,
        "annex-d", G_TYPE_BOOLEAN, (params->features & H263_OPTION_UMV_MODE),
        "annex-e", G_TYPE_BOOLEAN, (params->features & H263_OPTION_SAC_MODE),
        "annex-f", G_TYPE_BOOLEAN, (params->features & H263_OPTION_AP_MODE),
        "annex-g", G_TYPE_BOOLEAN, (params->features & H263_OPTION_PB_MODE),
        "annex-i", G_TYPE_BOOLEAN, (params->features & H263_OPTION_AIC_MODE),
        "annex-j", G_TYPE_BOOLEAN, (params->features & H263_OPTION_DF_MODE),
        "annex-k", G_TYPE_BOOLEAN, (params->features & H263_OPTION_SS_MODE),
        "annex-m", G_TYPE_BOOLEAN, (params->type == PICTURE_IMPROVED_PB),
        "annex-n", G_TYPE_BOOLEAN, (params->features & H263_OPTION_RPS_MODE),
        "annex-q", G_TYPE_BOOLEAN, (params->features & H263_OPTION_RRU_MODE),
        "annex-r", G_TYPE_BOOLEAN, (params->features & H263_OPTION_ISD_MODE),
        "annex-s", G_TYPE_BOOLEAN, (params->features & H263_OPTION_AIV_MODE),
        "annex-t", G_TYPE_BOOLEAN, (params->features & H263_OPTION_MQ_MODE),
        "annex-u", G_TYPE_BOOLEAN, (params->features & H263_OPTION_ERPS_MODE),
        "annex-v", G_TYPE_BOOLEAN, (params->features & H263_OPTION_DPS_MODE),
        NULL);

    h263parse->profile = gst_h263_parse_get_profile (params);
    if (h263parse->profile != -1)
      gst_caps_set_simple (caps, "profile", G_TYPE_UINT, h263parse->profile,
          NULL);

    h263parse->level = gst_h263_parse_get_level (params, h263parse->profile,
        h263parse->bitrate, fr_num, fr_denom);
    if (h263parse->level != -1)
      gst_caps_set_simple (caps, "level", G_TYPE_UINT, h263parse->level, NULL);
  }

  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (GST_BASE_PARSE (h263parse)), caps);
  gst_caps_unref (caps);
}
