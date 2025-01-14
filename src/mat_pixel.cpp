// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "mat.h"
#include <limits.h>
#include <math.h>
#include <algorithm>
#if __ARM_NEON
#include <arm_neon.h>
#endif // __ARM_NEON
#include "platform.h"

namespace ncnn {

#if NCNN_PIXEL
static Mat from_rgb(const unsigned char* rgb, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 3, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr0 = m.channel(0);
    float* ptr1 = m.channel(1);
    float* ptr2 = m.channel(2);

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x8x3_t _rgb = vld3_u8(rgb);
        uint16x8_t _r16 = vmovl_u8(_rgb.val[0]);
        uint16x8_t _g16 = vmovl_u8(_rgb.val[1]);
        uint16x8_t _b16 = vmovl_u8(_rgb.val[2]);

        float32x4_t _rlow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_r16)));
        float32x4_t _rhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_r16)));
        float32x4_t _glow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_g16)));
        float32x4_t _ghigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_g16)));
        float32x4_t _blow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_b16)));
        float32x4_t _bhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_b16)));

        vst1q_f32(ptr0, _rlow);
        vst1q_f32(ptr0+4, _rhigh);
        vst1q_f32(ptr1, _glow);
        vst1q_f32(ptr1+4, _ghigh);
        vst1q_f32(ptr2, _blow);
        vst1q_f32(ptr2+4, _bhigh);

        rgb += 3*8;
        ptr0 += 8;
        ptr1 += 8;
        ptr2 += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld3.u8    {d0-d2}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u8   q10, d2             \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vmovl.u16  q8, d20             \n"
        "vmovl.u16  q9, d21             \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "vcvt.f32.u32   q8, q8          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "vcvt.f32.u32   q9, q9          \n"
        "vst1.f32   {d4-d7}, [%3 :128]! \n"
        "vst1.f32   {d16-d19}, [%4 :128]!\n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgb),    // %1
          "=r"(ptr0),   // %2
          "=r"(ptr1),   // %3
          "=r"(ptr2)    // %4
        : "0"(nn),
          "1"(rgb),
          "2"(ptr0),
          "3"(ptr1),
          "4"(ptr2)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9", "q10"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr0 = rgb[0];
        *ptr1 = rgb[1];
        *ptr2 = rgb[2];

        rgb += 3;
        ptr0++;
        ptr1++;
        ptr2++;
    }

    return m;
}

static void to_rgb(const Mat& m, unsigned char* rgb)
{
    const float* ptr0 = m.channel(0);
    const float* ptr1 = m.channel(1);
    const float* ptr2 = m.channel(2);

    int size = m.w * m.h;

#define SATURATE_CAST_UCHAR(X) (unsigned char)::std::min(::std::max((int)(X), 0), 255);

    int remain = size;

    for (; remain>0; remain--)
    {
        rgb[0] = SATURATE_CAST_UCHAR(*ptr0);
        rgb[1] = SATURATE_CAST_UCHAR(*ptr1);
        rgb[2] = SATURATE_CAST_UCHAR(*ptr2);

        rgb += 3;
        ptr0++;
        ptr1++;
        ptr2++;
    }

#undef SATURATE_CAST_UCHAR
}

static Mat from_gray(const unsigned char* gray, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 1, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr = m;

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 4;
    int remain = size - (nn << 4);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x16_t _gray = vld1q_u8(gray);
        uint16x8_t _gray16_0 = vmovl_u8(vget_low_u8(_gray));
        uint16x8_t _gray16_1 = vmovl_u8(vget_high_u8(_gray));

        float32x4_t _graylow_0 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_gray16_0)));
        float32x4_t _grayhigh_0 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_gray16_0)));
        float32x4_t _graylow_1 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_gray16_1)));
        float32x4_t _grayhigh_1 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_gray16_1)));

        vst1q_f32(ptr, _graylow_0);
        vst1q_f32(ptr+4, _grayhigh_0);
        vst1q_f32(ptr+8, _graylow_1);
        vst1q_f32(ptr+12, _grayhigh_1);

        gray += 16;
        ptr += 16;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #128]          \n"
        "vld1.u8    {d0,d1}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "vst1.f32   {d4-d7}, [%2 :128]! \n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(gray),   // %1
          "=r"(ptr)     // %2
        : "0"(nn),
          "1"(gray),
          "2"(ptr)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr = *gray;

        gray++;
        ptr++;
    }

    return m;
}

static void to_gray(const Mat& m, unsigned char* gray)
{
    const float* ptr = m;

    int size = m.w * m.h;

#define SATURATE_CAST_UCHAR(X) (unsigned char)::std::min(::std::max((int)(X), 0), 255);

    int remain = size;

    for (; remain>0; remain--)
    {
        *gray = SATURATE_CAST_UCHAR(*ptr);

        gray++;
        ptr++;
    }

#undef SATURATE_CAST_UCHAR
}

