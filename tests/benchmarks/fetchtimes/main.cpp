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

#include <QContactManager>
#include <QContactFetchRequest>
#include <QContactRemoveRequest>
#include <QContactSaveRequest>
#include <QContactFavorite>
#include <QContactName>
#include <QContactEmailAddress>
#include <QContactPhoneNumber>
#include <QContactDisplayLabel>
#include <QContactHobby>
#include <QContactAvatar>
#include <QContactAddress>
#include <QContactPresence>
#include <QContactNickname>
#include <QContactOnlineAccount>
#include <QContactSyncTarget>
#include <QContactDetailFilter>
#include <QContactFetchHint>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QtDebug>

#include "../../../src/extensions/qtcontacts-extensions.h"

QTCONTACTS_USE_NAMESPACE

#include <QContactIdFilter>
static QContactId retrievalId(const QContact &contact) { return contact.id(); }

static QStringList generateNonOverlappingFirstNamesList()
{
    QStringList retn;
    retn << "Zach" << "Zane" << "Zinedine" << "Zockey"
         << "Yann" << "Yedrez" << "Yarrow" << "Yelter"
         << "Ximmy" << "Xascha" << "Xanthar" << "Xachy"
         << "William" << "Wally" << "Weston" << "Wulther"
         << "Vernon" << "Veston" << "Victoria" << "Vuitton"
         << "Urqhart" << "Uelela" << "Ulrich" << "Umpty"
         << "Timothy" << "Tigga" << "Tabitha" << "Texter"
         << "Stan" << "Steve" << "Sophie" << "Siphonie"
         << "Richard" << "Rafael" << "Rachael" << "Rascal"
         << "Quirky" << "Quilton" << "Quentin" << "Quarreller";
    return retn;
}

static QStringList generateNonOverlappingLastNamesList()
{
    QStringList retn;
    retn << "Quilter" << "Qualifa" << "Quarrier" << "Quickson"
         << "Rigger" << "Render" << "Ranger" << "Reader"
         << "Sailor" << "Smith" << "Salter" << "Shelfer"
         << "Tailor" << "Tasker" << "Toppler" << "Tipster"
         << "Underhill" << "Umpire" << "Upperhill" << "Uppsland"
         << "Vintner" << "Vester" << "Victor" << "Vacationer"
         << "Wicker" << "Whaler" << "Whistler" << "Wolf"
         << "Xylophone" << "Xabu" << "Xanadu" << "Xatti"
         << "Yeoman" << "Yesman" << "Yelper" << "Yachtsman"
         << "Zimmerman" << "Zomething" << "Zeltic" << "Zephyr";
    return retn;
}

static QStringList generateFirstNamesList()
{
    QStringList retn;
    retn << "Alexandria" << "Andrew" << "Adrien" << "Amos"
         << "Bob" << "Bronte" << "Barry" << "Braxton"
         << "Clarence" << "Chandler" << "Chris" << "Chantelle"
         << "Dominic" << "Diedre" << "David" << "Derrick"
         << "Eric" << "Esther" << "Eddie" << "Eean"
         << "Felicity" << "Fred" << "Fletcher" << "Farraday"
         << "Gary" << "Gertrude" << "Gerry" << "Germaine"
         << "Hillary" << "Henry" << "Hans" << "Haddock"
         << "Jacob" << "Jane" << "Jackson" << "Jennifer"
         << "Larry" << "Lilliane" << "Lambert" << "Lilly"
         << "Mary" << "Mark" << "Mirriam" << "Matthew"
         << "Nathene" << "Nicholas" << "Ned" << "Norris"
         << "Othello" << "Oscar" << "Olaf" << "Odinsdottur"
         << "Penny" << "Peter" << "Patrick" << "Pilborough";
    return retn;
}

static QStringList generateMiddleNamesList()
{
    QStringList retn;
    retn << "Aubrey" << "Cody" << "Taylor" << "Leslie";
    return retn;
}

static QStringList generateLastNamesList()
{
    QStringList retn;
    retn << "Arkady" << "Addleman" << "Axeman" << "Applegrower" << "Anderson"
         << "Baker" << "Bremmer" << "Bedlam" << "Barrymore" << "Battery"
         << "Cutter" << "Cooper" << "Cutler" << "Catcher" << "Capemaker"
         << "Driller" << "Dyer" << "Diver" << "Daytona" << "Duster"
         << "Eeler" << "Eckhart" << "Eggsman" << "Empty" << "Ellersly"
         << "Farmer" << "Farrier" << "Foster" << "Farseer" << "Fairtime"
         << "Grower" << "Gaston" << "Gerriman" << "Gipsland" << "Guilder"
         << "Helper" << "Hogfarmer" << "Harriet" << "Hope" << "Huxley"
         << "Inker" << "Innman" << "Ipland" << "Instiller" << "Innis"
         << "Joker" << "Jackson" << "Jolt" << "Jockey" << "Jerriman";
    return retn;
}

static QStringList generatePhoneNumbersList()
{
    QStringList retn;
    retn << "111222" << "111333" << "111444" << "111555" << "111666"
         << "111777" << "111888" << "111999" << "222333" << "222444"
         << "222555" << "222666" << "222777" << "222888" << "222999"
         << "333444" << "333555" << "333666" << "333777" << "333888"
         << "333999" << "444555" << "444666" << "444777" << "444888"
         << "444999" << "555666" << "555777" << "555888" << "555999"
         << "666111" << "666222" << "666333" << "666444" << "666555"
         << "777111" << "777222" << "777333" << "777444" << "777555"
         << "777666" << "888111" << "888222" << "888333" << "888444"
         << "888555" << "888666" << "888777" << "999111" << "999222"
         << "999333" << "999444" << "999555" << "999666" << "999777"
         << "999888" << "999999";
    return retn;
}

