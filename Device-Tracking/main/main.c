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
#include <time.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"

#include "core2forAWS.h"
#include "esp_sntp.h"

#include "wifi.h"
#include "iot.h"
#include "ui.h"
#include "main.h"


// Logging identifier for this module.
static const char *TAG = "MAIN";

// GPS location point.
struct GpsPoint {
    time_t sampleTime;
    double lon;
    double lat;
};

// Local buffer/queue for GPS points to upload to AWS IoT.
QueueHandle_t xGpsPointsQueue;

// AWS IoT device client identifier. Only valid after init(). See documentation for Atecc608_GetSerialString().
#define CLIENT_ID_LEN ((ATCA_SERIAL_NUM_SIZE * 2) + 1)
char clientId[CLIENT_ID_LEN] = "<UNK>";

// AWS IoT MQTT topic ("<client_id>/location"). Only valid after init().
const char mqttTopicNamePostfix[] = "/location";
#define MQTT_TOPIC_NAME_LEN (CLIENT_ID_LEN + sizeof(mqttTopicNamePostfix))
char mqttTopicName[MQTT_TOPIC_NAME_LEN] = "<UNK>";

// Optionally pause GPS point production (perhaps while out of WiFi range).
bool paused = false;


void check_task_stack_usage() {
    const char* const taskName = pcTaskGetTaskName(NULL);

    const UBaseType_t taskStackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    const UBaseType_t inBytes = (taskStackHighWaterMark * 4);                        // Assuming 32-bit CPU

    ESP_LOGD(TAG, "Task '%s' min unused stack: %d bytes", taskName, inBytes);

    if(512 > inBytes) {
        ESP_LOGW(TAG, "Task '%s' stack may be undersized.", taskName);
    }
}


void get_mock_gps_point(struct GpsPoint* gpsPoint) {
    // Read current accelerometer hardware values.
    float xA = 0, yA = 0, zA = 0;
    MPU6886_GetAccelData(&xA, &yA, &zA);
    gpsPoint->sampleTime = time(NULL);

    // Calibrate (see main.h).
    float xAc = xA + gpsMockAccelOffsetX, yAc = yA + gpsMockAccelOffsetY;

    // Round to tenths to remove jitter.
    float xAr = round(10 * xAc) / 10, yAr = round(10 * yAc) / 10;

    // Normalize per number of samples per second, to make impact independent of sample rate.
    xAr = ( xAr * (GPS_POINT_PERIOD_IN_MS / 1000.0) );
    yAr = ( yAr * (GPS_POINT_PERIOD_IN_MS / 1000.0) );

    // Consider each value (about -1.0 to 1.0) a percentage of jogging speed (6 MPH = 8.8 fps).
    float xIncD = xAr * 8.8;
    float yIncD = yAr * 8.8;

    // Scale by walking/driving/flying (see main.h).
    xIncD *= gpsMockScale;
    yIncD *= gpsMockScale;

    // Accumulate absolute distance from starting point.
    static double xD = 0, yD = 0;
    xD += xIncD;
    yD += yIncD;

    // Convert feet to GPS.  Conversion factor is a very rough estimate.
    double gpsLat = GPS_MOCK_START_LAT, gpsLon = GPS_MOCK_START_LON;
    const double ftToGpsConv = 0.000002160039;
    gpsLat -= (yD * ftToGpsConv); gpsLon -= (xD * ftToGpsConv);

    ESP_LOGD(TAG, "Raw: %+.2f/%+.2f | Calib: %+.2f/%+.2f | Rounded: %+.2f/%+.2f | Dist: %+.0lf/%+.0lf | GPS: %.6lf/%.6lf",
        xA, yA, xAc, yAc, xAr, yAr, xD, yD, gpsLat, gpsLon);

    gpsPoint->lat = gpsLat;
    gpsPoint->lon = gpsLon;
}


void get_gps_point(struct GpsPoint* gpsPoint) {
    if(gpsMock) {
        get_mock_gps_point(gpsPoint);
    }
    else {
        ESP_LOGW(TAG, "Non-Mock GPS points not implemented yet!");
        gpsPoint->sampleTime = time(NULL);
        gpsPoint->lat = GPS_MOCK_START_LAT;
        gpsPoint->lon = GPS_MOCK_START_LON;
    }
}


int get_produce_loops_per_gps_point() {
    static const uint32_t MOCK_LOOPS_PER_GPS_POINT = GPS_POINT_PERIOD_IN_MS / GPS_MOCK_CALC_PERIOD_IN_MS;
    return( gpsMock ? MOCK_LOOPS_PER_GPS_POINT : 1 );
}


