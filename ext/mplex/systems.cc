#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include "systems.hh"
#include "mplexconsts.hh"
#include <cassert>

uint8_t dummy_buf[8000];
void
PS_Stream::Init (unsigned _mpeg, unsigned int _sector_size, off_t max_seg_size)
{
  max_segment_size = max_seg_size;
  mpeg_version = _mpeg;
  sector_size = _sector_size;
  segment_num = 1;
  written = 0;

  sector_buf = new uint8_t[_sector_size];
}

bool PS_Stream::FileLimReached ()
{
  return max_segment_size != 0 && written > max_segment_size;
}

void
PS_Stream::NextFile ()
{
/*
  char prev_filename[strlen (cur_filename) + 1];

  //fclose (strm);
  ++segment_num;
  strcpy (prev_filename, cur_filename);
  snprintf (cur_filename, MAXPATHLEN, filename_pat, segment_num);
  if (strcmp (prev_filename, cur_filename) == 0) {
    mjpeg_error_exit1
      ("Need to split output but there appears to be no %%d in the filename pattern %s",
       filename_pat);
  }
  strm = fopen (cur_filename, "wb");
  if (strm == NULL) {
    mjpeg_error_exit1 ("Could not open for writing: %s", cur_filename);
  }
  */
}

/**************************************************************

 	 Packet payload compute how much payload a sector-sized packet with the 
	 specified headers can carry...
     TODO: Should really be called "Sector Payload"
**************************************************************/


unsigned int
PS_Stream::PacketPayload (MuxStream & strm,
			  Sys_header_struc * sys_header,
			  Pack_struc * pack_header, int buffers, int PTSstamp, int DTSstamp)
{
  int payload = sector_size - (PACKET_HEADER_SIZE + strm.zero_stuffing);

  if (sys_header != NULL)
    payload -= sys_header->length;
  if (mpeg_version == 2) {
    if (buffers)
      payload -= MPEG2_BUFFERINFO_LENGTH;

    payload -= MPEG2_AFTER_PACKET_LENGTH_MIN;
    if (pack_header != NULL)
      payload -= pack_header->length;
    if (DTSstamp)
      payload -= DTS_PTS_TIMESTAMP_LENGTH;
    if (PTSstamp)
      payload -= DTS_PTS_TIMESTAMP_LENGTH;

  } else {
    if (buffers)
      payload -= MPEG1_BUFFERINFO_LENGTH;

    payload -= MPEG1_AFTER_PACKET_LENGTH_MIN;
    if (pack_header != NULL)
      payload -= pack_header->length;
    if (DTSstamp)
      payload -= DTS_PTS_TIMESTAMP_LENGTH;
    if (PTSstamp)
      payload -= DTS_PTS_TIMESTAMP_LENGTH;
    if (DTSstamp || PTSstamp)
      payload += 1;		/* No need for nostamp marker ... */

  }

  return payload;
}



/*************************************************************************
    Kopiert einen TimeCode in einen Bytebuffer. Dabei wird er nach
    MPEG-Verfahren in bits aufgesplittet.

    Makes a Copy of a TimeCode in a Buffer, splitting it into bitfields
    for MPEG-1/2 DTS/PTS fields and MPEG-1 pack scr fields
*************************************************************************/

void
PS_Stream::BufferDtsPtsMpeg1ScrTimecode (clockticks timecode, uint8_t marker, uint8_t ** buffer)
{
  clockticks thetime_base;
  uint8_t temp;
  unsigned int msb, lsb;

  /* MPEG-1 uses a 90KHz clock, extended to 300*90KHz = 27Mhz in MPEG-2 */
  /* For these fields we only encode to MPEG-1 90Khz resolution... */

  thetime_base = timecode / 300;
  msb = (thetime_base >> 32) & 1;
  lsb = (thetime_base & 0xFFFFFFFFLL);

  temp = (marker << 4) | (msb << 3) | ((lsb >> 29) & 0x6) | 1;
  *((*buffer)++) = temp;
  temp = (lsb & 0x3fc00000) >> 22;
  *((*buffer)++) = temp;
  temp = ((lsb & 0x003f8000) >> 14) | 1;
  *((*buffer)++) = temp;
  temp = (lsb & 0x7f80) >> 7;
  *((*buffer)++) = temp;
  temp = ((lsb & 0x007f) << 1) | 1;
  *((*buffer)++) = temp;

}

