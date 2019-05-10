/*
 * Copyright (C) 2013 Jolla Ltd. <andrew.den.exter@jollamobile.com>
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

#include "contactreader.h"
#include "contactsengine.h"
#include "trace_p.h"

#include "../extensions/qtcontacts-extensions.h"
#include "../extensions/qcontactdeactivated.h"
#include "../extensions/qcontactincidental.h"
#include "../extensions/qcontactoriginmetadata.h"
#include "../extensions/qcontactstatusflags.h"

#include <QContactAddress>
#include <QContactAnniversary>
#include <QContactAvatar>
#include <QContactBirthday>
#include <QContactDisplayLabel>
#include <QContactEmailAddress>
#include <QContactFamily>
#include <QContactFavorite>
#include <QContactGender>
#include <QContactGeoLocation>
#include <QContactGlobalPresence>
#include <QContactGuid>
#include <QContactHobby>
#include <QContactName>
#include <QContactNickname>
#include <QContactNote>
#include <QContactOnlineAccount>
#include <QContactOrganization>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactRingtone>
#include <QContactSyncTarget>
#include <QContactTag>
#include <QContactTimestamp>
#include <QContactUrl>

#include <QContactDetailFilter>
#include <QContactDetailRangeFilter>
#include <QContactIdFilter>
#include <QContactChangeLogFilter>
#include <QContactUnionFilter>
#include <QContactIntersectionFilter>
#include <QContactRelationshipFilter>

#include <QContactManagerEngine>

#include <QSqlError>
#include <QVector>

#include <QtDebug>

static const int ReportBatchSize = 50;

static const QString aggregateSyncTarget(QString::fromLatin1("aggregate"));
static const QString localSyncTarget(QString::fromLatin1("local"));
static const QString wasLocalSyncTarget(QString::fromLatin1("was_local"));

enum FieldType {
    StringField = 0,
    StringListField,
    LocalizedField,
    LocalizedListField,
    IntegerField,
    DateField,
    BooleanField,
    RealField,
    OtherField
};

static const int invalidField = -1;

struct FieldInfo
{
    int field;
    const char *column;
    FieldType fieldType;
};

static void setValue(QContactDetail *detail, int key, const QVariant &value)
{
    if (value.type() != QVariant::String || !value.toString().isEmpty())
        detail->setValue(key, value);
}

static QVariant stringListValue(const QVariant &columnValue)
{
    if (columnValue.isNull())
        return columnValue;

    QString listString(columnValue.toString());
    return listString.split(QLatin1Char(';'), QString::SkipEmptyParts);
}

static QVariant urlValue(const QVariant &columnValue)
{
    if (columnValue.isNull())
        return columnValue;

    QString urlString(columnValue.toString());
    return QUrl(urlString);
}

static QVariant dateValue(const QVariant &columnValue)
{
    if (columnValue.isNull())
        return columnValue;

    QString dtString(columnValue.toString());
    return QDate::fromString(dtString, Qt::ISODate);
}

static const FieldInfo displayLabelFields[] =
{
    { QContactDisplayLabel::FieldLabel, "displayLabel", LocalizedField },
    { QContactDisplayLabel__FieldLabelGroup, "displayLabelGroup", LocalizedField }
};

static const FieldInfo nameFields[] =
{
    { QContactName::FieldFirstName, "firstName", LocalizedField },
    { invalidField, "lowerFirstName", LocalizedField },
    { QContactName::FieldLastName, "lastName", LocalizedField },
    { invalidField, "lowerLastName", LocalizedField },
    { QContactName::FieldMiddleName, "middleName", LocalizedField },
    { QContactName::FieldPrefix, "prefix", LocalizedField },
    { QContactName::FieldSuffix, "suffix", LocalizedField },
    { QContactName__FieldCustomLabel, "customLabel", LocalizedField }
};

static const FieldInfo syncTargetFields[] =
{
    { QContactSyncTarget::FieldSyncTarget, "syncTarget", StringField }
};

static const FieldInfo timestampFields[] =
{
    { QContactTimestamp::FieldCreationTimestamp, "created", DateField },
    { QContactTimestamp::FieldModificationTimestamp, "modified", DateField }
};

static const FieldInfo genderFields[] =
{
    { QContactGender::FieldGender, "gender", StringField }
};

static const FieldInfo favoriteFields[] =
{
    { QContactFavorite::FieldFavorite, "isFavorite", BooleanField }
};

static const FieldInfo statusFlagsFields[] =
{
    // No specific field; tests hasPhoneNumber/hasEmailAddress/hasOnlineAccount/isOnline/isDeactivated/isIncidental
    { QContactStatusFlags::FieldFlags, "", OtherField }
};

static const FieldInfo typeFields[] =
{
    { QContactType::FieldType, "type", IntegerField }
};

static const FieldInfo addressFields[] =
{
    { QContactAddress::FieldStreet, "street", LocalizedField },
    { QContactAddress::FieldPostOfficeBox, "postOfficeBox", LocalizedField },
    { QContactAddress::FieldRegion, "region", LocalizedField },
    { QContactAddress::FieldLocality, "locality", LocalizedField },
    { QContactAddress::FieldPostcode, "postCode", LocalizedField },
    { QContactAddress::FieldCountry, "country", LocalizedField },
    { QContactAddress::FieldSubTypes, "subTypes", StringListField },
    { QContactDetail::FieldContext, "context", StringField }

};

static QList<int> subTypeList(const QStringList &subTypeValues)
{
    QList<int> rv;
    foreach (const QString &value, subTypeValues) {
        rv.append(value.toInt());
    }
    return rv;
}

static void setValues(QContactAddress *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAddress T;

    setValue(detail, T::FieldStreet       , query->value(offset + 0));
    setValue(detail, T::FieldPostOfficeBox, query->value(offset + 1));
    setValue(detail, T::FieldRegion       , query->value(offset + 2));
    setValue(detail, T::FieldLocality     , query->value(offset + 3));
    setValue(detail, T::FieldPostcode     , query->value(offset + 4));
    setValue(detail, T::FieldCountry      , query->value(offset + 5));
    const QStringList subTypeValues(query->value(offset + 6).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes     , QVariant::fromValue<QList<int> >(subTypeList(subTypeValues)));
}

static const FieldInfo anniversaryFields[] =
{
    { QContactAnniversary::FieldOriginalDate, "originalDateTime", DateField },
    { QContactAnniversary::FieldCalendarId, "calendarId", StringField },
    { QContactAnniversary::FieldSubType, "subType", StringField },
    { QContactAnniversary::FieldEvent, "event", StringField }
};

static void setValues(QContactAnniversary *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAnniversary T;

    setValue(detail, T::FieldOriginalDate, dateValue(query->value(offset + 0)));
    setValue(detail, T::FieldCalendarId  , query->value(offset + 1));
    setValue(detail, T::FieldSubType     , QVariant::fromValue<QString>(query->value(offset + 2).toString()));
    setValue(detail, T::FieldEvent       , query->value(offset + 3));
}

static const FieldInfo avatarFields[] =
{
    { QContactAvatar::FieldImageUrl, "imageUrl", StringField },
    { QContactAvatar::FieldVideoUrl, "videoUrl", StringField },
    { QContactAvatar__FieldAvatarMetadata, "avatarMetadata", StringField }
};

static void setValues(QContactAvatar *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAvatar T;

    setValue(detail, T::FieldImageUrl, urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldVideoUrl, urlValue(query->value(offset + 1)));
    setValue(detail, QContactAvatar__FieldAvatarMetadata, query->value(offset + 2));
}

static const FieldInfo birthdayFields[] =
{
    { QContactBirthday::FieldBirthday, "birthday", DateField },
    { QContactBirthday::FieldCalendarId, "calendarId", StringField }
};

static void setValues(QContactBirthday *detail, QSqlQuery *query, const int offset)
{
    typedef QContactBirthday T;

    setValue(detail, T::FieldBirthday  , dateValue(query->value(offset + 0)));
    setValue(detail, T::FieldCalendarId, query->value(offset + 1));
}

static const FieldInfo emailAddressFields[] =
{
    { QContactEmailAddress::FieldEmailAddress, "emailAddress", StringField },
    { invalidField, "lowerEmailAddress", StringField },
    { QContactDetail::FieldContext, "context", StringField }
};

static void setValues(QContactEmailAddress *detail, QSqlQuery *query, const int offset)
{
    typedef QContactEmailAddress T;

    setValue(detail, T::FieldEmailAddress, query->value(offset + 0));
    // ignore lowerEmailAddress
}

static const FieldInfo familyFields[] =
{
    { QContactFamily::FieldSpouse, "spouse", LocalizedField },
    { QContactFamily::FieldChildren, "children", LocalizedListField }
};

static void setValues(QContactFamily *detail, QSqlQuery *query, const int offset)
{
    typedef QContactFamily T;

    setValue(detail, T::FieldSpouse  , query->value(offset + 0));
    setValue(detail, T::FieldChildren, query->value(offset + 1).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
}

static const FieldInfo geoLocationFields[] =
{
    { QContactGeoLocation::FieldLabel, "label", LocalizedField },
    { QContactGeoLocation::FieldLatitude, "latitude", RealField },
    { QContactGeoLocation::FieldLongitude, "longitude", RealField },
    { QContactGeoLocation::FieldAccuracy, "accuracy", RealField },
    { QContactGeoLocation::FieldAltitude, "altitude", RealField },
    { QContactGeoLocation::FieldAltitudeAccuracy, "altitudeAccuracy", RealField },
    { QContactGeoLocation::FieldHeading, "heading", RealField },
    { QContactGeoLocation::FieldSpeed, "speed", RealField },
    { QContactGeoLocation::FieldTimestamp, "timestamp", DateField }
};

static void setValues(QContactGeoLocation *detail, QSqlQuery *query, const int offset)
{
    typedef QContactGeoLocation T;

    setValue(detail, T::FieldLabel           , query->value(offset + 0));
    setValue(detail, T::FieldLatitude        , query->value(offset + 1).toDouble());
    setValue(detail, T::FieldLongitude       , query->value(offset + 2).toDouble());
    setValue(detail, T::FieldAccuracy        , query->value(offset + 3).toDouble());
    setValue(detail, T::FieldAltitude        , query->value(offset + 4).toDouble());
    setValue(detail, T::FieldAltitudeAccuracy, query->value(offset + 5).toDouble());
    setValue(detail, T::FieldHeading         , query->value(offset + 6).toDouble());
    setValue(detail, T::FieldSpeed           , query->value(offset + 7).toDouble());
    setValue(detail, T::FieldTimestamp       , ContactsDatabase::fromDateTimeString(query->value(offset + 8).toString()));
}

static const FieldInfo guidFields[] =
{
    { QContactGuid::FieldGuid, "guid", StringField }
};

static void setValues(QContactGuid *detail, QSqlQuery *query, const int offset)
{
    typedef QContactGuid T;

    setValue(detail, T::FieldGuid, query->value(offset + 0));
}

static const FieldInfo hobbyFields[] =
{
    { QContactHobby::FieldHobby, "hobby", LocalizedField }
};

static void setValues(QContactHobby *detail, QSqlQuery *query, const int offset)
{
    typedef QContactHobby T;

    setValue(detail, T::FieldHobby, query->value(offset + 0));
}

static const FieldInfo nicknameFields[] =
{
    { QContactNickname::FieldNickname, "nickname", LocalizedField },
    { invalidField, "lowerNickname", LocalizedField }
};

static void setValues(QContactNickname *detail, QSqlQuery *query, const int offset)
{
    typedef QContactNickname T;

    setValue(detail, T::FieldNickname, query->value(offset + 0));
    // ignore lowerNickname
}

static const FieldInfo noteFields[] =
{
    { QContactNote::FieldNote, "note", LocalizedField }
};

static void setValues(QContactNote *detail, QSqlQuery *query, const int offset)
{
    typedef QContactNote T;

    setValue(detail, T::FieldNote, query->value(offset + 0));
}

static const FieldInfo onlineAccountFields[] =
{
    { QContactOnlineAccount::FieldAccountUri, "accountUri", StringField },
    { invalidField, "lowerAccountUri", StringField },
    { QContactOnlineAccount::FieldProtocol, "protocol", StringField },
    { QContactOnlineAccount::FieldServiceProvider, "serviceProvider", LocalizedField },
    { QContactOnlineAccount::FieldCapabilities, "capabilities", StringListField },
    { QContactOnlineAccount::FieldSubTypes, "subTypes", StringListField },
    { QContactOnlineAccount__FieldAccountPath, "accountPath", StringField },
    { QContactOnlineAccount__FieldAccountIconPath, "accountIconPath", StringField },
    { QContactOnlineAccount__FieldEnabled, "enabled", BooleanField },
    { QContactOnlineAccount__FieldAccountDisplayName, "accountDisplayName", LocalizedField },
    { QContactOnlineAccount__FieldServiceProviderDisplayName, "serviceProviderDisplayName", LocalizedField }
};

static void setValues(QContactOnlineAccount *detail, QSqlQuery *query, const int offset)
{
    typedef QContactOnlineAccount T;

    setValue(detail, T::FieldAccountUri     , query->value(offset + 0));
    // ignore lowerAccountUri
    setValue(detail, T::FieldProtocol       , QVariant::fromValue<int>(query->value(offset + 2).toString().toInt()));
    setValue(detail, T::FieldServiceProvider, query->value(offset + 3));
    setValue(detail, T::FieldCapabilities   , stringListValue(query->value(offset + 4)));

    const QStringList subTypeValues(query->value(offset + 5).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(subTypeList(subTypeValues)));

    setValue(detail, QContactOnlineAccount__FieldAccountPath,                query->value(offset + 6));
    setValue(detail, QContactOnlineAccount__FieldAccountIconPath,            query->value(offset + 7));
    setValue(detail, QContactOnlineAccount__FieldEnabled,                    query->value(offset + 8));
    setValue(detail, QContactOnlineAccount__FieldAccountDisplayName,         query->value(offset + 9));
    setValue(detail, QContactOnlineAccount__FieldServiceProviderDisplayName, query->value(offset + 10));
}

static const FieldInfo organizationFields[] =
{
    { QContactOrganization::FieldName, "name", LocalizedField },
    { QContactOrganization::FieldRole, "role", LocalizedField },
    { QContactOrganization::FieldTitle, "title", LocalizedField },
    { QContactOrganization::FieldLocation, "location", LocalizedField },
    { QContactOrganization::FieldDepartment, "department", LocalizedField },
    { QContactOrganization::FieldLogoUrl, "logoUrl", StringField },
    { QContactOrganization::FieldAssistantName, "assistantName", StringField }
};

static void setValues(QContactOrganization *detail, QSqlQuery *query, const int offset)
{
    typedef QContactOrganization T;

    setValue(detail, T::FieldName      , query->value(offset + 0));
    setValue(detail, T::FieldRole      , query->value(offset + 1));
    setValue(detail, T::FieldTitle     , query->value(offset + 2));
    setValue(detail, T::FieldLocation  , query->value(offset + 3));
    setValue(detail, T::FieldDepartment, stringListValue(query->value(offset + 4)));
    setValue(detail, T::FieldLogoUrl   , urlValue(query->value(offset + 5)));
    setValue(detail, T::FieldAssistantName, query->value(offset + 6));
}

static const FieldInfo phoneNumberFields[] =
{
    { QContactPhoneNumber::FieldNumber, "phoneNumber", LocalizedField },
    { QContactPhoneNumber::FieldSubTypes, "subTypes", StringListField },
    { QContactPhoneNumber__FieldNormalizedNumber, "normalizedNumber", StringField }
};

static void setValues(QContactPhoneNumber *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPhoneNumber T;

    setValue(detail, T::FieldNumber  , query->value(offset + 0));

    const QStringList subTypeValues(query->value(offset + 1).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(subTypeList(subTypeValues)));

    setValue(detail, QContactPhoneNumber__FieldNormalizedNumber, query->value(offset + 2));
}

static const FieldInfo presenceFields[] =
{
    { QContactPresence::FieldPresenceState, "presenceState", IntegerField },
    { QContactPresence::FieldTimestamp, "timestamp", DateField },
    { QContactPresence::FieldNickname, "nickname", LocalizedField },
    { QContactPresence::FieldCustomMessage, "customMessage", LocalizedField },
    { QContactPresence::FieldPresenceStateText, "presenceStateText", StringField },
    { QContactPresence::FieldPresenceStateImageUrl, "presenceStateImageUrl", StringField }
};

static void setValues(QContactPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0).toInt());
    setValue(detail, T::FieldTimestamp    , ContactsDatabase::fromDateTimeString(query->value(offset + 1).toString()));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
    setValue(detail, T::FieldPresenceStateText, query->value(offset + 4));
    setValue(detail, T::FieldPresenceStateImageUrl, urlValue(query->value(offset + 5)));
}

static void setValues(QContactGlobalPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0).toInt());
    setValue(detail, T::FieldTimestamp    , ContactsDatabase::fromDateTimeString(query->value(offset + 1).toString()));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
    setValue(detail, T::FieldPresenceStateText, query->value(offset + 4));
    setValue(detail, T::FieldPresenceStateImageUrl, urlValue(query->value(offset + 5)));
}

static const FieldInfo ringtoneFields[] =
{
    { QContactRingtone::FieldAudioRingtoneUrl, "audioRingtone", StringField },
    { QContactRingtone::FieldVideoRingtoneUrl, "videoRingtone", StringField },
    { QContactRingtone::FieldVibrationRingtoneUrl, "vibrationRingtone", StringField }
};

static void setValues(QContactRingtone *detail, QSqlQuery *query, const int offset)
{
    typedef QContactRingtone T;

    setValue(detail, T::FieldAudioRingtoneUrl, urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldVideoRingtoneUrl, urlValue(query->value(offset + 1)));
    setValue(detail, T::FieldVibrationRingtoneUrl, urlValue(query->value(offset + 2)));
}

static const FieldInfo tagFields[] =
{
    { QContactTag::FieldTag, "tag", LocalizedField }
};

static void setValues(QContactTag *detail, QSqlQuery *query, const int offset)
{
    typedef QContactTag T;

    setValue(detail, T::FieldTag, query->value(offset + 0));
}

static const FieldInfo urlFields[] =
{
    { QContactUrl::FieldUrl, "url", StringField },
    { QContactUrl::FieldSubType, "subTypes", StringField }
};

static void setValues(QContactUrl *detail, QSqlQuery *query, const int offset)
{
    typedef QContactUrl T;

    setValue(detail, T::FieldUrl    , urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldSubType, QVariant::fromValue<QString>(query->value(offset + 1).toString()));
}

static const FieldInfo originMetadataFields[] =
{
    { QContactOriginMetadata::FieldId, "id", StringField },
    { QContactOriginMetadata::FieldGroupId, "groupId", StringField },
    { QContactOriginMetadata::FieldEnabled, "enabled", BooleanField }
};

static void setValues(QContactOriginMetadata *detail, QSqlQuery *query, const int offset)
{
    setValue(detail, QContactOriginMetadata::FieldId     , query->value(offset + 0));
    setValue(detail, QContactOriginMetadata::FieldGroupId, query->value(offset + 1));
    setValue(detail, QContactOriginMetadata::FieldEnabled, query->value(offset + 2));
}

static const FieldInfo extendedDetailFields[] =
{
    { QContactExtendedDetail::FieldName, "name", StringField },
    { QContactExtendedDetail::FieldData, "data", OtherField }
};

static void setValues(QContactExtendedDetail *detail, QSqlQuery *query, const int offset)
{
    setValue(detail, QContactExtendedDetail::FieldName, query->value(offset + 0));
    setValue(detail, QContactExtendedDetail::FieldData, query->value(offset + 1));
}

static QMap<QString, int> contextTypes()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Home"), QContactDetail::ContextHome);
    rv.insert(QString::fromLatin1("Work"), QContactDetail::ContextWork);
    rv.insert(QString::fromLatin1("Other"), QContactDetail::ContextOther);

    return rv;
}

static int contextType(const QString &type)
{
    static const QMap<QString, int> types(contextTypes());

    QMap<QString, int>::const_iterator it = types.find(type);
    if (it != types.end()) {
        return *it;
    }
    return -1;
}

template <typename T>
static void readDetail(QContact *contact, QSqlQuery &query, quint32 contactId, quint32 detailId, bool syncable, const QString &syncTarget, bool relaxConstraints, int offset)
{
    const bool aggregateContact(syncTarget == aggregateSyncTarget);

    T detail;

    /*
    const quint32 detailId = query.value(0).toUInt();
    const quint32 contactId = query.value(1).toUInt();
    const QString detailName = query.value(2).toString();
    */
    int col = 3;
    const QString detailUriValue = query.value(col++).toString();
    const QString linkedDetailUrisValue = query.value(col++).toString();
    const QString contextValue = query.value(col++).toString();
    const int accessConstraints = query.value(col++).toInt();
    QString provenance = query.value(col++).toString();
    const bool modifiable = query.value(col++).toBool();
    const bool nonexportable = query.value(col++).toBool();

    if (!detailUriValue.isEmpty()) {
        setValue(&detail,
                 QContactDetail::FieldDetailUri,
                 detailUriValue);
    }
    if (!linkedDetailUrisValue.isEmpty()) {
        setValue(&detail,
                 QContactDetail::FieldLinkedDetailUris,
                 linkedDetailUrisValue.split(QLatin1Char(';'), QString::SkipEmptyParts));
    }
    if (!contextValue.isEmpty()) {
        QList<int> contexts;
        foreach (const QString &context, contextValue.split(QLatin1Char(';'), QString::SkipEmptyParts)) {
            int type = contextType(context);
            if (type != -1) {
                contexts.append(type);
            }
        }
        if (!contexts.isEmpty()) {
            detail.setContexts(contexts);
        }
    }

    if (!aggregateContact) {
        // This detail is not aggregated from another - its provenance should match its ID
        provenance = QStringLiteral("%1:%2:%3").arg(contactId).arg(detailId).arg(syncTarget);
    }
    setValue(&detail, QContactDetail__FieldProvenance, provenance);

    // Only report modifiable state for non-local, non-aggregate contacts
    if (syncable) {
        setValue(&detail, QContactDetail__FieldModifiable, modifiable);
    }

    // Only include non-exportable if it is set
    if (nonexportable) {
        setValue(&detail, QContactDetail__FieldNonexportable, nonexportable);
    }

    // Constraints should be applied unless generating a partial aggregate; the partial aggregate
    // is intended for modification, so adding constraints prevents it from being used correctly
    if (!relaxConstraints) {
        QContactManagerEngine::setDetailAccessConstraints(&detail, static_cast<QContactDetail::AccessConstraints>(accessConstraints));
    }

    setValues(&detail, &query, offset);

    contact->saveDetail(&detail);
}

