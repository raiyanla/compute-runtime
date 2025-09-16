/*
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/ipc_socket_server.h"
#include "shared/source/os_interface/linux/ipc_socket_client.h"
#include "shared/source/os_interface/linux/sys_calls.h"
#include "shared/test/common/mocks/mock_device.h"

#include "level_zero/core/test/unit_tests/fixtures/device_fixture.h"
#include "level_zero/core/test/unit_tests/mocks/mock_context.h"
#include "level_zero/core/test/unit_tests/mocks/mock_driver.h"

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <unistd.h>

namespace L0 {
namespace ult {

class IpcSocketFallbackTest : public Test<DeviceFixture> {
  public:
    void SetUp() override {
        DeviceFixture::setUp();
        
        context = std::make_unique<ContextImp>(driverHandle.get());
        ASSERT_NE(context, nullptr);
        
        auto ret = context->getDevices().insert(std::make_pair(device->getRootDeviceIndex(), device->toHandle()));
        ASSERT_TRUE(ret.second);
        context->getDeviceHandles().push_back(device->toHandle());
        context->numDevices = 1;
    }

    void TearDown() override {
        context.reset();
        DeviceFixture::tearDown();
    }

    std::unique_ptr<ContextImp> context;
};

TEST_F(IpcSocketFallbackTest, GivenIpcSocketServerWhenInitializeThenSuccess) {
    NEO::IpcSocketServer server;
    
    EXPECT_TRUE(server.initialize());
    EXPECT_TRUE(server.isRunning());
    EXPECT_FALSE(server.getSocketPath().empty());
    
    server.shutdown();
    EXPECT_FALSE(server.isRunning());
}

TEST_F(IpcSocketFallbackTest, GivenIpcSocketServerWhenRegisterHandleThenSuccess) {
    NEO::IpcSocketServer server;
    ASSERT_TRUE(server.initialize());
    
    uint64_t handleId = 12345;
    int fd = 1; // stdin for test
    uint32_t processId = NEO::SysCalls::getpid();
    uint8_t memoryType = 1;
    uint64_t poolOffset = 0;
    
    EXPECT_TRUE(server.registerHandle(handleId, fd, processId, memoryType, poolOffset));
    
    // Try to register same handle again (should increment refcount)
    EXPECT_TRUE(server.registerHandle(handleId, fd, processId, memoryType, poolOffset));
    
    EXPECT_TRUE(server.unregisterHandle(handleId));
    EXPECT_TRUE(server.unregisterHandle(handleId)); // Second unregister should still succeed
    EXPECT_FALSE(server.unregisterHandle(handleId)); // Third should fail (no more references)
    
    server.shutdown();
}

TEST_F(IpcSocketFallbackTest, GivenIpcSocketClientWhenConnectToServerThenSuccess) {
    NEO::IpcSocketServer server;
    ASSERT_TRUE(server.initialize());
    
    NEO::IpcSocketClient client;
    EXPECT_TRUE(client.connectToServer(server.getSocketPath()));
    EXPECT_TRUE(client.isConnected());
    
    client.disconnect();
    EXPECT_FALSE(client.isConnected());
    
    server.shutdown();
}

TEST_F(IpcSocketFallbackTest, GivenIpcSocketClientWhenConnectToInvalidServerThenFail) {
    NEO::IpcSocketClient client;
    EXPECT_FALSE(client.connectToServer("/tmp/nonexistent_socket"));
    EXPECT_FALSE(client.isConnected());
}

TEST_F(IpcSocketFallbackTest, GivenIpcSocketWhenRegisterAndRequestHandleThenSuccess) {
    NEO::IpcSocketServer server;
    ASSERT_TRUE(server.initialize());
    
    uint64_t handleId = 54321;
    int pipefd[2];
    ASSERT_EQ(0, NEO::SysCalls::NEO::SysCalls::pipe(pipefd));
    
    uint32_t processId = NEO::SysCalls::getpid();
    uint8_t memoryType = 2;
    uint64_t poolOffset = 1024;
    
    // Register handle with server directly
    EXPECT_TRUE(server.registerHandle(handleId, pipefd[1], processId, memoryType, poolOffset));
    
    // Request handle via client
    NEO::IpcSocketClient client;
    ASSERT_TRUE(client.connectToServer(server.getSocketPath()));
    
    int receivedFd = client.requestHandle(handleId, processId);
    EXPECT_NE(-1, receivedFd);
    
    // Verify we can use the received fd
    char testData[] = "test";
    EXPECT_EQ(4, NEO::SysCalls::write(pipefd[1], testData, 4));
    
    char readBuffer[10] = {};
    EXPECT_EQ(4, NEO::SysCalls::read(receivedFd, readBuffer, 4));
    EXPECT_STREQ(testData, readBuffer);
    
    NEO::SysCalls::close(pipefd[0]);
    NEO::SysCalls::close(pipefd[1]);
    NEO::SysCalls::close(receivedFd);
    
    server.shutdown();
}

TEST_F(IpcSocketFallbackTest, GivenIpcSocketWhenRequestNonexistentHandleThenFail) {
    NEO::IpcSocketServer server;
    ASSERT_TRUE(server.initialize());
    
    NEO::IpcSocketClient client;
    ASSERT_TRUE(client.connectToServer(server.getSocketPath()));
    
    uint64_t nonexistentHandle = 99999;
    uint32_t processId = NEO::SysCalls::getpid();
    
    int receivedFd = client.requestHandle(nonexistentHandle, processId);
    EXPECT_EQ(-1, receivedFd);
    
    server.shutdown();
}

TEST_F(IpcSocketFallbackTest, GivenMultipleClientsWhenAccessServerThenAllSucceed) {
    NEO::IpcSocketServer server;
    ASSERT_TRUE(server.initialize());
    
    std::vector<std::thread> clientThreads;
    std::atomic<int> successCount{0};
    
    for (int i = 0; i < 5; ++i) {
        clientThreads.emplace_back([&server, &successCount, i]() {
            NEO::IpcSocketClient client;
            if (client.connectToServer(server.getSocketPath())) {
                successCount++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    
    for (auto &thread : clientThreads) {
        thread.join();
    }
    
    EXPECT_EQ(5, successCount.load());
    
    server.shutdown();
}

TEST_F(IpcSocketFallbackTest, GivenDebugFlagsWhenSocketFallbackForcedThenUsesSocket) {
    // Save original debug flag value
    auto originalForceSocketFallback = NEO::debugManager.flags.ForceIpcSocketFallback.get();
    auto originalEnableSocketFallback = NEO::debugManager.flags.EnableIpcSocketFallback.get();
    
    // Force socket fallback
    NEO::debugManager.flags.ForceIpcSocketFallback.set(true);
    NEO::debugManager.flags.EnableIpcSocketFallback.set(true);
    
    // Initialize driver handle with socket server
    auto driverHandleImp = static_cast<DriverHandleImp *>(driverHandle.get());
    EXPECT_TRUE(driverHandleImp->initializeIpcSocketServer());
    EXPECT_FALSE(driverHandleImp->getIpcSocketServerPath().empty());
    
    // Restore original debug flag values
    NEO::debugManager.flags.ForceIpcSocketFallback.set(originalForceSocketFallback);
    NEO::debugManager.flags.EnableIpcSocketFallback.set(originalEnableSocketFallback);
    
    driverHandleImp->shutdownIpcSocketServer();
}

TEST_F(IpcSocketFallbackTest, GivenIpcSocketWhenServerShutdownThenClientsDisconnect) {
    auto server = std::make_unique<NEO::IpcSocketServer>();
    ASSERT_TRUE(server->initialize());
    
    std::string socketPath = server->getSocketPath();
    
    NEO::IpcSocketClient client;
    ASSERT_TRUE(client.connectToServer(socketPath));
    EXPECT_TRUE(client.isConnected());
    
    // Shutdown server
    server->shutdown();
    server.reset();
    
    // Client should fail to communicate after server shutdown
    uint64_t handleId = 12345;
    uint32_t processId = NEO::SysCalls::getpid();
    int receivedFd = client.requestHandle(handleId, processId);
    EXPECT_EQ(-1, receivedFd);
}

class IpcSocketPerformanceTest : public IpcSocketFallbackTest {
  protected:
    static constexpr int numOperations = 100;
    static constexpr int numHandles = 10;
};

TEST_F(IpcSocketPerformanceTest, GivenIpcSocketWhenManyOperationsThenReasonablePerformance) {
    NEO::IpcSocketServer server;
    ASSERT_TRUE(server.initialize());
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Register multiple handles
    for (int i = 0; i < numHandles; ++i) {
        uint64_t handleId = 1000 + i;
        int pipefd[2];
        ASSERT_EQ(0, NEO::SysCalls::pipe(pipefd));
        
        EXPECT_TRUE(server.registerHandle(handleId, pipefd[1], NEO::SysCalls::getpid(), 1, 0));
        NEO::SysCalls::close(pipefd[0]);
        NEO::SysCalls::close(pipefd[1]);
    }
    
    // Perform multiple client operations
    NEO::IpcSocketClient client;
    ASSERT_TRUE(client.connectToServer(server.getSocketPath()));
    
    int successfulRequests = 0;
    for (int i = 0; i < numOperations; ++i) {
        uint64_t handleId = 1000 + (i % numHandles);
        int receivedFd = client.requestHandle(handleId, NEO::SysCalls::getpid());
        if (receivedFd != -1) {
            successfulRequests++;
            NEO::SysCalls::close(receivedFd);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_GT(successfulRequests, numOperations / 2); // At least 50% success rate
    EXPECT_LT(duration.count(), 5000); // Should complete within 5 seconds
    
    server.shutdown();
}

} // namespace ult
} // namespace L0