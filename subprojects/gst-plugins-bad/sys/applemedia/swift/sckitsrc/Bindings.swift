/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without esven the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
 * This file contains everything that I needed to re-do in Swift which was necessary
 * for the SCKit elements and wasn't automatically imported from C, e.g. macros.
 * Maybe one day we'll get actual bindings, like Rust has for example.
 * For now though, this'll have to do.
 */

/// Our macro from C is not supported unfortunately, gotta do this manually
let GST_SECOND = 1_000_000_000 as UInt64

let GST_CLOCK_TIME_NONE = 18_446_744_073_709_551_615 as UInt64

public func gstCapsSetSimple(
  caps: UnsafeMutablePointer<GstCaps>!, field: String, _ arguments: CVarArg...
) {
  // NULL-terminating: https://github.com/swiftlang/swift/issues/47622
  // Strings (seriously...): https://stackoverflow.com/questions/62885399/is-swifts-handling-of-cvararg-for-string-buggy
  withVaList(arguments + [Int(0)]) { varargs in
    gst_caps_set_simple_valist(caps, field, varargs)
  }
}

func gstCapsMakeWritable(_ caps: UnsafeMutablePointer<GstCaps>) -> UnsafeMutablePointer<
  GstCaps
> {
  return caps.withMemoryRebound(to: GstMiniObject.self, capacity: 1) {
    gst_mini_object_make_writable($0)!
  }
  .withMemoryRebound(to: GstCaps.self, capacity: 1) { $0 }
}

let G_TYPE_FUNDAMENTAL_SHIFT = 2

let G_TYPE_FUNDAMENTAL_MAX = 255 << G_TYPE_FUNDAMENTAL_SHIFT

private func _g_type_make_fundamental(_ x: Int) -> GType {
  return GType(x << G_TYPE_FUNDAMENTAL_SHIFT)
}

let G_TYPE_INVALID = _g_type_make_fundamental(0)
let G_TYPE_NONE = _g_type_make_fundamental(1)
let G_TYPE_INTERFACE = _g_type_make_fundamental(2)
let G_TYPE_CHAR = _g_type_make_fundamental(3)
let G_TYPE_UCHAR = _g_type_make_fundamental(4)
let G_TYPE_BOOLEAN = _g_type_make_fundamental(5)
let G_TYPE_INT = _g_type_make_fundamental(6)
let G_TYPE_UINT = _g_type_make_fundamental(7)
let G_TYPE_LONG = _g_type_make_fundamental(8)
let G_TYPE_ULONG = _g_type_make_fundamental(9)
let G_TYPE_INT64 = _g_type_make_fundamental(10)
let G_TYPE_UINT64 = _g_type_make_fundamental(11)
let G_TYPE_ENUM = _g_type_make_fundamental(12)
let G_TYPE_FLAGS = _g_type_make_fundamental(13)
let G_TYPE_FLOAT = _g_type_make_fundamental(14)
let G_TYPE_DOUBLE = _g_type_make_fundamental(15)
let G_TYPE_STRING = _g_type_make_fundamental(16)
let G_TYPE_POINTER = _g_type_make_fundamental(17)
let G_TYPE_BOXED = _g_type_make_fundamental(18)
let G_TYPE_PARAM = _g_type_make_fundamental(19)
let G_TYPE_OBJECT = _g_type_make_fundamental(20)
let G_TYPE_VARIANT = _g_type_make_fundamental(21)

/// ********************************************************************
/// * GstError port for use with GST_ELEMENT_ERROR and similar.
/// * Extremely ugly, but it's the simplest way to make this work with a macro.
/// * We can't cross-reference our code from here to the macros lib,
/// * so this is what we're stuck with for now.
/// *********************************************************************
public protocol GstError: RawRepresentable where RawValue == Int32 {
  var domainQuarkFunc: () -> GQuark { get }
}

enum GstStreamError: Int32, GstError {
  case failed, tooLazy, notImplemented, typeNotFound, wrongType, codecNotFound, decode,
    encode, demux, mux, format, decrypt, decryptNoKey

  var domainQuarkFunc: () -> GQuark {
    return gst_stream_error_quark
  }
}

enum GstResourceError: Int32, GstError {
  case failed, tooLazy, notFound, busy, openRead, openWrite, openReadWrite, close, read,
    write, seek, sync, settings, noSpaceLeft, notAuthorized

  var domainQuarkFunc: () -> GQuark {
    return gst_resource_error_quark
  }
}

enum GstLibraryError: Int32, GstError {
  case failed, tooLazy, initError, shutdown, settings, encode

  var domainQuarkFunc: () -> GQuark {
    return gst_library_error_quark
  }
}

enum GstCoreError: Int32, GstError {
  case failed, tooLazy, notImplemented, stateChange, pad, thread, negotiation, event, seek, caps,
    tag, missingPlugin, clock, disabled

  var domainQuarkFunc: () -> GQuark {
    return gst_core_error_quark
  }
}
