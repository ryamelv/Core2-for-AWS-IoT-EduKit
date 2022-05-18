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
 * @file main.c
 * @brief Demonstration of streaming AWS IoT EduKit + GPS module location updates to AWS IoT.
 *
 * Some configuration is required. See "Device Tracking" chapter of https://edukit.workshop.aws.
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"

#include "core2forAWS.h"

#include "wifi.h"
#include "iot.h"
#include "ui.h"
#include "main.h"


// Logging identifier for this module.
static const char *TAG = "MAIN";

// GPS location point (currently a place holder).
struct GpsPoint {
    int tbd;
};

// Local buffer/queue for GPS points to upload to AWS IoT.
QueueHandle_t xGpsPointsQueue;

// AWS IoT device client identifier. Only valid after init(). See documentation for Atecc608_GetSerialString().
#define CLIENT_ID_LEN ((ATCA_SERIAL_NUM_SIZE * 2) + 1)
char clientId[CLIENT_ID_LEN] = "<UNK>";

// AWS IoT MQTT topic: "<client_id>/location". Only valid after init().
#define MQTT_TOPIC_NAME_LEN (CLIENT_ID_LEN + 9)
char mqttTopicName[MQTT_TOPIC_NAME_LEN] = "<UNK>";


void check_task_stack_usage() {
    const char* const taskName = pcTaskGetTaskName(NULL);

    const UBaseType_t taskStackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    const UBaseType_t inBytes = (taskStackHighWaterMark * 4);                        // Assuming 32-bit CPU

    ESP_LOGI(TAG, "Task '%s' min unused stack: %d bytes", taskName, inBytes);

    if(512 > inBytes) {
        ESP_LOGW(TAG, "Task '%s' stack may be undersized.", taskName);
    }
}


void produce_gps_points_task(void *param) {
    bool giveUp = false;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(gpsPointPeriodInMs);
    BaseType_t rc = pdFALSE;
    struct GpsPoint gpsPoint = {0};

    // Don't bother producing GPS points until we connect to the network for the first time.
    wifi_wait_for_connection_up();

    while(!giveUp) {
        // Pause here to produce GPS points at a given frequency.
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        // create GPS point
        gpsPoint.tbd = 7;
        ESP_LOGI(TAG, "Producing GPS Point: %d", gpsPoint.tbd);

        // store to queue
        rc = xQueueSendToBack(xGpsPointsQueue, &gpsPoint, 0);

        if(pdTRUE != rc){
            ESP_LOGW(TAG, "GPS points queue full; discarding GPS point.");
        }
    }

    ESP_LOGE(TAG, "Fatal error in produce_gps_points task.");

    vTaskDelete(NULL);
}


IoT_Error_t publish_one_mqtt_message(AWS_IoT_Client* aws_iot_client, struct GpsPoint* gpsPoint) {
    char cPayload[100];

    sprintf(cPayload, "%s (%d)", "Hello from AWS IoT EduKit ", gpsPoint->tbd);

    IoT_Publish_Message_Params paramsQOS0;

    paramsQOS0.qos = QOS0;
    paramsQOS0.isRetained = 0;
    paramsQOS0.payload = (void *) cPayload;
    paramsQOS0.payloadLen = strlen(cPayload);

    // Publish and ignore if "ack" was received or from AWS IoT Core
    IoT_Error_t rc = aws_iot_mqtt_publish(aws_iot_client, mqttTopicName, strlen(mqttTopicName), &paramsQOS0);

    if (rc != SUCCESS){
        ESP_LOGW(TAG, "aws_iot_mqtt_publish() error: %d ", rc);
    }

    return(rc);
}


