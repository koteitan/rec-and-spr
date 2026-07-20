#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#else
#include <csignal>
#endif

namespace {

std::atomic_bool stop_requested{false};

[[maybe_unused]] void append_u16(std::vector<std::uint8_t>& bytes,
                                 std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

[[maybe_unused]] void append_u32(std::vector<std::uint8_t>& bytes,
                                 std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

class WavWriter {
public:
    bool open(const std::string& path, const std::vector<std::uint8_t>& wave_format) {
        file_.open(path, std::ios::binary | std::ios::trunc);
        if (!file_) {
            return false;
        }

        file_.write("RIFF", 4);
        riff_size_offset_ = file_.tellp();
        write_u32(0);
        file_.write("WAVEfmt ", 8);
        write_u32(static_cast<std::uint32_t>(wave_format.size()));
        file_.write(reinterpret_cast<const char*>(wave_format.data()),
                    static_cast<std::streamsize>(wave_format.size()));
        if (wave_format.size() % 2 != 0) {
            file_.put('\0');
        }
        file_.write("data", 4);
        data_size_offset_ = file_.tellp();
        write_u32(0);
        const auto data_start = file_.tellp();
        if (data_start < 8) {
            return false;
        }
        max_data_size_ = std::numeric_limits<std::uint32_t>::max() -
                         (static_cast<std::uint64_t>(data_start) - 8);
        return static_cast<bool>(file_);
    }

    bool write(const void* data, std::uint32_t size) {
        if (size > max_data_size_ - data_size_) {
            limit_reached_ = true;
            return false;
        }
        file_.write(static_cast<const char*>(data), size);
        if (!file_) {
            return false;
        }
        data_size_ += size;
        return true;
    }

    bool write_silence(std::uint32_t size) {
        static constexpr std::uint8_t zeros[4096]{};
        while (size != 0) {
            const auto chunk = std::min<std::uint32_t>(size, sizeof(zeros));
            if (!write(zeros, chunk)) {
                return false;
            }
            size -= chunk;
        }
        return true;
    }

    bool finish() {
        if (!file_.is_open()) {
            return false;
        }

        const auto end = file_.tellp();
        if (end < 0 || static_cast<std::uint64_t>(end) - 8 >
                           std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        file_.seekp(riff_size_offset_);
        write_u32(static_cast<std::uint32_t>(end) - 8);
        file_.seekp(data_size_offset_);
        write_u32(data_size_);
        file_.seekp(end);
        file_.close();
        return !file_.fail();
    }

    std::uint32_t data_size() const { return data_size_; }
    bool limit_reached() const { return limit_reached_; }

private:
    void write_u32(std::uint32_t value) {
        const char bytes[] = {
            static_cast<char>(value),
            static_cast<char>(value >> 8),
            static_cast<char>(value >> 16),
            static_cast<char>(value >> 24),
        };
        file_.write(bytes, sizeof(bytes));
    }

    std::ofstream file_;
    std::streampos riff_size_offset_{};
    std::streampos data_size_offset_{};
    std::uint32_t data_size_ = 0;
    std::uint32_t max_data_size_ = 0;
    bool limit_reached_ = false;
};

bool choose_index(std::size_t count, std::size_t& selected) {
    std::cout << "> " << std::flush;
    std::string input;
    if (!std::getline(std::cin, input)) {
        std::cerr << "No device was selected.\n";
        return false;
    }

    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(input, &consumed);
        while (consumed < input.size() &&
               (input[consumed] == ' ' || input[consumed] == '\t')) {
            ++consumed;
        }
        if (consumed != input.size() || value == 0 || value > count) {
            throw std::out_of_range("device number");
        }
        selected = static_cast<std::size_t>(value - 1);
        return true;
    } catch (const std::exception&) {
        std::cerr << "Invalid device number.\n";
        return false;
    }
}

#ifdef _WIN32

template <typename T>
class ComPtr {
public:
    ~ComPtr() {
        if (ptr_) {
            ptr_->Release();
        }
    }
    T** put() { return &ptr_; }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->Release();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr() = default;

private:
    T* ptr_ = nullptr;
};

struct DeviceInfo {
    ComPtr<IMMDevice> device;
    std::string name;
};

// DEVPKEY_Device_FriendlyName. Defining it locally also supports older MinGW
// import libraries that do not export the PKEY_Device_FriendlyName variable.
const PROPERTYKEY device_friendly_name_key = {
    {0xa45c254e,
     0xdf1c,
     0x4efd,
     {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14};

std::string utf8(const wchar_t* text) {
    if (!text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr,
                        nullptr);
    result.pop_back();
    return result;
}

std::string hresult_message(HRESULT result) {
    wchar_t* message = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, result, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::string text;
    if (size != 0 && message) {
        text = utf8(message);
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
            text.pop_back();
        }
    }
    if (message) {
        LocalFree(message);
    }
    if (text.empty()) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "HRESULT 0x%08lx",
                      static_cast<unsigned long>(result));
        text = buffer;
    }
    return text;
}

bool list_devices(std::vector<DeviceInfo>& devices, std::string& error) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(hr)) {
        error = hresult_message(hr);
        return false;
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                        collection.put());
    if (FAILED(hr)) {
        error = hresult_message(hr);
        return false;
    }

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr)) {
        error = hresult_message(hr);
        return false;
    }

    for (UINT i = 0; i < count; ++i) {
        DeviceInfo info;
        hr = collection->Item(i, info.device.put());
        if (FAILED(hr)) {
            continue;
        }

        ComPtr<IPropertyStore> properties;
        hr = info.device->OpenPropertyStore(STGM_READ, properties.put());
        if (FAILED(hr)) {
            continue;
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        hr = properties->GetValue(device_friendly_name_key, &value);
        if (SUCCEEDED(hr) && value.vt == VT_LPWSTR) {
            info.name = utf8(value.pwszVal);
        }
        PropVariantClear(&value);
        if (info.name.empty()) {
            info.name = "Unnamed render device";
        }
        devices.push_back(std::move(info));
    }
    return true;
}

