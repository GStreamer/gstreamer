/*
 *  inptstrm.c:  Members of video stream class related to raw stream
 *               scanning and buffering.
 *
 *  Copyright (C) 2001 Andrew Stevens <andrew.stevens@philips.com>
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

#include "videostrm.hh"
#include "outputstream.hh"



static void
marker_bit (IBitStream & bs, unsigned int what)
{
  if (what != bs.get1bit ()) {
    mjpeg_error ("Illegal MPEG stream at offset (bits) %lld: supposed marker bit not found.",
		 bs.bitcount ());
    exit (1);
  }
}


void
VideoStream::ScanFirstSeqHeader ()
{
  if (bs.getbits (32) == SEQUENCE_HEADER) {
    num_sequence++;
    horizontal_size = bs.getbits (12);
    vertical_size = bs.getbits (12);
    aspect_ratio = bs.getbits (4);
    pict_rate = bs.getbits (4);
    picture_rate = pict_rate;
    bit_rate = bs.getbits (18);
    marker_bit (bs, 1);
    vbv_buffer_size = bs.getbits (10);
    CSPF = bs.get1bit ();

  } else {
    mjpeg_error ("Invalid MPEG Video stream header.");
    exit (1);
  }

  if (pict_rate > 0 && pict_rate <= mpeg_num_framerates) {
    frame_rate = Y4M_RATIO_DBL (mpeg_framerate (pict_rate));
  } else {
    frame_rate = 25.0;
  }

}




void
VideoStream::Init (const int stream_num)
{
  mjpeg_debug ("SETTING video buffer to %d", muxinto.video_buffer_size);
  MuxStream::Init (VIDEO_STR_0 + stream_num, 1,	// Buffer scale
		   muxinto.video_buffer_size, 0,	// Zero stuffing
		   muxinto.buffers_in_video, muxinto.always_buffers_in_video);
  mjpeg_info ("Scanning for header info: Video stream %02x ", VIDEO_STR_0 + stream_num);
  InitAUbuffer ();

  ScanFirstSeqHeader ();

  /* Skip to the end of the 1st AU (*2nd* Picture start!)
   */
  AU_hdr = SEQUENCE_HEADER;
  AU_pict_data = 0;
  AU_start = 0LL;

  OutputSeqhdrInfo ();
}

//
// Set the Maximum STD buffer delay for this video stream.
// By default we set 1 second but if we have specified a video
// buffer that can hold more than 1.0 seconds demuxed data we
// set the delay to the time to fill the buffer.
//

void
VideoStream::SetMaxStdBufferDelay (unsigned int dmux_rate)
{
  double max_delay = CLOCKS;

  if (static_cast < double >(BufferSize ()) / dmux_rate > 1.0)
    max_delay *= static_cast < double >(BufferSize ()) / dmux_rate;

  //
  // To enforce a maximum STD buffer residency the
  // calculation is a bit tricky as when we decide to mux we may
  // (but not always) have some of the *previous* picture left to
  // mux in which case it is the timestamp of the next picture that counts.
  // For simplicity we simply reduce the limit by 1.5 frame intervals
  // and use the timestamp for the current picture.
  //
  if (frame_rate > 10.0)
    max_STD_buffer_delay = static_cast < clockticks > (max_delay * (frame_rate - 1.5) / frame_rate);
  else
    max_STD_buffer_delay = static_cast < clockticks > (10.0 * max_delay / frame_rate);

}

//
// Return whether AU buffer needs refilling.  There are two cases:
// 1. We have less than our look-ahead "FRAME_CHUNK" buffer AU's
// buffered 2. AU's are very small and we could have less than 1
// sector's worth of data buffered.
//

bool
VideoStream::AUBufferNeedsRefill ()
{
  return
    !eoscan
    && (aunits.current () + FRAME_CHUNK > last_buffered_AU
	|| bs.buffered_bytes () < muxinto.sector_size);
}

//
// Refill the AU unit buffer setting  AU PTS DTS from the scanned
// header information...
//

