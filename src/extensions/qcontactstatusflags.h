/*
 * Copyright (C) 2013 - 2014 Jolla Ltd.
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#ifndef QCONTACTSTATUSFLAGS_H
#define QCONTACTSTATUSFLAGS_H

#include <QContactDetail>
#include <QContactDetailFilter>

QT_BEGIN_NAMESPACE_CONTACTS

class QContactStatusFlags : public QContactDetail
{
public:
    Q_DECLARE_CUSTOM_CONTACT_DETAIL(QContactStatusFlags)

    enum {
        FieldFlags = 0
    };

    enum Flag {
        HasPhoneNumber = (1 << 0),
        HasEmailAddress = (1 << 1),
        HasOnlineAccount = (1 << 2),
        IsOnline = (1 << 3),
        IsDeactivated = (1 << 4),
        IsAdded = (1 << 5),
        IsModified = (1 << 6),
        IsDeleted = (1 << 7)
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    void setFlag(Flag flag, bool b);
    void setFlags(Flags flags);
    Flags flags() const;

    void setFlagsValue(quint64 value);
    quint64 flagsValue() const;

    bool testFlag(Flag flag) const;

    static QContactDetailFilter matchFlag(Flag flag, QContactFilter::MatchFlags matchFlags = QContactFilter::MatchContains);
    static QContactDetailFilter matchFlags(Flags flags, QContactFilter::MatchFlags matchFlags = QContactFilter::MatchContains);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QContactStatusFlags::Flags)

QT_END_NAMESPACE_CONTACTS

#endif
