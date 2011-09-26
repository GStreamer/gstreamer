/* Gstreamer
 * Copyright (C) <2011> Intel
 * Copyright (C) <2011> Collabora Ltd.
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION:gstvc1parser
 * @short_description: Convenience library for parsing vc1 video
 * bitstream.
 *
 * For more details about the structures, look at the
 * smpte specifications (S421m-2006.pdf).
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvc1parser.h"
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <string.h>

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("codecparsers_vc1", 0,
        "VC1 codec parsing library");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}

#else

#define ensure_debug_category() /* NOOP */

#endif /* GST_DISABLE_GST_DEBUG */

/* ------------------------------------------------------------------------- */

#define GET_BITS(b, num, bits) G_STMT_START {        \
  if (!gst_bit_reader_get_bits_uint32(b, bits, num)) \
    goto failed;                                     \
  GST_TRACE ("parsed %d bits: %d", num, *(bits));    \
} G_STMT_END

#define READ_UINT8(br, val, nbits) G_STMT_START {  \
  if (!gst_bit_reader_get_bits_uint8 (br, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto failed; \
  } \
} G_STMT_END

#define READ_UINT16(br, val, nbits) G_STMT_START { \
  if (!gst_bit_reader_get_bits_uint16 (br, &val, nbits)) { \
    GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    goto failed; \
  } \
} G_STMT_END

#define READ_UINT32(br, val, nbits) G_STMT_START { \
  if (!gst_bit_reader_get_bits_uint32 (br, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto failed; \
  } \
} G_STMT_END

#define SKIP(br, nbits) G_STMT_START {  \
  if (!gst_bit_reader_skip (br, nbits)) { \
    GST_WARNING ("Failed to skip nbits: %d", nbits); \
    goto failed; \
  } \
} G_STMT_END

