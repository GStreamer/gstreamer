[GLib.Signal ("new-uri") ]
event Gst.NewUriHandler Gst.URIHandler.NewUri {
  add {
    GLib.Signal sig = GLib.Signal.Lookup (GLib.Object.GetObject (Handle), "new-uri", typeof (Gst.NewUriArgs));
    sig.AddDelegate (value);
  }
  remove {
    GLib.Signal sig = GLib.Signal.Lookup (GLib.Object.GetObject (Handle), "new-uri", typeof (Gst.NewUriArgs));
    sig.RemoveDelegate (value);
  }
}

[DllImport ("libgstreamer-0.10.dll") ]
static extern uint gst_uri_handler_get_uri_type (IntPtr raw);

Gst.URIType Gst.URIHandler.UriType {
  get {
    uint raw_ret = gst_uri_handler_get_uri_type (Handle);
    Gst.URIType ret = (Gst.URIType) raw_ret;
    return ret;
  }
}

[DllImport ("libgstreamer-0.10.dll") ]
static extern bool gst_uri_handler_set_uri (IntPtr raw, IntPtr uri);

bool Gst.URIHandler.SetUri (string uri) {
  IntPtr native_uri = GLib.Marshaller.StringToPtrGStrdup (uri);
  bool raw_ret = gst_uri_handler_set_uri (Handle, native_uri);
  bool ret = raw_ret;
  GLib.Marshaller.Free (native_uri);
  return ret;
}

[DllImport ("libgstreamer-0.10.dll") ]
static extern IntPtr gst_uri_handler_get_protocols (IntPtr raw);

string[] Gst.URIHandler.Protocols {
  get {
    IntPtr raw_ret = gst_uri_handler_get_protocols (Handle);
    string[] ret = GLib.Marshaller.NullTermPtrToStringArray (raw_ret, false);
    return ret;
  }
}

[DllImport ("libgstreamer-0.10.dll") ]
static extern IntPtr gst_uri_handler_get_uri (IntPtr raw);

string Gst.URIHandler.Uri {
  get {
    IntPtr raw_ret = gst_uri_handler_get_uri (Handle);
    string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
    return ret;
  }
}

