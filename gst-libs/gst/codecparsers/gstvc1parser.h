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

#ifndef __GST_VC1_PARSER_H__
#define __GST_VC1_PARSER_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The VC1 parsing library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

#define MAX_HRD_NUM_LEAKY_BUCKETS 31

/**
 * @GST_VC1_BFRACTION_BASIS: The @bfraction variable should be divided
 * by this constant to have the actual value.
 */
#define GST_VC1_BFRACTION_BASIS 256

typedef enum {
  GST_VC1_END_OF_SEQ       = 0x0A,
  GST_VC1_SLICE            = 0x0B,
  GST_VC1_FIELD            = 0x0C,
  GST_VC1_FRAME            = 0x0D,
  GST_VC1_ENTRYPOINT       = 0x0E,
  GST_VC1_SEQUENCE         = 0x0F,
  GST_VC1_SLICE_USER       = 0x1B,
  GST_VC1_FIELD_USER       = 0x1C,
  GST_VC1_FRAME_USER       = 0x1D,
  GST_VC1_ENTRY_POINT_USER = 0x1E,
  GST_VC1_SEQUENCE_USER    = 0x1F
} GstVC1StartCode;

typedef enum {
  GST_VC1_PROFILE_SIMPLE,
  GST_VC1_PROFILE_MAIN,
  GST_VC1_PROFILE_RESERVED,
  GST_VC1_PROFILE_ADVANCED
} GstVC1Profile;

typedef enum {
  GST_VC1_PARSER_OK,
  GST_VC1_PARSER_BROKEN_DATA,
  GST_VC1_PARSER_NO_BDU,
  GST_VC1_PARSER_NO_BDU_END,
  GST_VC1_PARSER_ERROR,
} GstVC1ParseResult;

typedef enum
{
  GST_VC1_PICTURE_TYPE_P,
  GST_VC1_PICTURE_TYPE_B,
  GST_VC1_PICTURE_TYPE_I,
  GST_VC1_PICTURE_TYPE_BI,
  GST_VC1_PICTURE_TYPE_SKIPPED
} GstVC1PictureType;

typedef enum
{
    GST_VC1_LEVEL_LOW   = 0,    /* Simple/Main profile low level */
    GST_VC1_LEVELMEDIUM = 1,    /* Simple/Main profile medium level */
    GST_VC1_LEVELHIGH   = 2,    /* Main profile high level */

    GST_VC1_LEVEL_L0    = 0,    /* Advanced profile level 0 */
    GST_VC1_LEVEL_L1    = 1,    /* Advanced profile level 1 */
    GST_VC1_LEVEL_L2    = 2,    /* Advanced profile level 2 */
    GST_VC1_LEVEL_L3    = 3,    /* Advanced profile level 3 */
    GST_VC1_LEVEL_L4    = 4,    /* Advanced profile level 4 */

    /* 5 to 7 reserved */
    GST_VC1_LEVEL_UNKNOWN = 255  /* Unknown profile */
} GstVC1Level;

typedef enum
{
  GST_VC1_QUANTIZER_IMPLICITLY,
  GST_VC1_QUANTIZER_EXPLICITLY,
  GST_VC1_QUANTIZER_NON_UNIFORM,
  GST_VC1_QUANTIZER_UNIFORM
} GstVC1QuantizerSpec;

typedef enum {
  GST_VC1_DQPROFILE_FOUR_EDGES,
  GST_VC1_DQPROFILE_DOUBLE_EDGES,
  GST_VC1_DQPROFILE_SINGLE_EDGE,
  GST_VC1_DQPROFILE_ALL_MBS
} GstVC1DQProfile;

typedef enum {
  GST_VC1_CONDOVER_NONE,
  GST_VC1_CONDOVER_ALL,
  GST_VC1_CONDOVER_SELECT
} GstVC1Condover;

/**
 * GstVC1MvMode:
 *
 */
typedef enum
{
  GST_VC1_MVMODE_1MV_HPEL_BILINEAR,
  GST_VC1_MVMODE_1MV,
  GST_VC1_MVMODE_1MV_HPEL,
  GST_VC1_MVMODE_MIXED_MV,
  GST_VC1_MVMODE_INTENSITY_COMP
} GstVC1MvMode;

typedef struct _GstVC1SeqHdr            GstVC1SeqHdr;
typedef struct _GstVC1AdvancedSeqHdr    GstVC1AdvancedSeqHdr;
typedef struct _GstVC1SimpleMainSeqHdr  GstVC1SimpleMainSeqHdr;
typedef struct _GstVC1HrdParam          GstVC1HrdParam;
typedef struct _GstVC1EntryPointHdr     GstVC1EntryPointHdr;

