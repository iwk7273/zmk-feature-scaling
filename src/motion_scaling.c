#define DT_DRV_COMPAT zmk_input_processor_motion_scaler

#include "drivers/input_processor.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdint.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct scaler_data {
    int32_t x_accum;
    int32_t y_accum;
};

struct scaler_config {
    int scaling_mode; /* 0 or 1 */
    int scale_coeff_milli; /* e.g. 100 => 0.1 */
};

static int scaler_handle_event(
    const struct device *dev, struct input_event *event, uint32_t param1,
    uint32_t param2, struct zmk_input_processor_state *state) {
    struct scaler_data *data = dev->data;
    const struct scaler_config *config = dev->config;

    if (config->scaling_mode == 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->type) {
    case INPUT_EV_REL: {
        if (event->code == INPUT_REL_X) {
            data->x_accum += event->value;
            event->value = 0;
        } else if (event->code == INPUT_REL_Y) {
            data->y_accum += event->value;
            event->value = 0;
        } else {
            return ZMK_INPUT_PROC_CONTINUE;
        }

        float fx = (float)data->x_accum;
        float fy = (float)data->y_accum;
        float delta = sqrtf((fx * fx) + (fy * fy));

        if (delta <= 0.0f) {
            return ZMK_INPUT_PROC_STOP;
        }

        float coeff = ((float)config->scale_coeff_milli) / 1000.0f;
        float factor = coeff * delta;

        if (event->code == INPUT_REL_X) {
            float scaled_x = fx * factor;
            int32_t out_x = (int32_t)lrintf(scaled_x);
            event->value = out_x;
            data->x_accum = 0;
        } else if (event->code == INPUT_REL_Y) {
            float scaled_y = fy * factor;
            int32_t out_y = (int32_t)lrintf(scaled_y);
            event->value = out_y;
            data->y_accum = 0;
        }

        return ZMK_INPUT_PROC_CONTINUE;
    }
    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }
}

static struct zmk_input_processor_driver_api scaler_driver_api = {
    .handle_event = scaler_handle_event,
};

#define SCALER_INST(n) \
    static struct scaler_data scaler_data_##n = { \
        .x_accum = 0, \
        .y_accum = 0, \
    }; \
    static const struct scaler_config scaler_config_##n = { \
        .scaling_mode = DT_INST_PROP(n, scaling_mode), \
        .scale_coeff_milli = DT_INST_PROP_OR(n, scale_coeff_milli, CONFIG_ZMK_INPUT_PROCESSOR_SCALER_DEFAULT_COEFF_MILLI), \
    }; \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, \
                          &scaler_data_##n, &scaler_config_##n, \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                          &scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCALER_INST)


