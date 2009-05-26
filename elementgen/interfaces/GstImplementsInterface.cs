		[DllImport("libgstreamer-0.10.dll")]
		static extern bool gst_element_implements_interface(IntPtr raw, IntPtr iface_type);

		bool Gst.ImplementsInterface.Supported(GLib.GType iface_type) {
			bool raw_ret = gst_element_implements_interface(Handle, iface_type.Val);
			bool ret = raw_ret;
			return ret;
		}

