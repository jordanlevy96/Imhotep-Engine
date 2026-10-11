/**
 * @file EditorSnapshotIpc.h
 * @brief Opt-in local IPC for owner-thread authored-scene snapshots.
 * @lines ~109
 *
 * Quick-stats (Public API):
 * - Start() / Stop() - Manage the owner-only Unix socket
 * - ProcessPending() - Serve queued requests on the Editor owner thread
 * - IsRunning() / PendingRequestCount() - Inspect bridge state
 */
#pragma once

#include "editor/SceneSession.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/**
 * @brief Opt-in Unix socket that serves authored snapshots through the Editor owner thread.
 *
 * The listener accepts one request of up to 64 bytes at a time and queues at most eight requests.
 * Snapshot callbacks are invoked by ProcessPending(), allowing callers to run them on the Editor
 * thread. The socket is owner-only and Stop() removes only the endpoint created by this instance.
 */
class EditorSnapshotIpc
{
public:
    /** Result sent to a client after its snapshot request is processed. */
    struct Reply
    {
        bool success = false; ///< Whether a snapshot was produced.
        SceneSession::AuthoredSnapshot snapshot; ///< Snapshot payload on success.
        std::string error; ///< Failure reason on rejection or provider error.
    };

    /**
     * Provider called by ProcessPending() to copy current authored Editor state.
     * @param snapshot Receives the authored snapshot on success
     * @param error Receives the failure reason on rejection
     * @return true when the snapshot was produced
     */
    using SnapshotProvider = std::function<bool(SceneSession::AuthoredSnapshot &, std::string &)>;

    /** Construct a stopped bridge with no owned socket. */
    EditorSnapshotIpc() = default;

    /** Copying is disabled because the bridge owns a socket and worker thread. */
    EditorSnapshotIpc(const EditorSnapshotIpc &) = delete;

    /** Copy assignment is disabled because the bridge owns a socket and worker thread. */
    EditorSnapshotIpc &operator=(const EditorSnapshotIpc &) = delete;

    /** Stop the listener and remove its owned endpoint. */
    ~EditorSnapshotIpc();

    /**
     * @brief Start listening at a new owner-only Unix socket endpoint.
     * @param endpoint Absolute normalized path under an existing owner-only directory
     * @param error Receives setup failure details, or is cleared on success
     * @return true when listening starts; false for invalid paths, unsupported platforms, or socket errors
     * @note Existing endpoints are never replaced. The parent directory must be owned by the current user.
     */
    bool Start(const std::string &endpoint, std::string &error);

    /**
     * @brief Process queued requests using the supplied snapshot provider.
     * @param provider Callback that returns the authored snapshot and an error on failure
     * @return Number of requests completed by this call
     * @note Call on the Editor owner thread so the provider can safely read SceneSession.
     */
    size_t ProcessPending(const SnapshotProvider &provider);

    /** Stop accepting requests, complete waiters, join the listener, and remove the owned socket. */
    void Stop();

    /** @return true while the listener accepts requests. */
    bool IsRunning() const;

    /** @return Number of requests queued for owner-thread processing. */
    size_t PendingRequestCount() const;

    static constexpr size_t MaxRequestBytes = 64; ///< Maximum incoming request size in bytes.
    static constexpr size_t MaxSceneBytes = 2 * 1024 * 1024; ///< Maximum serialized scene size in bytes.
    static constexpr size_t MaxPathBytes = 4096; ///< Maximum serialized scene path in bytes.
    static constexpr unsigned RequestTimeoutMs = 1500; ///< Maximum wait for owner-thread service, in milliseconds.

private:
    struct PendingRequest;
    void ListenLoop();
    void HandleClient(int clientFd);
    void Complete(const std::shared_ptr<PendingRequest> &request, Reply reply);

    mutable std::mutex m_mutex;
    std::deque<std::shared_ptr<PendingRequest>> m_pending;
    std::shared_ptr<PendingRequest> m_waitingRequest;
    std::thread m_thread;
    std::string m_endpoint;
    int m_listenFd = -1;
    int m_activeClientFd = -1;
    bool m_running = false;
    bool m_stopping = false;
    unsigned long long m_endpointDevice = 0;
    unsigned long long m_endpointInode = 0;
};