/*************************************************************************
    Makes a Copy of a TimeCode in a Buffer, splitting it into bitfields
    for MPEG-2 pack scr fields  which use the full 27Mhz resolution
    
    Did they *really* need to put a 27Mhz
    clock source into the system stream.  Does anyone really need it
    for their decoders?  Get real... I guess they thought it might allow
    someone somewhere to save on a proper clock circuit.
*************************************************************************/


void
PS_Stream::BufferMpeg2ScrTimecode (clockticks timecode, uint8_t ** buffer)
{
  clockticks thetime_base;
  unsigned int thetime_ext;
  uint8_t temp;
  unsigned int msb, lsb;

  thetime_base = timecode / 300;
  thetime_ext = timecode % 300;
  msb = (thetime_base >> 32) & 1;
  lsb = thetime_base & 0xFFFFFFFFLL;


  temp = (MARKER_MPEG2_SCR << 6) | (msb << 5) | ((lsb >> 27) & 0x18) | 0x4 | ((lsb >> 28) & 0x3);
  *((*buffer)++) = temp;
  temp = (lsb & 0x0ff00000) >> 20;
  *((*buffer)++) = temp;
  temp = ((lsb & 0x000f8000) >> 12) | 0x4 | ((lsb & 0x00006000) >> 13);
  *((*buffer)++) = temp;
  temp = (lsb & 0x00001fe0) >> 5;
  *((*buffer)++) = temp;
  temp = ((lsb & 0x0000001f) << 3) | 0x4 | ((thetime_ext & 0x00000180) >> 7);
  *((*buffer)++) = temp;
  temp = ((thetime_ext & 0x0000007F) << 1) | 1;
  *((*buffer)++) = temp;
}

/*************************************************************************

BufferPaddingPacket - Insert a padding packet of the desired length
                      into the specified Program/System stream buffer

**************************************************************************/

void
PS_Stream::BufferPaddingPacket (int padding, uint8_t * &buffer)
{
  uint8_t *index = buffer;
  int i;

  assert ((mpeg_version == 2 && padding >= 6) || (mpeg_version == 1 && padding >= 7));

  *(index++) = static_cast < uint8_t > (PACKET_START) >> 16;
  *(index++) = static_cast < uint8_t > (PACKET_START & 0x00ffff) >> 8;
  *(index++) = static_cast < uint8_t > (PACKET_START & 0x0000ff);
  *(index++) = PADDING_STR;
  *(index++) = static_cast < uint8_t > ((padding - 6) >> 8);
  *(index++) = static_cast < uint8_t > ((padding - 6) & 0xff);
  if (mpeg_version == 2) {
    for (i = 0; i < padding - 6; i++)
      *(index++) = static_cast < uint8_t > (STUFFING_BYTE);
  } else {
    *(index++) = 0x0F;
    for (i = 0; i < padding - 7; i++)
      *(index++) = static_cast < uint8_t > (STUFFING_BYTE);
  }

  buffer = index;
}


void
PS_Stream::BufferSectorHeader (uint8_t * index,
			       Pack_struc * pack,
			       Sys_header_struc * sys_header, uint8_t * &header_end)
{
  /* Pack header if present */

  if (pack != NULL) {
    memcpy (index, pack->buf, pack->length);
    index += pack->length;
  }

  /* System header if present */

  if (sys_header != NULL) {
    memcpy (index, sys_header->buf, sys_header->length);
    index += sys_header->length;
  }
  header_end = index;
}

/******************************************
 *
 * BufferPacketHeader
 * Construct and MPEG-1/2 header for a packet in the specified
 * buffer (which *MUST* be long enough) and set points to the start of
 * the payload and packet length fields.
 *
 ******************************************/


