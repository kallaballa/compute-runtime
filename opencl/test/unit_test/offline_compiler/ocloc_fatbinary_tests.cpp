/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/offline_compiler/ocloc_fatbinary_tests.h"

#include "shared/source/helpers/hw_helper.h"

#include <algorithm>
#include <unordered_set>

namespace NEO {

TEST(OclocFatBinaryRequestedFatBinary, WhenDeviceArgMissingThenReturnsFalse) {
    const char *args[] = {"ocloc", "-aaa", "*", "-device", "*"};

    EXPECT_FALSE(NEO::requestedFatBinary(0, nullptr));
    EXPECT_FALSE(NEO::requestedFatBinary(1, args));
    EXPECT_FALSE(NEO::requestedFatBinary(2, args));
    EXPECT_FALSE(NEO::requestedFatBinary(3, args));
    EXPECT_FALSE(NEO::requestedFatBinary(4, args));
}

TEST(OclocFatBinaryRequestedFatBinary, WhenDeviceArgProvidedAndContainsFatbinaryArgFormatThenReturnsTrue) {
    const char *allPlatforms[] = {"ocloc", "-device", "*"};
    const char *manyPlatforms[] = {"ocloc", "-device", "a,b"};
    const char *manyGens[] = {"ocloc", "-device", "gen0,gen1"};
    const char *gen[] = {"ocloc", "-device", "gen0"};
    const char *rangePlatformFrom[] = {"ocloc", "-device", "skl-"};
    const char *rangePlatformTo[] = {"ocloc", "-device", "-skl"};
    const char *rangePlatformBounds[] = {"ocloc", "-device", "skl-icllp"};
    const char *rangeGenFrom[] = {"ocloc", "-device", "gen0-"};
    const char *rangeGenTo[] = {"ocloc", "-device", "-gen5"};
    const char *rangeGenBounds[] = {"ocloc", "-device", "gen0-gen5"};

    EXPECT_TRUE(NEO::requestedFatBinary(3, allPlatforms));
    EXPECT_TRUE(NEO::requestedFatBinary(3, manyPlatforms));
    EXPECT_TRUE(NEO::requestedFatBinary(3, manyGens));
    EXPECT_TRUE(NEO::requestedFatBinary(3, gen));
    EXPECT_TRUE(NEO::requestedFatBinary(3, rangePlatformFrom));
    EXPECT_TRUE(NEO::requestedFatBinary(3, rangePlatformTo));
    EXPECT_TRUE(NEO::requestedFatBinary(3, rangePlatformBounds));
    EXPECT_TRUE(NEO::requestedFatBinary(3, rangeGenFrom));
    EXPECT_TRUE(NEO::requestedFatBinary(3, rangeGenTo));
    EXPECT_TRUE(NEO::requestedFatBinary(3, rangeGenBounds));
}

TEST(OclocFatBinaryRequestedFatBinary, WhenDeviceArgProvidedButDoesnNotContainFatbinaryArgFormatThenReturnsFalse) {
    const char *skl[] = {"ocloc", "-device", "skl"};
    EXPECT_FALSE(NEO::requestedFatBinary(3, skl));
}

TEST(OclocFatBinaryGetAllSupportedTargetPlatforms, WhenRequestedThenReturnsAllPlatformsWithNonNullHardwarePrefixes) {
    auto platforms = NEO::getAllSupportedTargetPlatforms();
    std::unordered_set<uint32_t> platformsSet(platforms.begin(), platforms.end());
    for (unsigned int productId = 0; productId < IGFX_MAX_PRODUCT; ++productId) {
        if (nullptr != NEO::hardwarePrefix[productId]) {
            EXPECT_EQ(1U, platformsSet.count(static_cast<PRODUCT_FAMILY>(productId))) << productId;
        } else {
            EXPECT_EQ(0U, platformsSet.count(static_cast<PRODUCT_FAMILY>(productId))) << productId;
        }
    }
}

TEST(OclocFatBinaryToProductNames, GivenListOfProductIdsThenReturnsListOfHardwarePrefixes) {
    auto platforms = NEO::getAllSupportedTargetPlatforms();
    auto names = NEO::toProductNames(platforms);
    EXPECT_EQ(names.size(), platforms.size());
}

TEST(OclocFatBinaryAsProductId, GivenEnabledPlatformNameThenReturnsProperPlatformId) {
    auto platforms = NEO::getAllSupportedTargetPlatforms();
    auto names = NEO::toProductNames(platforms);
    for (size_t i = 0; i < platforms.size(); ++i) {
        auto idByName = NEO::asProductId(names[i], platforms);
        EXPECT_EQ(idByName, platforms[i]) << names[i].data() << " : " << platforms[i] << " != " << idByName;
    }
}

TEST(OclocFatBinaryAsProductId, GivenDisabledPlatformNameThenReturnsUnknownPlatformId) {
    auto platforms = NEO::getAllSupportedTargetPlatforms();
    auto names = NEO::toProductNames(platforms);
    platforms.clear();
    for (size_t i = 0; i < platforms.size(); ++i) {
        auto idByName = NEO::asProductId(names[i], platforms);
        EXPECT_EQ(IGFX_UNKNOWN, platforms[i]) << names[i].data() << " : IGFX_UNKNOWN != " << idByName;
    }
}

TEST(OclocFatBinaryAsGfxCoreIdList, GivenEnabledGfxCoreNameThenReturnsNonEmptyList) {
    for (unsigned int coreId = 0; coreId < IGFX_MAX_CORE; ++coreId) {
        if (nullptr != NEO::familyName[coreId]) {
            EXPECT_FALSE(NEO::asGfxCoreIdList(ConstStringRef(NEO::familyName[coreId], strlen(NEO::familyName[coreId]))).empty());
            std::string caseInsesitive = NEO::familyName[coreId];
            caseInsesitive[0] = 'g';
            EXPECT_FALSE(NEO::asGfxCoreIdList(caseInsesitive).empty());
        }
    }
}

TEST(OclocFatBinaryAsGfxCoreIdList, GivenDisabledGfxCoreNameThenReturnsEmptyList) {
    EXPECT_TRUE(NEO::asGfxCoreIdList(ConstStringRef("genA")).empty());
    EXPECT_TRUE(NEO::asGfxCoreIdList(ConstStringRef("gen0")).empty());
    EXPECT_TRUE(NEO::asGfxCoreIdList(ConstStringRef("gen1")).empty());
    EXPECT_TRUE(NEO::asGfxCoreIdList(ConstStringRef("gen2")).empty());
}

TEST(OclocFatBinaryAppendPlatformsForGfxCore, GivenCoreIdThenAppendsEnabledProductIdsThatMatch) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    auto platform0 = allEnabledPlatforms[0];
    auto gfxCore0 = NEO::hardwareInfoTable[platform0]->platform.eRenderCoreFamily;
    std::vector<PRODUCT_FAMILY> appendedPlatforms;
    NEO::appendPlatformsForGfxCore(gfxCore0, allEnabledPlatforms, appendedPlatforms);
    std::unordered_set<uint32_t> appendedPlatformsSet(appendedPlatforms.begin(), appendedPlatforms.end());
    EXPECT_EQ(1U, appendedPlatformsSet.count(platform0));
    for (auto platformId : allEnabledPlatforms) {
        if (gfxCore0 == NEO::hardwareInfoTable[platformId]->platform.eRenderCoreFamily) {
            EXPECT_EQ(1U, appendedPlatformsSet.count(platformId)) << platformId;
        } else {
            EXPECT_EQ(0U, appendedPlatformsSet.count(platformId)) << platformId;
        }
    }

