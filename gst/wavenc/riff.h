/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_RIFF_H__
#define __GST_RIFF_H__


#include <gst/gst.h>

typedef enum {
  GST_RIFF_OK 	    =  0,		
  GST_RIFF_ENOTRIFF = -1,
  GST_RIFF_EINVAL   = -2,
  GST_RIFF_ENOMEM   = -3
} GstRiffReturn;

#define MAKE_FOUR_CC(a,b,c,d) GST_MAKE_FOURCC(a,b,c,d)

/* RIFF types */
#define GST_RIFF_RIFF_WAVE MAKE_FOUR_CC('W','A','V','E')
#define GST_RIFF_RIFF_AVI  MAKE_FOUR_CC('A','V','I',' ')

/* tags */
#define GST_RIFF_TAG_RIFF MAKE_FOUR_CC('R','I','F','F')
#define GST_RIFF_TAG_RIFX MAKE_FOUR_CC('R','I','F','X')
#define GST_RIFF_TAG_LIST MAKE_FOUR_CC('L','I','S','T')
#define GST_RIFF_TAG_avih MAKE_FOUR_CC('a','v','i','h')
#define GST_RIFF_TAG_strd MAKE_FOUR_CC('s','t','r','d')
#define GST_RIFF_TAG_strn MAKE_FOUR_CC('s','t','r','n')
#define GST_RIFF_TAG_strh MAKE_FOUR_CC('s','t','r','h')
#define GST_RIFF_TAG_strf MAKE_FOUR_CC('s','t','r','f')
#define GST_RIFF_TAG_vedt MAKE_FOUR_CC('v','e','d','t')
#define GST_RIFF_TAG_JUNK MAKE_FOUR_CC('J','U','N','K')
#define GST_RIFF_TAG_idx1 MAKE_FOUR_CC('i','d','x','1')
#define GST_RIFF_TAG_dmlh MAKE_FOUR_CC('d','m','l','h')
/* WAV stuff */
#define GST_RIFF_TAG_fmt  MAKE_FOUR_CC('f','m','t',' ')
#define GST_RIFF_TAG_data MAKE_FOUR_CC('d','a','t','a')
#define GST_RIFF_TAG_plst MAKE_FOUR_CC('p','l','s','t')
#define GST_RIFF_TAG_cue  MAKE_FOUR_CC('c','u','e',' ')

/* LIST types */
#define GST_RIFF_LIST_movi MAKE_FOUR_CC('m','o','v','i')
#define GST_RIFF_LIST_hdrl MAKE_FOUR_CC('h','d','r','l')
#define GST_RIFF_LIST_strl MAKE_FOUR_CC('s','t','r','l')
#define GST_RIFF_LIST_INFO MAKE_FOUR_CC('I','N','F','O')
#define GST_RIFF_LIST_adtl MAKE_FOUR_CC('a','d','t','l')

/* adtl types */
#define GST_RIFF_adtl_ltxt MAKE_FOUR_CC('l','t','x','t')
#define GST_RIFF_adtl_labl MAKE_FOUR_CC('l','a','b','l')
#define GST_RIFF_adtl_note MAKE_FOUR_CC('n','o','t','e')

/* fcc types */
#define GST_RIFF_FCC_vids MAKE_FOUR_CC('v','i','d','s')
#define GST_RIFF_FCC_auds MAKE_FOUR_CC('a','u','d','s')
#define GST_RIFF_FCC_pads MAKE_FOUR_CC('p','a','d','s')
#define GST_RIFF_FCC_txts MAKE_FOUR_CC('t','x','t','s')
#define GST_RIFF_FCC_vidc MAKE_FOUR_CC('v','i','d','c')
#define GST_RIFF_FCC_iavs MAKE_FOUR_CC('i','a','v','s')
/* fcc handlers */
#define GST_RIFF_FCCH_RLE  MAKE_FOUR_CC('R','L','E',' ')
#define GST_RIFF_FCCH_msvc MAKE_FOUR_CC('m','s','v','c')
#define GST_RIFF_FCCH_MSVC MAKE_FOUR_CC('M','S','V','C')

