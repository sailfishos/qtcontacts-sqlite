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

#define QT_STATICPLUGIN

#include "../../util.h"
#include "../../qcontactmanagerdataholder.h"

#include "../../../src/extensions/qcontactdeactivated.h"
#include "../../../src/extensions/qcontactstatusflags.h"

#include <QLocale>

//TESTED_COMPONENT=src/contacts
//TESTED_CLASS=
//TESTED_FILES=

// Q_ASSERT replacement, since we often run in release builds
#define Q_FATAL_VERIFY(statement)                                         \
do {                                                                      \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__)) \
        qFatal("severe failure encountered, test cannot continue");       \
} while (0)

#define Q_ASSERT_VERIFY(statement) Q_FATAL_VERIFY(statement)

/*
 * This test is mostly just for testing sorting and filtering -
 * having it in tst_QContactManager makes maintenance more
 * difficult!
 *
 * Note: this test maintains the semantics of qtpim filtering, and
 * thus works correctly only in nonprivileged mode (in privileged
 * mode aggregation is used, which complicates filtering results).
 * The test enforces access to the nonprivileged database.
 *
 * When changing the results of filtering, ensure that the privileged
 * semantics are tested by the aggregation autotest.
 */

Q_DECLARE_METATYPE(QVariant)
Q_DECLARE_METATYPE(QContactManager*)

Q_DECLARE_METATYPE(QContactDetail::DetailType)

static int detailField(int field) { return field; }

static bool validDetailField(int field) { return (field != -1); }

static bool validDetailInfo(const QPair<QContactDetail::DetailType, int> &info)
{
    return validDetailType(info.first) && validDetailField(info.second);
}

/*
 * Global variables:
 * These are the definition and field names used by the actions for their matching.
 */
QMap<QString, QPair<QString, QString> > defAndFieldNamesForTypeForActions;


/*
 * We use this code to compare the output and expected lists of filtering
 * where no sort order is implied.
 * TODO: use this instead of QCOMPARE in the various filtering tests!
 */
#define QCOMPARE_UNSORTED(output, expected) if (output.size() != expected.size()) { \
                                                QCOMPARE(output, expected); \
                                            } else { \
                                                for (int i = 0; i < output.size(); i++) { \
                                                    if (!expected.contains(output.at(i))) { \
                                                        QCOMPARE(output, expected); \
                                                    } \
                                                } \
                                            }

class tst_QContactManagerFiltering : public QObject
{
Q_OBJECT

public:
    tst_QContactManagerFiltering();
    virtual ~tst_QContactManagerFiltering();

private:
    void dumpContactDifferences(const QContact& a, const QContact& b);
    void dumpContact(const QContact &c);
    void dumpContacts();
    bool isSuperset(const QContact& ca, const QContact& cb);

#ifdef DETAIL_DEFINITION_SUPPORTED
    QPair<QString, QString> definitionAndField(QContactManager *cm, QVariant::Type type, bool *nativelyFilterable);
#endif
    QList<QContactId> prepareModel(QContactManager* cm); // add the standard contacts

    QString convertIds(QList<QContactId> allIds, QList<QContactId> ids, QChar minimumContact = 'a', QChar maximumContact = 'z'); // convert back to "abcd"

    QContact createContact(QContactManager* cm, QContactType::TypeValues type, QString name);

    typedef QContactDetail::DetailType TypeIdentifier;
    typedef int FieldIdentifier;

    typedef QPair<TypeIdentifier, FieldIdentifier> FieldSelector;
    QMap<QContactManager*, QMap<QString, FieldSelector> > defAndFieldNamesForTypePerManager;
    QMultiMap<QContactManager*, QContactId> contactsAddedToManagers;
    QMultiMap<QContactManager*, QContactId> transientContacts;
    QMultiMap<QContactManager*, QString> detailDefinitionsAddedToManagers;
    QList<QContactManager*> managers;
    QScopedPointer<QContactManagerDataHolder> managerDataHolder;

    QTestData& newMRow(const char *tag, QContactManager *cm);

private slots:

    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void rangeFiltering(); // XXX should take all managers
    void rangeFiltering_data();

    void detailStringFiltering(); // XXX should take all managers
    void detailStringFiltering_data();

    void detailPhoneNumberFiltering();
    void detailPhoneNumberFiltering_data();

    void statusFlagsFiltering();
    void statusFlagsFiltering_data();

    void deactivation();
    void deactivation_data();

    void detailVariantFiltering();
    void detailVariantFiltering_data();

    void intersectionFiltering();
    void intersectionFiltering_data();

    void unionFiltering();
    void unionFiltering_data();

    void relationshipFiltering();
    void relationshipFiltering_data();

#if 0
    // These tests should be supported...
    void changelogFiltering();
    void changelogFiltering_data();
#endif

    void idListFiltering();
    void idListFiltering_data();

    void convenienceFiltering();
    void convenienceFiltering_data();

    void sorting(); // XXX should take all managers
    void sorting_data();

    void multiSorting();
    void multiSorting_data();

    void invalidFiltering_data();
    void invalidFiltering();

    void allFiltering_data();
    void allFiltering();

    void fetchHint_data();
    void fetchHint();
};

tst_QContactManagerFiltering::tst_QContactManagerFiltering()
{
    // In order to make our tests reliable, set the C locale
    QLocale::setDefault(QLocale::c());
}

tst_QContactManagerFiltering::~tst_QContactManagerFiltering()
{
}

void tst_QContactManagerFiltering::initTestCase()
{
    managerDataHolder.reset(new QContactManagerDataHolder(true));

    // firstly, build a list of the managers we wish to test.
    QStringList managerNames;
    managerNames << QLatin1String("org.nemomobile.contacts.sqlite"); // only test nemo sqlite backend

    foreach (const QString &mgr, managerNames) {
        QMap<QString, QString> params;
        params.insert(QStringLiteral("autoTest"), QStringLiteral("true"));
        params.insert(QStringLiteral("nonprivileged"), QStringLiteral("true"));
        QString mgrUri = QContactManager::buildUri(mgr, params);
        QContactManager* cm = QContactManager::fromUri(mgrUri);
        cm->setObjectName(mgr);
        managers.append(cm);

        if (mgr == "memory") {
            params.insert("id", "tst_QContactManager");
            mgrUri = QContactManager::buildUri(mgr, params);
            cm = QContactManager::fromUri(mgrUri);
            cm->setObjectName("memory[params]");
            managers.append(cm);
        }
    }

    // for each manager that we wish to test, prepare the model.
    foreach (QContactManager* cm, managers) {
        QList<QContactId> addedContacts = prepareModel(cm);
        if (addedContacts != contactsAddedToManagers.values(cm)) {
            qDebug() << "prepareModel returned:" << addedContacts;
            qDebug() << "contactsAdded are:    " << contactsAddedToManagers.values(cm);
            qFatal("returned list different from saved contacts list!");
        }
    }
}

void tst_QContactManagerFiltering::cleanupTestCase()
{
    // first, remove any contacts that we've added to any managers.
    foreach (QContactManager* manager, managers) {
        QList<QContactId> contactIds = contactsAddedToManagers.values(manager);
        manager->removeContacts(contactIds, 0);
    }
    contactsAddedToManagers.clear();

#ifdef MUTABLE_SCHEMA_SUPPORTED
    // then, remove any detail definitions that we've added.
    foreach (QContactManager* manager, managers) {
        QStringList definitionNames = detailDefinitionsAddedToManagers.values(manager);
        foreach (const QString& definitionName, definitionNames) {
            manager->removeDetailDefinition(definitionName);
        }
    }
    detailDefinitionsAddedToManagers.clear();
#endif

    // finally, we can delete all of our manager instances
    qDeleteAll(managers);
    managers.clear();
    defAndFieldNamesForTypePerManager.clear();

    // And restore old contacts
    managerDataHolder.reset(0);
}

void tst_QContactManagerFiltering::cleanup()
{
    foreach (QContactManager* manager, managers) {
        QList<QContactId> contactIds = transientContacts.values(manager);
        manager->removeContacts(contactIds, 0);
    }
    transientContacts.clear();
}

QString tst_QContactManagerFiltering::convertIds(QList<QContactId> allIds, QList<QContactId> ids, QChar minimumContact, QChar maximumContact)
{
    QString ret;
    /* Expected is of the form "abcd".. it's possible that there are some extra contacts */
    for (int i = 0; i < ids.size(); i++) {
        if (allIds.indexOf(ids.at(i)) >= 0) {
            QChar curr = ('a' + allIds.indexOf(ids.at(i)));
            if (curr >= minimumContact && curr <= maximumContact) {
                ret += curr;
            }
        }
    }

    return ret;
}

QTestData& tst_QContactManagerFiltering::newMRow(const char *tag, QContactManager *cm)
{
    // allocate a tag
    QString foo = QString("%1[%2]").arg(tag).arg(cm->objectName());
    return QTest::newRow(foo.toLatin1().constData());
}


void tst_QContactManagerFiltering::detailStringFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<TypeIdentifier>("defname");
    QTest::addColumn<FieldIdentifier>("fieldname");
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<int>("matchflags");
    QTest::addColumn<QString>("expected");

    QVariant ev; // empty variant
    QString es; // empty string

    TypeIdentifier name = detailType<QContactName>();
    FieldIdentifier firstname = QContactName::FieldFirstName;
    FieldIdentifier lastname = QContactName::FieldLastName;
    FieldIdentifier middlename = QContactName::FieldMiddleName;
    FieldIdentifier prefixname = QContactName::FieldPrefix;
    FieldIdentifier suffixname = QContactName::FieldSuffix;
    TypeIdentifier nickname = detailType<QContactNickname>();
    FieldIdentifier nicknameField = QContactNickname::FieldNickname;
    TypeIdentifier emailaddr = detailType<QContactEmailAddress>();
    FieldIdentifier emailfield = QContactEmailAddress::FieldEmailAddress;
    TypeIdentifier phonenumber = detailType<QContactPhoneNumber>();
    FieldIdentifier number = QContactPhoneNumber::FieldNumber;

    TypeIdentifier noType(QContactDetail::TypeUndefined);
    FieldIdentifier noField(-1);

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        newMRow("Name == Aaro", manager) << manager << name << firstname << QVariant("Aaro") << 0 << es;
        newMRow("Name == Aaron", manager) << manager << name << firstname << QVariant("Aaron") << 0 << "a";
        newMRow("Name == aaron", manager) << manager << name << firstname << QVariant("aaron") << 0 << "a";
        newMRow("Name == Aaron, case sensitive", manager) << manager << name << firstname << QVariant("Aaron") << (int)(QContactFilter::MatchCaseSensitive) << "a";
        newMRow("Name == aaron, case sensitive", manager) << manager << name << firstname << QVariant("aaron") << (int)(QContactFilter::MatchCaseSensitive) << es;

        newMRow("Name is empty", manager) << manager << name << firstname << QVariant("") << 0 << es;
        newMRow("Last name is empty", manager) << manager << name << lastname << QVariant("") << 0 << "hijk";

        newMRow("Name == A, begins", manager) << manager << name << firstname << QVariant("A") << (int)(QContactFilter::MatchStartsWith) << "a";
        newMRow("Name == Aaron, begins", manager) << manager << name << firstname << QVariant("Aaron") << (int)(QContactFilter::MatchStartsWith) << "a";
        newMRow("Name == aaron, begins", manager) << manager << name << firstname << QVariant("aaron") << (int)(QContactFilter::MatchStartsWith) << "a";
        newMRow("Name == Aaron, begins, case sensitive", manager) << manager << name << firstname << QVariant("Aaron") << (int)(QContactFilter::MatchStartsWith | QContactFilter::MatchCaseSensitive) << "a";
        newMRow("Name == aaron, begins, case sensitive", manager) << manager << name << firstname << QVariant("aaron") << (int)(QContactFilter::MatchStartsWith | QContactFilter::MatchCaseSensitive) << es;
        newMRow("Name == Aaron1, begins", manager) << manager << name << firstname << QVariant("Aaron1") << (int)(QContactFilter::MatchStartsWith) << es;
        newMRow("Last name == A, begins", manager) << manager << name << lastname << QVariant("A") << (int)(QContactFilter::MatchStartsWith) << "abc";
        newMRow("Last name == Aaronson, begins", manager) << manager << name << lastname << QVariant("Aaronson") << (int)(QContactFilter::MatchStartsWith) << "a";
        newMRow("Last Name == Aaronson1, begins", manager) << manager << name << lastname << QVariant("Aaronson1") << (int)(QContactFilter::MatchStartsWith) << es;

        newMRow("Name == Aar, begins", manager) << manager << name << firstname << QVariant("Aar") << (int)(QContactFilter::MatchStartsWith) << "a";
        newMRow("Name == aar, begins", manager) << manager << name << firstname << QVariant("aar") << (int)(QContactFilter::MatchStartsWith) << "a";
        newMRow("Name == Aar, begins, case sensitive", manager) << manager << name << firstname << QVariant("Aar") << (int)(QContactFilter::MatchStartsWith | QContactFilter::MatchCaseSensitive) << "a";
        newMRow("Name == aar, begins, case sensitive", manager) << manager << name << firstname << QVariant("aar") << (int)(QContactFilter::MatchStartsWith | QContactFilter::MatchCaseSensitive) << es;

        newMRow("Name == aro, contains", manager) << manager << name << firstname << QVariant("aro") << (int)(QContactFilter::MatchContains) << "a";
        newMRow("Name == ARO, contains", manager) << manager << name << firstname << QVariant("ARO") << (int)(QContactFilter::MatchContains) << "a";
        newMRow("Name == aro, contains, case sensitive", manager) << manager << name << firstname << QVariant("aro") << (int)(QContactFilter::MatchContains | QContactFilter::MatchCaseSensitive) << "a";
        newMRow("Name == ARO, contains, case sensitive", manager) << manager << name << firstname << QVariant("ARO") << (int)(QContactFilter::MatchContains | QContactFilter::MatchCaseSensitive) << es;

        newMRow("Name == ron, ends", manager) << manager << name << firstname << QVariant("ron") << (int)(QContactFilter::MatchEndsWith) << "a";
        newMRow("Name == ARON, ends", manager) << manager << name << firstname << QVariant("ARON") << (int)(QContactFilter::MatchEndsWith) << "a";
        newMRow("Name == aron, ends, case sensitive", manager) << manager << name << firstname << QVariant("aron") << (int)(QContactFilter::MatchEndsWith | QContactFilter::MatchCaseSensitive) << "a";
        newMRow("Name == ARON, ends, case sensitive", manager) << manager << name << firstname << QVariant("ARON") << (int)(QContactFilter::MatchEndsWith | QContactFilter::MatchCaseSensitive) << es;
        newMRow("Last name == n, ends", manager) << manager << name << lastname << QVariant("n") << (int)(QContactFilter::MatchEndsWith) << "abc";

        newMRow("Name == Aaron, fixed", manager) << manager << name << firstname << QVariant("Aaron") << (int)(QContactFilter::MatchFixedString) << "a";
        newMRow("Name == aaron, fixed", manager) << manager << name << firstname << QVariant("aaron") << (int)(QContactFilter::MatchFixedString) << "a";
        newMRow("Name == Aaron, fixed, case sensitive", manager) << manager << name << firstname << QVariant("Aaron") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchCaseSensitive) << "a";
        newMRow("Name == aaron, fixed, case sensitive", manager) << manager << name << firstname << QVariant("aaron") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchCaseSensitive) << es;

        // middle name
#ifdef DETAIL_DEFINITION_SUPPORTED
        if (manager->detailDefinitions().value(QContactName::DefinitionName).fields().contains(QContactName::FieldMiddleName))
#endif
            newMRow("MName == Arne", manager) << manager << name << middlename << QVariant("Arne") << (int)(QContactFilter::MatchContains) << "a";

        // prefix
#ifdef DETAIL_DEFINITION_SUPPORTED
        if (manager->detailDefinitions().value(QContactName::DefinitionName).fields().contains(QContactName::FieldPrefix))
#endif
            newMRow("Prefix == Sir", manager) << manager << name << prefixname << QVariant("Sir") << (int)(QContactFilter::MatchContains) << "a";

        // suffix
#ifdef DETAIL_DEFINITION_SUPPORTED
        if (manager->detailDefinitions().value(QContactName::DefinitionName).fields().contains(QContactName::FieldSuffix))
#endif
            newMRow("Suffix == Dr.", manager) << manager << name << suffixname << QVariant("Dr.") << (int)(QContactFilter::MatchContains) << "a";

        // nickname
#ifdef DETAIL_DEFINITION_SUPPORTED
        if (manager->detailDefinitions().contains(QContactNickname::DefinitionName)) {
#endif
            newMRow("Nickname detail exists", manager) << manager << nickname << noField << QVariant() << 0 << "ab";
            newMRow("Nickname == Aaron, contains", manager) << manager << nickname << nicknameField << QVariant("Aaron") << (int)(QContactFilter::MatchContains) << "a";
#ifdef DETAIL_DEFINITION_SUPPORTED
        }
#endif

        // email
#ifdef DETAIL_DEFINITION_SUPPORTED
        if (manager->detailDefinitions().contains(QContactEmailAddress::DefinitionName)) {
#endif
            newMRow("Email == Aaron@Aaronson.com", manager) << manager << emailaddr << emailfield << QVariant("Aaron@Aaronson.com") << 0 << "a";
            newMRow("Email == Aaron@Aaronsen.com", manager) << manager << emailaddr << emailfield << QVariant("Aaron@Aaronsen.com") << 0 << es;
#ifdef DETAIL_DEFINITION_SUPPORTED
        }
#endif
        
        // phone number
#ifdef DETAIL_DEFINITION_SUPPORTED
        if (manager->detailDefinitions().contains(QContactPhoneNumber::DefinitionName)) {
#endif
            newMRow("Phone number detail exists", manager) << manager << phonenumber << noField << QVariant("") << 0 << "ab";
            newMRow("Phone number = 5551212", manager) << manager << phonenumber << number << QVariant("5551212") << (int) QContactFilter::MatchExactly << "a";
            newMRow("Phone number = 555, contains", manager) << manager << phonenumber << number << QVariant("555") << (int) QContactFilter::MatchContains << "ab";
            newMRow("Phone number = 555, starts with", manager) << manager << phonenumber << number << QVariant("555") <<  (int) QContactFilter::MatchStartsWith << "a";
            newMRow("Phone number = 1212, ends with", manager) << manager << phonenumber << number << QVariant("1212") << (int) QContactFilter::MatchEndsWith << "a";
            newMRow("Phone number = 555-1212, match phone number", manager) << manager << phonenumber << number << QVariant("555-1212") << (int) QContactFilter::MatchPhoneNumber << "a"; // hyphens will be ignored by the match algorithm
#ifdef DETAIL_DEFINITION_SUPPORTED
        }
