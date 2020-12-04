/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "llvm/ADT/STLExtras.h"
#include "mlir/Conversion/GPUCommon/GPUCommonPass.h"  // from @llvm-project
#include "mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h"  // from @llvm-project
#include "mlir/Conversion/StandardToLLVM/ConvertStandardToLLVMPass.h"  // from @llvm-project
#include "mlir/Dialect/GPU/GPUDialect.h"  // from @llvm-project
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"  // from @llvm-project
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"  // from @llvm-project
#include "mlir/Dialect/StandardOps/IR/Ops.h"  // from @llvm-project
#include "mlir/Dialect/StandardOps/Transforms/Passes.h"  // from @llvm-project
#include "mlir/IR/BuiltinOps.h"  // from @llvm-project
#include "mlir/Transforms/DialectConversion.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/tools/kernel_gen/ir/tf_framework_ops.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/passes.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/rewriters.h"

namespace mlir {
namespace kernel_gen {
namespace transforms {
namespace {

constexpr StringRef kTfWrapperLibaryLaunchHelperName =
    "tfKernelGenLaunchKernel";

#define GEN_PASS_CLASSES
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/kernel_gen_passes.h.inc"

/// A rewrite patter to convert gpu.launch_func operations into a runtime call
/// for the TensorFlow runtime.
class ConvertLaunchFuncOpToTfRuntimeCallPattern
    : public ConvertOpToLLVMPattern<gpu::LaunchFuncOp> {
 public:
  ConvertLaunchFuncOpToTfRuntimeCallPattern(LLVMTypeConverter &type_converter,
                                            StringRef gpu_binary_annotation)
      : ConvertOpToLLVMPattern<gpu::LaunchFuncOp>(type_converter),
        gpu_binary_annotation_(gpu_binary_annotation) {}

 private:
  Value generateParamsArray(gpu::LaunchFuncOp launch_op,
                            ArrayRef<Value> operands, OpBuilder &builder) const;
  Value generateKernelNameConstant(StringRef moduleName, StringRef name,
                                   Location loc, OpBuilder &builder) const;

  LogicalResult matchAndRewrite(
      gpu::LaunchFuncOp launch_op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override;

  MLIRContext *context_ = &this->typeConverter.getContext();

  LLVM::LLVMType llvm_void_type_ = LLVM::LLVMType::getVoidTy(context_);
  LLVM::LLVMType llvm_pointer_type_ = LLVM::LLVMType::getInt8PtrTy(context_);
  LLVM::LLVMType llvm_pointer_pointer_type_ = llvm_pointer_type_.getPointerTo();
  LLVM::LLVMType llvm_int8_type_ = LLVM::LLVMType::getInt8Ty(context_);
  LLVM::LLVMType llvm_int32_type_ = LLVM::LLVMType::getInt32Ty(context_);
  LLVM::LLVMType llvm_int64_type_ = LLVM::LLVMType::getInt64Ty(context_);
  LLVM::LLVMType llvm_intptr_type_ = LLVM::LLVMType::getIntNTy(
      context_, this->typeConverter.getPointerBitwidth(0));

  llvm::SmallString<32> gpu_binary_annotation_;
};

// Creates a struct containing all kernel parameters on the stack and returns
// an array of type-erased pointers to the fields of the struct. The array can
// then be passed to the CUDA / ROCm (HIP) kernel launch calls.
// The generated code is essentially as follows:
//
// %struct = alloca(sizeof(struct { Parameters... }))
// %array = alloca(NumParameters * sizeof(void *))
// for (i : [0, NumParameters))
//   %fieldPtr = llvm.getelementptr %struct[0, i]
//   llvm.store parameters[i], %fieldPtr
//   %elementPtr = llvm.getelementptr %array[i]
//   llvm.store %fieldPtr, %elementPtr
// return %array
Value ConvertLaunchFuncOpToTfRuntimeCallPattern::generateParamsArray(
    gpu::LaunchFuncOp launch_op, ArrayRef<Value> operands,
    OpBuilder &builder) const {
  auto loc = launch_op.getLoc();
  auto num_kernel_operands = launch_op.getNumKernelOperands();
  auto arguments = typeConverter.promoteOperands(
      loc, launch_op.getOperands().take_back(num_kernel_operands),
      operands.take_back(num_kernel_operands), builder);
  auto num_arguments = arguments.size();
  SmallVector<LLVM::LLVMType, 4> argument_types;
  argument_types.reserve(num_arguments);
  for (auto argument : arguments)
    argument_types.push_back(argument.getType().cast<LLVM::LLVMType>());
  auto struct_type =
      LLVM::LLVMType::createStructTy(argument_types, StringRef());
  auto one = builder.create<LLVM::ConstantOp>(loc, llvm_int32_type_,
                                              builder.getI32IntegerAttr(1));
  auto struct_ptr = builder.create<LLVM::AllocaOp>(
      loc, struct_type.getPointerTo(), one, /*alignment=*/0);
  auto array_size = builder.create<LLVM::ConstantOp>(
      loc, llvm_int32_type_, builder.getI32IntegerAttr(num_arguments));
  auto array_ptr = builder.create<LLVM::AllocaOp>(
      loc, llvm_pointer_pointer_type_, array_size, /*alignment=*/0);
  auto zero = builder.create<LLVM::ConstantOp>(loc, llvm_int32_type_,
                                               builder.getI32IntegerAttr(0));
  for (auto en : llvm::enumerate(arguments)) {
    auto index = builder.create<LLVM::ConstantOp>(
        loc, llvm_int32_type_, builder.getI32IntegerAttr(en.index()));
    auto field_ptr = builder.create<LLVM::GEPOp>(
        loc, argument_types[en.index()].getPointerTo(), struct_ptr,
        ArrayRef<Value>{zero, index.getResult()});
    builder.create<LLVM::StoreOp>(loc, en.value(), field_ptr);
    auto element_ptr = builder.create<LLVM::GEPOp>(
        loc, llvm_pointer_pointer_type_, array_ptr, index.getResult());
    auto casted =
        builder.create<LLVM::BitcastOp>(loc, llvm_pointer_type_, field_ptr);
    builder.create<LLVM::StoreOp>(loc, casted, element_ptr);
  }
  return array_ptr;
}

// Emits LLVM IR to launch a kernel function. Expects the module that contains
// the compiled kernel function as a cubin in the 'nvvm.cubin' attribute, or a
// hsaco in the 'rocdl.hsaco' attribute of the kernel function in the IR.
//
// %0 = call %binarygetter
// %1 = <pointer to kernel function name>
// %2 = <see generateParamsArray>
// call %tfLaunchKernel(%ctx, %0, %1, <launch_op operands 0..5>, %2)
LogicalResult ConvertLaunchFuncOpToTfRuntimeCallPattern::matchAndRewrite(
    gpu::LaunchFuncOp launch_op, ArrayRef<Value> operands,
    ConversionPatternRewriter &rewriter) const {
  if (!launch_op.asyncDependencies().empty() || launch_op.asyncToken()) {
    return rewriter.notifyMatchFailure(
        launch_op, "Cannot convert with async dependency or result.");
  }

  Location loc = launch_op.getLoc();

  // Create an LLVM global with CUBIN extracted from the kernel annotation and
  // obtain a pointer to the first byte in it.
  auto kernel_module = SymbolTable::lookupNearestSymbolFrom<gpu::GPUModuleOp>(
      launch_op, launch_op.getKernelModuleName());
  assert(kernel_module && "expected a kernel module");

  auto binary_attr =
      kernel_module.getAttrOfType<StringAttr>(gpu_binary_annotation_);
  if (!binary_attr) {
    kernel_module.emitOpError()
        << "missing " << gpu_binary_annotation_ << " attribute";
    return failure();
  }

  // Create a global for the module blob.
  SmallString<128> name_buffer(kernel_module.getName());
  name_buffer.append("_blob");
  Value module_blob =
      LLVM::createGlobalString(loc, rewriter, name_buffer.str(),
                               binary_attr.getValue(), LLVM::Linkage::Internal);

  // Make sure the trailing zero is included in the constant.
  auto kernel_name = launch_op.getKernelName();
  SmallString<128> kernel_name_buffer(kernel_name);
  kernel_name_buffer.push_back('\0');

  // Create a global for the kernel name.
  SmallString<128> kernel_name_global_name_buffer;
  auto kernel_name_global_name =
      (kernel_module.getName() + "_" + kernel_name + "_kernel_name")
          .toStringRef(kernel_name_global_name_buffer);
  auto kernel_name_global =
      LLVM::createGlobalString(loc, rewriter, kernel_name_global_name,
                               kernel_name_buffer, LLVM::Linkage::Internal);

  auto adaptor = gpu::LaunchFuncOpAdaptor(
      operands, launch_op.getOperation()->getAttrDictionary());

  // The TensorFlow OpKernelContext is the first argument of the surrounding
  // LLVMFunc.
  Value context_arg =
      launch_op.getParentOfType<LLVM::LLVMFuncOp>().getArgument(0);
  auto kernel_params = generateParamsArray(launch_op, operands, rewriter);

  auto function = SymbolTable::lookupNearestSymbolFrom<LLVM::LLVMFuncOp>(
      launch_op, kTfWrapperLibaryLaunchHelperName);
  if (!function) {
    PatternRewriter::InsertionGuard guard(rewriter);
    auto function_type = LLVM::LLVMFunctionType::get(
        llvm_void_type_,
        {
            llvm_pointer_type_,         /* void* context */
            llvm_pointer_type_,         /* void* module_blob */
            llvm_pointer_type_,         /* void* function_name */
            llvm_intptr_type_,          /* intptr_t grid_x_dim */
            llvm_intptr_type_,          /* intptr_t grid_y_dim */
            llvm_intptr_type_,          /* intptr_t grid_z_dim */
            llvm_intptr_type_,          /* intptr_t block_x_dim */
            llvm_intptr_type_,          /* intptr_t block_y_dim */
            llvm_intptr_type_,          /* intptr_t block_z_dim */
            llvm_pointer_pointer_type_, /* void **kernel_params */
        });
    rewriter.setInsertionPointToStart(
        launch_op.getParentOfType<ModuleOp>().getBody());
    function = rewriter.create<LLVM::LLVMFuncOp>(
        loc, kTfWrapperLibaryLaunchHelperName, function_type);
  }
  rewriter.create<LLVM::CallOp>(
      loc, llvm_void_type_, rewriter.getSymbolRefAttr(function),
      ArrayRef<Value>{
          context_arg, module_blob, kernel_name_global, adaptor.gridSizeX(),
          adaptor.gridSizeY(), adaptor.gridSizeZ(), adaptor.blockSizeX(),
          adaptor.blockSizeY(), adaptor.blockSizeZ(), kernel_params});

  rewriter.eraseOp(launch_op);
  return success();
}

class TFKernelToLLVMPass : public TFKernelToLLVMPassBase<TFKernelToLLVMPass> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }

