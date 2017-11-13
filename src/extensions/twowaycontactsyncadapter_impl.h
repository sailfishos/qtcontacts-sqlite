/*
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jolla.com>
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

#ifndef TWOWAYCONTACTSYNCADAPTER_IMPL_H
#define TWOWAYCONTACTSYNCADAPTER_IMPL_H

#include <qtcontacts-extensions.h>
#include <contactmanagerengine.h>
#include <twowaycontactsyncadapter.h>
#include <qcontactoriginmetadata.h>

#include <QContactManager>
#include <QContactGuid>
#include <QContactSyncTarget>
#include <QContactTimestamp>
#include <QContactUrl>

#include <QDebug>
#include <QLocale>
#include <QDataStream>

#define QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG(msg)                           \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_twcsa_debug_trace_enabled())) { \
            qDebug() << msg;                                             \
        }                                                                \
    } while (0)

#define QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT_DELTA(contact1, contact2, ignorableDetails, ignorableFields) \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_twcsa_debug_trace_enabled())) { \
            (void)exactContactMatchExistsInList(                         \
                    contact1, QList<QContact>() << contact2,             \
                    ignorableDetails, ignorableFields,                   \
                    true); /* print differences */                       \
        }                                                                \
    } while (0)

#define QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT(contact, ignorableDetails, ignorableFields) \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_twcsa_debug_trace_enabled())) { \
            dumpContact(contact, ignorableDetails, ignorableFields);     \
        }                                                                \
    } while (0)

#define QTCONTACTS_SQLITE_TWCSA_DEBUG_DETAIL(detail)                     \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_twcsa_debug_trace_enabled())) { \
            dumpContactDetail(detail);                                   \
        }                                                                \
    } while (0)

namespace QtContactsSqliteExtensions {

    static bool qtcontacts_sqlite_twcsa_debug_trace_enabled()
    {
        static const bool traceEnabled(!QString(QLatin1String(qgetenv("QTCONTACTS_SQLITE_TWCSA_TRACE"))).isEmpty());
        return traceEnabled;
    }

    // QDataStream encoding version to use in OOB storage.
    // Don't change this without scheduling a migration for stored data!
    // (which can be done in contactsdatabase.cpp)
    const QDataStream::Version STREAM_VERSION = QDataStream::Qt_5_1;

    class TwoWayContactSyncAdapterPrivate
    {
    public:
        enum Status {
            Inactive = 0,
            Initialized,
            ReadSyncStateData,
            StoredRemoteChanges,
            DeterminedLocalChanges,
            Finished
        };

        enum ReadMode {
            ReadAll,
            ReadPartial,
            ReadRemaining
        };

        struct StateData {
            StateData()
                : m_status(Inactive)
                , m_mutated(false)
                , m_partial(false)
                , m_exportedIdsModified(false)
                , m_mutatedPrevRemoteModified(false)
            {
            }

            Status m_status;
            QString m_oobScope;
            QDateTime m_localSince;
            QDateTime m_remoteSince;
            QDateTime m_newLocalSince;
            QDateTime m_newRemoteSince;
            QList<QContactId> m_exportedIds;
            QList<QContact> m_prevRemote;
            QList<QContact> m_mutatedPrevRemote;
            QList<QContactId> m_remotelyDeletedThisSync;             // this one is ephemeral
            QMap<QContactId, QContact> m_possiblyUploadedAdditions;  // this one is stored OOB
            QMap<QContactId, QContact> m_reportedUploadedAdditions;  // this one is ephemeral
            QMap<QString, QContact> m_definitelyDownloadedAdditions; // this one is stored OOB. Guid key.
            bool m_mutated; // whether the MUTATED_PREV_REMOTE list has been populated or not
            bool m_partial;
            bool m_exportedIdsModified;
            bool m_mutatedPrevRemoteModified;
        };

        TwoWayContactSyncAdapterPrivate(TwoWayContactSyncAdapter *q, const QString &syncTarget, const QMap<QString, QString> &params);
        TwoWayContactSyncAdapterPrivate(TwoWayContactSyncAdapter *q, const QString &syncTarget, QContactManager &manager);
       ~TwoWayContactSyncAdapterPrivate();

        TwoWayContactSyncAdapter *m_q;
        QContactManager *m_manager;
        ContactManagerEngine *m_engine;
        QString m_syncTarget;
        bool m_deleteManager;

        bool readStateData(const QString &accountId, ReadMode mode);

        void clear(const QString &accountId);        // clears the state data below.

        QList<QPair<QContact, QContact> > createUpdateList(StateData &syncState,
                                                           const QList<QContact> &remoteDeleted,
                                                           QList<QContact> *remoteAddedModified,
                                                           QList<QPair<int, int> > *additionIndices,
                                                           bool needToApplyDelta,
                                                           const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
                                                           const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields);
        QContact applyRemoteDeltaToPrev(const QContact &prev, const QContact &curr);
        QList<QContactDetail> improveDelta(QList<QContactDetail> *removals, QList<QContactDetail> *additions);

        QMap<QString, StateData> m_stateData; // per account.
    };
}

QTCONTACTS_USE_NAMESPACE
using namespace QtContactsSqliteExtensions;

namespace {

void registerTypes()
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        qRegisterMetaType<QList<int> >();
        qRegisterMetaTypeStreamOperators<QList<int> >();
    }
}

