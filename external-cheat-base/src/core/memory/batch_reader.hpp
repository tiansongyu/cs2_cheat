#pragma once

// Three-step batch reader:
//   BatchReader br;
//   auto h = br.queue<vec3>(addr);          // reserve a slot, get a handle
//   ...                                      // queue more
//   br.flush();                              // one IOCTL / one RPM loop
//   vec3 v = br.get<vec3>(h);                // retrieve
//
// Handle is opaque (size_t index). BatchReader owns the storage.
// Reuse one instance per frame to avoid heap thrash.

#include "memory.hpp"

#include <vector>
#include <cstring>
#include <cstdint>
#include <type_traits>

class BatchReader
{
public:
    using Handle = size_t;

    void reset()
    {
        entries_.clear();
        storage_.clear();
        offsets_.clear();
        sizes_.clear();
    }

    template <typename T>
    Handle queue(uintptr_t address)
    {
        static_assert(std::is_trivially_copyable_v<T>, "BatchReader requires POD types");
        const size_t off = storage_.size();
        storage_.resize(off + sizeof(T));
        offsets_.push_back(off);
        sizes_.push_back(sizeof(T));

        memory::BatchEntry e{};
        e.address = static_cast<uint64_t>(address);
        e.size = static_cast<uint32_t>(sizeof(T));
        e.output_offset = 0;
        e.out = nullptr;  // bound at flush()
        entries_.push_back(e);
        return offsets_.size() - 1;
    }

    bool flush()
    {
        if (entries_.empty()) return true;

        // Bind out pointers now that storage_ is stable (no further queue()).
        for (size_t i = 0; i < entries_.size(); ++i) {
            entries_[i].out = storage_.data() + offsets_[i];
        }
        return memory::ReadBatch(entries_.data(), entries_.size());
    }

    template <typename T>
    T get(Handle h) const
    {
        T v{};
        if (h < offsets_.size() && sizes_[h] == sizeof(T)) {
            std::memcpy(&v, storage_.data() + offsets_[h], sizeof(T));
        }
        return v;
    }

private:
    std::vector<memory::BatchEntry> entries_;
    std::vector<uint8_t> storage_;
    std::vector<size_t> offsets_;
    std::vector<size_t> sizes_;
};
