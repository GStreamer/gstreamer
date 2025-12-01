#!/usr/bin/env python3
"""
Generate .modelinfo files from ONNX or TFLite models.

This script loads an ONNX or TFLite model and generates a GStreamer modelinfo file
with metadata for all input and output tensors. The generated .modelinfo file is
compatible with GStreamer's onnxinference and tfliteinference elements.
"""

import argparse
import sys
from pathlib import Path

# Try to import ONNX
try:
    import onnx
    from onnx import TensorProto
    ONNX_AVAILABLE = True
except ImportError:
    ONNX_AVAILABLE = False

# Try to import TensorFlow Lite
try:
    import tensorflow as tf
    TFLITE_AVAILABLE = True
except ImportError:
    TFLITE_AVAILABLE = False

if not ONNX_AVAILABLE and not TFLITE_AVAILABLE:
    print("Error: Either onnx or tensorflow package is required.", file=sys.stderr)
    print("Install with: pip install onnx tensorflow", file=sys.stderr)
    sys.exit(1)


# Current modelinfo format version
MODELINFO_VERSION = "1.0"

# ONNX type mapping (only define if ONNX is available)
if ONNX_AVAILABLE:
    ONNX_TO_GST_TYPE_MAP = {
        TensorProto.FLOAT: "float32",
        TensorProto.UINT8: "uint8",
        TensorProto.INT8: "int8",
        TensorProto.UINT16: "uint16",
        TensorProto.INT16: "int16",
        TensorProto.INT32: "int32",
        TensorProto.INT64: "int64",
        TensorProto.STRING: "string",
        TensorProto.BOOL: "bool",
        TensorProto.FLOAT16: "float16",
        TensorProto.DOUBLE: "float64",
        TensorProto.UINT32: "uint32",
        TensorProto.UINT64: "uint64",
        TensorProto.COMPLEX64: "complex64",
        TensorProto.COMPLEX128: "complex128",
        TensorProto.BFLOAT16: "bfloat16",
    }
else:
    ONNX_TO_GST_TYPE_MAP = {}

# TFLite tensor type mapping
TFLITE_TO_GST_TYPE_MAP = {
    "FLOAT32": "float32",
    "FLOAT16": "float16",
    "INT32": "int32",
    "UINT8": "uint8",
    "INT8": "int8",
    "INT64": "int64",
    "UINT32": "uint32",
    "UINT64": "uint64",
    "BOOL": "bool",
    "COMPLEX64": "complex64",
    "COMPLEX128": "complex128",
    "BFLOAT16": "bfloat16",
}


def parse_nominal_pixel_range(nominal_range_str):
    """Convert ONNX NominalPixelRange string to [min, max] float array string."""
    mapping = {
        'NominalRange_0_255': '0.0,255.0',
        'Normalized_0_1': '0.0,1.0',
        'Normalized_1_1': '-1.0,1.0',
        'NominalRange_16_235': '16.0,235.0',
    }
    return mapping.get(nominal_range_str)


def get_tensor_type(elem_type):
    """Convert ONNX element type or TFLite numpy dtype to GStreamer type string."""
    # Handle ONNX integer type codes
    if isinstance(elem_type, int):
        return ONNX_TO_GST_TYPE_MAP.get(elem_type, "unknown")
    # Handle TFLite numpy dtype
    else:
        return get_tflite_dtype_string(elem_type)


def get_dims_string(shape):
    """Convert tensor shape to comma-separated dims string.

    Args:
        shape: Either ONNX shape object (with .dim) or TFLite shape list
    """
    dims = []

    # Handle ONNX shape object
    if hasattr(shape, 'dim'):
        for dim in shape.dim:
            if dim.dim_value:
                dims.append(str(dim.dim_value))
            elif dim.dim_param:
                dims.append("-1")
            else:
                dims.append("-1")
    # Handle TFLite shape list
    else:
        for dim in shape:
            if dim is None or dim < 0:
                dims.append("-1")
            else:
                dims.append(str(dim))

    return ",".join(dims) if dims else "1"


