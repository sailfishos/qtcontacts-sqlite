/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Mobility Components.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QTCONTACTS_SQLITE_UTIL_H
#define QTCONTACTS_SQLITE_UTIL_H

#include <QtTest/QtTest>

#include <QtGlobal>
#include <QtCore/qnumeric.h>

#include <QtContacts>

#include "../../../src/engine/contactid_p.h"

#include "../../../src/extensions/qtcontacts-extensions_impl.h"
#include "../../../src/extensions/qtcontacts-extensions_manager_impl.h"

#include "../../../src/extensions/qcontactdeactivated.h"
#include "../../../src/extensions/qcontactdeactivated_impl.h"

#include "../../../src/extensions/qcontactincidental.h"
#include "../../../src/extensions/qcontactincidental_impl.h"

#include "../../../src/extensions/qcontactoriginmetadata.h"
#include "../../../src/extensions/qcontactoriginmetadata_impl.h"

#include "../../../src/extensions/qcontactstatusflags.h"
#include "../../../src/extensions/qcontactstatusflags_impl.h"

#include "../../../src/extensions/contactmanagerengine.h"

// qtpim doesn't support the customLabel field natively, but qtcontact-sqlite provides it
#define CUSTOM_LABEL_STORAGE_SUPPORTED

// qtpim doesn't support the displayLabelGroup field natively, but qtcontacts-sqlite provides it
#define DISPLAY_LABEL_GROUP_STORAGE_SUPPORTED

// Eventually these will make it into qtestcase.h
// but we might need to tweak the timeout values here.
#ifndef QTRY_COMPARE
#define QTRY_COMPARE(__expr, __expected) \
    do { \
        const int __step = 50; \
        const int __timeout = 5000; \
        if ((__expr) != (__expected)) { \
            QTest::qWait(0); \
        } \
        for (int __i = 0; __i < __timeout && ((__expr) != (__expected)); __i+=__step) { \
            QTest::qWait(__step); \
        } \
        QCOMPARE(__expr, __expected); \
    } while(0)
#endif

#ifndef QTRY_VERIFY
#define QTRY_VERIFY(__expr) \
        do { \
        const int __step = 50; \
        const int __timeout = 5000; \
        if (!(__expr)) { \
            QTest::qWait(0); \
        } \
        for (int __i = 0; __i < __timeout && !(__expr); __i+=__step) { \
            QTest::qWait(__step); \
        } \
        QVERIFY(__expr); \
    } while(0)
#endif


#define QTRY_WAIT(code, __expr) \
        do { \
        const int __step = 50; \
        const int __timeout = 5000; \
        if (!(__expr)) { \
            QTest::qWait(0); \
        } \
        for (int __i = 0; __i < __timeout && !(__expr); __i+=__step) { \
            do { code } while(0); \
            QTest::qWait(__step); \
        } \
    } while(0)

#define QCONTACTMANAGER_REMOVE_VERSIONS_FROM_URI(params)  params.remove(QString::fromLatin1(QTCONTACTS_VERSION_NAME)); \
                                                          params.remove(QString::fromLatin1(QTCONTACTS_IMPLEMENTATION_VERSION_NAME))

#define QTRY_COMPARE_SIGNALS_LOCALID_COUNT(__signalSpy, __expectedCount) \
    do { \
        int __spiedSigCount = 0; \
        const int __step = 50; \
        const int __timeout = 5000; \
        for (int __i = 0; __i < __timeout; __i+=__step) { \
            /* accumulate added from signals */ \
            __spiedSigCount = 0; \
            const QList<QList<QVariant> > __spiedSignals = __signalSpy; \
            foreach (const QList<QVariant> &__arguments, __spiedSignals) { \
                foreach (QContactId __apiId, __arguments.first().value<QList<QContactId> >()) { \
                    QVERIFY(ContactId::isValid(__apiId)); \
                    __spiedSigCount++; \
                } \
            } \
            if(__spiedSigCount == __expectedCount) { \
                break; \
            } \
            QTest::qWait(__step); \
        } \
        QCOMPARE(__spiedSigCount, __expectedCount); \
    } while(0)

QTCONTACTS_USE_NAMESPACE

Q_DECLARE_METATYPE(QList<QContactId>)

void registerIdType()
{
    qRegisterMetaType<QContactId>("QContactId");
    qRegisterMetaType<QList<QContactId> >("QList<QContactId>");
}

const char *contactsAddedSignal = SIGNAL(contactsAdded(QList<QContactId>));
const char *contactsChangedSignal = SIGNAL(contactsChanged(QList<QContactId>));
const char *contactsPresenceChangedSignal = SIGNAL(contactsPresenceChanged(QList<QContactId>));
const char *contactsRemovedSignal = SIGNAL(contactsRemoved(QList<QContactId>));
const char *relationshipsAddedSignal = SIGNAL(relationshipsAdded(QList<QContactId>));
const char *relationshipsRemovedSignal = SIGNAL(relationshipsRemoved(QList<QContactId>));
const char *selfContactIdChangedSignal = SIGNAL(selfContactIdChanged(QContactId,QContactId));

const QContactId &retrievalId(const QContactId &id) { return id; }

QContactId retrievalId(const QContact &contact)
{
    return retrievalId(contact.id());
}

QContactId removalId(const QContact &contact) { return retrievalId(contact); }

typedef QList<QContactDetail::DetailType> DetailList;

