#ifndef _MONOSCOPE_H
#define _MONOSCOPE_H

#include <glib.h>

void monoscope_init (guint32 resx, guint32 resy);
guint32 * monoscope_update (gint16 data [2][512]);
void monoscope_close ();

#endif
