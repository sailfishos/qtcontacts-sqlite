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

#if defined(USE_VERSIT_PLZ)
// This makes it easier to create specific QContacts
#include "qversitcontactimporter.h"
#include "qversitdocument.h"
#include "qversitreader.h"
#endif

#include "../../util.h"
#include "../../qcontactmanagerdataholder.h"

#define SQLITE_MANAGER "org.nemomobile.contacts.sqlite"

//TESTED_COMPONENT=src/contacts
//TESTED_CLASS=
//TESTED_FILES=

// to get QFETCH to work with the template expression...
typedef QMap<QString,QString> tst_QContactManager_QStringMap;
Q_DECLARE_METATYPE(tst_QContactManager_QStringMap)

/* A class that no backend can support */
class UnsupportedMetatype {
    int foo;
};
Q_DECLARE_METATYPE(UnsupportedMetatype)
Q_DECLARE_METATYPE(QContact)
Q_DECLARE_METATYPE(QContactManager::Error)
Q_DECLARE_METATYPE(Qt::CaseSensitivity)

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

static bool detailsEquivalent(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // Same as operator== except ignores differences in accessConstraints values
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesEqual(lhs, rhs);
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

static bool detailsSuperset(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // True is lhs is a superset of rhs
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesSuperset(lhs, rhs);
}

class tst_QContactManager : public QObject
{
Q_OBJECT

public:
    tst_QContactManager();
    virtual ~tst_QContactManager();

private:
    void dumpContactDifferences(const QContact& a, const QContact& b);
    void dumpContact(const QContact &c, QContactManager *cm);
    void dumpContacts(QContactManager *cm);
    bool isSuperset(const QContact& ca, const QContact& cb);
    QList<QContactDetail> removeAllDefaultDetails(const QList<QContactDetail>& details);
    QContactManager *newContactManager(const QMap<QString, QString> &params = QMap<QString, QString>());
    void addManagers(); // add standard managers to the data
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact createContact(QString firstName, QString lastName, QString phoneNumber);
    void saveContactName(QContact *contact, QContactName *contactName, const QString &name) const;
#else
    QContact createContact(QContactDetailDefinition nameDef, QString firstName, QString lastName, QString phoneNumber);
    void saveContactName(QContact *contact, QContactDetailDefinition nameDef, QContactName *contactName, const QString &name) const;
    void validateDefinitions(const QMap<QString, QContactDetailDefinition>& defs) const;
#endif

    QScopedPointer<QContactManagerDataHolder> managerDataHolder;

public slots:
    void initTestCase();
    void cleanupTestCase();
private slots:

    void doDump();
    void doDump_data() {addManagers();}

#ifdef MUTABLE_SCHEMA_SUPPORTED
    void doDumpSchema();
    void doDumpSchema_data() {addManagers();}
#endif

    /* Special test with special data */
    void uriParsing();
#ifdef COMPATIBLE_CONTACT_SUPPORTED
    void compatibleContact();
#endif

    /* Backend-specific tests */
    /* Presence reporting specific to qtcontacts-sqlite */
    void presenceReporting();
    void presenceReporting_data();

    /* Transient presence accumulation */
    void presenceAccumulation();
    void presenceAccumulation_data() {addManagers();}

    /* Nonprivileged DB variant */
    void nonprivileged();

    /* Tests that are run on all managers */
    void metadata();
    void nullIdOperations();
    void add();
    void update();
    void remove();
    void removeAsync();
    void batch();
    void observerDeletion();
    void signalEmission();
#ifdef DETAIL_DEFINITION_SUPPORTED
    void detailDefinitions();
#endif
#ifdef DISPLAY_LABEL_SUPPORTED
    void displayName();
#endif
    void actionPreferences();
    void selfContactId();
    void detailOrders();
    void relationships();
    void contactType();
    void familyDetail();
    void geoLocationDetail();
    void lateDeletion();
    void compareVariant();
    void constituentOfSelf();
    void searchSensitivity();

#if defined(USE_VERSIT_PLZ)
    void partialSave();
    void partialSave_data() {addManagers();}
#endif

    void extendedDetail();
    void extendedDetail_data() {addManagers();}

    void onlineAccountFields();
    void onlineAccountFields_data() {addManagers();}

    /* Tests that take no data */
#ifdef MUTABLE_SCHEMA_SUPPORTED
    void contactValidation();
#endif
    void errorStayingPut();
    void invalidManager();
    void changeSet();
    void fetchHint();
#ifdef MUTABLE_SCHEMA_SUPPORTED
    void engineDefaultSchema();
#endif

    /* Special test with special data */
    void uriParsing_data();
    void nameSynthesis_data();
#ifdef COMPATIBLE_CONTACT_SUPPORTED
    void compatibleContact_data();
#endif
    void compareVariant_data();

    /* Tests that are run on all managers */
    void metadata_data() {addManagers();}
    void nullIdOperations_data() {addManagers();}
    void add_data() {addManagers();}
    void update_data() {addManagers();}
    void remove_data() {addManagers();}
    void removeAsync_data() {addManagers();}
    void batch_data() {addManagers();}
    void signalEmission_data() {addManagers();}
    void detailDefinitions_data() {addManagers();}
    void displayName_data() {addManagers();}
    void actionPreferences_data() {addManagers();}
    void selfContactId_data() {addManagers();}
    void detailOrders_data() {addManagers();}
    void relationships_data() {addManagers();}
    void contactType_data() {addManagers();}
    void familyDetail_data() {addManagers();}
    void geoLocationDetail_data() {addManagers();}
    void lateDeletion_data() {addManagers();}
};

// Helper class that connects to a signal on ctor, and disconnects on dtor
class QTestSignalSink : public QObject {
    Q_OBJECT
public:
    // signal and object must remain valid for the lifetime
    QTestSignalSink(QObject *object, const char *signal)
        : mObject(object), mSignal(signal)
    {
        connect(object, signal, this, SLOT(ignored()));
    }

    ~QTestSignalSink()
    {
        disconnect(mObject, mSignal, this, SLOT(ignored()));
    }

public slots:
    void ignored() {}

private:
    QObject *mObject;
    const char * const mSignal;
};


static bool managerSupportsFeature(const QContactManager &m, const char *feature)
{
    Q_UNUSED(m)

    // No feature tests in qtpim
    if (feature == QString::fromLatin1("Relationships")) {
        return true;
    } else if (feature == QString::fromLatin1("ArbitraryRelationshipTypes")) {
        return true;
    }

    return false;
}

tst_QContactManager::tst_QContactManager()
{
}

tst_QContactManager::~tst_QContactManager()
{
}

void tst_QContactManager::initTestCase()
{
    registerIdType();

    managerDataHolder.reset(new QContactManagerDataHolder(false));

    /* Make sure these other test plugins are NOT loaded by default */
    // These are now removed from the list of managers in addManagers()
    //QVERIFY(!QContactManager::availableManagers().contains("testdummy"));
    //QVERIFY(!QContactManager::availableManagers().contains("teststaticdummy"));
    //QVERIFY(!QContactManager::availableManagers().contains("maliciousplugin"));
}

void tst_QContactManager::cleanupTestCase()
{
    managerDataHolder.reset(0);
}

void tst_QContactManager::dumpContactDifferences(const QContact& ca, const QContact& cb)
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
#elif defined(CUSTOM_LABEL_STORAGE_SUPPORTED)
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

    // Now dump the extra details that were unmatched in A (note that DisplayLabel and Type are always present).
    // We ignore timestamp since it can get autogenerated too
    aDetails = a.details();
    bDetails = b.details();
    foreach (const QContactDetail &d, aDetails) {
        if (detailType(d) != detailType<QContactDisplayLabel>() &&
            detailType(d) != detailType<QContactType>() &&
            detailType(d) != detailType<QContactTimestamp>())
            qDebug() << "A contact had extra detail:" << detailTypeName(d) << detailValues(d);
    }
    // and same for B
    foreach (const QContactDetail &d, bDetails) {
        if (detailType(d) != detailType<QContactDisplayLabel>() &&
            detailType(d) != detailType<QContactType>() &&
            detailType(d) != detailType<QContactTimestamp>())
            qDebug() << "B contact had extra detail:" << detailTypeName(d) << detailValues(d);
    }

#ifdef DISPLAY_LABEL_SUPPORTED
    // now test specifically the display label and the type
    if (a.displayLabel() != b.displayLabel()) {
        qDebug() << "A contact display label =" << a.displayLabel();
        qDebug() << "B contact display label =" << b.displayLabel();
    }
#endif
    if (a.type() != b.type()) {
        qDebug() << "A contact type =" << a.type();
        qDebug() << "B contact type =" << b.type();
    }
}

bool tst_QContactManager::isSuperset(const QContact& ca, const QContact& cb)
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
            if (detailsEquivalent(d, d2)) {
                a.removeDetail(&d);
                b.removeDetail(&d2);
                break;
            }
        }
    }

    // Second remove any superset matches (eg, backend adds a field)
    aDetails = a.details();
    bDetails = b.details();
    foreach (QContactDetail d, aDetails) {
        foreach (QContactDetail d2, bDetails) {
            if (detailType(d) == detailType(d2)) {
                bool canRemove = true;
                DetailMap d2map = detailValues(d2, false);
                foreach (DetailMap::key_type key, d2map.keys()) {
                    if (d.value(key) != d2.value(key)) {
                        // d can have _more_ keys than d2,
                        // but not _less_; and it cannot
                        // change the value.
                        canRemove = false;
                    }
                }

                if (canRemove) {
                    // if we get to here, we can remove the details.
                    a.removeDetail(&d);
                    b.removeDetail(&d2);
                    break;
                }
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

void tst_QContactManager::dumpContact(const QContact& contact, QContactManager *cm)
{
#ifndef DISPLAY_LABEL_SUPPORTED
    Q_UNUSED(cm)
    qDebug() << "Contact: " << ContactId::toString(contact);
#else
    qDebug() << "Contact: " << ContactId::toString(contact) << "(" << cm->synthesizedContactDisplayLabel(contact) << ")";
#endif
    foreach (const QContactDetail &d, contact.details()) {
        qDebug() << "  " << detailType(d) << ":";
        qDebug() << "    Vals:" << detailValues(d);
    }
}

void tst_QContactManager::dumpContacts(QContactManager *cm)
{
    QList<QContactId> ids = cm->contactIds();

    qDebug() << "There are" << ids.count() << "contacts in" << cm->managerUri();

    foreach (const QContactId &id, ids) {
        QContact c = cm->contact(id);
        dumpContact(c, cm);
    }
}

void tst_QContactManager::uriParsing_data()
{
    QTest::addColumn<QString>("uri");
    QTest::addColumn<bool>("good"); // is this a good uri or not
    QTest::addColumn<QString>("manager");
    QTest::addColumn<QMap<QString, QString> >("parameters");

    QMap<QString, QString> inparameters;
    inparameters.insert("foo", "bar");
    inparameters.insert("bazflag", QString());
    inparameters.insert("bar", "glob");

    QMap<QString, QString> inparameters2;
    inparameters2.insert("this has spaces", QString());
    inparameters2.insert("and& an", " &amp;");
    inparameters2.insert("and an ", "=quals");

    QTest::newRow("built") << QContactManager::buildUri("manager", inparameters) << true << "manager" << inparameters;
    QTest::newRow("built with escaped parameters") << QContactManager::buildUri("manager", inparameters2) << true << "manager" << inparameters2;
    QTest::newRow("no scheme") << "this should not split" << false << QString() << tst_QContactManager_QStringMap();
    QTest::newRow("wrong scheme") << "invalidscheme:foo bar" << false << QString() << tst_QContactManager_QStringMap();
    QTest::newRow("right scheme, no colon") << "qtcontacts" << false << QString() << tst_QContactManager_QStringMap();
    QTest::newRow("no manager, colon, no params") << "qtcontacts::" << false  << "manager" << tst_QContactManager_QStringMap();
    QTest::newRow("yes manager, no colon, no params") << "qtcontacts:manager" << true << "manager" << tst_QContactManager_QStringMap();
    QTest::newRow("yes manager, yes colon, no params") << "qtcontacts:manager:" << true << "manager"<< tst_QContactManager_QStringMap();
    QTest::newRow("yes params") << "qtcontacts:manager:foo=bar&bazflag=&bar=glob" << true << "manager" << inparameters;
    QTest::newRow("yes params but misformed") << "qtcontacts:manager:foo=bar&=gloo&bar=glob" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 2") << "qtcontacts:manager:=&=gloo&bar=glob" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 3") << "qtcontacts:manager:==" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 4") << "qtcontacts:manager:&&" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 5") << "qtcontacts:manager:&goo=bar" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 6") << "qtcontacts:manager:goo&bar" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 7") << "qtcontacts:manager:goo&bar&gob" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 8") << "qtcontacts:manager:==&&==&goo=bar" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 9") << "qtcontacts:manager:foo=bar=baz" << false << "manager" << inparameters;
    QTest::newRow("yes params but misformed 10") << "qtcontacts:manager:foo=bar=baz=glob" << false << "manager" << inparameters;
    QTest::newRow("no manager but yes params") << "qtcontacts::foo=bar&bazflag=&bar=glob" << false << QString() << inparameters;
    QTest::newRow("no manager or params") << "qtcontacts::" << false << QString() << inparameters;
    QTest::newRow("no manager or params or colon") << "qtcontacts:" << false << QString() << inparameters;
}

QContactManager *tst_QContactManager::newContactManager(const QMap<QString, QString> &params)
{
    QMap<QString, QString> parameters;
    parameters.insert("autoTest", "true");
    parameters.insert("mergePresenceChanges", "false");

    QMap<QString, QString>::const_iterator it = params.constBegin(), end = params.constEnd();
    for ( ; it != end; ++it) {
        parameters.insert(it.key(), it.value());
    }

    return new QContactManager(SQLITE_MANAGER, parameters);
}

void tst_QContactManager::addManagers()
{
    QTest::addColumn<QString>("uri");

    // Only test the qtcontacts-sqlite engine
    QMap<QString, QString> params;
    params.insert("autoTest", "true");
    params.insert("mergePresenceChanges", "false");
    QTest::newRow("mgr='" SQLITE_MANAGER "'") << QContactManager::buildUri(SQLITE_MANAGER, params);
}

/*
 * Helper method for creating a QContact instance with name and phone number
 * details. Name is generated according to the detail definition assuming that
 * either first and last name or custom label is supported.
 */
QContact tst_QContactManager::createContact(
#ifdef DETAIL_DEFINITION_SUPPORTED
    QContactDetailDefinition nameDef,
#endif
    QString firstName,
    QString lastName,
    QString phoneNumber)
{
    QContact contact;

    if(!firstName.isEmpty() || !lastName.isEmpty()) {
        QContactName n;

#ifndef DETAIL_DEFINITION_SUPPORTED
        n.setFirstName(firstName);
        n.setLastName(lastName);
#else
        if(nameDef.fields().contains(QContactName::FieldFirstName)
            && nameDef.fields().contains(QContactName::FieldFirstName)) {
            n.setFirstName(firstName);
            n.setLastName(lastName);
        } else if(nameDef.fields().contains(QContactName::FieldCustomLabel)) {
            n.setCustomLabel(firstName + " " + lastName);
        } else {
            // assume that either first and last name or custom label is supported
            QTest::qWarn("Neither custom label nor first name/last name supported!");
            return QContact();
        }
#endif
        contact.saveDetail(&n);
    }

    if (!phoneNumber.isEmpty()) {
        QContactPhoneNumber ph;
        ph.setNumber(phoneNumber);
        contact.saveDetail(&ph);
    }

    return contact;
}

#ifndef DETAIL_DEFINITION_SUPPORTED
void tst_QContactManager::saveContactName(QContact *contact, QContactName *contactName, const QString &name) const
#else
void tst_QContactManager::saveContactName(QContact *contact, QContactDetailDefinition nameDef, QContactName *contactName, const QString &name) const
#endif
{
#ifndef DETAIL_DEFINITION_SUPPORTED
#ifdef CUSTOM_LABEL_SUPPORTED
    contactName->setCustomLabel(name);
#elif defined(CUSTOM_LABEL_STORAGE_SUPPORTED)
    contactName->setValue(QContactName__FieldCustomLabel, name);
#else
    contactName->setFirstName(name);
#endif
#else
    // check which name fields are supported in the following order:
    // 1. custom label, 2. first name, 3. last name
    if(nameDef.fields().contains(QContactName::FieldCustomLabel)) {
        contactName->setCustomLabel(name);
    } else if(nameDef.fields().contains(QContactName::FieldFirstName)) {
        contactName->setFirstName(name);
    } else if(nameDef.fields().contains(QContactName::FieldLastName)) {
        contactName->setLastName(name);
    } else {
        // Assume that at least one of the above name fields is supported by the backend
        QVERIFY(false);
    }
#endif
    contact->saveDetail(contactName);
}

void tst_QContactManager::metadata()
{
    // ensure that the backend is publishing its metadata (name / parameters / uri) correctly
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(newContactManager());
    QVERIFY(QContactManager::buildUri(cm->managerName(), cm->managerParameters()) == cm->managerUri());
}


void tst_QContactManager::nullIdOperations()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(newContactManager());
    QVERIFY(!cm->removeContact(QContactId()));
    QVERIFY(cm->error() == QContactManager::DoesNotExistError);


    QContact c = cm->contact(QContactId());
    QVERIFY(c.id() == QContactId());
    QVERIFY(c.isEmpty());
    QVERIFY(cm->error() == QContactManager::DoesNotExistError);
}

void tst_QContactManager::uriParsing()
{
    QFETCH(QString, uri);
    QFETCH(bool, good);
    QFETCH(QString, manager);
    QFETCH(tst_QContactManager_QStringMap, parameters);

    QString outmanager;
    QMap<QString, QString> outparameters;

    if (good) {
        /* Good split */
        /* Test splitting */
        QVERIFY(QContactManager::parseUri(uri, 0, 0)); // no out parms

        // 1 out param
        QVERIFY(QContactManager::parseUri(uri, &outmanager, 0));
        QCOMPARE(manager, outmanager);
        QVERIFY(QContactManager::parseUri(uri, 0, &outparameters));

        QCONTACTMANAGER_REMOVE_VERSIONS_FROM_URI(outparameters);

        QCOMPARE(parameters, outparameters);

        outmanager.clear();
        outparameters.clear();
        QVERIFY(QContactManager::parseUri(uri, &outmanager, &outparameters));

        QCONTACTMANAGER_REMOVE_VERSIONS_FROM_URI(outparameters);

        QCOMPARE(manager, outmanager);
        QCOMPARE(parameters, outparameters);
    } else {
        /* bad splitting */
        outmanager.clear();
        outparameters.clear();
        QVERIFY(QContactManager::parseUri(uri, 0, 0) == false);
        QVERIFY(QContactManager::parseUri(uri, &outmanager, 0) == false);
        QVERIFY(outmanager.isEmpty());
        QVERIFY(QContactManager::parseUri(uri, 0, &outparameters) == false);
        QCONTACTMANAGER_REMOVE_VERSIONS_FROM_URI(outparameters);
        QVERIFY(outparameters.isEmpty());

        /* make sure the in parameters don't change with a bad split */
        outmanager = manager;
        outparameters = parameters;
        QVERIFY(QContactManager::parseUri(uri, &outmanager, 0) == false);
        QCOMPARE(manager, outmanager);
        QVERIFY(QContactManager::parseUri(uri, 0, &outparameters) == false);
        QCONTACTMANAGER_REMOVE_VERSIONS_FROM_URI(outparameters);
        QCOMPARE(parameters, outparameters);
    }
}

void tst_QContactManager::doDump()
{
    // Only do this if it has been explicitly selected
    if (QCoreApplication::arguments().contains("doDump")) {
        QFETCH(QString, uri);
        QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

        dumpContacts(cm.data());
    }
}

Q_DECLARE_METATYPE(QVariant)

#ifdef MUTABLE_SCHEMA_SUPPORTED
void tst_QContactManager::doDumpSchema()
{
    // Only do this if it has been explicitly selected
    if (QCoreApplication::arguments().contains("doDumpSchema")) {
        QFETCH(QString, uri);
        QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

        // Get the schema for each supported type
        foreach(QString type, cm->supportedContactTypes()) {
            QMap<QString, QContactDetailDefinition> defs = cm->detailDefinitions(type);

            foreach(QContactDetailDefinition def, defs.values()) {
                if (def.isUnique())
                    qDebug() << QString("%2::%1 (Unique) {").arg(def.name()).arg(type).toAscii().constData();
                else
                    qDebug() << QString("%2::%1 {").arg(def.name()).arg(type).toAscii().constData();
                QMap<QString, QContactDetailFieldDefinition> fields = def.fields();

                foreach(QString fname, fields.keys()) {
                    QContactDetailFieldDefinition field = fields.value(fname);

                    if (field.allowableValues().count() > 0) {
                        // Make some pretty output
                        QStringList allowedList;
                        foreach(QVariant var, field.allowableValues()) {
                            QString allowed;
                            if (var.type() == QVariant::String)
                                allowed = QString("'%1'").arg(var.toString());
                            else if (var.type() == QVariant::StringList)
                                allowed = QString("'%1'").arg(var.toStringList().join(","));
                            else {
                                // use the textstream <<
                                QDebug dbg(&allowed);
                                dbg << var;
                            }
                            allowedList.append(allowed);
                        }

                        qDebug() << QString("   %2 %1 {%3}").arg(fname).arg(QMetaType::typeName(field.dataType())).arg(allowedList.join(",")).toAscii().constData();
                    } else
                        qDebug() << QString("   %2 %1").arg(fname).arg(QMetaType::typeName(field.dataType())).toAscii().constData();
                }

                qDebug() << "}";
            }
        }
    }
}
#endif

void tst_QContactManager::add()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact alice = createContact("Alice", "inWonderland", "1234567");
#else
    QContactDetailDefinition nameDef = cm->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact);
    QContact alice = createContact(nameDef, "Alice", "inWonderland", "1234567");
#endif
    int currCount = cm->contactIds().count();
    QVERIFY(cm->saveContact(&alice));
    QVERIFY(cm->error() == QContactManager::NoError);

    QVERIFY(!alice.id().managerUri().isEmpty());
    QVERIFY(ContactId::isValid(alice.id()));
    QCOMPARE(cm->contactIds().count(), currCount+1);

    // Test that the ID is roundtripped via string correctly
    QCOMPARE(QContactId::fromString(alice.id().toString()), alice.id());

    QContact added = cm->contact(retrievalId(alice));
    QVERIFY(added.id() == alice.id());

    if (!isSuperset(added, alice)) {
        dumpContacts(cm.data());
        dumpContactDifferences(added, alice);
        QCOMPARE(added, alice);
    }

    // Verify that the computed properties are correct
    QContactStatusFlags flags = added.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasPhoneNumber), true);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasEmailAddress), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasOnlineAccount), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsDeactivated), false);

    // now try adding a contact that does not exist in the database with non-zero id
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact nonexistent = createContact("nonexistent", "contact", "");
#else
    QContact nonexistent = createContact(nameDef, "nonexistent", "contact", "");
#endif
    QVERIFY(cm->saveContact(&nonexistent));       // should work
    QVERIFY(cm->removeContact(removalId(nonexistent))); // now nonexistent has an id which does not exist
    QVERIFY(!cm->saveContact(&nonexistent));      // hence, should fail
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);
    nonexistent.setId(QContactId());
    QVERIFY(cm->saveContact(&nonexistent));       // after setting id to zero, should save
    QVERIFY(cm->removeContact(removalId(nonexistent)));