static Mat from_rgba(const unsigned char* rgba, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 4, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr0 = m.channel(0);
    float* ptr1 = m.channel(1);
    float* ptr2 = m.channel(2);
    float* ptr3 = m.channel(3);

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x8x4_t _rgba = vld4_u8(rgba);
        int16x8_t _r16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[0]));
        int16x8_t _g16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[1]));
        int16x8_t _b16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[2]));
        int16x8_t _a16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[3]));

        float32x4_t _rlow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_r16)));
        float32x4_t _rhigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_r16)));
        float32x4_t _glow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_g16)));
        float32x4_t _ghigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_g16)));
        float32x4_t _blow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_b16)));
        float32x4_t _bhigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_b16)));
        float32x4_t _alow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_a16)));
        float32x4_t _ahigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_a16)));

        vst1q_f32(ptr0, _rlow);
        vst1q_f32(ptr0+4, _rhigh);
        vst1q_f32(ptr1, _glow);
        vst1q_f32(ptr1+4, _ghigh);
        vst1q_f32(ptr2, _blow);
        vst1q_f32(ptr2+4, _bhigh);
        vst1q_f32(ptr3, _alow);
        vst1q_f32(ptr3+4, _ahigh);

        rgba += 4*8;
        ptr0 += 8;
        ptr1 += 8;
        ptr2 += 8;
        ptr3 += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld4.u8    {d0-d3}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u8   q10, d2             \n"
        "vmovl.u8   q11, d3             \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vmovl.u16  q8, d20             \n"
        "vmovl.u16  q9, d21             \n"
        "vmovl.u16  q10, d22            \n"
        "vmovl.u16  q11, d23            \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "vcvt.f32.u32   q8, q8          \n"
        "vcvt.f32.u32   q9, q9          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "vcvt.f32.u32   q10, q10        \n"
        "vcvt.f32.u32   q11, q11        \n"
        "vst1.f32   {d4-d7}, [%3 :128]! \n"
        "vst1.f32   {d16-d19}, [%4 :128]!\n"
        "vst1.f32   {d20-d23}, [%5 :128]!\n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgba),   // %1
          "=r"(ptr0),   // %2
          "=r"(ptr1),   // %3
          "=r"(ptr2),   // %4
          "=r"(ptr3)    // %5
        : "0"(nn),
          "1"(rgba),
          "2"(ptr0),
          "3"(ptr1),
          "4"(ptr2),
          "5"(ptr3)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9", "q10", "q11"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr0 = rgba[0];
        *ptr1 = rgba[1];
        *ptr2 = rgba[2];
        *ptr3 = rgba[3];

        rgba += 4;
        ptr0++;
        ptr1++;
        ptr2++;
        ptr3++;
    }

    return m;
}

static void to_rgba(const Mat& m, unsigned char* rgba)
{
    const float* ptr0 = m.channel(0);
    const float* ptr1 = m.channel(1);
    const float* ptr2 = m.channel(2);
    const float* ptr3 = m.channel(3);

    int size = m.w * m.h;

#define SATURATE_CAST_UCHAR(X) (unsigned char)::std::min(::std::max((int)(X), 0), 255);

    int remain = size;

    for (; remain>0; remain--)
    {
        rgba[0] = SATURATE_CAST_UCHAR(*ptr0);
        rgba[1] = SATURATE_CAST_UCHAR(*ptr1);
        rgba[2] = SATURATE_CAST_UCHAR(*ptr2);
        rgba[3] = SATURATE_CAST_UCHAR(*ptr3);

        rgba += 4;
        ptr0++;
        ptr1++;
        ptr2++;
        ptr3++;
    }

#undef SATURATE_CAST_UCHAR
}

static Mat from_rgb2bgr(const unsigned char* rgb, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 3, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr0 = m.channel(0);
    float* ptr1 = m.channel(1);
    float* ptr2 = m.channel(2);

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x8x3_t _rgb = vld3_u8(rgb);
        uint16x8_t _r16 = vmovl_u8(_rgb.val[0]);
        uint16x8_t _g16 = vmovl_u8(_rgb.val[1]);
        uint16x8_t _b16 = vmovl_u8(_rgb.val[2]);

        float32x4_t _rlow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_r16)));
        float32x4_t _rhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_r16)));
        float32x4_t _glow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_g16)));
        float32x4_t _ghigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_g16)));
        float32x4_t _blow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_b16)));
        float32x4_t _bhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_b16)));

        vst1q_f32(ptr2, _rlow);
        vst1q_f32(ptr2+4, _rhigh);
        vst1q_f32(ptr1, _glow);
        vst1q_f32(ptr1+4, _ghigh);
        vst1q_f32(ptr0, _blow);
        vst1q_f32(ptr0+4, _bhigh);

        rgb += 3*8;
        ptr0 += 8;
        ptr1 += 8;
        ptr2 += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld3.u8    {d0-d2}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u8   q10, d2             \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vmovl.u16  q8, d20             \n"
        "vmovl.u16  q9, d21             \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "vcvt.f32.u32   q8, q8          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%4 :128]! \n"
        "vcvt.f32.u32   q9, q9          \n"
        "vst1.f32   {d4-d7}, [%3 :128]! \n"
        "vst1.f32   {d16-d19}, [%2 :128]!\n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgb),    // %1
          "=r"(ptr0),   // %2
          "=r"(ptr1),   // %3
          "=r"(ptr2)    // %4
        : "0"(nn),
          "1"(rgb),
          "2"(ptr0),
          "3"(ptr1),
          "4"(ptr2)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9", "q10"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr0 = rgb[2];
        *ptr1 = rgb[1];
        *ptr2 = rgb[0];

        rgb += 3;
        ptr0++;
        ptr1++;
        ptr2++;
    }

    return m;
}

