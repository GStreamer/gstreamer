/* GStreamer RIFF I/O
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * riff-ids.h: RIFF IDs and structs
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

#ifndef __GST_RIFF_IDS_H__
#define __GST_RIFF_IDS_H__

#include <gst/gst.h>

/* RIFF types */
#define GST_RIFF_RIFF_WAVE GST_MAKE_FOURCC ('W','A','V','E')
#define GST_RIFF_RIFF_AVI  GST_MAKE_FOURCC ('A','V','I',' ')
#define GST_RIFF_RIFF_CDXA GST_MAKE_FOURCC ('C','D','X','A')

/* tags */
#define GST_RIFF_TAG_RIFF GST_MAKE_FOURCC ('R','I','F','F')
#define GST_RIFF_TAG_RIFX GST_MAKE_FOURCC ('R','I','F','X')
#define GST_RIFF_TAG_LIST GST_MAKE_FOURCC ('L','I','S','T')
#define GST_RIFF_TAG_avih GST_MAKE_FOURCC ('a','v','i','h')
#define GST_RIFF_TAG_strd GST_MAKE_FOURCC ('s','t','r','d')
#define GST_RIFF_TAG_strn GST_MAKE_FOURCC ('s','t','r','n')
#define GST_RIFF_TAG_strh GST_MAKE_FOURCC ('s','t','r','h')
#define GST_RIFF_TAG_strf GST_MAKE_FOURCC ('s','t','r','f')
#define GST_RIFF_TAG_vedt GST_MAKE_FOURCC ('v','e','d','t')
#define GST_RIFF_TAG_JUNK GST_MAKE_FOURCC ('J','U','N','K')
#define GST_RIFF_TAG_idx1 GST_MAKE_FOURCC ('i','d','x','1')
#define GST_RIFF_TAG_dmlh GST_MAKE_FOURCC ('d','m','l','h')
/* WAV stuff */
#define GST_RIFF_TAG_fmt  GST_MAKE_FOURCC ('f','m','t',' ')
#define GST_RIFF_TAG_data GST_MAKE_FOURCC ('d','a','t','a')
#define GST_RIFF_TAG_plst GST_MAKE_FOURCC ('p','l','s','t')
#define GST_RIFF_TAG_cue  GST_MAKE_FOURCC ('c','u','e',' ')
/* LIST types */
#define GST_RIFF_LIST_movi GST_MAKE_FOURCC ('m','o','v','i')
#define GST_RIFF_LIST_hdrl GST_MAKE_FOURCC ('h','d','r','l')
#define GST_RIFF_LIST_odml GST_MAKE_FOURCC ('o','d','m','l')
#define GST_RIFF_LIST_strl GST_MAKE_FOURCC ('s','t','r','l')
#define GST_RIFF_LIST_INFO GST_MAKE_FOURCC ('I','N','F','O')
#define GST_RIFF_LIST_AVIX GST_MAKE_FOURCC ('A','V','I','X')
#define GST_RIFF_LIST_adtl GST_MAKE_FOURCC ('a','d','t','l')

/* fcc types */
#define GST_RIFF_FCC_vids GST_MAKE_FOURCC ('v','i','d','s')
#define GST_RIFF_FCC_auds GST_MAKE_FOURCC ('a','u','d','s')
#define GST_RIFF_FCC_pads GST_MAKE_FOURCC ('p','a','d','s')
#define GST_RIFF_FCC_txts GST_MAKE_FOURCC ('t','x','t','s')
#define GST_RIFF_FCC_vidc GST_MAKE_FOURCC ('v','i','d','c')
#define GST_RIFF_FCC_iavs GST_MAKE_FOURCC ('i','a','v','s')
/* fcc handlers */
#define GST_RIFF_FCCH_RLE  GST_MAKE_FOURCC ('R','L','E',' ')
#define GST_RIFF_FCCH_msvc GST_MAKE_FOURCC ('m','s','v','c')
#define GST_RIFF_FCCH_MSVC GST_MAKE_FOURCC ('M','S','V','C')

