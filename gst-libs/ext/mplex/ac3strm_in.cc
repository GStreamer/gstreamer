/*
 *  ac3strm_in.c: AC3 Audio strem class members handling scanning and
 *  buffering raw input stream.
 *
 *  Copyright (C) 2001 Andrew Stevens <andrew.stevens@philips.com>
 *  Copyright (C) 2000,2001 Brent Byeler for original header-structure
 *                          parsing code.
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <config.h>
#include <math.h>
#include <stdlib.h>

#include "audiostrm.hh"
#include "outputstream.hh"
#include <cassert>



#define AC3_SYNCWORD            0x0b77
#define AC3_PACKET_SAMPLES      1536

/// table for the available AC3 bitrates
static const unsigned int ac3_bitrate_index[32] = 
{ 
  32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192,
  224, 256, 320, 384, 448, 512, 576, 640,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned int ac3_frame_size[3][32] = 
{
  {64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,
   448, 512, 640, 768, 896, 1024, 1152, 1280,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {69, 87, 104, 121, 139, 174, 208, 243, 278, 348, 417,
   487, 557, 696, 835, 975, 1114, 1253, 1393,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {96, 120, 144, 168, 192, 240, 288, 336, 384, 480, 576,
   672, 768, 960, 1152, 1344, 1536, 1728, 1920,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

/// table for the available AC3 frequencies
static const unsigned int ac3_frequency[4] = { 48000, 44100, 32000, 0 };


AC3Stream::AC3Stream (IBitStream & ibs, OutputStream & into):
AudioStream (ibs, into)
{
}

bool
AC3Stream::Probe (IBitStream & bs)
{
  return bs.getbits (16) == AC3_SYNCWORD;
}


/*************************************************************************
 *
 * Reads initial stream parameters and displays feedback banner to users
 * @param stream_num AC3 substream ID
 *************************************************************************/


void
AC3Stream::Init (const int stream_num)
{
  unsigned int framesize_code;

  this->stream_num = stream_num;

  MuxStream::Init (PRIVATE_STR_1, 1,	// Buffer scale
		   default_buffer_size,
		   muxinto.vcd_zero_stuffing,
		   muxinto.buffers_in_audio, muxinto.always_buffers_in_audio);
  mjpeg_info ("Scanning for header info: AC3 Audio stream %02x", stream_num);

  InitAUbuffer ();
  AU_start = bs.bitcount ();
  if (bs.getbits (16) == AC3_SYNCWORD) {
    num_syncword++;
    bs.getbits (16);		// CRC field
    frequency = bs.getbits (2);	// Sample rate code
    framesize_code = bs.getbits (6);	// Frame size code
    framesize = ac3_frame_size[frequency][framesize_code >> 1];
    framesize = (framesize_code & 1) && frequency == 1 ? (framesize + 1) << 1 : (framesize << 1);


    size_frames[0] = framesize;
    size_frames[1] = framesize;
    num_frames[0]++;
    access_unit.start = AU_start;
    access_unit.length = framesize;
    bit_rate = ac3_bitrate_index[framesize_code >> 1];
    samples_per_second = ac3_frequency[frequency];


    /* Presentation time-stamping  */
    access_unit.PTS = static_cast < clockticks > (decoding_order) *
      static_cast < clockticks > (AC3_PACKET_SAMPLES) *
      static_cast < clockticks > (CLOCKS) / samples_per_second;
    access_unit.DTS = access_unit.PTS;
    access_unit.dorder = decoding_order;
    ++decoding_order;
    aunits.append (access_unit);

  } else {
    mjpeg_error ("Invalid AC3 Audio stream header.");
    exit (1);
  }

  OutputHdrInfo ();
}

/// @returns the current bitrate
unsigned int
AC3Stream::NominalBitRate ()
{
  return bit_rate;
}