// Input must be UTC
QString dateTimeString(const QDateTime &qdt)
{
    return QLocale::c().toString(qdt, QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz"));
}

QDateTime fromDateTimeString(const QString &s)
{
    QDateTime rv(QLocale::c().toDateTime(s, QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz")));
    rv.setTimeSpec(Qt::UTC);
    return rv;
}

QMap<QString, QString> checkParams(const QMap<QString, QString> &params)
{
    QMap<QString, QString> rv(params);

    const QString presenceKey(QStringLiteral("mergePresenceChanges"));
    if (!rv.contains(presenceKey)) {
        // Don't report presence changes
        rv.insert(presenceKey, QStringLiteral("false"));
    }

    return rv;
}

QSet<QContactDetail::DetailType> getDefaultIgnorableDetailTypes()
{
    QSet<QContactDetail::DetailType> rv;
    rv.insert(QContactDetail__TypeDeactivated);
    rv.insert(QContactDetail::TypeDisplayLabel);
    rv.insert(QContactDetail::TypeGlobalPresence);
    rv.insert(QContactDetail__TypeIncidental);
    rv.insert(QContactDetail::TypePresence);
    rv.insert(QContactDetail::TypeOnlineAccount);
    rv.insert(QContactDetail__TypeStatusFlags);
    rv.insert(QContactDetail::TypeSyncTarget);
    rv.insert(QContactDetail::TypeTimestamp);
    return rv;
}

const QSet<QContactDetail::DetailType> &defaultIgnorableDetailTypes()
{
    static QSet<QContactDetail::DetailType> types(getDefaultIgnorableDetailTypes());
    return types;
}

QHash<QContactDetail::DetailType, QSet<int> > getDefaultIgnorableDetailFields()
{
    QHash<QContactDetail::DetailType, QSet<int> > rv;
    // Currently, no fields are ignorable by default other than PhoneNumber::NormalizedNumber
    // but that field is hardcoded to be removed, so doesn't appear here.
    // Clients can add their own ignorable fields depending on the semantics of their
    // sync service (eg, might not be able to handle some subtypes or contexts, etc)
    return rv;
}

const QHash<QContactDetail::DetailType, QSet<int> > &defaultIgnorableDetailFields()
{
    static QHash<QContactDetail::DetailType, QSet<int> > fields(getDefaultIgnorableDetailFields());
    return fields;
}

void removeIgnorableDetailsFromList(QList<QContactDetail> *dets,
                                    const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
                                    const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields)
{
    // ignore differences in certain detail types
    for (int i = dets->size() - 1; i >= 0; --i) {
        const QContactDetail::DetailType type(dets->at(i).type());
        if (ignorableDetailTypes.contains(type)) {
            dets->removeAt(i); // we can ignore this detail altogether
        } else {
            // remove any fields specified as ignorable by the adapter
            // (eg, if that adapter cannot sync certain fields of some detail types).
            QContactDetail modD = dets->at(i);
            if (ignorableDetailFields.contains(type)) {
                Q_FOREACH (int ignorableField, ignorableDetailFields.value(type)) {
                    modD.removeValue(ignorableField);
                }
            }
            // remove any fields which should never be synced by any adapter.
            modD.removeValue(QContactDetail__FieldProvenance);
            modD.removeValue(QContactDetail__FieldModifiable);
            if (type == QContactDetail::TypePhoneNumber) {
                modD.removeValue(QContactPhoneNumber__FieldNormalizedNumber);
            }
            // replace detail in list.
            dets->replace(i, modD);
        }
    }
}

void dumpContactDetail(const QContactDetail &d)
{
    qWarning() << "++ ---------" << d.type();
    QMap<int, QVariant> values = d.values();
    foreach (int key, values.keys()) {
        qWarning() << "    " << key << "=" << values.value(key);
    }
}

void dumpContact(const QContact &c,
                 const QSet<QContactDetail::DetailType> &ignorableDetailTypes = defaultIgnorableDetailTypes(),
                 const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields = defaultIgnorableDetailFields())
{
    qWarning() << "++++ ---- Contact:" << c.id();
    QList<QContactDetail> cdets = c.details();
    removeIgnorableDetailsFromList(&cdets, ignorableDetailTypes, ignorableDetailFields);
    foreach (const QContactDetail &det, cdets) {
        dumpContactDetail(det);
    }
}

int scoreForValuePair(const QVariant &removal, const QVariant &addition)
{
    // work around some variant-comparison issues.
    if (Q_UNLIKELY((((removal.type() == QVariant::String && addition.type() == QVariant::Invalid)
                   ||(addition.type() == QVariant::String && removal.type() == QVariant::Invalid))
                   &&(removal.toString().isEmpty() && addition.toString().isEmpty())))) {
        // it could be that "invalid" variant is stored as an empty
        // string in database, if the field is a string field.
        // if so, ignore that - it's not a difference.
        return 0;
    }

    if (removal.canConvert<QList<int> >() && addition.canConvert<QList<int> >()) {
        // direct comparison of QVariant::fromValue<QList<int> > doesn't work
        // so instead, do the conversion and compare them manually.
        QList<int> rlist = removal.value<QList<int> >();
        QList<int> llist = addition.value<QList<int> >();
        return rlist == llist ? 0 : 1;
    }

    // the sync adaptor might return url data as a string.
    if (removal.type() == QVariant::Url && addition.type() == QVariant::String) {
        QUrl rurl = removal.toUrl();
        QUrl aurl = QUrl(addition.toString());
        return rurl == aurl ? 0 : 1;
    } else if (removal.type() == QVariant::String && addition.type() == QVariant::Url) {
        QUrl rurl = QUrl(removal.toString());
        QUrl aurl = addition.toUrl();
        return rurl == aurl ? 0 : 1;
    }

    // normal case.  if they're different, increase the distance.
    return removal == addition ? 0 : 1;
}

int scoreForDetailPair(const QContactDetail &removal, const QContactDetail &addition)
{
    int score = 0; // distance
    QMap<int, QVariant> rvalues = removal.values();
    QMap<int, QVariant> avalues = addition.values();

    QList<int> seenFields;
    foreach (int field, rvalues.keys()) {
        seenFields.append(field);
        score += scoreForValuePair(rvalues.value(field), avalues.value(field));
    }

    foreach (int field, avalues.keys()) {
        if (seenFields.contains(field)) continue;
        score += scoreForValuePair(rvalues.value(field), avalues.value(field));
    }

    return score;
}

bool detailPairExactlyMatches(const QContactDetail &a, const QContactDetail &b, bool printDifferences = false)
{
    if (a.type() != b.type()) {
        return false;
    }

    // now ensure that all values match
    QMap<int, QVariant> avalues = a.values();
    QMap<int, QVariant> bvalues = b.values();
    foreach (int akey, avalues.keys()) {
        QVariant avalue = avalues.value(akey);
        if (!bvalues.contains(akey)) {
            // this may still be ok if the avalue is NULL
            // or if the avalue is an empty string,
            // as the database can sometimes return empty
            // string instead of NULL value.
            if ((avalue.type() == QVariant::String && avalue.toString().isEmpty())
                    || avalue.type() == QVariant::Invalid) {
                // this is ok.
            } else {
                // a has a real value which b does not have.
                if (Q_UNLIKELY(printDifferences)) {
                    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("detail A of type" << a.type() << "has value which B does not have:" << akey << "=" << avalue);
                }
                return false;
            }
        } else {
            // b contains the same key, but do the values match?
            if (scoreForValuePair(avalue, bvalues.value(akey)) != 0) {
                if (Q_UNLIKELY(printDifferences)) {
                    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("detail A of type" << a.type() << "has value which differs from B:" << akey << "=" << avalue << "!=" << bvalues.value(akey));
                }
                return false;
            }

            // yes, they match.
            bvalues.remove(akey);
        }
    }

    // if there are any non-empty/null values left in b, then
    // a and b do not exactly match.
    foreach (int bkey, bvalues.keys()) {
        QVariant bvalue = bvalues.value(bkey);
        if ((bvalue.type() == QVariant::String && bvalue.toString().isEmpty())
                || bvalue.type() == QVariant::Invalid) {
            // this is ok.
        } else {
            // b has a real value which a does not have.
            if (Q_UNLIKELY(printDifferences)) {
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("detail B of type" << b.type() << "has value which A does not have:" << bkey << "=" << bvalue);
            }
            return false;
        }
    }

    return true;
}

int exactDetailMatchExistsInList(const QContactDetail &det, const QList<QContactDetail> &list)
{
    for (int i = 0; i < list.size(); ++i) {
        if (detailPairExactlyMatches(det, list[i])) {
            return i; // exact match at this index.
        }
    }

    return -1;
}

bool contactDetailsMatchExactly(const QList<QContactDetail> &aDetails, const QList<QContactDetail> &bDetails, bool printDifferences = false)
{
    // for it to be an exact match:
    // a) every detail in aDetails must exist in bDetails
    // b) no extra details can exist in bDetails
    if (aDetails.size() != bDetails.size()) {
        if (Q_UNLIKELY(printDifferences)) {
            // detail count differs, and continue the analysis to find out precisely what the differences are.
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("A has more details than B:" << aDetails.size() << ">" << bDetails.size());
        } else {
            // detail count differs, return immediately.
            return false;
        }
    }

    QList<QContactDetail> nonMatchedADetails;
    QList<QContactDetail> nonMatchedBDetails = bDetails;
    bool allADetailsHaveMatches = true;
    foreach (const QContactDetail &aDetail, aDetails) {
        int exactMatchIndex = exactDetailMatchExistsInList(aDetail, nonMatchedBDetails);
        if (exactMatchIndex == -1) {
            // no exact match for this detail.
            allADetailsHaveMatches = false;
            if (Q_UNLIKELY(printDifferences)) {
                // we only record the difference if we're printing them.
                nonMatchedADetails.append(aDetail);
            } else {
                // we only break if we're not printing all differences.
                break;
            }
        } else {
            // found a match for this detail.
            // remove it from ldets so that duplicates in cdets
            // don't mess up our detection.
            nonMatchedBDetails.removeAt(exactMatchIndex);
        }
    }

    if (allADetailsHaveMatches && nonMatchedBDetails.size() == 0) {
        return true; // exact match
    }

    if (Q_UNLIKELY(printDifferences)) {
        Q_FOREACH (const QContactDetail &ad, nonMatchedADetails) {
            bool foundMatch = false;
            for (int i = 0; i < nonMatchedBDetails.size(); ++i) {
                const QContactDetail &bd = nonMatchedBDetails[i];
                if (ad.type() == bd.type()) { // most likely a modification.
                    foundMatch = true;
                    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("Detail modified from A to B:");
                    detailPairExactlyMatches(ad, bd, printDifferences);
                    nonMatchedBDetails.removeAt(i);
                    break;
                }
            }
            if (!foundMatch) {
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("New detail exists in contact A:");
                QTCONTACTS_SQLITE_TWCSA_DEBUG_DETAIL(ad);
            }
        }
        Q_FOREACH (const QContactDetail &bd, nonMatchedBDetails) {
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("New detail exists in contact B:");
            QTCONTACTS_SQLITE_TWCSA_DEBUG_DETAIL(bd);
        }
    }

    return false;
}

int exactContactMatchExistsInList(const QContact &aContact,
                                  const QList<QContact> &list,
                                  const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
                                  const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
                                  bool printDifferences = false)
{
    const QSet<QContactDetail::DetailType> &ignoreDetails(!ignorableDetailTypes.isEmpty() ? ignorableDetailTypes : defaultIgnorableDetailTypes());
    const QHash<QContactDetail::DetailType, QSet<int> > &ignoreFields(!ignorableDetailFields.isEmpty() ? ignorableDetailFields : defaultIgnorableDetailFields());

    QList<QContactDetail> aDetails = aContact.details();
    removeIgnorableDetailsFromList(&aDetails, ignoreDetails, ignoreFields);
    for (int i = 0; i < list.size(); ++i) {
        QList<QContactDetail> bDetails = list[i].details();
        removeIgnorableDetailsFromList(&bDetails, ignoreDetails, ignoreFields);
        if (contactDetailsMatchExactly(aDetails, bDetails, printDifferences)) {
            return i; // exact match at this index.
        }
    }

    return -1;
}

// Determine the delta between prev and curr.
// This function returns two lists:
//   - details in prev which need to removed from UPDATED (copy of prev)
//   - details from curr which need to be added to UPDATED (copy of prev)
// It's not a minimal delta because this function treats modifications as removal+addition.
// We later on attempt to determine which of those removal+addition pairs are "actually" modifications.
QPair<QList<QContactDetail>, QList<QContactDetail> > fallbackDelta(const QContact &prev, const QContact &curr)
{
    QList<QContactDetail> pdets = prev.details();
    QList<QContactDetail> cdets = curr.details();

    // XXX TODO: handle Unique details (Guid / Name / etc)

    // ignore all exact matches, as they don't form part of the delta.
    for (int i = pdets.size() - 1; i >= 0; --i) {
        int idx = -1;
        for (int j = cdets.size() - 1; j >= 0; --j) {
            if (detailPairExactlyMatches(pdets.at(i), cdets.at(j))) {
                idx = j;
                break;
            }
        }
        if (idx != -1) {
            // found an exact match; this detail hasn't changed.
            pdets.removeAt(i);
            cdets.removeAt(idx);
        }
    }

    // anything which remains is in the delta.
    return QPair<QList<QContactDetail>, QList<QContactDetail> >(pdets, cdets);
}

}

TwoWayContactSyncAdapterPrivate::TwoWayContactSyncAdapterPrivate(TwoWayContactSyncAdapter *q, const QString &syncTarget, const QMap<QString, QString> &params)
    : m_q(q)
    , m_manager(new QContactManager(QStringLiteral("org.nemomobile.contacts.sqlite"), checkParams(params)))
    , m_engine(contactManagerEngine(*m_manager))
    , m_syncTarget(syncTarget)
    , m_deleteManager(true)
{
    registerTypes();
}

TwoWayContactSyncAdapterPrivate::TwoWayContactSyncAdapterPrivate(TwoWayContactSyncAdapter *q, const QString &syncTarget, QContactManager &manager)
    : m_q(q)
    , m_manager(&manager)
    , m_engine(contactManagerEngine(*m_manager))
    , m_syncTarget(syncTarget)
    , m_deleteManager(false)
{
    registerTypes();
}

TwoWayContactSyncAdapterPrivate::~TwoWayContactSyncAdapterPrivate()
{
    if (m_deleteManager) {
        delete m_manager;
    }
}

void TwoWayContactSyncAdapterPrivate::clear(const QString &accountId)
{
    m_stateData.insert(accountId, TwoWayContactSyncAdapterPrivate::StateData());
}

bool TwoWayContactSyncAdapterPrivate::readStateData(const QString &accountId, ReadMode mode)
{
    StateData &syncState(m_stateData[accountId]);

    // Read the timestamps we should specify when requesting changes
    // from both the remote service and the local database, and also
    // the PREV_REMOTE and EXPORTED_IDS lists, from the out-of-band database.
    // The PREV_REMOTE list is the list of contacts which we upsynced to the remote
    // server during the last sync run.  The EXPORTED_IDS list is the list of ids
    // of contacts which we upsynced last run which do not have a synctarget constituent
    // associated with them (and so we need to explicitly tell the qtcontacts-sqlite
    // backend that we're interested in receiving change information about it).
    QStringList keys;
    if (mode == ReadAll || mode == ReadPartial) {
        keys << QStringLiteral("remoteSince")
             << QStringLiteral("localSince")
             << QStringLiteral("exportedIds");
    }
    if (mode == ReadAll || mode == ReadRemaining) {
        keys << QStringLiteral("prevRemote")
             << QStringLiteral("possiblyUploadedAdditions")
             << QStringLiteral("definitelyDownloadedAdditions");
    }
    QMap<QString, QVariant> values;
    if (!m_engine->fetchOOB(syncState.m_oobScope, keys, &values)) {
        // fetchOOB only returns false if a db error occurs; it still returns true
        // if the fetch "succeeded" but no values for those keys exist.
        qWarning() << Q_FUNC_INFO << "failed to read sync state data for" << m_syncTarget << "account" << accountId;
        clear(accountId);
        return false;
    }

    if (mode == ReadAll || mode == ReadPartial) {
        QString sinceStr = values.value(QStringLiteral("remoteSince")).toString();
        syncState.m_remoteSince = sinceStr.isEmpty() ? QDateTime() : fromDateTimeString(sinceStr);

        sinceStr = values.value(QStringLiteral("localSince")).toString();
        syncState.m_localSince = sinceStr.isEmpty() ? QDateTime() : fromDateTimeString(sinceStr);

        QByteArray cdata = values.value(QStringLiteral("exportedIds")).toByteArray();
        QDataStream readExportedIds(cdata);
        readExportedIds.setVersion(STREAM_VERSION);
        readExportedIds >> syncState.m_exportedIds;
    }

    if (mode == ReadAll || mode == ReadRemaining) {
        QByteArray cdata = values.value(QStringLiteral("prevRemote")).toByteArray();
        QDataStream readPrevRemote(cdata);
        readPrevRemote.setVersion(STREAM_VERSION);
        readPrevRemote >> syncState.m_prevRemote;

        cdata = values.value(QStringLiteral("possiblyUploadedAdditions")).toByteArray();
        QDataStream readPossiblyUploadedAdditions(cdata);
        readPossiblyUploadedAdditions.setVersion(STREAM_VERSION);
        readPossiblyUploadedAdditions >> syncState.m_possiblyUploadedAdditions;

        cdata = values.value(QStringLiteral("definitelyDownloadedAdditions")).toByteArray();
        QDataStream readDefinitelyDownloadedAdditions(cdata);
        readDefinitelyDownloadedAdditions.setVersion(STREAM_VERSION);
        readDefinitelyDownloadedAdditions >> syncState.m_definitelyDownloadedAdditions;
    }

    syncState.m_partial = (mode == ReadPartial);
    return true;
}

// Given the list of PREV_REMOTE contacts which were stored in qtcontacts-sqlite's out-of-band database
// at the successful completion of the last sync run by this sync adapter, and also the
// lists of remotely added/modified/deleted contacts, plus the list of "exportedIds" (which
// contains the QContactIds of previously-upsynced contacts which did not have a synctarget
// constituent (and therefore no GUID detail)), this function will create a list of update
// pairs which can be passed to the qtcontacts-sqlite storeSyncContacts() function.
// If any of the update pairs correspond to contact additions, the mapping of input index
// (into "remoteAddedModified") to output index (into the result list) is added to the
// "additionIndices" list.  This allows the eventual allocation of local IDs to be linked
// back to the remote contact from which they were created.
QList<QPair<QContact, QContact> > TwoWayContactSyncAdapterPrivate::createUpdateList(StateData &syncState,
                                                                                    const QList<QContact> &remoteDeleted,
                                                                                    QList<QContact> *remoteAddedModified,
                                                                                    QList<QPair<int, int> > *additionIndices,
                                                                                    bool needToApplyDelta,
                                                                                    const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
                                                                                    const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields)
{
    // <PREV_REMOTE, UPDATED_REMOTE> pairs.
    QList<QPair<QContact, QContact> > retn;
    QList<QPair<QContact, QContact> > alreadyUploadedArtifacts;
    QList<QPair<QContact, QContact> > alreadyDownloadedArtifacts;

    // Index all existing contacts by their ID value
    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("indexing previously seen remote contacts by id");
    QHash<QContactId, int> prevIdToIndex;
    for (int i = 0; i < syncState.m_prevRemote.size(); ++i) {
        const QContact &prevContact(syncState.m_prevRemote.at(i));
        const QContactId id = prevContact.id();
        if (!id.isNull()) {
            prevIdToIndex.insert(id, i);
        } else {
            qWarning() << Q_FUNC_INFO << "Invalid prev contact with no ID:" << prevContact;
        }
    }

    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("examining remote delta lists to determine which local contacts need to be added/modified/removed");
    QList<int> deletePositions;
    QList<QPair<int, int> > modificationPositions;
    QList<int> additionPositions;

    for (int i = 0; i < remoteDeleted.size(); ++i) {
        const QContact &prevContact(remoteDeleted.at(i));
        const QContactId id = prevContact.id();

        int prevIndex = -1;
        QHash<QContactId, int>::const_iterator it = prevIdToIndex.find(id);
        if (it != prevIdToIndex.end()) {
            prevIndex = *it;
        }
        if (prevIndex == -1) {
            const QString &pcGuid(prevContact.detail<QContactGuid>().guid());
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "marked as deleted remotely, not found locally");
            if (syncState.m_possiblyUploadedAdditions.contains(id)) {
                // The contact was uploaded during a failed sync, and subsequently deleted remotely.
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "was uploaded during a failed sync and then deleted remotely");
                alreadyUploadedArtifacts.append(qMakePair(syncState.m_possiblyUploadedAdditions.value(id), QContact()));
            } else if (syncState.m_definitelyDownloadedAdditions.contains(pcGuid)) {
                // The contact was downloaded during a failed sync, and subsequently deleted remotely.
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "was downloaded during a failed sync and then deleted remotely");
                alreadyDownloadedArtifacts.append(qMakePair(syncState.m_definitelyDownloadedAdditions.value(pcGuid), QContact()));
            } else {
                // Ignore this removal, the contact may already have been removed remotely
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "was never seen locally but reported as deleted remotely...");
            }
        } else {
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << prevContact.detail<QContactGuid>().guid() << "with id" << id << "marked as deleted remotely, found at index" << prevIndex);
            deletePositions.append(prevIndex);
            syncState.m_remotelyDeletedThisSync.append(id);
        }
    }

    for (int i = 0; i < remoteAddedModified->size(); ++i) {
        QContact prevContact(remoteAddedModified->at(i));
        const QContactId id = prevContact.id();

        int prevIndex = -1;
        QHash<QContactId, int>::const_iterator it = prevIdToIndex.find(id);
        if (it != prevIdToIndex.end()) {
            prevIndex = *it;
        }
        if (prevIndex == -1) {
            // Check if this contact might already exist locally due to failed sync.  This might occur if
            // either the contact was uploaded during a failed sync and is being reported as an addition,
            // or it was downloaded and stored during a failed sync and is being re-reported as an addition.
            // We always treat it as a remote modification in case it was subsequently modified remotely.
            const QString &pcGuid(prevContact.detail<QContactGuid>().guid());
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "marked as added or modified remotely, not found locally");
            if (syncState.m_possiblyUploadedAdditions.contains(id)) {
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "was uploaded during a failed sync and then modified remotely");
                alreadyUploadedArtifacts.append(qMakePair(syncState.m_possiblyUploadedAdditions.value(id), prevContact));
            } else if (syncState.m_definitelyDownloadedAdditions.contains(pcGuid)) {
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "was downloaded during a failed sync and then modified remotely");
                prevContact.setId(syncState.m_definitelyDownloadedAdditions.value(pcGuid).id());
                alreadyDownloadedArtifacts.append(qMakePair(syncState.m_definitelyDownloadedAdditions.value(pcGuid), prevContact));
            } else {
                // This must be a pure server-side addition
                QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << pcGuid << "was never seen locally, must be a pure server-side addition");
                additionPositions.append(i);
            }
        } else {
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("contact:" << prevContact.detail<QContactGuid>().guid() << "was modified remotely, found at index" << prevIndex);
            modificationPositions.append(qMakePair(prevIndex, i));
        }
    }

    // determine remote additions/modifications
    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("creating update-pairs for remote additions/modifications/removals");
    QList<int> prevRemoteDeletionIndexes;
    QHash<int, QContact> prevRemoteModificationIndexes;
    QList<QContact> prevRemoteAdditions;

    foreach (int prevIndex, deletePositions) {
        const QContact &prevContact(syncState.m_prevRemote.at(prevIndex));
        // remote removal: <PREV_REMOTE, NULL>
        retn.append(qMakePair(prevContact, QContact()));
        prevRemoteDeletionIndexes.append(prevIndex);

        const QContactId id = prevContact.id();
        if (!id.isNull()) {
            syncState.m_exportedIds.removeAll(id);
            syncState.m_exportedIdsModified = true;
        }
    }

    QList<QPair<int, int> >::const_iterator it = modificationPositions.constBegin(), end = modificationPositions.constEnd();
    for ( ; it != end; ++it) {
        const QContact &prevContact(syncState.m_prevRemote.at((*it).first));
        const QContact &newContact(remoteAddedModified->at((*it).second));

        QContact updated = needToApplyDelta ? applyRemoteDeltaToPrev(prevContact, newContact) : newContact;
        if (exactContactMatchExistsInList(prevContact, QList<QContact>() << updated, ignorableDetailTypes, ignorableDetailFields) == -1) {
            // the change is substantial (ie, wasn't just an eTag update, for example)
            prevRemoteModificationIndexes.insert((*it).first, updated);
            // modifications: <PREV_REMOTE, UPDATED_REMOTE>
            retn.append(qMakePair(prevContact, updated));
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("substantial change detected for contact" << prevContact.detail<QContactGuid>().guid());
            QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT(prevContact, ignorableDetailTypes, ignorableDetailFields);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("!=");
            QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT(newContact, ignorableDetailTypes, ignorableDetailFields);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("with delta applied, the updated contact will become:");
            QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT(updated, ignorableDetailTypes, ignorableDetailFields);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("the specific differences are:");
            QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT_DELTA(prevContact, newContact, ignorableDetailTypes, ignorableDetailFields);
        } else {
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("ignoring insubstantial change to contact" << prevContact.detail<QContactGuid>().guid());
            QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT(prevContact, ignorableDetailTypes, ignorableDetailFields);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("==");
            QTCONTACTS_SQLITE_TWCSA_DEBUG_CONTACT(newContact, ignorableDetailTypes, ignorableDetailFields);
        }
    }

    foreach (int index, additionPositions) {
        const int updateIndex = retn.count();
        QContact addedContact = remoteAddedModified->at(index);
        prevRemoteAdditions.append(addedContact);
        // pure server-side addition: <NULL, UPDATED_REMOTE>
        retn.append(qMakePair(QContact(), addedContact));
        additionIndices->append(qMakePair(index, updateIndex));
    }

    // mutate the m_mutatedPrevRemote list according to the changes.
    if (syncState.m_mutatedPrevRemoteModified) {
        qWarning() << Q_FUNC_INFO << "Overwriting existing changes in mutatedPrevRemote!";
    }
    syncState.m_mutatedPrevRemote = syncState.m_prevRemote;
    syncState.m_mutated = true;

    if (!prevRemoteModificationIndexes.isEmpty() || !prevRemoteDeletionIndexes.isEmpty() || !prevRemoteAdditions.isEmpty()) {
        syncState.m_mutatedPrevRemoteModified = true;

        // apply changes identified
        foreach (int idx, prevRemoteModificationIndexes.keys())
            syncState.m_mutatedPrevRemote.replace(idx, prevRemoteModificationIndexes.value(idx));
        qSort(prevRemoteDeletionIndexes);
        for (int i = prevRemoteDeletionIndexes.size() - 1; i >= 0; --i)
            syncState.m_mutatedPrevRemote.removeAt(prevRemoteDeletionIndexes.at(i));
        syncState.m_mutatedPrevRemote.append(prevRemoteAdditions);
    }

    // adjust for artifacts of incomplete sync operations
    for (int i = 0; i < alreadyUploadedArtifacts.size(); ++i) {
        const QPair<QContact, QContact> &update(alreadyUploadedArtifacts[i]);
        retn.append(update);
        syncState.m_reportedUploadedAdditions.insert(update.first.id(), update.second);
        if (update.second != QContact()) {
            // addition/modification - we need to track that contact.
            syncState.m_mutatedPrevRemote.append(update.second);
            syncState.m_mutatedPrevRemoteModified = true;
            syncState.m_exportedIds.append(update.second.id());
            syncState.m_exportedIdsModified = true;
        }
    }

    for (int i = 0; i < alreadyDownloadedArtifacts.size(); ++i) {
        const QPair<QContact, QContact> &update(alreadyDownloadedArtifacts[i]);
        retn.append(update);
        if (update.second != QContact()) {
            // addition/modification - we need to track that contact.
            syncState.m_mutatedPrevRemote.append(update.second);
            syncState.m_mutatedPrevRemoteModified = true;
            syncState.m_exportedIds.append(update.second.id());
            syncState.m_exportedIdsModified = true;
        }
    }

    return retn;
}

