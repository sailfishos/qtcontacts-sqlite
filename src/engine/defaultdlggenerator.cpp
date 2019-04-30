/*
 * Copyright (C) 2019 Jolla Ltd. <chris.adams@jollamobile.com>
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

#include "defaultdlggenerator.h"

/*
   This display label group generator provides the
   default (fallback) group generation semantics, and should
   be the last generator used (i.e. if no other generator
   is valid in the current locale).

   The semantics it implements are as follows:

   1) if the preferred name field data is empty,
      it falls back to display label data to generate the
      group.

   2) if the first character of the preferred data
      is a digit (0..9) then the group is '#'.

   3) if the first character of the preferred data
      is within 'A'..'Z' it returns that character.
      (TODO: perform a unidecode transliteration first.)

   4) otherwise, the group is '?'

   For example, if the preferred detail is
   QContactName::Type and the preferred field is
   QContactName::FieldLastName, and the client passes
   in a contact with name "John Smith", then the first
   letter of the last name (in this case, 'S') will be
   returned as the group.
*/

DefaultDlgGenerator::DefaultDlgGenerator(QObject *parent)
    : QObject(parent)
{
}

QString DefaultDlgGenerator::name() const
{
    return QStringLiteral("default");
}

int DefaultDlgGenerator::priority() const
{
    return 0;
}

bool DefaultDlgGenerator::preferredForLocale(const QLocale &) const
{
    return false; // this default plugin is the fallback, never preferred but always valid.
}

bool DefaultDlgGenerator::validForLocale(const QLocale &) const
{
    return true; // this default plugin is the fallback, always valid.
}

QStringList DefaultDlgGenerator::displayLabelGroups() const
{
    static QStringList groups {
        QStringLiteral("A"),
        QStringLiteral("B"),
        QStringLiteral("C"),
        QStringLiteral("D"),
        QStringLiteral("E"),
        QStringLiteral("F"),
        QStringLiteral("G"),
        QStringLiteral("H"),
        QStringLiteral("I"),
        QStringLiteral("J"),
        QStringLiteral("K"),
        QStringLiteral("L"),
        QStringLiteral("M"),
        QStringLiteral("N"),
        QStringLiteral("O"),
        QStringLiteral("P"),
        QStringLiteral("Q"),
        QStringLiteral("R"),
        QStringLiteral("S"),
        QStringLiteral("T"),
        QStringLiteral("U"),
        QStringLiteral("V"),
        QStringLiteral("W"),
        QStringLiteral("X"),
        QStringLiteral("Y"),
        QStringLiteral("Z"),
        QStringLiteral("#"),
        QStringLiteral("?")
    };
    return groups;
}

QString DefaultDlgGenerator::displayLabelGroup(const QString &data) const
{
    QString group;

    if (!data.isEmpty()) {
        QChar upperChar = data.at(0).toUpper();
        ushort val = upperChar.unicode();
        if (val >= 'A' && val <= 'Z') {
            group = QString(upperChar);
        } else if (data.at(0).isDigit()) {
            group = QStringLiteral("#");
        }
    }

    if (group.isEmpty()) {
        // unknown group. put in "other" group '?'
        group = QStringLiteral("?");
    }

    return group;
}
