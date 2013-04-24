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

#ifndef __GST_MIO_API_H__
#define __GST_MIO_API_H__

#include "cmapi.h"

#include <CoreFoundation/CoreFoundation.h>

G_BEGIN_DECLS

typedef struct _GstMIOApi GstMIOApi;
typedef struct _GstMIOApiClass GstMIOApiClass;

#define TUNDRA_SYSTEM_OBJECT_ID 1

typedef int TundraObjectID;
typedef int TundraDeviceID;
typedef int TundraUnitID;

typedef enum _TundraStatus TundraStatus;
typedef enum _TundraVendor TundraVendor;
typedef enum _TundraScope TundraScope;
typedef enum _TundraUnit TundraUnit;
typedef enum _TundraProperty TundraProperty;

typedef enum _TundraDeviceTransportType TundraDeviceTransportType;

typedef struct _TundraTargetSpec TundraTargetSpec;
typedef struct _TundraFramerate TundraFramerate;

typedef struct _TundraGraph TundraGraph;
typedef struct _TundraNode TundraNode;

typedef struct _TundraOutputDelegate TundraOutputDelegate;

enum _TundraStatus
{
  kTundraSuccess = 0,
  kTundraNotSupported = -67456
};

enum _TundraVendor
{
  kTundraVendorApple = 'appl'
};

enum _TundraScope
{
  kTundraScopeGlobal = 'glob',
  kTundraScopeDAL    = 'dal ',
  kTundraScope2PRC   = '2prc', /* TODO: Investigate this one */
  kTundraScopeInput  = 'inpt',
  kTundraScopeVSyn   = 'vsyn'
};

enum _TundraUnit
{
  kTundraUnitInput  = 'tinp',
  kTundraUnitOutput = 'tout',
  kTundraUnitSync   = 'tefc'
};

enum _TundraProperty
{
  kTundraSystemPropertyDevices = 'dev#',

  kTundraObjectPropertyClass = 'clas',
  kTundraObjectPropertyCreator = 'oplg',
  kTundraObjectPropertyName = 'lnam',
  kTundraObjectPropertyUID = 'uid ',
  kTundraObjectPropertyVendor = 'lmak',

  kTundraDevicePropertyConfigApp = 'capp', /* CFString: com.apple.mediaio.TundraDeviceSetup */
  kTundraDevicePropertyExclusiveMode = 'ixna',
  kTundraDevicePropertyHogMode = 'oink',
  kTundraDevicePropertyModelUID = 'muid',
  kTundraDevicePropertyStreams = 'stm#',
  kTundraDevicePropertySuspendedByUser = 'sbyu',
  kTundraDevicePropertyTransportType = 'tran',

  kTundraStreamPropertyFormatDescriptions = 'pfta',
  kTundraStreamPropertyFormatDescription = 'pft ',
  kTundraStreamPropertyFrameRates = 'nfr#',
  kTundraStreamPropertyFrameRate = 'nfrt'
};

struct _TundraTargetSpec
{
  FourCharCode name;
  FourCharCode scope;
  FourCharCode vendor;
  FourCharCode unk1;
  FourCharCode unk2;
};

struct _TundraFramerate
{
  gdouble value;
};

enum _TundraUnitProperty
{
  kTundraInputPropertyDeviceID                = 302,

  kTundraOutputPropertyDelegate               = 5903,

  kTundraInputUnitProperty_SourcePath         = 6780,

  kTundraSyncPropertyClockProvider            = 7100,
  kTundraSyncPropertyMasterSynchronizer       = 7102,
  kTundraSyncPropertySynchronizationDirection = 7104
};

enum _TundraDeviceTransportType
{
  kTundraDeviceTransportInvalid = 0,
  kTundraDeviceTransportBuiltin = 'bltn',
  kTundraDeviceTransportScreen  = 'scrn',
  kTundraDeviceTransportUSB     = 'usb ',
};

typedef TundraStatus (* TundraOutputRenderFunc) (gpointer instance,
    gpointer unk1, gpointer unk2, gpointer unk3, CMSampleBufferRef sampleBuf);
typedef TundraStatus (* TundraOutputInitializeFunc) (gpointer instance);
typedef TundraStatus (* TundraOutputUninitializeFunc) (gpointer instance);
typedef TundraStatus (* TundraOutputStartFunc) (gpointer instance);
typedef TundraStatus (* TundraOutputStopFunc) (gpointer instance);
typedef TundraStatus (* TundraOutputResetFunc) (gpointer instance);
typedef TundraStatus (* TundraOutputDeallocateFunc) (gpointer instance);
typedef gboolean (* TundraOutputCanRenderNowFunc) (gpointer instance,
    guint * unk);
typedef CFArrayRef (* TundraOutputAvailableFormatsFunc) (gpointer instance,
    gboolean ensureOnly);
