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

#ifndef QTCONTACTSSQLITE_CONTACTID_P_H
#define QTCONTACTSSQLITE_CONTACTID_P_H

#include <QtGlobal>
#include <QContactId>
#include <QContactCollectionId>
#include <QContact>
#include <QContactCollection>

QTCONTACTS_USE_NAMESPACE

namespace ContactId {
    QContactId apiId(const QContact &contact);
    QContactId apiId(quint32 databaseId, const QString &manager_uri);

    quint32 databaseId(const QContact &contact);
    quint32 databaseId(const QContactId &apiId);

    bool isValid(const QContact &contact);
    bool isValid(const QContactId &apiId);
    bool isValid(quint32 dbId);

    QString toString(const QContactId &apiId);
    QString toString(const QContact &c);

    QContactId fromString(const QString &id);
} // namespace ContactId

namespace ContactCollectionId {
    QContactCollectionId apiId(const QContactCollection &collection);
    QContactCollectionId apiId(quint32 databaseId, const QString &manager_uri);

    quint32 databaseId(const QContactCollection &collection);
    quint32 databaseId(const QContactCollectionId &apiId);

    bool isValid(const QContactCollection &collection);
    bool isValid(const QContactCollectionId &apiId);
    bool isValid(quint32 dbId);

    QString toString(const QContactCollectionId &apiId);
    QString toString(const QContactCollection &c);

    QContactCollectionId fromString(const QString &id);
} // namespace ContactId

#endif // QTCONTACTSSQLITE_CONTACTIDIMPL

