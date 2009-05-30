using System;
using System.Collections;
using System.Runtime.InteropServices;
using GLib;
using Gst;

namespace Gst.CorePlugins {
	[GTypeName ("GstMemIndex")]
	public class MemIndex : Gst.Index {
		public MemIndex (IntPtr raw) : base (raw) { }

		[DllImport("libgstreamer-0.10.dll") ]
		static extern IntPtr gst_index_factory_make (IntPtr index);

		public MemIndex () : base (IntPtr.Zero) {
			IntPtr native_index = GLib.Marshaller.StringToPtrGStrdup ("memindex");
			Raw = gst_index_factory_make (native_index);
			GLib.Marshaller.Free (native_index);
			if (Raw == IntPtr.Zero)
				throw new Exception ("Failed to instantiate index \"memindex\"");
		}

		public static MemIndex Make () {
		  return Gst.IndexFactory.Make ("memindex") as MemIndex;
		} 
	}

}
