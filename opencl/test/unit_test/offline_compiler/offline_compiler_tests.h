/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/offline_compiler/source/multi_command.h"
#include "shared/offline_compiler/source/offline_compiler.h"

#include "gtest/gtest.h"
#include <CL/cl.h>

#include <cstdint>
#include <memory>

namespace NEO {

class OfflineCompilerTests : public ::testing::Test {
  public:
    OfflineCompilerTests() : pOfflineCompiler(nullptr),
                             retVal(CL_SUCCESS) {
        uniqueHelper = std::make_unique<OclocArgHelper>();
        // ctor
    }

    OfflineCompiler *pOfflineCompiler;
    int retVal;
    std::unique_ptr<OclocArgHelper> uniqueHelper;
};

class MultiCommandTests : public ::testing::Test {
  public:
    MultiCommandTests() : pMultiCommand(nullptr),
                          retVal(CL_SUCCESS) {
        uniqueHelper = std::make_unique<OclocArgHelper>();
    }
    void createFileWithArgs(const std::vector<std::string> &, int numOfBuild);
    void deleteFileWithArgs();
    void deleteOutFileList();
    MultiCommand *pMultiCommand;
    std::string nameOfFileWithArgs;
    std::string outFileList;
    int retVal;
    std::unique_ptr<OclocArgHelper> uniqueHelper;
};

void MultiCommandTests::createFileWithArgs(const std::vector<std::string> &singleArgs, int numOfBuild) {
    std::ofstream myfile(nameOfFileWithArgs);
    if (myfile.is_open()) {
        for (int i = 0; i < numOfBuild; i++) {
            for (auto singleArg : singleArgs)
                myfile << singleArg + " ";
            myfile << std::endl;
        }
        myfile.close();
    } else
        printf("Unable to open file\n");
}

void MultiCommandTests::deleteFileWithArgs() {
    if (remove(nameOfFileWithArgs.c_str()) != 0)
        perror("Error deleting file");
}

void MultiCommandTests::deleteOutFileList() {
    if (remove(outFileList.c_str()) != 0)
        perror("Error deleting file");
}

} // namespace NEO
