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

/*** text/plain ****************************************************************/

static GstStaticCaps utf8_caps = GST_STATIC_CAPS ("text/plain");
#define UTF8_CAPS gst_caps_copy(gst_static_caps_get(&utf8_caps))
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

static GstStaticCaps uri_caps = GST_STATIC_CAPS ("text/uri-list");
#define URI_CAPS gst_caps_copy(gst_static_caps_get(&uri_caps))
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

static GstStaticCaps flx_caps = GST_STATIC_CAPS ("video/x-fli");
#define FLX_CAPS gst_caps_copy(gst_static_caps_get(&flx_caps))
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

static GstStaticCaps id3_caps = GST_STATIC_CAPS ("application/x-id3");
#define ID3_CAPS gst_caps_copy(gst_static_caps_get(&id3_caps))
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


static GstStaticCaps mp3_caps = GST_STATIC_CAPS ("audio/mpeg, "
    "mpegversion = (int) 1, layer = (int) [ 1, 3 ]");
#define MP3_CAPS gst_caps_copy(gst_static_caps_get(&mp3_caps))
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
      guint8* head_data = NULL;
      guint layer, bitrate, samplerate, channels;
      guint found = 0; /* number of valid headers found */
      guint64 offset = skipped;
      
      while (found < GST_MP3_TYPEFIND_TRY_HEADERS) {
	guint32 head;
	guint length;
	guint prev_layer = 0, prev_bitrate = 0,
	      prev_channels = 0, prev_samplerate = 0;
	
	if (offset + 4 <= skipped + size) {
	  head_data = data + offset - skipped;
	} else {
	  head_data = gst_type_find_peek (tf, offset, 4);
	}
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
	  GstCaps *caps;
	  g_assert (layer > 0);
	  caps = MP3_CAPS;
	  gst_structure_set (gst_caps_get_structure (caps, 0), "layer",
	      G_TYPE_INT, layer, 0);
	  gst_type_find_suggest (tf, probability, caps); 
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

static GstStaticCaps mpeg_sys_caps = GST_STATIC_CAPS ("video/mpeg, "
    "systemstream = (boolean) true, mpegversion = (int) [ 1, 2 ]");
#define MPEG_SYS_CAPS gst_caps_copy(gst_static_caps_get(&mpeg_sys_caps))
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
      GstCaps *caps;
      caps = MPEG_SYS_CAPS;
      gst_structure_set (gst_caps_get_structure (caps, 0), "mpegversion",
	  G_TYPE_INT, 2, 0);
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, caps);
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
 *				      = 3.6734..*10^-35
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
  GstCaps *caps;

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
      guint packet_size = 0;
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
	caps = MPEG_SYS_CAPS;
	gst_structure_set (gst_caps_get_structure (caps, 0), "mpegversion",
	    G_TYPE_INT, 1, 0);
	gst_type_find_suggest (tf, probability, caps); 
	return;
      }
    }
    data++;
    skipped++;
    size--;
  }
}

/*** video/mpeg video stream **************************************************/

static GstStaticCaps mpeg_video_caps = GST_STATIC_CAPS ("video/mpeg, "
    "systemstream = (boolean) false");
#define MPEG_VIDEO_CAPS gst_caps_copy(gst_static_caps_get(&mpeg_video_caps))
static void
mpeg_video_type_find (GstTypeFind *tf, gpointer unused)
{
  static const guint8 sequence_header[] = { 0x00, 0x00, 0x01, 0xb3 };
  guint8 *data = NULL;

  data = gst_type_find_peek (tf, 0, 8);

  if (data && memcmp(data, sequence_header, 4)==0){
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, MPEG_VIDEO_CAPS);
  }
}

/*** video/quicktime***********************************************************/

static GstStaticCaps qt_caps = GST_STATIC_CAPS ("video/quicktime");
#define QT_CAPS gst_caps_copy(gst_static_caps_get(&qt_caps))
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
	strncmp (&data[4], "pnot", 4) != 0 &&
	strncmp (&data[4], "PICT", 4) != 0 &&
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

/*** audio/x-aiff *********************************************/

static GstStaticCaps aiff_caps = GST_STATIC_CAPS ("audio/x-aiff");
#define AIFF_CAPS gst_caps_copy(gst_static_caps_get(&aiff_caps))
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

