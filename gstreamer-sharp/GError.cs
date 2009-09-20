//
// Copyright (c) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
//
// This class implements some helper functions to handle GError
//

using System;
using System.Runtime.InteropServices;

namespace Gst {

  [StructLayout (LayoutKind.Sequential) ]
  internal struct GError {
    uint domain_quark;
    int code;
    IntPtr message;

    public uint Domain {
      get {
        return domain_quark;
      } set {
        domain_quark = value;
      }
    }

    public int Code {
      get {
        return code;
      } set {
        code = value;
      }
    }

    public string Message {
      get {
        if (message == IntPtr.Zero)
          return null;
        return Gst.GLib.Marshaller.Utf8PtrToString (message);
      } set {
        if (message != IntPtr.Zero)
          Gst.GLib.Marshaller.Free (message);
        message = Gst.GLib.Marshaller.StringToPtrGStrdup (value);
      }
    }

    public void Unset () {
      Gst.GLib.Marshaller.Free (message);
      message = IntPtr.Zero;
      code = 0;
      domain_quark = 0;
    }
  }
}
