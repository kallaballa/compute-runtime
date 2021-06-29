/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/command_stream/command_stream_receiver_with_aub_dump.inl"
#include "opencl/source/os_interface/windows/device_command_stream.inl"
#include "opencl/source/os_interface/windows/wddm_device_command_stream.inl"

namespace NEO {

template class DeviceCommandStreamReceiver<XeHpFamily>;
template class WddmCommandStreamReceiver<XeHpFamily>;
template class CommandStreamReceiverWithAUBDump<WddmCommandStreamReceiver<XeHpFamily>>;
} // namespace NEO
