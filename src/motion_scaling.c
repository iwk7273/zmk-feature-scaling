#define DT_DRV_COMPAT zmk_input_processor_motion_scaler

#include "drivers/input_processor.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <stdint.h>
#include <stdbool.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct scaler_data {
    int32_t x_accum;
    int32_t y_accum;
    int32_t remainder_x_q16;
    int32_t remainder_y_q16;
};

struct scaler_config {
    int scaling_mode; /* 0 or 1 */
    int scale_coeff_milli; /* e.g. 100 => 0.1 */
    bool track_remainders;
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

        // coeff をQ16形式で計算: (scale_coeff_milli / 1000.0) * 65536
        const int32_t coeff_q16 = ((int64_t)config->scale_coeff_milli << 16) / 1000;

        if (event->code == INPUT_REL_X) {
            if (data->x_accum == 0) {
                return ZMK_INPUT_PROC_CONTINUE; // 提案1を適用
            }
            // x_accum をQ16形式に変換
            const int32_t fx_q16 = data->x_accum << 16;
            // delta_x をQ16形式で計算 (fabsfの代替)
            const int32_t delta_x_q16 = (fx_q16 > 0) ? fx_q16 : -fx_q16;
            
            // factorとscaled_xをQ16形式で計算
            const int32_t factor_q16 = (int32_t)(((int64_t)coeff_q16 * delta_x_q16) >> 16);
            int32_t scaled_x_q16 = (int32_t)(((int64_t)fx_q16 * factor_q16) >> 16);

            int32_t out_x;
            if (config->track_remainders) {
                scaled_x_q16 += data->remainder_x_q16;
                out_x = scaled_x_q16 >> 16; // 切り捨て
                data->remainder_x_q16 = scaled_x_q16 - (out_x << 16);
            } else {
                // 四捨五入 (lrintfの代替): (val + 0.5) >> 16
                out_x = (scaled_x_q16 + (1 << 15)) >> 16;
            }
            event->value = out_x;
            data->x_accum = 0;
        } else if (event->code == INPUT_REL_Y) {
            // Y軸も同様に処理
            if (data->y_accum == 0) {
                return ZMK_INPUT_PROC_CONTINUE; // 提案1を適用
            }
            const int32_t fy_q16 = data->y_accum << 16;
            const int32_t delta_y_q16 = (fy_q16 > 0) ? fy_q16 : -fy_q16;

            const int32_t factor_q16 = (int32_t)(((int64_t)coeff_q16 * delta_y_q16) >> 16);
            int32_t scaled_y_q16 = (int32_t)(((int64_t)fy_q16 * factor_q16) >> 16);

            int32_t out_y;
            if (config->track_remainders) {
                scaled_y_q16 += data->remainder_y_q16;
                out_y = scaled_y_q16 >> 16;
                data->remainder_y_q16 = scaled_y_q16 - (out_y << 16);
            } else {
                out_y = (scaled_y_q16 + (1 << 15)) >> 16;
            }
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
        .remainder_x_q16 = 0, \
        .remainder_y_q16 = 0, \
    }; \
    static const struct scaler_config scaler_config_##n = { \
        .scaling_mode = DT_INST_PROP(n, scaling_mode), \
        .scale_coeff_milli = DT_INST_PROP_OR(n, scale_coeff_milli, CONFIG_ZMK_INPUT_PROCESSOR_SCALER_DEFAULT_COEFF_MILLI), \
        .track_remainders = DT_INST_PROP(n, track_remainders), \
    }; \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, \
                          &scaler_data_##n, &scaler_config_##n, \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                          &scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCALER_INST)


