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
    int32_t gain_q16;       /* Latched isotropic gain used within this frame (Q16), updated at previous sync */
    int32_t acc_x;          /* Raw X accumulated within current frame */
    int32_t acc_y;          /* Raw Y accumulated within current frame */
};

struct scaler_config {
    int32_t scaling_mode; /* 0 or 1 */
    int max_output;    /* 最大出力（|y|の上限） */
    int half_input;    /* 半入力（r=|x|/half_input の基準） */
    int exponent_tenths; /* 指数p（小数1桁、p*10で保持） */
    bool track_remainders;
};

/* Compute target output magnitude for a given vector magnitude using the
 * existing y(x) formula (without sign, remainder, or clamping to axis).
 * Returns a non-negative float magnitude.
 */
static inline float scale_magnitude(float mag, const struct scaler_config *config) {
    if (mag <= 0.0f) {
        return 0.0f;
    }
    const int xs = (config->half_input > 0) ? config->half_input : 1;
    const float r = mag / (float)xs;
    float p = (float)((config->exponent_tenths < 0) ? 0 : config->exponent_tenths) / 10.0f;
    const float e = p + 1.0f;
    float rp1 = powf(r, e);
    if (!isfinite(rp1)) {
        rp1 = INFINITY;
    }
    const float frac = rp1 / (1.0f + rp1); // (0,1)
    float ymag = (float)config->max_output * frac;
    if (!isfinite(ymag)) {
        ymag = (float)config->max_output;
    }
    if (ymag < 0.0f) {
        ymag = 0.0f;
    }
    return ymag;
}

/* Clamp helper for axis output */
static inline int32_t clamp_axis_output(int32_t v, int max_output) {
    if (v > max_output) return max_output;
    if (v < -max_output) return -max_output;
    return v;
}

/* Apply latched isotropic gain (Q16) to a single axis input, with optional
 * Q16 remainder tracking and clamping to max_output. Returns integer output.
 */
static inline int32_t apply_gain_axis_q16(int32_t in,
                                          int32_t gain_q16,
                                          const struct scaler_config *config,
                                          int32_t *remainder_q16) {
    if (in == 0) {
        return 0;
    }
    int64_t scaled_q16 = (int64_t)in * (int64_t)gain_q16;
    scaled_q16 += *remainder_q16;
    int32_t out = (int32_t)(scaled_q16 / Q16_ONE);
    *remainder_q16 = (int32_t)(scaled_q16 - (int64_t)out * Q16_ONE);
    return clamp_axis_output(out, config->max_output);
}

/* Accumulate raw axis input into the current frame accumulator */
static inline void accumulate_axis(struct scaler_data *data, int code, int32_t v) {
    if (code == INPUT_REL_X) {
        data->acc_x += v;
    } else if (code == INPUT_REL_Y) {
        data->acc_y += v;
    }
}

/* Compute next frame's gain (Q16) from current accumulators using the
 * existing curve on vector magnitude. Returns Q16 gain.
 */
static inline int32_t compute_next_gain_q16_from_acc(const struct scaler_data *data,
                                                     const struct scaler_config *config) {
    float ax = (float)data->acc_x;
    float ay = (float)data->acc_y;
    float mag = sqrtf(ax * ax + ay * ay);
    if (mag <= 0.0f) {
        return Q16_ONE; /* unity gain when no movement */
    }
    float ymag = scale_magnitude(mag, config);
    float kf = ymag / mag;
    if (!isfinite(kf) || kf < 0.0f) {
        kf = 0.0f;
    }
    float kq = kf * (float)Q16_ONE;
    if (kq > 2147483000.0f) kq = 2147483000.0f;
    if (kq < 0.0f) kq = 0.0f;
    return (int32_t)lrintf(kq);
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
            accumulate_axis(data, INPUT_REL_X, in_x);
            int32_t out_x =
                apply_gain_axis_q16(in_x, data->gain_q16, config, &data->remainder_x_q16);
            LOG_DBG("motion_scaler REL_X in=%d out=%d rem_q16=%d k_q16=%d", in_x, out_x,
                    data->remainder_x_q16, data->gain_q16);
            event->value = out_x;
        } else if (event->code == INPUT_REL_Y) {
            int32_t in_y = event->value;
            accumulate_axis(data, INPUT_REL_Y, in_y);
            int32_t out_y =
                apply_gain_axis_q16(in_y, data->gain_q16, config, &data->remainder_y_q16);
            LOG_DBG("motion_scaler REL_Y in=%d out=%d rem_q16=%d k_q16=%d", in_y, out_y,
                    data->remainder_y_q16, data->gain_q16);
            event->value = out_y;
        }
        break;
    }
    default:
        break;
    }

    /* sync=true でフレーム終端。次フレーム用のkを算出してラッチ。 */
    if (event->sync) {
        int32_t next_k_q16 = compute_next_gain_q16_from_acc(data, config);
        data->gain_q16 = next_k_q16;
        data->acc_x = 0;
        data->acc_y = 0;
        LOG_DBG("motion_scaler frame end: k_q16=%d", data->gain_q16);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api scaler_driver_api = {
    .handle_event = scaler_handle_event,
};

#define SCALER_INST(n) \
    static struct scaler_data scaler_data_##n = { \
        .remainder_x_q16 = 0, \
        .remainder_y_q16 = 0, \
        .gain_q16 = Q16_ONE, \
        .acc_x = 0, \
        .acc_y = 0, \
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

