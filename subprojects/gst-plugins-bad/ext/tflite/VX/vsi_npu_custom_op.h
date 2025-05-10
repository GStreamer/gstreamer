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
#ifndef TENSORFLOW_LITE_DELEGATES_VSI_NPU_CUSTOM_OP_H_
#define TENSORFLOW_LITE_DELEGATES_VSI_NPU_CUSTOM_OP_H_

#include "tensorflow/lite/c/common.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

static const char kNbgCustomOp[] = "vsi-npu";

typedef struct {
  size_t length;
  size_t input_count;
  size_t output_cout;
  char* binary;
} TfLiteVsiNpuParams;

#ifdef __cplusplus
namespace tflite {
  namespace ops {
    namespace custom {
#endif  // __cplusplus

TfLiteRegistration* Register_VSI_NPU_PRECOMPILED(void);

#ifdef __cplusplus
    }  // namespace custom
  }  // namespace ops
}  // namespace tflite

}  // extern "C"
#endif  // __cplusplus

#endif //TENSORFLOW_LITE_DELEGATES_VSI_NPU_CUSTOM_OP_H_
