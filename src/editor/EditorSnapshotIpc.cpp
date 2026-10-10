#include "editor/EditorSnapshotIpc.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <sstream>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

struct EditorSnapshotIpc::PendingRequest
{
    std::mutex mutex;
    std::condition_variable ready;
    bool complete = false;
    Reply reply;
};

namespace
{
    constexpr size_t MaxPendingRequests = 8;
    constexpr unsigned IoTimeoutMs = 1000;
    constexpr size_t MaxErrorBytes = 512;

#ifndef _WIN32
    bool IsStopping(std::mutex &mutex, bool &stopping)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return stopping;
    }

    bool WaitForFd(int fd, short events, unsigned timeoutMs)
    {
        pollfd descriptor{fd, events, 0};
        int result;
        do
        {
            result = ::poll(&descriptor, 1, static_cast<int>(timeoutMs));
        } while (result < 0 && errno == EINTR);
        return result > 0 && (descriptor.revents & events) != 0;
    }

    bool SendAll(int fd, const std::string &bytes)
    {
        size_t sent = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(IoTimeoutMs);
        while (sent < bytes.size())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            if (!WaitForFd(fd, POLLOUT, 100))
            {
                continue;
            }
#ifdef MSG_NOSIGNAL
            const ssize_t count = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
#else
            const ssize_t count = ::send(fd, bytes.data() + sent, bytes.size() - sent, 0);
#endif
            if (count > 0)
            {
                sent += static_cast<size_t>(count);
            }
            else if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                return false;
            }
        }
        return true;
    }

    bool ReceiveRequest(int fd, std::string &request, bool &oversized, std::mutex &mutex, bool &stopping)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(IoTimeoutMs);
        char buffer[32];
        while (true)
        {
            if (IsStopping(mutex, stopping) || std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            if (!WaitForFd(fd, POLLIN, 100))
            {
                continue;
            }
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count == 0)
            {
                return true;
            }
            if (count < 0)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                return false;
            }
            request.append(buffer, static_cast<size_t>(count));
            if (request.size() > EditorSnapshotIpc::MaxRequestBytes)
            {
                oversized = true;
                return true;
            }
        }
    }

    void SendError(int fd, std::string error)
    {
        error.resize(std::min(error.size(), MaxErrorBytes));
        SendAll(fd, "IMHOTEP_SNAPSHOT/1 ERROR " + std::to_string(error.size()) + "\n" + error);
    }

    bool SendSnapshot(int fd, const SceneSession::AuthoredSnapshot &snapshot)
    {
        YAML::Emitter emitter;
        emitter << snapshot.scene;
        if (!emitter.good())
        {
            SendError(fd, emitter.GetLastError());
            return false;
        }
        const std::string yaml = emitter.c_str();
        if (snapshot.documentIdentity.empty() || snapshot.documentIdentity.size() > 64 ||
            snapshot.path.size() > EditorSnapshotIpc::MaxPathBytes || yaml.size() > EditorSnapshotIpc::MaxSceneBytes)
        {
            SendError(fd, "snapshot exceeds local bridge limits");
            return false;
        }
        std::ostringstream header;
        header << "IMHOTEP_SNAPSHOT/1 OK " << snapshot.documentIdentity.size() << ' '
               << snapshot.revision << ' ' << (snapshot.dirty ? 1 : 0) << ' '
               << snapshot.path.size() << ' ' << yaml.size() << '\n';
        return SendAll(fd, header.str() + snapshot.documentIdentity + snapshot.path + yaml);
    }
#endif
}

EditorSnapshotIpc::~EditorSnapshotIpc()
{
    Stop();
}