static QContactRelationship makeRelationship(const QString &type, quint32 firstId, quint32 secondId)
{
    QContactRelationship relationship;
    relationship.setRelationshipType(type);

    QContact first, second;
    first.setId(ContactId::apiId(firstId));
    second.setId(ContactId::apiId(secondId));
    relationship.setFirst(first);
    relationship.setSecond(second);

    return relationship;
}

typedef void (*ReadDetail)(QContact *contact, QSqlQuery &query, quint32 contactId, quint32 detailId, bool syncable, const QString &syncTarget, bool relaxConstraints, int offset);

struct DetailInfo
{
    QContactDetail::DetailType detailType;
    const char *detailName;
    const char *table;
    const FieldInfo *fields;
    const int fieldCount;
    const bool includesContext;
    const bool joinToSort;
    const ReadDetail read;

    QString where() const
    {
        return table ? QString::fromLatin1("Contacts.contactId IN (SELECT contactId FROM %1 WHERE %2)").arg(QLatin1String(table))
                     : QLatin1String("%2");
    }

    QString whereExists() const
    {
        return table ? QString::fromLatin1("EXISTS (SELECT contactId FROM %1 where contactId = Contacts.contactId)").arg(QLatin1String(table))
                     : QString::fromLatin1("Contacts.contactId != 0");
    }

