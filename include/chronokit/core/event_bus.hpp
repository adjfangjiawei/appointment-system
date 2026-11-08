#pragma once

#include "chronokit/core/json.hpp"
#include "chronokit/core/time_utils.hpp"

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace chronokit {

struct DomainEvent {
    std::string type;
    EpochMinute timestamp = 0;
    std::string booking_id;
    std::string resource_id;
    std::string actor_id;
    std::string message;
    JsonValue payload = JsonValue::object();

    JsonValue to_json() const {
        return JsonValue::object({
            {"type", type},
            {"timestamp", time_utils::format_datetime(timestamp)},
            {"booking_id", booking_id},
            {"resource_id", resource_id},
            {"actor_id", actor_id},
            {"message", message},
            {"payload", payload}
        });
    }
};

class EventBus {
public:
    using Listener = std::function<void(const DomainEvent&)>;

    void subscribe(Listener fn) {
        std::lock_guard<std::mutex> lk(mu_);
        listeners_.push_back(std::move(fn));
    }

    void clear_listeners() {
        std::lock_guard<std::mutex> lk(mu_);
        listeners_.clear();
    }

    void publish(DomainEvent ev) {
        std::vector<Listener> copy;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (ev.timestamp == 0) ev.timestamp = time_utils::now_local_minutes();
            history_.push_back(ev);
            if (history_.size() > max_history_) history_.pop_front();
            copy = listeners_;
        }
        for (const auto& fn : copy) {
            fn(ev);
        }
    }

    std::vector<DomainEvent> recent_events(size_t limit = 50) const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<DomainEvent> out;
        size_t start = history_.size() > limit ? history_.size() - limit : 0;
        for (size_t i = history_.size(); i > start; --i) {
            out.push_back(history_[i - 1]); // newest first
        }
        return out;
    }

    void clear_history() {
        std::lock_guard<std::mutex> lk(mu_);
        history_.clear();
    }

private:
    mutable std::mutex mu_;
    std::vector<Listener> listeners_;
    std::deque<DomainEvent> history_;
    size_t max_history_ = 250;
};

} // namespace chronokit