void
VideoStream::FillAUbuffer (unsigned int frames_to_buffer)
{
  if (eoscan)
    return;

  last_buffered_AU += frames_to_buffer;
  mjpeg_debug ("Scanning %d video frames to frame %d", frames_to_buffer, last_buffered_AU);

  // We set a limit of 2M to seek before we give up.
  // This is intentionally very high because some heavily
  // padded still frames may have a loooong gap before
  // a following sequence end marker.
  while (!bs.eos () &&
	 bs.seek_sync (SYNCWORD_START, 24, 2 * 1024 * 1024) &&
	 decoding_order < last_buffered_AU)
  {
    syncword = (SYNCWORD_START << 8) + bs.getbits (8);
    if (AU_pict_data) {

      /* Handle the header *ending* an AU...
         If we have the AU picture data an AU and have now
         reached a header marking the end of an AU fill in the
         the AU length and append it to the list of AU's and
         start a new AU.  I.e. sequence and gop headers count as
         part of the AU of the corresponding picture
       */
      stream_length = bs.bitcount () - 32LL;
      switch (syncword) {
	case SEQUENCE_HEADER:
	case GROUP_START:
	case PICTURE_START:
	  access_unit.start = AU_start;
	  access_unit.length = (int) (stream_length - AU_start) >> 3;
	  access_unit.end_seq = 0;
	  avg_frames[access_unit.type - 1] += access_unit.length;
	  aunits.append (access_unit);
	  mjpeg_debug ("Found AU %d: DTS=%d", access_unit.dorder, (int) access_unit.DTS / 300);
	  AU_hdr = syncword;
	  AU_start = stream_length;
	  AU_pict_data = 0;
	  break;
	case SEQUENCE_END:
	  access_unit.length = ((stream_length - AU_start) >> 3) + 4;
	  access_unit.end_seq = 1;
	  aunits.append (access_unit);
	  mjpeg_info ("Scanned to end AU %d", access_unit.dorder);
	  avg_frames[access_unit.type - 1] += access_unit.length;

	  /* Do we have a sequence split in the video stream? */
	  if (!bs.eos () && bs.getbits (32) == SEQUENCE_HEADER) {
	    stream_length = bs.bitcount () - 32LL;
	    AU_start = stream_length;
	    syncword = AU_hdr = SEQUENCE_HEADER;
	    AU_pict_data = 0;
	    if (opt_multifile_segment)
	      mjpeg_warn
		("Sequence end marker found in video stream but single-segment splitting specified!");
	  } else {
	    if (!bs.eos () && !opt_multifile_segment)
	      mjpeg_warn ("No seq. header starting new sequence after seq. end!");
	  }

	  num_seq_end++;
	  break;
      }
    }

    /* Handle the headers starting an AU... */
    switch (syncword) {
      case SEQUENCE_HEADER:
	/* TODO: Really we should update the info here so we can handle
	   streams where parameters change on-the-fly... */
	num_sequence++;
	break;

      case GROUP_START:
	num_groups++;
	group_order = 0;
	break;

      case PICTURE_START:
	/* We have reached AU's picture data... */
	AU_pict_data = 1;

	prev_temp_ref = temporal_reference;
	temporal_reference = bs.getbits (10);
	access_unit.type = bs.getbits (3);

	/* Now scan forward a little for an MPEG-2 picture coding extension
	   so we can get pulldown info (if present) */
	if (bs.seek_sync (EXT_START_CODE, 32, 64) && bs.getbits (4) == CODING_EXT_ID) {
	  /* Skip: 4 F-codes (4)... */
	  (void) bs.getbits (16);
	  /* Skip: DC Precision(2) */
	  (void) bs.getbits (2);
	  pict_struct = bs.getbits (2);
	  /* Skip: topfirst (1) frame pred dct (1),
	     concealment_mv(1), q_scale_type (1), */
	  (void) bs.getbits (4);
	  /* Skip: intra_vlc_format(1), alternate_scan (1) */
	  (void) bs.getbits (2);
	  repeat_first_field = bs.getbits (1);
	  pulldown_32 |= repeat_first_field;

	} else {
	  repeat_first_field = 0;
	  pict_struct = PIC_FRAME;
	}

	if (access_unit.type == IFRAME) {
	  unsigned int bits_persec =
	    (unsigned int) (((double) (stream_length - prev_offset)) *
			    2 * frame_rate / ((double) (2 + fields_presented - group_start_field)));

	  if (bits_persec > max_bits_persec) {
	    max_bits_persec = bits_persec;
	  }
	  prev_offset = stream_length;
	  group_start_pic = decoding_order;
	  group_start_field = fields_presented;
	}

	NextDTSPTS (access_unit.DTS, access_unit.PTS);

	access_unit.dorder = decoding_order;
	access_unit.porder = temporal_reference + group_start_pic;
	access_unit.seq_header = (AU_hdr == SEQUENCE_HEADER);

	decoding_order++;
	group_order++;

	if ((access_unit.type > 0) && (access_unit.type < 5)) {
	  num_frames[access_unit.type - 1]++;
	}


	if (decoding_order >= old_frames + 1000) {
	  mjpeg_debug ("Got %d picture headers.", decoding_order);
	  old_frames = decoding_order;
	}

	break;



    }
  }
  last_buffered_AU = decoding_order;
  num_pictures = decoding_order;
  eoscan = bs.eos ();
}

