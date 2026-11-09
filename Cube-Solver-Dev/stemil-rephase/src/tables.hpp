#pragma once
#include <cstdint>
#include <filesystem>

namespace rephase {
// Strict, streaming, read-only loader for the existing stemil ASCII byte tables.
// This never creates, converts, regenerates, or writes resource files.
uint64_t read_stemil_ascii(const std::filesystem::path& path,uint8_t* output,
                          size_t entries,bool phase1,bool progress=false);
class Tables {
public:
    explicit Tables(std::filesystem::path resources,bool progress=false);
    Tables(const Tables&) = delete;
    Tables& operator=(const Tables&) = delete;
    int phase1_lower_bound(int co,int eo,int slice) const;
    int phase1_mod3(int co,int eo,int slice) const;
    int phase2_lower_bound(int cp,int ep,int slice) const;
    size_t allocated_bytes() const;
    size_t phase1_bytes() const;
    size_t phase2_bytes() const;
    const std::filesystem::path& resources() const { return resources_; }
private:
    std::filesystem::path resources_;
};
}