    NEO::appendPlatformsForGfxCore(gfxCore0, allEnabledPlatforms, appendedPlatforms);
    EXPECT_EQ(2 * appendedPlatformsSet.size(), appendedPlatforms.size());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenAsterixThenReturnAllEnabledPlatforms) {
    auto allEnabledPlatformsIds = NEO::getAllSupportedTargetPlatforms();
    auto expected = NEO::toProductNames(allEnabledPlatformsIds);
    auto got = NEO::getTargetPlatformsForFatbinary("*", oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenThenReturnAllEnabledPlatformsThatMatch) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    auto platform0 = allEnabledPlatforms[0];
    auto gfxCore0 = NEO::hardwareInfoTable[platform0]->platform.eRenderCoreFamily;
    std::string genName = NEO::familyName[gfxCore0];
    genName[0] = 'g'; // ocloc uses lower case

    std::vector<PRODUCT_FAMILY> platformsForGen;
    NEO::appendPlatformsForGfxCore(gfxCore0, allEnabledPlatforms, platformsForGen);
    auto expected = NEO::toProductNames(platformsForGen);
    auto got = NEO::getTargetPlatformsForFatbinary(genName, oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenMutiplePlatformThenReturnThosePlatforms) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    if (allEnabledPlatforms.size() < 2) {
        return;
    }
    auto platform0 = allEnabledPlatforms[0];
    std::string platform0Name = NEO::hardwarePrefix[platform0];
    auto platform1 = allEnabledPlatforms[1];
    std::string platform1Name = NEO::hardwarePrefix[platform1];

    std::vector<ConstStringRef> expected{platform0Name, platform1Name};
    auto got = NEO::getTargetPlatformsForFatbinary(platform0Name + "," + platform1Name, oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenPlatformOpenRangeFromThenReturnAllEnabledPlatformsThatMatch) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    if (allEnabledPlatforms.size() < 3) {
        return;
    }
    auto platform0 = allEnabledPlatforms[allEnabledPlatforms.size() / 2];
    std::string platformName = NEO::hardwarePrefix[platform0];

    std::vector<PRODUCT_FAMILY> expectedPlatforms;
    auto platformFrom = std::find(allEnabledPlatforms.begin(), allEnabledPlatforms.end(), platform0);
    expectedPlatforms.insert(expectedPlatforms.end(), platformFrom, allEnabledPlatforms.end());
    auto expected = NEO::toProductNames(expectedPlatforms);
    auto got = NEO::getTargetPlatformsForFatbinary(platformName + "-", oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenPlatformOpenRangeToThenReturnAllEnabledPlatformsThatMatch) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    if (allEnabledPlatforms.size() < 3) {
        return;
    }
    auto platform0 = allEnabledPlatforms[allEnabledPlatforms.size() / 2];
    std::string platformName = NEO::hardwarePrefix[platform0];

    std::vector<PRODUCT_FAMILY> expectedPlatforms;
    auto platformTo = std::find(allEnabledPlatforms.begin(), allEnabledPlatforms.end(), platform0);
    expectedPlatforms.insert(expectedPlatforms.end(), allEnabledPlatforms.begin(), platformTo + 1);
    auto expected = NEO::toProductNames(expectedPlatforms);
    auto got = NEO::getTargetPlatformsForFatbinary("-" + platformName, oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenPlatformClosedRangeThenReturnAllEnabledPlatformsThatMatch) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    if (allEnabledPlatforms.size() < 4) {
        return;
    }
    auto platformFrom = allEnabledPlatforms[1];
    auto platformTo = allEnabledPlatforms[allEnabledPlatforms.size() - 2];
    std::string platformNameFrom = NEO::hardwarePrefix[platformFrom];
    std::string platformNameTo = NEO::hardwarePrefix[platformTo];

    std::vector<PRODUCT_FAMILY> expectedPlatforms;
    expectedPlatforms.insert(expectedPlatforms.end(), allEnabledPlatforms.begin() + 1, allEnabledPlatforms.begin() + allEnabledPlatforms.size() - 1);
    auto expected = NEO::toProductNames(expectedPlatforms);
    auto got = NEO::getTargetPlatformsForFatbinary(platformNameFrom + "-" + platformNameTo, oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);

    got = NEO::getTargetPlatformsForFatbinary(platformNameTo + "-" + platformNameFrom, oclocArgHelperWithoutInput.get()); // swap min with max implicitly
    EXPECT_EQ(expected, got);
}

