/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

/*SIMULATION FLAGS*/
DECLARE_DEBUG_VARIABLE(std::string, TbxServer, std::string("127.0.0.1"), "TCP-IP address of TBX server")
DECLARE_DEBUG_VARIABLE(std::string, ProductFamilyOverride, std::string("unk"), "Specify product for use in AUB/TBX")
DECLARE_DEBUG_VARIABLE(std::string, HardwareInfoOverride, std::string("default"), "Specify hardware info config, i.e 1x4x8, for use in AUB/TBX")
DECLARE_DEBUG_VARIABLE(std::string, ForceCompilerUsePlatform, std::string("unk"), "Specify product for use in compiler interface")
DECLARE_DEBUG_VARIABLE(std::string, AUBDumpBufferFormat, std::string("unk"), "Specify buffer format to be dumped in AUB files (TRE or BIN)")
DECLARE_DEBUG_VARIABLE(std::string, AUBDumpImageFormat, std::string("unk"), "Specify image format to to be dumped in AUB files (TRE or BMP)")
DECLARE_DEBUG_VARIABLE(std::string, AUBDumpCaptureFileName, std::string("unk"), "Name of file to save AUB capture into")
DECLARE_DEBUG_VARIABLE(std::string, AUBDumpFilterKernelName, std::string("unk"), "Name of kernel to AUB capture")
DECLARE_DEBUG_VARIABLE(std::string, AUBDumpToggleFileName, std::string("unk"), "Name of file to save AUB in toggle mode")
DECLARE_DEBUG_VARIABLE(std::string, OverrideGdiPath, std::string("unk"), "When different value than \"unk\", will override default path to gdi library.")
DECLARE_DEBUG_VARIABLE(std::string, AubDumpAddMmioRegistersList, std::string("unk"), "Semicolon separated sequence of additional MMIO registers offset;values pairs i.e. 0x111;0x123;0x222;0x456")
DECLARE_DEBUG_VARIABLE(int32_t, AUBDumpFilterNamedKernelStartIdx, 0, "Start index of named kernel to AUB capture")
DECLARE_DEBUG_VARIABLE(int32_t, AUBDumpFilterNamedKernelEndIdx, -1, "End index of named kernel to AUB capture")
DECLARE_DEBUG_VARIABLE(int32_t, AUBDumpSubCaptureMode, 0, "AUB dump subcapture mode (0 - off, 1 - filter by kernel name and/or index range, 2 - toggle on/off with dynamic regkey)")
DECLARE_DEBUG_VARIABLE(int32_t, AUBDumpFilterKernelStartIdx, 0, "Start index of kernel to AUB capture")
DECLARE_DEBUG_VARIABLE(int32_t, AUBDumpFilterKernelEndIdx, -1, "End index of kernel to AUB capture")
DECLARE_DEBUG_VARIABLE(int32_t, AUBDumpToggleCaptureOnOff, 0, "Toggle AUB capture on/off")
DECLARE_DEBUG_VARIABLE(int32_t, AubDumpOverrideMmioRegister, 0, "Override mmio offset from list with new value from AubDumpOverrideMmioRegisterValue")
DECLARE_DEBUG_VARIABLE(int32_t, AubDumpOverrideMmioRegisterValue, 0, "Value to override mmio offset from AubDumpOverrideMmioRegister")
DECLARE_DEBUG_VARIABLE(int32_t, SetCommandStreamReceiver, -1, "Set command stream receiver to: 0 - HW, 1 - AUB, 2 - TBX, 3 - HW & AUB, 4 - TBX & AUB")
DECLARE_DEBUG_VARIABLE(int32_t, TbxPort, 4321, "TCP-IP port of TBX server")
DECLARE_DEBUG_VARIABLE(bool, TbxFrontdoorMode, false, "Set TBX frontdoor mode for read and write memory accesses (the default mode is via backdoor)")
DECLARE_DEBUG_VARIABLE(bool, FlattenBatchBufferForAUBDump, false, "Dump multi-level batch buffers to AUB as single, flat batch buffer")
DECLARE_DEBUG_VARIABLE(bool, AddPatchInfoCommentsForAUBDump, false, "Dump comments containing allocations and patching information")
DECLARE_DEBUG_VARIABLE(bool, UseAubStream, true, "Use aub_stream for aub dumping")
DECLARE_DEBUG_VARIABLE(bool, AUBDumpAllocsOnEnqueueReadOnly, false, "Force dumping buffers and images on clEnqueueReadBuffer/Image only (blocking calls)")
DECLARE_DEBUG_VARIABLE(bool, AUBDumpAllocsOnEnqueueSVMMemcpyOnly, false, "Force dumping allocations on clEnqueueSVMMemcpy only (blocking calls)")
DECLARE_DEBUG_VARIABLE(bool, AUBDumpForceAllToLocalMemory, false, "Force placing every allocation in local memory address space")

