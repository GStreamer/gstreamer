/*** BEGIN file-header ***/
#ifndef __GSTRPICAM_ENUM_TYPES_H__
#define __GSTRPICAM_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/*** END file-header ***/

/*** BEGIN file-production ***/
/* Enumerations from "@filename@" */

/*** END file-production ***/

/*** BEGIN enumeration-production ***/
#define GST_RPI_CAM_TYPE_@ENUMSHORT@	(@enum_name@_get_type())
GType @enum_name@_get_type	(void) G_GNUC_CONST;

/*** END enumeration-production ***/

/*** BEGIN file-tail ***/
G_END_DECLS

#endif /* __GSTRPICAM_ENUM_TYPES_H__ */
/*** END file-tail ***/