/* INFO types - see http://www.saettler.com/RIFFMCI/riffmci.html */
#define GST_RIFF_INFO_IARL GST_MAKE_FOURCC ('I','A','R','L') /* location */
#define GST_RIFF_INFO_IART GST_MAKE_FOURCC ('I','A','R','T') /* artist */
#define GST_RIFF_INFO_ICMS GST_MAKE_FOURCC ('I','C','M','S') /* commissioned */
#define GST_RIFF_INFO_ICMT GST_MAKE_FOURCC ('I','C','M','T') /* comment */
#define GST_RIFF_INFO_ICOP GST_MAKE_FOURCC ('I','C','O','P') /* copyright */
#define GST_RIFF_INFO_ICRD GST_MAKE_FOURCC ('I','C','R','D') /* creation date */
#define GST_RIFF_INFO_ICRP GST_MAKE_FOURCC ('I','C','R','P') /* cropped */
#define GST_RIFF_INFO_IDIM GST_MAKE_FOURCC ('I','D','I','M') /* dimensions */
#define GST_RIFF_INFO_IDPI GST_MAKE_FOURCC ('I','D','P','I') /* dots-per-inch */
#define GST_RIFF_INFO_IENG GST_MAKE_FOURCC ('I','E','N','G') /* engineer(s) */
#define GST_RIFF_INFO_IGNR GST_MAKE_FOURCC ('I','G','N','R') /* genre */
#define GST_RIFF_INFO_IKEY GST_MAKE_FOURCC ('I','K','E','Y') /* keywords */
#define GST_RIFF_INFO_ILGT GST_MAKE_FOURCC ('I','L','G','T') /* lightness */
#define GST_RIFF_INFO_IMED GST_MAKE_FOURCC ('I','M','E','D') /* medium */
#define GST_RIFF_INFO_INAM GST_MAKE_FOURCC ('I','N','A','M') /* name */
#define GST_RIFF_INFO_IPLT GST_MAKE_FOURCC ('I','P','L','T') /* palette setting */
#define GST_RIFF_INFO_IPRD GST_MAKE_FOURCC ('I','P','R','D') /* product */
#define GST_RIFF_INFO_ISBJ GST_MAKE_FOURCC ('I','S','B','J') /* subject */
#define GST_RIFF_INFO_ISFT GST_MAKE_FOURCC ('I','S','F','T') /* software */
#define GST_RIFF_INFO_ISHP GST_MAKE_FOURCC ('I','S','H','P') /* sharpness */
#define GST_RIFF_INFO_ISRC GST_MAKE_FOURCC ('I','S','R','C') /* source */
#define GST_RIFF_INFO_ISRF GST_MAKE_FOURCC ('I','S','R','F') /* source form */
#define GST_RIFF_INFO_ITCH GST_MAKE_FOURCC ('I','T','C','H') /* technician(s) */

/*********Chunk Names***************/
#define GST_RIFF_FF00 GST_MAKE_FOURCC (0xFF,0xFF,0x00,0x00)
#define GST_RIFF_00   GST_MAKE_FOURCC ('0', '0',0x00,0x00)
#define GST_RIFF_01   GST_MAKE_FOURCC ('0', '1',0x00,0x00)
#define GST_RIFF_02   GST_MAKE_FOURCC ('0', '2',0x00,0x00)
#define GST_RIFF_03   GST_MAKE_FOURCC ('0', '3',0x00,0x00)
#define GST_RIFF_04   GST_MAKE_FOURCC ('0', '4',0x00,0x00)
#define GST_RIFF_05   GST_MAKE_FOURCC ('0', '5',0x00,0x00)
#define GST_RIFF_06   GST_MAKE_FOURCC ('0', '6',0x00,0x00)
#define GST_RIFF_07   GST_MAKE_FOURCC ('0', '7',0x00,0x00)
#define GST_RIFF_00pc GST_MAKE_FOURCC ('0', '0', 'p', 'c')
#define GST_RIFF_01pc GST_MAKE_FOURCC ('0', '1', 'p', 'c')
#define GST_RIFF_00dc GST_MAKE_FOURCC ('0', '0', 'd', 'c')
#define GST_RIFF_00dx GST_MAKE_FOURCC ('0', '0', 'd', 'x')
#define GST_RIFF_00db GST_MAKE_FOURCC ('0', '0', 'd', 'b')
#define GST_RIFF_00xx GST_MAKE_FOURCC ('0', '0', 'x', 'x')
#define GST_RIFF_00id GST_MAKE_FOURCC ('0', '0', 'i', 'd')
#define GST_RIFF_00rt GST_MAKE_FOURCC ('0', '0', 'r', 't')
#define GST_RIFF_0021 GST_MAKE_FOURCC ('0', '0', '2', '1')
#define GST_RIFF_00iv GST_MAKE_FOURCC ('0', '0', 'i', 'v')
#define GST_RIFF_0031 GST_MAKE_FOURCC ('0', '0', '3', '1')
#define GST_RIFF_0032 GST_MAKE_FOURCC ('0', '0', '3', '2')
#define GST_RIFF_00vc GST_MAKE_FOURCC ('0', '0', 'v', 'c')
#define GST_RIFF_00xm GST_MAKE_FOURCC ('0', '0', 'x', 'm')
#define GST_RIFF_01wb GST_MAKE_FOURCC ('0', '1', 'w', 'b')
#define GST_RIFF_01dc GST_MAKE_FOURCC ('0', '1', 'd', 'c')
#define GST_RIFF_00__ GST_MAKE_FOURCC ('0', '0', '_', '_')

