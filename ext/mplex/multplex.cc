
#include <config.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include <mjpeg_types.h>
#include <mjpeg_logging.h>
#include <format_codes.h>

#include "videostrm.hh"
#include "outputstream.hh"
#include <cassert>


/******************************************************************* 
	Find the timecode corresponding to given position in the system stream
   (assuming the SCR starts at 0 at the beginning of the stream 
@param bytepos byte position in the stream
@param ts returns the number of clockticks the bytepos is from the file start    
****************************************************************** */

void
OutputStream::ByteposTimecode (bitcount_t bytepos, clockticks & ts)
{
  ts = (bytepos * CLOCKS) / static_cast < bitcount_t > (dmux_rate);
}


/**********
 *
 * UpdateSectorHeaders - Update the sector headers after a change in stream
 *                     position / SCR
 **********

 **********
 *
 * NextPosAndSCR - Update nominal (may be >= actual) byte count
 * and SCR to next output sector.
 *
 ********/

void
OutputStream::NextPosAndSCR ()
{
  bytes_output += sector_transport_size;
  ByteposTimecode (bytes_output, current_SCR);
  if (start_of_new_pack) {
    psstrm->CreatePack (&pack_header, current_SCR, mux_rate);
    pack_header_ptr = &pack_header;
    if (include_sys_header)
      sys_header_ptr = &sys_header;
    else
      sys_header_ptr = NULL;

  } else
    pack_header_ptr = NULL;
}


/**********
 *
 * NextPosAndSCR - Update nominal (may be >= actual) byte count
 * and SCR to next output sector.
 * @param bytepos byte position in the stream
 ********/

void
OutputStream::SetPosAndSCR (bitcount_t bytepos)
{
  bytes_output = bytepos;
  ByteposTimecode (bytes_output, current_SCR);
  if (start_of_new_pack) {
    psstrm->CreatePack (&pack_header, current_SCR, mux_rate);
    pack_header_ptr = &pack_header;
    if (include_sys_header)
      sys_header_ptr = &sys_header;
    else
      sys_header_ptr = NULL;

  } else
    pack_header_ptr = NULL;
}

/* 
   Stream syntax parameters.
*/


/******************************************************************

	Initialisation of stream syntax paramters based on selected
	user options.
******************************************************************/


// TODO: this mixes class member parameters with opt_ globals...