static void to_bgr2rgb(const Mat& m, unsigned char* rgb)
{
    const float* ptr0 = m.channel(0);
    const float* ptr1 = m.channel(1);
    const float* ptr2 = m.channel(2);

    int size = m.w * m.h;

#define SATURATE_CAST_UCHAR(X) (unsigned char)::std::min(::std::max((int)(X), 0), 255);

    int remain = size;

    for (; remain>0; remain--)
    {
        rgb[2] = SATURATE_CAST_UCHAR(*ptr0);
        rgb[1] = SATURATE_CAST_UCHAR(*ptr1);
        rgb[0] = SATURATE_CAST_UCHAR(*ptr2);

        rgb += 3;
        ptr0++;
        ptr1++;
        ptr2++;
    }

#undef SATURATE_CAST_UCHAR
}

static Mat from_rgb2gray(const unsigned char* rgb, int w, int h, Allocator* allocator)
{
    // coeffs for r g b = 0.299f, 0.587f, 0.114f
    const unsigned char Y_shift = 8;//14
    const unsigned char R2Y = 77;
    const unsigned char G2Y = 150;
    const unsigned char B2Y = 29;

    Mat m(w, h, 1, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr = m;

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    uint8x8_t _R2Y = vdup_n_u8(R2Y);
    uint8x8_t _G2Y = vdup_n_u8(G2Y);
    uint8x8_t _B2Y = vdup_n_u8(B2Y);
    for (; nn>0; nn--)
    {
        uint8x8x3_t _rgb = vld3_u8(rgb);

        uint16x8_t _y16 = vmull_u8(_rgb.val[0], _R2Y);
        _y16 = vmlal_u8(_y16, _rgb.val[1], _G2Y);
        _y16 = vmlal_u8(_y16, _rgb.val[2], _B2Y);
        _y16 = vshrq_n_u16(_y16, Y_shift);

        float32x4_t _ylow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_y16)));
        float32x4_t _yhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_y16)));

        vst1q_f32(ptr, _ylow);
        vst1q_f32(ptr+4, _yhigh);

        rgb += 3*8;
        ptr += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "vdup.u8    d16, %6             \n"
        "vdup.u8    d17, %7             \n"
        "vdup.u8    d18, %8             \n"
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld3.u8    {d0-d2}, [%1]!      \n"
        "vmull.u8   q2, d0, d16         \n"
        "vmlal.u8   q2, d1, d17         \n"
        "vmlal.u8   q2, d2, d18         \n"
        "vshr.u16   q2, q2, #8          \n" // Y_shift
        "vmovl.u16  q0, d4              \n"
        "vmovl.u16  q1, d5              \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgb),    // %1
          "=r"(ptr)     // %2
        : "0"(nn),
          "1"(rgb),
          "2"(ptr),
          "r"(R2Y),     // %6
          "r"(G2Y),     // %7
          "r"(B2Y)      // %8
        : "cc", "memory", "q0", "q1", "q2", "q8", "q9"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr = (rgb[0] * R2Y + rgb[1] * G2Y + rgb[2] * B2Y) >> Y_shift;

        rgb += 3;
        ptr++;
    }

    return m;
}

static Mat from_bgr2gray(const unsigned char* bgr, int w, int h, Allocator* allocator)
{
    // coeffs for r g b = 0.299f, 0.587f, 0.114f
    const unsigned char Y_shift = 8;//14
    const unsigned char R2Y = 77;
    const unsigned char G2Y = 150;
    const unsigned char B2Y = 29;

    Mat m(w, h, 1, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr = m;

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    uint8x8_t _R2Y = vdup_n_u8(R2Y);
    uint8x8_t _G2Y = vdup_n_u8(G2Y);
    uint8x8_t _B2Y = vdup_n_u8(B2Y);
    for (; nn>0; nn--)
    {
        uint8x8x3_t _rgb = vld3_u8(bgr);

        uint16x8_t _y16 = vmull_u8(_rgb.val[2], _R2Y);
        _y16 = vmlal_u8(_y16, _rgb.val[1], _G2Y);
        _y16 = vmlal_u8(_y16, _rgb.val[0], _B2Y);
        _y16 = vshrq_n_u16(_y16, Y_shift);

        float32x4_t _ylow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_y16)));
        float32x4_t _yhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_y16)));

        vst1q_f32(ptr, _ylow);
        vst1q_f32(ptr+4, _yhigh);

        bgr += 3*8;
        ptr += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "vdup.u8    d16, %6             \n"
        "vdup.u8    d17, %7             \n"
        "vdup.u8    d18, %8             \n"
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld3.u8    {d0-d2}, [%1]!      \n"
        "vmull.u8   q2, d2, d16         \n"
        "vmlal.u8   q2, d1, d17         \n"
        "vmlal.u8   q2, d0, d18         \n"
        "vshr.u16   q2, q2, #8          \n" // Y_shift
        "vmovl.u16  q0, d4              \n"
        "vmovl.u16  q1, d5              \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(bgr),    // %1
          "=r"(ptr)     // %2
        : "0"(nn),
          "1"(bgr),
          "2"(ptr),
          "r"(R2Y),     // %6
          "r"(G2Y),     // %7
          "r"(B2Y)      // %8
        : "cc", "memory", "q0", "q1", "q2", "q8", "q9"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr = (bgr[2] * R2Y + bgr[1] * G2Y + bgr[0] * B2Y) >> Y_shift;

        bgr += 3;
        ptr++;
    }

    return m;
}

