#pragma once

#include <mln/actor/actor_ref.hpp>

#include <cassert>
#include <future>
#include <type_traits>
#include <utility>

namespace mln::android::detail {

// Each Renderer allocation gets its own mailbox. The supervisor supplies locking;
// close this generation before destroying the Renderer, then open a fresh one.
class RendererActorMailbox final {
public:
    RendererActorMailbox() = default;
    RendererActorMailbox(const RendererActorMailbox&) = delete;
    RendererActorMailbox& operator=(const RendererActorMailbox&) = delete;
    ~RendererActorMailbox() { close(); }

    void open(Scheduler& scheduler) {
        assert(!mailbox);
        mailbox = std::make_shared<Mailbox>(scheduler);
    }

    void close() {
        auto retired = std::move(mailbox);
        if (retired) retired->close();
    }

    template <typename Object>
    ActorRef<Object> ref(Object& object) const {
        if (!mailbox) throw ActorUnavailable();
        return {object, mailbox};
    }

private:
    std::shared_ptr<Mailbox> mailbox;
};

// This map-level option outlives individual Renderer allocations. The supervisor
// serializes set/get/apply; a fresh Renderer receives the latest desired value.
class RendererTileCachePolicy final {
public:
    bool get() const { return enabled; }
    void set(bool value) { enabled = value; }
    template <typename Renderer>
    void apply(Renderer& renderer) const { renderer.setTileCacheEnabled(enabled); }
private:
    bool enabled = true; // Exact RenderOrchestrator default.
};

// A context recreation can retire an actor before its query runs. Only those
// lifecycle cancellations produce an empty result; real query failures propagate.
template <typename Fn>
auto withLiveRenderer(Fn&& function) -> std::invoke_result_t<Fn> {
    using Result = std::invoke_result_t<Fn>;
    try {
        return std::forward<Fn>(function)();
    } catch (const ActorUnavailable&) {
    }
    if constexpr (std::is_void_v<Result>) {
        return;
    } else {
        return Result{};
    }
}

} // namespace mln::android::detail
