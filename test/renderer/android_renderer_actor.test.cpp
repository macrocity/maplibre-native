#include <mln/test/util.hpp>
#include <mln/actor/scheduler.hpp>
#include "../../platform/android/MapLibreAndroid/src/cpp/renderer_actor_mailbox.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <optional>
#include <thread>

using namespace mln;
using namespace mln::android::detail;
using namespace std::chrono_literals;

namespace {
class ManualScheduler final : public Scheduler {
public:
    ~ManualScheduler() override { weakFactory.invalidateWeakPtrs(); }
    void schedule(std::function<void()>&& fn) override { pending.push_back(std::move(fn)); }
    void schedule(const util::SimpleIdentity, std::function<void()>&& fn) override { schedule(std::move(fn)); }
    mapbox::base::WeakPtr<Scheduler> makeWeakPtr() override { return weakFactory.makeWeakPtr(); }
    void waitForEmpty(const util::SimpleIdentity) override { drain(); }
    void drain() {
        while (!pending.empty()) {
            auto fn = std::move(pending.front());
            pending.pop_front();
            fn();
        }
    }
private:
    std::deque<std::function<void()>> pending;
    mapbox::base::WeakPtrFactory<Scheduler> weakFactory{this};
};

struct QueryTarget {
    QueryTarget(int& count_, int value_) : count(count_), value(value_) {}
    int query() { ++count; return value; }
    int closeDuringQuery(std::function<void()> close) { close(); return query(); }
    int throwingQuery() { throw std::runtime_error("real query failure"); }
    void action() { ++count; }
    void throwingAction() { throw std::runtime_error("real action failure"); }
    int& count;
    int value;
};
}

TEST(AndroidRendererActor, QueuedQueryCancelledBeforeDestructionAndSameAddressRecreation) {
    ManualScheduler scheduler;
    RendererActorMailbox mailbox;
    int calls = 0;
    std::optional<QueryTarget> renderer;
    renderer.emplace(calls, 41);
    const auto* oldAddress = &*renderer;
    mailbox.open(scheduler);
    const auto stale = mailbox.ref(*renderer);
    auto pending = stale.ask(&QueryTarget::query);
    mailbox.close(); // Pending promise must resolve before Renderer allocation disappears.
    ASSERT_EQ(std::future_status::ready, pending.wait_for(0ms));
    EXPECT_EQ(0, withLiveRenderer([&] { return pending.get(); }));
    renderer.reset();
    renderer.emplace(calls, 99);
    ASSERT_EQ(oldAddress, &*renderer); // Deterministic raw-address reuse cannot revive stale actor.
    mailbox.open(scheduler);
    auto fresh = mailbox.ref(*renderer).ask(&QueryTarget::query);
    scheduler.drain();
    EXPECT_EQ(99, fresh.get());
    EXPECT_EQ(1, calls);
    auto rejected = stale.ask(&QueryTarget::query);
    ASSERT_EQ(std::future_status::ready, rejected.wait_for(0ms));
    EXPECT_THROW(rejected.get(), ActorUnavailable);
    EXPECT_EQ(1, calls);
}

TEST(AndroidRendererActor, ClosedMailboxCancelsAsksEvenWhenAnotherOwnerRetainsIt) {
    ManualScheduler scheduler;
    int calls = 0;
    QueryTarget renderer(calls, 7);
    auto retained = std::make_shared<Mailbox>(scheduler);
    ActorRef<QueryTarget> ref(renderer, retained);
    auto pending = ref.ask(&QueryTarget::query);
    retained->close();
    ASSERT_EQ(std::future_status::ready, pending.wait_for(0ms));
    EXPECT_EQ(0, withLiveRenderer([&] { return pending.get(); }));
    auto late = ref.ask(&QueryTarget::query);
    ASSERT_EQ(std::future_status::ready, late.wait_for(0ms));
    EXPECT_EQ(0, withLiveRenderer([&] { return late.get(); }));
    ref.invoke(&QueryTarget::query);
    scheduler.drain();
    EXPECT_EQ(0, calls);
}

TEST(AndroidRendererActor, SameThreadCloseResolvesQueuedAskWithoutDeadlock) {
    ManualScheduler scheduler;
    RendererActorMailbox mailbox;
    int calls = 0;
    QueryTarget renderer(calls, 17);
    mailbox.open(scheduler);
    const auto ref = mailbox.ref(renderer);
    auto running = ref.ask(&QueryTarget::closeDuringQuery, [&] { mailbox.close(); });
    auto cancelled = ref.ask(&QueryTarget::query);
    scheduler.drain();
    ASSERT_EQ(std::future_status::ready, running.wait_for(0ms));
    ASSERT_EQ(std::future_status::ready, cancelled.wait_for(0ms));
    EXPECT_EQ(17, running.get());
    EXPECT_EQ(0, withLiveRenderer([&] { return cancelled.get(); }));
    EXPECT_EQ(1, calls);
}

