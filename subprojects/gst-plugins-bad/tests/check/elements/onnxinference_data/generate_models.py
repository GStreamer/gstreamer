#!/usr/bin/env python3

import os
import sys
import onnx
from onnx import helper, TensorProto
import onnxruntime as ort

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def dtype_to_modelinfo(onnx_dtype):
    """Map ONNX/ONNXRuntime dtype to modelinfo string."""
    dtype_map = {
        TensorProto.FLOAT: "float32",
        TensorProto.INT32: "int32",
        TensorProto.UINT8: "uint8",
        TensorProto.INT8: "int8",
        TensorProto.DOUBLE: "float64",
        "tensor(float)": "float32",
        "tensor(int32)": "int32",
        "tensor(uint8)": "uint8",
        "tensor(int8)": "int8",
        "tensor(double)": "float64",
    }
    return dtype_map.get(onnx_dtype, str(onnx_dtype))


def dims_to_str(shape):
    """Convert shape list to modelinfo dims string (-1 for dynamic)."""
    return ",".join("-1" if (v is None or v < 0) else str(v) for v in shape)


def write_modelinfo(
    onnx_path, per_output_dims_order=None, version="1.1", input_caps=None,
    input_ranges="0.0,255.0;0.0,255.0;0.0,255.0",
):
    """
    Introspect ONNX model and generate .modelinfo file.
    Uses onnxruntime to extract tensor info.
    """
    if per_output_dims_order is None:
        per_output_dims_order = {}
    if version == "1.1" and input_caps is None:
        raise ValueError("Version 1.1 modelinfo requires explicit input caps")
    if version == "1.0" and input_caps is not None:
        raise ValueError("Version 1.0 modelinfo does not support input caps")

    session = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
    input_names = [inp.name for inp in session.get_inputs()]
    input_types = [inp.type for inp in session.get_inputs()]
    input_shapes = [inp.shape for inp in session.get_inputs()]

    output_names = [out.name for out in session.get_outputs()]
    output_types = [out.type for out in session.get_outputs()]
    output_shapes = [out.shape for out in session.get_outputs()]

    group_id = os.path.basename(onnx_path).replace(".onnx", "") + "-group"
    lines = [
        "# This file is generated. Modify generate_models.py instead.",
        "",
        "[modelinfo]",
        f"version={version}",
        f"group-id={group_id}",
        "",
    ]

    # Input section (required for onnxinference)
    for i, (name, onnx_type, shape) in enumerate(zip(input_names, input_types, input_shapes)):
        dtype_str = dtype_to_modelinfo(onnx_type)
        dims_str = dims_to_str(shape)
        lines += [
            f"[{name}]",
            f"id=input-{i}",
            f"type={dtype_str}",
            f"dims={dims_str}",
            "dir=input",
        ]
        if input_caps is not None:
            lines.append(f"caps={input_caps}")
        lines += [f"ranges={input_ranges}", ""]

    # Output section
    for i, (name, onnx_type, shape) in enumerate(zip(output_names, output_types, output_shapes)):
        dtype_str = dtype_to_modelinfo(onnx_type)
        dims_str = dims_to_str(shape)
        lines += [
            f"[{name}]",
            f"id=output-{i}",
            f"type={dtype_str}",
            f"dims={dims_str}",
            "dir=output",
        ]
        if i in per_output_dims_order:
            lines.append(f"dims-order={per_output_dims_order[i]}")
        lines.append("")

    modelinfo_path = onnx_path + ".modelinfo"
    with open(modelinfo_path, "w") as f:
        f.write("\n".join(lines).rstrip() + "\n")