static QStringList generateEmailProvidersList()
{
    QStringList retn;
    retn << "@test.com" << "@testing.com" << "@testers.com"
         << "@test.org" << "@testing.org" << "@testers.org"
         << "@test.net" << "@testing.net" << "@testers.net"
         << "@test.fi" << "@testing.fi" << "@testers.fi"
         << "@test.com.au" << "@testing.com.au" << "@testers.com.au"
         << "@test.co.uk" << "@testing.co.uk" << "@testers.co.uk"
         << "@test.co.jp" << "@test.co.jp" << "@testers.co.jp";
    return retn;
}

static QStringList generateAvatarsList()
{
    QStringList retn;
    retn << "-smiling.jpg" << "-laughing.jpg" << "-surprised.jpg"
         << "-smiling.png" << "-laughing.png" << "-surprised.png"
         << "-curious.jpg" << "-joking.jpg" << "-grinning.jpg"
         << "-curious.png" << "-joking.png" << "-grinning.png";
    return retn;
}

static QStringList generateHobbiesList()
{
    QStringList retn;
    retn << "tennis" << "soccer" << "squash" << "volleyball"
         << "chess" << "photography" << "painting" << "sketching";
    return retn;
}

QContact generateContact(const QString &syncTarget = QString(QLatin1String("local")), bool possiblyAggregate = false)
{
    static const QStringList firstNames(generateFirstNamesList());
    static const QStringList middleNames(generateMiddleNamesList());
    static const QStringList lastNames(generateLastNamesList());
    static const QStringList nonOverlappingFirstNames(generateNonOverlappingFirstNamesList());
    static const QStringList nonOverlappingLastNames(generateNonOverlappingLastNamesList());
    static const QStringList phoneNumbers(generatePhoneNumbersList());
    static const QStringList emailProviders(generateEmailProvidersList());
    static const QStringList avatars(generateAvatarsList());
    static const QStringList hobbies(generateHobbiesList());

    // we randomly determine whether to generate various details
    // to ensure that we have heterogeneous contacts in the db.
    QContact retn;
    int random = qrand();
    bool preventAggregate = (syncTarget != QLatin1String("local") && !possiblyAggregate);

    // We always have a sync target.
    QContactSyncTarget synctarget;
    synctarget.setSyncTarget(syncTarget);
    retn.saveDetail(&synctarget);

    // We always have a name.  Select an overlapping name if the sync target
    // is something other than "local" and possiblyAggregate is true.
    QContactName name;
    name.setFirstName(preventAggregate ?
            nonOverlappingFirstNames.at(random % nonOverlappingFirstNames.size()) :
            firstNames.at(random % firstNames.size()));
    name.setLastName(preventAggregate ?
            nonOverlappingLastNames.at(random % nonOverlappingLastNames.size()) :
            lastNames.at(random % lastNames.size()));
    if ((random % 6) == 0) name.setMiddleName(middleNames.at(random % middleNames.size()));
    if ((random % 17) == 0) name.setPrefix(QLatin1String("Dr."));
    retn.saveDetail(&name);

    // Favorite
    if ((random % 31) == 0) {
        QContactFavorite fav;
        fav.setFavorite(true);
        retn.saveDetail(&fav);
    }

    // Phone number
    if ((random % 3) == 0) {
        QContactPhoneNumber phn;
        QString randomPhn = phoneNumbers.at(random % phoneNumbers.size());
        phn.setNumber(preventAggregate ? QString(QString::number(random % 500000) + randomPhn) : randomPhn);
        if ((random % 9) == 0) phn.setContexts(QContactDetail::ContextWork);
        retn.saveDetail(&phn);
    }

    // Email
    if ((random % 2) == 0) {
        QContactEmailAddress em;
        em.setEmailAddress(QString(QLatin1String("%1%2%3%4"))
                .arg(preventAggregate ? QString(QString::number(random % 500000) + syncTarget) : QString())
                .arg(name.firstName()).arg(name.lastName())
                .arg(emailProviders.at(random % emailProviders.size())));
        if (random % 9) em.setContexts(QContactDetail::ContextWork);
        retn.saveDetail(&em);
    }

    // Avatar
    if ((random % 5) == 0) {
        QContactAvatar av;
        av.setImageUrl(name.firstName() + avatars.at(random % avatars.size()));
        retn.saveDetail(&av);
    }

    // Hobby
    if ((random % 21) == 0) {
        QContactHobby h1;
        h1.setHobby(hobbies.at(random % hobbies.size()));
        retn.saveDetail(&h1);

        int newRandom = qrand();
        if ((newRandom % 2) == 0) {
            QContactHobby h2;
            h2.setHobby(hobbies.at(newRandom % hobbies.size()));
            retn.saveDetail(&h2);
        }
    }

    return retn;
}