// Having received the current server-side version of the contact,
// we need to determine what actually changed server-side.
// Once we determine what changed, we mutate PREV with those
// changes, so that the detail ids are preserved.
// This will allow qtcontacts-sqlite to precisely determine
// what changed remotely, and apply the changes appropriately
// in the local database.
QContact TwoWayContactSyncAdapterPrivate::applyRemoteDeltaToPrev(const QContact &prev, const QContact &curr)
{
    QContact newRemote = prev;

    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("determining detail-delta for contact:" << prev.detail<QContactGuid>().guid());
    QPair<QList<QContactDetail>, QList<QContactDetail> > fbd = fallbackDelta(newRemote, curr);
    QList<QContactDetail> removals = fbd.first;
    QList<QContactDetail> additions = fbd.second;
    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("initial-guess delta has" << removals.size() << "removals and" << additions.size() << "additions.");

    QList<QContactDetail> modifications = improveDelta(&removals, &additions);
    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("improved delta has" << removals.size() << "removals and" << additions.size() << "additions, with" << modifications.size() << "modifications");

    for (int i = 0; i < removals.size(); ++i) {
        QContactDetail removal = removals.at(i);
        newRemote.removeDetail(&removal);
        QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("removing detail:");
        QTCONTACTS_SQLITE_TWCSA_DEBUG_DETAIL(removal);
    }

    for (int i = 0; i < additions.size(); ++i) {
        QContactDetail addition = additions.at(i);
        if (addition.type() == QContactDetail::TypeUndefined) {
            qWarning() << Q_FUNC_INFO << "invalid detail addition at index" << i << ": FIXME!";
        } else {
            newRemote.saveDetail(&addition);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("adding detail:");
        }
        QTCONTACTS_SQLITE_TWCSA_DEBUG_DETAIL(addition);
    }

    for (int i = 0; i < modifications.size(); ++i) {
        QContactDetail modification = modifications.at(i);
        if (modification.type() == QContactDetail::TypeUndefined) {
            qWarning() << Q_FUNC_INFO << "invalid detail modification at index" << i << ": FIXME!";
        } else {
            newRemote.saveDetail(&modification);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("modifying detail:");
        }
        QTCONTACTS_SQLITE_TWCSA_DEBUG_DETAIL(modification);
    }

    return newRemote;
}

