/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "cmapi.h"

#include "dynapi-internal.h"

#include <gmodule.h>

#define CM_FRAMEWORK_PATH "/System/Library/Frameworks/" \
    "CoreMedia.framework/CoreMedia"
#define CM_FRAMEWORK_PATH_OLD "/System/Library/PrivateFrameworks/" \
    "CoreMedia.framework/CoreMedia"

G_DEFINE_TYPE (GstCMApi, gst_cm_api, GST_TYPE_DYN_API);

static void
gst_cm_api_init (GstCMApi * self)
{
}

static void
gst_cm_api_class_init (GstCMApiClass * klass)
{
}

#define SYM_SPEC(name) GST_DYN_SYM_SPEC (GstCMApi, name)

GstCMApi *
gst_cm_api_obtain (GError ** error)
{
  static const GstDynSymSpec symbols[] = {
    SYM_SPEC (FigBaseObjectGetVTable),

    SYM_SPEC (CMGetAttachment),

    SYM_SPEC (FigFormatDescriptionRelease),
    SYM_SPEC (FigFormatDescriptionRetain),
    SYM_SPEC (CMFormatDescriptionEqual),
    SYM_SPEC (CMFormatDescriptionGetExtension),
    SYM_SPEC (CMFormatDescriptionGetMediaType),
    SYM_SPEC (CMFormatDescriptionGetMediaSubType),

    SYM_SPEC (CMVideoFormatDescriptionCreate),
    SYM_SPEC
        (FigVideoFormatDescriptionCreateWithSampleDescriptionExtensionAtom),
    SYM_SPEC (CMVideoFormatDescriptionGetDimensions),

    SYM_SPEC (CMTimeMake),

    SYM_SPEC (CMSampleBufferCreate),
    SYM_SPEC (CMSampleBufferDataIsReady),
    SYM_SPEC (CMSampleBufferGetDataBuffer),
    SYM_SPEC (CMSampleBufferGetFormatDescription),
    SYM_SPEC (CMSampleBufferGetImageBuffer),
    SYM_SPEC (CMSampleBufferGetNumSamples),
    SYM_SPEC (CMSampleBufferGetSampleAttachmentsArray),
    SYM_SPEC (CMSampleBufferGetSampleSize),
    SYM_SPEC (FigSampleBufferRelease),
    SYM_SPEC (FigSampleBufferRetain),

    SYM_SPEC (CMBlockBufferCreateWithMemoryBlock),
    SYM_SPEC (CMBlockBufferGetDataLength),
    SYM_SPEC (CMBlockBufferGetDataPointer),
    SYM_SPEC (FigBlockBufferRelease),
    SYM_SPEC (FigBlockBufferRetain),

    SYM_SPEC (CMBufferQueueDequeueAndRetain),
    SYM_SPEC (CMBufferQueueGetBufferCount),
    SYM_SPEC (CMBufferQueueInstallTrigger),
    SYM_SPEC (CMBufferQueueIsEmpty),
    SYM_SPEC (FigBufferQueueRelease),
    SYM_SPEC (CMBufferQueueRemoveTrigger),
    SYM_SPEC (CMBufferQueueSetValidationCallback),

    SYM_SPEC (kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms),
    SYM_SPEC (kCMSampleAttachmentKey_DependsOnOthers),
    SYM_SPEC (kCMTimeInvalid),

    {NULL, 0},
  };
  GstCMApi *result;
  GModule *module;

  /* We cannot stat() the library as it may not be present on the filesystem.
   * This is the case on newer versions of iOS where system libraries are all
   * part of dyld_shared_cache... */
  module = g_module_open (CM_FRAMEWORK_PATH, 0);
  if (module != NULL) {
    result = _gst_dyn_api_new (gst_cm_api_get_type (), CM_FRAMEWORK_PATH,
        symbols, error);
    g_module_close (module);
  } else {
    GstDynSymSpec *old_symbols;
    guint i;

    old_symbols = g_memdup (symbols, sizeof (symbols));
    for (i = 0; old_symbols[i].name != NULL; i++) {
      const gchar *name = old_symbols[i].name;
      const gchar *translated_name;

      if (g_str_has_prefix (name, "CM"))
        translated_name = g_strconcat ("Fig", name + 2, NULL);
      else if (g_str_has_prefix (name, "kCM"))
        translated_name = g_strconcat ("kFig", name + 3, NULL);
      else
        translated_name = g_strdup (name);

      old_symbols[i].name = translated_name;
    }

    result = _gst_dyn_api_new (gst_cm_api_get_type (), CM_FRAMEWORK_PATH_OLD,
        old_symbols, error);

    for (i = 0; old_symbols[i].name != NULL; i++)
      g_free ((gpointer) old_symbols[i].name);
    g_free (old_symbols);
  }

  return result;
}
