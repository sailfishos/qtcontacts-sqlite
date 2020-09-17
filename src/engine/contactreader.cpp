/*
 * Copyright (c) 2013 - 2019 Jolla Ltd.
 * Copyright (c) 2019 - 2020 Open Mobile Platform LLC.
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
#include <QContactCollectionFilter>
#include <QContactChangeLogFilter>
#include <QContactUnionFilter>
#include <QContactIntersectionFilter>
#include <QContactRelationshipFilter>

#include <QContact>
#include <QContactCollection>

#include <QContactManagerEngine>

#include <QSqlError>
#include <QVector>

#include <QtDebug>
#include <QElapsedTimer>

static const int ReportBatchSize = 50;

static const QString aggregateSyncTarget(QStringLiteral("aggregate"));
static const QString localSyncTarget(QStringLiteral("local"));
static const QString wasLocalSyncTarget(QStringLiteral("was_local"));

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

static void setDetailImmutableIfAggregate(bool isAggregate, QContactDetail *detail)
{
    // all details of an aggregate contact are immutable.
    if (isAggregate) {
        setValue(detail, QContactDetail__FieldModifiable, false);
        QContactManagerEngine::setDetailAccessConstraints(detail, QContactDetail::ReadOnly | QContactDetail::Irremovable);
    }
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

static const FieldInfo timestampFields[] =
{
    { QContactTimestamp::FieldCreationTimestamp, "created", DateField },
    { QContactTimestamp::FieldModificationTimestamp, "modified", DateField }
};

static const FieldInfo statusFlagsFields[] =
{
    // No specific field; tests hasPhoneNumber/hasEmailAddress/hasOnlineAccount/isOnline/isDeactivated/isDeleted
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
    { QContactAvatar::FieldMetaData, "avatarMetadata", StringField }
};

static void setValues(QContactAvatar *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAvatar T;

    setValue(detail, T::FieldImageUrl, urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldVideoUrl, urlValue(query->value(offset + 1)));
    setValue(detail, QContactAvatar::FieldMetaData, query->value(offset + 2));
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

static const FieldInfo displayLabelFields[] =
{
    { QContactDisplayLabel::FieldLabel, "displayLabel", LocalizedField },
    { QContactDisplayLabel__FieldLabelGroup, "displayLabelGroup", LocalizedField },
    { QContactDisplayLabel__FieldLabelGroupSortOrder, "displayLabelGroupSortOrder", IntegerField }
};

static void setValues(QContactDisplayLabel *detail, QSqlQuery *query, const int offset)
{
    typedef QContactDisplayLabel T;

    const QString label = query->value(offset + 0).toString();
    const QString group = query->value(offset + 1).toString();
    const int sortOrder = query->value(offset + 2).toInt();

    if (!label.trimmed().isEmpty())
        setValue(detail, T::FieldLabel, label);
    if (!group.trimmed().isEmpty())
        setValue(detail, QContactDisplayLabel__FieldLabelGroup, group);
    if (!label.trimmed().isEmpty() || !group.trimmed().isEmpty())
        setValue(detail, QContactDisplayLabel__FieldLabelGroupSortOrder, sortOrder);
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

static const FieldInfo favoriteFields[] =
{
    { QContactFavorite::FieldFavorite, "isFavorite", BooleanField },
};

static void setValues(QContactFavorite *detail, QSqlQuery *query, const int offset)
{
    typedef QContactFavorite T;

    setValue(detail, T::FieldFavorite  , query->value(offset + 0).toBool());
}

static const FieldInfo genderFields[] =
{
    { QContactGender::FieldGender, "gender", StringField },
};

static void setValues(QContactGender *detail, QSqlQuery *query, const int offset)
{
    typedef QContactGender T;

    setValue(detail, T::FieldGender, static_cast<QContactGender::GenderType>(query->value(offset + 0).toString().toInt()));
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

static const FieldInfo nameFields[] =
{
    { QContactName::FieldFirstName, "firstName", LocalizedField },
    { invalidField, "lowerFirstName", LocalizedField },
    { QContactName::FieldLastName, "lastName", LocalizedField },
    { invalidField, "lowerLastName", LocalizedField },
    { QContactName::FieldMiddleName, "middleName", LocalizedField },
    { QContactName::FieldPrefix, "prefix", LocalizedField },
    { QContactName::FieldSuffix, "suffix", LocalizedField },
    { QContactName::FieldCustomLabel, "customLabel", LocalizedField }
};

static void setValues(QContactName *detail, QSqlQuery *query, const int offset)
{
    typedef QContactName T;

    setValue(detail, T::FieldFirstName, query->value(offset + 0));
    // ignore lowerFirstName
    setValue(detail, T::FieldLastName, query->value(offset + 2));
    // ignore lowerLastName
    setValue(detail, T::FieldMiddleName, query->value(offset + 4));
    setValue(detail, T::FieldPrefix, query->value(offset + 5));
    setValue(detail, T::FieldSuffix, query->value(offset + 6));
    setValue(detail, T::FieldCustomLabel, query->value(offset + 7));
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
    { QContactPhoneNumber::FieldNormalizedNumber, "normalizedNumber", StringField },
    { QContactPhoneNumber::FieldSubTypes, "subTypes", StringListField }
};

static void setValues(QContactPhoneNumber *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPhoneNumber T;

    setValue(detail, T::FieldNumber  , query->value(offset + 0));

    const QStringList subTypeValues(query->value(offset + 1).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(subTypeList(subTypeValues)));

    setValue(detail, QContactPhoneNumber::FieldNormalizedNumber, query->value(offset + 2));
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

static const FieldInfo syncTargetFields[] =
{
    { QContactSyncTarget::FieldSyncTarget, "syncTarget", StringField }
};

static void setValues(QContactSyncTarget *detail, QSqlQuery *query, const int offset)
{
    typedef QContactSyncTarget T;

    setValue(detail, T::FieldSyncTarget, query->value(offset + 0));
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

    rv.insert(QStringLiteral("Home"), QContactDetail::ContextHome);
    rv.insert(QStringLiteral("Work"), QContactDetail::ContextWork);
    rv.insert(QStringLiteral("Other"), QContactDetail::ContextOther);

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
static void readDetail(QContact *contact, QSqlQuery &query, quint32 contactId, quint32 detailId, bool syncable, const QContactCollectionId &apiCollectionId, bool relaxConstraints, bool keepChangeFlags, int offset)
{
    const quint32 collectionId = ContactCollectionId::databaseId(apiCollectionId);
    const bool aggregateContact(collectionId == ContactsDatabase::AggregateAddressbookCollectionId);

    T detail;

    int col = 0;
    const quint32 dbId = query.value(col++).toUInt();
    Q_ASSERT(dbId == detailId);
    /*const quint32 contactId = query.value(1).toUInt();*/ col++;
    /*const QString detailName = query.value(2).toString();*/ col++;
    const QString detailUriValue = query.value(col++).toString();
    const QString linkedDetailUrisValue = query.value(col++).toString();
    const QString contextValue = query.value(col++).toString();
    const int accessConstraints = query.value(col++).toInt();
    QString provenance = query.value(col++).toString();
    const QVariant modifiableVariant = query.value(col++);
    const bool nonexportable = query.value(col++).toBool();
    const int changeFlags = query.value(col++).toInt();

    // only save the detail to the contact if it hasn't been deleted,
    // or if we are part of a sync fetch (i.e. keepChangeFlags is true)
    if (!keepChangeFlags && changeFlags >= 4) { // ChangeFlags::IsDeleted
        return;
    }

    setValue(&detail, QContactDetail__FieldDatabaseId, dbId);

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
            const int type = contextType(context);
            if (type != -1) {
                contexts.append(type);
            }
        }
        if (!contexts.isEmpty()) {
            detail.setContexts(contexts);
        }
    }

    // If the detail is not aggregated from another, then its provenance should match its ID.
    setValue(&detail, QContactDetail::FieldProvenance, aggregateContact ? provenance : QStringLiteral("%1:%2:%3").arg(collectionId).arg(contactId).arg(dbId));

    // Only report modifiable state for non-local contacts.
    // local contacts are always (implicitly) modifiable.
    if (syncable && !modifiableVariant.isNull() && modifiableVariant.isValid()) {
        setValue(&detail, QContactDetail__FieldModifiable, modifiableVariant.toBool());
    }

    // Only include non-exportable if it is set
    if (nonexportable) {
        setValue(&detail, QContactDetail__FieldNonexportable, nonexportable);
    }

    if (keepChangeFlags) {
        setValue(&detail, QContactDetail__FieldChangeFlags, changeFlags);
    }

    // Constraints should be applied unless generating a partial aggregate; the partial aggregate
    // is intended for modification, so adding constraints prevents it from being used correctly.
    // Normal aggregate contact details are always immutable.
    if (!relaxConstraints) {
        QContactManagerEngine::setDetailAccessConstraints(&detail, static_cast<QContactDetail::AccessConstraints>(accessConstraints));
    }

    setValues(&detail, &query, offset);
    setDetailImmutableIfAggregate(aggregateContact, &detail);
    contact->saveDetail(&detail, QContact::IgnoreAccessConstraints);
}

