# Unix Socket IPC Fallback Implementation

## Overview

This implementation provides a Unix socket-based fallback mechanism for IPC handle sharing when `pidfd_getfd` is unavailable due to kernel version limitations or permission issues. The fallback automatically triggers when the primary `pidfd_getfd` approach fails.

## Key Components

### 1. IpcSocketServer (`shared/source/os_interface/linux/ipc_socket_server.h/.cpp`)
- **Purpose**: Server thread that manages Unix domain socket connections for IPC handle sharing
- **Key Features**:
  - Thread-safe handle registration and retrieval
  - File descriptor passing using SCM_RIGHTS
  - Automatic cleanup and timeout handling
  - Support for multiple concurrent clients

### 2. IpcSocketClient (`shared/source/os_interface/linux/ipc_socket_client.h/.cpp`)
- **Purpose**: Client-side interface for communicating with the socket server
- **Key Features**:
  - Connection management to socket server
  - Handle request and response processing
  - File descriptor reception via SCM_RIGHTS

### 3. Integration Points

#### DriverHandleImp
- Added socket server management methods:
  - `initializeIpcSocketServer()`: Initialize server on demand
  - `getIpcSocketServerPath()`: Get server socket path
  - `registerIpcHandleWithServer()`: Register handles with server
  - `shutdownIpcSocketServer()`: Clean shutdown

#### Context Implementation
- **context_imp_drm.cpp**: Modified `getMemHandlePtr()` to implement fallback logic for Linux-only builds
- **context_imp_drm_or_wddm.cpp**: Modified both `isOpaqueHandleSupported()` and `getMemHandlePtr()` for cross-platform builds with Linux-specific socket fallback
- **context_imp.h**: Modified `setIPCHandleData()` to register handles with socket server

## Protocol Design

### Message Types
1. **REGISTER_HANDLE**: Register a file descriptor with the server
2. **REQUEST_HANDLE**: Request a file descriptor from the server  
3. **RESPONSE_HANDLE**: Server response with success/failure
4. **ERROR**: Error response

### Data Structures
```cpp
struct IpcSocketMessage {
    IpcSocketMessageType type;
    uint32_t processId;
    uint64_t handleId;
    uint32_t payloadSize;
};

struct IpcOpaqueMemoryData {
    union IpcHandle {
        int fd;
        uint64_t reserved;
    };
    IpcHandle handle;
    uint64_t poolOffset;
    unsigned int processId;
    IpcHandleType type;
    uint8_t memoryType;
};
```

## Fallback Logic

### Opaque Handle Support Detection
The `isOpaqueHandleSupported()` function now intelligently determines support:

1. **Force Socket Fallback**: If `ForceIpcSocketFallback=1`, immediately enable opaque handles
2. **Try pidfd First**: Attempt `prctl(PR_SET_PTRACER)` for pidfd support
3. **Graceful Degradation**: If prctl fails but `EnableIpcSocketFallback=1`, still enable opaque handles
4. **Disable Only When Necessary**: Only disable opaque handles if both pidfd and socket fallback are unavailable

### Handle Registration
1. When opaque IPC handles are created, they are automatically registered with the socket server
2. Server maintains a map of `handleId -> IpcHandleEntry` with reference counting
3. File descriptors are duplicated for safe ownership transfer

### Handle Retrieval  
1. **Primary**: Try `pidfd_getfd` approach first
2. **Fallback**: If pidfd fails and socket fallback is enabled:
   - Connect to socket server using stored socket path
   - Send REQUEST_HANDLE message with handle ID and target process ID
   - Receive file descriptor via SCM_RIGHTS
   - Use received FD for memory import

This ensures maximum compatibility - opaque handles work even in restricted environments where `prctl` fails, as long as Unix sockets are available.

## Configuration

### Debug Variables
- `ForceIpcSocketFallback`: Force using socket fallback instead of pidfd
- `EnableIpcSocketFallback`: Enable socket fallback when pidfd fails (default: true)
- `IpcSocketServerTimeout`: Timeout for socket operations (default: 5000ms)

### Usage Example
```bash
# Force socket fallback for testing
export NEO_ForceIpcSocketFallback=1

# Disable socket fallback
export NEO_EnableIpcSocketFallback=0
```

## Benefits

1. **Backwards Compatibility**: Works on older kernels without pidfd support
2. **Permission Resilience**: Works when ptrace permissions are restricted  
3. **Transparent Fallback**: Automatic fallback with no API changes required
4. **Performance**: Minimal overhead when pidfd works, efficient socket protocol
5. **Security**: Process-based access control and proper resource cleanup

## Testing

Comprehensive unit tests cover:
- Basic server/client functionality
- Handle registration and retrieval
- Multi-client scenarios
- Error conditions and edge cases
- Performance characteristics
- Debug flag behavior

## Thread Safety

- Server uses mutex protection for handle map operations
- Client connections are independent and thread-safe
- Proper resource cleanup on shutdown
- Reference counting for shared handles

## File Descriptor Management

- Server duplicates FDs to ensure proper ownership
- Automatic cleanup on process termination
- Reference counting prevents premature cleanup
- SCM_RIGHTS ensures secure FD transfer

This implementation provides a robust, secure, and efficient fallback mechanism that maintains compatibility with existing Level Zero applications while extending support to environments where pidfd is unavailable.