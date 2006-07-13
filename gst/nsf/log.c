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
** log.c
**
** Error logging functions
** $Id$
*/

#include <stdio.h>
#include <stdarg.h>
#include "types.h"
#include "log.h"


#ifdef OSD_LOG
#include "osd.h"
#endif

#if defined(OSD_LOG) && !defined(NOFRENDO_DEBUG)
#error NOFRENDO_DEBUG must be defined as well as OSD_LOG
#endif

/* Note that all of these functions will be empty if
** debugging is not enabled.
*/
#ifdef NOFRENDO_DEBUG
static FILE *errorlog;
#endif

int
log_init (void)
{
#ifdef NOFRENDO_DEBUG
#ifdef OSD_LOG
  /* Initialize an OSD logging system */
  osd_loginit ();
#endif /* OSD_LOG */
  errorlog = fopen ("errorlog.txt", "wt");
  if (NULL == errorlog)
    return (-1);
#endif /* NOFRENDO_DEBUG */
  return 0;
}

void
log_shutdown (void)
{
#ifdef NOFRENDO_DEBUG
  /* Snoop around for unallocated blocks */
  mem_checkblocks ();
  mem_checkleaks ();
#ifdef OSD_LOG
  osd_logshutdown ();
#endif /* OSD_LOG */
  fclose (errorlog);
#endif /* NOFRENDO_DEBUG */
}

void
log_print (const char *string)
{
#ifdef NOFRENDO_DEBUG
#ifdef OSD_LOG
  osd_logprint (string);
#endif /* OSD_LOG */
  /* Log it to disk, as well */
  fputs (string, errorlog);
#endif /* NOFRENDO_DEBUG */
}

void
log_printf (const char *format, ...)
{
#ifdef NOFRENDO_DEBUG
#ifdef OSD_LOG
  char buffer[1024 + 1];
#endif /* OSD_LOG */
  va_list arg;

  va_start (arg, format);

#ifdef OSD_LOG
  vsprintf (buffer, format, arg);
  osd_logprint (buffer);
#endif /* OSD_LOG */
  vfprintf (errorlog, format, arg);
  va_end (arg);
#endif /* NOFRENDO_DEBUG */
}

/*
** $Log$
** Revision 1.1  2006/07/13 15:07:28  wtay
** Based on patches by: Johan Dahlin <johan at gnome dot org>
** Ronald Bultje <rbultje at ronald dot bitfreak dot net>
** * configure.ac:
** * gst/nsf/Makefile.am:
** * gst/nsf/dis6502.h:
** * gst/nsf/fds_snd.c:
** * gst/nsf/fds_snd.h:
** * gst/nsf/fmopl.c:
** * gst/nsf/fmopl.h:
** * gst/nsf/gstnsf.c:
** * gst/nsf/gstnsf.h:
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
** Added NSF decoder plugin. Fixes 151192.
**
** Revision 1.5  2000/06/26 04:55:33  matt
** minor change
**
** Revision 1.4  2000/06/09 15:12:25  matt
** initial revision
**
*/