void
PS_Stream::BufferPacketHeader (uint8_t * buf,
			       uint8_t type,
			       unsigned int mpeg_version,
			       bool buffers,
			       unsigned int buffer_size,
			       uint8_t buffer_scale,
			       clockticks PTS,
			       clockticks DTS,
			       uint8_t timestamps, uint8_t * &size_field, uint8_t * &header_end)
{

  uint8_t *index = buf;
  uint8_t *pes_header_len_field = 0;


  /* konstante Packet Headerwerte eintragen */
  /* write constant packet header data */

  *(index++) = static_cast < uint8_t > (PACKET_START) >> 16;
  *(index++) = static_cast < uint8_t > (PACKET_START & 0x00ffff) >> 8;
  *(index++) = static_cast < uint8_t > (PACKET_START & 0x0000ff);
  *(index++) = type;


  /* we remember this offset so we can fill in the packet size field once
     we know the actual size... */
  size_field = index;
  index += 2;

  if (mpeg_version == 1) {
    /* MPEG-1: buffer information */
    if (buffers) {
      *(index++) = static_cast < uint8_t > (0x40 | (buffer_scale << 5) | (buffer_size >> 8));
      *(index++) = static_cast < uint8_t > (buffer_size & 0xff);
    }

    /* MPEG-1: PTS, PTS & DTS, oder gar nichts? */
    /* should we write PTS, PTS & DTS or nothing at all ? */

    switch (timestamps) {
      case TIMESTAMPBITS_NO:
	*(index++) = MARKER_NO_TIMESTAMPS;
	break;
      case TIMESTAMPBITS_PTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_JUST_PTS, &index);
	break;
      case TIMESTAMPBITS_PTS_DTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_PTS, &index);
	BufferDtsPtsMpeg1ScrTimecode (DTS, MARKER_DTS, &index);
	break;
    }
  } else if (type != PADDING_STR) {
    /* MPEG-2 packet syntax header flags. */
    /* These *DO NOT* appear in padding packets                     */
    /* TODO: They don't appear in several others either!    */
    /* First byte:
       <1,0><PES_scrambling_control:2=0><PES_priority><data_alignment_ind.=0>
       <copyright=0><original=1> */
    *(index++) = 0x81;
    /* Second byte: PTS PTS_DTS or neither?  Buffer info?
       <PTS_DTS:2><ESCR=0><ES_rate=0>
       <DSM_trick_mode:2=0><PES_CRC=0><PES_extension=(!!buffers)>
     */
    *(index++) = (timestamps << 6) | (!!buffers);
    /* Third byte:
       <PES_header_length:8> */
    pes_header_len_field = index;	/* To fill in later! */
    index++;
    /* MPEG-2: the timecodes if required */
    switch (timestamps) {
      case TIMESTAMPBITS_PTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_JUST_PTS, &index);
	break;

      case TIMESTAMPBITS_PTS_DTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_PTS, &index);
	BufferDtsPtsMpeg1ScrTimecode (DTS, MARKER_DTS, &index);
	break;
    }

    /* MPEG-2 The buffer information in a PES_extension */
    if (buffers) {
      /* MPEG-2 PES extension header
         <PES_private_data:1=0><pack_header_field=0>
         <program_packet_sequence_counter=0>
         <P-STD_buffer=1><reserved:3=1><{PES_extension_flag_2=0> */
      *(index++) = static_cast < uint8_t > (0x1e);
      *(index++) = static_cast < uint8_t > (0x40 | (buffer_scale << 5) | (buffer_size >> 8));
      *(index++) = static_cast < uint8_t > (buffer_size & 0xff);
    }
  }

  if (mpeg_version == 2 && type != PADDING_STR) {
    *pes_header_len_field = static_cast < uint8_t > (index - (pes_header_len_field + 1));
  }

  header_end = index;
}

/*************************************************************************
 *	CreateSector
 *
 *  Creates a complete sector to carry a padding packet or a packet
 *  from one of the elementary streams.  Pack and System headers are
 *  prepended if required.
 *
 *  We allow for situations where want to
 *  deliberately reduce the payload carried by stuffing.
 *  This allows us to deal with tricky situations where the
 *	header overhead of adding in additional information
 *	would exceed the remaining payload capacity.
 *
 *    Header stuffing and/or a padding packet is appended if the sector is
 *    unfilled.   Zero stuffing after the end of a packet is also supported
 *    to allow thos wretched audio packets from VCD's to be handled.
 *
 *  TODO: Should really be called "WriteSector"
 *  TODO: We need to add a mechanism for sub-headers of private streams
 *        to be generated...
 *
 *************************************************************************/