template <typename T>
static void appendUniqueDetail(QList<QContactDetail> *details, QSqlQuery &query)
{
    T detail;

    setValues(&detail, &query, 0);

    details->append(detail);
}

static QContactRelationship makeRelationship(const QString &type, quint32 firstId, quint32 secondId, const QString &manager_uri)
{
    QContactRelationship relationship;
    relationship.setRelationshipType(type);

    relationship.setFirst(ContactId::apiId(firstId, manager_uri));
    relationship.setSecond(ContactId::apiId(secondId, manager_uri));

    return relationship;
}

typedef void (*ReadDetail)(QContact *contact, QSqlQuery &query, quint32 contactId, quint32 detailId, bool syncable, const QContactCollectionId &collectionId, bool relaxConstraints, bool keepChangeFlags, int offset);
typedef void (*AppendUniqueDetail)(QList<QContactDetail> *details, QSqlQuery &query);

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
    const AppendUniqueDetail appendUnique;

    QString where(bool queryContacts) const
    {
        return table && queryContacts
                ? QStringLiteral("Contacts.contactId IN (SELECT contactId FROM %1 WHERE %2)").arg(QLatin1String(table))
                : QStringLiteral("%2");
    }

    QString whereExists(bool queryContacts) const
    {
        if (!queryContacts) {
            return QString();
        } else if (table) {
            return QStringLiteral("EXISTS (SELECT contactId FROM %1 where contactId = Contacts.contactId)").arg(QLatin1String(table));
        } else {
            return QStringLiteral("Contacts.contactId != 0");
        }
    }

    QString orderByExistence(bool asc) const
    {
        return table ? QStringLiteral("CASE EXISTS (SELECT contactId FROM %1 where contactId = Contacts.contactId) WHEN 1 THEN %2 ELSE %3 END")
                       .arg(QLatin1String(table)).arg(asc ? 0 : 1).arg(asc ? 1 : 0)
                     : QString();
    }
};

template <typename T, int N> static int lengthOf(const T(&)[N]) { return N; }

template <typename T> QContactDetail::DetailType detailIdentifier() { return T::Type; }

#define PREFIX_LENGTH 8
#define DEFINE_DETAIL(Detail, Table, fields, includesContext, joinToSort) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, #Table, fields, lengthOf(fields), includesContext, joinToSort, readDetail<Detail>, appendUniqueDetail<Detail> }

#define DEFINE_DETAIL_PRIMARY_TABLE(Detail, fields) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, 0, fields, lengthOf(fields), false, false, nullptr, nullptr }

