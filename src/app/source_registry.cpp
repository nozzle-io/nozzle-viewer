#include <app/source_registry.hpp>

#include <nozzle/discovery.hpp>

#include <algorithm>

namespace nozzle_viewer {

std::vector<source_entry> to_source_entries(const std::vector<nozzle::sender_info> &senders) {
    std::vector<source_entry> entries;
    entries.reserve(senders.size());
    for (const auto &sender : senders) {
        entries.push_back(source_entry{sender.name, sender.application_name, sender.id, sender.backend});
    }
    std::sort(entries.begin(), entries.end(), [](const source_entry &a, const source_entry &b) {
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.id < b.id;
    });
    entries.erase(std::unique(entries.begin(), entries.end(), [](const source_entry &a, const source_entry &b) {
        return a.id == b.id && a.name == b.name;
    }), entries.end());
    return entries;
}

void source_registry::refresh() {
    set_sources(to_source_entries(nozzle::enumerate_senders()));
}

void source_registry::set_sources(std::vector<source_entry> sources) {
    sources_ = std::move(sources);
    generation_ = generation_ + 1;
}

const std::vector<source_entry> &source_registry::sources() const noexcept {
    return sources_;
}

const source_entry *source_registry::find_by_id_or_name(const std::string &key) const noexcept {
    auto it = std::find_if(sources_.begin(), sources_.end(), [&key](const source_entry &source) {
        return source.id == key || source.name == key;
    });
    return it == sources_.end() ? nullptr : &*it;
}

std::uint64_t source_registry::generation() const noexcept {
    return generation_;
}

const char *backend_name(nozzle::backend_type backend) noexcept {
    switch (backend) {
    case nozzle::backend_type::d3d11: return "D3D11";
    case nozzle::backend_type::metal: return "Metal";
    case nozzle::backend_type::opengl: return "OpenGL";
    case nozzle::backend_type::dma_buf: return "DMA-BUF";
    case nozzle::backend_type::unknown: return "Unknown";
    }
    return "Unknown";
}

const char *format_name(nozzle::texture_format format) noexcept {
    switch (format) {
    case nozzle::texture_format::r8_unorm: return "r8_unorm";
    case nozzle::texture_format::rg8_unorm: return "rg8_unorm";
    case nozzle::texture_format::rgb8_unorm: return "rgb8_unorm";
    case nozzle::texture_format::rgba8_unorm: return "rgba8_unorm";
    case nozzle::texture_format::bgra8_unorm: return "bgra8_unorm";
    case nozzle::texture_format::rgba8_srgb: return "rgba8_srgb";
    case nozzle::texture_format::bgra8_srgb: return "bgra8_srgb";
    case nozzle::texture_format::r16_unorm: return "r16_unorm";
    case nozzle::texture_format::rg16_unorm: return "rg16_unorm";
    case nozzle::texture_format::rgb16_unorm: return "rgb16_unorm";
    case nozzle::texture_format::rgba16_unorm: return "rgba16_unorm";
    case nozzle::texture_format::r16_float: return "r16_float";
    case nozzle::texture_format::rg16_float: return "rg16_float";
    case nozzle::texture_format::rgb16_float: return "rgb16_float";
    case nozzle::texture_format::rgba16_float: return "rgba16_float";
    case nozzle::texture_format::r32_float: return "r32_float";
    case nozzle::texture_format::rg32_float: return "rg32_float";
    case nozzle::texture_format::rgb32_float: return "rgb32_float";
    case nozzle::texture_format::rgba32_float: return "rgba32_float";
    case nozzle::texture_format::r32_uint: return "r32_uint";
    case nozzle::texture_format::rgba32_uint: return "rgba32_uint";
    case nozzle::texture_format::rgb32_uint: return "rgb32_uint";
    case nozzle::texture_format::depth32_float: return "depth32_float";
    case nozzle::texture_format::unknown: return "unknown";
    }
    return "unknown";
}

} // namespace nozzle_viewer