#ifdef DETAIL_DEFINITION_SUPPORTED
    // now try adding a "megacontact"
    // - get list of all definitions supported by the manager
    // - add one detail of each definition to a contact
    // - save the contact
    // - read it back
    // - ensure that it's the same.
    QContact megacontact;
    QMap<QString, QContactDetailDefinition> defmap = cm->detailDefinitions();
    QList<QContactDetailDefinition> defs = defmap.values();
    foreach (const QContactDetailDefinition def, defs) {

        // Leave these warnings here - might need an API for this
        // XXX FIXME: access constraint reporting as moved to the detail itself
        //if (def.accessConstraint() == QContactDetailDefinition::ReadOnly) {
        //    continue;
        //}

        // This is probably read-only
        if (def.name() == QContactTimestamp::DefinitionName)
            continue;

        // otherwise, create a new detail of the given type and save it to the contact
        QContactDetail det(def.name());
        QMap<QString, QContactDetailFieldDefinition> fieldmap = def.fields();
        QStringList fieldKeys = fieldmap.keys();
        foreach (const QString& fieldKey, fieldKeys) {
            // get the field, and check to see that it's not constrained.
            QContactDetailFieldDefinition currentField = fieldmap.value(fieldKey);
            
            // Don't test detail uris as these are manager specific
            if (fieldKey == QContactDetail::FieldDetailUri)
                continue;

            // Special case: phone number.
            if (def.name() == QContactPhoneNumber::DefinitionName &&
                fieldKey == QContactPhoneNumber::FieldNumber) {
                det.setValue(fieldKey, "+3581234567890");
                continue;
            }

            // Attempt to create a worthy value
            if (!currentField.allowableValues().isEmpty()) {
                // we want to save a value that will be accepted.
                if (currentField.dataType() == QVariant::StringList)
                    det.setValue(fieldKey, QStringList() << currentField.allowableValues().first().toString());
                else if (currentField.dataType() == QVariant::List)
                    det.setValue(fieldKey, QVariantList() << currentField.allowableValues().first());
                else
                    det.setValue(fieldKey, currentField.allowableValues().first());
            } else {
                // any value of the correct type will be accepted
                bool savedSuccessfully = false;
                QVariant dummyValue = QVariant(fieldKey); // try to get some unique string data
                if (dummyValue.canConvert(currentField.dataType())) {
                    savedSuccessfully = dummyValue.convert(currentField.dataType());
                    if (savedSuccessfully) {
                        // we have successfully created a (supposedly) valid field for this detail.
                        det.setValue(fieldKey, dummyValue);
                        continue;
                    }
                }

                // nope, couldn't save the string value (test); try a date.
                dummyValue = QVariant(QDate::currentDate());
                if (dummyValue.canConvert(currentField.dataType())) {
                    savedSuccessfully = dummyValue.convert(currentField.dataType());
                    if (savedSuccessfully) {
                        // we have successfully created a (supposedly) valid field for this detail.
                        det.setValue(fieldKey, dummyValue);
                        continue;
                    }
                }

                // nope, couldn't convert a string or a date - try the integer value (42)
                dummyValue = QVariant(42);
                if (dummyValue.canConvert(currentField.dataType())) {
                    savedSuccessfully = dummyValue.convert(currentField.dataType());
                    if (savedSuccessfully) {
                        // we have successfully created a (supposedly) valid field for this detail.
                        det.setValue(fieldKey, dummyValue);
                        continue;
                    }
                }

                // if we get here, we don't know what sort of value can be saved...
            }
        }
        if (!det.isEmpty())
            megacontact.saveDetail(&det);
    }

    QVERIFY(cm->saveContact(&megacontact)); // must be able to save since built from definitions.
    QContact retrievedMegacontact = cm->contact(retrievalId(megacontact));
    if (!isSuperset(retrievedMegacontact, megacontact)) {
        dumpContactDifferences(retrievedMegacontact, megacontact);
        QEXPECT_FAIL("mgr='wince'", "Address Display Label mismatch", Continue);
        QCOMPARE(megacontact, retrievedMegacontact);
    }
#endif

    // now a contact with many details of a particular definition
    // if the detail is not unique it should then support minimum of two of the same kind
    const int nrOfdetails = 2;
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact veryContactable = createContact("Very", "Contactable", "");
#else
    QContact veryContactable = createContact(nameDef, "Very", "Contactable", "");
#endif
    for (int i = 0; i < nrOfdetails; i++) {
        QString phnStr = QString::number(i);
        QContactPhoneNumber vcphn;
        vcphn.setNumber(phnStr);
        QVERIFY(veryContactable.saveDetail(&vcphn));
    }

    // check that all the numbers were added successfully
    QVERIFY(veryContactable.details<QContactPhoneNumber>().size() == nrOfdetails);
    
#ifdef DETAIL_DEFINITION_SUPPORTED
    // check if it can be saved
    QContactDetailDefinition def = cm->detailDefinition(QContactPhoneNumber::DefinitionName);
    if (def.isUnique()) {    
        QVERIFY(!cm->saveContact(&veryContactable));
    }
    else {
        QVERIFY(cm->saveContact(&veryContactable));
        
        // verify save
        QContact retrievedContactable = cm->contact(retrievalId(veryContactable));
        if (!isSuperset(retrievedContactable, veryContactable)) {
            dumpContactDifferences(veryContactable, retrievedContactable);
            QEXPECT_FAIL("mgr='wince'", "Number of phones supported mismatch", Continue);
            QCOMPARE(veryContactable, retrievedContactable);
        }
    }
#endif
}

void tst_QContactManager::update()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    /* Save a new contact first */
    int contactCount = cm->contacts().size();
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact alice = createContact("AliceUpdate", "inWonderlandUpdate", "2345678");
#else
    QContactDetailDefinition nameDef = cm->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact);
    QContact alice = createContact(nameDef, "AliceUpdate", "inWonderlandUpdate", "2345678");
#endif
    QVERIFY(cm->saveContact(&alice));
    QVERIFY(cm->error() == QContactManager::NoError);
    contactCount += 1; // added a new contact.
    QCOMPARE(cm->contacts().size(), contactCount);

    /* Update name */
    QContactName name = alice.detail<QContactName>();
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&alice, &name, "updated");
#else
    saveContactName(&alice, nameDef, &name, "updated");
#endif
    QVERIFY(cm->saveContact(&alice));
    QVERIFY(cm->error() == QContactManager::NoError);
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&alice, &name, "updated2");
#else
    saveContactName(&alice, nameDef, &name, "updated2");
#endif
    QVERIFY(cm->saveContact(&alice));
    QVERIFY(cm->error() == QContactManager::NoError);
    alice = cm->contact(retrievalId(alice)); // force reload of (persisted) alice
    QContact updated = cm->contact(retrievalId(alice));
    QContactName updatedName = updated.detail<QContactName>();
    QCOMPARE(updatedName, name);
    QCOMPARE(cm->contacts().size(), contactCount); // contact count should be the same, no new contacts

    QContactStatusFlags flags = updated.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasPhoneNumber), true);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasEmailAddress), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasOnlineAccount), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsDeactivated), false);

    /* Test that adding a new detail doesn't cause unwanted side effects */
    int detailCount = alice.details().size();
    QContactEmailAddress email;
    email.setEmailAddress("test@example.com");
    alice.saveDetail(&email);
    QVERIFY(cm->saveContact(&alice));
    QCOMPARE(cm->contacts().size(), contactCount); // contact count shoudl be the same, no new contacts

    // This test is imprecise, since backends can add timestamps etc...
    detailCount += 1;
    updated = cm->contact(retrievalId(alice));
    QVERIFY(updated.details().size() >= detailCount);

    flags = updated.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasPhoneNumber), true);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasEmailAddress), true);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasOnlineAccount), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsDeactivated), false);

    /* Test that removal of fields in a detail works */
    QContactPhoneNumber phn = alice.detail<QContactPhoneNumber>();
    phn.setNumber("1234567");
    phn.setContexts(QContactDetail::ContextHome);
    alice.saveDetail(&phn);
    QVERIFY(cm->saveContact(&alice));
    alice = cm->contact(retrievalId(alice)); // force reload of (persisted) alice
    QVERIFY(alice.detail<QContactPhoneNumber>().contexts().contains(QContactDetail::ContextHome)); // check context saved.
    phn = alice.detail<QContactPhoneNumber>(); // reload the detail, since it's key could have changed
    phn.setContexts(QList<int>()); // remove context field.
    alice.saveDetail(&phn);
    QVERIFY(cm->saveContact(&alice));
    alice = cm->contact(retrievalId(alice)); // force reload of (persisted) alice
    QVERIFY(alice.detail<QContactPhoneNumber>().contexts().isEmpty()); // check context removed.
    QCOMPARE(cm->contacts().size(), contactCount); // removal of a field of a detail shouldn't affect the contact count

    // This test is dangerous, since backends can add timestamps etc...
    QCOMPARE(detailCount, alice.details().size()); // removing a field from a detail should affect the detail count

    /* Test that removal of details works */
    phn = alice.detail<QContactPhoneNumber>(); // reload the detail, since it's key could have changed
    alice.removeDetail(&phn);
    QVERIFY(cm->saveContact(&alice));
    alice = cm->contact(retrievalId(alice)); // force reload of (persisted) alice
    QVERIFY(alice.details<QContactPhoneNumber>().isEmpty()); // no such detail.
    QCOMPARE(cm->contacts().size(), contactCount); // removal of a detail shouldn't affect the contact count

    flags = alice.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasPhoneNumber), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasEmailAddress), true);
    QCOMPARE(flags.testFlag(QContactStatusFlags::HasOnlineAccount), false);
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsDeactivated), false);

    // This test is dangerous, since backends can add timestamps etc...
    //detailCount -= 1;
    //QCOMPARE(detailCount, alice.details().size()); // removing a detail should cause the detail count to decrease by one.

    if (managerSupportsFeature(*cm, "Groups")) {
        // Try changing types - not allowed
        // from contact -> group
        alice.setType(QContactType::TypeGroup);
        QContactName na = alice.detail<QContactName>();
        alice.removeDetail(&na);
        QVERIFY(!cm->saveContact(&alice));
        QVERIFY(cm->error() == QContactManager::AlreadyExistsError);

        // from group -> contact
#ifndef DETAIL_DEFINITION_SUPPORTED
        QContact jabberwock = createContact("", "", "1234567890");
#else
        QContact jabberwock = createContact(nameDef, "", "", "1234567890");
#endif
        jabberwock.setType(QContactType::TypeGroup);
        QVERIFY(cm->saveContact(&jabberwock));
        jabberwock.setType(QContactType::TypeContact);
        QVERIFY(!cm->saveContact(&jabberwock));
        QVERIFY(cm->error() == QContactManager::AlreadyExistsError);
    }
}

void tst_QContactManager::remove()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));
    const int contactCount = cm->contactIds().count();

    QTest::qWait(500); // wait for signal coalescing.
    QSignalSpy addedSpy(cm.data(), contactsAddedSignal);
    QSignalSpy removedSpy(cm.data(), contactsRemovedSignal);

    /* Save a new contact first */
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact alice = createContact("AliceRemove", "inWonderlandRemove", "123456789");
#else
    QContactDetailDefinition nameDef = cm->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact);
    QContact alice = createContact(nameDef, "AliceRemove", "inWonderlandRemove", "123456789");
#endif
    QVERIFY(cm->saveContact(&alice));
    QVERIFY(cm->error() == QContactManager::NoError);
    QVERIFY(alice.id() != QContactId());
    QVERIFY(cm->contactIds().count() > contactCount);

    QTRY_VERIFY(addedSpy.count() > 0);
    addedSpy.clear();
    QVERIFY(removedSpy.count() == 0);

    /* Remove the created contact */
    QVERIFY(cm->removeContact(retrievalId(alice)));
    QCOMPARE(cm->contactIds().count(), contactCount);
    QVERIFY(cm->contact(retrievalId(alice)).isEmpty());
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);

    QTRY_VERIFY(removedSpy.count() > 0);
    removedSpy.clear();
    QVERIFY(addedSpy.count() == 0);
}

void tst_QContactManager::removeAsync()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));
    const int contactCount = cm->contactIds().count();

    QTest::qWait(500); // wait for signal coalescing.
    QSignalSpy addedSpy(cm.data(), contactsAddedSignal);
    QSignalSpy removedSpy(cm.data(), contactsRemovedSignal);

    /* Save a new contact first */
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact alice = createContact("AliceRemove", "inWonderlandRemove", "123456789");
#else
    QContactDetailDefinition nameDef = cm->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact);
    QContact alice = createContact(nameDef, "AliceRemove", "inWonderlandRemove", "123456789");
#endif

    /* Use an asynchronous save request */
    QContactSaveRequest saveRequest;
    saveRequest.setContact(alice);
    saveRequest.setManager(cm.data());
    saveRequest.start();

    QVERIFY(saveRequest.waitForFinished());
    QVERIFY(saveRequest.isFinished());

    QMap<int, QContactManager::Error> errorMap(saveRequest.errorMap());
    QVERIFY(errorMap.count() == 0 || (errorMap.count() == 1 && errorMap.first() == QContactManager::NoError));

    alice = saveRequest.contacts().first();
    QVERIFY(alice.id() != QContactId());
    QVERIFY(cm->contactIds().count() > contactCount);

    QTRY_VERIFY(addedSpy.count() > 0);
    addedSpy.clear();
    QVERIFY(removedSpy.count() == 0);

    /* Remove the created contact asynchronously */
    QContactRemoveRequest removeRequest;
    removeRequest.setContactId(alice.id());
    removeRequest.setManager(cm.data());
    removeRequest.start();

    QVERIFY(removeRequest.waitForFinished());
    QVERIFY(removeRequest.isFinished());

    errorMap = removeRequest.errorMap();
    QVERIFY(errorMap.count() == 0 || (errorMap.count() == 1 && errorMap.first() == QContactManager::NoError));

    QCOMPARE(cm->contactIds().count(), contactCount);

    QVERIFY(cm->contact(retrievalId(alice)).isEmpty());
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);

    QTRY_VERIFY(removedSpy.count() > 0);
    removedSpy.clear();
    QVERIFY(addedSpy.count() == 0);
}

void tst_QContactManager::batch()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    /* First test null pointer operations */
    QVERIFY(!cm->saveContacts(NULL, NULL));
    QVERIFY(cm->error() == QContactManager::BadArgumentError);

    QVERIFY(!cm->removeContacts(QList<QContactId>(), NULL));
    QVERIFY(cm->error() == QContactManager::BadArgumentError);
    
    // Get supported name field
    int nameField = QContactName::FieldFirstName;

    /* Now add 3 contacts, all valid */
    QContact a;
    QContactName na;
    na.setValue(nameField, "XXXXXX Albert");
    a.saveDetail(&na);

    QContact b;
    QContactName nb;
    nb.setValue(nameField, "XXXXXX Bob");
    b.saveDetail(&nb);

    QContact c;
    QContactName nc;
    nc.setValue(nameField, "XXXXXX Carol");
    c.saveDetail(&nc);

    QList<QContact> contacts;
    contacts << a << b << c;

    QMap<int, QContactManager::Error> errorMap;
    // Add one dummy error to test if the errors are reset
    errorMap.insert(0, QContactManager::NoError);
    QVERIFY(cm->saveContacts(&contacts, &errorMap));
    QCOMPARE(cm->error(), QContactManager::NoError);
    QCOMPARE(errorMap.count(), 0);

    /* Make sure our contacts got updated too */
    QCOMPARE(contacts.count(), 3);
    QVERIFY(contacts.at(0).id() != QContactId());
    QVERIFY(contacts.at(1).id() != QContactId());
    QVERIFY(contacts.at(2).id() != QContactId());

    QCOMPARE(contacts.at(0).detail<QContactName>(), na);
    QCOMPARE(contacts.at(1).detail<QContactName>(), nb);
    QCOMPARE(contacts.at(2).detail<QContactName>(), nc);

    /* Retrieve again */
    a = cm->contact(retrievalId(contacts.at(0)));
    b = cm->contact(retrievalId(contacts.at(1)));
    c = cm->contact(retrievalId(contacts.at(2)));
    QCOMPARE(contacts.at(0).detail<QContactName>(), na);
    QCOMPARE(contacts.at(1).detail<QContactName>(), nb);
    QCOMPARE(contacts.at(2).detail<QContactName>(), nc);

    /* Save again, with a null error map */
    QVERIFY(cm->saveContacts(&contacts, NULL));
    QCOMPARE(cm->error(), QContactManager::NoError);

    /* Now make an update to them all */
    QContactPhoneNumber number;
    number.setNumber("1234567");

    QVERIFY(contacts[0].saveDetail(&number));
    number.setNumber("234567");
    QVERIFY(contacts[1].saveDetail(&number));
    number.setNumber("34567");
    QVERIFY(contacts[2].saveDetail(&number));

    QVERIFY(cm->saveContacts(&contacts, &errorMap));
    QCOMPARE(cm->error(), QContactManager::NoError);
    QCOMPARE(errorMap.count(), 0);

    /* Retrieve them and check them again */
    a = cm->contact(retrievalId(contacts.at(0)));
    b = cm->contact(retrievalId(contacts.at(1)));
    c = cm->contact(retrievalId(contacts.at(2)));
    QCOMPARE(contacts.at(0).detail<QContactName>(), na);
    QCOMPARE(contacts.at(1).detail<QContactName>(), nb);
    QCOMPARE(contacts.at(2).detail<QContactName>(), nc);

    QCOMPARE(a.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(b.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(c.details<QContactPhoneNumber>().count(), 1);

    QCOMPARE(a.details<QContactPhoneNumber>().at(0).number(), QString::fromLatin1("1234567"));
    QCOMPARE(b.details<QContactPhoneNumber>().at(0).number(), QString::fromLatin1("234567"));
    QCOMPARE(c.details<QContactPhoneNumber>().at(0).number(), QString::fromLatin1("34567"));

    /* Retrieve them with the batch ID fetch API */
    QList<QContactId> batchIds;
    batchIds << ContactId::apiId(a) << ContactId::apiId(b) << ContactId::apiId(c);

    // Null error map first (doesn't crash)
    QMap<int, QContactManager::Error> map;
    QList<QContact> batchFetch = cm->contacts(batchIds, QContactFetchHint(), &map);
    QCOMPARE(cm->error(), QContactManager::NoError);
    QCOMPARE(batchFetch.count(), 3);
    QCOMPARE(batchFetch.at(0).detail<QContactName>(), na);
    QCOMPARE(batchFetch.at(1).detail<QContactName>(), nb);
    QCOMPARE(batchFetch.at(2).detail<QContactName>(), nc);

    // With error map
    batchFetch = cm->contacts(batchIds, QContactFetchHint(), &errorMap);
    QCOMPARE(cm->error(), QContactManager::NoError);
    QCOMPARE(errorMap.count(), 0);
    QCOMPARE(batchFetch.count(), 3);
    QCOMPARE(batchFetch.at(0).detail<QContactName>(), na);
    QCOMPARE(batchFetch.at(1).detail<QContactName>(), nb);
    QCOMPARE(batchFetch.at(2).detail<QContactName>(), nc);

    /* Now an empty id */
    batchIds.clear();
    batchIds << QContactId() << ContactId::apiId(a) << ContactId::apiId(b) << ContactId::apiId(c);
    batchFetch = cm->contacts(batchIds, QContactFetchHint(), 0);
    QVERIFY(cm->error() != QContactManager::NoError);
    QCOMPARE(batchFetch.count(), 4);
    QCOMPARE(batchFetch.at(0).detail<QContactName>(), QContactName());
    QCOMPARE(batchFetch.at(1).detail<QContactName>(), na);
    QCOMPARE(batchFetch.at(2).detail<QContactName>(), nb);
    QCOMPARE(batchFetch.at(3).detail<QContactName>(), nc);

    batchFetch = cm->contacts(batchIds, QContactFetchHint(), &errorMap);
    QVERIFY(cm->error() != QContactManager::NoError);
    QCOMPARE(batchFetch.count(), 4);
    if (errorMap.size())
        QCOMPARE(errorMap[0], QContactManager::DoesNotExistError);
    QCOMPARE(batchFetch.at(0).detail<QContactName>(), QContactName());
    QCOMPARE(batchFetch.at(1).detail<QContactName>(), na);
    QCOMPARE(batchFetch.at(2).detail<QContactName>(), nb);
    QCOMPARE(batchFetch.at(3).detail<QContactName>(), nc);

    /* Now multiple of the same contact */
    batchIds.clear();
    batchIds << ContactId::apiId(c) << ContactId::apiId(b) << ContactId::apiId(c) << ContactId::apiId(a) << ContactId::apiId(a);
    batchFetch = cm->contacts(batchIds, QContactFetchHint(), &errorMap);
    QVERIFY(cm->error() == QContactManager::NoError);
    QCOMPARE(batchFetch.count(), 5);
    QCOMPARE(errorMap.count(), 0);
    QCOMPARE(batchFetch.at(0).detail<QContactName>(), nc);
    QCOMPARE(batchFetch.at(1).detail<QContactName>(), nb);
    QCOMPARE(batchFetch.at(2).detail<QContactName>(), nc);
    QCOMPARE(batchFetch.at(3).detail<QContactName>(), na);
    QCOMPARE(batchFetch.at(4).detail<QContactName>(), na);

    /* Now delete them all */
    QList<QContactId> ids;
    ids << ContactId::apiId(a) << ContactId::apiId(b) << ContactId::apiId(c);
    QVERIFY(cm->removeContacts(ids, &errorMap));
    QCOMPARE(errorMap.count(), 0);
    QCOMPARE(cm->error(), QContactManager::NoError);

    /* Make sure the contacts really don't exist any more */
    QCOMPARE(cm->contact(retrievalId(a)).id(), QContactId());
    QVERIFY(cm->contact(retrievalId(a)).isEmpty());
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);
    QCOMPARE(cm->contact(retrievalId(b)).id(), QContactId());
    QVERIFY(cm->contact(retrievalId(b)).isEmpty());
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);
    QCOMPARE(cm->contact(retrievalId(c)).id(), QContactId());
    QVERIFY(cm->contact(retrievalId(c)).isEmpty());
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);

    /* Now try removing with all invalid ids (e.g. the ones we just removed) */
    ids.clear();
    ids << ContactId::apiId(a) << ContactId::apiId(b) << ContactId::apiId(c);
    QVERIFY(!cm->removeContacts(ids, &errorMap));
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);
    QCOMPARE(errorMap.count(), 3);
    QCOMPARE(errorMap.values().at(0), QContactManager::DoesNotExistError);
    QCOMPARE(errorMap.values().at(1), QContactManager::DoesNotExistError);
    QCOMPARE(errorMap.values().at(2), QContactManager::DoesNotExistError);

    /* And again with a null error map */
    QVERIFY(!cm->removeContacts(ids, NULL));
    QCOMPARE(cm->error(), QContactManager::DoesNotExistError);

    /* Try adding some new ones again, this time one with an error */
    contacts.clear();
    a.setId(QContactId());
    b.setId(QContactId());
    c.setId(QContactId());

    /* Make B the bad guy */
    QContactDetail bad(static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 0x100));
    bad.setValue(100, "Very bad");
    b.saveDetail(&bad);

    contacts << a << b << c;
    QVERIFY(!cm->saveContacts(&contacts, &errorMap));
    /* We can't really say what the error will be.. maybe bad argument, maybe invalid detail */
    QVERIFY(cm->error() != QContactManager::NoError);

    /* It's permissible to fail all the adds, or to add the successful ones */
    QVERIFY(errorMap.count() > 0);
    QVERIFY(errorMap.count() <= 3);

    // A might have gone through
    if (errorMap.keys().contains(0)) {
        QVERIFY(errorMap.value(0) != QContactManager::NoError);
        QCOMPARE(contacts.at(0).id(), QContactId());
    } else {
        QVERIFY(contacts.at(0).id() != QContactId());
    }

    // B should have failed
    QCOMPARE(errorMap.value(1), QContactManager::InvalidDetailError);
    QCOMPARE(contacts.at(1).id(), QContactId());

    // C might have gone through
    if (errorMap.keys().contains(2)) {
        QVERIFY(errorMap.value(2) != QContactManager::NoError);
        QCOMPARE(contacts.at(2).id(), QContactId());
    } else {
        QVERIFY(contacts.at(2).id() != QContactId());
    }

    /* Fix up B and re save it */
    QVERIFY(contacts[1].removeDetail(&bad));
    QVERIFY(cm->saveContacts(&contacts, &errorMap));
    QCOMPARE(errorMap.count(), 0);
    QCOMPARE(cm->error(), QContactManager::NoError);
    
    // Save and remove a fourth contact. Store the id.
    a.setId(QContactId());
    QVERIFY(cm->saveContact(&a));
    QContactId removedId = ContactId::apiId(a);
    QVERIFY(cm->removeContact(removedId));

    /* Now delete 3 items, but with one bad argument */
    ids.clear();
    ids << ContactId::apiId(contacts.at(0));
    ids << removedId;
    ids << ContactId::apiId(contacts.at(2));

    QVERIFY(!cm->removeContacts(ids, &errorMap));
    QVERIFY(cm->error() != QContactManager::NoError);

    /* Again, the backend has the choice of either removing the successful ones, or not */
    QVERIFY(errorMap.count() > 0);
    QVERIFY(errorMap.count() <= 3);

    // A might have gone through
    if (errorMap.keys().contains(0)) {
        QVERIFY(errorMap.value(0) != QContactManager::NoError);
        QCOMPARE(contacts.at(0).id(), QContactId());
    } else {
        QVERIFY(contacts.at(0).id() != QContactId());
    }

    /* B should definitely have failed */
    QCOMPARE(errorMap.value(1), QContactManager::DoesNotExistError);
    QCOMPARE(ids.at(1), removedId);

    // A might have gone through
    if (errorMap.keys().contains(2)) {
        QVERIFY(errorMap.value(2) != QContactManager::NoError);
        QCOMPARE(contacts.at(2).id(), QContactId());
    } else {
        QVERIFY(contacts.at(2).id() != QContactId());
    }
}