/*********VIDEO CODECS**************/
#define GST_RIFF_cram GST_MAKE_FOURCC ('c', 'r', 'a', 'm')
#define GST_RIFF_CRAM GST_MAKE_FOURCC ('C', 'R', 'A', 'M')
#define GST_RIFF_wham GST_MAKE_FOURCC ('w', 'h', 'a', 'm')
#define GST_RIFF_WHAM GST_MAKE_FOURCC ('W', 'H', 'A', 'M')
#define GST_RIFF_rgb  GST_MAKE_FOURCC (0x00,0x00,0x00,0x00)
#define GST_RIFF_RGB  GST_MAKE_FOURCC ('R', 'G', 'B', ' ')
#define GST_RIFF_rle8 GST_MAKE_FOURCC (0x01,0x00,0x00,0x00)
#define GST_RIFF_RLE8 GST_MAKE_FOURCC ('R', 'L', 'E', '8')
#define GST_RIFF_rle4 GST_MAKE_FOURCC (0x02,0x00,0x00,0x00)
#define GST_RIFF_RLE4 GST_MAKE_FOURCC ('R', 'L', 'E', '4')
#define GST_RIFF_none GST_MAKE_FOURCC (0x00,0x00,0xFF,0xFF)
#define GST_RIFF_NONE GST_MAKE_FOURCC ('N', 'O', 'N', 'E')
#define GST_RIFF_pack GST_MAKE_FOURCC (0x01,0x00,0xFF,0xFF)
#define GST_RIFF_PACK GST_MAKE_FOURCC ('P', 'A', 'C', 'K')
#define GST_RIFF_tran GST_MAKE_FOURCC (0x02,0x00,0xFF,0xFF)
#define GST_RIFF_TRAN GST_MAKE_FOURCC ('T', 'R', 'A', 'N')
#define GST_RIFF_ccc  GST_MAKE_FOURCC (0x03,0x00,0xFF,0xFF)
#define GST_RIFF_CCC  GST_MAKE_FOURCC ('C', 'C', 'C', ' ')
#define GST_RIFF_cyuv GST_MAKE_FOURCC ('c', 'y', 'u', 'v')
#define GST_RIFF_CYUV GST_MAKE_FOURCC ('C', 'Y', 'U', 'V')
#define GST_RIFF_jpeg GST_MAKE_FOURCC (0x04,0x00,0xFF,0xFF)
#define GST_RIFF_JPEG GST_MAKE_FOURCC ('J', 'P', 'E', 'G')
#define GST_RIFF_MJPG GST_MAKE_FOURCC ('M', 'J', 'P', 'G')
#define GST_RIFF_mJPG GST_MAKE_FOURCC ('m', 'J', 'P', 'G')
#define GST_RIFF_IJPG GST_MAKE_FOURCC ('I', 'J', 'P', 'G')
#define GST_RIFF_rt21 GST_MAKE_FOURCC ('r', 't', '2', '1')
#define GST_RIFF_RT21 GST_MAKE_FOURCC ('R', 'T', '2', '1')
#define GST_RIFF_iv31 GST_MAKE_FOURCC ('i', 'v', '3', '1')
#define GST_RIFF_IV31 GST_MAKE_FOURCC ('I', 'V', '3', '1')
#define GST_RIFF_iv32 GST_MAKE_FOURCC ('i', 'v', '3', '2')
#define GST_RIFF_IV32 GST_MAKE_FOURCC ('I', 'V', '3', '2')
#define GST_RIFF_iv41 GST_MAKE_FOURCC ('i', 'v', '4', '1')
#define GST_RIFF_IV41 GST_MAKE_FOURCC ('I', 'V', '4', '1')
#define GST_RIFF_iv50 GST_MAKE_FOURCC ('i', 'v', '5', '0')
#define GST_RIFF_IV50 GST_MAKE_FOURCC ('I', 'V', '5', '0')
#define GST_RIFF_cvid GST_MAKE_FOURCC ('c', 'v', 'i', 'd')
#define GST_RIFF_CVID GST_MAKE_FOURCC ('C', 'V', 'I', 'D')
#define GST_RIFF_ULTI GST_MAKE_FOURCC ('U', 'L', 'T', 'I')
#define GST_RIFF_ulti GST_MAKE_FOURCC ('u', 'l', 't', 'i')
#define GST_RIFF_YUV9 GST_MAKE_FOURCC ('Y', 'V', 'U', '9')
#define GST_RIFF_YVU9 GST_MAKE_FOURCC ('Y', 'U', 'V', '9')
#define GST_RIFF_XMPG GST_MAKE_FOURCC ('X', 'M', 'P', 'G')
#define GST_RIFF_xmpg GST_MAKE_FOURCC ('x', 'm', 'p', 'g')
#define GST_RIFF_VDOW GST_MAKE_FOURCC ('V', 'D', 'O', 'W')
#define GST_RIFF_MVI1 GST_MAKE_FOURCC ('M', 'V', 'I', '1')
#define GST_RIFF_v422 GST_MAKE_FOURCC ('v', '4', '2', '2')
#define GST_RIFF_V422 GST_MAKE_FOURCC ('V', '4', '2', '2')
#define GST_RIFF_mvi1 GST_MAKE_FOURCC ('m', 'v', 'i', '1')
#define GST_RIFF_MPIX GST_MAKE_FOURCC (0x04,0x00, 'i', '1')     /* MotionPixels munged their id */
#define GST_RIFF_AURA GST_MAKE_FOURCC ('A', 'U', 'R', 'A')
#define GST_RIFF_DMB1 GST_MAKE_FOURCC ('D', 'M', 'B', '1')
#define GST_RIFF_dmb1 GST_MAKE_FOURCC ('d', 'm', 'b', '1')

