/* putbits.h, bit-level output                                              */

/* Copyright (C) 1996, MPEG Software Simulation Group. All Rights Reserved. */

/*
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

#ifndef __GST_PUTBITS_H__
#define __GST_PUTBITS_H__

typedef struct _gst_putbits_t gst_putbits_t;

struct _gst_putbits_t {
  unsigned char *outbfr;
  unsigned char *outbase;
  unsigned char temp;
  int outcnt;
  int bytecnt;
  int len;
  int newlen;
};

void gst_putbits_init(gst_putbits_t *pb);
void gst_putbits_new_empty_buffer(gst_putbits_t *pb, int len);
void gst_putbits_new_buffer(gst_putbits_t *pb, unsigned char *buffer, int len);
void gst_putbits(gst_putbits_t *pb, int val, int n);
void gst_putbits_align(gst_putbits_t *pb);
int gst_putbits_bitcount(gst_putbits_t *pb);

#define gst_putbits1(gb, val) gst_putbits(gb, val, 1) 
#define gst_putbits2(gb, val) gst_putbits(gb, val, 2) 
#define gst_putbits3(gb, val) gst_putbits(gb, val, 3) 
#define gst_putbits4(gb, val) gst_putbits(gb, val, 4) 
#define gst_putbits5(gb, val) gst_putbits(gb, val, 5) 
#define gst_putbits6(gb, val) gst_putbits(gb, val, 6) 
#define gst_putbits7(gb, val) gst_putbits(gb, val, 7) 
#define gst_putbits8(gb, val) gst_putbits(gb, val, 8) 
#define gst_putbits9(gb, val) gst_putbits(gb, val, 9) 
#define gst_putbits10(gb, val) gst_putbits(gb, val, 10) 
#define gst_putbits11(gb, val) gst_putbits(gb, val, 11) 
#define gst_putbits12(gb, val) gst_putbits(gb, val, 12) 
#define gst_putbits13(gb, val) gst_putbits(gb, val, 13) 
#define gst_putbits14(gb, val) gst_putbits(gb, val, 14) 
#define gst_putbits15(gb, val) gst_putbits(gb, val, 15) 
#define gst_putbits16(gb, val) gst_putbits(gb, val, 16) 
#define gst_putbits17(gb, val) gst_putbits(gb, val, 17) 
#define gst_putbits18(gb, val) gst_putbits(gb, val, 18) 
#define gst_putbits19(gb, val) gst_putbits(gb, val, 19) 
#define gst_putbits20(gb, val) gst_putbits(gb, val, 20) 
#define gst_putbits21(gb, val) gst_putbits(gb, val, 21) 
#define gst_putbits22(gb, val) gst_putbits(gb, val, 22) 
#define gst_putbits32(gb, val) gst_putbits(gb, val, 32) 

#define gst_putbitsn(gb, val, n) gst_putbits(gb, val, n) 

#endif /* __GST_PUTBITS_H__ */