void
OutputStream::InitSyntaxParameters ()
{
  video_buffer_size = 0;
  seg_starts_with_video = false;
  audio_buffer_size = 4 * 1024;

  switch (opt_mux_format) {
    case MPEG_FORMAT_VCD:
      opt_data_rate = 75 * 2352;	/* 75 raw CD sectors/sec */
      video_buffer_size = 46 * 1024;
      opt_VBR = 0;

    case MPEG_FORMAT_VCD_NSR:	/* VCD format, non-standard rate */
      mjpeg_info ("Selecting VCD output profile");
      if (video_buffer_size == 0)
	video_buffer_size = opt_buffer_size * 1024;
      vbr = opt_VBR;
      opt_mpeg = 1;
      packets_per_pack = 1;
      sys_header_in_pack1 = 0;
      always_sys_header_in_pack = 0;
      sector_transport_size = 2352;	/* Each 2352 bytes with 2324 bytes payload */
      transport_prefix_sectors = 30;
      sector_size = 2324;
      buffers_in_video = 1;
      always_buffers_in_video = 0;
      buffers_in_audio = 1;	// This is needed as otherwise we have
      always_buffers_in_audio = 1;	//  to stuff the packer header which 
      // must be 13 bytes for VCD audio
      vcd_zero_stuffing = 20;	// The famous 20 zero bytes for VCD
      // audio sectors.
      dtspts_for_all_vau = false;
      sector_align_iframeAUs = false;
      timestamp_iframe_only = false;
      seg_starts_with_video = true;
      break;

    case MPEG_FORMAT_MPEG2:
      mjpeg_info ("Selecting generic MPEG2 output profile");
      opt_mpeg = 2;
      packets_per_pack = 1;
      sys_header_in_pack1 = 1;
      always_sys_header_in_pack = 0;
      sector_transport_size = 2048;	/* Each 2352 bytes with 2324 bytes payload */
      transport_prefix_sectors = 0;
      sector_size = 2048;
      video_buffer_size = 234 * 1024;
      buffers_in_video = 1;
      always_buffers_in_video = 0;
      buffers_in_audio = 1;
      always_buffers_in_audio = 1;
      vcd_zero_stuffing = 0;
      dtspts_for_all_vau = 0;
      sector_align_iframeAUs = false;
      timestamp_iframe_only = false;
      video_buffers_iframe_only = false;
      vbr = opt_VBR;
      break;

    case MPEG_FORMAT_SVCD:
      opt_data_rate = 150 * 2324;
      video_buffer_size = 230 * 1024;

    case MPEG_FORMAT_SVCD_NSR:	/* Non-standard data-rate */
      mjpeg_info ("Selecting SVCD output profile");
      if (video_buffer_size == 0)
	video_buffer_size = opt_buffer_size * 1024;
      opt_mpeg = 2;
      packets_per_pack = 1;
      sys_header_in_pack1 = 0;
      always_sys_header_in_pack = 0;
      sector_transport_size = 2324;
      transport_prefix_sectors = 0;
      sector_size = 2324;
      vbr = true;
      buffers_in_video = 1;
      always_buffers_in_video = 0;
      buffers_in_audio = 1;
      always_buffers_in_audio = 0;
      vcd_zero_stuffing = 0;
      dtspts_for_all_vau = 0;
      sector_align_iframeAUs = true;
      seg_starts_with_video = true;
      timestamp_iframe_only = false;
      video_buffers_iframe_only = false;
      break;

    case MPEG_FORMAT_VCD_STILL:
      opt_data_rate = 75 * 2352;	/* 75 raw CD sectors/sec */
      vbr = false;
      opt_mpeg = 1;
      packets_per_pack = 1;
      sys_header_in_pack1 = 0;
      always_sys_header_in_pack = 0;
      sector_transport_size = 2352;	/* Each 2352 bytes with 2324 bytes payload */
      transport_prefix_sectors = 0;
      sector_size = 2324;
      buffers_in_video = 1;
      always_buffers_in_video = 0;
      buffers_in_audio = 1;
      always_buffers_in_audio = 0;
      vcd_zero_stuffing = 20;
      dtspts_for_all_vau = 1;
      sector_align_iframeAUs = true;
      timestamp_iframe_only = false;
      video_buffers_iframe_only = false;
      if (opt_buffer_size == 0)
	opt_buffer_size = 46;
      else if (opt_buffer_size > 220) {
	mjpeg_error_exit1 ("VCD stills has max. permissible video buffer size of 220KB");
      } else {
	/* Add a margin for sequence header overheads for HR stills */
	/* So the user simply specifies the nominal size... */
	opt_buffer_size += 4;
      }
      video_buffer_size = opt_buffer_size * 1024;
      break;

    case MPEG_FORMAT_SVCD_STILL:
      mjpeg_info ("Selecting SVCD output profile");
      if (opt_data_rate == 0)
	opt_data_rate = 150 * 2324;
      video_buffer_size = 230 * 1024;
      opt_mpeg = 2;
      packets_per_pack = 1;
      sys_header_in_pack1 = 0;
      always_sys_header_in_pack = 0;
      sector_transport_size = 2324;
      transport_prefix_sectors = 0;
      sector_size = 2324;
      vbr = true;
      buffers_in_video = 1;
      always_buffers_in_video = 0;
      buffers_in_audio = 1;
      always_buffers_in_audio = 0;
      vcd_zero_stuffing = 0;
      dtspts_for_all_vau = 0;
      sector_align_iframeAUs = true;
      timestamp_iframe_only = false;
      video_buffers_iframe_only = false;
      break;

    case MPEG_FORMAT_DVD:
      mjpeg_info ("Selecting DVD output profile (INCOMPLETE!!!!)");
      opt_data_rate = 1260000;
      opt_mpeg = 2;
      packets_per_pack = 1;
      sys_header_in_pack1 = false;	// Handle by control packets
      always_sys_header_in_pack = false;
      sector_transport_size = 2048;
      transport_prefix_sectors = 0;
      sector_size = 2048;
      video_buffer_size = 232 * 1024;
      buffers_in_video = true;
      always_buffers_in_video = false;
      buffers_in_audio = true;
      always_buffers_in_audio = false;
      vcd_zero_stuffing = 0;
      dtspts_for_all_vau = 0;
      sector_align_iframeAUs = true;
      timestamp_iframe_only = true;
      video_buffers_iframe_only = true;
      vbr = true;
      if (opt_max_segment_size == 0)
	opt_max_segment_size = 2000 * 1024 * 1024;
      break;

    default:			/* MPEG_FORMAT_MPEG1 - auto format MPEG1 */
      mjpeg_info ("Selecting generic MPEG1 output profile");
      opt_mpeg = 1;
      vbr = opt_VBR;
      packets_per_pack = opt_packets_per_pack;
      always_sys_header_in_pack = opt_always_system_headers;
      sys_header_in_pack1 = 1;
      sector_transport_size = opt_sector_size;
      transport_prefix_sectors = 0;
      sector_size = opt_sector_size;
      if (opt_buffer_size == 0) {
	opt_buffer_size = 46;
      }
      video_buffer_size = opt_buffer_size * 1024;
      buffers_in_video = 1;
      always_buffers_in_video = 1;
      buffers_in_audio = 0;
      always_buffers_in_audio = 1;
      vcd_zero_stuffing = 0;
      dtspts_for_all_vau = 0;
      sector_align_iframeAUs = false;
      timestamp_iframe_only = false;
      video_buffers_iframe_only = false;
      break;
  }

}

