/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/utilities/debug_settings_reader.h"

using namespace NEO;

class DebugManagerStateRestore {
  public:
    DebugManagerStateRestore() {
        debugVarSnapshot = DebugManager.flags;
        injectFcnSnapshot = DebugManager.injectFcn;
    }
    ~DebugManagerStateRestore() {
        DebugManager.flags = debugVarSnapshot;
        DebugManager.injectFcn = injectFcnSnapshot;
#undef DECLARE_DEBUG_VARIABLE
#define DECLARE_DEBUG_VARIABLE(dataType, variableName, defaultValue, description) shrink(DebugManager.flags.variableName.getRef());
#include "shared/source/debug_settings/release_variables.inl"

#include "debug_variables.inl"
#undef DECLARE_DEBUG_VARIABLE
    }
    DebugVariables debugVarSnapshot;
    void *injectFcnSnapshot = nullptr;

  protected:
    void shrink(std::string &flag) {
        flag.shrink_to_fit();
    }
    void shrink(int64_t &flag) {}
    void shrink(int32_t &flag) {}
    void shrink(bool &flag) {}
};

class RegistryReaderMock : public SettingsReader {
  public:
    RegistryReaderMock() {}
    ~RegistryReaderMock() override {}

    uint64_t forceRetValue = 1;

    int32_t getSetting(const char *settingName, int32_t defaultValue) override {
        return static_cast<uint32_t>(forceRetValue);
    }

    int64_t getSetting(const char *settingName, int64_t defaultValue) override {
        return forceRetValue;
    }

    bool getSetting(const char *settingName, bool defaultValue) override {
        return true;
    }

    std::string getSetting(const char *settingName, const std::string &value) override {
        return "";
    }

    const char *appSpecificLocation(const std::string &name) override {
        return name.c_str();
    }
};
