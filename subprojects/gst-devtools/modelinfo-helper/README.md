# ONNX to ModelInfo Generator

A Python script to generate GStreamer `.modelinfo` files from ONNX models.

## Requirements

```bash
pip install onnx
```

## Usage

### Basic usage:
```bash
./modelinfo-generator.py model.onnx
```

This will create `model.onnx.modelinfo` in the same directory.

### Specify output path:
```bash
./onnx_to_modelinfo.py model.onnx -o custom.modelinfo
```

## Interactive Prompts

The script will interactively prompt you for metadata for each tensor:

### For All Tensors:
- **Tensor ID**: Unique identifier for the tensor (default: tensor name with sanitized characters)
- **dims-order**: Optional dimension ordering (row-major or col-major)

### For Output Tensors:
- **group-id**: Groups related output tensors together (e.g., all outputs from same model)

### For Input Tensors:
- **ranges**: Expected input value range(s) for model input. Can be a single range (applies to all channels) or semicolon-separated per-channel ranges:
  - Single range: `0.0,255.0` (applies to all channels)
  - Per-channel: `0.0,255.0;-1.0,1.0;0.0,1.0` (e.g., different range per R,G,B channel)
  - Common ranges:
    - `0.0,255.0` - No normalization (passthrough)
    - `0.0,1.0` - Normalized to [0,1]
    - `-1.0,1.0` - Normalized to [-1,1]
    - `16.0,235.0` - TV/limited range

## Example Session

```
Loaded ONNX model: mobilenet_v2.onnx
Graph name: mobilenet_v2
Number of inputs: 1
Number of outputs: 1

======================================================================
PROCESSING INPUT TENSORS
======================================================================

======================================================================
Tensor: input
Direction: input
Type: float32
Dims: 1,224,224,3
======================================================================
Enter tensor ID [input]:
Specify dims-order (row-major/col-major)? [Y/n]: n
  Common ranges (applied to all channels):
    0.0,255.0  - No normalization (passthrough)
    0.0,1.0    - Normalized to [0,1]
    -1.0,1.0   - Normalized to [-1,1]
    16.0,235.0 - TV/limited range
  For per-channel ranges, use semicolon-separated values:
    0.0,255.0;-1.0,1.0;0.0,1.0  - Different range per channel (R,G,B)
  Enter ranges (min,max) [0.0,255.0]: -1.0,1.0

======================================================================
PROCESSING OUTPUT TENSORS
======================================================================

======================================================================
Tensor: MobilenetV2/Predictions/Reshape_1
Direction: output
Type: float32
Dims: 1,1001
======================================================================
Enter tensor ID [MobilenetV2_Predictions_Reshape_1]: classification-scores
Enter group-id (groups related tensors together): mobilenet-v2-output
Specify dims-order (row-major/col-major)? [Y/n]: n

Successfully generated modelinfo file: mobilenet_v2.onnx.modelinfo
```

## Generated ModelInfo Format

The script generates INI-format files compatible with GStreamer's onnxinference and tfliteinference elements.

### With NominalPixelRange from ONNX model (single range):
```ini
[modelinfo]
version=1.0
group-id=mobilenet-v2-output

[input]
id=input
type=float32
dims=1,224,224,3
dir=input
ranges=0.0,255.0

[MobilenetV2/Predictions/Reshape_1]
id=classification-scores
type=float32
dims=1,1001
dir=output
```

### With user-provided ranges (single range):
```ini
[modelinfo]
version=1.0
group-id=mobilenet-v2-output

[input]
id=input
type=float32
dims=1,224,224,3
dir=input
ranges=-1.0,1.0

[MobilenetV2/Predictions/Reshape_1]
id=classification-scores
type=float32
dims=1,1001
dir=output
```

### With per-channel ranges (RGB image with different normalization per channel):
```ini
[modelinfo]
version=1.0
group-id=mobilenet-v2-output

[input]
id=input
type=float32
dims=1,224,224,3
dir=input
ranges=0.0,255.0;-1.0,1.0;0.0,1.0

[MobilenetV2/Predictions/Reshape_1]
id=classification-scores
type=float32
dims=1,1001
dir=output
```

## ModelInfo Fields

### Required Fields:
- `[section_name]`: Tensor name from the model
- `type`: Data type (uint8, float32, etc.)
- `dims`: Comma-separated dimensions (-1 for dynamic dims)
- `dir`: Direction (input or output)

### Required Fields Only For Output:
- `group-id`: Groups related tensors (typically for outputs)
- `id`: Tensor identifier (ideally from [Tensor ID Registry](https://github.com/collabora/tensor-id-registry))

### Optional Fields:
- `dims-order`: Dimension ordering (row-major or col-major, defaults to row-major)
- `ranges`: Expected input value range(s) (for inputs only):
  - Single range: `"min,max"` - applies same range to all channels
  - Per-channel: `"min1,max1;min2,max2;min3,max3"` - different range per channel
  - If not specified, defaults to passthrough `[0.0, 255.0]`

## Tips

### Range Values and Per-Channel Normalization:
The script automatically reads `Image.NominalPixelRange` from ONNX model metadata when available.
The inference elements convert 8-bit input [0-255] to the specified ranges using:
```
output[channel] = input[channel] * scale[channel] + offset[channel]
where:
  scale[channel] = (max[channel] - min[channel]) / 255.0
  offset[channel] = min[channel]
```

#### Single Range (applies to all channels):
- `ranges=0.0,255.0` → All channels: scale=1.0, offset=0.0 (passthrough)
- `ranges=16.0,235.0` → All channels: scale≈0.859, offset=16.0 (TV range)
- `ranges=0.0,1.0` → All channels: scale≈0.00392, offset=0.0
- `ranges=-1.0,1.0` → All channels: scale≈0.00784, offset=-1.0

#### Per-Channel Ranges (RGB example):
- `ranges=0.0,255.0;0.0,255.0;0.0,255.0` → All channels passthrough
- `ranges=0.0,255.0;-1.0,1.0;0.0,1.0` → Different normalization per channel

#### Supported ONNX metadata conversions:
- `NominalRange_0_255` → `ranges=0.0,255.0`
- `NominalRange_16_235` → `ranges=16.0,235.0`
- `Normalized_0_1` → `ranges=0.0,1.0`
- `Normalized_1_1` → `ranges=-1.0,1.0`

### Group IDs:
- Use descriptive names like: `detection-outputs`, `classification-outputs`
- All related tensors from same model should share the same group-id

### Tensor IDs:
- Check the [Tensor ID Registry](https://github.com/collabora/tensor-id-registry) for standard IDs

## Integration with GStreamer

Once you have a `.modelinfo` file, use it with GStreamer:

```bash
gst-launch-1.0 filesrc location=input.mp4 ! ... ! \
    onnxinference model-file=model.onnx ! \
    ...
```

The onnxinference element will automatically look for `model.onnx.modelinfo` or `model.modelinfo` in the same directory.

## Troubleshooting

### "onnx package is required"
Install the ONNX package:
```bash
pip install onnx
```

### "Error loading ONNX model"
- Verify the file is a valid ONNX model
- Check file permissions
- Try opening with: `onnx.load(model_path)`

### Dynamic dimensions showing as -1
This is expected for batch sizes or variable-length inputs. The modelinfo uses `-1` as a wildcard that matches any dimension size at runtime.