// Given a list of removals and a list of additions,
// attempt to transform removal+addition pairs into modifications
// if the changes are minimal enough to be considered a modification.
QList<QContactDetail> TwoWayContactSyncAdapterPrivate::improveDelta(QList<QContactDetail> *removals, QList<QContactDetail> *additions)
{
    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("improving delta, have:" << removals->size() << "removals," << additions->size() << "additions");
    QList<QContactDetail> finalRemovals;
    QList<QContactDetail> finalAdditions;
    QList<QContactDetail> finalModifications;
    QMultiMap<int, QContactDetail> bucketedRemovals;
    QMultiMap<int, QContactDetail> bucketedAdditions;

    for (int i = 0; i < removals->size(); ++i)
        bucketedRemovals.insertMulti(removals->at(i).type(), removals->at(i));
    for (int i = 0; i < additions->size(); ++i)
        bucketedAdditions.insertMulti(additions->at(i).type(), additions->at(i));

    QSet<int> seenTypes;
    foreach (int type, bucketedRemovals.uniqueKeys()) {
        QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("dealing with detail type:" << type);
        seenTypes.insert(type);
        QList<QContactDetail> removalsOfThisType = bucketedRemovals.values(type);
        QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("have" << removalsOfThisType.size() << "removals of this type");
        QList<QContactDetail> additionsOfThisType = bucketedAdditions.values(type);
        QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("have" << additionsOfThisType.size() << "additions of this type");
        QList<QContactDetail> modificationsOfThisType = m_q->determineModifications(&removalsOfThisType, &additionsOfThisType);
        QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("have" << modificationsOfThisType.size() << "modifications of this type - and now rCount =" << removalsOfThisType.size() << ", aCount =" << additionsOfThisType.size());
        finalRemovals.append(removalsOfThisType);
        finalAdditions.append(additionsOfThisType);
        finalModifications.append(modificationsOfThisType);
    }

    foreach (int type, bucketedAdditions.uniqueKeys()) {
        if (!seenTypes.contains(type)) {
            finalAdditions.append(bucketedAdditions.values(type));
        }
    }

    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("ended up with detail a/m/r:" << finalAdditions.size() << "/" << finalModifications.size() << "/" << finalRemovals.size());

    *removals = finalRemovals;
    *additions = finalAdditions;
    return finalModifications;
}

