#pragma once

#include <gst/gst.h>
#include <windows.h>

G_BEGIN_DECLS

#pragma pack(push,4)

typedef struct
{
  gulong hi;
  gulong lo;
} ASIOSamples;

typedef struct
{
  gulong hi;
  gulong lo;
} ASIOTimeStamp;

typedef gdouble ASIOSampleRate;
typedef glong ASIOBool;
typedef glong ASIOSampleType;

enum {
  ASIOSTInt16MSB = 0,
  ASIOSTInt24MSB = 1,
  ASIOSTInt32MSB = 2,
  ASIOSTFloat32MSB = 3,
  ASIOSTFloat64MSB = 4,

  ASIOSTInt32MSB16 = 8,
  ASIOSTInt32MSB18 = 9,
  ASIOSTInt32MSB20 = 10,
  ASIOSTInt32MSB24 = 11,

  ASIOSTInt16LSB = 16,
  ASIOSTInt24LSB = 17,
  ASIOSTInt32LSB = 18,
  ASIOSTFloat32LSB = 19,
  ASIOSTFloat64LSB = 20,

  ASIOSTInt32LSB16 = 24,
  ASIOSTInt32LSB18 = 25,
  ASIOSTInt32LSB20 = 26,
  ASIOSTInt32LSB24 = 27,

  ASIOSTDSDInt8LSB1 = 32,
  ASIOSTDSDInt8MSB1 = 33,
  ASIOSTDSDInt8NER8 = 40,

  ASIOSTLastEntry
};

typedef glong ASIOError;

typedef struct ASIOTimeCode
{
  gdouble speed;
  ASIOSamples timeCodeSamples;
  gulong  flags;
  gchar future[64];
} ASIOTimeCode;

typedef struct AsioTimeInfo
{
  gdouble speed;
  ASIOTimeStamp systemTime;
  ASIOSamples samplePosition;
  ASIOSampleRate sampleRate;
  gulong flags;
  gchar reserved[12];
} AsioTimeInfo;

typedef struct ASIOTime
{
  glong reserved[4];
  AsioTimeInfo timeInfo;
  ASIOTimeCode timeCode;
} ASIOTime;

typedef struct ASIOCallbacks
{
  void  (*bufferSwitch)             (glong doubleBufferIndex,
                                     ASIOBool directProcess);
  void  (*sampleRateDidChange)      (ASIOSampleRate sRate);
  glong (*asioMessage)              (glong selector,
                                     glong value,
                                     gpointer message,
                                     gdouble * opt);
  ASIOTime* (*bufferSwitchTimeInfo) (ASIOTime* params,
                                     glong doubleBufferIndex,
                                     ASIOBool directProcess);
} ASIOCallbacks;

enum
{
  kAsioSelectorSupported = 1,
  kAsioEngineVersion,
  kAsioResetRequest,
  kAsioBufferSizeChange,
  kAsioResyncRequest,
  kAsioLatenciesChanged,
  kAsioSupportsTimeInfo,
  kAsioSupportsTimeCode,
  kAsioMMCCommand,
  kAsioSupportsInputMonitor,
  kAsioSupportsInputGain,
  kAsioSupportsInputMeter,
  kAsioSupportsOutputGain,
  kAsioSupportsOutputMeter,
  kAsioOverload,

  kAsioNumMessageSelectors
};

typedef struct ASIOClockSource
{
  glong index;
  glong associatedChannel;
  glong associatedGroup;
  ASIOBool isCurrentSource;
  gchar name[32];
} ASIOClockSource;

typedef struct ASIOChannelInfo
{
  glong channel;
  ASIOBool isInput;
  ASIOBool isActive;
  glong channelGroup;
  ASIOSampleType type;
  gchar name[32];
} ASIOChannelInfo;

typedef struct ASIOBufferInfo
{
  ASIOBool isInput;
  glong channelNum;
  gpointer buffers[2];
} ASIOBufferInfo;

#pragma pack(pop)

G_END_DECLS