/**
 * Compute the number of run-in sectors needed to fill up the buffers to
 * suit the type of stream being muxed.
 *
 * For stills we have to ensure an entire buffer is loaded as we only
 * ever process one frame at a time.
 * @returns the number of run-in sectors needed to fill up the buffers to suit the type of stream being muxed.
 */

unsigned int
OutputStream::RunInSectors ()
{
  vector < ElementaryStream * >::iterator str;
  unsigned int sectors_delay = 1;

  for (str = vstreams.begin (); str < vstreams.end (); ++str) {

    if (MPEG_STILLS_FORMAT (opt_mux_format)) {
      sectors_delay += (unsigned int) (1.02 * (*str)->BufferSize ()) / sector_size + 2;
    } else if (vbr)
      sectors_delay += 3 * (*str)->BufferSize () / (4 * sector_size);
    else
      sectors_delay += 5 * (*str)->BufferSize () / (6 * sector_size);
  }
  sectors_delay += astreams.size ();
  return sectors_delay;
}

/**
   Initializes the output stream. Traverses the input files and calculates their payloads.
   Estimates the multiplex rate. Estimates the neccessary stream delay for the different substreams.
 */


void
OutputStream::Init (vector < ElementaryStream * >*strms, PS_Stream *strm)
{
  vector < ElementaryStream * >::iterator str;
  clockticks delay;
  unsigned int sectors_delay;

  Pack_struc dummy_pack;
  Sys_header_struc dummy_sys_header;
  Sys_header_struc *sys_hdr;
  unsigned int nominal_rate_sum;

  packets_left_in_pack = 0;        /* Suppress warning */
  video_first = false;

  estreams = strms;

  for (str = estreams->begin (); str < estreams->end (); ++str) {
    switch ((*str)->Kind ()) {
      case ElementaryStream::audio:
	astreams.push_back (*str);
	break;
      case ElementaryStream::video:
	vstreams.push_back (*str);
	break;
      default:
	break;
    }

    completed.push_back (false);
  }

  mjpeg_info ("SYSTEMS/PROGRAM stream:");
  this->psstrm = strm;

  psstrm->Init (opt_mpeg, sector_size, opt_max_segment_size);

  /* These are used to make (conservative) decisions
     about whether a packet should fit into the recieve buffers... 
     Audio packets always have PTS fields, video packets needn'.      
     TODO: Really this should be encapsulated in Elementary stream...?
   */
  psstrm->CreatePack (&dummy_pack, 0, mux_rate);
  if (always_sys_header_in_pack) {
    vector < MuxStream * >muxstreams;
    AppendMuxStreamsOf (*estreams, muxstreams);
    psstrm->CreateSysHeader (&dummy_sys_header, mux_rate, !vbr, 1, true, true, muxstreams);
    sys_hdr = &dummy_sys_header;
  } else
    sys_hdr = NULL;

  nominal_rate_sum = 0;
  for (str = estreams->begin (); str < estreams->end (); ++str) {
    switch ((*str)->Kind ()) {
      case ElementaryStream::audio:
	(*str)->SetMaxPacketData (psstrm->PacketPayload (**str, NULL, NULL, 
						 false, true, false));
	(*str)->SetMinPacketData (psstrm->PacketPayload (**str, sys_hdr, &dummy_pack,
						 always_buffers_in_audio, true, false));

	break;
      case ElementaryStream::video:
	(*str)->SetMaxPacketData (psstrm->PacketPayload (**str, NULL, NULL, 
						 false, false, false));
	(*str)->SetMinPacketData (psstrm->PacketPayload (**str, sys_hdr, &dummy_pack,
						 always_buffers_in_video, true, true));
	break;
      default:
	mjpeg_error_exit1 ("INTERNAL: Only audio and video payload calculations implemented!");

    }

    if ((*str)->NominalBitRate () == 0 && opt_data_rate == 0)
      mjpeg_error_exit1
	("Variable bit-rate stream present: output stream (max) data-rate *must* be specified!");
    nominal_rate_sum += (*str)->NominalBitRate ();

  }

  /* Attempt to guess a sensible mux rate for the given video and *
     audio estreams-> This is a rough and ready guess for MPEG-1 like
     formats. */


  dmux_rate = static_cast < int >(1.015 * nominal_rate_sum);

  dmux_rate = (dmux_rate / 50 + 25) * 50;

  mjpeg_info ("rough-guess multiplexed stream data rate    : %07d", dmux_rate * 8);
  if (opt_data_rate != 0)
    mjpeg_info ("target data-rate specified               : %7d", opt_data_rate * 8);

  if (opt_data_rate == 0) {
    mjpeg_info ("Setting best-guess data rate.");
  } else if (opt_data_rate >= dmux_rate) {
    mjpeg_info ("Setting specified specified data rate: %7d", opt_data_rate * 8);
    dmux_rate = opt_data_rate;
  } else if (opt_data_rate < dmux_rate) {
    mjpeg_warn ("Target data rate lower than computed requirement!");
    mjpeg_warn ("N.b. a 20%% or so discrepancy in variable bit-rate");
    mjpeg_warn ("streams is common and harmless provided no time-outs will occur");
    dmux_rate = opt_data_rate;
  }

  mux_rate = dmux_rate / 50;

  /* To avoid Buffer underflow, the DTS of the first video and audio AU's
     must be offset sufficiently  forward of the SCR to allow the buffer 
     time to fill before decoding starts. Calculate the necessary delays...
   */

  sectors_delay = RunInSectors ();

  ByteposTimecode (static_cast < bitcount_t > (sectors_delay * sector_transport_size), delay);


  video_delay = delay + static_cast < clockticks > (opt_video_offset * CLOCKS / 1000);
  audio_delay = delay + static_cast < clockticks > (opt_audio_offset * CLOCKS / 1000);
  mjpeg_info ("Sectors = %d Video delay = %lld Audio delay = %lld",
	      sectors_delay, video_delay / 300, audio_delay / 300);


  //
  // Now that all mux parameters are set we can trigger parsing
  // of actual input stream data and calculation of associated 
  // PTS/DTS by causing the read of the first AU's...
  //
  for (str = estreams->begin (); str < estreams->end (); ++str) {
    (*str)->NextAU ();
  }


  //
  // Now that we have both output and input streams initialised and
  // data-rates set we can make a decent job of setting the maximum
  // STD buffer delay in video streams.
  //

  for (str = vstreams.begin (); str < vstreams.end (); ++str) {
    static_cast < VideoStream * >(*str)->SetMaxStdBufferDelay (dmux_rate);
  }



  /*  Let's try to read in unit after unit and to write it out into
     the outputstream. The only difficulty herein lies into the
     buffer management, and into the fact the the actual access
     unit *has* to arrive in time, that means the whole unit
     (better yet, packet data), has to arrive before arrival of
     DTS. If both buffers are full we'll generate a padding packet

     Of course, when we start we're starting a new segment with no
     bytes output...
   */

  ByteposTimecode (sector_transport_size, ticks_per_sector);
  seg_state = start_segment;
  running_out = false;
}