static Mat from_gray2rgb(const unsigned char* gray, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 3, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr0 = m.channel(0);
    float* ptr1 = m.channel(1);
    float* ptr2 = m.channel(2);

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 4;
    int remain = size - (nn << 4);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x16_t _gray = vld1q_u8(gray);
        uint16x8_t _gray16_0 = vmovl_u8(vget_low_u8(_gray));
        uint16x8_t _gray16_1 = vmovl_u8(vget_high_u8(_gray));

        float32x4_t _graylow_0 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_gray16_0)));
        float32x4_t _grayhigh_0 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_gray16_0)));
        float32x4_t _graylow_1 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_gray16_1)));
        float32x4_t _grayhigh_1 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_gray16_1)));

        vst1q_f32(ptr0, _graylow_0);
        vst1q_f32(ptr0+4, _grayhigh_0);
        vst1q_f32(ptr0+8, _graylow_1);
        vst1q_f32(ptr0+12, _grayhigh_1);

        vst1q_f32(ptr1, _graylow_0);
        vst1q_f32(ptr1+4, _grayhigh_0);
        vst1q_f32(ptr1+8, _graylow_1);
        vst1q_f32(ptr1+12, _grayhigh_1);

        vst1q_f32(ptr2, _graylow_0);
        vst1q_f32(ptr2+4, _grayhigh_0);
        vst1q_f32(ptr2+8, _graylow_1);
        vst1q_f32(ptr2+12, _grayhigh_1);

        gray += 16;
        ptr0 += 16;
        ptr1 += 16;
        ptr2 += 16;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #128]          \n"
        "vld1.u8    {d0,d1}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "vst1.f32   {d4-d7}, [%2 :128]! \n"
        "vst1.f32   {d0-d3}, [%3 :128]! \n"
        "vst1.f32   {d4-d7}, [%3 :128]! \n"
        "vst1.f32   {d0-d3}, [%4 :128]! \n"
        "vst1.f32   {d4-d7}, [%4 :128]! \n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(gray),   // %1
          "=r"(ptr0),   // %2
          "=r"(ptr1),   // %3
          "=r"(ptr2)    // %4
        : "0"(nn),
          "1"(gray),
          "2"(ptr0),
          "3"(ptr1),
          "4"(ptr2)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr0 = *gray;
        *ptr1 = *gray;
        *ptr2 = *gray;

        gray++;
        ptr0++;
        ptr1++;
        ptr2++;
    }

    return m;
}

static Mat from_rgba2rgb(const unsigned char* rgba, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 3, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr0 = m.channel(0);
    float* ptr1 = m.channel(1);
    float* ptr2 = m.channel(2);

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x8x4_t _rgba = vld4_u8(rgba);
        int16x8_t _r16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[0]));
        int16x8_t _g16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[1]));
        int16x8_t _b16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[2]));

        float32x4_t _rlow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_r16)));
        float32x4_t _rhigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_r16)));
        float32x4_t _glow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_g16)));
        float32x4_t _ghigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_g16)));
        float32x4_t _blow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_b16)));
        float32x4_t _bhigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_b16)));

        vst1q_f32(ptr0, _rlow);
        vst1q_f32(ptr0+4, _rhigh);
        vst1q_f32(ptr1, _glow);
        vst1q_f32(ptr1+4, _ghigh);
        vst1q_f32(ptr2, _blow);
        vst1q_f32(ptr2+4, _bhigh);

        rgba += 4*8;
        ptr0 += 8;
        ptr1 += 8;
        ptr2 += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld4.u8    {d0-d3}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u8   q10, d2             \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vmovl.u16  q8, d20             \n"
        "vmovl.u16  q9, d21             \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "vcvt.f32.u32   q8, q8          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "vcvt.f32.u32   q9, q9          \n"
        "vst1.f32   {d4-d7}, [%3 :128]! \n"
        "vst1.f32   {d16-d19}, [%4 :128]!\n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgba),   // %1
          "=r"(ptr0),   // %2
          "=r"(ptr1),   // %3
          "=r"(ptr2)    // %4
        : "0"(nn),
          "1"(rgba),
          "2"(ptr0),
          "3"(ptr1),
          "4"(ptr2)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr0 = rgba[0];
        *ptr1 = rgba[1];
        *ptr2 = rgba[2];

        rgba += 4;
        ptr0++;
        ptr1++;
        ptr2++;
    }

    return m;
}

