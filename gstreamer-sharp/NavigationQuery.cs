namespace Gst.Interfaces {

  using System;
  using System.Runtime.InteropServices;
  using System.Reflection;
  using Gst.GLib;
  using Gst;
  using Gst.Interfaces;

  public static class NavigationQuery {
    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern int gst_navigation_query_get_type (IntPtr query);

    public static Gst.Interfaces.NavigationQueryType QueryGetType (Gst.Query query) {
      int raw_ret = gst_navigation_query_get_type (query == null ? IntPtr.Zero : query.Handle);
      Gst.Interfaces.NavigationQueryType ret = (Gst.Interfaces.NavigationQueryType) raw_ret;
      return ret;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern IntPtr gst_navigation_query_new_commands ();

    public static Gst.Query NewCommands () {
      Gst.Query query = (Gst.Query) Gst.MiniObject.GetObject (gst_navigation_query_new_commands (), true);
      return query;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_navigation_query_set_commandsv (IntPtr query, uint n_commands, int[] cmds);

    public static void SetCommands (Gst.Query query, Gst.Interfaces.NavigationCommand[] cmds) {
      if (!query.IsWritable)
        throw new ApplicationException ();

      int[] raw_cmds = new int[cmds.Length];
      for (int i = 0; i < cmds.Length; i++)
        raw_cmds[i] = (int) cmds[i];

      gst_navigation_query_set_commandsv (query.Handle, (uint) raw_cmds.Length, raw_cmds);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_query_parse_commands_length (IntPtr query, out uint n_commands);
    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_query_parse_commands_nth (IntPtr query, uint nth, out int cmd);

    public static bool ParseCommands (Gst.Query query, out Gst.Interfaces.NavigationCommand[] cmds) {
      uint len;

      cmds = null;
      if (!gst_navigation_query_parse_commands_length (query.Handle, out len))
        return false;

      cmds = new Gst.Interfaces.NavigationCommand[len];

      for (uint i = 0; i < len; i++) {
        int cmd;

        if (!gst_navigation_query_parse_commands_nth (query.Handle, i, out cmd))
          return false;
        cmds[i] = (Gst.Interfaces.NavigationCommand) cmd;
      }

      return true;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern IntPtr gst_navigation_query_new_angles ();

    public static Gst.Query NewAngles () {
      Gst.Query query = (Gst.Query) Gst.MiniObject.GetObject (gst_navigation_query_new_angles (), true);
      return query;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_navigation_query_set_angles (IntPtr query, uint cur_angle, uint n_angles);

    public static void SetAngles (Gst.Query query, uint cur_angle, uint n_angles) {
      if (!query.IsWritable)
        throw new ApplicationException ();

      gst_navigation_query_set_angles (query.Handle, cur_angle, n_angles);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern bool gst_navigation_query_parse_angles (IntPtr query, out uint cur_angle, out uint n_angles);

    public static bool ParseAngles (Gst.Query query, out uint cur_angle, out uint n_angles) {
      return gst_navigation_query_parse_angles (query.Handle, out cur_angle, out n_angles);
    }
  }
}
