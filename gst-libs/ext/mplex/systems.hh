#ifndef __SYSTEMS_HH__
#define __SYSTEMS_HH__

#include "inputstrm.hh"

#include <vector>

using std::vector;

/* Buffer size parameters */

#define MAX_SECTOR_SIZE         16384
#define MAX_PACK_HEADER_SIZE	255
#define MAX_SYS_HEADER_SIZE     255


typedef class PS_Stream _PS_Stream;

typedef size_t (*WriteCallback) (_PS_Stream *str, uint8_t *data, size_t size, void *user_data);


typedef struct sector_struc	/* Ein Sektor, kann Pack, Sys Header    */
/* und Packet enthalten.		*/
{
  unsigned char buf[MAX_SECTOR_SIZE];
  unsigned int length_of_packet_data;
  //clockticks TS                ;
}
Sector_struc;

typedef struct pack_struc	/* Pack Info                            */
{
  unsigned char buf[MAX_PACK_HEADER_SIZE];
  int length;
  clockticks SCR;
}
Pack_struc;

typedef struct sys_header_struc	/* System Header Info                   */
{
  unsigned char buf[MAX_SYS_HEADER_SIZE];
  int length;
}
Sys_header_struc;


class PS_Stream {
public:
  PS_Stream (WriteCallback _callback, void *_user_data):
    callback (_callback),
    user_data (_user_data)
    {
    }

  void Init (unsigned _mpeg, unsigned int _sector_sizen, off_t max_segment_size);	// 0 = No Limit

  bool FileLimReached ();
  void NextFile ();
  unsigned int PacketPayload (MuxStream & strm,
		 Sys_header_struc * sys_header,
		 Pack_struc * pack_header, int buffers, int PTSstamp, int DTSstamp);

  unsigned int CreateSector (Pack_struc * pack,
		Sys_header_struc * sys_header,
		unsigned int max_packet_data_size,
		MuxStream & strm,
		bool buffers, bool end_marker, clockticks PTS, clockticks DTS, uint8_t timestamps);
  void RawWrite (uint8_t * data, unsigned int len);
  static void BufferSectorHeader (uint8_t * buf,
		      Pack_struc * pack, Sys_header_struc * sys_header, uint8_t * &header_end);
  static void BufferPacketHeader (uint8_t * buf,
		      uint8_t type,
		      unsigned int mpeg_version,
		      bool buffers,
		      unsigned int buffer_size,
		      uint8_t buffer_scale,
		      clockticks PTS,
		      clockticks DTS,
		      uint8_t timestamps, uint8_t * &size_field, uint8_t * &header_end);

  static inline void BufferPacketSize (uint8_t * size_field, uint8_t * packet_end)
  {
    unsigned int
      packet_size =
      packet_end -
      size_field -
      2;

    size_field[0] = static_cast < uint8_t > (packet_size >> 8);
    size_field[1] = static_cast < uint8_t > (packet_size & 0xff);

  }

  void CreatePack (Pack_struc * pack, clockticks SCR, unsigned int mux_rate);
  void CreateSysHeader (Sys_header_struc * sys_header,
		   unsigned int rate_bound,
		   bool fixed,
		   int CSPS, bool audio_lock, bool video_lock, vector < MuxStream * >&streams);

  void Close ()
  {
  }

private:

  /* TODO: Replace **'s with *&'s */
  static void BufferDtsPtsMpeg1ScrTimecode (clockticks timecode, uint8_t marker, uint8_t ** buffer);
  static void BufferMpeg2ScrTimecode (clockticks timecode, uint8_t ** buffer);
  void BufferPaddingPacket (int padding, uint8_t * &buffer);

private:
  unsigned int mpeg_version;
  unsigned int sector_size;
  int segment_num;

  off_t max_segment_size;
  uint8_t * sector_buf;
  WriteCallback callback;
  void	*user_data;
  off_t written;
};
#endif // __SYSTEMS_HH__


/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