void tst_QContactManager::invalidManager()
{
    /* Create an invalid manager */
    QContactManager manager("this should never work");
    QVERIFY(manager.managerName() == "invalid");
    QVERIFY(manager.managerVersion() == 0);

    /* also, test the other ctor behaviour is sane also */
    QContactManager anotherManager("this should never work", 15);
    QVERIFY(anotherManager.managerName() == "invalid");
    QVERIFY(anotherManager.managerVersion() == 0);

    /* Now test that all the operations fail */
    QVERIFY(manager.contactIds().count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);

    QContact foo;
    QContactName nf;
    nf.setLastName("Lastname");
    foo.saveDetail(&nf);

#ifdef DISPLAY_LABEL_SUPPORTED
    QVERIFY(manager.synthesizedContactDisplayLabel(foo).isEmpty());
    QVERIFY(manager.error() == QContactManager::NotSupportedError);
#endif

    QVERIFY(manager.saveContact(&foo) == false);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);
    QVERIFY(foo.id() == QContactId());
    QVERIFY(manager.contactIds().count() == 0);

    QVERIFY(manager.contact(retrievalId(foo)).id() == QContactId());
    QVERIFY(manager.contact(retrievalId(foo)).isEmpty());
    QVERIFY(manager.error() == QContactManager::NotSupportedError);

    QVERIFY(manager.removeContact(removalId(foo)) == false);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);

    QMap<int, QContactManager::Error> errorMap;
    errorMap.insert(0, QContactManager::NoError);
    QVERIFY(!manager.saveContacts(0, &errorMap));
    QVERIFY(manager.errorMap().count() == 0);
    QVERIFY(errorMap.count() == 0);
    QVERIFY(manager.error() == QContactManager::BadArgumentError);

    /* filters */
    QContactFilter f; // matches everything
    QContactDetailFilter df;
    setFilterDetail<QContactDisplayLabel>(df, QContactDisplayLabel::FieldLabel);
    QVERIFY(manager.contactIds(QContactFilter()).count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);
    QVERIFY(manager.contactIds(df).count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);
    QVERIFY(manager.contactIds(f | f).count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);
    QVERIFY(manager.contactIds(df | df).count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);

    QVERIFY(manager.isFilterSupported(f) == false);
    QVERIFY(manager.isFilterSupported(df) == false);

    QList<QContact> list;
    list << foo;

    QVERIFY(!manager.saveContacts(&list, &errorMap));
    QVERIFY(errorMap.count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);

    QVERIFY(!manager.removeContacts(QList<QContactId>(), &errorMap));
    QVERIFY(errorMap.count() == 0);
    QVERIFY(manager.error() == QContactManager::BadArgumentError);

    QList<QContactId> idlist;
    idlist << ContactId::apiId(foo);
    QVERIFY(!manager.removeContacts(idlist, &errorMap));
    QVERIFY(errorMap.count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError);

#ifdef DETAIL_DEFINITION_SUPPORTED
    /* Detail definitions */
    QVERIFY(manager.detailDefinitions().count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);

    QContactDetailDefinition def;
    def.setUnique(true);
    def.setName("new field");
    QMap<QString, QContactDetailFieldDefinition> fields;
    QContactDetailFieldDefinition currField;
    currField.setDataType(QVariant::String);
    fields.insert("value", currField);
    def.setFields(fields);

    QVERIFY(manager.saveDetailDefinition(def, QContactType::TypeContact) == false);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
    QVERIFY(manager.saveDetailDefinition(def) == false);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
    QVERIFY(manager.detailDefinitions().count(QContactType::TypeContact) == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
    QVERIFY(manager.detailDefinitions().count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
    QVERIFY(manager.detailDefinition("new field").name() == QString());
    QVERIFY(manager.removeDetailDefinition(def.name(), QContactType::TypeContact) == false);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
    QVERIFY(manager.removeDetailDefinition(def.name()) == false);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
    QVERIFY(manager.detailDefinitions().count() == 0);
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::InvalidContactTypeError);
#endif

    /* Self contact id */
    QVERIFY(!manager.setSelfContactId(ContactId::apiId(12)));
    QVERIFY(manager.error() == QContactManager::NotSupportedError);
    QVERIFY(manager.selfContactId() == QContactId());
    QVERIFY(manager.error() == QContactManager::NotSupportedError || manager.error() == QContactManager::DoesNotExistError);

    /* Capabilities */
    QVERIFY(manager.supportedDataTypes().count() == 0);
    QVERIFY(!managerSupportsFeature(manager, "ActionPreferences"));
    QVERIFY(!managerSupportsFeature(manager, "MutableDefinitions"));
}

void tst_QContactManager::presenceReporting()
{
    QFETCH(QString, uri);
    QFETCH(bool, mergePresenceChanges);

    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    QSignalSpy addedSpy(cm.data(), contactsAddedSignal);
    QSignalSpy changedSpy(cm.data(), contactsChangedSignal);
    QSignalSpy removedSpy(cm.data(), contactsRemovedSignal);

    // The contactsPresenceChanged signal is not exported by QContactManager, so we
    // need to find it from the manager's engine object
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*cm.data());

    QSignalSpy presenceChangedSpy(cme, contactsPresenceChangedSignal);

    QContact a;

    QContactName n;
    n.setFirstName("A");
    n.setMiddleName("Test");
    n.setLastName("PresenceUpdate");
    a.saveDetail(&n);

    QDateTime ts(QDateTime::currentDateTime());

    QContactPresence p;
    p.setPresenceState(QContactPresence::PresenceBusy);
    p.setTimestamp(ts);
    QVERIFY(a.saveDetail(&p));

    QContactOnlineAccount oa;
    oa.setAccountUri("FakeImAccount");
    oa.setValue(QContactOnlineAccount__FieldEnabled, false);
    QVERIFY(a.saveDetail(&oa));

    QContactOriginMetadata om;
    om.setId("TestContact");
    om.setGroupId("TestGroup");
    om.setEnabled(false);
    QVERIFY(a.saveDetail(&om));

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));

    QContact b;

    QContactName n2;
    n2.setFirstName("B");
    n2.setLastName("PresenceUnchanged");
    b.saveDetail(&n2);

    QContactPresence p2;
    p2.setPresenceState(QContactPresence::PresenceAway);
    p2.setTimestamp(ts);
    QVERIFY(b.saveDetail(&p2));

    QVERIFY(cm->saveContact(&b));
    b = cm->contact(retrievalId(b));

    QDateTime saveTimestamp(QDateTime::currentDateTime());

    QTest::qWait(500); // wait for signal coalescing.
    QTRY_VERIFY(addedSpy.count() > 0);
    addedSpy.clear();
    QCOMPARE(presenceChangedSpy.count(), 0);
    changedSpy.clear();
    removedSpy.clear();

    // Sort on presence
    QContactIdFilter idFilter;
    idFilter.setIds(QList<QContactId>() << a.id() << b.id());

    QList<QContactSortOrder> sortOrders;
    QContactSortOrder byPhoneNumber;
    setSortDetail<QContactPhoneNumber>(byPhoneNumber, QContactPhoneNumber::FieldNumber);
    sortOrders.append(byPhoneNumber);

    QContactSortOrder presenceOrder;
    setSortDetail<QContactGlobalPresence>(presenceOrder, QContactGlobalPresence::FieldPresenceState);

    QList<QContactId> sortedIds(cm->contactIds(idFilter, QList<QContactSortOrder>() << presenceOrder));
    QCOMPARE(sortedIds.count(), 2);
    QCOMPARE(sortedIds.at(0), b.id());
    QCOMPARE(sortedIds.at(1), a.id());

    QContactChangeLogFilter clFilter(QContactChangeLogFilter::EventChanged);
    clFilter.setSince(saveTimestamp);

    sortedIds = cm->contactIds(idFilter & clFilter);
    QCOMPARE(sortedIds.count(), 0);

    // Test a presence-only update (can include presence/origin-metadata/online-account changes)
    p = a.detail<QContactPresence>();
    p.setPresenceState(QContactPresence::PresenceAvailable);
    QVERIFY(a.saveDetail(&p));

    oa = a.detail<QContactOnlineAccount>();
    oa.setValue(QContactOnlineAccount__FieldEnabled, true);
    QVERIFY(a.saveDetail(&oa));

    om = a.detail<QContactOriginMetadata>();
    om.setEnabled(true);
    QVERIFY(a.saveDetail(&om));

    // We need to use a detail definition mask to enable the presence-only update
    QList<QContact> contacts;
    contacts.append(a);
    QVERIFY(cm->saveContacts(&contacts, DetailList() << detailType<QContactPresence>()
                                                     << detailType<QContactOnlineAccount>()
                                                     << detailType<QContactOriginMetadata>()));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.detail<QContactPresence>().presenceState(), QContactPresence::PresenceAvailable);
    QCOMPARE(a.detail<QContactPresence>().timestamp(), ts);

    QTest::qWait(500); // wait for signal coalescing.
    if (!mergePresenceChanges) {
        QTRY_VERIFY(presenceChangedSpy.count() > 0);
        presenceChangedSpy.clear();
        QCOMPARE(changedSpy.count(), 0);
    } else {
        QTRY_VERIFY(changedSpy.count() > 0);
        changedSpy.clear();
        QCOMPARE(presenceChangedSpy.count(), 0);
    }
    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);

    // Check that the sort now includes the update state
    sortedIds = cm->contactIds(idFilter, QList<QContactSortOrder>() << presenceOrder);
    QCOMPARE(sortedIds.count(), 2);
    QCOMPARE(sortedIds.at(0), a.id());
    QCOMPARE(sortedIds.at(1), b.id());

    // Perform queries that require access to the presence for filtering
    QContactDetailFilter availableFilter;
    setFilterDetail<QContactGlobalPresence>(availableFilter, QContactGlobalPresence::FieldPresenceState);
    setFilterValue(availableFilter, QContactPresence::PresenceAvailable);

    sortedIds = cm->contactIds(idFilter & availableFilter);
    QEXPECT_FAIL("", "fails due to invalid SQL result", Continue);
    QCOMPARE(sortedIds.count(), 1);
    //QCOMPARE(sortedIds.at(0), a.id());

    // Check that the modified timestamp is updated
    sortedIds = cm->contactIds(idFilter & clFilter);
    QCOMPARE(sortedIds.count(), 1);
    QCOMPARE(sortedIds.at(0), a.id());

    // Test an update including non-presence changes
    p = a.detail<QContactPresence>();
    p.setPresenceState(QContactPresence::PresenceBusy);
    QVERIFY(a.saveDetail(&p));

    n = a.detail<QContactName>();
    n.setMiddleName("Dummy");
    QVERIFY(a.saveDetail(&n));

    contacts.clear();
    contacts.append(a);
    QVERIFY(cm->saveContacts(&contacts, DetailList() << detailType<QContactPresence>()
                                                     << detailType<QContactName>()));
    a = cm->contact(retrievalId(a));

    QTest::qWait(500); // wait for signal coalescing.
    QTRY_VERIFY(changedSpy.count() > 0);
    changedSpy.clear();
    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(presenceChangedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);

    QVERIFY(cm->removeContact(retrievalId(a)));

    QTest::qWait(500);
    QTRY_VERIFY(removedSpy.count() > 0);
    removedSpy.clear();
    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(presenceChangedSpy.count(), 0);
}

void tst_QContactManager::presenceReporting_data()
{
    QTest::addColumn<bool>("mergePresenceChanges");
    QTest::addColumn<QString>("uri");

    const QString managerName(QString::fromLatin1(SQLITE_MANAGER));
    QMap<QString, QString> params;
    params.insert("autoTest", "true");

    params.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("true"));
    QTest::newRow("mergePresenceChanges=true") << true << QContactManager::buildUri(managerName, params);

    params.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("false"));
    QTest::newRow("mergePresenceChanges=false") << false << QContactManager::buildUri(managerName, params);
}

void tst_QContactManager::presenceAccumulation()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    QContact a;

    QContactName n;
    n.setFirstName("A");
    n.setMiddleName("Test");
    n.setLastName("Presence-Accumulation");
    a.saveDetail(&n);

    QDateTime ts(QDateTime::currentDateTime());

    QContactPresence p;
    p.setPresenceState(QContactPresence::PresenceAway);
    p.setTimestamp(ts);
    QVERIFY(a.saveDetail(&p));

    QContactOnlineAccount oa;
    oa.setAccountUri("FakeImAccount");
    oa.setValue(QContactOnlineAccount__FieldEnabled, false);
    QVERIFY(a.saveDetail(&oa));

    QContactOriginMetadata om;
    om.setId("TestContact");
    om.setGroupId("TestGroup");
    om.setEnabled(false);
    QVERIFY(a.saveDetail(&om));

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.detail<QContactPresence>().presenceState(), QContactPresence::PresenceAway);
    QCOMPARE(a.detail<QContactPresence>().timestamp(), ts);

    QCOMPARE(a.detail<QContactGlobalPresence>().presenceState(), QContactPresence::PresenceAway);
    QCOMPARE(a.detail<QContactGlobalPresence>().timestamp(), ts);

    for (int i = 0; i < 50; ++i) {
        // Test a presence-only update (increase the size each time)
        p = a.detail<QContactPresence>();
        p.setPresenceState(QContactPresence::PresenceAvailable);
        const QString newCustomMessage(p.customMessage() + QString::fromLatin1(QByteArray(10, 'a')));
        p.setCustomMessage(newCustomMessage);
        QVERIFY(a.saveDetail(&p));

        oa = a.detail<QContactOnlineAccount>();
        oa.setValue(QContactOnlineAccount__FieldEnabled, !oa.value(QContactOnlineAccount__FieldEnabled).toBool());
        QVERIFY(a.saveDetail(&oa));

        om = a.detail<QContactOriginMetadata>();
        om.setEnabled(true);
        QVERIFY(a.saveDetail(&om));

        // We need to use a detail definition mask to enable the presence-only update
        QList<QContact> contacts;
        contacts.append(a);
        QVERIFY(cm->saveContacts(&contacts, DetailList() << detailType<QContactPresence>()
                                                         << detailType<QContactOnlineAccount>()
                                                         << detailType<QContactOriginMetadata>()));
        a = cm->contact(retrievalId(a));

        QCOMPARE(a.detail<QContactPresence>().presenceState(), QContactPresence::PresenceAvailable);
        QCOMPARE(a.detail<QContactPresence>().customMessage(), newCustomMessage);
        QCOMPARE(a.detail<QContactPresence>().timestamp(), ts);

        QCOMPARE(a.detail<QContactGlobalPresence>().presenceState(), QContactPresence::PresenceAvailable);
        QCOMPARE(a.detail<QContactGlobalPresence>().customMessage(), newCustomMessage);
        QCOMPARE(a.detail<QContactGlobalPresence>().timestamp(), ts);

        QCOMPARE(a.detail<QContactOnlineAccount>().value(QContactOnlineAccount__FieldEnabled), oa.value(QContactOnlineAccount__FieldEnabled));

        QCOMPARE(a.detail<QContactOriginMetadata>().enabled(), om.enabled());
    }

    // Test an update including non-presence changes
    p = a.detail<QContactPresence>();
    p.setPresenceState(QContactPresence::PresenceBusy);
    QVERIFY(a.saveDetail(&p));

    n = a.detail<QContactName>();
    n.setMiddleName("Dummy");
    QVERIFY(a.saveDetail(&n));

    QList<QContact> contacts;
    contacts.append(a);
    QVERIFY(cm->saveContacts(&contacts, DetailList() << detailType<QContactPresence>()
                                                     << detailType<QContactName>()));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.detail<QContactPresence>().presenceState(), QContactPresence::PresenceBusy);
    QCOMPARE(a.detail<QContactPresence>().customMessage(), p.customMessage());
    QCOMPARE(a.detail<QContactPresence>().timestamp(), p.timestamp());

    QCOMPARE(a.detail<QContactGlobalPresence>().presenceState(), QContactPresence::PresenceBusy);
    QCOMPARE(a.detail<QContactGlobalPresence>().customMessage(), p.customMessage());
    QCOMPARE(a.detail<QContactGlobalPresence>().timestamp(), p.timestamp());

    QVERIFY(cm->removeContact(retrievalId(a)));
}

void tst_QContactManager::nonprivileged()
{
    const QString managerName(QString::fromLatin1(SQLITE_MANAGER));
    QMap<QString, QString> params;
    params.insert("autoTest", "true");

    QScopedPointer<QContactManager> privilegedCm(QContactManager::fromUri(QContactManager::buildUri(managerName, params)));
    QVERIFY(privilegedCm);
    QVERIFY(!privilegedCm->managerUri().isEmpty());

    params.insert(QString::fromLatin1("nonprivileged"), QString::fromLatin1("true"));
    QScopedPointer<QContactManager> nonprivilegedCm(QContactManager::fromUri(QContactManager::buildUri(managerName, params)));
    QVERIFY(nonprivilegedCm);
    QVERIFY(!nonprivilegedCm->managerUri().isEmpty());
    QVERIFY(nonprivilegedCm->managerUri() != privilegedCm->managerUri());

    QSignalSpy privilegedAddedSpy(privilegedCm.data(), contactsAddedSignal);
    QSignalSpy nonprivilegedAddedSpy(nonprivilegedCm.data(), contactsAddedSignal);

    QContact a;

    QContactName n;
    n.setFirstName("A");
    n.setMiddleName("Test");
    n.setLastName("Nonprivileged");
    a.saveDetail(&n);

    QVERIFY(privilegedCm->saveContact(&a));
    a = privilegedCm->contact(retrievalId(a));

    QVERIFY(a.id() != QContactId());
    QCOMPARE(a.detail<QContactName>().firstName(), n.firstName());
    QCOMPARE(a.detail<QContactName>().middleName(), n.middleName());
    QCOMPARE(a.detail<QContactName>().lastName(), n.lastName());

    QTest::qWait(500); // wait for signal coalescing.
    QTRY_VERIFY(privilegedAddedSpy.count() > 0);
    privilegedAddedSpy.clear();
    QTRY_VERIFY(nonprivilegedAddedSpy.count() == 0);

    // The contact should not be present in the other DB (or should be a different contact)
    QContact b = nonprivilegedCm->contact(retrievalId(a));
    QVERIFY(b.id() == QContactId() ||
            b.detail<QContactName>().firstName() != n.firstName() ||
            b.detail<QContactName>().middleName() != n.middleName() ||
            b.detail<QContactName>().lastName() != n.lastName());

    // Add a contact to the nonprivileged DB
    n.setFirstName("B");
    b.saveDetail(&n);

    QVERIFY(nonprivilegedCm->saveContact(&b));
    b = nonprivilegedCm->contact(retrievalId(b));
    QVERIFY(b.id() != QContactId());

    QTest::qWait(500);
    QTRY_VERIFY(nonprivilegedAddedSpy.count() > 0);
    nonprivilegedAddedSpy.clear();
    QTRY_VERIFY(privilegedAddedSpy.count() == 0);

    // This contact should not be present in the privileged DB
    QContact c = privilegedCm->contact(retrievalId(b));
    QVERIFY(c.id() == QContactId() ||
            c.detail<QContactName>().firstName() != n.firstName() ||
            c.detail<QContactName>().middleName() != n.middleName() ||
            c.detail<QContactName>().lastName() != n.lastName());
}

void tst_QContactManager::nameSynthesis_data()
{
    QTest::addColumn<QString>("expected");

    QTest::addColumn<bool>("addname");
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<QString>("first");
    QTest::addColumn<QString>("middle");
    QTest::addColumn<QString>("last");
    QTest::addColumn<QString>("suffix");

    QTest::addColumn<bool>("addcompany");
    QTest::addColumn<QString>("company");

    QTest::addColumn<bool>("addname2");
    QTest::addColumn<QString>("secondprefix");
    QTest::addColumn<QString>("secondfirst");
    QTest::addColumn<QString>("secondmiddle");
    QTest::addColumn<QString>("secondlast");
    QTest::addColumn<QString>("secondsuffix");

    QTest::addColumn<bool>("addcompany2");
    QTest::addColumn<QString>("secondcompany");

    QString e; // empty string.. gets a work out

    /* Various empty ones */
    QTest::newRow("empty contact") << e
            << false << e << e << e << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("empty name") << e
            << true << e << e << e << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("empty names") << e
            << true << e << e << e << e << e
            << false << e
            << true << e << e << e << e << e
            << false << e;
    QTest::newRow("empty org") << e
            << false << e << e << e << e << e
            << true << e
            << false << e << e << e << e << e
            << true << e;
    QTest::newRow("empty orgs") << e
            << false << e << e << e << e << e
            << true << e
            << false << e << e << e << e << e
            << true << e;
    QTest::newRow("empty orgs and names") << e
            << true << e << e << e << e << e
            << true << e
            << true << e << e << e << e << e
            << true << e;

    /* Single values */
    QTest::newRow("prefix") << "Prefix"
            << true << "Prefix" << e << e << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("first") << "First"
            << true << e << "First" << e << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("middle") << "Middle"
            << true << e << e << "Middle" << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("last") << "Last"
            << true << e << e << e << "Last" << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("suffix") << "Suffix"
            << true << e << e << e << e << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;

    /* Single values in the second name */
    QTest::newRow("prefix in second") << "Prefix"
            << false << "Prefix" << e << e << e << e
            << false << e
            << true << "Prefix" << e << e << e << e
            << false << e;
    QTest::newRow("first in second") << "First"
            << false << e << "First" << e << e << e
            << false << e
            << true << e << "First" << e << e << e
            << false << e;
    QTest::newRow("middle in second") << "Middle"
            << false << e << e << "Middle" << e << e
            << false << e
            << true << e << e << "Middle" << e << e
            << false << e;
    QTest::newRow("last in second") << "Last"
            << false << e << e << e << "Last" << e
            << false << e
            << true << e << e << e << "Last" << e
            << false << e;
    QTest::newRow("suffix in second") << "Suffix"
            << false << e << e << e << e << "Suffix"
            << false << e
            << true << e << e << e << e << "Suffix"
            << false << e;

    /* Multiple name values */
    QTest::newRow("prefix first") << "Prefix First"
            << true << "Prefix" << "First" << e << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("prefix middle") << "Prefix Middle"
            << true << "Prefix" << e << "Middle" << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("prefix last") << "Prefix Last"
            << true << "Prefix" << e << e << "Last" << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("prefix suffix") << "Prefix Suffix"
            << true << "Prefix" << e << e << e << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("first middle") << "First Middle"
            << true << e << "First" << "Middle" << e << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("first last") << "First Last"
            << true << e << "First" << e << "Last" << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("first suffix") << "First Suffix"
            << true << e << "First" << e << e << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("middle last") << "Middle Last"
            << true << e << e << "Middle" << "Last" << e
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("middle suffix") << "Middle Suffix"
            << true << e << e << "Middle" << e << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("last suffix") << "Last Suffix"
            << true << e << e << e << "Last" << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;

    /* Everything.. */
    QTest::newRow("all name") << "Prefix First Middle Last Suffix"
            << true << "Prefix" << "First" << "Middle" << "Last" << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("all name second") << "Prefix First Middle Last Suffix"
            << false << "Prefix" << "First" << "Middle" << "Last" << "Suffix"
            << false << e
            << true << "Prefix" << "First" << "Middle" << "Last" << "Suffix"
            << false << e;

    /* Org */
    QTest::newRow("org") << "Company"
            << false << e << e << e << e << e
            << true << "Company"
            << false << e << e << e << e << e
            << false << e;
    QTest::newRow("second org") << "Company"
            << false << e << e << e << e << e
            << false << e
            << false << e << e << e << e << e
            << true << "Company";

    /* Mix */
    QTest::newRow("org and empty name") << "Company"
            << true << e << e << e << e << e
            << true << "Company"
            << false << e << e << e << e << e
            << false << e;

    QTest::newRow("name and empty org") << "Prefix First Middle Last Suffix"
            << true << "Prefix" << "First" << "Middle" << "Last" << "Suffix"
            << false << e
            << false << e << e << e << e << e
            << false << e;

    /* names are preferred to orgs */
    QTest::newRow("name and org") << "Prefix First Middle Last Suffix"
            << true << "Prefix" << "First" << "Middle" << "Last" << "Suffix"
            << true << "Company"
            << false << e << e << e << e << e
            << false << e;

}

