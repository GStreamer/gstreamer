		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_navigation_send_event(IntPtr raw, IntPtr structure);

		[DllImport("libgstreamer-0.10.dll")]
		static extern IntPtr gst_structure_copy (IntPtr raw);

		void Gst.Interfaces.Navigation.SendEvent(Gst.Structure structure) {
			gst_navigation_send_event(Handle, structure == null ? IntPtr.Zero : gst_structure_copy (structure.Handle));
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_navigation_send_mouse_event(IntPtr raw, IntPtr evnt, int button, double x, double y);

		void Gst.Interfaces.Navigation.SendMouseEvent(string evnt, int button, double x, double y) {
			IntPtr native_evnt = GLib.Marshaller.StringToPtrGStrdup (evnt);
			gst_navigation_send_mouse_event(Handle, native_evnt, button, x, y);
			GLib.Marshaller.Free (native_evnt);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_navigation_send_key_event(IntPtr raw, IntPtr evnt, IntPtr key);

		void Gst.Interfaces.Navigation.SendKeyEvent(string evnt, string key) {
			IntPtr native_evnt = GLib.Marshaller.StringToPtrGStrdup (evnt);
			IntPtr native_key = GLib.Marshaller.StringToPtrGStrdup (key);
			gst_navigation_send_key_event(Handle, native_evnt, native_key);
			GLib.Marshaller.Free (native_evnt);
			GLib.Marshaller.Free (native_key);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_navigation_send_command(IntPtr raw, int command);

		void Gst.Interfaces.Navigation.SendCommand(Gst.Interfaces.NavigationCommand command) {
			gst_navigation_send_command(Handle, (int) command);
		}

