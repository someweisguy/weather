#pragma once

#include "esp_system.h"

float equalize(float *input, float* output, size_t len);

float weight_dBC(float *input, float* output, size_t len);