#ifndef GST_HGUARD_GSTARCH_H
#define GST_HGUARD_GSTARCH_H

#include "config.h"

#ifdef HAVE_CPU_I386
#include "gsti386.h"
#else
#ifdef HAVE_CPU_PPC
#include "gstppc.h"
#else
#warn Need to know about this architecture, or have a generic implementation
#endif
#endif

#endif /* GST_HGUARD_GSTARCH_H */