 public:
  explicit TFKernelToLLVMPass(StringRef blob_annotation) {
    if (!blob_annotation.empty()) {
      blob_annotation_ = blob_annotation.str();
    }
  }

  void runOnOperation() override {
    ModuleOp m = getOperation();

    // Populate type conversions.
    MLIRContext *ctx = m.getContext();
    LLVMTypeConverter type_converter(ctx);
    type_converter.addConversion([&](tf_framework::OpKernelContextType type) {
      return LLVM::LLVMType::getInt8PtrTy(ctx);
    });

    // Populate patterns.
    OwningRewritePatternList patterns;

    populateStdExpandOpsPatterns(ctx, patterns);
    populateStdToLLVMConversionPatterns(type_converter, patterns);
    tf_framework::PopulateTFFrameworkToLLVMConversionPatterns(&type_converter,
                                                              &patterns);
    patterns.insert<ConvertLaunchFuncOpToTfRuntimeCallPattern>(
        type_converter, blob_annotation_);
    // Set target.
    ConversionTarget target(*ctx);
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addIllegalDialect<gpu::GPUDialect, StandardOpsDialect,
                             tf_framework::TFFrameworkDialect>();
    target.addIllegalOp<LLVM::DialectCastOp>();
    // Mark modules as legal.
    target.addLegalOp<ModuleOp, ModuleTerminatorOp, gpu::GPUModuleOp>();
    // Do not look into gpu modules, only consider host-side.
    target.markOpRecursivelyLegal<gpu::GPUModuleOp>();

    if (failed(applyFullConversion(m, target, std::move(patterns)))) {
      signalPassFailure();
    }

    // Finally, strip the GPU modules, as they are no longer needed.
    for (auto op : llvm::make_early_inc_range(m.getOps<gpu::GPUModuleOp>())) {
      op.erase();
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<ModuleOp> > CreateTFKernelToLLVMPass(
    StringRef blob_annotation) {
  return std::make_unique<TFKernelToLLVMPass>(blob_annotation);
}

}  // namespace transforms
}  // namespace kernel_gen
}  // namespace mlir