#ifdef COMPATIBLE_CONTACT_SUPPORTED
void tst_QContactManager::compatibleContact_data()
{
    QTest::addColumn<QContact>("input");
    QTest::addColumn<QContact>("expected");
    QTest::addColumn<QContactManager::Error>("error");

    QContact baseContact;
    QContactName name;
    name.setFirstName(QLatin1String("First"));
    baseContact.saveDetail(&name);

    {
        QTest::newRow("already compatible") << baseContact << baseContact << QContactManager::NoError;
    }

    {
        QContact contact(baseContact);
        QContactDetail detail(static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 0x100));
        detail.setValue(100, QLatin1String("Value"));
        contact.saveDetail(&detail);
        QTest::newRow("unknown detail") << contact << baseContact << QContactManager::NoError;
    }

    {
        QContact contact(baseContact);
        QContactType type1;
        type1.setType(QContactType::TypeContact);
        contact.saveDetail(&type1);
        QContactType type2;
        type2.setType(QContactType::TypeGroup);
        contact.saveDetail(&type2);
        QContact expected(baseContact);
        expected.saveDetail(&type1);
        QTest::newRow("duplicate unique field") << contact << expected << QContactManager::NoError;
    }

    {
        QContact contact(baseContact);
        QContactPhoneNumber phoneNumber;
        phoneNumber.setValue("UnknownKey", "Value");
        contact.saveDetail(&phoneNumber);
        QTest::newRow("unknown field") << contact << baseContact << QContactManager::NoError;
    }

#ifdef DISPLAY_LABEL_SUPPORTED
    {
        QContact contact(baseContact);
        QContactDisplayLabel displayLabel;
        displayLabel.setValue(QContactDisplayLabel::FieldLabel, QStringList("Value"));
        contact.saveDetail(&displayLabel);
        QTest::newRow("wrong type") << contact << baseContact << QContactManager::NoError;
    }
#endif

    {
        QContact contact(baseContact);
        QContactPhoneNumber phoneNumber1;
        phoneNumber1.setNumber(QLatin1String("1234"));
        phoneNumber1.setSubTypes(QStringList()
                                << QContactPhoneNumber::SubTypeMobile
                                << QContactPhoneNumber::SubTypeVoice
                                << QLatin1String("InvalidSubtype"));
        contact.saveDetail(&phoneNumber1);
        QContact expected(baseContact);
        QContactPhoneNumber phoneNumber2;
        phoneNumber2.setNumber(QLatin1String("1234"));
        phoneNumber2.setSubTypes(QStringList()
                                << QContactPhoneNumber::SubTypeMobile
                                << QContactPhoneNumber::SubTypeVoice);
        expected.saveDetail(&phoneNumber2);
        QTest::newRow("bad value (list)") << contact << expected << QContactManager::NoError;
    }

    {
        QContact contact(baseContact);
        QContactPhoneNumber phoneNumber1;
        phoneNumber1.setNumber(QLatin1String("1234"));
        phoneNumber1.setSubTypes(QStringList(QLatin1String("InvalidSubtype")));
        contact.saveDetail(&phoneNumber1);
        QContact expected(baseContact);
        QContactPhoneNumber phoneNumber2;
        phoneNumber2.setNumber(QLatin1String("1234"));
        expected.saveDetail(&phoneNumber2);
        QTest::newRow("all bad value (list)") << contact << expected << QContactManager::NoError;
    }

    {
        QContact contact(baseContact);
        QContactGender gender;
        gender.setGender(QLatin1String("UnknownGender"));
        contact.saveDetail(&gender);
        QTest::newRow("bad value (string)") << contact << baseContact << QContactManager::NoError;
    }

    {
        QContact contact;
        QContactGender gender;
        gender.setGender(QLatin1String("UnknownGender"));
        contact.saveDetail(&gender);
        QTest::newRow("bad value (string)") << contact << QContact() << QContactManager::DoesNotExistError;
    }
}

void tst_QContactManager::compatibleContact()
{
    QScopedPointer<QContactManager> cm(newContactManager());

    QFETCH(QContact, input);
    QFETCH(QContact, expected);
    QFETCH(QContactManager::Error, error);
    QEXPECT_FAIL("duplicate unique field", "Inexplicably broken", Abort);
    QCOMPARE(cm->compatibleContact(input), expected);
    QCOMPARE(cm->error(), error);
}
#endif

#ifdef MUTABLE_SCHEMA_SUPPORTED
void tst_QContactManager::contactValidation()
{
    /* Use the default engine as a reference (validation is not engine specific) */
    QScopedPointer<QContactManager> cm(newContactManager());
    QContact c;

    /*
     * Add some definitions for testing:
     *
     * 1) a unique detail
     * 2) a detail with restricted values
     * 3) a create only detail
     * 4) a unique create only detail
     */
    QContactDetailDefinition uniqueDef;
    QMap<QString, QContactDetailFieldDefinition> fields;
    QContactDetailFieldDefinition field;
    field.setDataType(QVariant::String);
    fields.insert("value", field);

    uniqueDef.setName("UniqueDetail");
    uniqueDef.setFields(fields);
    uniqueDef.setUnique(true);

    QVERIFY(cm->saveDetailDefinition(uniqueDef));

    QContactDetailDefinition restrictedDef;
    restrictedDef.setName("RestrictedDetail");
    fields.clear();
    field.setAllowableValues(QVariantList() << "One" << "Two" << "Three");
    fields.insert("value", field);
    restrictedDef.setFields(fields);

    QVERIFY(cm->saveDetailDefinition(restrictedDef));

    // first, test an invalid definition
    QContactDetail d1 = QContactDetail("UnknownDefinition");
    d1.setValue("test", "test");
    c.saveDetail(&d1);
    QVERIFY(!cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::InvalidDetailError);
    c.removeDetail(&d1);

    // second, test an invalid uniqueness constraint
    QContactDetail d2 = QContactDetail("UniqueDetail");
    d2.setValue("value", "test");
    c.saveDetail(&d2);

    // One unique should be ok
    QVERIFY(cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::NoError);

    // Two uniques should not be ok
    QContactDetail d3 = QContactDetail("UniqueDetail");
    d3.setValue("value", "test2");
    c.saveDetail(&d3);
    QVERIFY(!cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::AlreadyExistsError);
    c.removeDetail(&d3);
    c.removeDetail(&d2);

    // third, test an invalid field name
    QContactDetail d4 = QContactDetail(QContactPhoneNumber::DefinitionName);
    d4.setValue("test", "test");
    c.saveDetail(&d4);
    QVERIFY(!cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::InvalidDetailError);
    c.removeDetail(&d4);

    // fourth, test an invalid field data type
    QContactDetail d5 = QContactDetail(QContactPhoneNumber::DefinitionName);
    d5.setValue(QContactPhoneNumber::FieldNumber, QDateTime::currentDateTime());
    c.saveDetail(&d5);
    QVERIFY(!cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::InvalidDetailError);
    c.removeDetail(&d5);

    // fifth, test an invalid field value (not in the allowed list)
    QContactDetail d6 = QContactDetail("RestrictedDetail");
    d6.setValue("value", "Seven"); // not in One, Two or Three
    c.saveDetail(&d6);
    QVERIFY(!cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::InvalidDetailError);
    c.removeDetail(&d6);

    /* Now a valid value */
    d6.setValue("value", "Two");
    c.saveDetail(&d6);
    QVERIFY(cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::NoError);
    c.removeDetail(&d6);

    // Test a completely valid one.
    QContactDetail d7 = QContactDetail(QContactPhoneNumber::DefinitionName);
    d7.setValue(QContactPhoneNumber::FieldNumber, "0123456");
    c.saveDetail(&d7);
    QVERIFY(cm->saveContact(&c));
    QCOMPARE(cm->error(), QContactManager::NoError);
    c.removeDetail(&d7);
}
#endif

void tst_QContactManager::observerDeletion()
{
    QContactManager *manager = newContactManager();
    QContact c;
    QVERIFY(manager->saveContact(&c));
    QContactId id = ContactId::apiId(c);
    QContactObserver *observer = new QContactObserver(manager, id);
    Q_UNUSED(observer)
    delete manager;
    delete observer;
    // Test for bug MOBILITY-2566 - that QContactObserver doesn't crash when it is
    // destroyed after the associated QContactManager
}

void tst_QContactManager::signalEmission()
{
    QTest::qWait(500); // clear the signal queue
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> m1(QContactManager::fromUri(uri));

    QSignalSpy spyCA(m1.data(), contactsAddedSignal);
    QSignalSpy spyCM(m1.data(), contactsChangedSignal);
    QSignalSpy spyCR(m1.data(), contactsRemovedSignal);

    QTestSignalSink casink(m1.data(), contactsAddedSignal);
    QTestSignalSink cmsink(m1.data(), contactsChangedSignal);
    QTestSignalSink crsink(m1.data(), contactsRemovedSignal);

    QList<QVariant> args;
    QList<QContactId> arg;
    QContact c;
    QList<QContact> batchAdd;
    QList<QContactId> batchRemove;
    QList<QContactId> sigids;
    int addSigCount = 0; // the expected signal counts.
    int modSigCount = 0;
    int remSigCount = 0;

#ifdef DETAIL_DEFINITION_SUPPORTED
    QContactDetailDefinition nameDef = m1->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact);
#endif

    // verify add emits signal added
    QContactName nc;
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&c, &nc, "John Sigem");
#else
    saveContactName(&c, nameDef, &nc, "John Sigem");
#endif
    QVERIFY(m1->saveContact(&c));
    QContactId cid = ContactId::apiId(c);
    addSigCount += 1;
    QTest::qWait(500); // wait for signal coalescing.
    QTRY_VERIFY(spyCA.count() >= addSigCount);
    addSigCount = spyCA.count();

    QScopedPointer<QContactObserver> c1Observer(new QContactObserver(m1.data(), cid));
    QScopedPointer<QSignalSpy> spyCOM1(new QSignalSpy(c1Observer.data(), SIGNAL(contactChanged())));
    QScopedPointer<QSignalSpy> spyCOR1(new QSignalSpy(c1Observer.data(), SIGNAL(contactRemoved())));

    // verify save modified emits signal changed
    spyCM.clear();
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&c, &nc, "Citizen Sigem");
#else
    saveContactName(&c, nameDef, &nc, "Citizen Sigem");
#endif
    QVERIFY(m1->saveContact(&c));
    modSigCount = 1;
    QTRY_VERIFY(spyCM.count() >= modSigCount);
    modSigCount = spyCM.count();
    QTRY_COMPARE(spyCOM1->count(), 1);
    args = spyCM.takeFirst();
    modSigCount -= 1;
    arg = args.first().value<QList<QContactId> >();
    while (spyCM.count()) {
        arg.append(spyCM.takeFirst().first().value<QList<QContactId> >());
    }
    modSigCount = spyCM.count();
    QVERIFY(arg.contains(cid));

    // verify remove emits signal removed
    m1->removeContact(removalId(c));
    remSigCount += 1;
    QTRY_COMPARE(spyCR.count(), remSigCount);
    QTRY_COMPARE(spyCOR1->count(), 1);
    args = spyCR.takeFirst();
    remSigCount -= 1;
    arg = args.first().value<QList<QContactId> >();
    while (spyCR.count()) {
        arg.append(spyCM.takeFirst().first().value<QList<QContactId> >());
    }
    remSigCount = spyCR.count();
    QVERIFY(arg.contains(cid));

    // verify multiple adds works as advertised
    QContact c2, c3;
    QContactName nc2, nc3;
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&c2, &nc2, "Mark");
    saveContactName(&c3, &nc3, "Garry");
#else
    saveContactName(&c2, nameDef, &nc2, "Mark");
    saveContactName(&c3, nameDef, &nc3, "Garry");
#endif
    QVERIFY(m1->saveContact(&c2));
    addSigCount += 1;
    QVERIFY(m1->saveContact(&c3));
    addSigCount += 1;
    QTRY_VERIFY(spyCA.count() >= (addSigCount-1));
    QVERIFY(spyCA.count() == (addSigCount-1)        // if all of the signals are coalesced
         || spyCA.count() == (addSigCount)          // if all but one of the signals are coalesced
         || spyCA.count() == (addSigCount+1)        // if only two of the signals are coalesced
         || spyCA.count() == (addSigCount+2));      // if no signals were coalesced.

    spyCOM1->clear();
    spyCOR1->clear();
    QScopedPointer<QContactObserver> c2Observer(new QContactObserver(m1.data(), ContactId::apiId(c2)));
    QScopedPointer<QContactObserver> c3Observer(new QContactObserver(m1.data(), ContactId::apiId(c3)));
    QScopedPointer<QSignalSpy> spyCOM2(new QSignalSpy(c2Observer.data(), SIGNAL(contactChanged())));
    QScopedPointer<QSignalSpy> spyCOM3(new QSignalSpy(c3Observer.data(), SIGNAL(contactChanged())));
    QScopedPointer<QSignalSpy> spyCOR2(new QSignalSpy(c2Observer.data(), SIGNAL(contactRemoved())));
    QScopedPointer<QSignalSpy> spyCOR3(new QSignalSpy(c3Observer.data(), SIGNAL(contactRemoved())));

    // verify multiple modifies works as advertised
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&c2, &nc2, "M.");
#else
    saveContactName(&c2, nameDef, &nc2, "M.");
#endif
    QVERIFY(m1->saveContact(&c2));
    modSigCount += 1;
    if (uri.contains(QLatin1String("sqlite"))) {
        // backend coalesces signals for performance reasons, so wait a little
        QTest::qWait(1000);
    }
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&c2, &nc2, "Mark");
    saveContactName(&c3, &nc3, "G.");
#else
    saveContactName(&c2, nameDef, &nc2, "Mark");
    saveContactName(&c3, nameDef, &nc3, "G.");
#endif
    QVERIFY(m1->saveContact(&c2));
    modSigCount += 1;
    QVERIFY(m1->saveContact(&c3));
    modSigCount += 1;
    QTRY_VERIFY(spyCM.count() >= (modSigCount - 2)); // it may coalesce signals, and it may update aggregates.
    QTRY_COMPARE(spyCOM2->count(), 2);
    QTRY_COMPARE(spyCOM3->count(), 1);
    QCOMPARE(spyCOM1->count(), 0);

    // verify multiple removes works as advertised
    m1->removeContact(removalId(c3));
    remSigCount += 1;
    m1->removeContact(removalId(c2));
    remSigCount += 1;
    QTRY_VERIFY(spyCM.count() >= (remSigCount - 2)); // it may coalesce signals, and it may remove aggregates.
    QTRY_COMPARE(spyCOR2->count(), 1);
    QTRY_COMPARE(spyCOR3->count(), 1);
    QCOMPARE(spyCOR1->count(), 0);

    spyCR.clear();
    if(! uri.contains(QLatin1String("tracker"))) {
        // The tracker backend does not support checking for existance of a contact.
        QVERIFY(!m1->removeContact(removalId(c))); // not saved.
    }

    QTest::qWait(2000); // the above removeContact() might cause a delayed/coalesced signal emission.

    /* Now test the batch equivalents */
    spyCA.clear();
    spyCM.clear();
    spyCR.clear();

    /* Batch adds - set ids to zero so add succeeds. */
    c.setId(QContactId());
    c2.setId(QContactId());
    c3.setId(QContactId());
    batchAdd << c << c2 << c3;
    QMap<int, QContactManager::Error> errorMap;
    QVERIFY(m1->saveContacts(&batchAdd, &errorMap));

    QVERIFY(batchAdd.count() == 3);
    c = batchAdd.at(0);
    c2 = batchAdd.at(1);
    c3 = batchAdd.at(2);

    /* We basically loop, processing events, until we've seen an Add signal for each contact */
    sigids.clear();

    QTRY_WAIT( while(spyCA.size() > 0) {sigids += spyCA.takeFirst().at(0).value<QList<QContactId> >(); },
               sigids.contains(ContactId::apiId(c)) && sigids.contains(ContactId::apiId(c2)) && sigids.contains(ContactId::apiId(c3)));
    // if we perform aggregation, aggregates might get updated; this cannot be verified:
    //QTRY_COMPARE(spyCM.count(), 0);

    c1Observer.reset(new QContactObserver(m1.data(), ContactId::apiId(c)));
    c2Observer.reset(new QContactObserver(m1.data(), ContactId::apiId(c2)));
    c3Observer.reset(new QContactObserver(m1.data(), ContactId::apiId(c3)));
    spyCOM1.reset(new QSignalSpy(c1Observer.data(), SIGNAL(contactChanged())));
    spyCOM2.reset(new QSignalSpy(c2Observer.data(), SIGNAL(contactChanged())));
    spyCOM3.reset(new QSignalSpy(c3Observer.data(), SIGNAL(contactChanged())));
    spyCOR1.reset(new QSignalSpy(c1Observer.data(), SIGNAL(contactRemoved())));
    spyCOR2.reset(new QSignalSpy(c2Observer.data(), SIGNAL(contactRemoved())));
    spyCOR3.reset(new QSignalSpy(c3Observer.data(), SIGNAL(contactRemoved())));
    QTRY_COMPARE(spyCR.count(), 0);

    /* Batch modifies */
#ifndef DETAIL_DEFINITION_SUPPORTED
    QContactName modifiedName = c.detail<QContactName>();
    saveContactName(&c, &modifiedName, "Modified number 1");
    modifiedName = c2.detail<QContactName>();
    saveContactName(&c2, &modifiedName, "Modified number 2");
    modifiedName = c3.detail<QContactName>();
    saveContactName(&c3, &modifiedName, "Modified number 3");
#else
    QContactName modifiedName = c.detail<QContactName>();
    saveContactName(&c, nameDef, &modifiedName, "Modified number 1");
    modifiedName = c2.detail<QContactName>();
    saveContactName(&c2, nameDef, &modifiedName, "Modified number 2");
    modifiedName = c3.detail<QContactName>();
    saveContactName(&c3, nameDef, &modifiedName, "Modified number 3");
#endif

    batchAdd.clear();
    batchAdd << c << c2 << c3;
    QVERIFY(m1->saveContacts(&batchAdd, &errorMap));

    sigids.clear();
    QTRY_WAIT( while(spyCM.size() > 0) {sigids += spyCM.takeFirst().at(0).value<QList<QContactId> >(); }, sigids.contains(ContactId::apiId(c)) && sigids.contains(ContactId::apiId(c2)) && sigids.contains(ContactId::apiId(c3)));
    QTRY_COMPARE(spyCOM1->count(), 1);
    QTRY_COMPARE(spyCOM2->count(), 1);
    QTRY_COMPARE(spyCOM3->count(), 1);

    /* Batch removes */
    batchRemove << ContactId::apiId(c) << ContactId::apiId(c2) << ContactId::apiId(c3);
    QVERIFY(m1->removeContacts(batchRemove, &errorMap));

    sigids.clear();
    QTRY_WAIT( while(spyCR.size() > 0) {sigids += spyCR.takeFirst().at(0).value<QList<QContactId> >(); }, sigids.contains(ContactId::apiId(c)) && sigids.contains(ContactId::apiId(c2)) && sigids.contains(ContactId::apiId(c3)));
    QTRY_COMPARE(spyCOR1->count(), 1);
    QTRY_COMPARE(spyCOR2->count(), 1);
    QTRY_COMPARE(spyCOR3->count(), 1);

    QTRY_COMPARE(spyCA.count(), 0);
    // if we perform aggregation, removes can cause regenerates of aggregates; this cannot be verified:
    //QTRY_COMPARE(spyCM.count(), 0);

    QScopedPointer<QContactManager> m2(QContactManager::fromUri(uri));
    
    QCOMPARE(managerSupportsFeature(*m1, "Anonymous"), managerSupportsFeature(*m2, "Anonymous"));

    /* Now some cross manager testing */
    if (!managerSupportsFeature(*m1, "Anonymous")) {
        // verify that signals are emitted for modifications made to other managers (same id).
        QContactName ncs = c.detail<QContactName>();
#ifndef DETAIL_DEFINITION_SUPPORTED
        saveContactName(&c, &ncs, "Test");
#else
        saveContactName(&c, nameDef, &ncs, "Test");
#endif
        c.setId(QContactId()); // reset id so save can succeed.
        QVERIFY(m2->saveContact(&c));
#ifndef DETAIL_DEFINITION_SUPPORTED
        saveContactName(&c, &ncs, "Test2");
#else
        saveContactName(&c, nameDef, &ncs, "Test2");
#endif
        QVERIFY(m2->saveContact(&c));
        QTRY_VERIFY(spyCA.count() >= 1); // check that we received the update signals.
        QTRY_VERIFY(spyCM.count() >= 1); // check that we received the update signals.
        m2->removeContact(removalId(c));
        QTRY_VERIFY(spyCR.count() >= 1); // check that we received the remove signal.
    }
}

void tst_QContactManager::errorStayingPut()
{
    /* Make sure that when we clone a manager, we don't clone the error */
    QMap<QString, QString> params;
    params.insert("id", "error isolation test");
    QScopedPointer<QContactManager> m1(newContactManager(params));

    QVERIFY(m1->error() == QContactManager::NoError);

    /* Remove an invalid contact to get an error */
    QVERIFY(m1->removeContact(ContactId::apiId(0)) == false);
    QVERIFY(m1->error() == QContactManager::DoesNotExistError);

    /* Create a new manager with hopefully the same backend */
    QScopedPointer<QContactManager> m2(newContactManager(params));

    QVERIFY(m1->error() == QContactManager::DoesNotExistError);
    QVERIFY(m2->error() == QContactManager::NoError);

    /* Cause an error on the other ones and check the first is not affected */
    m2->saveContacts(0, 0);
    QVERIFY(m1->error() == QContactManager::DoesNotExistError);
    QVERIFY(m2->error() == QContactManager::BadArgumentError);

    QContact c;
    QContactDetail d(static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 0x100));
    d.setValue(100, "Value that also doesn't exist");
    c.saveDetail(&d);

    QVERIFY(m1->saveContact(&c) == false);
    QVERIFY(m1->error() == QContactManager::InvalidDetailError);
    QVERIFY(m2->error() == QContactManager::BadArgumentError);
}

#ifdef DETAIL_DEFINITION_SUPPORTED
void tst_QContactManager::validateDefinitions(const QMap<QString, QContactDetailDefinition>& defs) const
{

    // Do some sanity checking on the definitions first
    if (defs.keys().count() != defs.uniqueKeys().count()) {
        qDebug() << "ERROR - duplicate definitions with the same name:";

        QList<QString> defkeys = defs.keys();
        foreach(QString uniq, defs.uniqueKeys()) {
            if (defkeys.count(uniq) > 1) {
                qDebug() << QString(" %1").arg(uniq).toAscii().constData();
                defkeys.removeAll(uniq);
            }
        }
        QVERIFY(defs.keys().count() == defs.uniqueKeys().count());
    }

    foreach(QContactDetailDefinition def, defs.values()) {
        QMap<QString, QContactDetailFieldDefinition> fields = def.fields();

        // Again some sanity checking
        if (fields.keys().count() != fields.uniqueKeys().count()) {
            qDebug() << "ERROR - duplicate fields with the same name:";

            QList<QString> defkeys = fields.keys();
            foreach(QString uniq, fields.uniqueKeys()) {
                if (defkeys.count(uniq) > 1) {
                    qDebug() << QString(" %2::%1").arg(uniq).arg(def.name()).toAscii().constData();
                    defkeys.removeAll(uniq);
                }
            }
            QVERIFY(fields.keys().count() == fields.uniqueKeys().count());
        }

        foreach(QContactDetailFieldDefinition field, def.fields().values()) {
            // Sanity check the allowed values
            if (field.allowableValues().count() > 0) {
                if (field.dataType() == QVariant::StringList) {
                    // We accept QString or QStringList allowed values
                    foreach(QVariant var, field.allowableValues()) {
                        if (var.type() != QVariant::String && var.type() != QVariant::StringList) {
                            QString foo;
                            QDebug dbg(&foo);
                            dbg.nospace() << var;
                            qDebug().nospace() << "Field " << QString("%1::%2").arg(def.name()).arg(def.fields().key(field)).toAscii().constData() << " allowable value '" << foo.simplified().toAscii().constData() << "' not supported for field type " << QMetaType::typeName(field.dataType());
                        }
                        QVERIFY(var.type() == QVariant::String || var.type() == QVariant::StringList);
                    }
                } else if (field.dataType() == QVariant::List || field.dataType() == QVariant::Map || field.dataType() == (QVariant::Type) qMetaTypeId<QVariant>()) {
                    // Well, anything goes
                } else {
                    // The type of each allowed value must match the data type
                    foreach(QVariant var, field.allowableValues()) {
                        if (var.type() != field.dataType()) {
                            QString foo;
                            QDebug dbg(&foo);
                            dbg.nospace() << var;
                            qDebug().nospace() << "Field " << QString("%1::%2").arg(def.name()).arg(def.fields().key(field)).toAscii().constData() << " allowable value '" << foo.simplified().toAscii().constData() << "' not supported for field type " << QMetaType::typeName(field.dataType());
                        }
                        QVERIFY(var.type() == field.dataType());
                    }
                }
            }
        }
    }
}
#endif