static GstStaticCaps shn_caps = GST_STATIC_CAPS ("audio/x-shorten");
#define SHN_CAPS gst_caps_copy(gst_static_caps_get(&shn_caps))
static void
shn_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);
  if (data && memcmp (data, "ajkg", 4) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SHN_CAPS);
  }
  data = gst_type_find_peek (tf, -8, 8);
  if (data && memcmp (data, "SHNAMPSK", 8) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SHN_CAPS);
  }
}

/*** audio/x-m4a *********************************************/

static GstStaticCaps aac_caps = GST_STATIC_CAPS ("audio/x-m4a");
#define AAC_CAPS gst_caps_copy(gst_static_caps_get(&aac_caps))
static void
m4a_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 4, 8);
  if (data && memcmp (data, "ftypM4A ", 8) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, AAC_CAPS);
  }
}

/*** audio/x-mod *********************************************/

static GstStaticCaps mod_caps = GST_STATIC_CAPS ("audio/x-mod");
#define MOD_CAPS gst_caps_copy(gst_static_caps_get(&mod_caps))
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

/*** application/x-shockwave-flash ********************************************/

static GstStaticCaps swf_caps = GST_STATIC_CAPS ("audio/x-shockwave-flash");
#define SWF_CAPS gst_caps_copy(gst_static_caps_get(&swf_caps))
static void
swf_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 4);

  if (data && (data[0] == 'F' || data[0] == 'C') && 
      data[1] == 'W' && data[2] == 'S') {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SWF_CAPS);
  }
}

/*** image/jpeg ***************************************************************/

static GstStaticCaps jpeg_caps = GST_STATIC_CAPS ("image/jpeg");
#define JPEG_CAPS gst_caps_copy(gst_static_caps_get(&jpeg_caps))
static void
jpeg_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 10);
  guint8 header[2] = {0xFF, 0xD8};

  if (data && memcmp (data, header, 2) == 0) {
    if (memcmp (data + 6, "JFIF", 4) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, JPEG_CAPS);
    } else if (memcmp (data + 6, "Exif", 4) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, JPEG_CAPS);
    } else {
      gst_type_find_suggest (tf, GST_TYPE_FIND_POSSIBLE, JPEG_CAPS);
    }
  }
}

/*** image/bmp *********************/

static GstStaticCaps bmp_caps = GST_STATIC_CAPS ("image/bmp");
#define BMP_CAPS gst_caps_copy(gst_static_caps_get(&bmp_caps))
static void
bmp_type_find (GstTypeFind *tf, gpointer unused)
{
  guint8 *data = gst_type_find_peek (tf, 0, 18);

  if (data && memcmp (data, "BM", 2) == 0) {
    if ((data[14] == 0x0C ||
	 data[14] == 0x28 ||
	 data[14] == 0xF0) &&
	data[15] == 0 &&
	data[16] == 0 &&
	data[17] == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, BMP_CAPS);
    }
  }
}

/*** image/tiff ********************/
static GstStaticCaps tiff_caps = GST_STATIC_CAPS ("image/tiff, "
    "endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }" );
#define TIFF_CAPS gst_caps_copy(gst_static_caps_get(&tiff_caps))
static GstStaticCaps tiff_be_caps = GST_STATIC_CAPS ("image/tiff, "
    "endianness = (int) BIG_ENDIAN");
#define TIFF_BE_CAPS gst_caps_copy(gst_static_caps_get(&tiff_be_caps))
static GstStaticCaps tiff_le_caps = GST_STATIC_CAPS ("image/tiff, "
    "endianness = (int) LITTLE_ENDIAN");
#define TIFF_LE_CAPS gst_caps_copy(gst_static_caps_get(&tiff_le_caps))
static void
tiff_type_find (GstTypeFind *tf, gpointer ununsed)
{
  guint8 *data = gst_type_find_peek (tf, 0, 8);
  guint8 le_header[4] = {0x49, 0x49, 0x2A, 0x00};
  guint8 be_header[4] = {0x4D, 0x4D, 0x00, 0x2A};
  
  if (data) {
    if (memcmp (data, le_header, 4) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, TIFF_LE_CAPS);
    } else if (memcmp (data, be_header, 4) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, TIFF_BE_CAPS);
    }
  }
}

/*** video/x-dv ***************************************************************/

static GstStaticCaps dv_caps = GST_STATIC_CAPS ("video/x-dv, "
    "systemstream = (boolean) true");