void produce_gps_points_task(void *param) {
    bool giveUp = false;
    BaseType_t rc = pdFALSE;
    TickType_t xWakePeriod = pdMS_TO_TICKS(GPS_POINT_PERIOD_IN_MS);
    struct GpsPoint gpsPoint = {0};

    // Don't bother producing GPS points until we connect to the network for the first time.
    wifi_wait_for_connection_up();

    // vTaskDelayUntil() below requires an initial starting time.
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Some complexity for mock mode due to looping faster than GPS point production period.
    for(int loops = 0; !giveUp; loops = (loops + 1) % get_produce_loops_per_gps_point()) {
        // Pause here to produce GPS points at a given frequency.
        vTaskDelayUntil(&xLastWakeTime, xWakePeriod / get_produce_loops_per_gps_point());

        get_gps_point(&gpsPoint);

        // If mocking GPS points, only produce at the desired upload rate despite calculating (looping) more frequently.
        if(0 == loops && !paused) {
            ESP_LOGD(TAG, "Producing GPS Point: %ld [%lf, %lf]", gpsPoint.sampleTime, gpsPoint.lon, gpsPoint.lat);

            // Store to queue.
            rc = xQueueSendToBack(xGpsPointsQueue, &gpsPoint, 0);

            if(pdTRUE != rc){
                ESP_LOGW(TAG, "GPS points queue full; discarding GPS point.");
            }

            check_task_stack_usage();
        }
    }

    ESP_LOGE(TAG, "Fatal error in produce_gps_points task.");

    vTaskDelete(NULL);
}


IoT_Error_t publish_one_gps_point(AWS_IoT_Client* aws_iot_client, struct GpsPoint* gpsPoint) {
    static const char example[] =
        "{ 'SampleTime': 1652985753, 'Position': [ -93.274963, 44.984379 ] }";

    static char msgBuf[sizeof(example) * 2];

    sprintf(msgBuf, "{ \"SampleTime\": %ld, \"Position\": [ %lf, %lf ] }",
        gpsPoint->sampleTime, gpsPoint->lon, gpsPoint->lat);

    IoT_Error_t rc = aws_iot_client_publish(aws_iot_client, mqttTopicName, msgBuf);

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

        // Message received; upload to AWS IoT.
        if(pdTRUE == rc) {
            iot_rc = publish_one_gps_point(&aws_iot_client, &gpsPoint);

            if(SUCCESS == iot_rc) {
                // Above only peeked; remove sent message from queue.
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

        check_task_stack_usage();
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
    }

    return(ATCA_SUCCESS == rc);
}


const char* MockModeText() {
    return( gpsMock ? "Mock" : "GPS" );
}


const char* MockScaleText () {
    return( (WALKING == gpsMockScale) ? "Walk" : (DRIVING == gpsMockScale) ? "Drive" : "Fly" );
}


const char* PausedText() {
    return( paused ? "Paused" : "Active" );
}


void on_btn_event(UiButton btn, lv_event_t event) {
    if(LV_EVENT_PRESSED == event) {
        if(BTN_LEFT == btn) {
            gpsMock = !gpsMock;
            ui_btn_txt_set(btn, MockModeText());
        }
        else if(BTN_CENTER == btn) {
            gpsMockScale = (WALKING == gpsMockScale) ? DRIVING : (DRIVING == gpsMockScale) ? FLYING : WALKING;
            ui_btn_txt_set(btn, MockScaleText());
        }
        else if(BTN_RIGHT == btn) {
            paused = !paused;
            ui_btn_txt_set(btn, PausedText());
        }
    }
}


void init() {
    Core2ForAWS_Init();
    Core2ForAWS_Display_SetBrightness(80);
    Core2ForAWS_LED_Enable(1);

    ui_init();
    initialise_wifi();

    // Accurate time is needed to timestamp the GPS points.
    wifi_wait_for_connection_up();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    if(!get_client_id()) {
        abort();
    }

    // Can set the MQTT topic now that the client id is available.
    sprintf(mqttTopicName, "%s%s", clientId, mqttTopicNamePostfix);

    // display
    ui_hdr_txt_set("ID: %s", clientId, CLIENT_ID_LEN);
    ui_out_txt_add("Device Tracking\n", NULL, 0);

    // buttons
    ui_btn_txt_set(BTN_LEFT, MockModeText());
    ui_btn_txt_set(BTN_CENTER, MockScaleText());
    ui_btn_txt_set(BTN_RIGHT, PausedText());

    ui_btn_event_cb_set(BTN_LEFT, on_btn_event);
    ui_btn_event_cb_set(BTN_CENTER, on_btn_event);
    ui_btn_event_cb_set(BTN_RIGHT, on_btn_event);
}


void app_main() {
    BaseType_t rc = pdPASS;

    ESP_LOGI(TAG, "Starting Device-Tracking Demo App...");

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    init();

    // Create a local buffer/queue for GPS points to then upload to AWS IoT (the math rounds up).

    UBaseType_t gpsPointQueueLength = ( GPS_POINT_BUFFER_DURATION_IN_MS + (GPS_POINT_PERIOD_IN_MS - 1) ) / GPS_POINT_PERIOD_IN_MS;

    ESP_LOGI(TAG, "Creating GPS points queue with depth %d items = %d min", gpsPointQueueLength, GPS_POINT_BUFFER_DURATION_IN_MIN);

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
