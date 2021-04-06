# Copyright 2021 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================

import enum
from typing import Any, ClassVar, Dict, List, Optional, Sequence, Tuple, Type, Union, overload

import numpy as np

from . import ops
from . import jax_jit
from . import outfeed_receiver
from . import pmap_lib
from . import profiler
from . import pytree

_LiteralSlice = Any
_Status = Any
_Dtype = Any
_XlaOpMetadata = Any

class PrimitiveType(enum.IntEnum):
  PRIMITIVE_TYPE_INVALID: PrimitiveType
  PRED: PrimitiveType
  S8: PrimitiveType
  S16: PrimitiveType
  S32: PrimitiveType
  S64: PrimitiveType
  U8: PrimitiveType
  U16: PrimitiveType
  U32: PrimitiveType
  U64: PrimitiveType
  BF16: PrimitiveType
  F16: PrimitiveType
  F32: PrimitiveType
  F64: PrimitiveType
  C64: PrimitiveType
  C128: PrimitiveType
  TUPLE: PrimitiveType
  OPAQUE_TYPE: PrimitiveType
  TOKEN: PrimitiveType

def bfloat16_dtype() -> Type[Any]: ...

# === BEGIN xla_compiler.cc

class Shape:
  def __init__(self, s: str): ...
  @staticmethod
  def tuple_shape(shapes: List[Shape]) -> Shape: ...
  @staticmethod
  def array_shape(
      type: Union[np.dtype, PrimitiveType],
      dims_seq: Any = ...,
      layout_seq: Any = ...,
      dynamic_dimensions: Optional[List[bool]] = ...) -> Shape: ...
  @staticmethod
  def token_shape(self) -> Shape: ...
  @staticmethod
  def scalar_shape(type: Union[np.dtype, PrimitiveType]) -> Shape: ...
  def dimensions(self) -> Tuple[int, ...]: ...
  def xla_element_type(self) -> PrimitiveType: ...
  def element_type(self) -> np.dtype: ...
  def numpy_dtype(self) -> np.dtype: ...
  def is_tuple(self) -> bool: ...
  def is_array(self) -> bool: ...
  def is_token(self) -> bool: ...
  def is_static(self) -> bool: ...
  def is_dynamic(self) -> bool: ...
  def is_dynamic_dimension(self, dimension: int) -> bool: ...
  def set_dynamic_dimension(self, dimension: int, is_dynamic: bool) -> None: ...
  def rank(self) -> int: ...
  def to_serialized_proto(self) -> bytes: ...
  def tuple_shapes(self) -> List[Shape]: ...
  def leaf_count(self) -> int: ...
  def with_major_to_minor_layout_if_absent(self) -> Shape: ...
  def __eq__(self, other: Shape) -> bool: ...
  def __ne__(self, other: Shape) -> bool: ...
  def __hash__(self) -> int: ...
  def __repr__(self) -> str: ...

class ProgramShape:
  def __init__(self, params: Sequence[Shape], result: Shape) -> None: ...
  def parameter_shapes(self) -> List[Shape]: ...
  def result_shape(self) -> Shape: ...
  def __repr__(self) -> str: ...

class Literal:
  def __repr__(self) -> str: ...

class XlaComputation:
  def __init__(self, serialized_hlo_module_proto: bytes) -> None: ...
  def get_hlo_module(self) -> HloModule: ...
  def program_shape(self) -> ProgramShape: ...
  def as_serialized_hlo_module_proto(self) -> bytes: ...
  def as_hlo_text(self) -> str: ...
  def as_hlo_dot_graph(self) -> str: ...
  def hash(self) -> int: ...
  def as_hlo_module(elf) -> HloModule: ...

class HloPrintOptions:
  def __init__(self) -> None: ...
  @staticmethod
  def short_parsable() -> HloPrintOptions: ...
  @staticmethod
  def canonical() -> HloPrintOptions: ...
  @staticmethod
  def fingerprint() -> HloPrintOptions: ...
  print_large_constants: bool
  print_metadata: bool
  print_backend_config: bool
  print_result_shape: bool
  print_operand_shape: bool
  print_operand_names: bool
  print_ids: bool
  print_extra_attributes: bool
  print_program_shape: bool
  print_percent: bool
  print_control_dependencies: bool
  compact_operands: bool
  include_layout_in_shapes: bool
  canonicalize_instruction_names: bool
  canonicalize_computations: bool
  indent_amount: int
  is_in_nested_computation: bool
  leading_and_trailing_instructions_number: int

