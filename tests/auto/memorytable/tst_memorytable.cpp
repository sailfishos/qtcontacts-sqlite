/*
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Matt Vogt <matthew.vogt@jollamobile.com>
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

#include "../../util.h"
#include "../../../src/engine/memorytable_p.h"

#include <QDateTime>

#include <cstring>

class tst_MemoryTable : public QObject
{
Q_OBJECT

public:
    tst_MemoryTable();
    virtual ~tst_MemoryTable();

public slots:
    void init();
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

private slots:
    void invalidInitialization();
    void initialization();
    void noninitialization();
    void addressIndependence();
    void basicOperation();
    void reinsertion();
    void repeatedReinsertion();
    void orderedReinsertion();
    void replacement();
    void migration();
    void iteration();

private:
    char *testBuffer(size_t length);
};

tst_MemoryTable::tst_MemoryTable()
{
}

tst_MemoryTable::~tst_MemoryTable()
{
}

void tst_MemoryTable::init()
{
}

void tst_MemoryTable::initTestCase()
{
}

void tst_MemoryTable::cleanup()
{
}

void tst_MemoryTable::cleanupTestCase()
{
}

char *tst_MemoryTable::testBuffer(size_t length)
{
    Q_ASSERT((length % 16) == 0);
    char* buf(new char[length]);

    // Fill the buffer with garbage
    qsrand(static_cast<quint32>(QDateTime::currentDateTime().toMSecsSinceEpoch()));
    for (char *p = buf, *end = p + length; p != end; p += sizeof(uint)) {
        *(reinterpret_cast<uint *>(p)) = qrand();
    }

    return buf;
}

void tst_MemoryTable::invalidInitialization()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    {
        MemoryTable mt(0, 128, true);
        QCOMPARE(mt.isValid(), false);
    }
    {
        MemoryTable mt(buf.data() + 1, 128, true);
        QCOMPARE(mt.isValid(), false);
    }
    {
        MemoryTable mt(buf.data() + 2, 128, true);
        QCOMPARE(mt.isValid(), false);
    }
    {
        MemoryTable mt(buf.data(), 0, true);
        QCOMPARE(mt.isValid(), false);
    }
    {
        MemoryTable mt(buf.data(), 8, true);
        QCOMPARE(mt.isValid(), false);
    }
}

void tst_MemoryTable::initialization()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(0), false);
}

void tst_MemoryTable::noninitialization()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    {
        MemoryTable mt(buf.data(), 128, true);
        QCOMPARE(mt.isValid(), true);

        QCOMPARE(mt.count(), static_cast<size_t>(0u));
        QCOMPARE(mt.contains(0), false);

        // Add some data
        QCOMPARE(mt.insert(1, QByteArray("abc")), MemoryTable::NoError);
        QCOMPARE(mt.insert(2, QByteArray("def")), MemoryTable::NoError);
        QCOMPARE(mt.insert(3, QByteArray("efg")), MemoryTable::NoError);

        QCOMPARE(mt.count(), static_cast<size_t>(3u));
        QCOMPARE(mt.contains(1), true);
        QCOMPARE(mt.contains(2), true);
        QCOMPARE(mt.contains(3), true);
        QCOMPARE(mt.value(1), QByteArray("abc"));
        QCOMPARE(mt.value(2), QByteArray("def"));
        QCOMPARE(mt.value(3), QByteArray("efg"));
    }
    {
        // Inspect the same data
        MemoryTable mt(buf.data(), 128, false);
        QCOMPARE(mt.isValid(), true);

        QCOMPARE(mt.count(), static_cast<size_t>(3u));
        QCOMPARE(mt.contains(1), true);
        QCOMPARE(mt.contains(2), true);
        QCOMPARE(mt.contains(3), true);
        QCOMPARE(mt.value(1), QByteArray("abc"));
        QCOMPARE(mt.value(2), QByteArray("def"));
        QCOMPARE(mt.value(3), QByteArray("efg"));
    }
}

void tst_MemoryTable::addressIndependence()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    {
        MemoryTable mt(buf.data(), 128, true);
        QCOMPARE(mt.isValid(), true);

        QCOMPARE(mt.count(), static_cast<size_t>(0u));
        QCOMPARE(mt.contains(0), false);

        // Add some data
        QCOMPARE(mt.insert(1, QByteArray("abc")), MemoryTable::NoError);
        QCOMPARE(mt.insert(2, QByteArray("def")), MemoryTable::NoError);
        QCOMPARE(mt.insert(3, QByteArray("efg")), MemoryTable::NoError);

        QCOMPARE(mt.count(), static_cast<size_t>(3u));
        QCOMPARE(mt.contains(1), true);
        QCOMPARE(mt.contains(2), true);
        QCOMPARE(mt.contains(3), true);
        QCOMPARE(mt.value(1), QByteArray("abc"));
        QCOMPARE(mt.value(2), QByteArray("def"));
        QCOMPARE(mt.value(3), QByteArray("efg"));
    }

    // Copy the data to a different address
    QScopedArrayPointer<char> buf2(testBuffer(128));
    std::memcpy(buf2.data(), buf.data(), 128);

    {
        // Inspect the copied data
        MemoryTable mt(buf2.data(), 128, false);
        QCOMPARE(mt.isValid(), true);

        QCOMPARE(mt.count(), static_cast<size_t>(3u));
        QCOMPARE(mt.contains(1), true);
        QCOMPARE(mt.contains(2), true);
        QCOMPARE(mt.contains(3), true);
        QCOMPARE(mt.value(1), QByteArray("abc"));
        QCOMPARE(mt.value(2), QByteArray("def"));
        QCOMPARE(mt.value(3), QByteArray("efg"));

        // Modify the data
        QCOMPARE(mt.remove(1), true);
        QCOMPARE(mt.count(), static_cast<size_t>(2u));
        QCOMPARE(mt.contains(1), false);
        QCOMPARE(mt.contains(2), true);
        QCOMPARE(mt.contains(3), true);
        QCOMPARE(mt.value(1), QByteArray());
        QCOMPARE(mt.value(2), QByteArray("def"));
        QCOMPARE(mt.value(3), QByteArray("efg"));

        QCOMPARE(mt.remove(2), true);
        QCOMPARE(mt.count(), static_cast<size_t>(1u));
        QCOMPARE(mt.contains(1), false);
        QCOMPARE(mt.contains(2), false);
        QCOMPARE(mt.contains(3), true);
        QCOMPARE(mt.value(1), QByteArray());
        QCOMPARE(mt.value(2), QByteArray());
        QCOMPARE(mt.value(3), QByteArray("efg"));

        QCOMPARE(mt.remove(3), true);
        QCOMPARE(mt.count(), static_cast<size_t>(0u));
        QCOMPARE(mt.contains(1), false);
        QCOMPARE(mt.contains(2), false);
        QCOMPARE(mt.contains(3), false);
        QCOMPARE(mt.value(1), QByteArray());
        QCOMPARE(mt.value(2), QByteArray());
        QCOMPARE(mt.value(3), QByteArray());
    }
}

void tst_MemoryTable::basicOperation()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(0), false);
    QCOMPARE(mt.contains(1), false);

    QCOMPARE(mt.insert(1, QByteArray()), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));
    QCOMPARE(mt.contains(0), false);
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.contains(2), false);
    QCOMPARE(mt.value(0), QByteArray());
    QCOMPARE(mt.value(1), QByteArray());

    QByteArray ba("test byte array");
    QCOMPARE(mt.insert(2, ba), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(0), false);
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.contains(3), false);
    QCOMPARE(mt.value(0), QByteArray());
    QCOMPARE(mt.value(1), QByteArray());
    QCOMPARE(mt.value(2), ba);

    QCOMPARE(mt.remove(0), false);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));

    QCOMPARE(mt.remove(1), true);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));
    QCOMPARE(mt.contains(0), false);
    QCOMPARE(mt.contains(1), false);
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.contains(3), false);
    QCOMPARE(mt.value(0), QByteArray());
    QCOMPARE(mt.value(1), QByteArray());
    QCOMPARE(mt.value(2), ba);

    QCOMPARE(mt.remove(1), false);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));

    QCOMPARE(mt.remove(2), true);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(0), false);
    QCOMPARE(mt.contains(1), false);
    QCOMPARE(mt.contains(2), false);
    QCOMPARE(mt.contains(3), false);
    QCOMPARE(mt.value(0), QByteArray());
    QCOMPARE(mt.value(1), QByteArray());
    QCOMPARE(mt.value(2), QByteArray());

    QCOMPARE(mt.remove(2), false);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));

    // An impossible allocation should not disrupt the table
    QCOMPARE(mt.insert(3, QByteArray(1, 'x')), MemoryTable::NoError);
    QCOMPARE(mt.insert(2, QByteArray(128, 'y')), MemoryTable::InsufficientSpace);
    QCOMPARE(mt.insert(1, QByteArray(1, 'z')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(0), false);
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.contains(2), false);
    QCOMPARE(mt.contains(3), true);
    QCOMPARE(mt.value(0), QByteArray());
    QCOMPARE(mt.value(1), QByteArray(1, 'z'));
    QCOMPARE(mt.value(2), QByteArray());
    QCOMPARE(mt.value(3), QByteArray(1, 'x'));
}

void tst_MemoryTable::reinsertion()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(1), false);

    QByteArray ba(65, 'x');

    QCOMPARE(mt.insert(1, ba), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.value(1), ba);

    QCOMPARE(mt.remove(1), true);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(1), false);
    QCOMPARE(mt.value(1), QByteArray());

    // Reinsert - if we haven't reclaimed the space, this will fail
    QCOMPARE(mt.insert(1, ba), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.value(1), ba);
}

void tst_MemoryTable::repeatedReinsertion()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(1), false);

    QByteArray ba(8, 'x');

    // Insert items until no space is available
    quint32 count[3] = { 0 };
    quint32 key = 0u;
    MemoryTable::Error e;
    while ((e = mt.insert(key, ba)) == MemoryTable::NoError) {
        ++key;
        ++count[0];
    }
    QCOMPARE(e, MemoryTable::InsufficientSpace);
    QCOMPARE(mt.count(), static_cast<size_t>(count[0]));

    // Remove all items
    key = 0u;
    while (count[1] != count[0]) {
        QCOMPARE(mt.contains(key), true);
        QCOMPARE(mt.value(key), ba);
        QCOMPARE(mt.remove(key), true);
        QCOMPARE(mt.contains(key), false);
        ++key;
        ++count[1];
    }
    QCOMPARE(count[1], count[0]);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));

    // Insert the items back
    key = 0u;
    while ((e = mt.insert(key, ba)) == MemoryTable::NoError) {
        QCOMPARE(mt.value(key), ba);
        ++key;
        ++count[2];
    }
    QCOMPARE(e, MemoryTable::InsufficientSpace);
    QCOMPARE(mt.count(), static_cast<size_t>(count[2]));

    // We should have been able to insert the same number of items
    QCOMPARE(count[2], count[0]);
}

void tst_MemoryTable::orderedReinsertion()
{
    QScopedArrayPointer<char> buf(testBuffer(256));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(1), false);

    // Insert items until no space is available
    quint32 count[5] = { 0 };
    quint32 key = 0u;
    MemoryTable::Error e;
    while ((e = mt.insert(key, QByteArray(key * 8, 'x'))) == MemoryTable::NoError) {
        ++key;
        ++count[0];
    }
    QCOMPARE(e, MemoryTable::InsufficientSpace);
    QCOMPARE(mt.count(), static_cast<size_t>(count[0]));

    // Remove all items, in reverse insertion order
    --key;
    while (count[1] != count[0]) {
        QCOMPARE(mt.contains(key), true);
        QCOMPARE(mt.value(key), QByteArray(key * 8, 'x'));
        QCOMPARE(mt.remove(key), true);
        QCOMPARE(mt.contains(key), false);
        --key;
        ++count[1];
    }
    QCOMPARE(count[1], count[0]);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));

    // Insert the items back
    key = 0u;
    while ((e = mt.insert(key, QByteArray(key * 8, 'y'))) == MemoryTable::NoError) {
        QCOMPARE(mt.value(key), QByteArray(key * 8, 'y'));
        ++key;
        ++count[2];
    }
    QCOMPARE(e, MemoryTable::InsufficientSpace);
    QCOMPARE(mt.count(), static_cast<size_t>(count[2]));

    // We should have been able to insert the same number of items
    QCOMPARE(count[2], count[0]);

    // Remove all items, in original insertion order
    key = 0u;
    while (count[3] != count[0]) {
        QCOMPARE(mt.contains(key), true);
        QCOMPARE(mt.value(key), QByteArray(key * 8, 'y'));
        QCOMPARE(mt.remove(key), true);
        QCOMPARE(mt.contains(key), false);
        ++key;
        ++count[3];
    }
    QCOMPARE(count[3], count[0]);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));

    // Insert the items back
    key = 0u;
    while ((e = mt.insert(key, QByteArray(key * 8, 'z'))) == MemoryTable::NoError) {
        QCOMPARE(mt.value(key), QByteArray(key * 8, 'z'));
        ++key;
        ++count[4];
    }
    QCOMPARE(e, MemoryTable::InsufficientSpace);
    QCOMPARE(mt.count(), static_cast<size_t>(count[4]));

    // We should have been able to insert the same number of items
    QCOMPARE(count[4], count[0]);
}

void tst_MemoryTable::replacement()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(1), false);

    QCOMPARE(mt.insert(1, QByteArray(1, 'x')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.value(1), QByteArray(1, 'x'));

    QCOMPARE(mt.insert(2, QByteArray(10, 'y')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.value(2), QByteArray(10, 'y'));

    // Replacement with a larger value requires a new allocation (68 bytes consumes all available space)
    QCOMPARE(mt.insert(2, QByteArray(68, 'y')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.value(2), QByteArray(68, 'y'));

    // Replacement with a smaller value uses the same allocation
    QCOMPARE(mt.insert(2, QByteArray(40, 'y')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.value(2), QByteArray(40, 'y'));

    QCOMPARE(mt.insert(2, QByteArray(60, 'y')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.value(2), QByteArray(60, 'y'));

    // Replacement with a larger value still fits
    QCOMPARE(mt.insert(2, QByteArray(68, 'y')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.value(2), QByteArray(68, 'y'));

    // Replacement may fail because we can't allocate more space
    QCOMPARE(mt.insert(2, QByteArray(69, 'y')), MemoryTable::InsufficientSpace);

    // Insertion may fail because we can't expand the index, even though we have a
    // large enough free block from the earlier replacement
    QCOMPARE(mt.insert(3, QByteArray(10, 'z')), MemoryTable::InsufficientSpace);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(1), true);
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.contains(3), false);

    // Remove another item to free up the index space
    QCOMPARE(mt.remove(1), true);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));
    QCOMPARE(mt.contains(1), false);
    QCOMPARE(mt.contains(2), true);
    QCOMPARE(mt.contains(3), false);

    // Insertion now succeeds
    QCOMPARE(mt.insert(3, QByteArray(10, 'z')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));
    QCOMPARE(mt.contains(3), true);
    QCOMPARE(mt.value(3), QByteArray(10, 'z'));

    // Free up all the space
    QCOMPARE(mt.remove(2), true);
    QCOMPARE(mt.remove(3), true);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));
    QCOMPARE(mt.contains(2), false);
    QCOMPARE(mt.contains(3), false);

    // Free list space remains fragmented after removing all items
    QCOMPARE(mt.insert(1, QByteArray(69, 'x')), MemoryTable::InsufficientSpace);
}

void tst_MemoryTable::migration()
{
    QScopedArrayPointer<char> buf(testBuffer(1024));
    QScopedArrayPointer<char> buf2(testBuffer(1024));

    quint32 seed = static_cast<quint32>(QDateTime::currentDateTime().toMSecsSinceEpoch());
    qDebug() << "Randomized test - seed:" << seed;
    qsrand(seed);

    for (int i = 0; i < 10; ++i) {
        MemoryTable mt(buf.data(), 1024, true);

        // Populate the table
        quint32 key = 0u;
        MemoryTable::Error e;
        while ((e = mt.insert(key, QByteArray(qrand() % 64, 'x'))) == MemoryTable::NoError) {
            if ((qrand() % 2) == 0) {
                // Update this key, possibly causing re-allocation of the block
                ++key;
            }
        }
        QCOMPARE(e, MemoryTable::InsufficientSpace);

        // Migrate to the next table
        MemoryTable mt2(buf2.data(), 1024, true);
        QCOMPARE(mt.migrateTo(mt2), MemoryTable::NoError);

        do {
            --key;
            QCOMPARE(mt2.contains(key), true);
            QCOMPARE(mt2.value(key), mt.value(key));
        } while (key != 0u);
    }
}

void tst_MemoryTable::iteration()
{
    QScopedArrayPointer<char> buf(testBuffer(128));

    MemoryTable mt(buf.data(), 128, true);
    QCOMPARE(mt.isValid(), true);

    QCOMPARE(mt.count(), static_cast<size_t>(0u));

    MemoryTable::const_iterator it, end;

    it = mt.constBegin();
    end = mt.constEnd();
    QVERIFY(it == end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(0));

    QCOMPARE(mt.insert(1, QByteArray()), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));

    it = mt.constBegin();
    end = mt.constEnd();
    QVERIFY(it != end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(1));
    QCOMPARE(static_cast<int>(it.key()), 1);
    QCOMPARE(it.value(), QByteArray());
    ++it;
    QVERIFY(it == end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(0));

    QByteArray ba("test byte array");
    QCOMPARE(mt.insert(2, ba), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));

    it = mt.constBegin();
    end = mt.constEnd();
    QVERIFY(it != end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(2));
    QCOMPARE(static_cast<int>(it.key()), 1);
    QCOMPARE(it.value(), QByteArray());
    ++it;
    QVERIFY(it != end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(1));
    QCOMPARE(static_cast<int>(it.key()), 2);
    QCOMPARE(it.value(), ba);
    ++it;
    QVERIFY(it == end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(0));

    QCOMPARE(mt.remove(1), true);
    QCOMPARE(mt.count(), static_cast<size_t>(1u));

    it = mt.constBegin();
    end = mt.constEnd();
    QVERIFY(it != end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(1));
    QCOMPARE(static_cast<int>(it.key()), 2);
    QCOMPARE(it.value(), ba);
    ++it;
    QVERIFY(it == end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(0));

    QCOMPARE(mt.remove(2), true);
    QCOMPARE(mt.count(), static_cast<size_t>(0u));

    it = mt.constBegin();
    end = mt.constEnd();
    QVERIFY(it == end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(0));

    // An impossible allocation should not disrupt the iteration
    QCOMPARE(mt.insert(3, QByteArray(1, 'x')), MemoryTable::NoError);
    QCOMPARE(mt.insert(2, QByteArray(128, 'y')), MemoryTable::InsufficientSpace);
    QCOMPARE(mt.insert(1, QByteArray(1, 'z')), MemoryTable::NoError);
    QCOMPARE(mt.count(), static_cast<size_t>(2u));

    it = mt.constBegin();
    end = mt.constEnd();
    QVERIFY(it != end);
    QCOMPARE(static_cast<int>(it.key()), 1);
    QCOMPARE(it.value(), QByteArray(1, 'z'));
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(2));
    ++it;
    QVERIFY(it != end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(1));
    QCOMPARE(static_cast<int>(it.key()), 3);
    QCOMPARE(it.value(), QByteArray(1, 'x'));
    ++it;
    QVERIFY(it == end);
    QCOMPARE(std::distance(it, end), static_cast<std::ptrdiff_t>(0));
}

QTEST_GUILESS_MAIN(tst_MemoryTable)
#include "tst_memorytable.moc"