    QString orderByExistence(bool asc) const
    {
        return table ? QString::fromLatin1("CASE EXISTS (SELECT contactId FROM %1 where contactId = Contacts.contactId) WHEN 1 THEN %2 ELSE %3 END")
                       .arg(QLatin1String(table)).arg(asc ? 0 : 1).arg(asc ? 1 : 0)
                     : QString();
    }
};

template <typename T, int N> static int lengthOf(const T(&)[N]) { return N; }

template <typename T> QContactDetail::DetailType detailIdentifier() { return T::Type; }

#define PREFIX_LENGTH 8
#define DEFINE_DETAIL(Detail, Table, fields, includesContext, joinToSort) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, #Table, fields, lengthOf(fields), includesContext, joinToSort, readDetail<Detail> }

#define DEFINE_DETAIL_PRIMARY_TABLE(Detail, fields) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, 0, fields, lengthOf(fields), false, false, 0 }

// Note: joinToSort should be true only if there can be only a single row for each contact in that table
static const DetailInfo detailInfo[] =
{
    DEFINE_DETAIL_PRIMARY_TABLE(QContactDisplayLabel, displayLabelFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactName,         nameFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactSyncTarget,   syncTargetFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactTimestamp,    timestampFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactGender,       genderFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactFavorite,     favoriteFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactStatusFlags,  statusFlagsFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactType,         typeFields),
    DEFINE_DETAIL(QContactAddress       , Addresses      , addressFields       , true , false),
    DEFINE_DETAIL(QContactAnniversary   , Anniversaries  , anniversaryFields   , false, false),
    DEFINE_DETAIL(QContactAvatar        , Avatars        , avatarFields        , false, false),
    DEFINE_DETAIL(QContactBirthday      , Birthdays      , birthdayFields      , false, true),
    DEFINE_DETAIL(QContactEmailAddress  , EmailAddresses , emailAddressFields  , true , false),
    DEFINE_DETAIL(QContactFamily        , Families       , familyFields        , false, false),
    DEFINE_DETAIL(QContactGeoLocation   , GeoLocations   , geoLocationFields   , false, false),
    DEFINE_DETAIL(QContactGuid          , Guids          , guidFields          , false, true),
    DEFINE_DETAIL(QContactHobby         , Hobbies        , hobbyFields         , false, false),
    DEFINE_DETAIL(QContactNickname      , Nicknames      , nicknameFields      , false, false),
    DEFINE_DETAIL(QContactNote          , Notes          , noteFields          , false, false),
    DEFINE_DETAIL(QContactOnlineAccount , OnlineAccounts , onlineAccountFields , false, false),
    DEFINE_DETAIL(QContactOrganization  , Organizations  , organizationFields  , false, false),
    DEFINE_DETAIL(QContactPhoneNumber   , PhoneNumbers   , phoneNumberFields   , false, false),
    DEFINE_DETAIL(QContactPresence      , Presences      , presenceFields      , false, false),
    DEFINE_DETAIL(QContactRingtone      , Ringtones      , ringtoneFields      , false, false),
    DEFINE_DETAIL(QContactTag           , Tags           , tagFields           , false, false),
    DEFINE_DETAIL(QContactUrl           , Urls           , urlFields           , false, false),
    DEFINE_DETAIL(QContactOriginMetadata, OriginMetadata , originMetadataFields, false, true),
    DEFINE_DETAIL(QContactGlobalPresence, GlobalPresences, presenceFields      , false, true),
    DEFINE_DETAIL(QContactExtendedDetail, ExtendedDetails, extendedDetailFields, false, false),
};

#undef DEFINE_DETAIL_PRIMARY_TABLE
#undef DEFINE_DETAIL
#undef PREFIX_LENGTH

const DetailInfo &detailInformation(QContactDetail::DetailType type)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (type == detail.detailType) {
            return detail;
        }
    }

    static const DetailInfo nullDetail = { QContactDetail::TypeUndefined, "Undefined", "", 0, 0, false, false, 0 };
    return nullDetail;
}

const FieldInfo &fieldInformation(const DetailInfo &detail, int field)
{
    for (int i = 0; i < detail.fieldCount; ++i) {
        const FieldInfo &fieldInfo = detail.fields[i];
        if (field == fieldInfo.field) {
            return fieldInfo;
        }
    }

    static const FieldInfo nullField = { invalidField, "", OtherField };
    return nullField;
}

QContactDetail::DetailType detailIdentifier(const QString &name)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (name == QLatin1String(detail.detailName)) {
            return detail.detailType;
        }
    }

    return QContactDetail::TypeUndefined;
}

static QString fieldName(const char *table, const char *field)
{
    return QString::fromLatin1(table ? table : "Contacts").append(QChar('.')).append(QString::fromLatin1(field));
}

static QHash<QString, QString> getCaseInsensitiveColumnNames()
{
    QHash<QString, QString> names;
    names.insert(fieldName("Contacts", "firstName"), QString::fromLatin1("lowerFirstName"));
    names.insert(fieldName("Contacts", "lastName"), QString::fromLatin1("lowerLastName"));
    names.insert(fieldName("EmailAddresses", "emailAddress"), QString::fromLatin1("lowerEmailAddress"));
    names.insert(fieldName("OnlineAccounts", "accountUri"), QString::fromLatin1("lowerAccountUri"));
    names.insert(fieldName("Nicknames", "nickname"), QString::fromLatin1("lowerNickname"));
    return names;
}

static QString caseInsensitiveColumnName(const char *table, const char *column)
{
    static QHash<QString, QString> columnNames(getCaseInsensitiveColumnNames());
    return columnNames.value(fieldName(table, column));
}

static QString dateString(const DetailInfo &detail, const QDateTime &qdt)
{
    if (detail.detailType == QContactBirthday::Type
            || detail.detailType == QContactAnniversary::Type) {
        // just interested in the date, not the whole date time (local time)
        return ContactsDatabase::dateString(qdt);
    }

    return ContactsDatabase::dateTimeString(qdt.toUTC());
}

template<typename T1, typename T2>
static bool matchOnType(const T1 &filter, T2 type)
{
    return filter.detailType() == type;
}

template<typename T, typename F>
static bool filterOnField(const QContactDetailFilter &filter, F field)
{
    return (filter.detailType() == T::Type &&
            filter.detailField() == field);
}

template<typename F>
static bool validFilterField(F filter)
{
    return (filter.detailField() != invalidField);
}

static QString convertFilterValueToString(const QContactDetailFilter &filter, const QString &defaultValue)
{
    // Some enum values are stored in textual columns
    if (filter.detailType() == QContactOnlineAccount::Type) {
        if (filter.detailField() == QContactOnlineAccount::FieldProtocol) {
            return QString::number(filter.value().toInt());
        } else if (filter.detailField() == QContactOnlineAccount::FieldSubTypes) {
            // TODO: what if the value is a list?
            return QString::number(filter.value().toInt());
        }
    } else if (filter.detailType() == QContactPhoneNumber::Type) {
        if (filter.detailField() == QContactPhoneNumber::FieldSubTypes) {
            // TODO: what if the value is a list?
            return QString::number(filter.value().toInt());
        }
    } else if (filter.detailType() == QContactAnniversary::Type) {
        if (filter.detailField() == QContactAnniversary::FieldSubType) {
            return QString::number(filter.value().toInt());
        }
    } else if (filter.detailType() == QContactUrl::Type) {
        if (filter.detailField() == QContactUrl::FieldSubType) {
            return QString::number(filter.value().toInt());
        }
    } else if (filter.detailType() == QContactGender::Type) {
        if (filter.detailField() == QContactGender::FieldGender) {
            return QString::number(filter.value().toInt());
        }
    }

    return defaultValue;
}