/* INFO types - see http://www.saettler.com/RIFFMCI/riffmci.html */
#define GST_RIFF_INFO_IARL MAKE_FOUR_CC('I','A','R','L') /* location */
#define GST_RIFF_INFO_IART MAKE_FOUR_CC('I','A','R','T') /* artist */
#define GST_RIFF_INFO_ICMS MAKE_FOUR_CC('I','C','M','S') /* commissioned */
#define GST_RIFF_INFO_ICMT MAKE_FOUR_CC('I','C','M','T') /* comment */
#define GST_RIFF_INFO_ICOP MAKE_FOUR_CC('I','C','O','P') /* copyright */
#define GST_RIFF_INFO_ICRD MAKE_FOUR_CC('I','C','R','D') /* creation date */
#define GST_RIFF_INFO_ICRP MAKE_FOUR_CC('I','C','R','P') /* cropped */
#define GST_RIFF_INFO_IDIM MAKE_FOUR_CC('I','D','I','M') /* dimensions */
#define GST_RIFF_INFO_IDPI MAKE_FOUR_CC('I','D','P','I') /* dots-per-inch */
#define GST_RIFF_INFO_IENG MAKE_FOUR_CC('I','E','N','G') /* engineer(s) */
#define GST_RIFF_INFO_IGNR MAKE_FOUR_CC('I','G','N','R') /* genre */
#define GST_RIFF_INFO_IKEY MAKE_FOUR_CC('I','K','E','Y') /* keywords */
#define GST_RIFF_INFO_ILGT MAKE_FOUR_CC('I','L','G','T') /* lightness */
#define GST_RIFF_INFO_IMED MAKE_FOUR_CC('I','M','E','D') /* medium */
#define GST_RIFF_INFO_INAM MAKE_FOUR_CC('I','N','A','M') /* name */
#define GST_RIFF_INFO_IPLT MAKE_FOUR_CC('I','P','L','T') /* palette setting */
#define GST_RIFF_INFO_IPRD MAKE_FOUR_CC('I','P','R','D') /* product */
#define GST_RIFF_INFO_ISBJ MAKE_FOUR_CC('I','S','B','J') /* subject */
#define GST_RIFF_INFO_ISFT MAKE_FOUR_CC('I','S','F','T') /* software */
#define GST_RIFF_INFO_ISHP MAKE_FOUR_CC('I','S','H','P') /* sharpness */
#define GST_RIFF_INFO_ISRC MAKE_FOUR_CC('I','S','R','C') /* source */
#define GST_RIFF_INFO_ISRF MAKE_FOUR_CC('I','S','R','F') /* source form */
#define GST_RIFF_INFO_ITCH MAKE_FOUR_CC('I','T','C','H') /* technician(s) */

/*********Chunk Names***************/
#define GST_RIFF_FF00 MAKE_FOUR_CC(0xFF,0xFF,0x00,0x00)
#define GST_RIFF_00   MAKE_FOUR_CC( '0', '0',0x00,0x00)
#define GST_RIFF_01   MAKE_FOUR_CC( '0', '1',0x00,0x00)
#define GST_RIFF_02   MAKE_FOUR_CC( '0', '2',0x00,0x00)
#define GST_RIFF_03   MAKE_FOUR_CC( '0', '3',0x00,0x00)
#define GST_RIFF_04   MAKE_FOUR_CC( '0', '4',0x00,0x00)
#define GST_RIFF_05   MAKE_FOUR_CC( '0', '5',0x00,0x00)
#define GST_RIFF_06   MAKE_FOUR_CC( '0', '6',0x00,0x00)
#define GST_RIFF_07   MAKE_FOUR_CC( '0', '7',0x00,0x00)
#define GST_RIFF_00pc MAKE_FOUR_CC( '0', '0', 'p', 'c')
#define GST_RIFF_01pc MAKE_FOUR_CC( '0', '1', 'p', 'c')
#define GST_RIFF_00dc MAKE_FOUR_CC( '0', '0', 'd', 'c')
#define GST_RIFF_00dx MAKE_FOUR_CC( '0', '0', 'd', 'x')
#define GST_RIFF_00db MAKE_FOUR_CC( '0', '0', 'd', 'b')
#define GST_RIFF_00xx MAKE_FOUR_CC( '0', '0', 'x', 'x')
#define GST_RIFF_00id MAKE_FOUR_CC( '0', '0', 'i', 'd')
#define GST_RIFF_00rt MAKE_FOUR_CC( '0', '0', 'r', 't')
#define GST_RIFF_0021 MAKE_FOUR_CC( '0', '0', '2', '1')
#define GST_RIFF_00iv MAKE_FOUR_CC( '0', '0', 'i', 'v')
#define GST_RIFF_0031 MAKE_FOUR_CC( '0', '0', '3', '1')
#define GST_RIFF_0032 MAKE_FOUR_CC( '0', '0', '3', '2')
#define GST_RIFF_00vc MAKE_FOUR_CC( '0', '0', 'v', 'c')
#define GST_RIFF_00xm MAKE_FOUR_CC( '0', '0', 'x', 'm')
#define GST_RIFF_01wb MAKE_FOUR_CC( '0', '1', 'w', 'b')
#define GST_RIFF_01dc MAKE_FOUR_CC( '0', '1', 'd', 'c')
#define GST_RIFF_00__ MAKE_FOUR_CC( '0', '0', '_', '_')

