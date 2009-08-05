using System;
using System.Collections;
using System.Runtime.InteropServices;
using Gst.GLib;
using Gst;

namespace Gst.CorePlugins {
	[GTypeName ("GstFileIndex")]
	public class FileIndex : Gst.Index {
		public FileIndex (IntPtr raw) : base (raw) { }

		[DllImport("libgstreamer-0.10.dll") ]
		static extern IntPtr gst_index_factory_make (IntPtr index);

		public FileIndex () : base (IntPtr.Zero) {
			IntPtr native_index = Gst.GLib.Marshaller.StringToPtrGStrdup ("fileindex");
			Raw = gst_index_factory_make (native_index);
			Gst.GLib.Marshaller.Free (native_index);
			if (Raw == IntPtr.Zero)
				throw new Exception ("Failed to instantiate index \"fileindex\"");
		}

		public static FileIndex Make () {
		  return Gst.IndexFactory.Make ("fileindex") as FileIndex;
		} 

                [Gst.GLib.Property ("location")]
                public string Location {
                        get {
                                Gst.GLib.Value val = GetProperty ("location");
                                string ret = (string) val.Val;
                                val.Dispose ();
                                return ret;
                        }
                        set {
                                Gst.GLib.Value val = new Gst.GLib.Value (this, "location");
                                val.Val = value;
                                SetProperty ("location", val);
                                val.Dispose ();
                        }
                }
	}

}
