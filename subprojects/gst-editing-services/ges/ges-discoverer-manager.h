#pragma once

#include <ges/ges-types.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GES_TYPE_DISCOVERER_MANAGER:
 *
 * Since: 1.24
 */
#define GES_TYPE_DISCOVERER_MANAGER ges_discoverer_manager_get_type ()

struct _GESDiscovererManagerClass
{
  GObjectClass parent_class;
};

GES_DECLARE_TYPE(DiscovererManager, discoverer_manager, DISCOVERER_MANAGER);

GES_API GstClockTime            ges_discoverer_manager_get_timeout    (GESDiscovererManager * self);
GES_API void                    ges_discoverer_manager_set_timeout    (GESDiscovererManager * self,
                                                                       GstClockTime timeout);
GES_API GESDiscovererManager *  ges_discoverer_manager_get_default    (void);
GES_API void                    ges_discoverer_manager_set_use_cache  (GESDiscovererManager *self,
                                                                       gboolean use_cache);
GES_API gboolean                ges_discoverer_manager_get_use_cache  (GESDiscovererManager *self);

G_END_DECLS
