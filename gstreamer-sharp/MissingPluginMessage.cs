namespace Gst.PbUtils {

  using System;
  using System.Runtime.InteropServices;
  using System.Reflection;
  using Gst.GLib;
  using Gst;

  public static class MissingPluginMessage {

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern bool gst_is_missing_plugin_message (IntPtr msg);

    public static bool IsMissingPluginMessage (Gst.Message msg) {
      return msg != null && gst_is_missing_plugin_message (msg.Handle);
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_decoder_message_new (IntPtr src, IntPtr caps);

    public static Gst.Message NewMissingDecoder (Gst.Object src, Gst.Caps caps) {
      Message msg = (Message) Gst.MiniObject.GetObject (gst_missing_decoder_message_new (src.Handle, caps.Handle), true);
      return msg;
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_encoder_message_new (IntPtr src, IntPtr caps);

    public static Gst.Message NewMissingEncoder (Gst.Object src, Gst.Caps caps) {
      Message msg = (Message) Gst.MiniObject.GetObject (gst_missing_encoder_message_new (src.Handle, caps.Handle), true);
      return msg;
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_uri_sink_message_new (IntPtr src, IntPtr protocol);

    public static Gst.Message NewMissingUriSink (Gst.Object src, string protocol) {
      IntPtr native_str = Gst.GLib.Marshaller.StringToPtrGStrdup (protocol);
      Message msg = (Message) Gst.MiniObject.GetObject (gst_missing_uri_sink_message_new (src.Handle, native_str), true);
      Gst.GLib.Marshaller.Free (native_str);
      return msg;
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_uri_source_message_new (IntPtr src, IntPtr protocol);

    public static Gst.Message NewMissingUriSource (Gst.Object src, string protocol) {
      IntPtr native_str = Gst.GLib.Marshaller.StringToPtrGStrdup (protocol);
      Message msg = (Message) Gst.MiniObject.GetObject (gst_missing_uri_source_message_new (src.Handle, native_str), true);
      Gst.GLib.Marshaller.Free (native_str);
      return msg;
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_element_message_new (IntPtr src, IntPtr factory);

    public static Gst.Message NewMissingElement (Gst.Object src, string factory) {
      IntPtr native_str = Gst.GLib.Marshaller.StringToPtrGStrdup (factory);
      Message msg = (Message) Gst.MiniObject.GetObject (gst_missing_element_message_new (src.Handle, native_str), true);
      Gst.GLib.Marshaller.Free (native_str);
      return msg;
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_plugin_message_get_description (IntPtr msg);

    public static string GetDescription (Gst.Message msg) {
      if (!IsMissingPluginMessage (msg))
        throw new ApplicationException ();

      IntPtr raw_ret = gst_missing_plugin_message_get_description (msg.Handle);
      string ret = Gst.GLib.Marshaller.PtrToStringGFree (raw_ret);
      return ret;
    }

    [DllImport ("libgstpbutils-0.10.dll") ]
    static extern IntPtr gst_missing_plugin_message_get_installer_detail (IntPtr msg);

    public static string GetInstallerDetail (Gst.Message msg) {
      if (!IsMissingPluginMessage (msg))
        throw new ApplicationException ();

      IntPtr raw_ret = gst_missing_plugin_message_get_installer_detail (msg.Handle);
      string ret = Gst.GLib.Marshaller.PtrToStringGFree (raw_ret);
      return ret;
    }
  }
}