const guint8 vc1_pquant_table[3][32] = {
  {                             /* Implicit quantizer */
        0, 1, 2, 3, 4, 5, 6, 7, 8, 6, 7, 8, 9, 10, 11, 12,
      13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 29, 31},
  {                             /* Explicit quantizer, pquantizer uniform */
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
  {                             /* Explicit quantizer, pquantizer non-uniform */
        0, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
      14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 31}
};

const guint8 mvmode_table[2][5] = {
  {
        GST_VC1_MVMODE_1MV_HPEL_BILINEAR,
        GST_VC1_MVMODE_1MV,
        GST_VC1_MVMODE_1MV_HPEL,
        GST_VC1_MVMODE_MIXED_MV,
      GST_VC1_MVMODE_INTENSITY_COMP},
  {
        GST_VC1_MVMODE_1MV,
        GST_VC1_MVMODE_MIXED_MV,
        GST_VC1_MVMODE_1MV_HPEL,
        GST_VC1_MVMODE_INTENSITY_COMP,
      GST_VC1_MVMODE_1MV_HPEL_BILINEAR}
};

const guint8 mvmode2_table[2][4] = {
  {
        GST_VC1_MVMODE_1MV_HPEL_BILINEAR,
        GST_VC1_MVMODE_1MV,
        GST_VC1_MVMODE_1MV_HPEL,
      GST_VC1_MVMODE_MIXED_MV},
  {
        GST_VC1_MVMODE_1MV,
        GST_VC1_MVMODE_MIXED_MV,
        GST_VC1_MVMODE_1MV_HPEL,
      GST_VC1_MVMODE_1MV_HPEL_BILINEAR}
};

static const guint bfraction_vlc_table[] = {
  0x00, 3, 128,
  0x01, 3, 85,
  0x02, 3, 170,
  0x03, 3, 64,
  0x04, 3, 192,
  0x05, 3, 51,
  0x06, 3, 102,
  0x70, 3, 153,
  0x71, 7, 204,
  0x72, 7, 43,
  0x73, 7, 215,
  0x74, 7, 37,
  0x75, 7, 74,
  0x76, 7, 111,
  0x77, 7, 148,
  0x78, 7, 185,
  0x79, 7, 222,
  0x7a, 7, 32,
  0x7b, 7, 96,
  0x7c, 7, 160,
  0x7d, 7, 224,
  0x7e, 7, 0,                   /* Indicate sthat it is smtpe reserved */
  0x7f, 7, GST_VC1_PICTURE_TYPE_BI
};

/* Imode types */
enum
{
  IMODE_RAW,
  IMODE_NORM2,
  IMODE_DIFF2,
  IMODE_NORM6,
  IMODE_DIFF6,
  IMODE_ROWSKIP,
  IMODE_COLSKIP
};

static const guint imode_vlc_table[] = {
  0x02, 2, IMODE_NORM2,         /* 10 */
  0x03, 2, IMODE_NORM6,         /* 11 */
  0x02, 3, IMODE_ROWSKIP,       /* 010 */
  0x03, 3, IMODE_COLSKIP,       /* 011 */
  0x01, 3, IMODE_DIFF2,         /* 001 */
  0x01, 4, IMODE_DIFF6,         /* 0001 */
  0x00, 4, IMODE_RAW            /* 0000 */
};

const guint vc1_norm2_codes_vlc_table[] = {
  0x00, 1, 1,
  0x03, 2, 3,
  0x04, 3, 3,
  0x05, 3, 2
};

const guint norm6_vlc_table[256] = {
  0x001, 1, 0,
  0x002, 4, 0,
  0x003, 4, 0,
  0x004, 4, 0,
  0x005, 4, 0,
  0x006, 4, 0,
  0x007, 4, 0,
  0x007, 6, 0,
  0x000, 8, 0,
  0x001, 8, 0,
  0x002, 8, 0,
  0x003, 8, 0,
  0x004, 8, 0,
  0x005, 8, 0,
  0x006, 8, 0,
  0x007, 8, 0,
  0x008, 8, 0,
  0x009, 8, 0,
  0x00A, 8, 0,
  0x00B, 8, 0,
  0x00C, 8, 0,
  0x00D, 8, 0,
  0x00E, 8, 0,
  0x037, 9, 0,
  0x036, 9, 0,
  0x035, 9, 0,
  0x034, 9, 0,
  0x033, 9, 0,
  0x032, 9, 0,
  0x047, 10, 0,
  0x04B, 10, 0,
  0x04D, 10, 0,
  0x04E, 10, 0,
  0x30E, 13, 0,
  0x053, 10, 0,
  0x055, 10, 0,
  0x056, 10, 0,
  0x30D, 13, 0,
  0x059, 10, 0,
  0x05A, 10, 0,
  0x30C, 13, 0,
  0x05C, 10, 0,
  0x30B, 13, 0,
  0x30A, 13, 0,
  0x043, 10, 0,
  0x045, 10, 0,
  0x046, 10, 0,
  0x309, 13, 0,
  0x049, 10, 0,
  0x04A, 10, 0,
  0x308, 13, 0,
  0x04C, 10, 0,
  0x307, 13, 0,
  0x306, 13, 0,
  0x051, 10, 0,
  0x052, 10, 0,
  0x305, 13, 0,
  0x054, 10, 0,
  0x304, 13, 0,
  0x303, 13, 0,
  0x058, 10, 0,
  0x302, 13, 0,
  0x301, 13, 0,
  0x300, 13, 0
};

static inline guint8
decode_colskip (GstBitReader * br, guint width, guint height)
{
  guint i;
  guint8 colskip;

  GST_DEBUG ("Colskip rowskip");

  for (i = 0; i < height; i++) {
    READ_UINT8 (br, colskip, 1);

    if (colskip)
      SKIP (br, width);
  }

  return 1;

failed:
  GST_WARNING ("Failed to parse colskip");

  return 0;
}

static inline guint8
decode_rowskip (GstBitReader * br, guint width, guint height)
{
  guint i;
  guint8 rowskip;

  GST_DEBUG ("Parsing rowskip");

  for (i = 0; i < height; i++) {
    READ_UINT8 (br, rowskip, 1);

    if (rowskip)
      SKIP (br, width);
  }
  return 1;

failed:
  GST_WARNING ("Failed to parse rowskip");

  return 0;
}

static inline gint8
decode012 (GstBitReader * br)
{
  guint8 n;

  READ_UINT8 (br, n, 1);

  if (n == 0)
    return 0;

  READ_UINT8 (br, n, 1);

  return n + 1;

failed:
  GST_WARNING ("Could not decode 0 1 2 returning -1");

  return -1;
}

static inline guint
calculate_nb_pan_scan_win (GstVC1AdvancedSeqHdr * advseqhdr,
    GstVC1PicAdvanced * pic)
{
  if (advseqhdr->interlace && !advseqhdr->psf) {
    if (advseqhdr->pulldown)
      return pic->rff + 2;

    return 2;

  } else {
    if (advseqhdr->pulldown)
      return pic->rptfrm + 1;

    return 1;
  }
}


/**
 * table should look like:
 *  {Value, nbBits, Meaning,
 *  ...
 *  } nbBits must be increasing
 */
static gboolean
decode_vlc (GstBitReader * br, guint * res, const guint * table, guint length)
{
  guint8 i;
  guint cbits = 0;
  guint32 value = 0;

  for (i = 0; i < length; i += 3) {
    if (cbits != table[i + 1]) {
      cbits = table[i + 1];
      if (!gst_bit_reader_peek_bits_uint32 (br, &value, cbits)) {
        goto failed;
      }
    }

    if (value == table[i]) {
      SKIP (br, cbits);
      if (res)
        *res = table[i + 2];

      return TRUE;
    }
  }

failed:
  {
    GST_DEBUG ("Could not decode VLC returning -1");

    return FALSE;
  }
}

/*** bitplane decoding ***/
static gint
bitplane_decoding (GstBitReader * br, guint height,
    guint width, guint8 * is_raw)
{
  guint imode;
  guint i, j, offset = 0;

  SKIP (br, 1);
  if (!decode_vlc (br, &imode, imode_vlc_table, G_N_ELEMENTS (imode_vlc_table)))
    goto failed;

  switch (imode) {
    case IMODE_RAW:

      GST_DEBUG ("Parsing IMODE_RAW");

      *is_raw = TRUE;
      return TRUE;

    case IMODE_DIFF2:
    case IMODE_NORM2:

      GST_DEBUG ("Parsing IMODE_DIFF2 or IMODE_NORM2 biplane");

      if ((height * width) & 1) {
        SKIP (br, 1);
      }

      for (i = offset; i < height * width; i += 2) {
        /*guint x; */
        if (!decode_vlc (br, NULL, vc1_norm2_codes_vlc_table,
                G_N_ELEMENTS (vc1_norm2_codes_vlc_table))) {
          goto failed;
        }
      }
      break;

    case IMODE_DIFF6:
    case IMODE_NORM6:

      GST_DEBUG ("Parsing IMODE_DIFF6 or IMODE_NORM6 biplane");

      if (!(height % 3) && (width % 3)) {       // use 2x3 decoding

        for (i = 0; i < height; i += 3) {
          for (j = width & 1; j < width; j += 2) {
            if (!decode_vlc (br, NULL, norm6_vlc_table,
                    G_N_ELEMENTS (norm6_vlc_table))) {
              goto failed;
            }
          }
        }
      } else {
        for (i = height & 1; i < height; i += 2) {
          for (j = width % 3; j < width; j += 3) {
            if (!decode_vlc (br, NULL, norm6_vlc_table,
                    G_N_ELEMENTS (norm6_vlc_table))) {
              goto failed;
            }
          }
        }

        j = width % 3;
        if (j)
          decode_colskip (br, height, width);

        if (height & 1)
          decode_rowskip (br, height, width);
      }
      break;
    case IMODE_ROWSKIP:

      GST_DEBUG ("Parsing IMODE_ROWSKIP biplane");

      if (!decode_rowskip (br, width, height))
        goto failed;
      break;
    case IMODE_COLSKIP:

      GST_DEBUG ("Parsing IMODE_COLSKIP biplane");

      if (decode_colskip (br, width, height))
        goto failed;
      break;
  }

  return TRUE;

failed:
  GST_WARNING ("Failed to decode bitplane");

  return FALSE;
}

static gboolean
parse_vopdquant (GstBitReader * br, GstVC1FrameHdr * framehdr, guint8 dquant)
{
  GstVC1VopDquant *vopdquant = &framehdr->vopdquant;

  GST_DEBUG ("Parsing vopdquant");

  vopdquant->dqbilevel = 0;

  if (dquant == 2) {
    READ_UINT8 (br, vopdquant->dquantfrm, 1);

    READ_UINT8 (br, vopdquant->pqdiff, 3);

    if (vopdquant->pqdiff == 7)
      READ_UINT8 (br, vopdquant->abspq, 5);
    else
      vopdquant->abspq = framehdr->pquant + vopdquant->pqdiff + 1;

  } else {
    READ_UINT8 (br, vopdquant->dquantfrm, 1);
    GST_DEBUG (" %u DquantFrm %u", gst_bit_reader_get_pos (br),
        vopdquant->dquantfrm);

    if (vopdquant->dquantfrm) {
      READ_UINT8 (br, vopdquant->dqprofile, 1);

      switch (vopdquant->dqprofile) {
        case GST_VC1_DQPROFILE_SINGLE_EDGE:
        case GST_VC1_DQPROFILE_DOUBLE_EDGES:
          READ_UINT8 (br, vopdquant->dqsbedge, 2);
          break;

        case GST_VC1_DQPROFILE_ALL_MBS:
          READ_UINT8 (br, vopdquant->dqbilevel, 1);
          break;
      }

      if (vopdquant->dqbilevel
          || vopdquant->dqprofile != GST_VC1_DQPROFILE_ALL_MBS) {
        {
          READ_UINT8 (br, vopdquant->pqdiff, 3);

          if (vopdquant->pqdiff == 7)
            READ_UINT8 (br, vopdquant->abspq, 5);
        }
      }
    }
  }

  return TRUE;

failed:
  GST_WARNING ("Failed to parse vopdquant");

  return FALSE;
}

static inline gint
scan_for_start_codes (const guint8 * data, guint size)
{
  GstByteReader br;
  gst_byte_reader_init (&br, data, size);

  /* NALU not empty, so we can at least expect 1 (even 2) bytes following sc */
  return gst_byte_reader_masked_scan_uint32 (&br, 0xffffff00, 0x00000100,
      0, size);
}

static inline gint
get_unary (GstBitReader * br, gint stop, gint len)
{
  int i;
  guint8 current = 0xff;

  for (i = 0; i < len; i++) {
    gst_bit_reader_get_bits_uint8 (br, &current, 1);
    if (current == stop)
      return i;
  }

  return i;
}

static GstVC1ParseResult
parse_hrd_param_flag (GstBitReader * br, GstVC1HrdParam * hrd_param)
{
  guint i;

  GST_DEBUG ("Parsing Hrd param flag");


  if (gst_bit_reader_get_remaining (br) < 13)
    goto failed;

  hrd_param->hrd_num_leaky_buckets =
      gst_bit_reader_get_bits_uint8_unchecked (br, 5);
  hrd_param->bit_rate_exponent =
      gst_bit_reader_get_bits_uint8_unchecked (br, 4);
  hrd_param->buffer_size_exponent =
      gst_bit_reader_get_bits_uint8_unchecked (br, 4);

  if (gst_bit_reader_get_remaining (br) <
      (32 * hrd_param->hrd_num_leaky_buckets))
    goto failed;

  for (i = 0; i < hrd_param->hrd_num_leaky_buckets; i++) {
    hrd_param->hrd_rate[i] = gst_bit_reader_get_bits_uint16_unchecked (br, 16);
    hrd_param->hrd_buffer[i] =
        gst_bit_reader_get_bits_uint16_unchecked (br, 16);
  }

  return GST_VC1_PARSER_OK;

failed:
  GST_WARNING ("Failed to parse hrd param flag");

  return GST_VC1_PARSER_ERROR;
}

static GstVC1ParseResult
parse_sequence_header_advanced (GstVC1SeqHdr * seqhdr, GstBitReader * br)
{
  GstVC1AdvancedSeqHdr *advanced = &seqhdr->profile.advanced;

  GST_DEBUG ("Parsing sequence header in advanced mode");

  READ_UINT8 (br, advanced->level, 3);

  READ_UINT8 (br, seqhdr->colordiff_format, 2);
  READ_UINT8 (br, seqhdr->frmrtq_postproc, 3);
  READ_UINT8 (br, seqhdr->bitrtq_postproc, 5);

  GST_DEBUG ("level %u, colordiff_format %u , frmrtq_postproc %u,"
      " bitrtq_postproc %u", advanced->level, seqhdr->colordiff_format,
      seqhdr->frmrtq_postproc, seqhdr->bitrtq_postproc);

  /* Calulate bitrate and framerate */
  if (seqhdr->frmrtq_postproc == 0 && seqhdr->bitrtq_postproc == 30) {
    seqhdr->framerate = 0;
    seqhdr->bitrate = 0;
  } else if (seqhdr->frmrtq_postproc == 0 && seqhdr->bitrtq_postproc == 30) {
    seqhdr->framerate = 2;
    seqhdr->bitrate = 1952;
  } else if (seqhdr->frmrtq_postproc == 0 && seqhdr->bitrtq_postproc == 31) {
    seqhdr->framerate = 6;
    seqhdr->bitrate = 2016;
  } else {
    if (seqhdr->frmrtq_postproc == 7) {
      seqhdr->framerate = 30;
    } else {
      seqhdr->framerate = 2 + (seqhdr->frmrtq_postproc * 4);
    }
    if (seqhdr->bitrtq_postproc == 31) {
      seqhdr->bitrate = 2016;
    } else {
      seqhdr->bitrate = 32 + (seqhdr->bitrtq_postproc * 64);
    }
  }

  if (gst_bit_reader_get_remaining (br) < 32)
    goto failed;

  advanced->postprocflag = gst_bit_reader_get_bits_uint8_unchecked (br, 1);
  advanced->max_coded_width = gst_bit_reader_get_bits_uint16_unchecked (br, 12);
  advanced->max_coded_height =
      gst_bit_reader_get_bits_uint16_unchecked (br, 12);
  advanced->max_coded_width = (advanced->max_coded_width + 1) << 1;
  advanced->max_coded_height = (advanced->max_coded_height + 1) << 1;
  advanced->pulldown = gst_bit_reader_get_bits_uint8_unchecked (br, 1);
  advanced->interlace = gst_bit_reader_get_bits_uint8_unchecked (br, 1);
  advanced->tfcntrflag = gst_bit_reader_get_bits_uint8_unchecked (br, 1);
  seqhdr->finterpflag = gst_bit_reader_get_bits_uint8_unchecked (br, 1);

  GST_DEBUG ("postprocflag %u, max_coded_width %u, max_coded_height %u,"
      "pulldown %u, interlace %u, tfcntrflag %u, finterpflag %u",
      advanced->postprocflag, advanced->max_coded_width,
      advanced->max_coded_height, advanced->pulldown,
      advanced->interlace, advanced->tfcntrflag, seqhdr->finterpflag);

  /* Skipping reserved bit */
  gst_bit_reader_skip_unchecked (br, 1);

  advanced->psf = gst_bit_reader_get_bits_uint8_unchecked (br, 1);
  advanced->display_ext = gst_bit_reader_get_bits_uint8_unchecked (br, 1);
  if (advanced->display_ext) {
    READ_UINT16 (br, advanced->disp_horiz_size, 14);
    READ_UINT16 (br, advanced->disp_vert_size, 14);

    advanced->disp_horiz_size++;
    advanced->disp_vert_size++;

    READ_UINT8 (br, advanced->aspect_ratio_flag, 1);

    if (advanced->aspect_ratio_flag) {
      READ_UINT8 (br, advanced->aspect_ratio, 4);

      if (advanced->aspect_ratio == 15) {
        READ_UINT8 (br, advanced->aspect_horiz_size, 8);
        READ_UINT8 (br, advanced->aspect_vert_size, 8);
      }
    }
    READ_UINT8 (br, advanced->framerate_flag, 1);
    if (advanced->framerate_flag) {
      READ_UINT8 (br, advanced->framerateind, 1);

      if (!advanced->framerateind) {
        READ_UINT8 (br, advanced->frameratenr, 8);
        READ_UINT8 (br, advanced->frameratedr, 4);
      } else {
        READ_UINT16 (br, advanced->framerateexp, 16);
      }
    }
    READ_UINT8 (br, advanced->color_format_flag, 1);

    if (advanced->color_format_flag) {
      if (gst_bit_reader_get_remaining (br) < 24)
        goto failed;

      advanced->color_prim = gst_bit_reader_get_bits_uint8_unchecked (br, 8);
      advanced->transfer_char = gst_bit_reader_get_bits_uint8_unchecked (br, 8);
      advanced->matrix_coef = gst_bit_reader_get_bits_uint8_unchecked (br, 8);
    }
  }
  READ_UINT8 (br, advanced->hrd_param_flag, 1);
  if (advanced->hrd_param_flag)
    return parse_hrd_param_flag (br, &advanced->hrd_param);

  return GST_VC1_PARSER_OK;

failed:
  GST_WARNING ("Failed to parse advanced headers");

  return GST_VC1_PARSER_ERROR;
}

static GstVC1ParseResult
parse_frame_header_advanced (GstBitReader * br, GstVC1FrameHdr * framehdr,
    GstVC1SeqHdr * seqhdr)
{
  GstVC1AdvancedSeqHdr *advhdr = &seqhdr->profile.advanced;
  GstVC1PicAdvanced *pic = &framehdr->pic.advanced;
  GstVC1EntryPointHdr *entrypthdr = &advhdr->entrypoint;
  guint8 mvmodeidx;
  guint width = (entrypthdr->coded_width + 15) >> 4;
  guint height = (entrypthdr->coded_height + 15) >> 4;

  GST_DEBUG ("Parsing Frame header advanced %u", advhdr->interlace);

  /* Set the conveninence fields */
  framehdr->profile = seqhdr->profiletype;
  framehdr->dquant = entrypthdr->dquant;

  if (advhdr->interlace) {
    gint8 fcm = decode012 (br);

    if (fcm < 0)
      goto failed;

    pic->fcm = (guint8) fcm;
  }

  framehdr->ptype = get_unary (br, 0, 4);

  if (framehdr->ptype == GST_VC1_PICTURE_TYPE_SKIPPED)
    goto failed;

  if (advhdr->tfcntrflag) {
    READ_UINT8 (br, pic->tfcntr, 8);
    GST_DEBUG ("tfcntr %u", pic->tfcntr);
  }

  if (advhdr->pulldown) {
    if (!advhdr->interlace || advhdr->psf) {

      READ_UINT8 (br, pic->rptfrm, 2);
      GST_DEBUG ("rptfrm %u", pic->rptfrm);

    } else {

      READ_UINT8 (br, pic->tff, 1);
      READ_UINT8 (br, pic->rff, 1);
      GST_DEBUG ("tff %u, rff %u", pic->tff, pic->rff);
    }
  }

  if (entrypthdr->panscan_flag) {
    READ_UINT8 (br, pic->ps_present, 1);

    if (pic->ps_present) {
      guint i, nb_pan_scan_win = calculate_nb_pan_scan_win (advhdr, pic);

      if (gst_bit_reader_get_remaining (br) < 64 * nb_pan_scan_win)
        goto failed;

      for (i = 0; i < nb_pan_scan_win; i++) {
        pic->ps_hoffset = gst_bit_reader_get_bits_uint32_unchecked (br, 18);
        pic->ps_voffset = gst_bit_reader_get_bits_uint32_unchecked (br, 18);
        pic->ps_width = gst_bit_reader_get_bits_uint16_unchecked (br, 14);
        pic->ps_height = gst_bit_reader_get_bits_uint16_unchecked (br, 14);
      }
    }
  }

  READ_UINT8 (br, pic->rndctrl, 1);

  if (advhdr->interlace) {
    READ_UINT8 (br, pic->uvsamp, 1);
    GST_DEBUG ("uvsamp %u", pic->uvsamp);
  }

  if (seqhdr->finterpflag) {
    READ_UINT8 (br, framehdr->interpfrm, 1);
    GST_DEBUG ("interpfrm %u", framehdr->interpfrm);
  }

  if (framehdr->ptype == GST_VC1_PICTURE_TYPE_B) {
    if (!decode_vlc (br, (guint *) & pic->bfraction, bfraction_vlc_table,
            G_N_ELEMENTS (bfraction_vlc_table)))
      goto failed;

    GST_DEBUG ("bfraction %u", pic->bfraction);

    if (pic->bfraction == GST_VC1_PICTURE_TYPE_BI) {
      framehdr->ptype = GST_VC1_PICTURE_TYPE_BI;
    }

  }

  READ_UINT8 (br, framehdr->pqindex, 5);
  if (!framehdr->pqindex)
    goto failed;

  /* compute pquant */
  if (entrypthdr->quantizer == GST_VC1_QUANTIZER_IMPLICITLY)
    framehdr->pquant = vc1_pquant_table[0][framehdr->pqindex];
  else
    framehdr->pquant = vc1_pquant_table[1][framehdr->pqindex];

  framehdr->pquantizer = 1;
  if (entrypthdr->quantizer == GST_VC1_QUANTIZER_IMPLICITLY)
    framehdr->pquantizer = framehdr->pqindex < 9;
  if (entrypthdr->quantizer == GST_VC1_QUANTIZER_NON_UNIFORM)
    framehdr->pquantizer = 0;

  if (framehdr->pqindex <= 8)
    READ_UINT8 (br, framehdr->halfqp, 1);
  else
    framehdr->halfqp = 0;

  if (entrypthdr->quantizer == GST_VC1_QUANTIZER_EXPLICITLY) {
    READ_UINT8 (br, framehdr->pquantizer, 1);
  }

  if (advhdr->postprocflag)
    READ_UINT8 (br, pic->postproc, 2);

  GST_DEBUG ("Parsing %u picture, pqindex %u, pquant %u pquantizer %u"
      "halfqp %u", framehdr->ptype, framehdr->pqindex, framehdr->pquant,
      framehdr->pquantizer, framehdr->halfqp);

  switch (framehdr->ptype) {
    case GST_VC1_PICTURE_TYPE_I:
    case GST_VC1_PICTURE_TYPE_BI:
      if (!bitplane_decoding (br, height, width, &pic->acpred))
        goto failed;

      if (entrypthdr->overlap && framehdr->pquant <= 8) {
        pic->condover = decode012 (br);

        if (pic->condover == (guint8) - 1)
          goto failed;

        else if (pic->condover == GST_VC1_CONDOVER_SELECT) {

          if (!bitplane_decoding (br, height, width, &pic->overflags))
            goto failed;
          GST_DEBUG ("overflags %u", pic->overflags);
        }
      }

      framehdr->transacfrm = get_unary (br, 0, 2);
      pic->transacfrm2 = get_unary (br, 0, 2);
      READ_UINT8 (br, framehdr->transdctab, 1);

      if (framehdr->dquant)
        parse_vopdquant (br, framehdr, framehdr->dquant);

      GST_DEBUG ("acpred %u, condover %u transacfrm %u transacfrm2 %u,",
          pic->acpred, pic->condover, framehdr->transacfrm, pic->transacfrm2);
      break;

    case GST_VC1_PICTURE_TYPE_B:
      if (entrypthdr->extended_mv)
        pic->mvrange = get_unary (br, 0, 3);
      else
        pic->mvrange = 0;

      READ_UINT8 (br, pic->mvmode, 1);

      if (!bitplane_decoding (br, height, width, &pic->directmb))
        goto failed;

      if (!bitplane_decoding (br, height, width, &pic->skipmb))
        goto failed;

      READ_UINT8 (br, pic->mvtab, 2);
      READ_UINT8 (br, pic->cbptab, 2);

      if (framehdr->dquant) {
        parse_vopdquant (br, framehdr, framehdr->dquant);
      }

      if (entrypthdr->vstransform) {
        READ_UINT8 (br, pic->ttmbf, 1);

        if (pic->ttmbf) {
          READ_UINT8 (br, pic->ttfrm, 2);
        }
      }

      framehdr->transacfrm = get_unary (br, 0, 2);
      READ_UINT8 (br, framehdr->transdctab, 1);

      GST_DEBUG ("transacfrm %u transdctab %u mvmode %u mvtab %u,"
          "cbptab %u directmb %u skipmb %u", framehdr->transacfrm,
          framehdr->transdctab, pic->mvmode, pic->mvtab, pic->cbptab,
          pic->directmb, pic->skipmb);

      break;
    case GST_VC1_PICTURE_TYPE_P:
      if (entrypthdr->extended_mv)
        pic->mvrange = get_unary (br, 0, 3);
      else
        pic->mvrange = 0;

      mvmodeidx = framehdr->pquant > 12 ? 0 : 1;
      pic->mvmode = mvmode_table[mvmodeidx][get_unary (br, 1, 4)];

      if (pic->mvmode == GST_VC1_MVMODE_INTENSITY_COMP) {

        pic->mvmode2 = mvmode2_table[mvmodeidx][get_unary (br, 1, 4)];
        READ_UINT8 (br, pic->lumscale, 6);
        READ_UINT8 (br, pic->lumshift, 6);

        GST_DEBUG ("lumscale %u lumshift %u", pic->lumscale, pic->lumshift);
      }

      if (pic->mvmode == GST_VC1_MVMODE_MIXED_MV ||
          (pic->mvmode == GST_VC1_MVMODE_INTENSITY_COMP &&
              pic->mvmode2 == GST_VC1_MVMODE_MIXED_MV)) {
        if (!bitplane_decoding (br, height, width, &pic->mvtypemb))
          goto failed;
        GST_DEBUG ("mvtypemb %u", pic->mvtypemb);
      }

      if (!bitplane_decoding (br, height, width, &pic->skipmb) ||
          gst_bit_reader_get_remaining (br) < 4)
        goto failed;

      pic->mvtab = gst_bit_reader_get_bits_uint8_unchecked (br, 2);
      pic->cbptab = gst_bit_reader_get_bits_uint8_unchecked (br, 2);

      if (framehdr->dquant) {
        parse_vopdquant (br, framehdr, framehdr->dquant);
      }

      if (entrypthdr->vstransform) {
        READ_UINT8 (br, pic->ttmbf, 1);

        if (pic->ttmbf) {
          READ_UINT8 (br, pic->ttfrm, 2);
        }
      }

      framehdr->transacfrm = get_unary (br, 0, 2);
      READ_UINT8 (br, framehdr->transdctab, 1);

      GST_DEBUG ("transacfrm %u transdctab %u mvmode %u mvtab %u,"
          "cbptab %u skipmb %u", framehdr->transacfrm, framehdr->transdctab,
          pic->mvmode, pic->mvtab, pic->cbptab, pic->skipmb);

      break;
  }

  return GST_VC1_PARSER_OK;

failed:
  GST_WARNING ("Failed to parse frame header");

  return GST_VC1_PARSER_ERROR;
}

static GstVC1ParseResult
parse_frame_header (GstBitReader * br, GstVC1FrameHdr * framehdr,
    GstVC1SeqHdr * seqhdr)
{
  guint8 mvmodeidx;
  GstVC1PicSimpleMain *pic = &framehdr->pic.simple;
  GstVC1SimpleMainSeqHdr *simplehdr = &seqhdr->profile.simplemain;
  guint width = (simplehdr->coded_width + 15) >> 4;
  guint height = (simplehdr->coded_height + 15) >> 4;


  GST_DEBUG ("Parsing frame header in simple or main mode");

  /* Set the conveninence fields */
  framehdr->profile = seqhdr->profiletype;
  framehdr->dquant = simplehdr->dquant;

  framehdr->interpfrm = 0;
  if (seqhdr->finterpflag)
    READ_UINT8 (br, framehdr->interpfrm, 1);

  READ_UINT8 (br, pic->frmcnt, 2);

  pic->rangeredfrm = 0;
  if (simplehdr->rangered) {
    READ_UINT8 (br, pic->rangeredfrm, 2);
  }

  /*  Figuring out the picture type */
  READ_UINT8 (br, framehdr->ptype, 1);
  if (simplehdr->maxbframes) {
    if (!framehdr->ptype) {
      READ_UINT8 (br, framehdr->ptype, 1);

      if (framehdr->ptype)
        framehdr->ptype = GST_VC1_PICTURE_TYPE_I;
      else
        framehdr->ptype = GST_VC1_PICTURE_TYPE_B;

    } else
      framehdr->ptype = GST_VC1_PICTURE_TYPE_P;

  } else {
    if (framehdr->ptype)
      framehdr->ptype = GST_VC1_PICTURE_TYPE_P;
    else
      framehdr->ptype = GST_VC1_PICTURE_TYPE_I;
  }


  if (framehdr->ptype == GST_VC1_PICTURE_TYPE_B) {

    if (!decode_vlc (br, (guint *) & pic->bfraction, bfraction_vlc_table,
            G_N_ELEMENTS (bfraction_vlc_table)))
      goto failed;

    if (pic->bfraction == GST_VC1_PICTURE_TYPE_BI) {
      framehdr->ptype = GST_VC1_PICTURE_TYPE_BI;
    }
    GST_DEBUG ("bfraction= %d", pic->bfraction);
  }

  if (framehdr->ptype == GST_VC1_PICTURE_TYPE_I ||
      framehdr->ptype == GST_VC1_PICTURE_TYPE_BI)
    READ_UINT8 (br, pic->bf, 7);

  READ_UINT8 (br, framehdr->pqindex, 5);
  if (!framehdr->pqindex)
    return GST_VC1_PARSER_ERROR;

  GST_DEBUG ("pqindex %u", framehdr->pqindex);

  /* compute pquant */
  if (simplehdr->quantizer == GST_VC1_QUANTIZER_IMPLICITLY)
    framehdr->pquant = vc1_pquant_table[0][framehdr->pqindex];
  else
    framehdr->pquant = vc1_pquant_table[1][framehdr->pqindex];

  GST_DEBUG ("pquant %u", framehdr->pquant);

  if (framehdr->pqindex <= 8)
    READ_UINT8 (br, framehdr->halfqp, 1);
  else
    framehdr->halfqp = 0;

  /* Set pquantizer */
  framehdr->pquantizer = 1;
  if (simplehdr->quantizer == GST_VC1_QUANTIZER_IMPLICITLY)
    framehdr->pquantizer = framehdr->pqindex < 9;
  else if (simplehdr->quantizer == GST_VC1_QUANTIZER_NON_UNIFORM)
    framehdr->pquantizer = 0;

  if (simplehdr->quantizer == GST_VC1_QUANTIZER_EXPLICITLY)
    READ_UINT8 (br, framehdr->pquantizer, 1);

  if (simplehdr->extended_mv == 1) {
    pic->mvrange = get_unary (br, 0, 3);
    GST_DEBUG ("mvrange %u", pic->mvrange);
  }

  if (simplehdr->multires && (framehdr->ptype == GST_VC1_PICTURE_TYPE_P ||
          framehdr->ptype == GST_VC1_PICTURE_TYPE_I)) {
    READ_UINT8 (br, pic->respic, 2);
    GST_DEBUG ("Respic %u", pic->respic);
  }

  GST_DEBUG ("Parsing %u Frame, pquantizer %u, halfqp %u, rangeredfrm %u, "
      "interpfrm %u", framehdr->ptype, framehdr->pquantizer, framehdr->halfqp,
      pic->rangeredfrm, framehdr->interpfrm);

  switch (framehdr->ptype) {
    case GST_VC1_PICTURE_TYPE_I:
    case GST_VC1_PICTURE_TYPE_BI:
      framehdr->transacfrm = get_unary (br, 0, 2);
      pic->transacfrm2 = get_unary (br, 0, 2);
      READ_UINT8 (br, framehdr->transdctab, 1);

      GST_DEBUG ("transacfrm %u, transacfrm2 %u, transdctab %u",
          framehdr->transacfrm, pic->transacfrm2, framehdr->transdctab);
      break;

    case GST_VC1_PICTURE_TYPE_P:
      mvmodeidx = framehdr->pquant > 12;

      pic->mvmode = mvmode_table[mvmodeidx][get_unary (br, 1, 4)];

      if (pic->mvmode == GST_VC1_MVMODE_INTENSITY_COMP) {
        pic->mvmode2 = mvmode2_table[mvmodeidx][get_unary (br, 1, 4)];
        READ_UINT8 (br, pic->lumscale, 6);
        READ_UINT8 (br, pic->lumshift, 6);
        GST_DEBUG ("lumscale %u lumshift %u", pic->lumscale, pic->lumshift);
      }

      if (pic->mvmode == GST_VC1_MVMODE_MIXED_MV ||
          (pic->mvmode == GST_VC1_MVMODE_INTENSITY_COMP &&
              pic->mvmode2 == GST_VC1_MVMODE_MIXED_MV)) {
        if (!bitplane_decoding (br, height, width, &pic->mvtypemb))
          goto failed;
        GST_DEBUG ("mvtypemb %u", pic->mvtypemb);
      }
      if (!bitplane_decoding (br, height, width, &pic->skipmb))
        goto failed;

      READ_UINT8 (br, pic->mvtab, 2);
      READ_UINT8 (br, pic->cbptab, 2);

      if (framehdr->dquant) {
        parse_vopdquant (br, framehdr, framehdr->dquant);
      }

      if (simplehdr->vstransform) {
        READ_UINT8 (br, pic->ttmbf, 1);
        GST_DEBUG ("ttmbf %u", pic->ttmbf);

        if (pic->ttmbf) {
          READ_UINT8 (br, pic->ttfrm, 2);
          GST_DEBUG ("ttfrm %u", pic->ttfrm);
        }
      }

      framehdr->transacfrm = get_unary (br, 0, 2);
      READ_UINT8 (br, framehdr->transdctab, 1);

      GST_DEBUG ("transacfrm %u transdctab %u mvmode %u mvtab %u,"
          "cbptab %u skipmb %u", framehdr->transacfrm, framehdr->transdctab,
          pic->mvmode, pic->mvtab, pic->cbptab, pic->skipmb);
      break;

    case GST_VC1_PICTURE_TYPE_B:
      READ_UINT8 (br, pic->mvmode, 1);
      if (!bitplane_decoding (br, height, width, &pic->directmb))
        goto failed;
      if (!bitplane_decoding (br, height, width, &pic->skipmb) == -1)
        goto failed;

      READ_UINT8 (br, pic->mvtab, 2);
      READ_UINT8 (br, pic->cbptab, 2);

      if (framehdr->dquant)
        parse_vopdquant (br, framehdr, framehdr->dquant);

      if (simplehdr->vstransform) {
        READ_UINT8 (br, pic->ttmbf, 1);

        if (pic->ttmbf) {
          READ_UINT8 (br, pic->ttfrm, 2);
        }
      }

      framehdr->transacfrm = get_unary (br, 0, 2);
      READ_UINT8 (br, framehdr->transdctab, 1);

      GST_DEBUG ("transacfrm %u transdctab %u mvmode %u mvtab %u,"
          "cbptab %u directmb %u skipmb %u", framehdr->transacfrm,
          framehdr->transdctab, pic->mvmode, pic->mvtab, pic->cbptab,
          pic->directmb, pic->skipmb);

      break;
  }

  return GST_VC1_PARSER_OK;

failed:
  GST_WARNING ("Failed to parse Simple picture header");

  return GST_VC1_PARSER_ERROR;
}

/**** API ****/
/**
 * gst_vc1_identify_next_bdu:
 * @data: The data to parse
 * @size: the size of @data
 * @bdu: (out): The #GstVC1BDU where to store parsed bdu headers
 *
 * Parses @data and fills @bdu fields
 *
 * Returns: a #GstVC1ParseResult
 */
GstVC1ParseResult
gst_vc1_identify_next_bdu (const guint8 * data, gsize size, GstVC1BDU * bdu)
{
  gint off1, off2;

  g_return_val_if_fail (bdu != NULL, GST_VC1_PARSER_ERROR);

  ensure_debug_category ();

  if (size <= 4) {
    GST_DEBUG ("Can't parse, buffer is to small size %" G_GSSIZE_FORMAT, size);
    return GST_VC1_PARSER_ERROR;
  }

  off1 = scan_for_start_codes (data, size);

  if (off1 < 0) {
    GST_DEBUG ("No start code prefix in this buffer");
    return GST_VC1_PARSER_NO_BDU;
  }

  bdu->sc_offset = off1;

  bdu->offset = off1 + 4;
  bdu->data = (guint8 *) data;
  bdu->type = (GstVC1StartCode) (data[bdu->offset - 1]);

  off2 = scan_for_start_codes (data + bdu->offset, size - bdu->offset);
  if (off2 < 0) {
    GST_DEBUG ("Bdu start %d, No end found", bdu->offset);

    return GST_VC1_PARSER_NO_BDU_END;
  }

  if (off2 > 0 && data[bdu->offset + off2 - 1] == 00)
    off2--;

  bdu->size = off2;

  GST_DEBUG ("Complete bdu found. Off: %d, Size: %d", bdu->offset, bdu->size);
  return GST_VC1_PARSER_OK;
}

/**
 * gst_vc1_parse_sequence_header:
 * @data: The data to parse
 * @size: the size of @data
 * @seqhdr: The #GstVC1SeqHdr to set.
 *
 * Parses @data, and fills @seqhdr fields.
 *
 * Returns: a #GstVC1ParseResult
 */
GstVC1ParseResult
gst_vc1_parse_sequence_header (const guint8 * data, gsize size,
    GstVC1SeqHdr * seqhdr)
{
  GstBitReader br;
  guint8 old_interlaced_mode;
  GstVC1SimpleMainSeqHdr *simplehdr = &seqhdr->profile.simplemain;

  g_return_val_if_fail (seqhdr != NULL, GST_VC1_PARSER_ERROR);

  ensure_debug_category ();

  gst_bit_reader_init (&br, data, size);

  READ_UINT8 (&br, seqhdr->profiletype, 2);

  if (seqhdr->profiletype == GST_VC1_PROFILE_ADVANCED) {
    return parse_sequence_header_advanced (seqhdr, &br);
  }

  GST_DEBUG ("Parsing sequence header in simple or main mode");

  if (gst_bit_reader_get_remaining (&br) < 29)
    goto failed;

  /* Reserved bits */
  old_interlaced_mode = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  if (old_interlaced_mode)
    GST_WARNING ("Old interlaced mode used");

  simplehdr->wmvp = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  if (simplehdr->wmvp)
    GST_DEBUG ("WMVP mode");

  seqhdr->frmrtq_postproc = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  seqhdr->bitrtq_postproc = gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
  simplehdr->loop_filter = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);

  /* Skipping reserved3 bit */
  gst_bit_reader_skip_unchecked (&br, 1);

  simplehdr->multires = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);

  /* Skipping reserved4 bit */
  gst_bit_reader_skip_unchecked (&br, 1);

  simplehdr->fastuvmc = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  simplehdr->extended_mv = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  simplehdr->dquant = gst_bit_reader_get_bits_uint8_unchecked (&br, 2);
  simplehdr->vstransform = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);

  /* Skipping reserved5 bit */
  gst_bit_reader_skip_unchecked (&br, 1);

  simplehdr->overlap = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  simplehdr->syncmarker = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  simplehdr->rangered = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  simplehdr->maxbframes = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  simplehdr->quantizer = gst_bit_reader_get_bits_uint8_unchecked (&br, 2);
  seqhdr->finterpflag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);

  GST_DEBUG ("frmrtq_postproc %u, bitrtq_postproc %u, loop_filter %u, "
      "multires %u, fastuvmc %u, extended_mv %u, dquant %u, vstransform %u, "
      "overlap %u, syncmarker %u, rangered %u, maxbframes %u, quantizer %u, "
      "finterpflag %u", seqhdr->frmrtq_postproc, seqhdr->bitrtq_postproc,
      simplehdr->loop_filter, simplehdr->multires, simplehdr->fastuvmc,
      simplehdr->extended_mv, simplehdr->dquant, simplehdr->vstransform,
      simplehdr->overlap, simplehdr->syncmarker, simplehdr->rangered,
      simplehdr->maxbframes, simplehdr->quantizer, seqhdr->finterpflag);

  if (simplehdr->wmvp) {
    if (gst_bit_reader_get_remaining (&br) < 29)
      goto failed;

    simplehdr->coded_width = gst_bit_reader_get_bits_uint16_unchecked (&br, 11);
    simplehdr->coded_height =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 11);
    simplehdr->framerate = gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
    gst_bit_reader_skip_unchecked (&br, 1);
    simplehdr->slice_code = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);

    GST_DEBUG ("coded_width %u, coded_height %u, framerate %u slice_code %u",
        simplehdr->coded_width, simplehdr->coded_height, simplehdr->framerate,
        simplehdr->slice_code);
  }

  return GST_VC1_PARSER_OK;

