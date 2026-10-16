#pragma once

#include <mln/gfx/attribute.hpp>
#include <mln/gl/index_buffer_resource.hpp>
#include <mln/gl/vertex_buffer_resource.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

namespace mln {
namespace gl {

// CPU values only: a retired attribute resource must never be dereferenced through this cache.
// Capture AFTER upload has resolved shared buffers, including any growth/replacement allocations.
class VertexArrayBindingCache {
public:
    using Bindings = std::vector<std::optional<gfx::AttributeBinding>>;

    bool matchesIndex(const IndexBufferResource& index) const { return indexAllocationID == index.getAllocationID(); }

    bool matches(const IndexBufferResource& index, const Bindings& bindings) const {
        if (!matchesIndex(index) || attributes.size() != bindings.size()) return false;
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            const auto& binding = bindings[i];
            if (bool(attributes[i]) != bool(binding)) return false;
            if (binding && (!binding->vertexBufferResource || !(*attributes[i] == capture(*binding)))) return false;
        }
        return true;
    }

    void remember(const IndexBufferResource& index, const Bindings& bindings) {
        attributes.resize(bindings.size());
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            if (bindings[i]) attributes[i] = capture(*bindings[i]);
            else attributes[i].reset();
        }
        indexAllocationID = index.getAllocationID();
    }

private:
    struct Attribute {
        gfx::AttributeDescriptor format;
        std::uint32_t stride;
        std::int64_t allocationID;
        std::uint32_t vertexOffset;
        std::uint32_t bufferIndex;

        bool operator==(const Attribute& other) const {
            return format == other.format && stride == other.stride && allocationID == other.allocationID &&
                   vertexOffset == other.vertexOffset && bufferIndex == other.bufferIndex;
        }
    };

    static Attribute capture(const gfx::AttributeBinding& binding) {
        assert(binding.vertexBufferResource);
        const auto& resource = static_cast<const VertexBufferResource&>(*binding.vertexBufferResource);
        return {binding.attribute, binding.vertexStride, resource.getAllocationID(), binding.vertexOffset,
                binding.bufferIndex};
    }

    std::int64_t indexAllocationID = 0;
    std::vector<std::optional<Attribute>> attributes;
};

} // namespace gl
} // namespace mln