#define DV_CAPS gst_caps_copy(gst_static_caps_get(&dv_caps))
static void
dv_type_find (GstTypeFind *tf, gpointer private)
{
  guint8 *data;

  data = gst_type_find_peek (tf, 0, 5);

  /* check for DIF  and DV flag */
  if (data && (data[0] == 0x1f) && (data[1] == 0x07) && (data[2] == 0x00) &&
      ((data[4]&0x01) == 0)){
    gchar *format;
    GstCaps *caps = DV_CAPS;

    if (data[3] & 0x80) {
      format = "PAL";
    } else {
      format = "NTSC";
    }
    gst_structure_set (gst_caps_get_structure (caps, 0), "format",
	G_TYPE_STRING, format, NULL);

    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, caps);
  }
}

/*** application/x-vorbis *****************************************************/

static GstStaticCaps vorbis_caps = GST_STATIC_CAPS ("audio/x-vorbis");
#define VORBIS_CAPS gst_caps_copy(gst_static_caps_get(&vorbis_caps))
static void
vorbis_type_find (GstTypeFind *tf, gpointer private)
{
  guint8 *data = gst_type_find_peek (tf, 0, 30);

  if (data) {
    guint blocksize_0;
    guint blocksize_1;
    /* 1 byte packet type (identification=0x01)
       6 byte string "vorbis"
       4 byte vorbis version */
    if (memcmp (data, "\001vorbis\000\000\000\000", 11) != 0) return;
    data += 11;
    /* 1 byte channels must be != 0 */
    if (data[0] == 0) return;
    data++;
    /* 4 byte samplerate must be != 0 */
    if (*((guint32 *) data) == 0) return;
    data += 16;
    /* blocksize checks */
    blocksize_0 = data[0] & 0x0F;
    blocksize_1 = (data[0] & 0xF0) >> 4;
    if (blocksize_0 > blocksize_1) return;
    if (blocksize_0 < 6 || blocksize_0 > 13) return;
    if (blocksize_1 < 6 || blocksize_1 > 13) return;
    data++;
    /* framing bit */
    if ((data[0] & 0x01) != 1) return;
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, VORBIS_CAPS);
  } 
}

/*** generic typefind for streams that have some data at a specific position***/

typedef struct {
  guint8 *	data;
  guint		size;
  guint		probability;
  GstCaps *	caps;
} GstTypeFindData;
static void
start_with_type_find (GstTypeFind *tf, gpointer private)
{
  GstTypeFindData *start_with = (GstTypeFindData *) private;
  guint8 *data;

  GST_LOG ("trying to find mime type %s with the first %u bytes of data", 
	   gst_structure_get_name (gst_caps_get_structure(start_with->caps, 0)),
	   start_with->size);
  data = gst_type_find_peek (tf, 0, start_with->size);
  if (data && memcmp (data, start_with->data, start_with->size)==0) {
    gst_type_find_suggest (tf, start_with->probability, start_with->caps);
  }
}
#define TYPE_FIND_REGISTER_START_WITH(plugin,name,rank,ext,_data,_size,_probability)\
G_BEGIN_DECLS{									\
  GstTypeFindData *sw_data = g_new (GstTypeFindData, 1);			\
  sw_data->data = _data;						      	\
  sw_data->size = _size;						      	\
  sw_data->probability = _probability;						\
  sw_data->caps = gst_caps_new_simple (name, NULL);			\
  TYPE_FIND_REGISTER (plugin, name, rank, start_with_type_find,			\
		      ext, sw_data->caps, sw_data);				\
}G_END_DECLS

/*** same for riff types ******************************************************/

static void
riff_type_find (GstTypeFind *tf, gpointer private)
{
  GstTypeFindData *riff_data = (GstTypeFindData *) private;
  guint8 *data = gst_type_find_peek (tf, 0, 12);

  if (data && memcmp (data, "RIFF", 4) == 0) {
    data += 8;
    if (memcmp (data, riff_data->data, 4) == 0)
      gst_type_find_suggest (tf, riff_data->probability, riff_data->caps);
  }
}
#define TYPE_FIND_REGISTER_RIFF(plugin,name,rank,ext,_data)			\
G_BEGIN_DECLS{									\
  GstTypeFindData *sw_data = g_new (GstTypeFindData, 1);			\
  sw_data->data = _data;						      	\
  sw_data->size = 4;							      	\
  sw_data->probability = GST_TYPE_FIND_MAXIMUM;					\
  sw_data->caps = gst_caps_new_simple (name, NULL);			\
  TYPE_FIND_REGISTER (plugin, name, rank, riff_type_find,			\
		      ext, sw_data->caps, sw_data);				\
}G_END_DECLS