def prompt_for_value(prompt, default=None, allow_empty=False, newline=False):
    """Prompt user for a value with optional default."""
    if default:
        prompt_text = f"{prompt} [{default}]: "
    else:
        prompt_text = f"{prompt}: "

    if newline:
        prompt_text = f"{prompt_text}\n"

    while True:
        value = input(prompt_text).strip()
        if not value and default:
            return default
        if not value and allow_empty:
            return None
        if value:
            return value
        print("  (This field is required)")


def prompt_yes_no(prompt, default=True):
    """Prompt user for yes/no question."""
    default_str = "Y/n" if default else "y/N"
    while True:
        value = input(f"{prompt} [{default_str}]: ").strip().lower()
        if not value:
            return default
        if value in ['y', 'yes']:
            return True
        if value in ['n', 'no']:
            return False
        print("  Please enter 'y' or 'n'")


def get_tensor_info(tensor, direction, group_id=None, prompt_mode=False, model=None):
    """Extract tensor information with optional user prompting.

    Args:
        tensor: The tensor to extract info from
        direction: 'input' or 'output'
        group_id: Pre-defined group-id (for output tensors in v1.0+)
        prompt_mode: If True, prompt user for metadata. If False, use auto-generated values.
        model: The ONNX model (optional, for reading Image.NominalPixelRange)
    """
    info = {
        'name': tensor.name,
        'dir': direction,
        'type': get_tensor_type(tensor.type.tensor_type.elem_type),
        'dims': get_dims_string(tensor.type.tensor_type.shape),
    }

    # Auto-generate tensor ID from name
    default_id = tensor.name.replace('/', '_').replace(':', '_')

    if prompt_mode:
        print(f"\n{'=' * 70}")
        print(f"Tensor: {tensor.name}")
        print(f"Direction: {direction}")
        print(f"Type: {info['type']}")
        print(f"Dims: {info['dims']}")
        print(f"{'=' * 70}")

        info['id'] = prompt_for_value(f"Enter tensor ID", default=default_id,
                                      newline=True)

        if prompt_yes_no("Specify dims-order (row-major/col-major)?", default=False):
            while True:
                dims_order = input("  Enter dims-order [row-major/col-major]: ").strip().lower()
                if dims_order in ['row-major', 'col-major', '']:
                    if dims_order:
                        info['dims-order'] = dims_order
                    break
                print("  Please enter 'row-major' or 'col-major'")

        if direction == 'input':
            # Try to read Image.NominalPixelRange from ONNX metadata
            ranges_from_onnx = None
            if model and hasattr(model, 'metadata_props'):
                for prop in model.metadata_props:
                    if prop.key == 'Image.NominalPixelRange':
                        ranges_from_onnx = parse_nominal_pixel_range(prop.value)
                        if ranges_from_onnx:
                            print(f"  Found Image.NominalPixelRange in model: {prop.value}")
                        break

            if ranges_from_onnx:
                # Confirm with user
                if prompt_yes_no(f"  Use ranges {ranges_from_onnx} from model metadata?", default=True):
                    info['ranges'] = ranges_from_onnx
                else:
                    # User rejected, prompt for custom ranges
                    print("  Common ranges (applied to all channels):")
                    print("    0.0,255.0  - No normalization (passthrough)")
                    print("    0.0,1.0    - Normalized to [0,1]")
                    print("    -1.0,1.0   - Normalized to [-1,1]")
                    print("    16.0,235.0 - TV/limited range")
                    print("  For per-channel ranges, use semicolon-separated values:")
                    print("    0.0,255.0;-1.0,1.0;0.0,1.0  - Different range per channel (R,G,B)")
                    ranges = prompt_for_value("  Enter ranges (min,max)", default="0.0,255.0")
                    info['ranges'] = ranges
            else:
                # No Image.NominalPixelRange in model
                print("  Common ranges (applied to all channels):")
                print("    0.0,255.0  - No normalization (passthrough)")
                print("    0.0,1.0    - Normalized to [0,1]")
                print("    -1.0,1.0   - Normalized to [-1,1]")
                print("    16.0,235.0 - TV/limited range")
                print("  For per-channel ranges, use semicolon-separated values:")
                print("    0.0,255.0;-1.0,1.0;0.0,1.0  - Different range per channel (R,G,B)")
                ranges = prompt_for_value("  Enter ranges (min,max)", default="0.0,255.0")
                info['ranges'] = ranges
    else:
        # No-prompt mode: use auto-generated values with PLACEHOLDER-*-REQUIRED format
        info['id'] = "PLACEHOLDER-ID-REQUIRED"

        if direction == 'input':
            # Try to read Image.NominalPixelRange from ONNX metadata
            if model and hasattr(model, 'metadata_props'):
                for prop in model.metadata_props:
                    if prop.key == 'Image.NominalPixelRange':
                        ranges_from_onnx = parse_nominal_pixel_range(prop.value)
                        if ranges_from_onnx:
                            info['ranges'] = ranges_from_onnx
                            print(f"  Input tensor '{tensor.name}': Using ranges from model metadata: {prop.value}")
                        break

            # If no ranges from metadata, use placeholder
            if 'ranges' not in info:
                info['ranges'] = "PLACEHOLDER-RANGES-REQUIRED"

    return info