#ifdef MUTABLE_SCHEMA_SUPPORTED
void tst_QContactManager::engineDefaultSchema()
{
    /* Test the default schemas - mostly just that they are valid, and v2 has certain changes */
    QMap<QString, QMap<QString, QContactDetailDefinition> > v1defaultSchemas = QContactManagerEngine::schemaDefinitions();
    QMap<QString, QMap<QString, QContactDetailDefinition> > v1Schemas = QContactManagerEngine::schemaDefinitions(1);
    QMap<QString, QMap<QString, QContactDetailDefinition> > v2Schemas = QContactManagerEngine::schemaDefinitions(2);

    QVERIFY(v1Schemas == v1defaultSchemas);
    QVERIFY(v1Schemas != v2Schemas);

    QCOMPARE(v1Schemas.keys().count(), v1Schemas.uniqueKeys().count());
    QCOMPARE(v2Schemas.keys().count(), v2Schemas.uniqueKeys().count());

    foreach(const QString& type, v1Schemas.keys()) {
        validateDefinitions(v1Schemas.value(type));
    }

    foreach(const QString& type, v2Schemas.keys()) {
        validateDefinitions(v2Schemas.value(type));
    }

    /* Make sure that birthdays do not have calendar ids in v1, but do in v2*/
    QVERIFY(!v1Schemas.value(QContactType::TypeContact).value(QContactBirthday::DefinitionName).fields().contains(QContactBirthday::FieldCalendarId));
    QVERIFY(!v1Schemas.value(QContactType::TypeGroup).value(QContactBirthday::DefinitionName).fields().contains(QContactBirthday::FieldCalendarId));
    QVERIFY(v2Schemas.value(QContactType::TypeContact).value(QContactBirthday::DefinitionName).fields().contains(QContactBirthday::FieldCalendarId));
    QVERIFY(v2Schemas.value(QContactType::TypeGroup).value(QContactBirthday::DefinitionName).fields().contains(QContactBirthday::FieldCalendarId));

    /* Urls can be blogs in v2 but not b1 */
    QVERIFY(!v1Schemas.value(QContactType::TypeContact).value(QContactUrl::DefinitionName).fields().value(QContactUrl::FieldSubType).allowableValues().contains(QString(QLatin1String(QContactUrl::SubTypeBlog))));
    QVERIFY(!v1Schemas.value(QContactType::TypeGroup).value(QContactUrl::DefinitionName).fields().value(QContactUrl::FieldSubType).allowableValues().contains(QString(QLatin1String(QContactUrl::SubTypeBlog))));
    QVERIFY(v2Schemas.value(QContactType::TypeContact).value(QContactUrl::DefinitionName).fields().value(QContactUrl::FieldSubType).allowableValues().contains(QString(QLatin1String(QContactUrl::SubTypeBlog))));
    QVERIFY(v2Schemas.value(QContactType::TypeGroup).value(QContactUrl::DefinitionName).fields().value(QContactUrl::FieldSubType).allowableValues().contains(QString(QLatin1String(QContactUrl::SubTypeBlog))));

    /* Make sure family, favorite and hobby are not in v1, but are in v2 */
    QVERIFY(!v1Schemas.value(QContactType::TypeContact).contains(QContactFamily::DefinitionName));
    QVERIFY(!v1Schemas.value(QContactType::TypeGroup).contains(QContactFamily::DefinitionName));
    QVERIFY(v2Schemas.value(QContactType::TypeContact).contains(QContactFamily::DefinitionName));
    QVERIFY(v2Schemas.value(QContactType::TypeGroup).contains(QContactFamily::DefinitionName));

    QVERIFY(!v1Schemas.value(QContactType::TypeContact).contains(QContactFavorite::DefinitionName));
    QVERIFY(!v1Schemas.value(QContactType::TypeGroup).contains(QContactFavorite::DefinitionName));
    QVERIFY(v2Schemas.value(QContactType::TypeContact).contains(QContactFavorite::DefinitionName));
    QVERIFY(v2Schemas.value(QContactType::TypeGroup).contains(QContactFavorite::DefinitionName));

    QVERIFY(!v1Schemas.value(QContactType::TypeContact).contains(QContactHobby::DefinitionName));
    QVERIFY(!v1Schemas.value(QContactType::TypeGroup).contains(QContactHobby::DefinitionName));
    QVERIFY(v2Schemas.value(QContactType::TypeContact).contains(QContactHobby::DefinitionName));
    QVERIFY(v2Schemas.value(QContactType::TypeGroup).contains(QContactHobby::DefinitionName));
}
#endif

#ifdef DETAIL_DEFINITION_SUPPORTED
void tst_QContactManager::detailDefinitions()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));
    QMap<QString, QContactDetailDefinition> defs = cm->detailDefinitions();

    /* Validate the existing definitions */
    foreach(const QString& contactType, cm->supportedContactTypes()) {
        validateDefinitions(cm->detailDefinitions(contactType));
    }

    /* Try to make a credible definition */
    QContactDetailDefinition newDef;
    QContactDetailFieldDefinition field;
    QMap<QString, QContactDetailFieldDefinition> fields;
    field.setDataType(cm->supportedDataTypes().value(0));
    fields.insert("New Value", field);
    newDef.setName("New Definition");
    newDef.setFields(fields);

    /* Updated version of an existing definition */
    QContactDetailDefinition updatedDef = defs.begin().value(); // XXX TODO Fixme
    fields = updatedDef.fields();
    fields.insert("New Value", field);
    updatedDef.setFields(fields);

    /* A detail definition with valid allowed values (or really just one) */
    QContactDetailDefinition allowedDef = newDef;
    field.setAllowableValues(field.allowableValues() << (QVariant(field.dataType())));
    fields.clear();
    fields.insert("Restricted value", field);
    allowedDef.setFields(fields);

    /* Many invalid definitions */
    QContactDetailDefinition noIdDef;
    noIdDef.setFields(fields);

    QContactDetailDefinition noFieldsDef;
    noFieldsDef.setName("No fields");

    QContactDetailDefinition invalidFieldKeyDef;
    invalidFieldKeyDef.setName("Invalid field key");
    QMap<QString, QContactDetailFieldDefinition> badfields;
    badfields.insert(QString(), field);
    invalidFieldKeyDef.setFields(badfields);

    QContactDetailDefinition invalidFieldTypeDef;
    invalidFieldTypeDef.setName("Invalid field type");
    badfields.clear();
    QContactDetailFieldDefinition badfield;
    badfield.setDataType((QVariant::Type) qMetaTypeId<UnsupportedMetatype>());
    badfields.insert("Bad type", badfield);
    invalidFieldTypeDef.setFields(badfields);

    QContactDetailDefinition invalidAllowedValuesDef;
    invalidAllowedValuesDef.setName("Invalid field allowed values");
    badfields.clear();
    badfield.setDataType(field.dataType()); // use a supported type
    badfield.setAllowableValues(QList<QVariant>() << "String" << 5); // but unsupported value
    badfields.insert("Bad allowed", badfield);
    invalidAllowedValuesDef.setFields(badfields);

    /* XXX Multiply defined fields.. depends on semantichangeSet. */

    if (managerSupportsFeature(*cm, "MutableDefinitions")) {
        /* First do some negative testing */

        /* Bad add class */
        QVERIFY(cm->saveDetailDefinition(QContactDetailDefinition()) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        /* Bad remove string */
        QVERIFY(cm->removeDetailDefinition(QString()) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        QVERIFY(cm->saveDetailDefinition(noIdDef) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        QVERIFY(cm->saveDetailDefinition(noFieldsDef) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        QVERIFY(cm->saveDetailDefinition(invalidFieldKeyDef) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        QVERIFY(cm->saveDetailDefinition(invalidFieldTypeDef) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        QVERIFY(cm->saveDetailDefinition(invalidAllowedValuesDef) == false);
        QVERIFY(cm->error() == QContactManager::BadArgumentError);

        /* Check that our new definition doesn't already exist */
        QVERIFY(cm->detailDefinition(newDef.name()).isEmpty());
        QVERIFY(cm->error() == QContactManager::DoesNotExistError);

        QVERIFY(cm->removeDetailDefinition(newDef.name()) == false);
        QVERIFY(cm->error() == QContactManager::DoesNotExistError);

        /* Add a new definition */
        QVERIFY(cm->saveDetailDefinition(newDef) == true);
        QVERIFY(cm->error() == QContactManager::NoError);

        /* Now retrieve it */
        QContactDetailDefinition def = cm->detailDefinition(newDef.name());
        QVERIFY(def == newDef);

        /* Update it */
        QMap<QString, QContactDetailFieldDefinition> newFields = def.fields();
        newFields.insert("Another new value", field);
        newDef.setFields(newFields);

        QVERIFY(cm->saveDetailDefinition(newDef) == true);
        QVERIFY(cm->error() == QContactManager::NoError);

        QVERIFY(cm->detailDefinition(newDef.name()) == newDef);

        /* Remove it */
        QVERIFY(cm->removeDetailDefinition(newDef.name()) == true);
        QVERIFY(cm->error() == QContactManager::NoError);

        /* and make sure it does not exist any more */
        QVERIFY(cm->detailDefinition(newDef.name()) == QContactDetailDefinition());
        QVERIFY(cm->error() == QContactManager::DoesNotExistError);

        /* Add the other good one */
        QVERIFY(cm->saveDetailDefinition(allowedDef) == true);
        QVERIFY(cm->error() == QContactManager::NoError);

        QVERIFY(allowedDef == cm->detailDefinition(allowedDef.name()));

        /* and remove it */
        QVERIFY(cm->removeDetailDefinition(allowedDef.name()) == true);
        QVERIFY(cm->detailDefinition(allowedDef.name()) == QContactDetailDefinition());
        QVERIFY(cm->error() == QContactManager::DoesNotExistError);

    } else {
        /* Bad add class */
        QVERIFY(cm->saveDetailDefinition(QContactDetailDefinition()) == false);
        QVERIFY(cm->error() == QContactManager::NotSupportedError);

        /* Make sure we can't add/remove/modify detail definitions */
        QVERIFY(cm->removeDetailDefinition(QString()) == false);
        QVERIFY(cm->error() == QContactManager::NotSupportedError);

        /* Try updating an existing definition */
        QVERIFY(cm->saveDetailDefinition(updatedDef) == false);
        QVERIFY(cm->error() == QContactManager::NotSupportedError);

        /* Try removing an existing definition */
        QVERIFY(cm->removeDetailDefinition(updatedDef.name()) == false);
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
    }
}
#endif

#ifdef DISPLAY_LABEL_SUPPORTED
void tst_QContactManager::displayName()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    /*
     * Very similar to the tst_QContact functions, except we test
     * saving and retrieving contacts updates the display label
     */

    /* Try to make this a bit more consistent by using a single name */
    QContact d;
    QContactName name;
    saveContactName(&d, cm->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact), &name, "Wesley");

    QVERIFY(d.displayLabel().isEmpty());

    QString synth = cm->synthesizedContactDisplayLabel(d);

    // Make sure this doesn't crash
    cm->synthesizeContactDisplayLabel(0);

    // Make sure this gives the same results
    cm->synthesizeContactDisplayLabel(&d);
    QCOMPARE(d.displayLabel(), synth);

    /*
     * The display label is not updated until you save the contact or call synthCDL
     */
    QVERIFY(cm->saveContact(&d));
    d = cm->contact(retrievalId(d));
    QVERIFY(!d.isEmpty());
    QCOMPARE(d.displayLabel(), synth);

    /* Remove the detail via removeDetail */
    QContactDisplayLabel old;
    int count = d.details().count();
    QVERIFY(!d.removeDetail(&old)); // should fail.
    QVERIFY(d.isEmpty() == false);
    QVERIFY(d.details().count() == count); // it should not be removed!

    /* Save the contact again */
    QVERIFY(cm->saveContact(&d));
    d = cm->contact(retrievalId(d));
    QVERIFY(!d.isEmpty());

    /* Make sure the label is still the synth version */
    QCOMPARE(d.displayLabel(), synth);

    /* And delete the contact */
    QVERIFY(cm->removeContact(removalId(d)));
}
#endif

void tst_QContactManager::actionPreferences()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    // early out if the manager doesn't support action preference saving.
    if (!managerSupportsFeature(*cm, "ActionPreferences")) {
        QSKIP("Manager does not support action preferences");
    }

    // create a sample contact
    QContactAvatar a;
    a.setImageUrl(QUrl("test.png"));
    QContactPhoneNumber p1;
    p1.setNumber("12345");
    QContactPhoneNumber p2;
    p2.setNumber("34567");
    QContactPhoneNumber p3;
    p3.setNumber("56789");
    QContactUrl u;
    u.setUrl("http://test.example.com");
    QContactName n;
    QContact c;
#ifndef DETAIL_DEFINITION_SUPPORTED
    saveContactName(&c, &n, "TestContact");
#else
    saveContactName(&c, cm->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact), &n, "TestContact");
#endif
    c.saveDetail(&a);
    c.saveDetail(&p1);
    c.saveDetail(&p2);
    c.saveDetail(&p3);
    c.saveDetail(&u);

    // set a preference for dialing a particular saved phonenumber.
    c.setPreferredDetail("Dial", p2);

    QVERIFY(cm->saveContact(&c));          // save the contact
    QContact loaded = cm->contact(retrievalId(c)); // reload the contact

    // test that the preference was saved correctly.
    QContactDetail pref = loaded.preferredDetail("Dial");
    QCOMPARE(pref, static_cast<QContactDetail &>(p2));

    cm->removeContact(removalId(c));
}

void tst_QContactManager::changeSet()
{
    QContactId id(ContactId::apiId(1));

    QContactChangeSet changeSet;
    QVERIFY(changeSet.addedContacts().isEmpty());
    QVERIFY(changeSet.changedContacts().isEmpty());
    QVERIFY(changeSet.removedContacts().isEmpty());

    changeSet.insertAddedContact(id);
    QVERIFY(!changeSet.addedContacts().isEmpty());
    QVERIFY(changeSet.changedContacts().isEmpty());
    QVERIFY(changeSet.removedContacts().isEmpty());
    QVERIFY(changeSet.addedContacts().contains(id));

    changeSet.insertChangedContact(id);
    changeSet.insertChangedContacts(QList<QContactId>() << id);
    QVERIFY(changeSet.changedContacts().size() == 1); // set, should only be added once.
    QVERIFY(!changeSet.addedContacts().isEmpty());
    QVERIFY(!changeSet.changedContacts().isEmpty());
    QVERIFY(changeSet.removedContacts().isEmpty());
    QVERIFY(changeSet.changedContacts().contains(id));
    changeSet.clearChangedContacts();
    QVERIFY(changeSet.changedContacts().isEmpty());

    changeSet.insertRemovedContacts(QList<QContactId>() << id);
    QVERIFY(changeSet.removedContacts().contains(id));
    changeSet.clearRemovedContacts();
    QVERIFY(changeSet.removedContacts().isEmpty());

    QVERIFY(changeSet.dataChanged() == false);
    QContactChangeSet changeSet2;
    changeSet2 = changeSet;
    QVERIFY(changeSet.addedContacts() == changeSet2.addedContacts());
    changeSet.emitSignals(0);

    changeSet2.clearAddedContacts();
    QVERIFY(changeSet2.addedContacts().isEmpty());
    changeSet2.insertAddedContacts(changeSet.addedContacts().toList());
    QVERIFY(changeSet.addedContacts() == changeSet2.addedContacts());

    changeSet2.clearAll();
    QVERIFY(changeSet.addedContacts() != changeSet2.addedContacts());

    QContactChangeSet changeSet3(changeSet2);
    QVERIFY(changeSet.addedContacts() != changeSet3.addedContacts());
    QVERIFY(changeSet2.addedContacts() == changeSet3.addedContacts());

    changeSet.setDataChanged(true);
    QVERIFY(changeSet.dataChanged() == true);
    QVERIFY(changeSet.dataChanged() != changeSet2.dataChanged());
    QVERIFY(changeSet.dataChanged() != changeSet3.dataChanged());
    changeSet.emitSignals(0);

    changeSet.addedRelationshipsContacts().insert(id);
    changeSet.insertAddedRelationshipsContacts(QList<QContactId>() << id);
    QVERIFY(changeSet.addedRelationshipsContacts().contains(id));
    changeSet.clearAddedRelationshipsContacts();
    QVERIFY(changeSet.addedRelationshipsContacts().isEmpty());
    changeSet.insertRemovedRelationshipsContacts(QList<QContactId>() << id);
    QVERIFY(changeSet.removedRelationshipsContacts().contains(id));
    changeSet.clearRemovedRelationshipsContacts();
    QVERIFY(changeSet.removedRelationshipsContacts().isEmpty());
    changeSet.emitSignals(0);
    changeSet.removedRelationshipsContacts().insert(id);
    changeSet.emitSignals(0);

    changeSet.setOldAndNewSelfContactId(QPair<QContactId, QContactId>(QContactId(0), id));
    changeSet2 = changeSet;
    QVERIFY(changeSet2.addedRelationshipsContacts() == changeSet.addedRelationshipsContacts());
    QVERIFY(changeSet2.removedRelationshipsContacts() == changeSet.removedRelationshipsContacts());
    QVERIFY(changeSet2.oldAndNewSelfContactId() == changeSet.oldAndNewSelfContactId());
    changeSet.emitSignals(0);
    changeSet.setOldAndNewSelfContactId(QPair<QContactId, QContactId>(id, QContactId(0)));
    QVERIFY(changeSet2.oldAndNewSelfContactId() != changeSet.oldAndNewSelfContactId());
    changeSet.setDataChanged(true);
    changeSet.emitSignals(0);
}

void tst_QContactManager::fetchHint()
{
    // This just tests the accessors and mutators (API).
    // See tst_qcontactmanagerfiltering for the "backend support" test.
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::NoBinaryBlobs);
    QCOMPARE(hint.optimizationHints(), QContactFetchHint::NoBinaryBlobs);

    QStringList rels;
    rels << relationshipString(QContactRelationship::HasMember);
    hint.setRelationshipTypesHint(rels);
    QCOMPARE(hint.relationshipTypesHint(), rels);

    QList<QContactDetail::DetailType> defs;
    defs << QContactName::Type
         << QContactPhoneNumber::Type;
    hint.setDetailTypesHint(defs);
    QCOMPARE(hint.detailTypesHint(), defs);

    QSize prefImageSize(33, 33);
    hint.setPreferredImageSize(prefImageSize);
    QCOMPARE(hint.preferredImageSize(), prefImageSize);

    int limit = 15;
    hint.setMaxCountHint(limit);
    QCOMPARE(hint.maxCountHint(), limit);
}

void tst_QContactManager::selfContactId()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    // early out if the manager doesn't support self contact id saving
    QContactId selfContact = cm->selfContactId();

    if (!managerSupportsFeature(*cm, "SelfContact")) {
        // ensure that the error codes / return values are meaningful failures.
        QEXPECT_FAIL("mgr='maemo5'", "maemo5 supports getting the self contact but not setting it.", Continue);
        QEXPECT_FAIL("mgr='org.nemomobile.contacts.sqlite'", "qtcontacts-sqlite supports getting the self contact but not setting it.", Continue);
        QVERIFY(cm->error() == QContactManager::DoesNotExistError);
        QVERIFY(!cm->setSelfContactId(ContactId::apiId(123)));
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        QSKIP("Manager does not support the concept of a self-contact");
    }

    // create a new "self" contact and retrieve its Id
    QVERIFY(cm->error() == QContactManager::NoError || cm->error() == QContactManager::DoesNotExistError);
    QContact self;
    QContactPhoneNumber selfPhn;
    selfPhn.setNumber("12345");
    self.saveDetail(&selfPhn);
    if (!cm->saveContact(&self)) {
        QSKIP("Unable to save the generated self contact");
    }

    QContactId newSelfContact = ContactId::apiId(self);

    // Setup signal spy
    QSignalSpy spy(cm.data(), selfContactIdChangedSignal);
    QTestSignalSink sink(cm.data(), selfContactIdChangedSignal);

    // Set new self contact
    QVERIFY(cm->setSelfContactId(newSelfContact));
    QCOMPARE(cm->error(), QContactManager::NoError);
    QTRY_VERIFY(spy.count() == 1);
    QCOMPARE(spy.at(0).count(), 2);
    QCOMPARE(spy.at(0).at(0), QVariant::fromValue(selfContact));
    QCOMPARE(spy.at(0).at(1), QVariant::fromValue(newSelfContact));
    QCOMPARE(cm->selfContactId(), newSelfContact);

    // Remove self contact
    if(cm->removeContact(removalId(self))) {
        QTRY_VERIFY(spy.count() == 2);
        QCOMPARE(spy.at(1).count(), 2);
        QCOMPARE(spy.at(1).at(0), QVariant::fromValue(newSelfContact));
        QCOMPARE(spy.at(1).at(1), QVariant::fromValue(QContactId()));
        QCOMPARE(cm->selfContactId(), QContactId()); // ensure reset after removed.

        // reset to original state.
        cm->setSelfContactId(selfContact);
    }
}

QList<QContactDetail> tst_QContactManager::removeAllDefaultDetails(const QList<QContactDetail>& details)
{
    QList<QContactDetail> newlist;
    foreach (const QContactDetail d, details) {
        if (detailType(d) != detailType<QContactDisplayLabel>()
                && detailType(d) != detailType<QContactType>()
                && detailType(d) != detailType<QContactTimestamp>()) {
            newlist << d;
        }
    }
    return newlist;
}

