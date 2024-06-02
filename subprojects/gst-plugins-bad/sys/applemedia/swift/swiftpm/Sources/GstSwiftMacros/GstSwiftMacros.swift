import SwiftCompilerPlugin
import SwiftSyntax
import SwiftSyntaxMacros

@main
struct GstSwiftMacros: CompilerPlugin {
  var providingMacros: [Macro.Type] = [
    GstErrorMacro.self,
    GstWarningMacro.self,
    GstFixmeMacro.self,
    GstInfoMacro.self,
    GstDebugMacro.self,
    GstLogMacro.self,
    GstTraceMacro.self,
    GstMemdumpMacro.self,
    GstErrorObjectMacro.self,
    GstWarningObjectMacro.self,
    GstFixmeObjectMacro.self,
    GstInfoObjectMacro.self,
    GstDebugObjectMacro.self,
    GstLogObjectMacro.self,
    GstTraceObjectMacro.self,
    GstMemdumpObjectMacro.self,
    GstElementErrorMacro.self,
    GstElementWarningMacro.self,
    GstElementInfoMacro.self,
  ]
}
