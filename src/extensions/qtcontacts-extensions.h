/*
 * Copyright (C) 2013 Jolla Ltd. <matthew.vogt@jollamobile.com>
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
#include <QContactId>

#include <QContactOnlineAccount>
#include <QContactPhoneNumber>
#include <QContactAvatar>
#include <QContactName>

// Defines the extended values supported by qtcontacts-sqlite

QT_BEGIN_NAMESPACE_CONTACTS

// In QContactDetail, we support the provenance and modifiable properties
static const int QContactDetail__FieldProvenance = (QContactDetail::FieldLinkedDetailUris+1);
static const int QContactDetail__FieldModifiable = (QContactDetail::FieldLinkedDetailUris+2);
static const int QContactDetail__FieldNonexportable = (QContactDetail::FieldLinkedDetailUris+3);

// In QContactName, we support the customLabel property
static const int QContactName__FieldCustomLabel = (QContactName::FieldSuffix+1);

// In QContactOnlineAccount we support the following properties:
//   AccountPath - identifying path value for the account
//   AccountIconPath - path to an icon indicating the service type of the account
//   Enabled - a boolean indicating whether or not the account is enabled for activity
static const int QContactOnlineAccount__FieldAccountPath = (QContactOnlineAccount::FieldSubTypes+1);
static const int QContactOnlineAccount__FieldAccountIconPath = (QContactOnlineAccount::FieldSubTypes+2);
static const int QContactOnlineAccount__FieldEnabled = (QContactOnlineAccount::FieldSubTypes+3);
static const int QContactOnlineAccount__FieldAccountDisplayName = (QContactOnlineAccount::FieldSubTypes+4);
static const int QContactOnlineAccount__FieldServiceProviderDisplayName = (QContactOnlineAccount::FieldSubTypes+5);

// In QContactPhoneNumber, we support a field for normalized form of the number
static const int QContactPhoneNumber__FieldNormalizedNumber = (QContactPhoneNumber::FieldSubTypes+1);

// In QContactAvatar, we support a field for storing caller metadata
static const int QContactAvatar__FieldAvatarMetadata = (QContactAvatar::FieldVideoUrl+1);

// We support the QContactOriginMetadata detail type
static const QContactDetail::DetailType QContactDetail__TypeOriginMetadata = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 1);

// We support the QContactStatusFlags detail type
static const QContactDetail::DetailType QContactDetail__TypeStatusFlags = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 2);

// We support the QContactDeactivated detail type
static const QContactDetail::DetailType QContactDetail__TypeDeactivated = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 3);

// Incidental is an internal property of a contact relating to the contact's inception
static const QContactDetail::DetailType QContactDetail__TypeIncidental = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 4);

QT_END_NAMESPACE_CONTACTS

namespace QtContactsSqliteExtensions {

QTCONTACTS_USE_NAMESPACE

QContactId apiContactId(quint32);
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