static qint64 aggregatedPresenceUpdate(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // This presence update benchmark should show whether presence update
    // time/cost scales linearly (we hope) or exponentially (which would be bad)
    // with the number of contacts in database if many of them are aggregated.
    // About of the "morePrefillData" contacts should share an aggregate
    // with one of the "prefillData" contacts.
    // This also benchmarks the effect of the per-contact-size (number of details
    // in each contact which change) of the update, and the effect of using
    // a filter mask to reduce the amount of work to be done.
    qDebug() << "--------";
    qDebug() << "Performing scaling aggregated batch (connectivity change) presence update tests:";

    // prefill the database
    const int prefillCount = quickMode ? 250 : 500;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("aggregatedPresenceUpdate")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }

    qDebug() << "    generating aggregated prefill data, please wait...";
    QList<QContact> morePrefillData;
    for (int i = 0; i < prefillCount; ++i) {
        if (i % 2 == 0) {
            morePrefillData.append(generateContact("aggregatedPresenceUpdate2", false)); // false = don't aggregate.
        } else {
            morePrefillData.append(generateContact("aggregatedPresenceUpdate2", true)); // false = don't aggregate.
        }
    }
    manager.saveContacts(&morePrefillData);
    for (const QContact &c : morePrefillData) {
        deleteIds.append(c.id());
    }

    // now do the update
    QList<QContact> contactsToUpdate;
    QDateTime timestamp = QDateTime::currentDateTime();
    QStringList presenceAvatars = generateAvatarsList();
    for (int j = 0; j < morePrefillData.size(); ++j) {
        QContact curr = morePrefillData.at(j);
        QString genstr = QString::number(j) + "5";
        QContactPresence cp = curr.detail<QContactPresence>();
        QContactNickname nn = curr.detail<QContactNickname>();
        QContactAvatar av = curr.detail<QContactAvatar>();
        cp.setNickname(genstr);
        cp.setCustomMessage(genstr);
        cp.setTimestamp(timestamp);
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>((qrand() % 4) + 1));
        nn.setNickname(nn.nickname() + genstr);
        av.setImageUrl(genstr + presenceAvatars.at(qrand() % presenceAvatars.size()));
        curr.saveDetail(&cp);
        curr.saveDetail(&nn);
        curr.saveDetail(&av);
        contactsToUpdate.append(curr);
    }

    // perform a batch save.
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate);
    qint64 presenceElapsed = syncTimer.elapsed();
    int totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") presence+nick+avatar (with" << totalAggregatesInDatabase << "existing in database, partial overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    // now test just update the presence status (not nickname or avatar details).
    morePrefillData = contactsToUpdate;
    contactsToUpdate.clear();
    for (int j = 0; j < morePrefillData.size(); ++j) {
        QContact curr = morePrefillData.at(j);
        QContactPresence cp = curr.detail<QContactPresence>();
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>((qrand() % 4) + 1));
        curr.saveDetail(&cp);
        contactsToUpdate.append(curr);
    }

    // perform a batch save.
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate);
    presenceElapsed = syncTimer.elapsed();
    totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") presence only (with" << totalAggregatesInDatabase << "existing in database, partial overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    // also pass a "detail type mask" to the update.  This allows the backend
    // to perform optimisation based upon which details are modified.
    morePrefillData = contactsToUpdate;
    contactsToUpdate.clear();
    for (int j = 0; j < morePrefillData.size(); ++j) {
        QContact curr = morePrefillData.at(j);
        QContactPresence cp = curr.detail<QContactPresence>();
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>((qrand() % 4) + 1));
        curr.saveDetail(&cp);
        contactsToUpdate.append(curr);
    }

    // perform a batch save.
    QList<QContactDetail::DetailType> typeMask;
    typeMask << QContactDetail::TypePresence;
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate, typeMask);
    presenceElapsed = syncTimer.elapsed();
    totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") masked presence only (with" << totalAggregatesInDatabase << "existing in database, partial overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    elapsedTimeTotal += deleteTime;
    return elapsedTimeTotal;
}

static qint64 nonAggregatedPresenceUpdate(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // this presence update benchmark should show whether presence update
    // time/cost scales linearly (we hope) or exponentially (which would be bad)
    // with the number of contacts in database even if they are unrelated / non-aggregated.
    qDebug() << "--------";
    qDebug() << "Performing scaling non-aggregated batch (connectivity change) presence update tests:";

    // prefill the database
    const int prefillCount = quickMode ? 250 : 500;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("nonAggregatedPresenceUpdate")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }

    qDebug() << "    generating non-overlapping / non-aggregated prefill data, please wait...";
    QList<QContact> morePrefillData;
    for (int i = 0; i < prefillCount; ++i) {
        morePrefillData.append(generateContact("nonAggregatedPresenceUpdate2", false)); // false = don't aggregate.
    }
    manager.saveContacts(&morePrefillData);
    for (const QContact &c : morePrefillData) {
        deleteIds.append(c.id());
    }

    // now do the update of only one set of those contacts
    QList<QContact> contactsToUpdate;
    QDateTime timestamp = QDateTime::currentDateTime();
    QStringList presenceAvatars = generateAvatarsList();
    for (int j = 0; j < morePrefillData.size(); ++j) {
        QContact curr = morePrefillData.at(j);
        QString genstr = QString::number(j) + "4";
        QContactPresence cp = curr.detail<QContactPresence>();
        QContactNickname nn = curr.detail<QContactNickname>();
        QContactAvatar av = curr.detail<QContactAvatar>();
        cp.setNickname(genstr);
        cp.setCustomMessage(genstr);
        cp.setTimestamp(timestamp);
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>((qrand() % 4) + 1));
        nn.setNickname(nn.nickname() + genstr);
        av.setImageUrl(genstr + presenceAvatars.at(qrand() % presenceAvatars.size()));
        curr.saveDetail(&cp);
        curr.saveDetail(&nn);
        curr.saveDetail(&av);
        contactsToUpdate.append(curr);
    }

    // perform a batch save.
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate);
    qint64 presenceElapsed = syncTimer.elapsed();
    int totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") presence+nick+avatar (with" << totalAggregatesInDatabase << "existing in database, no overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    elapsedTimeTotal += deleteTime;
    return elapsedTimeTotal;
}

