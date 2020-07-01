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

#ifndef QCONTACTDETAILFETCHREQUEST_H
#define QCONTACTDETAILFETCHREQUEST_H

#include <qcontactabstractrequest.h>
#include <qcontactdetail.h>
#include <qcontactsortorder.h>
#include <qcontactfilter.h>
#include <qcontactfetchhint.h>

QT_BEGIN_NAMESPACE_CONTACTS

class QContactDetailFetchRequestPrivate;
class QContactDetailFetchRequest : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QContactDetailFetchRequest)
    Q_DECLARE_PRIVATE(QContactDetailFetchRequest)
public:
    QContactDetailFetchRequest(QObject *parent = nullptr);
    ~QContactDetailFetchRequest() override;

    QContactManager *manager() const;
    void setManager(QContactManager *manager);

    QContactDetail::DetailType type() const;
    void setType(QContactDetail::DetailType type);

    QList<int> fields() const;
    void setFields(const QList<int> fields);

    QContactFilter filter() const;
    void setFilter(const QContactFilter &filter);

    QList<QContactSortOrder> sorting() const;
    void setSorting(const QList<QContactSortOrder> &sorting);

    QContactFetchHint fetchHint() const;
    void setFetchHint(const QContactFetchHint &hint);

    QContactAbstractRequest::State state() const;
    QContactManager::Error error() const;

    QList<QContactDetail> details() const;

public Q_SLOTS:
    bool start();
    bool cancel();

    bool waitForFinished(int msecs = 0);

Q_SIGNALS:
    void stateChanged(QContactAbstractRequest::State state);
    void resultsAvailable();

private:
    QScopedPointer<QContactDetailFetchRequestPrivate> d_ptr;
};

QT_END_NAMESPACE_CONTACTS

#endif
