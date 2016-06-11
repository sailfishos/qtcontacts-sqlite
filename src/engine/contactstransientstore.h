/*
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Matt Vogt <matthew.vogt@jollamobile.com>
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


#ifndef __CONTACTSTRANSIENTSTORE_H__
#define __CONTACTSTRANSIENTSTORE_H__

#include "memorytable_p.h"

#include <QContactDetail>

#include <QDateTime>
#include <QPair>
#include <QSharedPointer>

QTCONTACTS_USE_NAMESPACE

class DataLockPrivate;
class ContactsTransientStore
{
public:
    class const_iterator : public MemoryTable::const_iterator
    {
        friend class ContactsTransientStore;

    protected:
        const_iterator(const MemoryTable *table, quint32 position);

    public:
        const_iterator(const const_iterator &other);
        const_iterator &operator=(const const_iterator &other);

        quint32 key();
        QPair<QDateTime, QList<QContactDetail> > value();
    };

    class DataLock
    {
    public:
        ~DataLock();

        DataLock(const DataLock &other);
        DataLock& operator=(const DataLock &other);

        operator bool() const;

    private:
        friend class ContactsTransientStore;

        DataLock(DataLockPrivate *);

        QSharedPointer<DataLockPrivate> lock;
    };

    ContactsTransientStore();
    ~ContactsTransientStore();

    bool open(bool nonprivileged, bool createIfNecessary, bool reinitialize);

    bool contains(quint32 contactId) const;

    QPair<QDateTime, QList<QContactDetail> > contactDetails(quint32 contactId) const;
    bool setContactDetails(quint32 contactId, const QDateTime &timestamp, const QList<QContactDetail> &details);

    bool remove(quint32 contactId);
    bool remove(const QList<quint32> &contactId);

    DataLock dataLock() const;

    const_iterator constBegin(const DataLock &) const;
    const_iterator constEnd(const DataLock &) const;

private:
    QString m_identifier;
};

#endif
