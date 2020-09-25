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

#include "conversion_p.h"

#include <QContactAddress>
#include <QContactAnniversary>
#include <QContactGender>
#include <QContactOnlineAccount>
#include <QContactPhoneNumber>
#include <QContactUrl>

QTCONTACTS_USE_NAMESPACE

namespace Conversion {

// Note: all of this is only necessary because we remain compatible with databases
// created for QtMobility Contacts, where various properties has string representations,
// which were stored in string form

int propertyValue(const QString &name, const QMap<QString, int> &propertyValues)
{
    QMap<QString, int>::const_iterator it = propertyValues.find(name);
    if (it != propertyValues.end()) {
        return *it;
    }
    return -1;
}

QList<int> propertyValueList(const QStringList &names, const QMap<QString, int> &propertyValues)
{
    QList<int> rv;
    foreach (const QString &name, names) {
        rv.append(propertyValue(name, propertyValues));
    }
    return rv;
}

QString propertyName(int value, const QMap<int, QString> &propertyNames)
{
    QMap<int, QString>::const_iterator it = propertyNames.find(value);
    if (it != propertyNames.end()) {
        return *it;
    }
    return QString();
}

QStringList propertyNameList(const QList<int> &values, const QMap<int, QString> &propertyNames)
{
    QStringList list;
    foreach (int value, values) {
        list.append(propertyName(value, propertyNames));
    }
    return list;
}

namespace OnlineAccount {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("Sip"), QContactOnlineAccount::SubTypeSip);
    rv.insert(QStringLiteral("SipVoip"), QContactOnlineAccount::SubTypeSipVoip);
    rv.insert(QStringLiteral("Impp"), QContactOnlineAccount::SubTypeImpp);
    rv.insert(QStringLiteral("VideoShare"), QContactOnlineAccount::SubTypeVideoShare);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactOnlineAccount::SubTypeSip, QStringLiteral("Sip"));
    rv.insert(QContactOnlineAccount::SubTypeSipVoip, QStringLiteral("SipVoip"));
    rv.insert(QContactOnlineAccount::SubTypeImpp, QStringLiteral("Impp"));
    rv.insert(QContactOnlineAccount::SubTypeVideoShare, QStringLiteral("VideoShare"));

    return rv;
}

static QMap<QString, int> protocolValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("Unknown"), QContactOnlineAccount::ProtocolUnknown);
    rv.insert(QStringLiteral("Aim"), QContactOnlineAccount::ProtocolAim);
    rv.insert(QStringLiteral("Icq"), QContactOnlineAccount::ProtocolIcq);
    rv.insert(QStringLiteral("Irc"), QContactOnlineAccount::ProtocolIrc);
    rv.insert(QStringLiteral("Jabber"), QContactOnlineAccount::ProtocolJabber);
    rv.insert(QStringLiteral("Msn"), QContactOnlineAccount::ProtocolMsn);
    rv.insert(QStringLiteral("Qq"), QContactOnlineAccount::ProtocolQq);
    rv.insert(QStringLiteral("Skype"), QContactOnlineAccount::ProtocolSkype);
    rv.insert(QStringLiteral("Yahoo"), QContactOnlineAccount::ProtocolYahoo);

    return rv;
}

static QMap<int, QString> protocolNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactOnlineAccount::ProtocolUnknown, QStringLiteral("Unknown"));
    rv.insert(QContactOnlineAccount::ProtocolAim, QStringLiteral("Aim"));
    rv.insert(QContactOnlineAccount::ProtocolIcq, QStringLiteral("Icq"));
    rv.insert(QContactOnlineAccount::ProtocolIrc, QStringLiteral("Irc"));
    rv.insert(QContactOnlineAccount::ProtocolJabber, QStringLiteral("Jabber"));
    rv.insert(QContactOnlineAccount::ProtocolMsn, QStringLiteral("Msn"));
    rv.insert(QContactOnlineAccount::ProtocolQq, QStringLiteral("Qq"));
    rv.insert(QContactOnlineAccount::ProtocolSkype, QStringLiteral("Skype"));
    rv.insert(QContactOnlineAccount::ProtocolYahoo, QStringLiteral("Yahoo"));

    return rv;
}

QList<int> subTypeList(const QStringList &names)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValueList(names, subTypes);
}

QStringList subTypeList(const QList<int> &values)
{
    static const QMap<int, QString> typeNames(subTypeNames());

    return propertyNameList(values, typeNames);
}

int protocol(const QString &name)
{
    static const QMap<QString, int> protocols(protocolValues());

    return propertyValue(name, protocols);
}

QString protocol(int type)
{
    static const QMap<int, QString> names(protocolNames());

    return propertyName(type, names);
}

}

