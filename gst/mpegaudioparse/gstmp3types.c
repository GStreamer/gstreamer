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

/*#define DEBUG_ENABLED */
#include <gst/gst.h>
#include <string.h> /* memcmp */

static GstCaps* mp3_type_find(GstBuffer *buf, gpointer private);
static GstCaps* mp3_type_find_stream(GstBuffer *buf, gpointer private);

static GstTypeDefinition mp3type_definitions[] = {
  { "mp3types_audio/x-mp3", "audio/x-mp3", ".mp3 .mp2 .mp1 .mpga", mp3_type_find },
  { "mp3types_stream_audio/x-mp3", "audio/x-mp3", ".mp3 .mp2 .mp1 .mpga", mp3_type_find_stream },
  { NULL, NULL, NULL, NULL },
};

static GstCaps* 
mp3_type_find(GstBuffer *buf, gpointer private) 
{
  guint8 *data;
  gint size;
  guint32 head;
  GstCaps *caps;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG (0,"mp3typefind: typefind");

  /* gracefully ripped from libid3 */
  if (size >= 3 &&
      data[0] == 'T' && data[1] == 'A' && data[2] == 'G') {
    /* ID V1 tags */
    data += 128;
    size -= 128;

    GST_DEBUG (0, "mp3typefind: detected ID3 Tag V1");
  } else if (size >= 10 &&
        (data[0] == 'I' && data[1] == 'D' && data[2] == '3') &&
        data[3] < 0xff && data[4] < 0xff &&
        data[6] < 0x80 && data[7] < 0x80 && data[8] < 0x80 && data[9] < 0x80)
  {
    guint32 skip = 0;

    skip = (skip << 7) | (data[6] & 0x7f);
    skip = (skip << 7) | (data[7] & 0x7f);
    skip = (skip << 7) | (data[8] & 0x7f);
    skip = (skip << 7) | (data[9] & 0x7f);

    /* include size of header */
    skip += 10;
    /* footer present? (only available since version 4) */
    if (data[3] > 3 && (data[5] & 0x10))
      skip += 10;

    GST_DEBUG (0, "mp3typefind: detected ID3 Tag V2 with %u bytes", skip);
    size -= skip;
    data += skip;      
  }

  if (size < 4)
    return NULL;
  
  /* now with the right postion, do typefinding */
  head = GUINT32_FROM_BE(*((guint32 *)data));
  if ((head & 0xffe00000) != 0xffe00000)
    return NULL;
  if (!((head >> 17) & 3))
    return NULL;
  if (((head >> 12) & 0xf) == 0xf)
    return NULL;
  if (!((head >> 12) & 0xf))
    return NULL;
  if (((head >> 10) & 0x3) == 0x3)
    return NULL;

  caps = gst_caps_new ("mp3_type_find", "audio/x-mp3", NULL);

  return caps;
}
static guint mp3types_bitrates[2][3][16] =
{ { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, },
    {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, },
    {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, } },
  { {0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, },
    {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, },
    {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, } },
};
static guint mp3types_freqs[3][3] =
{ {44100, 48000, 32000},
  {22050, 24000, 16000}, 
  {11025, 12000,  8000}};
static inline guint
mp3_type_frame_length_from_header (guint32 header)
{
  guint length;
  gulong samplerate, bitrate, layer, version;

  /* we don't need extension, mode, copyright, original or emphasis for the frame length */
  header >>= 9;
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
  /* version */
  version = header & 0x3;
  if (version == 1)
    return 0;
  /* lookup */
  bitrate = mp3types_bitrates[version == 3 ? 0 : 1][layer - 1][bitrate];
  samplerate = mp3types_freqs[version > 0 ? version - 1 : 0][samplerate];
  /* calculating */
  if (layer == 1) {
    length = ((12000 * bitrate / samplerate) + length) * 4;
  } else {
    length += ((layer == 3 && version == 0) ? 144000 : 72000) * bitrate / samplerate;
  }

  GST_DEBUG (0, "Calculated mad frame length of %u bytes", length);
  GST_DEBUG (0, "samplerate = %lu - bitrate = %lu - layer = %lu - version = %lu", samplerate, bitrate, layer, version);
  return length;

}
/* increase this value when this function finds too many false positives */
/**
 * The chance that random data is identified as a valid mp3 header is 63 / 2^18
 * (0.024%) per try. This makes the function for calculating false positives
 *   1 - (1 - ((63 / 2 ^18) ^ GST_MP3_TYPEFIND_MIN_HEADERS)) ^ buffersize)
 * This has the following probabilities of false positives:
 * bufsize	          MIN_HEADERS
 * (bytes)	1	2	3	4
 * 4096		62.6%	 0.02%	 0%	 0%
 * 16384	98%	 0.09%	 0%	 0%
 * 1 MiB       100%	 5.88%	 0%	 0%
 * 1 GiB       100%    100%	 1.44%   0%
 * 1 TiB       100%    100%    100%      0.35%
 * This means that the current choice (3 headers by most of the time 4096 byte
 * buffers is pretty safe for now.
 * It is however important to note that in a worst case example a buffer of size
 *   1440 * GST_MP3_TYPEFIND_MIN_HEADERS + 3
 * bytes is needed to reliable find the mp3 stream in a buffer when scanning
 * starts at a random position. This is currently (4323 bytes) slightly above 
 * the default buffer size. But you rarely hit the worst case - average mp3 
 * frames are in the 500 bytes range.
 */
#define GST_MP3_TYPEFIND_MIN_HEADERS 3
static GstCaps* 
mp3_type_find_stream (GstBuffer *buf, gpointer private)
{
  guint8 *data;
  guint size;
  guint32 head;
  
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  
  while (size >= 4) {
    head = GUINT32_FROM_BE(*((guint32 *)data));
    if ((head & 0xffe00000) == 0xffe00000) {
      guint pos = 0;
      guint length;
      guint found = 0; /* number of valid headers found */
      do {
        if ((length = mp3_type_frame_length_from_header (head))) {
	  pos += length;
          found++;
	  if (pos + 4 >= size) {
	    if (found >= GST_MP3_TYPEFIND_MIN_HEADERS) 
	      goto success;
	  }
          head = GUINT32_FROM_BE(*((guint32 *) &(data[pos])));  
          if ((head & 0xffe00000) != 0xffe00000)
	    break;
	} else {
	  break;
	}
      } while (TRUE);
    }
    data++;
    size--;
  }

  return NULL;

success:
  return gst_caps_new ("mp3_type_find", "audio/x-mp3", NULL);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gint i=0;

  while (mp3type_definitions[i].name) {
    GstTypeFactory *type;

    type = gst_type_factory_new (&mp3type_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }

  /* gst_info("gsttypes: loaded %d mp3 types\n",i); */

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mp3types",
  plugin_init
};