/*********VIDEO CODECS**************/
#define GST_RIFF_cram MAKE_FOUR_CC( 'c', 'r', 'a', 'm')
#define GST_RIFF_CRAM MAKE_FOUR_CC( 'C', 'R', 'A', 'M')
#define GST_RIFF_wham MAKE_FOUR_CC( 'w', 'h', 'a', 'm')
#define GST_RIFF_WHAM MAKE_FOUR_CC( 'W', 'H', 'A', 'M')
#define GST_RIFF_rgb  MAKE_FOUR_CC(0x00,0x00,0x00,0x00)
#define GST_RIFF_RGB  MAKE_FOUR_CC( 'R', 'G', 'B', ' ')
#define GST_RIFF_rle8 MAKE_FOUR_CC(0x01,0x00,0x00,0x00)
#define GST_RIFF_RLE8 MAKE_FOUR_CC( 'R', 'L', 'E', '8')
#define GST_RIFF_rle4 MAKE_FOUR_CC(0x02,0x00,0x00,0x00)
#define GST_RIFF_RLE4 MAKE_FOUR_CC( 'R', 'L', 'E', '4')
#define GST_RIFF_none MAKE_FOUR_CC(0x00,0x00,0xFF,0xFF)
#define GST_RIFF_NONE MAKE_FOUR_CC( 'N', 'O', 'N', 'E')
#define GST_RIFF_pack MAKE_FOUR_CC(0x01,0x00,0xFF,0xFF)
#define GST_RIFF_PACK MAKE_FOUR_CC( 'P', 'A', 'C', 'K')
#define GST_RIFF_tran MAKE_FOUR_CC(0x02,0x00,0xFF,0xFF)
#define GST_RIFF_TRAN MAKE_FOUR_CC( 'T', 'R', 'A', 'N')
#define GST_RIFF_ccc  MAKE_FOUR_CC(0x03,0x00,0xFF,0xFF)
#define GST_RIFF_CCC  MAKE_FOUR_CC( 'C', 'C', 'C', ' ')
#define GST_RIFF_cyuv MAKE_FOUR_CC( 'c', 'y', 'u', 'v')
#define GST_RIFF_CYUV MAKE_FOUR_CC( 'C', 'Y', 'U', 'V')
#define GST_RIFF_jpeg MAKE_FOUR_CC(0x04,0x00,0xFF,0xFF)
#define GST_RIFF_JPEG MAKE_FOUR_CC( 'J', 'P', 'E', 'G')
#define GST_RIFF_MJPG MAKE_FOUR_CC( 'M', 'J', 'P', 'G')
#define GST_RIFF_mJPG MAKE_FOUR_CC( 'm', 'J', 'P', 'G')
#define GST_RIFF_IJPG MAKE_FOUR_CC( 'I', 'J', 'P', 'G')
#define GST_RIFF_rt21 MAKE_FOUR_CC( 'r', 't', '2', '1')
#define GST_RIFF_RT21 MAKE_FOUR_CC( 'R', 'T', '2', '1')
#define GST_RIFF_iv31 MAKE_FOUR_CC( 'i', 'v', '3', '1')
#define GST_RIFF_IV31 MAKE_FOUR_CC( 'I', 'V', '3', '1')
#define GST_RIFF_iv32 MAKE_FOUR_CC( 'i', 'v', '3', '2')
#define GST_RIFF_IV32 MAKE_FOUR_CC( 'I', 'V', '3', '2')
#define GST_RIFF_iv41 MAKE_FOUR_CC( 'i', 'v', '4', '1')
#define GST_RIFF_IV41 MAKE_FOUR_CC( 'I', 'V', '4', '1')
#define GST_RIFF_iv50 MAKE_FOUR_CC( 'i', 'v', '5', '0')
#define GST_RIFF_IV50 MAKE_FOUR_CC( 'I', 'V', '5', '0')
#define GST_RIFF_cvid MAKE_FOUR_CC( 'c', 'v', 'i', 'd')
#define GST_RIFF_CVID MAKE_FOUR_CC( 'C', 'V', 'I', 'D')
#define GST_RIFF_ULTI MAKE_FOUR_CC( 'U', 'L', 'T', 'I')
#define GST_RIFF_ulti MAKE_FOUR_CC( 'u', 'l', 't', 'i')
#define GST_RIFF_YUV9 MAKE_FOUR_CC( 'Y', 'V', 'U', '9')
#define GST_RIFF_YVU9 MAKE_FOUR_CC( 'Y', 'U', 'V', '9')
#define GST_RIFF_XMPG MAKE_FOUR_CC( 'X', 'M', 'P', 'G')
#define GST_RIFF_xmpg MAKE_FOUR_CC( 'x', 'm', 'p', 'g')
#define GST_RIFF_VDOW MAKE_FOUR_CC( 'V', 'D', 'O', 'W')
#define GST_RIFF_MVI1 MAKE_FOUR_CC( 'M', 'V', 'I', '1')
#define GST_RIFF_v422 MAKE_FOUR_CC( 'v', '4', '2', '2')
#define GST_RIFF_V422 MAKE_FOUR_CC( 'V', '4', '2', '2')
#define GST_RIFF_mvi1 MAKE_FOUR_CC( 'm', 'v', 'i', '1')
#define GST_RIFF_MPIX MAKE_FOUR_CC(0x04,0x00, 'i', '1')     /* MotionPixels munged their id */
#define GST_RIFF_AURA MAKE_FOUR_CC( 'A', 'U', 'R', 'A')
#define GST_RIFF_DMB1 MAKE_FOUR_CC( 'D', 'M', 'B', '1')
#define GST_RIFF_dmb1 MAKE_FOUR_CC( 'd', 'm', 'b', '1')