def load_tflite_model(model_path):
    """Load a TFLite model and return interpreter and model details.

    Returns:
        tuple: (interpreter, inputs_list, outputs_list, model_name)
    """
    if not TFLITE_AVAILABLE:
        print("Error: tensorflow package is required for TFLite support. Install with: pip install tensorflow", file=sys.stderr)
        sys.exit(1)

    try:
        interpreter = tf.lite.Interpreter(model_path=model_path)
        interpreter.allocate_tensors()
    except Exception as e:
        print(f"Error loading TFLite model: {e}", file=sys.stderr)
        sys.exit(1)

    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    model_name = Path(model_path).stem

    return interpreter, input_details, output_details, model_name


def get_tflite_dtype_string(dtype):
    """Convert TFLite numpy dtype to GST type string."""
    dtype_name = dtype.__name__ if hasattr(dtype, '__name__') else str(dtype)

    # Map numpy dtype names to TFLite type strings
    dtype_mapping = {
        'float32': 'float32',
        'float64': 'float64',
        'float16': 'float16',
        'int32': 'int32',
        'int64': 'int64',
        'uint8': 'uint8',
        'int8': 'int8',
        'uint32': 'uint32',
        'uint64': 'uint64',
        'bool': 'bool',
    }

    gst_type = dtype_mapping.get(dtype_name, "unknown")
    return gst_type


class TFLiteTensorAdapter:
    """Adapter to provide ONNX-like interface for TFLite tensor details."""

    def __init__(self, tensor_detail):
        """Create adapter from TFLite tensor detail dict.

        Args:
            tensor_detail: Dict with keys: 'name', 'dtype', 'shape', 'index'
        """
        self.name = tensor_detail['name']
        self.dtype = tensor_detail['dtype']
        self._shape_value = tensor_detail['shape']
        self._shape_obj = None

    @property
    def type(self):
        """Provide type-like interface matching ONNX tensor.type."""
        return self

    @property
    def tensor_type(self):
        """Provide tensor_type-like interface matching ONNX tensor.type.tensor_type."""
        return self

    @property
    def elem_type(self):
        """Return dtype as element type."""
        return self.dtype

    @property
    def shape(self):
        """Return shape object with dims."""
        if self._shape_obj is None:
            # Create a shape-like object with dims
            self._shape_obj = self._ShapeAdapter(self._shape_value)
        return self._shape_obj

    class _ShapeAdapter:
        """Adapter for shape to provide dim-like interface."""

        def __init__(self, shape_list):
            """Create from list of dimensions."""
            self.dim = [self._DimAdapter(d) for d in shape_list]

        class _DimAdapter:
            """Adapter for individual dimension."""

            def __init__(self, dim_value):
                """Create from dimension value."""
                if dim_value is None or dim_value < 0:
                    self.dim_value = 0
                    self.dim_param = None
                else:
                    self.dim_value = dim_value
                    self.dim_param = None


def get_model_type(model_path):
    """Determine model type from file extension.

    Returns:
        str: Either 'onnx' or 'tflite'
    """
    ext = Path(model_path).suffix.lower()
    if ext == '.onnx':
        return 'onnx'
    elif ext == '.tflite':
        return 'tflite'
    else:
        print(f"Error: Unsupported model file extension: {ext}", file=sys.stderr)
        print("Supported formats: .onnx, .tflite", file=sys.stderr)
        sys.exit(1)


