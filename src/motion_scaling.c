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

// Q16 fixed-point helpers
#define Q16_ONE        (1 << 16)
#define Q16_SAT_MAX    0x3FFFFFFF /* leave headroom to avoid overflow on addition */

static inline int32_t q16_mul_sat(int32_t a, int32_t b) {
    int64_t t = ((int64_t)a * (int64_t)b) >> 16;
    if (t > Q16_SAT_MAX) return Q16_SAT_MAX;
    if (t < -Q16_SAT_MAX) return -Q16_SAT_MAX;
    return (int32_t)t;
}

static inline int32_t q16_div(int32_t a, int32_t b) {
    if (b == 0) {
        // Division by zero not expected; return 0 (safe fallback for our usage)
        return 0;
    }
    return (int32_t)(((int64_t)a << 16) / (int64_t)b);
}

static inline int32_t q16_pow_i_sat(int32_t base_q16, unsigned int exp) {
    // Exponentiation by squaring with saturation
    int32_t result = Q16_ONE;
    int32_t base = base_q16;
    while (exp > 0) {
        if (exp & 1u) {
            result = q16_mul_sat(result, base);
        }
        exp >>= 1;
        if (exp) {
            base = q16_mul_sat(base, base);
        }
    }
    return result;
}

struct scaler_data {
    int32_t remainder_x_q16;
    int32_t remainder_y_q16;
};

struct scaler_config {
    int32_t scaling_mode; /* 0 or 1 */
    int u;  /* 最大出力 U (counts) */
    int xs; /* 半入力 xs (counts) */
    int p_tenths;  /* 指数 p (小数1桁, 10倍で保持) */
    int32_t inv_xs_q16; /* 1/xs in Q16 to avoid per-event divide */
    bool track_remainders;
};

static inline int32_t scale_axis_apply(int32_t accum, const struct scaler_config *config,
                                       int32_t *remainder_q16) {
    if (accum == 0) {
        return 0;
    }

    const int32_t sign = (accum >= 0) ? 1 : -1;
    const int32_t aabs = (accum >= 0) ? accum : -accum;
    // r = |x|/xs (use precomputed reciprocal in Q16)
    int64_t r_tmp = (int64_t)aabs * (int64_t)config->inv_xs_q16; // Q16
    int32_t r_q16 = (r_tmp > INT32_MAX) ? INT32_MAX : (int32_t)r_tmp;

    // rp1 = r^(p+1) where p is in tenths
    int p10 = config->p_tenths;
    if (p10 < 0) p10 = 0; // non-negative
    int e_int = 1 + (p10 / 10);           // integer part of exponent
    int e_frac_tenths = p10 % 10;         // fractional part (0..9)
    int32_t rp1_q16 = q16_pow_i_sat(r_q16, (unsigned int)e_int);
    if (e_frac_tenths != 0 && r_q16 > 0) {
        if (r_q16 == Q16_ONE) {
            // r == 1 -> r^e_frac == 1, skip powf
            // rp1_q16 unchanged
        } else {
        float r_f = (float)r_q16 / (float)Q16_ONE;
        float e_frac = (float)e_frac_tenths / 10.0f;
        float rp1_frac_f = powf(r_f, e_frac);
        if (!isfinite(rp1_frac_f)) {
            rp1_frac_f = 1.0f;
        }
        int32_t rp1_frac_q16 = (int32_t)lrintf(rp1_frac_f * (float)Q16_ONE);
        // clamp
        if (rp1_frac_q16 < 0) rp1_frac_q16 = 0;
        if (rp1_frac_q16 > Q16_SAT_MAX) rp1_frac_q16 = Q16_SAT_MAX;
        rp1_q16 = q16_mul_sat(rp1_q16, rp1_frac_q16);
        }
    }

    // frac = rp1/(1+rp1) with overflow-safe handling
    if (rp1_q16 >= Q16_SAT_MAX - Q16_ONE) {
        rp1_q16 = Q16_SAT_MAX - Q16_ONE;
    }
    int32_t denom_q16 = rp1_q16 + Q16_ONE; // safe due to headroom
    int32_t frac_q16 = q16_div(rp1_q16, denom_q16);

    // scaled = U * frac * sign
    const int32_t u_q16 = (int32_t)((int64_t)config->u << 16);
    int32_t scaled_q16 = q16_mul_sat(u_q16, frac_q16);
    if (sign < 0) scaled_q16 = -scaled_q16;

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

    // Clamp to [-U, U]
    if (out > config->u) out = config->u;
    if (out < -config->u) out = -config->u;

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
        .u = DT_INST_PROP_OR(n, u, 127), \
        .xs = DT_INST_PROP_OR(n, xs, 50), \
        .p_tenths = DT_INST_PROP_OR(n, p_tenths, 10), \
        .inv_xs_q16 = (int32_t)((((int64_t)(1 << 16)) + (DT_INST_PROP_OR(n, xs, 50) / 2)) / DT_INST_PROP_OR(n, xs, 50)), \
        .track_remainders = DT_INST_PROP(n, track_remainders), \
    }; \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, \
                          &scaler_data_##n, &scaler_config_##n, \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                          &scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCALER_INST)