class HloModule:
  def to_string(self, options: HloPrintOptions = ...) -> str: ...

def hlo_module_to_dot_graph(hlo_module: HloModule) -> str: ...

def hlo_module_cost_analysis(
    client: Client,
    module: HloModule) -> Dict[str, float]: ...

class XlaOp: ...

class XlaBuilder:
  def __init__(self, name: str) -> None: ...
  def Build(self, root: Optional[XlaOp] = ...) -> XlaComputation: ...
  def GetShape(self) -> Shape: ...
  build = Build
  def clear_op_metadata(self) -> None: ...
  get_shape = GetShape
  def get_program_shape(self, root: Optional[XlaOp] = ...) -> ProgramShape: ...
  def is_constant(self) -> bool: ...
  def set_op_metadata(self, metadata: _XlaOpMetadata) -> None: ...
  def set_sharding(self, sharding: OpSharding_Type) -> None: ...
  def clear_sharding(self) -> None: ...
  def setup_alias(
      self,
      output_index: Sequence[int],
      param_number: int,
      param_index: Sequence[int]) -> None: ...

class DeviceAssignment:
  @staticmethod
  def create(array: np.ndarray) -> DeviceAssignment: ...
  def replica_count(self) -> int: ...
  def computation_count(self) -> int: ...
  def __repr__(self) -> str: ...

class CompileOptions:
  def __init__(self) -> None: ...
  argument_layouts: Optional[List[Shape]]
  parameter_is_tupled_arguments: bool
  executable_build_options: ExecutableBuildOptions
  tuple_arguments: bool
  num_replicas: int
  num_partitions: int
  device_assignment: Optional[DeviceAssignment]

def register_custom_call_target(fn_name: str, capsule: Any, platform: str) -> _Status: ...

class DebugOptions:
  def __repr__(self) -> str: ...
  xla_cpu_enable_fast_math: bool
  xla_cpu_fast_math_honor_infs: bool
  xla_cpu_fast_math_honor_nans: bool
  xla_cpu_fast_math_honor_division: bool
  xla_cpu_fast_math_honor_functions: bool
  xla_gpu_enable_fast_min_max: bool
  xla_backend_optimization_level: int
  xla_cpu_enable_xprof_traceme: bool
  xla_llvm_disable_expensive_passes: bool
  xla_test_all_input_layouts: bool

class ExecutableBuildOptions:
  def __init__(self) -> None: ...
  def __repr__(self) -> str: ...
  result_layout: Optional[Shape]
  num_replicas: int
  num_partitions: int
  debug_options: DebugOptions
  device_assignment: Optional[DeviceAssignment]
  use_spmd_partitioning: bool

class PrecisionConfig_Precision(enum.IntEnum):
  DEFAULT: int
  HIGH: int
  HIGHEST: int

class OpSharding_Type(enum.IntEnum):
  REPLICATED: int
  MAXIMAL: int
  TUPLE: int
  OTHER: int

class ChannelHandle_ChannelType(enum.IntEnum):
  CHANNEL_TYPE_INVALID: int
  DEVICE_TO_DEVICE: int
  DEVICE_TO_HOST: int
  HOST_TO_DEVICE: int

class ChannelHandle:
  type: ChannelHandle_ChannelType
  handle: int
  def __repr__(self) -> str: ...

class FftType(enum.IntEnum):
  FFT: int
  IFFT: int
  RFFT: int
  IRFFT: int

# === END xla_compiler.cc

class Device:
  id: int
  host_id: int
  process_index: int
  platform: str
  device_kind: str
  client: Client
  def __str__(self) -> str: ...
  def transfer_to_infeed(self, literal: _LiteralSlice): ...
  def transfer_from_outfeed(self, shape: Shape): ...

class CpuDevice(Device):
  def __repr__(self) -> str: ...

class GpuDevice(Device):
  def __repr__(self) -> str: ...

class TpuDevice(Device):
  coords: Tuple[int, ...]
  core_on_chip: int
  def __repr__(self) -> str: ...

class GpuAllocatorConfig:
  class Kind(enum.IntEnum):
    DEFAULT: int
    PLATFORM: int
    BFC: int
    CUDA_ASYNC: int

  def __init__(
      self,
      kind: Kind = ...,
      memory_fraction: float = ...,
      preallocate: bool = ...) -> None: ...

class HostBufferSemantics(enum.IntEnum):
  IMMUTABLE_ONLY_DURING_CALL: HostBufferSemantics
  IMMUTABLE_UNTIL_TRANSFER_COMPLETES: HostBufferSemantics
  ZERO_COPY: HostBufferSemantics