def generate_modelinfo(model_path, output_path=None, prompt_mode=False):
    """Generate modelinfo file from ONNX or TFLite model.

    Args:
        model_path: Path to model file (.onnx or .tflite)
        output_path: Path for output modelinfo file (default: model.{ext}.modelinfo)
        prompt_mode: If True, prompt user for metadata. If False, use auto-generated values.
    """
    model_type = get_model_type(model_path)

    if model_type == 'onnx':
        if not ONNX_AVAILABLE:
            print("Error: onnx package is required for ONNX support. Install with: pip install onnx", file=sys.stderr)
            sys.exit(1)

        try:
            model = onnx.load(model_path)
        except Exception as e:
            print(f"Error loading ONNX model: {e}", file=sys.stderr)
            sys.exit(1)

        graph = model.graph
        print(f"\nLoaded ONNX model: {model_path}")
        print(f"Graph name: {graph.name}")
        print(f"Number of inputs: {len(graph.input)}")
        print(f"Number of outputs: {len(graph.output)}")

        # Store for later use in processing
        model_graph = graph
        model_inputs = graph.input
        model_outputs = graph.output
        is_onnx = True

    else:  # tflite
        interpreter, input_details, output_details, model_name = load_tflite_model(model_path)

        print(f"\nLoaded TFLite model: {model_path}")
        print(f"Number of inputs: {len(input_details)}")
        print(f"Number of outputs: {len(output_details)}")

        model_graph = None
        model_inputs = input_details
        model_outputs = output_details
        is_onnx = False

    if prompt_mode:
        print("\nGenerating modelinfo with user prompts...")
        print("\n" + "=" * 70)
        print("TENSOR ID REGISTRY")
        print("=" * 70)
        print("Tensor IDs should be registered in the Tensor ID Registry:")
        print("https://github.com/collabora/tensor-id-registry/blob/main/tensor-id-register.md")
    else:
        print("\nGenerating modelinfo (no-prompt mode - using auto-generated values)...")

    all_tensors = []

    if prompt_mode:
        print("\n" + "=" * 70)
        print("PROCESSING INPUT TENSORS")
        print("=" * 70)

    # Process input tensors (handle both ONNX and TFLite)
    for tensor_detail in model_inputs:
        # For ONNX: tensor_detail is an ONNX tensor object
        # For TFLite: tensor_detail is a dict; wrap it with adapter
        if is_onnx:
            tensor = tensor_detail
            # Skip ONNX initializers (weights/biases)
            if any(init.name == tensor.name for init in model_graph.initializer):
                continue
        else:
            # TFLite: wrap dict with adapter to provide ONNX-like interface
            tensor = TFLiteTensorAdapter(tensor_detail)

        tensor_info = get_tensor_info(tensor, 'input', prompt_mode=prompt_mode, model=model if is_onnx else None)
        all_tensors.append(tensor_info)

    # Ask for group-id
    group_id = None
    if prompt_mode:
        print("\n" + "=" * 70)
        print("GLOBAL GROUP-ID")
        print("=" * 70)
        group_id = prompt_for_value(
            "Enter group-id for all output tensors (applies globally to the model)",
            default=None,
            allow_empty=True,
            newline=True
        )
    else:
        # No-prompt mode: use placeholder with PLACEHOLDER-*-REQUIRED format
        group_id = "PLACEHOLDER-GROUP-ID-REQUIRED"
        print(f"Using placeholder group-id: {group_id}")

    if prompt_mode:
        print("\n" + "=" * 70)
        print("PROCESSING OUTPUT TENSORS")
        print("=" * 70)

    # Process output tensors (handle both ONNX and TFLite)
    for tensor_detail in model_outputs:
        # For ONNX: tensor_detail is an ONNX tensor object
        # For TFLite: tensor_detail is a dict; wrap it with adapter
        if is_onnx:
            tensor = tensor_detail
        else:
            # TFLite: wrap dict with adapter to provide ONNX-like interface
            tensor = TFLiteTensorAdapter(tensor_detail)

        tensor_info = get_tensor_info(tensor, 'output', group_id=group_id, prompt_mode=prompt_mode)
        all_tensors.append(tensor_info)

    # Generate output path with appropriate extension
    if output_path is None:
        if is_onnx:
            output_path = Path(model_path).with_suffix('.onnx.modelinfo')
        else:
            output_path = Path(model_path).with_suffix('.tflite.modelinfo')

    write_modelinfo(all_tensors, output_path, group_id=group_id, prompt_mode=prompt_mode)
    print(f"\nSuccessfully generated modelinfo file: {output_path}")


