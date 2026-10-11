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

/** Serves requests of at most 64 bytes, queues at most eight, and removes only its own socket. */
class EditorSnapshotIpc
{
public:
    struct Reply
    {
        bool success = false;
        SceneSession::AuthoredSnapshot snapshot;
        std::string error;
    };

    // Invoked by ProcessPending() on the Editor owner thread.
    using SnapshotProvider = std::function<bool(SceneSession::AuthoredSnapshot &, std::string &)>;

    EditorSnapshotIpc() = default;
    EditorSnapshotIpc(const EditorSnapshotIpc &) = delete;
    EditorSnapshotIpc &operator=(const EditorSnapshotIpc &) = delete;
    ~EditorSnapshotIpc();

    // Requires an existing owner-only directory; refuses to replace an existing endpoint.
    bool Start(const std::string &endpoint, std::string &error);

    // Call on the Editor owner thread; returns the number of completed requests.
    size_t ProcessPending(const SnapshotProvider &provider);
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
