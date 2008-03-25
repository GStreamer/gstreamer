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
** memguard.h
**
** memory allocation wrapper routines
** $Id$
*/

#ifndef  _MEMGUARD_H_
#define  _MEMGUARD_H_

#ifdef NOFRENDO_DEBUG

#define  malloc(s)   _my_malloc((s), __FILE__, __LINE__)
#define  free(d)     _my_free((void **) &(d), __FILE__, __LINE__)

extern void *_my_malloc(int size, char *file, int line);
extern void _my_free(void **data, char *file, int line);

#else /* Non-debugging versions of calls */

#define  malloc(s)   _my_malloc((s))
#define  free(d)     _my_free((void **) &(d))

extern void *_my_malloc(int size);
extern void _my_free(void **data);

#endif /* NOFRENDO_DEBUG */


extern void mem_checkblocks(void);
extern void mem_checkleaks(void);

extern boolean mem_debug;

#endif   /* _MEMGUARD_H_ */

/*
** $Log$
** Revision 1.3  2008/03/25 15:56:11  slomo
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
** Revision 1.1  2003/04/08 20:46:46  ben
** add new input for NES music file.
**
** Revision 1.5  2000/06/26 04:54:48  matt
** simplified and made more robust
**
** Revision 1.4  2000/06/09 15:12:25  matt
** initial revision
**
*/