/*DEBUG FLAGS*/
DECLARE_DEBUG_VARIABLE(bool, EnableSWTags, false, "Enable software tagging in batch buffer")
DECLARE_DEBUG_VARIABLE(bool, DumpSWTagsBXML, false, "Dump software tags BXML into a file")
DECLARE_DEBUG_VARIABLE(bool, DisableTimestampPacketOptimizations, false, "Allocate new allocation per node + dont reuse old nodes")
DECLARE_DEBUG_VARIABLE(bool, DisableCachingForStatefulBufferAccess, false, "Disable caching for stateful buffer access")
DECLARE_DEBUG_VARIABLE(bool, EnableDebugBreak, true, "Enable DEBUG_BREAKs")
DECLARE_DEBUG_VARIABLE(bool, FlushAllCaches, false, "pipe controls between enqueues flush all possible caches")
DECLARE_DEBUG_VARIABLE(bool, DoNotFlushCaches, false, "clear all possible cache flush flags from pipe controls between enqueue flush")
DECLARE_DEBUG_VARIABLE(bool, MakeEachEnqueueBlocking, false, "equivalent of finish after each enqueue")
DECLARE_DEBUG_VARIABLE(bool, DisableResourceRecycling, false, "when set to true disables resource recycling optimization")
DECLARE_DEBUG_VARIABLE(bool, ForceDispatchScheduler, false, "dispatches scheduler kernel instead of kernel enqueued")
DECLARE_DEBUG_VARIABLE(bool, TrackParentEvents, false, "events track their parents")
DECLARE_DEBUG_VARIABLE(bool, RebuildPrecompiledKernels, false, "forces driver to recompile precompiled kernels from sources")
DECLARE_DEBUG_VARIABLE(bool, LoopAtDriverInit, false, "Adds endless loop in DebugSettingsManager constructor, useful for debugging.")
DECLARE_DEBUG_VARIABLE(bool, DoNotRegisterTrimCallback, false, "When set to true driver is not registering trim callback.")
DECLARE_DEBUG_VARIABLE(bool, OverrideInvalidEngineWithDefault, false, "When set to true driver chooses engine 0 if no engine is found.")
DECLARE_DEBUG_VARIABLE(bool, ForceImplicitFlush, false, "Flush after each enqueue. Useful for debugging batched submission logic. ")
DECLARE_DEBUG_VARIABLE(bool, ForcePipeControlPriorToWalker, false, "Allows to force pipe contron prior to walker.")
DECLARE_DEBUG_VARIABLE(bool, ZebinAppendElws, false, "Append crossthread data with enqueue local work size")
DECLARE_DEBUG_VARIABLE(bool, ZebinIgnoreIcbeVersion, false, "Ignore IGC\'s ICBE version")
DECLARE_DEBUG_VARIABLE(bool, UseExternalAllocatorForSshAndDsh, false, "Use 32 bit external Allocator for ssh and dsh in Level Zero")
DECLARE_DEBUG_VARIABLE(bool, UseBindlessDebugSip, false, "Use bindless debug system routine")
DECLARE_DEBUG_VARIABLE(std::string, ForceDeviceId, std::string("unk"), "DeviceId selected for testing")
DECLARE_DEBUG_VARIABLE(int32_t, ForceL1Caching, -1, "-1: default, 0: disable, 1: enable, When set to true driver will program L1 cache policy for surface state and stateless accessess")
DECLARE_DEBUG_VARIABLE(int32_t, ForceAuxTranslationEnabled, -1, "-1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, SchedulerSimulationReturnInstance, 0, "prints execution model related debug information")
DECLARE_DEBUG_VARIABLE(int32_t, SchedulerGWS, 0, "Forces gws of scheduler kernel, only multiple of 24 allowed or 0 - default selected")
DECLARE_DEBUG_VARIABLE(int32_t, EnableExperimentalCommandBuffer, 0, "Enables injection of experimental command buffer")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideStatelessMocsIndex, -1, "-1: feature inactive, >=0 : following MOCS index will be programmed for stateless accesses in state base address")
DECLARE_DEBUG_VARIABLE(int32_t, CFEFusedEUDispatch, -1, "Set Fused EU dispatch in FrontEnd State command. -1 - default, 0 - enabled, 1 - disabled")
DECLARE_DEBUG_VARIABLE(int32_t, ForceAuxTranslationMode, -1, "-1: Default, 0: None, 1: Builtin, 2: Blit")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideGpuAddressSpace, -1, "-1: Default, !=-1: GPU address space range in bits")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideMaxWorkgroupSize, -1, "-1: Default, !=-1: Overrides max worgkroup size to this value")
DECLARE_DEBUG_VARIABLE(int32_t, DoCpuCopyOnReadBuffer, -1, "-1: default 0: do not use CPU copy, 1: triggers CPU copy path for Read Buffer calls, only supported for some basic use cases (no blocked user events in dependencies tree)")
DECLARE_DEBUG_VARIABLE(int32_t, DoCpuCopyOnWriteBuffer, -1, "-1: default 0: do not use CPU copy, 1: triggers CPU copy path for Write Buffer calls, only supported for some basic use cases (no blocked user events in dependencies tree)")
DECLARE_DEBUG_VARIABLE(int32_t, PauseOnEnqueue, -1, "-1: default, -2: always, x: pause on enqueue number x and ask for user confirmation before and after execution, counted from 0")
DECLARE_DEBUG_VARIABLE(int32_t, PauseOnBlitCopy, -1, "-1: default, -2: always, x: pause on blit enqueue number x and ask for user confirmation before and after execution, counted from 0. Note that single blit enqueue may have multiple copy instructions")
DECLARE_DEBUG_VARIABLE(int32_t, PauseOnGpuMode, -1, "-1: default (before and after), 0: before only, 1: after only")
DECLARE_DEBUG_VARIABLE(int32_t, EnableMultiStorageResources, -1, "-1: default, 0: Disable, 1: Enable")
DECLARE_DEBUG_VARIABLE(int32_t, LimitBlitterMaxWidth, -1, "-1: default, >=0: Max width")
DECLARE_DEBUG_VARIABLE(int32_t, LimitBlitterMaxHeight, -1, "-1: default, >=0: Max height")
DECLARE_DEBUG_VARIABLE(int32_t, PostBlitCommand, -1, "-1: default, 0: MI_ARB_CHECK, 1: MI_FLUSH, 2: Nothing")
DECLARE_DEBUG_VARIABLE(int32_t, OverridePreemptionSurfaceSizeInMb, -1, "-1: default, >=0 Override preemption surface size with value")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideLeastOccupiedBank, -1, "-1: default,  >=0 Override least occupied bank with value")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideRevision, -1, "-1: default,  >=0: Revision id")
DECLARE_DEBUG_VARIABLE(int32_t, ForceCacheFlushForBcs, -1, "Force cache flush from gpgpu engine before dispatching BCS copy. -1: default,  1: enabled, 0: disabled")
DECLARE_DEBUG_VARIABLE(int32_t, ForceGpgpuSubmissionForBcsEnqueue, -1, "-1: Default, 1: Submit gpgpu command buffer with cache flushing and completion synchronization, 0: Do nothing, if possible")
DECLARE_DEBUG_VARIABLE(int32_t, EnableUsmCompression, -1, "enable compression support for L0 USM Device and Shared Device side: -1 default, 0: disable, 1: enable")
DECLARE_DEBUG_VARIABLE(int32_t, EnableHostUsmSupport, -1, "-1: default, 0: disable, 1: enable, Enables USM host memory")
DECLARE_DEBUG_VARIABLE(int32_t, MediaVfeStateMaxSubSlices, -1, ">=0: Programs Media Vfe State Maximum Number of Dual-Subslices to given value ")
DECLARE_DEBUG_VARIABLE(int32_t, EnableMockSourceLevelDebugger, 0, "Switches driver to mode with active debugger. Active modes: 1: opt-disabled, 2: opt-enabled")
DECLARE_DEBUG_VARIABLE(int32_t, ForceBtpPrefetchMode, -1, "-1: default, 0: disable, 1: enable, Enables Btp prefetching")
DECLARE_DEBUG_VARIABLE(int32_t, EnableHostPointerImport, -1, "-1: default - disabled, 0: disabled, 1: enabled, Experimental implementation to import Host Pointer into L0")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideProfilingTimerResolution, -1, "-1: default - disabled, 0<=: Override deviceInfo.profilingTimerResolution")
DECLARE_DEBUG_VARIABLE(int32_t, GpuScratchRegWriteAfterWalker, -1, "-1: disabled, x: add GPU scratch register write after x walker")
DECLARE_DEBUG_VARIABLE(int32_t, GpuScratchRegWriteRegisterOffset, 0, "register offset for GPU scratch register write after walker")
DECLARE_DEBUG_VARIABLE(int32_t, GpuScratchRegWriteRegisterData, 0, "register data for GPU scratch register write after walker")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideSlmAllocationSize, -1, "-1: default, >=0: program value for shared local memory size")