/**
   Prints the current status of the substreams. 
   @param level the desired log level 
 */
void
OutputStream::MuxStatus (log_level_t level)
{
  vector < ElementaryStream * >::iterator str;
  for (str = estreams->begin (); str < estreams->end (); ++str) {
    switch ((*str)->Kind ()) {
      case ElementaryStream::video:
	mjpeg_log (level,
		   "Video %02x: buf=%7d frame=%06d sector=%08d",
		   (*str)->stream_id, (*str)->bufmodel.Space (), (*str)->au->dorder, (*str)->nsec);
	break;
      case ElementaryStream::audio:
	mjpeg_log (level,
		   "Audio %02x: buf=%7d frame=%06d sector=%08d",
		   (*str)->stream_id, (*str)->bufmodel.Space (), (*str)->au->dorder, (*str)->nsec);
	break;
      default:
	mjpeg_log (level,
		   "Other %02x: buf=%7d sector=%08d",
		   (*str)->stream_id, (*str)->bufmodel.Space (), (*str)->nsec);
	break;
    }
  }
  if (!vbr)
    mjpeg_log (level, "Padding : sector=%08d", pstrm.nsec);


}


/**
   Append input substreams to the output multiplex stream.
 */
void
OutputStream::AppendMuxStreamsOf (vector < ElementaryStream * >&elem, vector < MuxStream * >&mux)
{
  vector < ElementaryStream * >::iterator str;
  for (str = elem.begin (); str < elem.end (); ++str) {
    mux.push_back (static_cast < MuxStream * >(*str));
  }
}