// Note: joinToSort should be true only if there can be only a single row for each contact in that table
static const DetailInfo detailInfo[] =
{
    DEFINE_DETAIL_PRIMARY_TABLE(QContactTimestamp,    timestampFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactStatusFlags,  statusFlagsFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactType,         typeFields),
    DEFINE_DETAIL(QContactAddress       , Addresses      , addressFields       , true , false),
    DEFINE_DETAIL(QContactAnniversary   , Anniversaries  , anniversaryFields   , false, false),
    DEFINE_DETAIL(QContactAvatar        , Avatars        , avatarFields        , false, false),
    DEFINE_DETAIL(QContactBirthday      , Birthdays      , birthdayFields      , false, true),
    DEFINE_DETAIL(QContactDisplayLabel  , DisplayLabels  , displayLabelFields  , false, true),
    DEFINE_DETAIL(QContactEmailAddress  , EmailAddresses , emailAddressFields  , true , false),
    DEFINE_DETAIL(QContactFamily        , Families       , familyFields        , false, false),
    DEFINE_DETAIL(QContactFavorite      , Favorites      , favoriteFields      , false, false),
    DEFINE_DETAIL(QContactGender        , Genders        , genderFields        , false, false),
    DEFINE_DETAIL(QContactGeoLocation   , GeoLocations   , geoLocationFields   , false, false),
    DEFINE_DETAIL(QContactGuid          , Guids          , guidFields          , false, true),
    DEFINE_DETAIL(QContactHobby         , Hobbies        , hobbyFields         , false, false),
    DEFINE_DETAIL(QContactName          , Names          , nameFields          , false, true),
    DEFINE_DETAIL(QContactNickname      , Nicknames      , nicknameFields      , false, false),
    DEFINE_DETAIL(QContactNote          , Notes          , noteFields          , false, false),
    DEFINE_DETAIL(QContactOnlineAccount , OnlineAccounts , onlineAccountFields , false, false),
    DEFINE_DETAIL(QContactOrganization  , Organizations  , organizationFields  , false, false),
    DEFINE_DETAIL(QContactPhoneNumber   , PhoneNumbers   , phoneNumberFields   , false, false),
    DEFINE_DETAIL(QContactPresence      , Presences      , presenceFields      , false, false),
    DEFINE_DETAIL(QContactRingtone      , Ringtones      , ringtoneFields      , false, false),
    DEFINE_DETAIL(QContactSyncTarget    , SyncTargets    , syncTargetFields    , false, false),
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

    static const DetailInfo nullDetail = { QContactDetail::TypeUndefined, "Undefined", "", 0, 0, false, false, nullptr, nullptr };
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
    names.insert(fieldName("Names", "firstName"), QStringLiteral("lowerFirstName"));
    names.insert(fieldName("Names", "lastName"), QStringLiteral("lowerLastName"));
    names.insert(fieldName("EmailAddresses", "emailAddress"), QStringLiteral("lowerEmailAddress"));
    names.insert(fieldName("OnlineAccounts", "accountUri"), QStringLiteral("lowerAccountUri"));
    names.insert(fieldName("Nicknames", "nickname"), QStringLiteral("lowerNickname"));
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

static QString buildWhere(const QContactCollectionFilter &filter, QVariantList *bindings, bool *failed)
{
    const QSet<QContactCollectionId> &filterIds(filter.collectionIds());
    if (filterIds.isEmpty()) {
        // "retrieve all contacts, regardless of collection".
        return QStringLiteral("Contacts.collectionId IS NOT NULL");
    } else if (filterIds.count() < 800) {
        QList<quint32> dbIds;
        dbIds.reserve(filterIds.count());
        bindings->reserve(filterIds.count());
        foreach (const QContactCollectionId &id, filterIds) {
            dbIds.append(ContactCollectionId::databaseId(id));
        }

        QString statement = QStringLiteral("Contacts.collectionId IN (?");
        bindings->append(dbIds.first());

        for (int i = 1; i < dbIds.count(); ++i) {
            statement += QStringLiteral(",?");
            bindings->append(dbIds.at(i));
        }

        return statement + QStringLiteral(")");
    } else {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with too large collection ID list"));
        return QStringLiteral("FALSE");
    }
}

static QString buildWhere(
        const QContactDetailFilter &filter,
        bool queryContacts,
        QVariantList *bindings,
        bool *failed,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired)
{
    if (filter.matchFlags() & QContactFilter::MatchKeypadCollation) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with filter requiring keypad collation"));
        return QStringLiteral("FAILED");
    }

    const DetailInfo &detail(detailInformation(filter.detailType()));
    if (detail.detailType == QContactDetail::TypeUndefined) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail type: %1").arg(filter.detailType()));
        return QStringLiteral("FAILED");
    }

    if (filter.detailField() == invalidField) {
        // If there is no field, we're simply testing for the existence of matching details
        return detail.whereExists(queryContacts);
    }

    const FieldInfo &field(fieldInformation(detail, filter.detailField()));
    if (field.field == invalidField) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail field: %1").arg(filter.detailField()));
        return QStringLiteral("FAILED");
    }

    if (!filter.value().isValid()     // "match if detail and field exists, don't care about value" filter
        || (filterOnField<QContactSyncTarget>(filter, QContactSyncTarget::FieldSyncTarget) &&
            filter.value().toString().isEmpty())) { // match all sync targets if empty sync target filter
        const QString comparison(QStringLiteral("%1 IS NOT NULL"));
        return detail.where(queryContacts).arg(comparison.arg(field.column));
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
                                                 QContactStatusFlags::IsAdded,
                                                 QContactStatusFlags::IsModified,
                                                 QContactStatusFlags::IsDeleted };
                static const char *flagColumns[] = { "hasPhoneNumber",
                                                     "hasEmailAddress",
                                                     "hasOnlineAccount",
                                                     "isOnline",
                                                     "isDeactivated",
                                                     "changeFlags",
                                                     "changeFlags",
                                                     "changeFlags" };

                quint64 flagsValue = filter.value().value<quint64>();

                QStringList clauses;
                if (filter.matchFlags() == QContactFilter::MatchExactly) {
                    *globalPresenceRequired = true;
                    for (int i  = 0; i < lengthOf(flags); ++i) {
                        QString comparison;
                        if (flags[i] == QContactStatusFlags::IsOnline) {
                            // Use special case test to include transient presence state
                            comparison = QStringLiteral("COALESCE(temp.GlobalPresenceStates.isOnline, Contacts.isOnline) = %1");
                        } else if (flags[i] == QContactStatusFlags::IsAdded) {
                            // Use special case test to check changeFlags for added status
                            comparison = QStringLiteral("(%1 & 1) = %2").arg(flagColumns[i]); // ChangeFlags::IsAdded
                        } else if (flags[i] == QContactStatusFlags::IsModified) {
                            // Use special case test to check changeFlags for modified status
                            comparison = QStringLiteral("((%1 & 2)/2) = %2").arg(flagColumns[i]); // ChangeFlags::IsModified
                        } else if (flags[i] == QContactStatusFlags::IsDeleted) {
                            // Use special case test to check changeFlags for deleted status
                            comparison = QStringLiteral("((%1 & 4)/4) = %2").arg(flagColumns[i]); // ChangeFlags::IsDeleted
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
                            } else if (flags[i] == QContactStatusFlags::IsAdded) {
                                // Use special case test to check changeFlags for added status
                                clauses.append(QStringLiteral("(%1 & 1) = 1").arg(flagColumns[i])); // ChangeFlags::IsAdded
                            } else if (flags[i] == QContactStatusFlags::IsModified) {
                                // Use special case test to check changeFlags for modified status
                                clauses.append(QStringLiteral("(%1 & 2) = 2").arg(flagColumns[i])); // ChangeFlags::IsModified
                            } else if (flags[i] == QContactStatusFlags::IsDeleted) {
                                // Use special case test to check changeFlags for deleted status
                                clauses.append(QStringLiteral("%1 >= 4").arg(flagColumns[i])); // ChangeFlags::IsDeleted
                            } else {
                                clauses.append(QStringLiteral("%1 = 1").arg(flagColumns[i]));
                            }
                        }
                    }
                } else {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unsupported flags matching contact status flags"));
                    break;
                }

                if (!clauses.isEmpty()) {
                    return detail.where(queryContacts).arg(clauses.join(QStringLiteral(" AND ")));
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

        QString clause(detail.where(queryContacts));
        QString comparison = QStringLiteral("%1");
        QString bindValue;
        QString column;

        if (caseInsensitive) {
            column = caseInsensitiveColumnName(detail.table, field.column);
            if (!column.isEmpty()) {
                // We don't need to use lower() on the values in this column
            } else {
                comparison = QStringLiteral("lower(%1)");
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
                    return QStringLiteral("FAILED");
                }
                if (caseInsensitive) {
                    bindValue = bindValue.toLower();
                }
                column = QStringLiteral("normalizedNumber");
            } else {
                // remove any non-digit characters from the column value when we do our comparison: +,-, ,#,(,) are removed.
                comparison = QStringLiteral("replace(replace(replace(replace(replace(replace(%1, '+', ''), '-', ''), '#', ''), '(', ''), ')', ''), ' ', '')");
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
                    clause = QStringLiteral("Contacts.contactId IN ("
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
                bindValue = bindValue + QStringLiteral("*");
                comparison += QStringLiteral(" GLOB ?");
                bindings->append(bindValue);
            } else if (globValue == QContactFilter::MatchContains) {
                bindValue = QStringLiteral("*") + bindValue + QStringLiteral("*");
                comparison += QStringLiteral(" GLOB ?");
                bindings->append(bindValue);
            } else if (globValue == QContactFilter::MatchEndsWith) {
                bindValue = QStringLiteral("*") + bindValue;
                comparison += QStringLiteral(" GLOB ?");
                bindings->append(bindValue);
            } else {
                if (bindValue.isEmpty()) {
                    // An empty string test should match a NULL column also (no way to specify isNull from qtcontacts)
                    comparison = QStringLiteral("COALESCE(%1,'') = ''").arg(comparison);
                } else {
                    comparison += QStringLiteral(" = ?");
                    bindings->append(bindValue);
                }
            }
        } else {
            if (phoneNumberMatch && !useNormalizedNumber) {
                bindValue = QStringLiteral("*") + bindValue;
                comparison += QStringLiteral(" GLOB ?");
                bindings->append(bindValue);
            } else {
                comparison += QStringLiteral(" = ?");
                bindings->append(bindValue);
            }
        }

        return clause.arg(comparison.arg(column.isEmpty() ? field.column : column));
    } while (false);

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to buildWhere with DetailFilter detail: %1 field: %2").arg(filter.detailType()).arg(filter.detailField()));
    return QStringLiteral("FALSE");
}

static QString buildWhere(const QContactDetailRangeFilter &filter, bool queryContacts, QVariantList *bindings, bool *failed)
{
    const DetailInfo &detail(detailInformation(filter.detailType()));
    if (detail.detailType == QContactDetail::TypeUndefined) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail type: %1").arg(filter.detailType()));
        return QStringLiteral("FAILED");
    }

    if (filter.detailField() == invalidField) {
        // If there is no field, we're simply testing for the existence of matching details
        return detail.whereExists(queryContacts);
    }

    const FieldInfo &field(fieldInformation(detail, filter.detailField()));
    if (field.field == invalidField) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown detail field: %1").arg(filter.detailField()));
        return QStringLiteral("FAILED");
    }

    if (!validFilterField(filter) || (!filter.minValue().isValid() && !filter.maxValue().isValid())) {
        // "match if detail exists, don't care about field or value" filter
        return detail.where(queryContacts).arg(QStringLiteral("%1 IS NOT NULL").arg(QLatin1String(field.column)));
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
                    ? QStringLiteral("%1 > lower(?)")
                    : QStringLiteral("%1 >= lower(?)");
        } else {
            comparison = (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeLower)
                    ? QStringLiteral("%1 > ?")
                    : QStringLiteral("%1 >= ?");
        }
        needsAnd = true;
    }

    if (filter.maxValue().isValid()) {
        if (needsAnd)
            comparison += QStringLiteral(" AND ");
        if (dateField) {
            bindings->append(dateString(detail, filter.maxValue().toDateTime()));
        } else {
            bindings->append(filter.maxValue());
        }
        if (caseInsensitive) {
            comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                    ? QStringLiteral("%1 <= lower(?)")
                    : QStringLiteral("%1 < lower(?)");
        } else {
            comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                    ? QStringLiteral("%1 <= ?")
                    : QStringLiteral("%1 < ?");
        }
    }

    QString comparisonArg = field.column;
    if (caseInsensitive) {
        comparisonArg = caseInsensitiveColumnName(detail.table, field.column);
        if (!comparisonArg.isEmpty()) {
            // We don't need to use lower() on the values in this column
        } else {
            comparisonArg = QStringLiteral("lower(%1)").arg(QLatin1String(field.column));
        }
    }
    return detail.where(queryContacts).arg(comparison.arg(comparisonArg));
}

static QString buildWhere(const QContactIdFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings, bool *failed)
{
    const QList<QContactId> &filterIds(filter.ids());
    if (filterIds.isEmpty()) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with empty contact ID list"));
        return QStringLiteral("FALSE");
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
            return QStringLiteral("FALSE");
        }

        return QStringLiteral("Contacts.contactId IN (SELECT contactId FROM %1)").arg(transientTable);
    }

    QString statement = QStringLiteral("Contacts.contactId IN (?");
    bindings->append(dbIds.first());

    for (int i = 1; i < dbIds.count(); ++i) {
        statement += QStringLiteral(",?");
        bindings->append(dbIds.at(i));
    }
    return statement + QStringLiteral(")");
}

