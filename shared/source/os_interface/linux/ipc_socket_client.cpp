/*
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/ipc_socket_client.h"

#include "shared/source/debug_settings/debug_settings_manager.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace NEO {

IpcSocketClient::IpcSocketClient() = default;

IpcSocketClient::~IpcSocketClient() {
    disconnect();
}

bool IpcSocketClient::connectToServer(const std::string &socketPath) {
    if (isConnected()) {
        return true;
    }

    // Create socket
    clientSocket = NEO::SysCalls::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (clientSocket == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketClient: Failed to create socket: %s\n", strerror(errno));
        return false;
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = socketTimeout / 1000;
    timeout.tv_usec = (socketTimeout % 1000) * 1000;
    
    NEO::SysCalls::setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    NEO::SysCalls::setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect to server
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (NEO::SysCalls::connect(clientSocket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketClient: Failed to connect to server at %s: %s\n", 
                          socketPath.c_str(), strerror(errno));
        NEO::SysCalls::close(clientSocket);
        clientSocket = -1;
        return false;
    }

    serverSocketPath = socketPath;
    PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                      "IpcSocketClient: Connected to server at %s\n", socketPath.c_str());
    return true;
}

void IpcSocketClient::disconnect() {
    if (clientSocket != -1) {
        NEO::SysCalls::close(clientSocket);
        clientSocket = -1;
        serverSocketPath.clear();
    }
}

bool IpcSocketClient::registerHandle(uint64_t handleId, int fd, uint32_t processId, 
                                    uint8_t memoryType, uint64_t poolOffset) {
    if (!isConnected()) {
        return false;
    }

    // Prepare message
    IpcSocketMessage msg;
    msg.type = IpcSocketMessageType::registerHandle;
    msg.processId = processId;
    msg.handleId = handleId;
    msg.payloadSize = sizeof(IpcSocketRegisterPayload);

    IpcSocketRegisterPayload payload;
    payload.memoryType = memoryType;
    payload.poolOffset = poolOffset;

    // Send message and file descriptor
    if (!sendMessage(msg, &payload)) {
        return false;
    }

    if (!sendFileDescriptor(fd, nullptr, 0)) {
        return false;
    }

    // Receive response
    IpcSocketMessage response;
    IpcSocketResponsePayload responsePayload;
    if (!receiveMessage(response, &responsePayload, sizeof(responsePayload))) {
        return false;
    }

    if (response.type != IpcSocketMessageType::responseHandle || 
        response.handleId != handleId) {
        return false;
    }

    return responsePayload.success;
}

int IpcSocketClient::requestHandle(uint64_t handleId, uint32_t targetProcessId) {
    if (!isConnected()) {
        return -1;
    }

    // Prepare request message
    IpcSocketMessage msg;
    msg.type = IpcSocketMessageType::requestHandle;
    msg.processId = getpid();
    msg.handleId = handleId;
    msg.payloadSize = 0;

    // Send request
    if (!sendMessage(msg)) {
        return -1;
    }

    // Receive response with file descriptor
    IpcSocketResponsePayload responsePayload;
    int receivedFd = receiveFileDescriptor(&responsePayload, sizeof(responsePayload));
    
    if (receivedFd == -1) {
        // Try to receive regular response message
        IpcSocketMessage response;
        if (receiveMessage(response, &responsePayload, sizeof(responsePayload))) {
            if (response.type == IpcSocketMessageType::responseHandle &&
                response.handleId == handleId && !responsePayload.success) {
                PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                                  "IpcSocketClient: Server reported failure for handle %lu\n", handleId);
            }
        }
        return -1;
    }

    if (!responsePayload.success) {
        NEO::SysCalls::close(receivedFd);
        return -1;
    }

    PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                      "IpcSocketClient: Received handle %lu as fd %d\n", handleId, receivedFd);
    return receivedFd;
}

bool IpcSocketClient::sendFileDescriptor(int fd, const void *data, size_t dataSize) {
    struct msghdr msg = {};
    struct iovec iov;
    char cmsgBuffer[CMSG_SPACE(sizeof(int))];

    // Setup data payload if provided
    if (data && dataSize > 0) {
        iov.iov_base = const_cast<void *>(data);
        iov.iov_len = dataSize;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
    }

    // Setup control message for file descriptor
    msg.msg_control = cmsgBuffer;
    msg.msg_controllen = sizeof(cmsgBuffer);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *reinterpret_cast<int *>(CMSG_DATA(cmsg)) = fd;

    return NEO::SysCalls::sendmsg(clientSocket, &msg, 0) != -1;
}

int IpcSocketClient::receiveFileDescriptor(void *data, size_t dataSize) {
    struct msghdr msg = {};
    struct iovec iov;
    char cmsgBuffer[CMSG_SPACE(sizeof(int))];

    // Setup data reception if buffer provided
    if (data && dataSize > 0) {
        iov.iov_base = data;
        iov.iov_len = dataSize;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
    }

    msg.msg_control = cmsgBuffer;
    msg.msg_controllen = sizeof(cmsgBuffer);

    if (NEO::SysCalls::recvmsg(clientSocket, &msg, 0) == -1) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketClient: Failed to receive message: %s\n", strerror(errno));
        return -1;
    }

    // Extract file descriptor from control message
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *reinterpret_cast<int *>(CMSG_DATA(cmsg));
    }

    return -1;
}

bool IpcSocketClient::sendMessage(const IpcSocketMessage &msg, const void *payload) {
    // Send header
    if (NEO::SysCalls::send(clientSocket, &msg, sizeof(msg), 0) != sizeof(msg)) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketClient: Failed to send message header: %s\n", strerror(errno));
        return false;
    }

    // Send payload if present
    if (payload && msg.payloadSize > 0) {
        if (NEO::SysCalls::send(clientSocket, payload, msg.payloadSize, 0) != static_cast<ssize_t>(msg.payloadSize)) {
            PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                              "IpcSocketClient: Failed to send message payload: %s\n", strerror(errno));
            return false;
        }
    }

    return true;
}

bool IpcSocketClient::receiveMessage(IpcSocketMessage &msg, void *payload, size_t maxPayloadSize) {
    // Receive header
    if (NEO::SysCalls::recv(clientSocket, &msg, sizeof(msg), MSG_WAITALL) != sizeof(msg)) {
        PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                          "IpcSocketClient: Failed to receive message header: %s\n", strerror(errno));
        return false;
    }

    // Receive payload if present
    if (payload && msg.payloadSize > 0 && maxPayloadSize >= msg.payloadSize) {
        if (NEO::SysCalls::recv(clientSocket, payload, msg.payloadSize, MSG_WAITALL) != static_cast<ssize_t>(msg.payloadSize)) {
            PRINT_DEBUG_STRING(debugManager.flags.PrintDebugMessages.get(), stderr,
                              "IpcSocketClient: Failed to receive message payload: %s\n", strerror(errno));
            return false;
        }
    }

    return true;
}

} // namespace NEO