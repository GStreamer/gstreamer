/* GStreamer
 * Copyright (C) 2003 Benjamion Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttypefindfunctions.c: collection of various typefind functions
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
#include "config.h"
#endif

#include <glib/gstrfuncs.h>

#include <gst/gsttypefind.h>
#include <gst/gstelement.h>
#include <gst/gstversion.h>
#include <gst/gstinfo.h>

#include <string.h>
#include <ctype.h>
      
GST_DEBUG_CATEGORY_STATIC (type_find_debug);
#define GST_CAT_DEFAULT type_find_debug

/*** video/x-ms-asf ***********************************************************/

#define ASF_CAPS gst_caps_new ("asf_type_find", "video/x-ms-asf", NULL)
static void
asf_type_find (GstTypeFind *tf, gpointer unused)
{
  static guint8 header_guid[16] =
	  {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
	   0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};	     
  guint8 *data = gst_type_find_peek (tf, 0, 16);
  
  if (data && memcmp (data, header_guid, 16) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, ASF_CAPS);
  }
}
      
/*** audio/x-au ***************************************************************/

#define AU_CAPS gst_caps_new ("au_type_find", "audio/x-au", NULL)
static void
au_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && memcmp (data, ".snd", 4) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, AU_CAPS);
  }
};

/*** video/avi ****************************************************************/

#define AVI_CAPS GST_CAPS_NEW ("avi_type_find", "video/avi", NULL)
static void
avi_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 12);

  if (data && memcmp (data, "RIFF", 4) == 0) {
    data += 8;
    if (memcmp (data, "AVI ", 4) == 0)
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, AVI_CAPS);
  }
}

/*** video/x-cdxa ****************************************************************/

#define CDXA_CAPS GST_CAPS_NEW ("cdxa_type_find", "video/x-cdxa", NULL)
static void
cdxa_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && memcmp (data, "RIFF", 4) == 0) {
    data += 8;
    if (memcmp (data, "CDXA", 4) == 0)
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, CDXA_CAPS);
  }
}

/*** text/plain ****************************************************************/

#define UTF8_CAPS GST_CAPS_NEW ("cdxa_type_find", "text/plain", NULL)
static void
utf8_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data;
  /* randomly decided values */
  guint size = 1024; /* starting size */
  guint probability = 95; /* starting probability */
  guint step = 10; /* how much we reduce probability in each iteration */

  while (probability > step) {
    data = gst_type_find_peek (tf, 0, size);
    if (data) {
      gchar *end;
      gchar *start = (gchar *) data;

      if (g_utf8_validate (start, size, (const gchar **) &end) ||
	  (end - start + 4 > size)) { /* allow last char to be cut off */
	gst_type_find_suggest (tf, probability, UTF8_CAPS);
      }
      return;
    }
    size /= 2;
    probability -= step;
  }
}

/*** text/uri-list ************************************************************/

#define URI_CAPS GST_CAPS_NEW ("uri_type_find", "text/uri-list", NULL)
#define BUFFER_SIZE 16 /* If the string is < 16 bytes we're screwed */
#define INC_BUFFER { 							\
  pos++;								\
  if (pos == BUFFER_SIZE) {						\
    pos = 0;								\
    offset += BUFFER_SIZE;						\
    data = gst_type_find_peek (tf, offset, BUFFER_SIZE);		\
    if (data == NULL) return;						\
  } else {								\
    data++;								\
  }									\
}
static void
uri_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, BUFFER_SIZE);
  guint pos = 0;
  guint offset = 0;
  
  if (data) {
    /* Search for # comment lines */
    while (*data == '#') {
      /* Goto end of line */
      while (*data != '\n') {
        INC_BUFFER;
      }

      INC_BUFFER;
    }

    if (!g_ascii_isalpha (*data)) {
      /* Had a non alpha char - can't be uri-list */
      return;
    }

    INC_BUFFER;
    
    while (g_ascii_isalnum (*data)) {
      INC_BUFFER;
    }

    if (*data != ':') {
      /* First non alpha char is not a : */
      return;
    }

    /* Get the next 2 bytes as well */
    data = gst_type_find_peek (tf, offset + pos, 3);
    if (data == NULL) return;
    
    if (data[1] != '/' && data[2] != '/') {
      return;
    }

    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, URI_CAPS);
  }
}

