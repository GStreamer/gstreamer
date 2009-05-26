		[GLib.Signal("value-changed")]
		event Gst.Interfaces.ValueChangedHandler Gst.Interfaces.ColorBalance.ValueChanged {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (GLib.Object.GetObject (Handle), "value-changed", typeof (Gst.Interfaces.ValueChangedArgs));
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (GLib.Object.GetObject (Handle), "value-changed", typeof (Gst.Interfaces.ValueChangedArgs));
				sig.RemoveDelegate (value);
			}
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_color_balance_set_value(IntPtr raw, IntPtr channel, int value);

		void Gst.Interfaces.ColorBalance.SetValue(Gst.Interfaces.ColorBalanceChannel channel, int value) {
			gst_color_balance_set_value(Handle, channel == null ? IntPtr.Zero : channel.Handle, value);
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern IntPtr gst_color_balance_list_channels(IntPtr raw);

		Gst.Interfaces.ColorBalanceChannel[] Gst.Interfaces.ColorBalance.ListChannels() {
			IntPtr raw_ret = gst_color_balance_list_channels(Handle);
			Gst.Interfaces.ColorBalanceChannel[] ret = (Gst.Interfaces.ColorBalanceChannel[]) GLib.Marshaller.ListPtrToArray (raw_ret, typeof(GLib.List), false, false, typeof(Gst.Interfaces.ColorBalanceChannel));
			return ret;
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern int gst_color_balance_get_value(IntPtr raw, IntPtr channel);

		int Gst.Interfaces.ColorBalance.GetValue(Gst.Interfaces.ColorBalanceChannel channel) {
			int raw_ret = gst_color_balance_get_value(Handle, channel == null ? IntPtr.Zero : channel.Handle);
			int ret = raw_ret;
			return ret;
		}

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern void gst_color_balance_value_changed(IntPtr raw, IntPtr channel, int value);

		void Gst.Interfaces.ColorBalance.EmitValueChanged(Gst.Interfaces.ColorBalanceChannel channel, int value) {
			gst_color_balance_value_changed(Handle, channel == null ? IntPtr.Zero : channel.Handle, value);
		}

		[DllImport("libgobject-2.0-0.dll")]
		static extern IntPtr g_type_interface_peek (IntPtr klass, IntPtr iface_gtype);

		[DllImport("libgstinterfaces-0.10.dll")]
		static extern IntPtr gst_color_balance_get_type();

		Gst.Interfaces.ColorBalanceType Gst.Interfaces.ColorBalance.BalanceType {
			get {
				IntPtr gclass = Marshal.ReadIntPtr (Handle);
				IntPtr ifaceptr = g_type_interface_peek (gclass, gst_color_balance_get_type ());
				return (Gst.Interfaces.ColorBalanceType) Marshal.ReadInt32 (ifaceptr);
			}
		}