unsigned int
PS_Stream::CreateSector (Pack_struc * pack,
			 Sys_header_struc * sys_header,
			 unsigned int max_packet_data_size,
			 MuxStream & strm,
			 bool buffers,
			 bool end_marker, clockticks PTS, clockticks DTS, uint8_t timestamps)
{
  unsigned int i;
  unsigned int j;
  uint8_t *index;
  uint8_t *size_offset;
  uint8_t *fixed_packet_header_end;
  uint8_t *pes_header_len_offset = 0;
  unsigned int target_packet_data_size;
  unsigned int actual_packet_data_size;
  int packet_data_to_read;
  unsigned int bytes_short;
  uint8_t type = strm.stream_id;
  uint8_t buffer_scale = strm.BufferScale ();
  unsigned int buffer_size = strm.BufferSizeCode ();
  unsigned int sector_pack_area;

  index = sector_buf;

  sector_pack_area = sector_size - strm.zero_stuffing;
  if (end_marker)
    sector_pack_area -= 4;

  BufferSectorHeader (index, pack, sys_header, index);

  /* konstante Packet Headerwerte eintragen */
  /* write constant packet header data */

  *(index++) = static_cast < uint8_t > (PACKET_START) >> 16;
  *(index++) = static_cast < uint8_t > (PACKET_START & 0x00ffff) >> 8;
  *(index++) = static_cast < uint8_t > (PACKET_START & 0x0000ff);
  *(index++) = type;


  /* we remember this offset so we can fill in the packet size field once
     we know the actual size... */
  size_offset = index;
  index += 2;
  fixed_packet_header_end = index;

  if (mpeg_version == 1) {
    /* MPEG-1: buffer information */
    if (buffers) {
      *(index++) = static_cast < uint8_t > (0x40 | (buffer_scale << 5) | (buffer_size >> 8));
      *(index++) = static_cast < uint8_t > (buffer_size & 0xff);
    }

    /* MPEG-1: PTS, PTS & DTS, oder gar nichts? */
    /* should we write PTS, PTS & DTS or nothing at all ? */

    switch (timestamps) {
      case TIMESTAMPBITS_NO:
	*(index++) = MARKER_NO_TIMESTAMPS;
	break;
      case TIMESTAMPBITS_PTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_JUST_PTS, &index);
	break;
      case TIMESTAMPBITS_PTS_DTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_PTS, &index);
	BufferDtsPtsMpeg1ScrTimecode (DTS, MARKER_DTS, &index);
	break;
    }
  } else if (type != PADDING_STR) {
    /* MPEG-2 packet syntax header flags. */
    /* These *DO NOT* appear in padding packets                     */
    /* TODO: They don't appear in several others either!    */
    /* First byte:
       <1,0><PES_scrambling_control:2=0><PES_priority><data_alignment_ind.=0>
       <copyright=0><original=1> */
    *(index++) = 0x81;
    /* Second byte: PTS PTS_DTS or neither?  Buffer info?
       <PTS_DTS:2><ESCR=0><ES_rate=0>
       <DSM_trick_mode:2=0><PES_CRC=0><PES_extension=(!!buffers)>
     */
    *(index++) = (timestamps << 6) | (!!buffers);
    /* Third byte:
       <PES_header_length:8> */
    pes_header_len_offset = index;	/* To fill in later! */
    index++;
    /* MPEG-2: the timecodes if required */
    switch (timestamps) {
      case TIMESTAMPBITS_PTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_JUST_PTS, &index);
	break;

      case TIMESTAMPBITS_PTS_DTS:
	BufferDtsPtsMpeg1ScrTimecode (PTS, MARKER_PTS, &index);
	BufferDtsPtsMpeg1ScrTimecode (DTS, MARKER_DTS, &index);
	break;
    }

    /* MPEG-2 The buffer information in a PES_extension */
    if (buffers) {
      /* MPEG-2 PES extension header
         <PES_private_data:1=0><pack_header_field=0>
         <program_packet_sequence_counter=0>
         <P-STD_buffer=1><reserved:3=1><{PES_extension_flag_2=0> */
      *(index++) = static_cast < uint8_t > (0x1e);
      *(index++) = static_cast < uint8_t > (0x40 | (buffer_scale << 5) | (buffer_size >> 8));
      *(index++) = static_cast < uint8_t > (buffer_size & 0xff);
    }
  }