static QString buildWhere(const QContactRelationshipFilter &filter, QVariantList *bindings, bool *failed)
{
    QContactId rci = filter.relatedContactId();

    QContactRelationship::Role rcr = filter.relatedContactRole();
    QString rt = filter.relationshipType();

    quint32 dbId = ContactId::databaseId(rci);

    if (!rci.managerUri().isEmpty() && !rci.managerUri().startsWith(QStringLiteral("qtcontacts:org.nemomobile.contacts.sqlite"))) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with invalid manager URI: %1").arg(rci.managerUri()));
        return QStringLiteral("FALSE");
    }

    bool needsId = dbId != 0;
    bool needsType = !rt.isEmpty();
    QString statement = QStringLiteral("Contacts.contactId IN (\n");
    if (!needsId && !needsType) {
        // return the id of every contact who is in a relationship
        if (rcr == QContactRelationship::First) { // where the other contact is the First
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships");
            statement += QStringLiteral(" WHERE firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
        } else if (rcr == QContactRelationship::Second) { // where the other contact is the Second
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships");
            statement += QStringLiteral(" WHERE firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
        } else { // where the other contact is either First or Second
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships");
            statement += QStringLiteral(" WHERE firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" UNION ");
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships");
            statement += QStringLiteral(" WHERE firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
        }
    } else if (!needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type
        if (rcr == QContactRelationship::First) { // where the other contact is the First
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships WHERE type = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the other contact is the Second
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships WHERE type = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(rt);
        } else { // where the other contact is either First or Second
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships WHERE type = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" UNION ");
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships WHERE type = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(rt);
            bindings->append(rt);
        }
    } else if (needsId && !needsType) {
        // return the id of every contact who is in a relationship with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(dbId);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships WHERE secondId = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(dbId);
        } else { // where the specified contact is either First or Second
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" UNION ");
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships WHERE secondId = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(dbId);
            bindings->append(dbId);
        }
    } else if (needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(dbId);
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
            bindings->append(dbId);
            bindings->append(rt);
        } else { // where the specified contact is either First or Second
            statement += QStringLiteral(" SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?");
            statement += QStringLiteral(" AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(" UNION ");
            statement += QStringLiteral(" SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ?");
            statement += QStringLiteral(" AND firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)");
            statement += QStringLiteral(")");
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
    static const QString statement(QStringLiteral("%1 >= ?"));
    bindings->append(ContactsDatabase::dateTimeString(filter.since().toUTC()));
    switch (filter.eventType()) {
        case QContactChangeLogFilter::EventAdded:
            return statement.arg(QStringLiteral("Contacts.created"));
        case QContactChangeLogFilter::EventChanged:
            *transientModifiedRequired = true;
            return statement.arg(QStringLiteral("COALESCE(temp.Timestamps.modified, Contacts.modified)"));
        default: break;
    }

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with changelog filter on removed timestamps"));
    return QStringLiteral("FALSE");
}

typedef QString (*BuildFilterPart)(
        const QContactFilter &filter,
        ContactsDatabase &db,
        const QString &table,
        QContactDetail::DetailType detailType,
        QVariantList *bindings,
        bool *failed,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired);

static QString buildWhere(
        BuildFilterPart buildWhere,
        const QContactUnionFilter &filter,
        ContactsDatabase &db,
        const QString &table,
        QContactDetail::DetailType detailType,
        QVariantList *bindings,
        bool *failed,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, db, table, detailType, bindings, failed, transientModifiedRequired, globalPresenceRequired);
        if (!*failed && !fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    return QStringLiteral("( %1 )").arg(fragments.join(QStringLiteral(" OR ")));
}

static QString buildWhere(
        BuildFilterPart buildWhere,
        const QContactIntersectionFilter &filter,
        ContactsDatabase &db,
        const QString &table,
        QContactDetail::DetailType detailType,
        QVariantList *bindings,
        bool *failed,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, db, table, detailType, bindings, failed, transientModifiedRequired, globalPresenceRequired);
        if (filter.type() != QContactFilter::DefaultFilter && !*failed) {
            // default filter gets special (permissive) treatment by the intersection filter.
            fragments.append(fragment.isEmpty() ? QStringLiteral("NULL") : fragment);
        }
    }

    return fragments.join(QStringLiteral(" AND "));
}

static QString buildContactWhere(const QContactFilter &filter, ContactsDatabase &db, const QString &table, QContactDetail::DetailType detailType, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    Q_ASSERT(failed);
    Q_ASSERT(globalPresenceRequired);
    Q_ASSERT(transientModifiedRequired);

    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
        return QString();
    case QContactFilter::ContactDetailFilter:
        return buildWhere(static_cast<const QContactDetailFilter &>(filter), true, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::ContactDetailRangeFilter:
        return buildWhere(static_cast<const QContactDetailRangeFilter &>(filter), true, bindings, failed);
    case QContactFilter::ChangeLogFilter:
        return buildWhere(static_cast<const QContactChangeLogFilter &>(filter), bindings, failed, transientModifiedRequired);
    case QContactFilter::RelationshipFilter:
        return buildWhere(static_cast<const QContactRelationshipFilter &>(filter), bindings, failed);
    case QContactFilter::IntersectionFilter:
        return buildWhere(buildContactWhere, static_cast<const QContactIntersectionFilter &>(filter), db, table, detailType, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::UnionFilter:
        return buildWhere(buildContactWhere, static_cast<const QContactUnionFilter &>(filter), db, table, detailType, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::IdFilter:
        return buildWhere(static_cast<const QContactIdFilter &>(filter), db, table, bindings, failed);
    case QContactFilter::CollectionFilter:
        return buildWhere(static_cast<const QContactCollectionFilter &>(filter), bindings, failed);
    default:
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown filter type: %1").arg(filter.type()));
        return QStringLiteral("FALSE");
    }
}

static QString buildDetailWhere(
        const QContactFilter &filter,
        ContactsDatabase &db,
        const QString &table,
        QContactDetail::DetailType detailType,
        QVariantList *bindings,
        bool *failed,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired)
{
    Q_ASSERT(failed);
    Q_ASSERT(globalPresenceRequired);
    Q_ASSERT(transientModifiedRequired);

    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
        return QString();
    case QContactFilter::ContactDetailFilter: {
        const QContactDetailFilter &detailFilter = static_cast<const QContactDetailFilter &>(filter);

        if (detailFilter.detailType() == detailType) {
            return buildWhere(
                        detailFilter,
                        false,
                        bindings,
                        failed,
                        transientModifiedRequired,
                        globalPresenceRequired);
        } else {
            *failed = true;
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot build detail query with mismatched details type: %1 %2").arg(detailType).arg(detailFilter.detailType()));
            return QStringLiteral("FALSE");
        }
    }
    case QContactFilter::ContactDetailRangeFilter: {
        const QContactDetailRangeFilter &detailFilter = static_cast<const QContactDetailRangeFilter &>(filter);

        if (detailFilter.detailType() == detailType) {
            return buildWhere(detailFilter, false, bindings, failed);
        } else {
            *failed = true;
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot build detail query with mismatched details type: %1 != %2").arg(detailType).arg(detailFilter.detailType()));
            return QStringLiteral("FALSE");
        }
    }
    case QContactFilter::IntersectionFilter:
        return buildWhere(
                    buildDetailWhere,
                    static_cast<const QContactIntersectionFilter &>(filter),
                    db,
                    table,
                    detailType,
                    bindings,
                    failed,
                    transientModifiedRequired,
                    globalPresenceRequired);
    case QContactFilter::UnionFilter:
        return buildWhere(
                    buildDetailWhere,
                    static_cast<const QContactUnionFilter &>(filter),
                    db,
                    table,
                    detailType,
                    bindings,
                    failed,
                    transientModifiedRequired,
                    globalPresenceRequired);
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
    case QContactFilter::IdFilter:
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot build a detail query with a non-detail filter type: %1").arg(filter.type()));
        return QStringLiteral("FALSE");
    default:
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown filter type: %1").arg(filter.type()));
        return QStringLiteral("FALSE");
    }
}


static QString buildOrderBy(
        const QContactSortOrder &order,
        QContactDetail::DetailType detailType,
        QStringList *joins,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired,
        bool useLocale )
{
    Q_ASSERT(joins);
    Q_ASSERT(transientModifiedRequired);
    Q_ASSERT(globalPresenceRequired);

    const DetailInfo &detail(detailInformation(order.detailType()));
    if (detail.detailType == QContactDetail::TypeUndefined) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildOrderBy with unknown detail type: %1").arg(order.detailType()));
        return QString();
    } else if (detailType != QContactDetail::TypeUndefined && detail.detailType != detailType) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildOrderBy with unknown detail mismatched detail types: %1 != %2").arg(detailType).arg(order.detailType()));
        return QString();
    }

    if (order.detailField() == invalidField) {
        // If there is no field, we're simply sorting by the existence or otherwise of the detail
        return detail.orderByExistence(order.direction() == Qt::AscendingOrder);
    }

    const bool joinToSort = detail.joinToSort && detailType == QContactDetail::TypeUndefined;

    const FieldInfo &field(fieldInformation(detail, order.detailField()));
    if (field.field == invalidField) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildOrderBy with unknown detail field: %1").arg(order.detailField()));
        return QString();
    }

    const bool isDisplayLabelGroup = detail.detailType == QContactDisplayLabel::Type && field.field == QContactDisplayLabel__FieldLabelGroup;
    QString sortExpression(joinToSort
            ? QStringLiteral("%1.%2")
                .arg(detail.table)
                .arg(isDisplayLabelGroup ? QStringLiteral("DisplayLabelGroupSortOrder") : field.column)
            : QStringLiteral("%1")
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
                ? QStringLiteral("CASE WHEN COALESCE(%1, '') = '' THEN 1 ELSE 0 END, ")
                : QStringLiteral("CASE WHEN COALESCE(%1, '') = '' THEN 0 ELSE 1 END, ");
        result = blanksLocation.arg(sortExpression);
    }

    result.append(sortExpression);

    if (!isDisplayLabelGroup && collate) {
        if (localized && useLocale) {
            result.append(QStringLiteral(" COLLATE localeCollation"));
        } else {
            result.append((order.caseSensitivity() == Qt::CaseSensitive) ? QStringLiteral(" COLLATE RTRIM") : QStringLiteral(" COLLATE NOCASE"));
        }
    }

    result.append((order.direction() == Qt::AscendingOrder) ? QStringLiteral(" ASC") : QStringLiteral(" DESC"));

    if (joinToSort ) {
        QString join = QStringLiteral(
                "LEFT JOIN %1 ON Contacts.contactId = %1.contactId")
                .arg(QLatin1String(detail.table));

        if (!joins->contains(join))
            joins->append(join);

        return result;
    } else if (!detail.table || detailType != QContactDetail::TypeUndefined) {
        return result;
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("UNSUPPORTED SORTING: no join and not primary table for ORDER BY in query with: %1, %2")
                   .arg(order.detailType()).arg(order.detailField()));
    }

    return QString();
}

