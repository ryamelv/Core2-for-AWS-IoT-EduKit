/*
 * AWS IoT EduKit - Core2 for AWS IoT EduKit
 * Device Tracking v0.1.0
 * iot.h
 *
 * Copyright 2010-2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
 * @file iot.h
 * @brief Declarations for managing an AWS_IoT_Client.
 */

#pragma once

#include "aws_iot_mqtt_client_interface.h"


IoT_Error_t aws_iot_client_init(AWS_IoT_Client* aws_iot_client);
IoT_Error_t aws_iot_client_connect(AWS_IoT_Client* aws_iot_client, const char* clientId);