typedef TundraStatus (* TundraOutputCopyClockFunc) (gpointer instance);
typedef TundraStatus (* TundraOutputGetPropertyInfoFunc) (gpointer instance,
    guint propId);
typedef TundraStatus (* TundraOutputGetPropertyFunc) (gpointer instance,
    guint propId);
typedef TundraStatus (* TundraOutputSetPropertyFunc) (gpointer instance,
    guint propId);

#pragma pack(push, 1)

struct _TundraOutputDelegate
{
  int unk1;
  gpointer instance;
  TundraOutputRenderFunc Render;
  TundraOutputInitializeFunc Initialize;
  TundraOutputUninitializeFunc Uninitialize;
  TundraOutputStartFunc Start;
  TundraOutputStopFunc Stop;
  TundraOutputResetFunc Reset;
  TundraOutputDeallocateFunc Deallocate;
  TundraOutputCanRenderNowFunc CanRenderNow;
  TundraOutputAvailableFormatsFunc AvailableFormats;
  TundraOutputCopyClockFunc CopyClock;
  TundraOutputGetPropertyInfoFunc GetPropertyInfo;
  TundraOutputGetPropertyFunc GetProperty;
  TundraOutputSetPropertyFunc SetProperty;
};

#pragma pack(pop)

struct _GstMIOApi
{
  GstDynApi parent;

  TundraStatus (* TundraGraphCreate) (CFAllocatorRef allocator,
      TundraGraph ** graph);
  void (* TundraGraphRelease) (TundraGraph * graph);
  TundraStatus (* TundraGraphCreateNode) (TundraGraph * graph,
      gint nodeId, UInt32 unk1, UInt32 unk2, TundraTargetSpec * spec,
      UInt32 unk3, TundraUnitID * node);
  TundraStatus (* TundraGraphGetNodeInfo) (TundraGraph * graph,
      gint nodeId, UInt32 unk1, UInt32 unk2, UInt32 unk3, UInt32 unk4,
      gpointer * info);
  TundraStatus (* TundraGraphSetProperty) (TundraGraph * graph,
      gint nodeId, UInt32 unk1, guint propId, UInt32 unk2, UInt32 unk3,
      gpointer data, guint size);
  TundraStatus (* TundraGraphConnectNodeInput) (TundraGraph * graph,
      TundraUnitID from_node, guint from_bus,
      TundraUnitID to_node, guint to_bus);
  TundraStatus (* TundraGraphInitialize) (TundraGraph * graph);
  TundraStatus (* TundraGraphUninitialize) (TundraGraph * graph);
  TundraStatus (* TundraGraphStart) (TundraGraph * graph);
  TundraStatus (* TundraGraphStop) (TundraGraph * graph);

  TundraStatus (* TundraObjectGetPropertyDataSize) (TundraObjectID obj,
      TundraTargetSpec * spec, UInt32 contextSize, void * context, guint * size);
  TundraStatus (* TundraObjectGetPropertyData) (TundraObjectID obj,
      TundraTargetSpec * spec, UInt32 contextSize, void * context, guint * size,
      gpointer data);
  TundraStatus (* TundraObjectIsPropertySettable) (TundraObjectID obj,
      TundraTargetSpec * spec, Boolean *isSettable);
  TundraStatus (* TundraObjectSetPropertyData) (TundraObjectID obj,
      TundraTargetSpec * spec, gpointer unk1, gpointer unk2, guint size,
      gpointer data);

  CFStringRef * kTundraSampleBufferAttachmentKey_SequenceNumber;
  CFStringRef * kTundraSampleBufferAttachmentKey_HostTime;
};

struct _GstMIOApiClass
{
  GstDynApiClass parent_class;
};

GstMIOApi * gst_mio_api_obtain (GError ** error);

gpointer gst_mio_object_get_pointer (gint obj, TundraTargetSpec * pspec,
    GstMIOApi * mio);
gchar * gst_mio_object_get_string (gint obj, TundraTargetSpec * pspec,
    GstMIOApi * mio);
guint32 gst_mio_object_get_uint32 (gint obj, TundraTargetSpec * pspec,
    GstMIOApi * mio);
gchar * gst_mio_object_get_fourcc (gint obj, TundraTargetSpec * pspec,
    GstMIOApi * mio);
GArray * gst_mio_object_get_array (gint obj, TundraTargetSpec * pspec,
    guint element_size, GstMIOApi * mio);
GArray * gst_mio_object_get_array_full (gint obj, TundraTargetSpec * pspec,
    guint ctx_size, gpointer ctx, guint element_size, GstMIOApi * mio);
gpointer gst_mio_object_get_raw (gint obj, TundraTargetSpec * pspec,
    guint * size, GstMIOApi * mio);

gchar * gst_mio_fourcc_to_string (guint32 fcc);

G_END_DECLS

#endif