/*LOGGING FLAGS*/
DECLARE_DEBUG_VARIABLE(int32_t, PrintDriverDiagnostics, -1, "prints driver diagnostics messages to standard output, value corresponds to hint level")
DECLARE_DEBUG_VARIABLE(bool, PrintDeviceAndEngineIdOnSubmission, false, "print submissions device and engine IDs to standard output")
DECLARE_DEBUG_VARIABLE(bool, PrintExecutionBuffer, false, "print execution buffer information to standard output")
DECLARE_DEBUG_VARIABLE(bool, PrintBOsForSubmit, false, "print all BOs passed to submission")
DECLARE_DEBUG_VARIABLE(bool, PrintDebugSettings, false, "Dump all debug variables settings to text file. Print to stdout if value is different than default.")
DECLARE_DEBUG_VARIABLE(bool, PrintDebugMessages, false, "when enabled, some debug messages will be propagated to console")
DECLARE_DEBUG_VARIABLE(bool, DumpKernels, false, "Enables dumping kernels' program source code to text files and program from binary to bin file")
DECLARE_DEBUG_VARIABLE(bool, DumpKernelArgs, false, "Enables dumping kernels args to binary files")
DECLARE_DEBUG_VARIABLE(bool, LogApiCalls, false, "Enables logging api function calls, inputs and outputs to file")
DECLARE_DEBUG_VARIABLE(bool, LogPatchTokens, false, "Enables logging patch tokens, inputs and outputs to file")
DECLARE_DEBUG_VARIABLE(bool, LogTaskCounts, false, "Enables logging taskCounts and taskLevels to file")
DECLARE_DEBUG_VARIABLE(bool, LogAlignedAllocations, false, "Logs alignedMalloc and alignedFree allocations")
DECLARE_DEBUG_VARIABLE(bool, LogAllocationMemoryPool, false, "Logs memory pool for allocations")
DECLARE_DEBUG_VARIABLE(bool, LogMemoryObject, false, "Logs memory object ptrs, sizes and operations")
DECLARE_DEBUG_VARIABLE(bool, LogWaitingForCompletion, false, "Logs waiting for completion")
DECLARE_DEBUG_VARIABLE(bool, ResidencyDebugEnable, false, "enables debug messages and checks for Residency Model")
DECLARE_DEBUG_VARIABLE(bool, EventsDebugEnable, false, "enables debug messages for events, virtual events, blocked enqueues, events trees etc.")
DECLARE_DEBUG_VARIABLE(bool, EventsTrackerEnable, false, "enables event graphs dumping")
DECLARE_DEBUG_VARIABLE(bool, PrintEMDebugInformation, false, "prints execution model related debug information")
DECLARE_DEBUG_VARIABLE(bool, PrintLWSSizes, false, "prints driver choosen local workgroup sizes")
DECLARE_DEBUG_VARIABLE(bool, PrintDispatchParameters, false, "prints dispatch paramters of kernels passed to clEnqueueNDRangeKernel")
DECLARE_DEBUG_VARIABLE(bool, PrintProgramBinaryProcessingTime, false, "prints execution time of Program::processGenBinary() method during program building")
DECLARE_DEBUG_VARIABLE(bool, PrintRelocations, false, "prints relocations debug information")
DECLARE_DEBUG_VARIABLE(bool, PrintTimestampPacketContents, false, "prints all timestamps values during profiling data calculation")
DECLARE_DEBUG_VARIABLE(bool, WddmResidencyLogger, false, "gather Wddm residency statistics to file")
DECLARE_DEBUG_VARIABLE(bool, PrintBOCreateDestroyResult, false, "tracks the result of creation and destruction of BOs")
DECLARE_DEBUG_VARIABLE(bool, PrintBOBindingResult, false, "tracks the result of binding and unbinding of BOs")
DECLARE_DEBUG_VARIABLE(bool, PrintTagAllocationAddress, false, "Print tag allocation address for each engine")
DECLARE_DEBUG_VARIABLE(bool, ProvideVerboseImplicitFlush, false, "provides verbose messages about implicit flush mechanism")
DECLARE_DEBUG_VARIABLE(bool, PrintBlitDispatchDetails, false, "Print blit dispatch details")