/******************************************************************
    Program start-up packets.  Generate any irregular packets						
needed at the start of the stream...
	Note: *must* leave a sensible in-stream system header in
	sys_header.
	TODO: get rid of this grotty sys_header global.
******************************************************************/
void
OutputStream::OutputPrefix ()
{
  vector < MuxStream * >vmux, amux, emux;
  AppendMuxStreamsOf (vstreams, vmux);
  AppendMuxStreamsOf (astreams, amux);
  AppendMuxStreamsOf (*estreams, emux);

  /* Deal with transport padding */
  SetPosAndSCR (bytes_output + transport_prefix_sectors * sector_transport_size);

  /* VCD: Two padding packets with video and audio system headers */
  split_at_seq_end = !opt_multifile_segment;

  switch (opt_mux_format) {
    case MPEG_FORMAT_VCD:
    case MPEG_FORMAT_VCD_NSR:

      /* Annoyingly VCD generates seperate system headers for
         audio and video ... DOH... */
      if (astreams.size () > 1 || vstreams.size () > 1 ||
	  astreams.size () + vstreams.size () != estreams->size ()) {
	mjpeg_error_exit1 ("VCD man only have max. 1 audio and 1 video stream");
      }
      /* First packet carries video-info-only sys_header */
      psstrm->CreateSysHeader (&sys_header, mux_rate, false, true, true, true, vmux);
      sys_header_ptr = &sys_header;
      pack_header_ptr = &pack_header;
      OutputPadding (false);

      /* Second packet carries audio-info-only sys_header */
      psstrm->CreateSysHeader (&sys_header, mux_rate, false, true, true, true, amux);
      sys_header_ptr = &sys_header;
      pack_header_ptr = &pack_header;
      OutputPadding (true);
      break;

    case MPEG_FORMAT_SVCD:
    case MPEG_FORMAT_SVCD_NSR:
      /* First packet carries sys_header */
      psstrm->CreateSysHeader (&sys_header, mux_rate, !vbr, true, true, true, emux);
      sys_header_ptr = &sys_header;
      pack_header_ptr = &pack_header;
      OutputPadding (false);
      break;

    case MPEG_FORMAT_VCD_STILL:
      split_at_seq_end = false;
      /* First packet carries small-still sys_header */
      /* TODO No support mixed-mode stills sequences... */
      psstrm->CreateSysHeader (&sys_header, mux_rate, false, false, true, true, emux);
      sys_header_ptr = &sys_header;
      pack_header_ptr = &pack_header;
      OutputPadding (false);
      break;

    case MPEG_FORMAT_SVCD_STILL:
      /* TODO: Video only at present */
      /* First packet carries video-info-only sys_header */
      psstrm->CreateSysHeader (&sys_header, mux_rate, false, true, true, true, vmux);
      sys_header_ptr = &sys_header;
      pack_header_ptr = &pack_header;
      OutputPadding (false);
      break;

    case MPEG_FORMAT_DVD:
      /* A DVD System header is a weird thing.  We seem to need to
         include buffer info about streams 0xb8, 0xb9, 0xbf even if
         they're not physically present but the buffers for the actual
         video streams aren't included.  

         TODO: I have no idead about MPEG audio streams if present...
       */
    {
      DummyMuxStream dvd_0xb9_strm_dummy (0xb9, 1, video_buffer_size);
      DummyMuxStream dvd_0xb8_strm_dummy (0xb8, 0, 4096);
      DummyMuxStream dvd_0xbf_strm_dummy (0xbf, 1, 2048);

      vector < MuxStream * >dvdmux;
      vector < MuxStream * >::iterator muxstr;
      dvdmux.push_back (&dvd_0xb9_strm_dummy);
      dvdmux.push_back (&dvd_0xb8_strm_dummy);
      unsigned int max_priv1_buffer = 0;

      for (muxstr = amux.begin (); muxstr < amux.end (); ++muxstr) {
	// We mux *many* substreams on PRIVATE_STR_1
	// we set the system header buffer size to the maximum
	// of all those we find
	if ((*muxstr)->stream_id == PRIVATE_STR_1 && (*muxstr)->BufferSize () > max_priv1_buffer) {
	  max_priv1_buffer = (*muxstr)->BufferSize ();
	} else
	  dvdmux.push_back (*muxstr);
      }

      DummyMuxStream dvd_priv1_strm_dummy (PRIVATE_STR_1, 1, max_priv1_buffer);

      if (max_priv1_buffer > 0)
	dvdmux.push_back (&dvd_priv1_strm_dummy);

      dvdmux.push_back (&dvd_0xbf_strm_dummy);
      psstrm->CreateSysHeader (&sys_header, mux_rate, !vbr, false, true, true, dvdmux);
      sys_header_ptr = &sys_header;
      pack_header_ptr = &pack_header;
      /* It is then followed up by a pair of PRIVATE_STR_2 packets which
         we keep empty 'cos we don't know what goes there...
       */
    }
      break;

    default:
      /* Create the in-stream header in case it is needed */
      psstrm->CreateSysHeader (&sys_header, mux_rate, !vbr, false, true, true, emux);
  }
}

/******************************************************************
    Program shutdown packets.  Generate any irregular packets
    needed at the end of the stream...
   
******************************************************************/

void
OutputStream::OutputSuffix ()
{
  psstrm->CreatePack (&pack_header, current_SCR, mux_rate);
  psstrm->CreateSector (&pack_header, NULL, 0, pstrm, false, true, 0, 0, TIMESTAMPBITS_NO);
}

