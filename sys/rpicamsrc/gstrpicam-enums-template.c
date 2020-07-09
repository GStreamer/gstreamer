/*** BEGIN file-header ***/
#include "gstrpicam-enum-types.h"

/*** END file-header ***/

/*** BEGIN file-production ***/
/* enumerations from "@basename@" */
#include "@filename@"

#define C_ENUM(v) ((gint) v)
#define C_FLAGS(v) ((guint) v)

/*** END file-production ***/

/*** BEGIN value-header ***/
GType
@enum_name@_get_type (void)
{
  static volatile gsize gtype_id = 0;

  if (g_once_init_enter (&gtype_id)) {
    static const G@Type@Value values[] = {
/*** END value-header ***/

/*** BEGIN value-production ***/
        { C_@TYPE@(@VALUENAME@), "@VALUENAME@", "@valuenick@" },
/*** END value-production ***/

/*** BEGIN value-tail ***/
        { 0, NULL, NULL }
    };

    GType new_type = g_@type@_register_static (g_intern_static_string ("@EnumName@"), values);
    g_once_init_leave (&gtype_id, new_type);
  }
  return (GType) gtype_id;
}

/*** END value-tail ***/

