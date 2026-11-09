#!/bin/bash
# Builds TFLite Micro (reference kernels) as a static library for the PC.
set -e
T=${1:?tflm dir}; O=${2:?out dir}; mkdir -p $O
TL=$T/tensorflow/lite; M=$TL/micro
SRCS="$M/debug_log.cc $M/flatbuffer_utils.cc $M/memory_helpers.cc $M/micro_allocation_info.cc $M/micro_allocator.cc
 $M/micro_context.cc $M/micro_interpreter_context.cc $M/micro_interpreter_graph.cc $M/micro_interpreter.cc $M/micro_log.cc
 $M/micro_op_resolver.cc $M/micro_profiler.cc $M/micro_resource_variable.cc $M/micro_time.cc $M/micro_utils.cc
 $M/recording_micro_allocator.cc $M/system_setup.cc
 $(ls $M/kernels/*.cc | grep -v _test.cc) $(ls $M/tflite_bridge/*.cc)
 $TL/kernels/kernel_util.cc $M/memory_planner/greedy_memory_planner.cc $M/memory_planner/linear_memory_planner.cc
 $M/arena_allocator/non_persistent_arena_buffer_allocator.cc $M/arena_allocator/persistent_arena_buffer_allocator.cc
 $M/arena_allocator/recording_single_arena_buffer_allocator.cc $M/arena_allocator/single_arena_buffer_allocator.cc
 $TL/core/c/common.cc $TL/core/api/flatbuffer_conversions.cc $TL/core/api/tensor_utils.cc $TL/kernels/internal/common.cc
 $TL/kernels/internal/quantization_util.cc $TL/kernels/internal/portable_tensor_utils.cc $TL/kernels/internal/tensor_utils.cc
 $TL/kernels/internal/tensor_ctypes.cc $TL/kernels/internal/reference/portable_tensor_utils.cc
 $TL/kernels/internal/reference/comparisons.cc $T/tensorflow/compiler/mlir/lite/core/api/error_reporter.cc
 $T/tensorflow/compiler/mlir/lite/schema/schema_utils.cc"
FLAGS="-std=c++17 -O2 -fno-rtti -fno-exceptions -fno-threadsafe-statics -DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON -I$T -I$T/third_party/flatbuffers/include -I$T/third_party/gemmlowp -I$T/third_party/ruy -I$T/third_party/kissfft -w"
for s in $SRCS; do [ -f $s ] || continue; o=$O/$(echo ${s#$T/} | tr / _).o; echo "$o: $s"; echo "	g++ $FLAGS -c \$< -o \$@"; done > $O/rules.mk
echo "all: $(for s in $SRCS; do [ -f $s ] && echo -n "$O/$(echo ${s#$T/} | tr / _).o "; done)" > $O/Makefile; cat $O/rules.mk >> $O/Makefile
make -s -j$(nproc) -f $O/Makefile all
ar rcs $O/libtflm.a $O/*.o