// ----------------------------------------

TwoWayContactSyncAdapter::TwoWayContactSyncAdapter(const QString &syncTarget, const QMap<QString, QString> &params)
    : d(new TwoWayContactSyncAdapterPrivate(this, syncTarget, params))
{
}

TwoWayContactSyncAdapter::TwoWayContactSyncAdapter(const QString &syncTarget, QContactManager &manager)
    : d(new TwoWayContactSyncAdapterPrivate(this, syncTarget, manager))
{
}

TwoWayContactSyncAdapter::~TwoWayContactSyncAdapter()
{
    delete d;
}

// step one: init the sync adapter
bool TwoWayContactSyncAdapter::initSyncAdapter(const QString &accountId, const QString &oobIdentifier)
{
    TwoWayContactSyncAdapterPrivate::StateData &syncState(d->m_stateData[accountId]);

    if (syncState.m_status != TwoWayContactSyncAdapterPrivate::Inactive) {
        qWarning() << Q_FUNC_INFO << "already busy with another sync";
        return false;
    }

    if (!d->m_engine) {
        qWarning() << Q_FUNC_INFO << "no connection to qtcontacts-sqlite";
        return false;
    }

    syncState.m_oobScope = oobIdentifier.isEmpty() ? QStringLiteral("%1-%2").arg(d->m_syncTarget).arg(accountId) : oobIdentifier;
    syncState.m_status = TwoWayContactSyncAdapterPrivate::Initialized;
    return true;
}

// If mode is ReadPartialState, only the data necessary to test test for the
// existence of relevant changes is extracted immediately; otherwise all
// stored data is extracted from OOB storage immediately
bool TwoWayContactSyncAdapter::readSyncStateData(QDateTime *remoteSince, const QString &accountId, ReadStateMode mode)
{
    TwoWayContactSyncAdapterPrivate::StateData &syncState(d->m_stateData[accountId]);

    if (syncState.m_status != TwoWayContactSyncAdapterPrivate::Initialized) {
        qWarning() << Q_FUNC_INFO << "invalid state" << syncState.m_status;
        return false;
    }

    if (!d->readStateData(accountId, (mode == ReadPartialState ? TwoWayContactSyncAdapterPrivate::ReadPartial
                                                               : TwoWayContactSyncAdapterPrivate::ReadAll))) {
        qWarning() << Q_FUNC_INFO << "failed to read state data";
        return false;
    }

    // the next step is to sync down from the remote database,
    // so we can assume that we will get all changes which
    // occurred up until the current datetime.
    // thus, the next time we sync, we should use the current
    // datetime as the "since" value when retrieving changes.
    syncState.m_newRemoteSince = QDateTime::currentDateTimeUtc();

    // return the current remoteSince value for this sync.
    *remoteSince = syncState.m_remoteSince;
    syncState.m_status = TwoWayContactSyncAdapterPrivate::ReadSyncStateData;
    return true;
}

// step three: determine changes lists from the remote service.
void TwoWayContactSyncAdapter::determineRemoteChanges(const QDateTime &remoteSince, const QString &accountId)
{
    Q_UNUSED(remoteSince)
    Q_UNUSED(accountId)

    // asynchronous and implementation specific.  Once done, call storeRemoteChanges().
    // Important note: sync adapter implementations MUST ensure that the QContactId value
    // remains stable for any remote contact from one sync to the next.
    // See step six for more information.
}

