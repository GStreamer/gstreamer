namespace Gst.Interfaces {

  using System;
  using System.Runtime.InteropServices;
  using System.Reflection;
  using Gst.GLib;
  using Gst;
  using Gst.Interfaces;

  public static class MixerMessage {
    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern int gst_mixer_message_get_type (IntPtr message);

    public static Gst.Interfaces.MixerMessageType MessageGetType (Gst.Message message) {
      int raw_ret = gst_mixer_message_get_type (message == null ? IntPtr.Zero : message.Handle);
      Gst.Interfaces.MixerMessageType ret = (Gst.Interfaces.MixerMessageType) raw_ret;
      return ret;
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_mixer_message_parse_mute_toggled (IntPtr message, out IntPtr track, out bool mute);

    public static void ParseMuteToggled (Gst.Message message, out Gst.Interfaces.MixerTrack track, out bool mute) {
      IntPtr native_ptr;

      gst_mixer_message_parse_mute_toggled (message == null ? IntPtr.Zero : message.Handle, out native_ptr, out mute);

      track = (MixerTrack) Gst.GLib.Object.GetObject (native_ptr, false);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_mixer_message_parse_option_changed (IntPtr message, out IntPtr options, out IntPtr value);

    public static void ParseOptionChanged (Gst.Message message, out Gst.Interfaces.MixerOptions options, out string value) {
      IntPtr native_value;
      IntPtr native_options;

      gst_mixer_message_parse_option_changed (message == null ? IntPtr.Zero : message.Handle, out native_options, out native_value);

      options = (MixerOptions) Gst.GLib.Object.GetObject (native_options, false);
      value = Gst.GLib.Marshaller.Utf8PtrToString (native_value);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_mixer_message_parse_record_toggled (IntPtr message, out IntPtr track, out bool record);

    public static void ParseRecordToggled (Gst.Message message, out Gst.Interfaces.MixerTrack track, out bool record) {
      IntPtr native_ptr;

      gst_mixer_message_parse_record_toggled (message == null ? IntPtr.Zero : message.Handle, out native_ptr, out record);
      track = (MixerTrack) Gst.GLib.Object.GetObject (native_ptr, false);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_mixer_message_parse_volume_changed (IntPtr message, out IntPtr track, out IntPtr volumes, out int num_channels);

    public static void ParseVolumeChanged (Gst.Message message, out Gst.Interfaces.MixerTrack track, out int[] volumes) {
      IntPtr native_track;
      IntPtr native_volumes;
      int n_native_volumes;

      gst_mixer_message_parse_volume_changed (message == null ? IntPtr.Zero : message.Handle, out native_track, out native_volumes, out n_native_volumes);

      track = (MixerTrack) Gst.GLib.Object.GetObject (native_track, false);
      volumes = new int[n_native_volumes];
      for (int i = 0; i < n_native_volumes; i++)
        volumes[i] = Marshal.ReadInt32 (native_volumes, i * 4);
      Gst.GLib.Marshaller.Free (native_volumes);
    }

    [DllImport ("libgstinterfaces-0.10.dll") ]
    static extern void gst_mixer_message_parse_options_list_changed (IntPtr message, out IntPtr options);

    public static void ParseOptionsListChanged (Gst.Message message, out Gst.Interfaces.MixerOptions options) {
      IntPtr native_options;

      gst_mixer_message_parse_options_list_changed (message == null ? IntPtr.Zero : message.Handle, out native_options);
      options = (MixerOptions) Gst.GLib.Object.GetObject (native_options, false);
    }
  }
}
