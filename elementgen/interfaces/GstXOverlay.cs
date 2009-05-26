		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_x_overlay_expose(IntPtr raw);

		void Gst.Interfaces.XOverlay.Expose() {
			gst_x_overlay_expose(Handle);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_x_overlay_handle_events(IntPtr raw, bool handle_events);

		void Gst.Interfaces.XOverlay.HandleEvents(bool handle_events) {
			gst_x_overlay_handle_events(Handle, handle_events);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_x_overlay_got_xwindow_id(IntPtr raw, UIntPtr xwindow_id);

		void Gst.Interfaces.XOverlay.GotXwindowId(ulong xwindow_id) {
			gst_x_overlay_got_xwindow_id(Handle, new UIntPtr (xwindow_id));
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_x_overlay_prepare_xwindow_id(IntPtr raw);

		void Gst.Interfaces.XOverlay.PrepareXwindowId() {
			gst_x_overlay_prepare_xwindow_id(Handle);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_x_overlay_set_xwindow_id(IntPtr raw, UIntPtr xwindow_id);

		ulong Gst.Interfaces.XOverlay.XwindowId { 
			set {
				gst_x_overlay_set_xwindow_id(Handle, new UIntPtr (value));
			}
		}

