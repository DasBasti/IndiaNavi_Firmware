/*
 * Mockups for testing the display and rendering
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef TEST_HOST_PLATINENMACHER_MOCK_RENDERHOOKS_H
#define TEST_HOST_PLATINENMACHER_MOCK_RENDERHOOKS_H

#include "display.h"
#include "gui/label.h"

volatile int onBeforeRender_cnt = 0;
volatile int onAfterRender_cnt = 0;

/*
 * for the lable we render once and the second render will fail
 */

error_code_t onBeforeRenderCounter(const display_t* dsp, void* label)
{
    onBeforeRender_cnt += 1;
    if (onBeforeRender_cnt == 1)
        return PM_OK;

    return PM_FAIL;
}
error_code_t onAfterRenderCounter(const display_t* dsp, void* label)
{
    onAfterRender_cnt += 1;
    if (onAfterRender_cnt == 1)
        return PM_OK;

    return PM_FAIL;
}

#endif /* TEST_HOST_PLATINENMACHER_MOCK_RENDERHOOKS_H */