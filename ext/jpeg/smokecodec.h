/* Smoke Codec
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
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


#ifndef __SMOKECODEC_H__
#define __SMOKECODEC_H__


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct _SmokeCodecInfo SmokeCodecInfo;

typedef enum {
  SMOKECODEC_WRONGSIZE		= -4,
  SMOKECODEC_ERROR		= -3,
  SMOKECODEC_NOMEM		= -2,
  SMOKECODEC_NULLPTR		= -1,
  SMOKECODEC_OK  		=  0,
} SmokeCodecResult;
	
typedef enum {
  SMOKECODEC_KEYFRAME  		= (1<<0),
  SMOKECODEC_MOTION_VECTORS	= (1<<1)
} SmokeCodecFlags;
	

/* init */
int 			smokecodec_encode_new 	(SmokeCodecInfo **info,
		               		     	 const unsigned int width,
				        	 const unsigned int height);

int 			smokecodec_decode_new 	(SmokeCodecInfo **info);

/* config */
SmokeCodecResult	smokecodec_set_quality	(SmokeCodecInfo *info,
						 const unsigned int min,
						 const unsigned int max);
SmokeCodecResult	smokecodec_get_quality	(SmokeCodecInfo *info,
						 unsigned int *min,
						 unsigned int *max);

SmokeCodecResult	smokecodec_set_threshold (SmokeCodecInfo *info,
						 const unsigned int threshold);
SmokeCodecResult	smokecodec_get_threshold (SmokeCodecInfo *info,
						 unsigned int *threshold);

SmokeCodecResult	smokecodec_set_bitrate	(SmokeCodecInfo *info,
						 const unsigned int bitrate);
SmokeCodecResult	smokecodec_get_bitrate	(SmokeCodecInfo *info,
						 unsigned int *bitrate);

/* encoding */
SmokeCodecResult 	smokecodec_encode	(SmokeCodecInfo *info,
						 const unsigned char *in,
						 SmokeCodecFlags flags,
						 unsigned char *out,
						 unsigned int *outsize);

/* decoding */
SmokeCodecResult 	smokecodec_parse_header	(SmokeCodecInfo *info,
						 const unsigned char *in,
						 const unsigned int insize,
						 SmokeCodecFlags *flags,
						 unsigned int *width,
						 unsigned int *height);

SmokeCodecResult 	smokecodec_decode	(SmokeCodecInfo *info,
		 				 const unsigned char *in,
						 const unsigned int insize,
						 unsigned char *out);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __SMOKECODEC_H__ */
