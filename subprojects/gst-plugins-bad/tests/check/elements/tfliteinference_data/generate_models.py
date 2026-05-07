#!/usr/bin/env python3

import os
import flatbuffers
import numpy as np
import keras
from keras import layers, ops
import tensorflow as tf
from tensorflow.lite.python import schema_py_generated as tflite_schema


BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def convert_keras(model, out_name):
    blob = tf.lite.TFLiteConverter.from_keras_model(model).convert()
    out_path = os.path.join(BASE_DIR, out_name)
    with open(out_path, "wb") as f:
        f.write(blob)
    return out_path


def convert_concrete(concrete_fn, out_name):
    converter = tf.lite.TFLiteConverter.from_concrete_functions(
        [concrete_fn], trackable_obj=None
    )
    blob = converter.convert()
    out_path = os.path.join(BASE_DIR, out_name)
    with open(out_path, "wb") as f:
        f.write(blob)
    return out_path


def dims_to_str(shape_sig):
    return ",".join("-1" if int(v) < 0 else str(int(v)) for v in shape_sig)


def dtype_to_modelinfo(dt):
    if dt == np.float32:
        return "float32"
    if dt == np.uint8:
        return "uint8"
    if dt == np.int8:
        return "int8"
    if dt == np.int16:
        return "int16"
    if dt == np.int32:
        return "int32"
    if dt == np.int64:
        return "int64"
    raise ValueError(f"Unsupported dtype {dt}")


def write_modelinfo(model_path, include_ranges=True, per_output_dims_order=None):
    if per_output_dims_order is None:
        per_output_dims_order = {}

    itp = tf.lite.Interpreter(model_path=model_path)
    ins = itp.get_input_details()
    outs = itp.get_output_details()

    group_id = os.path.basename(model_path).replace(".tflite", "") + "-group"
    lines = ["[modelinfo]", "version=1.0", f"group-id={group_id}", ""]

    for i, d in enumerate(ins):
        name = d["name"]
        dtype = dtype_to_modelinfo(d["dtype"])
        dims = dims_to_str(d.get("shape_signature", d["shape"]))
        lines += [
            f"[{name}]",
            f"id=input-{i}",
            f"type={dtype}",
            f"dims={dims}",
            "dir=input",
        ]
        if include_ranges:
            lines.append("ranges=0.0,255.0")
        lines.append("")

    for i, d in enumerate(outs):
        name = d["name"]
        dtype = dtype_to_modelinfo(d["dtype"])
        dims = dims_to_str(d.get("shape_signature", d["shape"]))
        lines += [
            f"[{name}]",
            f"id=output-{i}",
            f"type={dtype}",
            f"dims={dims}",
            "dir=output",
        ]
        if i in per_output_dims_order:
            lines.append(f"dims-order={per_output_dims_order[i]}")
        lines.append("")

    with open(model_path + ".modelinfo", "w", encoding="utf-8") as f:
        f.write("\n".join(lines).rstrip() + "\n")