/*PERFORMANCE FLAGS*/
DECLARE_DEBUG_VARIABLE(bool, DisableZeroCopyForBuffers, false, "When active all buffer allocations will not share memory with CPU.")
DECLARE_DEBUG_VARIABLE(bool, DisableDcFlushInEpilogue, false, "Disable DC flush in epilogue")
DECLARE_DEBUG_VARIABLE(bool, EnableNullHardware, false, "works on Windows only, sets the Null Hardware flag that makes all Command buffers completed while GPU does nothing")
DECLARE_DEBUG_VARIABLE(bool, ForceLinearImages, false, "Force linear images. Default is Y-tiled.")
DECLARE_DEBUG_VARIABLE(bool, ForceSLML3Config, false, "Forces L3Config with SLM for all kernels")
DECLARE_DEBUG_VARIABLE(bool, Force32bitAddressing, false, "Forces 32 bit addresses to be used in 64 bit dll")
DECLARE_DEBUG_VARIABLE(bool, ForceCsrFlushing, false, "Forces flushing of command stream receiver")
DECLARE_DEBUG_VARIABLE(bool, ForceCsrReprogramming, false, "Forces reprogramming of command stream receiver")
DECLARE_DEBUG_VARIABLE(bool, OmitTimestampPacketDependencies, false, "Clears all node dependences on timestamp packet")
DECLARE_DEBUG_VARIABLE(bool, DisableStatelessToStatefulOptimization, false, "Disables stateless to stateful optimization for buffers")
DECLARE_DEBUG_VARIABLE(bool, DisableConcurrentBlockExecution, false, "disables concurrent block kernel execution")
DECLARE_DEBUG_VARIABLE(bool, UseNoRingFlushesKmdMode, true, "Windows only, passes flag to KMD that informs KMD to not emit any ring buffer flushes.")
DECLARE_DEBUG_VARIABLE(bool, DisableZeroCopyForUseHostPtr, false, "When active all buffer allocations created with CL_MEM_USE_HOST_PTR flag will not share memory with CPU.")
DECLARE_DEBUG_VARIABLE(int32_t, EnableHostPtrTracking, -1, "Enable host ptr tracking: -1 - default platform setting, 0 - disabled, 1 - enabled")
DECLARE_DEBUG_VARIABLE(int32_t, MaxHwThreadsPercent, 0, "If not zero then maximum number of used HW threads is capped to max * MaxHwThreadsPercent / 100")
DECLARE_DEBUG_VARIABLE(int32_t, MinHwThreadsUnoccupied, 0, "If not zero then maximum number of used HW threads is reduced by MinHwThreadsUnoccupied")
DECLARE_DEBUG_VARIABLE(int32_t, PerformImplicitFlushEveryEnqueueCount, -1, "If greater than 0, driver performs implicit flush every N submissions.")
DECLARE_DEBUG_VARIABLE(int32_t, PerformImplicitFlushForNewResource, -1, "-1: platform specific, 0: force disable, 1: force enable")
DECLARE_DEBUG_VARIABLE(int32_t, PerformImplicitFlushForIdleGpu, -1, "-1: platform specific, 0: force disable, 1: force enable")

