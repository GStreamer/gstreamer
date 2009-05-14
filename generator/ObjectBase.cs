// ObjectBase.cs - Base class for Object types
//
// Authors:  Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2005 Novell, Inc.
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

	public abstract class ObjectBase : HandleBase {
		bool is_interface;
		protected string class_struct_name = null;
		bool class_fields_valid; // false if the class structure contains a bitfield or fields of unknown types
		ArrayList class_members = new ArrayList ();
		protected ArrayList class_fields = new ArrayList ();
		// The default handlers of these signals need to be overridden with g_signal_override_class_closure
		protected ArrayList virtual_methods = new ArrayList ();
		// virtual methods that are generated as an IntPtr in the class struct
		protected ArrayList hidden_vms = new ArrayList ();
		protected ArrayList interface_vms = new ArrayList ();
		protected Hashtable sigs = new Hashtable();

		protected ObjectBase (XmlElement ns, XmlElement elem, bool is_interface) : base (ns, elem) 
		{
			this.is_interface = is_interface;
			XmlElement class_elem = null;
			Hashtable vms = new Hashtable ();
			Hashtable signal_vms = new Hashtable ();

			if (this.ParserVersion == 1)
				class_struct_name = this.CName + (is_interface ? "Iface" : "Class");
					
			foreach (XmlNode node in elem.ChildNodes) {
				if (!(node is XmlElement)) continue;
				XmlElement member = node as XmlElement;

				switch (node.Name) {
				case "virtual_method":
					if (this.ParserVersion == 1) {
						if (is_interface) // Generating non-signal GObject virtual methods is not supported in compatibility mode
							AddVM (member, false, is_interface);
					} else
						vms.Add (member.GetAttribute ("cname"), member);
					break;

				case "signal":
					if (this.ParserVersion == 1 || member.GetAttribute ("field_name") == "")
						AddVM (member, true, is_interface);
					else
						signal_vms.Add (member.GetAttribute ("field_name"), member);

					if (member.GetAttribute ("hidden") != "1") {
						string name = member.GetAttribute("name");
						while (sigs.ContainsKey(name))
							name += "mangled";
						sigs.Add (name, new Signal (member, this));
					}
					break;

				case "class_struct":
					class_elem = member;
					break;
				}
			}
				
			if (class_elem == null) return;
			class_struct_name = class_elem.GetAttribute ("cname");

			for (int node_idx = 0; node_idx < class_elem.ChildNodes.Count; node_idx++) {
				XmlNode node = class_elem.ChildNodes [node_idx];
				if (!(node is XmlElement)) continue;
				XmlElement member = (XmlElement) node;

				switch (member.Name) {
				case "method":
					string vm_name;
					XmlElement vm_elem;
					bool is_signal_vm = member.HasAttribute ("signal_vm");
					if (is_signal_vm) {
						vm_name = member.GetAttribute ("signal_vm");
						vm_elem = signal_vms [vm_name] as XmlElement;
					} else {
						vm_name = member.GetAttribute ("vm");
						vm_elem = vms [vm_name] as XmlElement;
					}

					AddVM (vm_elem, is_signal_vm, is_interface);
					break;
				case "field":
					if (node_idx == 0) continue; // Parent class
					ClassField field = new ClassField (member, this);
					class_fields.Add (field);
					class_members.Add (field);
					break;
				default:
					Console.WriteLine ("Unexpected node " + member.Name + " in " + class_elem.GetAttribute ("cname"));
					break;
				}
			}
		}

		VirtualMethod AddVM (XmlElement vm_elem, bool is_signal_vm, bool is_interface)
		{
			VirtualMethod vm;
			if (is_signal_vm)
				vm = new DefaultSignalHandler (vm_elem, this);
			else if (is_interface)
				vm = new InterfaceVM (vm_elem, methods [vm_elem.GetAttribute ("name")] as Method, this);
			else
				vm = new GObjectVM (vm_elem, this);

			if (vm_elem.GetAttribute ("padding") == "true" || vm_elem.GetAttribute ("hidden") == "1")
				hidden_vms.Add (vm);
			else {
				if (vm is GObjectVM)
					virtual_methods.Add (vm);
				else 
					interface_vms.Add (vm);
			}
			if (vm.CName != "")
				class_members.Add (vm);

			return vm;
		}

		protected override bool IsNodeNameHandled (string name)
		{
			switch (name) {
			case "virtual_method":
			case "signal":
			case "class_struct":
				return true;
			default:
				return base.IsNodeNameHandled (name);
			}
		}
					
		public override string FromNative (string var, bool owned)
		{
			return "GLib.Object.GetObject(" + var + (owned ? ", true" : "") + ") as " + QualifiedName;
		}

		public string ClassStructName {
			get {
				return class_struct_name;
			}
		}

		public bool CanGenerateClassStruct {
			get {
				/* Generation of interface class structs was already supported by version 2.12 of the GAPI parser. Their layout was determined by the order
				* in which the signal and virtual_method elements appeared in the XML. However, we cannot use that approach for old GObject class structs 
				* as they may contain class fields which don't appear in the old (version 1) API files. There are also cases in which the order of the
				* <signal> and <virtual_method> elements do not match the struct layout.
				*/
				return (is_interface || this.ParserVersion >= 2) && class_fields_valid;
			}
		}

		protected void GenerateClassStruct (GenerationInfo gen_info)
		{
			if (class_struct_name == null || !CanGenerateClassStruct) return;

			StreamWriter sw = gen_info.Writer;

			sw.WriteLine ("\t\t[StructLayout (LayoutKind.Sequential)]");
			sw.WriteLine ("\t\tstruct " + class_struct_name + " {");
			foreach (object member in class_members) {
				if (member is VirtualMethod) {
					VirtualMethod vm = member as VirtualMethod;
					if (hidden_vms.Contains (vm) || (is_interface && vm is DefaultSignalHandler))
						sw.WriteLine ("\t\t\tIntPtr {0};", vm.Name);
					else
						sw.WriteLine ("\t\t\tpublic {0}NativeDelegate {0};", vm.Name);
				} else if (member is ClassField) {
					ClassField field = member as ClassField;
					field.Generate (gen_info, "\t\t\t");
				}
			}
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		public Hashtable Signals {
			get {
				return sigs;
			}
		}

		public Signal GetSignal (string name)
		{
			return sigs[name] as Signal;
		}

		public Signal GetSignalRecursively (string name)
		{
			return GetSignalRecursively (name, false);
		}

		public virtual Signal GetSignalRecursively (string name, bool check_self)
		{
			Signal p = null;
			if (check_self)
				p = GetSignal (name);
			if (p == null && Parent != null) 
				p = (Parent as ObjectBase).GetSignalRecursively (name, true);
			
			if (check_self && p == null) {
				foreach (string iface in interfaces) {
					InterfaceGen igen = SymbolTable.Table.GetClassGen (iface) as InterfaceGen;
					if (igen == null)
						continue;
					p = igen.GetSignalRecursively (name, true);
					if (p != null)
						break;
				}
			}

			return p;
		}

		public void GenSignals (GenerationInfo gen_info, ObjectBase implementor)
		{
			foreach (Signal sig in sigs.Values)
				sig.Generate (gen_info, implementor);
		}

		public void GenVirtualMethods (GenerationInfo gen_info, ObjectBase implementor)
		{
			foreach (GObjectVM vm in virtual_methods)
				vm.Generate (gen_info, implementor);
		}

		public override bool Validate ()
		{
			if (Parent != null && !(Parent as ObjectBase).ValidateForSubclass ())
				return false;

			ArrayList invalids = new ArrayList ();

			foreach (GObjectVM vm in virtual_methods)
				if (!vm.Validate ())
					invalids.Add (vm);

			foreach (VirtualMethod invalid_vm in invalids) {
				virtual_methods.Remove (invalid_vm);
				hidden_vms.Add (invalid_vm);
			}
			invalids.Clear ();

			class_fields_valid = true;
			foreach (ClassField field in class_fields)
				if (!field.Validate ())
					class_fields_valid = false;
			
			foreach (InterfaceVM vm in interface_vms)
				if (!vm.Validate ())
					invalids.Add (vm);

			foreach (InterfaceVM invalid_vm in invalids) {
				interface_vms.Remove (invalid_vm);
				hidden_vms.Add (invalid_vm);
			}
			invalids.Clear ();

			foreach (Signal sig in sigs.Values) {
				if (!sig.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (sig);
				}
			}
			foreach (Signal sig in invalids)
				sigs.Remove (sig.Name);

			return base.Validate ();
		}

		public virtual bool ValidateForSubclass ()
		{
			ArrayList invalids = new ArrayList ();

			foreach (Signal sig in sigs.Values) {
				if (!sig.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (sig);
				}
			}
			foreach (Signal sig in invalids)
				sigs.Remove (sig.Name);
			invalids.Clear ();

			return true;
		}
	}
}
