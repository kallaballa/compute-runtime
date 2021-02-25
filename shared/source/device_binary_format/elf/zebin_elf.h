/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/device_binary_format/elf/elf.h"
#include "shared/source/utilities/const_stringref.h"

#include <inttypes.h>
#include <stddef.h>

namespace NEO {

namespace Elf {

enum ELF_TYPE_ZEBIN : uint16_t {
    ET_ZEBIN_REL = 0xff11, // A relocatable ZE binary file
    ET_ZEBIN_EXE = 0xff12, // An executable ZE binary file
    ET_ZEBIN_DYN = 0xff13, // A shared object ZE binary file
};

enum SHT_ZEBIN : uint32_t {
    SHT_ZEBIN_SPIRV = 0xff000009,     // .spv.kernel section, value the same as SHT_OPENCL_SPIRV
    SHT_ZEBIN_ZEINFO = 0xff000011,    // .ze_info section
    SHT_ZEBIN_GTPIN_INFO = 0xff000012 // .gtpin_info section
};

namespace SectionsNamesZebin {
static constexpr ConstStringRef textPrefix = ".text.";
static constexpr ConstStringRef dataConst = ".data.const";
static constexpr ConstStringRef dataGlobalConst = ".data.global_const";
static constexpr ConstStringRef dataGlobal = ".data.global";
static constexpr ConstStringRef symtab = ".symtab";
static constexpr ConstStringRef relTablePrefix = ".rel.";
static constexpr ConstStringRef spv = ".spv";
static constexpr ConstStringRef debugInfo = ".debug_info";
static constexpr ConstStringRef zeInfo = ".ze_info";
static constexpr ConstStringRef gtpinInfo = ".gtpin_info";
} // namespace SectionsNamesZebin

struct ZebinTargetFlags {
    union {
        struct {
            // bit[7:0]: dedicated for specific generator (meaning based on generatorId)
            uint8_t generatorSpecificFlags : 8;

            // bit[12:8]: values [0-31], min compatbile device revision Id (stepping)
            uint8_t minHwRevisionId : 5;

            // bit[13:13]:
            // 0 - full validation during decoding (safer decoding)
            // 1 - no validation (faster decoding - recommended for known generators)
            bool validateRevisionId : 1;

            // bit[14:14]:
            // 0 - ignore minHwRevisionId and maxHwRevisionId
            // 1 - underlying device must match specified revisionId info
            bool disableExtendedValidation : 1;

            // bit[15:15]:
            // 0 - elfFileHeader::machine is PRODUCT_FAMILY
            // 1 - elfFileHeader::machine is GFXCORE_FAMILY
            bool machineEntryUsesGfxCoreInsteadOfProductFamily : 1;

            // bit[20:16]:  max compatbile device revision Id (stepping)
            uint8_t maxHwRevisionId : 5;

            // bit[23:21]: generator of this device binary
            // 0 - Unregistered
            // 1 - IGC
            uint8_t generatorId : 3;

