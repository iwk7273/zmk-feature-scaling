## zmk-feature-scaling

A ZMK input_processor module that provides dynamic scaling for relative mouse inputs, often used for pointer acceleration.

The scaling factor is determined independently for each axis based on the magnitude of movement on that axis. This allows for stable and predictable acceleration without suffering from event synchronization issues.

- **Behavior**: `x_out = x_in * (coeff * |x_in|)`, `y_out = y_in * (coeff * |y_in|)`
- **Enable/Disable**: Active when `scaling_mode = 1`, pass-through when `scaling_mode = 0`.
- **Coefficient**: Set via the Device Tree property `scale-coeff-milli`.
- **Remainder Tracking**: Optionally, fractional parts from scaling can be carried over to subsequent movements for smoother low-speed control.

### Performance
All calculations are performed using fixed-point arithmetic, avoiding floating-point operations. This ensures high performance even on microcontrollers without a dedicated FPU.

### Kconfig
- `CONFIG_SCALER`: Enables the scaling input processor module, which compiles the source code.
- `CONFIG_ZMK_INPUT_PROCESSOR_SCALER_DEFAULT_COEFF_MILLI`: Default coefficient in milli-units (e.g., 100 => 0.1).

### DTS Binding
`compatible = "zmk,input-processor-motion-scaler";`

**Properties:**
- `scaling-mode` (int, required): `0` disables the processor, `1` enables it.
- `scale-coeff-milli` (int, optional): The acceleration coefficient in milli-units. Defaults to the Kconfig value.
- `track-remainders` (boolean, optional): If present, enables the carrying-over of fractional remainders from scaling calculations. This improves precision and smoothness, especially for small, slow movements.

**Example:**
```dts
&pointing_device {
    processors = <&scaler>;
};

scaler: scaler {
    compatible = "zmk,input-processor-motion-scaler";
    scaling-mode = <1>;
    scale-coeff-milli = <100>;
    track-remainders;
};
```

### Build
`src/motion_scaling.c` is compiled only when `CONFIG_SCALER=y`.

`CMakeLists.txt`:
```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SCALER src/motion_scaling.c)
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
      revision: main # or specific commit
      import: west.yml
```

### License
MIT License
