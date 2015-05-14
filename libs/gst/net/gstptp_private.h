#ifndef __GST_PTP_PRIVATE_H__
#define __GST_PTP_PRIVATE_H__

#include <glib.h>

enum
{
  TYPE_EVENT,
  TYPE_GENERAL,
  TYPE_CLOCK_ID
};

typedef struct
{
  guint16 size;
  guint8 type;
} StdIOHeader;

#endif /* __GST_PTP_PRIVATE_H__ */