// step four: store the server-side changes to the local database.
// the needToApplyDelta parameter should only be set to false if the adapter implementation
// takes care of ensuring that the detailIds are preserved when making modifications.
// Contacts passed in addModRemote with no QContactId will be modified to contain
// the QContactId value they were assigned in the local database, which must be
// preserved by the adapter when making subsequent modifications to that contact.
bool TwoWayContactSyncAdapter::storeRemoteChanges(const QList<QContact> &deletedRemote,
                                                  QList<QContact> *addModRemote,
                                                  const QString &accountId,
                                                  bool needToApplyDelta,
                                                  const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
                                                  const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields)
{
    TwoWayContactSyncAdapterPrivate::StateData &syncState(d->m_stateData[accountId]);

    if (syncState.m_status != TwoWayContactSyncAdapterPrivate::ReadSyncStateData) {
        qWarning() << Q_FUNC_INFO << "invalid state" << syncState.m_status;
        return false;
    }

    if (syncState.m_partial) {
        // We only have partial data retrieved from OOB storage; retrieve the other elements
        if (!d->readStateData(accountId, TwoWayContactSyncAdapterPrivate::ReadRemaining)) {
            qWarning() << Q_FUNC_INFO << "could not read remaining state!";
            return false;
        }
    }

    // mutate contacts from PREV_REMOTE according to the changes which occurred remotely.
    // depending on the change, the contact might need to be removed from the exportedIds list.
    // e.g., if the contact is deleted remotely, then we're no longer interested in syncing
    // that local contact up to the remote server, even if it changes on the local device in
    // the future.
    // the first value in each pair will be a copy of a value in the prevRemote list.
    // the second value in each pair will be a copy of a (newly constructed) value in the mutatedPrevRemote list.
    QList<QPair<int, int> > additionIndices;
    QList<QPair<QContact, QContact> > syncContactUpdates = d->createUpdateList(syncState,
                                                                               deletedRemote,
                                                                               addModRemote,
                                                                               &additionIndices,
                                                                               needToApplyDelta,
                                                                               ignorableDetailTypes,
                                                                               ignorableDetailFields);

    // store them to qtcontacts-sqlite.
    // Note that because of some magic in the mutation code in createUpdateList(), the
    // backend will be able to perfectly determine the change delta on a per-detail level.
    // Importantly, it is possible that the user locally deleted one of the contacts for
    // which we're trying to save modifications to - in that case, the qtcontacts-sqlite
    // backend will not write those modifications, as the contact no longer exists,
    // and the deletion of that contact will be reported in the locallyDeletedIds list
    // in the next qtcontacts-sqlite fetchSyncContacts() call (in the sixth step).
    QContactManager::Error error;
    if (syncContactUpdates.size()) {
        if (!d->m_engine->storeSyncContacts(d->m_syncTarget,
                                            QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges,
                                            &syncContactUpdates,
                                            &error)) {
            qWarning() << Q_FUNC_INFO << "error - couldn't store sync contacts!";
            d->clear(accountId);
            return false;
        }

        if (additionIndices.count()) {
            // Report IDs allocated for added contacts back to the caller, and update the contact
            // instances in the mutated prev remote array to match.  Record the fact that we added it.
            int additionIndex = syncState.m_mutatedPrevRemote.count() - additionIndices.count();
            QList<QPair<int, int> >::const_iterator it = additionIndices.constBegin(), end = additionIndices.constEnd();
            for ( ; it != end; ++it, ++additionIndex) {
                const int addModIndex((*it).first);
                const int updateIndex((*it).second);
                const QContact &addedContact(syncContactUpdates.at(updateIndex).second);
                const QContactId addedId(addedContact.id());
                (*addModRemote)[addModIndex].setId(addedId);
                syncState.m_mutatedPrevRemote[additionIndex].setId(addedId);
                if (!addedContact.detail<QContactGuid>().guid().isEmpty()) {
                    syncState.m_definitelyDownloadedAdditions.insert(addedContact.detail<QContactGuid>().guid(), addedContact);
                } else {
                    // we won't be able to track failure-case duplicates in this case
                    // but sync adapters shouldn't be providing us with guidless contacts
                    // unless they're willing to handle that mapping themselves.
                }
            }

            syncState.m_mutatedPrevRemoteModified = true;
        }

        // store partial-sync state data to OOB data so that we can avoid
        // uploading/downloading duplicates later on, if the sync partially succeeds.
        QMap<QString, QVariant> oobValues;
        QByteArray cdata;
        QDataStream write(&cdata, QIODevice::WriteOnly);
        write.setVersion(STREAM_VERSION);
        write << syncState.m_definitelyDownloadedAdditions;
        oobValues.insert(QStringLiteral("definitelyDownloadedAdditions"), QVariant(cdata));
        if (!d->m_engine->storeOOB(syncState.m_oobScope, oobValues)) {
            qWarning() << Q_FUNC_INFO << "error - couldn't store definitelyDownloadedAdditions to OOB database";
            d->clear(accountId);
            return false;
        }
    }

    syncState.m_status = TwoWayContactSyncAdapterPrivate::StoredRemoteChanges;
    return true;
}