static QString buildWhere(const QContactDetailFilter &filter, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    if (filter.matchFlags() & QContactFilter::MatchKeypadCollation) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with filter requiring keypad collation"));
        return QLatin1String("FAILED");
    }

    const DetailInfo &detail(detailInformation(filter.detailType()));
    if (detail.detailType == QContactDetail::TypeUndefined) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail type: %1").arg(filter.detailType()));
        return QLatin1String("FAILED");
    }

    if (filter.detailField() == invalidField) {
        // If there is no field, we're simply testing for the existence of matching details
        return detail.whereExists();
    }

    const FieldInfo &field(fieldInformation(detail, filter.detailField()));
    if (field.field == invalidField) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail field: %1").arg(filter.detailField()));
        return QLatin1String("FAILED");
    }

    if (!filter.value().isValid()     // "match if detail and field exists, don't care about value" filter
        || (filterOnField<QContactSyncTarget>(filter, QContactSyncTarget::FieldSyncTarget) &&
            filter.value().toString().isEmpty())) { // match all sync targets if empty sync target filter
        const QString comparison(QLatin1String("%1 IS NOT NULL"));
        return detail.where().arg(comparison.arg(field.column));
    }

    do {
        // Our match query depends on the value parameter
        if (field.fieldType == OtherField) {
            if (filterOnField<QContactStatusFlags>(filter, QContactStatusFlags::FieldFlags)) {
                static const quint64 flags[] = { QContactStatusFlags::HasPhoneNumber,
                                                 QContactStatusFlags::HasEmailAddress,
                                                 QContactStatusFlags::HasOnlineAccount,
                                                 QContactStatusFlags::IsOnline,
                                                 QContactStatusFlags::IsDeactivated,
                                                 QContactStatusFlags::IsIncidental };
                static const char *flagColumns[] = { "hasPhoneNumber",
                                                     "hasEmailAddress",
                                                     "hasOnlineAccount",
                                                     "isOnline",
                                                     "isDeactivated",
                                                     "isIncidental" };

                quint64 flagsValue = filter.value().value<quint64>();

                QStringList clauses;
                if (filter.matchFlags() == QContactFilter::MatchExactly) {
                    *globalPresenceRequired = true;
                    for (int i  = 0; i < lengthOf(flags); ++i) {
                        QString comparison;
                        if (flags[i] == QContactStatusFlags::IsOnline) {
                            // Use special case test to include transient presence state
                            comparison = QStringLiteral("COALESCE(temp.GlobalPresenceStates.isOnline, Contacts.isOnline) = %1");
                        } else {
                            comparison = QStringLiteral("%1 = %2").arg(flagColumns[i]);
                        }
                        clauses.append(comparison.arg((flagsValue & flags[i]) ? 1 : 0));
                    }
                } else if (filter.matchFlags() == QContactFilter::MatchContains) {
                    for (int i  = 0; i < lengthOf(flags); ++i) {
                        if (flagsValue & flags[i]) {
                            if (flags[i] == QContactStatusFlags::IsOnline) {
                                *globalPresenceRequired = true;
                                clauses.append(QStringLiteral("COALESCE(temp.GlobalPresenceStates.isOnline, Contacts.isOnline) = 1"));
                            } else {
                                clauses.append(QString::fromLatin1("%1 = 1").arg(flagColumns[i]));
                            }
                        }
                    }
                } else {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unsupported flags matching contact status flags"));
                    break;
                }

                if (!clauses.isEmpty()) {
                    return detail.where().arg(clauses.join(QString::fromLatin1(" AND ")));
                }
                break;
            }
        }

        bool dateField = field.fieldType == DateField;
        bool stringField = field.fieldType == StringField || field.fieldType == StringListField ||
                           field.fieldType == LocalizedField || field.fieldType == LocalizedListField;
        bool phoneNumberMatch = filter.matchFlags() & QContactFilter::MatchPhoneNumber;
        bool fixedString = filter.matchFlags() & QContactFilter::MatchFixedString;
        bool useNormalizedNumber = false;
        int globValue = filter.matchFlags() & 7;
        if (field.fieldType == StringListField || field.fieldType == LocalizedListField) {
            // With a string list, the only string match type we can do is 'contains'
            globValue = QContactFilter::MatchContains;
        }

        // We need to perform case-insensitive matching if MatchFixedString is specified (unless
        // CaseSensitive is also specified)
        bool caseInsensitive = stringField && fixedString && ((filter.matchFlags() & QContactFilter::MatchCaseSensitive) == 0);

        QString clause(detail.where());
        QString comparison = QLatin1String("%1");
        QString bindValue;
        QString column;

        if (caseInsensitive) {
            column = caseInsensitiveColumnName(detail.table, field.column);
            if (!column.isEmpty()) {
                // We don't need to use lower() on the values in this column
            } else {
                comparison = QLatin1String("lower(%1)");
            }
        }

        QString stringValue = filter.value().toString();

        if (phoneNumberMatch) {
            // If the phone number match is on the number field of a phoneNumber detail, then
            // match on the normalized number rather than the unconstrained number (for simple matches)
            useNormalizedNumber = (filterOnField<QContactPhoneNumber>(filter, QContactPhoneNumber::FieldNumber) &&
                                   globValue != QContactFilter::MatchStartsWith &&
                                   globValue != QContactFilter::MatchContains &&
                                   globValue != QContactFilter::MatchEndsWith);

            if (useNormalizedNumber) {
                // Normalize the input for comparison
                bindValue = ContactsEngine::normalizedPhoneNumber(stringValue);
                if (bindValue.isEmpty()) {
                    *failed = true;
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed with invalid phone number: %1").arg(stringValue));
                    return QLatin1String("FAILED");
                }
                if (caseInsensitive) {
                    bindValue = bindValue.toLower();
                }
                column = QString::fromLatin1("normalizedNumber");
            } else {
                // remove any non-digit characters from the column value when we do our comparison: +,-, ,#,(,) are removed.
                comparison = QLatin1String("replace(replace(replace(replace(replace(replace(%1, '+', ''), '-', ''), '#', ''), '(', ''), ')', ''), ' ', '')");
                QString tempValue = caseInsensitive ? stringValue.toLower() : stringValue;
                for (int i = 0; i < tempValue.size(); ++i) {
                    QChar current = tempValue.at(i).toLower();
                    if (current.isDigit()) {
                        bindValue.append(current);
                    }
                }
            }
        } else {
            const QVariant &v(filter.value());
            if (dateField) {
                bindValue = dateString(detail, v.toDateTime());

                if (filterOnField<QContactTimestamp>(filter, QContactTimestamp::FieldModificationTimestamp)) {
                    // Special case: we need to include the transient data timestamp in our comparison
                    column = QStringLiteral("COALESCE(temp.Timestamps.modified, Contacts.modified)");
                    *transientModifiedRequired = true;
                }
            } else if (!stringField && (v.type() == QVariant::Bool)) {
                // Convert to "1"/"0" rather than "true"/"false"
                bindValue = QString::number(v.toBool() ? 1 : 0);
            } else {
                stringValue = convertFilterValueToString(filter, stringValue);
                bindValue = caseInsensitive ? stringValue.toLower() : stringValue;

                if (filterOnField<QContactGlobalPresence>(filter, QContactGlobalPresence::FieldPresenceState)) {
                    // Special case: we need to include the transient data state in our comparison
                    clause = QLatin1String("Contacts.contactId IN ("
                                               "SELECT GlobalPresences.contactId FROM GlobalPresences "
                                               "LEFT JOIN temp.GlobalPresenceStates ON temp.GlobalPresenceStates.contactId = GlobalPresences.contactId "
                                               "WHERE %1)");
                    column = QStringLiteral("COALESCE(temp.GlobalPresenceStates.presenceState, GlobalPresences.presenceState)");
                    *globalPresenceRequired = true;
                }
            }
        }

        if (stringField || fixedString) {
            if (globValue == QContactFilter::MatchStartsWith) {
                bindValue = bindValue + QLatin1String("*");
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else if (globValue == QContactFilter::MatchContains) {
                bindValue = QLatin1String("*") + bindValue + QLatin1String("*");
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else if (globValue == QContactFilter::MatchEndsWith) {
                bindValue = QLatin1String("*") + bindValue;
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else {
                if (bindValue.isEmpty()) {
                    // An empty string test should match a NULL column also (no way to specify isNull from qtcontacts)
                    comparison = QString::fromLatin1("COALESCE(%1,'') = ''").arg(comparison);
                } else {
                    comparison += QLatin1String(" = ?");
                    bindings->append(bindValue);
                }
            }
        } else {
            if (phoneNumberMatch && !useNormalizedNumber) {
                bindValue = QLatin1String("*") + bindValue;
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else {
                comparison += QLatin1String(" = ?");
                bindings->append(bindValue);
            }
        }

        return clause.arg(comparison.arg(column.isEmpty() ? field.column : column));
    } while (false);

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to buildWhere with DetailFilter detail: %1 field: %2").arg(filter.detailType()).arg(filter.detailField()));
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactDetailRangeFilter &filter, QVariantList *bindings, bool *failed)
{
    const DetailInfo &detail(detailInformation(filter.detailType()));
    if (detail.detailType == QContactDetail::TypeUndefined) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail type: %1").arg(filter.detailType()));
        return QLatin1String("FAILED");
    }

    if (filter.detailField() == invalidField) {
        // If there is no field, we're simply testing for the existence of matching details
        return detail.whereExists();
    }

    const FieldInfo &field(fieldInformation(detail, filter.detailField()));
    if (field.field == invalidField) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail field: %1").arg(filter.detailField()));
        return QLatin1String("FAILED");
    }

    if (!validFilterField(filter) || (!filter.minValue().isValid() && !filter.maxValue().isValid())) {
        // "match if detail exists, don't care about field or value" filter
        return detail.where().arg(QString::fromLatin1("%1 IS NOT NULL").arg(field.column));
    }

    // Our match query depends on the minValue/maxValue parameters
    QString comparison;
    bool dateField = field.fieldType == DateField;
    bool stringField = field.fieldType == StringField || field.fieldType == LocalizedField;
    bool caseInsensitive = stringField &&
                           filter.matchFlags() & QContactFilter::MatchFixedString &&
                           (filter.matchFlags() & QContactFilter::MatchCaseSensitive) == 0;

    bool needsAnd = false;
    if (filter.minValue().isValid()) {
        if (dateField) {
            bindings->append(dateString(detail, filter.minValue().toDateTime()));
        } else {
            bindings->append(filter.minValue());
        }
        if (caseInsensitive) {
            comparison = (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeLower)
                    ? QString(QLatin1String("%1 > lower(?)"))
                    : QString(QLatin1String("%1 >= lower(?)"));
        } else {
            comparison = (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeLower)
                    ? QString(QLatin1String("%1 > ?"))
                    : QString(QLatin1String("%1 >= ?"));
        }
        needsAnd = true;
    }

    if (filter.maxValue().isValid()) {
        if (needsAnd)
            comparison += QLatin1String(" AND ");
        if (dateField) {
            bindings->append(dateString(detail, filter.maxValue().toDateTime()));
        } else {
            bindings->append(filter.maxValue());
        }
        if (caseInsensitive) {
            comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                    ? QString(QLatin1String("%1 <= lower(?)"))
                    : QString(QLatin1String("%1 < lower(?)"));
        } else {
            comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                    ? QString(QLatin1String("%1 <= ?"))
                    : QString(QLatin1String("%1 < ?"));
        }
    }

    QString comparisonArg = field.column;
    if (caseInsensitive) {
        comparisonArg = caseInsensitiveColumnName(detail.table, field.column);
        if (!comparisonArg.isEmpty()) {
            // We don't need to use lower() on the values in this column
        } else {
            comparisonArg = QString::fromLatin1("lower(%1)").arg(field.column);
        }
    }
    return detail.where().arg(comparison.arg(comparisonArg));
}

static QString buildWhere(const QContactIdFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings, bool *failed)
{
    const QList<QContactId> &filterIds(filter.ids());
    if (filterIds.isEmpty()) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with empty contact ID list"));
        return QLatin1String("FALSE");
    }

    QList<quint32> dbIds;
    dbIds.reserve(filterIds.count());
    bindings->reserve(filterIds.count());

    foreach (const QContactId &id, filterIds) {
        dbIds.append(ContactId::databaseId(id));
    }

    // We don't want to exceed the maximum bound variables limit; if there are too
    // many IDs in the list, create a temporary table to look them up from
    const int maxInlineIdsCount = 800;
    if (filterIds.count() > maxInlineIdsCount) {
        QVariantList varIds;
        foreach (const QContactId &id, filterIds) {
            varIds.append(QVariant(ContactId::databaseId(id)));
        }

        QString transientTable;
        if (!db.createTransientContactIdsTable(table, varIds, &transientTable)) {
            *failed = true;
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere due to transient table failure"));
            return QLatin1String("FALSE");
        }

        return QString::fromLatin1("Contacts.contactId IN (SELECT contactId FROM %1)").arg(transientTable);
    }

    QString statement = QLatin1String("Contacts.contactId IN (?");
    bindings->append(dbIds.first());

    for (int i = 1; i < dbIds.count(); ++i) {
        statement += QLatin1String(",?");
        bindings->append(dbIds.at(i));
    }
    return statement + QLatin1String(")");
}

