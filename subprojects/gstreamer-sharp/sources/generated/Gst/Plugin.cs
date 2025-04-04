// This file was generated by the Gtk# code generator.
// Any changes made will be lost if regenerated.

namespace Gst {

	using System;
	using System.Collections;
	using System.Collections.Generic;
	using System.Runtime.InteropServices;

#region Autogenerated code
	public partial class Plugin : Gst.Object {

		public Plugin (IntPtr raw) : base(raw) {}

		protected Plugin() : base(IntPtr.Zero)
		{
			CreateNativeObject (new string [0], new GLib.Value [0]);
		}


		// Internal representation of the wrapped structure ABI.
		static GLib.AbiStruct _class_abi = null;
		static public new GLib.AbiStruct class_abi {
			get {
				if (_class_abi == null)
					_class_abi = new GLib.AbiStruct (Gst.Object.class_abi.Fields);

				return _class_abi;
			}
		}


		// End of the ABI representation.

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_type();

		public static new GLib.GType GType { 
			get {
				IntPtr raw_ret = gst_plugin_get_type();
				GLib.GType ret = new GLib.GType(raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_list_free(IntPtr list);

		public static void ListFree(GLib.List list) {
			gst_plugin_list_free(list == null ? IntPtr.Zero : list.Handle);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_load_by_name(IntPtr name);

		public static Gst.Plugin LoadByName(string name) {
			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (name);
			IntPtr raw_ret = gst_plugin_load_by_name(native_name);
			Gst.Plugin ret = GLib.Object.GetObject(raw_ret, true) as Gst.Plugin;
			GLib.Marshaller.Free (native_name);
			return ret;
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern unsafe IntPtr gst_plugin_load_file(IntPtr filename, out IntPtr error);

		public static unsafe Gst.Plugin LoadFile(string filename) {
			IntPtr native_filename = GLib.Marshaller.StringToFilenamePtr (filename);
			IntPtr error = IntPtr.Zero;
			IntPtr raw_ret = gst_plugin_load_file(native_filename, out error);
			Gst.Plugin ret = GLib.Object.GetObject(raw_ret, true) as Gst.Plugin;
			GLib.Marshaller.Free (native_filename);
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_plugin_register_static(int major_version, int minor_version, IntPtr name, IntPtr description, GstSharp.PluginInitFuncNative init_func, IntPtr version, IntPtr license, IntPtr source, IntPtr package, IntPtr origin);

		public static bool RegisterStatic(int major_version, int minor_version, string name, string description, Gst.PluginInitFunc init_func, string version, string license, string source, string package, string origin) {
			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (name);
			IntPtr native_description = GLib.Marshaller.StringToPtrGStrdup (description);
			GstSharp.PluginInitFuncWrapper init_func_wrapper = new GstSharp.PluginInitFuncWrapper (init_func);
			IntPtr native_version = GLib.Marshaller.StringToPtrGStrdup (version);
			IntPtr native_license = GLib.Marshaller.StringToPtrGStrdup (license);
			IntPtr native_source = GLib.Marshaller.StringToPtrGStrdup (source);
			IntPtr native_package = GLib.Marshaller.StringToPtrGStrdup (package);
			IntPtr native_origin = GLib.Marshaller.StringToPtrGStrdup (origin);
			bool raw_ret = gst_plugin_register_static(major_version, minor_version, native_name, native_description, init_func_wrapper.NativeDelegate, native_version, native_license, native_source, native_package, native_origin);
			bool ret = raw_ret;
			GLib.Marshaller.Free (native_name);
			GLib.Marshaller.Free (native_description);
			GLib.Marshaller.Free (native_version);
			GLib.Marshaller.Free (native_license);
			GLib.Marshaller.Free (native_source);
			GLib.Marshaller.Free (native_package);
			GLib.Marshaller.Free (native_origin);
			return ret;
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_plugin_register_static_full(int major_version, int minor_version, IntPtr name, IntPtr description, GstSharp.PluginInitFullFuncNative init_full_func, IntPtr version, IntPtr license, IntPtr source, IntPtr package, IntPtr origin, IntPtr user_data);

		public static bool RegisterStaticFull(int major_version, int minor_version, string name, string description, Gst.PluginInitFullFunc init_full_func, string version, string license, string source, string package, string origin) {
			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (name);
			IntPtr native_description = GLib.Marshaller.StringToPtrGStrdup (description);
			GstSharp.PluginInitFullFuncWrapper init_full_func_wrapper = new GstSharp.PluginInitFullFuncWrapper (init_full_func);
			IntPtr native_version = GLib.Marshaller.StringToPtrGStrdup (version);
			IntPtr native_license = GLib.Marshaller.StringToPtrGStrdup (license);
			IntPtr native_source = GLib.Marshaller.StringToPtrGStrdup (source);
			IntPtr native_package = GLib.Marshaller.StringToPtrGStrdup (package);
			IntPtr native_origin = GLib.Marshaller.StringToPtrGStrdup (origin);
			bool raw_ret = gst_plugin_register_static_full(major_version, minor_version, native_name, native_description, init_full_func_wrapper.NativeDelegate, native_version, native_license, native_source, native_package, native_origin, IntPtr.Zero);
			bool ret = raw_ret;
			GLib.Marshaller.Free (native_name);
			GLib.Marshaller.Free (native_description);
			GLib.Marshaller.Free (native_version);
			GLib.Marshaller.Free (native_license);
			GLib.Marshaller.Free (native_source);
			GLib.Marshaller.Free (native_package);
			GLib.Marshaller.Free (native_origin);
			return ret;
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_add_dependency(IntPtr raw, IntPtr env_vars, IntPtr paths, IntPtr names, int flags);

		public void AddDependency(string[] env_vars, string[] paths, string[] names, Gst.PluginDependencyFlags flags) {
			IntPtr native_env_vars = GLib.Marshaller.StringArrayToStrvPtr(env_vars, true);
			IntPtr native_paths = GLib.Marshaller.StringArrayToStrvPtr(paths, true);
			IntPtr native_names = GLib.Marshaller.StringArrayToStrvPtr(names, true);
			gst_plugin_add_dependency(Handle, native_env_vars, native_paths, native_names, (int) flags);
			GLib.Marshaller.StrFreeV (native_env_vars);
			GLib.Marshaller.StrFreeV (native_paths);
			GLib.Marshaller.StrFreeV (native_names);
		}

		public void AddDependency(Gst.PluginDependencyFlags flags) {
			AddDependency (null, null, null, flags);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_add_dependency_simple(IntPtr raw, IntPtr env_vars, IntPtr paths, IntPtr names, int flags);

		public void AddDependencySimple(string env_vars, string paths, string names, Gst.PluginDependencyFlags flags) {
			IntPtr native_env_vars = GLib.Marshaller.StringToPtrGStrdup (env_vars);
			IntPtr native_paths = GLib.Marshaller.StringToPtrGStrdup (paths);
			IntPtr native_names = GLib.Marshaller.StringToPtrGStrdup (names);
			gst_plugin_add_dependency_simple(Handle, native_env_vars, native_paths, native_names, (int) flags);
			GLib.Marshaller.Free (native_env_vars);
			GLib.Marshaller.Free (native_paths);
			GLib.Marshaller.Free (native_names);
		}

		public void AddDependencySimple(Gst.PluginDependencyFlags flags) {
			AddDependencySimple (null, null, null, flags);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_add_status_error(IntPtr raw, IntPtr message);

		public void AddStatusError(string message) {
			IntPtr native_message = GLib.Marshaller.StringToPtrGStrdup (message);
			gst_plugin_add_status_error(Handle, native_message);
			GLib.Marshaller.Free (native_message);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_add_status_info(IntPtr raw, IntPtr message);

		public void AddStatusInfo(string message) {
			IntPtr native_message = GLib.Marshaller.StringToPtrGStrdup (message);
			gst_plugin_add_status_info(Handle, native_message);
			GLib.Marshaller.Free (native_message);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_add_status_warning(IntPtr raw, IntPtr message);

		public void AddStatusWarning(string message) {
			IntPtr native_message = GLib.Marshaller.StringToPtrGStrdup (message);
			gst_plugin_add_status_warning(Handle, native_message);
			GLib.Marshaller.Free (native_message);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_cache_data(IntPtr raw);

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_plugin_set_cache_data(IntPtr raw, IntPtr cache_data);

		public Gst.Structure CacheData { 
			get {
				IntPtr raw_ret = gst_plugin_get_cache_data(Handle);
				Gst.Structure ret = raw_ret == IntPtr.Zero ? null : (Gst.Structure) GLib.Opaque.GetOpaque (raw_ret, typeof (Gst.Structure), false);
				return ret;
			}
			set {
				value.Owned = false;
				gst_plugin_set_cache_data(Handle, value == null ? IntPtr.Zero : value.Handle);
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_description(IntPtr raw);

		public string Description { 
			get {
				IntPtr raw_ret = gst_plugin_get_description(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_filename(IntPtr raw);

		public string Filename { 
			get {
				IntPtr raw_ret = gst_plugin_get_filename(Handle);
				string ret = GLib.Marshaller.FilenamePtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_license(IntPtr raw);

		public string License { 
			get {
				IntPtr raw_ret = gst_plugin_get_license(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_origin(IntPtr raw);

		public string Origin { 
			get {
				IntPtr raw_ret = gst_plugin_get_origin(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_package(IntPtr raw);

		public string Package { 
			get {
				IntPtr raw_ret = gst_plugin_get_package(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_release_date_string(IntPtr raw);

		public string ReleaseDateString { 
			get {
				IntPtr raw_ret = gst_plugin_get_release_date_string(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_source(IntPtr raw);

		public string Source { 
			get {
				IntPtr raw_ret = gst_plugin_get_source(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_status_errors(IntPtr raw);

		public string[] StatusErrors { 
			get {
				IntPtr raw_ret = gst_plugin_get_status_errors(Handle);
				string[] ret = GLib.Marshaller.NullTermPtrToStringArray (raw_ret, true);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_status_infos(IntPtr raw);

		public string[] StatusInfos { 
			get {
				IntPtr raw_ret = gst_plugin_get_status_infos(Handle);
				string[] ret = GLib.Marshaller.NullTermPtrToStringArray (raw_ret, true);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_status_warnings(IntPtr raw);

		public string[] StatusWarnings { 
			get {
				IntPtr raw_ret = gst_plugin_get_status_warnings(Handle);
				string[] ret = GLib.Marshaller.NullTermPtrToStringArray (raw_ret, true);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_get_version(IntPtr raw);

		public string Version { 
			get {
				IntPtr raw_ret = gst_plugin_get_version(Handle);
				string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_plugin_is_loaded(IntPtr raw);

		public bool IsLoaded { 
			get {
				bool raw_ret = gst_plugin_is_loaded(Handle);
				bool ret = raw_ret;
				return ret;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_plugin_load(IntPtr raw);

		public Gst.Plugin Load() {
			IntPtr raw_ret = gst_plugin_load(Handle);
			Gst.Plugin ret = GLib.Object.GetObject(raw_ret, true) as Gst.Plugin;
			return ret;
		}


		// Internal representation of the wrapped structure ABI.
		static GLib.AbiStruct _abi_info = null;
		static public new GLib.AbiStruct abi_info {
			get {
				if (_abi_info == null)
					_abi_info = new GLib.AbiStruct (Gst.Object.abi_info.Fields);

				return _abi_info;
			}
		}


		// End of the ABI representation.

#endregion
	}
}