/******************************************************************

	Main multiplex iteration.
	Opens and closes all needed files and manages the correct
	call od the respective Video- and Audio- packet routines.
	The basic multiplexing is done here. Buffer capacity and 
	Timestamp checking is also done here, decision is taken
	wether we should genereate a Video-, Audio- or Padding-
	packet.
******************************************************************/



bool
OutputStream::OutputMultiplex ()
{
  for (;;) {
    bool completion = true;

    for (str = estreams->begin (); str < estreams->end (); ++str)
      completion &= (*str)->MuxCompleted ();
    if (completion)
      break;

    /* A little state-machine for handling the transition from one
       segment to the next 
     */
    bool runout_incomplete;
    VideoStream *master;

    switch (seg_state) {

	/* Audio and slave video access units at end of segment.
	   If there are any audio AU's whose PTS implies they
	   should be played *before* the video AU starting the
	   next segement is presented we mux them out.  Once
	   they're gone we've finished this segment so we write
	   the suffix switch file, and start muxing a new segment.
	 */
      case runout_segment:
	runout_incomplete = false;
	for (str = estreams->begin (); str < estreams->end (); ++str) {
	  runout_incomplete |= !(*str)->RunOutComplete ();
	}

	if (runout_incomplete)
	  break;

	/* Otherwise we write the stream suffix and start a new
	   stream file */
	OutputSuffix ();
	psstrm->NextFile ();

	running_out = false;
	seg_state = start_segment;

	/* Starting a new segment.
	   We send the segment prefix, video and audio reciever
	   buffers are assumed to start empty.  We reset the segment
	   length count and hence the SCR.

	 */

      case start_segment:
	mjpeg_info ("New sequence commences...");
	SetPosAndSCR (0);
	MuxStatus (LOG_INFO);

	for (str = estreams->begin (); str < estreams->end (); ++str) {
	  (*str)->AllDemuxed ();
	}

	packets_left_in_pack = packets_per_pack;
	start_of_new_pack = true;
	include_sys_header = sys_header_in_pack1;
	buffers_in_video = always_buffers_in_video;
	video_first = seg_starts_with_video & (vstreams.size () > 0);
	OutputPrefix ();

	/* Set the offset applied to the raw PTS/DTS of AU's to
	   make the DTS of the first AU in the master (video) stream
	   precisely the video delay plus whatever time we wasted in
	   the sequence pre-amble.

	   The DTS of the remaining streams are set so that
	   (modulo the relevant delay offset) they maintain the
	   same relative timing to the master stream.

	 */

	clockticks ZeroSCR;

	if (vstreams.size () != 0)
	  ZeroSCR = vstreams[0]->au->DTS;
	else
	  ZeroSCR = (*estreams)[0]->au->DTS;

	for (str = vstreams.begin (); str < vstreams.end (); ++str)
	  (*str)->SetSyncOffset (video_delay + current_SCR - ZeroSCR);
	for (str = astreams.begin (); str < astreams.end (); ++str)
	  (*str)->SetSyncOffset (audio_delay + current_SCR - ZeroSCR);
	pstrm.nsec = 0;
	for (str = estreams->begin (); str < estreams->end (); ++str)
	  (*str)->nsec = 0;
	seg_state = mid_segment;
	//for( str = estreams->begin(); str < estreams->end(); ++str )
	//{
	//mjpeg_info("STREAM %02x: SCR=%lld mux=%d reqDTS=%lld", 
	//(*str)->stream_id,
	//current_SCR /300,
	//(*str)->MuxPossible(current_SCR),
	//(*str)->RequiredDTS()/300
	//);
	//}
	break;

      case mid_segment:
	/* Once we exceed our file size limit, we need to
	   start a new file soon.  If we want a single stream we
	   simply switch.

	   Otherwise we're in the last gop of the current segment
	   (and need to start running streams out ready for a
	   clean continuation in the next segment).
	   TODO: runout_PTS really needs to be expressed in
	   sync delay adjusted units...
	 */

	master = vstreams.size () > 0 ? static_cast < VideoStream * >(vstreams[0]) : 0;
	if (psstrm->FileLimReached ()) {
	  if (opt_multifile_segment || master == 0)
	    psstrm->NextFile ();
	  else {
	    if (master->NextAUType () == IFRAME) {
	      seg_state = runout_segment;
	      runout_PTS = master->NextRequiredPTS ();
	      mjpeg_debug ("Running out to (raw) PTS %lld SCR=%lld",
			   runout_PTS / 300, current_SCR / 300);
	      running_out = true;
	      seg_state = runout_segment;
	    }
	  }
	} else if (master != 0 && master->EndSeq ()) {
	  if (split_at_seq_end && master->Lookahead () != 0) {
	    if (!master->SeqHdrNext () || master->NextAUType () != IFRAME) {
	      mjpeg_error_exit1 ("Sequence split detected %d but no following sequence found...",
				 master->NextAUType ());
	    }

	    runout_PTS = master->NextRequiredPTS ();
	    mjpeg_debug ("Running out to %lld SCR=%lld", runout_PTS / 300, current_SCR / 300);
	    MuxStatus (LOG_INFO);
	    running_out = true;
	    seg_state = runout_segment;
	  }
	}
	break;

    }

    padding_packet = false;
    start_of_new_pack = (packets_left_in_pack == packets_per_pack);

    for (str = estreams->begin (); str < estreams->end (); ++str) {
      (*str)->DemuxedTo (current_SCR);
    }



    //
    // Find the ready-to-mux stream with the most urgent DTS
    //
    ElementaryStream *despatch = 0;
    clockticks earliest = 0;

    for (str = estreams->begin (); str < estreams->end (); ++str) {
      /* DEBUG
         mjpeg_info("STREAM %02x: SCR=%lld mux=%d reqDTS=%lld", 
         (*str)->stream_id,
         current_SCR /300,
         (*str)->MuxPossible(current_SCR),
         (*str)->RequiredDTS()/300
         );
       */
      if ((*str)->MuxPossible (current_SCR) &&
	  (!video_first || (*str)->Kind () == ElementaryStream::video)
	) {
	if (despatch == 0 || earliest > (*str)->RequiredDTS ()) {
	  despatch = *str;
	  earliest = (*str)->RequiredDTS ();
	}
      }
    }

    if (underrun_ignore > 0)
      --underrun_ignore;

    if (despatch) {
      despatch->OutputSector ();
      video_first = false;
      if (current_SCR >= earliest && underrun_ignore == 0) {
	mjpeg_warn ("Stream %02x: data will arrive too late sent(SCR)=%lld required(DTS)=%d",
		    despatch->stream_id, current_SCR / 300, (int) earliest / 300);
	MuxStatus (LOG_WARN);
	// Give the stream a chance to recover
	underrun_ignore = 300;
	++underruns;
	if (underruns > 10 && !opt_ignore_underrun) {
	  mjpeg_error_exit1 ("Too many frame drops -exiting");
	}
      }
      if (despatch->nsec > 50 && despatch->Lookahead () != 0 && !running_out)
	despatch->UpdateBufferMinMax ();
      padding_packet = false;

    } else {
      //
      // If we got here no stream could be muxed out.
      // We therefore generate padding packets if necessary
      // usually this is because reciever buffers are likely to be
      // full.  
      //
      if (vbr) {
	//
	// VBR: For efficiency we bump SCR up to five times or
	// until it looks like buffer status will change
	NextPosAndSCR ();
	clockticks next_change = static_cast < clockticks > (0);

	for (str = estreams->begin (); str < estreams->end (); ++str) {
	  clockticks change_time = (*str)->bufmodel.NextChange ();

	  if (next_change == 0 || change_time < next_change)
	    next_change = change_time;
	}
	unsigned int bumps = 5;

	while (bumps > 0 && next_change > current_SCR + ticks_per_sector) {
	  NextPosAndSCR ();
	  --bumps;
	}
      } else {
	// Just output a padding packet
	OutputPadding (false);
      }
      padding_packet = true;
    }

    /* Update the counter for pack packets.  VBR is a tricky 
       case as here padding packets are "virtual" */

    if (!(vbr && padding_packet)) {
      --packets_left_in_pack;
      if (packets_left_in_pack == 0)
	packets_left_in_pack = packets_per_pack;
    }

    MuxStatus (LOG_DEBUG);
    /* Unless sys headers are always required we turn them off after the first
       packet has been generated */
    include_sys_header = always_sys_header_in_pack;

    pcomp = completed.begin ();
    str = estreams->begin ();
    while (str < estreams->end ()) {
      if (!(*pcomp) && (*str)->MuxCompleted ()) {
	mjpeg_info ("STREAM %02x completed @ %d.", (*str)->stream_id, (*str)->au->dorder);
	MuxStatus (LOG_DEBUG);
	(*pcomp) = true;
      }
      ++str;
      ++pcomp;
    }

    return true;
  }

  return false;
}


