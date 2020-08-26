/*
 * Copyright (C) 2014 - 2015 Jolla Ltd.
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

#ifndef CONTACTDELTA_H
#define CONTACTDELTA_H

#include <QString>
#include <QHash>
#include <QList>
#include <QSet>

#include <QContactDetail>
#include <QContact>

QTCONTACTS_USE_NAMESPACE

namespace QtContactsSqliteExtensions {

    struct ContactDetailDelta {
        bool isValid = false;
        QList<QContactDetail> deletions;
        QList<QContactDetail> modifications;
        QList<QContactDetail> additions;

        template<typename T>
        QList<T> deleted() const
        {
            QList<T> ret;
            for (QList<QContactDetail>::const_iterator it = deletions.constBegin(); it != deletions.constEnd(); ++it) {
                if (it->type() == T::Type) {
                    ret.append(T(*it));
                }
            }
            return ret;
        }

        template<typename T>
        QList<T> modified() const
        {
            QList<T> ret;
            for (QList<QContactDetail>::const_iterator it = modifications.constBegin(); it != modifications.constEnd(); ++it) {
                if (it->type() == T::Type) {
                    ret.append(T(*it));
                }
            }
            return ret;
        }

        template<typename T>
        QList<T> added() const
        {
            QList<T> ret;
            for (QList<QContactDetail>::const_iterator it = additions.constBegin(); it != additions.constEnd(); ++it) {
                if (it->type() == T::Type) {
                    ret.append(T(*it));
                }
            }
            return ret;
        }
    };

    const QSet<QContactDetail::DetailType>& defaultIgnorableDetailTypes();
    const QHash<QContactDetail::DetailType, QSet<int> >& defaultIgnorableDetailFields();
    const QSet<int>& defaultIgnorableCommonFields();

    ContactDetailDelta determineContactDetailDelta(
            const QList<QContactDetail> &oldDetails,
            const QList<QContactDetail> &newDetails,
            const QSet<QContactDetail::DetailType> &ignorableDetailTypes = defaultIgnorableDetailTypes(),
            const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields = defaultIgnorableDetailFields(),
            const QSet<int> &ignorableCommonFields = defaultIgnorableCommonFields());

    int exactContactMatchExistsInList(
            const QContact &aContact,
            const QList<QContact> &list,
            const QSet<QContactDetail::DetailType> &ignorableDetailTypes,
            const QHash<QContactDetail::DetailType, QSet<int> > &ignorableDetailFields,
            const QSet<int> &ignorableCommonFields,
            bool printDifferences = false);
}

#endif // CONTACTDELTA_H