#ifdef MUX_DEBUG
  // DVD MPEG2: AC3 in PRIVATE_STR_1
  if (type == PRIVATE_STR_1) {
    ac3_header = index;
    // TODO: should allow multiple AC3 streams...
    //ac3_header[0] = AC3_SUB_STR_1; // byte: Audio stream number
    // byte: num of AC3 syncwords
    // byte: Offset first AC3 syncword (hi)
    // byte: Offset 2nd AC2 syncword (lo)
    //index += 4;
    //subheader_size = 4;
    subheader_size = 0;
  } else
#endif


    /* MPEG-1, MPEG-2: data available to be filled is packet_size less
     * header and MPEG-1 trailer... */

    target_packet_data_size = sector_pack_area - (index - sector_buf);


  /* DEBUG: A handy consistency check when we're messing around */
#ifdef MUX_DEBUG
  if (type != PADDING_STR && (end_marker ? target_packet_data_size + 4 : target_packet_data_size)
      !=
      PacketPayload (strm, sys_header, pack, buffers,
		     timestamps & TIMESTAMPBITS_PTS, timestamps & TIMESTAMPBITS_DTS))
  {
    printf ("\nPacket size calculation error %d S%d P%d B%d %d %d!\n ",
	    timestamps,
	    sys_header != 0, pack != 0, buffers,
	    target_packet_data_size,
	    PacketPayload (strm, sys_header, pack, buffers,
			   timestamps & TIMESTAMPBITS_PTS, timestamps & TIMESTAMPBITS_DTS));
    exit (1);
  }
#endif

  /* If a maximum payload data size is specified (!=0) and is
     smaller than the space available thats all we read (the
     remaining space is stuffed) */
  if (max_packet_data_size != 0 && max_packet_data_size < target_packet_data_size) {
    packet_data_to_read = max_packet_data_size;
  } else
    packet_data_to_read = target_packet_data_size;


  /* MPEG-1, MPEG-2: read in available packet data ... */

  actual_packet_data_size = strm.ReadPacketPayload (index, packet_data_to_read);

  // DVD MPEG2: AC3 in PRIVATE_STR_1: fill in syncword count and offset
#ifdef MUX_DEBUG
  if (type == PRIVATE_STR_1) {
    unsigned int syncwords_found;

    for (i = 0; i < actual_packet_data_size; ++i) {
      if (index[i + 4] == 0x0b && i + 5 < actual_packet_data_size && index[i + 5] == 0x77) {
	if (syncwords_found == 0) {
	  if (ac3_header[2] != static_cast < uint8_t > ((i + 1) >> 8) ||
	      ac3_header[3] != static_cast < uint8_t > ((i + 1) & 0xff))
	    printf ("BROKEN HEADER %2x %2x (%2x %2x)\n",
		    ac3_header[2],
		    ac3_header[3],
		    static_cast < uint8_t > ((i + 1) >> 8),
		    static_cast < uint8_t > ((i + 1) & 0xff));
	}
	++syncwords_found;
      }
    }
  }
