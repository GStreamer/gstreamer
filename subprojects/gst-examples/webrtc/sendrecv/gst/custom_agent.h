#ifndef __CUSTOM_AGENT_H__
#define __CUSTOM_AGENT_H__

#include <gst/webrtc/ice.h>

G_BEGIN_DECLS

#define CUSTOMICE_TYPE_AGENT  (customice_agent_get_type ())
G_DECLARE_FINAL_TYPE (CustomICEAgent, customice_agent, CUSTOMICE, AGENT, GstWebRTCICE)

CustomICEAgent *             customice_agent_new                      (const gchar * name);

G_END_DECLS

#endif /* __CUSTOM_AGENT_H__ */