namespace PhoneNumber {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("Landline"), QContactPhoneNumber::SubTypeLandline);
    rv.insert(QStringLiteral("Mobile"), QContactPhoneNumber::SubTypeMobile);
    rv.insert(QStringLiteral("Fax"), QContactPhoneNumber::SubTypeFax);
    rv.insert(QStringLiteral("Pager"), QContactPhoneNumber::SubTypePager);
    rv.insert(QStringLiteral("Voice"), QContactPhoneNumber::SubTypeVoice);
    rv.insert(QStringLiteral("Modem"), QContactPhoneNumber::SubTypeModem);
    rv.insert(QStringLiteral("Video"), QContactPhoneNumber::SubTypeVideo);
    rv.insert(QStringLiteral("Car"), QContactPhoneNumber::SubTypeCar);
    rv.insert(QStringLiteral("BulletinBoardSystem"), QContactPhoneNumber::SubTypeBulletinBoardSystem);
    rv.insert(QStringLiteral("MessagingCapable"), QContactPhoneNumber::SubTypeMessagingCapable);
    rv.insert(QStringLiteral("Assistant"), QContactPhoneNumber::SubTypeAssistant);
    rv.insert(QStringLiteral("DtmfMenu"), QContactPhoneNumber::SubTypeDtmfMenu);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactPhoneNumber::SubTypeLandline, QStringLiteral("Landline"));
    rv.insert(QContactPhoneNumber::SubTypeMobile, QStringLiteral("Mobile"));
    rv.insert(QContactPhoneNumber::SubTypeFax, QStringLiteral("Fax"));
    rv.insert(QContactPhoneNumber::SubTypePager, QStringLiteral("Pager"));
    rv.insert(QContactPhoneNumber::SubTypeVoice, QStringLiteral("Voice"));
    rv.insert(QContactPhoneNumber::SubTypeModem, QStringLiteral("Modem"));
    rv.insert(QContactPhoneNumber::SubTypeVideo, QStringLiteral("Video"));
    rv.insert(QContactPhoneNumber::SubTypeCar, QStringLiteral("Car"));
    rv.insert(QContactPhoneNumber::SubTypeBulletinBoardSystem, QStringLiteral("BulletinBoardSystem"));
    rv.insert(QContactPhoneNumber::SubTypeMessagingCapable, QStringLiteral("MessagingCapable"));
    rv.insert(QContactPhoneNumber::SubTypeAssistant, QStringLiteral("Assistant"));
    rv.insert(QContactPhoneNumber::SubTypeDtmfMenu, QStringLiteral("DtmfMenu"));

    return rv;
}

QList<int> subTypeList(const QStringList &names)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValueList(names, subTypes);
}

QStringList subTypeList(const QList<int> &values)
{
    static const QMap<int, QString> typeNames(subTypeNames());

    return propertyNameList(values, typeNames);
}

}

namespace Address {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("Parcel"), QContactAddress::SubTypeParcel);
    rv.insert(QStringLiteral("Postal"), QContactAddress::SubTypePostal);
    rv.insert(QStringLiteral("Domestic"), QContactAddress::SubTypeDomestic);
    rv.insert(QStringLiteral("International"), QContactAddress::SubTypeInternational);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactAddress::SubTypeParcel, QStringLiteral("Parcel"));
    rv.insert(QContactAddress::SubTypePostal, QStringLiteral("Postal"));
    rv.insert(QContactAddress::SubTypeDomestic, QStringLiteral("Domestic"));
    rv.insert(QContactAddress::SubTypeInternational, QStringLiteral("International"));

    return rv;
}

QList<int> subTypeList(const QStringList &names)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValueList(names, subTypes);
}

QStringList subTypeList(const QList<int> &values)
{
    static const QMap<int, QString> typeNames(subTypeNames());

    return propertyNameList(values, typeNames);
}

}

namespace Anniversary {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("Wedding"), QContactAnniversary::SubTypeWedding);
    rv.insert(QStringLiteral("Engagement"), QContactAnniversary::SubTypeEngagement);
    rv.insert(QStringLiteral("House"), QContactAnniversary::SubTypeHouse);
    rv.insert(QStringLiteral("Employment"), QContactAnniversary::SubTypeEmployment);
    rv.insert(QStringLiteral("Memorial"), QContactAnniversary::SubTypeMemorial);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactAnniversary::SubTypeWedding, QStringLiteral("Wedding"));
    rv.insert(QContactAnniversary::SubTypeEngagement, QStringLiteral("Engagement"));
    rv.insert(QContactAnniversary::SubTypeHouse, QStringLiteral("House"));
    rv.insert(QContactAnniversary::SubTypeEmployment, QStringLiteral("Employment"));
    rv.insert(QContactAnniversary::SubTypeMemorial, QStringLiteral("Memorial"));

    return rv;
}

int subType(const QString &name)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValue(name, subTypes);
}

QString subType(int type)
{
    static const QMap<int, QString> subTypes(subTypeNames());

    return propertyName(type, subTypes);
}

}

namespace Url {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("HomePage"), QContactUrl::SubTypeHomePage);
    rv.insert(QStringLiteral("Blog"), QContactUrl::SubTypeBlog);
    rv.insert(QStringLiteral("Favourite"), QContactUrl::SubTypeFavourite);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactUrl::SubTypeHomePage, QStringLiteral("HomePage"));
    rv.insert(QContactUrl::SubTypeBlog, QStringLiteral("Blog"));
    rv.insert(QContactUrl::SubTypeFavourite, QStringLiteral("Favourite"));

    return rv;
}

int subType(const QString &name)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValue(name, subTypes);
}

QString subType(int type)
{
    static const QMap<int, QString> subTypes(subTypeNames());

    return propertyName(type, subTypes);
}

}

namespace Gender {

static QMap<QString, int> genderValues()
{
    QMap<QString, int> rv;

    rv.insert(QStringLiteral("Male"), QContactGender::GenderMale);
    rv.insert(QStringLiteral("Female"), QContactGender::GenderFemale);
    rv.insert(QStringLiteral(""), QContactGender::GenderUnspecified);

    return rv;
}

static QMap<int, QString> genderNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactGender::GenderMale, QStringLiteral("Male"));
    rv.insert(QContactGender::GenderFemale, QStringLiteral("Female"));
    rv.insert(QContactGender::GenderUnspecified, QStringLiteral(""));

    return rv;
}

int gender(const QString &name)
{
    static const QMap<QString, int> genders(genderValues());

    return propertyValue(name, genders);
}

QString gender(int type)
{
    static const QMap<int, QString> genders(genderNames());

    return propertyName(type, genders);
}

}

}

