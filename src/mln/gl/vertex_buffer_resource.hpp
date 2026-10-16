#pragma once

#include <mln/gfx/vertex_buffer.hpp>
#include <mln/gl/object.hpp>
#include <mln/util/monotonic_timer.hpp>
#include <mln/util/identity.hpp>

namespace mln {
namespace gl {

class VertexBufferResource : public gfx::VertexBufferResource {
public:
    VertexBufferResource(UniqueBuffer&& buffer_, int byteSize_);
    ~VertexBufferResource() noexcept override;

    int getByteSize() const { return byteSize; }

    const UniqueBuffer& getBuffer() const { return buffer; }
    std::int64_t getAllocationID() const { return allocationID.id(); }

    std::chrono::duration<double> getLastUpdated() const { return lastUpdated; }
    void setLastUpdated(std::chrono::duration<double> time) { lastUpdated = time; }

protected:
    UniqueBuffer buffer;
    int byteSize;
    std::chrono::duration<double> lastUpdated = util::MonotonicTimer::now();

private:
    // GLuint names and allocator addresses can both be reused after deletion.
    const util::SimpleIdentity allocationID;
};

} // namespace gl
} // namespace mln
