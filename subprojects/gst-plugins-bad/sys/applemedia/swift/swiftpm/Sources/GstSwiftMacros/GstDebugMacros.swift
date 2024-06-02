import SwiftSyntax
import SwiftSyntaxMacros

public struct GstErrorMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Error)
  }
}

public struct GstWarningMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Warning)
  }
}

public struct GstFixmeMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Fixme)
  }
}

public struct GstInfoMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Info)
  }
}

public struct GstDebugMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Debug)
  }
}

public struct GstLogMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Log)
  }
}

public struct GstTraceMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Trace)
  }
}

public struct GstMemdumpMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacro(of: node, in: context, with: .Memdump)
  }
}

public struct GstErrorObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Error)
  }
}

public struct GstWarningObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Warning)
  }
}

public struct GstFixmeObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Fixme)
  }
}

public struct GstInfoObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Info)
  }
}

public struct GstDebugObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Debug)
  }
}

public struct GstLogObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Log)
  }
}

public struct GstTraceObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Trace)
  }
}

public struct GstMemdumpObjectMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    try handleLoggingMacroWithObject(of: node, in: context, with: .Memdump)
  }
}

public struct GstElementErrorMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    return try handleElementMessageMacro(of: node, in: context, with: .Error)
  }
}

public struct GstElementWarningMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    return try handleElementMessageMacro(of: node, in: context, with: .Warning)
  }
}

public struct GstElementInfoMacro: ExpressionMacro {
  public static func expansion(
    of node: some FreestandingMacroExpansionSyntax,
    in context: some MacroExpansionContext
  ) throws -> ExprSyntax {
    return try handleElementMessageMacro(of: node, in: context, with: .Info)
  }
}

func handleLoggingMacro(
  of node: some FreestandingMacroExpansionSyntax,
  in context: some MacroExpansionContext,
  with debugLevel: DebugLevel
) throws -> ExprSyntax {
  guard let category = node.arguments.first?.expression,
    let message = node.arguments.dropFirst().first?.expression
  else {
    throw DebugMacroError.becauseOf("Missing arguments!")
  }

  return expandLoggingMacro(
    object: nil, category: category, level: debugLevel, message: message)
}

func handleLoggingMacroWithObject(
  of node: some FreestandingMacroExpansionSyntax,
  in context: some MacroExpansionContext,
  with debugLevel: DebugLevel
) throws -> ExprSyntax {
  guard let category = node.arguments.first?.expression,
    let object = node.arguments.dropFirst().first?.expression,
    let message = node.arguments.dropFirst(2).first?.expression
  else {
    throw DebugMacroError.becauseOf("Missing arguments!")
  }

  return expandLoggingMacro(
    object: object, category: category, level: debugLevel, message: message)
}

func handleElementMessageMacro(
  of node: some FreestandingMacroExpansionSyntax,
  in context: some MacroExpansionContext,
  with type: MessageType
) throws -> ExprSyntax {
  guard let category = node.arguments.first?.expression,
    let element = node.arguments.dropFirst().first?.expression,
    let code = node.arguments.dropFirst(2).first?.expression
  else {
    throw DebugMacroError.becauseOf("Missing arguments!")
  }

  let text = node.arguments.dropFirst(3).first?.expression
  let debug = node.arguments.dropFirst(4).first?.expression

  return expandElementMessageMacro(
    category: category, element: element, type: type, code: code, text: text, debug: debug)
}

func expandLoggingMacro(
  object: ExprSyntax?,
  category: ExprSyntax,
  level: DebugLevel,
  message: ExprSyntax
) -> ExprSyntax {
  let objectStr =
    object == nil
    ? "nil" : "UnsafeMutableRawPointer(\(object!)).assumingMemoryBound(to: GObject.self)"
  return """
      gst_debug_log_literal(
        \(category),
        \(raw: level.toGst()),
        #file,
        #function,
        #line,
        \(raw: objectStr),
        \(message)
      )
    """
}

func expandElementMessageMacro(
  category: ExprSyntax,
  element: ExprSyntax,
  type: MessageType,
  code: ExprSyntax,
  text: ExprSyntax?,
  debug: ExprSyntax?
) -> ExprSyntax {
  /* gst_element_message_full uses gchar* with no const, so
   * we have do a little dance to give it mutable pointers. */
  return """
      gst_element_message_full(
        UnsafeMutableRawPointer(\(element)).assumingMemoryBound(to: GstElement.self),
        \(raw: type.toGst()),
        \(code).domainQuarkFunc(),
        \(code).rawValue,
        \(text != nil ? "strdup(\(text!))" : "nil"),
        \(debug != nil ? "strdup(\(debug!))" : "nil"),
        #file,
        #function,
        #line
      )
    """
}

enum DebugMacroError: Error { case becauseOf(String) }

enum DebugLevel {
  case None, Error, Warning, Fixme, Info, Debug, Log, Trace, Memdump

  public func toGst() -> String {
    switch self {
    case .None:
      return "GST_LEVEL_NONE"
    case .Error:
      return "GST_LEVEL_ERROR"
    case .Warning:
      return "GST_LEVEL_WARNING"
    case .Fixme:
      return "GST_LEVEL_FIXME"
    case .Info:
      return "GST_LEVEL_INFO"
    case .Debug:
      return "GST_LEVEL_DEBUG"
    case .Log:
      return "GST_LEVEL_LOG"
    case .Trace:
      return "GST_LEVEL_TRACE"
    case .Memdump:
      return "GST_LEVEL_MEMDUMP"
    }
  }
}

enum MessageType {
  case Error, Warning, Info

  public func toGst() -> String {
    switch self {
    case .Error:
      return "GST_MESSAGE_ERROR"
    case .Warning:
      return "GST_MESSAGE_WARNING"
    case .Info:
      return "GST_MESSAGE_INFO"
    }
  }
}
