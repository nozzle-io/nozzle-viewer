#pragma once

#include <nozzle/types.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace nozzle_viewer {

struct source_entry {
    std::string name{};
    std::string application_name{};
    std::string id{};
    nozzle::backend_type backend{nozzle::backend_type::unknown};
};

class source_registry {
public:
    void refresh();
    void set_sources(std::vector<source_entry> sources);

    const std::vector<source_entry> &sources() const noexcept;
    const source_entry *find_by_id_or_name(const std::string &key) const noexcept;
    std::uint64_t generation() const noexcept;

private:
    std::vector<source_entry> sources_{};
    std::uint64_t generation_{0};
};

std::vector<source_entry> to_source_entries(const std::vector<nozzle::sender_info> &senders);
const char *backend_name(nozzle::backend_type backend) noexcept;
const char *format_name(nozzle::texture_format format) noexcept;

} // namespace nozzle_viewer
