#include "config.h"

#include "getbits.h"

/* Defined in gstgetbits_i386.s */
extern unsigned long _gst_get1bit_i386(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_getbits_i386(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_getbits_fast_i386(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_showbits_i386(gst_getbits_t *gb, unsigned long bits);
extern void _gst_flushbits_i386(gst_getbits_t *gb, unsigned long bits);
extern void _gst_getbits_back_i386(gst_getbits_t *gb, unsigned long bits);

/* Defined in gstgetbits_generic.c */
extern unsigned long _gst_getbits_int_cb(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_get1bit_int(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_getbits_int(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_getbits_fast_int(gst_getbits_t *gb, unsigned long bits);
extern unsigned long _gst_showbits_int(gst_getbits_t *gb, unsigned long bits);
extern void _gst_flushbits_int(gst_getbits_t *gb, unsigned long bits);
extern void _gst_getbits_back_int(gst_getbits_t *gb, unsigned long bits);


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

#ifdef unused
unsigned long _getbits_64_minus_index[] = {
  64,63,62,61,60,59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,
  40,39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,
  16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1 };

/* this routine taken from Intel AppNote AP-527:
   "Using MMX[tm] Instructions to Get Bits From a Data Stream"
   written in C with libmmx to *closely* mimic Intel's ASM implementation

   this isn't as cycle-efficient, due to the simple fact that I must call
   emms() at the end.  all state must be kept in *gb, not in registers */
unsigned long _gst_getbits_mmx(gst_getbits_t *gb,unsigned long bits) {
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
    dword1 = swab32(*(gb->ptr-8));
    /* grab the second 32 bits, swap */
    dword2 = swab32(*(gb->ptr-4));

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

unsigned long _gst_getbyte(gst_getbits_t *gb, unsigned long bits) {
  return *gb->ptr++;
}

/* initialize the getbits structure with the proper getbits func */
void gst_getbits_init(gst_getbits_t *gb, GstGetbitsCallback callback, void *data) {
  gb->ptr = NULL;
  gb->bits = 0;
  gb->callback = callback;
  gb->data = data;

#ifdef unused
  if (1) {
    gb->getbits = _gst_getbits_mmx;
/*    gb->backbits = _gst_getbits_back_mmx; */
/*    gb->backbytes = _gst_getbits_byteback_mmx; */
/*    printf("gstgetbits: using MMX optimized versions\n"); */
  } else
#endif /* HAVE_LIBMMX */
  {
    if (gb->callback) {
      gb->getbits = _gst_getbits_int_cb;
      gb->showbits = _gst_showbits_int;
      gb->flushbits = _gst_flushbits_int;
      gb->backbits = _gst_getbits_back_int;
/*      printf("gstgetbits: using callback versions\n"); */
    }
    else {
#ifdef HAVE_CPU_I386
      gb->get1bit = _gst_get1bit_i386;
      gb->getbits = _gst_getbits_i386;
      gb->getbits_fast = _gst_getbits_fast_i386;
      gb->getbyte = _gst_getbyte;
      gb->show1bit = _gst_showbits_i386;
      gb->showbits = _gst_showbits_i386;
      gb->flushbits = _gst_flushbits_i386;
      gb->backbits = _gst_getbits_back_i386;
/*      printf("gstgetbits: using i386 optimized versions\n"); */
#else
      gb->get1bit = _gst_get1bit_int;
      gb->getbits = _gst_getbits_int;
      gb->getbits_fast = _gst_getbits_fast_int;
      gb->getbyte = _gst_getbyte;
      gb->show1bit = _gst_showbits_int;
      gb->showbits = _gst_showbits_int;
      gb->flushbits = _gst_flushbits_int;
      gb->backbits = _gst_getbits_back_int;
/*      printf("gstgetbits: using normal versions\n"); */
#endif
    }
  }
}

/* set up the getbits structure with a new buffer */
void gst_getbits_newbuf(gst_getbits_t *gb,unsigned char *buffer, unsigned long len) {
  gb->ptr = buffer;
  gb->endptr = buffer+len;
  gb->bits = 0;
#ifdef unused
/*  gb->qword = 0; */
#endif /* HAVE_LIBMMX */
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstgetbits",
  "Accelerated routines for getting bits from a data stream",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)

