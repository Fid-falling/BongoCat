#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

LARGE_INTEGER timer_frequency{};

long long ticks() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

double milliseconds(long long value) {
    return value * 1000.0 / timer_frequency.QuadPart;
}

struct WindowSearch { DWORD process; HWND window; };

BOOL CALLBACK find_window(HWND window, LPARAM data) {
    auto *search = reinterpret_cast<WindowSearch *>(data);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == search->process && IsWindowVisible(window) &&
        GetWindow(window, GW_OWNER) == nullptr) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND process_window(DWORD process) {
    WindowSearch search{process, nullptr};
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

void move_cursor(int x, int y) {
    if (!SetCursorPos(x, y)) throw std::runtime_error("SetCursorPos failed");
}

void mouse_input(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    if (SendInput(1, &input, sizeof(input)) != 1)
        throw std::runtime_error("mouse SendInput failed");
}

void click(int x, int y) {
    move_cursor(x, y);
    mouse_input(MOUSEEVENTF_LEFTDOWN);
    mouse_input(MOUSEEVENTF_LEFTUP);
}

void key(WORD code) {
    INPUT input[2]{};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[1].ki.wVk = code;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, input, sizeof(INPUT)) != 2)
        throw std::runtime_error("keyboard SendInput failed");
    Sleep(80);
}

void prepare_viewer(HWND window, int expression) {
    ShowWindow(window, SW_RESTORE);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 1280, 720, SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    Sleep(1200);
    if (expression >= 0) {
        click(100, 194); Sleep(150); key(VK_RIGHT); Sleep(150);
        click(120, 222 + expression * 29); Sleep(80);
        click(120, 222 + expression * 29); Sleep(1000);
    }
    click(313, 659); Sleep(300);
    click(426, 659); Sleep(500);
}

struct Frame {
    long long present = 0;
    std::vector<std::uint8_t> pixels;
};

class DesktopCapture {
public:
    explicit DesktopCapture(const RECT &crop) : crop_(crop) {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
            throw std::runtime_error("CreateDXGIFactory1 failed");
        for (UINT a = 0; ; ++a) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(a, adapter.GetAddressOf()) ==
                DXGI_ERROR_NOT_FOUND) break;
            for (UINT o = 0; ; ++o) {
                ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(o, output.GetAddressOf()) ==
                    DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC description{};
                output->GetDesc(&description);
                RECT area = description.DesktopCoordinates;
                if (!description.AttachedToDesktop || crop.left < area.left ||
                    crop.top < area.top || crop.right > area.right ||
                    crop.bottom > area.bottom) continue;
                D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
                D3D_FEATURE_LEVEL level{};
                if (FAILED(D3D11CreateDevice(adapter.Get(),
                    D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                    ARRAYSIZE(levels), D3D11_SDK_VERSION,
                    device_.GetAddressOf(), &level, context_.GetAddressOf())))
                    continue;
                ComPtr<IDXGIOutput1> output1;
                if (FAILED(output.As(&output1)) || FAILED(output1->DuplicateOutput(
                    device_.Get(), duplication_.GetAddressOf()))) continue;
                output_ = area;
                return;
            }
        }
        throw std::runtime_error(
            "No DXGI output contains the Viewer capture area");
    }

    Frame capture_at(long long target) {
        for (;;) {
            DXGI_OUTDUPL_FRAME_INFO info{};
            ComPtr<IDXGIResource> resource;
            HRESULT result = duplication_->AcquireNextFrame(1000, &info,
                resource.GetAddressOf());
            if (result == DXGI_ERROR_WAIT_TIMEOUT) continue;
            if (FAILED(result))
                throw std::runtime_error("AcquireNextFrame failed");
            bool selected = info.LastPresentTime.QuadPart >= target;
            Frame frame;
            if (selected) {
                ComPtr<ID3D11Texture2D> texture;
                if (FAILED(resource.As(&texture))) {
                    duplication_->ReleaseFrame();
                    throw std::runtime_error(
                        "Desktop frame is not a D3D11 texture");
                }
                copy_frame(texture.Get(), frame);
                frame.present = info.LastPresentTime.QuadPart;
            }
            duplication_->ReleaseFrame();
            if (selected) return frame;
        }
    }

private:
    void copy_frame(ID3D11Texture2D *source, Frame &frame) {
        D3D11_TEXTURE2D_DESC source_description{};
        source->GetDesc(&source_description);
        if (source_description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            throw std::runtime_error("Unexpected desktop pixel format");
        if (!staging_) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = crop_.right - crop_.left;
            description.Height = crop_.bottom - crop_.top;
            description.MipLevels = description.ArraySize = 1;
            description.Format = source_description.Format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_STAGING;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(&description, nullptr,
                staging_.GetAddressOf())))
                throw std::runtime_error("Cannot create staging texture");
        }
        D3D11_BOX box{static_cast<UINT>(crop_.left - output_.left),
            static_cast<UINT>(crop_.top - output_.top), 0,
            static_cast<UINT>(crop_.right - output_.left),
            static_cast<UINT>(crop_.bottom - output_.top), 1};
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0,
            source, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0,
            &mapped))) throw std::runtime_error("Cannot map desktop frame");
        int width = crop_.right - crop_.left;
        int height = crop_.bottom - crop_.top;
        frame.pixels.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            std::memcpy(frame.pixels.data() +
                    static_cast<size_t>(y) * width * 4,
                static_cast<const std::uint8_t *>(mapped.pData) +
                    static_cast<size_t>(y) * mapped.RowPitch,
                static_cast<size_t>(width) * 4);
        context_->Unmap(staging_.Get(), 0);
    }

    RECT crop_{}, output_{};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> staging_;
};

