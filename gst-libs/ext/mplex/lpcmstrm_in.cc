/*
 *  lpcmstrm_in.c: LPCM Audio strem class members handling scanning and
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
#include <string.h>

#include "audiostrm.hh"
#include "outputstream.hh"
#include <cassert>




LPCMStream::LPCMStream (IBitStream & ibs, OutputStream & into):
AudioStream (ibs, into)
{
}

bool LPCMStream::Probe (IBitStream & bs)
{
  return true;
}


/*************************************************************************
 *
 * Reads initial stream parameters and displays feedback banner to users
 *
 *************************************************************************/


void
LPCMStream::Init (const int stream_num)
{

  MuxStream::Init (PRIVATE_STR_1, 1,	// Buffer scale
		   default_buffer_size,
		   muxinto.vcd_zero_stuffing,
		   muxinto.buffers_in_audio, muxinto.always_buffers_in_audio);
  mjpeg_info ("Scanning for header info: LPCM Audio stream %02x", stream_num);

  InitAUbuffer ();

  AU_start = bs.bitcount ();

  // This is a dummy debug version that simply assumes 48kHz
  // two channel 16 bit sample LPCM
  samples_per_second = 48000;
  channels = 2;
  bits_per_sample = 16;
  bytes_per_frame =
    samples_per_second * channels * bits_per_sample / 8 * ticks_per_frame_90kHz / 90000;
  frame_index = 0;
  dynamic_range_code = 0x80;

  /* Presentation/decoding time-stamping  */
  access_unit.start = AU_start;
  access_unit.length = bytes_per_frame;
  access_unit.PTS = static_cast < clockticks > (decoding_order) *
    (CLOCKS_per_90Kth_sec * ticks_per_frame_90kHz);
  access_unit.DTS = access_unit.PTS;
  access_unit.dorder = decoding_order;
  decoding_order++;
  aunits.append (access_unit);

  OutputHdrInfo ();
}

unsigned int
LPCMStream::NominalBitRate ()
{
  return samples_per_second * channels * bits_per_sample;
}



void
LPCMStream::FillAUbuffer (unsigned int frames_to_buffer)
{
  last_buffered_AU += frames_to_buffer;
  mjpeg_debug ("Scanning %d MPEG LPCM audio frames to frame %d",
	       frames_to_buffer, last_buffered_AU);

  static int header_skip = 0;	// Initially skipped past  5 bytes of header 
  int skip;
  bool bad_last_frame = false;

  while (!bs.eos () &&
	 decoding_order < last_buffered_AU) {
    skip = access_unit.length - header_skip;
    mjpeg_debug ("Buffering frame %d (%d bytes)\n", decoding_order - 1, skip);
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
    // Here we would check for header data but LPCM has no headers...
    if (bs.eos ())
      break;

    access_unit.start = AU_start;
    access_unit.length = bytes_per_frame;
    access_unit.PTS = static_cast < clockticks > (decoding_order) *
      (CLOCKS_per_90Kth_sec * ticks_per_frame_90kHz);
    access_unit.DTS = access_unit.PTS;
    access_unit.dorder = decoding_order;
    decoding_order++;
    aunits.append (access_unit);
    num_frames[0]++;

    num_syncword++;

    if (num_syncword >= old_frames + 10) {
      mjpeg_debug ("Got %d frame headers.", num_syncword);
      old_frames = num_syncword;
    }
    mjpeg_debug ("Got frame %d\n", decoding_order);

  }
  if (bad_last_frame) {
    mjpeg_error_exit1 ("Last LPCM frame ended prematurely!\n");
  }
  last_buffered_AU = decoding_order;
  eoscan = bs.eos ();

}



void
LPCMStream::Close ()
{
  stream_length = AU_start / 8;
  mjpeg_info ("AUDIO_STATISTICS: %02x", stream_id);
  mjpeg_info ("Audio stream length %lld bytes.", stream_length);
  mjpeg_info ("Frames         : %8u ", num_frames[0]);
  bs.close ();
}