static QString buildOrderBy(
        const QList<QContactSortOrder> &order,
        QString *join,
        bool *transientModifiedRequired,
        bool *globalPresenceRequired,
        bool useLocale,
        QContactDetail::DetailType detailType = QContactDetail::TypeUndefined,
        const QString &finalOrder = QStringLiteral("Contacts.contactId"))
{
    Q_ASSERT(join);
    Q_ASSERT(transientModifiedRequired);
    Q_ASSERT(globalPresenceRequired);

    if (order.isEmpty())
        return QString();

    QStringList joins;
    QStringList fragments;
    foreach (const QContactSortOrder &sort, order) {
        const QString fragment = buildOrderBy(
                    sort, detailType, &joins, transientModifiedRequired, globalPresenceRequired, useLocale);
        if (!fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    *join = joins.join(QStringLiteral(" "));

    if (!finalOrder.isEmpty())
        fragments.append(finalOrder);
    return fragments.join(QStringLiteral(", "));
}

static void debugFilterExpansion(const QString &description, const QString &query, const QVariantList &bindings)
{
    static const bool debugFilters = !qgetenv("QTCONTACTS_SQLITE_DEBUG_FILTERS").isEmpty();

    if (debugFilters) {
        qDebug() << description << ContactsDatabase::expandQuery(query, bindings);
    }
}

ContactReader::ContactReader(ContactsDatabase &database, const QString &managerUri)
    : m_database(database), m_managerUri(managerUri)
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
    case QContactFilter::CollectionFilter:
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

bool includesCollectionFilter(const QContactFilter &filter);

// Returns true if this filter includes a filter for specific collection
bool includesCollectionFilter(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesCollectionFilter(filter)) {
            return true;
        }
    }
    return false;
}
bool includesCollectionFilter(const QContactIntersectionFilter &filter)
{
    return includesCollectionFilter(filter.filters());
}
bool includesCollectionFilter(const QContactUnionFilter &filter)
{
    return includesCollectionFilter(filter.filters());
}
bool includesCollectionFilter(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::CollectionFilter:
        return true;

    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
    case QContactFilter::IdFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesCollectionFilter(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesCollectionFilter(static_cast<const QContactUnionFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesCollectionFilter with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesDeleted(const QContactFilter &filter);

// Returns true if this filter includes deleted contacts
bool includesDeleted(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesDeleted(filter)) {
            return true;
        }
    }
    return false;
}
bool includesDeleted(const QContactIntersectionFilter &filter)
{
    return includesDeleted(filter.filters());
}
bool includesDeleted(const QContactUnionFilter &filter)
{
    return includesDeleted(filter.filters());
}
bool includesDeleted(const QContactDetailFilter &filter)
{
    if (filterOnField<QContactStatusFlags>(filter, QContactStatusFlags::FieldFlags)) {
        quint64 flagsValue = filter.value().value<quint64>();
        if (flagsValue & QContactStatusFlags::IsDeleted) {
            return true;
        }
    }
    return false;
}
bool includesDeleted(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::IdFilter:
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
    case QContactFilter::CollectionFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesDeleted(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesDeleted(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::ContactDetailFilter:
        return includesDeleted(static_cast<const QContactDetailFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesDeleted with unknown filter type %1").arg(filter.type()));
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
    case QContactFilter::CollectionFilter:
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
    case QContactFilter::CollectionFilter:
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
    // intersected with a syncTarget detail filter or a collection filter
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
            // exclude non-aggregates, unless the filter specifies collections
            if (!includesCollectionFilter(filter)) {
                constraints.append("Contacts.collectionId = 1 "); // AggregateAddressbookCollectionId
            }
        }

        // exclude deactivated unless they're explicitly included
        if (!includesDeactivated(filter)) {
            constraints.append("Contacts.isDeactivated = 0 ");
        }

        // exclude deleted unless they're explicitly included
        if (!includesDeleted(filter)) {
            constraints.append("Contacts.changeFlags < 4 ");
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

    QString whereClause(QStringLiteral("WHERE "));
    if (!constraints.isEmpty()) {
        whereClause += constraints.join(QStringLiteral("AND "));
        if (!emptyFilter) {
            whereClause += QStringLiteral("AND ");
        }
    }
    if (!emptyFilter) {
        whereClause += where;
    }

    return whereClause;
}

}

QContactManager::Error ContactReader::fetchContacts(const QContactCollectionId &collectionId,
                                                    QList<QContact> *addedContacts,
                                                    QList<QContact> *modifiedContacts,
                                                    QList<QContact> *deletedContacts,
                                                    QList<QContact> *unmodifiedContacts)
{
    QContactCollectionFilter collectionFilter;
    collectionFilter.setCollectionId(collectionId);

    const QContactFilter addedContactsFilter = collectionFilter & QContactStatusFlags::matchFlag(QContactStatusFlags::IsAdded, QContactFilter::MatchContains);
    const QContactFilter modifiedContactsFilter = collectionFilter & QContactStatusFlags::matchFlag(QContactStatusFlags::IsModified, QContactFilter::MatchContains);
    const QContactFilter deletedContactsFilter = collectionFilter & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains);

    // optimisation: if the caller doesn't care about unmodified contacts,
    // we can save some memory by only fetching added/modified/deleted contacts.
    const QContactFilter filter = unmodifiedContacts
                                ? (collectionFilter
                                  |deletedContactsFilter)
                                : (addedContactsFilter
                                  |modifiedContactsFilter
                                  |deletedContactsFilter);

    const bool keepChangeFlags = true;

    QList<QContact> allContacts;
    const QContactManager::Error error = readContacts(
            QStringLiteral("FetchContacts"),
            &allContacts,
            filter,
            QList<QContactSortOrder>(),
            QContactFetchHint(),
            keepChangeFlags);

    for (QList<QContact>::const_iterator it = allContacts.constBegin(); it != allContacts.constEnd(); it++) {
        const QContactStatusFlags flags = it->detail<QContactStatusFlags>();
        if (flags.testFlag(QContactStatusFlags::IsDeleted)) {
            if (deletedContacts) {
                deletedContacts->append(*it);
            }
        } else if (flags.testFlag(QContactStatusFlags::IsAdded)) {
            if (addedContacts) {
                addedContacts->append(*it);
            }
        } else if (flags.testFlag(QContactStatusFlags::IsModified)) {
            if (modifiedContacts) {
                modifiedContacts->append(*it);
            }
        } else {
            Q_ASSERT(unmodifiedContacts);
            if (unmodifiedContacts) {
                unmodifiedContacts->append(*it);
            }
        }
    }

    return error;
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order,
        const QContactFetchHint &fetchHint,
        bool keepChangeFlags)
{
    QMutexLocker locker(m_database.accessMutex());

    m_database.clearTemporaryContactIdsTable(table);

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(order, &join, &transientModifiedRequired, &globalPresenceRequired, m_database.localized());

    bool whereFailed = false;
    QVariantList bindings;
    QString where = buildContactWhere(filter, m_database, table, QContactDetail::TypeUndefined, &bindings, &whereFailed, &transientModifiedRequired, &globalPresenceRequired);
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
        error = queryContacts(table, contacts, fetchHint,
                              false /* relax constraints */,
                              false /* ignore deleted - however they will be omitted unless filter specifically requires */,
                              keepChangeFlags);
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
        error = queryContacts(table, contacts, fetchHint, relaxConstraints, true /* ignore deleted */);
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
        const QString &tableName,
        QList<QContact> *contacts,
        const QContactFetchHint &fetchHint,
        bool relaxConstraints,
        bool ignoreDeleted,
        bool keepChangeFlags)
{
    QContactManager::Error err = QContactManager::NoError;

    const QString dataQueryStatement(QStringLiteral(
        "SELECT " // order and content can change due to schema upgrades, so list manually.
            "Contacts.contactId, "
            "Contacts.collectionId, "
            "Contacts.created, "
            "Contacts.modified, "
            "Contacts.deleted, "
            "Contacts.hasPhoneNumber, "
            "Contacts.hasEmailAddress, "
            "Contacts.hasOnlineAccount, "
            "Contacts.isOnline, "
            "Contacts.isDeactivated, "
            "Contacts.changeFlags "
        "FROM temp.%1 "
        "CROSS JOIN Contacts ON temp.%1.contactId = Contacts.contactId " // Cross join ensures we scan the temp table first
        "%2 "
        "ORDER BY temp.%1.rowId ASC").arg(tableName)
                                     .arg(ignoreDeleted ? QStringLiteral("WHERE Contacts.changeFlags < 4") // ChangeFlags::IsDeleted
                                                        : QString()));

    const QString relationshipQueryStatement(QStringLiteral(
        "SELECT "
            "temp.%1.contactId AS contactId,"
            "R1.type AS secondType,"
            "R1.firstId AS firstId,"
            "R2.type AS firstType,"
            "R2.secondId AS secondId "
        "FROM temp.%1 "
         // Must join in this order to get correct query plan.
         // We also filter based on ChangeFlags::IsDeleted here.
         // TODO: if this performs poorly, instead do a separate SELECT query to get deleted contacts,
         // and manually filter out the results in-memory when adding the relationships to the contact,
         // in the queryContacts(..., relationshipQuery, ...) method.
        "LEFT JOIN Relationships AS R1 ON R1.secondId = temp.%1.contactId AND R1.firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4) "
        "LEFT JOIN Relationships AS R2 ON R2.firstId = temp.%1.contactId AND R2.secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4) "
        "ORDER BY contactId ASC").arg(tableName));

    QSqlQuery contactQuery(m_database);
    QSqlQuery relationshipQuery(m_database);

    // Prepare the query for the contact properties
    if (!contactQuery.prepare(dataQueryStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for contact data:\n%1\nQuery:\n%2")
                .arg(contactQuery.lastError().text())
                .arg(dataQueryStatement));
        err = QContactManager::UnspecifiedError;
    } else {
        contactQuery.setForwardOnly(true);
        if (!ContactsDatabase::execute(contactQuery)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to execute query for contact data:\n%1\nQuery:\n%2")
                    .arg(contactQuery.lastError().text())
                    .arg(dataQueryStatement));
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
                err = queryContacts(tableName, contacts, fetchHint, relaxConstraints, keepChangeFlags, contactQuery, relationshipQuery);
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
        const QString &tableName,
        QList<QContact> *contacts,
        const QContactFetchHint &fetchHint,
        bool relaxConstraints,
        bool keepChangeFlags,
        QSqlQuery &contactQuery,
        QSqlQuery &relationshipQuery)
{
    // Formulate the query to fetch the contact details
    const QString detailQueryTemplate(QStringLiteral(
        "SELECT "
            "Details.detailId,"
            "Details.contactId,"
            "Details.detail,"
            "Details.detailUri,"
            "Details.linkedDetailUris,"
            "Details.contexts,"
            "Details.accessConstraints,"
            "Details.provenance,"
            "Details.modifiable,"
            "COALESCE(Details.nonexportable, 0),"
            "Details.changeFlags, "
            "%1 "
        "FROM temp.%2 "
        "CROSS JOIN Details ON Details.contactId = temp.%2.contactId " // Cross join ensures we scan the temp table first
        "%3 "
        "%4 "
        "ORDER BY temp.%2.rowId ASC"));

    const QString selectTemplate(QStringLiteral(
        "%1.*"));
    const QString joinTemplate(QStringLiteral(
        "LEFT JOIN %1 ON %1.detailId = Details.detailId"));
    const QString detailNameTemplate(QStringLiteral(
        "WHERE Details.detail IN ('%1')"));

    QStringList selectSpec;
    QStringList joinSpec;
    QStringList detailNameSpec;

    QHash<QString, QPair<ReadDetail, int> > readProperties;

    // Skip the Details table fields, and the indexing fields of the first join table
    int offset = 11 + 2;

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
        detailQueryStatement = detailQueryStatement.arg(detailNameTemplate.arg(detailNameSpec.join(QStringLiteral("','"))));

    // If selectSpec is empty, all required details are in the Contacts table
    ContactsDatabase::Query detailQuery(m_database.prepare(detailQueryStatement));
    if (!selectSpec.isEmpty()) {
        // Read the details for these contacts
        detailQuery.setForwardOnly(true);
        if (!ContactsDatabase::execute(detailQuery)) {
            detailQuery.reportError(QStringLiteral("Failed to prepare query for joined details"));
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
        const quint32 dbId = contactQuery.value(col++).toUInt();
        const quint32 collectionId = contactQuery.value(col++).toUInt();
        const QContactCollectionId apiCollectionId = ContactCollectionId::apiId(collectionId, m_managerUri);
        const bool aggregateContact = collectionId == ContactsDatabase::AggregateAddressbookCollectionId;

        QContact contact;
        contact.setId(ContactId::apiId(dbId, m_managerUri));
        contact.setCollectionId(apiCollectionId);

        QContactTimestamp timestamp;
        setValue(&timestamp, QContactTimestamp::FieldCreationTimestamp    , ContactsDatabase::fromDateTimeString(contactQuery.value(col++).toString()));
        setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, ContactsDatabase::fromDateTimeString(contactQuery.value(col++).toString()));
        col++; // ignore Deleted timestamp.

        QContactStatusFlags flags;
        flags.setFlag(QContactStatusFlags::HasPhoneNumber, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::HasEmailAddress, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::HasOnlineAccount, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::IsOnline, contactQuery.value(col++).toBool());
        flags.setFlag(QContactStatusFlags::IsDeactivated, contactQuery.value(col++).toBool());
        const int changeFlags = contactQuery.value(col++).toInt();
        flags.setFlag(QContactStatusFlags::IsAdded, changeFlags & ContactsDatabase::IsAdded);
        flags.setFlag(QContactStatusFlags::IsModified, changeFlags & ContactsDatabase::IsModified);
        flags.setFlag(QContactStatusFlags::IsDeleted, changeFlags >= ContactsDatabase::IsDeleted);

        if (flags.testFlag(QContactStatusFlags::IsDeactivated)) {
            QContactDeactivated deactivated;
            setDetailImmutableIfAggregate(aggregateContact, &deactivated);
            contact.saveDetail(&deactivated);
        }

        int contactType = contactQuery.value(col++).toInt();
        QContactType typeDetail = contact.detail<QContactType>();
        typeDetail.setType(static_cast<QContactType::TypeValues>(contactType));
        setDetailImmutableIfAggregate(aggregateContact, &typeDetail);
        contact.saveDetail(&typeDetail);

        bool syncable = collectionId != ContactsDatabase::AggregateAddressbookCollectionId
                && collectionId != ContactsDatabase::LocalAddressbookCollectionId;

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

                    setDetailImmutableIfAggregate(aggregateContact, &detail);
                    contact.saveDetail(&detail);
                    transientTypes.insert(transientType);
                }
            }
        }

        // Add the updated status flags
        QContactManagerEngine::setDetailAccessConstraints(&flags, QContactDetail::ReadOnly | QContactDetail::Irremovable);
        setDetailImmutableIfAggregate(aggregateContact, &flags);
        contact.saveDetail(&flags);

        // Add the timestamp info
        if (!timestamp.isEmpty()) {
            setDetailImmutableIfAggregate(aggregateContact, &timestamp);
            contact.saveDetail(&timestamp);
        }

        // Add the details of this contact from the detail tables
        if (includeDetails) {
            if (detailQuery.isValid()) {
                quint32 firstContactDetailId = 0;
                do {
                    const quint32 contactId = detailQuery.value(1).toUInt();
                    if (contactId != dbId) {
                        break;
                    }

                    const quint32 detailId = detailQuery.value(0).toUInt();
                    if (firstContactDetailId == 0) {
                        firstContactDetailId = detailId;
                    } else if (firstContactDetailId == detailId) {
                        // the client must have requested the same contact twice in a row, by id.
                        // we have already processed all of this contact's details, so break.
                        break;
                    }

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

                        // Extract the values from the result row (readDetail()).
                        properties.first(&contact, detailQuery, contactId, detailId, syncable,
                                         apiCollectionId, relaxConstraints, keepChangeFlags,
                                         properties.second);
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
                        QContactRelationship rel(makeRelationship(firstType, contactId, secondId, m_managerUri));
                        relationships.append(rel);
                    } else if (!secondType.isEmpty()) {
                        QContactRelationship rel(makeRelationship(secondType, firstId, contactId, m_managerUri));
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
    QList<QContactCollectionId> collectionIds;

    // The only queries we support regarding deleted contacts are for the IDs, possibly
    // intersected with a syncTarget detail filter or a collection filter
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
            } else if (filterType == QContactFilter::CollectionFilter) {
                const QContactCollectionFilter &collectionFilter(static_cast<const QContactCollectionFilter &>(partialFilter));
                collectionIds = collectionFilter.collectionIds().toList();
                if (collectionIds.size() > 1) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot readDeletedContactIds with more than one collection specified: %1").arg(collectionIds.size()));
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
    restrictions.append(QStringLiteral("changeFlags >= 4"));
    if (!since.isNull()) {
        restrictions.append(QStringLiteral("deleted >= ?"));
        bindings.append(ContactsDatabase::dateTimeString(since.toUTC()));
    }
    if (!syncTarget.isNull()) {
        restrictions.append(QStringLiteral("syncTarget = ?"));
        bindings.append(syncTarget);
    }
    if (!collectionIds.isEmpty()) {
        restrictions.append(QStringLiteral("collectionId = ?"));
        bindings.append(ContactCollectionId::databaseId(collectionIds.first()));
    }

    QString queryStatement(QStringLiteral("SELECT contactId FROM Contacts"));
    if (!restrictions.isEmpty()) {
        queryStatement.append(QStringLiteral(" WHERE "));
        queryStatement.append(restrictions.takeFirst());
        while (!restrictions.isEmpty()) {
            queryStatement.append(QStringLiteral(" AND "));
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
            contactIds->append(ContactId::apiId(query.value(0).toUInt(), m_managerUri));
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
    const QString tableName(QStringLiteral("readContactIds"));

    m_database.clearTransientContactIdsTable(tableName);

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(order, &join, &transientModifiedRequired, &globalPresenceRequired, m_database.localized());

    bool failed = false;
    QVariantList bindings;
    QString where = buildContactWhere(filter, m_database, tableName, QContactDetail::TypeUndefined, &bindings, &failed, &transientModifiedRequired, &globalPresenceRequired);
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

    QString queryString = QStringLiteral(
                "\n SELECT DISTINCT Contacts.contactId"
                "\n FROM Contacts %1"
                "\n %2").arg(join).arg(where);
    if (!orderBy.isEmpty()) {
        queryString.append(QStringLiteral(" ORDER BY ") + orderBy);
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
            contactIds->append(ContactId::apiId(query.value(0).toUInt(), m_managerUri));
        }
        contactIdsAvailable(*contactIds);
    } while (query.isValid());

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::getIdentity(
        ContactsDatabase::Identity identity, QContactId *contactId)
{
    QMutexLocker locker(m_database.accessMutex());

    if (identity == ContactsDatabase::InvalidContactId) {
        return QContactManager::BadArgumentError;
    } else if (identity == ContactsDatabase::SelfContactId) {
        // we don't allow setting the self contact id, it's always static
        *contactId = ContactId::apiId(selfId, m_managerUri);
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
            *contactId = ContactId::apiId(query.value<quint32>(0), m_managerUri);
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
        whereStatements.append(QStringLiteral("type = ?"));
        bindings.append(type);
    }

    quint32 firstId = ContactId::databaseId(first);
    if (firstId != 0) {
        whereStatements.append(QStringLiteral("firstId = ?"));
        bindings.append(firstId);
    }

    quint32 secondId = ContactId::databaseId(second);
    if (secondId != 0) {
        whereStatements.append(QStringLiteral("secondId = ?"));
        bindings.append(secondId);
    }

    const QString whereParticipantNotDeleted = QStringLiteral(
            "\n WHERE firstId NOT IN ("
                "\n SELECT contactId FROM Contacts WHERE changeFlags >= 4)"   // ChangeFlags::IsDeleted
            "\n AND secondId NOT IN ("
                "\n SELECT contactId FROM Contacts WHERE changeFlags >= 4)"); // ChangeFlags::IsDeleted

    const QString where = whereParticipantNotDeleted + (!whereStatements.isEmpty()
                        ? (QStringLiteral(" AND ") + whereStatements.join(QStringLiteral(" AND ")))
                        : QString());

    QString statement = QStringLiteral(
            "\n SELECT type, firstId, secondId"
            "\n FROM Relationships") + where + QStringLiteral(";");

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

        relationships->append(makeRelationship(type, firstId, secondId, m_managerUri));
    }
    query.finish();

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readDetails(
        QList<QContactDetail> *details,
        QContactDetail::DetailType type,
        QList<int> fields,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order,
        const QContactFetchHint &fetchHint)
{

    const DetailInfo &info = detailInformation(type);
    if (info.detailType == QContactDetail::TypeUndefined) {
        return QContactManager::UnspecifiedError;
    } else if (!info.appendUnique) {
        return QContactManager::UnspecifiedError;
    }

    QMutexLocker locker(m_database.accessMutex());

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(
                order,
                &join,
                &transientModifiedRequired,
                &globalPresenceRequired,
                m_database.localized(),
                type,
                QString());

    bool whereFailed = false;
    QVariantList bindings;
    QString where = buildDetailWhere(
                filter,
                m_database,
                QLatin1String(info.table),
                type,
                &bindings,
                &whereFailed,
                &transientModifiedRequired,
                &globalPresenceRequired);
    if (whereFailed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    const int maximumCount = fetchHint.maxCountHint();

    QStringList fieldNames;
    for (int i = 0; i < info.fieldCount; ++i) {
        if (fields.isEmpty() || fields.contains(info.fields[i].field)) {
            fieldNames.append(QLatin1String(info.fields[i].column));
        } else {
            // Instead of making every column read for a detail optional for the columns we're not
            // interested in we'll insert a null value.
            fieldNames.append(QStringLiteral("NULL"));
        }
    }

    const QString statement = QStringLiteral(
                    "SELECT %1, MAX(detailId) AS maxId"
                    " FROM %2"
                    "%3"        // WHERE
                    " GROUP BY %1"
                    "%4"        // ORDER BY
                    "%5").arg(  // LIMIT
                fieldNames.join(QStringLiteral(", ")),
                QLatin1String(info.table),
                !where.isEmpty() ? QStringLiteral(" WHERE ") + where : QString(),
                !orderBy.isEmpty() ? QStringLiteral(" ORDER BY ") + orderBy : QStringLiteral(" ORDER BY maxId DESC"), // If there's no sort order prioritize the most recent entries.
                maximumCount > 0 ? QStringLiteral(" LIMIT %1").arg(maximumCount): QString());

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare a unique details query: %1\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i) {
        query.bindValue(i, bindings.at(i));
    }

    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query unique details\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return QContactManager::UnspecifiedError;
    }


    while (query.next()) {
        info.appendUnique(details, query);
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::getCollectionIdentity(
        ContactsDatabase::CollectionIdentity identity,
        QContactCollectionId *collectionId)
{
    switch (identity) {
        case ContactsDatabase::AggregateAddressbookCollectionId: // fall through
        case ContactsDatabase::LocalAddressbookCollectionId:
            *collectionId = ContactCollectionId::apiId(static_cast<quint32>(identity), m_managerUri);
            break;
        default: return QContactManager::BadArgumentError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readCollections(
        const QString &table,
        QList<QContactCollection> *collections)
{
    Q_UNUSED(table);
    QList<QContactCollection> cols;
    QContactManager::Error err = fetchCollections(0, QString(), &cols, &cols, nullptr, &cols);
    if (err == QContactManager::NoError) {
        *collections = cols;
        collectionsAvailable(cols);
    }
    return err;
}

QContactManager::Error ContactReader::fetchCollections(
        int accountId,
        const QString &applicationName,
        QList<QContactCollection> *addedCollections,
        QList<QContactCollection> *modifiedCollections,
        QList<QContactCollection> *deletedCollections,
        QList<QContactCollection> *unmodifiedCollections)
{
    const QString where = accountId > 0
            ? (!applicationName.isEmpty()
                ? QStringLiteral("WHERE accountId = :accountId AND applicationName = :applicationName")
                : QStringLiteral("WHERE accountId = :accountId"))
            : (!applicationName.isEmpty()
                ? QStringLiteral("WHERE applicationName = :applicationName")
                : QString());

    const QString collectionsQueryStatement = QStringLiteral(
                  "SELECT " // order and content can change due to schema upgrades, so list manually.
                    "collectionId, "
                    "aggregable, "
                    "name, "
                    "description, "
                    "color, "
                    "secondaryColor, "
                    "image, "
                    "applicationName, "
                    "accountId, "
                    "remotePath, "
                    "changeFlags "
                  "FROM Collections "
                  "%1 "
                  "ORDER BY collectionId ASC").arg(where);

    QSqlQuery collectionsQuery(m_database);
    if (!collectionsQuery.prepare(collectionsQueryStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for collection details:\n%1\nQuery:\n%2")
                .arg(collectionsQuery.lastError().text())
                .arg(collectionsQueryStatement));
        return QContactManager::UnspecifiedError;
    }

    if (accountId > 0) {
        collectionsQuery.bindValue(":accountId", accountId);
    }
    if (!applicationName.isEmpty()) {
        collectionsQuery.bindValue(":applicationName", applicationName);
    }

    collectionsQuery.setForwardOnly(true);
    if (!ContactsDatabase::execute(collectionsQuery)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to execute query for collection details:\n%1\nQuery:\n%2")
                .arg(collectionsQuery.lastError().text())
                .arg(collectionsQueryStatement));
        return QContactManager::UnspecifiedError;
    }

    while (collectionsQuery.next()) {
        int col = 0;
        const quint32 dbId = collectionsQuery.value(col++).toUInt();

        QContactCollection collection;
        collection.setId(ContactCollectionId::apiId(dbId, m_managerUri));

        collection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, collectionsQuery.value(col++).toBool());
        collection.setMetaData(QContactCollection::KeyName, collectionsQuery.value(col++).toString());
        collection.setMetaData(QContactCollection::KeyDescription, collectionsQuery.value(col++).toString());
        collection.setMetaData(QContactCollection::KeyColor, collectionsQuery.value(col++).toString());
        collection.setMetaData(QContactCollection::KeySecondaryColor, collectionsQuery.value(col++).toString());
        collection.setMetaData(QContactCollection::KeyImage, collectionsQuery.value(col++).toString());
        collection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, collectionsQuery.value(col++).toString());
        collection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, collectionsQuery.value(col++).toInt());
        collection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, collectionsQuery.value(col++).toString());
        const int changeFlags = collectionsQuery.value(col++).toInt();

        const QString metadataStatement(QStringLiteral(
            "SELECT " // order and content can change due to schema upgrades, so list manually.
                "collectionId, "
                "key, "
                "value "
            "FROM CollectionsMetadata "
            "WHERE collectionId = :collectionId "
            "ORDER BY collectionId ASC"));

        QSqlQuery metadataQuery(m_database);
        if (!metadataQuery.prepare(metadataStatement)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for collection metadata details:\n%1\nQuery:\n%2")
                    .arg(metadataQuery.lastError().text())
                    .arg(metadataStatement));
            return QContactManager::UnspecifiedError;
        }

        metadataQuery.bindValue(":collectionId", dbId);
        metadataQuery.setForwardOnly(true);
        if (!ContactsDatabase::execute(metadataQuery)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to execute query for collection metadata details:\n%1\nQuery:\n%2")
                    .arg(metadataQuery.lastError().text())
                    .arg(metadataStatement));
            return QContactManager::UnspecifiedError;
        }

        while (metadataQuery.next()) {
            int col = 0;
            const quint32 dbId = metadataQuery.value(col++).toUInt();
            Q_ASSERT(ContactCollectionId::databaseId(collection.id()) == dbId);
            const QString key = metadataQuery.value(col++).toString();
            const QVariant value = metadataQuery.value(col++);
            collection.setExtendedMetaData(key, value);
        }

        if (changeFlags & ContactsDatabase::IsDeleted) {
            if (deletedCollections) {
                deletedCollections->append(collection);
            }
        } else if (changeFlags & ContactsDatabase::IsAdded) {
            if (addedCollections) {
                addedCollections->append(collection);
            }
        } else if (changeFlags & ContactsDatabase::IsModified) {
            if (modifiedCollections) {
                modifiedCollections->append(collection);
            }
        } else { // unmodified.
            if (unmodifiedCollections) {
                unmodifiedCollections->append(collection);
            }
        }
    }

    return QContactManager::NoError;
}


QContactManager::Error ContactReader::recordUnhandledChangeFlags(
        const QContactCollectionId &collectionId,
        bool *record)
{
    const QString unhandledChangeFlagsStatement = QStringLiteral(
            "SELECT recordUnhandledChangeFlags "
            "FROM Collections "
            "WHERE collectionId = :collectionId");


    QSqlQuery query(m_database);
    if (!query.prepare(unhandledChangeFlagsStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for record unhandled change flags:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(unhandledChangeFlagsStatement));
        return QContactManager::UnspecifiedError;
    }

    query.bindValue(":collectionId", ContactCollectionId::databaseId(collectionId));
    query.setForwardOnly(true);
    if (!ContactsDatabase::execute(query)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to execute query for record unhandled change flags:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(unhandledChangeFlagsStatement));
        return QContactManager::UnspecifiedError;
    }

    if (query.next()) {
        *record = query.value(0).toBool();
        return QContactManager::NoError;
    }

    return QContactManager::DoesNotExistError;
}

bool ContactReader::fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values)
{
    QVariantList keyNames;

    QString statement(QStringLiteral("SELECT name, value, compressed FROM OOB WHERE name "));
    if (keys.isEmpty()) {
        statement.append(QStringLiteral("LIKE '%1:%%'").arg(scope));
    } else {
        const QChar colon(QChar::fromLatin1(':'));

        QString keyList;
        foreach (const QString &key, keys) {
            keyNames.append(scope + colon + key);
            keyList.append(keyList.isEmpty() ? QStringLiteral("?") : QStringLiteral(",?"));
        }
        statement.append(QStringLiteral("IN (%1)").arg(keyList));
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
    QString statement(QStringLiteral("SELECT name FROM OOB WHERE name LIKE '%1:%%'").arg(scope));

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

void ContactReader::collectionsAvailable(const QList<QContactCollection> &)
{
}
