namespace Gst.Interfaces {

  using System;
  using System.Runtime.InteropServices;
  using System.Reflection;
  using Gst.GLib;
  using Gst;
  using Gst.Interfaces;

  public static class NavigationEvent {
    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern int gst_navigation_event_get_type (IntPtr evnt);

    public static Gst.Interfaces.NavigationEventType EventGetType (Gst.Event evnt) {
      int raw_ret = gst_navigation_event_get_type (evnt == null ? IntPtr.Zero : evnt.Handle);
      Gst.Interfaces.NavigationEventType ret = (Gst.Interfaces.NavigationEventType) raw_ret;
      return ret;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_event_parse_key_event (IntPtr evnt, out IntPtr key);

    public static bool ParseKeyEvent (Gst.Event evnt, out string key) {
      IntPtr raw_key;
      bool ret = gst_navigation_event_parse_key_event (evnt.Handle, out raw_key);

      key = Gst.GLib.Marshaller.Utf8PtrToString (raw_key);

      return ret;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_event_parse_mouse_button_event (IntPtr evnt, out int button, out double x, out double y);

    public static bool ParseMouseButtonEvent (Gst.Event evnt, out int button, out double x, out double y) {
      return gst_navigation_event_parse_mouse_button_event (evnt.Handle, out button, out x, out y);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_event_parse_mouse_move_event (IntPtr evnt, out double x, out double y);

    public static bool ParseMouseMoveEvent (Gst.Event evnt, out double x, out double y) {
      return gst_navigation_event_parse_mouse_move_event (evnt.Handle, out x, out y);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_event_parse_command (IntPtr evnt, out int command);

    public static bool ParseCommand (Gst.Event evnt, out Gst.Interfaces.NavigationCommand command) {
      int raw_command;
      bool ret = gst_navigation_event_parse_command (evnt.Handle, out raw_command);

      command = (Gst.Interfaces.NavigationCommand) raw_command;

      return ret;
    }

  }
}
