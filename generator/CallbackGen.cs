// GtkSharp.Generation.CallbackGen.cs - The Callback Generatable.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2002-2003 Mike Kestner
// Copyright (c) 2007 Novell, Inc.
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
	using System.IO;
	using System.Xml;

	public class CallbackGen : GenBase, IAccessor {

		private Parameters parms;
		private Signature sig = null;
		private ReturnValue retval;
		private bool valid = true;

		public CallbackGen (XmlElement ns, XmlElement elem) : base (ns, elem) 
		{
			retval = new ReturnValue (elem ["return-type"]);
			parms = new Parameters (elem ["parameters"]);
			parms.HideData = true;
		}

		public override string DefaultValue {
			get { return "null"; }
		}

		public override bool Validate ()
		{
			if (!retval.Validate ()) {
				Console.WriteLine ("rettype: " + retval.CType + " in callback " + CName);
				Statistics.ThrottledCount++;
				valid = false;
				return false;
			}

			if (!parms.Validate ()) {
				Console.WriteLine (" in callback " + CName);
				Statistics.ThrottledCount++;
				valid = false;
				return false;
			}

			valid = true;
			return true;
		}

		public string InvokerName {
			get {
				if (!valid)
					return String.Empty;
				return NS + "Sharp." + Name + "Invoker";
			}
		}

		public override string MarshalType {
			get {
				if (valid)
					return NS + "Sharp." + Name + "Native";
				else
					return "";
			}
		}

		public override string CallByName (string var_name)
		{
			return var_name + ".NativeDelegate";
		}

		public override string FromNative (string var)
		{
			return NS + "Sharp." + Name + "Wrapper.GetManagedDelegate (" + var + ")";
		}

		public void WriteAccessors (StreamWriter sw, string indent, string var)
		{
			sw.WriteLine (indent + "get {");
			sw.WriteLine (indent + "\treturn " + FromNative (var) + ";");
			sw.WriteLine (indent + "}");
		}

		string CastFromInt (string type)
		{
			return type != "int" ? "(" + type + ") " : "";
		}

		string InvokeString {
			get {
				if (parms.Count == 0)
					return String.Empty;

				string[] result = new string [parms.Count];
				for (int i = 0; i < parms.Count; i++) {
					Parameter p = parms [i];
					IGeneratable igen = p.Generatable;

					if (i > 0 && parms [i - 1].IsString && p.IsLength) {
						string string_name = parms [i - 1].Name;
						result[i] = igen.CallByName (CastFromInt (p.CSType) + "System.Text.Encoding.UTF8.GetByteCount (" +  string_name + ")");
						continue;
					}

					p.CallName = p.Name;
					result [i] = p.CallString;
					if (p.IsUserData)
						result [i] = "__data"; 
				}

				return String.Join (", ", result);
			}
		}

		MethodBody body;

		void GenInvoker (GenerationInfo gen_info, StreamWriter sw)
		{
			if (sig == null)
				sig = new Signature (parms);

			sw.WriteLine ("\tinternal class " + Name + "Invoker {");
			sw.WriteLine ();
			sw.WriteLine ("\t\t" + Name + "Native native_cb;");
			sw.WriteLine ("\t\tIntPtr __data;");
			sw.WriteLine ("\t\tGLib.DestroyNotify __notify;");
			sw.WriteLine ();
			sw.WriteLine ("\t\t~" + Name + "Invoker ()");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tif (__notify == null)");
			sw.WriteLine ("\t\t\t\treturn;");
			sw.WriteLine ("\t\t\t__notify (__data);");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tinternal " + Name + "Invoker (" + Name + "Native native_cb) : this (native_cb, IntPtr.Zero, null) {}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tinternal " + Name + "Invoker (" + Name + "Native native_cb, IntPtr data) : this (native_cb, data, null) {}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tinternal " + Name + "Invoker (" + Name + "Native native_cb, IntPtr data, GLib.DestroyNotify notify)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tthis.native_cb = native_cb;");
			sw.WriteLine ("\t\t\t__data = data;");
			sw.WriteLine ("\t\t\t__notify = notify;");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tinternal " + QualifiedName + " Handler {");
			sw.WriteLine ("\t\t\tget {");
			sw.WriteLine ("\t\t\t\treturn new " + QualifiedName + "(InvokeNative);");
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\t" + retval.CSType + " InvokeNative (" + sig + ")");
			sw.WriteLine ("\t\t{");
			body.Initialize (gen_info);
			string call = "native_cb (" + InvokeString + ")";
			if (retval.IsVoid)
				sw.WriteLine ("\t\t\t" + call + ";");
			else
				sw.WriteLine ("\t\t\t" + retval.CSType + " result = " + retval.FromNative (call) + ";");
			body.Finish (sw, String.Empty);
			if (!retval.IsVoid)
				sw.WriteLine ("\t\t\treturn result;");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ("\t}");
			sw.WriteLine ();
		}

		public string GenWrapper (GenerationInfo gen_info)
		{
			string wrapper = Name + "Native";
			string qualname = MarshalType;

			if (!Validate ())
				return String.Empty;

			body = new MethodBody (parms);

			StreamWriter save_sw = gen_info.Writer;
			StreamWriter sw = gen_info.Writer = gen_info.OpenStream (qualname);

			sw.WriteLine ("namespace " + NS + "Sharp {");
			sw.WriteLine ();
			sw.WriteLine ("\tusing System;");
			sw.WriteLine ("\tusing System.Runtime.InteropServices;");
			sw.WriteLine ();
			sw.WriteLine ("#region Autogenerated code");
			sw.WriteLine ("\t[GLib.CDeclCallback]");
			sw.WriteLine ("\tinternal delegate " + retval.MarshalType + " " + wrapper + "(" + parms.ImportSignature + ");");
			sw.WriteLine ();
			GenInvoker (gen_info, sw);
			sw.WriteLine ("\tinternal class " + Name + "Wrapper {");
			sw.WriteLine ();
			ManagedCallString call = new ManagedCallString (parms);
			sw.WriteLine ("\t\tpublic " + retval.MarshalType + " NativeCallback (" + parms.ImportSignature + ")");
			sw.WriteLine ("\t\t{");
			string unconditional = call.Unconditional ("\t\t\t");
			if (unconditional.Length > 0)
				sw.WriteLine (unconditional);
			sw.WriteLine ("\t\t\ttry {");
			string call_setup = call.Setup ("\t\t\t\t");
			if (call_setup.Length > 0)
				sw.WriteLine (call_setup);
			if (retval.CSType == "void")
				sw.WriteLine ("\t\t\t\tmanaged ({0});", call);
			else
				sw.WriteLine ("\t\t\t\t{0} __ret = managed ({1});", retval.CSType, call);
			string finish = call.Finish ("\t\t\t\t");
			if (finish.Length > 0)
				sw.WriteLine (finish);
			sw.WriteLine ("\t\t\t\tif (release_on_call)\n\t\t\t\t\tgch.Free ();");
			if (retval.CSType != "void")
				sw.WriteLine ("\t\t\t\treturn {0};", retval.ToNative ("__ret"));

			/* If the function expects one or more "out" parameters(error parameters are excluded) or has a return value different from void and bool, exceptions
			*  thrown in the managed function have to be considered fatal meaning that an exception is to be thrown and the function call cannot not return
			*/
			bool fatal = (retval.MarshalType != "void" && retval.MarshalType != "bool") || call.HasOutParam;
			sw.WriteLine ("\t\t\t} catch (Exception e) {");
			sw.WriteLine ("\t\t\t\tGLib.ExceptionManager.RaiseUnhandledException (e, " + (fatal ? "true" : "false") + ");");
			if (fatal) {
				sw.WriteLine ("\t\t\t\t// NOTREACHED: Above call does not return.");
				sw.WriteLine ("\t\t\t\tthrow e;");
			} else if (retval.MarshalType == "bool") {
				sw.WriteLine ("\t\t\t\treturn false;");
			}
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tbool release_on_call = false;");
			sw.WriteLine ("\t\tGCHandle gch;");
			sw.WriteLine ();
			sw.WriteLine ("\t\tpublic void PersistUntilCalled ()");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\trelease_on_call = true;");
			sw.WriteLine ("\t\t\tgch = GCHandle.Alloc (this);");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tinternal " + wrapper + " NativeDelegate;");
			sw.WriteLine ("\t\t" + NS + "." + Name + " managed;");
			sw.WriteLine ();
			sw.WriteLine ("\t\tpublic " + Name + "Wrapper (" + NS + "." + Name + " managed)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tthis.managed = managed;");
			sw.WriteLine ("\t\t\tif (managed != null)");
			sw.WriteLine ("\t\t\t\tNativeDelegate = new " + wrapper + " (NativeCallback);");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tpublic static " + NS + "." + Name + " GetManagedDelegate (" + wrapper + " native)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tif (native == null)");
			sw.WriteLine ("\t\t\t\treturn null;");
			sw.WriteLine ("\t\t\t" + Name + "Wrapper wrapper = (" + Name + "Wrapper) native.Target;");
			sw.WriteLine ("\t\t\tif (wrapper == null)");
			sw.WriteLine ("\t\t\t\treturn null;");
			sw.WriteLine ("\t\t\treturn wrapper.managed;");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ("\t}");
			sw.WriteLine ("#endregion");
			sw.WriteLine ("}");
			sw.Close ();
			gen_info.Writer = save_sw;
			return NS + "Sharp." + Name + "Wrapper";
		}
		
		public override void Generate (GenerationInfo gen_info)
		{
			gen_info.CurrentType = Name;

			sig = new Signature (parms);

			StreamWriter sw = gen_info.OpenStream (Name);

			sw.WriteLine ("namespace " + NS + " {");
			sw.WriteLine ();
			sw.WriteLine ("\tusing System;");
			sw.WriteLine ();
			sw.WriteLine ("\t{0} delegate " + retval.CSType + " " + Name + "(" + sig.ToString() + ");", IsInternal ? "internal" : "public");
			sw.WriteLine ();
			sw.WriteLine ("}");

			sw.Close ();
			
			GenWrapper (gen_info);

			Statistics.CBCount++;
		}
	}
}

