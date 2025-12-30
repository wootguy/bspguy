/*
//
// Copyright (c) 1998-2019 Joe Bertolami. All Right Reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, CLUDG, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//   ARE DISCLAIMED.  NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//   LIABLE FOR ANY DIRECT, DIRECT, CIDENTAL, SPECIAL, EXEMPLARY, OR
//   CONSEQUENTIAL DAMAGES (CLUDG, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
//   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSESS TERRUPTION)
//   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER  CONTRACT, STRICT
//   LIABILITY, OR TORT (CLUDG NEGLIGENCE OR OTHERWISE) ARISG  ANY WAY  OF THE
//   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
//
*/

#ifndef __BASE_RESAMPLE_H__
#define __BASE_RESAMPLE_H__

#include <cstdint>
#include <memory>
#include <string>

#ifndef __BASE_TYPES_H__
#define __BASE_TYPES_H__

#if defined(WIN32) || defined(_WIN64)
#define BASE_PLATFORM_WINDOWS
#include "windows.h"
#elif defined(__APPLE__)
#define BASE_PLATFORM_MACOS
#include "TargetConditionals.h"
#include "ctype.h"
#include "sys/types.h"
#include "unistd.h"
#else
#include "ctype.h"
#include "sys/types.h"
#include "unistd.h"
#endif

namespace base {

typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef float float32;
typedef double float64;

}  // namespace base

#endif  // __BASE_TYPES_H__

