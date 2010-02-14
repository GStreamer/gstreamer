[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_volume_changed (IntPtr raw, IntPtr track, IntPtr volumes);

public void VolumeChanged (Gst.Interfaces.MixerTrack track, int[] volumes) {
  if (track == null)
    return;

  if (volumes.Length != track.NumChannels)
    throw new ArgumentOutOfRangeException ();

  IntPtr native_volumes = Gst.GLib.Marshaller.Malloc ( (ulong) (4 * track.NumChannels));
  Marshal.Copy (volumes, 0, native_volumes, track.NumChannels);
  gst_mixer_volume_changed (Handle, track.Handle, native_volumes);
  Gst.GLib.Marshaller.Free (native_volumes);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern IntPtr gst_mixer_list_tracks (IntPtr raw);

public Gst.Interfaces.MixerTrack[] ListTracks() {
  IntPtr raw_ret = gst_mixer_list_tracks (Handle);
  Gst.Interfaces.MixerTrack[] ret = (Gst.Interfaces.MixerTrack[]) Gst.GLib.Marshaller.ListPtrToArray (raw_ret, typeof (Gst.GLib.List), false, false, typeof (Gst.Interfaces.MixerTrack));
  return ret;
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_set_option (IntPtr raw, IntPtr opts, IntPtr value);

public void SetOption (Gst.Interfaces.MixerOptions opts, string value) {
  gst_mixer_set_option (Handle, opts == null ? IntPtr.Zero : opts.Handle, Gst.GLib.Marshaller.StringToPtrGStrdup (value));
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_set_volume (IntPtr raw, IntPtr track, IntPtr volumes);

public void SetVolume (Gst.Interfaces.MixerTrack track, int[] volumes) {
  if (track == null)
    return;

  if (volumes.Length != track.NumChannels)
    throw new ArgumentOutOfRangeException ();
  IntPtr volumes_native = Gst.GLib.Marshaller.Malloc ( (ulong) (4 * track.NumChannels));
  Marshal.Copy (volumes, 0, volumes_native, track.NumChannels);
  gst_mixer_set_volume (Handle, track.Handle, volumes_native);
  Gst.GLib.Marshaller.Free (volumes_native);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern int gst_mixer_get_mixer_type (IntPtr raw);

public Gst.Interfaces.MixerType MixerType {
  get {
    int raw_ret = gst_mixer_get_mixer_type (Handle);
    Gst.Interfaces.MixerType ret = (Gst.Interfaces.MixerType) raw_ret;
    return ret;
  }
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_option_changed (IntPtr raw, IntPtr opts, IntPtr value);

public void OptionChanged (Gst.Interfaces.MixerOptions opts, string value) {
  gst_mixer_option_changed (Handle, opts == null ? IntPtr.Zero : opts.Handle, Gst.GLib.Marshaller.StringToPtrGStrdup (value));
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern IntPtr gst_mixer_get_option (IntPtr raw, IntPtr opts);

public string GetOption (Gst.Interfaces.MixerOptions opts) {
  IntPtr raw_ret = gst_mixer_get_option (Handle, opts == null ? IntPtr.Zero : opts.Handle);
  string ret = Gst.GLib.Marshaller.Utf8PtrToString (raw_ret);
  return ret;
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_set_record (IntPtr raw, IntPtr track, bool record);

public void SetRecord (Gst.Interfaces.MixerTrack track, bool record) {
  gst_mixer_set_record (Handle, track == null ? IntPtr.Zero : track.Handle, record);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_options_list_changed (IntPtr raw, IntPtr opts);

public void ListChanged (Gst.Interfaces.MixerOptions opts) {
  gst_mixer_options_list_changed (Handle, opts == null ? IntPtr.Zero : opts.Handle);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_record_toggled (IntPtr raw, IntPtr track, bool record);

public void RecordToggled (Gst.Interfaces.MixerTrack track, bool record) {
  gst_mixer_record_toggled (Handle, track == null ? IntPtr.Zero : track.Handle, record);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_mute_toggled (IntPtr raw, IntPtr track, bool mute);

public void MuteToggled (Gst.Interfaces.MixerTrack track, bool mute) {
  gst_mixer_mute_toggled (Handle, track == null ? IntPtr.Zero : track.Handle, mute);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_get_volume (IntPtr raw, IntPtr track, ref IntPtr volumes);

public int[] GetVolume (Gst.Interfaces.MixerTrack track) {
  if (track == null)
    return null;

  IntPtr native_volumes = Gst.GLib.Marshaller.Malloc ( (ulong) (4 * track.NumChannels));
  gst_mixer_get_volume (Handle, track.Handle, ref native_volumes);

  int[] volumes = new int[track.NumChannels];
  Marshal.Copy (native_volumes, volumes, 0, track.NumChannels);
  Gst.GLib.Marshaller.Free (native_volumes);
  return volumes;
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern int gst_mixer_get_mixer_flags (IntPtr raw);

public Gst.Interfaces.MixerFlags MixerFlags {
  get {
    int raw_ret = gst_mixer_get_mixer_flags (Handle);
    Gst.Interfaces.MixerFlags ret = (Gst.Interfaces.MixerFlags) raw_ret;
    return ret;
  }
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_mixer_changed (IntPtr raw);

public void MixerChanged() {
  gst_mixer_mixer_changed (Handle);
}

[DllImport ("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl) ]
static extern void gst_mixer_set_mute (IntPtr raw, IntPtr track, bool mute);

public void SetMute (Gst.Interfaces.MixerTrack track, bool mute) {
  gst_mixer_set_mute (Handle, track == null ? IntPtr.Zero : track.Handle, mute);
}

