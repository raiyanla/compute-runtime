/*
 * Copyright (c) 2017 - 2018, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "runtime/helpers/options.h"
#include "runtime/os_interface/device_factory.h"
#include "runtime/helpers/file_io.h"
#include "unit_tests/os_interface/linux/drm_mock.h"
#include "unit_tests/fixtures/memory_management_fixture.h"
#include "gtest/gtest.h"

#include "runtime/os_interface/os_interface.h"
#include <fstream>

using namespace OCLRT;
using namespace std;

TEST(DrmTest, GetDeviceID) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    pDrm->StoredDeviceID = 0x1234;
    int deviceID = 0;
    int ret = pDrm->getDeviceID(deviceID);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(pDrm->StoredDeviceID, deviceID);
    delete pDrm;
}

TEST(DrmTest, GivenConfigFileWithWrongDeviceIDWhenFrequencyIsQueriedThenReturnZero) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    pDrm->StoredDeviceID = 0x4321;
    int maxFrequency = 0;
    int ret = pDrm->getMaxGpuFrequency(maxFrequency);
    EXPECT_EQ(0, ret);

    EXPECT_EQ(0, maxFrequency);

    delete pDrm;
}

TEST(DrmTest, GivenConfigFileWithWrongDeviceIDFailIoctlWhenFrequencyIsQueriedThenReturnZero) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    pDrm->StoredDeviceID = 0x4321;
    pDrm->StoredRetValForDeviceID = -1;
    int maxFrequency = 0;
    int ret = pDrm->getMaxGpuFrequency(maxFrequency);
    EXPECT_EQ(-1, ret);

    EXPECT_EQ(0, maxFrequency);

    delete pDrm;
}

TEST(DrmTest, GivenValidConfigFileWhenFrequencyIsQueriedThenValidValueIsReturned) {

    int expectedMaxFrequency = 1000;

    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    pDrm->StoredDeviceID = 0x1234;

    string gpuFile = "test_files/devices/config";
    string gtMaxFreqFile = "test_files/devices/drm/card0/gt_max_freq_mhz";

    EXPECT_TRUE(fileExists(gpuFile));
    EXPECT_TRUE(fileExists(gtMaxFreqFile));

    int maxFrequency = 0;
    int ret = pDrm->getMaxGpuFrequency(maxFrequency);
    EXPECT_EQ(0, ret);

    EXPECT_EQ(expectedMaxFrequency, maxFrequency);
    delete pDrm;
}

TEST(DrmTest, GivenNoConfigFileWhenFrequencyIsQueriedThenReturnZero) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    pDrm->StoredDeviceID = 0x1234;
    // change directory
    pDrm->setSysFsDefaultGpuPath("./");
    int maxFrequency = 0;
    int ret = pDrm->getMaxGpuFrequency(maxFrequency);
    EXPECT_EQ(0, ret);

    EXPECT_EQ(0, maxFrequency);
    delete pDrm;
}

TEST(DrmTest, GetRevisionID) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    pDrm->StoredDeviceID = 0x1234;
    pDrm->StoredDeviceRevID = 0xB;
    int deviceID = 0;
    int ret = pDrm->getDeviceID(deviceID);
    EXPECT_EQ(0, ret);
    int revID = 0;
    ret = pDrm->getDeviceRevID(revID);
    EXPECT_EQ(0, ret);

    EXPECT_EQ(pDrm->StoredDeviceID, deviceID);
    EXPECT_EQ(pDrm->StoredDeviceRevID, revID);

    delete pDrm;
}

TEST(DrmTest, GivenMockDrmWhenAskedFor48BitAddressCorrectValueReturned) {
    DrmMock *pDrm = new DrmMock;
    pDrm->StoredPPGTT = 3;
    EXPECT_TRUE(pDrm->is48BitAddressRangeSupported());
    pDrm->StoredPPGTT = 2;
    EXPECT_FALSE(pDrm->is48BitAddressRangeSupported());
    delete pDrm;
}

#if defined(I915_PARAM_HAS_PREEMPTION)
TEST(DrmTest, GivenMockDrmWhenAskedForPreemptionCorrectValueReturned) {
    DrmMock *pDrm = new DrmMock;
    pDrm->StoredPreemptionSupport = 1;
    EXPECT_TRUE(pDrm->hasPreemption());
    pDrm->StoredPreemptionSupport = 0;
    EXPECT_FALSE(pDrm->hasPreemption());
    delete pDrm;
}

TEST(DrmTest, GivenMockDrmWhenAskedForContextThatPassedThenValidContextIdsReturned) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_EQ(0u, pDrm->lowPriorityContextId);
    pDrm->StoredRetVal = 0;
    pDrm->StoredCtxId = 2;
    EXPECT_TRUE(pDrm->contextCreate());
    EXPECT_EQ(2u, pDrm->lowPriorityContextId);
    pDrm->StoredRetVal = 0;
    pDrm->StoredCtxId = 1;
    delete pDrm;
}

TEST(DrmTest, GivenMockDrmWhenAskedForContextThatFailsThenFalseIsReturned) {
    DrmMock *pDrm = new DrmMock;
    pDrm->StoredRetVal = -1;
    EXPECT_FALSE(pDrm->contextCreate());
    pDrm->StoredRetVal = 0;
    delete pDrm;
}

TEST(DrmTest, GivenMockDrmWhenAskedForContextWithLowPriorityThatFailsThenFalseIsReturned) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_TRUE(pDrm->contextCreate());
    pDrm->StoredRetVal = -1;
    EXPECT_FALSE(pDrm->setLowPriority());
    pDrm->StoredRetVal = 0;
    delete pDrm;
}
#endif

TEST(DrmTest, getExecSoftPin) {
    DrmMock *pDrm = new DrmMock;
    int execSoftPin = 0;

    int ret = pDrm->getExecSoftPin(execSoftPin);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, execSoftPin);

    pDrm->StoredExecSoftPin = 1;
    ret = pDrm->getExecSoftPin(execSoftPin);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, execSoftPin);

    delete pDrm;
}

TEST(DrmTest, enableTurboBoost) {
    DrmMock *pDrm = new DrmMock;

    int ret = pDrm->enableTurboBoost();
    EXPECT_EQ(0, ret);

    delete pDrm;
}

TEST(DrmTest, getEnabledPooledEu) {
    DrmMock *pDrm = new DrmMock;

    int enabled = 0;
    int ret = 0;
    pDrm->StoredHasPooledEU = -1;
#if defined(I915_PARAM_HAS_POOLED_EU)
    ret = pDrm->getEnabledPooledEu(enabled);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(-1, enabled);

    pDrm->StoredHasPooledEU = 0;
    ret = pDrm->getEnabledPooledEu(enabled);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, enabled);

    pDrm->StoredHasPooledEU = 1;
    ret = pDrm->getEnabledPooledEu(enabled);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, enabled);

    pDrm->StoredRetValForPooledEU = -1;
    ret = pDrm->getEnabledPooledEu(enabled);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(1, enabled);
#else
    ret = pDrm->getEnabledPooledEu(enabled);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, enabled);
#endif
    delete pDrm;
}

TEST(DrmTest, getMinEuInPool) {
    DrmMock *pDrm = new DrmMock;

    pDrm->StoredMinEUinPool = -1;
    int minEUinPool = 0;
    int ret = 0;
#if defined(I915_PARAM_MIN_EU_IN_POOL)
    ret = pDrm->getMinEuInPool(minEUinPool);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(-1, minEUinPool);

    pDrm->StoredMinEUinPool = 0;
    ret = pDrm->getMinEuInPool(minEUinPool);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, minEUinPool);

    pDrm->StoredMinEUinPool = 1;
    ret = pDrm->getMinEuInPool(minEUinPool);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, minEUinPool);

    pDrm->StoredRetValForMinEUinPool = -1;
    ret = pDrm->getMinEuInPool(minEUinPool);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(1, minEUinPool);
#else
    ret = pDrm->getMinEuInPool(minEUinPool);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, minEUinPool);
#endif
    delete pDrm;
}

TEST(DrmTest, givenDrmWhenGetErrnoIsCalledThenErrnoValueIsReturned) {
    DrmMock *pDrm = new DrmMock;
    EXPECT_NE(nullptr, pDrm);

    auto errnoFromDrm = pDrm->getErrno();
    EXPECT_EQ(errno, errnoFromDrm);
    delete pDrm;
}