#endif
  bytes_short = target_packet_data_size - actual_packet_data_size;

  /* Handle the situations where we don't have enough data to fill
     the packet size fully ...
     Small shortfalls are dealt with by stuffing, big ones by inserting
     padding packets.
   */


  if (bytes_short < MINIMUM_PADDING_PACKET_SIZE && bytes_short > 0) {
    if (mpeg_version == 1) {
      /* MPEG-1 stuffing happens *before* header data fields. */
      memmove (fixed_packet_header_end + bytes_short,
	       fixed_packet_header_end, actual_packet_data_size + (index - fixed_packet_header_end)
	);
      for (j = 0; j < bytes_short; ++j)
	fixed_packet_header_end[j] = static_cast < uint8_t > (STUFFING_BYTE);
    } else {
      memmove (index + bytes_short, index, actual_packet_data_size);
      for (j = 0; j < bytes_short; ++j)
	*(index + j) = static_cast < uint8_t > (STUFFING_BYTE);
    }
    index += bytes_short;
    bytes_short = 0;
  }


  /* MPEG-2: we now know the header length... but we mustn't forget
     to take into account any non-MPEG headers we've included.
     Currently this only happens for AC3 audio, but who knows...
   */
  if (mpeg_version == 2 && type != PADDING_STR) {
    unsigned int pes_header_len = index - (pes_header_len_offset + 1);

    *pes_header_len_offset = static_cast < uint8_t > (pes_header_len);
  }
  index += actual_packet_data_size;
  /* MPEG-1, MPEG-2: Now we know that actual packet size */
  size_offset[0] = static_cast < uint8_t > ((index - size_offset - 2) >> 8);
  size_offset[1] = static_cast < uint8_t > ((index - size_offset - 2) & 0xff);

  /* The case where we have fallen short enough to allow it to be
     dealt with by inserting a stuffing packet... */
  if (bytes_short != 0) {
    *(index++) = static_cast < uint8_t > (PACKET_START) >> 16;
    *(index++) = static_cast < uint8_t > (PACKET_START & 0x00ffff) >> 8;
    *(index++) = static_cast < uint8_t > (PACKET_START & 0x0000ff);
    *(index++) = PADDING_STR;
    *(index++) = static_cast < uint8_t > ((bytes_short - 6) >> 8);
    *(index++) = static_cast < uint8_t > ((bytes_short - 6) & 0xff);
    if (mpeg_version == 2) {
      for (i = 0; i < bytes_short - 6; i++)
	*(index++) = static_cast < uint8_t > (STUFFING_BYTE);
    } else {
      *(index++) = 0x0F;	/* TODO: A.Stevens 2000 Why is this here? */
      for (i = 0; i < bytes_short - 7; i++)
	*(index++) = static_cast < uint8_t > (STUFFING_BYTE);
    }

    bytes_short = 0;
  }

  if (end_marker) {
    *(index++) = static_cast < uint8_t > ((ISO11172_END) >> 24);
    *(index++) = static_cast < uint8_t > ((ISO11172_END & 0x00ff0000) >> 16);
    *(index++) = static_cast < uint8_t > ((ISO11172_END & 0x0000ff00) >> 8);
    *(index++) = static_cast < uint8_t > (ISO11172_END & 0x000000ff);
  }

  for (i = 0; i < strm.zero_stuffing; i++)
    *(index++) = static_cast < uint8_t > (0);


  /* At this point padding or stuffing will have ensured the packet
     is filled to target_packet_data_size
   */
  RawWrite (sector_buf, sector_size);
  return actual_packet_data_size;
}




/*************************************************************************
	Create_Pack
	erstellt in einem Buffer die spezifischen Pack-Informationen.
	Diese werden dann spaeter von der Sector-Routine nochmals
	in dem Sektor kopiert.

	writes specifical pack header information into a buffer
	later this will be copied from the sector routine into
	the sector buffer
*************************************************************************/

