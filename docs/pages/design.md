\page design "Design Overview"

# Design Overview

This document outlines the high-level design and implementation strategy for my Raft library.

## Table of Contents

<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->

- [Overview](#overview)
    - [Core Interface](#core-interface)
    - [Network Interface](#network-interface)
    - [Planned Features](#planned-features)
        - [Snapshots](#snapshots)
    - [High-Level Wrapper](#high-level-wrapper)
        - [1. Waiting for Appends](#1-waiting-for-appends)
        - [2. Deduplication](#2-deduplication)
        - [3. Automatic Snapshots](#3-automatic-snapshots)
- [Server Implementation](#server-implementation)
    - [Concurrency Model](#concurrency-model)
        - [Considered Approaches](#considered-approaches)
        - [Selected Approach](#selected-approach)
        - [gRPC Integration](#grpc-integration)
    - [AppendEntries Strategy](#appendentries-strategy)
        - [Example](#example)
        - [Batch Size Adjustment](#batch-size-adjustment)
    - [Persistence Strategy](#persistence-strategy)
    - [Shutdown Handling](#shutdown-handling)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

## Overview

Instead of exposing a separate client-facing service, the core Raft logic is managed through a C++ server interface.
This allows a higher-level service to directly control the Raft node, submit commands to the log, and react to state
changes.

### Core Interface

The primary interface is the [Server](\ref raft::Server) class.

The append method accepts a `std::vector<std::byte>`, meaning clients are responsible for their own data serialization.
The returned EntryInfo struct provides metadata about the log entry's position.

```c++
/// Information about a log entry.
struct EntryInfo {
  uint64_t index; ///< The index of the log entry.
  uint64_t term;  ///< The term of the log entry.
};
```

### Network Interface

The network interface is responsible for sending and receiving Raft messages across the network. In this library's gRPC
implementation, it accepts a generic ServiceHandler to which the network forwards requests. This allows the network
interface to be tested independently of the Raft logic.

```protobuf
// The Raft service is used for communication between Raft replicas.
service Raft {
  // AppendEntries is invoked by the leader to replicate log entries and
  // also serves as a heartbeat.
  rpc AppendEntries(AppendEntriesRequest) returns (AppendEntriesReply);

  // RequestVote is invoked by candidates to gather votes during an election.
  rpc RequestVote(RequestVoteRequest) returns (RequestVoteReply);
}
```

This service is intended strictly for internal communication between Raft server replicas. An asynchronous
gRPC client is used internally by each replica to communicate with its peers.

### Planned Features

#### Snapshots

Support for log compaction via snapshotting is planned for a future release. This will require additions to the server
interface and a new InstallSnapshot RPC.

The proposed API additions are:

```c++
struct Snapshot {
  uint64_t lastIncludedIndex;
  uint64_t lastIncludedTerm;
  std::vector<std::byte> data;
};

/* In the server: */
void requestSnapshot();
void setCreateSnapshotCallback(CreateSnapshotCallback callback);
void setInstallSnapshotCallback(InstallSnapshotCallback callback);
```

There are a few design considerations for snapshotting:

- The `CreateSnapshotCallback` should be non-blocking with respect to new log entries. It will receive the commitIndex
  up
  to which it should create the snapshot.
- Ordering must be guaranteed. A call to `CreateSnapshotCallback(100)` must happen-before a call to
  `CommitCallback(101)`.

### High-Level Wrapper

A wrapper layer will be built on top of the core Raft Server to provide common, higher-level features that many
applications require.

#### 1. Waiting for Appends

Provides a mechanism to block until an appended entry is committed.

- Implementation: An entry is appended via the core append method, and its returned term and index are noted. The
  wrapper then waits for the CommitCallback to fire for that specific index. If the committed entry's term matches the
  original
  term, the operation is successful.
- Performance: To avoid waiting indefinitely, the operation will fast-fail if leadership changes (detected by a term
  change or through the LeaderChangedCallback) before the entry is committed. A timeout will handle other edge cases.

#### 2. Deduplication

Prevents the same command from being applied more than once in the case of client retries.

#### 3. Automatic Snapshots

When the log size, as reported by getLogByteCount(), exceeds a user-defined threshold, the wrapper will automatically
call requestSnapshot(). This process will be non-blocking.

## Server Implementation

One of the most important ideas about Raft is that it should logically act as a single-threaded state machine.
Events should be sent into the sever and processed in a FIFO order.

Several concurrency models can be used to implement this.

### Concurrency Model

#### Considered Approaches

1. A dedicated main thread runs an event queue. Each RPC handler than runs on a separate threads that waits for the main
   thread to complete processing and notify it (through a std::future or a callback). Similarly, each RPC client request
   runs on a separate thread and sends back the response to the main thread. This approach is very likely lower in
   throughput than the other options due to the overhead of thread creation and context switching.
2. An event queue with callbacks and a thread pool. In this implementation, a dedicated main thread runs an
   `asio::io_context`. Events are pushed into an explicit queue, and upon completion, a callback is posted to the
   io_context. This model is performant and offers direct control over the event queue (e.g., checking its size to
   reject requests when overloaded).
3. An `asio::strand` is used to serialize event handlers. Callers post work directly to the strand with a
   completion callback.

#### Selected Approach

The **third option** was chosen for its simplicity and performance.

While the second approach offers simpler explicit control over the event queue, this benefit is minor. For instance, the
same "overload protection" can be achieved in the
strand model by using an atomic counter for pending tasks.

While most operations will be asynchronous, some must be synchronous:

- File system I/O for persistence.
- Getter methods (getTerm(), etc.) need to return a value immediately.

For these cases, the promise/future pattern from Approach 1 will be used. An event is posted to the strand, and the
calling thread blocks on the future until the strand processes the event and fulfills the promise. This allows the
system to benefit from a primarily asynchronous design while accommodating necessary synchronous operations. Options (2)
and (3) can easily and performantly implement option (1), but the reverse is not true.

Interestingly, there is a slight optimization for the getters. We can use local mutexes or atomicity on their
corresponding variables and then define `GetStale*` methods that bypass the event loop/strand.

#### gRPC Integration

The asynchronous gRPC API is used. RPC completion events are translated into callbacks that are posted onto the
main asio::strand for processing.

There exists the `agrpc` library for asio integration with gRPC, but this would introduce a public dependency on asio
and force the usage of `agrpc::ExecutionContext` everywhere. Instead, separating gRPC and asio results in a cleaner
architecture.

### AppendEntries Strategy

AppendEntries has a lot of interesting directions for optimization in terms of latency and throughput.

For the initial implementation, AppendEntries uses a combination of pipelining and batching to maximize throughput while
maintaining a reasonable latency. AppendEntries, in this design, is entirely dependent on heartbeats; no AppendEntries
requests are run outside the timeout schedule.

Specifically, for each follower, the leader maintains a heartbeat timer and a current batch size. Whenever the timer
expires, the leader issues an AppendEntries call asynchronously with entries going from
`[nextIndex : nextIndex + batchSize)`, where `nextIndex` is the index of the next log entry to send as defined in the
Raft paper. Then, it resets the timer immediately and update `nextIndex` to `nextIndex + batchSize`.

#### Example

This design allows there to be multiple AppendEntries requests in flight. For instance, consider the following scenario
with 100 total entries, a batch size of 50, an RTT of 75ms, and a heartbeat timeout of 50ms:

1. At time 0ms, the leader sends AppendEntries([1, 51)) and resets the heartbeat timer.
2. At time 50ms, the timer expires, and the leader sends AppendEntries([51, 101)).
3. At time 75ms, the first AppendEntries request returns.
4. At time 125ms, the second AppendEntries request returns.

As is likely clear, the batch size is incredibly important here. First, in terms of latency and throughput,

- If it is too small, the throughput will be low.
- If it is too large, the round-trip time will be high. This might cause issues with election timeouts immediately after
  a leader change, when there are no heartbeats in flight.

Secondly, there exists a tradeoff in terms of errors. The larger the batch size, the more data that will lost if an
AppendEntries request fails (made worse by the pipelining, since all following requests will also fail).

There are also tradeoffs for the heartbeat timeout. The more frequent the heartbeat timeout, the more data will be
discarded due to the pipelining of heartbeats in-flight. However, latency increases with the larger heartbeat timeout.

#### Batch Size Adjustment

A dynamic batch size, loosely inspired by TCP congestion control, will be used. We maintain a variable `threshold` that
is initialized to a large (infinite) value.

- Start with a batch size of 1.
- If an AppendEntries request with the current maximum batch size succeeds:
    - If `batchSize < threshold`, double the batch size.
    - Otherwise, increment the batch size by a configurable amount.
- On log inconsistency, set `threshold = max(1, batchSize / 2)` and immediately reset the batch
  size to 1.
- On network timeout, halve both the batch size and the threshold.

### Persistence Strategy

The server's state must be persisted before responding to RPCs or returning to the client. However, to avoid blocking
the main logic thread on slow disk I/O, persistence must run on a dedicated I/O thread.

When the Raft logic needs to persist state (e.g., after updating its term or appending an entry), it will dispatch a "
persist" job to the I/O thread. To prevent the I/O thread from becoming a bottleneck, these jobs are batched:

1. The main logic thread adds persistence requests to a concurrent queue.
2. The I/O thread wakes on a timer or when the queue reaches a certain size (e.g. 1024 entries).
3. It atomically swaps the pending queue with a new empty one. Then, it persists the most recent state.
4. Once the batch is persisted, it invokes the callbacks for each request in the batch.

### Shutdown Handling

Keeping server resources alive for long enough is an issue (as is all lifetime and memory management).

The core challenge here is that, for correctness purposes and to avoid deadlocks, the server needs to finish executing
all current tasks in the main strand before the server terminates. Consider for instance, if a client calls GetTerm().
This function must:

- Post an event to the strand that:
    - Retrieves the current term
    - Post an event to the I/O thread that:
        - Persists current state
        - Posts another event on the strand that sends the result back to the GetTerm() thread
- Wait for the strand to surface the result

If we simply close off the ability to send new events into the strand, we could end up in a situation where:

- The final event that sends the result back is never queued.
- Therefore, GetTerm() waits indefinitely.

The key is to gracefully stop the generation of new events while allowing all existing events to be processed. In Raft,
there are only three possibilities for new events:

1. Any client requests (Append(), Get*, ...)
2. Heartbeat and election timeouts
3. Incoming requests for AppendEntries and RequestVote

We store an atomic lifecycle variable and check if is in the RUNNING state prior to dispatching the event to the
strand.

To keep the `asio::io_context` alive throughout execution and after shutdown, we can use
an [executor work guard](https://www.boost.org/doc/libs/1_84_0/doc/html/boost_asio/reference/executor_work_guard.html),
which functionally acts as a single increment (and RAII decrement) of the context's atomic work counter. If the
context's work counter is non-zero, it will stay alive. Thus, there are two required conditions for the asio context to
shutdown:

1. There is no work executing on any asio thread
2. There are no `asio::executor_work_guard`s alive

On server startup, we create this work guard, and we destroy it on server shutdown. For tasks that run on external
threads (like gRPC handlers or persistence callbacks), a temporary `executor_work_guard` is created to ensure the
`io_context` remains active until control is posted back to the main asio::strand.

## Wrapper Implementation

See [Wrapper](\ref wrapper) for the API reference.

### Deduplication

The wrapper, instead of deduplicating stored entries, deduplicates the processing of these entries.
This is to prevent the wrapper from needing direct access to the internal log or a complex persistence mechanism.

We accept two fields along with the entry:

- `clientID`: a unique string for each client.
- `requestID`: a monotonically increasing integer associated with each request.

As the Raft paper suggests, we store the latest request ID that has been processed for each client. Upon receiving a
new server commit, we first intercept the server's callback in the wrapper before passing it on to the provided
callback.
Then, we examine the entry's `requestID` corresponding to its client.
If the client's `requestID` is greater than the stored `requestID`, then we update our stored `requestID` to
the request's and proceed with the provided callbacks with a false duplication flag.
Otherwise, the callbacks are invoked with the duplicate flag. We also provide a `clearClient` function that erases
the stored information for a client.

The map of stored clients and requests must be included into snapshots (hence why we intercept snapshot requests).

Note that this design implies that client requests must be sequential. That is, request A should be guaranteed to be
committed before request B starts if $A < B$. If this is not the desired behavior, then creating a separate client
for each parallel operation can accommodate parallel requests at the cost of storage space. 