/*** video/x-fli **************************************************************/

#define FLX_CAPS GST_CAPS_NEW ("flx_type_find", "video/x-fli", NULL)
static void
flx_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8* data = gst_type_find_peek (tf, 0, 134);

  if (data) {
    /* check magic and the frame type of the first frame */
    if ((data[4] == 0x11 || data[4] == 0x12 ||
	 data[4] == 0x30 || data[4] == 0x44) &&
	data[5] == 0xaf &&
	((data[132] == 0x00 || data[132] == 0xfa) && data[133] == 0xf1)) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, FLX_CAPS);
    }
    return;
  }
  data = gst_type_find_peek (tf, 0, 6);
  if (data) {
    /* check magic only */
    if ((data[4] == 0x11 || data[4] == 0x12 ||
	 data[4] == 0x30 || data[4] == 0x44) &&
	data[5] == 0xaf) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, FLX_CAPS);
    }
    return;
  }
}

/*** application/x-id3 **************************************************************/

#define ID3_CAPS GST_CAPS_NEW ("id3_type_find", "application/x-id3", NULL)
static void
id3_type_find (GstTypeFind *tf, gpointer unused)
{
  /* detect ID3v2 first */
  guint8* data = gst_type_find_peek (tf, 0, 10);
  if (data) {
    /* detect valid header */
    if (memcmp (data, "ID3", 3) == 0 &&
	data[3] != 0xFF && data[4] != 0xFF &&
	(data[6] & 0x80) == 0 && (data[7] & 0x80) == 0 &&
	(data[8] & 0x80) == 0 && (data[9] & 0x80) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, ID3_CAPS);
      return;
    }
  }
  data = gst_type_find_peek (tf, -128, 3);
  if (data && memcmp (data, "TAG", 3) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, ID3_CAPS);
  }
}

/*** audio/mpeg **************************************************************/

/**
 * The chance that random data is identified as a valid mp3 header is 63 / 2^18
 * (0.024%) per try. This makes the function for calculating false positives
 *   1 - (1 - ((63 / 2 ^18) ^ GST_MP3_TYPEFIND_MIN_HEADERS)) ^ buffersize)
 * This has the following probabilities of false positives:
 * datasize	          MIN_HEADERS
 * (bytes)	1	2	3	4
 * 4096		62.6%	 0.02%	 0%	 0%
 * 16384	98%	 0.09%	 0%	 0%
 * 1 MiB       100%	 5.88%	 0%	 0%
 * 1 GiB       100%    100%	 1.44%   0%
 * 1 TiB       100%    100%    100%      0.35%
 * This means that the current choice (3 headers by most of the time 4096 byte
 * buffers is pretty safe for now.
 *
 * The max. size of each frame is 1440 bytes, which means that for N frames to
 * be detected, we need 1440 * GST_MP3_TYPEFIND_MIN_HEADERS + 3 bytes of data.
 * Assuming we step into the stream right after the frame header, this
 * means we need 1440 * (GST_MP3_TYPEFIND_MIN_HEADERS + 1) - 1 + 3 bytes
 * of data (5762) to always detect any mp3.
 */

static guint mp3types_bitrates[2][3][16] =
{ { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, },
    {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, },
    {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, } },
  { {0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, },
    {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, },
    {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, } },
};

static guint mp3types_freqs[3][3] =
{ {11025, 12000,  8000},
  {22050, 24000, 16000}, 
  {44100, 48000, 32000}};

