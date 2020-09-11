/*
 * Copyright (C) 2014 - 2016 Jolla Ltd.
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

#ifndef CONTACTDELTA_IMPL_H
#define CONTACTDELTA_IMPL_H

#include <contactdelta.h>
#include <qtcontacts-extensions.h>
#include <contactmanagerengine.h>

#include <QDebug>

#define QTCONTACTS_SQLITE_DELTA_DEBUG_LOG(msg)                           \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_delta_debug_trace_enabled())) { \
            qDebug() << msg;                                             \
        }                                                                \
    } while (0)

#define QTCONTACTS_SQLITE_DELTA_DEBUG_DETAIL(detail)                     \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_delta_debug_trace_enabled())) { \
            qWarning() << "++ ---------" << detail.type();               \
            QMap<int, QVariant> values = detail.values();                \
            foreach (int key, values.keys()) {                           \
                qWarning() << "    " << key << "=" << values.value(key); \
            }                                                            \
        }                                                                \
    } while (0)

QTCONTACTS_USE_NAMESPACE
using namespace QtContactsSqliteExtensions;

namespace {

static bool qtcontacts_sqlite_delta_debug_trace_enabled()
{
    static const bool traceEnabled(!QString(QLatin1String(qgetenv("QTCONTACTS_SQLITE_DELTA_TRACE"))).isEmpty());
    return traceEnabled;
}

QSet<QContactDetail::DetailType> getDefaultIgnorableDetailTypes()
{
    // these details are either read-only or composed.
    // sync adapters may wish to add transient details here also, i.e.:
    // rv.insert(QContactDetail::TypeGlobalPresence);
    // rv.insert(QContactDetail::TypePresence);
    // other candidates to ignore include:
    // rv.insert(QContactDetail::TypeOnlineAccount);
    // rv.insert(QContactDetail::TypeDisplayLabel);
    // rv.insert(QContactDetail::TypeTimestamp);
    QSet<QContactDetail::DetailType> rv;
    rv.insert(QContactDetail__TypeDeactivated);
    rv.insert(QContactDetail__TypeStatusFlags);
    return rv;
}

QHash<QContactDetail::DetailType, QSet<int> > getDefaultIgnorableDetailFields()
{
    QHash<QContactDetail::DetailType, QSet<int> > rv;
    rv.insert(QContactDetail::TypePhoneNumber, { QContactPhoneNumber::FieldNormalizedNumber });
    // Clients can specify their own ignorable fields depending on the semantics of their
    // sync service (eg, might not be able to handle some subtypes or contexts, etc)
    return rv;
}

QSet<int> getDefaultIgnorableCommonFields()
{
    return {
        QContactDetail::FieldProvenance,
        QContactDetail__FieldModifiable,
        QContactDetail__FieldNonexportable,
        QContactDetail__FieldChangeFlags,
        QContactDetail__FieldDatabaseId
    };
}

void removeIgnorableDetailsFromList(QList<QContactDetail> *dets, const QSet<QContactDetail::DetailType> &ignorableDetailTypes)
{
    // ignore differences in certain detail types
    for (int i = dets->size() - 1; i >= 0; --i) {
        const QContactDetail::DetailType type(dets->at(i).type());
        if (ignorableDetailTypes.contains(type)) {
            dets->removeAt(i); // we can ignore this detail altogether
        }
    }
}

void removeDatabaseIdsFromList(QList<QContactDetail> *dets)
{
    for (QList<QContactDetail>::iterator it = dets->begin(); it != dets->end(); ++it) {
        it->removeValue(QContactDetail__FieldDatabaseId);
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

// Given two details of the same type, determine a similarity score for them.
int scoreForDetailPair(
        const QContactDetail &removal,
        const QContactDetail &addition,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        const QSet<int> &ignorableCommonFields)
{
    int score = 0; // distance
    QMap<int, QVariant> rvalues = removal.values();
    QMap<int, QVariant> avalues = addition.values();

    QList<int> seenFields;
    foreach (int field, rvalues.keys()) {
        if (ignorableCommonFields.contains(field)
                || ignorableDetailFields.value(removal.type()).contains(field)) {
            continue;
        }
        seenFields.append(field);
        score += scoreForValuePair(rvalues.value(field), avalues.value(field));
    }

    foreach (int field, avalues.keys()) {
        if (seenFields.contains(field)
                || ignorableCommonFields.contains(field)
                || ignorableDetailFields.value(addition.type()).contains(field)) {
            continue;
        }
        score += scoreForValuePair(rvalues.value(field), avalues.value(field));
    }

    return score;
}

bool detailPairExactlyMatches(
        const QContactDetail &a,
        const QContactDetail &b,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        const QSet<int> &ignorableCommonFields,
        bool printDifferences = false)
{
    if (a.type() != b.type()) {
        return false;
    }

    // now ensure that all values match
    QMap<int, QVariant> avalues = a.values();
    QMap<int, QVariant> bvalues = b.values();
    foreach (int akey, avalues.keys()) {
        if (ignorableCommonFields.contains(akey)
                || ignorableDetailFields.value(a.type()).contains(akey)) {
            continue;
        }

        const QVariant avalue = avalues.value(akey);
        if (!bvalues.contains(akey)) {
            // this may still be ok if the avalue is NULL
            // or if the avalue is an empty string, or empty list,
            // as the database can sometimes return empty
            // string instead of NULL value.
            if (avalue.type() == QVariant::Invalid
                    || (avalue.type() == QVariant::String && avalue.toString().isEmpty())
                    || (avalue.userType() == QMetaType::type("QList<int>") && avalue.value<QList<int> >() == QList<int>())) {
                // this is ok.
            } else {
                // a has a real value which b does not have.
                if (Q_UNLIKELY(printDifferences)) {
                    QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("detail A of type" << a.type() << "has value which B does not have:" << akey << "=" << avalue);
                }
                return false;
            }
        } else {
            // b contains the same key, but do the values match?
            if (scoreForValuePair(avalue, bvalues.value(akey)) != 0) {
                if (Q_UNLIKELY(printDifferences)) {
                    QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("detail A of type" << a.type() << "has value which differs from B:" << akey << "=" << avalue << "!=" << bvalues.value(akey));
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
        if (ignorableCommonFields.contains(bkey)
                || ignorableDetailFields.value(b.type()).contains(bkey)) {
            continue;
        }

        const QVariant bvalue = bvalues.value(bkey);
        if (bvalue.type() == QVariant::Invalid
                || (bvalue.type() == QVariant::String && bvalue.toString().isEmpty())
                || (bvalue.userType() == QMetaType::type("QList<int>") && bvalue.value<QList<int> >() == QList<int>())) {
            // this is ok.
        } else {
            // b has a real value which a does not have.
            if (Q_UNLIKELY(printDifferences)) {
                QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("detail B of type" << b.type() << "has value which A does not have:" << bkey << "=" << bvalue);
            }
            return false;
        }
    }

    return true;
}

int exactDetailMatchExistsInList(
        const QContactDetail &det,
        const QList<QContactDetail> &list,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        QSet<int> ignorableCommonFields,
        bool printDifferences)
{
    for (int i = 0; i < list.size(); ++i) {
        if (detailPairExactlyMatches(det, list[i], ignorableDetailFields, ignorableCommonFields, printDifferences)) {
            return i; // exact match at this index.
        }
    }

    return -1;
}

bool contactDetailsMatchExactly(
        const QList<QContactDetail> &aDetails,
        const QList<QContactDetail> &bDetails,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        QSet<int> ignorableCommonFields,
        bool printDifferences = false)
{
    // for it to be an exact match:
    // a) every detail in aDetails must exist in bDetails
    // b) no extra details can exist in bDetails
    if (aDetails.size() != bDetails.size()) {
        if (Q_UNLIKELY(printDifferences)) {
            // detail count differs, and continue the analysis to find out precisely what the differences are.
            QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("A has more details than B:" << aDetails.size() << ">" << bDetails.size());
        } else {
            // detail count differs, return immediately.
            return false;
        }
    }

    QList<QContactDetail> nonMatchedADetails;
    QList<QContactDetail> nonMatchedBDetails = bDetails;
    bool allADetailsHaveMatches = true;
    foreach (const QContactDetail &aDetail, aDetails) {
        int exactMatchIndex = exactDetailMatchExistsInList(
                aDetail, nonMatchedBDetails, ignorableDetailFields,
                ignorableCommonFields, false);
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
                    QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("Detail modified from A to B:");
                    detailPairExactlyMatches(ad, bd, ignorableDetailFields, ignorableCommonFields, printDifferences);
                    nonMatchedBDetails.removeAt(i);
                    break;
                }
            }
            if (!foundMatch) {
                QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("New detail exists in contact A:");
                QTCONTACTS_SQLITE_DELTA_DEBUG_DETAIL(ad);
            }
        }
        Q_FOREACH (const QContactDetail &bd, nonMatchedBDetails) {
            QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("New detail exists in contact B:");
            QTCONTACTS_SQLITE_DELTA_DEBUG_DETAIL(bd);
        }
    }

    return false;
}

// move some information (modifiable, detail uris, provenance, database id)
// from the old detail to the new detail.
void constructModification(const QContactDetail &old, QContactDetail *update)
{
    const QMap<int, QVariant> values = update->values();
    const QMap<int, QVariant> oldValues = old.values();
    for (int field : oldValues.keys()) {
        if (field == QContactDetail__FieldDatabaseId
                || (!values.contains(field)
                    && field == QContactDetail__FieldModifiable
                    && field == QContactDetail::FieldProvenance
                    && field == QContactDetail::FieldDetailUri
                    && field == QContactDetail::FieldLinkedDetailUris)) {
            update->setValue(field, oldValues.value(field));
        }
    }
}

// Note: this implementation can be overridden if the sync adapter knows
// more about how to determine modifications (eg persistent detail ids)
QList<QContactDetail> determineModifications(
        QList<QContactDetail> *removalsOfThisType,
        QList<QContactDetail> *additionsOfThisType,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        const QSet<int> &ignorableCommonFields)
{
    QList<QContactDetail> modifications;
    QList<QContactDetail> finalRemovals;
    QList<QContactDetail> finalAdditions;

    QList<QPair<int, int> > permutationsOfIndexes;
    QHash<int, int> scoresForPermutations;

    QList<int> remainingRemovals;
    QList<int> remainingAdditions;
    for (int i = 0; i < additionsOfThisType->size(); ++i) {
        remainingAdditions.append(i);
    }

    QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("determining modifications from the given list of additions/removals for details of a particular type");

    // for each possible permutation, determine its score.
    // lower is a closer match (ie, score == distance).
    for (int i = 0; i < removalsOfThisType->size(); ++i) {
        remainingRemovals.append(i);
        for (int j = 0; j < additionsOfThisType->size(); ++j) {
            // determine the score for the permutation
            int score = scoreForDetailPair(removalsOfThisType->at(i),
                                           additionsOfThisType->at(j),
                                           ignorableDetailFields,
                                           ignorableCommonFields);
            scoresForPermutations.insert(permutationsOfIndexes.size(), score);
            permutationsOfIndexes.append(QPair<int, int>(i, j));
            QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("score for permutation" << i << "," << j << "=" << score);
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
            QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("have determined that permutation" << bestPermutation.first << "," << bestPermutation.second << "is a modification");
            remainingRemovals.removeAll(bestPermutation.first);
            remainingAdditions.removeAll(bestPermutation.second);
            const QContactDetail old = removalsOfThisType->at(bestPermutation.first);
            QContactDetail update = additionsOfThisType->at(bestPermutation.second);
            constructModification(old, &update);
            modifications.append(update);
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

// Given a list of removals and a list of additions,
// attempt to transform removal+addition pairs into modifications
// if the changes are minimal enough to be considered a modification.
QList<QContactDetail> improveDelta(
        QList<QContactDetail> *removals,
        QList<QContactDetail> *additions,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        const QSet<int> &ignorableCommonFields)
{
    QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("improving delta, have:" << removals->size() << "removals," << additions->size() << "additions");
    QList<QContactDetail> finalRemovals;
    QList<QContactDetail> finalAdditions;
    QList<QContactDetail> finalModifications;
    QMultiHash<int, QContactDetail> bucketedRemovals;
    QMultiHash<int, QContactDetail> bucketedAdditions;

    for (int i = 0; i < removals->size(); ++i)
        bucketedRemovals.insertMulti(removals->at(i).type(), removals->at(i));
    for (int i = 0; i < additions->size(); ++i)
        bucketedAdditions.insertMulti(additions->at(i).type(), additions->at(i));

    QSet<int> seenTypes;
    foreach (int type, bucketedRemovals.uniqueKeys()) {
        QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("dealing with detail type:" << type);
        seenTypes.insert(type);
        QList<QContactDetail> removalsOfThisType = bucketedRemovals.values(type);
        QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("have" << removalsOfThisType.size() << "removals of this type");
        QList<QContactDetail> additionsOfThisType = bucketedAdditions.values(type);
        QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("have" << additionsOfThisType.size() << "additions of this type");
        QList<QContactDetail> modificationsOfThisType = determineModifications(&removalsOfThisType, &additionsOfThisType, ignorableDetailFields, ignorableCommonFields);
        QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("have" << modificationsOfThisType.size() << "modifications of this type - and now rCount =" << removalsOfThisType.size() << ", aCount =" << additionsOfThisType.size());
        finalRemovals.append(removalsOfThisType);
        finalAdditions.append(additionsOfThisType);
        finalModifications.append(modificationsOfThisType);
    }

    foreach (int type, bucketedAdditions.uniqueKeys()) {
        if (!seenTypes.contains(type)) {
            finalAdditions.append(bucketedAdditions.values(type));
        }
    }

    QTCONTACTS_SQLITE_DELTA_DEBUG_LOG("ended up with detail a/m/r:" << finalAdditions.size() << "/" << finalModifications.size() << "/" << finalRemovals.size());

    *removals = finalRemovals;
    *additions = finalAdditions;
    return finalModifications;
}

typedef QMap<int, QVariant> DetailMap;

DetailMap detailValues(const QContactDetail &detail, bool includeProvenance = true, bool includeModifiable = true)
{
    DetailMap rv(detail.values());

    if (!includeProvenance || !includeModifiable) {
        DetailMap::iterator it = rv.begin();
        while (it != rv.end()) {
            if (!includeProvenance && it.key() == QContactDetail::FieldProvenance) {
                it = rv.erase(it);
            } else if (!includeModifiable && it.key() == QContactDetail__FieldModifiable) {
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
    const DetailMap lhsValues(detailValues(lhs, false, false));
    const DetailMap rhsValues(detailValues(rhs, false, false));

    if (lhsValues.count() != rhsValues.count()) {
        return false;
    }

    // Because of map ordering, matching fields should be in the same order in both details
    DetailMap::const_iterator lit = lhsValues.constBegin(), lend = lhsValues.constEnd();
    DetailMap::const_iterator rit = rhsValues.constBegin();
    for ( ; lit != lend; ++lit, ++rit) {
        if (lit.key() != rit.key() || !variantEqual(*lit, *rit)) {
            return false;
        }
    }

    return true;
}

static bool detailsEquivalent(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // Same as operator== except ignores differences in certain field values
    if (lhs.type() != rhs.type())
        return false;
    return detailValuesEqual(lhs, rhs);
}

} // namespace

const QSet<QContactDetail::DetailType>& QtContactsSqliteExtensions::defaultIgnorableDetailTypes()
{
    static QSet<QContactDetail::DetailType> types(getDefaultIgnorableDetailTypes());
    return types;
}

const QHash<QContactDetail::DetailType, QSet<int> >& QtContactsSqliteExtensions::defaultIgnorableDetailFields()
{
    static QHash<QContactDetail::DetailType, QSet<int> > fields(getDefaultIgnorableDetailFields());
    return fields;
}

const QSet<int>& QtContactsSqliteExtensions::defaultIgnorableCommonFields()
{
    static QSet<int> fields(getDefaultIgnorableCommonFields());
    return fields;
}

ContactDetailDelta QtContactsSqliteExtensions::determineContactDetailDelta(
        const QList<QContactDetail> &oldDetails,
        const QList<QContactDetail> &newDetails,
        const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        const QSet<int> &ignorableCommonFields)
{
    ContactDetailDelta delta;

    QList<QContactDetail> odets = oldDetails;
    QList<QContactDetail> ndets = newDetails;

    // XXXXXXXXX TODO: ensure Unique details (Guid / Name / etc) are unique
    removeIgnorableDetailsFromList(&odets, ignorableDetailTypes);
    removeIgnorableDetailsFromList(&ndets, ignorableDetailTypes);

    // ignore all exact matches, as they don't form part of the delta
    for (int i = odets.size() - 1; i >= 0; --i) {
        bool found = false;
        for (int j = ndets.size() - 1; j >= 0; --j) {
            if (detailPairExactlyMatches(odets.at(i), ndets.at(j),
                                         ignorableDetailFields,
                                         ignorableCommonFields)) {
                // found an exact match; this detail hasn't changed
                // or is a duplicate of an existing detail (in the
                // case where multiple constituents of an aggregate
                // have some identical details).
                ndets.removeAt(j);
                found = true;
            }
        }
        if (found) {
            odets.removeAt(i);
        }
    }

    // determine direct modifications by matching database id
    for (int i = odets.size() - 1; i >= 0; --i) {
        int idx = -1;
        const quint32 oDbId = odets.at(i).value(QContactDetail__FieldDatabaseId).toUInt();
        const QContactDetail::DetailType otype = odets.at(i).type();
        for (int j = ndets.size() - 1; j >= 0; --j) {
            const quint32 nDbId = ndets.at(j).value(QContactDetail__FieldDatabaseId).toUInt();
            const QContactDetail::DetailType ntype = ndets.at(j).type();
            if (oDbId > 0 && oDbId == nDbId && otype == ntype) {
                idx = j;
                break;
            }
        }
        if (idx != -1) {
            // found a direct modification.
            QContactDetail update = ndets.at(idx);
            constructModification(odets.at(i), &update);
            delta.modifications.append(update);
            odets.removeAt(i);
            ndets.removeAt(idx);
        }
    }

    // now determine which pairs of old+new details should be considered modifications
    delta.modifications.append(improveDelta(&odets, &ndets, ignorableDetailFields, ignorableCommonFields));
    delta.deletions = odets;
    // any detail addition requires a new/clean database id.
    removeDatabaseIdsFromList(&ndets);
    delta.additions = ndets;
    delta.isValid = true;

    return delta;
}

int QtContactsSqliteExtensions::exactContactMatchExistsInList(
        const QContact &aContact,
        const QList<QContact> &list,
        const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
        const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
        const QSet<int> &ignorableCommonFields,
        bool printDifferences)
{
    QList<QContactDetail> aDetails = aContact.details();
    removeIgnorableDetailsFromList(&aDetails, ignorableDetailTypes);
    for (int i = 0; i < list.size(); ++i) {
        QList<QContactDetail> bDetails = list[i].details();
        removeIgnorableDetailsFromList(&bDetails, ignorableDetailTypes);
        if (contactDetailsMatchExactly(aDetails, bDetails, ignorableDetailFields, ignorableCommonFields, printDifferences)) {
            return i; // exact match at this index.
        }
    }

    return -1;
}

#endif // CONTACTDELTA_IMPL_H

