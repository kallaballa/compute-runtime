/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/tools/source/sysman/pci/linux/os_pci_imp.h"

#include "level_zero/tools/source/sysman/linux/fs_access.h"
#include "level_zero/tools/source/sysman/sysman_const.h"

#include "sysman/pci/pci_imp.h"

#include <linux/pci_regs.h>

namespace L0 {

const std::string LinuxPciImp::deviceDir("device");
const std::string LinuxPciImp::resourceFile("device/resource");
const std::string LinuxPciImp::maxLinkSpeedFile("device/max_link_speed");
const std::string LinuxPciImp::maxLinkWidthFile("device/max_link_width");

ze_result_t LinuxPciImp::getProperties(zes_pci_properties_t *properties) {
    properties->haveBandwidthCounters = false;
    properties->havePacketCounters = false;
    properties->haveReplayCounters = false;
    return ZE_RESULT_SUCCESS;
}
ze_result_t LinuxPciImp::getPciBdf(std::string &bdf) {
    std::string bdfDir;
    ze_result_t result = pSysfsAccess->readSymLink(deviceDir, bdfDir);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }
    const auto loc = bdfDir.find_last_of('/');
    bdf = bdfDir.substr(loc + 1);
    return ZE_RESULT_SUCCESS;
}

ze_result_t LinuxPciImp::getMaxLinkSpeed(double &maxLinkSpeed) {
    ze_result_t result;
    if (isLmemSupported) {
        std::string rootPortPath;
        std::string realRootPath;
        result = pSysfsAccess->getRealPath(deviceDir, realRootPath);
        if (ZE_RESULT_SUCCESS != result) {
            maxLinkSpeed = 0;
            return result;
        }

        // we need to get actual values of speed and width at the Discrete card's root port.
        rootPortPath = pLinuxSysmanImp->getPciRootPortDirectoryPath(realRootPath);

        result = pfsAccess->read(rootPortPath + '/' + "max_link_speed", maxLinkSpeed);
        if (ZE_RESULT_SUCCESS != result) {
            maxLinkSpeed = 0;
            return result;
        }
    } else {
        result = pSysfsAccess->read(maxLinkSpeedFile, maxLinkSpeed);
        if (ZE_RESULT_SUCCESS != result) {
            maxLinkSpeed = 0;
            return result;
        }
    }
    return ZE_RESULT_SUCCESS;
}

ze_result_t LinuxPciImp::getMaxLinkWidth(int32_t &maxLinkwidth) {
    ze_result_t result;
    if (isLmemSupported) {
        std::string rootPortPath;
        std::string realRootPath;
        result = pSysfsAccess->getRealPath(deviceDir, realRootPath);
        if (ZE_RESULT_SUCCESS != result) {
            maxLinkwidth = -1;
            return result;
        }

        // we need to get actual values of speed and width at the Discrete card's root port.
        rootPortPath = pLinuxSysmanImp->getPciRootPortDirectoryPath(realRootPath);

        result = pfsAccess->read(rootPortPath + '/' + "max_link_width", maxLinkwidth);
        if (ZE_RESULT_SUCCESS != result) {
            maxLinkwidth = -1;
            return result;
        }
        if (maxLinkwidth == static_cast<int32_t>(unknownPcieLinkWidth)) {
            maxLinkwidth = -1;
        }
    } else {
        result = pSysfsAccess->read(maxLinkWidthFile, maxLinkwidth);
        if (ZE_RESULT_SUCCESS != result) {
            return result;
        }
        if (maxLinkwidth == static_cast<int32_t>(unknownPcieLinkWidth)) {
            maxLinkwidth = -1;
        }
    }
    return ZE_RESULT_SUCCESS;
}

void getBarBaseAndSize(std::string readBytes, uint64_t &baseAddr, uint64_t &barSize, uint64_t &barFlags) {

    unsigned long long start, end, flags;
    std::stringstream sStreamReadBytes;
    sStreamReadBytes << readBytes;
    sStreamReadBytes >> std::hex >> start;
    sStreamReadBytes >> end;
    sStreamReadBytes >> flags;

    flags &= 0xf;
    barFlags = flags;
    baseAddr = start;
    barSize = end - start + 1;
}

