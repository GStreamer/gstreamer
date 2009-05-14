// GtkSharp.Generation.SymbolTable.cs - The Symbol Table Class.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2001-2003 Mike Kestner
// Copyright (c) 2004-2005 Novell, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the GNU General Public
// License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.


namespace GtkSharp.Generation {

	using System;
	using System.Collections;

	public class SymbolTable {
		
		static SymbolTable table = null;

		Hashtable types = new Hashtable ();
		
		public static SymbolTable Table {
			get {
				if (table == null)
					table = new SymbolTable ();

				return table;
			}
		}

		public SymbolTable ()
		{
			// Simple easily mapped types
			AddType (new SimpleGen ("void", "void", String.Empty));
			AddType (new SimpleGen ("gpointer", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("AtkFunction", "IntPtr", "IntPtr.Zero")); // function definition used for padding
			AddType (new SimpleGen ("gboolean", "bool", "false"));
			AddType (new SimpleGen ("gint", "int", "0"));
			AddType (new SimpleGen ("guint", "uint", "0"));
			AddType (new SimpleGen ("int", "int", "0"));
			AddType (new SimpleGen ("unsigned", "uint", "0"));
			AddType (new SimpleGen ("unsigned int", "uint", "0"));
			AddType (new SimpleGen ("unsigned-int", "uint", "0"));
			AddType (new SimpleGen ("gshort", "short", "0"));
			AddType (new SimpleGen ("gushort", "ushort", "0"));
			AddType (new SimpleGen ("short", "short", "0"));
			AddType (new SimpleGen ("guchar", "byte", "0"));
			AddType (new SimpleGen ("unsigned char", "byte", "0"));
			AddType (new SimpleGen ("unsigned-char", "byte", "0"));
			AddType (new SimpleGen ("guint1", "bool", "false"));
			AddType (new SimpleGen ("uint1", "bool", "false"));
			AddType (new SimpleGen ("gint8", "sbyte", "0"));
			AddType (new SimpleGen ("guint8", "byte", "0"));
			AddType (new SimpleGen ("gint16", "short", "0"));
			AddType (new SimpleGen ("guint16", "ushort", "0"));
			AddType (new SimpleGen ("gint32", "int", "0"));
			AddType (new SimpleGen ("guint32", "uint", "0"));
			AddType (new SimpleGen ("gint64", "long", "0"));
			AddType (new SimpleGen ("guint64", "ulong", "0"));
			AddType (new SimpleGen ("long long", "long", "0"));
			AddType (new SimpleGen ("gfloat", "float", "0.0"));
			AddType (new SimpleGen ("float", "float", "0.0"));
			AddType (new SimpleGen ("gdouble", "double", "0.0"));
			AddType (new SimpleGen ("double", "double", "0.0"));
			AddType (new SimpleGen ("goffset", "long", "0"));
			AddType (new SimpleGen ("GQuark", "int", "0"));

			// platform specific integer types.
#if WIN64LONGS
			AddType (new SimpleGen ("long", "int", "0"));
			AddType (new SimpleGen ("glong", "int", "0"));
			AddType (new SimpleGen ("ulong", "uint", "0"));
			AddType (new SimpleGen ("gulong", "uint", "0"));
			AddType (new SimpleGen ("unsigned long", "uint", "0"));
#else
			AddType (new LPGen ("long"));
			AddType (new LPGen ("glong"));
			AddType (new LPUGen ("ulong"));
			AddType (new LPUGen ("gulong"));
			AddType (new LPUGen ("unsigned long"));
#endif

			AddType (new LPGen ("ssize_t"));
			AddType (new LPGen ("gssize"));
			AddType (new LPUGen ("size_t"));
			AddType (new LPUGen ("gsize"));

#if OFF_T_8
			AddType (new AliasGen ("off_t", "long"));
#else
			AddType (new LPGen ("off_t"));
#endif

			// string types
			AddType (new ConstStringGen ("const-gchar"));
			AddType (new ConstStringGen ("const-xmlChar"));
			AddType (new ConstStringGen ("const-char"));
			AddType (new ConstFilenameGen ("const-gfilename"));
			AddType (new MarshalGen ("gfilename", "string", "IntPtr", "GLib.Marshaller.StringToFilenamePtr({0})", "GLib.Marshaller.FilenamePtrToStringGFree({0})"));
			AddType (new MarshalGen ("gchar", "string", "IntPtr", "GLib.Marshaller.StringToPtrGStrdup({0})", "GLib.Marshaller.PtrToStringGFree({0})"));
			AddType (new MarshalGen ("char", "string", "IntPtr", "GLib.Marshaller.StringToPtrGStrdup({0})", "GLib.Marshaller.PtrToStringGFree({0})"));
			AddType (new SimpleGen ("GStrv", "string[]", "null"));

			// manually wrapped types requiring more complex marshaling
			AddType (new ManualGen ("GInitiallyUnowned", "GLib.InitiallyUnowned", "GLib.Object.GetObject ({0})"));
			AddType (new ManualGen ("GObject", "GLib.Object", "GLib.Object.GetObject ({0})"));
			AddType (new ManualGen ("GList", "GLib.List"));
			AddType (new ManualGen ("GPtrArray", "GLib.PtrArray"));
			AddType (new ManualGen ("GSList", "GLib.SList"));
			AddType (new MarshalGen ("gunichar", "char", "uint", "GLib.Marshaller.CharToGUnichar ({0})", "GLib.Marshaller.GUnicharToChar ({0})"));
			AddType (new MarshalGen ("time_t", "System.DateTime", "IntPtr", "GLib.Marshaller.DateTimeTotime_t ({0})", "GLib.Marshaller.time_tToDateTime ({0})"));
			AddType (new MarshalGen ("GString", "string", "IntPtr", "new GLib.GString ({0}).Handle", "GLib.GString.PtrToString ({0})"));
			AddType (new MarshalGen ("GType", "GLib.GType", "IntPtr", "{0}.Val", "new GLib.GType({0})", "GLib.GType.None"));
			AddType (new ByRefGen ("GValue", "GLib.Value"));
			AddType (new SimpleGen ("GDestroyNotify", "GLib.DestroyNotify", "null"));

			// FIXME: These ought to be handled properly.
			AddType (new SimpleGen ("GC", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GError", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GMemChunk", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GTimeVal", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GClosure", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GArray", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GByteArray", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GData", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GIOChannel", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GTypeModule", "GLib.Object", "null"));
			AddType (new SimpleGen ("GHashTable", "System.IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("va_list", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("GParamSpec", "IntPtr", "IntPtr.Zero"));
			AddType (new SimpleGen ("gconstpointer", "IntPtr", "IntPtr.Zero"));
		}
		
		public void AddType (IGeneratable gen)
		{
			types [gen.CName] = gen;
		}
		
		public void AddTypes (IGeneratable[] gens)
		{
			foreach (IGeneratable gen in gens)
				types [gen.CName] = gen;
		}
		
		public int Count {
			get
			{
				return types.Count;
			}
		}
		
		public IEnumerable Generatables {
			get {
				return types.Values;
			}
		}
		
		public IGeneratable this [string ctype] {
			get {
				return DeAlias (ctype) as IGeneratable;
			}
		}

		private bool IsConstString (string type)
		{
			switch (type) {
			case "const-gchar":
			case "const-char":
			case "const-xmlChar":
			case "const-gfilename":
				return true;
			default:
				return false;
			}
		}

		private string Trim(string type)
		{
			// HACK: If we don't detect this here, there is no
			// way of indicating it in the symbol table
			if (type == "void*" || type == "const-void*") return "gpointer";

			string trim_type = type.TrimEnd('*');

			if (IsConstString (trim_type))
				return trim_type;
			
			if (trim_type.StartsWith("const-")) return trim_type.Substring(6);
			return trim_type;
		}

		private object DeAlias (string type)
		{
			type = Trim (type);
			while (types [type] is AliasGen) {
				IGeneratable igen = types [type] as AliasGen;
				types [type] = types [igen.Name];
				type = igen.Name;
			}

			return types [type];
		}

		public string FromNativeReturn(string c_type, string val)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.FromNativeReturn (val);
		}
		
		public string ToNativeReturn(string c_type, string val)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.ToNativeReturn (val);
		}

		public string FromNative(string c_type, string val)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.FromNative (val);
		}

		public string GetCSType(string c_type)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.QualifiedName;
		}
		
		public string GetName(string c_type)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.Name;
		}
		