static Mat from_rgba2bgr(const unsigned char* rgba, int w, int h, Allocator* allocator)
{
    Mat m(w, h, 3, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr0 = m.channel(0);
    float* ptr1 = m.channel(1);
    float* ptr2 = m.channel(2);

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    for (; nn>0; nn--)
    {
        uint8x8x4_t _rgba = vld4_u8(rgba);
        int16x8_t _r16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[0]));
        int16x8_t _g16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[1]));
        int16x8_t _b16 = vreinterpretq_s16_u16(vmovl_u8(_rgba.val[2]));

        float32x4_t _rlow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_r16)));
        float32x4_t _rhigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_r16)));
        float32x4_t _glow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_g16)));
        float32x4_t _ghigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_g16)));
        float32x4_t _blow = vcvtq_f32_s32(vmovl_s16(vget_low_s16(_b16)));
        float32x4_t _bhigh = vcvtq_f32_s32(vmovl_s16(vget_high_s16(_b16)));

        vst1q_f32(ptr2, _rlow);
        vst1q_f32(ptr2+4, _rhigh);
        vst1q_f32(ptr1, _glow);
        vst1q_f32(ptr1+4, _ghigh);
        vst1q_f32(ptr0, _blow);
        vst1q_f32(ptr0+4, _bhigh);

        rgba += 4*8;
        ptr0 += 8;
        ptr1 += 8;
        ptr2 += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld4.u8    {d0-d3}, [%1]!      \n"
        "vmovl.u8   q8, d0              \n"
        "vmovl.u8   q9, d1              \n"
        "vmovl.u8   q10, d2             \n"
        "vmovl.u16  q0, d16             \n"
        "vmovl.u16  q1, d17             \n"
        "vmovl.u16  q2, d18             \n"
        "vmovl.u16  q3, d19             \n"
        "vmovl.u16  q8, d20             \n"
        "vmovl.u16  q9, d21             \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "vcvt.f32.u32   q2, q2          \n"
        "vcvt.f32.u32   q3, q3          \n"
        "vcvt.f32.u32   q8, q8          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%4 :128]! \n"
        "vcvt.f32.u32   q9, q9          \n"
        "vst1.f32   {d4-d7}, [%3 :128]! \n"
        "vst1.f32   {d16-d19}, [%2 :128]!\n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgba),   // %1
          "=r"(ptr0),   // %2
          "=r"(ptr1),   // %3
          "=r"(ptr2)    // %4
        : "0"(nn),
          "1"(rgba),
          "2"(ptr0),
          "3"(ptr1),
          "4"(ptr2)
        : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9", "q10"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr0 = rgba[2];
        *ptr1 = rgba[1];
        *ptr2 = rgba[0];

        rgba += 4;
        ptr0++;
        ptr1++;
        ptr2++;
    }

    return m;
}

static Mat from_rgba2gray(const unsigned char* rgba, int w, int h, Allocator* allocator)
{
    // coeffs for r g b = 0.299f, 0.587f, 0.114f
    const unsigned char Y_shift = 8;//14
    const unsigned char R2Y = 77;
    const unsigned char G2Y = 150;
    const unsigned char B2Y = 29;

    Mat m(w, h, 1, 4u, allocator);
    if (m.empty())
        return m;

    float* ptr = m;

    int size = w * h;

#if __ARM_NEON
    int nn = size >> 3;
    int remain = size - (nn << 3);
#else
    int remain = size;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
    uint8x8_t _R2Y = vdup_n_u8(R2Y);
    uint8x8_t _G2Y = vdup_n_u8(G2Y);
    uint8x8_t _B2Y = vdup_n_u8(B2Y);
    for (; nn>0; nn--)
    {
        uint8x8x4_t _rgba = vld4_u8(rgba);

        uint16x8_t _y16 = vmull_u8(_rgba.val[0], _R2Y);
        _y16 = vmlal_u8(_y16, _rgba.val[1], _G2Y);
        _y16 = vmlal_u8(_y16, _rgba.val[2], _B2Y);
        _y16 = vshrq_n_u16(_y16, Y_shift);

        float32x4_t _ylow = vcvtq_f32_u32(vmovl_u16(vget_low_u16(_y16)));
        float32x4_t _yhigh = vcvtq_f32_u32(vmovl_u16(vget_high_u16(_y16)));

        vst1q_f32(ptr, _ylow);
        vst1q_f32(ptr+4, _yhigh);

        rgba += 4*8;
        ptr += 8;
    }
#else
    if (nn > 0)
    {
    asm volatile(
        "vdup.u8    d16, %6             \n"
        "vdup.u8    d17, %7             \n"
        "vdup.u8    d18, %8             \n"
        "0:                             \n"
        "pld        [%1, #256]          \n"
        "vld4.u8    {d0-d3}, [%1]!      \n"
        "vmull.u8   q2, d0, d16         \n"
        "vmlal.u8   q2, d1, d17         \n"
        "vmlal.u8   q2, d2, d18         \n"
        "vshr.u16   q2, q2, #8          \n" // Y_shift
        "vmovl.u16  q0, d4              \n"
        "vmovl.u16  q1, d5              \n"
        "vcvt.f32.u32   q0, q0          \n"
        "vcvt.f32.u32   q1, q1          \n"
        "subs       %0, #1              \n"
        "vst1.f32   {d0-d3}, [%2 :128]! \n"
        "bne        0b                  \n"
        : "=r"(nn),     // %0
          "=r"(rgba),   // %1
          "=r"(ptr)     // %2
        : "0"(nn),
          "1"(rgba),
          "2"(ptr),
          "r"(R2Y),     // %6
          "r"(G2Y),     // %7
          "r"(B2Y)      // %8
        : "cc", "memory", "q0", "q1", "q2", "q8", "q9"
    );
    }
#endif // __aarch64__
#endif // __ARM_NEON
    for (; remain>0; remain--)
    {
        *ptr = (rgba[0] * R2Y + rgba[1] * G2Y + rgba[2] * B2Y) >> Y_shift;

        rgba += 4;
        ptr++;
    }

    return m;
}