ze_result_t LinuxPciImp::initializeBarProperties(std::vector<zes_pci_bar_properties_t *> &pBarProperties) {
    std::vector<std::string> ReadBytes;
    ze_result_t result = pSysfsAccess->read(resourceFile, ReadBytes);
    if (result != ZE_RESULT_SUCCESS) {
        return result;
    }
    for (uint32_t i = 0; i <= maxPciBars; i++) {
        uint64_t baseAddr, barSize, barFlags;
        getBarBaseAndSize(ReadBytes[i], baseAddr, barSize, barFlags);
        if (baseAddr && !(barFlags & 0x1)) { // we do not update for I/O ports
            zes_pci_bar_properties_t *pBarProp = new zes_pci_bar_properties_t;
            memset(pBarProp, 0, sizeof(zes_pci_bar_properties_t));
            pBarProp->index = i;
            pBarProp->base = baseAddr;
            pBarProp->size = barSize;
            // Bar Flags Desc.
            // Bit-0 - Value 0x0 -> MMIO type BAR
            // Bit-0 - Value 0x1 -> I/O type BAR
            if (i == 0) { // GRaphics MMIO is at BAR0, and is a 64-bit
                pBarProp->type = ZES_PCI_BAR_TYPE_MMIO;
            }
            if (i == 2) {
                pBarProp->type = ZES_PCI_BAR_TYPE_MEM; // device memory is always at BAR2
            }
            if (i == 6) { // the 7th entry of resource file is expected to be ROM BAR
                pBarProp->type = ZES_PCI_BAR_TYPE_ROM;
            }
            pBarProperties.push_back(pBarProp);
        }
    }
    if (pBarProperties.size() == 0) {
        result = ZE_RESULT_ERROR_UNKNOWN;
    }
    return result;
}

uint32_t LinuxPciImp::getRebarCapabilityPos() {
    uint32_t pos = PCI_CFG_SPACE_SIZE;
    uint32_t header = 0;

    if (!configMemory) {
        return 0;
    }

    // Minimum 8 bytes per capability. Hence maximum capabilities that
    // could be present in PCI extended configuration space are
    // represented by loopCount.
    auto loopCount = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;
    header = getDwordFromConfig(pos);
    if (!header) {
        return 0;
    }

    while (loopCount-- > 0) {
        if (PCI_EXT_CAP_ID(header) == PCI_EXT_CAP_ID_REBAR) {
            return pos;
        }
        pos = PCI_EXT_CAP_NEXT(header);
        if (pos < PCI_CFG_SPACE_SIZE) {
            return 0;
        }
        header = getDwordFromConfig(pos);
    }
    return 0;
}

// Parse PCIe configuration space to see if resizable Bar is supported
bool LinuxPciImp::resizableBarSupported() {
    return (getRebarCapabilityPos() > 0);
}

