## zmk-feature-scaling（日本語）

ZMK の相対マウス入力（ポインタ移動）に対して、軸ごとに非線形スケーリング（加速）を適用する input_processor モジュールです。

本モジュールは、毎イベント・各軸で次の式をそのまま用いて出力を計算します。

- y(x) = U · r^(p+1) / (1 + r^(p+1)) · sign(x)
- r = |x| / xs

用語:
- U（`max-output`）: 出力の最大絶対値（counts）。最終出力は `[-U, U]` にクランプされます。
- xs（`half-input`）: r = |x|/xs のスケール基準（counts）。
- p（`exponent-tenths/10`）: 曲線の鋭さを決める指数。0.1 刻みで指定します（例: 1.5 → 15）。

- 有効/無効: `scaling-mode = 1` のとき有効、`0` のときパススルー。
- 余りの繰越: 出力の小数部は Q16 で保持し、次イベントへ繰り越して滑らかさを向上させます（`track-remainders`）。

### パフォーマンス
- 計算は float の `powf(r, p+1)` を用いて式に忠実に実行します。
- 出力の整数化時のみ Q16 で余りを扱います。FPU を搭載した MCU（例: nRF52 系）で実運用可能なコストです。

### Kconfig
- `CONFIG_SCALER`: 本モジュールのビルドを有効化します。

### DTS バインディング
`compatible = "zmk,input-processor-motion-scaler";`

プロパティ:
- `scaling-mode` (int): `0` 無効、`1` 有効。
- `max-output` (int): 出力の最大絶対値 |y|。既定 127。
- `half-input` (int): r=|x|/xs の xs（counts）。既定 50。
- `exponent-tenths` (int): 指数 p を 0.1 分解能で指定（p*10、例: 15 は p=1.5）。既定 10（p=1.0）。
- `track-remainders` (boolean): 余り（Q16）の繰越を有効化。

### DTS オーバーレイ例
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

### ビルド
`CONFIG_SCALER=y` のときに `src/motion_scaling.c` がコンパイルされます。

`CMakeLists.txt`:
```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SCALER src/motion_scaling.c)
```

### 曲線チューニング用ツール
- Pointer Acceleration Curve Studio: https://pointer-acceleration-curve-studio.pages.dev/
- 本モジュールのパラメータをグラフで確認する際の参考になります。
  - マッピング: `max-output → U`, `half-input → xs`, `exponent-tenths/10 → p`
  - ツール内の表示を y(x) にすると本モジュールの式と一致します。

### ライセンス
MIT License

