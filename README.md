## zmk-feature-scaling

`zmk-feature-scaling` is a ZMK input processor that evaluates a configurable acceleration curve on frame-based pointer input while preserving the resulting direction vector.

### Key mechanics
- **Frame-scoped gain** – All relative events carrying the same `sync=true` flag are accumulated into a vector `v`. Its magnitude is fed to the curve
  `y(|v|) = U · r^(p+1) / (1 + r^(p+1))`, `r = |v| / xs`.
- **Isotropic application** – The gain `k = y(|v|) / |v|` is latched and applied to both axes during the next frame, so `X:Y` ratios are preserved.
- **Q16 residual tracking** – Each axis maintains a fixed-point remainder to keep sub-pixel precision and avoid drift at low velocity.
- **Runtime control** – `scaling-mode = 0` disables the processor at runtime; `1` enables the curve.

The expensive `powf` evaluation happens once per frame (at `sync=true`). All axis updates reuse the cached gain and consist of integer Q16 multiplies, so the runtime impact is small and deterministic.

### Kconfig
- `CONFIG_SCALER`: Enables building this input processor module.

### DTS Binding
`compatible = "zmk,input-processor-motion-scaler";`

Properties:
- `scaling-mode` (int): `0` disables, `1` enables.
- `max-output` (int): Maximum absolute output |y|. Default 127.
- `half-input` (int): Half-input xs used in r=|x|/xs. Default 50.
- `exponent-tenths` (int): Exponent p with 0.1 resolution (use p*10, e.g., 15 for p=1.5). Default 10 (p=1.0).

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

### Curve Tuning Tool
- Pointer Acceleration Curve Studio: https://pointer-acceleration-curve-studio.pages.dev/
- Use it to preview and tune this module’s parameters on a graph.
  - max-output → U
  - half-input → xs
  - exponent-tenths/10 → p
- Select y(x) in the tool to match the module’s formula. This helps find smooth, comfortable curves before applying settings in your DTS overlay.