/* Pictures Structures */
typedef struct _GstVC1FrameHdr          GstVC1FrameHdr;
typedef struct _GstVC1PicAdvanced       GstVC1PicAdvanced;
typedef struct _GstVC1PicSimpleMain     GstVC1PicSimpleMain;
typedef struct _GstVC1Picture           GstVC1Picture;

typedef struct _GstVC1VopDquant         GstVC1VopDquant;

typedef struct _GstVC1BDU               GstVC1BDU;

struct _GstVC1HrdParam
{
  guint8 hrd_num_leaky_buckets;
  guint8 bit_rate_exponent;
  guint8 buffer_size_exponent;
  guint16 hrd_rate[MAX_HRD_NUM_LEAKY_BUCKETS];
  guint16 hrd_buffer[MAX_HRD_NUM_LEAKY_BUCKETS];
};

/**
 * GstVC1SimpleMainSeqHdr:
 *
 * Structure for simple and main profile sequence headers specific parameters.
 */
struct _GstVC1SimpleMainSeqHdr
{
  guint8 res_sprite;
  guint8 loop_filter;
  guint8 multires;
  guint8 fastuvmc;
  guint8 extended_mv;
  guint8 dquant;
  guint8 vstransform;
  guint8 overlap;
  guint8 syncmarker;
  guint8 rangered;
  guint8 maxbframes;
  guint8 quantizer;

  /* This should be filled by user if previously known */
  guint16 coded_width;
  /* This should be filled by user if previously known */
  guint16 coded_height;

  /* Wmvp specific */
  guint8 wmvp;          /* Specify if the stream is wmp or not */
  guint8 framerate;
  guint8 slice_code;
};

/**
 * GstVC1EntryPointHdr:
 *
 * Structure for entrypoint header, this will be used only in advanced profiles
 */
struct _GstVC1EntryPointHdr
{
  guint8 broken_link;
  guint8 closed_entry;
  guint8 panscan_flag;
  guint8 refdist_flag;
  guint8 loopfilter;
  guint8 fastuvmc;
  guint8 extended_mv;
  guint8 dquant;
  guint8 vstransform;
  guint8 overlap;
  guint8 quantizer;
  guint8 coded_size_flag;
  guint16 coded_width;
  guint16 coded_height;
  guint8 extended_dmv;
  guint8 range_mapy_flag;
  guint8 range_mapy;
  guint8 range_mapuv_flag;
  guint8 range_mapuv;

  guint8 hrd_full[MAX_HRD_NUM_LEAKY_BUCKETS];
};

/**
 * GstVC1AdvancedSeqHdr:
 *
 * Structure for the advanced profile sequence headers specific parameters.
 */
struct _GstVC1AdvancedSeqHdr
{
  guint8  level;
  guint8  postprocflag;
  guint16 max_coded_width;
  guint16 max_coded_height;
  guint8  pulldown;
  guint8  interlace;
  guint8  tfcntrflag;
  guint8  psf;
  guint8  display_ext;
  guint16 disp_horiz_size;
  guint16 disp_vert_size;
  guint8  aspect_ratio_flag;
  guint8  aspect_ratio;
  guint8  aspect_horiz_size;
  guint8  aspect_vert_size;
  guint8  framerate_flag;
  guint8  framerateind;
  guint8  frameratenr;
  guint8  frameratedr;
  guint16 framerateexp;
  guint8  color_format_flag;
  guint8  color_prim;
  guint8  transfer_char;
  guint8  matrix_coef;
  guint8  hrd_param_flag;

  GstVC1HrdParam hrd_param;

  /* The last parsed entry point */
  GstVC1EntryPointHdr entrypoint;
};

/**
 * GstVC1SeqHdr:
 *
 * Structure for sequence headers in any profile.
 */
struct _GstVC1SeqHdr
{
  guint8 profiletype;
  guint8 colordiff_format;
  guint8 frmrtq_postproc;
  guint8 bitrtq_postproc;
  guint8 finterpflag;

  /*  calculated */
  guint framerate; /* Around in fps, 0 if unknown*/
  guint bitrate;   /* Around in kpbs, 0 if unknown*/

  union {
    GstVC1AdvancedSeqHdr   advanced;
    GstVC1SimpleMainSeqHdr simplemain;
  } profile;

};

/**
 * GstVC1PicSimpleMain:
 * @bfaction: Should be divided by #GST_VC1_BFRACTION_BASIS
 * to get the real value.
 */
struct _GstVC1PicSimpleMain
{
  guint8 frmcnt;
  guint8 mvrange;
  guint8 rangeredfrm;

  /* I and P pic simple and main profiles only */
  guint8 respic;