DetailList::value_type detailType(const QContactDetail &detail)
{
    return detail.type();
}

template<typename T>
DetailList::value_type detailType()
{
    return T::Type;
}

QString detailTypeName(const QContactDetail &detail)
{
    // We could create the table to print this, but I'm not bothering now...
    return QString::number(detail.type());
}

bool validDetailType(QContactDetail::DetailType type) { return (type != QContactDetail::TypeUndefined); }

bool validDetailType(const QContactDetail &detail)
{
    return validDetailType(detail.type());
}

typedef QMap<int, QVariant> DetailMap;

DetailMap detailValues(const QContactDetail &detail, bool includeProvenance = true)
{
    DetailMap rv(detail.values());

    if (!includeProvenance) {
        DetailMap::iterator it = rv.begin();
        while (it != rv.end()) {
            if (it.key() == QContactDetail__FieldProvenance) {
                it = rv.erase(it);
            } else {
                ++it;
            }
        }
    }

    return rv;
}

static bool variantEqual(const QVariant &lhs, const QVariant &rhs)
{
    // Work around incorrect result from QVariant::operator== when variants contain QList<int>
    static const int QListIntType = QMetaType::type("QList<int>");

    const int lhsType = lhs.userType();
    if (lhsType != rhs.userType()) {
        return false;
    }

    if (lhsType == QListIntType) {
        return (lhs.value<QList<int> >() == rhs.value<QList<int> >());
    }
    return (lhs == rhs);
}

static bool detailValuesEqual(const QContactDetail &lhs, const QContactDetail &rhs)
{
    const DetailMap lhsValues(detailValues(lhs, false));
    const DetailMap rhsValues(detailValues(rhs, false));

    if (lhsValues.count() != rhsValues.count()) {
        return false;
    }

    DetailMap::const_iterator lit = lhsValues.constBegin(), lend = lhsValues.constEnd();
    DetailMap::const_iterator rit = rhsValues.constBegin();
    for ( ; lit != lend; ++lit, ++rit) {
        if (!variantEqual(*lit, *rit)) {
            return false;
        }
    }

    return true;
}

static bool detailValuesSuperset(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // True if all values in rhs are present in lhs
    const DetailMap lhsValues(detailValues(lhs, false));
    const DetailMap rhsValues(detailValues(rhs, false));

    if (lhsValues.count() < rhsValues.count()) {
        return false;
    }

    foreach (const DetailMap::key_type &key, rhsValues.keys()) {
        if (!variantEqual(lhsValues[key], rhsValues[key])) {
            return false;
        }
    }

    return true;
}

static bool detailsEquivalent(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // Same as operator== except ignores differences in accessConstraints values
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesEqual(lhs, rhs);
}

static bool detailsSuperset(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // True is lhs is a superset of rhs
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesSuperset(lhs, rhs);
}

bool validContactType(const QContact &contact)
{
    return (contact.type() == QContactType::TypeContact);
}

template<typename T, typename F>
void setFilterDetail(QContactDetailFilter &filter, F field)
{
    filter.setDetailType(T::Type, field);
}

template<typename T, typename F>
void setFilterDetail(QContactDetailFilter &filter, T type, F field)
{
    filter.setDetailType(type, field);
}

template<typename T, typename F>
void setFilterDetail(QContactDetailRangeFilter &filter, F field)
{
    filter.setDetailType(T::Type, field);
}

template<typename T, typename F>
void setFilterDetail(QContactDetailRangeFilter &filter, T type, F field)
{
    filter.setDetailType(type, field);
}

template<typename T>
void setFilterDetail(QContactDetailFilter &filter)
{
    filter.setDetailType(T::Type);
}

template<typename T>
void setFilterValue(QContactDetailFilter &filter, T value)
{
    filter.setValue(value);
}

template<typename T, typename F>
void setSortDetail(QContactSortOrder &sort, F field)
{
    sort.setDetailType(T::Type, field);
}

template<typename T, typename F>
void setSortDetail(QContactSortOrder &sort, T type, F field)
{
    sort.setDetailType(type, field);
}

template<typename F>
QString relationshipString(F fn) { return fn(); }

template<typename T>
void setFilterType(QContactRelationshipFilter &filter, T type)
{
    filter.setRelationshipType(relationshipString(type));
}

void setFilterContact(QContactRelationshipFilter &filter, const QContact &contact)
{
    filter.setRelatedContact(contact);
}

QContactRelationship makeRelationship(const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship;

    QContact first, second;
    first.setId(firstId);
    second.setId(secondId);
    relationship.setFirst(first);
    relationship.setSecond(second);

    return relationship;
}

template<typename T>
QContactRelationship makeRelationship(T type, const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship(makeRelationship(firstId, secondId));
    relationship.setRelationshipType(relationshipString(type));
    return relationship;
}

QContactRelationship makeRelationship(const QString &type, const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship(makeRelationship(firstId, secondId));
    relationship.setRelationshipType(type);
    return relationship;
}

const QContact &relatedContact(const QContact &contact) { return contact; }

QContactId relatedContactId(const QContact &contact) { return contact.id(); }

QList<QContactId> relatedContactIds(const QList<QContact> &contacts)
{
    QList<QContactId> rv;
    foreach (const QContact &contact, contacts) {
        rv.append(contact.id());
    }
    return rv;
}

#endif
