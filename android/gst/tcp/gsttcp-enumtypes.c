
/* Generated data (by glib-mkenums) */

#include "gsttcp-enumtypes.h"

#include "gsttcp.h"

/* enumerations from "gsttcp.h" */
GType
gst_tcp_protocol_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_TCP_PROTOCOL_NONE, "GST_TCP_PROTOCOL_NONE", "none"},
      {GST_TCP_PROTOCOL_GDP, "GST_TCP_PROTOCOL_GDP", "gdp"},
      {0, NULL, NULL}
    };
    GType g_define_type_id = g_enum_register_static ("GstTCPProtocol", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

/* Generated data ends here */
