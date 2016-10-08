/*
 * Copyright (C) 2013 Jolla Ltd. <mattthew.vogt@jollamobile.com>
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

#ifndef QCONTACTSTATUSFLAGS_IMPL_H
#define QCONTACTSTATUSFLAGS_IMPL_H

#include "qcontactstatusflags.h"
#include "qtcontacts-extensions.h"

QTCONTACTS_USE_NAMESPACE

void QContactStatusFlags::setFlag(Flag flag, bool b)
{
    quint64 flagsValue = value<quint64>(FieldFlags);
    if (b) {
        flagsValue |= static_cast<quint64>(flag);
    } else {
        flagsValue &= ~(static_cast<quint64>(flag));
    }
    setFlagsValue(flagsValue);
}

void QContactStatusFlags::setFlags(Flags flags)
{
    setFlagsValue(static_cast<quint64>(flags));
}

QContactStatusFlags::Flags QContactStatusFlags::flags() const
{
    return Flags(static_cast<ulong>(flagsValue()));
}

void QContactStatusFlags::setFlagsValue(quint64 value)
{
    setValue(FieldFlags, value);
}

quint64 QContactStatusFlags::flagsValue() const
{
    return value<quint64>(FieldFlags);
}

bool QContactStatusFlags::testFlag(Flag flag) const
{
    return flags().testFlag(flag);
}

QContactDetailFilter QContactStatusFlags::matchFlag(Flag flag, QContactFilter::MatchFlags matchFlags)
{
    return QContactStatusFlags::matchFlags(Flags(flag), matchFlags);
}

QContactDetailFilter QContactStatusFlags::matchFlags(Flags flags, QContactFilter::MatchFlags matchFlags)
{
    QContactDetailFilter filter;
    filter.setDetailType(QContactStatusFlags::Type, FieldFlags);
    filter.setValue(static_cast<quint64>(flags));
    filter.setMatchFlags(matchFlags);
    return filter;
}

const QContactDetail::DetailType QContactStatusFlags::Type(static_cast<QContactDetail::DetailType>(QContactDetail__TypeStatusFlags));

#endif
