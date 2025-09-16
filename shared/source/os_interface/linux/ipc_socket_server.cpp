/*
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/ipc_socket_server.h"

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/os_interface/linux/sys_calls.h"

#include <chrono>
#include <cstring>
#include <errno.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace NEO {

IpcSocketServer::IpcSocketServer() {
    // Create unique socket path using process ID and timestamp
    auto pid = getpid();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
    socketPath = "/tmp/neo_ipc_" + std::to_string(pid) + "_" + std::to_string(timestamp);
}

IpcSocketServer::~IpcSocketServer() {
    shutdown();
}

bool IpcSocketServer::initialize() {
    if (serverRunning.load()) {
        return true;
    }

    // Create Unix domain socket
    serverSocket = SysCalls::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (serverSocket == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr, 
                          "IpcSocketServer: Failed to create socket: %s\n", strerror(errno));
        return false;
    }

    // Bind to socket path
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (SysCalls::bind(serverSocket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketServer: Failed to bind socket to %s: %s\n", 
                          socketPath.c_str(), strerror(errno));
        SysCalls::close(serverSocket);
        serverSocket = -1;
        return false;
    }

    // Listen for connections
    if (SysCalls::listen(serverSocket, maxConnections) == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketServer: Failed to listen on socket: %s\n", strerror(errno));
        SysCalls::close(serverSocket);
        serverSocket = -1;
        unlink(socketPath.c_str());
        return false;
    }

    // Start server thread
    serverRunning.store(true);
    shutdownRequested.store(false);
    
    serverThread = Thread::createFunc(serverThreadEntry, this);
    if (!serverThread) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketServer: Failed to create server thread\n");
        serverRunning.store(false);
        SysCalls::close(serverSocket);
        serverSocket = -1;
        unlink(socketPath.c_str());
        return false;
    }

    PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                      "IpcSocketServer: Started server on %s\n", socketPath.c_str());
    return true;
}

void IpcSocketServer::shutdown() {
    if (!serverRunning.load()) {
        return;
    }

    shutdownRequested.store(true);
    
    if (serverThread) {
        serverThread->join();
        serverThread.reset();
    }

    if (serverSocket != -1) {
        SysCalls::close(serverSocket);
        serverSocket = -1;
    }

    unlink(socketPath.c_str());
    
    // Clean up handle map
    std::lock_guard<std::mutex> lock(handleMapMutex);
    for (auto &entry : handleMap) {
        if (entry.second.fileDescriptor != -1) {
            SysCalls::close(entry.second.fileDescriptor);
        }
    }
    handleMap.clear();
    
    serverRunning.store(false);
    PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                      "IpcSocketServer: Server shutdown complete\n");
}

bool IpcSocketServer::registerHandle(uint64_t handleId, int fd, uint32_t processId, 
                                    uint8_t memoryType, uint64_t poolOffset) {
    std::lock_guard<std::mutex> lock(handleMapMutex);
    
    auto it = handleMap.find(handleId);
    if (it != handleMap.end()) {
        // Handle already exists, increment reference count
        it->second.refCount++;
        return true;
    }

    // Duplicate the file descriptor to ensure we own it
    int dupFd = SysCalls::dup(fd);
    if (dupFd == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketServer: Failed to duplicate fd %d: %s\n", fd, strerror(errno));
        return false;
    }

    IpcHandleEntry entry;
    entry.fileDescriptor = dupFd;
    entry.processId = processId;
    entry.memoryType = memoryType;
    entry.poolOffset = poolOffset;
    entry.refCount = 1;

    handleMap[handleId] = entry;
    
    PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                      "IpcSocketServer: Registered handle %lu with fd %d\n", handleId, dupFd);
    return true;
}

bool IpcSocketServer::unregisterHandle(uint64_t handleId) {
    std::lock_guard<std::mutex> lock(handleMapMutex);
    
    auto it = handleMap.find(handleId);
    if (it == handleMap.end()) {
        return false;
    }

    it->second.refCount--;
    if (it->second.refCount == 0) {
        if (it->second.fileDescriptor != -1) {
            SysCalls::close(it->second.fileDescriptor);
        }
        handleMap.erase(it);
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketServer: Unregistered handle %lu\n", handleId);
    }
    
    return true;
}

void *IpcSocketServer::serverThreadEntry(void *arg) {
    static_cast<IpcSocketServer *>(arg)->serverThreadRun();
    return nullptr;
}

void IpcSocketServer::serverThreadRun() {
    while (!shutdownRequested.load()) {
        struct pollfd pfd = {};
        pfd.fd = serverSocket;
        pfd.events = POLLIN;

        int pollResult = SysCalls::poll(&pfd, 1, 1000); // 1 second timeout
        if (pollResult == -1) {
            if (errno != EINTR) {
                PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                                  "IpcSocketServer: Poll error: %s\n", strerror(errno));
                break;
            }
            continue;
        }

        if (pollResult == 0) {
            // Timeout, continue
            continue;
        }

        if (pfd.revents & POLLIN) {
            int clientSocket = SysCalls::accept(serverSocket, nullptr, nullptr);
            if (clientSocket == -1) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                                      "IpcSocketServer: Accept error: %s\n", strerror(errno));
                }
                continue;
            }

            // Set socket timeout
            struct timeval timeout;
            timeout.tv_sec = socketTimeout / 1000;
            timeout.tv_usec = (socketTimeout % 1000) * 1000;
            
            SysCalls::setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            SysCalls::setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            handleClientConnection(clientSocket);
            SysCalls::close(clientSocket);
        }
    }
}

bool IpcSocketServer::handleClientConnection(int clientSocket) {
    IpcSocketMessage msg;
    if (!receiveMessage(clientSocket, msg)) {
        return false;
    }

    switch (msg.type) {
    case IpcSocketMessageType::requestHandle:
        return processRequestMessage(clientSocket, msg);
    case IpcSocketMessageType::registerHandle:
        return processRegisterMessage(clientSocket, msg);
    default:
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketServer: Unknown message type %u\n", static_cast<uint32_t>(msg.type));
        return false;
    }
}

bool IpcSocketServer::processRegisterMessage(int clientSocket, const IpcSocketMessage &msg) {
    if (msg.payloadSize != sizeof(IpcSocketRegisterPayload)) {
        return false;
    }

    IpcSocketRegisterPayload payload;
    if (!receiveMessage(clientSocket, const_cast<IpcSocketMessage &>(msg), &payload, sizeof(payload))) {
        return false;
    }

    // For register messages, we expect the file descriptor to be sent with the payload
    int receivedFd = receiveFileDescriptor(clientSocket, nullptr, 0);
    if (receivedFd == -1) {
        return false;
    }

    bool success = registerHandle(msg.handleId, receivedFd, msg.processId, 
                                 payload.memoryType, payload.poolOffset);
    SysCalls::close(receivedFd); // We duplicated it in registerHandle

    // Send response
    IpcSocketMessage response;
    response.type = IpcSocketMessageType::responseHandle;
    response.processId = getpid();
    response.handleId = msg.handleId;
    response.payloadSize = sizeof(IpcSocketResponsePayload);

    IpcSocketResponsePayload responsePayload;
    responsePayload.success = success;

    return sendMessage(clientSocket, response, &responsePayload);
}

bool IpcSocketServer::processRequestMessage(int clientSocket, const IpcSocketMessage &msg) {
    std::lock_guard<std::mutex> lock(handleMapMutex);
    
    auto it = handleMap.find(msg.handleId);
    bool success = (it != handleMap.end() && it->second.fileDescriptor != -1);

    IpcSocketMessage response;
    response.type = IpcSocketMessageType::responseHandle;
    response.processId = getpid();
    response.handleId = msg.handleId;
    response.payloadSize = sizeof(IpcSocketResponsePayload);

    IpcSocketResponsePayload responsePayload;
    responsePayload.success = success;

    if (success) {
        return sendFileDescriptor(clientSocket, it->second.fileDescriptor, &responsePayload, sizeof(responsePayload));
    } else {
        return sendMessage(clientSocket, response, &responsePayload);
    }
}

bool IpcSocketServer::sendFileDescriptor(int socket, int fd, const void *data, size_t dataSize) {
    struct msghdr msg = {};
    struct iovec iov;
    char cmsgBuffer[CMSG_SPACE(sizeof(int))];

    iov.iov_base = const_cast<void *>(data);
    iov.iov_len = dataSize;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgBuffer;
    msg.msg_controllen = sizeof(cmsgBuffer);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *reinterpret_cast<int *>(CMSG_DATA(cmsg)) = fd;

    return SysCalls::sendmsg(socket, &msg, 0) != -1;
}

int IpcSocketServer::receiveFileDescriptor(int socket, void *data, size_t dataSize) {
    struct msghdr msg = {};
    struct iovec iov;
    char cmsgBuffer[CMSG_SPACE(sizeof(int))];

    if (data && dataSize > 0) {
        iov.iov_base = data;
        iov.iov_len = dataSize;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
    }

    msg.msg_control = cmsgBuffer;
    msg.msg_controllen = sizeof(cmsgBuffer);

    if (SysCalls::recvmsg(socket, &msg, 0) == -1) {
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *reinterpret_cast<int *>(CMSG_DATA(cmsg));
    }

    return -1;
}

bool IpcSocketServer::sendMessage(int socket, const IpcSocketMessage &msg, const void *payload) {
    // Send header
    if (SysCalls::send(socket, &msg, sizeof(msg), 0) != sizeof(msg)) {
        return false;
    }

    // Send payload if present
    if (payload && msg.payloadSize > 0) {
        if (SysCalls::send(socket, payload, msg.payloadSize, 0) != static_cast<ssize_t>(msg.payloadSize)) {
            return false;
        }
    }

    return true;
}

bool IpcSocketServer::receiveMessage(int socket, IpcSocketMessage &msg, void *payload, size_t maxPayloadSize) {
    // Receive header
    if (SysCalls::recv(socket, &msg, sizeof(msg), MSG_WAITALL) != sizeof(msg)) {
        return false;
    }

    // Receive payload if present
    if (payload && msg.payloadSize > 0 && maxPayloadSize >= msg.payloadSize) {
        if (SysCalls::recv(socket, payload, msg.payloadSize, MSG_WAITALL) != static_cast<ssize_t>(msg.payloadSize)) {
            return false;
        }
    }

    return true;
}

} // namespace NEO