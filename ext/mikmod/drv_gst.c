#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "gstmikmod.h"

static int buffer_size;
static SBYTE *audiobuffer = NULL;
extern int need_sync;

static BOOL
mikmod_IsThere (void)
{
  return 1;
}

static BOOL
mikmod_Init (void)
{
  buffer_size = 32768;
  if (!(audiobuffer = (SBYTE *) g_malloc (buffer_size)))
    return 1;

  return VC_Init ();
}

static void
mikmod_Exit (void)
{
  VC_Exit ();

  if (audiobuffer) {
    g_free (audiobuffer);
    audiobuffer = NULL;
  }
}


static void
mikmod_Update (void)
{
  gint length;
  GstBuffer *outdata;

  length = VC_WriteBytes ((SBYTE *) audiobuffer, buffer_size);

  outdata = gst_buffer_new ();

  GST_BUFFER_DATA (outdata) = g_memdup (audiobuffer, length);
  GST_BUFFER_SIZE (outdata) = length;

  GST_BUFFER_TIMESTAMP (outdata) = timestamp;

  if (need_sync == 1) {
    /* FIXME, send a flush event or something */
    need_sync = 0;
  }
  gst_pad_push (srcpad, GST_DATA (outdata));

}

static BOOL
mikmod_Reset (void)
{
  VC_Exit ();
  return VC_Init ();
}


MDRIVER drv_gst = {
  NULL,
  "mikmod",
  "mikmod output driver v1.0",
  0, 255,
#if (LIBMIKMOD_VERSION > 0x030106)
  "mikmod",
  NULL,
#endif
  mikmod_IsThere,
  VC_SampleLoad,
  VC_SampleUnload,
  VC_SampleSpace,
  VC_SampleLength,
  mikmod_Init,
  mikmod_Exit,
  mikmod_Reset,
  VC_SetNumVoices,
  VC_PlayStart,
  VC_PlayStop,
  mikmod_Update,
  NULL,
  VC_VoiceSetVolume,
  VC_VoiceGetVolume,
  VC_VoiceSetFrequency,
  VC_VoiceGetFrequency,
  VC_VoiceSetPanning,
  VC_VoiceGetPanning,
  VC_VoicePlay,
  VC_VoiceStop,
  VC_VoiceStopped,
  VC_VoiceGetPosition,
  VC_VoiceRealVolume
};
