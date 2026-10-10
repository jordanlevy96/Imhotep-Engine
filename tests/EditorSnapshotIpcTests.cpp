#include "editor/EditorSnapshotIpc.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

#ifndef _WIN32
    struct TemporaryDirectory
    {
        std::filesystem::path path;
        TemporaryDirectory()
        {
            std::string pattern = (std::filesystem::temp_directory_path() / "imhotep-ipc-XXXXXX").string();
            std::vector<char> writable(pattern.begin(), pattern.end());
            writable.push_back('\0');
            char *created = ::mkdtemp(writable.data());
            if (!created)
            {
                throw std::runtime_error("mkdtemp failed");
            }
            path = std::filesystem::canonical(created);
            ::chmod(path.c_str(), 0700);
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    std::string Request(const std::filesystem::path &endpoint, const std::string &command)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            throw std::runtime_error("socket failed");
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const std::string path = endpoint.string();
        Require(path.size() < sizeof(address.sun_path), "test endpoint path too long");
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
        {
            ::close(fd);
            throw std::runtime_error(std::string("connect failed: ") + std::strerror(errno));
        }
        size_t sent = 0;
        while (sent < command.size())
        {
            const ssize_t count = ::send(fd, command.data() + sent, command.size() - sent, 0);
            if (count <= 0)
            {
                ::close(fd);
                throw std::runtime_error("send failed");
            }
            sent += static_cast<size_t>(count);
        }
        ::shutdown(fd, SHUT_WR);
        std::string reply;
        char buffer[1024];
        while (true)
        {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count == 0)
            {
                break;
            }
            if (count < 0)
            {
                ::close(fd);
                throw std::runtime_error("recv failed");
            }
            reply.append(buffer, static_cast<size_t>(count));
        }
        ::close(fd);
        return reply;
    }

    bool HasError(const std::string &reply)
    {
        return reply.rfind("IMHOTEP_SNAPSHOT/1 ERROR ", 0) == 0;
    }

    void TestMainThreadHandoff()
    {
        TemporaryDirectory directory;
        const auto endpoint = directory.path / "editor.sock";
        EditorSnapshotIpc bridge;
        std::string error;
        Require(bridge.Start(endpoint.string(), error), "bridge start failed: " + error);
        struct stat info{};
        Require(::lstat(endpoint.c_str(), &info) == 0 && S_ISSOCK(info.st_mode), "private socket was not created");
        Require((info.st_mode & 0077) == 0, "socket permissions are not owner-only");

        auto call = std::async(std::launch::async, [&]() { return Request(endpoint, "snapshot\n"); });
        const auto ownerThread = std::this_thread::get_id();
        bool providerOnOwner = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (call.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < deadline)
        {
            bridge.ProcessPending([&](SceneSession::AuthoredSnapshot &snapshot, std::string &)
            {
                providerOnOwner = std::this_thread::get_id() == ownerThread;
                snapshot.documentIdentity = "authored-scene-id";
                snapshot.path = "/project/res/scenes/test.yaml";
                snapshot.revision = 9;
                snapshot.dirty = true;
                snapshot.scene = YAML::Load("name: TestScene\nscene: {objects: []}\n");
                return true;
            });
        }
        const std::string reply = call.get();
        Require(providerOnOwner, "snapshot provider did not run on owner thread");
        Require(reply.rfind("IMHOTEP_SNAPSHOT/1 OK 17 9 1 ", 0) == 0, "snapshot metadata framing is invalid");
        Require(reply.find("authored-scene-id/project/res/scenes/test.yaml") != std::string::npos,
                "snapshot identity/path not present");
        Require(reply.find("name: TestScene") != std::string::npos, "serialized scene payload is missing");
        bridge.Stop();
        Require(!std::filesystem::exists(endpoint), "owned socket endpoint was not removed on stop");
    }

    void TestInvalidAndStoppedRequests()
    {
        TemporaryDirectory directory;
        const auto endpoint = directory.path / "editor.sock";
        EditorSnapshotIpc bridge;
        std::string error;
        Require(bridge.Start(endpoint.string(), error), "bridge start failed: " + error);
        auto malformed = std::async(std::launch::async, [&]() { return Request(endpoint, "run-lua\n"); });
        const auto malformedReply = malformed.get();
        Require(HasError(malformedReply), "malformed command did not return an error");

        auto stopped = std::async(std::launch::async, [&]() { return Request(endpoint, "snapshot\n"); });
        while (stopped.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
        {
            bridge.ProcessPending([](SceneSession::AuthoredSnapshot &, std::string &message)
            {
                message = "Editor is stopped or has no scene open.";
                return false;
            });
        }
        Require(HasError(stopped.get()), "stopped Editor provider did not return an error");
        bridge.Stop();
    }

    void TestTimeoutAndOversized()
    {
        TemporaryDirectory directory;
        const auto endpoint = directory.path / "editor.sock";
        EditorSnapshotIpc bridge;
        std::string error;
        Require(bridge.Start(endpoint.string(), error), "bridge start failed: " + error);
        auto timeout = std::async(std::launch::async, [&]() { return Request(endpoint, "snapshot\n"); });
        Require(HasError(timeout.get()), "request without owner-loop dispatch did not time out");
        auto oversized = std::async(std::launch::async, [&]() { return Request(endpoint, std::string(80, 'x')); });
        Require(HasError(oversized.get()), "oversized request was not rejected");
        bridge.Stop();
    }

    void TestInvalidEndpointPaths()
    {
        TemporaryDirectory directory;
        EditorSnapshotIpc bridge;
        std::string error;
        Require(!bridge.Start("relative.sock", error), "relative socket endpoint was accepted");
        Require(!bridge.Start((directory.path / ".." / "traversal.sock").string(), error),
                "non-normalized socket path was accepted");
        const auto unsafe = directory.path / "unsafe";
        std::filesystem::create_directory(unsafe);
        ::chmod(unsafe.c_str(), 0755);
        Require(!bridge.Start((unsafe / "editor.sock").string(), error), "shared endpoint directory was accepted");
        const auto symlinkParent = directory.path / "linked";
        std::filesystem::create_directory_symlink(directory.path, symlinkParent);
        Require(!bridge.Start((symlinkParent / "editor.sock").string(), error), "symlink parent was accepted");
        const auto existing = directory.path / "existing.sock";
        std::ofstream(existing) << "preserve";
        Require(!bridge.Start(existing.string(), error), "existing endpoint collision was accepted");
    }

    void TestShutdownPendingRequest()
    {
        TemporaryDirectory directory;
        EditorSnapshotIpc bridge;
        std::string error;
        const auto endpoint = directory.path / "pending.sock";
        Require(bridge.Start(endpoint.string(), error), "bridge start failed: " + error);
        auto pending = std::async(std::launch::async, [&]() { return Request(endpoint, "snapshot\n"); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (bridge.PendingRequestCount() == 0 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Require(bridge.PendingRequestCount() == 1, "request was not queued before shutdown");
        bridge.Stop();
        Require(HasError(pending.get()), "shutdown did not fail the pending request");
        Require(!std::filesystem::exists(endpoint), "shutdown left its endpoint behind");
    }
#endif
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    std::cout << "Editor snapshot IPC tests skipped on Windows (Unix sockets are not supported).\n";
    return 0;
#else
    try
    {
        if (argc > 1 && std::string(argv[1]) == "--paths")
        {
            TestInvalidEndpointPaths();
            std::cout << "Editor snapshot IPC endpoint validation tests passed\n";
            return 0;
        }
        TestMainThreadHandoff();
        TestInvalidAndStoppedRequests();
        TestTimeoutAndOversized();
        TestInvalidEndpointPaths();
        TestShutdownPendingRequest();
        std::cout << "Editor snapshot IPC tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Editor snapshot IPC test failed: " << exception.what() << '\n';
        return 1;
    }
#endif
}