bool LinuxPciImp::resizableBarEnabled(uint32_t barIndex) {
    bool isBarResizable = false;
    uint32_t capabilityRegister = 0, controlRegister = 0;
    uint32_t nBars = 1;
    auto rebarCapabilityPos = getRebarCapabilityPos();

    // If resizable Bar is not supported then return false.
    if (!rebarCapabilityPos) {
        return false;
    }

    // As per PCI spec, resizable BAR's capability structure's 52 byte length could be represented as:
    // --------------------------------------------------------------
    // | byte offset    |   Description of register                 |
    // -------------------------------------------------------------|
    // | +000h          |   PCI Express Extended Capability Header  |
    // -------------------------------------------------------------|
    // | +004h          |   Resizable BAR Capability Register (0)   |
    // -------------------------------------------------------------|
    // | +008h          |   Resizable BAR Control Register (0)      |
    // -------------------------------------------------------------|
    // | +00Ch          |   Resizable BAR Capability Register (1)   |
    // -------------------------------------------------------------|
    // | +010h          |   Resizable BAR Control Register (1)      |
    // -------------------------------------------------------------|
    // | +014h          |   ---                                     |
    // -------------------------------------------------------------|

    // Only first Control register(at offset 008h, as shown above), could tell about number of resizable Bars
    controlRegister = getDwordFromConfig(rebarCapabilityPos + PCI_REBAR_CTRL);
    nBars = BITS(controlRegister, 5, 3); // control register's bits 5,6 and 7 contain number of resizable bars information
    for (auto barNumber = 0u; barNumber < nBars; barNumber++) {
        uint32_t barId = 0;
        controlRegister = getDwordFromConfig(rebarCapabilityPos + PCI_REBAR_CTRL);
        barId = BITS(controlRegister, 0, 3); // Control register's bit 0,1,2 tells the index of bar
        if (barId == barIndex) {
            isBarResizable = true;
            break;
        }
        rebarCapabilityPos += 8;
    }

    if (isBarResizable == false) {
        return false;
    }

    capabilityRegister = getDwordFromConfig(rebarCapabilityPos + PCI_REBAR_CAP);
    // Capability register's bit 4 to 31 indicates supported Bar sizes.
    // In possibleBarSizes, position of each set bit indicates supported bar size. Example,  if set bit
    // position of possibleBarSizes is from 0 to n, then this indicates BAR size from 2^0 MB to 2^n MB
    auto possibleBarSizes = (capabilityRegister & PCI_REBAR_CAP_SIZES) >> 4; // First 4 bits are reserved
    uint32_t largestPossibleBarSize = 0;
    while (possibleBarSizes >>= 1) { // most significant set bit position of possibleBarSizes would tell larget possible bar size
        largestPossibleBarSize++;
    }

    // Control register's bit 8 to 13 indicates current BAR size in encoded form.
    // Example, real value of current size could be 2^currentSize MB
    auto currentSize = BITS(controlRegister, 8, 6);

    // If current size is equal to larget possible BAR size, it indicates resizable BAR is enabled.
    return (currentSize == largestPossibleBarSize);
}

ze_result_t LinuxPciImp::getState(zes_pci_state_t *state) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

void LinuxPciImp::pciExtendedConfigRead() {
    std::string pciConfigNode;
    pSysfsAccess->getRealPath("device/config", pciConfigNode);
    int fdConfig = -1;
    fdConfig = this->openFunction(pciConfigNode.c_str(), O_RDONLY);
    if (fdConfig < 0) {
        return;
    }
    configMemory = std::make_unique<uint8_t[]>(PCI_CFG_SPACE_EXP_SIZE);
    memset(configMemory.get(), 0, PCI_CFG_SPACE_EXP_SIZE);
    this->preadFunction(fdConfig, configMemory.get(), PCI_CFG_SPACE_EXP_SIZE, 0);
    this->closeFunction(fdConfig);
}

LinuxPciImp::LinuxPciImp(OsSysman *pOsSysman) {
    pLinuxSysmanImp = static_cast<LinuxSysmanImp *>(pOsSysman);
    pSysfsAccess = &pLinuxSysmanImp->getSysfsAccess();
    pfsAccess = &pLinuxSysmanImp->getFsAccess();
    Device *pDevice = pLinuxSysmanImp->getDeviceHandle();
    isLmemSupported = pDevice->getDriverHandle()->getMemoryManager()->isLocalMemorySupported(pDevice->getRootDeviceIndex());
    if (pSysfsAccess->isRootUser()) {
        pciExtendedConfigRead();
    }
}

OsPci *OsPci::create(OsSysman *pOsSysman) {
    LinuxPciImp *pLinuxPciImp = new LinuxPciImp(pOsSysman);
    return static_cast<OsPci *>(pLinuxPciImp);
}

} // namespace L0