static qint64 scalingPresenceUpdate(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // this presence update benchmark should show whether presence update
    // time/cost scales linearly (we hope) or exponentially (which would be bad)
    // with the number of contacts in database and the number of updates.
    qDebug() << "--------";
    qDebug() << "Performing scaling entire batch (connectivity change) presence update tests:";

    // prefill the database
    const int prefillCount = quickMode ? 250 : 500;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("scalingPresenceUpdate")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }

    // now do the updates and save.
    QList<QContact> contactsToUpdate;
    QDateTime timestamp = QDateTime::currentDateTime();
    QStringList presenceAvatars = generateAvatarsList();
    for (int j = 0; j < prefillData.size(); ++j) {
        QContact curr = prefillData.at(j);
        QString genstr = QString::number(j) + "3";
        QContactPresence cp = curr.detail<QContactPresence>();
        QContactNickname nn = curr.detail<QContactNickname>();
        QContactAvatar av = curr.detail<QContactAvatar>();
        cp.setNickname(genstr);
        cp.setCustomMessage(genstr);
        cp.setTimestamp(timestamp);
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>((qrand() % 4) + 1));
        nn.setNickname(nn.nickname() + genstr);
        av.setImageUrl(genstr + presenceAvatars.at(qrand() % presenceAvatars.size()));
        curr.saveDetail(&cp);
        curr.saveDetail(&nn);
        curr.saveDetail(&av);
        contactsToUpdate.append(curr);
    }

    // perform a batch save.
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate);
    qint64 presenceElapsed = syncTimer.elapsed();
    int totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") presence+nick+avatar (with" << totalAggregatesInDatabase << "existing in database, all overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    elapsedTimeTotal += deleteTime;
    return elapsedTimeTotal;
}

static qint64 entireBatchPresenceUpdate(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // in the second presence update test, we update ALL of the contacts
    // This simulates having a large number of contacts from a single source (eg, a social network)
    // where (due to changed connectivity status) presence updates for the entire set become available.
    qDebug() << "--------";
    qDebug() << "Performing entire batch (connectivity change) presence update tests:";

    // prefill the database
    const int prefillCount = quickMode ? 100 : 250;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("entireBatchPresenceUpdate")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }

    QList<QContact> contactsToUpdate;
    QDateTime timestamp = QDateTime::currentDateTime();
    QStringList presenceAvatars = generateAvatarsList();
    for (int j = 0; j < prefillData.size(); ++j) {
        QContact curr = prefillData.at(j);
        QString genstr = QString::number(j) + "2";
        QContactPresence cp = curr.detail<QContactPresence>();
        QContactNickname nn = curr.detail<QContactNickname>();
        QContactAvatar av = curr.detail<QContactAvatar>();
        cp.setNickname(genstr);
        cp.setCustomMessage(genstr);
        cp.setTimestamp(timestamp);
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>((qrand() % 4) + 1));
        nn.setNickname(nn.nickname() + genstr);
        av.setImageUrl(genstr + presenceAvatars.at(qrand() % presenceAvatars.size()));
        curr.saveDetail(&cp);
        curr.saveDetail(&nn);
        curr.saveDetail(&av);
        contactsToUpdate.append(curr);
    }

    // perform a batch save.
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate);
    qint64 presenceElapsed = syncTimer.elapsed();
    int totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") presence+nick+avatar (with" << totalAggregatesInDatabase << "existing in database, all overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    elapsedTimeTotal += deleteTime;
    return elapsedTimeTotal;
}

static qint64 smallBatchPresenceUpdate(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // The next test is about updating existing contacts, amongst a large set.
    // We're especially interested in presence updates, as these are common.
    qDebug() << "--------";
    qDebug() << "Performing small batch presence update tests:";

    // prefill the database
    const int prefillCount = quickMode ? 100 : 250;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("smallBatchPresenceUpdate")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }

    // in the first presence update test, we update a small number of contacts.
    QStringList presenceAvatars = generateAvatarsList();
    QList<QContact> contactsToUpdate;
    const int smallBatchSize = quickMode ? 5 : 10;
    for (int i = 0; i < smallBatchSize; ++i) {
        contactsToUpdate.append(prefillData.at(prefillData.size() - 1 - i));
    }

    // modify the presence, nickname and avatar of the test data
    for (int j = 0; j < contactsToUpdate.size(); ++j) {
        QString genstr = QString::number(j);
        QContact curr = contactsToUpdate[j];
        QContactPresence cp = curr.detail<QContactPresence>();
        QContactNickname nn = curr.detail<QContactNickname>();
        QContactAvatar av = curr.detail<QContactAvatar>();
        cp.setNickname(genstr);
        cp.setCustomMessage(genstr);
        cp.setTimestamp(QDateTime::currentDateTime());
        cp.setPresenceState(static_cast<QContactPresence::PresenceState>(qrand() % 4));
        nn.setNickname(nn.nickname() + genstr);
        av.setImageUrl(genstr + presenceAvatars.at(qrand() % presenceAvatars.size()));
        curr.saveDetail(&cp);
        curr.saveDetail(&nn);
        curr.saveDetail(&av);
        contactsToUpdate.replace(j, curr);
    }

    // perform a batch save.
    syncTimer.start();
    manager.saveContacts(&contactsToUpdate);
    qint64 presenceElapsed = syncTimer.elapsed();
    int totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    update ( batch of" << contactsToUpdate.size() << ") presence+nick+avatar (with" << totalAggregatesInDatabase << "existing in database, all overlap):" << presenceElapsed
             << "milliseconds (" << ((1.0 * presenceElapsed) / (1.0 * contactsToUpdate.size())) << " msec per updated contact )";
    elapsedTimeTotal += presenceElapsed;

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    elapsedTimeTotal += deleteTime;
    return elapsedTimeTotal;
}

