/*
 * AWS IoT EduKit - Core2 for AWS IoT EduKit
 * Device Tracking v0.1.0
 * ui.h
 * 
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "core2forAWS.h"


// There are three buttons on this application-specific user interface.
typedef enum UiButton {
    BTN_LEFT,
    BTN_CENTER,
    BTN_RIGHT,
    BTN_COUNT      // Not a button; indicates number of buttons.
} UiButton;

typedef void (*btn_event_cb_t)(UiButton btn, lv_event_t event);

void ui_hdr_txt_set(const char* txt, const char* param, size_t paramLen);
void ui_out_txt_add(const char* txt, const char* param, size_t paramLen);
void ui_wifi_label_update(bool state);

void ui_btn_txt_set(UiButton btn, const char* txt);
void ui_btn_event_cb_set(UiButton btn, btn_event_cb_t func);

void ui_init();