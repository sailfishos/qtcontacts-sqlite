/*
 * Copyright (C) 2013 Jolla Ltd. <chris.adams@jollamobile.com>
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

const QString default_uri = QString::fromLatin1("org.nemomobile.contacts.sqlite");

QString dbIdToString(quint32 dbId)
{
    return QString::fromLatin1("sql-%1").arg(dbId);
}

quint32 dbIdFromString(const QString &s)
{
    if (s.startsWith(QString::fromLatin1("sql-"))) {
        return s.mid(4).toUInt();
    }
    return 0;
}

}

#include <QContactManagerEngine>

QContactId ContactId::apiId(const QContact &contact)
{
    return contact.id();
}

QContactId ContactId::apiId(quint32 dbId)
{
    ContactId *eid = new ContactId(dbId);
    return QContactId(eid);
}

quint32 ContactId::databaseId(const QContact &contact)
{
    return databaseId(contact.id());
}

quint32 ContactId::databaseId(const QContactId &apiId)
{
    if (const QContactEngineId *eid = QContactManagerEngine::engineId(apiId)) {
        const ContactId *iid = static_cast<const ContactId*>(eid);
        return iid->m_databaseId;
    }
    return 0;
}

const QContactId &ContactId::contactId(const QContactId &apiId)
{
    return apiId;
}

QContactId ContactId::fromString(const QString &s)
{
    return apiId(dbIdFromString(s));
}

ContactId::ContactId(quint32 dbId)
    : QContactEngineId()
    , m_databaseId(dbId)
{
}

ContactId::ContactId(const QString &s)
    : QContactEngineId()
    , m_databaseId(dbIdFromString(s))
{
}

bool ContactId::isEqualTo(const QContactEngineId *other) const
{
    return m_databaseId == static_cast<const ContactId*>(other)->m_databaseId;
}

bool ContactId::isLessThan(const QContactEngineId *other) const
{
    return m_databaseId < static_cast<const ContactId*>(other)->m_databaseId;
}

QString ContactId::managerUri() const
{
    return QContactManager::buildUri(default_uri, QMap<QString, QString>());
}

QContactEngineId* ContactId::clone() const
{
    return new ContactId(m_databaseId);
}

QString ContactId::toString() const
{
    return dbIdToString(m_databaseId);
}

uint ContactId::hash() const
{
    return m_databaseId;
}

#ifndef QT_NO_DEBUG_STREAM
QDebug &ContactId::debugStreamOut(QDebug &dbg) const
{
    return dbg << dbIdToString(m_databaseId);
}
#endif // QT_NO_DEBUG_STREAM

bool ContactId::isValid(const QContact &contact)
{
    return isValid(databaseId(contact));
}

bool ContactId::isValid(const QContactId &contactId)
{
    return isValid(databaseId(contactId));
}

bool ContactId::isValid(quint32 dbId)
{
    return (dbId != 0);
}

QString ContactId::toString(const QContactId &apiId)
{
    return dbIdToString(databaseId(apiId));
}

QString ContactId::toString(const QContact &c)
{
    return toString(c.id());
}