static QString buildWhere(const QContactRelationshipFilter &filter, QVariantList *bindings, bool *failed)
{
    QContactId rci = filter.relatedContact().id();

    QContactRelationship::Role rcr = filter.relatedContactRole();
    QString rt = filter.relationshipType();

    quint32 dbId = ContactId::databaseId(rci);

    if (!rci.managerUri().isEmpty() && !rci.managerUri().startsWith(QString::fromLatin1("qtcontacts:org.nemomobile.contacts.sqlite:"))) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with invalid manager URI: %1").arg(rci.managerUri()));
        return QLatin1String("FALSE");
    }

    bool needsId = dbId != 0;
    bool needsType = !rt.isEmpty();
    QString statement = QLatin1String("Contacts.contactId IN (\n");
    if (!needsId && !needsType) {
        // return the id of every contact who is in a relationship
        if (rcr == QContactRelationship::First) { // where the other contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships)");
        } else if (rcr == QContactRelationship::Second) { // where the other contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships)");
        } else { // where the other contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships UNION SELECT DISTINCT secondId FROM Relationships)");
        }
    } else if (!needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type
        if (rcr == QContactRelationship::First) { // where the other contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE type = ?)");
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the other contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE type = ?)");
            bindings->append(rt);
        } else { // where the other contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE type = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE type = ?)");
            bindings->append(rt);
            bindings->append(rt);
        }
    } else if (needsId && !needsType) {
        // return the id of every contact who is in a relationship with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?)");
            bindings->append(dbId);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ?)");
            bindings->append(dbId);
        } else { // where the specified contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?)");
            bindings->append(dbId);
            bindings->append(dbId);
        }
    } else if (needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?)");
            bindings->append(dbId);
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ?)");
            bindings->append(dbId);
            bindings->append(rt);
        } else { // where the specified contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?)");
            bindings->append(dbId);
            bindings->append(rt);
            bindings->append(dbId);
            bindings->append(rt);
        }
    }

    return statement;
}

static QString buildWhere(const QContactChangeLogFilter &filter, QVariantList *bindings, bool *failed, bool *transientModifiedRequired)
{
    static const QString statement(QLatin1String("%1 >= ?"));
    bindings->append(ContactsDatabase::dateTimeString(filter.since().toUTC()));
    switch (filter.eventType()) {
        case QContactChangeLogFilter::EventAdded:
            return statement.arg(QLatin1String("Contacts.created"));
        case QContactChangeLogFilter::EventChanged:
            *transientModifiedRequired = true;
            return statement.arg(QLatin1String("COALESCE(temp.Timestamps.modified, Contacts.modified)"));
        default: break;
    }

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with changelog filter on removed timestamps"));
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired);

static QString buildWhere(const QContactUnionFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
        if (!*failed && !fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    return QString::fromLatin1("( %1 )").arg(fragments.join(QLatin1String(" OR ")));
}

static QString buildWhere(const QContactIntersectionFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
        if (filter.type() != QContactFilter::DefaultFilter && !*failed) {
            // default filter gets special (permissive) treatment by the intersection filter.
            fragments.append(fragment.isEmpty() ? QLatin1String("NULL") : fragment);
        }
    }

    return fragments.join(QLatin1String(" AND "));
}

static QString buildWhere(const QContactFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    Q_ASSERT(failed);
    Q_ASSERT(globalPresenceRequired);
    Q_ASSERT(transientModifiedRequired);

    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
        return QString();
    case QContactFilter::ContactDetailFilter:
        return buildWhere(static_cast<const QContactDetailFilter &>(filter), bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::ContactDetailRangeFilter:
        return buildWhere(static_cast<const QContactDetailRangeFilter &>(filter), bindings, failed);
    case QContactFilter::ChangeLogFilter:
        return buildWhere(static_cast<const QContactChangeLogFilter &>(filter), bindings, failed, transientModifiedRequired);
    case QContactFilter::RelationshipFilter:
        return buildWhere(static_cast<const QContactRelationshipFilter &>(filter), bindings, failed);
    case QContactFilter::IntersectionFilter:
        return buildWhere(static_cast<const QContactIntersectionFilter &>(filter), db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::UnionFilter:
        return buildWhere(static_cast<const QContactUnionFilter &>(filter), db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::IdFilter:
        return buildWhere(static_cast<const QContactIdFilter &>(filter), db, table, bindings, failed);
    default:
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown filter type: %1").arg(filter.type()));
        return QLatin1String("FALSE");
    }
}

static QString buildOrderBy(const QContactSortOrder &order, QStringList *joins, bool *transientModifiedRequired, bool *globalPresenceRequired, bool useLocale)
{
    Q_ASSERT(joins);
    Q_ASSERT(transientModifiedRequired);
    Q_ASSERT(globalPresenceRequired);

    const DetailInfo &detail(detailInformation(order.detailType()));
    if (detail.detailType == QContactDetail::TypeUndefined) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildOrderBy with unknown detail type: %1").arg(order.detailType()));
        return QString();
    }

    if (order.detailField() == invalidField) {
        // If there is no field, we're simply sorting by the existence or otherwise of the detail
        return detail.orderByExistence(order.direction() == Qt::AscendingOrder);
    }

    const FieldInfo &field(fieldInformation(detail, order.detailField()));
    if (field.field == invalidField) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildOrderBy with unknown detail field: %1").arg(order.detailField()));
        return QString();
    }

    const bool isDisplayLabelGroup = detail.detailType == QContactDisplayLabel::Type && field.field == QContactDisplayLabel__FieldLabelGroup;
    QString sortExpression(QStringLiteral("%1.%2")
            .arg(detail.joinToSort ? detail.table : QStringLiteral("Contacts"))
            .arg(isDisplayLabelGroup ? QStringLiteral("DisplayLabelGroupSortOrder") : field.column));
    bool sortBlanks = true;
    bool collate = true;
    bool localized = field.fieldType == LocalizedField;

    // Special case for accessing transient data
    if (detail.detailType == detailIdentifier<QContactGlobalPresence>() &&
        field.field == QContactGlobalPresence::FieldPresenceState) {
        // We need to coalesce the transient values with the table values
        *globalPresenceRequired = true;

        // Look at the temporary state value if present, otherwise use the normal value
        sortExpression = QStringLiteral("COALESCE(temp.GlobalPresenceStates.presenceState, GlobalPresences.presenceState)");
        sortBlanks = false;
        collate = false;

#ifdef SORT_PRESENCE_BY_AVAILABILITY
        // The order we want is Available(1),Away(4),ExtendedAway(5),Busy(3),Hidden(2),Offline(6),Unknown(0)
        sortExpression = QStringLiteral("CASE %1 WHEN 1 THEN 0 "
                                                "WHEN 4 THEN 1 "
                                                "WHEN 5 THEN 2 "
                                                "WHEN 3 THEN 3 "
                                                "WHEN 2 THEN 4 "
                                                "WHEN 6 THEN 5 "
                                                       "ELSE 6 END").arg(sortExpression);
#endif
    } else if (detail.detailType == detailIdentifier<QContactTimestamp>() &&
               field.field == QContactTimestamp::FieldModificationTimestamp) {
        *transientModifiedRequired = true;

        // Look at the temporary modified timestamp if present, otherwise use the normal value
        sortExpression = QStringLiteral("COALESCE(temp.Timestamps.modified, modified)");
        sortBlanks = false;
        collate = false;
    }

    QString result;

    if (sortBlanks) {
        QString blanksLocation = (order.blankPolicy() == QContactSortOrder::BlanksLast)
                ? QLatin1String("CASE WHEN COALESCE(%1, '') = '' THEN 1 ELSE 0 END, ")
                : QLatin1String("CASE WHEN COALESCE(%1, '') = '' THEN 0 ELSE 1 END, ");
        result = blanksLocation.arg(sortExpression);
    }

    result.append(sortExpression);

    if (!isDisplayLabelGroup && collate) {
        if (localized && useLocale) {
            result.append(QLatin1String(" COLLATE localeCollation"));
        } else {
            result.append((order.caseSensitivity() == Qt::CaseSensitive) ? QLatin1String(" COLLATE RTRIM") : QLatin1String(" COLLATE NOCASE"));
        }
    }

    result.append((order.direction() == Qt::AscendingOrder) ? QLatin1String(" ASC") : QLatin1String(" DESC"));

    if (detail.joinToSort) {
        QString join = QString(QLatin1String(
                "LEFT JOIN %1 ON Contacts.contactId = %1.contactId"))
                .arg(QLatin1String(detail.table));

        if (!joins->contains(join))
            joins->append(join);

        return result;
    } else if (!detail.table) {
        return result;
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("UNSUPPORTED SORTING: no join and not primary table for ORDER BY in query with: %1, %2")
                   .arg(order.detailType()).arg(order.detailField()));
    }

    return QString();
}

