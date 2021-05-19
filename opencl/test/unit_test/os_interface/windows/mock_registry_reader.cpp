/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/windows/windows_wrapper.h"

uint32_t regOpenKeySuccessCount = 0u;
uint32_t regQueryValueSuccessCount = 0u;
uint64_t regQueryValueExpectedData = 0ull;
const HKEY validHkey = reinterpret_cast<HKEY>(0);

LSTATUS APIENTRY RegOpenKeyExA(
    HKEY hKey,
    LPCSTR lpSubKey,
    DWORD ulOptions,
    REGSAM samDesired,
    PHKEY phkResult) {
    if (regOpenKeySuccessCount > 0) {
        regOpenKeySuccessCount--;
        if (phkResult) {
            *phkResult = validHkey;
        }
        return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;
};

LSTATUS APIENTRY RegQueryValueExA(
    HKEY hKey,
    LPCSTR lpValueName,
    LPDWORD lpReserved,
    LPDWORD lpType,
    LPBYTE lpData,
    LPDWORD lpcbData) {
    if (hKey == validHkey && regQueryValueSuccessCount > 0) {
        regQueryValueSuccessCount--;

        if (lpcbData) {
            if (strcmp(lpValueName, "settingSourceString") == 0) {
                const auto settingSource = "registry";
                if (lpData) {
                    strcpy(reinterpret_cast<char *>(lpData), settingSource);
                } else {
                    *lpcbData = static_cast<DWORD>(strlen(settingSource) + 1u);
                    if (lpType) {
                        *lpType = REG_SZ;
                    }
                }
            } else if (strcmp(lpValueName, "settingSourceInt") == 0) {
                if (lpData) {
                    *reinterpret_cast<DWORD *>(lpData) = 1;
                } else {
                    *lpcbData = sizeof(DWORD);
                }
            } else if (strcmp(lpValueName, "settingSourceInt64") == 0) {
                if (lpData) {
                    *reinterpret_cast<INT64 *>(lpData) = 0xffffffffeeeeeeee;
                } else {
                    *lpcbData = sizeof(INT64);
                }
            } else if (strcmp(lpValueName, "settingSourceBinary") == 0) {
                const auto settingSource = L"registry";
                auto size = wcslen(settingSource) * sizeof(wchar_t);
                if (lpData) {
                    memcpy(reinterpret_cast<wchar_t *>(lpData), settingSource, size);
                } else {
                    *lpcbData = static_cast<DWORD>(size);
                    if (lpType) {
                        *lpType = REG_BINARY;
                    }
                }
            } else if (strcmp(lpValueName, "boolRegistryKey") == 0) {
                if (*lpcbData == sizeof(int64_t)) {
                    if (lpData) {
                        *reinterpret_cast<uint64_t *>(lpData) = regQueryValueExpectedData;
                    }
                }
            }
        }

        return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;
};
