#include "graph_persistent_actor.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace graph_actor {
namespace {
bool equal_key(Key a, Key b) { return a.epoch == b.epoch && a.job == b.job; }
Result current(State s) {
    return s == State::Released ? Result::Complete :
        s == State::Prepared ? Result::Ready : Result::Waiting;
}
bool valid_descriptor(const Descriptor &d) {
    if (!d.key.epoch || !d.key.job || !d.clock || !d.owner_generation ||
        !d.source_generation || !d.ammo_receipt || !d.credit_receipt ||
        !d.fragments || !d.fragment_count) return false;
    bool hash = false;
    for (uint8_t b : d.source_closure) hash = hash || b != 0;
    if (!hash) return false;
    double length = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(d.origin.position[i]) ||
            !std::isfinite(d.origin.direction[i])) return false;
        length += double(d.origin.direction[i]) * d.origin.direction[i];
    }
    if (!(length > 0) || !std::isfinite(length)) return false;
    for (size_t i = 0; i < d.fragment_count; ++i) {
        if (!d.fragments[i].id || !d.fragments[i].payload) return false;
        for (size_t j = 0; j < i; ++j)
            if (d.fragments[i].id == d.fragments[j].id) return false;
    }
    return true;
}
bool equal_descriptor(const Descriptor &a, const Descriptor &b) {
    if (!equal_key(a.key, b.key) || a.clock != b.clock || a.deadline != b.deadline ||
        a.owner_generation != b.owner_generation || a.source_generation != b.source_generation ||
        a.ammo_receipt != b.ammo_receipt || a.credit_receipt != b.credit_receipt ||
        a.fragment_count != b.fragment_count ||
        std::memcmp(a.source_closure, b.source_closure, sizeof(a.source_closure))) return false;
    for (size_t i = 0; i < 3; ++i)
        if (a.origin.position[i] != b.origin.position[i] ||
            a.origin.direction[i] != b.origin.direction[i]) return false;
    for (size_t i = 0; i < a.fragment_count; ++i)
        if (a.fragments[i].id != b.fragments[i].id ||
            a.fragments[i].payload != b.fragments[i].payload) return false;
    return true;
}
}

Runtime::Runtime(JobStorage *jobs, size_t count, Fragment *descriptors,
    FragmentRecord *receipts, size_t storage, size_t per_job, Host host)
    : jobs_(jobs), capacity_(count), per_job_(per_job), fragments_(descriptors),
      receipts_(receipts), host_(host) {
    if (!jobs || !descriptors || !receipts || !count || !per_job ||
        count > std::numeric_limits<size_t>::max() / per_job || count * per_job > storage ||
        !host.reserve || !host.lookup || !host.materialize || !host.close) return;
    for (size_t i = 0; i < count; ++i)
        if (jobs[i].state != State::Empty) return;
    valid_ = true;
}

JobStorage *Runtime::find_mutable(Key key) {
    if (!valid_) return nullptr;
    for (size_t i = 0; i < capacity_; ++i)
        if (jobs_[i].state != State::Empty && equal_key(jobs_[i].descriptor.key, key))
            return &jobs_[i];
    return nullptr;
}
const JobStorage *Runtime::find(Key key) const {
    return const_cast<Runtime *>(this)->find_mutable(key);
}
size_t Runtime::slot(const JobStorage *job) const { return size_t(job - jobs_); }
uint64_t Runtime::receipt(Key key, size_t index) const {
    const auto *job = find(key);
    if (!job || index >= job->descriptor.fragment_count) return 0;
    return receipts_[slot(job) * per_job_ + index].receipt;
}
bool Runtime::checkpoint(Boundary b, const JobStorage &job, size_t index) {
    return !host_.checkpoint || host_.checkpoint(host_.context, b, job.descriptor.key, index);
}