void
OutputStream::Close ()
{
  // Tidy up
  OutputSuffix ();
  psstrm->Close ();
  mjpeg_info ("Multiplex completion at SCR=%lld.", current_SCR / 300);
  MuxStatus (LOG_INFO);
  for (str = estreams->begin (); str < estreams->end (); ++str) {
    (*str)->Close ();
    if ((*str)->nsec <= 50)
      mjpeg_info ("BUFFERING stream too short for useful statistics");
    else
      mjpeg_info ("BUFFERING min %d Buf max %d", (*str)->BufferMin (), (*str)->BufferMax ());
  }

  if (underruns > 0) {
    mjpeg_error ("foo");
    mjpeg_error_exit1 ("MUX STATUS: Frame data under-runs detected!");
  } else {
    mjpeg_info ("foo");
    mjpeg_info ("MUX STATUS: no under-runs detected.");
  }
}

/**
   Calculate the packet payload of the output stream at a certain timestamp. 
@param strm the output stream
@param buffers the number of buffers
@param PTSstamp presentation time stamp
@param DTSstamp decoding time stamp
 */
unsigned int
OutputStream::PacketPayload (MuxStream & strm, bool buffers, bool PTSstamp, bool DTSstamp)
{
  return psstrm->PacketPayload (strm, sys_header_ptr, pack_header_ptr, buffers, PTSstamp, DTSstamp)
    - strm.StreamHeaderSize ();
}