namespace base {

enum KernelType : uint8 {
  KernelTypeUnknown,
  KernelTypeNearest,
  KernelTypeAverage,
  KernelTypeBilinear,
  KernelTypeBicubic,
  KernelTypeMitchell,
  KernelTypeCardinal,
  KernelTypeBSpline,
  KernelTypeLanczos,
  KernelTypeLanczos2,
  KernelTypeLanczos3,
  KernelTypeLanczos4,
  KernelTypeLanczos5,
  KernelTypeCatmull,
  KernelTypeGaussian,
};

enum KernelDirection : uint8 {
  KernelDirectionUnknown,
  KernelDirectionHorizontal,
  KernelDirectionVertical,
};

#define BASE_PI (3.14159265359f)

#define BLOCK_OFFSET_RGB24(ptr, width, x, y) (ptr + (3 * width) * y + 3 * x)

inline int32 clip_range(int32 input, int32 low, int32 high) {
  return (input < low) ? low : (input > high) ? high : input;
}

/* Cubic weighing function

   Source: Mitchell, Netravali, "Reconstruction Filters in Computer Graphics"
   1988

   Several of the popular cubic functions used for bi-directional image
   filtering can be generated as a simple weight function with two parameters.
   Thus, we use a weight function to generate the majority of our bicubic
   kernels. */
inline float32 bicubic_weight(float32 f_b, float32 f_c, float32 distance) {
  /* Our bicubic function is designed to provide feedback over a radius of 2.0
   * pixels. */
  float32 distance2 = distance * distance;
  float32 distance3 = distance * distance * distance;
  float32 result = 0.0;

  if (distance < 1.0) {
    float32 cubic_term = (12.0 - 9.0 * f_b - 6.0 * f_c) * distance3;
    float32 quad_term = (-18.0 + 12.0 * f_b + 6.0 * f_c) * distance2;
    float32 const_term = (6.0 - 2.0 * f_b);
    result = (1.0f / 6.0f) * (cubic_term + quad_term + const_term);
  }

  else if (distance >= 1.0 && distance < 2.0) {
    float32 cubic_term = (-f_b - 6.0 * f_c) * distance3;
    float32 quad_term = (6.0 * f_b + 30.0 * f_c) * distance2;
    float32 lin_term = (-12.0 * f_b - 48.0 * f_c) * distance;
    float32 const_term = (8.0 * f_b + 24.0 * f_c);
    result = (1.0f / 6.0f) * (cubic_term + quad_term + lin_term + const_term);
  }

  if (result < 0) {
    result = 0.0;
  }

  return result;
}

/* Gaussian weighting function. Our simple gaussian distribution function with
   mean of zero and std-dev (d):

                   1.0         -(x^2 / (2 * d * d))
    f(x) =  --------------- * e
                        0.5
            d * (2 * Pi)
*/

inline float32 gaussian_weight(float32 distance, float32 radius) {
  float32 range = distance / radius;

  /* Gaussian function with mean = 0 and variance = 0.1. */
  static const float32 variance = 0.1f;
  static const float32 stddev = sqrt(variance);
  static const float32 coeff = 1.0f / (stddev * sqrt(2.0 * BASE_PI));
  return coeff * exp(-1.0f * (range * range) / (2.0 * variance));
}

inline float32 sinc(float32 f_x) {
  if (0.0 == f_x) return 1.0;
  return sin(BASE_PI * f_x) / (BASE_PI * f_x);
}

inline float32 lanczos_weight(float32 f_n, float32 distance) {
  if (distance <= f_n) {
    return sinc(distance) * sinc(distance / f_n);
  }
  return 0.0f;
}

bool SampleKernelBilinearH(uint8* src, uint32 src_width, uint32 src_height,
                           float32 f_x, float32 f_y, uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  /* We do not bias our float coordinate by 0.5 because we wish
     to sample using the nearest 2 pixels to our coordinate. */
  int32 sample_x = f_x;
  int32 sample_y = f_y;
  uint8* pixels[2] = {nullptr};
  float32 f_delta = (float32)f_x - sample_x;

  /* compute our two pixels that will be interpolated together. */
  for (uint32 i = 0; i < 2; i++) {
    int32 src_x = clip_range(sample_x + i, 0, src_width - 1);
    int32 src_y = clip_range(sample_y, 0, src_height - 1);

    pixels[i] = BLOCK_OFFSET_RGB24(src, src_width, src_x, src_y);
  }

  /* perform the interpolation of our lerp_pixels. */
  output[0] = pixels[0][0] * (1.0f - f_delta) + pixels[1][0] * f_delta;
  output[1] = pixels[0][1] * (1.0f - f_delta) + pixels[1][1] * f_delta;
  output[2] = pixels[0][2] * (1.0f - f_delta) + pixels[1][2] * f_delta;

  return true;
}

bool SampleKernelBilinearV(uint8* src, uint32 src_width, uint32 src_height,
                           float32 f_x, float32 f_y, uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  /* We do not bias our float coordinate by 0.5 because we wish
     to sample using the nearest 2 pixels to our coordinate. */
  int32 sample_x = f_x;
  int32 sample_y = f_y;
  uint8* pixels[2] = {nullptr};
  float32 f_delta = (float32)f_y - sample_y;

  /* compute our two pixels that will be interpolated together. */
  for (uint32 i = 0; i < 2; i++) {
    int32 src_x = clip_range(sample_x, 0, src_width - 1);
    int32 src_y = clip_range(sample_y + i, 0, src_height - 1);

    pixels[i] = BLOCK_OFFSET_RGB24(src, src_width, src_x, src_y);
  }

  /* perform the interpolation of our lerp_pixels. */
  output[0] = pixels[0][0] * (1.0f - f_delta) + pixels[1][0] * f_delta;
  output[1] = pixels[0][1] * (1.0f - f_delta) + pixels[1][1] * f_delta;
  output[2] = pixels[0][2] * (1.0f - f_delta) + pixels[1][2] * f_delta;

  return true;
}

bool SampleKernelBilinear(uint8* src, uint32 src_width, uint32 src_height,
                          KernelDirection direction, float32 f_x, float32 f_y,
                          uint8* output) {
  switch (direction) {
    case KernelDirectionHorizontal:
      return SampleKernelBilinearH(src, src_width, src_height, f_x, f_y,
                                   output);
    case KernelDirectionVertical:
      return SampleKernelBilinearV(src, src_width, src_height, f_x, f_y,
                                   output);
  }

  return false;
}

bool SampleKernelBicubicH(uint8* src, uint32 src_width, uint32 src_height,
                          float32 f_x, float32 f_y, float32 coeff_b,
                          float32 coeff_c, uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -2; i < 2; i++) {
    int32 i_x = (int32)f_x + i;
    int32 i_y = (int32)f_y;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 x_delta = (float32)f_x - i_x;
    float32 distance = fabs(x_delta);
    float32 weight = bicubic_weight(coeff_b, coeff_c, distance);

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelBicubicV(uint8* src, uint32 src_width, uint32 src_height,
                          float32 f_x, float32 f_y, float32 coeff_b,
                          float32 coeff_c, uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -2; i < 2; i++) {
    int32 i_x = (int32)f_x;
    int32 i_y = (int32)f_y + i;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 y_delta = (float32)f_y - i_y;
    float32 distance = fabs(y_delta);
    float32 weight = bicubic_weight(coeff_b, coeff_c, distance);
    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelBicubic(uint8* src, uint32 src_width, uint32 src_height,
                         KernelDirection direction, float32 f_x, float32 f_y,
                         float32 coeff_b, float32 coeff_c, uint8* output) {
  switch (direction) {
    case KernelDirectionHorizontal:
      return SampleKernelBicubicH(src, src_width, src_height, f_x, f_y, coeff_b,
                                  coeff_c, output);
    case KernelDirectionVertical:
      return SampleKernelBicubicV(src, src_width, src_height, f_x, f_y, coeff_b,
                                  coeff_c, output);
  }

  return false;
}

bool SampleKernelLanczosH(uint8* src, uint32 src_width, uint32 src_height,
                          float32 f_x, float32 f_y, float32 coeff_a,
                          uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  int32 radius = coeff_a;
  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -radius; i < radius; i++) {
    int32 i_x = (int32)f_x + i;
    int32 i_y = (int32)f_y;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 x_delta = (float32)f_x - i_x;
    float32 distance = fabs(x_delta);
    float32 weight = lanczos_weight(coeff_a, distance);

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelLanczosV(uint8* src, uint32 src_width, uint32 src_height,
                          float32 f_x, float32 f_y, float32 coeff_a,
                          uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  int32 radius = coeff_a;
  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -radius; i < radius; i++) {
    int32 i_x = (int32)f_x;
    int32 i_y = (int32)f_y + i;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 y_delta = (float32)f_y - i_y;
    float32 distance = fabs(y_delta);
    float32 weight = lanczos_weight(coeff_a, distance);

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelLanczos(uint8* src, uint32 src_width, uint32 src_height,
                         KernelDirection direction, float32 f_x, float32 f_y,
                         float32 coeff_a, uint8* output) {
  switch (direction) {
    case KernelDirectionHorizontal:
      return SampleKernelLanczosH(src, src_width, src_height, f_x, f_y, coeff_a,
                                  output);
    case KernelDirectionVertical:
      return SampleKernelLanczosV(src, src_width, src_height, f_x, f_y, coeff_a,
                                  output);
  }

  return false;
}

bool SampleKernelAverageH(uint8* src, uint32 src_width, uint32 src_height,
                          float32 f_x, float32 f_y, float32 h_ratio,
                          uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  int32 radius = h_ratio + 1.0f;
  float32 max_distance = h_ratio;
  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -radius + 1; i <= radius; i++) {
    int32 i_x = (int32)f_x + i;
    int32 i_y = (int32)f_y;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 x_delta = (float32)f_x - i_x;
    float32 distance = fabs(x_delta);
    float32 weight = 0.0f;

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    if (h_ratio >= 1.0) {
      distance = min(max_distance, distance);
      weight = 1.0f - distance / max_distance;
    } else {
      if (distance >= 0.5f - h_ratio) {
        weight = 1.0f - distance;
      } else {
        /* our average kernel is smaller than a pixel and is fully contained
           within the source pixel, so we simply copy the value out. */
        output[0] = src_pixel[0];
        output[1] = src_pixel[1];
        output[2] = src_pixel[2];
        return true;
      }
    }

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelAverageV(uint8* src, uint32 src_width, uint32 src_height,
                          float32 f_x, float32 f_y, float32 v_ratio,
                          uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  int32 radius = v_ratio + 1.0f;
  float32 max_distance = v_ratio;
  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -radius + 1; i <= radius; i++) {
    int32 i_x = (int32)f_x;
    int32 i_y = (int32)f_y + i;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 y_delta = (float32)f_y - i_y;
    float32 distance = fabs(y_delta);
    float32 weight = 0.0f;

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    if (v_ratio >= 1.0) {
      distance = min(max_distance, distance);
      weight = 1.0f - distance / max_distance;
    } else {
      if (distance >= 0.5f - v_ratio) {
        weight = 1.0f - distance;
      } else {
        /* our average kernel is smaller than a pixel and is fully contained
           within the source pixel, so we simply copy the value out. */
        output[0] = src_pixel[0];
        output[1] = src_pixel[1];
        output[2] = src_pixel[2];
        return true;
      }
    }

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelGaussianH(uint8* src, uint32 src_width, uint32 src_height,
                           float32 f_x, float32 f_y, float32 h_ratio,
                           uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  int32 radius = h_ratio + 1.0f;
  float32 max_distance = h_ratio;
  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -radius; i <= radius; i++) {
    int32 i_x = (int32)f_x + i;
    int32 i_y = (int32)f_y;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 x_delta = (float32)f_x - i_x;
    float32 distance = fabs(x_delta);
    float32 weight = gaussian_weight(distance, max_distance);

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelGaussianV(uint8* src, uint32 src_width, uint32 src_height,
                           float32 f_x, float32 f_y, float32 v_ratio,
                           uint8* output) {
  if (!src || !src_width || !src_height || f_x < 0 || f_y < 0 || !output) {
    return false;
  }

  int32 radius = v_ratio + 1.0f;
  float32 max_distance = v_ratio;
  float32 sample_count = 0;
  float32 total_samples[3] = {0};

  /* Scan the kernel space adding up the bicubic weights and pixel values. */
  for (int32 i = -radius; i <= radius; i++) {
    int32 i_x = (int32)f_x;
    int32 i_y = (int32)f_y + i;

    if (i_x < 0 || i_y < 0 || i_x > src_width - 1 || i_y > src_height - 1) {
      continue;
    }

    float32 y_delta = (float32)f_y - i_y;
    float32 distance = fabs(y_delta);
    float32 weight = gaussian_weight(distance, max_distance);

    uint8* src_pixel = BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y);

    /* accumulate bicubic weighted samples from the source. */
    total_samples[0] += src_pixel[0] * weight;
    total_samples[1] += src_pixel[1] * weight;
    total_samples[2] += src_pixel[2] * weight;

    /* record the total weights of the sample for later normalization. */
    sample_count += weight;
  }

  /* Normalize our bicubic sum back to the valid pixel range. */
  float32 scale_factor = 1.0f / sample_count;
  output[0] = clip_range(scale_factor * total_samples[0], 0, 255);
  output[1] = clip_range(scale_factor * total_samples[1], 0, 255);
  output[2] = clip_range(scale_factor * total_samples[2], 0, 255);

  return true;
}

bool SampleKernelAverage(uint8* src, uint32 src_width, uint32 src_height,
                         KernelDirection direction, float32 f_x, float32 f_y,
                         float32 h_ratio, float32 v_ratio, uint8* output) {
  switch (direction) {
    case KernelDirectionHorizontal:
      return SampleKernelAverageH(src, src_width, src_height, f_x, f_y, h_ratio,
                                  output);
    case KernelDirectionVertical:
      return SampleKernelAverageV(src, src_width, src_height, f_x, f_y, v_ratio,
                                  output);
  }

  return false;
}

bool SampleKernelGaussian(uint8* src, uint32 src_width, uint32 src_height,
                          KernelDirection direction, float32 f_x, float32 f_y,
                          float32 h_ratio, float32 v_ratio, uint8* output) {
  switch (direction) {
    case KernelDirectionHorizontal:
      return SampleKernelGaussianH(src, src_width, src_height, f_x, f_y,
                                   h_ratio, output);
    case KernelDirectionVertical:
      return SampleKernelGaussianV(src, src_width, src_height, f_x, f_y,
                                   v_ratio, output);
  }

  return false;
}

bool SampleKernelNearest(uint8* src, uint32 src_width, uint32 src_height,
                         float32 f_x, float32 f_y, uint8* output) {
  if (!src || !src_width || !src_height || !output) {
    return false;
  }

  int32 i_x = (int32)(f_x + 0.5f);
  int32 i_y = (int32)(f_y + 0.5f);

  /* Floating point pixel coordinates are pixel-center based. Thus, a coordinate
     of (0,0) refers to the center of the first pixel in an image, and a
     coordinate of (0.5,0) refers to the border between the first and second
     pixels. */
  i_x = clip_range(i_x, 0, src_width - 1);
  i_y = clip_range(i_y, 0, src_height - 1);

  /* Sample our pixel and write it to the output buffer. */
  memcpy(output, BLOCK_OFFSET_RGB24(src, src_width, i_x, i_y), 3);

  return true;
}

bool SampleKernel(uint8* src, uint32 src_width, uint32 src_height,
                  KernelDirection direction, float32 f_x, float32 f_y,
                  KernelType type, float32 h_ratio, float32 v_ratio,
                  uint8* output) {
  switch (type) {
    case KernelTypeNearest:
      return SampleKernelNearest(src, src_width, src_height, f_x, f_y, output);
    case KernelTypeBilinear:
      return SampleKernelBilinear(src, src_width, src_height, direction, f_x,
                                  f_y, output);
    case KernelTypeBicubic:
      return SampleKernelBicubic(src, src_width, src_height, direction, f_x,
                                 f_y, 0, 1, output);
    case KernelTypeCatmull:
      return SampleKernelBicubic(src, src_width, src_height, direction, f_x,
                                 f_y, 0, 0.5, output);
    case KernelTypeMitchell:
      return SampleKernelBicubic(src, src_width, src_height, direction, f_x,
                                 f_y, 1.0f / 3.0f, 1.0f / 3.0f, output);
    case KernelTypeCardinal:
      return SampleKernelBicubic(src, src_width, src_height, direction, f_x,
                                 f_y, 0.0f, 0.75f, output);
    case KernelTypeBSpline:
      return SampleKernelBicubic(src, src_width, src_height, direction, f_x,
                                 f_y, 1, 0, output);
    case KernelTypeLanczos:
      return SampleKernelLanczos(src, src_width, src_height, direction, f_x,
                                 f_y, 1, output);
    case KernelTypeLanczos2:
      return SampleKernelLanczos(src, src_width, src_height, direction, f_x,
                                 f_y, 2, output);
    case KernelTypeLanczos3:
      return SampleKernelLanczos(src, src_width, src_height, direction, f_x,
                                 f_y, 3, output);
    case KernelTypeLanczos4:
      return SampleKernelLanczos(src, src_width, src_height, direction, f_x,
                                 f_y, 4, output);
    case KernelTypeLanczos5:
      return SampleKernelLanczos(src, src_width, src_height, direction, f_x,
                                 f_y, 5, output);
    case KernelTypeAverage:
      return SampleKernelAverage(src, src_width, src_height, direction, f_x,
                                 f_y, h_ratio, v_ratio, output);
    case KernelTypeGaussian:
      return SampleKernelGaussian(src, src_width, src_height, direction, f_x,
                                  f_y, h_ratio, v_ratio, output);
  }

  return false;
}

/* Resamples a 24 bit RGB image using a bilinear, bicubic, or lanczos filter. */
bool ResampleImage24(uint8* src, uint32 src_width, uint32 src_height,
                     uint8* dst, uint32 dst_width, uint32 dst_height,
                     KernelType type, ::std::string* errors = nullptr) {
  if (!src || !dst || !src_width || !src_height || !dst_width || !dst_height ||
      type == KernelTypeUnknown) {
    if (errors) {
      *errors = "Invalid parameter passed to ResampleImage24.";
    }
    return false;
  }

  uint32 src_row_pitch = 3 * src_width;
  uint32 dst_row_pitch = 3 * dst_width;
  uint32 buffer_size = dst_row_pitch * src_height;
  uint32 dst_image_size = dst_row_pitch * dst_height;

  if (src_width == dst_width && src_height == dst_height) {
    /* no resampling needed, simply copy the image over. */
    memcpy(dst, src, dst_image_size);
    return true;
  }

  /* allocate a temporary buffer to hold our horizontal pass output. We're
     using unique_ptr rather than vector because we want a fast and smart way
     to allocate very large buffers without initialization. */
  ::std::unique_ptr<uint8[]> buffer(new uint8[buffer_size]);

  /* Prepare to perform our resample. This is perhaps the most important part of
    our resizer -- the calculation of our image ratios. These ratios are
    responsible for mapping between our integer pixel locations of the source
    image and our float sub-pixel coordinates within the source image that
    represent a reflection of our destination pixels.

    For a source 2x1 image and a destination 4x1 image:

            +------------+------------+
      Src:  |      0     |      1     |
            +------------+------------+
                   |           |
                  0.0         1.0
                   |           |
                 +---+---+---+---+
      Dst:       | 0 | 1 | 2 | 3 |
                 +---+---+---+---+

   o: Note that the center of the first and last pixels in both our src and dst
      images line up with our float edges of 0.0 and 1.0.

   o: Our sub-pixel interpolated coordinates will always be >= 0 and <=
      src_width.

   o: Thus the src pixel coordinate of our final destination pixel will always
      be src_width - 1.
  */

  /* ratios define our kernel size and resample factor. */
  float32 h_ratio =
      (1 == dst_width ? 1.0f : ((float32)src_width - 1) / (dst_width - 1));
  float32 v_ratio =
      (1 == dst_height ? 1.0f : ((float32)src_height - 1) / (dst_height - 1));

  /* horizontal sampling first. */
  for (uint32 j = 0; j < src_height; j++)
    for (uint32 i = 0; i < dst_width; i++) {
      uint8* output = BLOCK_OFFSET_RGB24(buffer.get(), dst_width, i, j);

      /* Determine the sub-pixel location of our *target* (i,j) coordinate, in
         the space of our source image. */
      float32 f_x = (float32)i * h_ratio;
      float32 f_y = (float32)j;

      if (!SampleKernel(src, src_width, src_height, KernelDirectionHorizontal,
                        f_x, f_y, type, h_ratio, v_ratio, output)) {
        if (errors) {
          *errors = "Failure during horizontal resample operation.";
        }
        return false;
      }
    }

  /* vertical sampling next. */
  for (uint32 j = 0; j < dst_height; j++)
    for (uint32 i = 0; i < dst_width; i++) {
      uint8* output = BLOCK_OFFSET_RGB24(dst, dst_width, i, j);

      /* Determine the sub-pixel location of our *target* (i,j) coordinate, in
         the space of our temp image. */
      float32 f_x = (float32)i;
      float32 f_y = (float32)j * v_ratio;

      if (!SampleKernel(buffer.get(), dst_width, src_height,
                        KernelDirectionVertical, f_x, f_y, type, h_ratio,
                        v_ratio, output)) {
        if (errors) {
          *errors = "Failure during vertical resample operation.";
        }
        return false;
      }
    }

  return true;
}

}  // namespace base

#endif  // __BASE_RESAMPLE_H__
