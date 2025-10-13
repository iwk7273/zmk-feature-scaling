## zmk-feature-scaling

ZMK input_processor module that scales relative mouse input. When `scaling_mode` is 1, it multiplies the movement based on the per-poll movement magnitude \(\Delta = \sqrt{x^2 + y^2}\) and a configurable coefficient `coeff`.

- Behavior: `x := x * (coeff * Δ)`, `y := y * (coeff * Δ)`
- Enable/Disable: Active when `scaling_mode = 1`, pass-through when `scaling_mode = 0`
- Coefficient: Device Tree `scale-coeff-milli` (milli-units); falls back to Kconfig default if omitted

### Kconfig
- `SCALER`: Menu to enable the scaling module
- `ZMK_INPUT_PROCESSOR_SCALER`: Enable the scaling input processor
- `ZMK_INPUT_PROCESSOR_SCALER_DEFAULT_COEFF_MILLI`: Default coefficient in milli (e.g., 100 => 0.1)

### DTS Binding
compatible: `"zmk,input-processor-motion-scaler"`

Properties:
- `scaling-mode` (int, required): 0 disables, 1 enables
- `scale-coeff-milli` (int, optional): coefficient in milli; defaults to Kconfig value

Example:
```dts
scaler0: scaler@0 {
    compatible = "zmk,input-processor-motion-scaler";
    scaling-mode = <1>;            // enable
    scale-coeff-milli = <100>;     // 0.1
};
```

### Build
`src/scaling.c` is compiled only when `CONFIG_ZMK_INPUT_PROCESSOR_SCALER=y`.

`CMakeLists.txt`:
```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_SCALER src/scaling.c)
```

`west.yml` (example):
```yaml
manifest:
  remotes:
    - name: iwk7273
      url-base: https://github.com/iwk7273
  projects:
    - name: zmk-feature-scaling
      remote: iwk7273
      import: west.yml
```

### License
MIT License