void
PS_Stream::CreatePack (Pack_struc * pack, clockticks SCR, unsigned int mux_rate)
{
  uint8_t *index;

  index = pack->buf;

  *(index++) = static_cast < uint8_t > ((PACK_START) >> 24);
  *(index++) = static_cast < uint8_t > ((PACK_START & 0x00ff0000) >> 16);
  *(index++) = static_cast < uint8_t > ((PACK_START & 0x0000ff00) >> 8);
  *(index++) = static_cast < uint8_t > (PACK_START & 0x000000ff);

  if (mpeg_version == 2) {
    /* Annoying: MPEG-2's SCR pack header time is different from
       all the rest... */
    BufferMpeg2ScrTimecode (SCR, &index);
    *(index++) = static_cast < uint8_t > (mux_rate >> 14);
    *(index++) = static_cast < uint8_t > (0xff & (mux_rate >> 6));
    *(index++) = static_cast < uint8_t > (0x03 | ((mux_rate & 0x3f) << 2));
    *(index++) = static_cast < uint8_t > (RESERVED_BYTE << 3 | 0);	/* No pack stuffing */
  } else {
    BufferDtsPtsMpeg1ScrTimecode (SCR, MARKER_MPEG1_SCR, &index);
    *(index++) = static_cast < uint8_t > (0x80 | (mux_rate >> 15));
    *(index++) = static_cast < uint8_t > (0xff & (mux_rate >> 7));
    *(index++) = static_cast < uint8_t > (0x01 | ((mux_rate & 0x7f) << 1));
  }
  pack->SCR = SCR;
  pack->length = index - pack->buf;
}


/*************************************************************************
	Create_Sys_Header
	erstelle in einem Buffer die spezifischen Sys_Header
	Informationen. Diese werden spaeter von der Sector-Routine
	nochmals zum Sectorbuffer kopiert.

	writes specifical system header information into a buffer
	later this will be copied from the sector routine into
	the sector buffer
	RETURN: Length of header created...
*************************************************************************/

void
PS_Stream::CreateSysHeader (Sys_header_struc * sys_header,
			    unsigned int rate_bound,
			    bool fixed,
			    int CSPS,
			    bool audio_lock, bool video_lock, vector < MuxStream * >&streams)
{
  uint8_t *index;
  uint8_t *len_index;
  int system_header_size;

  index = sys_header->buf;
  int video_bound = 0;
  int audio_bound = 0;

  vector < MuxStream * >::iterator str;
  for (str = streams.begin (); str < streams.end (); ++str) {
    switch (((*str)->stream_id & 0xe0)) {
      case 0xe0:		// MPEG Video
	++video_bound;
	break;
      case 0xb9:		// DVD seems to use this stream id in
	++video_bound;		// system headers for video buffer size
	break;
      case 0xc0:
	++audio_bound;		// MPEG Audio
	break;
      default:
	break;
    }
  }

  /* if we are not using both streams, we should clear some
     options here */

  *(index++) = static_cast < uint8_t > ((SYS_HEADER_START) >> 24);
  *(index++) = static_cast < uint8_t > ((SYS_HEADER_START & 0x00ff0000) >> 16);
  *(index++) = static_cast < uint8_t > ((SYS_HEADER_START & 0x0000ff00) >> 8);
  *(index++) = static_cast < uint8_t > (SYS_HEADER_START & 0x000000ff);

  len_index = index;		/* Skip length field for now... */
  index += 2;

  *(index++) = static_cast < uint8_t > (0x80 | (rate_bound >> 15));
  *(index++) = static_cast < uint8_t > (0xff & (rate_bound >> 7));
  *(index++) = static_cast < uint8_t > (0x01 | ((rate_bound & 0x7f) << 1));
  *(index++) = static_cast < uint8_t > ((audio_bound << 2) | (fixed << 1) | CSPS);
  *(index++) = static_cast < uint8_t > ((audio_lock << 7) | (video_lock << 6) | 0x20 | video_bound);

  *(index++) = static_cast < uint8_t > (RESERVED_BYTE);
  for (str = streams.begin (); str < streams.end (); ++str) {
    *(index++) = (*str)->stream_id;
    *(index++) = static_cast < uint8_t >
      (0xc0 | ((*str)->BufferScale () << 5) | ((*str)->BufferSizeCode () >> 8));
    *(index++) = static_cast < uint8_t > ((*str)->BufferSizeCode () & 0xff);
  }


  system_header_size = (index - sys_header->buf);
  len_index[0] = static_cast < uint8_t > ((system_header_size - 6) >> 8);
  len_index[1] = static_cast < uint8_t > ((system_header_size - 6) & 0xff);
  sys_header->length = system_header_size;
}


void
PS_Stream::RawWrite (uint8_t * buf, unsigned int len)
{
  if (callback (this, buf, len, user_data) != len) {
    mjpeg_error_exit1 ("Failed write");
  }
  written += len;
}

/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