/// Prefills the internal buffer for output multiplexing.
/// @param frames_to_buffer the number of audio frames to read ahead
void
AC3Stream::FillAUbuffer (unsigned int frames_to_buffer)
{
  unsigned int framesize_code;

  last_buffered_AU += frames_to_buffer;
  mjpeg_debug ("Scanning %d MPEG audio frames to frame %d", frames_to_buffer, last_buffered_AU);

  static int header_skip = 5;	// Initially skipped past  5 bytes of header 
  int skip;
  bool bad_last_frame = false;

  while (!bs.eos () &&
	 decoding_order < last_buffered_AU) {
    skip = access_unit.length - header_skip;
    if (skip & 0x1)
      bs.getbits (8);
    if (skip & 0x2)
      bs.getbits (16);
    skip = skip >> 2;

    for (int i = 0; i < skip; i++) {
      bs.getbits (32);
    }

    prev_offset = AU_start;
    AU_start = bs.bitcount ();
    if (AU_start - prev_offset != access_unit.length * 8) {
      bad_last_frame = true;
      break;
    }

    /* Check we have reached the end of have  another catenated 
       stream to process before finishing ... */
    if ((syncword = bs.getbits (16)) != AC3_SYNCWORD) {
      if (!bs.eos ()) {
	mjpeg_error_exit1 ("Can't find next AC3 frame - broken bit-stream?");
      }
      break;
    }

    bs.getbits (16);		// CRC field
    bs.getbits (2);		// Sample rate code TOOD: check for change!
    framesize_code = bs.getbits (6);
    framesize = ac3_frame_size[frequency][framesize_code >> 1];
    framesize = (framesize_code & 1) && frequency == 1 ? (framesize + 1) << 1 : (framesize << 1);

    access_unit.start = AU_start;
    access_unit.length = framesize;
    access_unit.PTS = static_cast < clockticks > (decoding_order) *
      static_cast < clockticks > (AC3_PACKET_SAMPLES) *
      static_cast < clockticks > (CLOCKS) / samples_per_second;;
    access_unit.DTS = access_unit.PTS;
    access_unit.dorder = decoding_order;
    decoding_order++;
    aunits.append (access_unit);
    num_frames[0]++;

    num_syncword++;


#ifdef DEBUG_AC3_HEADERS
    /* Some stuff to generate frame-header information */
    printf ("bsid       = %d\n", bs.getbits (5));
    printf ("bsmode     = 0x%1x\n", bs.getbits (3));
    int acmode = bs.getbits (3);

    printf ("acmode     = 0x%1x\n", acmode);
    if ((acmode & 0x1) && (acmode != 1))
      printf ("cmixlev   = %d\n", bs.getbits (2));
    if ((acmode & 0x4))
      printf ("smixlev   = %d\n", bs.getbits (2));
    if (acmode == 2)
      printf ("dsurr     = %d\n", bs.getbits (2));
    printf ("lfeon      = %d\n", bs.getbits (1));
    printf ("dialnorm   = %02d\n", bs.getbits (5));
    int compre = bs.getbits (1);

    printf ("compre     = %d\n", compre);
    if (compre)
      printf ("compr    = %02d\n", bs.getbits (8));
    int langcode = bs.getbits (1);

    printf ("langcode     = %d\n", langcode);
    if (langcode)
      printf ("langcod  = 0x%02x\n", bs.getbits (8));

    while (bs.bitcount () % 8 != 0)
      bs.getbits (1);
    header_skip = (bs.bitcount () - AU_start) / 8;
#endif
    if (num_syncword >= old_frames + 10) {
      mjpeg_debug ("Got %d frame headers.", num_syncword);
      old_frames = num_syncword;
    }


  }
  if (bad_last_frame) {
    mjpeg_error_exit1 ("Last AC3 frame ended prematurely!\n");
  }
  last_buffered_AU = decoding_order;
  eoscan = bs.eos ();

}


