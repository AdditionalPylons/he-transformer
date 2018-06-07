/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#pragma once

#include <memory>
#include <vector>

#include "ngraph/coordinate_transform.hpp"

namespace ngraph
{
    namespace element
    {
        class Type;
    }
    namespace runtime
    {
        namespace he
        {
            class HEBackend;
            class HECiphertext;
            namespace kernel
            {
                void avg_pool(const std::vector<std::shared_ptr<runtime::he::HECiphertext>>& arg,
                              std::vector<std::shared_ptr<runtime::he::HECiphertext>>& out,
                              const Shape& arg_shape,
                              const Shape& out_shape,
                              const Shape& window_shape,
                              const Strides& window_movement_strides,
                              const Shape& padding_below,
                              const Shape& padding_above,
                              bool include_padding_in_avg_computation,
                              const element::Type& type,
                              const std::shared_ptr<runtime::he::HEBackend>& he_backend);

                void avg_pool(const std::vector<std::shared_ptr<runtime::he::HEPlaintext>>& arg,
                              std::vector<std::shared_ptr<runtime::he::HEPlaintext>>& out,
                              const Shape& arg_shape,
                              const Shape& out_shape,
                              const Shape& window_shape,
                              const Strides& window_movement_strides,
                              const Shape& padding_below,
                              const Shape& padding_above,
                              bool include_padding_in_avg_computation,
                              const element::Type& type,
                              const std::shared_ptr<runtime::he::HEBackend>& he_backend);
            }
        }
    }
}