static inline guint
mp3_type_frame_length_from_header (guint32 header, guint *put_layer,
				   guint *put_channels, guint *put_bitrate,
				   guint *put_samplerate)
{
  guint length;
  gulong mode, samplerate, bitrate, layer, version, channels;

  if ((header & 0xffe00000) != 0xffe00000)
    return 0;

  /* we don't need extension, copyright, original or
   * emphasis for the frame length */
  header >>= 6;

  /* mode */
  mode = header & 0x3;
  header >>= 3;

  /* padding */
  length = header & 0x1;
  header >>= 1;

  /* sampling frequency */
  samplerate = header & 0x3;
  if (samplerate == 3)
    return 0;
  header >>= 2;

  /* bitrate index */
  bitrate = header & 0xF;
  if (bitrate == 15 || bitrate == 0)
    return 0;

  /* ignore error correction, too */
  header >>= 5;

  /* layer */
  layer = 4 - (header & 0x3);
  if (layer == 4)
    return 0;
  header >>= 2;

  /* version 0=MPEG2.5; 2=MPEG2; 3=MPEG1 */
  version = header & 0x3;
  if (version == 1)
    return 0;

  /* lookup */
  channels = (mode == 3) ? 1 : 2;
  bitrate = mp3types_bitrates[version == 3 ? 0 : 1][layer - 1][bitrate];
  samplerate = mp3types_freqs[version > 0 ? version - 1 : 0][samplerate];

  /* calculating */
  if (layer == 1) {
    length = ((12000 * bitrate / samplerate) + length) * 4;
  } else {
    length += ((layer == 3 && version == 0) ? 72000 : 144000) * bitrate / samplerate;
  }
  
  GST_LOG ("mp3typefind: alculated mp3 frame length of %u bytes", length);
  GST_LOG ("mp3typefind: samplerate = %lu - bitrate = %lu - layer = %lu - version = %lu"
	     " - channels = %lu",
	     samplerate, bitrate, layer, version, channels);
  
  if (put_layer)
    *put_layer = layer;
  if (put_channels)
    *put_channels = channels;
  if (put_bitrate)
    *put_bitrate = bitrate;
  if (put_samplerate)
    *put_samplerate = samplerate;

  return length;
}


#define MP3_CAPS(layer) (layer == 0 ?						\
    GST_CAPS_NEW ("mp3_type_find", "audio/mpeg",				\
		    "mpegversion", GST_PROPS_INT (1),				\
		    "layer", GST_PROPS_INT_RANGE (1, 3)) :			\
    GST_CAPS_NEW ("mp3_type_find", "audio/mpeg",				\
		    "mpegversion", GST_PROPS_INT (1),				\
		    "layer", GST_PROPS_INT (layer)))
/*
 * random values for typefinding
 * if no more data is available, we will return a probability of
 * (found_headers/TRY_HEADERS) * (MAXIMUM * (TRY_SYNC - bytes_skipped)
 *	  / TRY_SYNC)
 */
#define GST_MP3_TYPEFIND_TRY_HEADERS 5
#define GST_MP3_TYPEFIND_TRY_SYNC (GST_TYPE_FIND_MAXIMUM * 100) /* 10kB */
#define GST_MP3_TYPEFIND_SYNC_SIZE 2048

static void
mp3_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = NULL;
  guint size = 0;
  guint64 skipped = 0;

  while (skipped < GST_MP3_TYPEFIND_TRY_SYNC) {
    if (size <= 0) {
      data = gst_type_find_peek (tf, skipped, GST_MP3_TYPEFIND_SYNC_SIZE);
      if (!data)
	break;
      size = GST_MP3_TYPEFIND_SYNC_SIZE;
    }
    if (*data == 0xFF) {
      guint8* head_data;
      guint layer, bitrate, samplerate, channels;
      guint found = 0; /* number of valid headers found */
      guint64 offset = skipped;
      
      while (found < GST_MP3_TYPEFIND_TRY_HEADERS) {
	guint32 head;
	guint length;
	guint prev_layer = 0, prev_bitrate = 0,
	      prev_channels = 0, prev_samplerate = 0;
	
	head_data = gst_type_find_peek (tf, offset, 4);
	if (!head_data)
	  break;
	head = GUINT32_FROM_BE(*((guint32 *) head_data));
        if (!(length = mp3_type_frame_length_from_header (head, &layer,
			&channels, &bitrate, &samplerate))) {
	  break;
	}
	if ((prev_layer && prev_layer != layer) ||
	    /* (prev_bitrate && prev_bitrate != bitrate) || <-- VBR */
	    (prev_samplerate && prev_samplerate != samplerate) ||
	    (prev_channels && prev_channels != channels)) {
          /* this means an invalid property, or a change, which might mean
	   * that this is not a mp3 but just a random bytestream. It could
	   * be a freaking funky encoded mp3 though. We'll just not count
	   * this header*/
	  prev_layer = layer;
	  prev_bitrate = bitrate;
	  prev_channels = channels;
	  prev_samplerate = samplerate;
	} else {
	  found++;
	}
	offset += length;
      }
      g_assert (found <= GST_MP3_TYPEFIND_TRY_HEADERS);
      if (found == GST_MP3_TYPEFIND_TRY_HEADERS ||
	  head_data == NULL) {
	/* we can make a valid guess */
	guint probability = found * GST_TYPE_FIND_MAXIMUM * 
			    (GST_MP3_TYPEFIND_TRY_SYNC - skipped) /
			    GST_MP3_TYPEFIND_TRY_HEADERS / GST_MP3_TYPEFIND_TRY_SYNC;
	if (probability < GST_TYPE_FIND_MINIMUM)
	  probability = GST_TYPE_FIND_MINIMUM;

	/* make sure we're not id3 tagged */
	head_data = gst_type_find_peek (tf, -128, 3);
	if (!head_data) {
	  probability = probability * 4 / 5;
	} else if (memcmp (head_data, "TAG", 3) == 0) {
	  probability = 0;
	}
	g_assert (probability <= GST_TYPE_FIND_MAXIMUM);
	if (probability > 0) {
	  g_assert (layer > 0);
	  gst_type_find_suggest (tf, probability, MP3_CAPS (layer)); 
	}
	return;
      }
    }
    data++;
    skipped++;
    size--;
  }
}