static QString buildOrderBy(const QList<QContactSortOrder> &order, QString *join, bool *transientModifiedRequired, bool *globalPresenceRequired, bool useLocale)
{
    Q_ASSERT(join);
    Q_ASSERT(transientModifiedRequired);
    Q_ASSERT(globalPresenceRequired);

    if (order.isEmpty())
        return QString();

    QStringList joins;
    QStringList fragments;
    foreach (const QContactSortOrder &sort, order) {
        const QString fragment = buildOrderBy(sort, &joins, transientModifiedRequired, globalPresenceRequired, useLocale);
        if (!fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    *join = joins.join(QLatin1String(" "));

    fragments.append(QLatin1String("displayLabel"));
    return fragments.join(QLatin1String(", "));
}

static void debugFilterExpansion(const QString &description, const QString &query, const QVariantList &bindings)
{
    static const bool debugFilters = !qgetenv("QTCONTACTS_SQLITE_DEBUG_FILTERS").isEmpty();

    if (debugFilters) {
        qDebug() << description << ContactsDatabase::expandQuery(query, bindings);
    }
}

ContactReader::ContactReader(ContactsDatabase &database)
    : m_database(database)
{
}

ContactReader::~ContactReader()
{
}

struct Table
{
    QSqlQuery *query;
    QContactDetail::DetailType detailType;
    ReadDetail read;
    quint32 currentId;
};

namespace {

// The selfId is fixed - DB ID 1 is the 'self' local contact, and DB ID 2 is the aggregate
const quint32 selfId(2);

bool includesSelfId(const QContactFilter &filter);

// Returns true if this filter includes the self contact by ID
bool includesSelfId(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesSelfId(filter)) {
            return true;
        }
    }
    return false;
}
bool includesSelfId(const QContactIntersectionFilter &filter)
{
    return includesSelfId(filter.filters());
}
bool includesSelfId(const QContactUnionFilter &filter)
{
    return includesSelfId(filter.filters());
}
bool includesSelfId(const QContactIdFilter &filter)
{
    foreach (const QContactId &id, filter.ids()) {
        if (ContactId::databaseId(id) == selfId)
            return true;
    }
    return false;
}
bool includesSelfId(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesSelfId(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesSelfId(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::IdFilter:
        return includesSelfId(static_cast<const QContactIdFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesSelfId with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesSyncTarget(const QContactFilter &filter);

// Returns true if this filter includes a filter for specific syncTarget
bool includesSyncTarget(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesSyncTarget(filter)) {
            return true;
        }
    }
    return false;
}
bool includesSyncTarget(const QContactIntersectionFilter &filter)
{
    return includesSyncTarget(filter.filters());
}
bool includesSyncTarget(const QContactUnionFilter &filter)
{
    return includesSyncTarget(filter.filters());
}
bool includesSyncTarget(const QContactDetailFilter &filter)
{
    return filter.detailType() == QContactSyncTarget::Type;
}
bool includesSyncTarget(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
    case QContactFilter::IdFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesSyncTarget(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesSyncTarget(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::ContactDetailFilter:
        return includesSyncTarget(static_cast<const QContactDetailFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesSyncTarget with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesDeactivated(const QContactFilter &filter);

// Returns true if this filter includes deactivated contacts
bool includesDeactivated(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesDeactivated(filter)) {
            return true;
        }
    }
    return false;
}
bool includesDeactivated(const QContactIntersectionFilter &filter)
{
    return includesDeactivated(filter.filters());
}
bool includesDeactivated(const QContactUnionFilter &filter)
{
    return includesDeactivated(filter.filters());
}
bool includesDeactivated(const QContactDetailFilter &filter)
{
    if (filterOnField<QContactStatusFlags>(filter, QContactStatusFlags::FieldFlags)) {
        quint64 flagsValue = filter.value().value<quint64>();
        if (flagsValue & QContactStatusFlags::IsDeactivated) {
            return true;
        }
    }
    return false;
}
bool includesDeactivated(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::IdFilter:
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesDeactivated(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesDeactivated(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::ContactDetailFilter:
        return includesDeactivated(static_cast<const QContactDetailFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesDeactivated with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesIdFilter(const QContactFilter &filter);

// Returns true if this filter includes a filter for specific IDs
bool includesIdFilter(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesIdFilter(filter)) {
            return true;
        }
    }
    return false;
}
bool includesIdFilter(const QContactIntersectionFilter &filter)
{
    return includesIdFilter(filter.filters());
}
bool includesIdFilter(const QContactUnionFilter &filter)
{
    return includesIdFilter(filter.filters());
}
bool includesIdFilter(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesIdFilter(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesIdFilter(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::IdFilter:
        return true;

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesIdFilter with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

static bool deletedContactFilter(const QContactFilter &filter)
{
    const QContactFilter::FilterType filterType(filter.type());

    // The only queries we suport regarding deleted contacts are for the IDs, possibly
    // intersected with a syncTarget detail filter
    if (filterType == QContactFilter::ChangeLogFilter) {
        const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(filter));
        return changeLogFilter.eventType() == QContactChangeLogFilter::EventRemoved;
    } else if (filterType == QContactFilter::IntersectionFilter) {
        const QContactIntersectionFilter &intersectionFilter(static_cast<const QContactIntersectionFilter &>(filter));
        const QList<QContactFilter> filters(intersectionFilter.filters());
        if (filters.count() <= 2) {
            foreach (const QContactFilter &partialFilter, filters) {
                if (partialFilter.type() == QContactFilter::ChangeLogFilter) {
                    const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(partialFilter));
                    if (changeLogFilter.eventType() == QContactChangeLogFilter::EventRemoved) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

QString expandWhere(const QString &where, const QContactFilter &filter, const bool aggregating)
{
    QStringList constraints;

    // remove the self contact, unless specifically included
    if (!includesSelfId(filter)) {
        constraints.append("Contacts.contactId > 2 ");
    }

    // if the filter does not specify contacts by ID
    if (!includesIdFilter(filter)) {
        if (aggregating) {
            // exclude non-aggregates, unless the filter specifies syncTarget
            if (!includesSyncTarget(filter)) {
                constraints.append("Contacts.syncTarget = 'aggregate' ");
            }
        }

        // exclude deactivated unless they're explicitly included
        if (!includesDeactivated(filter)) {
            constraints.append("Contacts.isDeactivated = 0 ");
        }
    }

    // some (union) filters can add spurious braces around empty expressions
    bool emptyFilter = false;
    {
        QString strippedWhere = where;
        strippedWhere.remove(QChar('('));
        strippedWhere.remove(QChar(')'));
        strippedWhere.remove(QChar(' '));
        emptyFilter = strippedWhere.isEmpty();
    }

    if (emptyFilter && constraints.isEmpty())
        return QString();

    QString whereClause(QString::fromLatin1("WHERE "));
    if (!constraints.isEmpty()) {
        whereClause += constraints.join(QLatin1String("AND "));
        if (!emptyFilter) {
            whereClause += QLatin1String("AND ");
        }
    }
    if (!emptyFilter) {
        whereClause += where;
    }

    return whereClause;
}

}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order,
        const QContactFetchHint &fetchHint)
{
    QMutexLocker locker(m_database.accessMutex());

    m_database.clearTemporaryContactIdsTable(table);

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(order, &join, &transientModifiedRequired, &globalPresenceRequired, m_database.localized());

    bool whereFailed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, m_database, table, &bindings, &whereFailed, &transientModifiedRequired, &globalPresenceRequired);
    if (whereFailed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter, m_database.aggregating());

    if (transientModifiedRequired || globalPresenceRequired) {
        // Provide the temporary transient state information to filter/sort on
        if (!m_database.populateTemporaryTransientState(transientModifiedRequired, globalPresenceRequired)) {
            return QContactManager::UnspecifiedError;
        }

        if (transientModifiedRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.Timestamps ON Contacts.contactId = temp.Timestamps.contactId"));
        }
        if (globalPresenceRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.GlobalPresenceStates ON Contacts.contactId = temp.GlobalPresenceStates.contactId"));
        }
    }

    const int maximumCount = fetchHint.maxCountHint();

    QContactManager::Error error = QContactManager::NoError;
    if (!m_database.createTemporaryContactIdsTable(table, join, where, orderBy, bindings, maximumCount)) {
        error = QContactManager::UnspecifiedError;
    } else {
        error = queryContacts(table, contacts, fetchHint);
    }

    return error;
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<QContactId> &contactIds,
        const QContactFetchHint &fetchHint)
{
    QList<quint32> databaseIds;
    databaseIds.reserve(contactIds.size());

    foreach (const QContactId &id, contactIds) {
        databaseIds.append(ContactId::databaseId(id));
    }

    return readContacts(table, contacts, databaseIds, fetchHint);
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<quint32> &databaseIds,
        const QContactFetchHint &fetchHint,
        bool relaxConstraints)
{
    QMutexLocker locker(m_database.accessMutex());

    QVariantList boundIds;
    boundIds.reserve(databaseIds.size());
    foreach (quint32 id, databaseIds) {
        boundIds.append(id);
    }

    contacts->reserve(databaseIds.size());

    m_database.clearTemporaryContactIdsTable(table);

    const int maximumCount = fetchHint.maxCountHint();

    QContactManager::Error error = QContactManager::NoError;
    if (!m_database.createTemporaryContactIdsTable(table, boundIds, maximumCount)) {
        error = QContactManager::UnspecifiedError;
    } else {
        error = queryContacts(table, contacts, fetchHint, relaxConstraints);
    }

    // the ordering of the queried contacts is identical to
    // the ordering of the input contact ids list.
    int contactIdsSize = databaseIds.size();
    int contactsSize = contacts->size();
    if (contactIdsSize != contactsSize) {
        for (int i = 0; i < contactIdsSize; ++i) {
            if (i >= contactsSize || ContactId::databaseId((*contacts)[i].id()) != databaseIds[i]) {
                // the id list contained a contact id which doesn't exist
                contacts->insert(i, QContact());
                contactsSize++;
                error = QContactManager::DoesNotExistError;
            }
        }
    }

    return error;
}

QContactManager::Error ContactReader::queryContacts(
        const QString &tableName, QList<QContact> *contacts, const QContactFetchHint &fetchHint, bool relaxConstraints)
{
    QContactManager::Error err = QContactManager::NoError;

    const QString idsQueryStatement(QString::fromLatin1(
        "SELECT " // order and content can change due to schema upgrades, so list manually.
            "Contacts.contactId, "
            "Contacts.displayLabel, "
            "Contacts.displayLabelGroup, "
            "Contacts.firstName, "
            "Contacts.lowerFirstName, "
            "Contacts.lastName, "
            "Contacts.lowerLastName, "
            "Contacts.middleName, "
            "Contacts.prefix, "
            "Contacts.suffix, "
            "Contacts.customLabel, "
            "Contacts.syncTarget, "
            "Contacts.created, "
            "Contacts.modified, "
            "Contacts.gender, "
            "Contacts.isFavorite, "
            "Contacts.hasPhoneNumber, "
            "Contacts.hasEmailAddress, "
            "Contacts.hasOnlineAccount, "
            "Contacts.isOnline, "
            "Contacts.isDeactivated, "
            "Contacts.isIncidental, "
            "Contacts.type "
        "FROM temp.%1 "
        "CROSS JOIN Contacts ON temp.%1.contactId = Contacts.contactId " // Cross join ensures we scan the temp table first
        "ORDER BY temp.%1.rowId ASC").arg(tableName));

    const QString relationshipQueryStatement(QString::fromLatin1(
        "SELECT "
            "temp.%1.contactId AS contactId,"
            "R1.type AS secondType,"
            "R1.firstId AS firstId,"
            "R2.type AS firstType,"
            "R2.secondId AS secondId "
        "FROM temp.%1 "
        "LEFT JOIN Relationships AS R1 ON R1.secondId = temp.%1.contactId " // Must join in this order to get correct query plan
        "LEFT JOIN Relationships AS R2 ON R2.firstId = temp.%1.contactId "
        "ORDER BY contactId ASC").arg(tableName));

    QSqlQuery contactQuery(m_database);
    QSqlQuery relationshipQuery(m_database);

    // Prepare the query for the contact properties
    if (!contactQuery.prepare(idsQueryStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for contact details:\n%1\nQuery:\n%2")
                .arg(contactQuery.lastError().text())
                .arg(idsQueryStatement));
        err = QContactManager::UnspecifiedError;
    } else {
        contactQuery.setForwardOnly(true);
        if (!ContactsDatabase::execute(contactQuery)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to execute query for contact details:\n%1\nQuery:\n%2")
                    .arg(contactQuery.lastError().text())
                    .arg(idsQueryStatement));
            err = QContactManager::UnspecifiedError;
        } else {
            QContactFetchHint::OptimizationHints optimizationHints(fetchHint.optimizationHints());
            const bool fetchRelationships((optimizationHints & QContactFetchHint::NoRelationships) == 0);

            if (fetchRelationships) {
                // Prepare the query for the contact relationships
                if (!relationshipQuery.prepare(relationshipQueryStatement)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for relationships:\n%1\nQuery:\n%2")
                            .arg(relationshipQuery.lastError().text())
                            .arg(relationshipQueryStatement));
                    err = QContactManager::UnspecifiedError;
                } else {
                    relationshipQuery.setForwardOnly(true);
                    if (!ContactsDatabase::execute(relationshipQuery)) {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for relationships:\n%1\nQuery:\n%2")
                                .arg(relationshipQuery.lastError().text())
                                .arg(relationshipQueryStatement));
                        err = QContactManager::UnspecifiedError;
                    } else {
                        // Move to the first row
                        relationshipQuery.next();
                    }
                }
            }

            if (err == QContactManager::NoError) {
                err = queryContacts(tableName, contacts, fetchHint, relaxConstraints, contactQuery, relationshipQuery);
            }

            contactQuery.finish();
            if (fetchRelationships) {
                relationshipQuery.finish();
            }
        }
    }

    return err;
}

QContactManager::Error ContactReader::queryContacts(
        const QString &tableName, QList<QContact> *contacts, const QContactFetchHint &fetchHint, bool relaxConstraints, QSqlQuery &contactQuery, QSqlQuery &relationshipQuery)
{
    // Formulate the query to fetch the contact details
    const QString detailQueryTemplate(QString::fromLatin1(
        "SELECT "
            "Details.detailId,"
            "Details.contactId,"
            "Details.detail,"
            "Details.detailUri,"
            "Details.linkedDetailUris,"
            "Details.contexts,"
            "Details.accessConstraints,"
            "Details.provenance,"
            "COALESCE(Details.modifiable, 0),"
            "COALESCE(Details.nonexportable, 0),"
            "%1 "
        "FROM temp.%2 "
        "CROSS JOIN Details ON Details.contactId = temp.%2.contactId " // Cross join ensures we scan the temp table first
        "%3 "
        "%4 "
        "ORDER BY temp.%2.rowId ASC"));

    const QString selectTemplate(QString::fromLatin1(
        "%1.*"));
    const QString joinTemplate(QString::fromLatin1(
        "LEFT JOIN %1 ON %1.detailId = Details.detailId"));
    const QString detailNameTemplate(QString::fromLatin1(
        "WHERE Details.detail IN ('%1')"));

    QStringList selectSpec;
    QStringList joinSpec;
    QStringList detailNameSpec;

    QHash<QString, QPair<ReadDetail, int> > readProperties;

    // Skip the Details table fields, and the indexing fields of the first join table
    int offset = 10 + 2;

    const ContactWriter::DetailList &definitionMask = fetchHint.detailTypesHint();

    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!detail.read)
            continue;

        if (definitionMask.isEmpty() || definitionMask.contains(detail.detailType)) {
            // we need to join this particular detail table
            const QString detailTable(QString::fromLatin1(detail.table));
            const QString detailName(QString::fromLatin1(detail.detailName));

            selectSpec.append(selectTemplate.arg(detailTable));
            joinSpec.append(joinTemplate.arg(detailTable));
            detailNameSpec.append(detailName);

            readProperties.insert(detailName, qMakePair(detail.read, offset));
            offset += detail.fieldCount + (detail.includesContext ? 1 : 2);
        }
    }

    // Formulate the query string we need
    QString detailQueryStatement(detailQueryTemplate.arg(selectSpec.join(QChar::fromLatin1(','))));
    detailQueryStatement = detailQueryStatement.arg(tableName);
    detailQueryStatement = detailQueryStatement.arg(joinSpec.join(QChar::fromLatin1(' ')));
    if (definitionMask.isEmpty())
        detailQueryStatement = detailQueryStatement.arg(QString());
    else
        detailQueryStatement = detailQueryStatement.arg(detailNameTemplate.arg(detailNameSpec.join(QLatin1String("','"))));

    // If selectSpec is empty, all required details are in the Contacts table
    QSqlQuery detailQuery(m_database);
    if (!selectSpec.isEmpty()) {
        if (!detailQuery.prepare(detailQueryStatement)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for joined details:\n%1\nQuery:\n%2")
                    .arg(detailQuery.lastError().text())
                    .arg(detailQueryStatement));
            return QContactManager::UnspecifiedError;
        }

        // Read the details for these contacts
        detailQuery.setForwardOnly(true);
        if (!ContactsDatabase::execute(detailQuery)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for joined details:\n%1\nQuery:\n%2")
                    .arg(detailQuery.lastError().text())
                    .arg(detailQueryStatement));
            return QContactManager::UnspecifiedError;
        } else {
            // Move to the first row
            detailQuery.next();
        }
    }

    const bool includeRelationships(relationshipQuery.isValid());
    const bool includeDetails(detailQuery.isValid());

    // We need to report our retrievals periodically
    int unreportedCount = 0;

    const int maximumCount = fetchHint.maxCountHint();
    const int batchSize = (maximumCount > 0) ? 0 : ReportBatchSize; // If count is constrained, don't report periodically

    while (contactQuery.next()) {
        int col = 0;
        quint32 dbId = contactQuery.value(col++).toUInt();

        QContact contact;

        QContactId id(ContactId::contactId(ContactId::apiId(dbId)));
        contact.setId(id);

        QString persistedDL = contactQuery.value(col++).toString();
        QString displayLabelGroup = contactQuery.value(col++).toString();
        ContactsEngine::setContactDisplayLabel(&contact, persistedDL, displayLabelGroup);

        QContactName name;
        setValue(&name, QContactName::FieldFirstName  , contactQuery.value(col++));
        col++; // ignore lowerFirstName
        setValue(&name, QContactName::FieldLastName   , contactQuery.value(col++));
        col++; // ignore lowerLastName
        setValue(&name, QContactName::FieldMiddleName , contactQuery.value(col++));
        setValue(&name, QContactName::FieldPrefix     , contactQuery.value(col++));
        setValue(&name, QContactName::FieldSuffix     , contactQuery.value(col++));
        setValue(&name, QContactName__FieldCustomLabel, contactQuery.value(col++));
        if (!name.isEmpty())
            contact.saveDetail(&name);

        const QString syncTarget(contactQuery.value(col++).toString());

        QContactSyncTarget starget;
        setValue(&starget, QContactSyncTarget::FieldSyncTarget, syncTarget);
        if (!starget.isEmpty())
            contact.saveDetail(&starget);

        QContactTimestamp timestamp;
        setValue(&timestamp, QContactTimestamp::FieldCreationTimestamp    , ContactsDatabase::fromDateTimeString(contactQuery.value(col++).toString()));
        setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, ContactsDatabase::fromDateTimeString(contactQuery.value(col++).toString()));

        QContactGender gender;
        // Gender is an enum in qtpim
        QString genderText = contactQuery.value(col++).toString();
        gender.setGender(static_cast<QContactGender::GenderField>(genderText.toInt()));
        contact.saveDetail(&gender);

        QContactFavorite favorite;
        setValue(&favorite, QContactFavorite::FieldFavorite, contactQuery.value(col++).toBool());
        if (!favorite.isEmpty())
            contact.saveDetail(&favorite);

        QContactStatusFlags flags;
        flags.setFlag(QContactStatusFlags::HasPhoneNumber, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::HasEmailAddress, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::HasOnlineAccount, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::IsOnline, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::IsDeactivated, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::IsIncidental, contactQuery.value(col++).toBool());

        if (flags.testFlag(QContactStatusFlags::IsDeactivated)) {
            QContactDeactivated deactivated;
            contact.saveDetail(&deactivated);
        }
        if (flags.testFlag(QContactStatusFlags::IsIncidental)) {
            QContactIncidental incidental;
            contact.saveDetail(&incidental);
        }

        int contactType = contactQuery.value(col++).toInt();
        QContactType typeDetail = contact.detail<QContactType>();
        typeDetail.setType(static_cast<QContactType::TypeValues>(contactType));
        contact.saveDetail(&typeDetail);

        bool syncable = !syncTarget.isEmpty() &&
                        (syncTarget != aggregateSyncTarget) &&
                        (syncTarget != localSyncTarget) &&
                        (syncTarget != wasLocalSyncTarget);

        QSet<QContactDetail::DetailType> transientTypes;

        // Find any transient details for this contact
        if (m_database.hasTransientDetails(dbId)) {
            const QPair<QDateTime, QList<QContactDetail> > transientDetails(m_database.transientDetails(dbId));
            if (!transientDetails.first.isNull()) {
                // Update the contact timestamp to that of the transient details
                setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, transientDetails.first);

                QList<QContactDetail>::const_iterator it = transientDetails.second.constBegin(), end = transientDetails.second.constEnd();
                for ( ; it != end; ++it) {
                    // Copy the transient detail into the contact
                    const QContactDetail &transient(*it);

                    const QContactDetail::DetailType transientType(transient.type());

                    if (transientType == QContactGlobalPresence::Type) {
                        // If global presence is in the transient details, the IsOnline status flag is out of date
                        const int presenceState = transient.value<int>(QContactGlobalPresence::FieldPresenceState);
                        const bool isOnline(presenceState >= QContactPresence::PresenceAvailable &&
                                            presenceState <= QContactPresence::PresenceExtendedAway);
                        flags.setFlag(QContactStatusFlags::IsOnline, isOnline);
                    }

                    // Ignore details that aren't in the requested types
                    if (!definitionMask.isEmpty() && !definitionMask.contains(transientType)) {
                        continue;
                    }

                    QContactDetail detail(transient.type());
                    if (!relaxConstraints) {
                        QContactManagerEngine::setDetailAccessConstraints(&detail, transient.accessConstraints());
                    }

                    const QMap<int, QVariant> values(transient.values());
                    QMap<int, QVariant>::const_iterator vit = values.constBegin(), vend = values.constEnd();
                    for ( ; vit != vend; ++vit) {
                        bool append(true);

                        if (vit.key() == QContactDetail__FieldModifiable) {
                            append = syncable;
                        }

                        if (append) {
                            detail.setValue(vit.key(), vit.value());
                        }
                    }

                    contact.saveDetail(&detail);

                    transientTypes.insert(transientType);
                }
            }
        }

        // Add the updated status flags
        QContactManagerEngine::setDetailAccessConstraints(&flags, QContactDetail::ReadOnly | QContactDetail::Irremovable);
        contact.saveDetail(&flags);

        // Add the timestamp info
        if (!timestamp.isEmpty())
            contact.saveDetail(&timestamp);

        // Add the details of this contact from the detail tables
        if (includeDetails) {
            if (detailQuery.isValid()) {
                do {
                    const quint32 contactId = detailQuery.value(1).toUInt();
                    if (contactId != dbId) {
                        break;
                    }

                    const quint32 detailId = detailQuery.value(0).toUInt();
                    const QString detailName = detailQuery.value(2).toString();

                    // Are we reporting this detail type?
                    const QPair<ReadDetail, int> properties(readProperties[detailName]);
                    if (properties.first && properties.second) {
                        // Are there transient details of this type for this contact?
                        const QContactDetail::DetailType detailType(detailIdentifier(detailName));
                        if (transientTypes.contains(detailType)) {
                            // This contact has transient details of this type; skip the extraction
                            continue;
                        }

                        // Extract the values from the result row
                        properties.first(&contact, detailQuery, contactId, detailId, syncable, syncTarget, relaxConstraints, properties.second);
                    }
                } while (detailQuery.next());
            }
        }

        if (includeRelationships) {
            // Find any relationships for this contact
            if (relationshipQuery.isValid()) {
                // Find the relationships for the contacts in this batch
                QList<QContactRelationship> relationships;

                do {
                    const quint32 contactId = relationshipQuery.value(0).toUInt();
                    if (contactId != dbId) {
                        break;
                    }

                    const QString secondType = relationshipQuery.value(1).toString();
                    const quint32 firstId = relationshipQuery.value(2).toUInt();
                    const QString firstType = relationshipQuery.value(3).toString();
                    const quint32 secondId = relationshipQuery.value(4).toUInt();

                    if (!firstType.isEmpty()) {
                        QContactRelationship rel(makeRelationship(firstType, contactId, secondId));
                        relationships.append(rel);
                    } else if (!secondType.isEmpty()) {
                        QContactRelationship rel(makeRelationship(secondType, firstId, contactId));
                        relationships.append(rel);
                    }
                } while (relationshipQuery.next());

                QContactManagerEngine::setContactRelationships(&contact, relationships);
            }
        }

        // Append this contact to the output set
        contacts->append(contact);

        // Periodically report our retrievals
        if (++unreportedCount == batchSize) {
            unreportedCount = 0;
            contactsAvailable(*contacts);
        }
    }

    detailQuery.finish();

    // If any retrievals are not yet reported, do so now
    if (unreportedCount > 0) {
        contactsAvailable(*contacts);
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readDeletedContactIds(
        QList<QContactId> *contactIds,
        const QContactFilter &filter)
{
    QDateTime since;
    QString syncTarget;

    // The only queries we support regarding deleted contacts are for the IDs, possibly
    // intersected with a syncTarget detail filter
    if (filter.type() == QContactFilter::ChangeLogFilter) {
        const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(filter));
        since = changeLogFilter.since();
    } else if (filter.type() == QContactFilter::IntersectionFilter) {
        const QContactIntersectionFilter &intersectionFilter(static_cast<const QContactIntersectionFilter &>(filter));
        foreach (const QContactFilter &partialFilter, intersectionFilter.filters()) {
            const QContactFilter::FilterType filterType(partialFilter.type());

            if (filterType == QContactFilter::ChangeLogFilter) {
                const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(partialFilter));
                since = changeLogFilter.since();
            } else if (filterType == QContactFilter::ContactDetailFilter) {
                const QContactDetailFilter &detailFilter(static_cast<const QContactDetailFilter &>(partialFilter));
                if (filterOnField<QContactSyncTarget>(detailFilter, QContactSyncTarget::FieldSyncTarget)) {
                    syncTarget = detailFilter.value().toString();
                } else {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot readDeletedContactIds with unsupported detail filter type: %1").arg(detailFilter.detailType()));
                    return QContactManager::UnspecifiedError;
                }
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot readDeletedContactIds with invalid filter type: %1").arg(filterType));
                return QContactManager::UnspecifiedError;
            }
        }
    }

    QStringList restrictions;
    QVariantList bindings;
    if (!since.isNull()) {
        restrictions.append(QString::fromLatin1("deleted >= ?"));
        bindings.append(ContactsDatabase::dateTimeString(since.toUTC()));
    }
    if (!syncTarget.isNull()) {
        restrictions.append(QString::fromLatin1("syncTarget = ?"));
        bindings.append(syncTarget);
    }

    QString queryStatement(QString::fromLatin1("SELECT contactId FROM DeletedContacts"));
    if (!restrictions.isEmpty()) {
        queryStatement.append(QString::fromLatin1(" WHERE "));
        queryStatement.append(restrictions.takeFirst());
        if (!restrictions.isEmpty()) {
            queryStatement.append(QString::fromLatin1(" AND "));
            queryStatement.append(restrictions.takeFirst());
        }
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(queryStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare deleted contacts ids:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryStatement));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query deleted contacts ids\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryStatement));
        return QContactManager::UnspecifiedError;
    }

    do {
        for (int i = 0; i < ReportBatchSize && query.next(); ++i) {
            contactIds->append(ContactId::apiId(query.value(0).toUInt()));
        }
        contactIdsAvailable(*contactIds);
    } while (query.isValid());

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readContactIds(
        QList<QContactId> *contactIds,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order)
{
    QMutexLocker locker(m_database.accessMutex());

    // Is this a query on deleted contacts?
    if (deletedContactFilter(filter)) {
        return readDeletedContactIds(contactIds, filter);
    }

    // Use a dummy table name to identify any temporary tables we create
    const QString tableName(QString::fromLatin1("readContactIds"));

    m_database.clearTransientContactIdsTable(tableName);

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(order, &join, &transientModifiedRequired, &globalPresenceRequired, m_database.localized());

    bool failed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, m_database, tableName, &bindings, &failed, &transientModifiedRequired, &globalPresenceRequired);
    if (failed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter, m_database.aggregating());

    if (transientModifiedRequired || globalPresenceRequired) {
        // Provide the temporary transient state information to filter/sort on
        if (!m_database.populateTemporaryTransientState(transientModifiedRequired, globalPresenceRequired)) {
            return QContactManager::UnspecifiedError;
        }

        if (transientModifiedRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.Timestamps ON Contacts.contactId = temp.Timestamps.contactId"));
        }
        if (globalPresenceRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.GlobalPresenceStates ON Contacts.contactId = temp.GlobalPresenceStates.contactId"));
        }
    }

    QString queryString = QString(QLatin1String(
                "\n SELECT DISTINCT Contacts.contactId"
                "\n FROM Contacts %1"
                "\n %2")).arg(join).arg(where);
    if (!orderBy.isEmpty()) {
        queryString.append(QString::fromLatin1(" ORDER BY ") + orderBy);
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(queryString)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare contacts ids:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query contacts ids\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    } else {
        debugFilterExpansion("Contact IDs selection:", queryString, bindings);
    }

    do {
        for (int i = 0; i < ReportBatchSize && query.next(); ++i) {
            contactIds->append(ContactId::apiId(query.value(0).toUInt()));
        }
        contactIdsAvailable(*contactIds);
    } while (query.isValid());

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::getIdentity(
        ContactsDatabase::Identity identity, QContactId *contactId)
{
    QMutexLocker locker(m_database.accessMutex());

    if (identity == ContactsDatabase::SelfContactId) {
        // we don't allow setting the self contact id, it's always static
        *contactId = ContactId::apiId(selfId);
    } else {
        const QString identityId(QStringLiteral(
            " SELECT contactId"
            " FROM Identities"
            " WHERE identity = :identity"
        ));

        ContactsDatabase::Query query(m_database.prepare(identityId));
        query.bindValue(":identity", identity);
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch contact identity");
            return QContactManager::UnspecifiedError;
        }
        if (!query.next()) {
            *contactId = QContactId();
            return QContactManager::UnspecifiedError;
        } else {
            *contactId = ContactId::apiId(query.value<quint32>(0));
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readRelationships(
        QList<QContactRelationship> *relationships,
        const QString &type,
        const QContactId &first,
        const QContactId &second)
{
    QMutexLocker locker(m_database.accessMutex());

    QStringList whereStatements;
    QVariantList bindings;
    if (!type.isEmpty()) {
        whereStatements.append(QLatin1String("type = ?"));
        bindings.append(type);
    }

    quint32 firstId = ContactId::databaseId(first);
    if (firstId != 0) {
        whereStatements.append(QLatin1String("firstId = ?"));
        bindings.append(firstId);
    }

    quint32 secondId = ContactId::databaseId(second);
    if (secondId != 0) {
        whereStatements.append(QLatin1String("secondId = ?"));
        bindings.append(secondId);
    }

    const QString where = !whereStatements.isEmpty()
            ? QLatin1String("\n WHERE ") + whereStatements.join(QLatin1String(" AND "))
            : QString();

    QString statement = QLatin1String(
            "\n SELECT type, firstId, secondId"
            "\n FROM Relationships") + where + QLatin1String(";");

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare relationships query:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query relationships: %1")
                .arg(query.lastError().text()));
        return QContactManager::UnspecifiedError;
    }

    while (query.next()) {
        QString type = query.value(0).toString();
        quint32 firstId = query.value(1).toUInt();
        quint32 secondId = query.value(2).toUInt();

        relationships->append(makeRelationship(type, firstId, secondId));
    }
    query.finish();

    return QContactManager::NoError;
}

