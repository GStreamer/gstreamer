
/* Generated data (by glib-mkenums) */

#include "gstudp-enumtypes.h"

#include "gstudp.h"

/* enumerations from "gstudp.h" */
GType
gst_udp_control_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {CONTROL_ZERO, "CONTROL_ZERO", "zero"},
      {CONTROL_NONE, "CONTROL_NONE", "none"},
      {CONTROL_UDP, "CONTROL_UDP", "udp"},
      {CONTROL_TCP, "CONTROL_TCP", "tcp"},
      {0, NULL, NULL}
    };
    GType g_define_type_id = g_enum_register_static ("GstUDPControl", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

/* Generated data ends here */