def make_model_flatten_f32_f32():
    """[1,4,4,3] float32 → reshape → [1,48] float32"""
    input_name = "input_f32"
    output_name = "output_flat_f32"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 48]
    )

    reshape_node = helper.make_node(
        "Reshape",
        inputs=[input_name, "shape_const"],
        outputs=[output_name],
    )

    shape_const = helper.make_tensor(
        name="shape_const",
        data_type=TensorProto.INT64,
        dims=[2],
        vals=[1, 48]
    )

    graph = helper.make_graph(
        [reshape_node],
        "flatten_f32_f32",
        [input_tensor],
        [output_tensor],
        [shape_const]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    model.ir_version = 10
    return model


def make_model_uint8_to_f32():
    """[1,4,4,3] uint8 → cast float32 → reshape → [1,48] float32"""
    input_name = "input_u8"
    cast_output = "cast_out"
    output_name = "output_flat_f32"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.UINT8, [1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 48]
    )

    cast_node = helper.make_node(
        "Cast",
        inputs=[input_name],
        outputs=[cast_output],
        to=TensorProto.FLOAT
    )

    reshape_node = helper.make_node(
        "Reshape",
        inputs=[cast_output, "shape_const"],
        outputs=[output_name],
    )

    shape_const = helper.make_tensor(
        name="shape_const",
        data_type=TensorProto.INT64,
        dims=[2],
        vals=[1, 48]
    )

    graph = helper.make_graph(
        [cast_node, reshape_node],
        "uint8_to_f32",
        [input_tensor],
        [output_tensor],
        [shape_const]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_flatten_gray():
    input_tensor = helper.make_tensor_value_info(
        "input_gray", TensorProto.FLOAT, [1, 1, 4, 4]
    )
    output_tensor = helper.make_tensor_value_info(
        "output_flat", TensorProto.FLOAT, [1, 16]
    )
    flatten_node = helper.make_node(
        "Flatten", inputs=["input_gray"], outputs=["flattened"], axis=1
    )
    reshape_node = helper.make_node(
        "Reshape", inputs=["flattened", "shape"], outputs=["output_flat"]
    )
    shape_const = helper.make_tensor(
        "shape", TensorProto.INT64, [2], [1, 16]
    )
    graph = helper.make_graph(
        [flatten_node, reshape_node], "flatten_gray",
        [input_tensor], [output_tensor], [shape_const]
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    model.ir_version = 7
    return model


def make_model_int32_out():
    """[1,4,4,3] float32 → cast int32 → [1,4,4,3] int32"""
    input_name = "input_f32"
    output_name = "output_i32"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.INT32, [1, 4, 4, 3]
    )

    cast_node = helper.make_node(
        "Cast",
        inputs=[input_name],
        outputs=[output_name],
        to=TensorProto.INT32
    )

    graph = helper.make_graph(
        [cast_node],
        "int32_out",
        [input_tensor],
        [output_tensor]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_dynamic_batch():
    """[-1,4,4,3] float32 → reshape → [-1,48] float32"""
    input_name = "input_dyn"
    output_name = "output_dyn"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [-1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [-1, 48]
    )

    # Shape computation: [-1, 48]
    reshape_node = helper.make_node(
        "Reshape",
        inputs=[input_name, "shape_const"],
        outputs=[output_name],
    )

    shape_const = helper.make_tensor(
        name="shape_const",
        data_type=TensorProto.INT64,
        dims=[2],
        vals=[-1, 48]
    )

    graph = helper.make_graph(
        [reshape_node],
        "dynamic_batch",
        [input_tensor],
        [output_tensor],
        [shape_const]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_multi_output():
    """[1,4,4,3] float32 → two outputs: [1,48] and [1,3,16]"""
    input_name = "input_f32"
    out1_name = "out1_flat"      # [1,48] row-major
    out0_name = "out0_chw"       # [1,3,16] col-major (from transpose+reshape)

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    out1_tensor = helper.make_tensor_value_info(
        out1_name, TensorProto.FLOAT, [1, 48]
    )
    out0_tensor = helper.make_tensor_value_info(
        out0_name, TensorProto.FLOAT, [1, 3, 16]
    )

    # Output 1: flatten to [1,48]
    reshape1 = helper.make_node(
        "Reshape",
        inputs=[input_name, "shape1"],
        outputs=[out1_name],
    )

    # Output 0: transpose to [1,3,4,4], then reshape to [1,3,16]
    transpose = helper.make_node(
        "Transpose",
        inputs=[input_name],
        outputs=["transpose_out"],
        perm=[0, 3, 1, 2]  # BHWC -> BCHW
    )

    reshape0 = helper.make_node(
        "Reshape",
        inputs=["transpose_out", "shape0"],
        outputs=[out0_name],
    )

    shape1_const = helper.make_tensor("shape1", TensorProto.INT64, [2], [1, 48])
    shape0_const = helper.make_tensor("shape0", TensorProto.INT64, [3], [1, 3, 16])

    graph = helper.make_graph(
        [reshape1, transpose, reshape0],
        "multi_output",
        [input_tensor],
        [out1_tensor, out0_tensor],
        [shape1_const, shape0_const]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_3d_input():
    """[3,4,4] float32 → reshape → [3,16] float32"""
    input_name = "input_3d"
    output_name = "output_3d"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [3, 4, 4]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [3, 16]
    )

    reshape_node = helper.make_node(
        "Reshape",
        inputs=[input_name, "shape_const"],
        outputs=[output_name],
    )

    shape_const = helper.make_tensor(
        name="shape_const",
        data_type=TensorProto.INT64,
        dims=[2],
        vals=[3, 16]
    )

    graph = helper.make_graph(
        [reshape_node],
        "3d_input",
        [input_tensor],
        [output_tensor],
        [shape_const]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_planar_chw():
    """[1,3,4,4] float32 → identity → [1,3,4,4] float32 (planar CHW layout)"""
    input_name = "input_chw"
    output_name = "output_chw"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 3, 4, 4]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 3, 4, 4]
    )

    identity = helper.make_node(
        "Identity",
        inputs=[input_name],
        outputs=[output_name],
    )

    graph = helper.make_graph(
        [identity],
        "planar_chw",
        [input_tensor],
        [output_tensor]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_invalid_1d():
    """[48] float32 → invalid 1D input"""
    input_name = "input_1d"
    output_name = "output_1d"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [48]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [48]
    )

    identity = helper.make_node("Identity", inputs=[input_name], outputs=[output_name])

    graph = helper.make_graph([identity], "invalid_1d", [input_tensor], [output_tensor])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_invalid_5d():
    """[1,2,2,2,3] float32 → invalid 5D input"""
    input_name = "input_5d"
    output_name = "output_5d"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 2, 2, 2, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 2, 2, 2, 3]
    )

    identity = helper.make_node("Identity", inputs=[input_name], outputs=[output_name])

    graph = helper.make_graph([identity], "invalid_5d", [input_tensor], [output_tensor])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_invalid_channels_2():
    """[1,4,4,2] float32 → invalid 2-channel input"""
    input_name = "input_ch2"
    output_name = "output_ch2"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 4, 4, 2]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 4, 4, 2]
    )

    identity = helper.make_node("Identity", inputs=[input_name], outputs=[output_name])

    graph = helper.make_graph([identity], "invalid_ch2", [input_tensor], [output_tensor])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_multi_input():
    """Two [1,4,4,3] float32 inputs → Add → [1,4,4,3] output (invalid for onnxinference)"""
    input0_name = "input0"
    input1_name = "input1"
    output_name = "output_add"

    input0_tensor = helper.make_tensor_value_info(
        input0_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    input1_tensor = helper.make_tensor_value_info(
        input1_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )

    add_node = helper.make_node(
        "Add",
        inputs=[input0_name, input1_name],
        outputs=[output_name],
    )

    graph = helper.make_graph(
        [add_node],
        "multi_input",
        [input0_tensor, input1_tensor],
        [output_tensor]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_uint8_out():
    """[1,4,4,3] float32 → Cast UINT8 (unsupported output type)"""
    input_name = "input_f32"
    output_name = "output_u8"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.UINT8, [1, 4, 4, 3]
    )

    cast_node = helper.make_node(
        "Cast",
        inputs=[input_name],
        outputs=[output_name],
        to=TensorProto.UINT8
    )

    graph = helper.make_graph(
        [cast_node],
        "uint8_out",
        [input_tensor],
        [output_tensor]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def make_model_float64_out():
    """[1,4,4,3] float32 → Cast FLOAT64 (unsupported output type)"""
    input_name = "input_f32"
    output_name = "output_f64"

    input_tensor = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, [1, 4, 4, 3]
    )
    output_tensor = helper.make_tensor_value_info(
        output_name, TensorProto.DOUBLE, [1, 4, 4, 3]
    )

    cast_node = helper.make_node(
        "Cast",
        inputs=[input_name],
        outputs=[output_name],
        to=TensorProto.DOUBLE
    )

    graph = helper.make_graph(
        [cast_node],
        "float64_out",
        [input_tensor],
        [output_tensor]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    model.ir_version = 10
    return model


def main():
    # Clean old models
    for f in os.listdir(BASE_DIR):
        if f.endswith(('.onnx', '.modelinfo')):
            os.remove(os.path.join(BASE_DIR, f))

    # Valid models
    models = [
        ("flatten_float32in_float32out.onnx", make_model_flatten_f32_f32(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
        ("flatten_uint8in_float32out.onnx", make_model_uint8_to_f32(),
         {"input_caps": "video/x-raw, format=RGB"}),
        ("flatten_gray.onnx", make_model_flatten_gray(),
         {"version": "1.0", "input_ranges": "0.0,255.0"}),
        ("int32out.onnx", make_model_int32_out(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
        ("dynamic_batch.onnx", make_model_dynamic_batch(),
         {"version": "1.0"}),
        ("multi_output.onnx", make_model_multi_output(),
         {"input_caps": "video/x-raw, format=RGB_F32LE",
          "per_output_dims_order": {1: "col-major"}}),
        ("flatten_3d_float32.onnx", make_model_3d_input(),
         {"version": "1.0"}),
        ("planar_chw.onnx", make_model_planar_chw(),
         {"version": "1.0"}),
        ("invalid_dims_1d.onnx", make_model_invalid_1d(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
        ("invalid_dims_5d.onnx", make_model_invalid_5d(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
        ("invalid_dims_channels_2.onnx", make_model_invalid_channels_2(),
         {"version": "1.0"}),
        ("multi_input_two_tensors.onnx", make_model_multi_input(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
        ("uint8out.onnx", make_model_uint8_out(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
        ("float64out.onnx", make_model_float64_out(),
         {"input_caps": "video/x-raw, format=RGB_F32LE"}),
    ]

    for filename, model, modelinfo_options in models:
        path = os.path.join(BASE_DIR, filename)
        onnx.save(model, path)
        write_modelinfo(path, **modelinfo_options)

    # Corrupt model
    with open(os.path.join(BASE_DIR, "corrupt_model.onnx"), "wb") as f:
        f.write(b"not-a-valid-onnx-model")

    print(f"Generated models in {BASE_DIR}")


if __name__ == "__main__":
    main()
