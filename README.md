## zmk-feature-scaling

A ZMK input_processor module that provides dynamic, per-axis scaling for relative mouse inputs (pointer acceleration).

The output is computed strictly from the following formula on every event (for each axis independently):

- y(x) = U · r^(p+1) / (1 + r^(p+1)) · sign(x)
- r = |x| / xs

Where:
- U is the maximum absolute output (counts)
- xs is the half-input that defines the scale for r
- p is the exponent controlling the curve shape (with 0.1 resolution)

- Enable/Disable: Active when `scaling-mode = 1`, pass-through when `scaling-mode = 0`.
- Remainder Tracking: Optionally track fractional parts in Q16 and carry over to subsequent events for smoother low-speed control.

### Performance
- The scaling is computed using float with `powf(r, p+1)` per event. This matches the formula closely without fixed-point approximations.
- Only the final output uses Q16 for remainder accumulation. Outputs are clamped to `[-max-output, max-output]`.

### Kconfig
- `CONFIG_SCALER`: Enables building this input processor module.

### DTS Binding
`compatible = "zmk,input-processor-motion-scaler";`

Properties:
- `scaling-mode` (int): `0` disables, `1` enables.
- `max-output` (int): Maximum absolute output |y|. Default 127.
- `half-input` (int): Half-input xs used in r=|x|/xs. Default 50.
- `exponent-tenths` (int): Exponent p with 0.1 resolution (use p*10, e.g., 15 for p=1.5). Default 10 (p=1.0).
- `track-remainders` (boolean): If present, enables carrying-over of fractional Q16 remainders.

Example overlay:
```dts
&pointing_device {
    processors = <&motion_scaler>;
};

motion_scaler: motion_scaler {
    compatible = "zmk,input-processor-motion-scaler";
    #input-processor-cells = <0>;
    scaling-mode = <1>;
    max-output = <300>;
    half-input = <50>;
    exponent-tenths = <20>; // p = 2.0
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
