#include <stdlib.h>
#include "gstgetbits.h"

char *
print_bits (unsigned long bits, int size)
{
  char *ret = (char *) malloc (size + 1);
  int i;

  ret[size] = 0;
  for (i = 0; i < size; i++) {
    if (bits & (1 << i))
      ret[(size - 1) - i] = '1';
    else
      ret[(size - 1) - i] = '0';
  }
  return ret;
}

static unsigned char testbuffer[] = {
  0x11, 0x22, 0x44, 0x88, 0xCC, 0xEE, 0xFF, 0x11
};

void
empty (gst_getbits_t * gb, void *data)
{
  printf ("buffer empty\n");

  gst_getbits_newbuf (gb, (unsigned char *) testbuffer, 7);
}

int
main (int argc, char *argv[])
{
  gst_getbits_t gb;
  int i, j;
  int bits;

  gst_getbits_init (&gb, NULL, NULL);
  gst_getbits_newbuf (&gb, (unsigned char *) testbuffer, 7);

  for (i = 0; i < 7; i++) {
    for (j = 0; j < 8; j++) {
      printf ("%lu", gst_getbits2 (&gb));
      gst_backbitsn (&gb, 1);
    }
    printf (" = %01x\n", testbuffer[i]);
  }

  gst_getbits_newbuf (&gb, (unsigned char *) testbuffer, 7);

  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000011 %lu\n", bits, gb.bits);
  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000022 %lu\n", bits, gb.bits);
  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000044 %lu\n", bits, gb.bits);
  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000088 %lu\n", bits, gb.bits);
  bits = gst_getbits6 (&gb);
  printf ("%08x <-> 00000033 %lu\n", bits, gb.bits);

  gst_backbitsn (&gb, 16);

  bits = gst_getbits10 (&gb);
  printf ("%08x <-> 00000088 \n", bits);

  gst_getbits_newbuf (&gb, (unsigned char *) testbuffer, 7);

  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000011 \n", bits);
  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000022 \n", bits);
  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000044 \n", bits);

  bits = gst_getbits6 (&gb);
  printf ("%08x <-> 00000022 \n", bits);

  gst_backbitsn (&gb, 19);

  bits = gst_getbits19 (&gb);
  printf ("%08x <-> 00009122 \n", bits);

  bits = gst_getbits10 (&gb);
  printf ("%08x <-> 000000cc \n", bits);

  gst_backbitsn (&gb, 8);

  gst_backbitsn (&gb, 19);

  gst_backbitsn (&gb, 8);

  bits = gst_getbits19 (&gb);
  printf ("%08x <-> 00012244 \n", bits);
  bits = gst_getbits8 (&gb);
  printf ("%08x <-> 00000088 \n", bits);

  return 0;
}
