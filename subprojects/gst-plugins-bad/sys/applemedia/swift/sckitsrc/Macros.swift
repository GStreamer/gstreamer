@freestanding(expression)
public macro gstError(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstErrorMacro")

@freestanding(expression)
public macro gstWarning(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstWarningMacro")

@freestanding(expression)
public macro gstFixme(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstFixmeMacro")

@freestanding(expression)
public macro gstInfo(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstInfoMacro")

@freestanding(expression)
public macro gstDebug(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstDebugMacro")

@freestanding(expression)
public macro gstLog(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstLogMacro")

@freestanding(expression)
public macro gstTrace(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstTraceMacro")

@freestanding(expression)
public macro gstMemdump(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "GstSwiftMacros", type: "GstMemdumpMacro")

@freestanding(expression)
public macro gstErrorObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstErrorObjectMacro")

@freestanding(expression)
public macro gstWarningObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstWarningObjectMacro")

@freestanding(expression)
public macro gstFixmeObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstFixmeObjectMacro")

@freestanding(expression)
public macro gstInfoObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstInfoObjectMacro")

@freestanding(expression)
public macro gstDebugObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstDebugObjectMacro")

@freestanding(expression)
public macro gstLogObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstLogObjectMacro")

@freestanding(expression)
public macro gstTraceObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstTraceObjectMacro")

@freestanding(expression)
public macro gstMemdumpObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstMemdumpObjectMacro")

@freestanding(expression)
public macro gstElementError(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ el: Any,
  _ code: any GstError, _ text: String, _ debug: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstElementErrorMacro")

@freestanding(expression)
public macro gstElementWarning(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ el: Any,
  _ code: any GstError, _ text: String, _ debug: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstElementWarningMacro")

@freestanding(expression)
public macro gstElementInfo(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ el: Any,
  _ code: any GstError, _ text: String, _ debug: String
) =
  #externalMacro(module: "GstSwiftMacros", type: "GstElementInfoMacro")