/*** video/mpeg systemstream **************************************************/

#define MPEG_SYS_CAPS(version) (version == 0 ? 				\
    GST_CAPS_NEW ("mpeg_type_find", "video/mpeg",			\
	          "systemstream", GST_PROPS_BOOLEAN (TRUE),		\
		  "mpegversion", GST_PROPS_INT_RANGE (1, 2)) :		\
    GST_CAPS_NEW ("mpeg_type_find", "video/mpeg",			\
	    	  "systemstream", GST_PROPS_BOOLEAN (TRUE),		\
		  "mpegversion", GST_PROPS_INT (version)))
#define IS_MPEG_HEADER(data)		((((guint8 *)data)[0] == 0x00) &&	\
					 (((guint8 *)data)[1] == 0x00) &&	\
					 (((guint8 *)data)[2] == 0x01) &&	\
					 (((guint8 *)data)[3] == 0xBA))
#define IS_MPEG_SYSTEM_HEADER(data)	((((guint8 *)data)[0] == 0x00) &&	\
					 (((guint8 *)data)[1] == 0x00) &&	\
					 (((guint8 *)data)[2] == 0x01) &&	\
					 (((guint8 *)data)[3] == 0xBB))
#define IS_MPEG_PACKET_HEADER(data)	((((guint8 *)data)[0] == 0x00) &&	\
					 (((guint8 *)data)[1] == 0x00) &&	\
					 (((guint8 *)data)[2] == 0x01) &&	\
					 ((((guint8 *)data)[3] & 0x80) == 0x80))
static void
mpeg2_sys_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 5);

  if (data && IS_MPEG_HEADER (data)) {
    if ((data[4] & 0xC0) == 0x40) {
      /* type 2 */
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MPEG_SYS_CAPS (2));
    }
  }
};
/* ATTANTION: ugly return value:
 * 0 -  invalid data
 * 1 - not enough data
 * anything else - size until next package
 */