static qint64 aggregationOperations(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // The next test is about saving contacts which should get aggregated into others.
    // Aggregation is an expensive operation, so we expect these save operations to take longer.
    qDebug() << "--------";
    qDebug() << "Performing aggregation tests";

    // prefill the database
    const int prefillCount = quickMode ? 100 : 250;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("aggregationOperations")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }

    // generate contacts which will be aggregated into the prefill contacts.
    const int aggregateCount = quickMode ? 50 : 100;
    QList<QContact> contactsToAggregate;
    for (int i = 0; i < aggregateCount; ++i) {
        QContact existingContact = prefillData.at(prefillData.size() - 1 - i);
        QContact contactToAggregate;
        QContactSyncTarget newSyncTarget;
        newSyncTarget.setSyncTarget(QString(QLatin1String("aggregationOperations")));
        QContactName aggName = existingContact.detail<QContactName>(); // ensures it'll get aggregated
        QContactOnlineAccount newOnlineAcct; // new data, which should get promoted up etc.
        newOnlineAcct.setAccountUri(QString(QLatin1String("aggregationOperations%1@fetchtimes.benchmark")).arg(i));
        contactToAggregate.saveDetail(&newSyncTarget);
        contactToAggregate.saveDetail(&aggName);
        contactToAggregate.saveDetail(&newOnlineAcct);
        contactsToAggregate.append(contactToAggregate);
    }

    syncTimer.start();
    manager.saveContacts(&contactsToAggregate);
    qint64 aggregationElapsed = syncTimer.elapsed();
    int totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    average time for aggregation of" << contactsToAggregate.size() << "contacts (with" << totalAggregatesInDatabase << "existing in database):" << aggregationElapsed
             << "milliseconds (" << ((1.0 * aggregationElapsed) / (1.0 * contactsToAggregate.size())) << " msec per aggregated contact )";
    elapsedTimeTotal += aggregationElapsed;
    for (const QContact &c : contactsToAggregate) {
        deleteIds.append(c.id());
    }

    // Now perform the test again, this time with more aggregates, to test nonlinearity.
    contactsToAggregate.clear();
    const int high = prefillData.size() / 2, low = high / 2;
    for (int i = low; i < high; ++i) {
        QContact existingContact = prefillData.at(prefillData.size() - 1 - i);
        QContact contactToAggregate;
        QContactSyncTarget newSyncTarget;
        newSyncTarget.setSyncTarget(QString(QLatin1String("aggregationOperations2")));
        QContactName aggName = existingContact.detail<QContactName>(); // ensures it'll get aggregated
        QContactOnlineAccount newOnlineAcct; // new data, which should get promoted up etc.
        newOnlineAcct.setAccountUri(QString(QLatin1String("aggregationOperations%1@fetchtimes.benchmark")).arg(i));
        contactToAggregate.saveDetail(&newSyncTarget);
        contactToAggregate.saveDetail(&aggName);
        contactToAggregate.saveDetail(&newOnlineAcct);
        contactsToAggregate.append(contactToAggregate);
    }

    syncTimer.start();
    manager.saveContacts(&contactsToAggregate);
    aggregationElapsed = syncTimer.elapsed();
    totalAggregatesInDatabase = manager.contactIds().count();
    qDebug() << "    average time for aggregation of" << contactsToAggregate.size() << "contacts (with" << totalAggregatesInDatabase << "existing in database):" << aggregationElapsed
             << "milliseconds (" << ((1.0 * aggregationElapsed) / (1.0 * contactsToAggregate.size())) << " msec per aggregated contact )";
    elapsedTimeTotal += aggregationElapsed;
    for (const QContact &c : contactsToAggregate) {
        deleteIds.append(c.id());
    }

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    elapsedTimeTotal += deleteTime;
    return elapsedTimeTotal;
}

static qint64 smallBatchWithExistingData(QContactManager &manager, bool quickMode)
{
    qint64 elapsedTimeTotal = 0;
    QElapsedTimer syncTimer;

    // these tests are slightly different to the others.  They operate on much smaller
    // batches, but occur after the database has already been prefilled with some data.
    qDebug() << "--------";
    qDebug() << "Performing test of small batch updates with large existing data set";
    QList<int> smallerNbrContacts;
    if (quickMode) {
        smallerNbrContacts << 20;
    } else {
        smallerNbrContacts << 1 << 2 << 5 << 10 << 20 << 50;
    }
    QList<QList<QContact> > smallerTestData;
    qDebug() << "    generating smaller test data for prefilled timings...";
    for (int i = 0; i < smallerNbrContacts.size(); ++i) {
        int howMany = smallerNbrContacts.at(i);
        QList<QContact> newTestData;
        newTestData.reserve(howMany);

        for (int j = 0; j < howMany; ++j) {
            newTestData.append(generateContact(QStringLiteral("smallBatchWithExistingData")));
        }

        smallerTestData.append(newTestData);
    }

    // prefill the database
    const int prefillCount = quickMode ? 100 : 250;
    QList<QContact> prefillData;
    prefillData.reserve(prefillCount);
    for (int i = 0; i < prefillCount; ++i) {
        prefillData.append(generateContact(QStringLiteral("smallBatchWithExistingData")));
    }
    qDebug() << "    prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);

    qDebug() << "    now performing timings (shouldn't get aggregated)...";
    for (int i = 0; i < smallerTestData.size(); ++i) {
        QList<QContact> td = smallerTestData.at(i);
        qint64 ste = 0;
        qDebug() << "    performing tests for" << td.size() << "contacts:";

        syncTimer.start();
        manager.saveContacts(&td);
        ste = syncTimer.elapsed();
        qDebug() << "    saving took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        QContactFetchHint fh;
        syncTimer.start();
        QList<QContact> readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all (" << readContacts.size() << "), all details, took" << ste << "milliseconds";
        elapsedTimeTotal += ste;

        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type
                << QContactPhoneNumber::Type << QContactEmailAddress::Type);
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, common details, took" << ste << "milliseconds";
        elapsedTimeTotal += ste;

        fh.setOptimizationHints(QContactFetchHint::NoRelationships);
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, no relationships, took" << ste << "milliseconds";
        elapsedTimeTotal += ste;

        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type);
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, display details + no rels, took" << ste << "milliseconds";
        elapsedTimeTotal += ste;

        QContactDetailFilter firstNameStartsA;
        firstNameStartsA.setDetailType(QContactName::Type, QContactName::FieldFirstName);
        firstNameStartsA.setValue("A");
        firstNameStartsA.setMatchFlags(QContactDetailFilter::MatchStartsWith);
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
        syncTimer.start();
        readContacts = manager.contacts(firstNameStartsA, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading filtered (" << readContacts.size() << "), no relationships, took" << ste << "milliseconds";
        elapsedTimeTotal += ste;

        QList<QContactId> idsToRemove;
        for (int j = 0; j < td.size(); ++j) {
            idsToRemove.append(retrievalId(td.at(j)));
        }

        syncTimer.start();
        manager.removeContacts(idsToRemove);
        ste = syncTimer.elapsed();
        qDebug() << "    removing test data took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;
    }

    qDebug() << "    removing prefill data";
    QList<QContactId> deleteIds;
    for (const QContact &c : prefillData) {
        deleteIds.append(c.id());
    }
    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    elapsedTimeTotal += deleteTime;
    qDebug() << "    removing prefill data took" << deleteTime << "milliseconds";

    return elapsedTimeTotal;
}

