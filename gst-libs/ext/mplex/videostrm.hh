
/*
 *  videostrm.hh:  Video stream elementary input stream
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

#ifndef __VIDEOSTRM_H__
#define __VIDEOSTRM_H__

#include "inputstrm.hh"

class VideoStream:public ElementaryStream
{
public:
  VideoStream (IBitStream & ibs, OutputStream & into);
  void Init (const int stream_num);
  static bool Probe (IBitStream & bs);

  void Close ();

  inline int AUType ()
  {
    return au->type;
  }

  inline bool EndSeq ()
  {
    return au->end_seq;
  }

  inline int NextAUType ()
  {
    VAunit *p_au = Lookahead ();

    if (p_au != NULL)
      return p_au->type;
    else
      return NOFRAME;
  }

  inline bool SeqHdrNext ()
  {
    VAunit *p_au = Lookahead ();

    return p_au != NULL && p_au->seq_header;
  }

  virtual unsigned int NominalBitRate ()
  {
    return bit_rate * 50;
  }

  virtual void OutputGOPControlSector ();
  bool RunOutComplete ();
  virtual bool MuxPossible (clockticks currentSCR);
  void SetMaxStdBufferDelay (unsigned int demux_rate);
  void OutputSector ();

protected:
  void OutputSeqhdrInfo ();
  virtual bool AUBufferNeedsRefill ();
  virtual void FillAUbuffer (unsigned int frames_to_buffer);
  virtual void InitAUbuffer ();
  virtual void NextDTSPTS (clockticks & DTS, clockticks & PTS);
  void ScanFirstSeqHeader ();
  uint8_t NewAUTimestamps (int AUtype);
  bool NewAUBuffers (int AUtype);

public:
  unsigned int num_sequence;
  unsigned int num_seq_end;
  unsigned int num_pictures;
  unsigned int num_groups;
  unsigned int num_frames[4];
  unsigned int avg_frames[4];

  unsigned int horizontal_size;
  unsigned int vertical_size;
  unsigned int aspect_ratio;
  unsigned int picture_rate;
  unsigned int bit_rate;
  unsigned int comp_bit_rate;
  unsigned int peak_bit_rate;
  unsigned int vbv_buffer_size;
  unsigned int CSPF;
  double secs_per_frame;


  bool dtspts_for_all_au;
  bool gop_control_packet;

protected:

  /* State variables for scanning source bit-stream */
  VAunit access_unit;
  unsigned int fields_presented;
  unsigned int group_order;
  unsigned int group_start_pic;
  unsigned int group_start_field;
  int temporal_reference;
  unsigned int pict_rate;
  unsigned int pict_struct;
  int pulldown_32;
  int repeat_first_field;
  int prev_temp_ref;
  double frame_rate;
  unsigned int max_bits_persec;
  int AU_pict_data;
  int AU_hdr;
  clockticks max_PTS;
  clockticks max_STD_buffer_delay;

  int opt_mpeg;
  int opt_multifile_segment;
};

//
// DVD's generate control sectors for each GOP...
//

class DVDVideoStream:public VideoStream
{
public:
  DVDVideoStream (IBitStream & ibs, OutputStream & into):VideoStream (ibs, into)
  {
    gop_control_packet = true;
  }
  void OutputGOPControlSector ();
};

#endif // __INPUTSTRM_H__


/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
