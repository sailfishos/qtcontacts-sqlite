/*
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#ifndef QCONTACTSEARCHFILTERREQUEST_IMPL_H
#define QCONTACTSEARCHFILTERREQUEST_IMPL_H

#include "./qcontactsearchfilterrequest_p.h"
#include "./contactmanagerengine.h"
#include "./qtcontacts-extensions_manager_impl.h"

#include <QContactFetchRequest>
#include <QContactDetailFilter>

#include <QContactName>
#include <QContactEmailAddress>
#include <QContactPhoneNumber>
#include <QContactOrganization>
#include <QContactDisplayLabel>

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

QContactSearchFilterRequest::QContactSearchFilterRequest(QObject *parent)
    : QObject(parent)
    , d_ptr(new QContactSearchFilterRequestPrivate(
                this,
                &QContactSearchFilterRequest::stateChanged,
                &QContactSearchFilterRequest::resultsAvailable))
{
}

QContactSearchFilterRequest::~QContactSearchFilterRequest()
{
}

QContactManager *QContactSearchFilterRequest::manager() const
{
    return d_ptr->manager.data();
}

void QContactSearchFilterRequest::setManager(QContactManager *manager)
{
    d_ptr->manager = manager;
}

QList<QContactSearchFilterRequest::SearchFilter> QContactSearchFilterRequest::searchFilters() const
{
    return d_ptr->searchFilters;
}

void QContactSearchFilterRequest::setSearchFilters(const QList<SearchFilter> &filters)
{
    QList<SearchFilter> sanitised;
    for (const SearchFilter &filter : filters) {
        if (!filter.fields.size()) {
            qWarning() << "Ignoring invalid search filter with empty fields list";
            continue;
        }
        sanitised.append(filter);
    }

    d_ptr->searchFilters = sanitised;
}

QString QContactSearchFilterRequest::searchFilterValue() const
{
    return d_ptr->searchFilterValue;
}

void QContactSearchFilterRequest::setSearchFilterValue(const QString &value)
{
    d_ptr->searchFilterValue = value;
}

QContactFetchHint QContactSearchFilterRequest::fetchHint() const
{
    return d_ptr->hint;
}

void QContactSearchFilterRequest::setFetchHint(const QContactFetchHint &hint)
{
    d_ptr->hint = hint;
}

QContactAbstractRequest::State QContactSearchFilterRequest::state() const
{
    return d_ptr->state;
}

QContactManager::Error QContactSearchFilterRequest::error() const
{
    return d_ptr->error;
}

QList<QContact> QContactSearchFilterRequest::contacts() const
{
    return d_ptr->contacts;
}

bool QContactSearchFilterRequest::start()
{
    if (d_ptr->state == QContactAbstractRequest::ActiveState) {
        // Already executing.
    } else if (!d_ptr->manager) {
        // No manager.
    } else {
        // Start the request.
        d_ptr->requests.clear();
        d_ptr->requestResultsHandled.clear();
        d_ptr->seenContacts.clear();
        d_ptr->isCanceled = false;

        if (d_ptr->searchFilterValue.isEmpty()
                || d_ptr->searchFilters.isEmpty()) {
            // no filter values, cannot start the request
            return false;
        }

        for (const SearchFilter &filter : d_ptr->searchFilters) {
            // start a QContactFetchRequest based on this filter.
            QContactFetchRequest *r = new QContactFetchRequest(this);
            bool haveSetFilter = false;
            r->setManager(d_ptr->manager);
            r->setFetchHint(d_ptr->hint);
            for (const SearchField &field : filter.fields) {
                QContactDetailFilter detailFilter;
                detailFilter.setDetailType(field.detailType, field.field);
                detailFilter.setMatchFlags(filter.matchFlags);
                detailFilter.setValue(d_ptr->searchFilterValue);
                r->setFilter(haveSetFilter ? (r->filter() | detailFilter) : detailFilter);
                haveSetFilter = true;
            }

            d_ptr->requests.append(r);

            connect(r, &QContactFetchRequest::stateChanged, [this, r] {
                if (r->state() == QContactAbstractRequest::FinishedState
                        || r->state() == QContactAbstractRequest::CanceledState) {
                    bool newResultsAvailable = false;
                    bool allRequestsAreFinished = true;
                    for (QContactFetchRequest *req : d_ptr->requests) {
                        if (req->state() != QContactAbstractRequest::FinishedState
                                && req->state() != QContactAbstractRequest::CanceledState) {
                            // we don't want to handle results out-of-order, so immediately
                            // break so that we don't append results from later requests.
                            allRequestsAreFinished = false;
                            break;
                        } else if (!d_ptr->requestResultsHandled.contains(req)) {
                            // append the results of this request now that it has finished.
                            d_ptr->requestResultsHandled.append(req);
                            const QList<QContact> requestResults = req->contacts();
                            for (const QContact &c : requestResults) {
                                if (!d_ptr->seenContacts.contains(c.id())) {
                                    d_ptr->seenContacts.insert(c.id());
                                    d_ptr->contacts.append(c);
                                    newResultsAvailable = true;
                                }
                            }
                        }
                    }

                    if (newResultsAvailable) {
                        emit resultsAvailable();
                    }

                    if (allRequestsAreFinished) {
                        emit stateChanged(d_ptr->isCanceled
                                                ? QContactAbstractRequest::CanceledState
                                                : QContactAbstractRequest::FinishedState);
                    }
                }
            });

            r->start();
        }
    }
    return false;
}

bool QContactSearchFilterRequest::cancel()
{
    if (!d_ptr->manager) {
        // No manager.
    } else {
        // Cancel any outstanding sub-request.
        bool result = true;
        d_ptr->isCanceled = true;
        for (QContactFetchRequest *r : d_ptr->requests) {
            if (r->state() != QContactAbstractRequest::FinishedState
                    && r->state() != QContactAbstractRequest::CanceledState) {
                result = r->cancel();
            }
        }
        return result;
    }
    return false;
}

bool QContactSearchFilterRequest::waitForFinished(int msecs)
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        int timeLeft = msecs;
        bool result = true;
        int i = 0;
        while ((timeLeft > 0 || msecs <= 0) && result && i < d_ptr->requests.size()) {
            QElapsedTimer et;
            et.start();
            result = engine->waitForRequestFinished(d_ptr->requests[i++], msecs <= 0 ? msecs : timeLeft);
            timeLeft -= et.elapsed();
        }

        if (i < d_ptr->requests.size()) {
            // unable to finish all requests within the time.
            return false;
        }

        return result;
    }
    return false;
}

namespace QContactSearchFilterRequestImpl {
    QList<QContactSearchFilterRequest::SearchFilter> buildDefaultSearchFilters()
    {
        QContactSearchFilterRequest::SearchField firstNameField;
        firstNameField.detailType = QContactName::Type;
        firstNameField.field = QContactName::FieldFirstName;

        QContactSearchFilterRequest::SearchField lastNameField;
        lastNameField.detailType = QContactName::Type;
        lastNameField.field = QContactName::FieldLastName;

        QContactSearchFilterRequest::SearchField phoneField;
        phoneField.detailType = QContactPhoneNumber::Type;
        phoneField.field = QContactPhoneNumber::FieldNumber;

        QContactSearchFilterRequest::SearchField emailField;
        emailField.detailType = QContactEmailAddress::Type;
        emailField.field = QContactEmailAddress::FieldEmailAddress;

        QContactSearchFilterRequest::SearchField organizationField;
        organizationField.detailType = QContactOrganization::Type;
        organizationField.field = QContactOrganization::FieldName;

        QContactSearchFilterRequest::SearchField displayLabelGroupField;
        displayLabelGroupField.detailType = QContactDisplayLabel::Type;
        displayLabelGroupField.field = QContactDisplayLabel__FieldLabelGroup;

        QContactSearchFilterRequest::SearchField displayLabelField;
        displayLabelField.detailType = QContactDisplayLabel::Type;
        displayLabelField.field = QContactDisplayLabel::FieldLabel;

        //------------

        QContactSearchFilterRequest::SearchFilter firstNameStartsWithFilter;
        firstNameStartsWithFilter.fields.append(firstNameField);
        firstNameStartsWithFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

        QContactSearchFilterRequest::SearchFilter lastNameStartsWithFilter;
        lastNameStartsWithFilter.fields.append(lastNameField);
        lastNameStartsWithFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

        QContactSearchFilterRequest::SearchFilter displayLabelGroupStartsWithFilter;
        displayLabelGroupStartsWithFilter.fields.append(displayLabelGroupField);
        displayLabelGroupStartsWithFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

        // note: we cannot use a MatchPhoneNumber filter or it will fail to build the query
        // in the general case, since most characters are invalid normalized numbers.
        QContactSearchFilterRequest::SearchFilter phoneStartsWithFilter;
        phoneStartsWithFilter.fields.append(phoneField);
        phoneStartsWithFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

        QContactSearchFilterRequest::SearchFilter emailOrOrgStartsWithFilter;
        emailOrOrgStartsWithFilter.fields.append(emailField);
        emailOrOrgStartsWithFilter.fields.append(organizationField);
        emailOrOrgStartsWithFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

        QContactSearchFilterRequest::SearchFilter anyDetailContainsFilter;
        anyDetailContainsFilter.fields.append(firstNameField);
        anyDetailContainsFilter.fields.append(lastNameField);
        anyDetailContainsFilter.fields.append(displayLabelField);
        anyDetailContainsFilter.fields.append(organizationField);
        anyDetailContainsFilter.fields.append(emailField);
        anyDetailContainsFilter.fields.append(phoneField);
        anyDetailContainsFilter.matchFlags = QContactFilter::MatchContains | QContactFilter::MatchFixedString;

        //------------

        return QList<QContactSearchFilterRequest::SearchFilter>()
                << firstNameStartsWithFilter
                << lastNameStartsWithFilter
                << displayLabelGroupStartsWithFilter
                << phoneStartsWithFilter
                << emailOrOrgStartsWithFilter
                << anyDetailContainsFilter;
    }
}

QList<QContactSearchFilterRequest::SearchFilter> QContactSearchFilterRequest::defaultSearchFilters()
{
    static QList<QContactSearchFilterRequest::SearchFilter> defaultFilters
            = QContactSearchFilterRequestImpl::buildDefaultSearchFilters();
    return defaultFilters;
}

QT_END_NAMESPACE_CONTACTS

#endif // QCONTACTSEARCHFILTERREQUEST_IMPL_H