void yuv420sp2rgb(const unsigned char* yuv420sp, int w, int h, unsigned char* rgb)
{
    const unsigned char* yptr = yuv420sp;
    const unsigned char* vuptr = yuv420sp + w * h;

#if __ARM_NEON
    int8x8_t _v128 = vdup_n_s8(128);
    int8x8_t _v90 = vdup_n_s8(90);
    int8x8_t _v46 = vdup_n_s8(46);
    int8x8_t _v22 = vdup_n_s8(22);
    int8x8_t _v113 = vdup_n_s8(113);
#endif // __ARM_NEON

    for (int y=0; y<h; y+=2)
    {
        const unsigned char* yptr0 = yptr;
        const unsigned char* yptr1 = yptr + w;
        unsigned char* rgb0 = rgb;
        unsigned char* rgb1 = rgb + w*3;

#if __ARM_NEON
        int nn = w >> 3;
        int remain = w - (nn << 3);
#else
        int remain = w;
#endif // __ARM_NEON

#if __ARM_NEON
#if __aarch64__
        for (; nn>0; nn--)
        {
            int16x8_t _yy0 = vreinterpretq_s16_u16(vshll_n_u8(vld1_u8(yptr0), 6));
            int16x8_t _yy1 = vreinterpretq_s16_u16(vshll_n_u8(vld1_u8(yptr1), 6));

            int8x8_t _vvuu = vsub_s8(vreinterpret_s8_u8(vld1_u8(vuptr)), _v128);
            int8x8x2_t _vvvvuuuu = vtrn_s8(_vvuu, _vvuu);
            int8x8_t _vv = _vvvvuuuu.val[0];
            int8x8_t _uu = _vvvvuuuu.val[1];

            int16x8_t _r0 = vmlal_s8(_yy0, _vv, _v90);
            int16x8_t _g0 = vmlsl_s8(_yy0, _vv, _v46);
            _g0 = vmlsl_s8(_g0, _uu, _v22);
            int16x8_t _b0 = vmlal_s8(_yy0, _uu, _v113);

            int16x8_t _r1 = vmlal_s8(_yy1, _vv, _v90);
            int16x8_t _g1 = vmlsl_s8(_yy1, _vv, _v46);
            _g1 = vmlsl_s8(_g1, _uu, _v22);
            int16x8_t _b1 = vmlal_s8(_yy1, _uu, _v113);

            uint8x8x3_t _rgb0;
            _rgb0.val[0] = vqshrun_n_s16(_r0, 6);
            _rgb0.val[1] = vqshrun_n_s16(_g0, 6);
            _rgb0.val[2] = vqshrun_n_s16(_b0, 6);

            uint8x8x3_t _rgb1;
            _rgb1.val[0] = vqshrun_n_s16(_r1, 6);
            _rgb1.val[1] = vqshrun_n_s16(_g1, 6);
            _rgb1.val[2] = vqshrun_n_s16(_b1, 6);

            vst3_u8(rgb0, _rgb0);
            vst3_u8(rgb1, _rgb1);

            yptr0 += 8;
            yptr1 += 8;
            vuptr += 8;
            rgb0 += 24;
            rgb1 += 24;
        }
#else
        if (nn > 0)
        {
        asm volatile(
            "pld        [%3, #128]          \n"
            "vld1.u8    {d2}, [%3]!         \n"
            "vsub.s8    d2, d2, %12         \n"
            "0:                             \n"
            "pld        [%1, #128]          \n"
            "vld1.u8    {d0}, [%1]!         \n"
            "pld        [%2, #128]          \n"
            "vld1.u8    {d1}, [%2]!         \n"
            "vshll.u8   q2, d0, #6          \n"
            "vorr       d3, d2, d2          \n"
            "vshll.u8   q3, d1, #6          \n"
            "vorr       q9, q2, q2          \n"
            "vtrn.s8    d2, d3              \n"
            "vorr       q11, q3, q3         \n"
            "vmlsl.s8   q9, d2, %14         \n"
            "vorr       q8, q2, q2          \n"
            "vmlsl.s8   q11, d2, %14        \n"
            "vorr       q10, q3, q3         \n"
            "vmlal.s8   q8, d2, %13         \n"
            "vmlal.s8   q2, d3, %16         \n"
            "vmlal.s8   q10, d2, %13        \n"
            "vmlsl.s8   q9, d3, %15         \n"
            "vmlal.s8   q3, d3, %16         \n"
            "vmlsl.s8   q11, d3, %15        \n"
            "vqshrun.s16 d24, q8, #6        \n"
            "vqshrun.s16 d26, q2, #6        \n"
            "vqshrun.s16 d4, q10, #6        \n"
            "vqshrun.s16 d25, q9, #6        \n"
            "vqshrun.s16 d6, q3, #6         \n"
            "vqshrun.s16 d5, q11, #6        \n"
            "pld        [%3, #128]          \n"
            "vld1.u8    {d2}, [%3]!         \n"
            "subs       %0, #1              \n"
            "vst3.u8    {d24-d26}, [%4]!    \n"
            "vsub.s8    d2, d2, %12         \n"
            "vst3.u8    {d4-d6}, [%5]!      \n"
            "bne        0b                  \n"
            "sub        %3, #8              \n"
            : "=r"(nn),     // %0
              "=r"(yptr0),  // %1
              "=r"(yptr1),  // %2
              "=r"(vuptr),  // %3
              "=r"(rgb0),   // %4
              "=r"(rgb1)    // %5
            : "0"(nn),
              "1"(yptr0),
              "2"(yptr1),
              "3"(vuptr),
              "4"(rgb0),
              "5"(rgb1),
              "w"(_v128),   // %12
              "w"(_v90),    // %13
              "w"(_v46),    // %14
              "w"(_v22),    // %15
              "w"(_v113)    // %16
            : "cc", "memory", "q0", "q1", "q2", "q3", "q8", "q9", "q10", "q11", "q12", "d26"
        );
        }
#endif // __aarch64__
#endif // __ARM_NEON

#define SATURATE_CAST_UCHAR(X) (unsigned char)::std::min(::std::max((int)(X), 0), 255);
        for (; remain>0; remain-=2)
        {
            // R = 1.164 * yy + 1.596 * vv
            // G = 1.164 * yy - 0.813 * vv - 0.391 * uu
            // B = 1.164 * yy              + 2.018 * uu

            // R = Y + (1.370705 * (V-128))
            // G = Y - (0.698001 * (V-128)) - (0.337633 * (U-128))
            // B = Y + (1.732446 * (U-128))

            // R = ((Y << 6) + 87.72512 * (V-128)) >> 6
            // G = ((Y << 6) - 44.672064 * (V-128) - 21.608512 * (U-128)) >> 6
            // B = ((Y << 6) + 110.876544 * (U-128)) >> 6

            // R = ((Y << 6) + 90 * (V-128)) >> 6
            // G = ((Y << 6) - 46 * (V-128) - 22 * (U-128)) >> 6
            // B = ((Y << 6) + 113 * (U-128)) >> 6

            // R = (yy + 90 * vv) >> 6
            // G = (yy - 46 * vv - 22 * uu) >> 6
            // B = (yy + 113 * uu) >> 6

            int v = vuptr[0] - 128;
            int u = vuptr[1] - 128;

            int ruv = 90 * v;
            int guv = -46 * v + -22 * u;
            int buv = 113 * u;

            int y00 = yptr0[0] << 6;
            rgb0[0] = SATURATE_CAST_UCHAR((y00 + ruv) >> 6);
            rgb0[1] = SATURATE_CAST_UCHAR((y00 + guv) >> 6);
            rgb0[2] = SATURATE_CAST_UCHAR((y00 + buv) >> 6);

            int y01 = yptr0[1] << 6;
            rgb0[3] = SATURATE_CAST_UCHAR((y01 + ruv) >> 6);
            rgb0[4] = SATURATE_CAST_UCHAR((y01 + guv) >> 6);
            rgb0[5] = SATURATE_CAST_UCHAR((y01 + buv) >> 6);

            int y10 = yptr1[0] << 6;
            rgb1[0] = SATURATE_CAST_UCHAR((y10 + ruv) >> 6);
            rgb1[1] = SATURATE_CAST_UCHAR((y10 + guv) >> 6);
            rgb1[2] = SATURATE_CAST_UCHAR((y10 + buv) >> 6);

            int y11 = yptr1[1] << 6;
            rgb1[3] = SATURATE_CAST_UCHAR((y11 + ruv) >> 6);
            rgb1[4] = SATURATE_CAST_UCHAR((y11 + guv) >> 6);
            rgb1[5] = SATURATE_CAST_UCHAR((y11 + buv) >> 6);

            yptr0 += 2;
            yptr1 += 2;
            vuptr += 2;
            rgb0 += 6;
            rgb1 += 6;
        }
#undef SATURATE_CAST_UCHAR

        yptr += 2*w;
        rgb += 2*3*w;
    }
}

