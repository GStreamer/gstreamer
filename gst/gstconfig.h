/* This header interprets the various GST_* macros that are typically	*
 * provided by the gstreamer-config or gstreamer.pc files.		*/

#ifndef __GST_CONFIG_H__
#define __GST_CONFIG_H__


/***** We include config.h in case someone perhaps used a gstreamer.m4 or
       something else that provides funky overrides.  BEWARE! *****/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */


/***** Deal with XML stuff, we have to handle both loadsave and registry *****/

#if (! (defined(GST_DISABLE_LOADSAVE) && defined(GST_DISABLE_REGISTRY)) )
# include <libxml/parser.h>
#else
# define GST_DISABLE_LOADSAVE_REGISTRY
#endif

#endif /* __GST_CONFIG_H__ */