bool EditorSnapshotIpc::Start(const std::string &endpoint, std::string &error)
{
#ifdef _WIN32
    (void)endpoint;
    error = "Editor snapshot IPC is not supported on Windows.";
    return false;
#else
    namespace fs = std::filesystem;
    const fs::path requested(endpoint);
    if (endpoint.empty() || !requested.is_absolute() || requested.filename().empty() ||
        requested != requested.lexically_normal())
    {
        error = "Snapshot endpoint must be an absolute socket path.";
        return false;
    }
    std::error_code pathError;
    const fs::path parent = requested.parent_path();
    const fs::path canonicalParent = fs::canonical(parent, pathError);
    if (pathError || canonicalParent != parent.lexically_normal())
    {
        error = "Snapshot endpoint parent must exist and must not traverse symlinks.";
        return false;
    }
    struct stat directoryInfo{};
    if (::lstat(parent.c_str(), &directoryInfo) != 0 || !S_ISDIR(directoryInfo.st_mode) ||
        directoryInfo.st_uid != ::geteuid() || (directoryInfo.st_mode & 0077) != 0 ||
        (directoryInfo.st_mode & 0700) != 0700)
    {
        error = "Snapshot endpoint parent must be an owner-only directory owned by this user.";
        return false;
    }
    struct stat existing{};
    if (::lstat(requested.c_str(), &existing) == 0 || errno != ENOENT)
    {
        error = "Snapshot endpoint already exists or cannot be checked; refusing to replace it.";
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string endpointPath = requested.string();
    if (endpointPath.size() >= sizeof(address.sun_path))
    {
        error = "Snapshot endpoint path is too long for a Unix socket.";
        return false;
    }
    std::memcpy(address.sun_path, endpointPath.c_str(), endpointPath.size() + 1);
    const int socketFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0)
    {
        error = std::string("Could not create snapshot socket: ") + std::strerror(errno);
        return false;
    }
    const int bindResult = ::bind(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (bindResult != 0)
    {
        error = std::string("Could not bind snapshot socket: ") + std::strerror(errno);
        ::close(socketFd);
        return false;
    }
    struct stat socketInfo{};
    if (::lstat(endpointPath.c_str(), &socketInfo) != 0 || !S_ISSOCK(socketInfo.st_mode) ||
        socketInfo.st_uid != ::geteuid())
    {
        error = "Bound snapshot endpoint is not an owned socket.";
        ::close(socketFd);
        return false;
    }
    const dev_t socketDevice = socketInfo.st_dev;
    const ino_t socketInode = socketInfo.st_ino;
    const auto removeOwnedEndpoint = [&]()
    {
        struct stat current{};
        if (::lstat(endpointPath.c_str(), &current) == 0 && S_ISSOCK(current.st_mode) &&
            current.st_dev == socketDevice && current.st_ino == socketInode)
        {
            ::unlink(endpointPath.c_str());
        }
    };
    if (::chmod(endpointPath.c_str(), 0600) != 0 || ::listen(socketFd, 8) != 0)
    {
        error = std::string("Could not secure/listen on snapshot socket: ") + std::strerror(errno);
        ::close(socketFd);
        removeOwnedEndpoint();
        return false;
    }
    struct stat securedInfo{};
    if (::lstat(endpointPath.c_str(), &securedInfo) != 0 || !S_ISSOCK(securedInfo.st_mode) ||
        securedInfo.st_uid != ::geteuid() || securedInfo.st_dev != socketDevice ||
        securedInfo.st_ino != socketInode || (securedInfo.st_mode & 0077) != 0)
    {
        error = "Snapshot socket did not retain owner-only permissions.";
        ::close(socketFd);
        removeOwnedEndpoint();
        return false;
    }
#ifdef SO_NOSIGPIPE
    int noSignal = 1;
    ::setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
#endif
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running || m_thread.joinable())
        {
            error = "Snapshot bridge is already running.";
            ::close(socketFd);
            removeOwnedEndpoint();
            return false;
        }
        m_endpoint = endpointPath;
        m_endpointDevice = static_cast<unsigned long long>(socketDevice);
        m_endpointInode = static_cast<unsigned long long>(socketInode);
        m_listenFd = socketFd;
        m_stopping = false;
        m_running = true;
    }
    try
    {
        m_thread = std::thread(&EditorSnapshotIpc::ListenLoop, this);
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        m_stopping = true;
        m_listenFd = -1;
        ::close(socketFd);
        removeOwnedEndpoint();
        return false;
    }
    error.clear();
    return true;
#endif
}

size_t EditorSnapshotIpc::ProcessPending(const SnapshotProvider &provider)
{
    std::deque<std::shared_ptr<PendingRequest>> requests;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_stopping)
        {
            return 0;
        }
        requests.swap(m_pending);
    }
    size_t processed = 0;
    for (const auto &request : requests)
    {
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            if (request->complete)
            {
                continue;
            }
        }
        Reply reply;
        try
        {
            reply.success = provider && provider(reply.snapshot, reply.error);
            if (!reply.success && reply.error.empty())
            {
                reply.error = "Editor is stopped or no authored scene is open.";
            }
        }
        catch (const std::exception &exception)
        {
            reply.error = exception.what();
        }
        catch (...)
        {
            reply.error = "Snapshot provider failed.";
        }
        Complete(request, std::move(reply));
        ++processed;
    }
    return processed;
}

