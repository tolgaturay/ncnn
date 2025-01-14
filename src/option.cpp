// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "option.h"
#include "cpu.h"

namespace ncnn {

Option::Option()
{
    lightmode = true;
    num_threads = get_cpu_count();
    blob_allocator = 0;
    workspace_allocator = 0;

#if NCNN_VULKAN
    blob_vkallocator = 0;
    workspace_vkallocator = 0;
    staging_vkallocator = 0;
#endif // NCNN_VULKAN

    use_winograd_convolution = true;
    use_sgemm_convolution = true;
    use_int8_inference = true;
    use_vulkan_compute = false;// TODO enable me

    use_fp16_packed = false;// TODO enable me
    use_fp16_storage = true;
    use_fp16_arithmetic = false;
    use_int8_storage = true;
    use_int8_arithmetic = false;

    // sanitize
    if (num_threads <= 0)
        num_threads = 1;
}

} // namespace ncnn