static qint64 synchronousOperations(QContactManager &manager, bool quickMode)
{
    // Time some synchronous operations.  First, generate the test data.
    QElapsedTimer syncTimer;
    qint64 elapsedTimeTotal = 0;
    QList<int> nbrContacts;
    if (quickMode) {
        nbrContacts << 100;
    } else {
        nbrContacts << 10 << 100 << 200 << 500 << 1000;
    }

    QList<QList<QContact> > testData;
    qDebug() << "--------";
    qDebug() << "Performing basic synchronous operations";
    qDebug() << "    generating test data for timings...";
    for (int i = 0; i < nbrContacts.size(); ++i) {
        int howMany = nbrContacts.at(i);
        QList<QContact> newTestData;
        newTestData.reserve(howMany);

        for (int j = 0; j < howMany; ++j) {
            // Use testing sync target, so 'local' won't be modified into 'was_local' via aggregation
            newTestData.append(generateContact(QString::fromLatin1("synchronousOperations")));
        }

        testData.append(newTestData);
    }

    // Perform the timings - these all create new contacts and assume an "empty" initial database
    for (int i = 0; i < testData.size(); ++i) {
        QList<QContact> td = testData.at(i);
        qint64 ste = 0;
        qDebug() << "    -> performing tests for" << td.size() << "contacts:";

        syncTimer.start();
        manager.saveContacts(&td);
        ste = syncTimer.elapsed();
        qDebug() << "    saving took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        QContactDetailFilter testingFilter;
        testingFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
        testingFilter.setValue(QString::fromLatin1("synchronousOperations"));

        QContactFetchHint fh;
        syncTimer.start();
        QList<QContact> readContacts = manager.contacts(testingFilter, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        if (readContacts.size() != td.size()) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << td.size();
        }
        qDebug() << "    reading all (" << readContacts.size() << "), all details, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type
                << QContactPhoneNumber::Type << QContactEmailAddress::Type);
        syncTimer.start();
        readContacts = manager.contacts(testingFilter, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        if (readContacts.size() != td.size()) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << td.size();
        }
        qDebug() << "    reading all, common details, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        fh.setOptimizationHints(QContactFetchHint::NoRelationships);
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
        syncTimer.start();
        readContacts = manager.contacts(testingFilter, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        if (readContacts.size() != td.size()) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << td.size();
        }
        qDebug() << "    reading all, no relationships, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type);
        syncTimer.start();
        readContacts = manager.contacts(testingFilter, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        if (readContacts.size() != td.size()) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << td.size();
        }
        qDebug() << "    reading all, display details + no rels, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        // Read the contacts, selected by ID
        QList<QContactId> idsToRetrieve;
        for (int j = 0; j < td.size(); ++j) {
            idsToRetrieve.append(retrievalId(td.at(j)));
        }

        syncTimer.start();
        readContacts = manager.contacts(idsToRetrieve, fh, 0);
        ste = syncTimer.elapsed();
        if (readContacts.size() != td.size()) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << td.size();
        }
        qDebug() << "    reading all by IDs, display details + no rels, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        // Read the same set using ID filtering
        QContactIdFilter idFilter;
        idFilter.setIds(idsToRetrieve);

        syncTimer.start();
        readContacts = manager.contacts(idFilter, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        if (readContacts.size() != td.size()) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << td.size();
        }
        qDebug() << "    reading all by ID filter, display details + no rels, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        // Read the same set, but filter everything out using syncTarget
        QContactDetailFilter aggregateFilter;
        aggregateFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
        aggregateFilter.setValue(QString::fromLatin1("aggregate"));

        syncTimer.start();
        readContacts = manager.contacts(idFilter & aggregateFilter, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        if (readContacts.size() != 0) {
            qWarning() << "Invalid retrieval count:" << readContacts.size() << "expecting:" << 0;
        }
        qDebug() << "    reading all by ID filter & aggregate, display details + no rels, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        QContactDetailFilter firstNameStartsA;
        firstNameStartsA.setDetailType(QContactName::Type, QContactName::FieldFirstName);
        firstNameStartsA.setValue("A");
        firstNameStartsA.setMatchFlags(QContactDetailFilter::MatchStartsWith);
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
        syncTimer.start();
        readContacts = manager.contacts(firstNameStartsA, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading filtered (" << readContacts.size() << "), no relationships, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;

        QList<QContactId> idsToRemove;
        for (int j = 0; j < td.size(); ++j) {
            idsToRemove.append(retrievalId(td.at(j)));
        }

        syncTimer.start();
        manager.removeContacts(idsToRemove);
        ste = syncTimer.elapsed();
        qDebug() << "    removing test data took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
        elapsedTimeTotal += ste;
    }

    return elapsedTimeTotal;
}