		public string GetMarshalReturnType(string c_type)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.MarshalReturnType;
		}
		
		public string GetToNativeReturnType(string c_type)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.ToNativeReturnType;
		}
		
		public string GetMarshalType(string c_type)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.MarshalType;
		}
		
		public string CallByName(string c_type, string var_name)
		{
			IGeneratable gen = this[c_type];
			if (gen == null)
				return "";
			return gen.CallByName(var_name);
		}
	
		public bool IsOpaque(string c_type)
		{
			if (this[c_type] is OpaqueGen)
				return true;

			return false;
		}
	
		public bool IsBoxed(string c_type)
		{
			if (this[c_type] is BoxedGen)
				return true;

			return false;
		}
		
		public bool IsStruct(string c_type)
		{
			if (this[c_type] is StructGen)
				return true;

			return false;
		}
	
		public bool IsEnum(string c_type)
		{
			if (this[c_type] is EnumGen)
				return true;

			return false;
		}
	
		public bool IsEnumFlags(string c_type)
		{
			EnumGen gen = this [c_type] as EnumGen;
			return (gen != null && gen.Elem.GetAttribute ("type") == "flags");
		}
	
		public bool IsInterface(string c_type)
		{
			if (this[c_type] is InterfaceGen)
				return true;

			return false;
		}
		
		public ClassBase GetClassGen(string c_type)
		{
			return this[c_type] as ClassBase;
		}
			
		public bool IsObject(string c_type)
		{
			if (this[c_type] is ObjectGen)
				return true;

			return false;
		}

		public bool IsCallback(string c_type)
		{
			if (this[c_type] is CallbackGen)
				return true;

			return false;
		}

		public bool IsManuallyWrapped(string c_type)
		{
			if (this[c_type] is ManualGen)
				return true;

			return false;
		}

		public string MangleName(string name)
		{
			switch (name) {
			case "string":
				return "str1ng";
			case "event":
				return "evnt";
			case "null":
				return "is_null";
			case "object":
				return "objekt";
			case "params":
				return "parms";
			case "ref":
				return "reference";
			case "in":
				return "in_param";
			case "out":
				return "out_param";
			case "fixed":
				return "mfixed";
			case "byte":
				return "_byte";
			case "new":
				return "_new";
			case "base":
				return "_base";
			case "lock":
				return "_lock";
			case "callback":
				return "cb";
			case "readonly":
				return "read_only";
			case "interface":
				return "iface";
			case "internal":
				return "_internal";
			case "where":
				return "wh3r3";
			case "foreach":
				return "for_each";
			case "remove":
				return "_remove";
			default:
				break;
			}

			return name;
		}
	}
}