failed:
  GST_WARNING ("Failed to parse sequence header");

  return GST_VC1_PARSER_ERROR;
}

/**
 * gst_vc1_parse_entry_point_header:
 * @data: The data to parse
 * @size: the size of @data
 * @entrypoint: (out): The #GstVC1EntryPointHdr to set.
 * @seqhdr: The #GstVC1SeqHdr currently being parsed
 *
 * Parses @data, and sets @entrypoint fields.
 *
 * Returns: a #GstVC1EntryPointHdr
 */
GstVC1ParseResult
gst_vc1_parse_entry_point_header (const guint8 * data, gsize size,
    GstVC1EntryPointHdr * entrypoint, GstVC1SeqHdr * seqhdr)
{
  GstBitReader br;
  guint8 i;
  GstVC1AdvancedSeqHdr *advanced = &seqhdr->profile.advanced;

  g_return_val_if_fail (entrypoint != NULL, GST_VC1_PARSER_ERROR);

  ensure_debug_category ();

  gst_bit_reader_init (&br, data, size);

  if (gst_bit_reader_get_remaining (&br) < 13)
    goto failed;

  entrypoint->broken_link = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->closed_entry = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->panscan_flag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->refdist_flag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->loopfilter = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->fastuvmc = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->extended_mv = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->dquant = gst_bit_reader_get_bits_uint8_unchecked (&br, 2);
  entrypoint->vstransform = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->overlap = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  entrypoint->quantizer = gst_bit_reader_get_bits_uint8_unchecked (&br, 2);

  if (advanced->hrd_param_flag) {
    for (i = 0; i < seqhdr->profile.advanced.hrd_param.hrd_num_leaky_buckets;
        i++)
      READ_UINT8 (&br, entrypoint->hrd_full[MAX_HRD_NUM_LEAKY_BUCKETS], 8);
  }

  READ_UINT8 (&br, entrypoint->coded_size_flag, 1);
  if (entrypoint->coded_size_flag) {
    READ_UINT16 (&br, entrypoint->coded_width, 12);
    READ_UINT16 (&br, entrypoint->coded_height, 12);
    entrypoint->coded_height = (entrypoint->coded_height + 1) << 1;
    entrypoint->coded_width = (entrypoint->coded_width + 1) << 1;
  }

  if (entrypoint->extended_mv)
    READ_UINT8 (&br, entrypoint->extended_dmv, 1);

  READ_UINT8 (&br, entrypoint->range_mapy_flag, 1);
  if (entrypoint->range_mapy_flag)
    READ_UINT8 (&br, entrypoint->range_mapy, 3);

  READ_UINT8 (&br, entrypoint->range_mapuv_flag, 1);
  if (entrypoint->range_mapy_flag)
    READ_UINT8 (&br, entrypoint->range_mapuv, 3);

  advanced->entrypoint = *entrypoint;

  return GST_VC1_PARSER_OK;

failed:
  GST_WARNING ("Failed to parse entry point header");

  return GST_VC1_PARSER_ERROR;
}

/**
 * gst_vc1_parse_frame_header:
 * @data: The data to parse
 * @size: the size of @data
 * @entrypoint: The #GstVC1EntryPointHdr to set.
 * @seqhdr: The #GstVC1SeqHdr currently being parsed
 *
 * Parses @data, and fills @entrypoint fields.
 *
 * Returns: a #GstVC1EntryPointHdr
 */
GstVC1ParseResult
gst_vc1_parse_frame_header (const guint8 * data, gsize size,
    GstVC1FrameHdr * framehdr, GstVC1SeqHdr * seqhdr)
{
  GstBitReader br;

  ensure_debug_category ();

  gst_bit_reader_init (&br, data, size);

  if (seqhdr->profiletype == GST_VC1_PROFILE_ADVANCED)
    return parse_frame_header_advanced (&br, framehdr, seqhdr);
  else
    return parse_frame_header (&br, framehdr, seqhdr);

}