void EditorSnapshotIpc::Complete(const std::shared_ptr<PendingRequest> &request, Reply reply)
{
    {
        std::lock_guard<std::mutex> lock(request->mutex);
        if (request->complete)
        {
            return;
        }
        request->reply = std::move(reply);
        request->complete = true;
    }
    request->ready.notify_all();
}

void EditorSnapshotIpc::ListenLoop()
{
#ifndef _WIN32
    int listenFd = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        listenFd = m_listenFd;
    }
    while (!IsStopping(m_mutex, m_stopping))
    {
        if (!WaitForFd(listenFd, POLLIN, 100))
        {
            continue;
        }
        const int clientFd = ::accept(listenFd, nullptr, nullptr);
        if (clientFd < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || IsStopping(m_mutex, m_stopping))
            {
                continue;
            }
            continue;
        }
#ifdef SO_NOSIGPIPE
        int noSignal = 1;
        ::setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
#endif
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping)
            {
                ::close(clientFd);
                break;
            }
            m_activeClientFd = clientFd;
        }
        HandleClient(clientFd);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_activeClientFd == clientFd)
            {
                m_activeClientFd = -1;
            }
            m_waitingRequest.reset();
        }
        ::close(clientFd);
    }
#endif
}

void EditorSnapshotIpc::HandleClient(int clientFd)
{
#ifndef _WIN32
    std::string command;
    bool oversized = false;
    if (!ReceiveRequest(clientFd, command, oversized, m_mutex, m_stopping))
    {
        return;
    }
    if (oversized)
    {
        SendError(clientFd, "snapshot request is oversized");
        return;
    }
    if (command != "snapshot\n")
    {
        SendError(clientFd, "expected the single request 'snapshot\\n'");
        return;
    }
    auto request = std::make_shared<PendingRequest>();
    std::string rejection;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping || !m_running)
        {
            rejection = "snapshot bridge is stopping";
        }
        else if (m_pending.size() >= MaxPendingRequests)
        {
            rejection = "snapshot request queue is full";
        }
        else
        {
            m_pending.push_back(request);
            m_waitingRequest = request;
        }
    }
    if (!rejection.empty())
    {
        SendError(clientFd, rejection);
        return;
    }
    Reply reply;
    bool timedOut = false;
    {
        std::unique_lock<std::mutex> lock(request->mutex);
        if (!request->ready.wait_for(lock, std::chrono::milliseconds(RequestTimeoutMs),
                                    [&]() { return request->complete; }))
        {
            timedOut = true;
            request->complete = true;
            request->reply.error = "timed out waiting for the Editor owner thread";
        }
        reply = request->reply;
    }
    if (timedOut)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), request), m_pending.end());
    }
    if (!reply.success)
    {
        SendError(clientFd, reply.error.empty() ? "snapshot request failed" : reply.error);
    }
    else
    {
        SendSnapshot(clientFd, reply.snapshot);
    }
#endif
}

void EditorSnapshotIpc::Stop()
{
    std::deque<std::shared_ptr<PendingRequest>> pending;
    std::shared_ptr<PendingRequest> waiting;
    int listenFd = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running && !m_thread.joinable())
        {
            return;
        }
        m_stopping = true;
        pending.swap(m_pending);
        waiting = m_waitingRequest;
        listenFd = m_listenFd;
#ifndef _WIN32
        if (listenFd >= 0)
        {
            ::shutdown(listenFd, SHUT_RDWR);
        }
#endif
    }
    Reply stopped;
    stopped.error = "snapshot bridge is stopping";
    for (const auto &request : pending)
    {
        Complete(request, stopped);
    }
    if (waiting)
    {
        Complete(waiting, stopped);
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    std::string endpoint;
    unsigned long long device = 0;
    unsigned long long inode = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        endpoint = m_endpoint;
        device = m_endpointDevice;
        inode = m_endpointInode;
        m_endpoint.clear();
        m_waitingRequest.reset();
        m_activeClientFd = -1;
        m_listenFd = -1;
        m_running = false;
    }
#ifndef _WIN32
    if (listenFd >= 0)
    {
        ::close(listenFd);
    }
    struct stat current{};
    if (!endpoint.empty() && ::lstat(endpoint.c_str(), &current) == 0 && S_ISSOCK(current.st_mode) &&
        static_cast<unsigned long long>(current.st_dev) == device &&
        static_cast<unsigned long long>(current.st_ino) == inode)
    {
        ::unlink(endpoint.c_str());
    }
#endif
}

bool EditorSnapshotIpc::IsRunning() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running && !m_stopping;
}

size_t EditorSnapshotIpc::PendingRequestCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.size();
}
