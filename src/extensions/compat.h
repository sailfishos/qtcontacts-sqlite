/*
 * Copyright (C) 2026 Adriaan de Groot <groot@kde.org>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */
#ifndef EXTENSIONS_COMPAT_H
#define EXTENSIONS_COMPAT_H

#include <QVariant>

/**
 * @file
 *
 * Defines functions for doing QVariant type-checking. (Long) Before
 * Qt 5.14, using v.type() == QVariant::<type-id> was the way to go,
 * but now it is QMetaType that is responsible for the type-ids.
 *
 * Introduce convenience functions for common "is-this-a ..." questions.
 */

[[nodiscard]] inline bool isVariantType(const QVariant & v, QMetaType::Type type)
{
#if QT_VERSION < 0x060000
    return v.type() == static_cast<QVariant::Type>(type);
#else
    return v.typeId() == type;
#endif
}

[[nodiscard]] inline bool isString(const QVariant & v)
{
    return isVariantType(v,QMetaType::QString);
}

[[nodiscard]] inline bool isInvalid(const QVariant & v)
{
    return isVariantType(v,QMetaType::UnknownType);
}

[[nodiscard]] inline bool isByteArray(const QVariant & v)
{
    return isVariantType(v,QMetaType::QByteArray);
}

[[nodiscard]] inline bool isUrl(const QVariant & v)
{
    return isVariantType(v,QMetaType::QUrl);
}

#endif
