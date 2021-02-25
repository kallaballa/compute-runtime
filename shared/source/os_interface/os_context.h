/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/preemption_mode.h"
#include "shared/source/helpers/common_types.h"
#include "shared/source/utilities/reference_tracked_object.h"

#include "engine_node.h"

#include <memory>

namespace NEO {
class OSInterface;

struct DirectSubmissionProperties;
struct HardwareInfo;

class OsContext : public ReferenceTrackedObject<OsContext> {
  public:
    OsContext() = delete;

    static OsContext *create(OSInterface *osInterface, uint32_t contextId, DeviceBitfield deviceBitfield,
                             aub_stream::EngineType engineType, PreemptionMode preemptionMode,
                             bool lowPriority, bool internalEngine, bool rootDevice);
    uint32_t getContextId() const { return contextId; }
    uint32_t getNumSupportedDevices() const { return numSupportedDevices; }
    DeviceBitfield getDeviceBitfield() const { return deviceBitfield; }
    PreemptionMode getPreemptionMode() const { return preemptionMode; }
    aub_stream::EngineType &getEngineType() { return engineType; }
    bool isLowPriority() const { return lowPriority; }
    bool isInternalEngine() const { return internalEngine; }
    bool isRootDevice() const { return rootDevice; }
    virtual bool isInitialized() const { return true; }
    bool isDefaultContext() const { return defaultContext; }
    void setDefaultContext(bool value) { defaultContext = value; }
    bool isDirectSubmissionActive() { return directSubmissionActive; }
    void setDirectSubmissionActive() { directSubmissionActive = true; }

    bool isDirectSubmissionAvailable(const HardwareInfo &hwInfo, bool &submitOnInit);
    bool checkDirectSubmissionSupportsEngine(const DirectSubmissionProperties &directSubmissionProperty,
                                             aub_stream::EngineType contextEngineType,
                                             bool &startOnInit,
                                             bool &startInContext);

  protected:
    OsContext(uint32_t contextId, DeviceBitfield deviceBitfield, aub_stream::EngineType engineType, PreemptionMode preemptionMode,
              bool lowPriority, bool internalEngine, bool rootDevice)
        : contextId(contextId),
          deviceBitfield(deviceBitfield),
          preemptionMode(preemptionMode),
          numSupportedDevices(static_cast<uint32_t>(deviceBitfield.count())),
          engineType(engineType),
          lowPriority(lowPriority),
          internalEngine(internalEngine),
          rootDevice(rootDevice) {}

    const uint32_t contextId;
    const DeviceBitfield deviceBitfield;
    const PreemptionMode preemptionMode;
    const uint32_t numSupportedDevices;
    aub_stream::EngineType engineType = aub_stream::ENGINE_RCS;
    const bool lowPriority = false;
    const bool internalEngine = false;
    const bool rootDevice = false;
    bool defaultContext = false;
    bool directSubmissionActive = false;
};
} // namespace NEO