/*DIRECT SUBMISSION FLAGS*/
DECLARE_DEBUG_VARIABLE(int32_t, EnableDirectSubmission, -1, "-1: default (disabled), 0: disable, 1:enable. Enables direct submission of command buffers bypassing KMD")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionBufferPlacement, -1, "-1: do not override, 0: non-system, 1: system")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionSemaphorePlacement, -1, "-1: do not override, 0: non-system, 1: system")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionBufferAddressing, -1, "-1: do not override, 0: not use 48bit, 1: use 48bit")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionSemaphoreAddressing, -1, "-1: do not override, 0: not use 48bit, 1: use 48bit")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionDisableCpuCacheFlush, -1, "-1: do not override, 0: disable, 1: enable")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionEnableDebugBuffer, 0, "0: diagnostic feature disabled - dispatch regular workload, 1: dispatch diagnostic buffer - mode 1 - single SDI command, 2: dispatch diagnostic buffer - mode 2 - no command")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionDiagnosticExecutionCount, 30, "Number of executions of EnableDebugBuffer modes within diagnostic run")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionOverrideBlitterSupport, -1, "Overrides default blitter support: -1: do not override, 0: disable engine support, 1: enable engine support with init start, 2: enable engine support without init start")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionOverrideRenderSupport, -1, "Overrides default render support: -1: do not override, 0: disable engine support, 1: enable engine support with init start, 2: enable engine support without init start")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionOverrideComputeSupport, -1, "Overrides default compute support: -1: do not override, 0: disable engine support, 1: enable engine support with init start, 2: enable engine support without init start")
DECLARE_DEBUG_VARIABLE(int32_t, DirectSubmissionDisableCacheFlush, -1, "-1: driver default, 0: additional cache flush is present 1: disable dispatching cache flush commands")
DECLARE_DEBUG_VARIABLE(bool, USMEvictAfterMigration, true, "Evict USM allocation after implicit migration to GPU")
DECLARE_DEBUG_VARIABLE(bool, DirectSubmissionDisableMonitorFence, false, "Disable dispatching monitor fence commands")

