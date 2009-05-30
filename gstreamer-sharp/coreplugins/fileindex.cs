using System;
using System.Collections;
using System.Runtime.InteropServices;
using GLib;
using Gst;

namespace Gst.CorePlugins {
	[GTypeName ("GstFileIndex")]
	public class FileIndex : Gst.Index {
		public FileIndex (IntPtr raw) : base (raw) { }

		[DllImport("libgstreamer-0.10.dll") ]
		static extern IntPtr gst_index_factory_make (IntPtr index);

		public FileIndex () : base (IntPtr.Zero) {
			IntPtr native_index = GLib.Marshaller.StringToPtrGStrdup ("fileindex");
			Raw = gst_index_factory_make (native_index);
			GLib.Marshaller.Free (native_index);
			if (Raw == IntPtr.Zero)
				throw new Exception ("Failed to instantiate index \"fileindex\"");
		}

		public static FileIndex Make () {
		  return Gst.IndexFactory.Make ("fileindex") as FileIndex;
		} 

                [GLib.Property ("location")]
                public string Location {
                        get {
                                GLib.Value val = GetProperty ("location");
                                string ret = (string) val.Val;
                                val.Dispose ();
                                return ret;
                        }
                        set {
                                GLib.Value val = new GLib.Value (this, "location");
                                val.Val = value;
                                SetProperty ("location", val);
                                val.Dispose ();
                        }
                }
	}

}