def make_offset_input_model(out_name, input_name, input_shape):
    output_size = int(np.prod(input_shape))
    output_shape = [1, output_size]

    model = tflite_schema.ModelT()
    model.version = 3
    model.description = (
        "Input tensor intentionally not at index 0 for inference testing"
    )

    buf0 = tflite_schema.BufferT()
    buf0.data = None
    shape_const = np.array(output_shape, dtype=np.int32)
    buf1 = tflite_schema.BufferT()
    buf1.data = list(shape_const.tobytes())
    buf2 = tflite_schema.BufferT()
    buf2.data = None
    buf3 = tflite_schema.BufferT()
    buf3.data = None
    buf4 = tflite_schema.BufferT()
    buf4.data = None
    model.buffers = [buf0, buf1, buf2, buf3, buf4]

    cast_opcode = tflite_schema.OperatorCodeT()
    cast_opcode.builtinCode = tflite_schema.BuiltinOperator.CAST
    cast_opcode.deprecatedBuiltinCode = tflite_schema.BuiltinOperator.CAST
    reshape_opcode = tflite_schema.OperatorCodeT()
    reshape_opcode.builtinCode = tflite_schema.BuiltinOperator.RESHAPE
    reshape_opcode.deprecatedBuiltinCode = tflite_schema.BuiltinOperator.RESHAPE
    model.operatorCodes = [cast_opcode, reshape_opcode]

    subgraph = tflite_schema.SubGraphT()
    subgraph.name = "main"

    t_shape = tflite_schema.TensorT()
    t_shape.name = "reshape_shape"
    t_shape.shape = np.array([2], dtype=np.int32)
    t_shape.type = tflite_schema.TensorType.INT32
    t_shape.buffer = 1

    t_cast = tflite_schema.TensorT()
    t_cast.name = "cast_output"
    t_cast.shape = np.array(input_shape, dtype=np.int32)
    t_cast.type = tflite_schema.TensorType.FLOAT32
    t_cast.buffer = 3

    t_input = tflite_schema.TensorT()
    t_input.name = input_name
    t_input.shape = np.array(input_shape, dtype=np.int32)
    t_input.type = tflite_schema.TensorType.UINT8
    t_input.buffer = 2

    t_output = tflite_schema.TensorT()
    t_output.name = "output_flat_f32"
    t_output.shape = np.array(output_shape, dtype=np.int32)
    t_output.type = tflite_schema.TensorType.FLOAT32
    t_output.buffer = 4

    # Keep input tensor at global tensor index 2 on purpose.
    subgraph.tensors = [t_shape, t_cast, t_input, t_output]
    subgraph.inputs = np.array([2], dtype=np.int32)
    subgraph.outputs = np.array([3], dtype=np.int32)

    cast_op = tflite_schema.OperatorT()
    cast_op.opcodeIndex = 0
    cast_op.inputs = np.array([2], dtype=np.int32)
    cast_op.outputs = np.array([1], dtype=np.int32)
    cast_opts = tflite_schema.CastOptionsT()
    cast_opts.inDataType = tflite_schema.TensorType.UINT8
    cast_opts.outDataType = tflite_schema.TensorType.FLOAT32
    cast_op.builtinOptionsType = tflite_schema.BuiltinOptions.CastOptions
    cast_op.builtinOptions = cast_opts

    reshape_op = tflite_schema.OperatorT()
    reshape_op.opcodeIndex = 1
    reshape_op.inputs = np.array([1, 0], dtype=np.int32)
    reshape_op.outputs = np.array([3], dtype=np.int32)
    reshape_opts = tflite_schema.ReshapeOptionsT()
    reshape_opts.newShape = np.array(output_shape, dtype=np.int32)
    reshape_op.builtinOptionsType = tflite_schema.BuiltinOptions.ReshapeOptions
    reshape_op.builtinOptions = reshape_opts

    subgraph.operators = [cast_op, reshape_op]
    model.subgraphs = [subgraph]

    builder = flatbuffers.Builder(1024)
    model_offset = model.Pack(builder)
    builder.Finish(model_offset, b"TFL3")
    out_path = os.path.join(BASE_DIR, out_name)
    with open(out_path, "wb") as f:
        f.write(bytes(builder.Output()))

    return out_path