  /* I and BI pic simple and main profiles only */
  guint8 transacfrm2;
  guint8 bf;

  /* B and P pic simple and main profiles only */
  guint8 mvmode;
  guint8 mvtab;
  guint8 ttmbf;

  /* P pic simple and main profiles only */
  guint8 mvmode2;
  guint8 lumscale;
  guint8 lumshift;

  guint8 cbptab;
  guint8 ttfrm;

  /* B and BI picture only
   * Should be divided by #GST_VC1_BFRACTION_BASIS
   * to get the real value. */
  guint8 bfraction;

  /* Biplane value, those fields only mention the fact
   * that the bitplane is in raw mode or not */
  guint8 mvtypemb;
  guint8 skipmb;
  guint8 directmb; /* B pic main profile only */
};

/**
 * GstVC1PicAdvanced:
 * @bfaction: Should be divided by #GST_VC1_BFRACTION_BASIS
 * to get the real value.
 */
struct _GstVC1PicAdvanced
{
  guint8  fcm;
  guint8  tfcntr;

  guint8  rptfrm;
  guint8  tff;
  guint8  rff;
  guint8  ps_present;
  guint32 ps_hoffset;
  guint32 ps_voffset;
  guint16 ps_width;
  guint16 ps_height;
  guint8  rndctrl;
  guint8  uvsamp;
  guint8  postproc;

  /*  B and P picture specific */
  guint8  mvrange;
  guint8  mvmode;
  guint8  mvtab;
  guint8  cbptab;
  guint8  ttmbf;
  guint8  ttfrm;

  /* B and BI picture only
   * Should be divided by #GST_VC1_BFRACTION_BASIS
   * to get the real value. */
  guint8  bfraction;

  /* ppic */
  guint8  mvmode2;
  guint8  lumscale;
  guint8  lumshift;

  /* bipic */
  guint8  bf;
  guint8  condover;
  guint8  transacfrm2;

  /* Biplane value, those fields only mention the fact
   * that the bitplane is in raw mode or not */
  guint8  acpred;
  guint8  overflags;
  guint8  mvtypemb;
  guint8  skipmb;
  guint8  directmb;
};


struct _GstVC1VopDquant
{
  guint8 pqdiff;
  guint8 abspq;


  /*  if dqant != 2*/
  guint8 dquantfrm;
  guint8 dqprofile;

  /* if dqprofile == GST_VC1_DQPROFILE_SINGLE_EDGE
   * or GST_VC1_DQPROFILE_DOUBLE_EDGE:*/
  guint8 dqsbedge;

  /* if dqprofile == GST_VC1_DQPROFILE_SINGLE_EDGE
   * or GST_VC1_DQPROFILE_DOUBLE_EDGE:*/
  guint8 dqbedge;

  /* if dqprofile == GST_VC1_DQPROFILE_ALL_MBS */
  guint8 dqbilevel;

};

/**
 * GstVC1FrameHdr:
 *
 * Structure that represent picture in any profile or mode.
 * You should look at @ptype and @profile to know what is currently
 * in use.
 */
struct _GstVC1FrameHdr
{
  /* common fields */
  guint8 ptype;
  guint8 interpfrm;
  guint8 halfqp;
  guint8 transacfrm;
  guint8 transdctab;
  guint8 pqindex;
  guint8 pquantizer;

  /* Computed */
  guint8 pquant;

  /* Convenience fields */
  guint8 profile;
  guint8 dquant;

  /*  If dquant */
  GstVC1VopDquant vopdquant;

  union {
    GstVC1PicSimpleMain simple;
    GstVC1PicAdvanced advanced;
  } pic;
};

/**
 * GstVC1BDU:
 *
 * Structure that represents a Bitstream Data Unit.
 */
struct _GstVC1BDU
{
  GstVC1StartCode type;
  guint size;
  guint sc_offset;
  guint offset;
  guint8 * data;
};

GstVC1ParseResult gst_vc1_identify_next_bdu            (const guint8 *data,
                                                        gsize size,
                                                        GstVC1BDU *bdu);


GstVC1ParseResult gst_vc1_parse_sequence_header        (const guint8 *data,
                                                        gsize size,
                                                        GstVC1SeqHdr * seqhdr);

GstVC1ParseResult gst_vc1_parse_entry_point_header     (const  guint8 *data,
                                                        gsize size,
                                                        GstVC1EntryPointHdr * entrypoint,
                                                        GstVC1SeqHdr *seqhdr);

GstVC1ParseResult gst_vc1_parse_frame_header           (const guint8 *data,
                                                        gsize size,
                                                        GstVC1FrameHdr * framehdr,
                                                        GstVC1SeqHdr *seqhdr);

G_END_DECLS
#endif
