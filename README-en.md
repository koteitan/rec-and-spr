[English](README-en.md) | [Japanese](README.md)

# Recording

Select one render device and record its output audio to a WAV file using loopback capture. The program uses PipeWire/PulseAudio on Linux and WASAPI on Windows.

1. Run `./rec.exe out.wav`.
2. A list of render devices appears. Enter the number of the device you want to record.
3. Recording starts.
4. Press `Ctrl+C` to stop and save the WAV file.

# Transcription

`spr` runs WhisperX for English audio. Its defaults are tuned for an RTX 4060 Ti: the `medium.en` model, float16 computation, and a batch size of 4.

```sh
./spr out.wav
```

Check the version with `./spr --version`.

## Output formats

When `--output_format` is omitted, all five formats below are generated. When `--output_dir` is omitted, files are written to the current directory. Each output uses the input audio's base name; for example, `kar.wav` produces files such as `kar.txt`.

| Format | Contents | Typical uses |
|---|---|---|
| TXT | Plain transcript text without timestamps. | Reading, editing, and copy-and-paste |
| SRT | A common subtitle format containing numbered cues, start and end times, and subtitle text. | Video players and video editing software |
| VTT | Subtitles in the WebVTT standard, similar to SRT but designed for the Web. | HTML `<video>` elements and browser subtitles |
| TSV | Tab-separated start time, end time, and text. Times are expressed in milliseconds. | Spreadsheets, scripts, and programmatic processing |
| JSON | Structured data containing the language, segments, word-level timestamps, and recognition scores. | Detailed analysis, search, and input to other programs |

To generate only one format, specify it explicitly. For example:

```sh
./spr out.wav --output_format srt
```

To change the output location, pass a directory. WhisperX creates it automatically.

```sh
./spr out.wav --output_dir transcript
```

If GPU memory is insufficient, use a smaller model or batch size.

```sh
WHISPERX_MODEL=small.en WHISPERX_BATCH_SIZE=2 ./spr out.wav
```

Speaker diarization requires accepting the relevant model's terms on Hugging Face and passing `--diarize --hf_token TOKEN`. Normal transcription and word-level alignment do not require a token.

# Build and installation

## rec

### Windows

Build with the MinGW-w64 cross-compiler.

```sh
make windows
```

This produces `rec.exe`.

### Linux

Build in an environment running PipeWire (`pipewire-pulse`) or PulseAudio.

```sh
make
```

This produces `rec`. If devices cannot be listed, use `pactl info` to check the PipeWire/PulseAudio connection.

## spr

Use Python 3.10 and `uv` to install WhisperX 3.8.5 in a project-specific virtual environment.

```sh
uv venv .venv-whisperx --python 3.10
uv pip install --python .venv-whisperx/bin/python -r requirements-whisperx.txt
chmod +x spr
```

On first use, the speech recognition model and English alignment model are downloaded into `.models` and `.cache`. Subsequent runs use the local cache.

To run without a GPU, select CPU mode.

```sh
WHISPERX_DEVICE=cpu WHISPERX_COMPUTE_TYPE=int8 ./spr out.wav
```
