#ifndef _MONOSCOPE_H
#define _MONOSCOPE_H

#include <glib.h>
#include "convolve.h"

#define scope_width 256
#define scope_height 128

struct monoscope_state
{
  gint16 copyEq[CONVOLVE_BIG];
  int avgEq[CONVOLVE_SMALL];	/* a running average of the last few. */
  int avgMax;			/* running average of max sample. */
  guint32 display[(scope_width + 1) * (scope_height + 1)];

  convolve_state *cstate;
  guint32 colors[64];
};

struct monoscope_state *monoscope_init (guint32 resx, guint32 resy);
guint32 *monoscope_update (struct monoscope_state *stateptr, gint16 data[512]);
void monoscope_close (struct monoscope_state *stateptr);

#endif
