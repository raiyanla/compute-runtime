/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <level_zero/zet_api.h>

namespace L0 {

class sysmanDevice {
  public:
    virtual ~sysmanDevice(){};
    virtual ze_result_t deviceGetProperties(zet_sysman_properties_t *pProperties) = 0;

    virtual void init() = 0;
};

} // namespace L0