#define GST_RIFF_BW10 GST_MAKE_FOURCC ('B', 'W', '1', '0')
#define GST_RIFF_bw10 GST_MAKE_FOURCC ('b', 'w', '1', '0')

#define GST_RIFF_yuy2 GST_MAKE_FOURCC ('y', 'u', 'y', '2')
#define GST_RIFF_YUY2 GST_MAKE_FOURCC ('Y', 'U', 'Y', '2')
#define GST_RIFF_YUV8 GST_MAKE_FOURCC ('Y', 'U', 'V', '8')
#define GST_RIFF_WINX GST_MAKE_FOURCC ('W', 'I', 'N', 'X')
#define GST_RIFF_WPY2 GST_MAKE_FOURCC ('W', 'P', 'Y', '2')
#define GST_RIFF_m263 GST_MAKE_FOURCC ('m', '2', '6', '3')
#define GST_RIFF_M263 GST_MAKE_FOURCC ('M', '2', '6', '3')

#define GST_RIFF_Q1_0 GST_MAKE_FOURCC ('Q', '1',0x2e, '0')
#define GST_RIFF_SFMC GST_MAKE_FOURCC ('S', 'F', 'M', 'C')

#define GST_RIFF_y41p GST_MAKE_FOURCC ('y', '4', '1', 'p')
#define GST_RIFF_Y41P GST_MAKE_FOURCC ('Y', '4', '1', 'P')
#define GST_RIFF_yv12 GST_MAKE_FOURCC ('y', 'v', '1', '2')
#define GST_RIFF_YV12 GST_MAKE_FOURCC ('Y', 'V', '1', '2')
#define GST_RIFF_vixl GST_MAKE_FOURCC ('v', 'i', 'x', 'l')
#define GST_RIFF_VIXL GST_MAKE_FOURCC ('V', 'I', 'X', 'L')
#define GST_RIFF_iyuv GST_MAKE_FOURCC ('i', 'y', 'u', 'v')
#define GST_RIFF_IYUV GST_MAKE_FOURCC ('I', 'Y', 'U', 'V')
#define GST_RIFF_i420 GST_MAKE_FOURCC ('i', '4', '2', '0')
#define GST_RIFF_I420 GST_MAKE_FOURCC ('I', '4', '2', '0')
#define GST_RIFF_vyuy GST_MAKE_FOURCC ('v', 'y', 'u', 'y')
#define GST_RIFF_VYUY GST_MAKE_FOURCC ('V', 'Y', 'U', 'Y')

