		[GLib.Signal("probe-needed")]
		event Gst.Interfaces.ProbeNeededHandler Gst.Interfaces.PropertyProbe.ProbeNeeded {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (GLib.Object.GetObject (Handle), "probe-needed", typeof (Gst.Interfaces.ProbeNeededArgs));
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (GLib.Object.GetObject (Handle), "probe-needed", typeof (Gst.Interfaces.ProbeNeededArgs));
				sig.RemoveDelegate (value);
			}
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern bool gst_property_probe_needs_probe_name(IntPtr raw, IntPtr name);

		bool Gst.Interfaces.PropertyProbe.NeedsProbe(string name) {
			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (name);
			bool raw_ret = gst_property_probe_needs_probe_name(Handle, native_name);
			bool ret = raw_ret;
			GLib.Marshaller.Free (native_name);
			return ret;
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_property_probe_probe_property_name(IntPtr raw, IntPtr name);

		void Gst.Interfaces.PropertyProbe.Probe(string name) {
			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (name);
			gst_property_probe_probe_property_name(Handle, native_name);
			GLib.Marshaller.Free (native_name);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern IntPtr gst_property_probe_get_properties(IntPtr raw);

		string[] Gst.Interfaces.PropertyProbe.Properties { 
			get {
				IntPtr raw_ret = gst_property_probe_get_properties(Handle);
				if (raw_ret == IntPtr.Zero)
					return new string[] {};

				GLib.List raw_ret_list = new GLib.List(raw_ret, typeof (IntPtr));
				ArrayList ret = new ArrayList ();

				foreach (IntPtr pspec in raw_ret_list) {
					Gst.PropertyInfo pi = new Gst.PropertyInfo (pspec);
					ret.Add (pi.Name);
				}

				return (string[]) ret.ToArray (typeof (string));
			}
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern IntPtr gst_property_probe_get_values_name (IntPtr raw, IntPtr name);

		object[] Gst.Interfaces.PropertyProbe.GetValues (string name) {
			IntPtr raw_name = GLib.Marshaller.StringToPtrGStrdup (name);
			IntPtr raw_ret = gst_property_probe_get_values_name (Handle, raw_name);
			GLib.Marshaller.Free (raw_name);
			if (raw_ret == IntPtr.Zero)
				return new object[] {};

			GLib.ValueArray va = new GLib.ValueArray (raw_ret);
			ArrayList ret = new ArrayList ();
			foreach (GLib.Value v in va)
			  ret.Add ((object) v.Val);

			va.Dispose ();

			return (object[]) ret.ToArray (typeof (object));		
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern IntPtr gst_property_probe_probe_and_get_values_name (IntPtr raw, IntPtr name);

		object[] Gst.Interfaces.PropertyProbe.ProbeAndGetValues (string name) {
			IntPtr raw_name = GLib.Marshaller.StringToPtrGStrdup (name);
			IntPtr raw_ret = gst_property_probe_probe_and_get_values_name (Handle, raw_name);
			GLib.Marshaller.Free (raw_name);
			if (raw_ret == IntPtr.Zero)
				return new object[] {};

			GLib.ValueArray va = new GLib.ValueArray (raw_ret);
			ArrayList ret = new ArrayList ();
			foreach (GLib.Value v in va)
			  ret.Add ((object) v.Val);

			va.Dispose ();

			return (object[]) ret.ToArray (typeof (object));		
		}
