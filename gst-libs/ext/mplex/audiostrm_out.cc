
/*
 *  inptstrm.c:  Members of audi stream classes related to muxing out into
 *               the output stream.
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
#include <assert.h>
#include "fastintfns.h"
#include "audiostrm.hh"
#include "outputstream.hh"



AudioStream::AudioStream (IBitStream & ibs, OutputStream & into):
ElementaryStream (ibs, into, ElementaryStream::audio), num_syncword (0)
{
  FRAME_CHUNK = 24;
  for (int i = 0; i < 2; ++i)
    num_frames[i] = size_frames[i] = 0;
}

void
AudioStream::InitAUbuffer ()
{
  unsigned int i;

  for (i = 0; i < aunits.BUF_SIZE; ++i)
    aunits.init (new AAunit);
}



/*********************************
 * Signals when audio stream has completed mux run-out specified
 * in associated mux stream. 
 *********************************/

bool
AudioStream::RunOutComplete ()
{
  return (au_unsent == 0 || (muxinto.running_out && RequiredPTS () >= muxinto.runout_PTS));
}

bool
AudioStream::AUBufferNeedsRefill ()
{
  return
    !eoscan
    && (aunits.current () + FRAME_CHUNK > last_buffered_AU
	|| bs.buffered_bytes () < muxinto.sector_size);
}

/******************************************************************
	Output_Audio
	generates Pack/Sys Header/Packet information from the
	audio stream and saves them into the sector
******************************************************************/

void
AudioStream::OutputSector ()
{
  clockticks PTS;
  unsigned int max_packet_data;
  unsigned int actual_payload;
  unsigned int old_au_then_new_payload;

  PTS = RequiredDTS ();
  old_au_then_new_payload = muxinto.PacketPayload (*this, buffers_in_header, false, false);

  max_packet_data = 0;
  if (muxinto.running_out && NextRequiredPTS () > muxinto.runout_PTS) {
    /* We're now in the last AU of a segment.  So we don't want to
       go beyond it's end when writing sectors. Hence we limit
       packet payload size to (remaining) AU length.
     */
    max_packet_data = au_unsent;
  }

  /* CASE: packet starts with new access unit                     */

  if (new_au_next_sec) {
    actual_payload =
      muxinto.WritePacket (max_packet_data, *this, buffers_in_header, PTS, 0, TIMESTAMPBITS_PTS);

  }


  /* CASE: packet starts with old access unit, no new one */
  /*       starts in this very same packet                        */
  else if (!(new_au_next_sec) && (au_unsent >= old_au_then_new_payload)) {
    actual_payload =
      muxinto.WritePacket (max_packet_data, *this, buffers_in_header, 0, 0, TIMESTAMPBITS_NO);
  }


  /* CASE: packet starts with old access unit, a new one  */
  /*       starts in this very same packet                        */
  else {			/* !(new_au_next_sec) &&  (au_unsent < old_au_then_new_payload)) */

    /* is there another access unit anyway ? */
    if (Lookahead () != 0) {
      PTS = NextRequiredDTS ();
      actual_payload =
	muxinto.WritePacket (max_packet_data, *this, buffers_in_header, PTS, 0, TIMESTAMPBITS_PTS);

    } else {
      actual_payload = muxinto.WritePacket (0, *this, buffers_in_header, 0, 0, TIMESTAMPBITS_NO);
    };

  }

  ++nsec;

  buffers_in_header = always_buffers_in_header;

}



/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
