#include <gui/render_backend.hpp>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx11.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <cstdint>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace nozzle_viewer {

struct preview_tex {
    ID3D11Texture2D *texture{nullptr};
    ID3D11ShaderResourceView *srv{nullptr};
};

class d3d11_render_backend : public render_backend {
public:
    bool init(GLFWwindow *window) override {
        window_ = window;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
        if (FAILED(hr)) {
            return false;
        }

        IDXGIDevice *dxgi_device = nullptr;
        hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_device));
        if (FAILED(hr)) {
            return false;
        }

        IDXGIAdapter *adapter = nullptr;
        hr = dxgi_device->GetAdapter(&adapter);
        dxgi_device->Release();
        if (FAILED(hr)) {
            return false;
        }

        IDXGIFactory *factory = nullptr;
        hr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory));
        adapter->Release();
        if (FAILED(hr)) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC sc_desc{};
        sc_desc.BufferCount = 2;
        sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc_desc.OutputWindow = glfwGetWin32Window(window_);
        sc_desc.SampleDesc.Count = 1;
        sc_desc.Windowed = TRUE;
        sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        hr = factory->CreateSwapChain(device_, &sc_desc, &swap_chain_);
        factory->Release();
        if (FAILED(hr) || !create_render_target()) {
            return false;
        }

        ImGui_ImplGlfw_InitForOther(window_, true);
        ImGui_ImplDX11_Init(device_, context_);
        return true;
    }

    void shutdown() override {
        for (auto *item : preview_textures_) {
            if (item->srv) {
                item->srv->Release();
            }
            if (item->texture) {
                item->texture->Release();
            }
            delete item;
        }
        preview_textures_.clear();

        if (window_) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplGlfw_Shutdown();
        }
        if (rtv_) {
            rtv_->Release();
            rtv_ = nullptr;
        }
        if (swap_chain_) {
            swap_chain_->Release();
            swap_chain_ = nullptr;
        }
        if (context_) {
            context_->Release();
            context_ = nullptr;
        }
        if (device_) {
            device_->Release();
            device_ = nullptr;
        }
    }

    void begin_frame() override {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        if (width != last_width_ || height != last_height_) {
            if (rtv_) {
                rtv_->Release();
                rtv_ = nullptr;
            }
            swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            create_render_target();
            last_width_ = width;
            last_height_ = height;
        }
        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
    }

    void end_frame() override {
        ImGui::Render();
        const float clear_color[4] = {0.08f, 0.08f, 0.08f, 1.0f};
        context_->OMSetRenderTargets(1, &rtv_, nullptr);
        context_->ClearRenderTargetView(rtv_, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_chain_->Present(1, 0);
    }

    void *create_preview_texture(std::uint32_t width, std::uint32_t height) override {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D *texture = nullptr;
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(hr)) {
            return nullptr;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView *srv = nullptr;
        hr = device_->CreateShaderResourceView(texture, &srv_desc, &srv);
        if (FAILED(hr)) {
            texture->Release();
            return nullptr;
        }

        auto *item = new preview_tex{texture, srv};
        preview_textures_.push_back(item);
        return static_cast<void *>(srv);
    }

    void destroy_preview_texture(void *texture) override {
        if (!texture) {
            return;
        }
        auto *srv = static_cast<ID3D11ShaderResourceView *>(texture);
        for (auto it = preview_textures_.begin(); it != preview_textures_.end(); ++it) {
            if ((*it)->srv == srv) {
                (*it)->srv->Release();
                (*it)->texture->Release();
                delete *it;
                preview_textures_.erase(it);
                break;
            }
        }
    }

    bool update_preview_texture(void *texture, const void *pixels, std::uint32_t width, std::uint32_t height,
        std::ptrdiff_t row_stride_bytes, preview_format format) override {
        if (!texture || !pixels || width == 0 || height == 0) {
            return false;
        }
        auto *srv = static_cast<ID3D11ShaderResourceView *>(texture);
        ID3D11Texture2D *target = nullptr;
        for (auto *item : preview_textures_) {
            if (item->srv == srv) {
                target = item->texture;
                break;
            }
        }
        if (!target) {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(target, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            return false;
        }

        const auto *src_base = static_cast<const std::uint8_t *>(pixels);
        auto *dst_base = static_cast<std::uint8_t *>(mapped.pData);
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto *src = src_base + static_cast<std::ptrdiff_t>(y) * row_stride_bytes;
            auto *dst = dst_base + static_cast<std::size_t>(y) * mapped.RowPitch;
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

        context_->Unmap(target, 0);
        return true;
    }

    const char *get_system_font_path() override {
        return "C:\\Windows\\Fonts\\msgothic.ttc";
    }

private:
    bool create_render_target() {
        ID3D11Texture2D *back_buffer = nullptr;
        HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back_buffer));
        if (FAILED(hr)) {
            return false;
        }
        hr = device_->CreateRenderTargetView(back_buffer, nullptr, &rtv_);
        back_buffer->Release();
        return SUCCEEDED(hr);
    }

    GLFWwindow *window_{nullptr};
    ID3D11Device *device_{nullptr};
    ID3D11DeviceContext *context_{nullptr};
    IDXGISwapChain *swap_chain_{nullptr};
    ID3D11RenderTargetView *rtv_{nullptr};
    int last_width_{0};
    int last_height_{0};
    std::vector<preview_tex *> preview_textures_{};
};

std::unique_ptr<render_backend> create_render_backend() {
    return std::make_unique<d3d11_render_backend>();
}

} // namespace nozzle_viewer
