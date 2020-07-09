/*** BEGIN file-header ***/
#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

/*** END file-header ***/

/*** BEGIN file-production ***/
/* Enumerations from "@basename@" */

/*** END file-production ***/

/*** BEGIN enumeration-production ***/
#define @ENUMPREFIX@_TYPE_@ENUMSHORT@ (@enum_name@_get_type())
GType @enum_name@_get_type (void);

/*** END enumeration-production ***/

/*** BEGIN file-tail ***/
G_END_DECLS

/*** END file-tail ***/