void tst_QContactManager::detailOrders()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    if (!managerSupportsFeature(*cm, "DetailOrdering"))
        QSKIP("Skipping: This manager does not support detail ordering!");

    QContact a;
    QContactDetail::DetailContext contextOther(QContactDetail::ContextOther);

    //phone numbers
    {

#ifdef DETAIL_DEFINITION_SUPPORTED
    d = cm->detailDefinition(QContactPhoneNumber::DefinitionName, QContactType::TypeContact);
    supportedContexts = d.fields().value(QContactDetail::FieldContext);
    contextOther = QString(QLatin1String(QContactDetail::ContextOther));
    if (!supportedContexts.allowableValues().contains(contextOther)) {
        contextOther = QString();
    }    
#endif
    
    QContactPhoneNumber number1, number2, number3;
    
    number1.setNumber("11111111");
    number1.setContexts(QContactPhoneNumber::ContextHome);

    number2.setNumber("22222222");
    number2.setContexts(QContactPhoneNumber::ContextWork);

    number3.setNumber("33333333");
    number3.setContexts(QContactPhoneNumber::ContextOther);
    number3.setContexts(contextOther);

    a.saveDetail(&number1);
    a.saveDetail(&number2);
    a.saveDetail(&number3);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    
    QList<QContactPhoneNumber> details = a.details<QContactPhoneNumber>();
    QVERIFY(details.count() == 3);
    QVERIFY(details.at(0).value(QContactPhoneNumber::FieldContext) == QContactPhoneNumber::ContextHome);
    QVERIFY(details.at(1).value(QContactPhoneNumber::FieldContext) == QContactPhoneNumber::ContextWork);
    QVERIFY(details.at(2).value(QContactPhoneNumber::FieldContext) == contextOther);
    
    QVERIFY(a.removeDetail(&number2));
    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    details = a.details<QContactPhoneNumber>();
    QVERIFY(details.count() == 2);
    QVERIFY(details.at(0).value(QContactPhoneNumber::FieldContext) == QContactPhoneNumber::ContextHome);
    QVERIFY(details.at(1).value(QContactPhoneNumber::FieldContext) == contextOther);

    a.saveDetail(&number2);
    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    
    details = a.details<QContactPhoneNumber>();
    QVERIFY(details.count() == 3);
    QVERIFY(details.at(0).value(QContactPhoneNumber::FieldContext) == QContactPhoneNumber::ContextHome);
    QVERIFY(details.at(1).value(QContactPhoneNumber::FieldContext) == contextOther);
    QVERIFY(details.at(2).value(QContactPhoneNumber::FieldContext) == QContactPhoneNumber::ContextWork);

    }
    //addresses
    {
    
#ifdef DETAIL_DEFINITION_SUPPORTED
    d = cm->detailDefinition(QContactAddress::DefinitionName, QContactType::TypeContact);
    supportedContexts = d.fields().value(QContactDetail::FieldContext);
    contextOther = QString(QLatin1String(QContactDetail::ContextOther));
    if (!supportedContexts.allowableValues().contains(contextOther)) {
        contextOther = QString();
    }     
#endif
    
    QContactAddress address1, address2, address3;
    
    address1.setStreet("Brandl St");
    address1.setRegion("Brisbane");
    address3 = address2 = address1;

    address1.setContexts(QContactAddress::ContextHome);
    address2.setContexts(QContactAddress::ContextWork);
    address3.setContexts(contextOther);

    a.saveDetail(&address1);
    a.saveDetail(&address2);
    a.saveDetail(&address3);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    
    QList<QContactAddress> details = a.details<QContactAddress>();
    QVERIFY(details.count() == 3);
    
    QVERIFY(details.at(0).value(QContactAddress::FieldContext) == QContactAddress::ContextHome);
    QVERIFY(details.at(1).value(QContactAddress::FieldContext) == QContactAddress::ContextWork);
    QVERIFY(details.at(2).value(QContactAddress::FieldContext) == contextOther);

    QVERIFY(a.removeDetail(&address2));
    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    details = a.details<QContactAddress>();
    QVERIFY(details.count() == 2);
    QVERIFY(details.at(0).value(QContactAddress::FieldContext) == QContactAddress::ContextHome);
    QVERIFY(details.at(1).value(QContactAddress::FieldContext) == contextOther);

    a.saveDetail(&address2);
    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    
    details = a.details<QContactAddress>();
    QVERIFY(details.count() == 3);
    QVERIFY(details.at(0).value(QContactAddress::FieldContext) == QContactAddress::ContextHome);
    QVERIFY(details.at(1).value(QContactAddress::FieldContext) == contextOther);
    QVERIFY(details.at(2).value(QContactAddress::FieldContext) == QContactAddress::ContextWork);


    }
    //emails
    {

#ifdef DETAIL_DEFINITION_SUPPORTED
    d = cm->detailDefinition(QContactEmailAddress::DefinitionName, QContactType::TypeContact);
    supportedContexts = d.fields().value(QContactDetail::FieldContext);
    contextOther = QString(QLatin1String(QContactDetail::ContextOther));
    if (!supportedContexts.allowableValues().contains(contextOther)) {
        contextOther = QString();
    }      
#endif
    
    QContactEmailAddress email1, email2, email3;

    email1.setEmailAddress("aaron@example.com");
    email3 = email2 = email1;
    email1.setContexts(QContactEmailAddress::ContextHome);
    email2.setContexts(QContactEmailAddress::ContextWork);
    email3.setContexts(contextOther);

    a.saveDetail(&email1);
    a.saveDetail(&email2);
    a.saveDetail(&email3);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    
    QList<QContactEmailAddress> details = a.details<QContactEmailAddress>();
    QVERIFY(details.count() == 3);
    
    QVERIFY(details.at(0).value(QContactEmailAddress::FieldContext) == QContactEmailAddress::ContextHome);
    QVERIFY(details.at(1).value(QContactEmailAddress::FieldContext) == QContactEmailAddress::ContextWork);
    QVERIFY(details.at(2).value(QContactEmailAddress::FieldContext) == contextOther);

    QVERIFY(a.removeDetail(&email2));
    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    details = a.details<QContactEmailAddress>();
    QVERIFY(details.count() == 2);
    QVERIFY(details.at(0).value(QContactEmailAddress::FieldContext) == QContactEmailAddress::ContextHome);
    QVERIFY(details.at(1).value(QContactEmailAddress::FieldContext) == contextOther);

    a.saveDetail(&email2);
    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));
    
    details = a.details<QContactEmailAddress>();
    QVERIFY(details.count() == 3);
    QVERIFY(details.at(0).value(QContactEmailAddress::FieldContext) == QContactEmailAddress::ContextHome);
    QVERIFY(details.at(1).value(QContactEmailAddress::FieldContext) == contextOther);
    QVERIFY(details.at(2).value(QContactEmailAddress::FieldContext) == QContactEmailAddress::ContextWork);

    QVERIFY(cm->removeContact(removalId(a)));
    QVERIFY(cm->error() == QContactManager::NoError);

    }
}

void tst_QContactManager::relationships()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    // save some contacts
    QContact source;
    QContact dest1, dest2, dest3, dest4;
    QContactPhoneNumber n1, n2, n3, n4;
    n1.setNumber("1");
    n2.setNumber("2");
    n3.setNumber("3");
    n4.setNumber("4");

    dest1.saveDetail(&n1);
    dest2.saveDetail(&n2);
    dest3.saveDetail(&n3);
    dest4.saveDetail(&n3);

    cm->saveContact(&source);
    cm->saveContact(&dest1);
    cm->saveContact(&dest2);
    cm->saveContact(&dest3);
    cm->saveContact(&dest4);

    // check if manager supports relationships
    if (!managerSupportsFeature(*cm, "Relationships")) {
        // ensure that the operations all fail as required.
        QContactRelationship r1, r2, r3;
        r1 = makeRelationship(QContactRelationship::HasManager, source.id(), dest1.id());
        r2 = makeRelationship(QContactRelationship::HasManager, source.id(), dest2.id());
        r3 = makeRelationship(QContactRelationship::HasManager, source.id(), dest3.id());

        QList<QContactRelationship> batchList;
        batchList << r2 << r3;

        // test save and remove
        QVERIFY(!cm->saveRelationship(&r1));
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        QVERIFY(!cm->removeRelationship(r1));
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        cm->saveRelationships(&batchList, NULL);
        QVERIFY(cm->error() == QContactManager::NotSupportedError);

        // test retrieval
        QList<QContactRelationship> retrieveList;
        retrieveList = cm->relationships(relatedContact(source), QContactRelationship::First);
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        retrieveList = cm->relationships(relatedContact(source), QContactRelationship::Second);
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        retrieveList = cm->relationships(relatedContact(source), QContactRelationship::Either); // Either
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);


        retrieveList = cm->relationships(relationshipString(QContactRelationship::HasManager), relatedContact(source), QContactRelationship::First);
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        retrieveList = cm->relationships(relationshipString(QContactRelationship::HasManager), relatedContact(source), QContactRelationship::Second);
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        retrieveList = cm->relationships(relationshipString(QContactRelationship::HasManager), relatedContact(source), QContactRelationship::Either);
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        retrieveList = cm->relationships(relationshipString(QContactRelationship::HasManager), relatedContact(source));
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        retrieveList = cm->relationships(relationshipString(QContactRelationship::HasManager));
        QVERIFY(retrieveList.isEmpty());
        QVERIFY(cm->error() == QContactManager::NotSupportedError);
        return;
    }
    
    // Get supported relationship types
    QStringList availableRelationshipTypes;
    if (cm->isRelationshipTypeSupported(relationshipString(QContactRelationship::HasMember)))
        availableRelationshipTypes << relationshipString(QContactRelationship::HasMember);
    if (cm->isRelationshipTypeSupported(relationshipString(QContactRelationship::HasAssistant)))
        availableRelationshipTypes << relationshipString(QContactRelationship::HasAssistant);
    if (cm->isRelationshipTypeSupported(relationshipString(QContactRelationship::HasManager)))
        availableRelationshipTypes << relationshipString(QContactRelationship::HasManager);
    if (cm->isRelationshipTypeSupported(relationshipString(QContactRelationship::HasSpouse)))
        availableRelationshipTypes << relationshipString(QContactRelationship::HasSpouse);
    if (cm->isRelationshipTypeSupported(relationshipString(QContactRelationship::IsSameAs)))
        availableRelationshipTypes << relationshipString(QContactRelationship::IsSameAs);

    
    // Check arbitrary relationship support
    if (managerSupportsFeature(*cm, "ArbitraryRelationshipTypes")) {
        // add some arbitrary type for testing
        if (availableRelationshipTypes.count())
            availableRelationshipTypes.insert(0, "test-arbitrary-relationship-type");
        else {
            availableRelationshipTypes.append("test-arbitrary-relationship-type");
            availableRelationshipTypes.append(relationshipString(QContactRelationship::HasMember));
            availableRelationshipTypes.append(relationshipString(QContactRelationship::HasAssistant));
        }
    }
    
    // Verify that we have relationship types. If there are none then the manager
    // is saying it supports relationships but does not actually implement any 
    // relationship type.
    QVERIFY(!availableRelationshipTypes.isEmpty());
    
    // Some backends (eg. symbian) require that when type is "HasMember" 
    // then "first" contact must be a group.
    if (availableRelationshipTypes.at(0) == relationshipString(QContactRelationship::HasMember)) {
        cm->removeContact(removalId(source));
        source.setId(QContactId());
        source.setType(QContactType::TypeGroup);
        cm->saveContact(&source);
    }

    // Create some common contact id's for testing
    QContactId dest1Uri = dest1.id();
    QContactId dest2Uri = dest2.id();
    QContactId dest3Uri = dest3.id();

    QContactId dest1EmptyUri = dest1.id();
    QContactId dest3EmptyUri = dest3.id();

    // build our relationship - source is the manager all of the dest contacts.
    QContactRelationship customRelationshipOne;
    customRelationshipOne = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest1EmptyUri);
    QVERIFY(customRelationshipOne.relationshipType() == availableRelationshipTypes.at(0));

    // save the relationship
    int managerRelationshipsCount = cm->relationships(availableRelationshipTypes.at(0)).count();
    QVERIFY(cm->saveRelationship(&customRelationshipOne));

    // test our accessors.
    QCOMPARE(cm->relationships(availableRelationshipTypes.at(0)).count(), (managerRelationshipsCount + 1));
    QVERIFY(cm->relationships(availableRelationshipTypes.at(0), relatedContact(source)).count() == 1);

    // remove the dest1 contact, relationship should be removed.
    QContact copy1(dest1);
    cm->removeContact(removalId(dest1));
    QCOMPARE(cm->relationships(availableRelationshipTypes.at(0), relatedContact(copy1), QContactRelationship::Second).count(), 0);

    // modify and save the relationship
    customRelationshipOne = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest2Uri);
    QVERIFY(cm->saveRelationship(&customRelationshipOne));

    // attempt to save the relationship again.  XXX TODO: what should the result be?  currently succeeds (overwrites)
    int relationshipsCount = cm->relationships().count();
    QVERIFY(cm->saveRelationship(&customRelationshipOne));    // succeeds, but just overwrites
    QCOMPARE(relationshipsCount, cm->relationships().count()); // shouldn't change; save should have overwritten.

    // removing the source contact should result in removal of the relationship.
    QVERIFY(cm->removeContact(removalId(source)));
    QCOMPARE(cm->relationships().count(), relationshipsCount - 2); // the relationship should have been removed, as well as an Aggregates.

    // now ensure that qcontact relationship caching works as required - perhaps this should be in tst_QContact?
    source.setId(QContactId());         // reset id so we can resave
    QVERIFY(cm->saveContact(&source));  // save source again.
    customRelationshipOne = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest2.id());
    QVERIFY(cm->saveRelationship(&customRelationshipOne));

    // Add a second relationship
    QContactRelationship customRelationshipTwo;
    int reltype = availableRelationshipTypes.count() > 1 ? 1 : 0;
    customRelationshipTwo = makeRelationship(availableRelationshipTypes.at(reltype), source.id(), dest3.id());
    QVERIFY(cm->saveRelationship(&customRelationshipTwo));

    // currently, the contacts are "stale" - no cached relationships
    QVERIFY(dest3.relatedContacts().isEmpty());
    QVERIFY(dest3.relationships().isEmpty());
    QVERIFY(dest2.relatedContacts().isEmpty());
    QVERIFY(dest2.relationships().isEmpty());

    // now refresh the contacts
    dest3 = cm->contact(retrievalId(dest3));
    dest2 = cm->contact(retrievalId(dest2));
    source = cm->contact(retrievalId(source));

    // and test again.
    // when we aggregate, the source will be the second in an Aggregated relationship
    QVERIFY(!relatedContactIds(source.relatedContacts(QString(), QContactRelationship::First)).contains(dest1.id()));
    QVERIFY(!relatedContactIds(source.relatedContacts(QString(), QContactRelationship::First)).contains(dest2.id()));
    QVERIFY(!relatedContactIds(source.relatedContacts(QString(), QContactRelationship::First)).contains(dest3.id()));
    QVERIFY(!relatedContactIds(source.relatedContacts(QString(), QContactRelationship::First)).contains(dest4.id()));
    QVERIFY(relatedContactIds(source.relatedContacts(QString(), QContactRelationship::Second)).contains(dest2.id()));
    QVERIFY(relatedContactIds(source.relatedContacts(QString(), QContactRelationship::Either)).contains(dest2.id()));
    QVERIFY(relatedContactIds(source.relatedContacts(QString(), QContactRelationship::Second)).contains(dest3.id()));
    QVERIFY(relatedContactIds(source.relatedContacts(QString(), QContactRelationship::Either)).contains(dest3.id()));
    QVERIFY(relatedContactIds(source.relatedContacts(availableRelationshipTypes.at(0), QContactRelationship::Second)).contains(dest2.id()));
    QVERIFY(relatedContactIds(source.relatedContacts(availableRelationshipTypes.at(0), QContactRelationship::First)).isEmpty());

    QVERIFY(relatedContactIds(dest2.relatedContacts()).contains(source.id()));
    QVERIFY(dest2.relationships().contains(customRelationshipOne));
    QVERIFY(!dest2.relationships().contains(customRelationshipTwo));
    QVERIFY(dest2.relationships(availableRelationshipTypes.at(0)).contains(customRelationshipOne));
    QVERIFY(!dest2.relationships(availableRelationshipTypes.at(0)).contains(customRelationshipTwo));
    QVERIFY(relatedContactIds(dest2.relatedContacts(availableRelationshipTypes.at(0))).contains(source.id()));
    QVERIFY(relatedContactIds(dest2.relatedContacts(availableRelationshipTypes.at(0), QContactRelationship::First)).contains(source.id()));
    QVERIFY(relatedContactIds(dest2.relatedContacts(availableRelationshipTypes.at(0), QContactRelationship::Second)).isEmpty());
    QVERIFY(!relatedContactIds(dest2.relatedContacts(availableRelationshipTypes.at(0), QContactRelationship::Second)).contains(source.id()));
    
    QVERIFY(relatedContactIds(dest3.relatedContacts()).contains(source.id()));
    QVERIFY(!dest3.relationships().contains(customRelationshipOne));
    QVERIFY(dest3.relationships().contains(customRelationshipTwo));
    QVERIFY(!dest3.relationships(availableRelationshipTypes.at(0)).contains(customRelationshipOne));

    // Test iteration
    QList<QContactRelationship> relats = source.relationships();
    QList<QContactRelationship>::iterator it = relats.begin();

    while (it != relats.end()) {
        if (it->first() != relatedContact(source)) {
            // assume it's the aggregate and ignore it...
            it++;
            continue;
        }
        QVERIFY(relatedContactId(it->second()) == dest2.id() || relatedContactId(it->second()) == dest3.id());
        it++;
    }
    
    if (availableRelationshipTypes.count() > 1) {
        QVERIFY(relatedContactIds(source.relatedContacts(availableRelationshipTypes.at(1), QContactRelationship::Second)).contains(dest3.id()));
        QVERIFY(relatedContactIds(source.relatedContacts(availableRelationshipTypes.at(1), QContactRelationship::First)).isEmpty());
        
        QVERIFY(dest2.relationships(availableRelationshipTypes.at(1)).isEmpty());
        
        QVERIFY(!dest3.relationships(availableRelationshipTypes.at(0)).contains(customRelationshipTwo));
        QVERIFY(dest3.relationships(availableRelationshipTypes.at(1)).contains(customRelationshipTwo));
        QVERIFY(!dest3.relationships(availableRelationshipTypes.at(1)).contains(customRelationshipOne));
        QVERIFY(relatedContactIds(dest3.relatedContacts(availableRelationshipTypes.at(1))).contains(source.id()));
        QVERIFY(!relatedContactIds(dest3.relatedContacts(availableRelationshipTypes.at(0))).contains(source.id()));
        QVERIFY(relatedContactIds(dest3.relatedContacts(availableRelationshipTypes.at(1))).contains(source.id())); // role = either
        QVERIFY(!relatedContactIds(dest3.relatedContacts(availableRelationshipTypes.at(1), QContactRelationship::Second)).contains(source.id()));
        QVERIFY(relatedContactIds(dest3.relatedContacts(availableRelationshipTypes.at(1), QContactRelationship::First)).contains(source.id()));
        QVERIFY(relatedContactIds(dest2.relatedContacts(availableRelationshipTypes.at(1))).isEmpty());
    }
    else {
        QVERIFY(relatedContactIds(source.relatedContacts(availableRelationshipTypes.at(0), QContactRelationship::Second)).contains(dest3.id()));
    }
    
    // Cleanup a bit
    QMap<int, QContactManager::Error> errorMap;
    QList<QContactRelationship> moreRels;

    moreRels << customRelationshipOne << customRelationshipTwo;
    errorMap.insert(5, QContactManager::BadArgumentError);
    QVERIFY(cm->removeRelationships(moreRels, &errorMap));
    QVERIFY(errorMap.count() == 0);

    // test batch API and ordering in contacts
    QList<QContactRelationship> currentRelationships = cm->relationships(relatedContact(source), QContactRelationship::First);
    QList<QContactRelationship> batchList;
    QContactRelationship br1, br2, br3;
    br1 = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest2.id());
    br2 = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest3.id());
    if (availableRelationshipTypes.count() > 1)
    {
        br3 = makeRelationship(availableRelationshipTypes.at(1), source.id(), dest3.id());
    }
    else
    {
        br3 = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest4.id());
    }
    batchList << br1 << br2 << br3;

    // ensure that the batch save works properly
    cm->saveRelationships(&batchList, NULL);
    QCOMPARE(cm->error(), QContactManager::NoError);
    QList<QContactRelationship> batchRetrieve = cm->relationships(relatedContact(source), QContactRelationship::First);
    QVERIFY(batchRetrieve.contains(br1));
    QVERIFY(batchRetrieve.contains(br2));
    QVERIFY(batchRetrieve.contains(br3));
    
    // remove a single relationship
    QVERIFY(cm->removeRelationship(br3));
    batchRetrieve = cm->relationships(relatedContact(source), QContactRelationship::First);
    QVERIFY(batchRetrieve.contains(br1));
    QVERIFY(batchRetrieve.contains(br2));
    QVERIFY(!batchRetrieve.contains(br3)); // has already been removed.

    // now ensure that the batch remove works and we get returned to the original state.
    batchList.removeOne(br3);
    cm->removeRelationships(batchList, NULL);
    QVERIFY(cm->error() == QContactManager::NoError);
    QCOMPARE(cm->relationships(relatedContact(source), QContactRelationship::First), currentRelationships);

    // attempt to save relationships between an existing source but non-existent destination
    quint32 idSeed = 0x5544;
    QContactId nonexistentId = ContactId::apiId(idSeed);
    QContactId nonexistentDest = ContactId::contactId(nonexistentId);
    while (true) {
        QContact r = cm->contact(retrievalId(nonexistentDest));
        if (!ContactId::isValid(r)) {
            // found a "spare" local id (no contact with that id)
            break;
        }

        // keep looking...
        idSeed += 1;
        QVERIFY(idSeed != 0); // integer overflow check.

        nonexistentId = ContactId::apiId(idSeed);
        nonexistentDest = ContactId::contactId(nonexistentId);
    }

    QContactRelationship maliciousRel;
    maliciousRel = makeRelationship(QString::fromLatin1("test-invalid-relationship-type"), source.id(), nonexistentDest);
    QVERIFY(!cm->saveRelationship(&maliciousRel));

    // attempt to save a circular relationship - should fail!
    maliciousRel = makeRelationship(availableRelationshipTypes.at(0), source.id(), source.id());
    QVERIFY(!cm->saveRelationship(&maliciousRel));

    // remove the nonexistent relationship
    relationshipsCount = cm->relationships().count();
    QVERIFY(!cm->removeRelationship(maliciousRel));         // does not exist; fail remove.
    QVERIFY(cm->error() == QContactManager::DoesNotExistError || cm->error() == QContactManager::InvalidRelationshipError);
    QCOMPARE(cm->relationships().count(), relationshipsCount); // should be unchanged.

    // now we want to ensure that a relationship is removed if one of the contacts is removed.
    customRelationshipOne = makeRelationship(availableRelationshipTypes.at(0), source.id(), dest2.id());

    // Test batch save with an error map
    moreRels.clear();
    moreRels << customRelationshipOne;
    errorMap.insert(0, QContactManager::BadArgumentError);
    QVERIFY(cm->saveRelationships(&moreRels, &errorMap));
    QVERIFY(cm->error() == QContactManager::NoError);
    QVERIFY(errorMap.count() == 0); // should be reset
    source = cm->contact(retrievalId(source));
    dest2 = cm->contact(retrievalId(dest2));
    QVERIFY(cm->removeContact(removalId(dest2))); // remove dest2, the relationship should be removed
    QVERIFY(cm->relationships(availableRelationshipTypes.at(0), relatedContact(dest2), QContactRelationship::Second).isEmpty());
    source = cm->contact(retrievalId(source));
    QVERIFY(!relatedContactIds(source.relatedContacts()).contains(dest2.id())); // and it shouldn't appear in cache.

    // now clean up and remove our dests.
    QVERIFY(cm->removeContact(removalId(source)));
    QVERIFY(cm->removeContact(removalId(dest3)));

    // attempt to save relationships with nonexistent contacts
    QVERIFY(!cm->saveRelationship(&br1));
    QVERIFY(cm->error() == QContactManager::InvalidRelationshipError);
    cm->saveRelationships(&batchList, NULL);
    QVERIFY(cm->error() == QContactManager::InvalidRelationshipError);
    QVERIFY(!cm->removeRelationship(br1));
    QVERIFY(cm->error() == QContactManager::DoesNotExistError || cm->error() == QContactManager::InvalidRelationshipError);
    cm->removeRelationships(batchList, NULL);
    QVERIFY(cm->error() == QContactManager::DoesNotExistError || cm->error() == QContactManager::InvalidRelationshipError);
}

void tst_QContactManager::contactType()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    QContact g1, g2, c;
    g1.setType(QContactType::TypeGroup);
    g2.setType(QContactType::TypeGroup);

    QContactPhoneNumber g1p, g2p, cp;
    g1p.setNumber("22222");
    g2p.setNumber("11111");
    cp.setNumber("33333");

    g1.saveDetail(&g1p);
    g2.saveDetail(&g2p);
    c.saveDetail(&cp);

    QVERIFY(cm->saveContact(&c));

    // test that the accessing by type works properly for contacts
    QContactDetailFilter typeFilter;
    setFilterDetail<QContactType>(typeFilter, QContactType::FieldType);
    setFilterValue(typeFilter, QContactType::TypeContact);

    QContactDetailFilter stFilter;
    stFilter.setDetailType(QContactSyncTarget::Type);

    QVERIFY(cm->contactIds(typeFilter & stFilter).contains(ContactId::apiId(c)));

    if (!managerSupportsFeature(*cm, "Groups"))
        QSKIP("Skipping: This manager does not support group contacts!");

    QVERIFY(cm->saveContact(&g1));
    QVERIFY(cm->saveContact(&g2));

    // test that the accessing by type works properly for groups
    QContactDetailFilter groupFilter;
    setFilterDetail<QContactType>(groupFilter, QContactType::FieldType);
    setFilterValue(groupFilter, QContactType::TypeGroup);
    QVERIFY(cm->contactIds(groupFilter).contains(ContactId::apiId(g1)));
    QVERIFY(cm->contactIds(groupFilter).contains(ContactId::apiId(g2)));
    QVERIFY(!cm->contactIds(groupFilter).contains(ContactId::apiId(c)));

    QList<QContactSortOrder> sortOrders;
    QContactSortOrder byPhoneNumber;
    setSortDetail<QContactPhoneNumber>(byPhoneNumber, QContactPhoneNumber::FieldNumber);
    sortOrders.append(byPhoneNumber);

    // and ensure that sorting works properly with typed contacts also
    QList<QContactId> sortedIds = cm->contactIds(groupFilter, sortOrders);
    QVERIFY(sortedIds.indexOf(ContactId::apiId(g2)) < sortedIds.indexOf(ContactId::apiId(g1)));

    cm->removeContact(removalId(g1));
    cm->removeContact(removalId(g2));
    cm->removeContact(removalId(c));
}