            // bit[31:24]: MBZ, reserved for future use
            uint8_t reserved : 8;
        };
        uint32_t packed = 0U;
    };
};
static_assert(sizeof(ZebinTargetFlags) == sizeof(uint32_t), "");

namespace ZebinKernelMetadata {
namespace Tags {
static constexpr ConstStringRef kernels("kernels");
static constexpr ConstStringRef version("version");
namespace Kernel {
static constexpr ConstStringRef name("name");
static constexpr ConstStringRef executionEnv("execution_env");
static constexpr ConstStringRef payloadArguments("payload_arguments");
static constexpr ConstStringRef bindingTableIndices("binding_table_indices");
static constexpr ConstStringRef perThreadPayloadArguments("per_thread_payload_arguments");
static constexpr ConstStringRef perThreadMemoryBuffers("per_thread_memory_buffers");
static constexpr ConstStringRef experimentalProperties("experimental_properties");

namespace ExecutionEnv {
static constexpr ConstStringRef actualKernelStartOffset("actual_kernel_start_offset");
static constexpr ConstStringRef barrierCount("barrier_count");
static constexpr ConstStringRef disableMidThreadPreemption("disable_mid_thread_preemption");
static constexpr ConstStringRef grfCount("grf_count");
static constexpr ConstStringRef has4gbBuffers("has_4gb_buffers");
static constexpr ConstStringRef hasDeviceEnqueue("has_device_enqueue");
static constexpr ConstStringRef hasFenceForImageAccess("has_fence_for_image_access");
static constexpr ConstStringRef hasGlobalAtomics("has_global_atomics");
static constexpr ConstStringRef hasMultiScratchSpaces("has_multi_scratch_spaces");
static constexpr ConstStringRef hasNoStatelessWrite("has_no_stateless_write");
static constexpr ConstStringRef hwPreemptionMode("hw_preemption_mode");
static constexpr ConstStringRef offsetToSkipPerThreadDataLoad("offset_to_skip_per_thread_data_load");
static constexpr ConstStringRef offsetToSkipSetFfidGp("offset_to_skip_set_ffid_gp");
static constexpr ConstStringRef requiredSubGroupSize("required_sub_group_size");
static constexpr ConstStringRef requiredWorkGroupSize("required_work_group_size");
static constexpr ConstStringRef simdSize("simd_size");
static constexpr ConstStringRef slmSize("slm_size");
static constexpr ConstStringRef subgroupIndependentForwardProgress("subgroup_independent_forward_progress");
static constexpr ConstStringRef workGroupWalkOrderDimensions("work_group_walk_order_dimensions");
} // namespace ExecutionEnv

namespace PayloadArgument {
static constexpr ConstStringRef argType("arg_type");
static constexpr ConstStringRef argIndex("arg_index");
static constexpr ConstStringRef offset("offset");
static constexpr ConstStringRef size("size");
static constexpr ConstStringRef addrmode("addrmode");
static constexpr ConstStringRef addrspace("addrspace");
static constexpr ConstStringRef accessType("access_type");
namespace ArgType {
static constexpr ConstStringRef localSize("local_size");
static constexpr ConstStringRef groupCount("group_count");
static constexpr ConstStringRef globalIdOffset("global_id_offset");
static constexpr ConstStringRef globalSize("global_size");
static constexpr ConstStringRef enqueuedLocalSize("enqueued_local_size");
static constexpr ConstStringRef privateBaseStateless("private_base_stateless");
static constexpr ConstStringRef argByvalue("arg_byvalue");
static constexpr ConstStringRef argBypointer("arg_bypointer");
} // namespace ArgType
namespace MemoryAddressingMode {
static constexpr ConstStringRef stateless("stateless");
static constexpr ConstStringRef stateful("stateful");
static constexpr ConstStringRef bindless("bindless");
static constexpr ConstStringRef sharedLocalMemory("slm");
} // namespace MemoryAddressingMode
namespace AddrSpace {
static constexpr ConstStringRef global("global");
static constexpr ConstStringRef local("local");
static constexpr ConstStringRef constant("constant");
static constexpr ConstStringRef image("image");
static constexpr ConstStringRef sampler("sampler");
} // namespace AddrSpace
namespace AccessType {
static constexpr ConstStringRef readonly("readonly");
static constexpr ConstStringRef writeonly("writeonly");
static constexpr ConstStringRef readwrite("readwrite");
} // namespace AccessType
} // namespace PayloadArgument

namespace BindingTableIndex {
static constexpr ConstStringRef btiValue("bti_value");
static constexpr ConstStringRef argIndex("arg_index");
} // namespace BindingTableIndex

namespace PerThreadPayloadArgument {
static constexpr ConstStringRef argType("arg_type");
static constexpr ConstStringRef offset("offset");
static constexpr ConstStringRef size("size");
namespace ArgType {
static constexpr ConstStringRef packedLocalIds("packed_local_ids");
static constexpr ConstStringRef localId("local_id");
} // namespace ArgType
} // namespace PerThreadPayloadArgument

namespace PerThreadMemoryBuffer {
static constexpr ConstStringRef allocationType("type");
static constexpr ConstStringRef memoryUsage("usage");
static constexpr ConstStringRef size("size");
static constexpr ConstStringRef isSimtThread("is_simt_thread");
static constexpr ConstStringRef slot("slot");
namespace AllocationType {
static constexpr ConstStringRef global("global");
static constexpr ConstStringRef scratch("scratch");
static constexpr ConstStringRef slm("slm");
} // namespace AllocationType
namespace MemoryUsage {
static constexpr ConstStringRef privateSpace("private_space");
static constexpr ConstStringRef spillFillSpace("spill_fill_space");
static constexpr ConstStringRef singleSpace("single_space");
} // namespace MemoryUsage
} // namespace PerThreadMemoryBuffer
namespace ExperimentalProperties {
static constexpr ConstStringRef hasNonKernelArgLoad("has_non_kernel_arg_load");
static constexpr ConstStringRef hasNonKernelArgStore("has_non_kernel_arg_store");
static constexpr ConstStringRef hasNonKernelArgAtomic("has_non_kernel_arg_atomic");
} // namespace ExperimentalProperties
} // namespace Kernel
} // namespace Tags

namespace Types {

struct Version {
    uint32_t major = 0U;
    uint32_t minor = 0U;
};

namespace Kernel {
namespace ExecutionEnv {
using ActualKernelStartOffsetT = int32_t;
using BarrierCountT = int32_t;
using DisableMidThreadPreemptionT = bool;
using GrfCountT = int32_t;
using Has4GBBuffersT = bool;
using HasDeviceEnqueueT = bool;
using HasFenceForImageAccessT = bool;
using HasGlobalAtomicsT = bool;
using HasMultiScratchSpacesT = bool;
using HasNoStatelessWriteT = bool;
using HwPreemptionModeT = int32_t;
using OffsetToSkipPerThreadDataLoadT = int32_t;
using OffsetToSkipSetFfidGpT = int32_t;
using RequiredSubGroupSizeT = int32_t;
using RequiredWorkGroupSizeT = int32_t[3];
using SimdSizeT = int32_t;
using SlmSizeT = int32_t;
using SubgroupIndependentForwardProgressT = bool;
using WorkgroupWalkOrderDimensionsT = int32_t[3];
using HasNonKernelArgLoad = int32_t;
using HasNonKernelArgStore = int32_t;
using HasNonKernelArgAtomic = int32_t;

namespace Defaults {
static constexpr BarrierCountT barrierCount = 0;
static constexpr DisableMidThreadPreemptionT disableMidThreadPreemption = false;
static constexpr Has4GBBuffersT has4GBBuffers = false;
static constexpr HasDeviceEnqueueT hasDeviceEnqueue = false;
static constexpr HasFenceForImageAccessT hasFenceForImageAccess = false;
static constexpr HasGlobalAtomicsT hasGlobalAtomics = false;
static constexpr HasMultiScratchSpacesT hasMultiScratchSpaces = false;
static constexpr HasNoStatelessWriteT hasNoStatelessWrite = false;
static constexpr HwPreemptionModeT hwPreemptionMode = -1;
static constexpr OffsetToSkipPerThreadDataLoadT offsetToSkipPerThreadDataLoad = 0;
static constexpr OffsetToSkipSetFfidGpT offsetToSkipSetFfidGp = 0;
static constexpr RequiredSubGroupSizeT requiredSubGroupSize = 0;
static constexpr RequiredWorkGroupSizeT requiredWorkGroupSize = {0, 0, 0};
static constexpr SlmSizeT slmSize = 0;
static constexpr SubgroupIndependentForwardProgressT subgroupIndependentForwardProgress = false;
static constexpr WorkgroupWalkOrderDimensionsT workgroupWalkOrderDimensions = {0, 1, 2};
static constexpr HasNonKernelArgLoad hasNonKernelArgLoad = false;
static constexpr HasNonKernelArgStore hasNonKernelArgStore = false;
static constexpr HasNonKernelArgAtomic hasNonKernelArgAtomic = false;
} // namespace Defaults

static constexpr ConstStringRef required[] = {
    Tags::Kernel::ExecutionEnv::actualKernelStartOffset,
    Tags::Kernel::ExecutionEnv::grfCount,
    Tags::Kernel::ExecutionEnv::simdSize};

struct ExecutionEnvBaseT {
    ActualKernelStartOffsetT actualKernelStartOffset = -1;
    BarrierCountT barrierCount = Defaults::barrierCount;
    DisableMidThreadPreemptionT disableMidThreadPreemption = Defaults::disableMidThreadPreemption;
    GrfCountT grfCount = -1;
    Has4GBBuffersT has4GBBuffers = Defaults::has4GBBuffers;
    HasDeviceEnqueueT hasDeviceEnqueue = Defaults::hasDeviceEnqueue;
    HasFenceForImageAccessT hasFenceForImageAccess = Defaults::hasFenceForImageAccess;
    HasGlobalAtomicsT hasGlobalAtomics = Defaults::hasGlobalAtomics;
    HasMultiScratchSpacesT hasMultiScratchSpaces = Defaults::hasMultiScratchSpaces;
    HasNoStatelessWriteT hasNoStatelessWrite = Defaults::hasNoStatelessWrite;
    HwPreemptionModeT hwPreemptionMode = Defaults::hwPreemptionMode;
    OffsetToSkipPerThreadDataLoadT offsetToSkipPerThreadDataLoad = Defaults::offsetToSkipPerThreadDataLoad;
    OffsetToSkipSetFfidGpT offsetToSkipSetFfidGp = Defaults::offsetToSkipSetFfidGp;
    RequiredSubGroupSizeT requiredSubGroupSize = Defaults::requiredSubGroupSize;
    RequiredWorkGroupSizeT requiredWorkGroupSize = {Defaults::requiredWorkGroupSize[0], Defaults::requiredWorkGroupSize[1], Defaults::requiredWorkGroupSize[2]};
    SimdSizeT simdSize = -1;
    SlmSizeT slmSize = Defaults::slmSize;
    SubgroupIndependentForwardProgressT subgroupIndependentForwardProgress = Defaults::subgroupIndependentForwardProgress;
    WorkgroupWalkOrderDimensionsT workgroupWalkOrderDimensions{Defaults::workgroupWalkOrderDimensions[0], Defaults::workgroupWalkOrderDimensions[1], Defaults::workgroupWalkOrderDimensions[2]};
};

struct ExperimentalPropertiesBaseT {
    HasNonKernelArgLoad hasNonKernelArgLoad = Defaults::hasNonKernelArgLoad;
    HasNonKernelArgStore hasNonKernelArgStore = Defaults::hasNonKernelArgStore;
    HasNonKernelArgAtomic hasNonKernelArgAtomic = Defaults::hasNonKernelArgAtomic;
};

} // namespace ExecutionEnv

enum ArgType : uint8_t {
    ArgTypeUnknown = 0,
    ArgTypePackedLocalIds = 1,
    ArgTypeLocalId,
    ArgTypeLocalSize,
    ArgTypeGroupCount,
    ArgTypeGlobalSize,
    ArgTypeEnqueuedLocalSize,
    ArgTypeGlobalIdOffset,
    ArgTypePrivateBaseStateless,
    ArgTypeArgByvalue,
    ArgTypeArgBypointer,
};

namespace PerThreadPayloadArgument {

using OffsetT = int32_t;
using SizeT = int32_t;
using ArgTypeT = ArgType;

namespace Defaults {
}

struct PerThreadPayloadArgumentBaseT {
    ArgTypeT argType = ArgTypeUnknown;
    OffsetT offset = -1;
    SizeT size = -1;
};

} // namespace PerThreadPayloadArgument

namespace PayloadArgument {

enum MemoryAddressingMode : uint8_t {
    MemoryAddressingModeUnknown = 0,
    MemoryAddressingModeStateful = 1,
    MemoryAddressingModeStateless,
    MemoryAddressingModeBindless,
    MemoryAddressingModeSharedLocalMemory,
};

enum AddressSpace : uint8_t {
    AddressSpaceUnknown = 0,
    AddressSpaceGlobal = 1,
    AddressSpaceLocal,
    AddressSpaceConstant,
    AddressSpaceImage,
    AddressSpaceSampler,
};

enum AccessType : uint8_t {
    AccessTypeUnknown = 0,
    AccessTypeReadonly = 1,
    AccessTypeWriteonly,
    AccessTypeReadwrite,
};

using ArgTypeT = ArgType;
using OffseT = int32_t;
using SizeT = int32_t;
using ArgIndexT = int32_t;
using AddrmodeT = MemoryAddressingMode;
using AddrspaceT = AddressSpace;
using AccessTypeT = AccessType;
using SlmAlignment = uint8_t;

namespace Defaults {
static constexpr ArgIndexT argIndex = -1;
static constexpr SlmAlignment slmArgAlignment = 16U;
} // namespace Defaults

struct PayloadArgumentBaseT {
    ArgTypeT argType = ArgTypeUnknown;
    OffseT offset = 0;
    SizeT size = 0;
    ArgIndexT argIndex = Defaults::argIndex;
    AddrmodeT addrmode = MemoryAddressingModeUnknown;
    AddrspaceT addrspace = AddressSpaceUnknown;
    AccessTypeT accessType = AccessTypeUnknown;
};

} // namespace PayloadArgument

namespace BindingTableEntry {
using BtiValueT = int32_t;
using ArgIndexT = int32_t;
struct BindingTableEntryBaseT {
    BtiValueT btiValue = 0U;
    ArgIndexT argIndex = 0U;
};
} // namespace BindingTableEntry

namespace PerThreadMemoryBuffer {
enum AllocationType : uint8_t {
    AllocationTypeUnknown = 0,
    AllocationTypeGlobal,
    AllocationTypeScratch,
    AllocationTypeSlm
};

enum MemoryUsage : uint8_t {
    MemoryUsageUnknown = 0,
    MemoryUsagePrivateSpace,
    MemoryUsageSpillFillSpace,
    MemoryUsageSingleSpace
};

using SizeT = int32_t;
using AllocationTypeT = AllocationType;
using MemoryUsageT = MemoryUsage;
using IsSimtThreadT = bool;
using Slot = int32_t;

namespace Defaults {
static constexpr IsSimtThreadT isSimtThread = false;
static constexpr Slot slot = 0U;
} // namespace Defaults

struct PerThreadMemoryBufferBaseT {
    AllocationType allocationType = AllocationTypeUnknown;
    MemoryUsageT memoryUsage = MemoryUsageUnknown;
    SizeT size = 0U;
    IsSimtThreadT isSimtThread = Defaults::isSimtThread;
    Slot slot = Defaults::slot;
};
} // namespace PerThreadMemoryBuffer

} // namespace Kernel

} // namespace Types

} // namespace ZebinKernelMetadata

} // namespace Elf

} // namespace NEO