#endif
        
        /* Converting other types to strings */
        FieldSelector defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Integer");
        if (validDetailInfo(defAndFieldNames)) {
            QTest::newRow("integer == 20") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("20") << 0 << es;
            QTest::newRow("integer == 20, as string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("20") << (int)(QContactFilter::MatchFixedString) << "b";
            QTest::newRow("integer == 20, begins with, string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("20") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchStartsWith) << "b";
            QTest::newRow("integer == 2, begins with, string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("2") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchStartsWith) << "b";
            QTest::newRow("integer == 20, ends with, string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("20") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchEndsWith) << "bc";
            QTest::newRow("integer == 0, ends with, string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("0") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchEndsWith) << "bc";
            QTest::newRow("integer == 20, contains, string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("20") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchContains) << "bc";
            QTest::newRow("integer == 0, contains, string") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant("0") << (int)(QContactFilter::MatchFixedString | QContactFilter::MatchContains) << "bc";
        }

        /* Detail filter semantics: empty definition or field */
        newMRow("Empty Definition Name", manager) << manager << noType << lastname << QVariant("A") << (int)(QContactFilter::MatchStartsWith) << es; // empty definition name means filter matches nothing
        newMRow("Empty Def And Field Name", manager) << manager << noType << noField << QVariant("A") << (int)(QContactFilter::MatchStartsWith) << es; // as above
        newMRow("Empty Field Name", manager) << manager << name << noField << QVariant("A") << (int)(QContactFilter::MatchStartsWith) << "abcdefghijk"; // empty field name matches any with a name detail
    }
}

void tst_QContactManagerFiltering::detailStringFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(TypeIdentifier, defname);
    QFETCH(FieldIdentifier, fieldname);
    QFETCH(QVariant, value);
    QFETCH(QString, expected);
    QFETCH(int, matchflags);

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    QContactDetailFilter df;
    setFilterDetail(df, defname, fieldname);
    df.setValue(value);
    if ((matchflags & QContactFilter::MatchCaseSensitive) == 0) {
        // Case insensitivity only applies to MatchFixedString
        matchflags |= QContactFilter::MatchFixedString;
    }
    df.setMatchFlags(QContactFilter::MatchFlags(matchflags));

    if (cm->managerName() == "memory") {
        /* At this point, since we're using memory, assume the filter isn't really supported */
        QVERIFY(cm->isFilterSupported(df) == false);
    }

    ids = cm->contactIds(df);

    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QEXPECT_FAIL("integer == 20", "Not sure if this should pass or fail", Continue);
    QCOMPARE_UNSORTED(output, expected);
}

void tst_QContactManagerFiltering::detailPhoneNumberFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<TypeIdentifier>("defname");
    QTest::addColumn<FieldIdentifier>("fieldname");
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<int>("matchflags");
    QTest::addColumn<QString>("expected");

    // ITU-T standard keypad collation:
    // 2 = abc, 3 = def, 4 = ghi, 5 = jkl, 6 = mno, 7 = pqrs, 8 = tuv, 9 = wxyz, 0 = space

    TypeIdentifier phoneDef = detailType<QContactPhoneNumber>();
    FieldIdentifier phoneField = QContactPhoneNumber::FieldNumber;

    const int mpn = (int)QContactFilter::MatchPhoneNumber;
    const int msw = (int)QContactFilter::MatchStartsWith;

    // purely to test phone number filtering.
    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        // now do phone number matching - first, aaron's phone number
        QTest::newRow("a phone hyphen") << manager << phoneDef << phoneField << QVariant(QString("555-1212")) << mpn << "a";
        // An initial-plus variant is not the same number, so this test is not valid:
        //QTest::newRow("a phone plus") << manager << phoneDef << phoneField << QVariant(QString("+5551212")) << mpn << "a";
        QTest::newRow("a phone brackets") << manager << phoneDef << phoneField << QVariant(QString("(555)1212")) << mpn << "a";
        QTest::newRow("a phone nospaces") << manager << phoneDef << phoneField << QVariant(QString("5551212")) << mpn << "a";
        QTest::newRow("a phone single space") << manager << phoneDef << phoneField << QVariant(QString("555 1212")) << mpn << "a";
        QTest::newRow("a phone random spaces") << manager << phoneDef << phoneField << QVariant(QString("55 512 12")) << mpn << "a";
        QTest::newRow("a phone every space") << manager << phoneDef << phoneField << QVariant(QString("5 5 5 1 2 1 2")) << mpn << "a";
        QTest::newRow("a phone hyphen") << manager << phoneDef << phoneField << QVariant(QString("555-1212")) << mpn << "a";
        QTest::newRow("a phone brackets") << manager << phoneDef << phoneField << QVariant(QString("5(55)1212")) << mpn << "a";
        QTest::newRow("a phone brackets hyphen") << manager << phoneDef << phoneField << QVariant(QString("5(55)1-212")) << mpn << "a";
        QTest::newRow("a phone brackets hyphen spaces") << manager << phoneDef << phoneField << QVariant(QString("5 (55) 1-212")) << mpn << "a";

        // XXX TODO: should we test for character to number conversions (eg, dial 1800-PESTCONTROL) etc ?
        //QTest::newRow("a phone characters") << manager << phoneDef << phoneField << QVariant(QString("jjj1a1a")) << mpn << "a"; // 5551212
        //QTest::newRow("a phone characters") << manager << phoneDef << phoneField << QVariant(QString("jkl1b1a")) << mpn << "a"; // 5551212

        // then matches bob's phone number (which has the initial-plus)
        QTest::newRow("b phone hyphen") << manager << phoneDef << phoneField << QVariant(QString("5555-3456")) << mpn << "b";
        QTest::newRow("b phone plus") << manager << phoneDef << phoneField << QVariant(QString("+55553456")) << mpn << "b";
        QTest::newRow("b phone brackets") << manager << phoneDef << phoneField << QVariant(QString("(5555)3456")) << mpn << "b";
        QTest::newRow("b phone nospaces") << manager << phoneDef << phoneField << QVariant(QString("55553456")) << mpn << "b";
        QTest::newRow("b phone single space") << manager << phoneDef << phoneField << QVariant(QString("5555 3456")) << mpn << "b";
        QTest::newRow("b phone random spaces") << manager << phoneDef << phoneField << QVariant(QString("555 534 56")) << mpn << "b";
        QTest::newRow("b phone every space") << manager << phoneDef << phoneField << QVariant(QString("5 5 5 5 3 4 5 6")) << mpn << "b";
        QTest::newRow("b phone plus hyphen") << manager << phoneDef << phoneField << QVariant(QString("+5555-3456")) << mpn << "b";
        QTest::newRow("b phone plus brackets") << manager << phoneDef << phoneField << QVariant(QString("+5(555)3456")) << mpn << "b";
        QTest::newRow("b phone plus brackets hyphen") << manager << phoneDef << phoneField << QVariant(QString("+5(555)3-456")) << mpn << "b";
        QTest::newRow("b phone plus brackets hyphen spaces") << manager << phoneDef << phoneField << QVariant(QString("+55 (55) 3-456")) << mpn << "b";

        // then match no phone numbers (negative testing) -- 555-9999 matches nobody in our test set.
        QTest::newRow("no phone hyphen") << manager << phoneDef << phoneField << QVariant(QString("555-9999")) << mpn << "";
        QTest::newRow("no phone plus") << manager << phoneDef << phoneField << QVariant(QString("+5559999")) << mpn << "";
        QTest::newRow("no phone brackets") << manager << phoneDef << phoneField << QVariant(QString("(555)9999")) << mpn << "";
        QTest::newRow("no phone nospaces") << manager << phoneDef << phoneField << QVariant(QString("5559999")) << mpn << "";
        QTest::newRow("no phone single space") << manager << phoneDef << phoneField << QVariant(QString("555 9999")) << mpn << "";
        QTest::newRow("no phone random spaces") << manager << phoneDef << phoneField << QVariant(QString("55 599 99")) << mpn << "";
        QTest::newRow("no phone every space") << manager << phoneDef << phoneField << QVariant(QString("5 5 5 9 9 9 9")) << mpn << "";
        QTest::newRow("no phone plus hyphen") << manager << phoneDef << phoneField << QVariant(QString("+555-9999")) << mpn << "";
        QTest::newRow("no phone plus brackets") << manager << phoneDef << phoneField << QVariant(QString("+5(55)9999")) << mpn << "";
        QTest::newRow("no phone plus brackets hyphen") << manager << phoneDef << phoneField << QVariant(QString("+5(55)9-999")) << mpn << "";
        QTest::newRow("no phone plus brackets hyphen spaces") << manager << phoneDef << phoneField << QVariant(QString("+5 (55) 9-999")) << mpn << "";

        // then match both aaron and bob via starts with
        QTest::newRow("ab phone starts nospace") << manager << phoneDef << phoneField << QVariant(QString("555")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts hyphen") << manager << phoneDef << phoneField << QVariant(QString("555-")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts space") << manager << phoneDef << phoneField << QVariant(QString("55 5")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts brackets") << manager << phoneDef << phoneField << QVariant(QString("(555)")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts plus") << manager << phoneDef << phoneField << QVariant(QString("+555")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts hyphen space") << manager << phoneDef << phoneField << QVariant(QString("5 55-")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts hyphen space brackets") << manager << phoneDef << phoneField << QVariant(QString("5 (55)-")) << (mpn | msw) << "ab";
        QTest::newRow("ab phone starts hyphen space brackets plus") << manager << phoneDef << phoneField << QVariant(QString("+5 (55)-")) << (mpn | msw) << "ab";
    }
}

void tst_QContactManagerFiltering::detailPhoneNumberFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(TypeIdentifier, defname);
    QFETCH(FieldIdentifier, fieldname);
    QFETCH(QVariant, value);
    QFETCH(int, matchflags);
    QFETCH(QString, expected);

    // note: this test is exactly the same as string filtering, but uses different fields and specific matchflags.

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    QContactDetailFilter df;
    setFilterDetail(df, defname, fieldname);
    df.setValue(value);
    df.setMatchFlags(QContactFilter::MatchFlags(matchflags));

    if (cm->managerName() == "memory") {
        /* At this point, since we're using memory, assume the filter isn't really supported */
        QVERIFY(cm->isFilterSupported(df) == false);
    }

    ids = cm->contactIds(df);

    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    //QSKIP("TODO: fix default implementation of phone number matching!");
    QCOMPARE_UNSORTED(output, expected);
}

void tst_QContactManagerFiltering::statusFlagsFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *cm = managers.at(i);
        QTest::newRow(qPrintable(cm->objectName())) << cm;
    }
}

void tst_QContactManagerFiltering::statusFlagsFiltering()
{
    QFETCH(QContactManager*, cm);

    // Test for correct matching of all contact properties
    QSet<QContactId> phoneNumberIds = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::HasPhoneNumber, QContactFilter::MatchContains)).toSet();
    QSet<QContactId> emailAddressIds = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::HasEmailAddress, QContactFilter::MatchContains)).toSet();
    QSet<QContactId> onlineAccountIds = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::HasOnlineAccount, QContactFilter::MatchContains)).toSet();
    QSet<QContactId> onlineIds = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::IsOnline, QContactFilter::MatchContains)).toSet();
    QSet<QContactId> deactivatedIds = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains)).toSet();

    // Also test for combination tests
    QContactFilter filter(QContactStatusFlags::matchFlags(QContactStatusFlags::HasPhoneNumber | QContactStatusFlags::HasEmailAddress, QContactFilter::MatchContains));
    QSet<QContactId> phoneAndEmailIds = cm->contactIds(filter).toSet();

    filter = QContactStatusFlags::matchFlags(QContactStatusFlags::HasPhoneNumber, QContactFilter::MatchExactly);
    QSet<QContactId> phoneOnlyIds = cm->contactIds(filter).toSet();

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    foreach (const QContact &contact, cm->contacts(contacts)) {
        QContactId contactId(ContactId::apiId(contact));

        const bool hasPhoneNumber = !contact.details<QContactPhoneNumber>().isEmpty();
        const bool hasEmailAddress = !contact.details<QContactEmailAddress>().isEmpty();
        const bool hasOnlineAccount = !contact.details<QContactOnlineAccount>().isEmpty();

        QContactGlobalPresence presence = contact.detail<QContactGlobalPresence>();
        QContactPresence::PresenceState presenceState = presence.presenceState();
        const bool isOnline = (presenceState > QContactPresence::PresenceUnknown) && (presenceState < QContactPresence::PresenceOffline);

        const bool isDeactivated = (contact.details<QContactDeactivated>().count() > 0);

        QCOMPARE(phoneNumberIds.contains(contactId), hasPhoneNumber);
        QCOMPARE(emailAddressIds.contains(contactId), hasEmailAddress);
        QCOMPARE(onlineAccountIds.contains(contactId), hasOnlineAccount);
        QCOMPARE(onlineIds.contains(contactId), isOnline);
        QCOMPARE(deactivatedIds.contains(contactId), isDeactivated);

        QCOMPARE(phoneAndEmailIds.contains(contactId), (hasPhoneNumber && hasEmailAddress));
        QCOMPARE(phoneOnlyIds.contains(contactId), (hasPhoneNumber && !hasEmailAddress && !hasOnlineAccount && !isOnline));
    }
}

void tst_QContactManagerFiltering::deactivation_data()
{
    QTest::addColumn<QContactManager *>("cm");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *cm = managers.at(i);
        QTest::newRow(qPrintable(cm->objectName())) << cm;
    }
}

void tst_QContactManagerFiltering::deactivation()
{
    QFETCH(QContactManager*, cm);

    QContact alice;
    QContact bob;

    QContactName an, bn;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    alice.saveDetail(&an);
    bn.setFirstName("Bob");
    bn.setMiddleName("The");
    bn.setLastName("Demolisher");
    bob.saveDetail(&bn);

    // Alice must have a sync-target to be deactivated
    QContactSyncTarget st;
    st.setSyncTarget("test");
    alice.saveDetail(&st);

    QVERIFY(cm->saveContact(&alice));
    QVERIFY(cm->saveContact(&bob));
    alice = cm->contact(alice.id());
    bob = cm->contact(bob.id());
    transientContacts.insert(cm, ContactId::apiId(alice));
    transientContacts.insert(cm, ContactId::apiId(bob));

    QList<QContactId> ids(cm->contactIds());
    QVERIFY(ids.contains(alice.id()));
    QVERIFY(ids.contains(bob.id()));

    // Verify that both contacts are non-deactivated
    QCOMPARE(alice.details<QContactDeactivated>().count(), 0);
    QCOMPARE(alice.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsDeactivated), false);
    QCOMPARE(bob.details<QContactDeactivated>().count(), 0);
    QCOMPARE(bob.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsDeactivated), false);

    // Deactivate alice
    QContactDeactivated deactivated;
    alice.saveDetail(&deactivated);
    QVERIFY(cm->saveContact(&alice));
    alice = cm->contact(alice.id());

    // Verify that alice is now deactivated
    QCOMPARE(alice.details<QContactDeactivated>().count(), 1);
    QCOMPARE(alice.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsDeactivated), true);

    // Alice is no longer returned in the contact set
    ids = cm->contactIds();
    QVERIFY(ids.contains(alice.id()) == false);
    QVERIFY(ids.contains(bob.id()));

    // Alice is returned by a deactivated query
    ids = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains));
    QVERIFY(ids.contains(alice.id()));
    QVERIFY(ids.contains(bob.id()) == false);

    // Re-activate alice
    deactivated = alice.detail<QContactDeactivated>();
    alice.removeDetail(&deactivated);
    QVERIFY(cm->saveContact(&alice));
    alice = cm->contact(alice.id());

    // Verify that alice is no longer deactivated
    QCOMPARE(alice.details<QContactDeactivated>().count(), 0);
    QCOMPARE(alice.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsDeactivated), false);

    // Alice is now returned in the contact set
    ids = cm->contactIds();
    QVERIFY(ids.contains(alice.id()));
    QVERIFY(ids.contains(bob.id()));

    // Alice is not retuned by a deactivated query
    ids = cm->contactIds(QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains));
    QVERIFY(ids.contains(alice.id()) == false);
    QVERIFY(ids.contains(bob.id()) == false);
}

