// GtkSharp.Generation.DefaultSignalHandler.cs - The default signal handler generatable
//
// Author: Christian Hoff <christian_hoff@gmx.net>
//
// Copyright (c) 2008 Novell Inc.
// Copyright (c) 2008-2009 Christian Hoff
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

	public class DefaultSignalHandler : GObjectVM {
		private string signal_name;

		public DefaultSignalHandler (XmlElement elem, ObjectBase container_type) : base (elem, container_type)
		{
			signal_name = elem.GetAttribute ("cname");
		}

		public override string CName {
			get {
				return elem.GetAttribute ("field_name");
			}
		}

		protected override bool CanGenerate (GenerationInfo gen_info, ObjectBase implementor)
		{
			return true;
		}

		protected override void GenerateOverride (GenerationInfo gen_info, ObjectBase implementor)
		{
			StreamWriter sw = gen_info.Writer;

			if (!base.CanGenerate (gen_info, implementor)) {
				GenerateOverrideBody (sw);
				sw.WriteLine ("\t\t\tOverrideVirtualMethod (gtype, \"{0}\", callback);", signal_name);
				sw.WriteLine ("\t\t}");
			} else
				base.GenerateOverride (gen_info, implementor);
		}

		protected override void GenerateUnmanagedInvocation (GenerationInfo gen_info, ObjectBase implementor)
		{
			if (!base.CanGenerate (gen_info, implementor))
				GenerateChainVirtualMethod (gen_info.Writer, implementor);
			else
				base.GenerateUnmanagedInvocation (gen_info, implementor);
		}

		private void GenerateChainVirtualMethod (StreamWriter sw, ObjectBase implementor)
		{
			GenerateMethodBody (sw, implementor);
			if (retval.IsVoid)
				sw.WriteLine ("\t\t\tGLib.Value ret = GLib.Value.Empty;");
			else
				sw.WriteLine ("\t\t\tGLib.Value ret = new GLib.Value (" + ReturnGType + ");");

			sw.WriteLine ("\t\t\tGLib.ValueArray inst_and_params = new GLib.ValueArray (" + (parms.Count + 1) + ");");
			sw.WriteLine ("\t\t\tGLib.Value[] vals = new GLib.Value [" + (parms.Count + 1) + "];");
			sw.WriteLine ("\t\t\tvals [0] = new GLib.Value (this);");
			sw.WriteLine ("\t\t\tinst_and_params.Append (vals [0]);");
			string cleanup = "";
			for (int i = 0; i < parms.Count; i++) {
				Parameter p = parms [i];
				if (p.PassAs != "") {
					if (SymbolTable.Table.IsBoxed (p.CType)) {
						if (p.PassAs == "ref")
							sw.WriteLine ("\t\t\tvals [" + (i + 1) + "] = new GLib.Value (" + p.Name + ");");
						else
							sw.WriteLine ("\t\t\tvals [" + (i + 1) + "] = new GLib.Value ((GLib.GType)typeof (" + p.CSType + "));");
						cleanup += "\t\t\t" + p.Name + " = (" + p.CSType + ") vals [" + i + "];\n";
					} else {
						if (p.PassAs == "ref")
							sw.WriteLine ("\t\t\tIntPtr " + p.Name + "_ptr = GLib.Marshaller.StructureToPtrAlloc (" + p.Generatable.CallByName (p.Name) + ");");
						else
							sw.WriteLine ("\t\t\tIntPtr " + p.Name + "_ptr = Marshal.AllocHGlobal (Marshal.SizeOf (typeof (" + p.MarshalType + ")));");

						sw.WriteLine ("\t\t\tvals [" + (i + 1) + "] = new GLib.Value (" + p.Name + "_ptr);");
						cleanup += "\t\t\t" + p.Name + " = " + p.FromNative ("(" + p.MarshalType + ") Marshal.PtrToStructure (" + p.Name + "_ptr, typeof (" + p.MarshalType + "))") + ";\n";
						cleanup += "\t\t\tMarshal.FreeHGlobal (" + p.Name + "_ptr);\n";
					}
				} else if (p.IsLength && i > 0 && parms [i - 1].IsString)
					sw.WriteLine ("\t\t\tvals [" + (i + 1) + "] = new GLib.Value (System.Text.Encoding.UTF8.GetByteCount (" + parms [i-1].Name + "));");
				else
					sw.WriteLine ("\t\t\tvals [" + (i + 1) + "] = new GLib.Value (" + p.Name + ");");

				sw.WriteLine ("\t\t\tinst_and_params.Append (vals [" + (i + 1) + "]);");
			}

			sw.WriteLine ("\t\t\tg_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);");
			if (cleanup != "")
				sw.WriteLine (cleanup);
			sw.WriteLine ("\t\t\tforeach (GLib.Value v in vals)");
			sw.WriteLine ("\t\t\t\tv.Dispose ();");
			if (!retval.IsVoid) {
				IGeneratable igen = SymbolTable.Table [retval.CType];
				sw.WriteLine ("\t\t\t" + retval.CSType + " result = (" + (igen is EnumGen ? retval.CSType + ") (Enum" : retval.CSType) + ") ret;");
				sw.WriteLine ("\t\t\tret.Dispose ();");
				sw.WriteLine ("\t\t\treturn result;");
			}
			sw.WriteLine ("\t\t}\n");
		}

		private string ReturnGType {
			get {
				IGeneratable igen = SymbolTable.Table [retval.CType];

				if (igen is ObjectGen)
					return "GLib.GType.Object";
				if (igen is BoxedGen)
					return retval.CSType + ".GType";
				if (igen is EnumGen)
					return retval.CSType + "GType.GType";

				switch (retval.CSType) {
				case "bool":
					return "GLib.GType.Boolean";
				case "string":
					return "GLib.GType.String";
				case "int":
					return "GLib.GType.Int";
				default:
					throw new Exception (retval.CSType);
				}
			}
		}
	 }
}

