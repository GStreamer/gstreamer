/* putbits.c, bit-level output                                              */

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

#include <stdlib.h>
#include <stdio.h>
#include <gst/putbits/putbits.h>

/* initialize buffer, call once before first putbits or alignbits */
void gst_putbits_init(gst_putbits_t *pb)
{
  pb->outcnt = 8;
  pb->bytecnt = 0;
  pb->outbase = 0;
}

void gst_putbits_new_empty_buffer(gst_putbits_t *pb, int len)
{
  pb->outbfr = pb->outbase = malloc(len);
  pb->temp = 0;
  pb->len = len;
  pb->newlen = 0;
  pb->outcnt = 8;
}

void gst_putbits_new_buffer(gst_putbits_t *pb, unsigned char *buffer, int len)
{
  pb->outbfr = buffer;
  pb->temp = 0;
  pb->outcnt = 8;
  pb->bytecnt = 0;
  pb->len = len;
}

/* write rightmost n (0<=n<=32) bits of val to outfile */
void gst_putbits(gst_putbits_t *pb, int val, int n)
{
  int i;
  unsigned int mask;

  //printf("putbits: %p %08x %d %d %d\n", pb, val, n, pb->outcnt, pb->newlen);
  mask = 1 << (n-1); /* selects first (leftmost) bit */

  for (i=0; i<n; i++)
  {
    pb->temp <<= 1;

    if (val & mask)
      pb->temp|= 1;

    mask >>= 1; /* select next bit */
    pb->outcnt--;

    if (pb->outcnt==0) /* 8 bit buffer full */
    {
      pb->len--;
      pb->newlen++;
      *(pb->outbfr++) = pb->temp;
      pb->outcnt = 8;
      pb->bytecnt++;
    }
  }
}

/* zero bit stuffing to next byte boundary (5.2.3, 6.2.1) */
void gst_putbits_align(gst_putbits_t *pb)
{
  if (pb->outcnt!=8)
    gst_putbits(pb, 0, pb->outcnt);
}

/* return total number of generated bits */
int gst_putbits_bitcount(gst_putbits_t *pb)
{
  return 8*pb->bytecnt + (8-pb->outcnt);
}