void tst_QContactManager::familyDetail()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    QContact a;

    QContactName n;
    n.setFirstName("Adam");
    a.saveDetail(&n);

    QContactFamily f;
    f.setSpouse("Eve");
    f.setChildren(QStringList() << "Cain" << "Abel");
    a.saveDetail(&f);

    QVERIFY(cm->saveContact(&a));

    a = cm->contact(retrievalId(a));

    QCOMPARE(a.details<QContactName>().count(), 1);
    n = a.details<QContactName>().at(0);
    QCOMPARE(n.firstName(), QLatin1String("Adam"));

    QCOMPARE(a.details<QContactFamily>().count(), 1);
    f = a.details<QContactFamily>().at(0);
    QCOMPARE(f.spouse(), QLatin1String("Eve"));
    QCOMPARE(f.children().toSet(), QSet<QString>() << "Cain" << "Abel");

    QCOMPARE(a.relatedContacts(QContactRelationship::Aggregates(), QContactRelationship::First).count(), 1);

    QContactId aa(a.relatedContacts(QContactRelationship::Aggregates(), QContactRelationship::First).first().id());
    QVERIFY(!aa.isNull());

    QContactDetailFilter familyFilter;
    setFilterDetail<QContactFamily>(familyFilter, -1);
    QVERIFY(cm->contactIds(familyFilter).contains(aa));

    QContactDetailFilter spouseFilter;
    setFilterDetail<QContactFamily>(spouseFilter, QContactFamily::FieldSpouse);
    setFilterValue(spouseFilter, "Eve");
    QVERIFY(cm->contactIds(spouseFilter).contains(aa));

    QContactDetailFilter childrenFilter;
    setFilterDetail<QContactFamily>(childrenFilter, QContactFamily::FieldChildren);
    setFilterValue(childrenFilter, "Abel");
    QVERIFY(cm->contactIds(childrenFilter).contains(aa));

    setFilterValue(childrenFilter, "Mabel");
    QCOMPARE(cm->contactIds(childrenFilter).contains(aa), false);
}

void tst_QContactManager::geoLocationDetail()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    QContact a;

    QContactName n;
    n.setFirstName("Cristo Redentor");
    a.saveDetail(&n);

    const QDateTime ts(QDateTime::currentDateTime());

    QContactGeoLocation l;
    l.setLabel("Position");
    l.setLatitude(-22.951994);
    l.setLongitude(-43.210492);
    l.setAccuracy(0.000001);
    l.setTimestamp(ts);
    a.saveDetail(&l);

    QVERIFY(cm->saveContact(&a));

    a = cm->contact(retrievalId(a));

    QCOMPARE(a.details<QContactName>().count(), 1);
    n = a.details<QContactName>().at(0);
    QCOMPARE(n.firstName(), QLatin1String("Cristo Redentor"));

    QCOMPARE(a.details<QContactGeoLocation>().count(), 1);
    l = a.details<QContactGeoLocation>().at(0);
    QCOMPARE(l.label(), QLatin1String("Position"));
    QCOMPARE(l.latitude(), -22.951994);
    QCOMPARE(l.longitude(), -43.210492);
    QCOMPARE(l.accuracy(), 0.000001);
    QCOMPARE(l.timestamp(), ts);

    QCOMPARE(a.relatedContacts(QContactRelationship::Aggregates(), QContactRelationship::First).count(), 1);

    QContactId aa(a.relatedContacts(QContactRelationship::Aggregates(), QContactRelationship::First).first().id());
    QVERIFY(!aa.isNull());

    QContactDetailFilter geoLocationFilter;
    setFilterDetail<QContactGeoLocation>(geoLocationFilter, -1);
    QVERIFY(cm->contactIds(geoLocationFilter).contains(aa));

    QContactDetailFilter labelFilter;
    setFilterDetail<QContactGeoLocation>(labelFilter, QContactGeoLocation::FieldLabel);
    setFilterValue(labelFilter, "Position");
    QVERIFY(cm->contactIds(labelFilter).contains(aa));

    QContactDetailFilter latitudeFilter;
    setFilterDetail<QContactGeoLocation>(latitudeFilter, QContactGeoLocation::FieldLatitude);
    setFilterValue(latitudeFilter, -22.951994);
    QVERIFY(cm->contactIds(latitudeFilter).contains(aa));

    QContactDetailFilter altitudeFilter;
    setFilterDetail<QContactGeoLocation>(altitudeFilter, QContactGeoLocation::FieldAltitude);
    setFilterValue(altitudeFilter, 1.0);
    QCOMPARE(cm->contactIds(altitudeFilter).contains(aa), false);
}