/*FEATURE FLAGS*/
DECLARE_DEBUG_VARIABLE(bool, EnableNV12, true, "Enables NV12 extension")
DECLARE_DEBUG_VARIABLE(bool, EnablePackedYuv, true, "Enables cl_packed_yuv extension")
DECLARE_DEBUG_VARIABLE(bool, EnableDeferredDeleter, true, "Enables async deleter")
DECLARE_DEBUG_VARIABLE(bool, EnableAsyncDestroyAllocations, true, "Enables async destroying graphics allocations in mem obj destructor")
DECLARE_DEBUG_VARIABLE(bool, EnableAsyncEventsHandler, true, "Enables async events handler")
DECLARE_DEBUG_VARIABLE(bool, EnableForcePin, true, "Enables early pinning for memory object")
DECLARE_DEBUG_VARIABLE(bool, EnableComputeWorkSizeND, true, "Enables different algorithm to compute local work size")
DECLARE_DEBUG_VARIABLE(bool, EnableMultiRootDeviceContexts, false, "Enables support for multi root device contexts")
DECLARE_DEBUG_VARIABLE(bool, EnableComputeWorkSizeSquared, false, "Enables algorithm to compute the most squared work group as possible")
DECLARE_DEBUG_VARIABLE(bool, EnableExtendedVaFormats, false, "Enable more formats in cl-va sharing")
DECLARE_DEBUG_VARIABLE(bool, AddClGlSharing, false, "Add cl-gl extension")
DECLARE_DEBUG_VARIABLE(bool, EnableFormatQuery, false, "Enable sharing format querying")
DECLARE_DEBUG_VARIABLE(bool, EnableFreeMemory, false, "Enable freeMemory in memory manager")
DECLARE_DEBUG_VARIABLE(bool, ForceSamplerLowFilteringPrecision, false, "Force Low Filtering Precision Sampler mode")
DECLARE_DEBUG_VARIABLE(int32_t, EnableKernelTunning, -1, "Perform a tunning of enqueue kernel, -1:default(disabled), 0:disable, 1:enable simple kernel tunning, 2:enable full kernel tunning")
DECLARE_DEBUG_VARIABLE(int32_t, EnableBOMmapCreate, -1, "Create BOs using mmap, -1:default, 0:disable(GEM_USERPTR), 1:enable")
DECLARE_DEBUG_VARIABLE(int32_t, EnableGemCloseWorker, -1, "Use asynchronous gem object closing, -1:default, 0:disable, 1:enable")
DECLARE_DEBUG_VARIABLE(int32_t, EnableIntelVme, -1, "-1: default, 0: disabled, 1: Enables cl_intel_motion_estimation extension")
DECLARE_DEBUG_VARIABLE(int32_t, EnableIntelAdvancedVme, -1, "-1: default, 0: disabled, 1: Enables cl_intel_advanced_motion_estimation extension")
DECLARE_DEBUG_VARIABLE(int32_t, EnableBlitterOperationsSupport, -1, "-1: default, 0: disable, 1: enable")
DECLARE_DEBUG_VARIABLE(int32_t, EnableBlitterForEnqueueOperations, -1, "Use Blitter engine for enqueue operations. -1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, EnableBlitterForReadWriteImage, -1, "Use Blitter engine for read/write image operations. -1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, EnableCacheFlushAfterWalker, -1, "-1: platform behavior, 0: disabled, 1: enabled. Adds dedicated cache flush command after WALKER command when surfaces used by kernel require to flush the cache")
DECLARE_DEBUG_VARIABLE(int32_t, EnableLocalMemory, -1, "-1: default behavior, 0: disabled, 1: enabled, Allows allocating graphics memory in Local Memory")
DECLARE_DEBUG_VARIABLE(int32_t, EnableStatelessToStatefulBufferOffsetOpt, -1, "-1: dont override, 0: disable, 1: enable, Enables buffer-offset improvement of the stateless to stateful optimization")
DECLARE_DEBUG_VARIABLE(int32_t, EnableVaLibCalls, -1, "-1: default, 0: disable, 1: enable cl-va sharing lib calls")
DECLARE_DEBUG_VARIABLE(int32_t, CreateMultipleRootDevices, 0, "0: default - disable, 1+: Driver will create multiple (N) devices during initialization.")
DECLARE_DEBUG_VARIABLE(int32_t, CreateMultipleSubDevices, 0, "0: default - disable, 1+: Driver will create multiple (N) sub devices during initialization.")
DECLARE_DEBUG_VARIABLE(int32_t, LimitAmountOfReturnedDevices, 0, "0: default - disable, 1+: Driver will limit the number of devices returned from clGetDeviceIds to N.")
DECLARE_DEBUG_VARIABLE(int32_t, Enable64kbpages, -1, "-1: default behaviour, 0 Disables, 1 Enables support for 64KB pages for driver allocated fine grain svm buffers")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideEnableKmdNotify, -1, "-1: dont override, 0: disable, 1: enable")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideKmdNotifyDelayMicroseconds, -1, "-1: dont override, 0: infinite timeout, >0: timeout in microseconds")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideEnableQuickKmdSleep, -1, "-1: dont override, 0: disable, 1: enable. It works only when Kmd Notify is enabled.")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideQuickKmdSleepDelayMicroseconds, -1, "-1: dont override, 0: infinite timeout, >0: timeout in microseconds")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideEnableQuickKmdSleepForSporadicWaits, -1, "-1: dont override, 0: disable, 1: enable. It works only when QuickKmdSleep is enabled.")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideDelayQuickKmdSleepForSporadicWaitsMicroseconds, -1, "-1: dont override, >0: timeout in microseconds")
DECLARE_DEBUG_VARIABLE(int32_t, PowerSavingMode, 0, "0: default 1: enable. Whenever driver waits on GPU and its not ready, put waiting thread to sleep and wait for notification.")
DECLARE_DEBUG_VARIABLE(int32_t, CsrDispatchMode, 0, "Chooses DispatchMode for Csr")
DECLARE_DEBUG_VARIABLE(int32_t, RenderCompressedImagesEnabled, -1, "-1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, RenderCompressedBuffersEnabled, -1, "-1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, EnableSharedSystemUsmSupport, -1, "-1: default, 0: shared system memory disabled, 1: shared system memory enabled")
DECLARE_DEBUG_VARIABLE(int32_t, EnablePassInlineData, -1, "-1: default, 0: Do not allow to pass inline data 1: Enable passing of inline data")
DECLARE_DEBUG_VARIABLE(int32_t, ForceFineGrainedSVMSupport, -1, "-1: default, 0: Do not report Fine Grained SVM capabilties 1: Report SVM Fine Grained capabilities if device supports SVM")
DECLARE_DEBUG_VARIABLE(int32_t, ForceDeviceEnqueueSupport, -1, "-1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, ForcePipeSupport, -1, "-1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, UseAsyncDrmExec, -1, "-1: default, 0: Disabled 1: Enabled. If enabled, pass EXEC_OBJECT_ASYNC to exec ioctl.")
DECLARE_DEBUG_VARIABLE(int32_t, UseBindlessMode, -1, "Use precompiled builtins in bindless mode, -1: api dependent, 0: disabled, 1: enabled")