BOOL WINAPI console_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT) {
        stop_requested.store(true);
        return TRUE;
    }
    return FALSE;
}

int record_device(DeviceInfo& device, const std::string& output_path) {
    ComPtr<IAudioClient> audio_client;
    HRESULT hr = device.device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(audio_client.put()));
    if (FAILED(hr)) {
        std::cerr << "Could not open the device: " << hresult_message(hr) << '\n';
        return 1;
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr = audio_client->GetMixFormat(&mix_format);
    if (FAILED(hr)) {
        std::cerr << "Could not get the device format: " << hresult_message(hr)
                  << '\n';
        return 1;
    }

    const std::uint32_t format_size =
        (mix_format->wFormatTag == WAVE_FORMAT_PCM ||
         mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            ? 16U
            : 18U + mix_format->cbSize;
    std::vector<std::uint8_t> format_bytes(
        reinterpret_cast<std::uint8_t*>(mix_format),
        reinterpret_cast<std::uint8_t*>(mix_format) + format_size);

    REFERENCE_TIME device_period = 0;
    hr = audio_client->GetDevicePeriod(&device_period, nullptr);
    if (FAILED(hr)) {
        CoTaskMemFree(mix_format);
        std::cerr << "Could not get the device period: " << hresult_message(hr)
                  << '\n';
        return 1;
    }

    hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0,
                                  mix_format, nullptr);
    const auto block_align = mix_format->nBlockAlign;
    CoTaskMemFree(mix_format);
    if (FAILED(hr)) {
        std::cerr << "Could not start loopback mode: " << hresult_message(hr)
                  << '\n';
        return 1;
    }

    ComPtr<IAudioCaptureClient> capture;
    hr = audio_client->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(capture.put()));
    if (FAILED(hr)) {
        std::cerr << "Could not create a capture client: "
                  << hresult_message(hr) << '\n';
        return 1;
    }

    WavWriter wav;
    if (!wav.open(output_path, format_bytes)) {
        std::cerr << "Could not open output file: " << output_path << '\n';
        return 1;
    }

    SetConsoleCtrlHandler(console_handler, TRUE);
    hr = audio_client->Start();
    if (FAILED(hr)) {
        wav.finish();
        std::cerr << "Could not start recording: " << hresult_message(hr) << '\n';
        return 1;
    }

    std::cout << "Recording " << device.name << " to " << output_path
              << ". Press Ctrl+C to stop.\n";

    bool recording_ok = true;
    const DWORD sleep_ms =
        std::max<DWORD>(1, static_cast<DWORD>(device_period / 20000));
    while (!stop_requested.load()) {
        UINT32 packet_frames = 0;
        hr = capture->GetNextPacketSize(&packet_frames);
        if (FAILED(hr)) {
            recording_ok = false;
            break;
        }
        if (packet_frames == 0) {
            Sleep(sleep_ms);
            continue;
        }

        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
            recording_ok = false;
            break;
        }

        const auto bytes = frames * static_cast<std::uint32_t>(block_align);
        const bool wrote = (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                               ? wav.write_silence(bytes)
                               : wav.write(data, bytes);
        capture->ReleaseBuffer(frames);
        if (!wrote) {
            recording_ok = false;
            break;
        }
    }

    audio_client->Stop();
    SetConsoleCtrlHandler(console_handler, FALSE);
    if (!wav.finish()) {
        std::cerr << "Could not finalize the WAV file.\n";
        return 1;
    }
    if (!recording_ok) {
        if (wav.limit_reached()) {
            std::cerr << "The 4 GiB WAV size limit was reached.\n";
        } else {
            std::cerr << "Recording failed: " << hresult_message(hr) << '\n';
        }
        return 1;
    }

    std::cout << "Saved " << wav.data_size() << " bytes of audio.\n";
    return 0;
}

