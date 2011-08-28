#ifndef _GES_PITIVI_FORMATTER
#define _GES_PITIVI_FORMATTER
#include <libxml/xmlreader.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include "libxml/encoding.h"
#include "libxml/xmlwriter.h"


#define GES_TYPE_PITIVI_FORMATTER ges_pitivi_formatter_get_type()

#define GES_PITIVI_FORMATTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_PITIVI_FORMATTER, GESPitiviFormatter))

#define GES_PITIVI_FORMATTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_PITIVI_FORMATTER, GESPitiviFormatterClass))

#define GES_IS_PITIVI_FORMATTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_PITIVI_FORMATTER))

#define GES_IS_PITIVI_FORMATTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_PITIVI_FORMATTER))

#define GES_PITIVI_FORMATTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_PITIVI_FORMATTER, GESPitiviFormatterClass))

typedef struct _GESPitiviFormatterPrivate GESPitiviFormatterPrivate;

/**
 * GESPitiviFormatter:
 *
 * Serializes a #GESTimeline to a file using
 */

struct _GESPitiviFormatter {
  GESFormatter parent;

  /*< private >*/
  GESPitiviFormatterPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESPitiviFormatterClass {
  /*< private >*/
  GESFormatterClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_pitivi_formatter_get_type (void);

GESPitiviFormatter *ges_pitivi_formatter_new (void);



#endif /* _GES_PITIVI_FORMATTER */
