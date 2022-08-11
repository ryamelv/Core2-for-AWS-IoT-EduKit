/*
 * AWS IoT EduKit - Core2 for AWS IoT EduKit
 * Device Tracking v0.1.0
 * iot.c
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
 * @file iot.c
 * @brief Functions for managing an AWS_IoT_Client.
 */

#include <unistd.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_mqtt_client_interface.h"

#include "ui.h"


// Logging identifier for this module.
static const char *TAG = "IOT";

// Use identifiers from aws_iot_config.h, but AWS account/region specific values ultimately come from sdkconfig.
// Need a char buffer only because IoT_Client_Init_Params.pHostURL requires a (non-cost) char*.
char awsIotMqttHostUrl[] = AWS_IOT_MQTT_HOST;
const uint16_t awsIotMqttHostPort = AWS_IOT_MQTT_PORT;

// AWS Root Certificate Authority certificate.
extern const char aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");


void aws_iot_client_disconnect_handler(AWS_IoT_Client* aws_iot_client, void* data) {
    ESP_LOGW(TAG, "AWS IoT Client Disconnect; Auto-reconnecting...");
}


IoT_Error_t aws_iot_client_init(AWS_IoT_Client* aws_iot_client) {
    IoT_Client_Init_Params initParams = iotClientInitParamsDefault;

    initParams.pHostURL = awsIotMqttHostUrl;
    initParams.port = awsIotMqttHostPort;

    initParams.pRootCALocation = aws_root_ca_pem_start;

    initParams.pDeviceCertLocation = "#";
    initParams.pDevicePrivateKeyLocation = "#0";
    initParams.isSSLHostnameVerify = true;

    initParams.enableAutoReconnect = false;       // Auto-connect is later enabled explicitly after first connect.
    initParams.mqttCommandTimeout_ms = 20000;
    initParams.tlsHandshakeTimeout_ms = 5000;

    initParams.disconnectHandler = aws_iot_client_disconnect_handler;
    initParams.disconnectHandlerData = NULL;

    // aws_iot_mqtt_init() makes its own copy of the init params.
    IoT_Error_t rc = aws_iot_mqtt_init(aws_iot_client, &initParams);

    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_init() error: %d ", rc);
    }

    return(rc);
}


IoT_Error_t aws_iot_client_connect(AWS_IoT_Client* aws_iot_client, const char* clientId) {
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    connectParams.MQTTVersion = MQTT_3_1_1;

    connectParams.pClientID = clientId;
    connectParams.clientIDLen = strlen(clientId);

    connectParams.isCleanSession = true;
    connectParams.isWillMsgPresent = false;
    connectParams.keepAliveIntervalInSec = 10;

    ui_out_txt_add("Connecting to AWS IoT Core...\n", NULL, 0);

    IoT_Error_t rc = SUCCESS;

    do {
        ESP_LOGI(TAG, "Connecting to AWS IoT Core at %s:%d...", awsIotMqttHostUrl, awsIotMqttHostPort);

        rc = aws_iot_mqtt_connect(aws_iot_client, &connectParams);

        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "aws_iot_mqtt_connect() error: %d ", rc);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

    } while(SUCCESS != rc);

    ui_out_txt_add("Connected to AWS IoT Core.\n", NULL, 0);
    ESP_LOGI(TAG, "Connected to AWS IoT Core.");

    // Enable auto-reconnect (must be done after first connect).
    rc = aws_iot_mqtt_autoreconnect_set_status(aws_iot_client, true);

    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_autoreconnect_set_status() error: %d ", rc);
    }

    // Return value is based on connection, not enabling auto-reconnect.
    return(SUCCESS);
}


IoT_Error_t aws_iot_client_publish(AWS_IoT_Client* aws_iot_client, const char* topic, const char* msg) {
    IoT_Publish_Message_Params params;

    params.qos = QOS0;
    params.isRetained = 0;
    params.payload = (void *) msg;
    params.payloadLen = strlen(msg);

    ESP_LOGI(TAG, "Publishing MQTT Message: [%s] %s", topic, msg);

    IoT_Error_t rc = aws_iot_mqtt_publish(aws_iot_client, topic, strlen(topic), &params);

    if (rc != SUCCESS){
        ESP_LOGW(TAG, "aws_iot_mqtt_publish() error: %d ", rc);
    }

    return(rc);
}
