// screen_recorder.cc - Expose frames as buffers for local display
#include <node.h>
#include <node_buffer.h>
#include <uv.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
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

using v8::Context;
using v8::Exception;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;
using v8::Boolean;
using v8::ArrayBuffer;
using v8::Uint8Array;

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

void GetNextFrame(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    
    auto frame_data = g_recorder.CaptureFrame();
    g_recorder.IncrementFrameCount();
    
    Local<ArrayBuffer> buffer = ArrayBuffer::New(isolate, frame_data.size());
    memcpy(buffer->GetBackingStore()->Data(), frame_data.data(), frame_data.size());
    
    Local<Uint8Array> uint8_array = Uint8Array::New(buffer, 0, frame_data.size());
    args.GetReturnValue().Set(uint8_array);
}

void GetFramesCount(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    int frames_count = g_recorder.GetFramesCount();
    args.GetReturnValue().Set(Number::New(isolate, frames_count));
}

void GetScreenDimensions(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    
    auto dimensions = g_recorder.GetScreenDimensions();
    
    Local<Object> result = Object::New(isolate);
    result->Set(context, String::NewFromUtf8(isolate, "width").ToLocalChecked(), 
               Number::New(isolate, dimensions.width)).Check();
    result->Set(context, String::NewFromUtf8(isolate, "height").ToLocalChecked(), 
               Number::New(isolate, dimensions.height)).Check();
    
    args.GetReturnValue().Set(result);
}

void Initialize(Local<Object> exports) {
    NODE_SET_METHOD(exports, "getNextFrame", GetNextFrame);
    NODE_SET_METHOD(exports, "getFramesCount", GetFramesCount);
    NODE_SET_METHOD(exports, "getScreenDimensions", GetScreenDimensions);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Initialize)

}  // namespace screen_recorder