static guint
mpeg1_parse_header (GstTypeFind *tf, guint64 offset)
{
  guint8 *data = gst_type_find_peek (tf, offset, 18);
  guint size;
  
  if (!data) {
    GST_LOG ("couldn't get 18 bytes to parse MPEG header");
    return 1;
  }
  
  /* check header */
  if (!IS_MPEG_HEADER (data)) {
    GST_LOG ("This isn't an MPEG header");
    return 0;
  }
  data += 4;

  /* check marker bits */
  if ((*data & 0xF1) != 0x21) {
    GST_LOG ("marker bits in byte 4 don't match");
    return 0;
  }
  data += 2;
  if ((*data & 0x01) != 0x01) {
    GST_LOG ("marker bits in byte 6 don't match");
    return 0;
  }
  data += 2;
  if ((*data & 0x01) != 0x01) {
    GST_LOG ("marker bits in byte 8 don't match");
    return 0;
  }
  data ++;
  if ((*data & 0x80) != 0x80) {
    GST_LOG ("marker bits in byte 9 don't match");
    return 0;
  }
  data += 2;
  if ((*data & 0x01) != 0x01) {
    GST_LOG ("marker bits in byte 11 don't match");
    return 0;
  }
  data++;
  
  if (!IS_MPEG_PACKET_HEADER (data) &&
      !IS_MPEG_SYSTEM_HEADER (data)) {
    GST_LOG ("MPEG packet header doesn't match: %8.8X", GUINT32_FROM_BE (*((guint32 *) data)));
    return 0;
  }
    
  data += 4;
  
  size = GUINT16_FROM_BE (*((guint16 *) data)) + 18;

  GST_DEBUG ("found mpeg1 packet at offset %"G_GUINT64_FORMAT" with size %u", offset, size);
  return size;
}
/* calculation of possibility to identify random data as mpeg systemstream:
 * bits that must match in header detection:		65
 * chance that random data is identifed:		1/2^65
 * chance that GST_MPEG_TYPEFIND_TRY_HEADERS headers are identified:	
 *					1/2^(65*GST_MPEG_TYPEFIND_TRY_HEADERS)
 * chance that this happens in GST_MPEG_TYPEFIND_TRY_SYNC bytes:
 *					1-(1-1/2^(65*GST_MPEG_TYPEFIND_TRY_HEADERS))^GST_MPEG_TYPEFIND_TRY_SYNC
 * for current values:
 *					1-(1-1/2^(65*2)^50000
 *  and that value is way smaller than 0,1%
 */
#define GST_MPEG_TYPEFIND_TRY_HEADERS 2
#define GST_MPEG_TYPEFIND_TRY_SYNC (GST_TYPE_FIND_MAXIMUM * 500) /* 50kB */
#define GST_MPEG_TYPEFIND_SYNC_SIZE 2048
static void
mpeg1_sys_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = NULL;
  guint size = 0;
  guint64 skipped = 0;

  while (skipped < GST_MPEG_TYPEFIND_TRY_SYNC) {
    if (size < 4) {
      data = gst_type_find_peek (tf, skipped, GST_MPEG_TYPEFIND_SYNC_SIZE);
      if (!data)
	break;
      size = GST_MPEG_TYPEFIND_SYNC_SIZE;
    }
    if (IS_MPEG_HEADER (data)) {
      /* found packet start code */
      guint found = 0;
      guint packet_size;
      guint64 offset = skipped;
      
      while (found < GST_MPEG_TYPEFIND_TRY_HEADERS) {
	packet_size = mpeg1_parse_header (tf, offset);
	if (packet_size <= 1)
	  break;
	offset += packet_size;
	found++;
      }
      g_assert (found <= GST_MPEG_TYPEFIND_TRY_HEADERS);
      if (found == GST_MPEG_TYPEFIND_TRY_HEADERS ||
	  packet_size == 1) {
	guint probability = found * GST_TYPE_FIND_MAXIMUM * 
			    (GST_MPEG_TYPEFIND_TRY_SYNC - skipped) /
			    GST_MPEG_TYPEFIND_TRY_HEADERS / GST_MPEG_TYPEFIND_TRY_SYNC;
	
	if (probability < GST_TYPE_FIND_MINIMUM)
	  probability = GST_TYPE_FIND_MINIMUM;
	g_assert (probability <= GST_TYPE_FIND_MAXIMUM);
	gst_type_find_suggest (tf, probability, MPEG_SYS_CAPS (1)); 
	return;
      }
    }
    data++;
    skipped++;
    size--;
  }
}

/*** video/quicktime***********************************************************/