// step five: ask qtcontacts-sqlite for changes which occurred locally since last sync.  Note: localSince is an outparam!
bool TwoWayContactSyncAdapter::determineLocalChanges(QDateTime *localSince,
                                                     QList<QContact> *locallyAdded,
                                                     QList<QContact> *locallyModified,
                                                     QList<QContact> *locallyDeleted,
                                                     const QString &accountId,
                                                     const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
                                                     const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields)
{
    TwoWayContactSyncAdapterPrivate::StateData &syncState(d->m_stateData[accountId]);

    if (syncState.m_status != TwoWayContactSyncAdapterPrivate::StoredRemoteChanges) {
        qWarning() << Q_FUNC_INFO << "invalid state" << syncState.m_status;
        return false;
    }

    // read synctarget+exported contacts which have been added/modified/removed since
    // the last local since timestamp.
    // Importantly, this WILL include the changes we made in the fifth step above.
    // This means that we need to look over the list and remove any contact which exactly
    // matches an entry in the MUTATED_PREV_REMOTE list (as there is no new changes in those
    // contacts which doesn't already exist server-side).
    // Note: if localSince isn't valid, then we're doing a clean sync, and we can
    // to pass in zero-pointers for modified/deleted list (or at least, ignore them)
    // (otherwise enabling/disabling/re-enabling an account could cause local deletions
    // to be pushed upstream, which would be very bad (tm)).
    // The backend will populate the newLocalSince timestamp appropriately.
    QContactManager::Error error;
    QList<QContactId> locallyDeletedIds;
    bool cleanSync = syncState.m_localSince.isValid() ? false : true;

    if (!d->m_engine->fetchSyncContacts(d->m_syncTarget,
                                        syncState.m_localSince,
                                        syncState.m_exportedIds,
                                        locallyModified,
                                        locallyAdded,
                                        &locallyDeletedIds,
                                        &syncState.m_newLocalSince,
                                        &error)) {
        qWarning() << Q_FUNC_INFO << "error - couldn't fetch locally modified sync contacts!";
        d->clear(accountId);
        return false;
    }

    // ensure that the added/modified/deleted lists contain the appropriate content for the current account.
    ensureAccountProvenance(locallyAdded, locallyModified, locallyDeleted,
                            syncState.m_exportedIds,
                            accountId);

    // Do we have any changes to process?
    if (!locallyDeletedIds.isEmpty() ||
        (locallyAdded && !locallyAdded->isEmpty()) ||
        (locallyModified && !locallyModified->isEmpty())) {
        // We have local changes to apply
        if (syncState.m_partial) {
            // We only have partial data retrieved from OOB storage; retrieve the other elements
            if (!d->readStateData(accountId, TwoWayContactSyncAdapterPrivate::ReadRemaining)) {
                qWarning() << Q_FUNC_INFO << "could not read remaining state!";
                return false;
            }
        }

        // Depending on the order with which the sync functions are called,
        // we may not have yet populated the MUTATED_PREV_REMOTE data from PREV_REMOTE.
        if (!syncState.m_mutated) {
            syncState.m_mutated = true;
            if (syncState.m_mutatedPrevRemoteModified) {
                qWarning() << Q_FUNC_INFO << "Overwriting existing changes in m_mutatedPrevRemote!";
            }
            syncState.m_mutatedPrevRemote = syncState.m_prevRemote;
        }

        bool uploadedAdditionsChanged = false;

        // we treat deletions/modifications differently in clean sync case.
        if (cleanSync) {
            // a) ignore these deletions.  We're clean syncing and CANNOT clobber the remote data.
            locallyDeletedIds.clear();

            // b) update our m_mutatedPrevRemote with the values in the
            //    locallyModified list and then clear locallyModified.
            if (locallyModified) {
                for (int i = 0; i < locallyModified->size(); ++i) {
                    const QContact &lmc(locallyModified->at(i));
                    const QContactId id(lmc.id());
                    if (!id.isNull()) {
                        for (int j = 0; j < syncState.m_mutatedPrevRemote.size(); ++j) {
                            if (syncState.m_mutatedPrevRemote[j].id() == id) {
                                // found matching prevRemote - replace it with the updated version.
                                syncState.m_mutatedPrevRemote.replace(j, lmc);
                                break;
                            }
                        }
                    }
                }
                locallyModified->clear();
            }
        } else {
            // a) find contacts which were deleted locally
            if (locallyDeleted) {
                foreach (const QContactId &id, locallyDeletedIds) {
                    // May not exist, if already deleted remotely
                    for (int i = 0; i < syncState.m_mutatedPrevRemote.size(); ++i) {
                        const QContact &prev(syncState.m_mutatedPrevRemote[i]);
                        if (prev.id() == id) {
                            locallyDeleted->append(prev);
                            syncState.m_mutatedPrevRemote.removeAt(i); // we are deleting this contact from the remote server.
                            break;
                        }
                    }
                }
            }

            // b) remove from the lists of changes to upsync any contact which isn't actually
            //    different to the corresponding contact in the MUTATED_PREV_REMOTE list.
            //    In that case, however, we do need to update the corresponding contact's id.
            for (int i = 0; i < syncState.m_mutatedPrevRemote.size(); ++i) {
                // check this contact to see whether it already represents one from the changes lists.
                QContact remoteContact = syncState.m_mutatedPrevRemote.at(i);
                if (locallyAdded) {
                    int matchIndex = exactContactMatchExistsInList(remoteContact, *locallyAdded, ignorableDetailTypes, ignorableDetailFields);
                    if (matchIndex != -1) {
                        remoteContact = locallyAdded->takeAt(matchIndex);
                        syncState.m_mutatedPrevRemote.replace(i, remoteContact);
                    }
                }
                if (locallyModified) {
                    int matchIndex = exactContactMatchExistsInList(remoteContact, *locallyModified, ignorableDetailTypes, ignorableDetailFields);
                    if (matchIndex != -1) {
                        remoteContact = locallyModified->takeAt(matchIndex);
                        syncState.m_mutatedPrevRemote.replace(i, remoteContact);
                    }
                }
            }
        }

        // c) any locally added contact which we intend to upsync should be added
        //    to our list of exportedIds.
        //    Note that locallyModified/locallyDeleted will only contain
        //    local-constituent-only contacts if they were identified in the
        //    exportedIds list which we passed into the fetchSyncContacts() function.
        if (locallyAdded) {
            for (int i = 0; i < locallyAdded->size(); ++i) {
                QContact localContact = locallyAdded->at(i);
                syncState.m_exportedIds.append(localContact.id());
                syncState.m_exportedIdsModified = true;

                // we remove from the local contact any guid detail.  A guid must be globally
                // unique in the database, so we couldn't store it after round-trip in the synctarget
                // constituent or we'd violate the global-uniqueness constraint.
                QContactGuid localContactGuid = localContact.detail<QContactGuid>();
                if (!localContactGuid.isEmpty()) {
                    qWarning() << Q_FUNC_INFO << "Clobbering local QContactGuid - cannot sync this detail.";
                    localContact.removeDetail(&localContactGuid);
                }

                // Now we detect whether this contact was actually already upsynced
                // during a previous (but failed) sync run.  If we don't detect this,
                // we could erroneously upsync a duplicate.
                if (syncState.m_reportedUploadedAdditions.contains(localContact.id())) {
                    // This contact has been previously upsynced.
                    locallyAdded->removeAt(i);
                    i--;

                    // If the contact was already deleted remotely, don't treat it as an addition or modification.
                    if (syncState.m_reportedUploadedAdditions.value(localContact.id()) != QContact()) {
                        // Determine whether the version which was upsynced differs from the
                        // version which is being reported now.  If it does not differ, then
                        // we can ignore it (already uploaded).  If it does differ, then we
                        // must treat it as a modification.
                        if (localContact == syncState.m_possiblyUploadedAdditions.value(localContact.id())) {
                            // already uploaded this one as-is.  we can ignore this addition.
                        } else {
                            // already uploaded, but it was modified locally since we uploaded it.
                            if (locallyModified) {
                                locallyModified->append(localContact);
                            }
                        }
                    } else {
                        // This codepath shouldn't ever be hit in reality, as the contact should have been deleted locally
                        // due to applying the remote (deletion) update in a previous step.
                        qWarning() << Q_FUNC_INFO << "reported addition of local contact which was deleted server-side!";
                    }
                } else {
                    // This is a newly added contact which has not yet been upsynced.
                    locallyAdded->replace(i, localContact);
                    syncState.m_possiblyUploadedAdditions.insert(localContact.id(), localContact);
                    uploadedAdditionsChanged = true;
                }
            }
        }

        // d) now further mutate MUTATED_PREV_REMOTE to include the local changes which we
        //    are just about to upsync to the remote server.
        if (locallyAdded) {
            for (int i = 0; i < locallyAdded->size(); ++i) {
                syncState.m_mutatedPrevRemote.append(locallyAdded->at(i));
            }
        }
        if (locallyModified) {
            bool foundToReplace = false;
            for (int i = 0; i < locallyModified->size(); ++i) {
                const QContactId &lid(locallyModified->at(i).id());
                foundToReplace = false;
                for (int j = 0; j < syncState.m_mutatedPrevRemote.size(); ++j) {
                    const QContactId &pid(syncState.m_mutatedPrevRemote[j].id());
                    if (pid == lid) {
                        foundToReplace = true;
                        syncState.m_mutatedPrevRemote.replace(j, locallyModified->at(i));
                        break;
                    }
                }

                if (!foundToReplace) {
                    if (syncState.m_remotelyDeletedThisSync.contains(lid)) {
                        // local modification reported for remotely deleted contact.
                        // we shouldn't get here, since the storeRemoteChanges() function should result in
                        // the contact being removed from the local database, but if we do, don't upsync
                        // the spurious modification.
                        qWarning() << Q_FUNC_INFO << "ignoring local modification reported for remotely deleted contact:" << lid;
                    } else {
                        // we shouldn't treat this as a local addition, this is always a bug.
                        qWarning() << Q_FUNC_INFO << "FIXME: local modification reported for non-upsynced local contact:" << lid;
                    }
                }
            }
        }

        if (uploadedAdditionsChanged) {
            // store partial-sync state data to OOB data so that we can avoid
            // uploading/downloading duplicates later on, if the sync partially succeeds.
            QMap<QString, QVariant> oobValues;
            QByteArray cdata;
            QDataStream write(&cdata, QIODevice::WriteOnly);
            write.setVersion(STREAM_VERSION);
            write << syncState.m_possiblyUploadedAdditions;
            oobValues.insert(QStringLiteral("possiblyUploadedAdditions"), QVariant(cdata));
            if (!d->m_engine->storeOOB(syncState.m_oobScope, oobValues)) {
                qWarning() << Q_FUNC_INFO << "error - couldn't store possiblyUploadedAdditions to OOB database";
                d->clear(accountId);
                return false;
            }
        }

        syncState.m_mutatedPrevRemoteModified = true;
    }

    // done.
    syncState.m_status = TwoWayContactSyncAdapterPrivate::DeterminedLocalChanges;
    *localSince = syncState.m_localSince;
    return true;
}

// step six: sync the changes up to the remote server.
void TwoWayContactSyncAdapter::upsyncLocalChanges(const QDateTime &localSince,
                                                  const QList<QContact> &locallyAdded,
                                                  const QList<QContact> &locallyModified,
                                                  const QList<QContact> &locallyDeleted,
                                                  const QString &accountId)
{
    Q_UNUSED(localSince)
    Q_UNUSED(locallyAdded)
    Q_UNUSED(locallyModified)
    Q_UNUSED(locallyDeleted)
    Q_UNUSED(accountId)

    // asynchronous and implementation specific.  when finished, call storeSyncStateData().
    // Important note: sync adapter implementations MUST ensure that the QContactId
    // of the local contact is used to map to the correct remote instance of the contact.
    // See step three for more information.
}

// step seven: store state data to qtcontacts-sqlite oob database.
bool TwoWayContactSyncAdapter::storeSyncStateData(const QString &accountId)
{
    TwoWayContactSyncAdapterPrivate::StateData &syncState(d->m_stateData[accountId]);

    if (syncState.m_status != TwoWayContactSyncAdapterPrivate::DeterminedLocalChanges) {
        qWarning() << Q_FUNC_INFO << "invalid state" << syncState.m_status;
        return false;
    }

    QMap<QString, QVariant> values;

    if (syncState.m_mutatedPrevRemoteModified) {
        // store the MUTATED_PREV_REMOTE list into oob as PREV_REMOTE for next time.
        QByteArray cdata;
        QDataStream write(&cdata, QIODevice::WriteOnly);
        write.setVersion(STREAM_VERSION);
        write << syncState.m_mutatedPrevRemote;
        values.insert(QStringLiteral("prevRemote"), QVariant(cdata));
    }

    if (syncState.m_exportedIdsModified) {
        // also store the EXPORTED_IDS list into oob to track non-synctarget contacts we upsynced.
        QByteArray cdata;
        QDataStream writeExportedIds(&cdata, QIODevice::WriteOnly);
        writeExportedIds.setVersion(STREAM_VERSION);
        writeExportedIds << syncState.m_exportedIds;
        values.insert(QStringLiteral("exportedIds"), QVariant(cdata));
    }

    // clear the values of the partial-sync-state maps as the sync was
    // successful and so we don't need to track partial-sync artifacts.
    values.insert(QStringLiteral("possiblyUploadedAdditions"), QVariant::fromValue<QByteArray>(QByteArray()));
    values.insert(QStringLiteral("definitelyDownloadedAdditions"), QVariant::fromValue<QByteArray>(QByteArray()));

    // finally, store the new local and remote since timestamps to the OOB db
    values.insert(QStringLiteral("remoteSince"), QVariant(dateTimeString(syncState.m_newRemoteSince)));
    values.insert(QStringLiteral("localSince"), QVariant(dateTimeString(syncState.m_newLocalSince)));

    // perform the store operation to the oob db.
    if (!d->m_engine->storeOOB(syncState.m_oobScope, values)) {
        qWarning() << Q_FUNC_INFO << "error - couldn't store sync state data to oob!";
        d->clear(accountId);
        return false;
    }

    // finished the sync process successfully.
    syncState.m_status = TwoWayContactSyncAdapterPrivate::Finished;
    d->clear(accountId); // this actually sets state back to Inactive as required.
    return true;
}