/*DRIVER TOGGLES*/
DECLARE_DEBUG_VARIABLE(int32_t, ForceOCLVersion, 0, "Force specific OpenCL API version")
DECLARE_DEBUG_VARIABLE(int32_t, ForceOCL21FeaturesSupport, -1, "-1: default, 0: disable, 1:enable. Force support of OpenCL 2.0 and OpenCL 2.1 API features")
DECLARE_DEBUG_VARIABLE(int32_t, ForcePreemptionMode, -1, "Keep this variable in sync with PreemptionMode enum. -1 - devices default mode, 1 - disable, 2 - midBatch, 3 - threadGroup, 4 - midThread")
DECLARE_DEBUG_VARIABLE(int32_t, ForceKernelPreemptionMode, -1, "Keep this variable in sync with PreemptionMode enum. -1 - kernel default mode, 1 - disable, 2 - midBatch, 3 - threadGroup, 4 - midThread")
DECLARE_DEBUG_VARIABLE(int32_t, NodeOrdinal, -1, "-1: default do not override, 0: ENGINE_RCS")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideThreadArbitrationPolicy, -1, "-1 (dont override) or any valid config (0: Age Based, 1: Round Robin)")
DECLARE_DEBUG_VARIABLE(int32_t, OverrideAubDeviceId, -1, "-1 dont override, any other: use this value for AUB generation device id")
DECLARE_DEBUG_VARIABLE(int32_t, EnableTimestampPacket, -1, "-1: default, 0: disable, 1:enable. Write Timestamp Packet for each set of gpu walkers")
DECLARE_DEBUG_VARIABLE(int32_t, AllocateSharedAllocationsWithCpuAndGpuStorage, -1, "When enabled driver creates cpu & gpu storage for shared unified memory allocations. (-1 - devices default mode, 0 - disable, 1 - enable)")
DECLARE_DEBUG_VARIABLE(int32_t, UseKmdMigration, 0, "-1: devices default mode, 0: disable - pagefault handling by UMD using handler for SIGSEGV, 1: enable - pagefault handling by KMD, GEM objects migrated by KMD upon access)")
DECLARE_DEBUG_VARIABLE(int32_t, ForceSemaphoreDelayBetweenWaits, -1, "Specifies the minimum number of microseconds allowed for command streamer to wait before re-fetching the data. 0 - poll interval will be equal to the memory latency of the read completion")
DECLARE_DEBUG_VARIABLE(int32_t, ForceLocalMemoryAccessMode, -1, "-1: don't override, 0: default rules apply, 1: CPU can access local memory, 3: CPU never accesses local memory")
DECLARE_DEBUG_VARIABLE(int32_t, ForceUserptrAlignment, -1, "-1: no force (4kb), >0: n kb alignment")
DECLARE_DEBUG_VARIABLE(int32_t, PreferCopyEngineForCopyBufferToBuffer, -1, "-1: default, 0: prefer EUs, 1: prefer blitter")
DECLARE_DEBUG_VARIABLE(int64_t, ForceSystemMemoryPlacement, 0, "0: default,  >0: (bitmask) for given Graphics Allocation Type, force system memory placement")
DECLARE_DEBUG_VARIABLE(int64_t, ForceNonSystemMemoryPlacement, 0, "0: default,  >0: (bitmask) for given Graphics Allocation Type, force non-system memory placement")
DECLARE_DEBUG_VARIABLE(int64_t, DisableIndirectAccess, -1, "0: default,  0: Use indirect access settings provided by application, 1: Disable indirect access and ignore settings provided by application")
DECLARE_DEBUG_VARIABLE(int32_t, UseVmBind, 0, "Use new residency model on Linux (requires kernel support), -1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(int32_t, PassBoundBOToExec, -1, "Pass bound BOs to exec call to keep dependencies")
DECLARE_DEBUG_VARIABLE(int32_t, EnableStaticPartitioning, -1, "Divide workload into partitions during dispatch, -1: default, 0: disabled, 1: enabled")
DECLARE_DEBUG_VARIABLE(bool, UseMaxSimdSizeToDeduceMaxWorkgroupSize, false, "With this flag on, max workgroup size is deduced using SIMD32 instead of SIMD8, this causes the max wkg size to be 4 times bigger")
DECLARE_DEBUG_VARIABLE(bool, ReturnRawGpuTimestamps, false, "Driver returns raw GPU tiemstamps instead of calculated ones.")
DECLARE_DEBUG_VARIABLE(bool, ForcePerDssBackedBufferProgramming, false, "Always program per-DSS memory backed buffer in preamble")
DECLARE_DEBUG_VARIABLE(bool, DisableAtomicForPostSyncs, false, "When enabled, post syncs are not tracked with atomics")
DECLARE_DEBUG_VARIABLE(bool, UseCommandBufferHeaderSizeForWddmQueueSubmission, true, "0: Page size (4096), 1: sizeof(COMMAND_BUFFER_HEADER)")
DECLARE_DEBUG_VARIABLE(bool, DisableDeepBind, false, "Disable passing RTLD_DEEPBIND flag to all dlopen calls.")
