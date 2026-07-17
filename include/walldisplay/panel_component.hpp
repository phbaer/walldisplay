#pragma once

#include "esp_err.h"

class PanelComponent {
public:
    virtual ~PanelComponent() = default;
    virtual esp_err_t update(const char *payload) = 0;
    virtual void set_visible(bool visible) = 0;
};
