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

// Cadence of sampling GPS location points for upload to AWS IoT, in milliseconds.
const uint32_t GPS_POINT_PERIOD_IN_MS = 1000;

// GPS location point buffer size, in minutes. Used when, for example, network connection is down.
const uint32_t GPS_POINT_BUFFER_DURATION_IN_MIN = 10;

// Calculated milliseconds version of GPS_POINT_BUFFER_DURATION_IN_MIN.
const uint32_t GPS_POINT_BUFFER_DURATION_IN_MS = GPS_POINT_BUFFER_DURATION_IN_MIN * 60 * 1000;

// Mocking is smoother if accelerometer is sampled quickly - faster than the desired GPS point upload rate.
// Should be an even divisor of GPS_POINT_PERIOD_IN_MS above for accurate smoothing.
const uint32_t GPS_MOCK_CALC_PERIOD_IN_MS = 100;

// Mock GPS is not absolute; it must be relative to a given starting point.
const double GPS_MOCK_START_LAT = 44.98421;
const double GPS_MOCK_START_LON = -93.27502;

// Whether to mock GPS points (i.e., 'drive' based on tilting the EduKit) or use GPS hardware module accessory.
bool gpsMock = true;

// Mock GPS movement scale (multiplier of tilt angle to velocity).
enum MockScale {
    WALKING = 1,      // ~6 MPH max
    DRIVING = 10,     // ~60 MPH max
    FLYING  = 100     // ~600 MPH max
};

enum MockScale gpsMockScale = DRIVING;

// Optionally apply an offset to accelerometer values read from hardware (ex: compensate for uneven work surface).
float gpsMockAccelOffsetX = 0.05;
float gpsMockAccelOffsetY = 0.00;
