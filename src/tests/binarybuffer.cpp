/*
 *  tests/binarybuffer.cpp
 *
 *  Copyright (C) 2008 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "../binaryaccess.h"
#include "../binders.h"

#include "tests.h"

using namespace std;

void binaryBufferTest() throw () {
    void (BinaryBuffer<20>::*S8)(signed) = &BinaryBuffer<20>::S8;
    void (BinaryBuffer<20>::*S8_)(signed, int8_t, int8_t) = &BinaryBuffer<20>::S8;
    void (BinaryBuffer<20>::*S16)(signed) = &BinaryBuffer<20>::S16;

    BinaryBuffer<20> b1, b2;

    nAssert(b1.getCapacity() == 20);

    b1.U8(200);
    b1.S8(-100);
    b1.U16(65535);
    b1.S16(-1);
    b1.U32(0);
    b1.S32(-4);
    b1.constLengthStr("bl", 2);
    b1.str("st");
    nAssert(b1.getPosition() == 19);

    const uint8_t* data1 = b1.accessData();
    uint8_t* data2 = b2.accessData();
    memcpy(data2, data1, b1.getPosition());
    b2.setPosition(b1.getPosition());

    b1.U8(0);
    nAssert(b1.getPosition() == 20);
    testAssertion(VMFbind1(b1, S8, 0));

    b1.setPosition(0);
    testAssertion(VMFbind3(b1, S8_, -11, -10, -1));
    b1.S8(-10, -10, -1);
    b1.S8(-1, -10, -1);
    testAssertion(VMFbind3(b1, S8_, 0, -10, -1));
    b1.setPosition(0);
    for (int i = 0; i < 10; ++i)
        b1.S8(i, 0, 9);
    const double testFloat = .123456789123456789123456789;
    b1.dbl(testFloat);
    b1.S8(0);
    nAssert(b1.getPosition() == 19);
    testAssertion(VMFbind1(b1, S16, 0));

    BinaryDataBlockReader r1(b1);
    nAssert(r1.U8(0, 0) == 0);
    nAssert(r1.U64() == uint64_t(0x01020304) << 32 | 0x05060708);
    nAssert(r1.S8() == 9);
    nAssert(r1.dbl() == testFloat);
    b1.setPosition(0);
    nAssert(r1.S8() == 0);

    BinaryDataBlockReader r2(b2);
    nAssert(r2.U8(200, 200) == 200);
    nAssert(r2.S8(-128, -1) == -100);
    nAssert(r2.U16() == 65535);
    nAssert(r2.S16() == -1);
    try {
        r2.U32(1, 1000);
        nAssert(0);
    } catch (BinaryReader::DataOutOfRange) { }
    nAssert(r2.S32() == -4);
    nAssert(r2.constLengthStr(2) == "bl");
    nAssert(r2.str() == "st");
    try {
        r2.S8();
        nAssert(0);
    } catch (BinaryReader::ReadOutside) { }

    {
        static const unsigned K = 1024, M = K * K;
        static const uint32_t tests[] = { 0, 1, 239, 240, 3071, 3072, 128*K-1, 128*K, 16*M-1, 16*M, 0xFFFFFFFF };
        static const unsigned sizes[] = { 1, 1,   1,   2,    2,    3,       3,     4,      4,    5,          5, 99 };
        for (unsigned i = 0; sizes[i] != 99; ++i) {
            b1.clear();
            b1.U32dyn8(tests[i]);
            nAssert(b1.size() == sizes[i]);
            BinaryDataBlockReader r(b1);
            nAssert(r.U32dyn8() == tests[i]);
            nAssert(r.getPosition() == b1.size());
        }
    }
    {
        static const  int32_t tests[] = { 0, 1, 119, 120, 1535, 1536, 0x7FFFFFFF, -1, -120, -121, -1536, -1537, -(0x80000000) };
        static const unsigned sizes[] = { 1, 1,   1,   2,    2,    3,          5,  1,    1,    2,     2,     3,             5, 99 };
        for (unsigned i = 0; sizes[i] != 99; ++i) {
            b1.clear();
            b1.S32dyn8(tests[i]);
            nAssert(b1.size() == sizes[i]);
            BinaryDataBlockReader r(b1);
            nAssert(r.S32dyn8() == tests[i]);
            nAssert(r.getPosition() == b1.size());
        }
    }
    {
        static const unsigned K = 1024, M = K * K;
        static const uint32_t tests[] = { 0, 48*K-1, 48*K, 2*M-1, 2*M, 496*M-1, 496*M, 0xFFFFFFFF };
        static const unsigned sizes[] = { 2,      2,    3,     3,   4,       4,     5,          5, 99 };
        for (unsigned i = 0; sizes[i] != 99; ++i) {
            b1.clear();
            b1.U32dyn16(tests[i]);
            nAssert(b1.size() == sizes[i]);
            BinaryDataBlockReader r(b1);
            nAssert(r.U32dyn16() == tests[i]);
            nAssert(r.getPosition() == b1.size());
        }
    }
}

int main() {
    binaryBufferTest();
    return 0;
}
