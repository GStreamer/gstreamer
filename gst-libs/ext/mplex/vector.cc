#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mjpeg_types.h"
#include "vector.hh"


AUStream::AUStream ():
cur_rd (0), cur_wr (0), totalctr (0), size (0), buf (0)
{
  buf = new (Aunit *)[AUStream::BUF_SIZE];
}


void
AUStream::init (Aunit * rec)
{
  buf[cur_wr] = rec;
  ++cur_wr;
  cur_wr = cur_wr >= AUStream::BUF_SIZE ? 0 : cur_wr;
  cur_rd = cur_wr;
}