def write_modelinfo(tensors, output_path, version=None, group_id=None, prompt_mode=False):
    """Write modelinfo file in INI format with version header.

    Args:
        tensors: List of tensor dictionaries
        output_path: Path to output file
        version: Version string (default: MODELINFO_VERSION)
        group_id: Global group-id for v2.0+ (written in [modelinfo] section)
        prompt_mode: If False, add informational comments for PLACEHOLDER values
    """
    if version is None:
        version = MODELINFO_VERSION

    # Parse version to determine format
    version_parts = version.split('.')
    major_version = int(version_parts[0]) if version_parts else 1

    with open(output_path, 'w') as f:
        # Add informational comment for no-prompt mode
        if not prompt_mode:
            f.write("# Auto-generated GStreamer modelinfo file\n")
            f.write("# Replace PLACEHOLDER-* values with actual metadata from:\n")
            f.write("# Tensor ID Registry: https://github.com/collabora/tensor-id-registry/blob/main/tensor-id-register.md\n")
            f.write("#\n")
            f.write("# PLACEHOLDER fields to update:\n")
            f.write("#   - id: Tensor identifier (use from registry)\n")
            f.write("#   - group-id: Model identifier grouping related tensors\n")
            f.write("#   - ranges: Input normalization ranges (min,max per channel)\n")
            f.write("#\n")

        # Write version header section first
        f.write("[modelinfo]\n")
        f.write(f"version={version}\n")

        # v1.0+: Write global group-id in [modelinfo] section
        # (prefer group_id parameter first, else look for it in tensors)
        if not group_id:
            for tensor in tensors:
                if tensor.get('dir') == 'output' and 'group-id' in tensor:
                    group_id = tensor['group-id']
                    break

        if group_id:
            f.write(f"group-id={group_id}\n")

        f.write("\n")

        for i, tensor in enumerate(tensors):
            # Add blank line between sections (except before first tensor)
            if i > 0:
                f.write("\n")

            f.write(f"[{tensor['name']}]\n")

            # Write fields in specific order
            # Required fields first
            if 'id' in tensor:
                f.write(f"id={tensor['id']}\n")

            f.write(f"type={tensor['type']}\n")
            f.write(f"dims={tensor['dims']}\n")
            f.write(f"dir={tensor['dir']}\n")

            # Optional fields
            if 'dims-order' in tensor:
                f.write(f"dims-order={tensor['dims-order']}\n")

            # Normalization parameters (for inputs)
            if 'ranges' in tensor:
                f.write(f"ranges={tensor['ranges']}\n")


def parse_modelinfo(input_path):
    """Parse existing modelinfo file.

    Returns:
        tuple: (version_string, list of tensor dictionaries)
    """
    import configparser

    config = configparser.ConfigParser()
    config.read(input_path)

    # Get version (default to 1.0 if not present)
    version = "1.0"
    global_group_id = None
    if config.has_section('modelinfo'):
        version = config.get('modelinfo', 'version', fallback='1.0')
        # Read global group-id from [modelinfo] section (v1.0+ format)
        global_group_id = config.get('modelinfo', 'group-id', fallback=None)

    tensors = []
    for section in config.sections():
        if section == 'modelinfo':
            continue

        tensor = {'name': section}

        # Read all fields
        for key in config.options(section):
            value = config.get(section, key)
            # For ranges/pixel_range, parse as comma-separated floats (with optional semicolon separation)
            if key in ('ranges', 'pixel_range'):
                value = value.strip()
            tensor[key] = value

        # Add global group-id
        if global_group_id and tensor.get('dir') == 'output' and 'group-id' not in tensor:
            tensor['group-id'] = global_group_id

        tensors.append(tensor)

    return version, tensors


