
namespace Gst
{
	using System;
	using System.Collections;
	using System.Runtime.InteropServices;

	using Gst;

	public class PlayBin : Pipeline {
		[Obsolete]
		protected PlayBin(GLib.GType gtype) : base(gtype) {}
		public PlayBin(IntPtr raw) : base(raw) {}

		[GLib.Property ("uri")]
		public string Uri {
			get {
				GLib.Value val = GetProperty ("uri");
				string ret = val.Val as string;
				val.Dispose ();
				return ret;
			}

			set {
				GLib.Value val = new GLib.Value (value);
				SetProperty ("uri", val);
				val.Dispose ();
			}

		}

		[GLib.Property ("source")]
		public Element Source {
			get {
				GLib.Value val = GetProperty ("source");
				Element element = val.Val as Element;
				val.Dispose ();
				return element;
			}
		}
	}
}