// error case: purge state data from qtcontacts-sqlite oob database.
bool TwoWayContactSyncAdapter::purgeSyncStateData(const QString &accountId, bool purgePartialSyncStateData)
{
    TwoWayContactSyncAdapterPrivate::StateData &syncState(d->m_stateData[accountId]);

    bool purgeSucceeded = true;
    QStringList purgeKeys;
    purgeKeys << QStringLiteral("prevRemote") << QStringLiteral("exportedIds");
    purgeKeys << QStringLiteral("remoteSince") << QStringLiteral("localSince");

    if (purgePartialSyncStateData) { // false by default.
        // The partial sync state data maps should only be cleared if the sync
        // was successful (or if the account is being totally purged), otherwise
        // we might upsync/downsync a single contact addition twice.
        purgeKeys << QStringLiteral("possiblyUploadedAdditions");
        purgeKeys << QStringLiteral("definitelyDownloadedAdditions");
    }

    // If this function is called without first calling initSyncAdapter, the
    // OOB scope variable will not be initialized.  In that case, try to use
    // the default OOB scope instead.
    if (syncState.m_oobScope.isEmpty()) {
        qWarning() << Q_FUNC_INFO << "error - cannot purge sync state data for uninitialized adapter";
        purgeSucceeded = false;
    } else if (!d->m_engine->removeOOB(syncState.m_oobScope, purgeKeys)) {
        qWarning() << Q_FUNC_INFO << "error - couldn't purge state data from oob!";
        purgeSucceeded = false;
    }

    d->clear(accountId);
    return purgeSucceeded;
}

bool TwoWayContactSyncAdapter::removeAllContacts()
{
    QContactDetailFilter syncTargetFilter;
    syncTargetFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    syncTargetFilter.setValue(d->m_syncTarget);

    QContactDetailFilter typeFilter;
    typeFilter.setDetailType(QContactType::Type, QContactType::FieldType);
    typeFilter.setValue(QContactType::TypeContact);

    // Remove all contacts stored for our syncTarget
    QList<QContactId> syncTargetIds(d->m_manager->contactIds(syncTargetFilter & typeFilter));
    if (!syncTargetIds.isEmpty()) {
        if (!d->m_manager->removeContacts(syncTargetIds)) {
            qWarning() << Q_FUNC_INFO << "error - couldn't remove" << syncTargetIds.count() << "existing contacts!";
            return false;
        }
    }

    return true;
}

const QContactManager &TwoWayContactSyncAdapter::contactManager() const
{
    return *d->m_manager;
}

QContactManager &TwoWayContactSyncAdapter::contactManager()
{
    return *d->m_manager;
}

// ------------------------------------------------------------

// Override this function to specify whether a contact belongs to the supplied
// account; multi-account adapters must implement this feature.
bool TwoWayContactSyncAdapter::testAccountProvenance(const QContact &contact, const QString &accountId)
{
    Q_UNUSED(contact)
    Q_UNUSED(accountId)
    return true;
}

// The function is called to remove from the lists of local changes which
// are reported by the qtcontacts-sqlite backend any contacts which are
// not from the correct account.
// Implementations can override this function if they store such information
// in a different way; the default implementation calls testAccountProvenance().
void TwoWayContactSyncAdapter::ensureAccountProvenance(QList<QContact> *locallyAdded,
                                                       QList<QContact> *locallyModified,
                                                       QList<QContact> *locallyDeleted,
                                                       const QList<QContactId> &exportedIds,
                                                       const QString &accountId)
{
    // By default, added contacts are considered relevant to all accounts
    Q_UNUSED(locallyAdded)

    // For deletions and modifications, we check to see if each contact belongs to the account.
    // If not, we remove it from the list (as it was never in this account). Contacts listed
    // in exportedIds are explicitly claimed by the adapter for this account.
    for (int i = locallyDeleted->size() - 1; i >= 0; --i) {
        const QContact &contact(locallyDeleted->at(i));
        if (!exportedIds.contains(contact.id()) && !testAccountProvenance(contact, accountId)) {
            locallyDeleted->removeAt(i);
        }
    }
    for (int i = locallyModified->size() - 1; i >= 0; --i) {
        const QContact &contact(locallyModified->at(i));
        if (!exportedIds.contains(contact.id()) && !testAccountProvenance(contact, accountId)) {
            locallyModified->removeAt(i);
        }
    }
}

// Note: this implementation can be overridden if the sync adapter knows
// more about how to determine modifications (eg persistent detail ids)
QList<QContactDetail> TwoWayContactSyncAdapter::determineModifications(QList<QContactDetail> *removalsOfThisType, QList<QContactDetail> *additionsOfThisType) const
{
    QList<QContactDetail> modifications;
    QList<QContactDetail> finalRemovals;
    QList<QContactDetail> finalAdditions;

    QList<QPair<int, int> > permutationsOfIndexes;
    QMap<int, int> scoresForPermutations;

    QList<int> remainingRemovals;
    QList<int> remainingAdditions;
    for (int i = 0; i < additionsOfThisType->size(); ++i) {
        remainingAdditions.append(i);
    }

    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("determining modifications from the given list of additions/removals for details of a particular type");

    // for each possible permutation, determine its score.
    // lower is a closer match (ie, score == distance).
    for (int i = 0; i < removalsOfThisType->size(); ++i) {
        remainingRemovals.append(i);
        for (int j = 0; j < additionsOfThisType->size(); ++j) {
            // determine the score for the permutation
            int score = scoreForDetailPair(removalsOfThisType->at(i),
                                           additionsOfThisType->at(j));
            scoresForPermutations.insert(permutationsOfIndexes.size(), score);
            permutationsOfIndexes.append(QPair<int, int>(i, j));
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("score for permutation" << i << "," << j << "=" << score);
        }
    }

    while (remainingRemovals.size() > 0 && remainingAdditions.size() > 0) {
        int lowScore = 1000;
        int lowScorePermutationIdx = -1;

        foreach (int permutationIdx, scoresForPermutations.keys()) {
            QPair<int, int> permutation = permutationsOfIndexes.at(permutationIdx);
            if (remainingRemovals.contains(permutation.first)
                    && remainingAdditions.contains(permutation.second)) {
                // this permutation is still "possible".
                if (scoresForPermutations.value(permutationIdx) < lowScore) {
                    lowScorePermutationIdx = permutationIdx;
                    lowScore = scoresForPermutations.value(permutationIdx);
                }
            }
        }

        if (lowScorePermutationIdx != -1) {
            // we have a valid permutation which should be treated as a modification.
            QPair<int, int> bestPermutation = permutationsOfIndexes.at(lowScorePermutationIdx);
            QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG("have determined that permutation" << bestPermutation.first << "," << bestPermutation.second << "is a modification");
            remainingRemovals.removeAll(bestPermutation.first);
            remainingAdditions.removeAll(bestPermutation.second);
            QContactDetail modification = removalsOfThisType->at(bestPermutation.first);
            QContactDetail finalValue = additionsOfThisType->at(bestPermutation.second);
            QMap<int, QVariant> values = finalValue.values();
            foreach (int field, values.keys()) {
                modification.setValue(field, values.value(field));
            }
            QMap<int, QVariant> possiblyStaleValues = modification.values();
            foreach (int field, possiblyStaleValues.keys()) {
                if (!values.contains(field)
                        && field != QContactDetail__FieldProvenance // maintain provenance for modifications
                        && field != QContactDetail::FieldDetailUri
                        && field != QContactDetail::FieldLinkedDetailUris) {
                    modification.removeValue(field);
                }
            }
            modifications.append(modification);
        }
    }

    // rebuild the return values, removing the permutations which were applied as modifications.
    foreach (int idx, remainingRemovals)
        finalRemovals.append(removalsOfThisType->at(idx));
    foreach (int idx, remainingAdditions)
        finalAdditions.append(additionsOfThisType->at(idx));

    // and return.
    *removalsOfThisType = finalRemovals;
    *additionsOfThisType = finalAdditions;
    return modifications;
}

#endif

