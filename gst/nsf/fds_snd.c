/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** fds_snd.c
**
** Famicom Disk System sound emulation
** $Id$
*/

#include <stdio.h>
#include "types.h"
#include "nes_apu.h"
#include "fds_snd.h"

static int32 fds_incsize = 0;

/* mix sound channels together */
static int32
fds_process (void)
{
  int32 output;

  output = 0;

  return output;
}

/* write to registers */
static void
fds_write (uint32 address, uint8 value)
{
}

/* reset state of vrcvi sound channels */
static void
fds_reset (void)
{
  fds_incsize = apu_getcyclerate ();
}

static void
fds_init (void)
{
}

/* TODO: bleh */
static void
fds_shutdown (void)
{
}

static apu_memwrite fds_memwrite[] = {
  {0x4040, 0x4092, fds_write},
  {(uint32) - 1, (uint32) - 1, NULL}
};

apuext_t fds_ext = {
  fds_init,
  fds_shutdown,
  fds_reset,
  fds_process,
  NULL,                         /* no reads */
  fds_memwrite
};

/*
** $Log$
** Revision 1.4  2008/03/26 07:40:55  slomo
** * gst/nsf/Makefile.am:
** * gst/nsf/fds_snd.c:
** * gst/nsf/mmc5_snd.c:
** * gst/nsf/nsf.c:
** * gst/nsf/types.h:
** * gst/nsf/vrc7_snd.c:
** * gst/nsf/vrcvisnd.c:
** * gst/nsf/memguard.c:
** * gst/nsf/memguard.h:
** Remove memguard again and apply hopefully all previously dropped
** local patches. Should be really better than the old version now.
**
** Revision 1.3  2008-03-25 15:56:10  slomo
** Patch by: Andreas Henriksson <andreas at fatal dot set>
** * gst/nsf/Makefile.am:
** * gst/nsf/dis6502.h:
** * gst/nsf/fds_snd.c:
** * gst/nsf/fds_snd.h:
** * gst/nsf/fmopl.c:
** * gst/nsf/fmopl.h:
** * gst/nsf/gstnsf.c:
** * gst/nsf/log.c:
** * gst/nsf/log.h:
** * gst/nsf/memguard.c:
** * gst/nsf/memguard.h:
** * gst/nsf/mmc5_snd.c:
** * gst/nsf/mmc5_snd.h:
** * gst/nsf/nes6502.c:
** * gst/nsf/nes6502.h:
** * gst/nsf/nes_apu.c:
** * gst/nsf/nes_apu.h:
** * gst/nsf/nsf.c:
** * gst/nsf/nsf.h:
** * gst/nsf/osd.h:
** * gst/nsf/types.h:
** * gst/nsf/vrc7_snd.c:
** * gst/nsf/vrc7_snd.h:
** * gst/nsf/vrcvisnd.c:
** * gst/nsf/vrcvisnd.h:
** Update our internal nosefart to nosefart-2.7-mls to fix segfaults
** on some files. Fixes bug #498237.
** Remove some // comments, fix some compiler warnings and use pow()
** instead of a slow, selfmade implementation.
**
** Revision 1.1  2003/04/08 20:53:00  ben
** Adding more files...
**
** Revision 1.3  2000/07/03 02:18:53  matt
** much better external module exporting
**
** Revision 1.2  2000/06/20 04:06:16  matt
** migrated external sound definition to apu module
**
** Revision 1.1  2000/06/20 00:06:47  matt
** initial revision
**
*/
