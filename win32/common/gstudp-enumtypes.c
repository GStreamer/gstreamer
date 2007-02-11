
/* Generated data (by glib-mkenums) */

#include <gstudp.h>

/* enumerations from "gstudp.h" */
GType
gst_udp_control_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {CONTROL_ZERO, "CONTROL_ZERO", "zero"},
      {CONTROL_NONE, "CONTROL_NONE", "none"},
      {CONTROL_UDP, "CONTROL_UDP", "udp"},
      {CONTROL_TCP, "CONTROL_TCP", "tcp"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstUDPControl", values);
  }
  return etype;
}

/* Generated data ends here */
