/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/lite/tools/serialization/writer_lib.h"

#include <cstdlib>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>

#include <gtest/gtest.h>
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/kernels/subgraph_test_util.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/testing/util.h"

namespace tflite {

using subgraph_test_util::CheckIntTensor;
using subgraph_test_util::FillIntTensor;

std::string CreateFilePath(const std::string& file_name) {
  return std::string(getenv("TEST_TMPDIR")) + file_name;
}

// The bool param indicates whether we use SubgraphWriter(true) or
// ModelWriter(false) for the test
class SingleSubgraphTest : public ::testing::TestWithParam<bool> {
 protected:
  void WriteToFile(Interpreter* interpreter, const std::string& filename,
                   bool use_subgraph_writer) {
    if (use_subgraph_writer) {
      SubgraphWriter writer(&interpreter->primary_subgraph());
      CHECK_EQ(writer.Write(filename), kTfLiteOk);
    } else {
      ModelWriter writer(interpreter);
      CHECK_EQ(writer.Write(filename), kTfLiteOk);
    }
  }
};

TEST_P(SingleSubgraphTest, InvalidDestinations) {
  Interpreter interpreter;
  interpreter.AddTensors(3);
  float foo[] = {1, 2, 3};
  interpreter.SetTensorParametersReadWrite(0, kTfLiteFloat32, "a", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadOnly(
      1, kTfLiteFloat32, "b", {3}, TfLiteQuantization(),
      reinterpret_cast<char*>(foo), sizeof(foo));
  interpreter.SetTensorParametersReadWrite(2, kTfLiteFloat32, "c", {3},
                                           TfLiteQuantization());
  interpreter.SetInputs({0, 1});
  interpreter.SetOutputs({2});
  const char* initial_data = "";
  tflite::ops::builtin::BuiltinOpResolver resolver;
  TfLiteAddParams* builtin_data =
      reinterpret_cast<TfLiteAddParams*>(malloc(sizeof(TfLiteAddParams)));
  builtin_data->activation = kTfLiteActNone;
  builtin_data->pot_scale_int16 = false;
  const TfLiteRegistration* reg = resolver.FindOp(BuiltinOperator_ADD, 1);
  interpreter.AddNodeWithParameters({0, 1}, {2}, initial_data, 0,
                                    reinterpret_cast<void*>(builtin_data), reg);

  // Check if invalid filename is handled gracefully.
  if (GetParam()) {
    SubgraphWriter writer(&interpreter.primary_subgraph());
    CHECK_EQ(writer.Write(""), kTfLiteError);
  } else {
    ModelWriter writer(&interpreter);
    CHECK_EQ(writer.Write(""), kTfLiteError);
  }

  // Check if invalid buffer is handled gracefully.
  size_t size;
  if (GetParam()) {
    SubgraphWriter writer(&interpreter.primary_subgraph());
    CHECK_EQ(writer.GetBuffer(nullptr, &size), kTfLiteError);
  } else {
    ModelWriter writer(&interpreter);
    CHECK_EQ(writer.GetBuffer(nullptr, &size), kTfLiteError);
  }
}

TEST_P(SingleSubgraphTest, FloatModelTest) {
  Interpreter interpreter;
  interpreter.AddTensors(3);
  float foo[] = {1, 2, 3};
  interpreter.SetTensorParametersReadWrite(0, kTfLiteFloat32, "a", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadOnly(
      1, kTfLiteFloat32, "b", {3}, TfLiteQuantization(),
      reinterpret_cast<char*>(foo), sizeof(foo));
  interpreter.SetTensorParametersReadWrite(2, kTfLiteFloat32, "c", {3},
                                           TfLiteQuantization());
  interpreter.SetInputs({0, 1});
  interpreter.SetOutputs({2});
  const char* initial_data = "";
  tflite::ops::builtin::BuiltinOpResolver resolver;
  TfLiteAddParams* builtin_data =
      reinterpret_cast<TfLiteAddParams*>(malloc(sizeof(TfLiteAddParams)));
  builtin_data->activation = kTfLiteActNone;
  builtin_data->pot_scale_int16 = false;
  const TfLiteRegistration* reg = resolver.FindOp(BuiltinOperator_ADD, 1);
  interpreter.AddNodeWithParameters({0, 1}, {2}, initial_data, 0,
                                    reinterpret_cast<void*>(builtin_data), reg);

  const std::string test_file = CreateFilePath("test_float.tflite");
  WriteToFile(&interpreter, test_file, GetParam());
  std::unique_ptr<FlatBufferModel> model =
      FlatBufferModel::BuildFromFile(test_file.c_str());
  InterpreterBuilder builder(*model, resolver);
  std::unique_ptr<Interpreter> new_interpreter;
  builder(&new_interpreter);
  CHECK_EQ(new_interpreter->AllocateTensors(), kTfLiteOk);
}

// Tests writing only a portion of the subgraph.
TEST_P(SingleSubgraphTest, CustomInputOutputTest) {
  Interpreter interpreter;
  interpreter.AddTensors(4);
  constexpr float kFoo[] = {1, 2, 3};
  interpreter.SetTensorParametersReadWrite(0, kTfLiteFloat32, "a", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadOnly(
      1, kTfLiteFloat32, "b", {3}, TfLiteQuantization(),
      reinterpret_cast<const char*>(kFoo), sizeof(kFoo));
  interpreter.SetTensorParametersReadWrite(2, kTfLiteFloat32, "c", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadWrite(3, kTfLiteFloat32, "d", {3},
                                           TfLiteQuantization());
  interpreter.SetInputs({0, 1});
  interpreter.SetOutputs({3});

  // Add two ops: Add and Relu
  const char* initial_data = "";
  tflite::ops::builtin::BuiltinOpResolver resolver;
  TfLiteAddParams* builtin_data =
      reinterpret_cast<TfLiteAddParams*>(malloc(sizeof(TfLiteAddParams)));
  builtin_data->activation = kTfLiteActNone;
  builtin_data->pot_scale_int16 = false;
  const TfLiteRegistration* reg = resolver.FindOp(BuiltinOperator_ADD, 1);
  interpreter.AddNodeWithParameters({0, 1}, {2}, initial_data, 0,
                                    reinterpret_cast<void*>(builtin_data), reg);

  const TfLiteRegistration* reg2 = resolver.FindOp(BuiltinOperator_RELU, 1);
  interpreter.AddNodeWithParameters({2}, {3}, nullptr, 0, nullptr, reg2);

  // Only write the second op.
  const std::string test_file = CreateFilePath("test_custom.tflite");
  SubgraphWriter writer(&interpreter.primary_subgraph());
  EXPECT_EQ(writer.SetCustomInputOutput(/*inputs=*/{2}, /*outputs=*/{3},
                                        /*execution_plan=*/{1}),
            kTfLiteOk);
  writer.SetUnusedTensors({0, 1});
  writer.Write(test_file);

  std::unique_ptr<FlatBufferModel> model =
      FlatBufferModel::BuildFromFile(test_file.c_str());
  InterpreterBuilder builder(*model, resolver);
  std::unique_ptr<Interpreter> new_interpreter;
  builder(&new_interpreter);
  ASSERT_EQ(new_interpreter->AllocateTensors(), kTfLiteOk);
}

TEST_P(SingleSubgraphTest, CustomInputOutputErrorCasesTest) {
  Interpreter interpreter;
  interpreter.AddTensors(5);
  constexpr float kFoo[] = {1, 2, 3};
  interpreter.SetTensorParametersReadWrite(0, kTfLiteFloat32, "a", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadOnly(
      1, kTfLiteFloat32, "b", {3}, TfLiteQuantization(),
      reinterpret_cast<const char*>(kFoo), sizeof(kFoo));
  interpreter.SetTensorParametersReadWrite(2, kTfLiteFloat32, "c", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadWrite(3, kTfLiteFloat32, "d", {3},
                                           TfLiteQuantization());
  interpreter.SetTensorParametersReadWrite(4, kTfLiteFloat32, "e", {3},
                                           TfLiteQuantization());
  interpreter.SetInputs({0, 1});
  interpreter.SetOutputs({4});

  // Add three ops.
  const char* initial_data = "";
  tflite::ops::builtin::BuiltinOpResolver resolver;
  TfLiteAddParams* builtin_data =
      reinterpret_cast<TfLiteAddParams*>(malloc(sizeof(TfLiteAddParams)));
  builtin_data->activation = kTfLiteActNone;
  builtin_data->pot_scale_int16 = false;
  const TfLiteRegistration* reg = resolver.FindOp(BuiltinOperator_ADD, 1);
  interpreter.AddNodeWithParameters({0, 1}, {2}, initial_data, 0,
                                    reinterpret_cast<void*>(builtin_data), reg);

  const TfLiteRegistration* reg2 = resolver.FindOp(BuiltinOperator_RELU, 1);
  interpreter.AddNodeWithParameters({2}, {3}, nullptr, 0, nullptr, reg2);

  const TfLiteRegistration* reg3 = resolver.FindOp(BuiltinOperator_RELU6, 1);
  interpreter.AddNodeWithParameters({3}, {4}, nullptr, 0, nullptr, reg3);

  SubgraphWriter writer(&interpreter.primary_subgraph());

  // Test wrong input.
  EXPECT_EQ(writer.SetCustomInputOutput(/*inputs=*/{2}, /*outputs=*/{3},
                                        /*execution_plan=*/{0, 1}),
            kTfLiteError);
  // Test wrong output.
  EXPECT_EQ(writer.SetCustomInputOutput(/*inputs=*/{0, 1}, /*outputs=*/{4},
                                        /*execution_plan=*/{0, 1}),
            kTfLiteError);
  // Test a valid case.
  EXPECT_EQ(writer.SetCustomInputOutput(/*inputs=*/{0, 1}, /*outputs=*/{3},
                                        /*execution_plan=*/{0, 1}),
            kTfLiteOk);
}

TEST_P(SingleSubgraphTest, PerTensorQuantizedModelTest) {
  Interpreter interpreter;
  interpreter.AddTensors(3);
  interpreter.SetTensorParametersReadWrite(
      0, kTfLiteUInt8, "a", {3}, TfLiteQuantizationParams({1 / 256., 128}));
  interpreter.SetTensorParametersReadWrite(
      1, kTfLiteUInt8, "b", {3}, TfLiteQuantizationParams({1 / 256., 128}));
  interpreter.SetTensorParametersReadWrite(
      2, kTfLiteUInt8, "c", {3}, TfLiteQuantizationParams({1 / 256., 128}));
  interpreter.SetInputs({0, 1});
  interpreter.SetOutputs({2});
  const char* initial_data = "";
  tflite::ops::builtin::BuiltinOpResolver resolver;
  TfLiteAddParams* builtin_data =
      reinterpret_cast<TfLiteAddParams*>(malloc(sizeof(TfLiteAddParams)));
  builtin_data->activation = kTfLiteActNone;
  builtin_data->pot_scale_int16 = false;
  const TfLiteRegistration* reg = resolver.FindOp(BuiltinOperator_ADD, 1);
  interpreter.AddNodeWithParameters({0, 1}, {2}, initial_data, 0,
                                    reinterpret_cast<void*>(builtin_data), reg);

  const std::string test_file = CreateFilePath("test_uint8.tflite");
  WriteToFile(&interpreter, test_file, GetParam());
  std::unique_ptr<FlatBufferModel> model =
      FlatBufferModel::BuildFromFile(test_file.c_str());
  InterpreterBuilder builder(*model, resolver);
  std::unique_ptr<Interpreter> new_interpreter;
  builder(&new_interpreter);
  CHECK_EQ(new_interpreter->AllocateTensors(), kTfLiteOk);
}

INSTANTIATE_TEST_SUITE_P(Writer, SingleSubgraphTest, ::testing::Bool());

struct ReshapeTestPattern {
  int num_inputs;
  bool is_param_valid;
};

class ReshapeLayerTest : public ::testing::TestWithParam<ReshapeTestPattern> {};

TEST_P(ReshapeLayerTest, ReshapeLayerTest) {
  const auto param = GetParam();
  Interpreter interpreter;
  const int total_tensors = param.num_inputs + 1;
  interpreter.AddTensors(total_tensors);
  int output_shape[] = {1, 2, 3};
  interpreter.SetTensorParametersReadWrite(/*tensor_index=*/0, kTfLiteFloat32,
                                           /*name=*/"a", /*dims=*/{6},
                                           TfLiteQuantization());
  ASSERT_LE(param.num_inputs, 2);
  if (param.num_inputs == 2) {
    interpreter.SetTensorParametersReadOnly(
        /*tensor_index=*/1, kTfLiteInt32, /*name=*/"b", /*dims=*/{3},
        TfLiteQuantization(), reinterpret_cast<char*>(output_shape),
        sizeof(output_shape));
  }
  interpreter.SetTensorParametersReadWrite(/*tensor_index=*/total_tensors - 1,
                                           kTfLiteFloat32, /*name=*/"c",
                                           /*dims=*/{3}, TfLiteQuantization());

  std::vector<int> input_tensors(param.num_inputs);
  std::iota(input_tensors.begin(), input_tensors.end(), 0);

  interpreter.SetInputs(input_tensors);
  interpreter.SetOutputs({total_tensors - 1});
  const char* initial_data = "";
  tflite::ops::builtin::BuiltinOpResolver resolver;
  TfLiteReshapeParams* builtin_data = reinterpret_cast<TfLiteReshapeParams*>(
      malloc(sizeof(TfLiteReshapeParams)));
  if (param.is_param_valid) {
    builtin_data->num_dimensions = 3;
    for (int dim = 0; dim < builtin_data->num_dimensions; ++dim) {
      builtin_data->shape[dim] = output_shape[dim];
    }
  }
  const TfLiteRegistration* reg = resolver.FindOp(BuiltinOperator_RESHAPE, 1);
  interpreter.AddNodeWithParameters(input_tensors,
                                    /*outputs=*/{total_tensors - 1},
                                    initial_data, /*init_data_size=*/0,
                                    reinterpret_cast<void*>(builtin_data), reg);

  SubgraphWriter writer(&interpreter.primary_subgraph());
  std::stringstream ss;
  ss << CreateFilePath("test_reshape_") << param.num_inputs
     << param.is_param_valid << ".tflite";
  std::string filename = ss.str();
  writer.Write(filename);
  std::unique_ptr<FlatBufferModel> model =
      FlatBufferModel::BuildFromFile(filename.c_str());
  InterpreterBuilder builder(*model, resolver);
  std::unique_ptr<Interpreter> new_interpreter;
  builder(&new_interpreter);
  ASSERT_EQ(new_interpreter->AllocateTensors(), kTfLiteOk);
}

INSTANTIATE_TEST_SUITE_P(
    Writer, ReshapeLayerTest,
    ::testing::Values(ReshapeTestPattern{/*num_inputs=*/2,
                                         /*is_param_valid=*/true},
                      ReshapeTestPattern{/*num_inputs=*/2,
                                         /*is_param_valid=*/false},
                      ReshapeTestPattern{/*num_inputs=*/1,
                                         /*is_param_valid=*/true}),
    [](const ::testing::TestParamInfo<ReshapeLayerTest::ParamType>& info) {
      std::stringstream ss;
      ss << "num_inputs_" << info.param.num_inputs << "_valid_param_"
         << info.param.is_param_valid;
      std::string name = ss.str();
      return name;
    });

class WhileTest : public subgraph_test_util::ControlFlowOpTest {};

// The test builds a model that produces the i-th number of
// triangular number sequence: 1, 3, 6, 10, 15, 21, 28.
TEST_F(WhileTest, TestTriangularNumberSequence) {
  const int kSeqNumber = 4;
  const int kExpectedValue = 15;

  interpreter_.reset(new Interpreter);
  interpreter_->AddSubgraphs(2);
  builder_->BuildLessEqualCondSubgraph(interpreter_->subgraph(1), kSeqNumber);
  builder_->BuildAccumulateLoopBodySubgraph(interpreter_->subgraph(2));
  builder_->BuildWhileSubgraph(&interpreter_->primary_subgraph());

  interpreter_->ResizeInputTensor(interpreter_->inputs()[0], {1});
  interpreter_->ResizeInputTensor(interpreter_->inputs()[1], {1});
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[0]), {1});
  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[1]), {1});

  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
  TfLiteTensor* output1 = interpreter_->tensor(interpreter_->outputs()[0]);
  CheckIntTensor(output1, {1}, {kSeqNumber + 1});
  TfLiteTensor* output2 = interpreter_->tensor(interpreter_->outputs()[1]);
  CheckIntTensor(output2, {1}, {kExpectedValue});

  // Now serialize & deserialize model into a new Interpreter.
  ModelWriter writer(interpreter_.get());
  const std::string test_file = CreateFilePath("test_while.tflite");
  writer.Write(test_file);
  std::unique_ptr<FlatBufferModel> model =
      FlatBufferModel::BuildFromFile(test_file.c_str());
  tflite::ops::builtin::BuiltinOpResolver resolver;
  InterpreterBuilder builder(*model, resolver);
  std::unique_ptr<Interpreter> new_interpreter;
  builder(&new_interpreter);

  // Check deserialized model.
  new_interpreter->ResizeInputTensor(interpreter_->inputs()[0], {1});
  new_interpreter->ResizeInputTensor(interpreter_->inputs()[1], {1});
  ASSERT_EQ(new_interpreter->AllocateTensors(), kTfLiteOk);
  FillIntTensor(new_interpreter->tensor(interpreter_->inputs()[0]), {1});
  FillIntTensor(new_interpreter->tensor(interpreter_->inputs()[1]), {1});
  ASSERT_EQ(new_interpreter->Invoke(), kTfLiteOk);
  output1 = new_interpreter->tensor(interpreter_->outputs()[0]);
  CheckIntTensor(output1, {1}, {kSeqNumber + 1});
  output2 = new_interpreter->tensor(interpreter_->outputs()[1]);
  CheckIntTensor(output2, {1}, {kExpectedValue});
}

}  // namespace tflite

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