TEST(AndroidRendererActor, CloseWaitsForAnAlreadyRunningQueryBeforeObjectDestruction) {
    struct SlowQuery {
        std::promise<void>& entered;
        std::shared_future<void> release;
        int query() { entered.set_value(); release.wait(); return 23; }
    };
    auto scheduler = Scheduler::GetBackground();
    std::promise<void> entered;
    auto ready = entered.get_future();
    std::promise<void> release;
    SlowQuery renderer{entered, release.get_future().share()};
    RendererActorMailbox mailbox;
    mailbox.open(*scheduler);
    auto query = mailbox.ref(renderer).ask(&SlowQuery::query);
    const auto started = ready.wait_for(5s);
    if (started != std::future_status::ready) {
        release.set_value();
        mailbox.close();
    }
    ASSERT_EQ(std::future_status::ready, started);
    auto closing = std::async(std::launch::async, [&] { mailbox.close(); });
    EXPECT_EQ(std::future_status::timeout, closing.wait_for(10ms));
    release.set_value();
    ASSERT_EQ(std::future_status::ready, closing.wait_for(5s));
    closing.get();
    ASSERT_EQ(std::future_status::ready, query.wait_for(0ms));
    EXPECT_EQ(23, query.get());
}

TEST(AndroidRendererActor, OnlyLifecycleCancellationBecomesNeutralResult) {
    ManualScheduler scheduler;
    RendererActorMailbox mailbox;
    int calls = 0;
    QueryTarget renderer(calls, 3);
    EXPECT_EQ(0, withLiveRenderer([&] { return mailbox.ref(renderer).ask(&QueryTarget::query).get(); }));
    EXPECT_NO_THROW(withLiveRenderer([] { throw ActorUnavailable(); }));
    EXPECT_THROW(withLiveRenderer([]() -> int { throw std::runtime_error("real query failure"); }), std::runtime_error);
    EXPECT_THROW(withLiveRenderer([]() -> int { throw std::future_error(std::future_errc::no_state); }), std::future_error);
    mailbox.open(scheduler);
    auto live = mailbox.ref(renderer).ask(&QueryTarget::query);
    scheduler.drain();
    EXPECT_EQ(3, withLiveRenderer([&] { return live.get(); }));
    EXPECT_EQ(1, calls);
    auto realFailure = mailbox.ref(renderer).ask(&QueryTarget::throwingQuery);
    EXPECT_THROW(scheduler.drain(), std::runtime_error);
    ASSERT_EQ(std::future_status::ready, realFailure.wait_for(0ms));
    // Existing AskMessage propagates the original exception to its scheduler and
    // broken_promise to the caller. Neither may become an empty successful query.
    EXPECT_THROW(withLiveRenderer([&] { return realFailure.get(); }), std::future_error);
}

TEST(AndroidRendererActor, UnstartedVoidAsksCancelButStartedFailuresPropagate) {
    ManualScheduler scheduler;
    int calls = 0;
    QueryTarget renderer(calls, 3);
    RendererActorMailbox mailbox;
    mailbox.open(scheduler);
    auto cancelled = mailbox.ref(renderer).ask(&QueryTarget::action);
    mailbox.close();
    ASSERT_EQ(std::future_status::ready, cancelled.wait_for(0ms));
    EXPECT_NO_THROW(withLiveRenderer([&] { cancelled.get(); }));
    EXPECT_EQ(0, calls);
    mailbox.open(scheduler);
    auto failed = mailbox.ref(renderer).ask(&QueryTarget::throwingAction);
    EXPECT_THROW(scheduler.drain(), std::runtime_error);
    ASSERT_EQ(std::future_status::ready, failed.wait_for(0ms));
    EXPECT_THROW(withLiveRenderer([&] { failed.get(); }), std::future_error);
}

TEST(AndroidRendererActor, TileCacheIntentSurvivesPreinitAndSuccessiveGenerations) {
    struct RendererCacheTarget {
        bool enabled = true;
        int updates = 0;
        void setTileCacheEnabled(bool value) { enabled = value; ++updates; }
    };
    RendererTileCachePolicy policy;
    EXPECT_TRUE(policy.get());
    policy.set(false); // Original startup crash: setter precedes first Renderer.
    EXPECT_FALSE(policy.get());
    RendererCacheTarget initial;
    policy.apply(initial);
    EXPECT_FALSE(initial.enabled);
    policy.set(true);
    policy.set(false); // Last writer wins even if earlier queued applies are delayed.
    policy.apply(initial);
    EXPECT_FALSE(initial.enabled);
    RendererCacheTarget recreated;
    policy.apply(recreated);
    EXPECT_FALSE(recreated.enabled);
    policy.set(true);
    policy.apply(recreated);
    EXPECT_TRUE(recreated.enabled);
    RendererCacheTarget next;
    policy.apply(next);
    EXPECT_TRUE(next.enabled);
    EXPECT_EQ(2, initial.updates);
    EXPECT_EQ(2, recreated.updates);
    EXPECT_EQ(1, next.updates);
}
