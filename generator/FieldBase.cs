// GtkSharp.Generation.FieldBase.cs - base class for struct and object
// fields
//
// Copyright (c) 2004 Novell, Inc.
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

	public abstract class FieldBase : PropertyBase {
		public FieldBase (XmlElement elem, ClassBase container_type) : base (elem, container_type) {}

		public virtual bool Validate ()
		{
			if (!Ignored && !Hidden && CSType == "") {
				Console.Write("Field {0} has unknown Type {1} ", Name, CType);
				Statistics.ThrottledCount++;
				return false;
			}

			return true;
		}

		protected virtual bool Readable {
			get {
				return elem.GetAttribute ("readable") != "false";
			}
		}

		protected virtual bool Writable {
			get {
				return elem.GetAttribute ("writeable") != "false";
			}
		}

		protected abstract string DefaultAccess { get; }

		protected string Access {
			get {
				return elem.HasAttribute ("access") ? elem.GetAttribute ("access") : DefaultAccess;
			}
		}

		public bool IsArray {
			get {
				return elem.HasAttribute("array_len") || elem.HasAttribute("array");
			}
		}

		public bool IsBitfield {
			get {
				return elem.HasAttribute("bits");
			}
		}

		public bool Ignored {
			get {
				if (container_type.GetProperty (Name) != null)
					return true;
				if (IsArray)
					return true;
				if (Access == "private" && (Getter == null) && (Setter == null))
					return true;
				return false;
			}
		}

		string getterName, setterName;
		string getOffsetName, offsetName;

		void CheckGlue ()
		{
			getterName = setterName = getOffsetName = null;
			if (DefaultAccess != "public" && (!elem.HasAttribute ("access") || (Access != "public" && Access != "protected" && Access != "internal")))
				return;

			string prefix = (container_type.NS + "Sharp_" + container_type.NS + "_" + container_type.Name).Replace(".", "__").ToLower ();

			if (IsBitfield) {
				if (Readable && Getter == null)
					getterName = prefix + "_get_" + CName;
				if (Writable && Setter == null)
					setterName = prefix + "_set_" + CName;
			} else {
				if ((Readable && Getter == null) || (Writable && Setter == null)) {
					offsetName = CName + "_offset";
					getOffsetName = prefix + "_get_" + offsetName;
				}
			}
		}

		protected override void GenerateImports (GenerationInfo gen_info, string indent)
		{
			StreamWriter sw = gen_info.Writer;
			SymbolTable table = SymbolTable.Table;

			if (getterName != null) {
				sw.WriteLine (indent + "[DllImport (\"{0}\")]", gen_info.GluelibName);
				sw.WriteLine (indent + "extern static {0} {1} ({2} raw);",
					      table.GetMarshalType (CType), getterName,
					      container_type.MarshalType);
			}

			if (setterName != null) {
				sw.WriteLine (indent + "[DllImport (\"{0}\")]", gen_info.GluelibName);
				sw.WriteLine (indent + "extern static void {0} ({1} raw, {2} value);",
					      setterName, container_type.MarshalType, table.GetMarshalType (CType));
			}

			if (getOffsetName != null) {
				sw.WriteLine (indent + "[DllImport (\"{0}\")]", gen_info.GluelibName);
				sw.WriteLine (indent + "extern static uint {0} ();", getOffsetName);
				sw.WriteLine ();
				sw.WriteLine (indent + "static uint " + offsetName + " = " + getOffsetName + " ();");
			}

			base.GenerateImports (gen_info, indent);
		}

		public virtual void Generate (GenerationInfo gen_info, string indent)
		{
			if (Ignored || Hidden)
				return;

			CheckGlue ();
			if ((getterName != null || setterName != null || getOffsetName != null) &&
			    gen_info.GlueWriter == null) {
				Console.WriteLine ("No glue-filename specified, can't create glue for {0}.{1}",
						   container_type.Name, Name);
				return;
			}

			GenerateImports (gen_info, indent);

			SymbolTable table = SymbolTable.Table;
			StreamWriter sw = gen_info.Writer;
			string modifiers = elem.HasAttribute ("new_flag") ? "new " : "";
			bool is_struct = table.IsStruct (CType) || table.IsBoxed (CType);
			string access = elem.HasAttribute ("access") ? elem.GetAttribute ("access") : "public";

			sw.WriteLine (indent + access + " " + modifiers + CSType + " " + Name + " {");

			if (Getter != null) {
				sw.Write (indent + "\tget ");
				Getter.GenerateBody (gen_info, container_type, "\t");
				sw.WriteLine ("");
			} else if (getterName != null) {
				sw.WriteLine (indent + "\tget {");
				container_type.Prepare (sw, indent + "\t\t");
				sw.WriteLine (indent + "\t\t" + CSType + " result = " + table.FromNative (ctype, getterName + " (" + container_type.CallByName () + ")") + ";");
				container_type.Finish (sw, indent + "\t\t");
				sw.WriteLine (indent + "\t\treturn result;");
				sw.WriteLine (indent + "\t}");
			} else if (Readable && offsetName != null) {
				sw.WriteLine (indent + "\tget {");
				sw.WriteLine (indent + "\t\tunsafe {");
				if (is_struct) {
					sw.WriteLine (indent + "\t\t\t" + CSType + "* raw_ptr = (" + CSType + "*)(((byte*)" + container_type.CallByName () + ") + " + offsetName + ");");
					sw.WriteLine (indent + "\t\t\treturn *raw_ptr;");
				} else {
					sw.WriteLine (indent + "\t\t\t" + table.GetMarshalType (CType) + "* raw_ptr = (" + table.GetMarshalType (CType) + "*)(((byte*)" + container_type.CallByName () + ") + " + offsetName + ");");
					sw.WriteLine (indent + "\t\t\treturn " + table.FromNative (ctype, "(*raw_ptr)") + ";");
				}
				sw.WriteLine (indent + "\t\t}");
				sw.WriteLine (indent + "\t}");
			}

			IGeneratable gen = table [CType];
			string to_native = (gen is IManualMarshaler) ? (gen as IManualMarshaler).AllocNative ("value") : gen.CallByName ("value");

			if (Setter != null) {
				sw.Write (indent + "\tset ");
				Setter.GenerateBody (gen_info, container_type, "\t");
				sw.WriteLine ("");
			} else if (setterName != null) {
				sw.WriteLine (indent + "\tset {");
				container_type.Prepare (sw, indent + "\t\t");
				sw.WriteLine (indent + "\t\t" + setterName + " (" + container_type.CallByName () + ", " + to_native + ");");
				container_type.Finish (sw, indent + "\t\t");
				sw.WriteLine (indent + "\t}");
			} else if (Writable && offsetName != null) {
				sw.WriteLine (indent + "\tset {");
				sw.WriteLine (indent + "\t\tunsafe {");
				if (is_struct) {
					sw.WriteLine (indent + "\t\t\t" + CSType + "* raw_ptr = (" + CSType + "*)(((byte*)" + container_type.CallByName () + ") + " + offsetName + ");");
					sw.WriteLine (indent + "\t\t\t*raw_ptr = value;");
				} else {
					sw.WriteLine (indent + "\t\t\t" + table.GetMarshalType (CType) + "* raw_ptr = (" + table.GetMarshalType (CType) + "*)(((byte*)" + container_type.CallByName () + ") + " + offsetName + ");");
					sw.WriteLine (indent + "\t\t\t*raw_ptr = " + to_native + ";");
				}
				sw.WriteLine (indent + "\t\t}");
				sw.WriteLine (indent + "\t}");
			}

			sw.WriteLine (indent + "}");
			sw.WriteLine ("");

			if (getterName != null || setterName != null || getOffsetName != null)
				GenerateGlue (gen_info);
		}

		protected void GenerateGlue (GenerationInfo gen_info)
		{
			StreamWriter sw = gen_info.GlueWriter;
			SymbolTable table = SymbolTable.Table;

			string FieldCType = CType.Replace ("-", " ");
			bool byref = table[CType] is ByRefGen || table[CType] is StructGen;
			string GlueCType = byref ? FieldCType + " *" : FieldCType;
			string ContainerCType = container_type.CName;
			string ContainerCName = container_type.Name.ToLower ();

			if (getterName != null) {
				sw.WriteLine ("{0} {1} ({2} *{3});",
					      GlueCType, getterName, ContainerCType, ContainerCName);
			}
			if (setterName != null) {
				sw.WriteLine ("void {0} ({1} *{2}, {3} value);",
					      setterName, ContainerCType, ContainerCName, GlueCType);
			}
			if (getOffsetName != null)
				sw.WriteLine ("guint {0} (void);", getOffsetName);
			sw.WriteLine ("");

			if (getterName != null) {
				sw.WriteLine (GlueCType);
				sw.WriteLine ("{0} ({1} *{2})", getterName, ContainerCType, ContainerCName);
				sw.WriteLine ("{");
				sw.WriteLine ("\treturn ({0}){1}{2}->{3};", GlueCType,
					      byref ? "&" : "", ContainerCName, CName);
				sw.WriteLine ("}");
				sw.WriteLine ("");
			}
			if (setterName != null) {
				sw.WriteLine ("void");
				sw.WriteLine ("{0} ({1} *{2}, {3} value)",
					      setterName, ContainerCType, ContainerCName, GlueCType);
				sw.WriteLine ("{");
				sw.WriteLine ("\t{0}->{1} = ({2}){3}value;", ContainerCName, CName,
					      FieldCType, byref ? "*" : "");
				sw.WriteLine ("}");
				sw.WriteLine ("");
			}
			if (getOffsetName != null) {
				sw.WriteLine ("guint");
				sw.WriteLine ("{0} (void)", getOffsetName);
				sw.WriteLine ("{");
				sw.WriteLine ("\treturn (guint)G_STRUCT_OFFSET ({0}, {1});",
					      ContainerCType, CName);
				sw.WriteLine ("}");
				sw.WriteLine ("");
			}
		}
	}
}

