#ifndef _SYNAESCOPE_H
#define _SYNAESCOPE_H

#include <glib.h>

void synaesthesia_init (guint32 resx, guint32 resy);
guint32 * synaesthesia_update (gint16 data [2][512]);
void synaesthesia_close ();

#endif
