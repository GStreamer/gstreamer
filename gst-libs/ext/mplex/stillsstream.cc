
/*
 *  stillsstreams.c: Class for elemenary still video streams
 *                   Most functionality is inherited from VideoStream
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <format_codes.h>

#include "stillsstream.hh"
#include "outputstream.hh"
#include <cassert>

void
StillsStream::Init ()
{
  int stream_id = 0;
  int buffer_size = 0;

  SetBufSize (4 * 1024 * 1024);
  InitAUbuffer ();
  ScanFirstSeqHeader ();

  mjpeg_debug ("SETTING video buffer to %d", muxinto.video_buffer_size);
  switch (opt_mux_format) {
    case MPEG_FORMAT_VCD_STILL:
      if (horizontal_size > 352) {
	stream_id = VIDEO_STR_0 + 2;
	buffer_size = vbv_buffer_size * 2048;
	mjpeg_info ("Stills Stream %02x: high-resolution VCD stills %d KB each",
		    stream_id, buffer_size);
	if (buffer_size < 46 * 1024)
	  mjpeg_error_exit1
	    ("I Can't multiplex high-res stills smaller than normal res stills - sorry!");

      } else {
	stream_id = VIDEO_STR_0 + 1;
	buffer_size = 46 * 1024;
	mjpeg_info ("Stills Stream %02x: normal VCD stills", stream_id);
      }
      break;
    case MPEG_FORMAT_SVCD_STILL:
      if (horizontal_size > 480) {
	stream_id = VIDEO_STR_0 + 1;
	buffer_size = 230 * 1024;
	mjpeg_info ("Stills Stream %02x: high-resolution SVCD stills.", stream_id);
      } else {
	stream_id = VIDEO_STR_0 + 1;
	buffer_size = 230 * 1024;
	mjpeg_info ("Stills Stream %02x: normal-resolution SVCD stills.", stream_id);
      }
      break;
  }


  MuxStream::Init (stream_id, 1,	// Buffer scale
		   buffer_size, 0,	// Zero stuffing
		   muxinto.buffers_in_video, muxinto.always_buffers_in_video);

  /* Skip to the end of the 1st AU (*2nd* Picture start!)
   */
  AU_hdr = SEQUENCE_HEADER;
  AU_pict_data = 0;
  AU_start = 0LL;

  OutputSeqhdrInfo ();

}




/*
 * Compute DTS / PTS for a VCD/SVCD Stills sequence
 * TODO: Very crude. Simply assumes each still stays for the specified
 * frame interval and that enough run-in delay is present for the first
 * frame to be loaded.
 *
 */

void
StillsStream::NextDTSPTS (clockticks & DTS, clockticks & PTS)
{
  clockticks interval = static_cast < clockticks >
    (intervals->NextFrameInterval () * CLOCKS / frame_rate);
  clockticks time_for_xfer;

  muxinto.ByteposTimecode (BufferSize (), time_for_xfer);

  DTS = current_PTS + time_for_xfer;	// This frame decoded just after
  // Predecessor completed.
  PTS = current_PTS + time_for_xfer + interval;
  current_PTS = PTS;
  current_DTS = DTS;
}

/*
 * VCD mixed stills segment items have the constraint that both stills
 * streams must end together.  To do this each stream has to know
 * about its "sibling".
 *
 */

void
VCDStillsStream::SetSibling (VCDStillsStream * _sibling)
{
  assert (_sibling != 0);
  sibling = _sibling;
  if (sibling->stream_id == stream_id) {
    mjpeg_error_exit1 ("VCD mixed stills stream cannot contain two streams of the same type!");
  }

}

/*
 * Check if we've reached the last sector of the last AU.  Note: that
 * we know no PTS/DTS time-stamps will be needed because no new AU
 * will appear in the last sector.  WARNING: We assume a still won't
 * fit into a single secotr.
 *
 */

bool
VCDStillsStream::LastSectorLastAU ()
{
  return (Lookahead () == 0 &&
	  au_unsent <= muxinto.PacketPayload (*this, buffers_in_header, false, false)
    );
}


/*
 * The requirement that VCD mixed stills segment items constituent streams
 * end together means we can't mux the last sector of the last AU of
 * such streams until its sibling has already completed muxing or is
 * also ready to mux the last sector of its last AU.
 *
 * NOTE: Will not work right if sector_align_iframe_AUs not set as this
 * will allow multiple AU's in  a sector.
 *
 */


bool
VCDStillsStream::MuxPossible ()
{
  if (bufmodel.Size () < au_unsent) {
    mjpeg_error_exit1
      ("Illegal VCD still: larger than maximum permitted by its buffering parameters!");
  }
  if (RunOutComplete () || bufmodel.Space () < au_unsent) {
    return false;
  }

  if (LastSectorLastAU ()) {
    if (sibling != 0) {
      if (!stream_mismatch_warned && sibling->NextAUType () != NOFRAME) {
	mjpeg_warn ("One VCD stills stream runs significantly longer than the other!");
	mjpeg_warn ("Simultaneous stream ending recommended by standard not possible");
	return true;
      }
      return sibling->MuxCompleted () || sibling->LastSectorLastAU ();
    } else
      return true;
  } else
    return true;
}




/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
