/*
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/os_interface/os_thread.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>

namespace NEO {

// IPC Socket Protocol Messages
enum class IpcSocketMessageType : uint32_t {
    registerHandle = 1,
    requestHandle = 2,
    responseHandle = 3,
    error = 4
};

#pragma pack(1)
struct IpcSocketMessage {
    IpcSocketMessageType type;
    uint32_t processId;
    uint64_t handleId;
    uint32_t payloadSize;
    uint32_t reserved;
};

struct IpcSocketRegisterPayload {
    uint8_t memoryType;
    uint64_t poolOffset;
    uint8_t reserved[7];
};

struct IpcSocketResponsePayload {
    bool success;
    uint8_t reserved[7];
};
#pragma pack()

static_assert(sizeof(IpcSocketMessage) == 24, "IpcSocketMessage size must be 24 bytes");
static_assert(sizeof(IpcSocketRegisterPayload) == 16, "IpcSocketRegisterPayload size must be 16 bytes");
static_assert(sizeof(IpcSocketResponsePayload) == 8, "IpcSocketResponsePayload size must be 8 bytes");

struct IpcHandleEntry {
    int fileDescriptor = -1;
    uint32_t processId = 0;
    uint8_t memoryType = 0;
    uint64_t poolOffset = 0;
    uint64_t refCount = 0;
};

class IpcSocketServer {
  public:
    IpcSocketServer();
    ~IpcSocketServer();

    bool initialize();
    void shutdown();

    bool registerHandle(uint64_t handleId, int fd, uint32_t processId, uint8_t memoryType, uint64_t poolOffset);
    bool unregisterHandle(uint64_t handleId);
    
    std::string getSocketPath() const { return socketPath; }
    bool isRunning() const { return serverRunning.load(); }

  protected:
    static void *serverThreadEntry(void *arg);
    void serverThreadRun();
    
    bool handleClientConnection(int clientSocket);
    bool processRegisterMessage(int clientSocket, const IpcSocketMessage &msg);
    bool processRequestMessage(int clientSocket, const IpcSocketMessage &msg);
    
    bool sendFileDescriptor(int socket, int fd, const void *data, size_t dataSize);
    int receiveFileDescriptor(int socket, void *data, size_t dataSize);
    
    bool sendMessage(int socket, const IpcSocketMessage &msg, const void *payload = nullptr);
    bool receiveMessage(int socket, IpcSocketMessage &msg, void *payload = nullptr, size_t maxPayloadSize = 0);

  private:
    std::atomic<bool> serverRunning{false};
    std::atomic<bool> shutdownRequested{false};
    std::unique_ptr<Thread> serverThread;
    
    int serverSocket = -1;
    std::string socketPath;
    
    std::mutex handleMapMutex;
    std::map<uint64_t, IpcHandleEntry> handleMap;
    
    static constexpr size_t maxConnections = 16;
    static constexpr int socketTimeout = 5000; // 5 seconds
};

} // namespace NEO