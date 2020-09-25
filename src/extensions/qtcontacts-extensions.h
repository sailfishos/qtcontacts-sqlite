/*
 * Copyright (C) 2013 - 2014 Jolla Ltd.
 * Copyright (C) 2019 - 2020 Open Mobile Platform LLC.
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

#ifndef QTCONTACTS_EXTENSIONS_H
#define QTCONTACTS_EXTENSIONS_H

#include <QContactDetail>
#include <QContactCollectionId>
#include <QContactId>
#include <QContactManager>

#include <QContactOnlineAccount>
#include <QContactPhoneNumber>
#include <QContactAvatar>
#include <QContactName>
#include <QContactDisplayLabel>

// Defines the extended values supported by qtcontacts-sqlite

QT_BEGIN_NAMESPACE_CONTACTS

// In QContactDetail, we support some extra fields
static const int QContactDetail__FieldModifiable = (QContactDetail::FieldLinkedDetailUris+2);
static const int QContactDetail__FieldNonexportable = (QContactDetail::FieldLinkedDetailUris+3);
static const int QContactDetail__FieldChangeFlags = (QContactDetail::FieldLinkedDetailUris+4);
static const int QContactDetail__FieldUnhandledChangeFlags = (QContactDetail::FieldLinkedDetailUris+5);
static const int QContactDetail__FieldDatabaseId = (QContactDetail::FieldLinkedDetailUris+6);

// The following change types can be reported for a detail when fetched via the synchronization plugin fetch API.
static const int QContactDetail__ChangeFlag_IsAdded    = 1 << 0;
static const int QContactDetail__ChangeFlag_IsModified = 1 << 1;
static const int QContactDetail__ChangeFlag_IsDeleted  = 1 << 2;

// In QContactDisplayLabel, we support the labelGroup property
static const int QContactDisplayLabel__FieldLabelGroup = (QContactDisplayLabel::FieldLabel+1);
static const int QContactDisplayLabel__FieldLabelGroupSortOrder = (QContactDisplayLabel::FieldLabel+2);

// In QContactOnlineAccount we support the following properties:
//   AccountPath - identifying path value for the account
//   AccountIconPath - path to an icon indicating the service type of the account
//   Enabled - a boolean indicating whether or not the account is enabled for activity
static const int QContactOnlineAccount__FieldAccountPath = (QContactOnlineAccount::FieldSubTypes+1);
static const int QContactOnlineAccount__FieldAccountIconPath = (QContactOnlineAccount::FieldSubTypes+2);
static const int QContactOnlineAccount__FieldEnabled = (QContactOnlineAccount::FieldSubTypes+3);
static const int QContactOnlineAccount__FieldAccountDisplayName = (QContactOnlineAccount::FieldSubTypes+4);
static const int QContactOnlineAccount__FieldServiceProviderDisplayName = (QContactOnlineAccount::FieldSubTypes+5);

// We support the QContactOriginMetadata detail type
static const QContactDetail::DetailType QContactDetail__TypeOriginMetadata = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 1);

// We support the QContactStatusFlags detail type
static const QContactDetail::DetailType QContactDetail__TypeStatusFlags = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 2);

// We support the QContactDeactivated detail type
static const QContactDetail::DetailType QContactDetail__TypeDeactivated = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 3);

// We support the QContactUndelete detail type
static const QContactDetail::DetailType QContactDetail__TypeUndelete = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 4);

static const QString COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE = QString::fromLatin1("Aggregable");
static const QString COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME = QString::fromLatin1("ApplicationName");
static const QString COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID = QString::fromLatin1("AccountId");
static const QString COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH = QString::fromLatin1("RemotePath");
static const QString COLLECTION_EXTENDEDMETADATA_KEY_READONLY = QString::fromLatin1("ReadOnly");

QT_END_NAMESPACE_CONTACTS

namespace QtContactsSqliteExtensions {

QTCONTACTS_USE_NAMESPACE

QContactCollectionId aggregateCollectionId(const QString &managerUri);
QContactCollectionId localCollectionId(const QString &managerUri);

QContactId apiContactId(quint32, const QString &managerUri);
quint32 internalContactId(const QContactId &);

enum NormalizePhoneNumberFlag {
    KeepPhoneNumberPunctuation = (1 << 0),
    KeepPhoneNumberDialString = (1 << 1),
    ValidatePhoneNumber = (1 << 2)
};
Q_DECLARE_FLAGS(NormalizePhoneNumberFlags, NormalizePhoneNumberFlag)

enum { DefaultMaximumPhoneNumberCharacters = 8 };

QString normalizePhoneNumber(const QString &input, NormalizePhoneNumberFlags flags);
QString minimizePhoneNumber(const QString &input, int maxCharacters = DefaultMaximumPhoneNumberCharacters);

class ContactManagerEngine;
ContactManagerEngine *contactManagerEngine(QContactManager &manager);

}

Q_DECLARE_OPERATORS_FOR_FLAGS(QtContactsSqliteExtensions::NormalizePhoneNumberFlags)

/* We define the name of the QCoreApplication property which holds our ContactsEngine */
#define CONTACT_MANAGER_ENGINE_PROP "qc_sqlite_extension_engine"

#endif