/*************************************************************************
	OutputAudioInfo
	gibt gesammelte Informationen zu den Audio Access Units aus.

	Prints information on audio access units
*************************************************************************/

void
LPCMStream::OutputHdrInfo ()
{
  mjpeg_info ("LPCM AUDIO STREAM:");

  mjpeg_info ("Bit rate       : %8u bytes/sec (%3u kbit/sec)",
	      NominalBitRate () / 8, NominalBitRate ());
  mjpeg_info ("Channels       :     %d\n", channels);
  mjpeg_info ("Bits per sample:     %d\n", bits_per_sample);
  mjpeg_info ("Frequency      :     %d Hz", samples_per_second);

}


unsigned int
LPCMStream::ReadPacketPayload (uint8_t * dst, unsigned int to_read)
{
  unsigned int header_size = LPCMStream::StreamHeaderSize ();
  unsigned int bytes_read = bs.read_buffered_bytes (dst + header_size,
						    to_read - header_size);
  clockticks decode_time;
  bool starting_frame_found = false;
  uint8_t starting_frame_index = 0;

  int starting_frame_offset = (new_au_next_sec || au_unsent > bytes_read)
    ? 0 : au_unsent;

  unsigned int frames = 0;
  unsigned int bytes_muxed = bytes_read;

  if (bytes_muxed == 0 || MuxCompleted ()) {
    goto completion;
  }


  /* Work through what's left of the current frames and the
     following frames's updating the info until we reach a point where
     an frame had to be split between packets. 

     The DTS/PTS field for the packet in this case would have been
     given the that for the first AU to start in the packet.

   */

  decode_time = RequiredDTS ();
  while (au_unsent < bytes_muxed) {
    assert (bytes_muxed > 1);
    bufmodel.Queued (au_unsent, decode_time);
    bytes_muxed -= au_unsent;
    if (new_au_next_sec) {
      ++frames;
      if (!starting_frame_found) {
	starting_frame_index = static_cast < uint8_t > (au->dorder % 20);
	starting_frame_found = true;
      }
    }
    if (!NextAU ()) {
      goto completion;
    }
    new_au_next_sec = true;
    decode_time = RequiredDTS ();
  };

  // We've now reached a point where the current AU overran or
  // fitted exactly.  We need to distinguish the latter case so we
  // can record whether the next packet starts with the tail end of
  // // an already started frame or a new one. We need this info to
  // decide what PTS/DTS info to write at the start of the next
  // packet.

  if (au_unsent > bytes_muxed) {
    if (new_au_next_sec)
      ++frames;
    bufmodel.Queued (bytes_muxed, decode_time);
    au_unsent -= bytes_muxed;
    new_au_next_sec = false;
  } else			//  if (au_unsent == bytes_muxed)
  {
    bufmodel.Queued (bytes_muxed, decode_time);
    if (new_au_next_sec)
      ++frames;
    new_au_next_sec = NextAU ();
  }
completion:
  // Generate the LPCM header...
  // Note the index counts from the low byte of the offset so
  // the smallest value is 1!
  dst[0] = LPCM_SUB_STR_0 + stream_num;
  dst[1] = frames;
  dst[2] = (starting_frame_offset + 1) >> 8;
  dst[3] = (starting_frame_offset + 1) & 0xff;
  unsigned int bps_code;

  switch (bits_per_sample) {
    case 16:
      bps_code = 0;
      break;
    case 20:
      bps_code = 1;
      break;
    case 24:
      bps_code = 2;
      break;
    default:
      bps_code = 3;
      break;
  }
  dst[4] = starting_frame_index;
  unsigned int bsf_code = (samples_per_second == 48000) ? 0 : 1;
  unsigned int channels_code = channels - 1;

  dst[5] = (bps_code << 6) | (bsf_code << 4) | channels_code;
  dst[6] = dynamic_range_code;
  return bytes_read + header_size;
}



/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