void
VideoStream::Close ()
{

  bs.close ();
  stream_length = (unsigned int) (AU_start / 8);
  for (int i = 0; i < 4; i++) {
    avg_frames[i] /= num_frames[i] == 0 ? 1 : num_frames[i];
  }

  comp_bit_rate = (unsigned int)
    ((((double) stream_length) / ((double) fields_presented)) * 2.0
     * ((double) frame_rate) + 25.0) / 50;

  /* Peak bit rate in 50B/sec units... */
  peak_bit_rate = ((max_bits_persec / 8) / 50);
  mjpeg_info ("VIDEO_STATISTICS: %02x", stream_id);
  mjpeg_info ("Video Stream length: %11llu bytes", stream_length / 8);
  mjpeg_info ("Sequence headers: %8u", num_sequence);
  mjpeg_info ("Sequence ends   : %8u", num_seq_end);
  mjpeg_info ("No. Pictures    : %8u", num_pictures);
  mjpeg_info ("No. Groups      : %8u", num_groups);
  mjpeg_info ("No. I Frames    : %8u avg. size%6u bytes", num_frames[0], avg_frames[0]);
  mjpeg_info ("No. P Frames    : %8u avg. size%6u bytes", num_frames[1], avg_frames[1]);
  mjpeg_info ("No. B Frames    : %8u avg. size%6u bytes", num_frames[2], avg_frames[2]);
  mjpeg_info ("No. D Frames    : %8u avg. size%6u bytes", num_frames[3], avg_frames[3]);
  mjpeg_info ("Average bit-rate : %8u bits/sec", comp_bit_rate * 400);
  mjpeg_info ("Peak bit-rate    : %8u  bits/sec", peak_bit_rate * 400);

}




/*************************************************************************
	OutputSeqHdrInfo
     Display sequence header parameters
*************************************************************************/

void
VideoStream::OutputSeqhdrInfo ()
{
  const char *str;

  mjpeg_info ("VIDEO STREAM: %02x", stream_id);

  mjpeg_info ("Frame width     : %u", horizontal_size);
  mjpeg_info ("Frame height    : %u", vertical_size);
  if (aspect_ratio <= mpeg_num_aspect_ratios[opt_mpeg - 1])
    str = mpeg_aspect_code_definition (opt_mpeg, aspect_ratio);
  else
    str = "forbidden";
  mjpeg_info ("Aspect ratio    : %s", str);


  if (picture_rate == 0)
    mjpeg_info ("Picture rate    : forbidden");
  else if (picture_rate <= mpeg_num_framerates)
    mjpeg_info ("Picture rate    : %2.3f frames/sec",
		Y4M_RATIO_DBL (mpeg_framerate (picture_rate)));
  else
    mjpeg_info ("Picture rate    : %x reserved", picture_rate);

  if (bit_rate == 0x3ffff) {
    bit_rate = 0;
    mjpeg_info ("Bit rate        : variable");
  } else if (bit_rate == 0)
    mjpeg_info ("Bit rate       : forbidden");
  else
    mjpeg_info ("Bit rate        : %u bits/sec", bit_rate * 400);

  mjpeg_info ("Vbv buffer size : %u bytes", vbv_buffer_size * 2048);
  mjpeg_info ("CSPF            : %u", CSPF);
}

//
// Compute PTS DTS of current AU in the video sequence being
// scanned.  This is is the PTS/DTS calculation for normal video only.
// It is virtual and over-ridden for non-standard streams (Stills
// etc!).
//

void
VideoStream::NextDTSPTS (clockticks & DTS, clockticks & PTS)
{
  if (pict_struct != PIC_FRAME) {
    DTS = static_cast < clockticks > (fields_presented * (double) (CLOCKS / 2) / frame_rate);
    int dts_fields = temporal_reference * 2 + group_start_field + 1;

    if (temporal_reference == prev_temp_ref)
      dts_fields += 1;
    PTS = static_cast < clockticks > (dts_fields * (double) (CLOCKS / 2) / frame_rate);
    access_unit.porder = temporal_reference + group_start_pic;
    fields_presented += 1;
  } else if (pulldown_32) {
    int frames2field;
    int frames3field;

    DTS = static_cast < clockticks > (fields_presented * (double) (CLOCKS / 2) / frame_rate);
    if (repeat_first_field) {
      frames2field = (temporal_reference + 1) / 2;
      frames3field = temporal_reference / 2;
      fields_presented += 3;
    } else {
      frames2field = (temporal_reference) / 2;
      frames3field = (temporal_reference + 1) / 2;
      fields_presented += 2;
    }
    PTS = static_cast < clockticks >
      ((frames2field * 2 + frames3field * 3 + group_start_field +
	1) * (double) (CLOCKS / 2) / frame_rate);
    access_unit.porder = temporal_reference + group_start_pic;
  } else {
    DTS = static_cast < clockticks > (decoding_order * (double) CLOCKS / frame_rate);
    PTS = static_cast < clockticks >
      ((temporal_reference + group_start_pic + 1) * (double) CLOCKS / frame_rate);
    fields_presented += 2;
  }

}





/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