Mat Mat::from_pixels(const unsigned char* pixels, int type, int w, int h, Allocator* allocator)
{
    if (type & PIXEL_CONVERT_MASK)
    {
        if (type == PIXEL_RGB2BGR || type == PIXEL_BGR2RGB)
            return from_rgb2bgr(pixels, w, h, allocator);

        if (type == PIXEL_RGB2GRAY)
            return from_rgb2gray(pixels, w, h, allocator);

        if (type == PIXEL_BGR2GRAY)
            return from_bgr2gray(pixels, w, h, allocator);

        if (type == PIXEL_GRAY2RGB || type == PIXEL_GRAY2BGR)
            return from_gray2rgb(pixels, w, h, allocator);

        if (type == PIXEL_RGBA2RGB)
            return from_rgba2rgb(pixels, w, h, allocator);

        if (type == PIXEL_RGBA2BGR)
            return from_rgba2bgr(pixels, w, h, allocator);

        if (type == PIXEL_RGBA2GRAY)
            return from_rgba2gray(pixels, w, h, allocator);
    }
    else
    {
        if (type == PIXEL_RGB || type == PIXEL_BGR)
            return from_rgb(pixels, w, h, allocator);

        if (type == PIXEL_GRAY)
            return from_gray(pixels, w, h, allocator);

        if (type == PIXEL_RGBA)
            return from_rgba(pixels, w, h, allocator);
    }

    return Mat();
}