static qint64 performAsynchronousFetch(QContactManager &manager, bool quickMode)
{
    const int repeatCount = quickMode ? 1 : 3; // test caching effects
    qint64 elapsedTimeTotal = 0;
    QContactFetchRequest request;
    request.setManager(&manager);

    // Fetch all, no optimization hints
    for (int i = 0; i < repeatCount; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << "    " << i << ": Fetch completed in" << elapsed << "ms";
        elapsedTimeTotal += elapsed;
    }

    // Skip relationships
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::NoRelationships);
    request.setFetchHint(hint);

    for (int i = 0; i < repeatCount; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << "    "  << i << ": No-relationships fetch completed in" << elapsed << "ms";
        elapsedTimeTotal += elapsed;
    }

    // Reduce data access
    hint.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactName::Type << QContactAddress::Type);
    request.setFetchHint(hint);

    for (int i = 0; i < repeatCount; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << "    "  << i << ": Reduced data fetch completed in" << elapsed << "ms";
        elapsedTimeTotal += elapsed;
    }

    // Reduce number of results
    hint.setMaxCountHint(request.contacts().count() / 8);
    request.setFetchHint(hint);

    for (int i = 0; i < repeatCount; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << "    "  << i << ": Max count fetch completed in" << elapsed << "ms";
        elapsedTimeTotal += elapsed;
    }

    return elapsedTimeTotal;
}

static qint64 asynchronousOperations(QContactManager &manager, bool quickMode)
{
    const int numberContacts = quickMode ? 100 : 1000;
    QElapsedTimer totalTimeTimer;
    totalTimeTimer.start();

    qDebug() << "--------";
    qDebug() << "Performing asynchronous fetch with empty database";
    qint64 requestTime = performAsynchronousFetch(manager, quickMode);
    qDebug() << "    asynchronous fetch requests took:" << requestTime << "milliseconds";

    qDebug() << "--------";
    qDebug() << "Performing asynchronous save of" << numberContacts << "contacts";
    QList<QContact> testData;
    for (int i = 0; i < numberContacts; ++i) {
        testData.append(generateContact(QStringLiteral("asynchronousOperations")));
    }
    QElapsedTimer storeTimer;
    storeTimer.start();
    QContactSaveRequest sreq;
    sreq.setManager(&manager);
    sreq.setContacts(testData);
    sreq.start();
    sreq.waitForFinished();
    QList<QContact> savedContacts = sreq.contacts();
    qint64 storeTime = storeTimer.elapsed();
    qDebug() << "    saved" << numberContacts << "contacts in" << storeTime << "milliseconds";

    qDebug() << "--------";
    qDebug() << "Performing asynchronous fetch with filled database";
    requestTime = performAsynchronousFetch(manager, quickMode);
    qDebug() << "    asynchronous fetch requests took:" << requestTime << "milliseconds";

    qDebug() << "--------";
    qDebug() << "Performing asynchronous remove with filled database";
    QList<QContactId> deleteIds;
    for (const QContact &c : savedContacts) {
        deleteIds.append(c.id());
    }
    QElapsedTimer deleteTimer;
    deleteTimer.start();
    QContactRemoveRequest rreq;
    rreq.setManager(&manager);
    rreq.setContactIds(deleteIds);
    rreq.start();
    rreq.waitForFinished();
    qint64 deleteTime = deleteTimer.elapsed();
    qDebug() << "    asynchronous remove request took" << deleteTime << "milliseconds";

    return totalTimeTimer.elapsed();
}