#pragma pack(push, 1)
struct BitmapFileHeader {
    std::uint16_t type = 0x4D42;
    std::uint32_t size = 0;
    std::uint16_t reserved1 = 0, reserved2 = 0;
    std::uint32_t offset = 0;
};
#pragma pack(pop)

void save_bitmap(const std::filesystem::path &path, const Frame &frame,
    int width, int height) {
    BitmapFileHeader file;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    info.biSizeImage = static_cast<DWORD>(frame.pixels.size());
    file.offset = sizeof(file) + sizeof(info);
    file.size = file.offset + info.biSizeImage;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(&file), sizeof(file));
    output.write(reinterpret_cast<const char *>(&info), sizeof(info));
    output.write(reinterpret_cast<const char *>(frame.pixels.data()),
        static_cast<std::streamsize>(frame.pixels.size()));
    if (!output) throw std::runtime_error("Cannot save captured frame");
}

struct Sample {
    std::string name;
    Frame frame;
    long long phase;
    double requested;
};

std::string sample_name(const char *phase, int frame) {
    return phase + std::string("-") + (frame < 10 ? "00" : "0") +
        std::to_string(frame);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc < 3) throw std::runtime_error(
            "usage: output-directory viewer-pid [capture arguments]");
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        QueryPerformanceFrequency(&timer_frequency);
        std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        DWORD process = static_cast<DWORD>(_wtoi(argv[2]));
        int left = argc > 3 ? _wtoi(argv[3]) : 247;
        int top = argc > 4 ? _wtoi(argv[4]) : 82;
        int width = argc > 5 ? _wtoi(argv[5]) : 1022;
        int height = argc > 6 ? _wtoi(argv[6]) : 560;
        int press_x = argc > 7 ? _wtoi(argv[7]) : 758;
        int press_y = argc > 8 ? _wtoi(argv[8]) : 362;
        int target_x = argc > 9 ? _wtoi(argv[9]) : 1014;
        int target_y = argc > 10 ? _wtoi(argv[10]) : 194;
        int settle = argc > 11 ? _wtoi(argv[11]) : 150;
        int expression = argc > 12 ? _wtoi(argv[12]) : -1;
        HWND window = process_window(process);
        if (!window) throw std::runtime_error("Viewer window not found");
        prepare_viewer(window, expression);
        move_cursor(press_x, press_y); Sleep(800);
        DesktopCapture capture(RECT{left, top, left + width, top + height});
        std::vector<Sample> samples;
        samples.push_back({"idle", capture.capture_at(ticks()), 0, 0});
        mouse_input(MOUSEEVENTF_LEFTDOWN); Sleep(settle);
        samples.push_back({"track-000", capture.capture_at(ticks()), 0, 0});
        move_cursor(target_x, target_y);
        long long track_start = ticks();
        const int frames[] = {1, 2, 4, 8, 15, 30};
        for (int frame : frames) {
            double requested = frame * (1000.0 / 60.0);
            long long target = track_start + static_cast<long long>(
                requested * timer_frequency.QuadPart / 1000.0);
            samples.push_back({sample_name("track", frame),
                capture.capture_at(target), track_start, requested});
        }
        mouse_input(MOUSEEVENTF_LEFTUP);
        long long return_start = ticks();
        for (int frame : frames) {
            double requested = frame * (1000.0 / 60.0);
            long long target = return_start + static_cast<long long>(
                requested * timer_frequency.QuadPart / 1000.0);
            samples.push_back({sample_name("return", frame),
                capture.capture_at(target), return_start, requested});
        }
        mouse_input(MOUSEEVENTF_LEFTUP);
        std::ofstream trace(directory / "trace.csv");
        trace << "frame,phase_ms,capture_mid_ms,requested_ms,capture_end_ms,"
            "press_x,press_y,target_x,target_y,target_drag_x,target_drag_y,"
            "capture_left,capture_top,capture_width,capture_height\n";
        double drag_x = 2.0 * (target_x - left) / width - 1.0;
        double drag_y = 1.0 - 2.0 * (target_y - top) / height;
        for (const auto &sample : samples) {
            double actual = sample.phase ?
                milliseconds(sample.frame.present - sample.phase) : 0;
            trace << sample.name << ',' << std::fixed << std::setprecision(3)
                << actual << ',' << actual << ',' << sample.requested << ','
                << actual << ',' << press_x << ',' << press_y << ','
                << target_x << ',' << target_y << ',' << std::setprecision(6)
                << drag_x << ',' << drag_y << ',' << left << ',' << top << ','
                << width << ',' << height << '\n';
            save_bitmap(directory / (sample.name + ".bmp"), sample.frame,
                width, height);
        }
        std::cout << "frames=" << samples.size() << " output="
            << directory.string() << '\n';
        return trace ? 0 : 1;
    } catch (const std::exception &error) {
        try { mouse_input(MOUSEEVENTF_LEFTUP); } catch (...) {}
        std::cerr << error.what() << '\n';
        return 1;
    }
}