Mat Mat::from_pixels_resize(const unsigned char* pixels, int type, int w, int h, int target_width, int target_height, Allocator* allocator)
{
    if (w == target_width && h == target_height)
        return Mat::from_pixels(pixels, type, w, h);

    Mat m;

    int type_from = type & PIXEL_FORMAT_MASK;

    if (type_from == PIXEL_RGB || type_from == PIXEL_BGR)
    {
        Mat dst(target_width, target_height, (size_t)3u, 3);

        resize_bilinear_c3(pixels, w, h, dst, target_width, target_height);

        m = Mat::from_pixels(dst, type, target_width, target_height, allocator);
    }
    else if (type_from == PIXEL_GRAY)
    {
        Mat dst(target_width, target_height, (size_t)1u, 1);

        resize_bilinear_c1(pixels, w, h, dst, target_width, target_height);

        m = Mat::from_pixels(dst, type, target_width, target_height, allocator);
    }
    else if (type_from == PIXEL_RGBA)
    {
        Mat dst(target_width, target_height, (size_t)4u, 4);

        resize_bilinear_c4(pixels, w, h, dst, target_width, target_height);

        m = Mat::from_pixels(dst, type, target_width, target_height, allocator);
    }

    return m;
}

void Mat::to_pixels(unsigned char* pixels, int type) const
{
    if (type & PIXEL_CONVERT_MASK)
    {
        if (type == PIXEL_RGB2BGR || type == PIXEL_BGR2RGB)
            return to_bgr2rgb(*this, pixels);
    }
    else
    {
        if (type == PIXEL_RGB || type == PIXEL_BGR)
            return to_rgb(*this, pixels);

        if (type == PIXEL_GRAY)
            return to_gray(*this, pixels);

        if (type == PIXEL_RGBA)
            return to_rgba(*this, pixels);
    }
}

void Mat::to_pixels_resize(unsigned char* pixels, int type, int target_width, int target_height) const
{
    if (w == target_width && h == target_height)
        return to_pixels(pixels, type);

    int type_to = (type & PIXEL_CONVERT_MASK) ? (type >> PIXEL_CONVERT_SHIFT) : (type & PIXEL_FORMAT_MASK);

    if (type_to == PIXEL_RGB || type_to == PIXEL_BGR)
    {
        Mat src(target_width, target_height, (size_t)3u, 3);

        to_pixels(src, type);

        resize_bilinear_c3(src, w, h, pixels, target_width, target_height);
    }
    else if (type_to == PIXEL_GRAY)
    {
        Mat src(target_width, target_height, (size_t)1u, 1);

        to_pixels(src, type);

        resize_bilinear_c1(src, w, h, pixels, target_width, target_height);
    }
    else if (type_to == PIXEL_RGBA)
    {
        Mat src(target_width, target_height, (size_t)4u, 4);

        to_pixels(src, type);

        resize_bilinear_c4(src, w, h, pixels, target_width, target_height);
    }
}
#endif // NCNN_PIXEL

} // namespace ncnn
