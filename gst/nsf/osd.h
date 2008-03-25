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
** osd.h
**
** O/S dependent routine defintions (must be customized)
** $Id$
*/

#ifndef _OSD_H_
#define _OSD_H_


#ifdef __GNUC__
#define  __PACKED__  __attribute__ ((packed))
#define  PATH_SEP    '/'
#ifdef __DJGPP__
#include <dpmi.h>
#include "dos_ints.h"
#endif
#elif defined(WIN32)
#define  __PACKED__
#define  PATH_SEP    '\\'
#else /* crapintosh? */
#define  __PACKED__
#define  PATH_SEP    ':'
#endif

extern void osd_loginit(void);
extern void osd_logshutdown(void);
extern void osd_logprint(const char *string);

extern int osd_startsound(void (*playfunc)(void *buffer, int size));
extern int osd_getsoundbps(void);
extern int osd_getsamplerate(void);


#ifndef NSF_PLAYER
#include "rgb.h"
#include "bitmap.h"

extern bitmap_t *osd_getvidbuf(void);
typedef void (*blitproc_t)(bitmap_t *bmp, int x_pos, int y_pos, int width, int height);
extern blitproc_t osd_blit;
extern void osd_copytoscreen(void);

extern void osd_showusage(char *filename);
extern void osd_fullname(char *fullname, const char *shortname);
extern char *osd_newextension(char *string, char *ext);

extern void osd_setpalette(rgb_t *pal);
extern void osd_restorepalette(void);

extern void osd_getinput(void);
extern int osd_gethostinput(void);
extern void osd_getmouse(int *x, int *y, int *button);

extern int osd_init(void);
extern void osd_shutdown(void);
#endif /* !NSF_PLAYER */

#endif /* _OSD_H_ */

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
** Revision 1.1  2003/04/08 20:46:46  ben
** add new input for NES music file.
**
** Revision 1.7  2000/07/04 04:45:33  matt
** moved INLINE define into types.h
**
** Revision 1.6  2000/06/29 16:06:18  neil
** Wrapped DOS-specific headers in an ifdef
**
** Revision 1.5  2000/06/09 15:12:25  matt
** initial revision
**
*/

