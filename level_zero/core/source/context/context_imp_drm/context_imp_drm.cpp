/*
 * Copyright (C) 2022-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/device.h"
#include "shared/source/memory_manager/unified_memory_manager.h"
#include "shared/source/os_interface/linux/sys_calls.h"
#include "shared/source/os_interface/linux/ipc_socket_client.h"

#include "level_zero/core/source/context/context_imp.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/driver/driver_handle_imp.h"

#include <sys/prctl.h>
namespace L0 {

bool ContextImp::isOpaqueHandleSupported(IpcHandleType *handleType) {
    bool useOpaqueHandle = contextSettings.enablePidfdOrSockets;
    *handleType = IpcHandleType::fdHandle;
    
    if (useOpaqueHandle) {
        // Force socket fallback if requested
        if (NEO::debugManager.flags.ForceIpcSocketFallback.get()) {
            PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                              "Forcing IPC socket fallback as requested\n");
            return true;
        }
        
        // Try prctl for pidfd support
        if (NEO::SysCalls::prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) == -1) {
            PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                              "prctl Syscall for PR_SET_PTRACER, PR_SET_PTRACER_ANY failed: %s\n", strerror(errno));
            
            // Check if socket fallback is available
            if (NEO::debugManager.flags.EnableIpcSocketFallback.get()) {
                PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                                  "pidfd unavailable, but socket fallback is enabled - opaque handles still supported\n");
                return true;
            } else {
                PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                                  "pidfd unavailable and socket fallback disabled - disabling opaque handles\n");
                return false;
            }
        }
    }
    return useOpaqueHandle;
}

bool ContextImp::isShareableMemory(const void *exportDesc, bool exportableMemory, NEO::Device *neoDevice, bool shareableWithoutNTHandle) {
    if (exportableMemory) {
        return true;
    }

    return false;
}

void *ContextImp::getMemHandlePtr(ze_device_handle_t hDevice, uint64_t handle, NEO::AllocationType allocationType, unsigned int processId, ze_ipc_memory_flags_t flags) {
    auto neoDevice = Device::fromHandle(hDevice)->getNEODevice();
    bool useOpaqueHandle = contextSettings.enablePidfdOrSockets;
    uint64_t importHandle = handle;
    bool pidfdSuccess = false;

    if (useOpaqueHandle && !NEO::debugManager.flags.ForceIpcSocketFallback.get()) {
        // Try pidfd approach first extract parent pid and target fd before importing handle
        pid_t exporterPid = static_cast<pid_t>(processId);
        unsigned int pidfdFlags = 0u;
        int pidfd = NEO::SysCalls::pidfdopen(exporterPid, pidfdFlags);
        if (pidfd == -1) {
            PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, "pidfd_open Syscall failed: %s\n", strerror(errno));
        } else {
            unsigned int getfdFlags = 0u;
            int newfd = NEO::SysCalls::pidfdgetfd(pidfd, static_cast<int>(handle), getfdFlags);
            NEO::SysCalls::close(pidfd);
            if (newfd < 0) {
                PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, "pidfd_getfd Syscall failed: %s\n", strerror(errno));
            } else {
                importHandle = static_cast<uint64_t>(newfd);
                pidfdSuccess = true;
            }
        }
    }

    // Try socket fallback if pidfd failed or was disabled
    if (useOpaqueHandle && !pidfdSuccess && NEO::debugManager.flags.EnableIpcSocketFallback.get()) {
        auto driverHandleImp = static_cast<DriverHandleImp *>(this->driverHandle);
        std::string socketPath = driverHandleImp->getIpcSocketServerPath();
        
        if (!socketPath.empty()) {
            NEO::IpcSocketClient socketClient;
            if (socketClient.connectToServer(socketPath)) {
                int receivedFd = socketClient.requestHandle(handle, processId);
                if (receivedFd != -1) {
                    importHandle = static_cast<uint64_t>(receivedFd);
                    PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                                      "IPC socket fallback successful for handle %lu\n", handle);
                } else {
                    PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                                      "IPC socket fallback failed for handle %lu\n", handle);
                }
            } else {
                PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                                  "Failed to connect to IPC socket server at %s\n", socketPath.c_str());
            }
        } else {
            PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, 
                              "IPC socket server not available for fallback\n");
        }
    }

    NEO::SvmAllocationData allocDataInternal(neoDevice->getRootDeviceIndex());
    return this->driverHandle->importFdHandle(neoDevice, flags, importHandle, allocationType, nullptr, nullptr, allocDataInternal);
}

void ContextImp::getDataFromIpcHandle(ze_device_handle_t hDevice, const ze_ipc_mem_handle_t ipcHandle, uint64_t &handle, uint8_t &type, unsigned int &processId, uint64_t &poolOffset) {
    bool useOpaqueHandle = contextSettings.enablePidfdOrSockets;

    if (useOpaqueHandle) {
        const IpcOpaqueMemoryData *ipcData = reinterpret_cast<const IpcOpaqueMemoryData *>(ipcHandle.data);
        handle = static_cast<uint64_t>(ipcData->handle.fd);
        type = ipcData->memoryType;
        processId = ipcData->processId;
        poolOffset = ipcData->poolOffset;
    } else {
        const IpcMemoryData *ipcData = reinterpret_cast<const IpcMemoryData *>(ipcHandle.data);
        handle = ipcData->handle;
        type = ipcData->type;
        poolOffset = ipcData->poolOffset;
    }
}

} // namespace L0