void upload_gps_points_task(void* param) {
    // Establish connection to AWS IoT.
    AWS_IoT_Client aws_iot_client;

    if(SUCCESS != aws_iot_client_init(&aws_iot_client)) {
        abort();
    }

    // Don't bother trying to connect to AWS IoT until we connect to the network for the first time.
    wifi_wait_for_connection_up();

    // This blocks (retries) until a first connection is established.
    if(SUCCESS != aws_iot_client_connect(&aws_iot_client, clientId)) {
        abort();
    }

    bool giveUp = false;
    BaseType_t rc = pdFALSE;
    IoT_Error_t iot_rc = SUCCESS;
    struct GpsPoint gpsPoint = {0};

    while(!giveUp) {
        // Read from queue. Must wake periodically to yield (see below).
        const TickType_t xBlockTime = pdMS_TO_TICKS(10000);
        rc = xQueuePeek(xGpsPointsQueue, &gpsPoint, xBlockTime);

        if(pdTRUE == rc) {
            // Received a queue item.
            ESP_LOGI(TAG, "Uploading GPS Point: %d", gpsPoint.tbd);
            // upload to AWS IoT message topic
            iot_rc = publish_one_mqtt_message(&aws_iot_client, &gpsPoint);

            if(SUCCESS == iot_rc) {
                rc = xQueueReceive(xGpsPointsQueue, &gpsPoint, 0);

                if(pdTRUE != rc) {
                    ESP_LOGW(TAG, "xQueueReceive() failed: %d ", rc);
                }
            }
        }

        // AWS IoT Client requires periodic thread time to manage the AWS IoT connection and receive messages.
        iot_rc = aws_iot_mqtt_yield(&aws_iot_client, 100);

        if(SUCCESS != iot_rc) {
            ESP_LOGW(TAG, "aws_iot_mqtt_yield() returned: %d ", iot_rc);
        }
    }

    ESP_LOGE(TAG, "Fatal error in upload_gps_points task.");

    vTaskDelete(NULL);
}


bool get_client_id() {
    ATCA_STATUS rc = Atecc608_GetSerialString(clientId);

    if (ATCA_SUCCESS != rc) {
        ESP_LOGE(TAG, "Atecc608_GetSerialString() failed: %i", rc);
    }
    else {
        ESP_LOGI(TAG, "ATECC608 SN# = AWS IoT Device Client ID = '%s'", clientId);
        ui_textarea_add("\nAWS IoT Device Client ID:\n>> %s <<\n", clientId, CLIENT_ID_LEN);
    }

    return(ATCA_SUCCESS == rc);
}


void init() {
    Core2ForAWS_Init();
    Core2ForAWS_Display_SetBrightness(80);
    Core2ForAWS_LED_Enable(1);

    ui_init();
    initialise_wifi();

    if(!get_client_id()) {
        abort();
    }

    sprintf(mqttTopicName, "%s/location", clientId);
}


void app_main() {
    BaseType_t rc = pdPASS;

    ESP_LOGI(TAG, "Starting Device-Tracking Demo App...");

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    init();

    // Create a local buffer/queue for GPS points to then upload to AWS IoT (the math rounds up).

    UBaseType_t gpsPointQueueLength = ( gpsPointBufferDurationInMs + (gpsPointPeriodInMs - 1) ) / gpsPointPeriodInMs;

    ESP_LOGI(TAG, "Creating GPS points queue with depth %d items = %d min", gpsPointQueueLength, gpsPointBufferDurationInMin);

    xGpsPointsQueue = xQueueCreate(gpsPointQueueLength, sizeof(struct GpsPoint));

    if(NULL == xGpsPointsQueue) {
        ESP_LOGE(TAG, "Failed to create GPS points queue.");
        abort();
    }

    // Create the task that produces (acquires from the hardware GPS module or mocks) GPS points into the queue.

    ESP_LOGI(TAG, "Creating task to produce GPS points...");

    rc = xTaskCreate(produce_gps_points_task, "produce_gps_points", 2*4096, NULL, 5, NULL);

    if(pdPASS != rc) {
        ESP_LOGE(TAG, "Failed to create task to produce GPS points: %d", rc);
    }

    // Create the task that uploads GPS points from the queue to AWS IoT.

    ESP_LOGI(TAG, "Creating task to upload GPS points...");

    rc = xTaskCreate(upload_gps_points_task, "upload_gps_points", 2*4096, NULL, 10, NULL);

    if(pdPASS != rc) {
        ESP_LOGE(TAG, "Failed to create task to upload GPS points: %d", rc);
    }
}
