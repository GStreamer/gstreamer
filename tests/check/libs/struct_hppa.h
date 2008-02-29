static GstCheckABIStruct list[] = {
  {"GstAdapter", sizeof (GstAdapter), 52},
  {"GstAdapterClass", sizeof (GstAdapterClass), 84},
  {"GstBaseSink", sizeof (GstBaseSink), 408},
  {"GstBaseSinkClass", sizeof (GstBaseSinkClass), 368},
  {"GstBaseSrc", sizeof (GstBaseSrc), 392},
  {"GstBaseSrcClass", sizeof (GstBaseSrcClass), 376},
  {"GstBaseTransform", sizeof (GstBaseTransform), 368},
  {"GstBaseTransformClass", sizeof (GstBaseTransformClass), 376},
  {"GstCollectData", sizeof (GstCollectData), 120},
  {"GstCollectPads", sizeof (GstCollectPads), 92},
  {"GstCollectPadsClass", sizeof (GstCollectPadsClass), 136},
  {"GstPushSrc", sizeof (GstPushSrc), 408},
  {"GstPushSrcClass", sizeof (GstPushSrcClass), 396},

  {"GstTimedValue", sizeof (GstTimedValue), 32},
  {"GstValueArray", sizeof (GstValueArray), 24},
  {"GstController", sizeof (GstController), 40},
  {"GstControllerClass", sizeof (GstControllerClass), 84},

  {"GstNetClientClock", sizeof (GstNetClientClock), 256},
  {"GstNetClientClockClass", sizeof (GstNetClientClockClass), 192},
  {"GstNetTimePacket", sizeof (GstNetTimePacket), 16},
  {"GstNetTimeProvider", sizeof (GstNetTimeProvider), 84},
  {"GstNetTimeProviderClass", sizeof (GstNetTimeProviderClass), 120},
  {NULL, 0, 0}
};
