
#ifndef __OUTPUTSTREAM_H__
#define __OUTPUTSTREAM_H__

#include <config.h>
#include <stdio.h>
#include "mjpeg_types.h"

#include "inputstrm.hh"
#include "padstrm.hh"
#include "systems.hh"

typedef enum
{ start_segment, mid_segment,
  runout_segment
}
segment_state;

class OutputStream
{
public:
  OutputStream ()
  {
    underrun_ignore = 0;
    underruns = 0;

    opt_verbosity = 1;
    opt_buffer_size = 46;
    opt_data_rate = 0;          /* 3486 = 174300B/sec would be right for VCD */
    opt_video_offset = 0;
    opt_audio_offset = 0;
    opt_sector_size = 2324;
    opt_VBR = 0;
    opt_mpeg = 1;
    opt_mux_format = 0;         /* Generic MPEG-1 stream as default */
    opt_multifile_segment = 0;
    opt_always_system_headers = 0;
    opt_packets_per_pack = 20;
    opt_ignore_underrun = false;
    opt_max_segment_size = 0;
  }
  bool OutputMultiplex ();
  void Init (vector < ElementaryStream * >*strms, PS_Stream *stream);
  void Close ();

  void InitSyntaxParameters ();
  void ByteposTimecode (bitcount_t bytepos, clockticks & ts);

  inline Sys_header_struc *SystemHeader ()
  {
    return &sys_header;
  }

  unsigned int PacketPayload (MuxStream & strm, bool buffers, bool PTSstamp, bool DTSstamp);
  unsigned int WritePacket (unsigned int max_packet_data_size,
			    MuxStream & strm,
			    bool buffers, clockticks PTS, clockticks DTS, uint8_t timestamps);

  /* Special "unusual" sector types needed for particular formats 
   */

  void OutputDVDPriv2 ();

  /* Syntax control parameters, public becaus they're partly referenced
     by the input stream objects.
   */

  bool always_sys_header_in_pack;
  bool dtspts_for_all_vau;
  bool sys_header_in_pack1;
  bool buffers_in_video;
  bool always_buffers_in_video;
  bool buffers_in_audio;
  bool always_buffers_in_audio;
  bool sector_align_iframeAUs;
  bool split_at_seq_end;
  bool seg_starts_with_video;
  bool timestamp_iframe_only;
  bool video_buffers_iframe_only;
  unsigned int audio_buffer_size;
  unsigned int video_buffer_size;

  /* more profile options */
  int opt_verbosity;
  int opt_quiet_mode;
  int opt_buffer_size;
  int opt_data_rate;
  int opt_video_offset;
  int opt_audio_offset;
  int opt_sector_size;
  int opt_VBR;
  int opt_mpeg;
  int opt_mux_format;
  int opt_multifile_segment;
  int opt_always_system_headers;
  int opt_packets_per_pack;
  bool opt_stills;
  bool opt_ignore_underrun;
  int verbose;
  off_t opt_max_segment_size;

  /* Sequence run-out control */
  bool running_out;
  clockticks runout_PTS;


/* In some situations the system/PES packets are embedded with
   external transport data which has to be taken into account for SCR
   calculations to be correct.  E.g. VCD streams. Where each 2324 byte
   system packet is embedded in a 2352 byte CD sector and the actual
   MPEG data is preceded by 30 empty sectors.
*/

  unsigned int sector_transport_size;
  unsigned int transport_prefix_sectors;
  unsigned int sector_size;
  unsigned int vcd_zero_stuffing;	/* VCD audio sectors have 20 0 bytes :-( */

  int dmux_rate;		/* Actual data mux-rate for calculations always a multiple of 50  */
  int mux_rate;			/* MPEG mux rate (50 byte/sec units      */
  unsigned int packets_per_pack;

private:

  /* Stream packet component buffers */

  Sys_header_struc sys_header;
  Pack_struc pack_header;
  Pack_struc *pack_header_ptr;
  Sys_header_struc *sys_header_ptr;
  bool start_of_new_pack;
  bool include_sys_header;

  /* Under-run error messages */
  unsigned int underruns;
  unsigned int underrun_ignore;

  /* Output data stream... */
  PS_Stream *psstrm;
  bitcount_t bytes_output;
  clockticks ticks_per_sector;

public:
  clockticks current_SCR;
private:
  clockticks audio_delay;
  clockticks video_delay;
  bool vbr;

  /* Source data streams */
  /* Note: 1st video stream is regarded as the "master" stream for
     the purpose of splitting sequences etc...
   */
  vector < ElementaryStream * >*estreams;	// Complete set
  vector < ElementaryStream * >vstreams;	// Video streams in estreams
  vector < ElementaryStream * >astreams;	// Audio streams in estreams

  PaddingStream pstrm;
  VCDAPadStream vcdapstrm;
  DVDPriv2Stream dvdpriv2strm;

private:
  unsigned int RunInSectors ();

  void NextPosAndSCR ();
  void SetPosAndSCR (bitcount_t bytepos);

  void OutputPrefix ();

  void OutputSuffix ();
  void OutputPadding (bool vcd_audio_pad);
  void MuxStatus (log_level_t level);

  void WriteRawSector (uint8_t * rawpackets, unsigned int length);
  void AppendMuxStreamsOf (vector < ElementaryStream * >&elem, vector < MuxStream * >&mux);

  /* state var for muxing */
  segment_state seg_state;

  vector < bool > completed;
  vector < bool >::iterator pcomp;
  vector < ElementaryStream * >::iterator str;

  unsigned int packets_left_in_pack;        /* Suppress warning */
  bool padding_packet;
  bool video_first;

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
};


#endif //__OUTPUTSTREAM_H__