void tst_QContactManagerFiltering::detailVariantFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<TypeIdentifier>("defname");
    QTest::addColumn<FieldIdentifier>("fieldname");
    QTest::addColumn<bool>("setValue");
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<QString>("expected");

    QVariant ev; // empty variant
    QString es; // empty string

    QContactDetail::DetailType noType(QContactDetail::TypeUndefined);
    int noField(-1);
    int invalidField(0x666);

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        /* Nothings */
        newMRow("no name", manager) << manager << noType << noField << false << ev << es;
        newMRow("no def name", manager) << manager << noType << detailField(QContactName::FieldFirstName) << false << ev << es;

        /* Strings (name) */
        newMRow("first name presence", manager) << manager << detailType<QContactName>() << detailField(QContactName::FieldFirstName) << false << ev << "abcdefghijk";
        newMRow("first name == Aaron", manager) << manager << detailType<QContactName>() << detailField(QContactName::FieldFirstName) << true << QVariant("Aaron") << "a";

        /*
         * Doubles
         * B has double(4.0)
         * C has double(4.0)
         * D has double(-128.0)
         */
        FieldSelector defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Double");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("double presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "bcd";
            QTest::newRow("double presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "bcd";
            QTest::newRow("double presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("double value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("double value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(QDateTime()) << es;
            QTest::newRow("double value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(3.5) << es;
            newMRow("double value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(4.0) << "bc";
            QTest::newRow("double value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(4.0) << es;
            QTest::newRow("double value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(-128.0) << "d";
            QTest::newRow("double value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(-128.0) << es;
        }

        /*
         * Integers
         * A has 3
         * B has 20
         * C has -20
         */
        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Integer");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("integer presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "abc";
            QTest::newRow("integer presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "abc";
            QTest::newRow("integer presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("integer value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(50) << es;
            QTest::newRow("integer value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("integer value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(50) << es;
            newMRow("integer value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3) << "a";
            QTest::newRow("integer value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(3) << es;
            QTest::newRow("integer value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(-20) << "c";
            QTest::newRow("integer value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(-20) << es;
        }

        /*
         * Date times
         * A has QDateTime(QDate(2009, 06, 29), QTime(16, 52, 23, 0))
         * C has QDateTime(QDate(2009, 06, 29), QTime(16, 54, 17, 0))
         * NOTE: value presence filtering can fail due to automatic timestamp insertion by qtcontacts-sqlite backend
         */
        const QDateTime adt(QDate(2009, 06, 29), QTime(16, 52, 23, 0));
        const QDateTime cdt(QDate(2009, 06, 29), QTime(16, 54, 17, 0));

        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("DateTime");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("datetime presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "abcdefghijk"; // all contacts have a Timestamp detail
            QTest::newRow("datetime presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "abcdefghijk";
            QTest::newRow("datetime presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("datetime value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(QDateTime(QDate(2100,5,13), QTime(5,5,5))) << es;
            QTest::newRow("datetime value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("datetime value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(QDateTime(QDate(2100,5,13), QTime(5,5,5))) << es;
            newMRow("datetime value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(adt) << "a";
            QTest::newRow("datetime value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(adt) << es;
            QTest::newRow("datetime value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(cdt)<< "c";
            QTest::newRow("datetime value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(cdt) << es;
        }

        /*
         * Dates
         * A has QDate(1988, 1, 26)
         * B has QDate(2492, 5, 5)
         * D has QDate(2770, 10, 1)
         */
        const QDate ad(1988, 1, 26);
        const QDate bd(2492, 5, 5);
        const QDate dd(2770, 10, 1);

        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Date");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("date presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "abd";
            QTest::newRow("date presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "abd";
            QTest::newRow("date presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("date value (no match)") << manager << defAndFieldNames.first <<defAndFieldNames.second << true << QVariant(QDate(2100,5,13)) << es;
            QTest::newRow("date value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("date value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(QDate(2100,5,13)) << es;
            newMRow("date value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(ad) << "a";
            QTest::newRow("date value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(ad) << es;
            QTest::newRow("date value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(bd) << es;
            QTest::newRow("date value 3 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(dd) << es;
            QTest::newRow("date value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(bd)<< "b";
            QTest::newRow("date value 3") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(dd)<< "d";
        }

        /*
         * Times
         * A has QTime(16,52,23,0)
         * B has QTime(15,52,23,0)
         */
        const QTime at = QTime(16,52,23,0);
        const QTime bt = QTime(15,52,23,0);

        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Time");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("time presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "ab";
            QTest::newRow("time presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "ab";
            QTest::newRow("time presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("time value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(QTime(5,5,5)) << es;
            QTest::newRow("time value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("time value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(QTime(5,5,5)) << es;
            newMRow("time value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(at) << "a";
            QTest::newRow("time value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(at) << es;
            QTest::newRow("time value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(bt)<< "b";
            QTest::newRow("time value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(bt) << es;
        }


#if 0 // nemo sqlite backend always fills boolean fields with false values by default
        /*
         * Bool
         * A has bool(true)
         * B has bool(false)
         * C has bool(false)
         */
        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Bool");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("bool presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "abc";
            QTest::newRow("bool presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "abc";
            QTest::newRow("bool presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("bool value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(4.0) << es;
            newMRow("bool value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(true) << "a";
            QTest::newRow("bool value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(true) << es;
            QTest::newRow("bool value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(false) << "bc";
            QTest::newRow("bool value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(false) << es;
        }
#endif

        /*
         * LongLong
         * C has LongLong(8000000000LL)
         * D has LongLong(-14000000000LL)
         */
        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("LongLong");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("longlong presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "cd";
            QTest::newRow("longlong presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "cd";
            QTest::newRow("longlong presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("longlong value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(50000000000LL) << es;
            QTest::newRow("longlong value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("longlong value (wrong field, no match)") << manager << defAndFieldNames.first<< invalidField << true << QVariant(50000000000LL) << es;
            newMRow("longlong value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(8000000000LL) << "c";
            QTest::newRow("longlong value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(8000000000LL) << es;
            QTest::newRow("longlong value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(-14000000000LL) << "d";
            QTest::newRow("longlong value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(-14000000000LL) << es;
        }

        /*
         * ULongLong
         * A has ULongLong(120000000000ULL)
         * B has ULongLong(80000000000ULL)
         * C has ULongLong(80000000000ULL)
         */
        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("ULongLong");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("ulonglong presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "abc";
            QTest::newRow("ulonglong presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "abc";
            QTest::newRow("ulonglong presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("ulonglong value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(50000000000ULL) << es;
            QTest::newRow("ulonglong value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("ulonglong value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(50000000000ULL) << es;
            newMRow("ulonglong value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(120000000000ULL) << "a";
            QTest::newRow("ulonglong value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(120000000000ULL) << es;
            QTest::newRow("ulonglong value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(80000000000ULL) << "bc";
            QTest::newRow("ulonglong value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(80000000000ULL) << es;
        }

        /*
         * UInt
         * B has UInt(4000000000u)
         * D has UInt(3000000000u)
         */
        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("UInt");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("unsigned integer presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "bd";
            QTest::newRow("unsigned integer presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "bd";
            QTest::newRow("unsigned integer presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("unsigned integer value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3500000000u) << es;
            QTest::newRow("unsigned integer value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("unsigned integer value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(3500000000u) << es;
            newMRow("unsigned integer value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(4000000000u) << "b";
            QTest::newRow("unsigned integer value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(4000000000u) << es;
            QTest::newRow("unsigned integer value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3000000000u) << "d";
            QTest::newRow("unsigned integer value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(3000000000u) << es;
        }

        /*
         * Char
         * B has QChar('b')
         * C has QChar('c')
         */
        const QChar bchar('b');
        const QChar cchar('c');
        defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Char");
        if (validDetailInfo(defAndFieldNames)) {
            newMRow("char presence", manager) << manager << defAndFieldNames.first << noField << false << ev << "bc";
            QTest::newRow("char presence (inc field)") << manager << defAndFieldNames.first << defAndFieldNames.second << false << ev << "bc";
            QTest::newRow("char presence (wrong field)") << manager << defAndFieldNames.first << invalidField << false << ev << es;
            QTest::newRow("char value (no match)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(QChar('a')) << es;
            QTest::newRow("char value (wrong type)") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(3.5) << es;
            QTest::newRow("char value (wrong field, no match)") << manager << defAndFieldNames.first << invalidField << true << QVariant(QChar('a')) << es;
            newMRow("char value", manager) << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(bchar) << "b";
            QTest::newRow("char value (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(bchar) << es;
            QTest::newRow("char value 2") << manager << defAndFieldNames.first << defAndFieldNames.second << true << QVariant(cchar)<< "c";
            QTest::newRow("char value 2 (wrong field)") << manager << defAndFieldNames.first << invalidField << true << QVariant(cchar) << es;
        }
    }
}

void tst_QContactManagerFiltering::detailVariantFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(TypeIdentifier, defname);
    QFETCH(FieldIdentifier, fieldname);
    QFETCH(bool, setValue);
    QFETCH(QVariant, value);
    QFETCH(QString, expected);

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    QContactDetailFilter df;
    setFilterDetail(df, defname, fieldname);
    if (setValue)
        df.setValue(value);

    if (cm->managerName() == "memory") {
        /* At this point, since we're using memory, assume the filter isn't really supported */
        QVERIFY(cm->isFilterSupported(df) == false);
    }

    ids = cm->contactIds(df);

    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);
}

void tst_QContactManagerFiltering::rangeFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<TypeIdentifier>("defname");
    QTest::addColumn<FieldIdentifier>("fieldname");
    QTest::addColumn<QVariant>("minrange");
    QTest::addColumn<QVariant>("maxrange");
    QTest::addColumn<bool>("setrfs");
    QTest::addColumn<int>("rangeflagsi");
    QTest::addColumn<bool>("setmfs");
    QTest::addColumn<int>("matchflagsi");
    QTest::addColumn<QString>("expected");

    QVariant ev; // empty variant
    QString es; // empty string

    TypeIdentifier namedef = detailType<QContactName>();
    FieldIdentifier firstname = QContactName::FieldFirstName;
    TypeIdentifier phonedef = detailType<QContactPhoneNumber>();
    FieldIdentifier phonenum = QContactPhoneNumber::FieldNumber;

    TypeIdentifier noType(QContactDetail::TypeUndefined);
    FieldIdentifier noField(-1);
    TypeIdentifier invalidType = static_cast<TypeIdentifier>(0x666);
    FieldIdentifier invalidField = 0x666;

    int csflag = (int)QContactFilter::MatchCaseSensitive;

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        /* First, cover the "empty defname / fieldname / ranges" cases */
        newMRow("invalid defname", manager) << manager << noType << firstname << QVariant("A") << QVariant("Bob") << false << 0 << true << 0 << es;
        newMRow("defn presence test", manager) << manager << namedef << noField << QVariant("A") << QVariant("Bob") << false << 0 << true << 0 << "abcdefghijk";
        newMRow("field presence test", manager) << manager << phonedef << phonenum << QVariant() << QVariant() << false << 0 << true << 0 << "ab";
        newMRow("good def, bad field", manager) << manager << namedef << invalidField << QVariant("A") << QVariant("Bob") << false << 0 << true << 0 << es;
        newMRow("bad def", manager) << manager << invalidType << invalidField << QVariant("A") << QVariant("Bob") << false << 0 << true << 0 << es;

        /* Presence for fields that aren't there */
        newMRow("defn presence test negative", manager) << manager << invalidType << noField << ev << ev << false << 0 << false << 0 << es;
        newMRow("field presence test negative", manager) << manager << invalidType << invalidField << ev << ev << false << 0 << false << 0 << es;
        newMRow("defn yes, field no presence test negative", manager) << manager << namedef << invalidField << ev << ev << false << 0 << false << 0 << es;

        newMRow("no max, all results", manager) << manager << namedef << firstname << QVariant("a") << QVariant() << false << 0 << true << 0 << "abcdefghijk";
        newMRow("no max, some results", manager) << manager << namedef << firstname << QVariant("bob") << QVariant() << false << 0 << true << 0 << "bcdefghijk";
        newMRow("no max, no results", manager) << manager << namedef << firstname << QVariant("ZamBeZI") << QVariant() << false << 0 << true << 0 << es;
        newMRow("no min, all results", manager) << manager << namedef << firstname << QVariant() << QVariant("zambezi") << false << 0 << true << 0 << "abcdefghijk";
        newMRow("no min, some results", manager) << manager << namedef << firstname << QVariant() << QVariant("bOb") << false << 0 << true << 0 << "a";
        newMRow("no min, no results", manager) << manager << namedef << firstname << QVariant() << QVariant("aardvark") << false << 0 << true << 0 << es;

        /* now case sensitive */
        newMRow("no max, cs, all results", manager) << manager << namedef << firstname << QVariant("A") << QVariant() << false << 0 << true << csflag << "abcdefghijk";
        newMRow("no max, cs, some results", manager) << manager << namedef << firstname << QVariant("Bob") << QVariant() << false << 0 << true << csflag << "bcdefghijk";
        newMRow("no max, cs, no results", manager) << manager << namedef << firstname << QVariant("Xambezi") << QVariant() << false << 0 << true << csflag << "hijk";
        newMRow("no min, cs, most results", manager) << manager << namedef << firstname << QVariant() << QVariant("Xambezi") << false << 0 << true << csflag << "abcdefg";
        newMRow("no min, cs, some results", manager) << manager << namedef << firstname << QVariant() << QVariant("Bob") << false << 0 << true << csflag << "a";
        newMRow("no min, cs, no results", manager) << manager << namedef << firstname << QVariant() << QVariant("Aardvark") << false << 0 << true << csflag << es;
        newMRow("no max, cs, badcase, all results", manager) << manager << namedef << firstname << QVariant("A") << QVariant() << false << 0 << true << csflag << "abcdefghijk";
#if 0
        qWarning() << "Test case \"no max, cs, badcase, some results\" will fail on some platforms because of QString::localeAwareCompare is not actually locale aware";
        newMRow("no max, cs, badcase, some results", manager) << manager << namedef << firstname << QVariant("BOB") << QVariant() << false << 0 << true << csflag << "cdefghijk";
#endif
        newMRow("no max, cs, badcase, no results", manager) << manager << namedef << firstname << QVariant("XAMBEZI") << QVariant() << false << 0 << true << csflag << "hijk";
        newMRow("no min, cs, badcase, all results", manager) << manager << namedef << firstname << QVariant() << QVariant("XAMBEZI") << false << 0 << true << csflag << "abcdefg";
#if 0
        qWarning() << "Test case \"no min, cs, badcase, some results\" will fail on some platforms because of QString::localeAwareCompare is not actually locale aware";
        newMRow("no min, cs, badcase, some results", manager) << manager << namedef << firstname << QVariant() << QVariant("BOB") << false << 0 << true << csflag << "ab";
#endif
        newMRow("no min, cs, badcase, no results", manager) << manager << namedef << firstname << QVariant() << QVariant("AARDVARK") << false << 0 << true << csflag << es;

        /* 'a' has phone number ("5551212") */
        QTest::newRow("range1") << manager << phonedef << phonenum << QVariant("5551200") << QVariant("5551220") << false << 0 << false << 0 << "a";

        /* A(Aaron Aaronson), B(Bob Aaronsen), C(Boris Aaronsun), D(Dennis FitzMacyntire) */
        // string range matching - no matchflags set.
        QTest::newRow("string range - no matchflags - 1") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << false << 0 << true << 0 << "a";
        QTest::newRow("string range - no matchflags - 2") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << 0 << "a";
        QTest::newRow("string range - no matchflags - 3") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << 0 << "a";
        QTest::newRow("string range - no matchflags - 4") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << true << 0 << "ab";
        QTest::newRow("string range - no matchflags - 5") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::IncludeUpper) << true << 0 << "ab";
        QTest::newRow("string range - no matchflags - 6") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::IncludeUpper) << true << 0 << "c";
        QTest::newRow("string range - no matchflags - 7") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << 0 << "bc";
        QTest::newRow("string range - no matchflags - 8") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << true << 0 << "bc";
        QTest::newRow("string range - no matchflags - 9") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << 0 << "c";
        QTest::newRow("string range - no matchflags - 10") << manager << namedef << firstname << QVariant("Barry") << QVariant("C") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << 0 << "bc";

        // string range matching - QContactFilter::MatchStartsWith should produce the same results as without matchflags set.
        QTest::newRow("string range - startswith - 1") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "a";
        QTest::newRow("string range - startswith - 2") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "a";
        QTest::newRow("string range - startswith - 3") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "ab";
        QTest::newRow("string range - startswith - 4") << manager << namedef << firstname << QVariant("A") << QVariant("Bob") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::IncludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "ab";
        QTest::newRow("string range - startswith - 5") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::IncludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "c";
        QTest::newRow("string range - startswith - 6") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "bc";
        QTest::newRow("string range - startswith - 7") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "bc";
        QTest::newRow("string range - startswith - 8") << manager << namedef << firstname << QVariant("Bob") << QVariant("C") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "c";
        QTest::newRow("string range - startswith - 9") << manager << namedef << firstname << QVariant("Barry") << QVariant("C") << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "bc";

        // Open ended starts with
        QTest::newRow("string range - startswith open top - 1") << manager << namedef << firstname << QVariant("A") << ev << true << (int)(QContactDetailRangeFilter::IncludeLower) << true << (int)(QContactFilter::MatchStartsWith) << "abcdefghijk";
        QTest::newRow("string range - startswith open top - 2") << manager << namedef << firstname << QVariant("A") << ev << true << (int)(QContactDetailRangeFilter::ExcludeLower) << true << (int)(QContactFilter::MatchStartsWith) << "abcdefghijk";
        QTest::newRow("string range - startswith open top - 3") << manager << namedef << firstname << QVariant("Aaron") << ev << true << (int)(QContactDetailRangeFilter::IncludeLower) << true << (int)(QContactFilter::MatchStartsWith) << "abcdefghijk";
        QTest::newRow("string range - startswith open top - 4") << manager << namedef << firstname << QVariant("Aaron") << ev << true << (int)(QContactDetailRangeFilter::ExcludeLower) << true << (int)(QContactFilter::MatchStartsWith) << "bcdefghijk";
        QTest::newRow("string range - startswith open bottom - 1") << manager << namedef << firstname << ev << QVariant("Borit") << true << (int)(QContactDetailRangeFilter::IncludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "abc";
        QTest::newRow("string range - startswith open bottom - 2") << manager << namedef << firstname << ev << QVariant("Borit") << true << (int)(QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "abc";
        QTest::newRow("string range - startswith open bottom - 3") << manager << namedef << firstname << ev << QVariant("Boris") << true << (int)(QContactDetailRangeFilter::IncludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "abc";
        QTest::newRow("string range - startswith open bottom - 4") << manager << namedef << firstname << ev << QVariant("Boris") << true << (int)(QContactDetailRangeFilter::ExcludeUpper) << true << (int)(QContactFilter::MatchStartsWith) << "ab";

        /* A(10), B(20), C(-20) */
        // Now integer range testing
        FieldSelector defAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Integer");
        if (validDetailInfo(defAndFieldNames)) {
            QTest::newRow("int range - no rangeflags - 1") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(2) << QVariant(2) << false << 0 << false << 0 << es;
            QTest::newRow("int range - no rangeflags - 2") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(2) << QVariant(3) << false << 0 << false << 0 << es;
            QTest::newRow("int range - no rangeflags - 3") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(2) << QVariant(4) << false << 0 << false << 0 << "a";
            QTest::newRow("int range - no rangeflags - 4") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(3) << QVariant(3) << false << 0 << false << 0 << es;
            QTest::newRow("int range - rangeflags - 1") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(3) << QVariant(3) << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::ExcludeUpper) << false << 0 << es;
            QTest::newRow("int range - rangeflags - 2") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(3) << QVariant(3) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::ExcludeUpper) << false << 0 << es;
            QTest::newRow("int range - rangeflags - 3") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(3) << QVariant(3) << true << (int)(QContactDetailRangeFilter::ExcludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << es;
            QTest::newRow("int range - rangeflags - 4") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(3) << QVariant(3) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << "a";
            QTest::newRow("int range - rangeflags - 5") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(3) << QVariant(4) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << "a";
            QTest::newRow("int range - rangeflags - 6") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(4) << QVariant(4) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << es;
            QTest::newRow("int range - rangeflags - 7") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(-30) << QVariant(-19) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << "c";
            QTest::newRow("int range - rangeflags - 8") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(-20) << QVariant(-30) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << es;
            QTest::newRow("int range - rangeflags - variant - 1") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant(2) << QVariant() << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << "ab";
            QTest::newRow("int range - rangeflags - variant - 2") << manager << defAndFieldNames.first << defAndFieldNames.second << QVariant() << QVariant(4) << true << (int)(QContactDetailRangeFilter::IncludeLower | QContactDetailRangeFilter::IncludeUpper) << false << 0 << "ac";
        }
    }
}

void tst_QContactManagerFiltering::rangeFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(TypeIdentifier, defname);
    QFETCH(FieldIdentifier, fieldname);
    QFETCH(QVariant, minrange);
    QFETCH(QVariant, maxrange);
    QFETCH(bool, setrfs);
    QFETCH(int, rangeflagsi);
    QFETCH(bool, setmfs);
    QFETCH(int, matchflagsi);
    QFETCH(QString, expected);

    QContactDetailRangeFilter::RangeFlags rangeflags = (QContactDetailRangeFilter::RangeFlags)rangeflagsi;

    QContactFilter::MatchFlags matchflags = (QContactFilter::MatchFlags) matchflagsi;
    if ((matchflags & QContactFilter::MatchCaseSensitive) == 0) {
        // Case insensitivity only applies to MatchFixedString
        matchflags |= QContactFilter::MatchFixedString;
    }

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    /* Build the range filter */
    QContactDetailRangeFilter drf;
    setFilterDetail(drf, defname, fieldname);
    if (setrfs)
        drf.setRange(minrange, maxrange, rangeflags);
    else
        drf.setRange(minrange, maxrange);
    if (setmfs)
        drf.setMatchFlags(matchflags);

    if (cm->managerName() == "memory") {
        /* At this point, since we're using memory, assume the filter isn't really supported */
        QVERIFY(cm->isFilterSupported(drf) == false);
    }
    ids = cm->contactIds(drf);

    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);
}

void tst_QContactManagerFiltering::intersectionFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<bool>("firstfilter");
    QTest::addColumn<int>("fftype"); // 1 = detail, 2 = detailrange, 3 = groupmembership, 4 = union, 5 = intersection
    QTest::addColumn<TypeIdentifier>("ffdefname");
    QTest::addColumn<FieldIdentifier>("fffieldname");
    QTest::addColumn<bool>("ffsetvalue");
    QTest::addColumn<QVariant>("ffvalue");
    QTest::addColumn<QVariant>("ffminrange");
    QTest::addColumn<QVariant>("ffmaxrange");
    QTest::addColumn<bool>("secondfilter");
    QTest::addColumn<int>("sftype");
    QTest::addColumn<TypeIdentifier>("sfdefname");
    QTest::addColumn<FieldIdentifier>("sffieldname");
    QTest::addColumn<bool>("sfsetvalue");
    QTest::addColumn<QVariant>("sfvalue");
    QTest::addColumn<QVariant>("sfminrange");
    QTest::addColumn<QVariant>("sfmaxrange");
    QTest::addColumn<QString>("order");
    QTest::addColumn<QString>("expected");

    QString es; // empty string.

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        // for the following tests, terminology:
        // X will be an (empty) intersection filter created in the test
        // Y will be the first filter defined here
        // Z will be the second filter defined here

        // note: have contacts: A(Aaron Aaronson), B(Bob Aaronsen), C(Boris Aaronsun),
        // D(Dennis FitzMacintyre), E(John Smithee), F(John Smithey), G(John Smithy)

        // WITH Y AND Z AS DETAIL FILTERS (with no overlap between Y and Z results)
        QTest::newRow("A1") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("Bob")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronson")) << QVariant() << QVariant()
                            << "YZ" << es;

        // WITH Y AND Z AS DETAIL FILTERS (with 1 overlap between Y(B) and Z(B) results)
        QTest::newRow("A2") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("Bob")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronsen")) << QVariant() << QVariant()
                            << "YZ" << "b";

        // WITH Y AND Z AS DETAIL FILTERS (with 1 overlap between Y(EFG) and Z(E) results)
        QTest::newRow("A3") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("John")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Smithee")) << QVariant() << QVariant()
                            << "YZ" << "e";

        // WITH Y AND Z AS DETAIL FILTERS (with 1 overlap between Y(EFG) and Z(E) results) but intersecting X AND Y
        // Where X is the empty intersection filter.  This should match nothing (anything intersected with empty intersection = nothing)
        QTest::newRow("A4") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("John")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Smithee")) << QVariant() << QVariant()
                            << "XY" << es;

        // WITH Y AS DETAIL RANGE FILTER AND Z AS DETAIL FILTER (with no overlap between Y(BC+) and Z(A) results)
        QTest::newRow("B1") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Bob")) << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronson")) << QVariant() << QVariant()
                            << "YZ" << es;

        // WITH Y AS DETAIL RANGE FILTER AND Z AS DETAIL FILTER (with 1 overlap between Y(BC+) and Z(B) results)
        QTest::newRow("B2") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Bob")) << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronsen")) << QVariant() << QVariant()
                            << "YZ" << "b";

        // WITH Y AND Z AS DETAIL RANGE FILTERS (with no overlap between Y(E+) and Z(ABC) results)
        QTest::newRow("C1") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant(QString::fromLatin1("John")) << QVariant()
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Aaronaaa")) << QVariant(QLatin1String("Aaronzzz"))
                            << "YZ" << es;

        // WITH Y AND Z AS DETAIL RANGE FILTERS (with 2 overlap between Y(BC) and Z(ABC) results)
        QTest::newRow("C2") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Boa")) << QVariant()
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Aaronaaa")) << QVariant(QLatin1String("Aaronzzz"))
                            << "YZ" << "bc";
    }
}

void tst_QContactManagerFiltering::intersectionFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(bool, firstfilter);
    QFETCH(int, fftype); // 1 = detail, 2 = detailrange, 3 = groupmembership, 4 = union, 5 = intersection
    QFETCH(TypeIdentifier, ffdefname);
    QFETCH(FieldIdentifier, fffieldname);
    QFETCH(bool, ffsetvalue);
    QFETCH(QVariant, ffvalue);
    QFETCH(QVariant, ffminrange);
    QFETCH(QVariant, ffmaxrange);
    QFETCH(bool, secondfilter);
    QFETCH(int, sftype);
    QFETCH(TypeIdentifier, sfdefname);
    QFETCH(FieldIdentifier, sffieldname);
    QFETCH(bool, sfsetvalue);
    QFETCH(QVariant, sfvalue);
    QFETCH(QVariant, sfminrange);
    QFETCH(QVariant, sfmaxrange);
    QFETCH(QString, order);
    QFETCH(QString, expected);

    QContactFilter *x = new QContactIntersectionFilter();
    QContactFilter *y = 0, *z = 0;

    if (firstfilter) {
        switch (fftype) {
            case 1: // detail filter
                y = new QContactDetailFilter();
                setFilterDetail(*static_cast<QContactDetailFilter*>(y), ffdefname, fffieldname);
                if (ffsetvalue)
                    static_cast<QContactDetailFilter*>(y)->setValue(ffvalue);
                break;
            case 2: // range filter
                y = new QContactDetailRangeFilter();
                setFilterDetail(*static_cast<QContactDetailRangeFilter*>(y), ffdefname, fffieldname);
                static_cast<QContactDetailRangeFilter*>(y)->setRange(ffminrange, ffmaxrange);
                break;
            case 3: // group membership filter
            case 4: // union filter
            case 5: // intersection filter
                break;

            default:
                QVERIFY(false); // force fail.
            break;
        }
    }

    if (secondfilter) {
        switch (sftype) {
            case 1: // detail filter
                z = new QContactDetailFilter();
                setFilterDetail(*static_cast<QContactDetailFilter*>(z), sfdefname, sffieldname);
                if (sfsetvalue)
                    static_cast<QContactDetailFilter*>(z)->setValue(sfvalue);
                break;
            case 2: // range filter
                z = new QContactDetailRangeFilter();
                setFilterDetail(*static_cast<QContactDetailRangeFilter*>(z), sfdefname, sffieldname);
                static_cast<QContactDetailRangeFilter*>(z)->setRange(sfminrange, sfmaxrange);
                break;
            case 3: // group membership filter
            case 4: // union filter
            case 5: // intersection filter
                break;

            default:
                QVERIFY(false); // force fail.
            break;
        }
    }

    // control variables - order: starts, ends, mids
    bool sX = false;
    bool sY = false;
    bool sZ = false;
    bool eX = false;
    bool eY = false;
    bool eZ = false;
    bool mX = false;
    bool mY = false;
    bool mZ = false;

    if (order.startsWith("X"))
        sX = true;
    if (order.startsWith("Y"))
        sY = true;
    if (order.startsWith("Z"))
        sZ = true;
    if (order.endsWith("X"))
        eX = true;
    if (order.endsWith("Y"))
        eY = true;
    if (order.endsWith("Z"))
        eZ = true;
    if (order.size() > 2) {
        if (order.at(1) == 'X')
            mX = true;
        if (order.at(1) == 'Y')
            mY = true;
        if (order.at(1) == 'Z')
            mZ = true;
    }

    // now perform the filtering.
    QContactIntersectionFilter resultFilter;
    if (sX) {
        if (mY && eZ)
            resultFilter = *x & *y & *z;
        else if (mZ && eY)
            resultFilter = *x & *z & *y;
        else if (eY)
            resultFilter = *x & *y;
        else if (eZ)
            resultFilter = *x & *z;
    } else if (sY) {
        if (mX && eZ)
            resultFilter = *y & *x & *z;
        else if (mZ && eX)
            resultFilter = *y & *z & *x;
        else if (eX)
            resultFilter = *y & *x;
        else if (eZ)
            resultFilter = *y & *z;
    } else if (sZ) {
        if (mX && eY)
            resultFilter = *z & *x & *y;
        else if (mY && eX)
            resultFilter = *z & *y & *x;
        else if (eX)
            resultFilter = *z & *x;
        else if (eY)
            resultFilter = *z & *y;
    }

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    ids = cm->contactIds(resultFilter);

    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);

    delete x;
    if (y) delete y;
    if (z) delete z;
}

void tst_QContactManagerFiltering::unionFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<bool>("firstfilter");
    QTest::addColumn<int>("fftype"); // 1 = detail, 2 = detailrange, 3 = groupmembership, 4 = union, 5 = intersection
    QTest::addColumn<TypeIdentifier>("ffdefname");
    QTest::addColumn<FieldIdentifier>("fffieldname");
    QTest::addColumn<bool>("ffsetvalue");
    QTest::addColumn<QVariant>("ffvalue");
    QTest::addColumn<QVariant>("ffminrange");
    QTest::addColumn<QVariant>("ffmaxrange");
    QTest::addColumn<bool>("secondfilter");
    QTest::addColumn<int>("sftype");
    QTest::addColumn<TypeIdentifier>("sfdefname");
    QTest::addColumn<FieldIdentifier>("sffieldname");
    QTest::addColumn<bool>("sfsetvalue");
    QTest::addColumn<QVariant>("sfvalue");
    QTest::addColumn<QVariant>("sfminrange");
    QTest::addColumn<QVariant>("sfmaxrange");
    QTest::addColumn<QString>("order");
    QTest::addColumn<QString>("expected");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        // for the following tests, terminology:
        // X will be an (empty) union filter created in the test
        // Y will be the first filter defined here
        // Z will be the second filter defined here

        // WITH Y AND Z AS DETAIL FILTERS (with no overlap between Y(1) and Z(1) results)
        QTest::newRow("A1") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("Bob")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronson")) << QVariant() << QVariant()
                            << "YZ" << "ab";

        // WITH Y AND Z AS DETAIL FILTERS (with 1 overlap between Y(B) and Z(B) results)
        QTest::newRow("A2") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("Bob")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronsen")) << QVariant() << QVariant()
                            << "YZ" << "b";

        // WITH Y AND Z AS DETAIL FILTERS (with 1 overlap between Y(EFG) and Z(E) results)
        QTest::newRow("A3") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("John")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Smithee")) << QVariant() << QVariant()
                            << "YZ" << "efg";

        // WITH Y AND Z AS DETAIL FILTERS (with 1 overlap between Y(EFG) and Z(E) results) but intersecting X AND Y
        // Where X is the empty union filter.  This should match all of Y matches.
        QTest::newRow("A4") << manager
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << true << QVariant(QString::fromLatin1("John")) << QVariant() << QVariant()
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Smithee")) << QVariant() << QVariant()
                            << "XY" << "efg";

        // WITH Y AS DETAIL RANGE FILTER AND Z AS DETAIL FILTER (with no overlap between Y(AB) and Z(C) results)
        QTest::newRow("B1") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant() << QVariant(QString::fromLatin1("Boz"))
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronsun")) << QVariant() << QVariant()
                            << "YZ" << "abc";

        // WITH Y AS DETAIL RANGE FILTER AND Z AS DETAIL FILTER (with 1 overlap between Y(AB) and Z(A) results)
        QTest::newRow("B2") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Aaro")) << QVariant(QString::fromLatin1("Bod"))
                            << true << 1 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << true << QVariant(QString::fromLatin1("Aaronson")) << QVariant() << QVariant()
                            << "YZ" << "ab";

        // WITH Y AND Z AS DETAIL RANGE FILTERS (with 2 overlap between Y(AB) and Z(ABC) results)
        QTest::newRow("C1") << manager
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldFirstName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Aaro")) << QVariant(QString::fromLatin1("Bod"))
                            << true << 2 << detailType<QContactName>() << detailField(QContactName::FieldLastName)
                            << false << QVariant() << QVariant(QString::fromLatin1("Aaronaaa")) << QVariant(QLatin1String("Aaronzzz"))
                            << "YZ" << "abc";
    }
}

void tst_QContactManagerFiltering::unionFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(bool, firstfilter);
    QFETCH(int, fftype); // 1 = detail, 2 = detailrange, 3 = groupmembership, 4 = union, 5 = intersection
    QFETCH(TypeIdentifier, ffdefname);
    QFETCH(FieldIdentifier, fffieldname);
    QFETCH(bool, ffsetvalue);
    QFETCH(QVariant, ffvalue);
    QFETCH(QVariant, ffminrange);
    QFETCH(QVariant, ffmaxrange);
    QFETCH(bool, secondfilter);
    QFETCH(int, sftype);
    QFETCH(TypeIdentifier, sfdefname);
    QFETCH(FieldIdentifier, sffieldname);
    QFETCH(bool, sfsetvalue);
    QFETCH(QVariant, sfvalue);
    QFETCH(QVariant, sfminrange);
    QFETCH(QVariant, sfmaxrange);
    QFETCH(QString, order);
    QFETCH(QString, expected);

    QContactFilter *x = new QContactUnionFilter();
    QContactFilter *y = 0, *z = 0;

    if (firstfilter) {
        switch (fftype) {
            case 1: // detail filter
                y = new QContactDetailFilter();
                setFilterDetail(*static_cast<QContactDetailFilter*>(y), ffdefname, fffieldname);
                if (ffsetvalue)
                    static_cast<QContactDetailFilter*>(y)->setValue(ffvalue);
                break;
            case 2: // range filter
                y = new QContactDetailRangeFilter();
                setFilterDetail(*static_cast<QContactDetailRangeFilter*>(y), ffdefname, fffieldname);
                static_cast<QContactDetailRangeFilter*>(y)->setRange(ffminrange, ffmaxrange);
                break;
            case 3: // group membership filter
            case 4: // union filter
            case 5: // intersection filter
                break;

            default:
                QVERIFY(false); // force fail.
            break;
        }
    }

    if (secondfilter) {
        switch (sftype) {
            case 1: // detail filter
                z = new QContactDetailFilter();
                setFilterDetail(*static_cast<QContactDetailFilter*>(z), sfdefname, sffieldname);
                if (sfsetvalue)
                    static_cast<QContactDetailFilter*>(z)->setValue(sfvalue);
                break;
            case 2: // range filter
                z = new QContactDetailRangeFilter();
                setFilterDetail(*static_cast<QContactDetailRangeFilter*>(z), sfdefname, sffieldname);
                static_cast<QContactDetailRangeFilter*>(z)->setRange(sfminrange, sfmaxrange);
                break;
            case 3: // group membership filter
            case 4: // union filter
            case 5: // intersection filter
                break;

            default:
                QVERIFY(false); // force fail.
            break;
        }
    }

    // control variables - order: starts, ends, mids
    bool sX = false;
    bool sY = false;
    bool sZ = false;
    bool eX = false;
    bool eY = false;
    bool eZ = false;
    bool mX = false;
    bool mY = false;
    bool mZ = false;

    if (order.startsWith("X"))
        sX = true;
    if (order.startsWith("Y"))
        sY = true;
    if (order.startsWith("Z"))
        sZ = true;
    if (order.endsWith("X"))
        eX = true;
    if (order.endsWith("Y"))
        eY = true;
    if (order.endsWith("Z"))
        eZ = true;
    if (order.size() > 2) {
        if (order.at(1) == 'X')
            mX = true;
        if (order.at(1) == 'Y')
            mY = true;
        if (order.at(1) == 'Z')
            mZ = true;
    }

    // now perform the filtering.
    QContactUnionFilter resultFilter;
    if (sX) {
        if (mY && eZ)
            resultFilter = *x | *y | *z;
        else if (mZ && eY)
            resultFilter = *x | *z | *y;
        else if (eY)
            resultFilter = *x | *y;
        else if (eZ)
            resultFilter = *x | *z;
    } else if (sY) {
        if (mX && eZ)
            resultFilter = *y | *x | *z;
        else if (mZ && eX)
            resultFilter = *y | *z | *x;
        else if (eX)
            resultFilter = *y | *x;
        else if (eZ)
            resultFilter = *y | *z;
    } else if (sZ) {
        if (mX && eY)
            resultFilter = *z | *x | *y;
        else if (mY && eX)
            resultFilter = *z | *y | *x;
        else if (eX)
            resultFilter = *z | *x;
        else if (eY)
            resultFilter = *z | *y;
    }

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    ids = cm->contactIds(resultFilter);

    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);

    delete x;
    if (y) delete y;
    if (z) delete z;
}

void tst_QContactManagerFiltering::relationshipFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<int>("relatedContactRole");
    QTest::addColumn<QString>("relationshipType");
    QTest::addColumn<quint32>("relatedContactLocalId");
    QTest::addColumn<QString>("otherManagerUri");
    QTest::addColumn<QString>("expected");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        // HasMember
        QTest::newRow("RF-1") << manager << static_cast<int>(QContactRelationship::Second) << relationshipString(QContactRelationship::HasMember) << static_cast<unsigned int>(0) << QString() << "a";
        QTest::newRow("RF-2") << manager << static_cast<int>(QContactRelationship::First) << relationshipString(QContactRelationship::HasMember) << static_cast<unsigned int>(0) << QString() << "b";
        QTest::newRow("RF-3") << manager << static_cast<int>(QContactRelationship::Either) << relationshipString(QContactRelationship::HasMember) << static_cast<unsigned int>(0) << QString() << "ab";

        // match any contact that has an assistant
        QTest::newRow("RF-4") << manager << static_cast<int>(QContactRelationship::Second) << relationshipString(QContactRelationship::HasAssistant) << static_cast<unsigned int>(0) << QString() << "a";
        // match any contact that is an assistant
        QTest::newRow("RF-5") << manager << static_cast<int>(QContactRelationship::First) << relationshipString(QContactRelationship::HasAssistant) << static_cast<unsigned int>(0) << QString() << "b";
        // match any contact that has an assistant or is an assistant
        QTest::newRow("RF-6") << manager << static_cast<int>(QContactRelationship::Either) << relationshipString(QContactRelationship::HasAssistant) << static_cast<unsigned int>(0) << QString() << "ab";

        // IsSameAs
        QTest::newRow("RF-7") << manager << static_cast<int>(QContactRelationship::Second) << relationshipString(QContactRelationship::IsSameAs) << static_cast<unsigned int>(0) << QString() << "a";
        QTest::newRow("RF-8") << manager << static_cast<int>(QContactRelationship::First) << relationshipString(QContactRelationship::IsSameAs) << static_cast<unsigned int>(0) << QString() << "b";
        QTest::newRow("RF-9") << manager << static_cast<int>(QContactRelationship::Either) << relationshipString(QContactRelationship::IsSameAs) << static_cast<unsigned int>(0) << QString() << "ab";

        // Aggregates
        QTest::newRow("RF-10") << manager << static_cast<int>(QContactRelationship::Second) << relationshipString(QContactRelationship::Aggregates) << static_cast<unsigned int>(0) << QString() << "a";
        QTest::newRow("RF-11") << manager << static_cast<int>(QContactRelationship::First) << relationshipString(QContactRelationship::Aggregates) << static_cast<unsigned int>(0) << QString() << "b";
        QTest::newRow("RF-12") << manager << static_cast<int>(QContactRelationship::Either) << relationshipString(QContactRelationship::Aggregates) << static_cast<unsigned int>(0) << QString() << "ab";

        // HasManager
        QTest::newRow("RF-13") << manager << static_cast<int>(QContactRelationship::Second) << relationshipString(QContactRelationship::HasManager) << static_cast<unsigned int>(0) << QString() << "a";
        QTest::newRow("RF-14") << manager << static_cast<int>(QContactRelationship::First) << relationshipString(QContactRelationship::HasManager) << static_cast<unsigned int>(0) << QString() << "b";
        QTest::newRow("RF-15") << manager << static_cast<int>(QContactRelationship::Either) << relationshipString(QContactRelationship::HasManager) << static_cast<unsigned int>(0) << QString() << "ab";

        // HasSpouse
        QTest::newRow("RF-16") << manager << static_cast<int>(QContactRelationship::Second) << relationshipString(QContactRelationship::HasSpouse) << static_cast<unsigned int>(0) << QString() << "a";
        QTest::newRow("RF-17") << manager << static_cast<int>(QContactRelationship::First) << relationshipString(QContactRelationship::HasSpouse) << static_cast<unsigned int>(0) << QString() << "b";
        QTest::newRow("RF-18") << manager << static_cast<int>(QContactRelationship::Either) << relationshipString(QContactRelationship::HasSpouse) << static_cast<unsigned int>(0) << QString() << "ab";

        const bool aribtraryRelationshipsFeatureSupported = true;
        // Unknown relationship
        if (aribtraryRelationshipsFeatureSupported) {
            QTest::newRow("RF-19") << manager << static_cast<int>(QContactRelationship::Second) << QString::fromLatin1("UnknownRelationship") << static_cast<unsigned int>(0) << QString() << "a";
            QTest::newRow("RF-20") << manager << static_cast<int>(QContactRelationship::First) << QString::fromLatin1("UnknownRelationship") << static_cast<unsigned int>(0) << QString() << "b";
            QTest::newRow("RF-21") << manager << static_cast<int>(QContactRelationship::Either) << QString::fromLatin1("UnknownRelationship") << static_cast<unsigned int>(0) << QString() << "ab";
        } else {
            QTest::newRow("RF-19") << manager << static_cast<int>(QContactRelationship::Second) << QString::fromLatin1("UnknownRelationship") << static_cast<unsigned int>(0) << QString() << "";
            QTest::newRow("RF-20") << manager << static_cast<int>(QContactRelationship::First) << QString::fromLatin1("UnknownRelationship") << static_cast<unsigned int>(0) << QString() << "";
            QTest::newRow("RF-21") << manager << static_cast<int>(QContactRelationship::Either) << QString::fromLatin1("UnknownRelationship") << static_cast<unsigned int>(0) << QString() << "";
        }

        // match any contact that is the related contact in a relationship with contact-A
        //QTest::newRow("RF-19") << manager << static_cast<int>(QContactRelationship::Second) << QString() << static_cast<unsigned int>(contactAId.value(manager).localId()) << contactAId.value(manager).managerUri() << "h";
        // match any contact has contact-A as the related contact
        //QTest::newRow("RF-20") << manager << static_cast<int>(QContactRelationship::First) << QString() << static_cast<unsigned int>(contactAId.value(manager).localId()) << contactAId.value(manager).managerUri() << "i";
        // match any contact that has any relationship with contact-A
        //QTest::newRow("RF-21") << manager << static_cast<int>(QContactRelationship::Either) << QString() << static_cast<unsigned int>(contactAId.value(manager).localId()) << contactAId.value(manager).managerUri() << "hi";
    }
}

QContact tst_QContactManagerFiltering::createContact(QContactManager* cm, QContactType::TypeValues type, QString name)
{
    QContact contact;
    contact.setType(type);
    QContactName contactName;
#ifndef DETAIL_DEFINITION_SUPPORTED
    contactName.setFirstName(name);
    contactName.setLastName(name);
    contactName.setMiddleName(name);
    contactName.setPrefix(name);
    contactName.setSuffix(name);
#else
    QContactDetailDefinition detailDefinition = cm->detailDefinition(QContactName::DefinitionName, type);
    detailDefinition.removeField(QContactDetail::FieldContext);
    foreach (const QString &fieldKey, detailDefinition.fields().keys()) {
        contactName.setValue(fieldKey, name);
    }
#endif
    contact.saveDetail(&contactName);
    cm->saveContact(&contact);
    return contact;
}

void tst_QContactManagerFiltering::relationshipFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(int, relatedContactRole);
    QFETCH(QString, relationshipType);
    QFETCH(quint32, relatedContactLocalId);
    QFETCH(QString, otherManagerUri);
    QFETCH(QString, expected);

    // TODO: A little re-factoring could be used to make the test case more readable

    // 1. Create contacts to be used in relationship testing
    QContact contactA;
    if (relationshipType == relationshipString(QContactRelationship::HasMember)) {
        if (!cm->supportedContactTypes().contains(QContactType::TypeGroup))
            QSKIP("Manager does not support groups; skipping relationship filtering");

        // Change contact type to group as this is required at least by symbian backend
        // TODO: should it be possible to query this constraint from the backend?
        contactA = createContact(cm, QContactType::TypeGroup, "ContactA");
    } else {
        contactA = createContact(cm, QContactType::TypeContact, "ContactA");
    }
    QContact contactB = createContact(cm, QContactType::TypeContact, "ContactB");

    // 2. Create the relationship between the contacts
    QContactId firstId(contactA.id());
    QContactId secondId(contactB.id());

    QContactRelationship h2i;
    h2i = makeRelationship(relationshipType, firstId, secondId);
    // save and check error code
    bool succeeded = false;
    const bool relationshipsFeatureSupported = true;
    const bool aribtraryRelationshipsFeatureSupported = true;
    if((relationshipsFeatureSupported
        && cm->isRelationshipTypeSupported(relationshipType, contactA.type())
        && cm->isRelationshipTypeSupported(relationshipType, contactB.type()))
        || aribtraryRelationshipsFeatureSupported) {
        succeeded = true;
        QVERIFY(cm->saveRelationship(&h2i));
        QCOMPARE(cm->error(), QContactManager::NoError);
    } else {
        QVERIFY(!cm->saveRelationship(&h2i));
        QCOMPARE(cm->error(), QContactManager::NotSupportedError);
    }

    // 3. Construct the filter
    QContactId relatedContactId;
    relatedContactId = ContactId::contactId(ContactId::apiId(relatedContactLocalId));
    Q_UNUSED(otherManagerUri)

    QContactRelationshipFilter crf;
    crf.setRelatedContactRole(static_cast<QContactRelationship::Role>(relatedContactRole));
    crf.setRelationshipType(relationshipType);
    QContact rc;
    rc.setId(relatedContactId);
    crf.setRelatedContact(rc);

    // 4. Grab the filtering results
    QList<QContactId> contacts;
    contacts.append(ContactId::apiId(contactA));
    contacts.append(ContactId::apiId(contactB));
    QList<QContactId> ids = cm->contactIds(crf);
    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts

    // 5. Remove the created relationship and contacts
    if(succeeded) {
        // Check that an existing relationship can be removed
        QVERIFY(cm->removeRelationship(h2i));
        QCOMPARE(cm->error(), QContactManager::NoError);
    } else {
        // Check that non-existing relationship cannot be removed
        QVERIFY(!cm->removeRelationship(h2i));
        //TODO: what is the expected error code?
        //QCOMPARE(cm->error(), QContactManager::DoesNotExistError);
    }
    foreach (const QContactId& cid, contacts) {
        cm->removeContact(cid);
    }

    // 6. Verify the filtering result
    if (!relationshipsFeatureSupported) {
        QSKIP("Manager does not support relationships; skipping relationship filtering");
    } else if(relationshipType.isEmpty()
        || (cm->isRelationshipTypeSupported(relationshipType, contactA.type())
            && cm->isRelationshipTypeSupported(relationshipType, contactB.type()))) {
        // check that the relationship type is supported for both contacts.
        QCOMPARE_UNSORTED(output, expected);
    } else {
        QString msg = "Manager does not support relationship type " + relationshipType + " between " + contactA.type() + " and " + contactB.type() + " type contacts.";
        QSKIP(msg.toLatin1());
    }
}

void tst_QContactManagerFiltering::sorting_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<TypeIdentifier>("defname");
    QTest::addColumn<FieldIdentifier>("fieldname");
    QTest::addColumn<int>("directioni");
    QTest::addColumn<bool>("setbp");
    QTest::addColumn<int>("blankpolicyi");
    QTest::addColumn<int>("casesensitivityi");
    QTest::addColumn<QString>("expected");
    QTest::addColumn<QString>("unstable");

    FieldIdentifier firstname = QContactName::FieldFirstName;
    FieldIdentifier lastname = QContactName::FieldLastName;
    TypeIdentifier namedef = detailType<QContactName>();
    TypeIdentifier dldef = detailType<QContactDisplayLabel>();
    FieldIdentifier dlfld = QContactDisplayLabel::FieldLabel;

    int asc = Qt::AscendingOrder;
    int desc = Qt::DescendingOrder;
    int cs = Qt::CaseSensitive;
    int ci = Qt::CaseInsensitive;
    int bll = QContactSortOrder::BlanksLast;
    int blf = QContactSortOrder::BlanksFirst;

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

#if 0 // the nemo sqlite backend has different sorting semantics to what is expected

        FieldSelector integerDefAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("Integer");
        FieldSelector stringDefAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("String");

        newMRow("first ascending", manager) << manager << namedef << firstname << asc << false << 0 << cs << "abcdefghjik" << "efg";  // efg have the same first name
        newMRow("first descending", manager) << manager << namedef << firstname << desc << false << 0 << cs << "kijhefgdcba" << "efg";// efg have the same first name
        newMRow("last ascending", manager) << manager << namedef << lastname << asc << false << 0 << cs << "bacdefghijk" << "hijk";       // all have a well defined, sortable last name except hijk
        newMRow("last descending", manager) << manager << namedef << lastname << desc << false << 0 << cs << "gfedcabhijk" << "hijk";     // all have a well defined, sortable last name except hijk
        if (validDetailInfo(integerDefAndFieldNames)) {
            newMRow("integer ascending, blanks last", manager) << manager << integerDefAndFieldNames.first << integerDefAndFieldNames.second << asc << true << bll << cs << "cabgfedhijk" << "gfedhijk"; // gfedhijk have no integer
            newMRow("integer descending, blanks last", manager) << manager << integerDefAndFieldNames.first << integerDefAndFieldNames.second << desc << true << bll << cs << "bacgfedhijk" << "gfedhijk"; // gfedhijk have no integer
            newMRow("integer ascending, blanks first", manager) << manager << integerDefAndFieldNames.first << integerDefAndFieldNames.second << asc << true << blf << cs << "hijkdefgcab" << "gfedhijk"; // gfedhijk have no integer
            newMRow("integer descending, blanks first", manager) << manager << integerDefAndFieldNames.first << integerDefAndFieldNames.second << desc << true << blf << cs << "hijkdefgbac" << "gfedhijk"; // gfedhijk have no integer
        }
        if (validDetailInfo(stringDefAndFieldNames)) {
            QTest::newRow("string ascending (null value), blanks first") << manager << stringDefAndFieldNames.first << stringDefAndFieldNames.second << asc << true << blf << cs << "feabcdg" << "fehijk"; // f and e have blank string
            QTest::newRow("string ascending (null value), blanks last") << manager << stringDefAndFieldNames.first << stringDefAndFieldNames.second << asc << true << bll << cs << "abcdgef" << "efhijk";   // f and e have blank string
        }

        newMRow("display label insensitive", manager) << manager << dldef << dlfld << asc << false << 0 << ci << "abcdefghjik" << "efghji";
        newMRow("display label sensitive", manager) << manager << dldef << dlfld << asc << false << 0 << cs << "abcdefghjik" << "efg";
#else
        Q_UNUSED(ci)
        Q_UNUSED(bll)
        Q_UNUSED(blf)
        Q_UNUSED(dldef)
        Q_UNUSED(dlfld)
#endif // nemo sqlite collation - instead we ensure the correctness of our ordering code with the following tests:
        newMRow("first ascending, cs, binary collation", manager) << manager << namedef << firstname << asc << false << 0 << cs << "abcdefgikjh" << "efg";
        newMRow("first descending, cs, binary collation", manager) << manager << namedef << firstname << desc << false << 0 << cs << "hjkiefgdcba" << "efg";
        newMRow("last ascending, cs, binary collation", manager) << manager << namedef << lastname << asc << false << 0 << cs << "bacdefgikjh" << "hijk";
        newMRow("last descending, cs, binary collation", manager) << manager << namedef << lastname << desc << false << 0 << cs << "gfedcabhijk" << "hijk";

        // Note - the current display label algorithm follows that of nemo-qml-plugin-contacts, and does not include prefix
        //newMRow("display label insensitive, binary collation", manager) << manager << dldef << dlfld << asc << false << 0 << ci << "bcdefgaijhk" << "efg"; // the display label is synthesized so that A has "Sir" at the start of it (instead of "Aaron").
        //newMRow("display label sensitive, binary collation", manager) << manager << dldef << dlfld << asc << false << 0 << cs << "bcdefgaikjh" << "efg";
    }
}

void tst_QContactManagerFiltering::sorting()
{
    QFETCH(QContactManager*, cm);
    QFETCH(TypeIdentifier, defname);
    QFETCH(FieldIdentifier, fieldname);
    QFETCH(int, directioni);
    QFETCH(bool, setbp);
    QFETCH(int, blankpolicyi);
    QFETCH(int, casesensitivityi);
    QFETCH(QString, expected);
    QFETCH(QString, unstable);

    Qt::SortOrder direction = (Qt::SortOrder)directioni;
    QContactSortOrder::BlankPolicy blankpolicy = (QContactSortOrder::BlankPolicy) blankpolicyi;
    Qt::CaseSensitivity casesensitivity = (Qt::CaseSensitivity) casesensitivityi;
    
    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    /* Build the sort order */
    QContactSortOrder s;
    setSortDetail(s, defname, fieldname); 
    s.setDirection(direction);
    if (setbp)
        s.setBlankPolicy(blankpolicy);
    s.setCaseSensitivity(casesensitivity);

    ids = cm->contactIds(s);
    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts

    // It's possible to get some contacts back in an arbitrary order (since we single sort)
    if (unstable.length() > 1) {
        // ensure that the maximum distance between unstable elements in the output is the size of the unstable string.
        int firstIndex = -1;
        int lastIndex = -1;

        for (int i = 0; i < output.size(); i++) {
            if (unstable.contains(output.at(i))) {
                firstIndex = i;
                break;
            }
        }

        for (int i = output.size() - 1; i >= 0; i--) {
            if (unstable.contains(output.at(i))) {
                lastIndex = i;
                break;
            }
        }

        if (firstIndex == -1 || lastIndex == -1) {
            bool containsAllUnstableElements = false;
            QVERIFY(containsAllUnstableElements);
        }

        bool unstableElementsAreGrouped = ((lastIndex - firstIndex) == (unstable.length() - 1));
        QVERIFY(unstableElementsAreGrouped);

        // now remove all unstable elements from the output
        for (int i = 1; i < unstable.length(); i++) {
            output.remove(unstable.at(i));
            expected.remove(unstable.at(i));
        }
    }

    QCOMPARE(output, expected);

    /* Now do a check with a filter involved; the filter should not affect the sort order */
    QContactDetailFilter presenceName;
    setFilterDetail<QContactName>(presenceName);

    ids = cm->contactIds(presenceName, s);

    output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts

    // It's possible to get some contacts back in an arbitrary order (since we single sort)
    if (unstable.length() > 1) {
        // ensure that the maximum distance between unstable elements in the output is the size of the unstable string.
        int firstIndex = -1;
        int lastIndex = -1;

        for (int i = 0; i < output.size(); i++) {
            if (unstable.contains(output.at(i))) {
                firstIndex = i;
                break;
            }
        }

        for (int i = output.size() - 1; i >= 0; i--) {
            if (unstable.contains(output.at(i))) {
                lastIndex = i;
                break;
            }
        }

        if (firstIndex == -1 || lastIndex == -1) {
            bool containsAllUnstableElements = false;
            QVERIFY(containsAllUnstableElements);
        }

        bool unstableElementsAreGrouped = ((lastIndex - firstIndex) == (unstable.length() - 1));
        QVERIFY(unstableElementsAreGrouped);

        // now remove all unstable elements from the output
        for (int i = 1; i < unstable.length(); i++) {
            output.remove(unstable.at(i));
            expected.remove(unstable.at(i));
        }
    }

    QCOMPARE(output, expected);
}

void tst_QContactManagerFiltering::multiSorting_data()
{
    QTest::addColumn<QContactManager *>("cm");

    QTest::addColumn<bool>("firstsort");
    QTest::addColumn<TypeIdentifier>("fsdefname");
    QTest::addColumn<FieldIdentifier>("fsfieldname");
    QTest::addColumn<int>("fsdirectioni");

    QTest::addColumn<bool>("secondsort");
    QTest::addColumn<TypeIdentifier>("ssdefname");
    QTest::addColumn<FieldIdentifier>("ssfieldname");
    QTest::addColumn<int>("ssdirectioni");

    QTest::addColumn<QString>("expected");
    QTest::addColumn<bool>("efunstable");
    QTest::addColumn<bool>("efgunstable");


    QString es;

    FieldIdentifier firstname = QContactName::FieldFirstName;
    FieldIdentifier lastname = QContactName::FieldLastName;
    TypeIdentifier namedef = detailType<QContactName>();
    TypeIdentifier phonedef = detailType<QContactPhoneNumber>();
    FieldIdentifier numberfield = QContactPhoneNumber::FieldNumber;

    TypeIdentifier noType(QContactDetail::TypeUndefined);
    FieldIdentifier noField(-1);

    int asc = Qt::AscendingOrder;
    int desc = Qt::DescendingOrder;

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        FieldSelector stringDefAndFieldNames = defAndFieldNamesForTypePerManager.value(manager).value("String");

        QTest::newRow("1") << manager
                           << true << namedef << firstname << asc
                           << true << namedef << lastname << asc
                           << "abcdefg" << false << false;
        QTest::newRow("2") << manager
                           << true << namedef << firstname << asc
                           << true << namedef << lastname << desc
                           << "abcdgfe" << false << false;
        QTest::newRow("3") << manager
                           << true << namedef << firstname << desc
                           << true << namedef << lastname << asc
                           << "efgdcba" << false << false;
        QTest::newRow("4") << manager
                           << true << namedef << firstname << desc
                           << true << namedef << lastname << desc
                           << "gfedcba" << false << false;

        QTest::newRow("5") << manager
                           << true << namedef << firstname << asc
                           << false << namedef << lastname << asc
                           << "abcdefg" << true << true;

        QTest::newRow("5b") << manager
                           << true << namedef << firstname << asc
                           << true << noType << noField << asc
                           << "abcdefg" << true << true;

        QTest::newRow("6") << manager
                           << false << namedef << firstname << asc
                           << true << namedef << lastname << asc
                           << "bacdefg" << false << false;

        // This test is completely unstable; no sort criteria means dependent upon internal sort order of manager.
        //QTest::newRow("7") << manager
        //                   << false << namedef << firstname << asc
        //                   << false << namedef << lastname << asc
        //                   << "abcdefg" << false; // XXX Isn't this totally unstable?

        if (validDetailInfo(stringDefAndFieldNames)) {
            QTest::newRow("8") << manager
                               << true
                               << stringDefAndFieldNames.first
                               << stringDefAndFieldNames.second
                               << asc
                               << false
                               << stringDefAndFieldNames.first
                               << stringDefAndFieldNames.second
                               << desc
                               << "abcdgef" << true << false; // default policy = blanks last, and ef have no value (e is empty, f is null)

            QTest::newRow("8b") << manager
                               << true
                               << stringDefAndFieldNames.first
                               << stringDefAndFieldNames.second
                               << asc
                               << false << noType << noField << desc
                               << "abcdgef" << true << false; // default policy = blanks last, and ef have no value (e is empty, f is null)
        }

#if 0
        QTest::newRow("9") << manager
                           << true << phonedef << numberfield << asc
                           << true << namedef << lastname << desc
                           << "abgfedc" << false;
#else
        // We can't sort by phone number since there may be multiple instances per contact
        Q_UNUSED(phonedef)
        Q_UNUSED(numberfield)
#endif

        QTest::newRow("10") << manager
                            << true << namedef << firstname << asc
                            << true << namedef << firstname << desc
                            << "abcdefg" << true << true;

    }
}

void tst_QContactManagerFiltering::multiSorting()
{
    QFETCH(QContactManager*, cm);
    QFETCH(bool, firstsort);
    QFETCH(TypeIdentifier, fsdefname);
    QFETCH(FieldIdentifier, fsfieldname);
    QFETCH(int, fsdirectioni);
    QFETCH(bool, secondsort);
    QFETCH(TypeIdentifier, ssdefname);
    QFETCH(FieldIdentifier, ssfieldname);
    QFETCH(int, ssdirectioni);
    QFETCH(QString, expected);
    QFETCH(bool, efunstable);
    QFETCH(bool, efgunstable);

    Qt::SortOrder fsdirection = (Qt::SortOrder)fsdirectioni;
    Qt::SortOrder ssdirection = (Qt::SortOrder)ssdirectioni;

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);

    /* Build the sort orders */
    QContactSortOrder fs;
    setSortDetail(fs, fsdefname, fsfieldname);
    fs.setDirection(fsdirection);
    QContactSortOrder ss;
    setSortDetail(ss, ssdefname, ssfieldname);
    ss.setDirection(ssdirection);
    QList<QContactSortOrder> sortOrders;
    if (firstsort)
        sortOrders.append(fs);
    if (secondsort)
        sortOrders.append(ss);

    QList<QContactId> ids = cm->contactIds(sortOrders);
    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts

    // Remove the display label tests
    output.remove('h');
    output.remove('i');
    output.remove('j');
    output.remove('k');

    // Just like the single sort test, we might get some contacts back in indeterminate order
    // (but their relative position with other contacts should not change)
    if (efunstable || efgunstable) {
        QVERIFY(output.count('e') == 1);
        QVERIFY(output.count('f') == 1);
        if (efgunstable) {
            QVERIFY(output.count('g') == 1);
        }

        output.remove('f');
        expected.remove('f');
        if (efgunstable) {
            output.remove('g');
            expected.remove('g');
        }
    }

    QCOMPARE(output, expected);
}

void tst_QContactManagerFiltering::idListFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QString es;

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        newMRow("empty", manager) << manager << es << es;
        newMRow("a", manager) << manager << "a" << "a";
        newMRow("ab", manager) << manager << "ab" << "ab";
        newMRow("aa", manager) << manager << "aa" << "a";
        newMRow("ba", manager) << manager << "ba" << "ab";
        newMRow("abcd", manager) << manager << "abcd" << "abcd";
        newMRow("abcdefg", manager) << manager << "abcdefg" << "abcd";
    }
}

void tst_QContactManagerFiltering::idListFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(QString, input);
    QFETCH(QString, expected);

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QList<QContactId> ids;

    // 3 extra ids that (hopefully) won't exist
    QContactId e = ContactId::apiId(0x54555657);
    QContactId f = ContactId::apiId(0x96969696);
    QContactId g = ContactId::apiId(0x44335566);

    /* Convert the input to a list of ids */
    foreach (const QChar &c, input) {
        if (c == 'a')
            ids << contacts.at(0);
        else if (c == 'b')
            ids << contacts.at(1);
        else if (c == 'c')
            ids << contacts.at(2);
        else if (c == 'd')
            ids << contacts.at(3);
        else if (c == 'e')
            ids << e;
        else if (c == 'f')
            ids << f;
        else if (c == 'g')
            ids << g;
    }

    /* And do the search */
    QContactIdFilter idf;
    idf.setIds(ids);

    /* Retrieve contacts matching the filter, and compare (unsorted) output */
    ids = cm->contactIds(idf);
    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);
}

void tst_QContactManagerFiltering::convenienceFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<QString>("addressSubString");
    QTest::addColumn<bool>("addressEnabled");
    QTest::addColumn<QString>("emailAddressSubString");
    QTest::addColumn<bool>("emailEnabled");
    QTest::addColumn<QString>("phoneSubString");
    QTest::addColumn<bool>("phoneEnabled");
    QTest::addColumn<QString>("displayLabelSubString");
    QTest::addColumn<bool>("displayLabelEnabled");
    QTest::addColumn<QString>("nameSubString");
    QTest::addColumn<bool>("nameEnabled");
    QTest::addColumn<bool>("favoriteEnabled");
    QTest::addColumn<QString>("tagSubString");
    QTest::addColumn<bool>("tagEnabled");
    QTest::addColumn<QString>("expected");

    QString es; // empty string

    QSet<QContactDetail::DetailType> allDefs;
    allDefs.insert(detailType<QContactAddress>());
    allDefs.insert(detailType<QContactEmailAddress>());
    allDefs.insert(detailType<QContactFavorite>());
    allDefs.insert(detailType<QContactName>());
    allDefs.insert(detailType<QContactPhoneNumber>());
    allDefs.insert(detailType<QContactTag>());

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        if (allDefs.contains(detailType<QContactAddress>())) {
            newMRow("address matching only", manager) << manager
                                                      << "streetstring" << true
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << false // Favorite has no substring associated.
                                                      << es << false
                                                      << "l";
        }
        if (allDefs.contains(detailType<QContactEmailAddress>())) {
            newMRow("emailAddress matching only", manager) << manager
                                                      << es << false
                                                      << "@test.com" << true
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << false
                                                      << es << false
                                                      << "m";
        }
        if (allDefs.contains(detailType<QContactPhoneNumber>())) {
            newMRow("phone matching only", manager) << manager
                                                      << es << false
                                                      << es << false
                                                      << "12345" << true
                                                      << es << false
                                                      << es << false
                                                      << false
                                                      << es << false
                                                      << "n";
        }
        if (allDefs.contains(detailType<QContactDisplayLabel>())) {
            newMRow("displayLabel matching only", manager) << manager
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << "Freddy" << true
                                                      << es << false
                                                      << false
                                                      << es << false
                                                      << "o";
        }
        if (allDefs.contains(detailType<QContactName>())) {
            newMRow("name matching only", manager) << manager
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << "Frederic" << true
                                                      << false
                                                      << es << false
                                                      << "p";
        }
        if (allDefs.contains(detailType<QContactFavorite>())) {
            newMRow("favorite matching only", manager) << manager
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << true
                                                      << es << false
                                                      << "q";
        }
        if (allDefs.contains(detailType<QContactTag>())) {
            newMRow("tag matching only", manager) << manager
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << false
                                                      << "Football" << true
                                                      << "r";
        }
        if (allDefs.contains(detailType<QContactAddress>()) &&
                allDefs.contains(detailType<QContactPhoneNumber>())) {
            newMRow("address or phone matching", manager) << manager
                                                      << "streetstring" << true
                                                      << es << false
                                                      << "12345" << true
                                                      << es << false
                                                      << es << false
                                                      << false
                                                      << es << false
                                                      << "ln";
        }
        if (allDefs.contains(detailType<QContactFavorite>())
                && allDefs.contains(detailType<QContactTag>())) {
            newMRow("favorite or tag matching", manager) << manager
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << es << false
                                                      << true
                                                      << "Football" << true
                                                      << "qr";
        }
    }
}

void tst_QContactManagerFiltering::convenienceFiltering()
{
    QFETCH(QContactManager*, cm);
    QFETCH(QString, addressSubString);
    QFETCH(bool, addressEnabled);
    QFETCH(QString, emailAddressSubString);
    QFETCH(bool, emailEnabled);
    QFETCH(QString, phoneSubString);
    QFETCH(bool, phoneEnabled);
    QFETCH(QString, displayLabelSubString);
    QFETCH(bool, displayLabelEnabled);
    QFETCH(QString, nameSubString);
    QFETCH(bool, nameEnabled);
    QFETCH(bool, favoriteEnabled);
    QFETCH(QString, tagSubString);
    QFETCH(bool, tagEnabled);
    QFETCH(QString, expected);

    QContactFilter af = QContactAddress::match(addressSubString);
    QContactFilter ef = QContactEmailAddress::match(emailAddressSubString);
    QContactFilter pf = QContactPhoneNumber::match(phoneSubString);
    QContactFilter df = QContactDisplayLabel::match(displayLabelSubString);
    QContactFilter nf = QContactName::match(nameSubString);
    QContactFilter ff = QContactFavorite::match();
    QContactFilter tf = QContactTag::match(tagSubString);

    QList<QContactFilter> convenienceFilters;
    if (addressEnabled) convenienceFilters << af;
    if (emailEnabled) convenienceFilters << ef;
    if (phoneEnabled) convenienceFilters << pf;
    if (displayLabelEnabled) convenienceFilters << df;
    if (nameEnabled) convenienceFilters << nf;
    if (favoriteEnabled) convenienceFilters << ff;
    if (tagEnabled) convenienceFilters << tf;

    QContactFilter finalFilter;
    finalFilter = convenienceFilters.at(0);
    if (convenienceFilters.size() > 1) {
        for (int i = 1; i < convenienceFilters.size(); ++i) {
            // if more than one filter, we union them.
            finalFilter = (finalFilter | convenienceFilters.at(i));
        }
    }

    /* Retrieve contacts matching the filter, and ensure that the results are expected */
    QList<QContactId> ids = cm->contactIds(finalFilter);

    // build a string containing letters corresponding to the ids we retrieved.
    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QString resultString = convertIds(contacts, ids, 'l', 'r'); // just the convenience filtering contacts (L->R)
    QCOMPARE(resultString, expected);
}

void tst_QContactManagerFiltering::invalidFiltering_data()
{
    QTest::addColumn<QContactManager*>("cm");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        QTest::newRow(manager->managerName().toLatin1().constData()) << manager;
    }
}

void tst_QContactManagerFiltering::invalidFiltering()
{
    QFETCH(QContactManager*, cm);

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QContactInvalidFilter f; // invalid
    QList<QContactId> ids = cm->contactIds(f);
    QVERIFY(ids.count() == 0);

    // Try unions/intersections of invalids too
    ids = cm->contactIds(f | f);
    QVERIFY(ids.count() == 0);

    ids = cm->contactIds(f & f);
    QVERIFY(ids.count() == 0);
}

void tst_QContactManagerFiltering::allFiltering_data()
{
    QTest::addColumn<QContactManager*>("cm");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        QTest::newRow(manager->managerName().toLatin1().constData()) << manager;
    }
}

void tst_QContactManagerFiltering::allFiltering()
{
    QFETCH(QContactManager*, cm);

    QList<QContactId> contacts = contactsAddedToManagers.values(cm);
    QContactFilter f; // default = permissive
    QList<QContactId> ids = cm->contactIds(f);
    QVERIFY(ids.count() == contacts.size());
    QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QString expected = convertIds(contacts, contacts, 'a', 'k'); // :)
    QCOMPARE_UNSORTED(output, expected);

    // Try unions/intersections of defaults
    ids = cm->contactIds(f | f);
    output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);

    ids = cm->contactIds(f & f);
    output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
    QCOMPARE_UNSORTED(output, expected);
}

void tst_QContactManagerFiltering::fetchHint_data()
{
    QTest::addColumn<QContactManager*>("cm");

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);
        QTest::newRow(manager->managerName().toLatin1().constData()) << manager;
    }
}

void tst_QContactManagerFiltering::fetchHint()
{
    QFETCH(QContactManager*, cm);

    // if no fetch hint is provided, the manager should return all data
    // if a fetch hint is provided, it should have a clearly defined effect,
    // unless it is ignored by the manager, in which case the result should
    // be equivalent to not providing a fetch hint.

    // we use a defined sort order for the retrieval of contacts to make comparison simple.
    // We sort on name, because we include name details in the fetch hint.
    QList<QContactSortOrder> nameSort;
    QContactSortOrder firstNameSort, middleNameSort, lastNameSort;
    firstNameSort.setDetailType(detailType<QContactName>(), QContactName::FieldFirstName);
    middleNameSort.setDetailType(detailType<QContactName>(), QContactName::FieldMiddleName);
    lastNameSort.setDetailType(detailType<QContactName>(), QContactName::FieldLastName);
    nameSort << lastNameSort << middleNameSort << firstNameSort;

    // fetch all contacts from the manager.
    QList<QContact> allContacts = cm->contacts(nameSort);

    // define some maximum count limit, and some list of detail definitions to retrieve.
    int countLimit = (allContacts.size() / 2) + 1;
    QList<QContactDetail::DetailType> defs;
    defs << detailType<QContactName>()
         << detailType<QContactPhoneNumber>();

    // test that the manager doesn't incorrectly implement fetch hints.
    // we test max count limit, and detail definition limits.
    // XXX TODO: other hints!
    QContactFetchHint mclh; // max count limited hint
    QContactFetchHint ddh;  // detail definitions hint
    mclh.setMaxCountHint(countLimit);
    ddh.setDetailTypesHint(defs);

    // the next part of the test requires some contacts to be saved in the manager.
    if (allContacts.size() == 0) {
        QSKIP("No contacts in manager; skipping fetch hint limit test.");
    }

    // test with a hint which sets a maximum count limit for retrieved contacts.
    QList<QContact> mclhContacts = cm->contacts(nameSort, mclh);
    QVERIFY(allContacts.size() >= mclhContacts.size());
    if (allContacts.size() > mclh.maxCountHint()) {
        // shouldn't return an arbitrarily smaller amount of contacts.
        QVERIFY(mclhContacts.size() == mclh.maxCountHint()
                || mclhContacts.size() == allContacts.size());
    }
    for (int i = 0; i < mclhContacts.size(); ++i) {
        // the sort order should still be defined.
        QVERIFY(mclhContacts.at(i) == allContacts.at(i));
    }

    // now test with a hint which describes which details the client is interested in.
    QList<QContact> ddhContacts = cm->contacts(nameSort, ddh);
    QCOMPARE(ddhContacts.size(), allContacts.size());
    for (int i = 0; i < allContacts.size(); ++i) {
        QContact a = allContacts.at(i);
        QContact b = ddhContacts.at(i);

        // since we're sorting on a detail which should exist (since it was included in the hint)
        // the order of the contacts returned shouldn't have changed.
        QVERIFY(a.id() == b.id());

        // check that the hint didn't remove names or phones.
        QCOMPARE(a.details<QContactName>().size(),
                 b.details<QContactName>().size());
        QCOMPARE(a.details<QContactPhoneNumber>().size(),
                 b.details<QContactPhoneNumber>().size());

        // other details are not necessarily returned.
        QVERIFY(a.details().size() >= b.details().size());
    }
}


#if 0
void tst_QContactManagerFiltering::changelogFiltering_data()
{
    QTest::addColumn<QContactManager *>("cm");
    QTest::addColumn<QList<QContactId> >("contacts");
    QTest::addColumn<int>("eventType");
    QTest::addColumn<QDateTime>("since");
    QTest::addColumn<QString>("expected");

    int added = (int)QContactChangeLogFilter::EventAdded;
    int changed = (int)QContactChangeLogFilter::EventChanged;
    int removed = (int)QContactChangeLogFilter::EventRemoved;

    for (int i = 0; i < managers.size(); i++) {
        QContactManager *manager = managers.at(i);

        if (manager->hasFeature(QContactManager::ChangeLogs)) {
            QList<QContactId> contacts = contactsAddedToManagers.values(manager);
            QContact a,b,c,d;
            a = manager->contact(contacts.at(0));
            b = manager->contact(contacts.at(1));
            c = manager->contact(contacts.at(2));
            d = manager->contact(contacts.at(3));

            QDateTime ac = a.detail<QContactTimestamp>().created();
            QDateTime bc = b.detail<QContactTimestamp>().created();
            QDateTime cc = c.detail<QContactTimestamp>().created();
            QDateTime dc = d.detail<QContactTimestamp>().created();

            QDateTime am = a.detail<QContactTimestamp>().lastModified();
            QDateTime bm = b.detail<QContactTimestamp>().lastModified();
            QDateTime cm = c.detail<QContactTimestamp>().lastModified();
            QDateTime dm = d.detail<QContactTimestamp>().lastModified();

            newMRow("Added since before start", manager) << manager << contacts << added << ac.addSecs(-1) << "abcdefg";
            newMRow("Added since first", manager) << manager << contacts << added << ac << "abcdefg";
            newMRow("Added since second", manager) << manager << contacts << added << bc << "bcdefg";
            newMRow("Added since third", manager) << manager << contacts << added << cc << "cdefg";
            newMRow("Added since fourth", manager) << manager << contacts << added << dc << "defg";
            newMRow("Added since after fourth", manager) << manager << contacts << added << dc.addSecs(1) << "efg";
            newMRow("Added since first changed", manager) << manager << contacts << added << am << "";
            newMRow("Added since second changed", manager) << manager << contacts << added << bm << "";
            newMRow("Added since third changed", manager) << manager << contacts << added << cm << "";
            newMRow("Added since fourth changed", manager) << manager << contacts << added << cm << "";

            newMRow("Changed since before start", manager) << manager << contacts << changed << ac.addSecs(-1) << "abcdefg";
            newMRow("Changed since first", manager) << manager << contacts << changed << ac << "abcdefg";
            newMRow("Changed since second", manager) << manager << contacts << changed << bc << "abcdefg";
            newMRow("Changed since third", manager) << manager << contacts << changed << cc << "abcdefg";
            newMRow("Changed since fourth", manager) << manager << contacts << changed << dc << "abcdefg";
            newMRow("Changed since after fourth", manager) << manager << contacts << changed << dc.addSecs(1) << "abcefg";
            newMRow("Changed since first changed", manager) << manager << contacts << changed << am << "a";
            newMRow("Changed since second changed", manager) << manager << contacts << changed << bm << "ab";
            newMRow("Changed since third changed", manager) << manager << contacts << changed << cm << "abc";
            newMRow("Changed since fourth changed", manager) << manager << contacts << changed << dm << "abcdefg";

            // These are currently useless..
            newMRow("Removed since before start", manager) << manager << contacts << removed << ac.addSecs(-1) << "";
            newMRow("Removed since first", manager) << manager << contacts << removed << ac << "";
            newMRow("Removed since second", manager) << manager << contacts << removed << bc << "";
            newMRow("Removed since third", manager) << manager << contacts << removed << cc << "";
            newMRow("Removed since fourth", manager) << manager << contacts << removed << dc << "";
            newMRow("Removed since after fourth", manager) << manager << contacts << removed << dc.addSecs(1) << "";
        } else {
            // Stop spam and asserts with a single row
            newMRow("Unsupported", manager) << manager << QList<QContactId>() << added << QDateTime() << QString();
        }
    }
}

void tst_QContactManagerFiltering::changelogFiltering()
{
    QFETCH(int, eventType);
    QFETCH(QDateTime, since);
    QFETCH(QString, expected);
    QFETCH(QContactManager*, cm);
    QFETCH(QList<QContactId>, contacts);

    if (cm->hasFeature(QContactManager::ChangeLogs)) {
        QList<QContactId> ids;

        QContactChangeLogFilter clf((QContactChangeLogFilter::EventType)eventType);
        clf.setSince(since);

        ids = cm->contactIds(clf);

        QString output = convertIds(contacts, ids, 'a', 'k'); // don't include the convenience filtering contacts
        QCOMPARE(output, expected); // unsorted? or sorted?
    } else {
        QSKIP("Changelogs not supported by this manager.");
    }
}
#endif

#ifdef DETAIL_DEFINITION_SUPPORTED
QPair<QString, QString> tst_QContactManagerFiltering::definitionAndField(QContactManager *cm, QVariant::Type type, bool *nativelyFilterable)
{
    QPair<QString, QString> result;
    QString definitionName, fieldName;

    // step one: search for an existing definition with a field of the specified type
    QMap<QString, QContactDetailDefinition> allDefs = cm->detailDefinitions();
    QStringList defNames = allDefs.keys();
    bool found = false;
    bool isNativelyFilterable = false;
    foreach (const QString& defName, defNames) {
        // check the current definition.
        QContactDetailDefinition def = allDefs.value(defName);

        // if unique, we cannot use this definition.
        if (def.isUnique()) {
            continue;
        }

        // if read only, we cannot use this definition.
        // special case these, since read-only is reported via details, not definitions...
        if (def.name() == QString(QLatin1String(QContactName::DefinitionName)) || def.name() == QString(QLatin1String(QContactPresence::DefinitionName)) || def.name() == QString(QLatin1String(QContactGlobalPresence::DefinitionName))) {
            continue;
        }

        // grab the fields and search for a field of the required type
        QMap<QString, QContactDetailFieldDefinition> allFields = def.fields();
        QList<QString> fNames = allFields.keys();
        foreach (const QString& fName, fNames) {
            QContactDetailFieldDefinition field = allFields.value(fName);
            if (field.dataType() == type) {
                // this field of the current definition is of the required type.
                definitionName = defName;
                fieldName = fName;
                found = true;

                // step two: check to see whether the definition/field is natively filterable
                QContactDetailFilter filter;
                filter.setDetailDefinitionName(definitionName, fieldName);
                bool isNativelyFilterable = cm->isFilterSupported(filter);

                if (isNativelyFilterable) {
                    // we've found the optimal definition + field for our test.
                    break;
                }
            }
        }

        if (found && isNativelyFilterable) {
            // we've found the optimal definition + field for our test.
            break;
        }
    }

    if (found) {
        // whether it is natively filterable or not, we found a definition that matches our requirements.
        result.first = definitionName;
        result.second = fieldName;
        *nativelyFilterable = isNativelyFilterable;
        return result;
    }

    // step three (or, if not step one): check to see whether the manager allows mutable definitions
    // no existing definition matched our requirements, but we might be able to add one that does.
    if (cm->supportedDataTypes().contains(type) && cm->hasFeature(QContactManager::MutableDefinitions)) {
        // ok, the manager does not have a definition matching our criteria, but we could probably add it.
        int defCount = detailDefinitionsAddedToManagers.values(cm).count();
        QString generatedDefinitionName = QString("x-nokia-mobility-contacts-test-definition-") + QString::number((defCount+1));

        // build a definition that matches the criteria.
        QContactDetailDefinition generatedDefinition;
        generatedDefinition.setName(generatedDefinitionName);
        QContactDetailFieldDefinition generatedField;
        generatedField.setDataType(type);
        QMap<QString, QContactDetailFieldDefinition> fields;
        fields.insert("generatedField", generatedField);
        generatedDefinition.setFields(fields);
        generatedDefinition.setUnique(false);

        // attempt to save it to the manager.
        if (cm->saveDetailDefinition(generatedDefinition)) {
            // successfully added definition.
            definitionName = generatedDefinitionName;
            fieldName = "generatedField";
            detailDefinitionsAddedToManagers.insert(cm, definitionName); // cleanup stack.
        }
    } else {
        qWarning() << "Unable to perform tests involving detail values of the" << type << "type: not supported by manager:" << cm->managerName();
    }

    result.first = definitionName;
    result.second = fieldName;
    *nativelyFilterable = false;
    return result;
}
#endif

QList<QContactId> tst_QContactManagerFiltering::prepareModel(QContactManager *cm)
{
#ifdef DETAIL_DEFINITION_SUPPORTED
    /* Discover the definition and field names required for testing */
    QMap<QString, QPair<QString, QString> > definitionDetails; // per value type string
    QPair<QString, QString> defAndFieldNames;
    bool nativelyFilterable;
    // If the engine doesn't support changelogs, don't insert pauses.
    bool supportsChangelog = cm->hasFeature(QContactManager::ChangeLogs);
    const int napTime = supportsChangelog ? 2000 : 1;

    /* For our test actions: memory engine, add the "special" definitions. */
    if (cm->managerName() == QString(QLatin1String("memory"))) {
        QContactDetailDefinition def;
        QContactDetailFieldDefinition field;
        QMap<QString, QContactDetailFieldDefinition> fields;

        // integer
        def.setName("IntegerDefinition");
        field.setDataType(QVariant::Int);
        field.setAllowableValues(QVariantList());
        fields.clear();
        fields.insert("IntegerField", field);
        def.setFields(fields);
        defAndFieldNames = QPair<QString, QString>("IntegerDefinition", "IntegerField");
        definitionDetails.insert("Integer", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
        cm->saveDetailDefinition(def, QContactType::TypeContact);

        // double
        def.setName("DoubleDefinition");
        field.setDataType(QVariant::Double);
        field.setAllowableValues(QVariantList());
        fields.clear();
        fields.insert("DoubleField", field);
        def.setFields(fields);
        defAndFieldNames = QPair<QString, QString>("DoubleDefinition", "DoubleField");
        definitionDetails.insert("Double", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
        cm->saveDetailDefinition(def, QContactType::TypeContact);

        // boolean
        def.setName("BooleanDefinition");
        field.setDataType(QVariant::Bool);
        field.setAllowableValues(QVariantList());
        fields.clear();
        fields.insert("BooleanField", field);
        def.setFields(fields);
        defAndFieldNames = QPair<QString, QString>("BooleanDefinition", "BooleanField");
        definitionDetails.insert("Boolean", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
        cm->saveDetailDefinition(def, QContactType::TypeContact);

        // date
        def.setName("DateDefinition");
        field.setDataType(QVariant::Date);
        field.setAllowableValues(QVariantList());
        fields.clear();
        fields.insert("DateField", field);
        def.setFields(fields);
        defAndFieldNames = QPair<QString, QString>("DateDefinition", "DateField");
        definitionDetails.insert("Date", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
        cm->saveDetailDefinition(def, QContactType::TypeContact);
    }

    /* String */
    /* Override this to ensure we select a string field that supports sorting:
    defAndFieldNames = definitionAndField(cm, QVariant::String, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("String", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();
    */
    definitionDetails.insert("String", qMakePair(QString(QLatin1String(QContactGuid::DefinitionName)), QString(QLatin1String(QContactGuid::FieldGuid))));
    defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);

    /* Integer */
    defAndFieldNames = definitionAndField(cm, QVariant::Int, &nativelyFilterable);
    if (cm->managerName() != "memory" && !defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        // we don't insert for memory engine, as we already handled this above (for action filtering)
        definitionDetails.insert("Integer", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* Date time detail */
    defAndFieldNames = definitionAndField(cm, QVariant::DateTime, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("DateTime", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* double detail */
    defAndFieldNames = definitionAndField(cm, QVariant::Double, &nativelyFilterable);
    if (cm->managerName() != "memory" && !defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        // we don't insert for memory engine, as we already handled this above (for action filtering)
        definitionDetails.insert("Double", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* bool */
    defAndFieldNames = definitionAndField(cm, QVariant::Bool, &nativelyFilterable);
    if (cm->managerName() != "memory" && !defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        // we don't insert for memory engine, as we already handled this above (for action filtering)
        definitionDetails.insert("Bool", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* long long */
    defAndFieldNames = definitionAndField(cm, QVariant::LongLong, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("LongLong", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* unsigned long long */
    defAndFieldNames = definitionAndField(cm, QVariant::ULongLong, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("ULongLong", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* date */
    defAndFieldNames = definitionAndField(cm, QVariant::Date, &nativelyFilterable);
    if (cm->managerName() != "memory" && !defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        // we don't insert for memory engine, as we already handled this above (for action filtering)
        definitionDetails.insert("Date", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* time */
    defAndFieldNames = definitionAndField(cm, QVariant::Time, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("Time", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* uint */
    defAndFieldNames = definitionAndField(cm, QVariant::UInt, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("UInt", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();

    /* char */
    defAndFieldNames = definitionAndField(cm, QVariant::Char, &nativelyFilterable);
    if (!defAndFieldNames.first.isEmpty() && !defAndFieldNames.second.isEmpty()) {
        definitionDetails.insert("Char", defAndFieldNames);
        defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);
    }
    defAndFieldNames.first = QString();
    defAndFieldNames.second = QString();
#else
    const int napTime = 1;
#endif

    QMap<QString, QPair<QContactDetail::DetailType, int> > definitionDetails;
    //definitionDetails.insert("String", qMakePair(QContactOrganization::Type, static_cast<int>(QContactOrganization::FieldName)));
    definitionDetails.insert("String", qMakePair(QContactGuid::Type, static_cast<int>(QContactGuid::FieldGuid)));
    definitionDetails.insert("Integer", qMakePair(QContactPresence::Type, static_cast<int>(QContactPresence::FieldPresenceState)));
    definitionDetails.insert("DateTime", qMakePair(QContactTimestamp::Type, static_cast<int>(QContactTimestamp::FieldCreationTimestamp)));
    definitionDetails.insert("Date", qMakePair(QContactBirthday::Type, static_cast<int>(QContactBirthday::FieldBirthday)));
    definitionDetails.insert("Bool", qMakePair(QContactFavorite::Type, static_cast<int>(QContactFavorite::FieldFavorite)));
    // Only used by geolocation - not supported by qtcontacts-sqlite
    definitionDetails.insert("Double", qMakePair(QContactDetail::TypeUndefined, -1));
    // Unused:
    definitionDetails.insert("LongLong", qMakePair(QContactDetail::TypeUndefined, -1));
    definitionDetails.insert("ULongLong", qMakePair(QContactDetail::TypeUndefined, -1));
    definitionDetails.insert("Time", qMakePair(QContactDetail::TypeUndefined, -1));
    definitionDetails.insert("UInt", qMakePair(QContactDetail::TypeUndefined, -1));
    definitionDetails.insert("Char", qMakePair(QContactDetail::TypeUndefined, -1));

    defAndFieldNamesForTypePerManager.insert(cm, definitionDetails);

    /* Add some contacts */
    QContact contactA, contactB, contactC, contactD;
    QContactName name;
    QContactPhoneNumber number;
    QContactDetail string(definitionDetails.value("String").first);
    QContactDetail integer(definitionDetails.value("Integer").first);
    QContactDetail datetime(definitionDetails.value("DateTime").first);
    QContactDetail dubble(definitionDetails.value("Double").first);
    QContactDetail boool(definitionDetails.value("Bool").first);
    QContactDetail llong(definitionDetails.value("LongLong").first);
    QContactDetail ullong(definitionDetails.value("ULongLong").first);
    QContactDetail date(definitionDetails.value("Date").first);
    QContactDetail time(definitionDetails.value("Time").first);
    QContactDetail uintt(definitionDetails.value("UInt").first);
    QContactDetail charr(definitionDetails.value("Char").first);

    name.setFirstName("Aaron");
    name.setLastName("Aaronson");
#ifdef DETAIL_DEFINITION_SUPPORTED
    if (cm->detailDefinition(QContactName::DefinitionName).fields().contains(QContactName::FieldMiddleName))
#endif
        name.setMiddleName("Arne");
#ifdef DETAIL_DEFINITION_SUPPORTED
    if (cm->detailDefinition(QContactName::DefinitionName).fields().contains(QContactName::FieldPrefix))
#endif
        name.setPrefix("Sir");
#ifdef DETAIL_DEFINITION_SUPPORTED
    if (cm->detailDefinition(QContactName::DefinitionName).fields().contains(QContactName::FieldSuffix))
#endif
        name.setSuffix("Dr.");
    QContactNickname nick;
    nick.setNickname("Sir Aaron");
    QContactEmailAddress emailAddr;
    emailAddr.setEmailAddress("Aaron@Aaronson.com");
    number.setNumber("5551212");
    string.setValue(definitionDetails.value("String").second, "Aaron Aaronson");
    integer.setValue(definitionDetails.value("Integer").second, 3); // QContactPresence::PresenceBusy
    datetime.setValue(definitionDetails.value("DateTime").second, QDateTime(QDate(2009, 06, 29), QTime(16, 52, 23, 0)));
    boool.setValue(definitionDetails.value("Bool").second, true);
    ullong.setValue(definitionDetails.value("ULongLong").second, (qulonglong)120000000000LL); // 120B
    date.setValue(definitionDetails.value("Date").second, QDate(1988, 1, 26));
    time.setValue(definitionDetails.value("Time").second, QTime(16,52,23,0));

    contactA.saveDetail(&name);
    contactA.saveDetail(&nick);
    contactA.saveDetail(&emailAddr);
    contactA.saveDetail(&number);
    if (validDetailInfo(definitionDetails.value("String")))
        contactA.saveDetail(&string);
    if (validDetailInfo(definitionDetails.value("Integer")))
        contactA.saveDetail(&integer);
    if (validDetailInfo(definitionDetails.value("DateTime")))
        contactA.saveDetail(&datetime);
    if (validDetailInfo(definitionDetails.value("Bool")))
        contactA.saveDetail(&boool);
    if (validDetailInfo(definitionDetails.value("ULongLong")))
        contactA.saveDetail(&ullong);
    if (validDetailInfo(definitionDetails.value("Date")))
        contactA.saveDetail(&date);
    if (validDetailInfo(definitionDetails.value("Time")))
        contactA.saveDetail(&time);

    name = QContactName();
    name.setFirstName("Bob");
    name.setLastName("Aaronsen");
    nick.setNickname("Sir Bob");
    number.setNumber("+55553456");
    string.setValue(definitionDetails.value("String").second, "Bob Aaronsen");
    integer.setValue(definitionDetails.value("Integer").second, 20);
    dubble.setValue(definitionDetails.value("Double").second, 4.0);
    boool.setValue(definitionDetails.value("Bool").second, false);
    ullong.setValue(definitionDetails.value("ULongLong").second, (qulonglong) 80000000000LL); // 80B
    uintt.setValue(definitionDetails.value("UInt").second, 4000000000u); // 4B
    date.setValue(definitionDetails.value("Date").second, QDate(2492, 5, 5));
    time.setValue(definitionDetails.value("Time").second, QTime(15,52,23,0));
    charr.setValue(definitionDetails.value("Char").second, QVariant(QChar('b')));

    contactB.saveDetail(&name);
    contactB.saveDetail(&nick);
    contactB.saveDetail(&number);
    if (validDetailInfo(definitionDetails.value("String")))
        contactB.saveDetail(&string);
    if (validDetailInfo(definitionDetails.value("Integer")))
        contactB.saveDetail(&integer);
    if (validDetailInfo(definitionDetails.value("Double")))
        contactB.saveDetail(&dubble);
    if (validDetailInfo(definitionDetails.value("Bool")))
        contactB.saveDetail(&boool);
    if (validDetailInfo(definitionDetails.value("ULongLong")))
        contactB.saveDetail(&ullong);
    if (validDetailInfo(definitionDetails.value("UInt")))
        contactB.saveDetail(&uintt);
    if (validDetailInfo(definitionDetails.value("Date")))
        contactB.saveDetail(&date);
    if (validDetailInfo(definitionDetails.value("Time")))
        contactB.saveDetail(&time);
    if (validDetailInfo(definitionDetails.value("Char")))
        contactB.saveDetail(&charr);

    name.setFirstName("Boris");
    name.setLastName("Aaronsun");
    string.setValue(definitionDetails.value("String").second, "Boris Aaronsun");
    integer.setValue(definitionDetails.value("Integer").second, -20);
    datetime.setValue(definitionDetails.value("DateTime").second, QDateTime(QDate(2009, 06, 29), QTime(16, 54, 17, 0)));
    llong.setValue(definitionDetails.value("LongLong").second, (qlonglong)8000000000LL); // 8B
    charr.setValue(definitionDetails.value("Char").second, QVariant(QChar('c')));

    contactC.saveDetail(&name);
    if (validDetailInfo(definitionDetails.value("String")))
        contactC.saveDetail(&string);
    if (validDetailInfo(definitionDetails.value("Integer")))
        contactC.saveDetail(&integer);
    if (validDetailInfo(definitionDetails.value("DateTime")))
        contactC.saveDetail(&datetime);
    if (validDetailInfo(definitionDetails.value("Double")))
        contactC.saveDetail(&dubble);
    if (validDetailInfo(definitionDetails.value("Bool")))
        contactC.saveDetail(&boool);
    if (validDetailInfo(definitionDetails.value("LongLong")))
        contactC.saveDetail(&llong);
    if (validDetailInfo(definitionDetails.value("ULongLong")))
        contactC.saveDetail(&ullong);
    if (validDetailInfo(definitionDetails.value("Char")))
        contactC.saveDetail(&charr);

    name.setFirstName("Dennis");
    name.setLastName("FitzMacintyre");
    string.setValue(definitionDetails.value("String").second, "Dennis FitzMacintyre");
    dubble.setValue(definitionDetails.value("Double").second, -128.0);
    llong.setValue(definitionDetails.value("LongLong").second, (qlonglong)-14000000000LL);
    uintt.setValue(definitionDetails.value("UInt").second, 3000000000u); // 3B
    date.setValue(definitionDetails.value("Date").second, QDate(2770, 10, 1));

    contactD.saveDetail(&name);
    if (validDetailInfo(definitionDetails.value("String")))
        contactD.saveDetail(&string);
    if (validDetailInfo(definitionDetails.value("Double")))
        contactD.saveDetail(&dubble);
    if (validDetailInfo(definitionDetails.value("LongLong")))
        contactD.saveDetail(&llong);
    if (validDetailInfo(definitionDetails.value("UInt")))
        contactD.saveDetail(&uintt);
    if (validDetailInfo(definitionDetails.value("Date")))
        contactD.saveDetail(&date);

    qDebug() << "Generating contacts with different timestamps, please wait..";
    int originalContactCount = cm->contactIds().count();
    bool successfulSave = cm->saveContact(&contactA);
    Q_ASSERT_VERIFY(successfulSave);
    QTest::qSleep(napTime);
    successfulSave = cm->saveContact(&contactB);
    Q_ASSERT_VERIFY(successfulSave);
    QTest::qSleep(napTime);
    successfulSave = cm->saveContact(&contactC);
    Q_ASSERT_VERIFY(successfulSave);
    QTest::qSleep(napTime);
    successfulSave = cm->saveContact(&contactD);
    Q_ASSERT_VERIFY(successfulSave);
    QTest::qSleep(napTime);

    /* Now add some contacts specifically for multisorting */
    QContact contactE,contactF,contactG;
    QContactName n;
    n.setFirstName("John");
    n.setLastName("Smithee");
    string.setValue(definitionDetails.value("String").second, "");
    if (validDetailInfo(definitionDetails.value("String")))
        contactE.saveDetail(&string);
    contactE.saveDetail(&n);
    n = QContactName();
    n.setFirstName("John");
    n.setLastName("Smithey");
    contactF.saveDetail(&n);
    n = QContactName();
    n.setFirstName("John");
    n.setLastName("Smithy");
    string.setValue(definitionDetails.value("String").second, "zzz");
    if (validDetailInfo(definitionDetails.value("String")))
        contactG.saveDetail(&string);
    contactG.saveDetail(&n);
    successfulSave = cm->saveContact(&contactE);
    Q_ASSERT_VERIFY(successfulSave);
    successfulSave = cm->saveContact(&contactF);
    Q_ASSERT_VERIFY(successfulSave);
    successfulSave = cm->saveContact(&contactG);
    Q_ASSERT_VERIFY(successfulSave);
    originalContactCount += 7;
    Q_FATAL_VERIFY(cm->contactIds().count() == originalContactCount);

    /* Now some for the locale aware sorting */
    QContact contactH, contactI, contactJ, contactK;
    QContactName n2;
    n2.setFirstName("xander");
#ifdef CUSTOM_LABEL_SUPPORTED
    n2.setCustomLabel("xander");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    n2.setValue(QContactName__FieldCustomLabel, "xander");
#endif
    contactH.saveDetail(&n2);
    n2.setFirstName("Xander");
#ifdef CUSTOM_LABEL_SUPPORTED
    n2.setCustomLabel("Xander");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    n2.setValue(QContactName__FieldCustomLabel, "Xander");
#endif
    contactI.saveDetail(&n2);
    n2.setFirstName("xAnder");
#ifdef CUSTOM_LABEL_SUPPORTED
    n2.setCustomLabel("xAnder");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    n2.setValue(QContactName__FieldCustomLabel, "xAnder");
#endif
    contactJ.saveDetail(&n2);
    n2.setFirstName("Yarrow");
#ifdef CUSTOM_LABEL_SUPPORTED
    n2.setCustomLabel("Yarrow");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    n2.setValue(QContactName__FieldCustomLabel, "Yarrow");
#endif
    contactK.saveDetail(&n2);

    // XXX add &aumlaut; or &acircum; etc to test those sort orders
#ifdef COMPATIBLE_CONTACT_SUPPORTED
    contactH = cm->compatibleContact(contactH);
    contactI = cm->compatibleContact(contactI);
    contactJ = cm->compatibleContact(contactJ);
    contactK = cm->compatibleContact(contactK);
#endif
    Q_ASSERT_VERIFY(cm->saveContact(&contactH));
    Q_ASSERT_VERIFY(cm->saveContact(&contactI));
    Q_ASSERT_VERIFY(cm->saveContact(&contactJ));
    Q_ASSERT_VERIFY(cm->saveContact(&contactK));

    /* Ensure the last modified times are different */
    QTest::qSleep(napTime);
    QContactName modifiedName = contactC.detail<QContactName>();
#ifdef CUSTOM_LABEL_SUPPORTED
    modifiedName.setCustomLabel("Clarence");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    modifiedName.setValue(QContactName__FieldCustomLabel, "Clarence");
#endif
    contactC.saveDetail(&modifiedName);
    cm->saveContact(&contactC);
    QTest::qSleep(napTime);
    modifiedName = contactB.detail<QContactName>();
#ifdef CUSTOM_LABEL_SUPPORTED
    modifiedName.setCustomLabel("Boris");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    modifiedName.setValue(QContactName__FieldCustomLabel, "Boris");
#endif
    contactB.saveDetail(&modifiedName);
    cm->saveContact(&contactB);
    QTest::qSleep(napTime);
    modifiedName = contactA.detail<QContactName>();
#ifdef CUSTOM_LABEL_SUPPORTED
    modifiedName.setCustomLabel("Albert");
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    modifiedName.setValue(QContactName__FieldCustomLabel, "Albert");
#endif
    contactA.saveDetail(&modifiedName);
    cm->saveContact(&contactA);
    QTest::qSleep(napTime);

    /* Now some for convenience filtering */
    QSet<QContactDetail::DetailType> allDefs;
    allDefs.insert(detailType<QContactAddress>());
    allDefs.insert(detailType<QContactEmailAddress>());
    allDefs.insert(detailType<QContactFavorite>());
    allDefs.insert(detailType<QContactName>());
    allDefs.insert(detailType<QContactPhoneNumber>());
    allDefs.insert(detailType<QContactTag>());

    // Contact L ----------------------------------------
    QContact contactL;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress ladr;
        ladr.setStreet("streetstring road"); // Contact L matches streetstring.
        ladr.setLocality("testplace");
        ladr.setRegion("somewhere");
        contactL.saveDetail(&ladr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress led;
        led.setEmailAddress("frad@test.domain");
        contactL.saveDetail(&led);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber lp;
        lp.setNumber("11111");
        contactL.saveDetail(&lp);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName ln;
        ln.setFirstName("Fradarick");
        ln.setLastName("Gumboots");
        contactL.saveDetail(&ln);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag lt;
        lt.setTag("Soccer");
        contactL.saveDetail(&lt);
    }
    // Contact M ----------------------------------------
    QContact contactM;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress madr;
        madr.setStreet("some road");
        madr.setLocality("testplace");
        madr.setRegion("somewhere");
        contactM.saveDetail(&madr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress med;
        med.setEmailAddress("frbd@test.com"); // Contact M matches @test.com
        contactM.saveDetail(&med);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber mp;
        mp.setNumber("22222");
        contactM.saveDetail(&mp);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName mn;
        mn.setFirstName("Frbdbrick");
        mn.setLastName("Gumboots");
        contactM.saveDetail(&mn);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag mt;
        mt.setTag("Soccer");
        contactM.saveDetail(&mt);
    }
    // Contact N ----------------------------------------
    QContact contactN;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress nadr;
        nadr.setStreet("some road");
        nadr.setLocality("testplace");
        nadr.setRegion("somewhere");
        contactN.saveDetail(&nadr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress ned;
        ned.setEmailAddress("frcd@test.domain");
        contactN.saveDetail(&ned);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber np;
        np.setNumber("12345"); // Contact N matches 12345
        contactN.saveDetail(&np);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName nn;
        nn.setFirstName("Frcdcrick");
        nn.setLastName("Gumboots");
        contactN.saveDetail(&nn);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag nt;
        nt.setTag("Soccer");
        contactN.saveDetail(&nt);
    }
    // Contact O ----------------------------------------
    QContact contactO;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress oadr;
        oadr.setStreet("some road");
        oadr.setLocality("testplace");
        oadr.setRegion("somewhere");
        contactO.saveDetail(&oadr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress oed;
        oed.setEmailAddress("frdd@test.domain");
        contactO.saveDetail(&oed);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber op;
        op.setNumber("44444");
        contactO.saveDetail(&op);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName on;
        on.setFirstName("Freddy"); // Contact O matches Freddy
        on.setLastName("Gumboots");
        contactO.saveDetail(&on);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag ot;
        ot.setTag("Soccer");
        contactO.saveDetail(&ot);
    }
    // Contact P ----------------------------------------
    QContact contactP;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress padr;
        padr.setStreet("some road");
        padr.setLocality("testplace");
        padr.setRegion("somewhere");
        contactP.saveDetail(&padr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress ped;
        ped.setEmailAddress("fred@test.domain");
        contactP.saveDetail(&ped);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber pp;
        pp.setNumber("55555");
        contactP.saveDetail(&pp);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName pn;
        pn.setFirstName("Frederick"); // Contact P matches Frederic (contains).
        pn.setLastName("Gumboots");
        contactP.saveDetail(&pn);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag pt;
        pt.setTag("Soccer");
        contactP.saveDetail(&pt);
    }
    // Contact Q ----------------------------------------
    QContact contactQ;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress qadr;
        qadr.setStreet("some road");
        qadr.setLocality("testplace");
        qadr.setRegion("somewhere");
        contactQ.saveDetail(&qadr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress qed;
        qed.setEmailAddress("frfd@test.domain");
        contactQ.saveDetail(&qed);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber qp;
        qp.setNumber("66666");
        contactQ.saveDetail(&qp);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName qn;
        qn.setFirstName("Frfdfrick");
        qn.setLastName("Gumboots");
        contactQ.saveDetail(&qn);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag qt;
        qt.setTag("Soccer");
        contactQ.saveDetail(&qt);
    }
    if (allDefs.contains(detailType<QContactFavorite>())) {
        QContactFavorite qf;
        qf.setFavorite(true); // Contact Q matches favorite = true
        contactQ.saveDetail(&qf);
    }
    // Contact R ----------------------------------------
    QContact contactR;
    if (allDefs.contains(detailType<QContactAddress>())) {
        QContactAddress radr;
        radr.setStreet("some road");
        radr.setLocality("testplace");
        radr.setRegion("somewhere");
        contactR.saveDetail(&radr);
    }
    if (allDefs.contains(detailType<QContactEmailAddress>())) {
        QContactEmailAddress red;
        red.setEmailAddress("frgd@test.domain");
        contactR.saveDetail(&red);
    }
    if (allDefs.contains(detailType<QContactPhoneNumber>())) {
        QContactPhoneNumber rp;
        rp.setNumber("77777");
        contactR.saveDetail(&rp);
    }
    if (allDefs.contains(detailType<QContactName>())) {
        QContactName rn;
        rn.setFirstName("Frgdgrick");
        rn.setLastName("Gumboots");
        contactR.saveDetail(&rn);
    }
    if (allDefs.contains(detailType<QContactTag>())) {
        QContactTag rt;
        rt.setTag("Football"); // Contact R matches Football
        contactR.saveDetail(&rt);
    }
#ifdef COMPATIBLE_CONTACT_SUPPORTED
    // --------------------- save.
    contactL = cm->compatibleContact(contactL);
    contactM = cm->compatibleContact(contactM);
    contactN = cm->compatibleContact(contactN);
    contactO = cm->compatibleContact(contactO);
    contactP = cm->compatibleContact(contactP);
    contactQ = cm->compatibleContact(contactQ);
    contactR = cm->compatibleContact(contactR);
#endif
    Q_ASSERT_VERIFY(cm->saveContact(&contactL));
    Q_ASSERT_VERIFY(cm->saveContact(&contactM));
    Q_ASSERT_VERIFY(cm->saveContact(&contactN));
    Q_ASSERT_VERIFY(cm->saveContact(&contactO));
    Q_ASSERT_VERIFY(cm->saveContact(&contactP));
    Q_ASSERT_VERIFY(cm->saveContact(&contactQ));
    Q_ASSERT_VERIFY(cm->saveContact(&contactR));
    // --------------------- end.

    /* Add our newly saved contacts to our internal list of added contacts */
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactR));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactQ));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactP));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactO));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactN));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactM));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactL));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactK));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactJ));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactI));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactH));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactG));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactF));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactE));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactD));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactC));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactB));
    contactsAddedToManagers.insert(cm, ContactId::apiId(contactA));

    /* Reload the contacts to pick up any changes */
    contactA = cm->contact(retrievalId(contactA));
    contactB = cm->contact(retrievalId(contactB));
    contactC = cm->contact(retrievalId(contactC));
    contactD = cm->contact(retrievalId(contactD));
    contactE = cm->contact(retrievalId(contactE));
    contactF = cm->contact(retrievalId(contactF));
    contactG = cm->contact(retrievalId(contactG));
    contactH = cm->contact(retrievalId(contactH));
    contactI = cm->contact(retrievalId(contactI));
    contactJ = cm->contact(retrievalId(contactJ));
    contactK = cm->contact(retrievalId(contactK));
    contactL = cm->contact(retrievalId(contactL));
    contactM = cm->contact(retrievalId(contactM));
    contactN = cm->contact(retrievalId(contactN));
    contactO = cm->contact(retrievalId(contactO));
    contactP = cm->contact(retrievalId(contactP));
    contactQ = cm->contact(retrievalId(contactQ));
    contactR = cm->contact(retrievalId(contactR));

    QList<QContactId> list;
    if (!contactA.isEmpty())
        list << ContactId::apiId(contactA);
    if (!contactB.isEmpty())
        list << ContactId::apiId(contactB);
    if (!contactC.isEmpty())
        list << ContactId::apiId(contactC);
    if (!contactD.isEmpty())
        list << ContactId::apiId(contactD);
    if (!contactE.isEmpty())
        list << ContactId::apiId(contactE);
    if (!contactF.isEmpty())
        list << ContactId::apiId(contactF);
    if (!contactG.isEmpty())
        list << ContactId::apiId(contactG);
    if (!contactH.isEmpty())
        list << ContactId::apiId(contactH);
    if (!contactI.isEmpty())
        list << ContactId::apiId(contactI);
    if (!contactJ.isEmpty())
        list << ContactId::apiId(contactJ);
    if (!contactK.isEmpty())
        list << ContactId::apiId(contactK);
    if (!contactL.isEmpty())
        list << ContactId::apiId(contactL);
    if (!contactM.isEmpty())
        list << ContactId::apiId(contactM);
    if (!contactN.isEmpty())
        list << ContactId::apiId(contactN);
    if (!contactO.isEmpty())
        list << ContactId::apiId(contactO);
    if (!contactP.isEmpty())
        list << ContactId::apiId(contactP);
    if (!contactQ.isEmpty())
        list << ContactId::apiId(contactQ);
    if (!contactR.isEmpty())
        list << ContactId::apiId(contactR);

    return list;
}

/* ============ Utility functions ============= */

void tst_QContactManagerFiltering::dumpContactDifferences(const QContact& ca, const QContact& cb)
{
    // Try to narrow down the differences
    QContact a(ca);
    QContact b(cb);

    QContactName n1 = a.detail<QContactName>();
    QContactName n2 = b.detail<QContactName>();

    // Check the name components in more detail
    QCOMPARE(n1.firstName(), n2.firstName());
    QCOMPARE(n1.middleName(), n2.middleName());
    QCOMPARE(n1.lastName(), n2.lastName());
    QCOMPARE(n1.prefix(), n2.prefix());
    QCOMPARE(n1.suffix(), n2.suffix());
#ifdef CUSTOM_LABEL_SUPPORTED
    QCOMPARE(n1.customLabel(), n2.customLabel());
#elif defined (CUSTOM_LABEL_STORAGE_SUPPORTED)
    QCOMPARE(n1.value<QString>(QContactName__FieldCustomLabel), n2.value<QString>(QContactName__FieldCustomLabel));
#endif

#ifdef DISPLAY_LABEL_SUPPORTED
    // Check the display label
    QCOMPARE(a.displayLabel(), b.displayLabel());
#endif

    // Now look at the rest
    QList<QContactDetail> aDetails = a.details();
    QList<QContactDetail> bDetails = b.details();

    // They can be in any order, so loop
    // First remove any matches
    foreach(QContactDetail d, aDetails) {
        foreach(QContactDetail d2, bDetails) {
            if(d == d2) {
                a.removeDetail(&d);
                b.removeDetail(&d2);
                break;
            }
        }
    }

    // Now dump the extra details that were unmatched in A
    aDetails = a.details();
    bDetails = b.details();
    foreach (const QContactDetail &d, aDetails) {
        if (detailType(d) != detailType<QContactDisplayLabel>())
            qDebug() << "A contact had extra detail:" << detailTypeName(d) << detailValues(d);
    }
    // and same for B
    foreach (const QContactDetail &d, bDetails) {
        if (detailType(d) != detailType<QContactDisplayLabel>())
            qDebug() << "B contact had extra detail:" << detailTypeName(d) << detailValues(d);
    }

    QCOMPARE(b, a);
}

bool tst_QContactManagerFiltering::isSuperset(const QContact& ca, const QContact& cb)
{
    // returns true if contact ca is a superset of contact cb
    // we use this test instead of equality because dynamic information
    // such as presence/location, and synthesised information such as
    // display label and (possibly) type, may differ between a contact
    // in memory and the contact in the managed store.

    QContact a(ca);
    QContact b(cb);
    QList<QContactDetail> aDetails = a.details();
    QList<QContactDetail> bDetails = b.details();

    // They can be in any order, so loop
    // First remove any matches
    foreach(QContactDetail d, aDetails) {
        foreach(QContactDetail d2, bDetails) {
            if(d == d2) {
                a.removeDetail(&d);
                b.removeDetail(&d2);
                break;
            }
        }
    }

    // check for contact type updates
    if (validContactType(a))
        if (validContactType(b))
            if (a.type() != b.type())
                return false; // nonempty type is different.

    // Now check to see if b has any details remaining; if so, a is not a superset.
    // Note that the DisplayLabel and Type can never be removed.
    if (b.details().size() > 2
            || (b.details().size() == 2 && (detailType(b.details().value(0)) != detailType<QContactDisplayLabel>()
                                            || detailType(b.details().value(1)) != detailType<QContactType>())))
        return false;
    return true;
}

void tst_QContactManagerFiltering::dumpContact(const QContact& contact)
{
    QContactManager m;
    qDebug() << "Contact: " << ContactId::toString(contact);
    QList<QContactDetail> details = contact.details();
    foreach (const QContactDetail &d, details) {
        qDebug() << "  " << detailTypeName(d) << ":";
        qDebug() << "    Vals:" << detailValues(d);
    }
}

void tst_QContactManagerFiltering::dumpContacts()
{
    QContactManager m;
    QList<QContactId> ids = m.contactIds();

    foreach (const QContactId &id, ids) {
        QContact c = m.contact(id);
        dumpContact(c);
    }
}

QTEST_MAIN(tst_QContactManagerFiltering)
#include "tst_qcontactmanagerfiltering.moc"
#include "qcontactmanager.h"
