using System;
using System.Runtime.InteropServices;
using Gst;
using Gst.Interfaces;

namespace Gst.BasePlugins {
  [GTypeName ("GstXvImageSink") ]
  public class XvImageSink : Element, XOverlay {
    public XvImageSink (IntPtr raw) : base (raw) { }

    public static XvImageSink Make (string name) {
      return ElementFactory.Make ("xvimagesink", name) as XvImageSink;
    }

    [DllImport ("gstinterfaces-0.10.dll") ]
    static extern void gst_x_overlay_expose (IntPtr raw);

    public void Expose() {
      gst_x_overlay_expose (Handle);
    }

    [DllImport ("gstinterfaces-0.10.dll") ]
    static extern void gst_x_overlay_handle_events (IntPtr raw, bool handle_events);

    public void HandleEvents (bool handle_events) {
      gst_x_overlay_handle_events (Handle, handle_events);
    }

    [DllImport ("gstinterfaces-0.10.dll") ]
    static extern void gst_x_overlay_got_xwindow_id (IntPtr raw, UIntPtr xwindow_id);

    public void GotXwindowId (ulong xwindow_id) {
      gst_x_overlay_got_xwindow_id (Handle, new UIntPtr (xwindow_id));
    }

    [DllImport ("gstinterfaces-0.10.dll") ]
    static extern void gst_x_overlay_prepare_xwindow_id (IntPtr raw);

    public void PrepareXwindowId() {
      gst_x_overlay_prepare_xwindow_id (Handle);
    }

    [DllImport ("gstinterfaces-0.10.dll") ]
    static extern void gst_x_overlay_set_xwindow_id (IntPtr raw, UIntPtr xwindow_id);

    public ulong XwindowId {
      set {
        gst_x_overlay_set_xwindow_id (Handle, new UIntPtr (value));
      }
    }

  }
}

