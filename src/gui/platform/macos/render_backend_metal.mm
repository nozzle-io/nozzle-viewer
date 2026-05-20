#include <gui/render_backend.hpp>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace nozzle_viewer {

class metal_render_backend : public render_backend {
public:
    bool init(GLFWwindow *window) override {
        window_ = window;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            return false;
        }
        device_ = (void *)CFBridgingRetain(device);
        command_queue_ = (void *)CFBridgingRetain([device newCommandQueue]);

        NSWindow *ns_window = glfwGetCocoaWindow(window_);
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.contentsScale = ns_window.screen.backingScaleFactor;

        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        layer.drawableSize = CGSizeMake(fb_w, fb_h);
        ns_window.contentView.layer = layer;
        ns_window.contentView.wantsLayer = YES;

        ImGui_ImplGlfw_InitForOther(window_, true);
        ImGui_ImplMetal_Init(device);
        return true;
    }

    void shutdown() override {
        if (window_) {
            ImGui_ImplMetal_Shutdown();
            ImGui_ImplGlfw_Shutdown();
        }
        if (command_queue_) {
            CFRelease(command_queue_);
            command_queue_ = nullptr;
        }
        if (device_) {
            CFRelease(device_);
            device_ = nullptr;
        }
    }

    void begin_frame() override {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);

        NSWindow *ns_window = glfwGetCocoaWindow(window_);
        CAMetalLayer *layer = (CAMetalLayer *)ns_window.contentView.layer;
        CGFloat scale = ns_window.screen.backingScaleFactor;
        if (layer.contentsScale != scale) {
            layer.contentsScale = scale;
        }
        CGSize fb_size = CGSizeMake(width, height);
        if (!CGSizeEqualToSize(layer.drawableSize, fb_size)) {
            layer.drawableSize = fb_size;
        }

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        drawable_ = (void *)CFBridgingRetain(drawable);

        MTLRenderPassDescriptor *render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
        render_pass.colorAttachments[0].texture = drawable.texture;
        render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        render_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.08, 0.08, 1.0);
        render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        render_pass_ = (void *)CFBridgingRetain(render_pass);

        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplMetal_NewFrame(render_pass);
        ImGui::NewFrame();
    }

    void end_frame() override {
        ImGui::Render();
        id<MTLCommandQueue> command_queue = (__bridge id<MTLCommandQueue>)command_queue_;
        MTLRenderPassDescriptor *render_pass = (__bridge MTLRenderPassDescriptor *)render_pass_;
        id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)drawable_;

        id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
        id<MTLRenderCommandEncoder> render_encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass];
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), command_buffer, render_encoder);
        [render_encoder endEncoding];
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];

        CFRelease(render_pass_);
        render_pass_ = nullptr;
        CFRelease(drawable_);
        drawable_ = nullptr;
    }

    void *create_preview_texture(std::uint32_t width, std::uint32_t height) override {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:width height:height mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModeShared;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        return (void *)CFBridgingRetain(texture);
    }

    void destroy_preview_texture(void *texture) override {
        if (texture) {
            CFRelease(texture);
        }
    }

    bool update_preview_texture(void *texture, const void *pixels, std::uint32_t width, std::uint32_t height,
        std::ptrdiff_t row_stride_bytes, preview_format format) override {
        if (!texture || !pixels || width == 0 || height == 0) {
            return false;
        }
        id<MTLTexture> mtl_texture = (__bridge id<MTLTexture>)texture;
        const void *upload = pixels;
        std::size_t upload_stride = static_cast<std::size_t>(row_stride_bytes);
        if (format == preview_format::bgra8 || row_stride_bytes != static_cast<std::ptrdiff_t>(width * 4)) {
            scratch_.resize(static_cast<std::size_t>(width) * height * 4);
            const auto *src_base = static_cast<const std::uint8_t *>(pixels);
            for (std::uint32_t y = 0; y < height; ++y) {
                const auto *src = src_base + static_cast<std::ptrdiff_t>(y) * row_stride_bytes;
                auto *dst = scratch_.data() + static_cast<std::size_t>(y) * width * 4;
                if (format == preview_format::bgra8) {
                    for (std::uint32_t x = 0; x < width; ++x) {
                        dst[x * 4 + 0] = src[x * 4 + 2];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = src[x * 4 + 0];
                        dst[x * 4 + 3] = src[x * 4 + 3];
                    }
                } else {
                    std::memcpy(dst, src, static_cast<std::size_t>(width) * 4);
                }
            }
            upload = scratch_.data();
            upload_stride = static_cast<std::size_t>(width) * 4;
        }
        [mtl_texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
            mipmapLevel:0
            withBytes:upload
            bytesPerRow:upload_stride];
        return true;
    }

    const char *get_system_font_path() override {
        return "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc";
    }

private:
    GLFWwindow *window_{nullptr};
    void *device_{nullptr};
    void *command_queue_{nullptr};
    void *render_pass_{nullptr};
    void *drawable_{nullptr};
    std::vector<std::uint8_t> scratch_{};
};

std::unique_ptr<render_backend> create_render_backend() {
    return std::make_unique<metal_render_backend>();
}

} // namespace nozzle_viewer
