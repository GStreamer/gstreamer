// GtkSharp.Generation.StructField.cs - The Structure Field generation
// Class.
//
// Author: Mike Kestner <mkestner@ximian.com>
//
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
	using System.IO;
	using System.Xml;

	public class StructField : FieldBase {

		public static int bitfields;

		public StructField (XmlElement elem, ClassBase container_type) : base (elem, container_type) {}

		protected override string DefaultAccess {
			get {
				if (IsPadding)
					return "private";

				return "public";
			}
		}

		int ArrayLength {
			get {
				if (!IsArray)
					return 0;
				
				int result;
				try {
					result = Int32.Parse (elem.GetAttribute("array_len"));
				} catch (Exception) {
					Console.Write ("Non-numeric array_len: " + elem.GetAttribute("array_len"));
					Console.WriteLine (" warning: array field {0} incorrectly generated", Name);
					result = 0;
				}
				return result;
			}
		}

		public new string CSType {
			get {
				string type = base.CSType;
				if (IsArray)
					type += "[]";
				else if ((IsPointer || SymbolTable.Table.IsOpaque (CType)) && type != "string")
					type = "IntPtr";

				return type;
			}
		}

		bool IsPadding {
			get {
				return (CName.StartsWith ("dummy") || CName.StartsWith ("padding"));
			}
		}

		public bool IsPointer {
			get {
				return (CType.EndsWith ("*") || CType.EndsWith ("pointer"));
			}
		}

		public new string Name {
			get {
				string result = "";
				if ((IsPointer || SymbolTable.Table.IsOpaque (CType)) && CSType != "string")
					result = "_";
				result += SymbolTable.Table.MangleName (CName);

				return result;
			}
		}

		string StudlyName {
			get {
				string studly = base.Name;
				if (studly == "")
					throw new Exception ("API file must be regenerated with a current version of the GAPI parser. It is incompatible with this version of the GAPI code generator.");

				return studly;
			}
		}

		public override void Generate (GenerationInfo gen_info, string indent)
		{
			if (Hidden)
				return;

			StreamWriter sw = gen_info.Writer;
			SymbolTable table = SymbolTable.Table;

			string wrapped = table.GetCSType (CType);
			string wrapped_name = SymbolTable.Table.MangleName (CName);
			IGeneratable gen = table [CType];

			if (IsArray) {
				sw.WriteLine (indent + "[MarshalAs (UnmanagedType.ByValArray, SizeConst=" + ArrayLength + ")]");
				sw.WriteLine (indent + "{0} {1} {2};", Access, CSType, StudlyName);
			} else if (IsBitfield) {
				base.Generate (gen_info, indent);
			} else if (gen is IAccessor) {
				sw.WriteLine (indent + "private {0} {1};", gen.MarshalType, Name);

				if (Access != "private") {
					IAccessor acc = table [CType] as IAccessor;
					sw.WriteLine (indent + Access + " " + wrapped + " " + StudlyName + " {");
					acc.WriteAccessors (sw, indent + "\t", Name);
					sw.WriteLine (indent + "}");
				}
			} else if (IsPointer && (gen is StructGen || gen is BoxedGen)) {
				sw.WriteLine (indent + "private {0} {1};", CSType, Name);
				sw.WriteLine ();
				if (Access != "private") {
					sw.WriteLine (indent + Access + " " + wrapped + " " + wrapped_name + " {");
					sw.WriteLine (indent + "\tget { return " + table.FromNativeReturn (CType, Name) + "; }");
					sw.WriteLine (indent + "}");
				}
			} else if (IsPointer && CSType != "string") {
				// FIXME: probably some fields here which should be visible.
				sw.WriteLine (indent + "private {0} {1};", CSType, Name);
			} else {
				sw.WriteLine (indent + "{0} {1} {2};", Access, CSType, Access == "public" ? StudlyName : Name);
			}
		}
	}
}

