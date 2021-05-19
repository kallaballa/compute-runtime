/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

namespace L0 {
namespace ult {

template <typename Type>
struct WhiteBox : public Type {
    using Type::Type;
};

template <typename Type>
WhiteBox<Type> *whitebox_cast(Type *obj) {
    return static_cast<WhiteBox<Type> *>(obj);
}

template <typename Type>
WhiteBox<Type> &whitebox_cast(Type &obj) {
    return static_cast<WhiteBox<Type> &>(obj);
}

template <typename Type>
Type *blackbox_cast(WhiteBox<Type> *obj) {
    return static_cast<Type *>(obj);
}

} // namespace ult
} // namespace L0