bool ContactReader::fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values)
{
    QVariantList keyNames;

    QString statement(QString::fromLatin1("SELECT name, value, compressed FROM OOB WHERE name "));
    if (keys.isEmpty()) {
        statement.append(QString::fromLatin1("LIKE '%1:%%'").arg(scope));
    } else {
        const QChar colon(QChar::fromLatin1(':'));

        QString keyList;
        foreach (const QString &key, keys) {
            keyNames.append(scope + colon + key);
            keyList.append(QString::fromLatin1(keyList.isEmpty() ? "?" : ",?"));
        }
        statement.append(QString::fromLatin1("IN (%1)").arg(keyList));
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB query:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return false;
    }

    foreach (const QVariant &name, keyNames) {
        query.addBindValue(name);
    }

    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query OOB: %1")
                .arg(query.lastError().text()));
        return false;
    }
    while (query.next()) {
        const QString name(query.value(0).toString());
        const QVariant value(query.value(1));
        const quint32 compressed(query.value(2).toUInt());

        const QString key(name.mid(scope.length() + 1));
        if (compressed > 0) {
            QByteArray compressedData(value.value<QByteArray>());
            if (compressed == 1) {
                // QByteArray data
                values->insert(key, QVariant(qUncompress(compressedData)));
            } else if (compressed == 2) {
                // QString data
                values->insert(key, QVariant(QString::fromUtf8(qUncompress(compressedData))));
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid compression type for OOB data:%1, key:%2")
                        .arg(compressed).arg(key));
            }
        } else {
            values->insert(key, value);
        }
    }
    query.finish();

    return true;
}

bool ContactReader::fetchOOBKeys(const QString &scope, QStringList *keys)
{
    QString statement(QString::fromLatin1("SELECT name FROM OOB WHERE name LIKE '%1:%%'").arg(scope));

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB query:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return false;
    }

    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query OOB: %1")
                .arg(query.lastError().text()));
        return false;
    }
    while (query.next()) {
        const QString name(query.value(0).toString());
        keys->append(name.mid(scope.length() + 1));
    }
    query.finish();

    return true;
}

void ContactReader::contactsAvailable(const QList<QContact> &)
{
}

void ContactReader::contactIdsAvailable(const QList<QContactId> &)
{
}
