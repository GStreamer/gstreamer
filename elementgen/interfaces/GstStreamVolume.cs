		[DllImport("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_stream_volume_get_mute(IntPtr raw);
		[DllImport("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_stream_volume_set_mute(IntPtr raw, bool mute);

		public bool Mute { 
			get {
				bool raw_ret = gst_stream_volume_get_mute(Handle);
				bool ret = raw_ret;
				return ret;
			}
			set {
				gst_stream_volume_set_mute(Handle, value);
			}
		}

		[DllImport("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern double gst_stream_volume_convert_volume(int from, int to, double val);

		public static double ConvertVolume(Gst.Interfaces.StreamVolumeFormat from, Gst.Interfaces.StreamVolumeFormat to, double val) {
			double raw_ret = gst_stream_volume_convert_volume((int) from, (int) to, val);
			double ret = raw_ret;
			return ret;
		}

		[DllImport("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_stream_volume_set_volume(IntPtr raw, int format, double val);

		public void SetVolume(Gst.Interfaces.StreamVolumeFormat format, double val) {
			gst_stream_volume_set_volume(Handle, (int) format, val);
		}

		[DllImport("libgstinterfaces-0.10.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern double gst_stream_volume_get_volume(IntPtr raw, int format);

		public double GetVolume(Gst.Interfaces.StreamVolumeFormat format) {
			double raw_ret = gst_stream_volume_get_volume(Handle, (int) format);
			double ret = raw_ret;
			return ret;
		}

