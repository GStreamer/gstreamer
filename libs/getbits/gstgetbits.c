#include <byteswap.h>
#include "gstgetbits.h"

unsigned long gst_getbits_nBitMask[] = { 
  0x00000000, 0x80000000, 0xc0000000, 0xe0000000,
  0xf0000000, 0xf8000000, 0xfc000000, 0xfe000000,
  0xff000000, 0xff800000, 0xffc00000, 0xffe00000,
  0xfff00000, 0xfff80000, 0xfffc0000, 0xfffe0000,
  0xffff0000, 0xffff8000, 0xffffc000, 0xffffe000,
  0xfffff000, 0xfffff800, 0xfffffc00, 0xfffffe00,
  0xffffff00, 0xffffff80, 0xffffffc0, 0xffffffe0,
  0xfffffff0, 0xfffffff8, 0xfffffffc, 0xfffffffe};

unsigned long _getbits_masks[] = {
  0x00000000,
  0x00000001, 0x00000003, 0x00000007, 0x0000000f,
  0x0000001f, 0x0000003f, 0x0000007f, 0x000000ff,
  0x000001ff, 0x000003ff, 0x000007ff, 0x00000fff,
  0x00001fff, 0x00003fff, 0x00007fff, 0x0000ffff,
  0x0001ffff, 0x0003ffff, 0x0007ffff, 0x000fffff,
  0x001fffff, 0x003fffff, 0x007fffff, 0x00ffffff,
  0x01ffffff, 0x03ffffff, 0x07ffffff, 0x0fffffff,
  0x1fffffff, 0x3fffffff, 0x7fffffff, 0xffffffff,
};

#ifdef HAVE_LIBMMX
unsigned long _getbits_64_minus_index[] = {
  64,63,62,61,60,59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,
  40,39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,
  16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1 };

/* this routine taken from Intel AppNote AP-527:
   "Using MMX[tm] Instructions to Get Bits From a Data Stream"
   written in C with libmmx to *closely* mimic Intel's ASM implementation

   this isn't as cycle-efficient, due to the simple fact that I must call
   emms() at the end.  all state must be kept in *gb, not in registers */
unsigned long getbits_mmx(gst_getbits_t *gb,unsigned long bits) {
  signed long remaining;
  unsigned long result;

  /* NOTE: this is a code-size optimization Intel seems to have missed!
     according to the MMX Programmer's Reference Manual, Chapter 5,
     neither movd nor movq have any effect on the flags.  that means you
     can put them before the sub and jl in their code, which I've done
     symbolically in this C code.  gcc is probably going to lose horribly,
     I'll do an inline asm version later.  I've a point to prove ;-) */
  /* find the right shift value, put it in mm3 */
  movd_m2r(_getbits_64_minus_index[bits],mm3);
  /* load the current quadword into mm0 */
  movq_m2r(gb->qword,mm0);
  /* copy it to mm2 */
  movq_r2r(mm0,mm2);

  remaining = gb->bits - bits;

  if (remaining <= 0) {
    unsigned long dword1,dword2;

    /* shift the pointer by 64 bits (8 bytes) */
    gb->ptr += 8;
    /* add 64 to bits remaining, to bring it positive */
    remaining += 64;

    /* grab the first 32 bits from the buffer and swap them around */
    dword1 = bswap_32(*(gb->ptr-8));
    /* grab the second 32 bits, swap */
    dword2 = bswap_32(*(gb->ptr-4));

    /* put second dword in mm4 */
    movd_m2r(dword2,mm4);
    /* shift mm2 over to make room for new bits */
    psrlq_r2r(mm3,mm2);

    /* put first dword in mm1 */
    movd_m2r(dword1,mm1);
    /* shift second dword up 32 bits */
    psrlq_i2r(32,mm4);

    /* put the shift counter in mm3 */
    movd_m2r(remaining,mm3);
    /* combine the swapped data in mm4 */
    por_r2r(mm1,mm4);

    /* save off the bits in mm4 to mm0 */
    movq_r2r(mm4,mm0);
    /* get the new low-order bits in mm4, shifted by 'mm3' */
    psrlq_r2r(mm3,mm4);

    /* save off new remaining bits */
    gb->bits = remaining;
    /* combine bits into mm2 */
    por_r2r(mm2,mm4);

    /* save off the result */
    movd_r2m(mm2,result);
    /* get rid of the bits we just read */
    psllq_r2r(mm1,mm0);

    /* save off mm0 */
    movq_r2m(mm0,gb->qword);

    /* finished with MMX */
    emms();

    /* we have what we came for */
    return(result);
  } else {
    /* load the number of bits requested into mm1 */
    movd_m2r(bits,mm1);
    /* shift the quadword in mm2 by 'mm3' bits */
    psrlq_r2r(mm3,mm2);

    /* update the number of valid bits */
    gb->bits = remaining;

    /* save off the remaining bits */
    movd_r2m(mm2,result);
    /* discard those bits in mm0 */
    psllq_r2r(mm1,mm0);

    /* save off mm0 */
    movq_r2m(mm0,gb->qword);
    /* finished with MMX */
    emms();

    /* we have what we came for */
    return(result);
  }
}
#endif /* HAVE_LIBMMX */

