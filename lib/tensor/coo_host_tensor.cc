// Copyright 2020 The TensorFlow Runtime Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//===- coo_host_tensor.cc -------------------------------------------------===//
//
// This file implements the CooHostTensor class.
//
//===----------------------------------------------------------------------===//

#include "tfrt/tensor/coo_host_tensor.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "tfrt/host_context/async_value_ref.h"
#include "tfrt/host_context/host_context.h"
#include "tfrt/tensor/dense_host_tensor_view.h"
#include "tfrt/tensor/scalar_host_tensor.h"

namespace tfrt {

namespace {
template <typename DType>
void ConvertToDHTTensorHelper(const DenseHostTensor &indices,
                              const DenseHostTensor &values,
                              DenseHostTensor *result_tensor) {
  auto result_tensor_view = MutableDHTArrayView<DType>(result_tensor);
  const TensorMetadata &result_metadata = result_tensor->metadata();
  const auto &result_shape = result_metadata.shape;
  result_tensor_view.Fill(DType(0));
  auto indices_view = DHTIndexableView<int64_t, 2>(&indices);
  auto values_view = DHTIndexableView<DType, 1>(&values);
  for (int i = 0, e = values_view.FixedShape().GetNumElements(); i != e; ++i) {
    size_t offset = 0;
    size_t stride = 1;
    for (int j = result_shape.GetRank() - 1; j >= 0; --j) {
      assert(indices_view.ElementAt(i, j) < result_shape.GetDimensionSize(j));
      offset += stride * indices_view.ElementAt(i, j);
      stride *= result_shape.GetDimensionSize(j);
    }
    result_tensor_view[offset] = values_view.ElementAt(i);
  }
}
}  // namespace

AsyncValueRef<HostTensor> CooHostTensor::ConvertToHostTensor(
    HostContext *host, uint32_t allowed_formats) const {
  // Allows conversion to ScalarHostTensor if at most one element or if it is an
  // arbitrary-shaped COO tensor but all elements are zero.
  if (allowed_formats &
      (1 << static_cast<uint32_t>(Tensor::Subclass::ScalarHost))) {
    switch (dtype().kind()) {
      default:
        llvm_unreachable("can't happen");
#define DTYPE_NUMERIC(ENUM)                                             \
  case DType::ENUM:                                                     \
    if (NumElements() == 0) {                                           \
      return host->MakeConcreteAsyncValueRef<                           \
          ScalarHostTensor<TypeForDTypeKind<DType::ENUM>>>(metadata()); \
    } else if (NumElements() == 1) {                                    \
      return host->MakeConcreteAsyncValueRef<                           \
          ScalarHostTensor<TypeForDTypeKind<DType::ENUM>>>(             \
          metadata(),                                                   \
          DHTArrayView<TypeForDTypeKind<DType::ENUM>>(Values())[0]);    \
    } else if (Indices()->NumElements() == 0) {                         \
      return host->MakeConcreteAsyncValueRef<                           \
          ScalarHostTensor<TypeForDTypeKind<DType::ENUM>>>(             \
          metadata(), TypeForDTypeKind<DType::ENUM>(0));                \
    }
#include "tfrt/tensor/dtype.def"  // NOLINT
    }
  }

  // Otherwise, return a DenseHostTensor.
  assert(allowed_formats &
         (1 << static_cast<uint32_t>(Tensor::Subclass::DenseHost)));
  auto result = host->MakeUnconstructedAsyncValueRef<DenseHostTensor>();
  auto result_alloc = DenseHostTensor::CreateUninitialized(metadata(), host);
  if (!result_alloc)
    return host->MakeErrorAsyncValueRef(
        "out of memory converting coo tensor to dht tensor");
  auto &result_tensor = result_alloc.getValue();

  switch (dtype().kind()) {
    default:
      llvm_unreachable("can't happen");
#define DTYPE_NUMERIC(ENUM)                                                    \
  case DType::ENUM:                                                            \
    ConvertToDHTTensorHelper<TypeForDTypeKind<DType::ENUM>>(indices_, values_, \
                                                            &result_tensor);   \
    break;
#include "tfrt/tensor/dtype.def"  // NOLINT
  }

  result.emplace(std::move(result_tensor));
  return result;
}

void CooHostTensor::Print(raw_ostream &os) const {
  // Just dumps the flat values for now.
  os << "CooHostTensor dtype = " << dtype() << " shape = " << shape();
  os << ", indices = [";

  llvm::interleaveComma(DHTIndexableView<int64_t, 2>(Indices()).Elements(), os);
  os << "], values = [";

  auto element_size = dtype().GetHostSize();
  auto *data_ptr = static_cast<const char *>(Values()->data());
  for (ssize_t i = 0, e = Values()->NumElements(); i != e; ++i) {
    if (i != 0) os << ", ";
    dtype().Print(data_ptr + i * element_size, os);
  }
  os << "]\n";
}

}  // namespace tfrt