#define GST_RIFF_DIV3 GST_MAKE_FOURCC ('D', 'I', 'V', '3')

#define GST_RIFF_rpza GST_MAKE_FOURCC ('r', 'p', 'z', 'a')
/* And this here's the mistakes that need to be supported */
#define GST_RIFF_azpr GST_MAKE_FOURCC ('a', 'z', 'p', 'r')  /* recognize Apple's rpza mangled? */

/*********** FND in MJPG **********/
#define GST_RIFF_ISFT GST_MAKE_FOURCC ('I', 'S', 'F', 'T')
#define GST_RIFF_IDIT GST_MAKE_FOURCC ('I', 'D', 'I', 'T')

#define GST_RIFF_00AM GST_MAKE_FOURCC ('0', '0', 'A', 'M')
#define GST_RIFF_DISP GST_MAKE_FOURCC ('D', 'I', 'S', 'P')
#define GST_RIFF_ISBJ GST_MAKE_FOURCC ('I', 'S', 'B', 'J')

#define GST_RIFF_rec  GST_MAKE_FOURCC ('r', 'e', 'c', ' ')

/* common data structures */
typedef struct _gst_riff_strh {
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
} gst_riff_strh;

typedef struct _gst_riff_strf_vids {       /* == BitMapInfoHeader */
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
} gst_riff_strf_vids;


typedef struct _gst_riff_strf_auds {       /* == WaveHeader (?) */
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
#define GST_RIFF_WAVE_FORMAT_WMAV1          (0x0160)
#define GST_RIFF_WAVE_FORMAT_WMAV2          (0x0161)
#define GST_RIFF_WAVE_FORMAT_WMAV3          (0x0162)
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
} gst_riff_strf_auds;

typedef struct _gst_riff_strf_iavs {    
  guint32 DVAAuxSrc;
  guint32 DVAAuxCtl;
  guint32 DVAAuxSrc1;
  guint32 DVAAuxCtl1;
  guint32 DVVAuxSrc;
  guint32 DVVAuxCtl;
  guint32 DVReserved1;
  guint32 DVReserved2;
} gst_riff_strf_iavs;

typedef struct _gst_riff_index_entry {  
  guint32 id;
  guint32 flags;
#define GST_RIFF_IF_LIST		(0x00000001L)
#define GST_RIFF_IF_KEYFRAME		(0x00000010L)
#define GST_RIFF_IF_NO_TIME		(0x00000100L)
#define GST_RIFF_IF_COMPUSE		(0x0FFF0000L)
  guint32 offset;
  guint32 size;
} gst_riff_index_entry;

typedef struct _gst_riff_dmlh {
  guint32 totalframes;
} gst_riff_dmlh;

#endif /* __GST_RIFF_IDS_H__ */