#define GST_RIFF_BW10 MAKE_FOUR_CC( 'B', 'W', '1', '0')
#define GST_RIFF_bw10 MAKE_FOUR_CC( 'b', 'w', '1', '0')

#define GST_RIFF_yuy2 MAKE_FOUR_CC( 'y', 'u', 'y', '2')
#define GST_RIFF_YUY2 MAKE_FOUR_CC( 'Y', 'U', 'Y', '2')
#define GST_RIFF_YUV8 MAKE_FOUR_CC( 'Y', 'U', 'V', '8')
#define GST_RIFF_WINX MAKE_FOUR_CC( 'W', 'I', 'N', 'X')
#define GST_RIFF_WPY2 MAKE_FOUR_CC( 'W', 'P', 'Y', '2')
#define GST_RIFF_m263 MAKE_FOUR_CC( 'm', '2', '6', '3')
#define GST_RIFF_M263 MAKE_FOUR_CC( 'M', '2', '6', '3')

#define GST_RIFF_Q1_0 MAKE_FOUR_CC( 'Q', '1',0x2e, '0')
#define GST_RIFF_SFMC MAKE_FOUR_CC( 'S', 'F', 'M', 'C')

#define GST_RIFF_y41p MAKE_FOUR_CC( 'y', '4', '1', 'p')
#define GST_RIFF_Y41P MAKE_FOUR_CC( 'Y', '4', '1', 'P')
#define GST_RIFF_yv12 MAKE_FOUR_CC( 'y', 'v', '1', '2')
#define GST_RIFF_YV12 MAKE_FOUR_CC( 'Y', 'V', '1', '2')
#define GST_RIFF_vixl MAKE_FOUR_CC( 'v', 'i', 'x', 'l')
#define GST_RIFF_VIXL MAKE_FOUR_CC( 'V', 'I', 'X', 'L')
#define GST_RIFF_iyuv MAKE_FOUR_CC( 'i', 'y', 'u', 'v')
#define GST_RIFF_IYUV MAKE_FOUR_CC( 'I', 'Y', 'U', 'V')
#define GST_RIFF_i420 MAKE_FOUR_CC( 'i', '4', '2', '0')
#define GST_RIFF_I420 MAKE_FOUR_CC( 'I', '4', '2', '0')
#define GST_RIFF_vyuy MAKE_FOUR_CC( 'v', 'y', 'u', 'y')
#define GST_RIFF_VYUY MAKE_FOUR_CC( 'V', 'Y', 'U', 'Y')

#define GST_RIFF_DIV3 MAKE_FOUR_CC( 'D', 'I', 'V', '3')

