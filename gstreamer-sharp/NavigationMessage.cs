namespace Gst.Interfaces {

  using System;
  using System.Runtime.InteropServices;
  using System.Reflection;
  using Gst.GLib;
  using Gst;
  using Gst.Interfaces;

  public static class NavigationMessage {
    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern int gst_navigation_message_get_type (IntPtr message);

    public static Gst.Interfaces.NavigationMessageType MessageGetType (Gst.Message message) {
      int raw_ret = gst_navigation_message_get_type (message == null ? IntPtr.Zero : message.Handle);
      Gst.Interfaces.NavigationMessageType ret = (Gst.Interfaces.NavigationMessageType) raw_ret;
      return ret;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern IntPtr gst_navigation_message_new_mouse_over (IntPtr src, bool active);

    public static Gst.Message NewMouseOver (Gst.Object src, bool active) {
      Message msg = (Message) Gst.MiniObject.GetObject (gst_navigation_message_new_mouse_over (src.Handle, active), true);
      return msg;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_message_parse_mouse_over (IntPtr msg, out bool active);

    public static bool ParseMouseOver (Gst.Message msg, out bool active) {
      return gst_navigation_message_parse_mouse_over (msg.Handle, out active);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern IntPtr gst_navigation_message_new_angles_changed (IntPtr src, uint cur_angle, uint n_angles);

    public static Gst.Message NewAnglesChanged (Gst.Object src, uint cur_angle, uint n_angles) {
      Message msg = (Message) Gst.MiniObject.GetObject (gst_navigation_message_new_angles_changed (src.Handle, cur_angle, n_angles), true);
      return msg;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_message_parse_angles_changed (IntPtr msg, out uint cur_angle, out uint n_angles);

    public static bool ParseMouseOver (Gst.Message msg, out uint cur_angle, out uint n_angles) {
      return gst_navigation_message_parse_angles_changed (msg.Handle, out cur_angle, out n_angles);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern IntPtr gst_navigation_message_new_commands_changed (IntPtr src);

    public static Gst.Message NewCommandsChanged (Gst.Object src) {
      Message msg = (Message) Gst.MiniObject.GetObject (gst_navigation_message_new_commands_changed (src.Handle), true);
      return msg;
    }
  }
}