Result Runtime::prepare(const Descriptor &d, uint64_t now) {
    // Bound the caller count BEFORE reading any fragment descriptors.
    if (!valid_ || !d.fragment_count || d.fragment_count > per_job_) return Result::Capacity;
    if (!valid_descriptor(d)) return Result::Invalid;
    if (auto *existing = find_mutable(d.key)) {
        if (!equal_descriptor(existing->descriptor, d)) return Result::Conflict;
        if (existing->state == State::Prepared) {
            if (now < existing->last_time || now > d.deadline) return Result::Invalid;
            existing->last_time = now;
        }
        return current(existing->state);
    }
    if (d.deadline < now) return Result::Invalid;
    JobStorage *available = nullptr;
    for (size_t i = 0; i < capacity_; ++i) {
        const auto &job = jobs_[i];
        if (job.state == State::Empty) { if (!available) available = &jobs_[i]; continue; }
        if (job.descriptor.key.epoch != d.key.epoch) continue;
        for (size_t j = 0; j < job.descriptor.fragment_count; ++j)
            for (size_t k = 0; k < d.fragment_count; ++k)
                if (job.descriptor.fragments[j].id == d.fragments[k].id) return Result::Conflict;
    }
    if (!available) return Result::Capacity;
    // Copy before reserve; the host and job see the same immutable descriptor.
    const size_t offset = slot(available) * per_job_;
    for (size_t i = 0; i < d.fragment_count; ++i) {
        fragments_[offset + i] = d.fragments[i];
        receipts_[offset + i] = {d.fragments[i], 0};
    }
    Descriptor snapshot = d;
    snapshot.fragments = fragments_ + offset;
    uint64_t token = 0;
    const Result result = host_.reserve(host_.context, snapshot, &token);
    if (result != Result::Ready) return result == Result::Waiting ? Result::Waiting : Result::HostFailure;
    if (!token) return Result::HostFailure;
    available->descriptor = snapshot;
    available->reservation = token;
    available->last_time = now;
    available->next_fragment = 0;
    available->state = State::Prepared;
    return Result::Ready;
}

Result Runtime::commit(Key key, uint64_t now) {
    auto *job = find_mutable(key);
    if (!job) return Result::Invalid;
    if (job->state == State::Prepared) {
        if (now < job->last_time || now > job->descriptor.deadline) return Result::Invalid;
        job->last_time = now;
        job->state = State::Committed;
    }
    return current(job->state);
}
Result Runtime::cancel_prepared(Key key) {
    auto *job = find_mutable(key);
    if (!job || job->state != State::Prepared) return Result::Invalid;
    if (!host_.close(host_.context, job->descriptor, job->reservation, false)) return Result::HostFailure;
    *job = JobStorage{};
    return Result::Ready;
}

Result Runtime::advance(Key key, uint64_t now) {
    auto *job = find_mutable(key);
    if (!job) return Result::Invalid;
    if (job->state == State::Released) return Result::Complete;
    if (job->state == State::Prepared || now < job->last_time) return Result::Invalid;
    job->last_time = now;
    if (now < job->descriptor.deadline) return Result::Waiting;
    if (job->state == State::Committed) {
        if (!checkpoint(Boundary::BeforeRelease, *job, 0)) return Result::Interrupted;
        job->state = State::Releasing; // The immutable outbox is now claimed once.
        if (!checkpoint(Boundary::AfterReleaseClaim, *job, 0)) return Result::Interrupted;
    }
    while (job->next_fragment < job->descriptor.fragment_count) {
        const size_t i = job->next_fragment;
        auto &record = receipts_[slot(job) * per_job_ + i];
        uint64_t proof = 0;
        Result found = host_.lookup(host_.context, job->descriptor, job->reservation, record.fragment, &proof);
        if (found == Result::Waiting) {
            if (!checkpoint(Boundary::BeforeMaterialize, *job, i)) return Result::Interrupted;
            found = host_.materialize(host_.context, job->descriptor, job->reservation, record.fragment, &proof);
            if (!checkpoint(Boundary::AfterMaterialize, *job, i)) return Result::Interrupted;
        }
        if (found != Result::Ready || !proof) return Result::HostFailure;
        if (!checkpoint(Boundary::BeforeReceipt, *job, i)) return Result::Interrupted;
        record.receipt = proof;
        ++job->next_fragment;
        if (!checkpoint(Boundary::AfterReceipt, *job, i)) return Result::Interrupted;
    }
    if (!checkpoint(Boundary::BeforeComplete, *job, job->next_fragment)) return Result::Interrupted;
    if (!host_.close(host_.context, job->descriptor, job->reservation, true)) return Result::HostFailure;
    job->state = State::Released;
    return Result::Complete;
}
} // namespace graph_actor
