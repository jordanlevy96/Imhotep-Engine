/** @file EditorSnapshotIpc.h Owner-thread Unix socket service for Editor snapshots. */
#pragma once

#include "editor/SceneSession.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/** Owns one listener thread and socket; queues at most eight requests of up to 64 bytes. */
class EditorSnapshotIpc
{
public:
    struct Reply
    {
        bool success = false;
        SceneSession::AuthoredSnapshot snapshot;
        std::string error;
    };

    /// Invoked by ProcessPending() on the Editor owner thread.
    using SnapshotProvider = std::function<bool(SceneSession::AuthoredSnapshot &, std::string &)>;

    EditorSnapshotIpc() = default;
    EditorSnapshotIpc(const EditorSnapshotIpc &) = delete;
    EditorSnapshotIpc &operator=(const EditorSnapshotIpc &) = delete;
    ~EditorSnapshotIpc();

    /// Requires an absolute normalized path in an existing owner-only directory.
    /// Refuses to replace an existing endpoint.
    bool Start(const std::string &endpoint, std::string &error);

    /// Call on the Editor owner thread; returns the number of completed requests.
    size_t ProcessPending(const SnapshotProvider &provider);
    /// Joins the listener and removes only the endpoint this instance owns.
    void Stop();
    bool IsRunning() const;
    size_t PendingRequestCount() const;

    static constexpr size_t MaxRequestBytes = 64;
    static constexpr size_t MaxSceneBytes = 2 * 1024 * 1024;
    static constexpr size_t MaxPathBytes = 4096;
    static constexpr unsigned RequestTimeoutMs = 1500;

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
