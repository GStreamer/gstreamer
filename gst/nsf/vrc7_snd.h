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
** vrc7_snd.h
**
** VRCVII (Konami MMC) sound hardware emulation header
** Thanks to Charles MacDonald (cgfm2@hooked.net) for donating code.
**
** $Id$
*/

#ifndef _VRC7_SND_H_
#define _VRC7_SND_H_

#include "fmopl.h"

/* VRC7 context */
typedef struct vrc7_s
{
   uint8 reg[0x40];        /* 64 registers */
   uint8 latch;            /* Register latch */
   uint8 user[0x10];       /* User instrument settings */
   struct
   {
      uint16 frequency;    /* Channel frequency */
      uint8 volume;        /* Channel volume */
      uint8 instrument;    /* Channel instrument */
   } channel[9];

   FM_OPL *ym3812;
} vrc7_t;


#include "nes_apu.h"

extern apuext_t vrc7_ext;

#endif /* !_VRC7_SND_H_ */

/*
** $Log$
** Revision 1.2  2008/03/25 15:56:12  slomo
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
** Revision 1.1  2003/04/08 20:53:01  ben
** Adding more files...
**
** Revision 1.3  2000/07/04 04:51:02  matt
** made data types stricter
**
** Revision 1.2  2000/06/20 04:06:16  matt
** migrated external sound definition to apu module
**
** Revision 1.1  2000/06/20 00:06:47  matt
** initial revision
**
*/