/*** plugin initialization ****************************************************/

#define TYPE_FIND_REGISTER(plugin,name,rank,func,ext,caps,priv) G_BEGIN_DECLS{\
  if (!gst_type_find_register (plugin, name, rank, func, ext, caps, priv))\
    return FALSE; \
}G_END_DECLS
static gboolean
plugin_init (GstPlugin *plugin)
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
  static gchar * mpeg_video_exts[] = {"mpv", "mpeg", "mpg", NULL};
  static gchar * ogg_exts[] = {"ogg", NULL};
  static gchar * qt_exts[] = {"mov", NULL};
  static gchar * rm_exts[] = {"ra", "ram", "rm", NULL};
  static gchar * swf_exts[] = {"swf", "swfl", NULL};
  static gchar * utf8_exts[] = {"txt", NULL};
  static gchar * wav_exts[] = {"wav", NULL};
  static gchar * aiff_exts[] = {"aiff", "aif", "aifc", NULL};
  static gchar * shn_exts[] = {"shn", NULL};
  static gchar * uri_exts[] = {"ram", NULL};
  static gchar * jpeg_exts[] = {"jpg", "jpe", "jpeg", NULL};
  static gchar * gif_exts[] = {"gif", NULL};
  static gchar * png_exts[] = {"png", NULL};
  static gchar * bmp_exts[] = {"bmp", NULL};
  static gchar * tiff_exts[] = {"tif", "tiff", NULL};
  static gchar * matroska_exts[] = {"mkv", "mka", NULL};
  static gchar * dv_exts[] = {"dv", NULL};
  static gchar * sid_exts[] = {"sid", NULL};
  static gchar * xcf_exts[] = {"xcf", NULL};
  static gchar * mng_exts[] = {"mng", NULL};
  static gchar * jng_exts[] = {"jng", NULL};
  static gchar * xpm_exts[] = {"xpm", NULL};
  static gchar * ras_exts[] = {"ras", NULL};
  static gchar * bz2_exts[] = {"bz2", NULL};
  static gchar * gz_exts[] = {"gz", NULL};
  static gchar * zip_exts[] = {"zip", NULL};
  static gchar * compress_exts[] = {"Z", NULL};
  static gchar * m4a_exts[] = {"m4a", NULL};
  
  GST_DEBUG_CATEGORY_INIT (type_find_debug, "typefindfunctions", GST_DEBUG_FG_GREEN | GST_DEBUG_BG_RED, "generic type find functions");

  /* must use strings, macros don't accept initializers */
  TYPE_FIND_REGISTER_START_WITH (plugin, "video/x-ms-asf", GST_RANK_SECONDARY, asf_exts,
	  "\060\046\262\165\216\146\317\021\246\331\000\252\000\142\316\154",
	  16, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "audio/x-au", GST_RANK_MARGINAL, au_exts,
	  ".snd", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_RIFF (plugin, "video/x-msvideo", GST_RANK_PRIMARY, avi_exts, "AVI ");
  TYPE_FIND_REGISTER_RIFF (plugin, "video/x-cdxa", GST_RANK_SECONDARY, cdxa_exts, "CDXA");
  TYPE_FIND_REGISTER_START_WITH (plugin, "audio/x-flac", GST_RANK_PRIMARY,
	  flac_exts, "fLaC", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER (plugin, "video/x-fli", GST_RANK_MARGINAL,
	  flx_type_find, flx_exts, FLX_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "application/x-id3", GST_RANK_PRIMARY,
	  id3_type_find, id3_exts, ID3_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "audio/x-mod", GST_RANK_SECONDARY,
	  mod_type_find, mod_exts, MOD_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "audio/mpeg", GST_RANK_PRIMARY,
	  mp3_type_find, mp3_exts, MP3_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "video/mpeg1", GST_RANK_PRIMARY,
	  mpeg1_sys_type_find, mpeg_sys_exts, MPEG_SYS_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "video/mpeg2", GST_RANK_SECONDARY,
	  mpeg2_sys_type_find, mpeg_sys_exts, MPEG_SYS_CAPS, NULL);
  TYPE_FIND_REGISTER_START_WITH (plugin, "application/ogg", GST_RANK_PRIMARY,
	  ogg_exts, "OggS", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER (plugin, "video/mpeg", GST_RANK_SECONDARY,
	  mpeg_video_type_find, mpeg_video_exts, MPEG_VIDEO_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "video/quicktime", GST_RANK_SECONDARY,
	  qt_type_find, qt_exts, QT_CAPS, NULL);
  TYPE_FIND_REGISTER_START_WITH (plugin, "application/vnd.rn-realmedia", GST_RANK_SECONDARY, 
	  rm_exts, ".RMF", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER (plugin, "application/x-shockwave-flash", GST_RANK_SECONDARY,
	  swf_type_find, swf_exts, SWF_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "text/plain", GST_RANK_MARGINAL,
	  utf8_type_find, utf8_exts, UTF8_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "text/uri-list", GST_RANK_MARGINAL,
	  uri_type_find, uri_exts, URI_CAPS, NULL);
  TYPE_FIND_REGISTER_RIFF (plugin, "audio/x-wav", GST_RANK_PRIMARY, wav_exts, "WAVE");
  TYPE_FIND_REGISTER (plugin, "audio/x-aiff", GST_RANK_SECONDARY,
	  aiff_type_find, aiff_exts, AIFF_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "audio/x-shorten", GST_RANK_SECONDARY,
	  shn_type_find, shn_exts, SHN_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "image/jpeg", GST_RANK_PRIMARY,
	  jpeg_type_find, jpeg_exts, JPEG_CAPS, NULL);
  TYPE_FIND_REGISTER_START_WITH (plugin, "image/gif", GST_RANK_PRIMARY,
	  gif_exts, "GIF8", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "image/png", GST_RANK_PRIMARY,
	  png_exts, "\211PNG\015\012\032\012", 8, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER (plugin, "image/bmp", GST_RANK_PRIMARY,
	  bmp_type_find, bmp_exts, BMP_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "image/tiff", GST_RANK_PRIMARY,
	  tiff_type_find, tiff_exts, TIFF_CAPS, NULL);
  TYPE_FIND_REGISTER_START_WITH (plugin, "video/x-matroska", GST_RANK_SECONDARY,
	  matroska_exts, "\032\105\337\243\223\102\202\210matroska", 16, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER (plugin, "video/x-dv", GST_RANK_SECONDARY,
	  dv_type_find, dv_exts, DV_CAPS, NULL);
  TYPE_FIND_REGISTER_START_WITH (plugin, "audio/x-sid", GST_RANK_MARGINAL,
	  sid_exts, "PSID", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "image/x-xcf", GST_RANK_SECONDARY,
	  xcf_exts, "gimp xcf", 8, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "video/x-mng", GST_RANK_SECONDARY,
	  mng_exts, "\212MNG\015\012\032\012", 8, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "image/x-jng", GST_RANK_SECONDARY,
	  jng_exts, "\213JNG\015\012\032\012", 8, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "image/x-xpixmap", GST_RANK_SECONDARY,
	  xpm_exts, "/* XPM */", 9, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "image/x-sun-raster", GST_RANK_SECONDARY,
	  ras_exts, "\131\246\152\225", 4, GST_TYPE_FIND_MAXIMUM);
  TYPE_FIND_REGISTER_START_WITH (plugin, "application/x-bzip", GST_RANK_SECONDARY,
	  bz2_exts, "BZh", 3, GST_TYPE_FIND_LIKELY);
  TYPE_FIND_REGISTER_START_WITH (plugin, "application/x-gzip", GST_RANK_SECONDARY,
	  gz_exts, "\037\213", 2, GST_TYPE_FIND_LIKELY);
  TYPE_FIND_REGISTER_START_WITH (plugin, "application/zip", GST_RANK_SECONDARY,
	  zip_exts, "PK\003\004", 4, GST_TYPE_FIND_LIKELY);
  TYPE_FIND_REGISTER_START_WITH (plugin, "application/x-compress", GST_RANK_SECONDARY,
	  compress_exts, "\037\235", 2, GST_TYPE_FIND_LIKELY);
  TYPE_FIND_REGISTER (plugin, "audio/x-vorbis", GST_RANK_PRIMARY,
	  vorbis_type_find, NULL, VORBIS_CAPS, NULL);
  TYPE_FIND_REGISTER (plugin, "audio/x-m4a", GST_RANK_PRIMARY,
	  m4a_type_find, m4a_exts, AAC_CAPS, NULL);
  
  return TRUE;
}
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "typefindfunctions",
  "default typefind functions",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)