#if defined(USE_VERSIT_PLZ)
void tst_QContactManager::partialSave()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    const bool isAllowingDetailsNotInSchema = false;

    QVersitContactImporter imp;
    QVersitReader reader(QByteArray(
            "BEGIN:VCARD\r\nFN:Alice\r\nN:Alice\r\nTEL:12345\r\nEND:VCARD\r\n"
            "BEGIN:VCARD\r\nFN:Bob\r\nN:Bob\r\nTEL:5678\r\nEND:VCARD\r\n"
            "BEGIN:VCARD\r\nFN:Carol\r\nN:Carol\r\nEMAIL:carol@example.com\r\nEND:VCARD\r\n"
            "BEGIN:VCARD\r\nFN:David\r\nN:David\r\nORG:DavidCorp\r\nEND:VCARD\r\n"));
    reader.startReading();
    reader.waitForFinished();
    QCOMPARE(reader.error(), QVersitReader::NoError);

    QCOMPARE(reader.results().count(), 4);
    QVERIFY(imp.importDocuments(reader.results()));
    QCOMPARE(imp.contacts().count(), 4);
    QVERIFY(imp.contacts()[0].displayLabel() == QLatin1String("Alice"));
    QVERIFY(imp.contacts()[1].displayLabel() == QLatin1String("Bob"));
    QVERIFY(imp.contacts()[2].displayLabel() == QLatin1String("Carol"));
    QVERIFY(imp.contacts()[3].displayLabel() == QLatin1String("David"));

    QList<QContact> contacts = imp.contacts();
    QMap<int, QContactManager::Error> errorMap;

    // First save these contacts
    QVERIFY(cm->saveContacts(&contacts, &errorMap));
    QList<QContact> originalContacts = contacts;

    // Now try some partial save operations
    // 0) empty mask == full save
    // 1) Ignore an added phonenumber
    // 2) Only save a modified phonenumber, not a modified email
    // 3) Remove an email address & phone, mask out phone
    // 4) new contact, no details in the mask
    // 5) new contact, some details in the mask
    // 6) Have a bad manager uri in the middle
    // 7) Have a non existing contact in the middle
    // 8) A list entirely of new contacts

    QContactPhoneNumber pn;
    pn.setNumber("111111");
    contacts[0].saveDetail(&pn);

    // 0) empty mask
    QVERIFY(cm->saveContacts(&contacts, QStringList(), &errorMap));

    // That should have updated everything
    QContact a = cm->contact(retrievalId(originalContacts[0]));
    QVERIFY(a.details<QContactPhoneNumber>().count() == 2);

    // 1) Add a phone number to b, mask it out
    contacts[1].saveDetail(&pn);
    QVERIFY(cm->saveContacts(&contacts, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());

    QContact b = cm->contact(retrievalId(originalContacts[1]));
    QVERIFY(b.details<QContactPhoneNumber>().count() == 1);

    // 2) save a modified detail in the mask
    QContactEmailAddress e;
    e.setEmailAddress("example@example.com");
    contacts[1].saveDetail(&e); // contacts[1] should have both phone and email

    QVERIFY(cm->saveContacts(&contacts, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());
    b = cm->contact(retrievalId(originalContacts[1]));
    QVERIFY(b.details<QContactPhoneNumber>().count() == 1);
    QVERIFY(b.details<QContactEmailAddress>().count() == 1);

    // 3) Remove an email address and a phone number
    QVERIFY(contacts[1].removeDetail(&e));
    QVERIFY(contacts[1].removeDetail(&pn));
    QVERIFY(contacts[1].details<QContactEmailAddress>().count() == 0);
    QVERIFY(contacts[1].details<QContactPhoneNumber>().count() == 1);
    QVERIFY(cm->saveContacts(&contacts, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());
    b = cm->contact(retrievalId(originalContacts[1]));
    QVERIFY(b.details<QContactPhoneNumber>().count() == 1);
    QVERIFY(b.details<QContactEmailAddress>().count() == 0);

    // 4 - New contact, no details in the mask
    QContact newContact = originalContacts[3];
    newContact.setId(QContactId());

    contacts.append(newContact);
    QVERIFY(cm->saveContacts(&contacts, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());
    QVERIFY(ContactId::isValid(contacts[4])); // Saved
    b = cm->contact(retrievalId(contacts[4]));
    QVERIFY(b.details<QContactOrganization>().count() == 0); // not saved
    QVERIFY(b.details<QContactName>().count() == 0); // not saved

    // 5 - New contact, some details in the mask
    newContact = originalContacts[2];
    newContact.setId(QContactId());
    contacts.append(newContact);
    QVERIFY(cm->saveContacts(&contacts, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());
    QVERIFY(ContactId::isValid(contacts[5])); // Saved
    b = cm->contact(retrievalId(contacts[5]));
    QVERIFY(b.details<QContactEmailAddress>().count() == 1);
    QVERIFY(b.details<QContactName>().count() == 0); // not saved

    // 6) Have a bad manager uri in the middle followed by a save error
    QContactId id4(contacts[4].id());
    QContactId badId(id4);
    badId.setManagerUri(QString());
    contacts[4].setId(badId);
    QContactDetail badDetail("BadDetail");
    badDetail.setValue("BadField", "BadValue");
    contacts[5].saveDetail(&badDetail);
    QVERIFY(!cm->saveContacts(&contacts, QStringList("BadDetail"), &errorMap));
    QCOMPARE(errorMap.count(), isAllowingDetailsNotInSchema ? 1 : 2);
    QCOMPARE(errorMap[4], QContactManager::DoesNotExistError);
    QCOMPARE(errorMap[5], isAllowingDetailsNotInSchema ? QContactManager::NoError : QContactManager::InvalidDetailError);

    // 7) Have a non existing contact in the middle followed by a save error
    badId = id4;
    badId.setLocalId(987234); // something nonexistent
    contacts[4].setId(badId);
    QVERIFY(!cm->saveContacts(&contacts, QStringList("BadDetail"), &errorMap));
    QCOMPARE(errorMap.count(), isAllowingDetailsNotInSchema ? 1 : 2);
    QCOMPARE(errorMap[4], QContactManager::DoesNotExistError);
    QCOMPARE(errorMap[5], isAllowingDetailsNotInSchema ? QContactManager::NoError : QContactManager::InvalidDetailError);

    // 8 - New contact, no details in the mask
    newContact = originalContacts[3];
    QCOMPARE(newContact.details<QContactOrganization>().count(), 1);
    QCOMPARE(newContact.details<QContactName>().count(), 1);
    newContact.setId(QContactId());
    QList<QContact> contacts2;
    contacts2.append(newContact);
    QVERIFY(cm->saveContacts(&contacts2, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());
    QVERIFY(ContactId::isValid(contacts2[0])); // Saved
    b = cm->contact(retrievalId(contacts2[0]));
    QVERIFY(b.details<QContactOrganization>().count() == 0); // not saved
    QVERIFY(b.details<QContactName>().count() == 0); // not saved

    // 9 - A list with only a new contact, with some details in the mask
    newContact = originalContacts[2];
    newContact.setId(QContactId());
    contacts2.clear();
    contacts2.append(newContact);
    QVERIFY(cm->saveContacts(&contacts2, QStringList(QContactEmailAddress::DefinitionName), &errorMap));
    QVERIFY(errorMap.isEmpty());
    QVERIFY(ContactId::isValid(contacts2[0])); // Saved
    b = cm->contact(retrievalId(contacts2[0]));
    QVERIFY(b.details<QContactEmailAddress>().count() == 1);
    QVERIFY(b.details<QContactName>().count() == 0); // not saved

    // 10 - A list with new a contact for the wrong manager, followed by a new contact with an
    // invalid detail
    newContact = originalContacts[2];
    newContact.setId(QContactId());
    contacts2.clear();
    contacts2.append(newContact);
    contacts2.append(newContact);
    contacts2[0].setId(badId);
    contacts2[1].saveDetail(&badDetail);
    QVERIFY(!cm->saveContacts(&contacts2, QStringList("BadDetail"), &errorMap));
    QCOMPARE(errorMap.count(), isAllowingDetailsNotInSchema ? 1 : 2);
    QCOMPARE(errorMap[0], QContactManager::DoesNotExistError);
    QCOMPARE(errorMap[1], isAllowingDetailsNotInSchema ? QContactManager::NoError : QContactManager::InvalidDetailError);
}
#endif

void tst_QContactManager::extendedDetail()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    // Verify that QContactExtendedDetail is supported
    QContact a;

    QContactName n;
    n.setFirstName("A");
    n.setMiddleName("Test");
    n.setLastName("Person");
    a.saveDetail(&n);

    QContactExtendedDetail ed;
    ed.setName(QString::fromLatin1("Testing"));
    a.saveDetail(&ed);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.details<QContactExtendedDetail>().count(), 1);
    QCOMPARE(a.details<QContactExtendedDetail>().at(0).name(), QString::fromLatin1("Testing"));
    QCOMPARE(a.details<QContactExtendedDetail>().at(0).data(), QVariant());

    QByteArray d1(QString::fromLatin1("1-2-3").toUtf8());
    ed = a.details<QContactExtendedDetail>().at(0);
    ed.setData(d1);
    a.saveDetail(&ed);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.details<QContactExtendedDetail>().count(), 1);
    QCOMPARE(a.details<QContactExtendedDetail>().at(0).name(), QString::fromLatin1("Testing"));
    QCOMPARE(a.details<QContactExtendedDetail>().at(0).data().toByteArray(), d1);

    QByteArray d2;
    {
        QDataStream ds(&d2, QIODevice::WriteOnly);
        for (int i = 0; i < 10; ++i) {
            int x = qrand();
            int y = qrand();
            const double q = x / (y ? y : 1);
            ds << q;
        }
    }
    QCOMPARE(static_cast<size_t>(d2.size()), 10 * sizeof(double));

    ed = QContactExtendedDetail();
    ed.setName(QString::fromLatin1("Second"));
    ed.setData(d2);
    a.saveDetail(&ed);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.details<QContactExtendedDetail>().count(), 2);
    if (a.details<QContactExtendedDetail>().at(0).name() == QString::fromLatin1("Testing")) {
        QCOMPARE(a.details<QContactExtendedDetail>().at(0).data().toByteArray(), d1);
        QCOMPARE(a.details<QContactExtendedDetail>().at(1).name(), QString::fromLatin1("Second"));
        QCOMPARE(a.details<QContactExtendedDetail>().at(1).data().toByteArray(), d2);
    } else {
        QCOMPARE(a.details<QContactExtendedDetail>().at(0).name(), QString::fromLatin1("Second"));
        QCOMPARE(a.details<QContactExtendedDetail>().at(0).data().toByteArray(), d2);
        QCOMPARE(a.details<QContactExtendedDetail>().at(1).name(), QString::fromLatin1("Testing"));
        QCOMPARE(a.details<QContactExtendedDetail>().at(1).data().toByteArray(), d1);
    }

    QContactDetailFilter filter;
    filter.setDetailType(QContactExtendedDetail::Type, QContactExtendedDetail::FieldName);
    filter.setValue(QString::fromLatin1("Second"));

    QList<QContact> contacts(cm->contacts(filter));
    QCOMPARE(contacts.count(), 1);
}

void tst_QContactManager::onlineAccountFields()
{
    QFETCH(QString, uri);
    QScopedPointer<QContactManager> cm(QContactManager::fromUri(uri));

    // Verify that the extended fields of QContactOnlineAccount are correctly implemented
    QContact a;

    QContactName n;
    n.setFirstName("A");
    n.setMiddleName("Test");
    n.setLastName("Person");
    a.saveDetail(&n);

    const QString accountUri(QString::fromLatin1("test@example.org"));
    const QString serviceProvider(QString::fromLatin1("example-im"));
    const QContactOnlineAccount::Protocol protocol(QContactOnlineAccount::ProtocolJabber);
    const QStringList capabilities(QStringList() << QString::fromLatin1("text") << QString::fromLatin1("voice"));
    const QList<int> subTypes(QList<int>() << QContactOnlineAccount::SubTypeSip << QContactOnlineAccount::SubTypeImpp);
    const QString accountPath(QString::fromLatin1("/example/jabber/0"));
    const QString accountIconPath(QString::fromLatin1("icons/example.png"));
    const bool enabled(true);
    const QString accountDisplayName(QString::fromLatin1("My Account"));
    const QString serviceProviderDisplayName(QString::fromLatin1("Example"));

    QContactOnlineAccount oa;
    oa.setAccountUri(accountUri);
    oa.setServiceProvider(serviceProvider);
    oa.setProtocol(protocol);
    oa.setCapabilities(capabilities);
    oa.setSubTypes(subTypes);
    oa.setValue(QContactOnlineAccount__FieldAccountPath, accountPath);
    oa.setValue(QContactOnlineAccount__FieldAccountIconPath, accountIconPath);
    oa.setValue(QContactOnlineAccount__FieldEnabled, enabled);
    oa.setValue(QContactOnlineAccount__FieldAccountDisplayName, accountDisplayName);
    oa.setValue(QContactOnlineAccount__FieldServiceProviderDisplayName, serviceProviderDisplayName);
    a.saveDetail(&oa);

    QVERIFY(cm->saveContact(&a));
    a = cm->contact(retrievalId(a));

    QCOMPARE(a.details<QContactOnlineAccount>().count(), 1);

    oa = a.detail<QContactOnlineAccount>();
    QCOMPARE(oa.accountUri(), accountUri);
    QCOMPARE(oa.serviceProvider(), serviceProvider);
    QCOMPARE(oa.protocol(), protocol);
    QCOMPARE(oa.capabilities(), capabilities);
    QCOMPARE(oa.subTypes(), subTypes);
    QCOMPARE(oa.value(QContactOnlineAccount__FieldAccountPath).value<QString>(), accountPath);
    QCOMPARE(oa.value(QContactOnlineAccount__FieldAccountIconPath).value<QString>(), accountIconPath);
    QCOMPARE(oa.value(QContactOnlineAccount__FieldEnabled).value<bool>(), enabled);
    QCOMPARE(oa.value(QContactOnlineAccount__FieldAccountDisplayName).value<QString>(), accountDisplayName);
    QCOMPARE(oa.value(QContactOnlineAccount__FieldServiceProviderDisplayName).value<QString>(), serviceProviderDisplayName);
}

void tst_QContactManager::lateDeletion()
{
    // Create some engines, but make them get deleted at shutdown
    QFETCH(QString, uri);
    QContactManager* cm = QContactManager::fromUri(uri);

    cm->setParent(qApp); // now do nothing
}

void tst_QContactManager::compareVariant()
{
    // Exercise this function a bit
    QFETCH(QVariant, a);
    QFETCH(QVariant, b);
    QFETCH(Qt::CaseSensitivity, cs);
    QFETCH(int, expected);

    int comparison = QContactManagerEngine::compareVariant(a, b, cs);
    // Since compareVariant is a little imprecise (just sign matters)
    // convert that here.
    if (comparison < 0)
        comparison = -1;
    else if (comparison > 0)
        comparison = 1;

    QCOMPARE(comparison, expected);

    comparison = QContactManagerEngine::compareVariant(b, a, cs);
    if (comparison < 0)
        comparison = -1;
    else if (comparison > 0)
        comparison = 1;

    // The sign should be flipped now
    QVERIFY((comparison + expected) == 0);
}

void tst_QContactManager::compareVariant_data()
{
    QTest::addColumn<QVariant>("a");
    QTest::addColumn<QVariant>("b");

    QTest::addColumn<Qt::CaseSensitivity>("cs");
    QTest::addColumn<int>("expected");

    // bool
    QTest::newRow("bool <") << QVariant(false) << QVariant(true) << Qt::CaseInsensitive << -1;
    QTest::newRow("bool =") << QVariant(false) << QVariant(false) << Qt::CaseInsensitive << -0;
    QTest::newRow("bool >") << QVariant(true) << QVariant(false) << Qt::CaseInsensitive << 1;

    // char (who uses these??)
    QTest::newRow("char <") << QVariant(QChar('a')) << QVariant(QChar('b')) << Qt::CaseInsensitive << -1;
    QTest::newRow("char < ci") << QVariant(QChar('A')) << QVariant(QChar('b')) << Qt::CaseInsensitive << -1;
    QTest::newRow("char < ci 2") << QVariant(QChar('a')) << QVariant(QChar('B')) << Qt::CaseInsensitive << -1;
    QTest::newRow("char < cs") << QVariant(QChar('a')) << QVariant(QChar('b')) << Qt::CaseSensitive << -1;
    QTest::newRow("char < cs") << QVariant(QChar('A')) << QVariant(QChar('b')) << Qt::CaseSensitive << -1;

    QTest::newRow("char = ci") << QVariant(QChar('a')) << QVariant(QChar('a')) << Qt::CaseInsensitive << 0;
    QTest::newRow("char = ci 2") << QVariant(QChar('a')) << QVariant(QChar('A')) << Qt::CaseInsensitive << 0;
    QTest::newRow("char = ci 3") << QVariant(QChar('A')) << QVariant(QChar('a')) << Qt::CaseInsensitive << 0;
    QTest::newRow("char = ci 4") << QVariant(QChar('A')) << QVariant(QChar('A')) << Qt::CaseInsensitive << 0;
    QTest::newRow("char = cs") << QVariant(QChar('a')) << QVariant(QChar('a')) << Qt::CaseSensitive << 0;
    QTest::newRow("char = cs 2") << QVariant(QChar('A')) << QVariant(QChar('A')) << Qt::CaseSensitive << 0;

    QTest::newRow("char >") << QVariant(QChar('b')) << QVariant(QChar('a')) << Qt::CaseInsensitive << 1;
    QTest::newRow("char > ci") << QVariant(QChar('b')) << QVariant(QChar('A')) << Qt::CaseInsensitive << 1;
    QTest::newRow("char > ci 2") << QVariant(QChar('B')) << QVariant(QChar('a')) << Qt::CaseInsensitive << 1;
    QTest::newRow("char > cs") << QVariant(QChar('b')) << QVariant(QChar('a')) << Qt::CaseSensitive << 1;
    QTest::newRow("char > cs") << QVariant(QChar('b')) << QVariant(QChar('A')) << Qt::CaseSensitive << 1;

    // Some numeric types
    // uint
    QTest::newRow("uint < boundary") << QVariant(uint(1)) << QVariant(uint(-1)) << Qt::CaseInsensitive << -1;
    QTest::newRow("uint <") << QVariant(uint(1)) << QVariant(uint(2)) << Qt::CaseInsensitive << -1;
    QTest::newRow("uint =") << QVariant(uint(2)) << QVariant(uint(2)) << Qt::CaseInsensitive << 0;
    QTest::newRow("uint = 0") << QVariant(uint(0)) << QVariant(uint(0)) << Qt::CaseInsensitive << 0;
    QTest::newRow("uint = boundary") << QVariant(uint(-1)) << QVariant(uint(-1)) << Qt::CaseInsensitive << 0;
    QTest::newRow("uint >") << QVariant(uint(5)) << QVariant(uint(2)) << Qt::CaseInsensitive << 1;
    QTest::newRow("uint > boundary") << QVariant(uint(-1)) << QVariant(uint(2)) << Qt::CaseInsensitive << 1; // boundary

    // int (hmm, signed 32 bit assumed)
    QTest::newRow("int < boundary") << QVariant(int(0x80000000)) << QVariant(int(0x7fffffff)) << Qt::CaseInsensitive << -1;
    QTest::newRow("int <") << QVariant(int(1)) << QVariant(int(2)) << Qt::CaseInsensitive << -1;
    QTest::newRow("int =") << QVariant(int(2)) << QVariant(int(2)) << Qt::CaseInsensitive << 0;
    QTest::newRow("int = 0") << QVariant(int(0)) << QVariant(int(0)) << Qt::CaseInsensitive << 0;
    QTest::newRow("int = boundary") << QVariant(int(0x80000000)) << QVariant(int(0x80000000)) << Qt::CaseInsensitive << 0;
    QTest::newRow("int >") << QVariant(int(5)) << QVariant(int(2)) << Qt::CaseInsensitive << 1;
    QTest::newRow("int > boundary") << QVariant(int(0x7fffffff)) << QVariant(int(0x80000000)) << Qt::CaseInsensitive << 1; // boundary

    // ulonglong
    QTest::newRow("ulonglong < boundary") << QVariant(qulonglong(1)) << QVariant(qulonglong(-1)) << Qt::CaseInsensitive << -1;
    QTest::newRow("ulonglong <") << QVariant(qulonglong(1)) << QVariant(qulonglong(2)) << Qt::CaseInsensitive << -1;
    QTest::newRow("ulonglong =") << QVariant(qulonglong(2)) << QVariant(qulonglong(2)) << Qt::CaseInsensitive << 0;
    QTest::newRow("ulonglong = 0") << QVariant(qulonglong(0)) << QVariant(qulonglong(0)) << Qt::CaseInsensitive << 0;
    QTest::newRow("ulonglong = boundary") << QVariant(qulonglong(-1)) << QVariant(qulonglong(-1)) << Qt::CaseInsensitive << 0;
    QTest::newRow("ulonglong >") << QVariant(qulonglong(5)) << QVariant(qulonglong(2)) << Qt::CaseInsensitive << 1;
    QTest::newRow("ulonglong > boundary") << QVariant(qulonglong(-1)) << QVariant(qulonglong(2)) << Qt::CaseInsensitive << 1; // boundary

    // longlong
    QTest::newRow("longlong < boundary") << QVariant(qlonglong(0x8000000000000000LL)) << QVariant(qlonglong(0x7fffffffffffffffLL)) << Qt::CaseInsensitive << -1;
    QTest::newRow("longlong <") << QVariant(qlonglong(1)) << QVariant(qlonglong(2)) << Qt::CaseInsensitive << -1;
    QTest::newRow("longlong =") << QVariant(qlonglong(2)) << QVariant(qlonglong(2)) << Qt::CaseInsensitive << 0;
    QTest::newRow("longlong = 0") << QVariant(qlonglong(0)) << QVariant(qlonglong(0)) << Qt::CaseInsensitive << 0;
    QTest::newRow("longlong = boundary") << QVariant(qlonglong(0x8000000000000000LL)) << QVariant(qlonglong(0x8000000000000000LL)) << Qt::CaseInsensitive << 0;
    QTest::newRow("longlong >") << QVariant(qlonglong(5)) << QVariant(qlonglong(2)) << Qt::CaseInsensitive << 1;
    QTest::newRow("longlong > boundary") << QVariant(qlonglong(0x7fffffffffffffffLL)) << QVariant(qlonglong(0x8000000000000000LL)) << Qt::CaseInsensitive << 1; // boundary

    // double (hmm, skips NaNs etc)
    QTest::newRow("double < inf 2") << QVariant(-qInf()) << QVariant(qInf()) << Qt::CaseInsensitive << -1;
    QTest::newRow("double < inf") << QVariant(1.0) << QVariant(qInf()) << Qt::CaseInsensitive << -1;
    QTest::newRow("double <") << QVariant(1.0) << QVariant(2.0) << Qt::CaseInsensitive << -1;
    QTest::newRow("double =") << QVariant(2.0) << QVariant(2.0) << Qt::CaseInsensitive << 0;
    QTest::newRow("double = 0") << QVariant(0.0) << QVariant(0.0) << Qt::CaseInsensitive << 0;
    QTest::newRow("double = inf") << QVariant(qInf()) << QVariant(qInf()) << Qt::CaseInsensitive << 0;
    QTest::newRow("double >") << QVariant(5.0) << QVariant(2.0) << Qt::CaseInsensitive << 1;
    QTest::newRow("double > inf") << QVariant(qInf()) << QVariant(5.0) << Qt::CaseInsensitive << 1;
    QTest::newRow("double > inf 2") << QVariant(0.0) << QVariant(-qInf()) << Qt::CaseInsensitive << 1;
    QTest::newRow("double > inf 3") << QVariant(qInf()) << QVariant(-qInf()) << Qt::CaseInsensitive << 1;

    // strings
    QTest::newRow("string <") << QVariant(QString("a")) << QVariant(QString("b")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string <") << QVariant(QString("a")) << QVariant(QString("B")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string <") << QVariant(QString("A")) << QVariant(QString("b")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string <") << QVariant(QString("A")) << QVariant(QString("B")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string < cs") << QVariant(QString("a")) << QVariant(QString("b")) << Qt::CaseSensitive << -1;
    QTest::newRow("string < cs 2") << QVariant(QString("A")) << QVariant(QString("b")) << Qt::CaseSensitive << -1;

    QTest::newRow("string < length") << QVariant(QString("a")) << QVariant(QString("aa")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string < length cs") << QVariant(QString("a")) << QVariant(QString("aa")) << Qt::CaseSensitive << -1;
    QTest::newRow("string < length 2") << QVariant(QString("a")) << QVariant(QString("ba")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string < length cs 2") << QVariant(QString("a")) << QVariant(QString("ba")) << Qt::CaseSensitive << -1;

    QTest::newRow("string aa < b") << QVariant(QString("aa")) << QVariant(QString("b")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string aa < b cs") << QVariant(QString("aa")) << QVariant(QString("b")) << Qt::CaseSensitive << -1;

    QTest::newRow("string '' < a") << QVariant(QString("")) << QVariant(QString("aa")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string '' < aa cs") << QVariant(QString("")) << QVariant(QString("aa")) << Qt::CaseSensitive << -1;
    QTest::newRow("string 0 < a") << QVariant(QString()) << QVariant(QString("aa")) << Qt::CaseInsensitive << -1;
    QTest::newRow("string 0 < aa cs") << QVariant(QString()) << QVariant(QString("aa")) << Qt::CaseSensitive << -1;

    QTest::newRow("string '' = ''") << QVariant(QString("")) << QVariant(QString("")) << Qt::CaseInsensitive << 0;
    QTest::newRow("string '' = '' cs") << QVariant(QString("")) << QVariant(QString("")) << Qt::CaseSensitive << 0;
    QTest::newRow("string 0 = 0") << QVariant(QString()) << QVariant(QString()) << Qt::CaseInsensitive << 0;
    QTest::newRow("string 0 = 0 cs") << QVariant(QString()) << QVariant(QString()) << Qt::CaseSensitive << 0;
    QTest::newRow("string a = a") << QVariant(QString("a")) << QVariant(QString("a")) << Qt::CaseInsensitive << 0;
    QTest::newRow("string a = a cs") << QVariant(QString("a")) << QVariant(QString("a")) << Qt::CaseSensitive << 0;

    // Stringlists
    // {} < {"a"} < {"aa"} < {"aa","bb"} < {"aa", "cc"} < {"bb"}

    QStringList empty;
    QStringList listA("a");
    QStringList listAA("aa");
    QStringList listAABB;
    listAABB << "aa" << "bb";
    QStringList listAACC;
    listAACC << "aa" << "cc";
    QStringList listBB;
    listBB << "bb";
    QStringList listCCAA;
    listCCAA << "cc" << "aa";
    QStringList listA2("A");
    QStringList listAA2("AA");

    QTest::newRow("stringlist {} < {a}") << QVariant(empty) << QVariant(listA) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {} < {a} cs") << QVariant(empty) << QVariant(listA) << Qt::CaseSensitive << -1;
    QTest::newRow("stringlist {} < {A}") << QVariant(empty) << QVariant(listA2) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {} < {A} cs") << QVariant(empty) << QVariant(listA2) << Qt::CaseSensitive << -1;

    QTest::newRow("stringlist {a} < {aa}") << QVariant(listA) << QVariant(listAA) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {a} < {aa} cs") << QVariant(listA) << QVariant(listAA) << Qt::CaseSensitive << -1;
    QTest::newRow("stringlist {a} < {AA}") << QVariant(listA) << QVariant(listAA2) << Qt::CaseInsensitive << -1;
    // The results of this test are variable - ignore for now...
    //QTest::newRow("stringlist {a} < {AA} cs") << QVariant(listA) << QVariant(listAA2) << Qt::CaseSensitive << -1;

    QTest::newRow("stringlist {A} < {aa,bb}") << QVariant(listA2) << QVariant(listAABB) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {A} < {aa,bb} cs") << QVariant(listA2) << QVariant(listAABB) << Qt::CaseSensitive << -1;
    QTest::newRow("stringlist {aa} < {aa,bb}") << QVariant(listAA) << QVariant(listAABB) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {aa} < {aa,bb} cs") << QVariant(listAA) << QVariant(listAABB) << Qt::CaseSensitive << -1;

    QTest::newRow("stringlist {aa,bb} < {aa,cc}") << QVariant(listAABB) << QVariant(listAACC) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {aa,bb} < {aa,cc} cs") << QVariant(listAABB) << QVariant(listAACC) << Qt::CaseSensitive << -1;

    QTest::newRow("stringlist {aa,cc} < {bb}") << QVariant(listAACC) << QVariant(listBB) << Qt::CaseInsensitive << -1;
    QTest::newRow("stringlist {aa,cc} < {bb} cs") << QVariant(listAACC) << QVariant(listBB) << Qt::CaseSensitive << -1;

    // equality
    QTest::newRow("stringlist {} = {}") << QVariant(empty) << QVariant(empty) << Qt::CaseInsensitive << 0;
    QTest::newRow("stringlist {} = {} cs") << QVariant(empty) << QVariant(empty) << Qt::CaseSensitive << 0;
    QTest::newRow("stringlist {aa} = {aa}") << QVariant(listAA) << QVariant(listAA) << Qt::CaseInsensitive << 0;
    QTest::newRow("stringlist {aa} = {AA}") << QVariant(listAA) << QVariant(listAA2) << Qt::CaseInsensitive << 0;
    QTest::newRow("stringlist {aa} = {aa} cs") << QVariant(listAA) << QVariant(listAA) << Qt::CaseSensitive << 0;

    // Times
    QTime t0;
    QTime t1(0,0,0,0);
    QTime t2(0,59,0,0);
    QTime t3(1,0,0,0);
    QTime t4(23,59,59,999);

    QTest::newRow("times t0 < t1") << QVariant(t0) << QVariant(t1) << Qt::CaseInsensitive << -1;
    QTest::newRow("times t1 < t2") << QVariant(t1) << QVariant(t2) << Qt::CaseInsensitive << -1;
    QTest::newRow("times t2 < t3") << QVariant(t2) << QVariant(t3) << Qt::CaseInsensitive << -1;
    QTest::newRow("times t3 < t4") << QVariant(t3) << QVariant(t4) << Qt::CaseInsensitive << -1;
    QTest::newRow("times t0 = t0") << QVariant(t0) << QVariant(t0) << Qt::CaseInsensitive << 0;
    QTest::newRow("times t4 = t4") << QVariant(t4) << QVariant(t4) << Qt::CaseInsensitive << 0;

    // Dates
    QDate d0;
    QDate d1 = QDate::fromJulianDay(1);
    QDate d2(1,1,1);
    QDate d3(2011,6,9);
    QDate d4 = QDate::fromJulianDay(0x7fffffff);
    QDate d5 = QDate::fromJulianDay(0x80000000);
    QDate d6 = QDate::fromJulianDay(0xffffffff);

    QTest::newRow("dates d0 < d1") << QVariant(d0) << QVariant(d1) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d1 < d2") << QVariant(d1) << QVariant(d2) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d2 < d3") << QVariant(d2) << QVariant(d3) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d3 < d4") << QVariant(d3) << QVariant(d4) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d4 < d5") << QVariant(d4) << QVariant(d5) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d5 < d6") << QVariant(d5) << QVariant(d6) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d0 < d6") << QVariant(d0) << QVariant(d6) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d1 < d6") << QVariant(d1) << QVariant(d6) << Qt::CaseInsensitive << -1;
    QTest::newRow("dates d0 = d0") << QVariant(d0) << QVariant(d0) << Qt::CaseInsensitive << 0;
    QTest::newRow("dates d1 = d1") << QVariant(d1) << QVariant(d1) << Qt::CaseInsensitive << 0;
    QTest::newRow("dates d2 = d2") << QVariant(d2) << QVariant(d2) << Qt::CaseInsensitive << 0;
    QTest::newRow("dates d3 = d3") << QVariant(d3) << QVariant(d3) << Qt::CaseInsensitive << 0;
    QTest::newRow("dates d4 = d4") << QVariant(d4) << QVariant(d4) << Qt::CaseInsensitive << 0;
    QTest::newRow("dates d5 = d5") << QVariant(d5) << QVariant(d5) << Qt::CaseInsensitive << 0;
    QTest::newRow("dates d6 = d6") << QVariant(d6) << QVariant(d6) << Qt::CaseInsensitive << 0;

    // DateTimes
    // Somewhat limited testing here
    QDateTime dt0;
    QDateTime dt1(d1, t1);
    QDateTime dt2(d1, t2);
    QDateTime dt3(d4, t4);
    QDateTime dt4(d5, t1);
    QDateTime dt5(d6, t4); // end of the universe

    QTest::newRow("datetimes dt1 < dt2") << QVariant(dt1) << QVariant(dt2) << Qt::CaseInsensitive << -1;
    QTest::newRow("datetimes dt2 < dt3") << QVariant(dt2) << QVariant(dt3) << Qt::CaseInsensitive << -1;
    QTest::newRow("datetimes dt3 < dt4") << QVariant(dt3) << QVariant(dt4) << Qt::CaseInsensitive << -1;
    QTest::newRow("datetimes dt4 < dt5") << QVariant(dt4) << QVariant(dt5) << Qt::CaseInsensitive << -1;
    QTest::newRow("datetimes dt0 < dt5") << QVariant(dt0) << QVariant(dt5) << Qt::CaseInsensitive << -1;
    QTest::newRow("datetimes dt1 < dt5") << QVariant(dt1) << QVariant(dt5) << Qt::CaseInsensitive << -1;
    QTest::newRow("datetimes dt0 = dt0") << QVariant(dt0) << QVariant(dt0) << Qt::CaseInsensitive << 0;
    QTest::newRow("datetimes dt1 = dt1") << QVariant(dt1) << QVariant(dt1) << Qt::CaseInsensitive << 0;
    QTest::newRow("datetimes dt2 = dt2") << QVariant(dt2) << QVariant(dt2) << Qt::CaseInsensitive << 0;
    QTest::newRow("datetimes dt3 = dt3") << QVariant(dt3) << QVariant(dt3) << Qt::CaseInsensitive << 0;
    QTest::newRow("datetimes dt4 = dt4") << QVariant(dt4) << QVariant(dt4) << Qt::CaseInsensitive << 0;
    QTest::newRow("datetimes dt5 = dt5") << QVariant(dt5) << QVariant(dt5) << Qt::CaseInsensitive << 0;

    // Uninitialized datetime now compares as the epoch date
    QTest::newRow("datetimes dt0 > dt1") << QVariant(dt0) << QVariant(dt1) << Qt::CaseInsensitive << 1;
}

void tst_QContactManager::constituentOfSelf()
{
    QScopedPointer<QContactManager> m(newContactManager());

    QContactId selfId(ContactId::contactId(m->selfContactId()));

    // Create a contact which is aggregated by the self contact
    QContactSyncTarget cst;
    cst.setSyncTarget("test");

    QContact constituent;
    QVERIFY(constituent.saveDetail(&cst));

    QVERIFY(m->saveContact(&constituent));
    QVERIFY(m->error() == QContactManager::NoError);

    // Find the aggregate contact created by saving
    QContactRelationshipFilter relationshipFilter;
    setFilterType(relationshipFilter, QContactRelationship::Aggregates);
    setFilterContact(relationshipFilter, constituent);
    relationshipFilter.setRelatedContactRole(QContactRelationship::Second);

    // Now connect our contact to the real self contact
    QContactRelationship relationship(makeRelationship(QContactRelationship::Aggregates, selfId, constituent.id()));
    QVERIFY(m->saveRelationship(&relationship));

    foreach (const QContact &aggregator, m->contacts(relationshipFilter)) {
        if (aggregator.id() != selfId) {
            // Remove the relationship between these contacts
            QContactRelationship relationship;
            relationship = makeRelationship(QContactRelationship::Aggregates, aggregator.id(), constituent.id());
            QVERIFY(m->removeRelationship(relationship));

            // The aggregator should have been removed
            QContact nonexistent = m->contact(retrievalId(aggregator));
            QVERIFY(m->error() == QContactManager::DoesNotExistError);
            QCOMPARE(nonexistent.id(), QContactId());
        }
    }

    // Update the constituent
    QContactNickname nn;
    nn.setNickname("nickname");

    constituent = m->contact(retrievalId(constituent));
    QVERIFY(constituent.saveDetail(&nn));

    QVERIFY(m->saveContact(&constituent));
    QVERIFY(m->error() == QContactManager::NoError);

    constituent = m->contact(retrievalId(constituent));
    QVERIFY(detailsSuperset(constituent.detail<QContactNickname>(), nn));

    // Change should be reflected in the self contact
    QContact self = m->contact(m->selfContactId());
    QVERIFY(detailsSuperset(self.detail<QContactNickname>(), nn));

    // Check that no new aggregate has been generated
    foreach (const QContact &aggregator, m->contacts(relationshipFilter))
        QCOMPARE(aggregator.id(), selfId);

    QContactStatusFlags flags = self.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsOnline), false);

    // Do a presence update
    QContactPresence presence;
    presence.setPresenceState(QContactPresence::PresenceAway);

    constituent = m->contact(retrievalId(constituent));
    QVERIFY(constituent.saveDetail(&presence));

    QVERIFY(m->saveContact(&constituent));
    QVERIFY(m->error() == QContactManager::NoError);

    constituent = m->contact(retrievalId(constituent));
    QVERIFY(detailsSuperset(constituent.detail<QContactPresence>(), presence));
    QCOMPARE(constituent.detail<QContactGlobalPresence>().presenceState(), presence.presenceState());

    // Update should be relected in the self contact
    self = m->contact(m->selfContactId());
    QCOMPARE(self.detail<QContactGlobalPresence>().presenceState(), presence.presenceState());

    flags = self.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsOnline), true);

    // Update again
    presence = constituent.detail<QContactPresence>();
    presence.setPresenceState(QContactPresence::PresenceBusy);
    QVERIFY(constituent.saveDetail(&presence));

    QVERIFY(m->saveContact(&constituent));
    QVERIFY(m->error() == QContactManager::NoError);

    constituent = m->contact(retrievalId(constituent));
    QVERIFY(detailsSuperset(constituent.detail<QContactPresence>(), presence));
    QCOMPARE(constituent.detail<QContactGlobalPresence>().presenceState(), presence.presenceState());

    self = m->contact(m->selfContactId());
    QCOMPARE(self.detail<QContactGlobalPresence>().presenceState(), presence.presenceState());

    // Check that no new aggregate has been generated
    foreach (const QContact &aggregator, m->contacts(relationshipFilter))
        QCOMPARE(aggregator.id(), selfId);

    flags = self.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsOnline), true);

    // Offline status makes the contact no longer offline
    presence = constituent.detail<QContactPresence>();
    presence.setPresenceState(QContactPresence::PresenceOffline);
    QVERIFY(constituent.saveDetail(&presence));

    QVERIFY(m->saveContact(&constituent));
    QVERIFY(m->error() == QContactManager::NoError);

    self = m->contact(m->selfContactId());
    QCOMPARE(self.detail<QContactGlobalPresence>().presenceState(), presence.presenceState());

    flags = self.detail<QContactStatusFlags>();
    QCOMPARE(flags.testFlag(QContactStatusFlags::IsOnline), false);

    // Add a name to the self contact
    QContactName n = self.detail<QContactName>();
    n.setFirstName("firstname");
    n.setLastName("lastname");
    self.saveDetail(&n);

    QVERIFY(m->saveContact(&self));
    QVERIFY(m->error() == QContactManager::NoError);

    // Create a new contact with a matching name
    QContact newContact;

    n = QContactName();
    n.setFirstName("firstname");
    n.setLastName("lastname");
    newContact.saveDetail(&n);

    QVERIFY(m->saveContact(&newContact));
    QVERIFY(m->error() == QContactManager::NoError);

    // Verify that the new contact was not aggregated into the self contact
    newContact = m->contact(retrievalId(newContact));
    QVERIFY(!relatedContactIds(newContact.relatedContacts()).contains(m->selfContactId()));
}

void tst_QContactManager::searchSensitivity()
{
    QScopedPointer<QContactManager> m(newContactManager());

    QContactDetailFilter exactMatch;
    setFilterDetail<QContactName>(exactMatch, QContactName::FieldFirstName);
    exactMatch.setMatchFlags(QContactFilter::MatchExactly);
    exactMatch.setValue("Ada");

    QContactDetailFilter exactMismatch;
    setFilterDetail<QContactName>(exactMismatch, QContactName::FieldFirstName);
    exactMismatch.setMatchFlags(QContactFilter::MatchExactly);
    exactMismatch.setValue("adA");

    QContactDetailFilter insensitiveMatch;
    setFilterDetail<QContactName>(insensitiveMatch, QContactName::FieldFirstName);
    insensitiveMatch.setMatchFlags(QContactFilter::MatchFixedString);
    insensitiveMatch.setValue("Ada");

    QContactDetailFilter insensitiveMismatch;
    setFilterDetail<QContactName>(insensitiveMismatch, QContactName::FieldFirstName);
    insensitiveMismatch.setMatchFlags(QContactFilter::MatchFixedString);
    insensitiveMismatch.setValue("adA");

    QContactDetailFilter sensitiveMatch;
    setFilterDetail<QContactName>(sensitiveMatch, QContactName::FieldFirstName);
    sensitiveMatch.setMatchFlags(QContactFilter::MatchFixedString | QContactFilter::MatchCaseSensitive);
    sensitiveMatch.setValue("Ada");

    QContactDetailFilter sensitiveMismatch;
    setFilterDetail<QContactName>(sensitiveMismatch, QContactName::FieldFirstName);
    sensitiveMismatch.setMatchFlags(QContactFilter::MatchFixedString | QContactFilter::MatchCaseSensitive);
    sensitiveMismatch.setValue("adA");

    int originalCount[6];
    originalCount[0] = m->contactIds(exactMatch).count();
    originalCount[1] = m->contactIds(exactMismatch).count();
    originalCount[2] = m->contactIds(insensitiveMatch).count();
    originalCount[3] = m->contactIds(insensitiveMismatch).count();
    originalCount[4] = m->contactIds(sensitiveMatch).count();
    originalCount[5] = m->contactIds(sensitiveMismatch).count();

#ifndef DETAIL_DEFINITION_SUPPORTED
    QContact ada = createContact("Ada", "Lovelace", "9876543");
#else
    QContactDetailDefinition nameDef = m->detailDefinition(QContactName::DefinitionName, QContactType::TypeContact);
    QContact ada = createContact(nameDef, "Ada", "Lovelace", "9876543");
#endif
    int currCount = m->contactIds().count();
    QVERIFY(m->saveContact(&ada));
    QVERIFY(m->error() == QContactManager::NoError);
    QVERIFY(!ada.id().managerUri().isEmpty());
    QVERIFY(ContactId::isValid(ada));
    QCOMPARE(m->contactIds().count(), currCount+1);

    QCOMPARE(m->contactIds(exactMatch).count(), originalCount[0] + 1);
    QCOMPARE(m->contactIds(exactMismatch).count(), originalCount[1]);
    QCOMPARE(m->contactIds(insensitiveMatch).count(), originalCount[2] + 1);
    QCOMPARE(m->contactIds(insensitiveMismatch).count(), originalCount[3] + 1);
    QCOMPARE(m->contactIds(sensitiveMatch).count(), originalCount[4] + 1);
    QCOMPARE(m->contactIds(sensitiveMismatch).count(), originalCount[5]);
}

QTEST_MAIN(tst_QContactManager)
#include "tst_qcontactmanager.moc"