#define GST_RIFF_rpza MAKE_FOUR_CC( 'r', 'p', 'z', 'a')
/* And this here's the mistakes that need to be supported */
#define GST_RIFF_azpr MAKE_FOUR_CC( 'a', 'z', 'p', 'r')  /* recognize Apple's rpza mangled? */

/*********** FND in MJPG **********/
#define GST_RIFF_ISFT MAKE_FOUR_CC( 'I', 'S', 'F', 'T')
#define GST_RIFF_IDIT MAKE_FOUR_CC( 'I', 'D', 'I', 'T')

#define GST_RIFF_00AM MAKE_FOUR_CC( '0', '0', 'A', 'M')
#define GST_RIFF_DISP MAKE_FOUR_CC( 'D', 'I', 'S', 'P')
#define GST_RIFF_ISBJ MAKE_FOUR_CC( 'I', 'S', 'B', 'J')

#define GST_RIFF_rec  MAKE_FOUR_CC( 'r', 'e', 'c', ' ')

/* common data structures */
struct _gst_riff_avih {
  guint32 us_frame;          /* microsec per frame */
  guint32 max_bps;           /* byte/s overall */
  guint32 pad_gran;          /* pad_gran (???) */
  guint32 flags;
/* flags values */
#define GST_RIFF_AVIH_HASINDEX       0x00000010 /* has idx1 chunk */
#define GST_RIFF_AVIH_MUSTUSEINDEX   0x00000020 /* must use idx1 chunk to determine order */
#define GST_RIFF_AVIH_ISINTERLEAVED  0x00000100 /* AVI file is interleaved */
#define GST_RIFF_AVIH_WASCAPTUREFILE 0x00010000 /* specially allocated used for capturing real time video */
#define GST_RIFF_AVIH_COPYRIGHTED    0x00020000 /* contains copyrighted data */
  guint32 tot_frames;        /* # of frames (all) */
  guint32 init_frames;       /* initial frames (???) */
  guint32 streams;
  guint32 bufsize;           /* suggested buffer size */
  guint32 width;
  guint32 height;
  guint32 scale;
  guint32 rate;
  guint32 start;
  guint32 length;
};

struct _gst_riff_strh {
  guint32 type;             /* stream type */
  guint32 fcc_handler;       /* fcc_handler */
  guint32 flags;
/* flags values */
#define GST_RIFF_STRH_DISABLED        0x000000001
#define GST_RIFF_STRH_VIDEOPALCHANGES 0x000010000
  guint32 priority;
  guint32 init_frames;       /* initial frames (???) */
  guint32 scale;
  guint32 rate;
  guint32 start;
  guint32 length;
  guint32 bufsize;           /* suggested buffer size */
  guint32 quality;
  guint32 samplesize;
  /* XXX 16 bytes ? */
};

struct _gst_riff_strf_vids {       /* == BitMapInfoHeader */
  guint32 size;
  guint32 width;
  guint32 height;
  guint16 planes;
  guint16 bit_cnt;
  guint32 compression;
  guint32 image_size;
  guint32 xpels_meter;
  guint32 ypels_meter;
  guint32 num_colors;        /* used colors */
  guint32 imp_colors;        /* important colors */
  /* may be more for some codecs */
};


