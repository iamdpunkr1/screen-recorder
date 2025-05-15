#include <napi.h>
#include <vector>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

namespace screen_recorder {

struct ScreenDimensions {
    int width;
    int height;
};

class Recorder {
public:
    Recorder() : frames_count_(0) {}

    std::vector<uint8_t> CaptureFrame() {
        ScreenDimensions dimensions = GetScreenDimensions();
        return CaptureScreenFrame(dimensions);
    }

    int GetFramesCount() const {
        return frames_count_;
    }

    void IncrementFrameCount() {
        frames_count_++;
    }

    ScreenDimensions GetScreenDimensions() {
        ScreenDimensions dimensions;
        
#ifdef _WIN32
        dimensions.width = GetSystemMetrics(SM_CXSCREEN);
        dimensions.height = GetSystemMetrics(SM_CYSCREEN);
#elif defined(__APPLE__)
        CGRect mainMonitor = CGDisplayBounds(CGMainDisplayID());
        dimensions.width = CGRectGetWidth(mainMonitor);
        dimensions.height = CGRectGetHeight(mainMonitor);
#else
        Display* display = XOpenDisplay(NULL);
        Screen* screen = DefaultScreenOfDisplay(display);
        dimensions.width = screen->width;
        dimensions.height = screen->height;
        XCloseDisplay(display);
#endif
        
        return dimensions;
    }

private:
    std::vector<uint8_t> CaptureScreenFrame(const ScreenDimensions& dimensions) {
        std::vector<uint8_t> frame_data;
        
#ifdef _WIN32
        HDC hScreenDC = GetDC(NULL);
        HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, dimensions.width, dimensions.height);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
        BitBlt(hMemoryDC, 0, 0, dimensions.width, dimensions.height, hScreenDC, 0, 0, SRCCOPY);
        
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = dimensions.width;
        bi.biHeight = -dimensions.height;
        bi.biPlanes = 1;
        bi.biBitCount = 24;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = 0;
        bi.biClrImportant = 0;
        
        DWORD dwBmpSize = ((dimensions.width * bi.biBitCount + 31) / 32) * 4 * dimensions.height;
        frame_data.resize(dwBmpSize);
        
        GetDIBits(hMemoryDC, hBitmap, 0, dimensions.height, frame_data.data(), 
                 (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
#elif defined(__APPLE__)
        CGImageRef image = CGDisplayCreateImage(CGMainDisplayID());
        CFDataRef dataRef = CGDataProviderCopyData(CGImageGetDataProvider(image));
        size_t length = CFDataGetLength(dataRef);
        frame_data.resize(length);
        memcpy(frame_data.data(), CFDataGetBytePtr(dataRef), length);
        CFRelease(dataRef);
        CGImageRelease(image);
#else
        Display* display = XOpenDisplay(NULL);
        Window root = DefaultRootWindow(display);
        XWindowAttributes attributes;
        XGetWindowAttributes(display, root, &attributes);
        XImage* ximage = XGetImage(display, root, 0, 0, dimensions.width, dimensions.height, AllPlanes, ZPixmap);
        int bytesPerPixel = ximage->bits_per_pixel / 8;
        frame_data.resize(dimensions.width * dimensions.height * 3);
        for (int y = 0; y < dimensions.height; y++) {
            for (int x = 0; x < dimensions.width; x++) {
                unsigned long pixel = XGetPixel(ximage, x, y);
                int index = (y * dimensions.width + x) * 3;
                frame_data[index] = (pixel & ximage->red_mask) >> 16;
                frame_data[index+1] = (pixel & ximage->green_mask) >> 8;
                frame_data[index+2] = pixel & ximage->blue_mask;
            }
        }
        XDestroyImage(ximage);
        XCloseDisplay(display);
#endif
        
        return frame_data;
    }

    std::atomic<int> frames_count_;
};

// Global recorder instance
Recorder g_recorder;

// NAPI functions
Napi::Value GetNextFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    auto frame_data = g_recorder.CaptureFrame();
    g_recorder.IncrementFrameCount();
    
    // Create a new buffer with the frame data
    Napi::ArrayBuffer buffer = Napi::ArrayBuffer::New(env, frame_data.size());
    memcpy(buffer.Data(), frame_data.data(), frame_data.size());
    
    // Create a typed array view of the buffer
    Napi::Uint8Array uint8_array = Napi::Uint8Array::New(env, frame_data.size(), buffer, 0);
    return uint8_array;
}

Napi::Value GetFramesCount(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    int frames_count = g_recorder.GetFramesCount();
    return Napi::Number::New(env, frames_count);
}

Napi::Value GetScreenDimensions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    auto dimensions = g_recorder.GetScreenDimensions();
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("width", Napi::Number::New(env, dimensions.width));
    result.Set("height", Napi::Number::New(env, dimensions.height));
    
    return result;
}

Napi::Object Initialize(Napi::Env env, Napi::Object exports) {
    exports.Set("getNextFrame", Napi::Function::New(env, GetNextFrame));
    exports.Set("getFramesCount", Napi::Function::New(env, GetFramesCount));
    exports.Set("getScreenDimensions", Napi::Function::New(env, GetScreenDimensions));
    return exports;
}

NODE_API_MODULE(screen_recorder, Initialize)

}  // namespace screen_recorder