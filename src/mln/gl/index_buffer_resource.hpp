#pragma once

#include <mln/gfx/index_buffer.hpp>
#include <mln/gl/object.hpp>
#include <mln/util/identity.hpp>

namespace mln {
namespace gl {

class IndexBufferResource : public gfx::IndexBufferResource {
public:
    IndexBufferResource(UniqueBuffer&& buffer_, int byteSize_);
    ~IndexBufferResource() noexcept override;
    std::int64_t getAllocationID() const { return allocationID.id(); }

    UniqueBuffer buffer;
    int byteSize;

private:
    const util::SimpleIdentity allocationID;
};

} // namespace gl
} // namespace mln
