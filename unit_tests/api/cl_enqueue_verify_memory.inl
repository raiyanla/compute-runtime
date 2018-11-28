/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "public/cl_ext_private.h"
#include "runtime/aub_mem_dump/aub_services.h"
#include "unit_tests/api/cl_api_tests.h"
#include "unit_tests/mocks/mock_csr.h"
#include "test.h"

using namespace OCLRT;

TEST(CheckVerifyMemoryRelatedApiConstants, givenVerifyMemoryRelatedApiConstantsWhenVerifyingTheirValueThenCorrectValuesAreReturned) {
    EXPECT_EQ(CmdServicesMemTraceMemoryCompare::CompareOperationValues::CompareEqual, CL_MEM_COMPARE_EQUAL);
    EXPECT_EQ(CmdServicesMemTraceMemoryCompare::CompareOperationValues::CompareNotEqual, CL_MEM_COMPARE_NOT_EQUAL);
}

struct clEnqueueVerifyMemorySettings {
    const cl_uint comparisonMode = CL_MEM_COMPARE_EQUAL;
    const size_t bufferSize = 1;
    static constexpr size_t expectedSize = 1;
    int expected[expectedSize];
    // Use any valid pointer as gpu address because non aub tests will not actually validate the memory
    void *gpuAddress = expected;
};

class clEnqueueVerifyMemoryTests : public api_tests,
                                   public clEnqueueVerifyMemorySettings {
};

TEST_F(clEnqueueVerifyMemoryTests, givenSizeOfComparisonEqualZeroWhenCallingVerifyMemoryThenErrorIsReturned) {
    cl_int retval = clEnqueueVerifyMemory(nullptr, nullptr, nullptr, 0, comparisonMode);
    EXPECT_EQ(CL_INVALID_VALUE, retval);
}

TEST_F(clEnqueueVerifyMemoryTests, givenNullExpectedDataWhenCallingVerifyMemoryThenErrorIsReturned) {
    cl_int retval = clEnqueueVerifyMemory(nullptr, nullptr, nullptr, expectedSize, comparisonMode);
    EXPECT_EQ(CL_INVALID_VALUE, retval);
}

TEST_F(clEnqueueVerifyMemoryTests, givenInvalidAllocationPointerWhenCallingVerifyMemoryThenErrorIsReturned) {
    cl_int retval = clEnqueueVerifyMemory(nullptr, nullptr, expected, expectedSize, comparisonMode);
    EXPECT_EQ(CL_INVALID_VALUE, retval);
}

TEST_F(clEnqueueVerifyMemoryTests, givenInvalidCommandQueueWhenCallingVerifyMemoryThenErrorIsReturned) {
    cl_int retval = clEnqueueVerifyMemory(nullptr, gpuAddress, expected, expectedSize, comparisonMode);
    EXPECT_EQ(CL_INVALID_COMMAND_QUEUE, retval);
}

TEST_F(clEnqueueVerifyMemoryTests, givenCommandQueueWithoutAubCsrWhenCallingVerifyMemoryThenErrorIsReturned) {
    cl_int retval = clEnqueueVerifyMemory(pCommandQueue, gpuAddress, expected, expectedSize, comparisonMode);
    EXPECT_EQ(CL_INVALID_VALUE, retval);
}

template <typename GfxFamily>
struct MockCsrWithExpectMemory : MockCsr<GfxFamily> {
    MockCsrWithExpectMemory() = delete;
    MockCsrWithExpectMemory(const HardwareInfo &hwInfoIn) = delete;
    MockCsrWithExpectMemory(int32_t &execStamp, ExecutionEnvironment &executionEnvironment)
        : MockCsr<GfxFamily>(execStamp, executionEnvironment) {
    }
    bool expectMemory(const void *gfxAddress, const void *srcAddress, size_t length, uint32_t compareOperation) override {
        return true;
    }
};

HWTEST_F(clEnqueueVerifyMemoryTests, givenCommandQueueWithMockAubCsrWhenCallingVerifyMemoryThenSuccessIsReturned) {
    std::unique_ptr<MockDevice> mockDevice(MockDevice::createWithNewExecutionEnvironment<MockDevice>(platformDevices[0]));
    int32_t executionStamp = 0;
    auto *mockCsr = new MockCsrWithExpectMemory<FamilyType>(executionStamp, *mockDevice.get()->executionEnvironment);
    mockDevice.get()->resetCommandStreamReceiver(mockCsr);
    CommandQueue cmdQ(nullptr, mockDevice.get(), 0);

    cl_int retval = clEnqueueVerifyMemory(&cmdQ, gpuAddress, expected, expectedSize, comparisonMode);
    EXPECT_EQ(CL_SUCCESS, retval);
}
