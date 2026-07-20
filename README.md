[English](README-en.md) | [Japanese](README.md)

# 録音方法

再生デバイスを1つ選び、その出力音声をWAVファイルへループバック録音します。LinuxではPipeWire/PulseAudio、WindowsではWASAPIを使用します。

1. `./rec.exe out.wav`
2. 再生デバイスの一覧が表示されます。録音したいデバイスの番号を確認してください。
3. 録音が始まります。
4. `Ctrl+C`で終了すると wav ファイルが保存されます。

# 書き起こし方法

英語音声向けのWhisperXを実行します。RTX 4060 Ti向けに、`medium.en`モデル、float16、batch size 4が既定値です。

```sh
./spr out.wav
```

バージョンは`./spr --version`で確認できます。

## 出力フォーマット

`--output_format`を省略すると、次の5形式がすべて出力されます。`--output_dir`を省略した場合の出力先は現在のディレクトリです。ファイル名には入力音声と同じベース名が使われます。たとえば`kar.wav`からは`kar.txt`などが生成されます。

| 形式 | 内容 | 主な用途 |
|---|---|---|
| TXT | 書き起こした文章だけを保存するプレーンテキスト。タイムスタンプは含みません。 | 内容の確認、文章の編集、コピー＆ペースト |
| SRT | 連番、開始・終了時刻、字幕本文を持つ一般的な字幕形式。 | 動画プレーヤー、動画編集ソフトへの字幕追加 |
| VTT | WebVTT規格の字幕形式。SRTに似ていますが、Webでの利用を想定しています。 | HTMLの`<video>`要素、ブラウザ上の字幕表示 |
| TSV | 開始時刻、終了時刻、本文をタブで区切った表形式。時刻の単位はミリ秒です。 | 表計算ソフトでの確認、スクリプトやプログラムでの処理 |
| JSON | 言語、セグメント、単語単位の時刻、認識スコアなどを持つ構造化データ。 | 詳細な解析、検索、別プログラムへの入力 |

1形式だけ出力する場合は、たとえば次のように指定します。

```sh
./spr out.wav --output_format srt
```

出力先を変更する場合、ディレクトリはWhisperXが自動作成します。

```sh
./spr out.wav --output_dir transcript
```

VRAMが足りない場合はモデルまたはバッチサイズを下げられます。

```sh
WHISPERX_MODEL=small.en WHISPERX_BATCH_SIZE=2 ./spr out.wav
```

話者分離を使う場合はHugging Faceで対象モデルの利用条件へ同意し、`--diarize --hf_token TOKEN`を指定してください。通常の書き起こしと単語単位アラインメントにはトークンは不要です。

# コマンドリファレンス

## コマンド

```text
./rec.exe [-h|--help] [-v|--version] <wav>                 # Windows
./rec [-h|--help] [-v|--version] <wav>                     # Linux
./spr [-h|--help] [-v|--version] [<オプション>...] <音声>...
```

ヘルプまたはバージョンのオプションを指定した場合、WAVまたは音声ファイルの指定は不要です。`spr`は複数の音声ファイルを一度に受け取れます。

## オプション

### rec

| オプション・引数 | 説明 | 既定値 |
|---|---|---|
| `<wav>` | 録音先のWAVファイル | 必須 |
| `-v`, `--version` | `rec`のバージョンを表示して終了 | ― |
| `-h`, `--help` | 使用方法を表示して終了 | ― |

### spr

| オプション・引数 | 説明 | 既定値 |
|---|---|---|
| `<音声>...` | 書き起こす音声ファイル。複数指定可能 | 必須 |
| `-v`, `--version` | `spr`のバージョンを表示して終了 | ― |
| `-h`, `--help` | WhisperXを含む全オプションのヘルプを表示 | ― |
| `--model <名前>` | 使用するWhisperモデル | `medium.en` |
| `--device <デバイス>` | 推論デバイス。`cuda`または`cpu` | `cuda` |
| `--compute_type <型>`, `--type <型>` | 計算精度。`float16`、`float32`、`int8`など | `float16` |
| `--batch_size <数>`, `--batcha <数>` | 一度に処理する音声区間数 | `4` |
| `--language <言語コード>`, `--lang <言語コード>` | 音声の言語 | `en` |
| `--output_dir <ディレクトリ>`, `--dir <ディレクトリ>` | 出力先。存在しない場合は自動作成 | 現在のディレクトリ |
| `--output_format <形式>`, `--format <形式>` | `all`、`txt`、`srt`、`vtt`、`tsv`、`json`、`aud` | `all` |
| `--task <処理>` | `transcribe`で書き起こし、`translate`で英訳 | `transcribe` |
| `--no_align`, `--no-align` | 単語単位の時刻アラインメントを無効化 | 無効 |
| `--diarize` | 話者分離を有効化 | 無効 |
| `--min_speakers <数>` | 話者数の下限 | 自動 |
| `--max_speakers <数>` | 話者数の上限 | 自動 |
| `--hf_token <トークン>` | 話者分離モデルに使用するHugging Faceトークン | なし |

`spr`が受け付ける全オプションは`./spr --help`で確認できます。

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
