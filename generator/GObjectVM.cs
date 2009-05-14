// GtkSharp.Generation.GObjectVM.cs - GObject specific part of VM creation
//
// Author: Christian Hoff <christian_hoff@gmx.net>
//
// Copyright (c) 2007 Novell, Inc.
// Copyright (c) 2009 Christian Hoff
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
	using System.IO;
	using System.Xml;

	public class GObjectVM : VirtualMethod
	{
		protected string class_struct_name;
		const bool force_glue_generation = false;

		public GObjectVM (XmlElement elem, ObjectBase container_type) : base (elem, container_type)
		{
			parms.HideData = false;
			this.Protection = "protected";
			class_struct_name = container_type.ClassStructName;
		}

		// Some types don't install headers. In that case, the glue code will not compile.
		bool BlockGlue {
			get {
				return elem.GetAttribute ("block_glue") == "1";
			}
		}

		protected override string CallString {
			get {
				return String.Format ("{0} ({1})", IsStatic ? this.CName + "_handler" : "On" + this.Name, call.ToString ());
			}
		}

		public void Generate (GenerationInfo gen_info, ObjectBase implementor)
		{
			if (!CanGenerate (gen_info, implementor))
				throw new NotSupportedException (String.Format ("Cannot generate virtual method {0}.{1}. Make sure a writable glue path was provided to the generator.", container_type.Name, this.CallString));

			GenerateOverride (gen_info, implementor);
			GenerateCallback (gen_info.Writer, implementor);
			if (!IsStatic)
				GenerateUnmanagedInvocation (gen_info, implementor);
		}

		protected virtual bool CanGenerate (GenerationInfo gen_info, ObjectBase implementor)
		{
			if (implementor != null || this.CName.Length == 0 || CodeType == VMCodeType.None || (CodeType == VMCodeType.Glue && !gen_info.GlueEnabled))
				return false;
			else
				return true;
		}

		enum VMCodeType {
			None,
			Managed,
			Glue
		}

		VMCodeType CodeType {
			get {
				if (!(container_type as ObjectBase).CanGenerateClassStruct || force_glue_generation) {
					if (BlockGlue)
						return VMCodeType.None;
					else
						return VMCodeType.Glue;
				} else
					return VMCodeType.Managed;
			}
		}

		enum VMOverrideType {
			Unspecified,
			DeclaringClass,
			ImplementingClass
		}

		/* There are basically two types of static virtual methods:
		* 1. VMs overridden in the declaring class (e.g. AtkUtil vms):
		* The VM is overridden in the class in which it is declared and not in the derived classes. In that case, the GAPI generates a static XYZHandler property
		* in the declaring class.
		* 2. VMs overridden in derived classes (e.g. GIO is_supported vms):
		* As with nonstatic vms, this VM type hooks into the class structure field of derived classes. This type is currently unsupported as it is rarely used
		* and we would need anonymous methods for the callback (we are using only *one* callback method; the callback does not know to which type that method call
		* has to be redirected).
		*/
		VMOverrideType OverrideType {
			get {
				if (IsStatic) {
					switch (elem.GetAttribute ("override_in")) {
					case "declaring_class":
						return VMOverrideType.DeclaringClass;
					case "implementing_class":
						return VMOverrideType.ImplementingClass;
					default:
						return VMOverrideType.Unspecified;
					}
				} else
					return VMOverrideType.ImplementingClass;
			}
		}

		protected virtual void GenerateOverride (GenerationInfo gen_info, ObjectBase implementor)
		{
			if (CodeType == VMCodeType.Glue)
				GenerateOverride_glue (gen_info);
			else
				GenerateOverride_managed (gen_info.Writer);
		}

		protected virtual void GenerateUnmanagedInvocation (GenerationInfo gen_info, ObjectBase implementor)
		{
			if (CodeType == VMCodeType.Glue)
				GenerateUnmanagedInvocation_glue (gen_info);
			else
				GenerateUnmanagedInvocation_managed (gen_info);
		}

		protected void GenerateOverrideBody (StreamWriter sw)
		{
			sw.WriteLine ("\t\tstatic {0}NativeDelegate {0}_cb_delegate;", Name);
			sw.WriteLine ("\t\tstatic " + Name + "NativeDelegate " + Name + "VMCallback {");
			sw.WriteLine ("\t\t\tget {");
			sw.WriteLine ("\t\t\t\tif ({0}_cb_delegate == null)", Name);
			sw.WriteLine ("\t\t\t\t\t{0}_cb_delegate = new {0}NativeDelegate ({0}_cb);", Name);
			sw.WriteLine ("\t\t\t\treturn {0}_cb_delegate;", Name);
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			if (IsStatic) {
				sw.WriteLine ("\t\tpublic delegate {0} {1}Delegate ({2});", retval.CSType, Name, Signature.ToString ());
				sw.WriteLine ("\t\tstatic {0}Delegate {1}_handler;", Name, CName);
				sw.WriteLine ();
				sw.WriteLine ("\t\tpublic static " + Name + "Delegate " + Name + "Handler {");
				sw.WriteLine ("\t\t\tset {");
				sw.WriteLine ("\t\t\t\t{0}_handler = value;", CName);
				sw.WriteLine ("\t\t\t\tOverride{0} ((GLib.GType) typeof ({1}), value == null ? null : {0}VMCallback);", Name, container_type.Name);
				sw.WriteLine ("\t\t\t}");
				sw.WriteLine ("\t\t}");
			} else {
				sw.WriteLine ("\t\tstatic void Override{0} (GLib.GType gtype)", this.Name);
				sw.WriteLine ("\t\t{");
				sw.WriteLine ("\t\t\tOverride{0} (gtype, {0}VMCallback);", this.Name);
				sw.WriteLine ("\t\t}");
			}
			sw.WriteLine ();
			sw.WriteLine ("\t\tstatic void Override{0} (GLib.GType gtype, {0}NativeDelegate callback)", this.Name);
			sw.WriteLine ("\t\t{");
		}

		protected void GenerateOverride_managed (StreamWriter sw)
		{
			GenerateOverrideBody (sw);
			// Override VM; class_offset var is generated by object generatable
			sw.WriteLine ("\t\t\t{0} class_iface = GetClassStruct (gtype, false);", class_struct_name);
			sw.WriteLine ("\t\t\tclass_iface.{0} = callback;", this.Name);
			sw.WriteLine ("\t\t\tOverrideClassStruct (gtype, class_iface);");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		protected void GenerateMethodBody (StreamWriter sw, ClassBase implementor)
		{
			sw.WriteLine ("\t\t[GLib.DefaultSignalHandler(Type=typeof(" + (implementor != null ? implementor.QualifiedName : container_type.QualifiedName) + "), ConnectionMethod=\"Override" + this.Name +"\")]");
			sw.Write ("\t\t{0} ", this.Protection);
			if (this.modifiers != "")
				sw.Write ("{0} ", this.modifiers);
			sw.WriteLine ("virtual {0} On{1} ({2})", retval.CSType, this.Name, Signature.ToString ());
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\t{0}Internal{1} ({2});", retval.IsVoid ? "" : "return ", this.Name, Signature.GetCallString (false));
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			// This method is to be invoked from existing VM implementations in the .customs
			sw.WriteLine ("\t\tprivate {0} Internal{1} ({2})", retval.CSType, this.Name, Signature.ToString ());
			sw.WriteLine ("\t\t{");
		}

		void GenerateUnmanagedInvocation_managed (GenerationInfo gen_info)
		{
			StreamWriter sw = gen_info.Writer;
			string native_call = "this.Handle";
			if (parms.Count > 0)
				native_call += ", " + Body.GetCallString (false);

			this.GenerateMethodBody (sw, null);
			// Find the first unmanaged ancestor
			sw.WriteLine ("\t\t\t{0}NativeDelegate unmanaged = GetClassStruct (this.LookupGType ().ThresholdType, true).{0};", this.Name);
			sw.Write ("\t\t\tif (unmanaged == null) ");
			if (parms.HasOutParam)
				sw.WriteLine ("throw new InvalidOperationException (\"No base method to invoke\");");
			else if (retval.IsVoid)
				sw.WriteLine ("return;");
			else
				sw.WriteLine ("return {0};", retval.DefaultValue);
			sw.WriteLine ();
			Body.Initialize (gen_info);
			sw.Write ("\t\t\t");
			if (!retval.IsVoid)
				sw.Write ("{0} __result = ", retval.MarshalType);
			sw.WriteLine ("unmanaged ({0});", native_call);
			Body.Finish (gen_info.Writer, "");
			if(!retval.IsVoid)
				sw.WriteLine ("\t\t\treturn {0};", retval.FromNative ("__result"));
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		/* old glue code. This code is to be used if
		* a) the generated api file is version 1
		* b) an old Mono version(< 2.4) is being used
		* Punt it when we drop support for the parser version 1.
		*/

		private string CastFromInt (string type)
		{
			return type != "int" ? "(" + type + ") " : "";
		}

		private string GlueSignature {
			get {
				string[] glue_params = new string [this.IsStatic ? parms.Count + 1 : parms.Count + 2];
				glue_params [0] = class_struct_name + " *class_struct";
				if (!IsStatic)
					glue_params [1] = container_type.CName + "* inst";
				for (int i = 0; i < parms.Count; i++)
					glue_params [i + (IsStatic ? 1 : 2)] = parms [i].CType.Replace ("const-", "const ") + " " + parms [i].Name;
				return String.Join (", ", glue_params);
			}
		}

		private string DefaultGlueValue {
			get {
				if (retval.IGen is EnumGen)
					return String.Format ("({0}) 0", retval.CType);

				string val = retval.DefaultValue;
				switch (val) {
				case "null":
					return "NULL";
				case "false":
					return "FALSE";
				case "true":
					return "TRUE";
				case "GLib.GType.None":
					return "G_TYPE_NONE";
				default:
					return val;
				}
			}
		}

		void GenerateOverride_glue (GenerationInfo gen_info)
		{
			StreamWriter glue = gen_info.GlueWriter;
			StreamWriter sw = gen_info.Writer;

			string glue_name = String.Format ("{0}sharp_{1}_override_{2}", container_type.NS.ToLower ().Replace (".", "_"), container_type.Name.ToLower (), CName);
			sw.WriteLine ("\t\t[DllImport (\"{0}\")]", gen_info.GluelibName);
			sw.WriteLine ("\t\tstatic extern void {0} (IntPtr class_struct, {1}NativeDelegate cb);", glue_name, Name);
			sw.WriteLine ();
			glue.WriteLine ("void {0} ({1} *class_struct, gpointer cb);\n", glue_name, class_struct_name);
			glue.WriteLine ("void\n{0} ({1} *class_struct, gpointer cb)", glue_name, class_struct_name);
			glue.WriteLine ("{");
			glue.WriteLine ("\tclass_struct->{0} = cb;", CName);
			glue.WriteLine ("}");
			glue.WriteLine ();

			GenerateOverrideBody (sw);
			sw.WriteLine ("\t\t\t{0} (gtype.ClassPtr, callback);", glue_name);
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateUnmanagedInvocation_glue (GenerationInfo gen_info)
		{
			StreamWriter glue = gen_info.GlueWriter;
			string glue_name = String.Format ("{0}sharp_{1}_invoke_{2}", container_type.NS.ToLower ().Replace (".", "_"), container_type.Name.ToLower (), CName);

			glue.WriteLine ("{0} {1} ({2});\n", retval.CType.Replace ("const-", "const "), glue_name, GlueSignature);
			glue.WriteLine ("{0}\n{1} ({2})", retval.CType.Replace ("const-", "const "), glue_name, GlueSignature);
			glue.WriteLine ("{");
			glue.Write ("\tif (class_struct->{0})\n\t\t", CName);
			if (!retval.IsVoid)
				glue.Write ("return ");
			string[] call_args = new string [IsStatic ? parms.Count : parms.Count + 1];
			if (!IsStatic)
				call_args [0] = "inst";
			for (int i = 0; i < parms.Count; i++)
				call_args [IsStatic ? i : i + 1] = parms[i].Name;
			glue.WriteLine ("(* class_struct->{0}) ({1});", CName, String.Join (", ", call_args));
			if (!retval.IsVoid)
				glue.WriteLine ("\treturn " + DefaultGlueValue + ";");
			glue.WriteLine ("}");
			glue.WriteLine ();

			StreamWriter sw = gen_info.Writer;
			sw.WriteLine ("\t\t[DllImport (\"{0}\")]", gen_info.GluelibName);
			sw.Write ("\t\tstatic extern {0} {1} (IntPtr class_struct", retval.MarshalType, glue_name);
			if (!IsStatic)
				sw.Write (", IntPtr inst");
			if (parms.Count > 0)
				sw.Write (", {0}", parms.ImportSignature);
			sw.WriteLine (");");
			sw.WriteLine ();

			GenerateMethodBody (sw, null);
			Body.Initialize (gen_info, false, false, String.Empty);
			string glue_call_string = "this.LookupGType ().ThresholdType.ClassPtr";
			if (!IsStatic)
				glue_call_string += ", Handle";
			if (parms.Count > 0)
				glue_call_string += ", " + Body.GetCallString (false);

			sw.Write ("\t\t\t");
			if (!retval.IsVoid)
				sw.Write ("{0} __result = ", retval.MarshalType);
			sw.WriteLine ("{0} ({1});", glue_name, glue_call_string);
			Body.Finish (gen_info.Writer, "");
			if(!retval.IsVoid)
				sw.WriteLine ("\t\t\treturn {0};", retval.FromNative ("__result"));
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		public override bool Validate ()
		{
			if (!base.Validate ()) return false;
			bool is_valid = true;

			if (this.IsStatic) {
				switch (OverrideType) {
				case VMOverrideType.Unspecified:
					Console.Write ("Static virtual methods can only be generated if you provide info on how to override this method via the metadata ");
					is_valid = false;
					break;
				case VMOverrideType.ImplementingClass:
					Console.Write ("Overriding static virtual methods in the implementing class is not supported yet ");
					is_valid = false;
					break;
				}
			}

			if (!is_valid)
				Console.WriteLine (" (in virtual method {0}.{1})", container_type.QualifiedName, this.Name);
			return is_valid;
		}
	}
}
