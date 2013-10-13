/*** BEGIN file-header ***/
#include "gstrpicam-enum-types.h"

/*** END file-header ***/

/*** BEGIN file-production ***/
/* enumerations from "@filename@" */
#include "@filename@"

/*** END file-production ***/

/*** BEGIN value-header ***/
GType
@enum_name@_get_type (void)
{
	static GType the_type = 0;
	
	if (the_type == 0)
	{
		static const G@Type@Value values[] = {
/*** END value-header ***/

/*** BEGIN value-production ***/
			{ @VALUENAME@,
			  "@VALUENAME@",
			  "@valuenick@" },
/*** END value-production ***/

/*** BEGIN value-tail ***/
			{ 0, NULL, NULL }
		};
		the_type = g_@type@_register_static (
				g_intern_static_string ("@EnumName@"),
				values);
	}
	return the_type;
}

/*** END value-tail ***/

