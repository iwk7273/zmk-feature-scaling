#define DT_DRV_COMPAT zmk_input_processor_motion_scaler

#include "drivers/input_processor.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>

LOG_MODULE_REGISTER(motion_scaler, CONFIG_ZMK_LOG_LEVEL);

// Q16 layout for remainder tracking only
#define Q16_ONE        (1 << 16)

struct scaler_data {
    int32_t remainder_x_q16;
    int32_t remainder_y_q16;
};

struct scaler_config {
    int32_t scaling_mode; /* 0 or 1 */
    int max_output;    /* 最大出力（|y|の上限） */
    int half_input;    /* 半入力（r=|x|/half_input の基準） */
    int exponent_tenths; /* 指数p（小数1桁、p*10で保持） */
    bool track_remainders;
};

static inline int32_t scale_axis_apply(int32_t dx, const struct scaler_config *config,
                                       int32_t *remainder_q16) {
    if (dx == 0) {
        return 0;
    }

    // Compute using float strictly per formula
    const int sign = (dx >= 0) ? 1 : -1;
    const int aabs = (dx >= 0) ? dx : -dx;
    const int xs = (config->half_input > 0) ? config->half_input : 1;
    const float r = (float)aabs / (float)xs;
    float p = (float)((config->exponent_tenths < 0) ? 0 : config->exponent_tenths) / 10.0f;
    const float e = p + 1.0f;
    float rp1 = powf(r, e);
    if (!isfinite(rp1)) {
        rp1 = INFINITY; // treat as saturating
    }
    const float frac = rp1 / (1.0f + rp1); // in (0,1)
    float y = (float)config->max_output * frac * (float)sign;
    // Convert to Q16 for remainder accumulation
    int32_t scaled_q16 = (int32_t)lrintf(y * (float)Q16_ONE);

    int32_t out;
    if (config->track_remainders) {
        scaled_q16 += *remainder_q16;
        // Use truncate-towards-zero division to extract integer part
        out = scaled_q16 / Q16_ONE;
        // Keep fractional remainder with the same sign as scaled_q16
        *remainder_q16 = scaled_q16 - out * Q16_ONE;
    } else {
        out = (scaled_q16 + (1 << 15)) >> 16; // round to nearest
    }

    // Clamp to [-max_output, max_output]
    if (out > config->max_output) out = config->max_output;
    if (out < -config->max_output) out = -config->max_output;

    return out;
}

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
            int32_t in_x = event->value;
            int32_t out_x = scale_axis_apply(in_x, config, &data->remainder_x_q16);
            LOG_DBG("motion_scaler REL_X in=%d out=%d rem_q16=%d", in_x, out_x, data->remainder_x_q16);
            event->value = out_x;
        } else if (event->code == INPUT_REL_Y) {
            int32_t in_y = event->value;
            int32_t out_y = scale_axis_apply(in_y, config, &data->remainder_y_q16);
            LOG_DBG("motion_scaler REL_Y in=%d out=%d rem_q16=%d", in_y, out_y, data->remainder_y_q16);
            event->value = out_y;
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
        .remainder_x_q16 = 0, \
        .remainder_y_q16 = 0, \
    }; \
    static const struct scaler_config scaler_config_##n = { \
        .scaling_mode = DT_INST_PROP(n, scaling_mode), \
        .max_output = DT_INST_PROP_OR(n, max_output, 127), \
        .half_input = DT_INST_PROP_OR(n, half_input, 50), \
        .exponent_tenths = DT_INST_PROP_OR(n, exponent_tenths, 10), \
        .track_remainders = DT_INST_PROP(n, track_remainders), \
    }; \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, \
                          &scaler_data_##n, &scaler_config_##n, \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                          &scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCALER_INST)




