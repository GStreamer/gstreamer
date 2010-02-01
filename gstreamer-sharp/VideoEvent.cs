namespace Gst.Video {

  using System;
  using System.Runtime.InteropServices;
  using System.Reflection;
  using Gst.GLib;
  using Gst;
  using Gst.Interfaces;

  public static class VideoEvent {
    [DllImport ("libgstvideo-0.10.dll") ]
    static extern bool gst_video_event_parse_still_frame (IntPtr evnt, out bool in_still);

    public static bool ParseStillFrame (Gst.Event evnt, out bool in_still) {
      bool ret = gst_video_event_parse_still_frame (evnt.Handle, out in_still);

      return ret;
    }

    [DllImport ("libgstvideo-0.10.dll") ]
    static extern IntPtr gst_video_event_new_still_frame (bool in_still);
    
    public static Gst.Event NewStillFrame (bool in_still) {
      Gst.Event ev = (Gst.Event) Gst.MiniObject.GetObject (gst_video_event_new_still_frame (in_still), true);
      return ev;
    }
  }
}