std::vector<GFXCORE_FAMILY> getEnabledCores() {
    std::vector<GFXCORE_FAMILY> ret;
    for (unsigned int coreId = 0; coreId < IGFX_MAX_CORE; ++coreId) {
        if (nullptr != NEO::familyName[coreId]) {
            ret.push_back(static_cast<GFXCORE_FAMILY>(coreId));
        }
    }
    return ret;
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenOpenRangeFromThenReturnAllEnabledPlatformsThatMatch) {
    auto allSupportedPlatforms = NEO::getAllSupportedTargetPlatforms();

    auto allEnabledCores = getEnabledCores();
    if (allEnabledCores.size() < 3) {
        return;
    }
    auto core0 = allEnabledCores[allEnabledCores.size() / 2];
    std::string genName = NEO::familyName[core0];
    genName[0] = 'g'; // ocloc uses lower case

    std::vector<PRODUCT_FAMILY> expectedPlatforms;
    unsigned int coreIt = core0;
    while (coreIt < static_cast<unsigned int>(IGFX_MAX_CORE)) {
        NEO::appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(coreIt), allSupportedPlatforms, expectedPlatforms);
        ++coreIt;
    }

    auto expected = NEO::toProductNames(expectedPlatforms);
    auto got = NEO::getTargetPlatformsForFatbinary(genName + "-", oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenOpenRangeToThenReturnAllEnabledPlatformsThatMatch) {
    auto allSupportedPlatforms = NEO::getAllSupportedTargetPlatforms();

    auto allEnabledCores = getEnabledCores();
    if (allEnabledCores.size() < 3) {
        return;
    }
    auto core0 = allEnabledCores[allEnabledCores.size() / 2];
    std::string genName = NEO::familyName[core0];
    genName[0] = 'g'; // ocloc uses lower case

    std::vector<PRODUCT_FAMILY> expectedPlatforms;
    unsigned int coreIt = IGFX_UNKNOWN_CORE;
    ++coreIt;
    while (coreIt <= static_cast<unsigned int>(core0)) {
        NEO::appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(coreIt), allSupportedPlatforms, expectedPlatforms);
        ++coreIt;
    }

    auto expected = NEO::toProductNames(expectedPlatforms);
    auto got = NEO::getTargetPlatformsForFatbinary("-" + genName, oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenClosedRangeThenReturnAllEnabledPlatformsThatMatch) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    auto allEnabledCores = getEnabledCores();
    if (allEnabledCores.size() < 4) {
        return;
    }
    auto genFrom = allEnabledCores[1];
    auto genTo = allEnabledCores[allEnabledCores.size() - 2];
    std::string genNameFrom = NEO::familyName[genFrom];
    genNameFrom[0] = 'g';
    std::string genNameTo = NEO::familyName[genTo];
    genNameTo[0] = 'g';

    std::vector<PRODUCT_FAMILY> expectedPlatforms;
    auto genIt = genFrom;
    while (genIt <= genTo) {
        NEO::appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(genIt), allEnabledPlatforms, expectedPlatforms);
        genIt = static_cast<GFXCORE_FAMILY>(static_cast<unsigned int>(genIt) + 1);
    }

    auto expected = NEO::toProductNames(expectedPlatforms);
    auto got = NEO::getTargetPlatformsForFatbinary(genNameFrom + "-" + genNameTo, oclocArgHelperWithoutInput.get());
    EXPECT_EQ(expected, got);

    got = NEO::getTargetPlatformsForFatbinary(genNameTo + "-" + genNameFrom, oclocArgHelperWithoutInput.get()); // swap min with max implicitly
    EXPECT_EQ(expected, got);
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenUnkownGenThenReturnEmptyList) {
    auto got = NEO::getTargetPlatformsForFatbinary("gen0", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenMutiplePlatformWhenAnyOfPlatformsIsUnknownThenReturnEmptyList) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    auto platform0 = allEnabledPlatforms[0];
    std::string platform0Name = NEO::hardwarePrefix[platform0];

    auto got = NEO::getTargetPlatformsForFatbinary(platform0Name + ",unk", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenPlatformOpenRangeFromWhenPlatformsIsUnkownThenReturnEmptyList) {
    auto got = NEO::getTargetPlatformsForFatbinary("unk-", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenPlatformOpenRangeToWhenPlatformsIsUnkownThenReturnEmptyList) {
    auto got = NEO::getTargetPlatformsForFatbinary("-unk", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenPlatformClosedRangeWhenAnyOfPlatformsIsUnkownThenReturnEmptyList) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    auto platform0 = allEnabledPlatforms[0];
    std::string platform0Name = NEO::hardwarePrefix[platform0];

    auto got = NEO::getTargetPlatformsForFatbinary("unk-" + platform0Name, oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());

    got = NEO::getTargetPlatformsForFatbinary(platform0Name + "-unk", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenOpenRangeFromWhenGenIsUnknownThenReturnEmptyList) {
    auto got = NEO::getTargetPlatformsForFatbinary("gen2-", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenOpenRangeToWhenGenIsUnknownThenReturnEmptyList) {
    auto got = NEO::getTargetPlatformsForFatbinary("-gen2", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}

TEST_F(OclocFatBinaryGetTargetPlatformsForFatbinary, GivenGenClosedRangeWhenAnyOfGensIsUnknownThenReturnEmptyList) {
    auto allEnabledPlatforms = NEO::getAllSupportedTargetPlatforms();
    auto platform0 = allEnabledPlatforms[0];
    auto gfxCore0 = NEO::hardwareInfoTable[platform0]->platform.eRenderCoreFamily;
    std::string genName = NEO::familyName[gfxCore0];
    genName[0] = 'g'; // ocloc uses lower case

    auto got = NEO::getTargetPlatformsForFatbinary("gen2-" + genName, oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());

    got = NEO::getTargetPlatformsForFatbinary(genName + "-gen2", oclocArgHelperWithoutInput.get());
    EXPECT_TRUE(got.empty());
}
} // namespace NEO