/// Closes the AC3 stream and prints some statistics.
void
AC3Stream::Close ()
{
  stream_length = AU_start >> 3;
  mjpeg_info ("AUDIO_STATISTICS: %02x", stream_id);
  mjpeg_info ("Audio stream length %lld bytes.", stream_length);
  mjpeg_info ("Syncwords      : %8u", num_syncword);
  mjpeg_info ("Frames         : %8u padded", num_frames[0]);
  mjpeg_info ("Frames         : %8u unpadded", num_frames[1]);

  bs.close ();
}

/*************************************************************************
	OutputAudioInfo
	gibt gesammelte Informationen zu den Audio Access Units aus.

	Prints information on audio access units
*************************************************************************/

void
AC3Stream::OutputHdrInfo ()
{
  mjpeg_info ("AC3 AUDIO STREAM:");

  mjpeg_info ("Bit rate       : %8u bytes/sec (%3u kbit/sec)", bit_rate * 128, bit_rate);

  if (frequency == 3)
    mjpeg_info ("Frequency      : reserved");
  else
    mjpeg_info ("Frequency      :     %d Hz", ac3_frequency[frequency]);

}

/**
Reads the bytes neccessary to complete the current packet payload. 
@param to_read number of bytes to read
@param dst byte buffer pointer to read to 
@returns the number of bytes read
 */
unsigned int
AC3Stream::ReadPacketPayload (uint8_t * dst, unsigned int to_read)
{
  static unsigned int aus = 0;
  static unsigned int rd = 0;

  unsigned int bytes_read = bs.read_buffered_bytes (dst + 4, to_read - 4);

  rd += bytes_read;
  clockticks decode_time;

  unsigned int first_header = (new_au_next_sec || au_unsent > bytes_read)
    ? 0 : au_unsent;

  // BUG BUG BUG: how do we set the 1st header pointer if we have
  // the *middle* part of a large frame?
  assert (first_header <= to_read - 2);


  unsigned int syncwords = 0;
  unsigned int bytes_muxed = bytes_read;

  if (bytes_muxed == 0 || MuxCompleted ()) {
    goto completion;
  }


  /* Work through what's left of the current AU and the following AU's
     updating the info until we reach a point where an AU had to be
     split between packets.
     NOTE: It *is* possible for this loop to iterate. 

     The DTS/PTS field for the packet in this case would have been
     given the that for the first AU to start in the packet.

   */

  decode_time = RequiredDTS ();
  while (au_unsent < bytes_muxed) {
    // BUG BUG BUG: if we ever had odd payload / packet size we might
    // split an AC3 frame in the middle of the syncword!
    assert (bytes_muxed > 1);
    bufmodel.Queued (au_unsent, decode_time);
    bytes_muxed -= au_unsent;
    if (new_au_next_sec)
      ++syncwords;
    aus += au->length;
    if (!NextAU ()) {
      goto completion;
    }
    new_au_next_sec = true;
    decode_time = RequiredDTS ();
  };

  // We've now reached a point where the current AU overran or
  // fitted exactly.  We need to distinguish the latter case
  // so we can record whether the next packet starts with an
  // existing AU or not - info we need to decide what PTS/DTS
  // info to write at the start of the next packet.

  if (au_unsent > bytes_muxed) {
    if (new_au_next_sec)
      ++syncwords;
    bufmodel.Queued (bytes_muxed, decode_time);
    au_unsent -= bytes_muxed;
    new_au_next_sec = false;
  } else			//  if (au_unsent == bytes_muxed)
  {
    bufmodel.Queued (bytes_muxed, decode_time);
    if (new_au_next_sec)
      ++syncwords;
    aus += au->length;
    new_au_next_sec = NextAU ();
  }
completion:
  // Generate the AC3 header...
  // Note the index counts from the low byte of the offset so
  // the smallest value is 1!

  dst[0] = AC3_SUB_STR_0 + stream_num;
  dst[1] = syncwords;
  dst[2] = (first_header + 1) >> 8;
  dst[3] = (first_header + 1) & 0xff;

  return bytes_read + 4;
}



/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