#define QT_CAPS gst_caps_new ("qt_typefind", "video/quicktime", NULL)
static void
qt_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data;
  guint tip = 0;
  guint64 offset = 0;

  while ((data = gst_type_find_peek (tf, offset, 8)) != NULL) {
    if (strncmp (&data[4], "wide", 4) != 0 &&
	strncmp (&data[4], "moov", 4) != 0 &&
	strncmp (&data[4], "mdat", 4) != 0 &&
	strncmp (&data[4], "free", 4) != 0) {
      tip = 0;
      break;
    }
    if (tip == 0) {
      tip = GST_TYPE_FIND_LIKELY;
    } else {
      tip = GST_TYPE_FIND_MAXIMUM;
      break;
    }
    offset += GUINT32_FROM_BE (*((guint32 *) data));
  }
  if (tip > 0) {
    gst_type_find_suggest (tf, tip, QT_CAPS);
  }
};

/*** application/vnd.rn-realmedia *********************************************/

#define RM_CAPS gst_caps_new ("realmedia_type_find", "application/vnd.rn-realmedia", NULL)
static void
rm_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && memcmp (data, ".RMF", 4) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, RM_CAPS);
  }
};

/*** audio/x-wav ****************************************************************/

#define WAV_CAPS GST_CAPS_NEW ("wav_type_find", "audio/x-wav", NULL)
static void
wav_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 12);

  if (data && memcmp (data, "RIFF", 4) == 0) {
    data += 8;
    if (memcmp (data, "WAVE", 4) == 0)
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, WAV_CAPS);
  }
}
      
/*** audio/x-aiff *********************************************/

#define AIFF_CAPS GST_CAPS_NEW ("aiff_type_find", "audio/x-aiff", NULL)
static void
aiff_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && memcmp (data, "FORM", 4) == 0) {
    data += 8;
    if (memcmp (data, "AIFF", 4) == 0)
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, AIFF_CAPS);
  }
}

/*** audio/x-shorten ****************************************/

#define SHN_CAPS GST_CAPS_NEW ("shn_type_find", "audio/x-shorten", NULL)
static void
shn_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, -8, 8);

  if (data && memcmp (data, "SHNAMPSK", 8) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SHN_CAPS);
  }
}

/*** audio/x-mod *********************************************/

#define MOD_CAPS gst_caps_new ("mod_type_find", "audio/x-mod", NULL)
/* FIXME: M15 CheckType to do */
static void
mod_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data;
  
  /* MOD */
  if ((data = gst_type_find_peek (tf, 1080, 4)) != NULL) {	
        /* Protracker and variants */
    if ((memcmp(data, "M.K.", 4) == 0) || 
	(memcmp(data, "M!K!", 4) == 0) ||
	/* Star Tracker */
	(memcmp(data, "FLT", 3) == 0 && isdigit (data[3])) ||
	(memcmp(data, "EXO", 3) == 0 && isdigit (data[3])) ||
	/* Oktalyzer (Amiga) */
	(memcmp(data, "OKTA", 4) == 0) ||
	/* Oktalyser (Atari) */
	(memcmp(data, "CD81", 4) == 0) ||
	/* Fasttracker */
	(memcmp(data + 1, "CHN", 3) == 0 && isdigit (data[0])) ||
	/* Fasttracker or Taketracker */
	(memcmp(data + 2, "CH", 2) == 0 && isdigit (data[0]) && isdigit (data[1])) ||
	(memcmp(data + 2, "CN", 2) == 0 && isdigit (data[0]) && isdigit (data[1]))) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
      return;
    }
  }
  /* XM */
  if ((data = gst_type_find_peek (tf, 0, 38)) != NULL) {
    if (memcmp(data, "Extended Module: ", 17) == 0 &&
	data[37] == 0x1A) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
      return;
    }
  }
  /* OKT */
  if (data || (data = gst_type_find_peek (tf, 0, 8)) != NULL) {
    if (memcmp(data, "OKTASONG", 8) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
      return;
    }
  }
  if (data || (data = gst_type_find_peek (tf, 0, 4)) != NULL) {	
    /* 669 */
    if ((memcmp(data, "if", 2) == 0) || 
	(memcmp(data, "JN", 2) == 0)) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, MOD_CAPS);
      return;
    }
	/* AMF */
    if ((memcmp(data, "AMF", 3) == 0 && data[3] > 10 && data[3] < 14) ||
	/* IT */
	(memcmp(data, "IMPM", 4) == 0) ||
	/* MED */
	(memcmp(data, "MMD0", 4) == 0) ||
	(memcmp(data, "MMD1", 4) == 0) ||
	/* MTM */
	(memcmp(data, "MTM", 3) == 0)){
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
      return;
    }
    /* DSM */
    if (memcmp(data, "RIFF", 4) == 0) {
      guint8 *data2 = gst_type_find_peek (tf, 8, 4);
      if (data2) {
	if (memcmp (data2, "DSMF", 4) == 0) {
	  gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
	  return;
	}
      }
    }
    /* FAM */
    if (memcmp(data, "FAM\xFE", 4) == 0) {
      guint8 *data2 = gst_type_find_peek (tf, 44, 3);
      if (data2) {
	if (memcmp (data2, "compare", 3) == 0) {
	  gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
	  return;
	}
      } else {
	gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, MOD_CAPS);
	return;
      }
    }
    /* GDM */
    if (memcmp(data, "GDM\xFE", 4) == 0) {
      guint8 *data2 = gst_type_find_peek (tf, 71, 4);
      if (data2) {
	if (memcmp (data2, "GMFS", 4) == 0) {
	  gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
	  return;
	}
      } else {
	gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, MOD_CAPS);
	return;
      }
    }
  }
  /* IMF */
  if ((data = gst_type_find_peek (tf, 60, 4)) != NULL) {
    if (memcmp(data, "IM10", 4) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
      return;
    }
  }
  /* S3M */
  if ((data = gst_type_find_peek (tf, 44, 4)) != NULL) {
    if (memcmp(data, "SCRM", 4) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MOD_CAPS);
      return;
    }
  }
}