def main():
    for n in os.listdir(BASE_DIR):
        if n.endswith(".tflite") or n.endswith(".modelinfo"):
            os.unlink(os.path.join(BASE_DIR, n))

    inp = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="uint8", name="input_u8")
    x = layers.Lambda(lambda t: ops.cast(t, "float32"))(inp)
    out = layers.Reshape((48,), name="output_flat_f32")(x)
    p = convert_keras(keras.Model(inp, out), "flatten_uint8in_float32out.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="float32", name="input_f32")
    out = layers.Reshape((48,), name="output_flat_f32")(inp)
    p = convert_keras(keras.Model(inp, out), "flatten_float32in_float32out.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="float32", name="input_f32")
    x = layers.Lambda(lambda t: ops.clip(t, 0.0, 255.0))(inp)
    out = layers.Lambda(lambda t: ops.cast(t, "uint8"), name="output_u8")(x)
    p = convert_keras(keras.Model(inp, out), "uint8out.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="float32", name="input_f32")
    x = layers.Lambda(lambda t: ops.clip(t, -128.0, 127.0))(inp)
    out = layers.Lambda(lambda t: ops.cast(t, "int8"), name="output_i8")(x)
    p = convert_keras(keras.Model(inp, out), "int8out.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 3), dtype="float32", name="input_dyn")
    out = layers.Reshape((48,), name="output_dyn")(inp)
    p = convert_keras(keras.Model(inp, out), "dynamic_batch.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="float32", name="input_f32")
    out1 = layers.Reshape((48,), name="out1")(inp)  # (1, 48) row-major - will be output-0
    x = layers.Permute((3, 1, 2))(inp)  # Transpose to (1, 3, 4, 4) - channels first
    out0 = layers.Reshape((3, 16), name="out0")(x)  # (3, 16) col-major - will be output-1
    p = convert_keras(keras.Model(inp, [out1, out0]), "multi_output.tflite")
    write_modelinfo(p, per_output_dims_order={1: "col-major"})

    inp = keras.Input(shape=(4, 4), batch_size=3, dtype="float32", name="input_3d")
    out = layers.Reshape((16,), name="output_3d")(inp)
    p = convert_keras(keras.Model(inp, out), "flatten_3d_float32.tflite")
    write_modelinfo(p)

    inp0 = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="float32", name="input0")
    inp1 = keras.Input(shape=(4, 4, 3), batch_size=1, dtype="float32", name="input1")
    out = layers.Add(name="output_add")([inp0, inp1])
    p = convert_keras(keras.Model([inp0, inp1], out), "multi_input_two_tensors.tflite")
    write_modelinfo(p)

    @tf.function(input_signature=[tf.TensorSpec(shape=[48], dtype=tf.float32, name="input_1d")])
    def model_1d(x):
        return tf.identity(x, name="output_1d")

    p = convert_concrete(model_1d.get_concrete_function(), "invalid_dims_1d.tflite")
    write_modelinfo(p)

    @tf.function(input_signature=[tf.TensorSpec(shape=[1, 2, 2, 2, 3], dtype=tf.float32, name="input_5d")])
    def model_5d(x):
        return tf.identity(x, name="output_5d")

    p = convert_concrete(model_5d.get_concrete_function(), "invalid_dims_5d.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 2), batch_size=1, dtype="float32", name="input_ch2")
    out = layers.Lambda(lambda t: t, name="output_ch2")(inp)
    p = convert_keras(keras.Model(inp, out), "invalid_dims_channels_2.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(3, 4, 4), batch_size=1, dtype="float32", name="input_chw")
    out = layers.Lambda(lambda t: t, name="output_chw")(inp)
    p = convert_keras(keras.Model(inp, out), "planar_chw.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(3, 4, 4), batch_size=1, dtype="uint8", name="input_chw_u8")
    x = layers.Lambda(lambda t: ops.cast(t, "float32"))(inp)
    out = layers.Reshape((48,), name="output_flat_f32")(x)
    p = convert_keras(
        keras.Model(inp, out), "planar_chw_uint8in_float32out.tflite"
    )
    write_modelinfo(p)

    p = make_offset_input_model(
        "offset_input_planar_chw_uint8in_float32out.tflite",
        "input_chw_u8",
        [1, 3, 4, 4],
    )
    write_modelinfo(p)

    p = make_offset_input_model(
        "offset_input_hwc_uint8in_float32out.tflite",
        "input_hwc_u8",
        [1, 4, 4, 3],
    )
    write_modelinfo(p)

    @tf.function(input_signature=[tf.TensorSpec(shape=[4, 4], dtype=tf.float32, name="input_gray2d")])
    def model_gray2d(x):
        return tf.identity(x, name="output_gray2d")

    p = convert_concrete(model_gray2d.get_concrete_function(), "grayscale_2d.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 1), batch_size=1, dtype="float32", name="input_gray4d")
    out = layers.Lambda(lambda t: t, name="output_gray4d")(inp)
    p = convert_keras(keras.Model(inp, out), "grayscale_4d.tflite")
    write_modelinfo(p)

    inp = keras.Input(shape=(4, 4, 1), batch_size=1, dtype="uint8", name="input_gray4d_u8")
    out = layers.Lambda(lambda t: ops.cast(t, "float32"), name="output_gray4d_f32")(inp)
    p = convert_keras(keras.Model(inp, out), "grayscale_uint8in_float32out.tflite")
    write_modelinfo(p)

    with open(os.path.join(BASE_DIR, "corrupt_model.tflite"), "wb") as f:
        f.write(b"not-a-valid-tflite-model")

    print("Generated models in", BASE_DIR)


if __name__ == "__main__":
    main()
