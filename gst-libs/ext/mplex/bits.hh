#ifndef __BITS_H__
#define __BITS_H__

#include <config.h>
#include <stdio.h>

typedef uint64_t bitcount_t;


class BitStreamUndo
{
//protected:
public:
  uint8_t outbyte;
  unsigned int byteidx;
  int bitidx;
  unsigned int bufcount;
  fpos_t actpos;
  bitcount_t totbits;
  bitcount_t buffer_start;
  bitcount_t readpos;
  uint8_t *bfr;
  unsigned int bfr_size;
public:
  bool eobs;
};

typedef class BitStream _BitStream;

typedef size_t (*ReadCallback) 	(_BitStream *bs, uint8_t *dest, size_t size, void *user_data);

class BitStream : public BitStreamUndo
{
public:
  void *user_data;
  static const unsigned int BUFFER_SIZE = 4 * 1024;
public:
    BitStream ();
   ~BitStream ();
  inline bitcount_t bitcount ()
  {
    return totbits;
  }
  inline bool eos ()
  {
    return eobs;
  }
};


//
// Input bit stream class.   Supports the "scanning" of a stream
// into a large buffer which is flushed once it has been read.
// N.b. if you scan ahead a long way and don't read its your
// responsibility to flush manually...
//

class IBitStream:public BitStream
{
public:
  void open (ReadCallback read_callback, void *user_data, unsigned int buf_size = BUFFER_SIZE);
  void SetBufSize (unsigned int buf_size);
  void close ();
  uint32_t get1bit ();
  uint32_t getbits (int N);
  void prepareundo (BitStreamUndo & undobuf);
  void undochanges (BitStreamUndo & undobuf);
  bool seek_sync (unsigned int sync, int N, int lim);
  void flush (bitcount_t bitposition);
  inline unsigned int buffered_bytes ()
  {
    return (buffer_start + bufcount - readpos);
  }
  unsigned int read_buffered_bytes (uint8_t * dst, unsigned int length_bytes);

private:
  bool refill_buffer ();
  static uint8_t masks[8];
  ReadCallback read_callback;
};

#endif // __BITS_H__
