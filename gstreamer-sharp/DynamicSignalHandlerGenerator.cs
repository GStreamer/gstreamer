//
// Authors
//    Khaled Mohammed < Khaled.Mohammed@gmail.com >
//
// (C) 2006 Novell
//

using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;

namespace Gst {

public class DynamicSignalHandlerGenerator {

	Type _NewType;

	public Type NewType {
		get { return _NewType; }
		set { _NewType = value; }
	}

	public DynamicSignalHandlerGenerator(Type [] eventParams, string ASSEMBLY_NAME, string classname) {

		// Define the parameters of marshall handler
		Type [] paramTypes = new Type[eventParams.Length + 1];
		for(int i=0; i < paramTypes.Length; i++)
			paramTypes[i] = typeof(IntPtr);


		Type [] paramBeginInvokeTypes = new Type[eventParams.Length + 2];
		for(int i=0; i < paramBeginInvokeTypes.Length - 2 ; i++) 
			paramBeginInvokeTypes[i] = typeof(IntPtr);

		paramBeginInvokeTypes[paramBeginInvokeTypes.Length - 2] = typeof(System.AsyncCallback);
		paramBeginInvokeTypes[paramBeginInvokeTypes.Length - 1] = typeof(object);
	
		
		// defining the assembly
		AssemblyName dynamic_assembly_name = new AssemblyName();
		dynamic_assembly_name.Name = ASSEMBLY_NAME;

		AssemblyBuilder dynamic_assembly = AppDomain.CurrentDomain.DefineDynamicAssembly(
			dynamic_assembly_name, AssemblyBuilderAccess.RunAndSave);

		// create the dll file in the memory
		ModuleBuilder dynamic_module = dynamic_assembly.DefineDynamicModule(ASSEMBLY_NAME, ASSEMBLY_NAME + ".dll");

		TypeBuilder dynamic_class = dynamic_module.DefineType(ASSEMBLY_NAME + "." + classname, TypeAttributes.Public, typeof(Gst.DynamicSignalMarshalHandler));

		MethodBuilder method_CSCB = dynamic_class.DefineMethod("CustomSignalCallback", MethodAttributes.Public | MethodAttributes.HideBySig, typeof(void), paramTypes );
	
		ILGenerator method_IL = method_CSCB.GetILGenerator();
		method_IL.Emit(OpCodes.Ldc_I4, paramTypes.Length);
		method_IL.Emit(OpCodes.Newarr, typeof(IntPtr));

		for(int i=0; i < paramTypes.Length; i++) {
			method_IL.Emit(OpCodes.Dup);
			method_IL.Emit(OpCodes.Ldc_I4, i);
			method_IL.Emit(OpCodes.Ldelema, typeof(IntPtr));
			method_IL.Emit(OpCodes.Ldarg, i+1);
			method_IL.Emit(OpCodes.Stobj, typeof(IntPtr));
		}
		
		method_IL.Emit(OpCodes.Call, typeof(Gst.Element).GetMethod("CustomMarshaller"));	
		method_IL.Emit(OpCodes.Ret);


		// define delegate
		TypeBuilder tbDelegate = dynamic_class.DefineNestedType("CustomSignalDelegate", TypeAttributes.NestedPublic | TypeAttributes.Sealed, typeof(MulticastDelegate), null);

	
		// CustomSignalDelegate()
		ConstructorBuilder constructor = tbDelegate.DefineConstructor(MethodAttributes.Public | MethodAttributes.RTSpecialName | MethodAttributes.SpecialName | MethodAttributes.HideBySig, CallingConventions.Standard, new Type [] { typeof(object), typeof(IntPtr) });
		constructor.SetImplementationFlags (MethodImplAttributes.Runtime);
		//constructor_IL = constructor.GetILGenerator();
		//constructor_IL.Emit(OpCodes.Ret);

		MethodBuilder method = tbDelegate.DefineMethod("Invoke", MethodAttributes.Public | MethodAttributes.Virtual | MethodAttributes.HideBySig , typeof(void), paramTypes);
		method.SetImplementationFlags (MethodImplAttributes.Runtime);

		//   .method public virtual  hidebysig  newslot 
		//    instance default class [mscorlib]System.IAsyncResult BeginInvoke (native int arg0, native int arg1, native int gch, 			// class [mscorlib]System.AsyncCallback callback, object 'object')  runtime managed 
		method = tbDelegate.DefineMethod("BeginInvoke", MethodAttributes.Public | MethodAttributes.Virtual | MethodAttributes.HideBySig | MethodAttributes.NewSlot, typeof(System.IAsyncResult), paramBeginInvokeTypes);
		method.SetImplementationFlags(MethodImplAttributes.Runtime); 

	
		// .method public virtual  hidebysig  newslot 
	        //   instance default void EndInvoke (class [mscorlib]System.IAsyncResult result)  runtime managed 
		method = tbDelegate.DefineMethod("EndInvoke", MethodAttributes.Public | MethodAttributes.HideBySig | MethodAttributes.NewSlot | MethodAttributes.Virtual, typeof(void), new Type [] { typeof(System.IAsyncResult) });
		method.SetImplementationFlags(MethodImplAttributes.Runtime); 

		//  .method public virtual  hidebysig 
	        //   instance default class [mscorlib]System.Delegate Func ()  cil managed 
		method = dynamic_class.DefineMethod("Func",  MethodAttributes.Public | MethodAttributes.Virtual | MethodAttributes.HideBySig , typeof(Delegate), null);

		method_IL = method.GetILGenerator();


		method_IL.Emit(OpCodes.Ldarg_0);
		method_IL.Emit(OpCodes.Ldftn, method_CSCB);
		method_IL.Emit(OpCodes.Newobj, constructor);
		method_IL.Emit(OpCodes.Ret);

		_NewType = dynamic_class.CreateType();

		dynamic_assembly.Save(ASSEMBLY_NAME + ".dll");

	}
}
}
