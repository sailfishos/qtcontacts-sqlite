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

#include "contactid_p.h"

#include <QContact>
#include <QContactManager>
#include <QDebug>

namespace {

QString dbIdToString(quint32 dbId, bool isCollection = false)
{
    return isCollection ? QStringLiteral("col-%1").arg(dbId)
                        : QStringLiteral("sql-%1").arg(dbId);
}

quint32 dbIdFromString(const QString &s, bool isCollection = false)
{
    if ((isCollection && s.startsWith(QStringLiteral("col-")))
            || (!isCollection && s.startsWith(QStringLiteral("sql-")))) {
        return s.mid(4).toUInt();
    }
    return 0;
}

QByteArray dbIdToByteArray(quint32 dbId, bool isCollection = false)
{
    return isCollection ? (QByteArrayLiteral("col-") + QByteArray::number(dbId))
                        : (QByteArrayLiteral("sql-") + QByteArray::number(dbId));
}

quint32 dbIdFromByteArray(const QByteArray &b, bool isCollection = false)
{
    if ((isCollection && b.startsWith(QByteArrayLiteral("col-")))
            || (!isCollection && b.startsWith(QByteArrayLiteral("sql-")))) {
        return b.mid(4).toUInt();
    }
    return 0;
}

}


namespace ContactId {

QContactId apiId(const QContact &contact)
{
    return contact.id();
}

QContactId apiId(quint32 dbId, const QString &manager_uri)
{
    return QContactId(manager_uri, dbIdToByteArray(dbId));
}

quint32 databaseId(const QContact &contact)
{
    return databaseId(contact.id());
}

quint32 databaseId(const QContactId &apiId)
{
    return dbIdFromByteArray(apiId.localId());
}

QString toString(const QContactId &apiId)
{
    return dbIdToString(databaseId(apiId));
}

QString toString(const QContact &c)
{
    return toString(c.id());
}

QContactId fromString(const QString &s, const QString &manager_uri)
{
    return apiId(dbIdFromString(s), manager_uri);
}

bool isValid(const QContact &contact)
{
    return isValid(databaseId(contact));
}

bool isValid(const QContactId &contactId)
{
    return isValid(databaseId(contactId));
}

bool isValid(quint32 dbId)
{
    return (dbId != 0);
}

} // namespace ContactId


namespace ContactCollectionId {

QContactCollectionId apiId(const QContactCollection &collection)
{
    return collection.id();
}

QContactCollectionId apiId(quint32 dbId, const QString &manager_uri)
{
    return QContactCollectionId(manager_uri, dbIdToByteArray(dbId, true));
}

quint32 databaseId(const QContactCollection &collection)
{
    return databaseId(collection.id());
}

quint32 databaseId(const QContactCollectionId &apiId)
{
    return dbIdFromByteArray(apiId.localId(), true);
}

QString toString(const QContactCollectionId &apiId)
{
    return dbIdToString(databaseId(apiId), true);
}

QString toString(const QContactCollection &c)
{
    return toString(c.id());
}

QContactCollectionId fromString(const QString &s, const QString &manager_uri)
{
    return apiId(dbIdFromString(s, true), manager_uri);
}

bool isValid(const QContactCollection &collection)
{
    return isValid(databaseId(collection));
}

bool isValid(const QContactCollectionId &collectionId)
{
    return isValid(databaseId(collectionId));
}

bool isValid(quint32 dbId)
{
    return (dbId != 0);
}

} // namespace ContactCollectionId