unsigned long getbits_int(gst_getbits_t *gb,unsigned long bits) {
  int remaining;
  int result = 0;

  if (bits == 0) {
//    fprintf(stderr,"getbits(0) = 0\n");
    return 0;
  }

  remaining = gb->bits - bits;
  if (remaining < 0) {
    /* how many bits are left to get? */
    remaining = -remaining;
//    printf("have to get %d more bits from next dword\n",remaining);
    /* move what's left over to accomodate the new stuff */
    result = gb->dword >> (32 - bits);
//    printf("have \t\t%s from previous dword\n",print_bits(result));
    /* get the new word into the buffer */
//    fprintf(stderr,"getbits: incrementing %p by 4\n",gb->ptr);
    gb->ptr += 4;
    gb->dword = bswap_32(*((unsigned long *)(gb->ptr)));
//    gb->dword = *((unsigned char *)(gb->ptr)) << 24 |
//                *((unsigned char *)(gb->ptr)+1) << 16 |
//                *((unsigned char *)(gb->ptr)+2) << 8 |
//                *((unsigned char *)(gb->ptr)+3);
//    gb->dword = *((unsigned long *)(gb->ptr));
    gb->bits = 32;
//    fprintf(stderr,"got new dword %08x\n",gb->dword);
    /* & in the right number of bits */
    result |= (gb->dword >> (32 - remaining));
    /* shift the buffer over */
    gb->dword <<= remaining;
    /* record how many bits are left */
    gb->bits -= remaining;
  } else {
    result = gb->dword >> (32 - bits);
    gb->dword <<= bits;
    gb->bits -= bits;
//    printf("have %d bits left\n",gb->bits);
  }
//  printf("have \t\t%s left\n",print_bits(gb->dword));
//  fprintf(stderr,"getbits.c:getbits(%ld) = %d\n",bits,result);
  return result;
}

void getbits_back_int(gst_getbits_t *gb,unsigned long bits) {
  fprintf(stderr,"getbits.c:getbits_back(%lu)\n",bits);
  // moving within the same dword...
  if ((bits + gb->bits) <= 32) {
    fprintf(stderr,"moving back %lu bits in the same dword\n",bits);
    gb->bits += bits;
  // otherwise we're going to have move the pointer (ick)
  } else {
    // rare case where we're moving a multiple of 32 bits...
    if ((bits % 32) == 0) {
      fprintf(stderr,"moving back exactly %lu dwords\n",bits/32);
      gb->ptr -= (bits / 8);
    // we have to both shift bits and the pointer (NOOOOOOO!)
    } else {
      // strip off the first dword
      bits -= (32 - gb->bits);
      gb->ptr -= 4;
      // now strip off as many others as necessary
      gb->ptr -= 4 * (bits/32);
      // and set the bits to what's left
      gb->bits = bits % 32;
      fprintf(stderr,"moved back %lu bytes to %p\n",4 * ((bits/32)+1),gb->ptr);
    }
  }
  gb->dword = bswap_32(*((unsigned long *)(gb->ptr)));
  fprintf(stderr,"orignal new loaded word is %08lx\n",gb->dword);
  gb->dword <<= (32 - gb->bits);
  fprintf(stderr,"shifted (by %lu) word is %08lx\n",gb->bits,gb->dword);
}

void getbits_byteback_int(gst_getbits_t *gb,unsigned long bytes) {
  fprintf(stderr,"getbits.c:getbits_byteback(%lu)\n",bytes);
  getbits_back_int(gb,bytes*8);
  gb->bits = gb->bits & ~0x07;
}

int getbits_offset(gst_getbits_t *gb) {
  fprintf(stderr,"getbits.c:getbits_offset() = %lu\n",gb->bits % 8);
  return gb->bits % 8;
}

/* initialize the getbits structure with the proper getbits func */
void getbits_init(gst_getbits_t *gb) {
  gb->ptr = NULL;
  gb->bits = 0;

#ifdef HAVE_LIBMMX
  if (1) {
    gb->getbits = getbits_mmx;
//    gb->backbits = getbits_back_mmx;
//    gb->backbytes = getbits_byteback_mmx;
  } else
#endif /* HAVE_LIBMMX */
  {
    gb->getbits = getbits_int;
    gb->backbits = getbits_back_int;
    gb->backbytes = getbits_byteback_int;
  }
}

/* set up the getbits structure with a new buffer */
void getbits_newbuf(gst_getbits_t *gb,unsigned char *buffer) {
  gb->ptr = buffer - 4;
//  fprintf(stderr,"setting ptr to %p\n",gb->ptr);
  gb->bits = 0;
  gb->dword = 0;
#ifdef HAVE_LIBMMX
//  gb->qword = 0;
#endif /* HAVE_LIBMMX */
}
