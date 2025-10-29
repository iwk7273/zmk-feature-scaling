## zmk-feature-scaling（日本語）

ZMK の入力パイプラインに組み込むことで、相対マウス入力に対する加速カーブをフレーム単位で評価し、結果の方向ベクトルを保ったまま適用する input_processor です。

### 動作の要点
- **フレーム単位でのゲイン計算** – `sync=true` が付与された REL イベント群を 1 フレームとして集約し、ベクトル `v` の大きさを  
  `y(|v|) = U · r^(p+1) / (1 + r^(p+1))`（`r = |v| / xs`）に入力してゲインを求めます。
- **等方スケーリング** – 得られた `k = y(|v|) / |v|` を次フレームで X/Y 両軸に同一係数として適用し、方向比を維持します。
- **Q16 余り管理** – 各軸は固定小数(Q16)の余りを保持し、サブピクセル精度を維持したまま次のイベントに転送します。
- **実行時切り替え** – `scaling-mode = 0` でバイパス、`1` でカーブ適用。設定変更だけで有効/無効を切り替えられます。

コストの大きい `powf` はフレーム終端(`sync=true`)時に 1 回だけ呼び、軸方向の計算はキャッシュしたゲインによる Q16 乗算のみで完結するため、実行時の負荷は低く抑えられています。

### Kconfig
- `CONFIG_SCALER`: 本モジュールのビルドを有効化。

### DTS バインディング
`compatible = "zmk,input-processor-motion-scaler";`

プロパティ:
- `scaling-mode` (int): `0` 無効、`1` 有効。
- `max-output` (int): 出力の最大絶対値 |y|。既定 127。
- `half-input` (int): r=|x|/xs に使う xs。既定 50。
- `exponent-tenths` (int): p を 0.1 刻みで表す整数（例: 15 → p=1.5）。既定 10。

#### DTS オーバーレイ例
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

### ビルド
`CONFIG_SCALER=y` のときに `src/motion_scaling.c` がコンパイルされます。

`CMakeLists.txt`:
```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SCALER src/motion_scaling.c)
```

`west.yml`（例）:
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

### ライセンス
MIT License

### カーブ調整ツール
- Pointer Acceleration Curve Studio: https://pointer-acceleration-curve-studio.pages.dev/
- `max-output` / `half-input` / `exponent-tenths/10` を指定し、グラフでカーブを事前確認できます。