/*** audio/x-flac *************************************************************/

#define FLAC_CAPS gst_caps_new ("flac_type_find", "audio/x-flac", NULL)
static void
flac_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && memcmp (data, "fLaC", 4) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, FLAC_CAPS);
  }
};

/*** application/x-shockwave-flash ********************************************/

#define SWF_CAPS gst_caps_new ("swf_type_find", "application/x-shockwave-flash", NULL)
static void
swf_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && (data[0] == 'F' || data[0] == 'C') && 
      data[1] == 'W' && data[2] == 'S') {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SWF_CAPS);
  }
};

/*** application/ogg **********************************************************/

#define OGG_CAPS gst_caps_new ("ogg_type_find", "application/ogg", NULL)
static void
ogg_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && memcmp (data, "OggS", 4) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, OGG_CAPS);
  }
};

/*** plugin initialization ****************************************************/

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  /* can't initialize this via a struct as caps can't be statically initialized */
 
  /* note: asx/wax/wmx are XML files, asf doesn't handle them */
  static gchar * asf_exts[] = {"asf", "wm", "wma", "wmv", NULL}; 
  static gchar * au_exts[] = {"au", "snd", NULL};
  static gchar * avi_exts[] = {"avi", NULL};
  static gchar * cdxa_exts[] = {"dat", NULL};
  static gchar * flac_exts[] = {"flac", NULL};
  static gchar * flx_exts[] = {"flc", "fli", NULL};
  static gchar * id3_exts[] = {"mp3", "mp2", "mp1", "mpga", "ogg", "flac", NULL};
  static gchar * mod_exts[] = {"669", "amf", "dsm", "gdm", "far", "imf", 
			       "it",  "med", "mod", "mtm", "okt", "sam", 
			       "s3m", "stm", "stx", "ult", "xm",  NULL};
  static gchar * mp3_exts[] = {"mp3", "mp2", "mp1", "mpga", NULL};
  static gchar * mpeg_sys_exts[] = {"mpe", "mpeg", "mpg", NULL};
  static gchar * ogg_exts[] = {"ogg", NULL};
  static gchar * qt_exts[] = {"mov", NULL};
  static gchar * rm_exts[] = {"ra", "ram", "rm", NULL};
  static gchar * swf_exts[] = {"swf", "swfl", NULL};
  static gchar * utf8_exts[] = {"txt", NULL};
  static gchar * wav_exts[] = {"wav", NULL};
  static gchar * aiff_exts[] = {"aiff", "aif", "aifc", NULL};
  static gchar * shn_exts[] = {"shn", NULL};
  static gchar * uri_exts[] = {"ram", NULL};

  GST_DEBUG_CATEGORY_INIT (type_find_debug, "typefindfunctions", GST_DEBUG_FG_GREEN | GST_DEBUG_BG_RED, "generic type find functions");

  gst_type_find_factory_register (plugin, "video/x-ms-asf", GST_ELEMENT_RANK_SECONDARY,
	  asf_type_find, asf_exts, ASF_CAPS, NULL);
  gst_type_find_factory_register (plugin, "audio/x-au", GST_ELEMENT_RANK_MARGINAL,
	  au_type_find, au_exts, AU_CAPS, NULL);
  gst_type_find_factory_register (plugin, "video/avi", GST_ELEMENT_RANK_PRIMARY,
	  avi_type_find, avi_exts, AVI_CAPS, NULL);
  gst_type_find_factory_register (plugin, "video/x-cdxa", GST_ELEMENT_RANK_SECONDARY,
	  cdxa_type_find, cdxa_exts, CDXA_CAPS, NULL);
  gst_type_find_factory_register (plugin, "audio/x-flac", GST_ELEMENT_RANK_PRIMARY,
	  flac_type_find, flac_exts, FLAC_CAPS, NULL);
  gst_type_find_factory_register (plugin, "video/x-fli", GST_ELEMENT_RANK_MARGINAL,
	  flx_type_find, flx_exts, FLX_CAPS, NULL);
  gst_type_find_factory_register (plugin, "application/x-id3", GST_ELEMENT_RANK_PRIMARY,
	  id3_type_find, id3_exts, ID3_CAPS, NULL);
  gst_type_find_factory_register (plugin, "audio/x-mod", GST_ELEMENT_RANK_SECONDARY,
	  mod_type_find, mod_exts, MOD_CAPS, NULL);
  gst_type_find_factory_register (plugin, "audio/mpeg", GST_ELEMENT_RANK_PRIMARY,
	  mp3_type_find, mp3_exts, MP3_CAPS (0), NULL);
  gst_type_find_factory_register (plugin, "video/mpeg1", GST_ELEMENT_RANK_PRIMARY,
	  mpeg1_sys_type_find, mpeg_sys_exts, MPEG_SYS_CAPS (1), NULL);
  gst_type_find_factory_register (plugin, "video/mpeg2", GST_ELEMENT_RANK_SECONDARY,
	  mpeg2_sys_type_find, mpeg_sys_exts, MPEG_SYS_CAPS (2), NULL);
  gst_type_find_factory_register (plugin, "application/ogg", GST_ELEMENT_RANK_PRIMARY,
	  ogg_type_find, ogg_exts, OGG_CAPS, NULL);
  gst_type_find_factory_register (plugin, "video/quicktime", GST_ELEMENT_RANK_SECONDARY,
	  qt_type_find, qt_exts, QT_CAPS, NULL);
  gst_type_find_factory_register (plugin, "application/vnd.rn-realmedia", GST_ELEMENT_RANK_SECONDARY,
	  rm_type_find, rm_exts, RM_CAPS, NULL);
  gst_type_find_factory_register (plugin, "application/x-shockwave-flash", GST_ELEMENT_RANK_SECONDARY,
	  swf_type_find, swf_exts, SWF_CAPS, NULL);
  gst_type_find_factory_register (plugin, "text/plain", GST_ELEMENT_RANK_MARGINAL,
	  utf8_type_find, utf8_exts, UTF8_CAPS, NULL);
  gst_type_find_factory_register (plugin, "text/uri-list", GST_ELEMENT_RANK_MARGINAL,
	  uri_type_find, uri_exts, URI_CAPS, NULL);
  gst_type_find_factory_register (plugin, "audio/x-wav", GST_ELEMENT_RANK_SECONDARY,
	  wav_type_find, wav_exts, WAV_CAPS, NULL);
  gst_type_find_factory_register (plugin, "audio/x-aiff", GST_ELEMENT_RANK_SECONDARY,
	  aiff_type_find, aiff_exts, AIFF_CAPS, NULL);

  gst_type_find_factory_register (plugin, "audio/x-shorten", GST_ELEMENT_RANK_SECONDARY,
	  shn_type_find, shn_exts, SHN_CAPS, NULL);

  return TRUE;
}
GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "typefindfunctions",
  plugin_init
};