def upgrade_modelinfo(input_path, output_path=None):
    """Upgrade a modelinfo file to the current version.

    Args:
        input_path: Path to existing modelinfo file
        output_path: Path to output (default: overwrite input)
    """
    if output_path is None:
        output_path = input_path

    print(f"\nUpgrading modelinfo file: {input_path}")

    # Parse existing file
    try:
        old_version, tensors = parse_modelinfo(input_path)
    except Exception as e:
        print(f"Error reading modelinfo file: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Current version: {old_version}")
    print(f"Target version: {MODELINFO_VERSION}")

    if old_version == MODELINFO_VERSION:
        print(f"File is already at version {MODELINFO_VERSION}")
        return

    # Parse version numbers
    try:
        old_major, old_minor = map(int, old_version.split('.'))
        new_major, new_minor = map(int, MODELINFO_VERSION.split('.'))
    except ValueError:
        print(f"Error: Invalid version format", file=sys.stderr)
        sys.exit(1)

    # Check version compatibility
    # Only support upgrades within the same major version
    if old_major != new_major:
        print(f"Error: Cannot upgrade from v{old_major}.x to v{new_major}.x", file=sys.stderr)
        print(f"Cross-major version upgrades are not supported at this time.", file=sys.stderr)
        sys.exit(1)

    # Minor version upgrade within same major version (e.g., v1.0 → v1.1)
    global_group_id = None
    if old_minor < new_minor:
        print(f"\nMinor version upgrade: {old_version} -> {MODELINFO_VERSION}")
        # No format changes within same major version

    # Write upgraded file
    print(f"\nWriting upgraded file to: {output_path}")
    write_modelinfo(tensors, output_path, version=MODELINFO_VERSION, group_id=global_group_id, prompt_mode=False)
    print(f"Successfully upgraded to version {MODELINFO_VERSION}")


def main():
    parser = argparse.ArgumentParser(
        description='Generate or upgrade GStreamer modelinfo files from ONNX or TFLite models',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate new modelinfo from ONNX model (no-prompt mode - default)
  %(prog)s model.onnx
  %(prog)s model.onnx -o custom.modelinfo

  # Generate new modelinfo from TFLite model (no-prompt mode - default)
  %(prog)s model.tflite
  %(prog)s model.tflite -o custom.modelinfo

  # Generate with interactive prompts
  %(prog)s --prompt model.onnx
  %(prog)s --prompt model.tflite
  %(prog)s --prompt model.onnx -o custom.modelinfo

  # Upgrade existing modelinfo file
  %(prog)s --upgrade model.onnx.modelinfo
  %(prog)s --upgrade old.modelinfo -o new.modelinfo

The generated .modelinfo file can be used with GStreamer's onnxinference
and tfliteinference elements for ML inference pipelines.

Modes:
  - No-prompt (default): Auto-generates tensor IDs and group-id with
    PLACEHOLDER- prefix, skips optional fields. Edit the file to replace
    placeholder values with actual metadata.
  - Prompt mode (--prompt): Interactively asks for tensor metadata
        """
    )

    parser.add_argument(
        'input_path',
        type=str,
        help='Path to model file (.onnx or .tflite) or modelinfo file (with --upgrade)'
    )

    parser.add_argument(
        '-o', '--output',
        type=str,
        default=None,
        help='Output path for .modelinfo file (default: <model>.onnx.modelinfo or <model>.tflite.modelinfo)'
    )

    parser.add_argument(
        '--upgrade',
        action='store_true',
        help='Upgrade an existing modelinfo file to the current version'
    )

    parser.add_argument(
        '--prompt',
        action='store_true',
        help='Enable interactive prompts for tensor metadata (default is no-prompt mode with auto-generated values)'
    )

    args = parser.parse_args()

    # Check if input file exists
    if not Path(args.input_path).exists():
        print(f"Error: File not found: {args.input_path}", file=sys.stderr)
        sys.exit(1)

    # Handle upgrade mode
    if args.upgrade:
        upgrade_modelinfo(args.input_path, args.output)
    else:
        # Generate modelinfo from ONNX model
        generate_modelinfo(args.input_path, args.output, prompt_mode=args.prompt)


if __name__ == '__main__':
    main()