qint64 simpleFilterAndSort(QContactManager &manager, bool quickMode)
{
    // Now we perform a simple create+filter+sort test, where contacts are saved in small chunks.
    qDebug() << "--------";
    qDebug() << "Starting save (chunks) / fetch (filter + sort) / delete (all) test...";
    QList<QContact> testData, testData2;
    const int chunkSize = quickMode ? 25 : 50;
    const int prefillCount = quickMode ? 250 : 1000;
    for (int i = 0; i < prefillCount/2; ++i) {
        testData.append(generateContact(QString::fromLatin1("simpleFilterAndSort"), true));
        testData2.append(generateContact(QString::fromLatin1("simpleFilterAndSort2"), true));
    }

    QList<QList<QContact> > chunks, chunks2;
    for (int i = 0; i < testData.size(); i += chunkSize) {
        QList<QContact> chunk, chunk2;
        for (int j = 0; j < chunkSize && ((i+j) < testData.size()); ++j) {
            chunk.append(testData[i+j]);
            chunk2.append(testData2[i+j]);
        }
        chunks.append(chunk);
        chunks2.append(chunk2);
    }

    QContactFetchHint listDisplayFetchHint;
    listDisplayFetchHint.setDetailTypesHint(QList<QContactDetail::DetailType>()
            << QContactDisplayLabel::Type << QContactName::Type << QContactAvatar::Type);
    QContactSortOrder sort;
    sort.setDetailType(QContactDisplayLabel::Type, QContactDisplayLabel__FieldLabelGroup);
    QContactDetailFilter phoneFilter;
    phoneFilter.setDetailType(QContactPhoneNumber::Type); // existence filter, don't care about value.
    QContactDetailFilter syncTargetFilter;
    syncTargetFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    syncTargetFilter.setValue(QStringLiteral("aggregate"));

    qDebug() << "    storing" << prefillCount << "contacts... this will take a while...";
    QElapsedTimer syncTimer;
    syncTimer.start();
    for (int i = 0; i < chunks.size(); ++i) {
        manager.saveContacts(&chunks[i]);
    }
    for (int i = 0; i < chunks2.size(); ++i) {
        manager.saveContacts(&chunks2[i]);
    }
    qint64 saveTime = syncTimer.elapsed();
    qDebug() << "    stored" << (testData.size()+testData2.size()) << "contacts in" << saveTime << "milliseconds";

    qDebug() << "    retrieving aggregate contacts with filter, sort order, and fetch hint applied";
    syncTimer.start();
    QList<QContact> filteredSorted = manager.contacts(syncTargetFilter & phoneFilter, sort, listDisplayFetchHint);
    qint64 fetchTime = syncTimer.elapsed();
    qDebug() << "    retrieved" << filteredSorted.size() << "contacts in" << fetchTime << "milliseconds";

    QList<QContactId> deleteIds;
    for (const QList<QContact> &chunk : chunks) {
        for (const QContact &c : chunk) {
            deleteIds.append(c.id());
        }
    }
    for (const QList<QContact> &chunk2 : chunks2) {
        for (const QContact &c : chunk2) {
            deleteIds.append(c.id());
        }
    }

    syncTimer.start();
    manager.removeContacts(deleteIds);
    qint64 deleteTime = syncTimer.elapsed();
    qDebug() << "    deleted" << deleteIds.size() << "contacts in" << deleteTime << "milliseconds";

    if (filteredSorted.size() == 0) {
        qWarning() << "Zero aggregate contacts found.  Are you sure you're running with privileged permissions?";
    }

    return saveTime + fetchTime + deleteTime;
}

int main(int argc, char  *argv[])
{
    QCoreApplication application(argc, argv);

    const QStringList &args(application.arguments());
    QStringList functionArgs;

    if (args.size() <= 1) {
        qDebug() << "usage: fetchtimes [--stable] [--quick] --help|--all|--function=<function>";
        return 0;
    } else if (args.contains("--help") || args.contains("-h")) {
        qDebug() << "usage: fetchtimes [--stable] --help|--all|--quick|<function>";
        qDebug() << "If --stable is specified, a stable prng seed will be used.";
        qDebug() << "If --quick is specified, the benchmark will complete more quickly (but results will have higher variance)";
        qDebug() << "Available functions:";
        qDebug() << "    simpleFilterAndSort";
        qDebug() << "    asynchronousOperations";
        qDebug() << "    synchronousOperations";
        qDebug() << "    smallBatchWithExistingData";
        qDebug() << "    aggregationOperations";
        qDebug() << "    smallBatchPresenceUpdate";
        qDebug() << "    entireBatchPresenceUpdate";
        qDebug() << "    scalingPresenceUpdate";
        qDebug() << "    nonAggregatedPresenceUpdate";
        qDebug() << "    aggregatedPresenceUpdate";
        return 0;
    }

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i).startsWith(QStringLiteral("--function="))) {
            functionArgs.append(args.at(i).mid(11));
        } else if (args.at(i).compare(QStringLiteral("-f")) == 0 && args.size() > (i+1)) {
            i = i+1;
            functionArgs.append(args.at(i));
        }
    }

    const bool quickMode(args.contains(QStringLiteral("-q")) || args.contains(QStringLiteral("--quick")));
    const bool stable(args.contains(QStringLiteral("-s")) || args.contains(QStringLiteral("--stable")));
    const bool runAll(args.contains(QStringLiteral("-a")) || args.contains(QStringLiteral("--all")));

    QMap<QString, QString> parameters;
    parameters.insert(QString::fromLatin1("autoTest"), QString::fromLatin1("true"));
    parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("false"));
    QContactManager manager(QString::fromLatin1("org.nemomobile.contacts.sqlite"), parameters);
    QList<QContactId> aggregateIds = manager.contactIds(); // ensure the database has been created.
    if (!aggregateIds.isEmpty()) {
        qWarning() << "Database not empty at beginning of test!  Contains:" << aggregateIds.size() << "aggregate contacts!";
    }

    qint64 elapsedTimeTotal = 0;
    qsrand(stable ? 42 : QDateTime::currentDateTime().time().second());
    elapsedTimeTotal += (runAll || functionArgs.contains("simpleFilterAndSort")) ? simpleFilterAndSort(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("asynchronousOperations")) ? asynchronousOperations(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("synchronousOperations")) ? synchronousOperations(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("smallBatchWithExistingData")) ? smallBatchWithExistingData(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("aggregationOperations")) ? aggregationOperations(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("smallBatchPresenceUpdate")) ? smallBatchPresenceUpdate(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("entireBatchPresenceUpdate")) ? entireBatchPresenceUpdate(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("scalingPresenceUpdate")) ? scalingPresenceUpdate(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("nonAggregatedPresenceUpdate")) ? nonAggregatedPresenceUpdate(manager, quickMode) : 0;
    elapsedTimeTotal += (runAll || functionArgs.contains("aggregatedPresenceUpdate")) ? aggregatedPresenceUpdate(manager, quickMode) : 0;

    qDebug() << "\n\nCumulative elapsed time:" << elapsedTimeTotal << "milliseconds";
    return 0;
}