#else

// Minimal declarations from PulseAudio's stable public ABI. Keeping them here
// lets the program build on machines that have the runtime libraries (normally
// installed with PipeWire/PulseAudio) without requiring development headers.
extern "C" {
struct pa_mainloop;
struct pa_mainloop_api;
struct pa_context;
struct pa_operation;
struct pa_simple;

enum pa_context_state_t {
    PA_CONTEXT_UNCONNECTED,
    PA_CONTEXT_CONNECTING,
    PA_CONTEXT_AUTHORIZING,
    PA_CONTEXT_SETTING_NAME,
    PA_CONTEXT_READY,
    PA_CONTEXT_FAILED,
    PA_CONTEXT_TERMINATED
};

enum pa_operation_state_t {
    PA_OPERATION_RUNNING,
    PA_OPERATION_DONE,
    PA_OPERATION_CANCELLED
};

enum pa_sample_format_t {
    PA_SAMPLE_U8,
    PA_SAMPLE_ALAW,
    PA_SAMPLE_ULAW,
    PA_SAMPLE_S16LE
};

struct pa_sample_spec {
    pa_sample_format_t format;
    std::uint32_t rate;
    std::uint8_t channels;
};

enum pa_channel_position_t : int;
struct pa_channel_map {
    std::uint8_t channels;
    pa_channel_position_t map[32];
};
using pa_volume_t = std::uint32_t;
struct pa_cvolume {
    std::uint8_t channels;
    pa_volume_t values[32];
};

struct pa_sink_info {
    const char* name;
    std::uint32_t index;
    const char* description;
    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    std::uint32_t owner_module;
    pa_cvolume volume;
    int mute;
    std::uint32_t monitor_source;
    const char* monitor_source_name;
};

pa_mainloop* pa_mainloop_new();
void pa_mainloop_free(pa_mainloop*);
pa_mainloop_api* pa_mainloop_get_api(pa_mainloop*);
int pa_mainloop_iterate(pa_mainloop*, int block, int* retval);
pa_context* pa_context_new(pa_mainloop_api*, const char* name);
void pa_context_unref(pa_context*);
int pa_context_connect(pa_context*, const char* server, int flags,
                       const void* api);
void pa_context_disconnect(pa_context*);
pa_context_state_t pa_context_get_state(const pa_context*);
int pa_context_errno(const pa_context*);
const char* pa_strerror(int error);
using pa_sink_info_cb_t = void (*)(pa_context*, const pa_sink_info*, int,
                                   void*);
pa_operation* pa_context_get_sink_info_list(pa_context*, pa_sink_info_cb_t,
                                            void* userdata);
pa_operation_state_t pa_operation_get_state(const pa_operation*);
void pa_operation_unref(pa_operation*);
pa_simple* pa_simple_new(const char* server, const char* name, int direction,
                         const char* device, const char* stream_name,
                         const pa_sample_spec*, const pa_channel_map*,
                         const void* buffer_attributes, int* error);
void pa_simple_free(pa_simple*);
int pa_simple_read(pa_simple*, void* data, std::size_t bytes, int* error);
}

struct DeviceInfo {
    std::string name;
    std::string monitor_source;
};

void sink_callback(pa_context*, const pa_sink_info* info, int end_of_list,
                   void* userdata) {
    if (end_of_list != 0 || !info || !info->monitor_source_name) {
        return;
    }
    auto& devices = *static_cast<std::vector<DeviceInfo>*>(userdata);
    devices.push_back({info->description ? info->description : info->name,
                       info->monitor_source_name});
}

