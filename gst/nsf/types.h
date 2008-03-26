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
** types.h
**
** Data type definitions
** $Id$
*/

#ifndef _NSF_TYPES_H_
#define _NSF_TYPES_H_

#include <glib.h> /* for types, endianness */

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
/* Define this if running on little-endian (x86) systems */
#define HOST_LITTLE_ENDIAN
#else
#undef HOST_LITTLE_ENDIAN
#endif

#ifdef __GNUC__
#define  INLINE      static inline
#elif defined(WIN32)
#define  INLINE      static __inline
#else /* crapintosh? */
#define  INLINE      static
#endif

/* These should be changed depending on the platform */

typedef  gint8     int8;
typedef  gint16    int16;
typedef  gint32      int32;

typedef  guint8  uint8;
typedef  guint16 uint16;
typedef  guint32 uint32;
typedef  guint8  boolean;

#ifndef  TRUE
#define  TRUE     1
#endif
#ifndef  FALSE
#define  FALSE    0
#endif

#ifndef  NULL
#define  NULL     ((void *) 0)
#endif

#ifdef NOFRENDO_DEBUG
#include <stdlib.h>
/* #include "memguard.h" */
#include "log.h"
#define  ASSERT(expr)      if (FALSE == (expr))\
                           {\
                             log_printf("ASSERT: line %d of %s\n", __LINE__, __FILE__);\
                             log_shutdown();\
                             exit(1);\
                           }
#define  ASSERT_MSG(msg)   {\
                             log_printf("ASSERT: %s\n", msg);\
                             log_shutdown();\
                             exit(1);\
                           }
#else /* Not debugging */
#include <stdlib.h>
#define  ASSERT(expr)
#define  ASSERT_MSG(msg)
#endif

#endif /* _NSF_TYPES_H_ */

/*
** $Log$
** Revision 1.6  2008/03/26 07:40:55  slomo
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
** Revision 1.5  2008-03-25 16:58:53  wtay
** * gst/nsf/memguard.c: (_my_free):
** * gst/nsf/types.h:
** Unbreak compilation by disabling memguard and doing some dirty hack
** fixes to make it compile on 64bits.
**
** Revision 1.4  2008-03-25 15:56:12  slomo
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
** Revision 1.7  2000/07/04 04:46:44  matt
** moved INLINE define from osd.h
**
** Revision 1.6  2000/06/09 15:12:25  matt
** initial revision
**
*/
