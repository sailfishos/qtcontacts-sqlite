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

#ifndef QTCONTACTS_EXTENSIONS_IMPL_H
#define QTCONTACTS_EXTENSIONS_IMPL_H

#include <qtcontacts-extensions.h>

namespace {

QString normalize(const QString &input, int flags, int maxCharacters)
{
    // Allow '[' and ']' even though RFC3966 doesn't
    static const QString allowedSeparators(QString::fromLatin1(" -()[]"));
    static const QString dtmfChars(QString::fromLatin1("pP,.wWxX#*"));
    static const QString sipScheme(QString::fromLatin1("sips:"));
    static const QString hashControl(QString::fromLatin1("#31#"));
    static const QString starControl(QString::fromLatin1("*31#"));

    static const QChar plus(QChar::fromLatin1('+'));
    static const QChar colon(QChar::fromLatin1(':'));
    static const QChar at(QChar::fromLatin1('@'));

    // If this is a SIP URI (empty scheme means 'sips'), validate the identifier part
    QString number(input);
    if (number.startsWith(sipScheme) || number.startsWith(colon)) {
        int colonIndex = number.indexOf(colon);
        int atIndex = number.indexOf(at, colonIndex + 1);
        if (atIndex != -1) {
            number = number.mid(colonIndex + 1, (atIndex - colonIndex - 1));
        }
    }

    QString subset;
    subset.reserve(number.length());

    QChar initialChar;
    bool numericComponent = false;
    int firstDtmfIndex = -1;

    QString::const_iterator it = number.constBegin(), end = number.constEnd();
    for ( ; it != end; ++it) {
        if ((*it).isDigit()) {
            // Convert to ASCII, capturing unicode digit values
            const QChar digit(QChar::fromLatin1('0' + (*it).digitValue()));
            subset.append(digit);
            numericComponent = true;
            if (initialChar.isNull()) {
                initialChar = digit;
            }
        } else if (*it == plus) {
            if (initialChar.isNull()) {
                // This is the start of the diallable number
                subset.append(*it);
                initialChar = *it;
            } else if (firstDtmfIndex != -1) {
                // Allow inside the DMTF section
                subset.append(*it);
            } else if (flags & QtContactsSqliteExtensions::ValidatePhoneNumber) {
                // Not valid in this location
                return QString();
            }
        } else if (allowedSeparators.contains(*it)) {
            if (flags & QtContactsSqliteExtensions::KeepPhoneNumberPunctuation) {
                subset.append(*it);
            }
        } else if (dtmfChars.contains(*it)) {
            if ((*it).isLetter() && !numericComponent) {
                // Alphabetic DTMF chars can only occur after some numeric component
                if (flags & QtContactsSqliteExtensions::ValidatePhoneNumber) {
                    return QString();
                } else {
                    // Skip this character, not valid in this position
                }
            } else {
                if ((flags & QtContactsSqliteExtensions::KeepPhoneNumberDialString) == 0) {
                    // No need to continue accumulating
                    if (flags & QtContactsSqliteExtensions::ValidatePhoneNumber) {
                        // Ensure the remaining characters are permissible
                        while (++it != end) {
                            if ((!(*it).isDigit()) && !allowedSeparators.contains(*it) && !dtmfChars.contains(*it)) {
                                // Invalid character
                                return QString();
                            }
                        }
                    }
                    break;
                }

                // Otherwise, continue with processing
                if (firstDtmfIndex == -1) {
                    firstDtmfIndex = subset.length();
                }

                // Accept 'x' and 'X', but convert them to 'p' in the normalized form
                if ((*it).toLower() == QChar::fromLatin1('x')) {
                    subset.append(QChar::fromLatin1('p'));
                } else {
                    subset.append(*it);
                }
            }
        } else if (flags & QtContactsSqliteExtensions::ValidatePhoneNumber) {
            // Invalid character
            return QString();
        }
    }

    if ((flags & QtContactsSqliteExtensions::ValidatePhoneNumber) &&
        (initialChar == plus) && (firstDtmfIndex != -1)) {
        // If this number starts with '+', it mustn't contain control codes
        if ((subset.indexOf(hashControl, firstDtmfIndex) != -1) ||
            (subset.indexOf(starControl, firstDtmfIndex) != -1)) {
            return QString();
        }
    }

    if (maxCharacters != -1) {
        int characters = 0;
        int index = (firstDtmfIndex == -1) ? (subset.length() - 1) : (firstDtmfIndex - 1);
        for ( ; index > 0; --index) {
            const QChar &c(subset.at(index));
            if (c.isDigit() || c == plus) {
                if (++characters == maxCharacters) {
                    // Only include the digits from here
                    subset = subset.mid(index);
                    break;
                }
            }
        }
    }

    return subset.trimmed();
}

}

namespace QtContactsSqliteExtensions {

QContactId apiContactId(quint32 iid)
{
    QContactId contactId;
    if (iid != 0) {
        static const QString idStr(QString::fromLatin1("qtcontacts:org.nemomobile.contacts.sqlite::sql-%1"));
        contactId = QContactId::fromString(idStr.arg(iid));
        if (contactId.isNull()) {
            qWarning() << "Unable to formulate valid ID from:" << iid;
        }
    }
    return contactId;
}

quint32 internalContactId(const QContactId &id)
{
    if (!id.isNull()) {
        QStringList components = id.toString().split(QChar::fromLatin1(':'));
        const QString &idComponent = components.isEmpty() ? QString() : components.last();
        if (idComponent.startsWith(QString::fromLatin1("sql-"))) {
            return idComponent.mid(4).toUInt();
        }
    }
    return 0;
}

QString normalizePhoneNumber(const QString &input, NormalizePhoneNumberFlags flags)
{
    return normalize(input, flags, -1);
}

QString minimizePhoneNumber(const QString &input, int maxCharacters)
{
    // Minimal form number should preserve DTMF dial string to differentiate PABX extensions
    return normalize(input, KeepPhoneNumberDialString, maxCharacters);
}

}

#endif
