#ifndef _GST_ALAW_CONVERSION_H
#define _GST_ALAW_CONVERSION_H

#include <glib.h>

void
isdn_audio_ulaw2alaw(guint8 *buff, gulong len);

void
isdn_audio_alaw2ulaw(guint8 *buff, gulong len);

#endif