class Client:
  platform: str
  platform_version: str
  def device_count(self) -> int: ...
  def local_device_count(self) -> int: ...
  def devices(self) -> List[Device]: ...
  def local_devices(self) -> List[Device]: ...
  def live_buffers(self) -> List[Buffer]: ...
  def host_id(self) -> int: ...
  def process_index(self) -> int: ...
  @overload
  def get_default_device_assignment(
      self,
      num_replicas: int,
      num_partitions: int) -> List[List[Device]]: ...
  @overload
  def get_default_device_assignment(
      self,
      num_replicas: int) -> List[Device]: ...
  def create_channel_handle(self) -> ChannelHandle: ...
  def create_device_to_host_channel_handle(self) -> ChannelHandle: ...
  def create_host_to_device_channel_handle(self) -> ChannelHandle: ...
  def buffer_from_pyval(
      self,
      argument: Any,
      device: Device = ...,
      force_copy: bool = ...,
      host_buffer_semantics: HostBufferSemantics = ...) -> Buffer: ...
  def compile(
      self,
      computation: XlaComputation,
      compile_options: CompileOptions = ...) -> Executable: ...
  def heap_profile(self) -> bytes: ...
  def defragment(self) -> _Status: ...

def get_cpu_client(asynchronous: bool = ...) -> Client: ...
def get_interpreter_client() -> Client: ...
def get_gpu_client(
    asynchronous: bool = ...,
    allocator_config: GpuAllocatorConfig = ...,
    distributed_client: DistributedRuntimeClient = ...,
    node_id: int = ...) -> Client:...
def get_tpu_client(asynchronous: bool = ...) -> Client: ...

class DeviceArrayBase: ...

class DeviceArray(DeviceArrayBase):
  __array_priority__: int
  _device: Device
  aval: Any
  weak_type: Optional[bool]
  _lazy_expr: Any
  device_buffer: Buffer
  shape: Tuple[int, ...]
  dtype: np.dtype
  size: int
  ndim: int
  _value: np.array
  def copy_to_device(self, dst_device: Device) -> DeviceArray: ...
  def on_device_size_in_bytes(self) -> int: ...
  def delete(self) -> None: ...
  def block_until_ready(self) -> DeviceArray: ...
  def copy_to_host_async(self) -> _Status: ...
  def xla_shape(self) -> Shape: ...
  def xla_dynamic_shape(self) -> Shape: ...
  client: Client
  def device(self) -> Device: ...
  def platform(self) -> str: ...
  def is_deleted(self) -> bool: ...
  def unsafe_buffer_pointer(self) -> Any: ...
  __cuda_array_interface__: Dict[str, Any]
  traceback: Traceback
  def clone(self) -> DeviceArray: ...

PyLocalBuffer = DeviceArray
Buffer = DeviceArray

class Executable:
  client: Client
  def local_logical_device_ids(self) -> List[Tuple[int, int]]: ...
  def local_devices(self): List[Device]: ...
  def size_of_generated_code_in_bytes(self) -> int: ...
  def delete(self) -> None: ...
  def execute(self, arguments: Sequence[DeviceArray]) -> List[DeviceArray]: ...
  def execute_sharded_on_local_devices(
    arguments: Sequence[List[DeviceArray]]) -> List[List[DeviceArray]]: ...
  def hlo_modules(self) -> List[HloModule]: ...
  traceback: Traceback

def buffer_to_dlpack_managed_tensor(
    buffer: Buffer,
    take_ownership: bool = ...) -> Any: ...
def dlpack_managed_tensor_to_buffer(tensor: Any, client: Client) -> Buffer: ...

# === BEGIN py_traceback.cc

class Frame:
  file_name: str
  function_name: str
  function_line_start: int
  line_num: int
  def __repr__(self) -> str: ...

class Traceback:
  enabled: ClassVar[bool]
  @staticmethod
  def get_traceback() -> Traceback: ...
  frames: Sequence[Frame]
  def __str__(self) -> str: ...

# === END py_traceback.cc

class DistributedRuntimeService: ...
class DistributedRuntimeClient:
  def connect(self) -> _Status: ...
  def shutdown(self) -> _Status: ...

def get_distributed_runtime_service(
    address: str,
    options: Any) -> DistributedRuntimeService: ...
def get_distributed_runtime_client(
    address: str,
    options: Any) -> DistributedRuntimeClient: ...

def collect_garbage() -> None: ...

def is_optimized_build() -> bool: ...
