/*
 * AWS IoT EduKit - Core2 for AWS IoT EduKit
 * Device Tracking v0.1.0
 * main.c
 * 
 * Copyright 2010-2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Additions Copyright 2016 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
/**
 * @file main.h
 * @brief Compile-time configuration values for Device-Tracker demo app.
 */
#include <stdint.h>

// Cadence of reading GPS location points locally, for buffering and uploading to AWS IoT, in milliseconds.
static const uint32_t gpsPointPeriodInMs = 10000;

// GPS location point buffer size, in minutes. Used when, for example, network connection is down.
static const uint32_t gpsPointBufferDurationInMin = 10;

// Calculated milliseconds version of gpsPointBufferDurationInMin.
static const uint32_t gpsPointBufferDurationInMs = gpsPointBufferDurationInMin * 60 * 1000;