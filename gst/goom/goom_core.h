#ifndef _GOOMCORE_H
#define _GOOMCORE_H

#include <glib.h>

void goom_init (guint32 resx, guint32 resy);
void goom_set_resolution (guint32 resx, guint32 resy);

guint32 * goom_update (gint16 data [2][512]);

void goom_close ();

#endif
