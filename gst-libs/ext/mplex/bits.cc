/** @file bits.cc, bit-level output                                              */

/* Copyright (C) 2001, Andrew Stevens <andrew.stevens@philips.com> *

 *
 * Disclaimer of Warranty
 *
 * These software programs are available to the user without any license fee or
 * royalty on an "as is" basis.  The MPEG Software Simulation Group disclaims
 * any and all warranties, whether express, implied, or statuary, including any
 * implied warranties or merchantability or of fitness for a particular
 * purpose.  In no event shall the copyright-holder be liable for any
 * incidental, punitive, or consequential damages of any kind whatsoever
 * arising from the use of these programs.
 *
 * This disclaimer of warranty extends to the user of these programs and user's
 * customers, employees, agents, transferees, successors, and assigns.
 *
 * The MPEG Software Simulation Group does not represent or warrant that the
 * programs furnished hereunder are free of infringement of any third-party
 * patents.
 *
 * Commercial implementations of MPEG-1 and MPEG-2 video, including shareware,
 * are subject to royalty fees to patent holders.  Many of these patents are
 * general enough such that they are unavoidable regardless of implementation
 * design.
 *
 */

#include <config.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <assert.h>
#include "mjpeg_logging.h"
#include "bits.hh"


/// Initializes the bitstream, sets internal variables.
// TODO: The buffer size should be set dynamically to sensible sizes.
//
BitStream::BitStream ():
user_data (NULL)
{
  totbits = 0LL;
  buffer_start = 0LL;
  eobs = true;
  readpos = 0LL;
  bfr = 0;
  bfr_size = 0;
}

/// Deconstructor. Deletes the internal buffer. 
BitStream::~BitStream ()
{
  delete bfr;
}

/**
   Refills an IBitStream's input buffer based on the internal variables bufcount and bfr_size. 
 */
bool
IBitStream::refill_buffer ()
{
  size_t i;

  if (bufcount >= bfr_size) {
    SetBufSize (bfr_size + 4096);
  }

  i = read_callback (this, bfr + bufcount, static_cast < size_t > (bfr_size - bufcount), user_data);
  bufcount += i;

  if (i == 0) {
    eobs = true;
    return false;
  }
  return true;
}

/**
  Flushes all read input up-to *but not including* bit
  unbuffer_upto.
@param flush_to the number of bits to flush
*/

void
IBitStream::flush (bitcount_t flush_upto)
{
  if (flush_upto > buffer_start + bufcount)
    mjpeg_error_exit1 ("INTERNAL ERROR: attempt to flush input beyond buffered amount");

  if (flush_upto < buffer_start)
    mjpeg_error_exit1
      ("INTERNAL ERROR: attempt to flush input stream before  first buffered byte %d last is %d",
       (int) flush_upto, (int) buffer_start);
  unsigned int bytes_to_flush = static_cast < unsigned int >(flush_upto - buffer_start);

  //
  // Don't bother actually flushing until a good fraction of a buffer
  // will be cleared.
  //

  if (bytes_to_flush < bfr_size * 3 / 4)
    return;

  bufcount -= bytes_to_flush;
  buffer_start = flush_upto;
  byteidx -= bytes_to_flush;
  memmove (bfr, bfr + bytes_to_flush, static_cast < size_t > (bufcount));
}


/**
  Undo scanning / reading
  N.b buffer *must not* be flushed between prepareundo and undochanges.
  @param undo handle to store the undo information
*/
void
IBitStream::prepareundo (BitStreamUndo & undo)
{
  undo = *(static_cast < BitStreamUndo * >(this));
}

/**
Undoes changes committed to an IBitStream. 
@param undo handle to retrieve the undo information   
 */
void
IBitStream::undochanges (BitStreamUndo & undo)
{
  *(static_cast < BitStreamUndo * >(this)) = undo;
}

/**
   Read a number bytes over an IBitStream, using the buffer. 
   @param dst buffer to read to 
   @param length the number of bytes to read
 */
unsigned int
IBitStream::read_buffered_bytes (uint8_t * dst, unsigned int length)
{
  unsigned int to_read = length;

  if (readpos < buffer_start)
    mjpeg_error_exit1
      ("INTERNAL ERROR: access to input stream buffer @ %d: before first buffered byte (%d)",
       (int) readpos, (int) buffer_start);

  if (readpos + length > buffer_start + bufcount) {
  /*
    if (!feof (fileh)) {
      mjpeg_error
	("INTERNAL ERROR: access to input stream buffer beyond last buffered byte @POS=%lld END=%d REQ=%lld + %d bytes",
	 readpos, bufcount, readpos - (bitcount_t) buffer_start, length);
      abort ();
    }
    */
    to_read = static_cast < unsigned int >((buffer_start + bufcount) - readpos);
  }
  memcpy (dst, bfr + (static_cast < unsigned int >(readpos - buffer_start)), to_read);
  // We only ever flush up to the start of a read as we
  // have only scanned up to a header *beginning* a block that is then
  // read
  flush (readpos);
  readpos += to_read;
  return to_read;
}

