# 録音方法

再生デバイスを1つ選び、その出力音声をWAVファイルへループバック録音します。LinuxではPipeWire/PulseAudio、WindowsではWASAPIを使用します。

Linux:

```sh
./rec out.wav
```

Windows:

```sh
./rec.exe out.wav
```

バージョンは`./rec --version`（Windowsでは`./rec.exe --version`）で確認できます。

表示された再生デバイスの番号を入力すると録音が始まります。`Ctrl+C`で終了するとWAVヘッダーが確定されます。

- 録音中のWAVは未確定のため、必ず`Ctrl+C`で正常終了してください。
- 通常のRIFF/WAV形式を使うため、1ファイルの上限は4 GiB未満です。
- Linux版の出力形式は48 kHz、16 bit、ステレオPCMです。
- Windows版は選択したデバイスの共有モード・ミックス形式をそのまま保存します。

# 書き起こし方法

英語音声向けのWhisperXを実行します。RTX 4060 Ti向けに、`medium.en`モデル、float16、batch size 4が既定値です。

```sh
mkdir -p transcript
./spr out.wav --output_dir transcript --output_format all
```

バージョンは`./spr --version`で確認できます。

結果は`transcript`ディレクトリへTXT、SRT、VTT、TSV、JSON形式で出力されます。

VRAMが足りない場合はモデルまたはバッチサイズを下げられます。

```sh
WHISPERX_MODEL=small.en WHISPERX_BATCH_SIZE=2 ./spr out.wav
```

話者分離を使う場合はHugging Faceで対象モデルの利用条件へ同意し、`--diarize --hf_token TOKEN`を指定してください。通常の書き起こしと単語単位アラインメントにはトークンは不要です。

# ビルド・インストール

## rec

### Windows

MinGW-w64クロスコンパイラを使ってビルドします。

```sh
make windows
```

`rec.exe`が生成されます。

### Linux

PipeWire（`pipewire-pulse`）またはPulseAudioが動いている環境でビルドします。

```sh
make
```

`rec`が生成されます。デバイス一覧を取得できない場合は、`pactl info`でPipeWire/PulseAudioへの接続を確認してください。

## spr

Python 3.10と`uv`を使い、プロジェクト専用の仮想環境へWhisperX 3.8.5をインストールします。

```sh
uv venv .venv-whisperx --python 3.10
uv pip install --python .venv-whisperx/bin/python -r requirements-whisperx.txt
chmod +x spr
```

初回実行時に音声認識モデルと英語アラインメントモデルを`.models`および`.cache`へダウンロードします。以降はローカルキャッシュを使用します。

GPUを使わない場合はCPUモードを指定できます。

```sh
WHISPERX_DEVICE=cpu WHISPERX_COMPUTE_TYPE=int8 ./spr out.wav
```