struct _gst_riff_strf_auds {       /* == WaveHeader (?) */
  guint16 format;
/**** from public Microsoft RIFF docs ******/
#define GST_RIFF_WAVE_FORMAT_UNKNOWN        (0x0000)
#define GST_RIFF_WAVE_FORMAT_PCM            (0x0001)
#define GST_RIFF_WAVE_FORMAT_ADPCM          (0x0002)
#define GST_RIFF_WAVE_FORMAT_IBM_CVSD       (0x0005)
#define GST_RIFF_WAVE_FORMAT_ALAW           (0x0006)
#define GST_RIFF_WAVE_FORMAT_MULAW          (0x0007)
#define GST_RIFF_WAVE_FORMAT_OKI_ADPCM      (0x0010)
#define GST_RIFF_WAVE_FORMAT_DVI_ADPCM      (0x0011)
#define GST_RIFF_WAVE_FORMAT_DIGISTD        (0x0015)
#define GST_RIFF_WAVE_FORMAT_DIGIFIX        (0x0016)
#define GST_RIFF_WAVE_FORMAT_YAMAHA_ADPCM   (0x0020)
#define GST_RIFF_WAVE_FORMAT_DSP_TRUESPEECH (0x0022)
#define GST_RIFF_WAVE_FORMAT_GSM610         (0x0031)
#define GST_RIFF_WAVE_FORMAT_MSN            (0x0032)
#define GST_RIFF_WAVE_FORMAT_MPEGL12        (0x0050)
#define GST_RIFF_WAVE_FORMAT_MPEGL3         (0x0055)
#define GST_RIFF_IBM_FORMAT_MULAW           (0x0101)
#define GST_RIFF_IBM_FORMAT_ALAW            (0x0102)
#define GST_RIFF_IBM_FORMAT_ADPCM           (0x0103)
#define GST_RIFF_WAVE_FORMAT_DIVX_WMAV1     (0x0160)
#define GST_RIFF_WAVE_FORMAT_DIVX_WMAV2     (0x0161)
#define GST_RIFF_WAVE_FORMAT_WMAV9          (0x0162)
#define GST_RIFF_WAVE_FORMAT_A52	    (0x2000)
#define GST_RIFF_WAVE_FORMAT_VORBIS1        (0x674f)
#define GST_RIFF_WAVE_FORMAT_VORBIS2        (0x6750)
#define GST_RIFF_WAVE_FORMAT_VORBIS3        (0x6751)
#define GST_RIFF_WAVE_FORMAT_VORBIS1PLUS    (0x676f)
#define GST_RIFF_WAVE_FORMAT_VORBIS2PLUS    (0x6770)
#define GST_RIFF_WAVE_FORMAT_VORBIS3PLUS    (0x6771)
  guint16 channels;
  guint32 rate;
  guint32 av_bps;
  guint16 blockalign;
  guint16 size;
};

struct _gst_riff_strf_iavs {    
  guint32 DVAAuxSrc;
  guint32 DVAAuxCtl;
  guint32 DVAAuxSrc1;
  guint32 DVAAuxCtl1;
  guint32 DVVAuxSrc;
  guint32 DVVAuxCtl;
  guint32 DVReserved1;
  guint32 DVReserved2;
};

struct _gst_riff_riff {  
  guint32 id;
  guint32 size;
  guint32 type;
};

struct _gst_riff_list {  
  guint32 id;
  guint32 size;
  guint32 type;
};

struct _gst_riff_labl {
  guint32 id;
  guint32 size;

  guint32 identifier;
};

struct _gst_riff_ltxt {
  guint32 id;
  guint32 size;

  guint32 identifier;
  guint32 length;
  guint32 purpose;
  guint16 country;
  guint16 language;
  guint16 dialect;
  guint16 codepage;
};

struct _gst_riff_note {
  guint32 id;
  guint32 size;

  guint32 identifier;
};

struct _gst_riff_cuepoints {
  guint32 identifier;
  guint32 position;
  guint32 id;
  guint32 chunkstart;
  guint32 blockstart;
  guint32 offset;
};

struct _gst_riff_cue {
  guint32 id;
  guint32 size;

  guint32 cuepoints; /* Number of cue points held in the data */
};

struct _gst_riff_chunk {  
  guint32 id;
  guint32 size;
};

struct _gst_riff_index_entry {  
  guint32 id;
  guint32 flags;
#define GST_RIFF_IF_LIST		(0x00000001L)
#define GST_RIFF_IF_KEYFRAME		(0x00000010L)
#define GST_RIFF_IF_NO_TIME		(0x00000100L)
#define GST_RIFF_IF_COMPUSE		(0x0FFF0000L)
  guint32 offset;
  guint32 size;
};

struct _gst_riff_dmlh {
  guint32 totalframes;
};

typedef struct _gst_riff_riff 		gst_riff_riff;
typedef struct _gst_riff_list 		gst_riff_list;
typedef struct _gst_riff_chunk 		gst_riff_chunk;
typedef struct _gst_riff_index_entry 	gst_riff_index_entry;

typedef struct _gst_riff_avih 		gst_riff_avih;
typedef struct _gst_riff_strh 		gst_riff_strh;
typedef struct _gst_riff_strf_vids 	gst_riff_strf_vids;
typedef struct _gst_riff_strf_auds 	gst_riff_strf_auds;
typedef struct _gst_riff_strf_iavs 	gst_riff_strf_iavs;
typedef struct _gst_riff_dmlh           gst_riff_dmlh;
typedef struct _GstRiffChunk 		GstRiffChunk;

struct _GstRiffChunk {
  gulong offset;

  guint32 id;
  guint32 size;
  guint32 form; /* for list chunks */

  gchar *data;
};

#endif /* __GST_RIFF_H__ */