/** open the device to read the bit stream from it 
@param bs_filename filename to open
@param buf_size size of the internal buffer 
*/
void
IBitStream::open (ReadCallback read_callback, void *user_data, unsigned int buf_size)
{
  this->read_callback = read_callback;
  this->user_data = user_data;

  bfr_size = buf_size;
  if (bfr == NULL)
    bfr = new uint8_t[buf_size];
  else {
    delete bfr;

    bfr = new uint8_t[buf_size];
  }

  byteidx = 0;
  bitidx = 8;
  totbits = 0LL;
  bufcount = 0;
  eobs = false;
  if (!refill_buffer ()) {
    if (bufcount == 0) {
      mjpeg_error_exit1 ("Unable to read.");
    }
  }
}


/** sets the internal buffer size. 
    @param new_buf_size the new internal buffer size 
*/
void
IBitStream::SetBufSize (unsigned int new_buf_size)
{
  assert (bfr != NULL);		// Must be open first!
  assert (new_buf_size >= bfr_size);	// Can only be increased in size...

  if (bfr_size != new_buf_size) {
    uint8_t *new_buf = new uint8_t[new_buf_size];

    memcpy (new_buf, bfr, static_cast < size_t > (bfr_size));
    delete bfr;

    bfr_size = new_buf_size;
    bfr = new_buf;
  }

}

/**
   close the device containing the bit stream after a read process
*/
void
IBitStream::close ()
{
}


// TODO replace with shift ops!

uint8_t
  IBitStream::masks[8] = {
    0x1,
    0x2,
    0x4,
    0x8,
    0x10,
    0x20,
    0x40,
0x80 };

/*read 1 bit from the bit stream 
@returns the read bit, 0 on EOF */
uint32_t
IBitStream::get1bit ()
{
  unsigned int bit;

  if (eobs)
    return 0;

  bit = (bfr[byteidx] & masks[bitidx - 1]) >> (bitidx - 1);
  totbits++;
  bitidx--;
  if (!bitidx) {
    bitidx = 8;
    byteidx++;
    if (byteidx == bufcount) {
      refill_buffer ();
    }
  }

  return bit;
}

/*read N bits from the bit stream 
@returns the read bits, 0 on EOF */
uint32_t
IBitStream::getbits (int N)
{
  uint32_t val = 0;
  int i = N;
  unsigned int j;

  // Optimize: we are on byte boundary and want to read multiple of bytes!
  if ((bitidx == 8) && ((N & 7) == 0)) {
    i = N >> 3;
    while (i > 0) {
      if (eobs)
	return 0;
      val = (val << 8) | bfr[byteidx];
      byteidx++;
      totbits += 8;
      if (byteidx == bufcount) {
	refill_buffer ();
      }
      i--;
    }
  } else {
    while (i > 0) {
      if (eobs)
	return 0;

      j = (bfr[byteidx] & masks[bitidx - 1]) >> (bitidx - 1);
      totbits++;
      bitidx--;
      if (!bitidx) {
	bitidx = 8;
	byteidx++;
	if (byteidx == bufcount) {
	  refill_buffer ();
	}
      }
      val = (val << 1) | j;
      i--;
    }
  }
  return val;
}


/** This function seeks for a byte aligned sync word (max 32 bits) in the bit stream and
    places the bit stream pointer right after the sync.
    This function returns 1 if the sync was found otherwise it returns 0
@param sync the sync word to search for
@param N the number of bits to retrieve
@param lim number of bytes to search through
@returns false on error */

bool
IBitStream::seek_sync (uint32_t sync, int N, int lim)
{
  uint32_t val, val1;
  uint32_t maxi = ((1U << N) - 1); /* pow(2.0, (double)N) - 1 */ ;
  if (maxi == 0) {
    maxi = 0xffffffff;
  }
  while (bitidx != 8) {
    get1bit ();
  }

  val = getbits (N);
  if (eobs)
    return false;
  while ((val & maxi) != sync && --lim) {
    val <<= 8;
    val1 = getbits (8);
    val |= val1;
    if (eobs)
      return false;
  }

  return (!!lim);
}



/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
