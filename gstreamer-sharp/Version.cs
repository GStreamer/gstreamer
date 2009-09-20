//
// Version.cs: Lightweight Version Object for GStreamer
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// Copyright (C) 2006 Novell, Inc.
//

using System;
using System.Runtime.InteropServices;

namespace Gst {
  public static class Version {
    private static uint major;
    private static uint minor;
    private static uint micro;
    private static uint nano;
    private static string version_string;

    static Version() {
      gst_version (out major, out minor, out micro, out nano);
    }

    public static string Description {
      get {
        if (version_string == null) {
          IntPtr version_string_ptr = gst_version_string();
          version_string = Gst.GLib.Marshaller.Utf8PtrToString (version_string_ptr);
        }

        return version_string;
      }
    }

    public static uint Major {
      get {
        return major;
      }
    }

    public static uint Minor {
      get {
        return minor;
      }
    }

    public static uint Micro {
      get {
        return micro;
      }
    }

    public static uint Nano {
      get {
        return nano;
      }
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern void gst_version (out uint major, out uint minor, out uint micro, out uint nano);

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern IntPtr gst_version_string();
  }
}
