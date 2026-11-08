#pragma once

#include "chronokit/core/types.hpp"
#include "chronokit/storage/storage.hpp"

#include <memory>
#include <string>
#include <vector>

namespace chronokit {

struct BrickParamSpec {
    std::string name;
    std::string type; // "int", "double", "bool", "string", "array", "object"
    JsonValue default_value;
    std::string description;

    JsonValue to_json() const {
        return JsonValue::object({
            {"name", name},
            {"type", type},
            {"default_value", default_value},
            {"description", description}
        });
    }
};

struct BrickDescriptor {
    std::string type_id;
    std::string category;
    std::string summary;
    int order = 50;
    std::vector<BrickParamSpec> params;
    JsonValue current_config = JsonValue::object();

    JsonValue to_json() const {
        JsonValue p_arr = JsonValue::array();
        for (const auto& p : params) p_arr.push_back(p.to_json());
        return JsonValue::object({
            {"type_id", type_id},
            {"category", category},
            {"summary", summary},
            {"order", order},
            {"params", std::move(p_arr)},
            {"current_config", current_config}
        });
    }
};

struct BookingContext {
    const BookingRequest& request;
    Actor actor;
    std::vector<Resource> resources; // Matches request.resource_ids in order
    const std::vector<BookingRecord>& existing_bookings;
    EpochMinute now = 0;
    bool dry_run = false;

    BookingContext(
        const BookingRequest& req,
        Actor act,
        std::vector<Resource> res,
        const std::vector<BookingRecord>& existing,
        EpochMinute now_val,
        bool is_dry_run)
        : request(req)
        , actor(std::move(act))
        , resources(std::move(res))
        , existing_bookings(existing)
        , now(now_val)
        , dry_run(is_dry_run)
    {}

    // Mutable working fields shaped by Bricks along the pipeline
    EpochMinute effective_start = 0;
    EpochMinute effective_end = 0;
    int pre_buffer_minutes = 0;
    int post_buffer_minutes = 0;
    BookingState target_state = BookingState::CONFIRMED;
    int priority = 0;
    double price = 0.0;
    double deposit = 0.0;
    std::string currency = "CNY";
    bool waitlist_enabled = false;
    int max_waitlist_per_slot = 0;
    MetadataMap working_metadata;
    std::vector<std::string> step_notes;

    const Resource& primary_resource() const { return resources.front(); }
    int duration_minutes() const { return static_cast<int>(request.end_time - request.start_time); }
    void add_note(const std::string& n) { step_notes.push_back(n); }
};

struct CancelContext {
    BookingRecord& record;
    Actor actor;
    EpochMinute now = 0;
    std::string reason;
    bool by_admin = false;
    double penalty_fee = 0.0;
    double refund_amount = 0.0;
    std::vector<std::string> step_notes;

    void add_note(const std::string& n) { step_notes.push_back(n); }
};

struct CheckInContext {
    BookingRecord& record;
    Actor actor;
    EpochMinute now = 0;
    std::vector<std::string> step_notes;

    void add_note(const std::string& n) { step_notes.push_back(n); }
};

struct BrickVerdict {
    bool ok = true;
    std::string error_code;
    std::string outcome = "PASS"; // PASS, MUTATED, WAITLIST, APPROVAL_REQUIRED, REJECT
    std::string note;

    static BrickVerdict pass(std::string note = "") {
        return {true, "", "PASS", std::move(note)};
    }
    static BrickVerdict mutated(std::string note, std::string outcome_tag = "MUTATED") {
        return {true, "", std::move(outcome_tag), std::move(note)};
    }
    static BrickVerdict reject(std::string code, std::string msg) {
        return {false, std::move(code), "REJECT", std::move(msg)};
    }
};

class IBrick {
public:
    virtual ~IBrick() = default;

    virtual std::string type_id() const = 0;
    virtual std::string instance_name() const { return type_id(); }
    virtual std::string category() const = 0;
    virtual int order() const = 0;

    virtual void configure(const JsonValue& cfg) = 0;
    virtual BrickDescriptor describe() const = 0;

    virtual BrickVerdict on_pre_book(BookingContext& /*ctx*/) { return BrickVerdict::pass(); }
    virtual void on_post_book(BookingContext& /*ctx*/, BookingRecord& /*rec*/) {}

    virtual BrickVerdict on_pre_cancel(CancelContext& /*ctx*/) { return BrickVerdict::pass(); }
    virtual void on_post_cancel(CancelContext& /*ctx*/, BookingRecord& /*rec*/) {}

    virtual BrickVerdict on_pre_check_in(CheckInContext& /*ctx*/) { return BrickVerdict::pass(); }

    // Lifecycle policy queries used by background sweeps
    virtual std::optional<int> no_show_grace_minutes() const { return std::nullopt; }
    virtual int no_show_credit_deduction() const { return 0; }
    virtual int complete_credit_reward() const { return 0; }
    virtual bool auto_promote_waitlist() const { return false; }
    virtual bool is_exclusive_mode() const { return false; }
};

using BrickPtr = std::shared_ptr<IBrick>;

} // namespace chronokit
