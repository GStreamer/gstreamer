// GtkSharp.Generation.InterfaceGen.cs - The Interface Generatable.
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001-2003 Mike Kestner
// Copyright (c) 2004, 2007 Novell, Inc.
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

	public class InterfaceGen : ObjectBase {

		bool consume_only;

		public InterfaceGen (XmlElement ns, XmlElement elem) : base (ns, elem, true) 
		{
			consume_only = elem.HasAttribute ("consume_only");
			foreach (XmlNode node in elem.ChildNodes) {
				if (!(node is XmlElement)) continue;
				XmlElement member = (XmlElement) node;

				switch (member.Name) {
				case "signal":
					object sig = sigs [member.GetAttribute ("name")];
					if (sig == null)
						sig = new Signal (node as XmlElement, this);
					break;
				default:
					if (!base.IsNodeNameHandled (node.Name))
						Console.WriteLine ("Unexpected node " + node.Name + " in " + CName);
					break;
				}
			}
		}

		public bool IsConsumeOnly {
			get {
				return consume_only;
			}
		}

		public override string CallByName (string var, bool owned)
		{
			return String.Format ("{0} == null ? IntPtr.Zero : (({0} is GLib.Object) ? ({0} as GLib.Object).{1} : ({0} as {2}Adapter).{1})", var, owned ? "OwnedHandle" : "Handle", QualifiedName);
		}

		public override string FromNative (string var, bool owned)
		{
			return QualifiedName + "Adapter.GetObject (" + var + ", " + (owned ? "true" : "false") + ")";
		}

		public override bool ValidateForSubclass ()
		{
			ArrayList invalids = new ArrayList ();

			foreach (Method method in methods.Values) {
				if (!method.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (method);
				}
			}
			foreach (Method method in invalids)
				methods.Remove (method.Name);
			invalids.Clear ();

			return base.ValidateForSubclass ();
		}

		void GenerateStaticCtor (StreamWriter sw)
		{
			sw.WriteLine ("\t\tstatic {0} iface;", class_struct_name);
			sw.WriteLine ();
			sw.WriteLine ("\t\tstatic " + Name + "Adapter ()");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tGLib.GType.Register (_gtype, typeof({0}Adapter));", Name);
			foreach (InterfaceVM vm in interface_vms) {
				if (vm.IsValid)
					sw.WriteLine ("\t\t\tiface.{0} = new {0}NativeDelegate ({0}_cb);", vm.Name);
			}
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateInitialize (StreamWriter sw)
		{
			sw.WriteLine ("\t\tstatic int class_offset = 2 * IntPtr.Size;"); // Class size of GTypeInterface struct
			sw.WriteLine ();
			sw.WriteLine ("\t\tstatic void Initialize (IntPtr ptr, IntPtr data)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tIntPtr ifaceptr = new IntPtr (ptr.ToInt64 () + class_offset);");
			sw.WriteLine ("\t\t\t{0} native_iface = ({0}) Marshal.PtrToStructure (ifaceptr, typeof ({0}));", class_struct_name);
			foreach (InterfaceVM vm in interface_vms)
				sw.WriteLine ("\t\t\tnative_iface." + vm.Name + " = iface." + vm.Name + ";");
			sw.WriteLine ("\t\t\tMarshal.StructureToPtr (native_iface, ifaceptr, false);");
			sw.WriteLine ("\t\t\tGCHandle gch = (GCHandle) data;");
			sw.WriteLine ("\t\t\tgch.Free ();");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateCallbacks (StreamWriter sw)
		{
			foreach (InterfaceVM vm in interface_vms) {
				vm.GenerateCallback (sw, null);
				}
			}

		void GenerateCtors (StreamWriter sw)
		{
			// Native GObjects do not implement the *Implementor interfaces
			sw.WriteLine ("\t\tGLib.Object implementor;", Name);
			sw.WriteLine ();

			if (!IsConsumeOnly) {
				sw.WriteLine ("\t\tpublic " + Name + "Adapter ()");
				sw.WriteLine ("\t\t{");
				sw.WriteLine ("\t\t\tInitHandler = new GLib.GInterfaceInitHandler (Initialize);");
				sw.WriteLine ("\t\t}");
				sw.WriteLine ();
				sw.WriteLine ("\t\tpublic {0}Adapter ({0}Implementor implementor)", Name);
				sw.WriteLine ("\t\t{");
				sw.WriteLine ("\t\t\tif (implementor == null)");
				sw.WriteLine ("\t\t\t\tthrow new ArgumentNullException (\"implementor\");");
				sw.WriteLine ("\t\t\telse if (!(implementor is GLib.Object))");
				sw.WriteLine ("\t\t\t\tthrow new ArgumentException (\"implementor must be a subclass of GLib.Object\");");
				sw.WriteLine ("\t\t\tthis.implementor = implementor as GLib.Object;");
				sw.WriteLine ("\t\t}");
				sw.WriteLine ();
			}

			sw.WriteLine ("\t\tpublic " + Name + "Adapter (IntPtr handle)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tif (!_gtype.IsInstance (handle))");
			sw.WriteLine ("\t\t\t\tthrow new ArgumentException (\"The gobject doesn't implement the GInterface of this adapter\", \"handle\");");
			sw.WriteLine ("\t\t\timplementor = GLib.Object.GetObject (handle);");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateGType (StreamWriter sw)
		{
			Method m = GetMethod ("GetType");
			m.GenerateImport (sw);
			sw.WriteLine ("\t\tprivate static GLib.GType _gtype = new GLib.GType ({0} ());", m.CName);
			sw.WriteLine ();
			sw.WriteLine ("\t\tpublic override GLib.GType GType {");
			sw.WriteLine ("\t\t\tget {");
			sw.WriteLine ("\t\t\t\treturn _gtype;");
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateHandleProp (StreamWriter sw)
		{
			sw.WriteLine ("\t\tpublic override IntPtr Handle {");
			sw.WriteLine ("\t\t\tget {");
			sw.WriteLine ("\t\t\t\treturn implementor.Handle;");
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tpublic IntPtr OwnedHandle {");
			sw.WriteLine ("\t\t\tget {");
			sw.WriteLine ("\t\t\t\treturn implementor.OwnedHandle;");
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateGetObject (StreamWriter sw)
		{
			sw.WriteLine ("\t\tpublic static " + Name + " GetObject (IntPtr handle, bool owned)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tGLib.Object obj = GLib.Object.GetObject (handle, owned);");
			sw.WriteLine ("\t\t\treturn GetObject (obj);");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
			sw.WriteLine ("\t\tpublic static " + Name + " GetObject (GLib.Object obj)");
			sw.WriteLine ("\t\t{");
			sw.WriteLine ("\t\t\tif (obj == null)");
			sw.WriteLine ("\t\t\t\treturn null;");
			if (!IsConsumeOnly) {
				sw.WriteLine ("\t\t\telse if (obj is " + Name + "Implementor)");
				sw.WriteLine ("\t\t\t\treturn new {0}Adapter (obj as {0}Implementor);", Name);
			}
			sw.WriteLine ("\t\t\telse if (obj as " + Name + " == null)");
			sw.WriteLine ("\t\t\t\treturn new {0}Adapter (obj.Handle);", Name);
			sw.WriteLine ("\t\t\telse");
			sw.WriteLine ("\t\t\t\treturn obj as {0};", Name);
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateImplementorProp (StreamWriter sw)
		{
			sw.WriteLine ("\t\tpublic " + Name + "Implementor Implementor {");
			sw.WriteLine ("\t\t\tget {");
			sw.WriteLine ("\t\t\t\treturn implementor as {0}Implementor;", Name);
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		void GenerateAdapter (GenerationInfo gen_info)
		{
			StreamWriter sw = gen_info.Writer = gen_info.OpenStream (Name + "Adapter");

			sw.WriteLine ("namespace " + NS + " {");
			sw.WriteLine ();
			sw.WriteLine ("\tusing System;");
			sw.WriteLine ("\tusing System.Runtime.InteropServices;");
			sw.WriteLine ();
			sw.WriteLine ("#region Autogenerated code");
			sw.WriteLine ("\tpublic class " + Name + "Adapter : GLib.GInterfaceAdapter, " + QualifiedName + " {");
			sw.WriteLine ();

			if (!IsConsumeOnly) {
				GenerateClassStruct (gen_info);
				GenerateStaticCtor (sw);
				GenerateCallbacks (sw);
				GenerateInitialize (sw);
			}
			GenerateCtors (sw);
			GenerateGType (sw);
			GenerateHandleProp (sw);
			GenerateGetObject (sw);
			if (!IsConsumeOnly)
				GenerateImplementorProp (sw);

			GenProperties (gen_info, null);

			foreach (Signal sig in sigs.Values)
				sig.GenEvent (sw, null, "GLib.Object.GetObject (Handle)");

			Method temp = methods ["GetType"] as Method;
			if (temp != null)
				methods.Remove ("GetType");
			GenMethods (gen_info, new Hashtable (), this);
			if (temp != null)
				methods ["GetType"] = temp;

			sw.WriteLine ("#endregion");

			AppendCustom (sw, gen_info.CustomDir, Name + "Adapter");

			sw.WriteLine ("\t}");
			sw.WriteLine ("}");
			sw.Close ();
			gen_info.Writer = null;
		}

		void GenerateImplementorIface (GenerationInfo gen_info)
		{
			StreamWriter sw = gen_info.Writer;
			if (IsConsumeOnly)
				return;

			sw.WriteLine ();
			sw.WriteLine ("\t[GLib.GInterface (typeof (" + Name + "Adapter))]");
			string access = IsInternal ? "internal" : "public";
			sw.WriteLine ("\t" + access + " interface " + Name + "Implementor : GLib.IWrapper {");
			sw.WriteLine ();
			Hashtable vm_table = new Hashtable ();
			foreach (InterfaceVM vm in interface_vms) {
				vm_table [vm.Name] = vm;
			}
			foreach (InterfaceVM vm in interface_vms) {
				if (vm_table [vm.Name] == null)
					continue;
				else if (!vm.IsValid) {
					vm_table.Remove (vm.Name);
					continue;
				} else if (vm.IsGetter || vm.IsSetter) {
					string cmp_name = (vm.IsGetter ? "Set" : "Get") + vm.Name.Substring (3);
					InterfaceVM cmp = vm_table [cmp_name] as InterfaceVM;
					if (cmp != null && (cmp.IsGetter || cmp.IsSetter)) {
						if (vm.IsSetter)
							cmp.GenerateDeclaration (sw, vm);
						else
							vm.GenerateDeclaration (sw, cmp);
						vm_table.Remove (cmp.Name);
					} else 
						vm.GenerateDeclaration (sw, null);
					vm_table.Remove (vm.Name);
				} else {
					vm.GenerateDeclaration (sw, null);
					vm_table.Remove (vm.Name);
				}
			}

			AppendCustom (sw, gen_info.CustomDir, Name + "Implementor");

			sw.WriteLine ("\t}");
		}

		public override void Generate (GenerationInfo gen_info)
		{
			GenerateAdapter (gen_info);
			StreamWriter sw = gen_info.Writer = gen_info.OpenStream (Name);

			sw.WriteLine ("namespace " + NS + " {");
			sw.WriteLine ();
			sw.WriteLine ("\tusing System;");
			sw.WriteLine ();
			sw.WriteLine ("#region Autogenerated code");
			string access = IsInternal ? "internal" : "public";
			sw.WriteLine ("\t" + access + " interface " + Name + " : GLib.IWrapper {");
			sw.WriteLine ();
			
			foreach (Signal sig in sigs.Values) {
				sig.GenerateDecl (sw);
				sig.GenEventHandler (gen_info);
			}

			foreach (Method method in methods.Values) {
				if (IgnoreMethod (method, this))
					continue;
				method.GenerateDecl (sw);
			}

			foreach (Property prop in props.Values)
				prop.GenerateDecl (sw, "\t\t");

			AppendCustom (sw, gen_info.CustomDir);

			sw.WriteLine ("\t}");
			GenerateImplementorIface (gen_info);
			sw.WriteLine ("#endregion");
			sw.WriteLine ("}");
			sw.Close ();
			gen_info.Writer = null;
			Statistics.IFaceCount++;
		}
	}
}

