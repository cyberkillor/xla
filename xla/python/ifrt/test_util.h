/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

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

#ifndef XLA_PYTHON_IFRT_TEST_UTIL_H_
#define XLA_PYTHON_IFRT_TEST_UTIL_H_

#include <functional>
#include <memory>

#include "xla/python/ifrt/client.h"
#include "xla/statusor.h"

namespace xla {
namespace ifrt {
namespace test_util {

// Registers an IFRT client factory function. Must be called only once.
void RegisterClientFactory(
    std::function<StatusOr<std::unique_ptr<Client>>()> factory);

// Returns true iff an IFRT client factory function has been registered.
bool IsClientFactoryRegistered();

// Gets a new IFRT client using the registered client factory.
StatusOr<std::unique_ptr<Client>> GetClient();

}  // namespace test_util
}  // namespace ifrt
}  // namespace xla

#endif  // XLA_PYTHON_IFRT_TEST_UTIL_H_