/***************************************************

  WritePacket - Write out a normal packet carrying data from one of
              the elementary stream being muxed.
@param max_packet_data_size the maximum packet data size allowed
@param strm output mux stream
@param buffers ?
@param PTSstamp presentation time stamp of the packet
@param DTSstamp decoding time stamp of the packet
@param timestamps ?
@param returns the written bytes/packets (?)
***************************************************/

unsigned int
OutputStream::WritePacket (unsigned int max_packet_data_size,
			   MuxStream & strm,
			   bool buffers, clockticks PTS, clockticks DTS, uint8_t timestamps)
{
  unsigned int written = psstrm->CreateSector (pack_header_ptr,
					       sys_header_ptr,
					       max_packet_data_size,
					       strm,
					       buffers,
					       false,
					       PTS,
					       DTS,
					       timestamps);

  NextPosAndSCR ();
  return written;
}

/***************************************************
 *
 * WriteRawSector - Write out a packet carrying data for
 *                    a control packet with irregular content.
@param rawsector data for the raw sector
@param length length of the raw sector
 ***************************************************/

void
OutputStream::WriteRawSector (uint8_t * rawsector, unsigned int length)
{
  //
  // Writing raw sectors when packs stretch over multiple sectors
  // is a recipe for disaster!
  //
  assert (packets_per_pack == 1);
  psstrm->RawWrite (rawsector, length);
  NextPosAndSCR ();

}



/******************************************************************
	OutputPadding

	generates Pack/Sys Header/Packet information for a 
	padding stream and saves the sector

	We have to pass in a special flag to cope with appalling mess VCD
	makes of audio packets (the last 20 bytes being dropped thing) 0 =
	Fill the packet completetely.  This include "audio packets" that
    include no actual audio, only a system header and padding.
@param vcd_audio_pad flag for VCD audio padding
******************************************************************/


void
OutputStream::OutputPadding (bool vcd_audio_pad)
{
  if (vcd_audio_pad)
    psstrm->CreateSector (pack_header_ptr, sys_header_ptr,
			  0, vcdapstrm, false, false, 0, 0, TIMESTAMPBITS_NO);
  else
    psstrm->CreateSector (pack_header_ptr, sys_header_ptr,
			  0, pstrm, false, false, 0, 0, TIMESTAMPBITS_NO);
  ++pstrm.nsec;
  NextPosAndSCR ();

}

 /******************************************************************
 *	OutputGOPControlSector
 *  DVD System headers are carried in peculiar sectors carrying 2
 *  PrivateStream2 packets.   We're sticking 0's in the packets
 *  as we have no idea what's supposed to be in there.
 *
 * Thanks to Brent Byeler who worked out this work-around.
 *
 ******************************************************************/

void
OutputStream::OutputDVDPriv2 ()
{
  uint8_t *packet_size_field;
  uint8_t *index;
  uint8_t sector_buf[sector_size];
  unsigned int tozero;

  assert (sector_size == 2048);
  PS_Stream::BufferSectorHeader (sector_buf, pack_header_ptr, &sys_header, index);
  PS_Stream::BufferPacketHeader (index, PRIVATE_STR_2, 2,	// MPEG 2
				 false,	// No buffers
				 0, 0, 0,	// No timestamps
				 0, TIMESTAMPBITS_NO, packet_size_field, index);
  tozero = sector_buf + 1024 - index;
  memset (index, 0, tozero);
  index += tozero;
  PS_Stream::BufferPacketSize (packet_size_field, index);

  PS_Stream::BufferPacketHeader (index, PRIVATE_STR_2, 2,	// MPEG 2
				 false,	// No buffers
				 0, 0, 0,	// No timestamps
				 0, TIMESTAMPBITS_NO, packet_size_field, index);
  tozero = sector_buf + 2048 - index;
  memset (index, 0, tozero);
  index += tozero;
  PS_Stream::BufferPacketSize (packet_size_field, index);

  WriteRawSector (sector_buf, sector_size);
}


/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