bool list_devices(std::vector<DeviceInfo>& devices, std::string& error) {
    pa_mainloop* mainloop = pa_mainloop_new();
    if (!mainloop) {
        error = "could not create the PulseAudio main loop";
        return false;
    }
    pa_context* context = pa_context_new(pa_mainloop_get_api(mainloop),
                                         "loopbackrec");
    if (!context) {
        pa_mainloop_free(mainloop);
        error = "could not create a PulseAudio context";
        return false;
    }

    if (pa_context_connect(context, nullptr, 0, nullptr) < 0) {
        error = pa_strerror(pa_context_errno(context));
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return false;
    }

    bool ready = false;
    while (!ready) {
        if (pa_mainloop_iterate(mainloop, 1, nullptr) < 0) {
            error = "PulseAudio main loop failed";
            break;
        }
        const auto state = pa_context_get_state(context);
        if (state == PA_CONTEXT_READY) {
            ready = true;
        } else if (state == PA_CONTEXT_FAILED ||
                   state == PA_CONTEXT_TERMINATED) {
            error = pa_strerror(pa_context_errno(context));
            break;
        }
    }

    bool listed = false;
    if (ready) {
        pa_operation* operation =
            pa_context_get_sink_info_list(context, sink_callback, &devices);
        if (!operation) {
            error = pa_strerror(pa_context_errno(context));
        } else {
            while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING) {
                if (pa_mainloop_iterate(mainloop, 1, nullptr) < 0) {
                    error = "PulseAudio main loop failed";
                    break;
                }
            }
            listed = pa_operation_get_state(operation) == PA_OPERATION_DONE;
            pa_operation_unref(operation);
        }
    }

    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(mainloop);
    return listed;
}

void signal_handler(int) {
    stop_requested.store(true);
}

int record_device(const DeviceInfo& device, const std::string& output_path) {
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits_per_sample = 16;
    const pa_sample_spec sample_spec{PA_SAMPLE_S16LE, sample_rate, channels};

    int pulse_error = 0;
    pa_simple* recorder = pa_simple_new(
        nullptr, "loopbackrec", 2, device.monitor_source.c_str(),
        "Loopback recording", &sample_spec, nullptr, nullptr, &pulse_error);
    if (!recorder) {
        std::cerr << "Could not open the monitor source: "
                  << pa_strerror(pulse_error) << '\n';
        return 1;
    }

    std::vector<std::uint8_t> format;
    append_u16(format, 1);  // PCM
    append_u16(format, channels);
    append_u32(format, sample_rate);
    append_u32(format, sample_rate * channels * bits_per_sample / 8);
    append_u16(format, channels * bits_per_sample / 8);
    append_u16(format, bits_per_sample);

    WavWriter wav;
    if (!wav.open(output_path, format)) {
        pa_simple_free(recorder);
        std::cerr << "Could not open output file: " << output_path << '\n';
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::cout << "Recording " << device.name << " to " << output_path
              << ". Press Ctrl+C to stop.\n";

    std::vector<std::uint8_t> buffer(4800 * channels * bits_per_sample / 8);
    bool recording_ok = true;
    while (!stop_requested.load()) {
        if (pa_simple_read(recorder, buffer.data(), buffer.size(),
                           &pulse_error) < 0) {
            if (!stop_requested.load()) {
                recording_ok = false;
            }
            break;
        }
        if (!wav.write(buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
            recording_ok = false;
            break;
        }
    }

    pa_simple_free(recorder);
    if (!wav.finish()) {
        std::cerr << "Could not finalize the WAV file.\n";
        return 1;
    }
    if (!recording_ok) {
        if (wav.limit_reached()) {
            std::cerr << "The 4 GiB WAV size limit was reached.\n";
        } else {
            std::cerr << "Recording failed: " << pa_strerror(pulse_error) << '\n';
        }
        return 1;
    }

    std::cout << "Saved " << wav.data_size() << " bytes of audio.\n";
    return 0;
}

#endif

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " out.wav\n";
        return 2;
    }

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) {
        std::cerr << "Could not initialize COM: "
                  << hresult_message(com_result) << '\n';
        return 1;
    }
#endif

    std::vector<DeviceInfo> devices;
    std::string error;
    if (!list_devices(devices, error)) {
        std::cerr << "Could not enumerate render devices: " << error << '\n';
#ifdef _WIN32
        CoUninitialize();
#endif
        return 1;
    }
    if (devices.empty()) {
        std::cerr << "No active render devices were found.\n";
#ifdef _WIN32
        CoUninitialize();
#endif
        return 1;
    }

    std::cout << "Choose a render device:\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        std::cout << i + 1 << ". " << devices[i].name << '\n';
    }

    std::size_t selected = 0;
    if (!choose_index(devices.size(), selected)) {
#ifdef _WIN32
        CoUninitialize();
#endif
        return 2;
    }

    const int result = record_device(devices[selected], argv[1]);
#ifdef _WIN32
    CoUninitialize();
#endif
    return result;
}
