#ifndef PD_GRAPH_PERSISTENT_ACTOR_H
#define PD_GRAPH_PERSISTENT_ACTOR_H

#include <cstddef>
#include <cstdint>

/* Pure component, with no graph profile or gameplay adapter enabled. All
 * storage is supplied once by the match owner. Keep storage/host/source leases
 * alive until every committed job closes; destroying this view cancels nothing.
 * Single serialized caller. Host callbacks must not throw or re-enter it. */
namespace graph_actor {

enum class Result { Ready, Waiting, Interrupted, Complete, Invalid, Capacity,
    Conflict, HostFailure };
enum class State { Empty, Prepared, Committed, Releasing, Released };
enum class Boundary { BeforeRelease, AfterReleaseClaim, BeforeMaterialize,
    AfterMaterialize, BeforeReceipt, AfterReceipt, BeforeComplete };

struct Key { uint64_t epoch = 0, job = 0; };
struct Fragment { uint64_t id = 0, payload = 0; };
struct Origin { float position[3] = {}, direction[3] = {}; };
struct Descriptor {
    Key key;
    uint64_t clock = 0, deadline = 0;
    uint64_t owner_generation = 0, source_generation = 0;
    uint8_t source_closure[32] = {};
    Origin origin;
    uint64_t ammo_receipt = 0, credit_receipt = 0;
    const Fragment *fragments = nullptr;
    size_t fragment_count = 0;
};

struct Host {
    void *context = nullptr;
    /* Ready with a nonzero token atomically reserves the complete finite native
     * release path and source lifetime. Any other result transfers no resource.
     * A production adapter must independently prove this contract and deadline
     * capacity: the component or a fake host cannot certify it. */
    Result (*reserve)(void *, const Descriptor &, uint64_t *token) = nullptr;
    /* Lookup/materialize are idempotent under (epoch, job, fragment ID), with
     * immutable payload binding. Lookup returns Waiting when no receipt exists.
     * A receipt is nonzero and means actual materialization, not an intent bit.
     * After creation-before-ack interruption, lookup must recover that receipt.
     * Materialize may fail explicitly; it must never create a second fragment. */
    Result (*lookup)(void *, const Descriptor &, uint64_t token,
        const Fragment &, uint64_t *receipt) = nullptr;
    Result (*materialize)(void *, const Descriptor &, uint64_t token,
        const Fragment &, uint64_t *receipt) = nullptr;
    /* Idempotent close releases reservations/source leases. committed=true is
     * called only after every materialization receipt. Failure retains them.
     * committed=false is permitted only for a cancelled prepared job. */
    bool (*close)(void *, const Descriptor &, uint64_t token, bool committed) = nullptr;
    /* Optional continuation boundary for controlled fault tests. false yields
     * without clearing state. It is not a production save format. */
    bool (*checkpoint)(void *, Boundary, Key, size_t fragment_index) = nullptr;
};

struct FragmentRecord { Fragment fragment; uint64_t receipt = 0; };
struct JobStorage {
    State state = State::Empty;
    Descriptor descriptor;
    uint64_t reservation = 0, last_time = 0;
    size_t next_fragment = 0;
};

class Runtime {
public:
    /* Per-job fragment descriptors and receipts use separate caller arrays.
     * Capacities are explicit, checked for overflow. Fresh empty job storage is
     * required. There is no reset/recycle operation: completed identities stay
     * as tombstones, preventing same-match replay from minting another volley. */
    Runtime(JobStorage *jobs, size_t job_capacity, Fragment *descriptors,
        FragmentRecord *receipts, size_t fragment_storage_count,
        size_t fragments_per_job, Host host);
    bool valid() const { return valid_; }
    Result prepare(const Descriptor &, uint64_t now);
    // The selected clock's fresh time is required at irrevocable admission.
    // Late/backward commits remain Prepared and cancellable.
    Result commit(Key, uint64_t now);
    Result cancel_prepared(Key);
    Result advance(Key, uint64_t now);
    const JobStorage *find(Key) const;
    uint64_t receipt(Key, size_t index) const;

private:
    JobStorage *jobs_;
    size_t capacity_, per_job_;
    Fragment *fragments_;
    FragmentRecord *receipts_;
    Host host_;
    bool valid_ = false;
    JobStorage *find_mutable(Key);
    size_t slot(const JobStorage *) const;
    bool checkpoint(Boundary, const JobStorage &, size_t);
};

} // namespace graph_actor
#endif
