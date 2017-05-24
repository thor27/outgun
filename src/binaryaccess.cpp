/*
 *  binaryaccess.cpp
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

#include <iostream>
#include <climits>
#include <cstring>
#include <cstdlib>

#include "binaryaccess.h"

using std::string;

float BinaryReader::flt() throw (ReadOutside) {
    STATIC_ASSERT(sizeof(uint32_t) == sizeof(float));
    union { uint32_t i; float f; } conversionHack;
    conversionHack.i = U32();
    return conversionHack.f;
}

double BinaryReader::dbl() throw (ReadOutside) {
    STATIC_ASSERT(sizeof(uint64_t) == sizeof(double));
    union { uint64_t i; double d; } conversionHack;
    conversionHack.i = U64();
    return conversionHack.d;
}

string BinaryReader::constLengthStr(unsigned length) throw (ReadOutside) {
    STATIC_ASSERT(CHAR_BIT == 8);
    return string(static_cast<const char*>(getBlock(length).data()), length);
}

string BinaryReader::str() throw (ReadOutside) {
    string s;
    for (;;) {
        const uint8_t byte = U8();
        if (byte == 0)
            return s;
        s += static_cast<char>(byte);
    }
}

void BinaryWriter::uncheckedU8(uint8_t wData) throw () {
    reserve(pos + 1);
    data[pos] = wData;
    ++pos;
}

void BinaryWriter::uncheckedU16(uint16_t wData) throw () {
    reserve(pos + 2);
    data[pos    ] = wData >> 8;
    data[pos + 1] = wData;
    pos += 2;
}

void BinaryWriter::uncheckedU24(uint32_t wData) throw () {
    reserve(pos + 3);
    data[pos    ] = wData >> 16;
    data[pos + 1] = wData >> 8;
    data[pos + 2] = wData;
    pos += 3;
}

void BinaryWriter::uncheckedU32(uint32_t wData) throw () {
    reserve(pos + 4);
    data[pos    ] = wData >> 24;
    data[pos + 1] = wData >> 16;
    data[pos + 2] = wData >>  8;
    data[pos + 3] = wData;
    pos += 4;
}

void BinaryWriter::U64(uint64_t wData) throw () {
    reserve(pos + 8);
    data[pos    ] = wData >> 56;
    data[pos + 1] = wData >> 48;
    data[pos + 2] = wData >> 40;
    data[pos + 3] = wData >> 32;
    data[pos + 4] = wData >> 24;
    data[pos + 5] = wData >> 16;
    data[pos + 6] = wData >>  8;
    data[pos + 7] = wData;
    pos += 8;
}

void BinaryWriter::flt(float wData) throw () {
    STATIC_ASSERT(sizeof(uint32_t) == sizeof(float));
    union { uint32_t i; float f; } conversionHack;
    conversionHack.f = wData;
    uncheckedU32(conversionHack.i);
}

void BinaryWriter::dbl(double wData) throw () {
    STATIC_ASSERT(sizeof(uint64_t) == sizeof(double));
    union { uint64_t i; double d; } conversionHack;
    conversionHack.d = wData;
    U64(conversionHack.i);
}

void BinaryWriter::constLengthStr(const string& wData, unsigned length) throw () {
    numAssert2(wData.length() == length, wData.length(), length);
    reserve(pos + length);
    for (unsigned i = 0; i < length; ++i)
        data[pos + i] = wData[i];
    pos += length;
}

void BinaryWriter::str(const string& wData) throw () {
    nAssert(wData.find_first_of('\0') == string::npos);
    constLengthStr(wData, wData.length());
    uncheckedU8(0);
}

void BinaryWriter::block(ConstDataBlockRef wData) throw () {
    if (wData.size() == 0)
        return;
    reserve(pos + wData.size());
    memcpy(data + pos, wData.data(), wData.size());
    pos += wData.size();
}

#define DEFINE_BOUND_METHODS(BasicType, RealType, name, uncheckedWriteMethod)                                  \
    RealType BinaryReader::name(RealType minBound, RealType maxBound) throw (ReadOutside, DataOutOfRange) {    \
        const RealType ret = name();                                                                           \
        if (ret < minBound || ret > maxBound)                                                                  \
            throw DataOutOfRange();                                                                            \
        return ret;                                                                                            \
    }                                                                                                          \
    void BinaryWriter::name(BasicType wData, RealType minBound, RealType maxBound) throw () {                  \
        numAssert3(wData >= minBound && wData <= maxBound, wData, minBound, maxBound);                         \
        uncheckedWriteMethod(static_cast<RealType>(wData));                                                    \
    }

#define DEFINE_METHODS_WITH_CHECKED(BasicType, RealType, name)                                                 \
    DEFINE_BOUND_METHODS(BasicType, RealType, name, unchecked##name)                                           \
    void BinaryWriter::name(BasicType wData) throw () {                                                        \
        name(wData, std::numeric_limits<RealType>::min(), std::numeric_limits<RealType>::max());               \
    }

#define DEFINE_METHODS_WITHOUT_CHECKED(BasicType, RealType, name)                                              \
    DEFINE_BOUND_METHODS(BasicType, RealType, name, name)

DEFINE_METHODS_WITH_CHECKED   (unsigned,  uint8_t, U8 )
DEFINE_METHODS_WITH_CHECKED   (  signed,   int8_t, S8 )
DEFINE_METHODS_WITH_CHECKED   (unsigned, uint16_t, U16)
DEFINE_METHODS_WITH_CHECKED   (  signed,  int16_t, S16)
DEFINE_METHODS_WITH_CHECKED   (unsigned, uint32_t, U32)
DEFINE_METHODS_WITH_CHECKED   (  signed,  int32_t, S32)
DEFINE_METHODS_WITHOUT_CHECKED(uint64_t, uint64_t, U64)
DEFINE_METHODS_WITHOUT_CHECKED( int64_t,  int64_t, S64)

DEFINE_METHODS_WITHOUT_CHECKED(float , float , flt)
DEFINE_METHODS_WITHOUT_CHECKED(double, double, dbl)

#undef DEFINE_BOUND_METHODS
#undef DEFINE_METHODS_WITHOUT_CHECKED
#undef DEFINE_METHODS_WITH_CHECKED

// 24-bit data requires special care since we don't have an appropriately sized built-in type
uint32_t BinaryReader::U24(uint32_t minBound, uint32_t maxBound) throw (ReadOutside, DataOutOfRange) {
    const uint32_t ret = U24();
    if (ret < minBound || ret > maxBound)
        throw DataOutOfRange();
    return ret;
}
void BinaryWriter::U24(unsigned wData, uint32_t minBound, uint32_t maxBound) throw () {
    nAssert(maxBound <= 0xFFFFFF);
    numAssert3(wData >= minBound && wData <= maxBound, wData, minBound, maxBound);
    uncheckedU24(static_cast<uint32_t>(wData));
}
void BinaryWriter::U24(unsigned wData) throw () {
    U24(wData, 0, 0xFFFFFF);
}
int32_t BinaryReader::S24(int32_t minBound, int32_t maxBound) throw (ReadOutside, DataOutOfRange) {
    const int32_t ret = S24();
    if (ret < minBound || ret > maxBound)
        throw DataOutOfRange();
    return ret;
}
void BinaryWriter::S24(signed wData, int32_t minBound, int32_t maxBound) throw () {
    nAssert(minBound >= -(0x800000) && maxBound <= 0x7FFFFF);
    numAssert3(wData >= minBound && wData <= maxBound, wData, minBound, maxBound);
    uncheckedS24(static_cast<int32_t>(wData));
}
void BinaryWriter::S24(signed wData) throw () {
    S24(wData, -(0x800000), 0x7FFFFF);
}

/* U32dyn8 encoding:
 *
 * xxxxxxxx  < 11110000
 * 1111xxxx xxxxxxxx  < 1100 00000000
 * 1111110x xxxxxxxx xxxxxxxx
 * 11111110 xxxxxxxx xxxxxxxx xxxxxxxx
 * 11111111 xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 */
uint32_t BinaryReader::U32dyn8() throw (ReadOutside) {
    const uint8_t b0 = U8();
    if      (b0 < 0xF0) return          b0;
    else if (b0 < 0xFC) return uint32_t(b0 & 0x0F) <<  8 | U8();
    else if (b0 < 0xFE) return uint32_t(b0 & 0x01) << 16 | U16();
    else if (b0 < 0xFF) return                             U24();
    else                return                             U32();
}
void BinaryWriter::U32dyn8(uint32_t wData) throw () {
    if      (wData <      0xF0) uncheckedU8 (             wData);
    else if (wData <     0xC00) uncheckedU16(0xF000     | wData);
    else if (wData <=  0x1FFFF) uncheckedU24(0xFC0000   | wData);
    else if (wData <= 0xFFFFFF) uncheckedU32(0xFE000000 | wData);
    else {
        uncheckedU8(0xFF);
        uncheckedU32(wData);
    }
}

/* U32dyn16 encoding:
 *
 * xxxxxxxx xxxxxxxx  < 11000000 00000000
 * 110xxxxx xxxxxxxx xxxxxxxx
 * 111xxxxx xxxxxxxx xxxxxxxx xxxxxxxx  < 11111 0...
 * 11111111 xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 */
uint32_t BinaryReader::U32dyn16() throw (ReadOutside) {
    const uint16_t d0 = U16();
    if      (d0 < 0xC000) return          d0;
    else if (d0 < 0xE000) return uint32_t(d0 & 0x1FFF) <<  8 | U8();
    else if (d0 < 0xFF00) return uint32_t(d0 & 0x1FFF) << 16 | U16();
    else                  return uint32_t(d0         ) << 24 | U24();
}
void BinaryWriter::U32dyn16(uint32_t wData) throw () {
    if      (wData <      0xC000) uncheckedU16(             wData);
    else if (wData <=   0x1FFFFF) uncheckedU24(0xC00000   | wData);
    else if (wData <  0x1F000000) uncheckedU32(0xE0000000 | wData);
    else {
        uncheckedU8(0xFF);
        uncheckedU32(wData);
    }
}

/* Signed dynamic length types:
 *
 * A trick is performed to save on code complexity: The sign bit is moved to the low end, and
 * bits are flipped for negative numbers so that smaller absolute values always have 0s in front.
 * When processed like that, the unsigned storage code works just fine.
 */
int32_t BinaryReader::decodeSignedDynamic(uint32_t value) {
    return int32_t((value >> 1) ^ (0xFFFFFFFF * (value & 1)));
    /* equivalent code:
    if (value & 1)
        return int32_t(~(value >> 1));
    else
        return int32_t(  value >> 1 );
    */
}
uint32_t BinaryWriter::encodeSignedDynamic(int32_t value) {
    return uint32_t(uint32_t(value) << 1) ^ (0xFFFFFFFF * (uint32_t(value) >> 31));
    /* equivalent code:
    if (value < 0)
        return ~(uint32_t(value) << 1);
    else
        return   uint32_t(value) << 1 ;
    */
}

uint8_t BinaryDataBlockReader::getU8() throw (ReadOutside) {
    if (pos + 1 > dataLength)
        throw ReadOutside();
    return data[pos++];
}

uint16_t BinaryDataBlockReader::getU16() throw (ReadOutside) {
    if (pos + 2 > dataLength)
        throw ReadOutside();
    pos += 2;
    return (uint16_t(data[pos - 2]) << 8) | uint16_t(data[pos - 1]);
}

uint32_t BinaryDataBlockReader::getU24() throw (ReadOutside) {
    if (pos + 3 > dataLength)
        throw ReadOutside();
    pos += 3;
    return (uint32_t(data[pos - 3]) << 16) | (uint32_t(data[pos - 2]) << 8) | uint32_t(data[pos - 1]);
}

uint32_t BinaryDataBlockReader::getU32() throw (ReadOutside) {
    if (pos + 4 > dataLength)
        throw ReadOutside();
    pos += 4;
    return (uint32_t(data[pos - 4]) << 24) | (uint32_t(data[pos - 3]) << 16) | (uint32_t(data[pos - 2]) << 8) | uint32_t(data[pos - 1]);
}

uint64_t BinaryDataBlockReader::getU64() throw (ReadOutside) {
    if (pos + 8 > dataLength)
        throw ReadOutside();
    pos += 8;
    return (uint64_t(data[pos - 8]) << 56) | (uint64_t(data[pos - 7]) << 48) | (uint64_t(data[pos - 6]) << 40) | (uint64_t(data[pos - 5]) << 32)
         | (uint64_t(data[pos - 4]) << 24) | (uint64_t(data[pos - 3]) << 16) | (uint64_t(data[pos - 2]) <<  8) |  uint64_t(data[pos - 1]);
}

ConstDataBlockRef BinaryDataBlockReader::getBlock(unsigned length) throw (ReadOutside) {
    if (pos + length > dataLength)
        throw ReadOutside();
    pos += length;
    return ConstDataBlockRef(data + pos - length, length);
}

ConstDataBlockRef BinaryDataBlockReader::getBlockUpTo(unsigned length) throw () {
    if (pos + length > dataLength)
        length = dataLength - pos;
    pos = dataLength;
    return ConstDataBlockRef(data + pos - length, length);
}

void BinaryDataBlockReader::storeBlock(DataBlockRef buffer) throw (ReadOutside) {
    ConstDataBlockRef data = getBlock(buffer.size());
    memcpy(buffer.data(), data.data(), buffer.size());
}

ConstDataBlockRef BinaryDataBlockReader::storeBlockUpTo(DataBlockRef buffer) throw () {
    const unsigned length = std::min(buffer.size(), dataLength - pos);
    memcpy(buffer.data(), data + pos, length);
    pos += length;
    return ConstDataBlockRef(buffer.data(), length);
}

STATIC_ASSERT(CHAR_BIT == 8); // the whole of BinaryStreamReader depends on this

void BinaryStreamReader::setPosition(unsigned position) throw () {
    stream.clear();
    stream.seekg(position);
}

unsigned BinaryStreamReader::getPosition() const throw () {
    return stream.tellg();
}

bool BinaryStreamReader::hasMore() const throw () {
    return !stream.eof();
}

uint8_t BinaryStreamReader::getU8() throw (ReadOutside) {
    uint8_t buf[1];
    stream.read(reinterpret_cast<char*>(buf), 1);
    if (!stream)
        throw ReadOutside();
    return buf[0];
}

uint16_t BinaryStreamReader::getU16() throw (ReadOutside) {
    uint8_t buf[2];
    stream.read(reinterpret_cast<char*>(buf), 2);
    if (!stream)
        throw ReadOutside();
    return (uint16_t(buf[0]) << 8) | uint16_t(buf[1]);
}

uint32_t BinaryStreamReader::getU24() throw (ReadOutside) {
    uint8_t buf[3];
    stream.read(reinterpret_cast<char*>(buf), 3);
    if (!stream)
        throw ReadOutside();
    return (uint32_t(buf[0]) << 16) | (uint32_t(buf[1]) << 8) | uint32_t(buf[2]);
}

uint32_t BinaryStreamReader::getU32() throw (ReadOutside) {
    uint8_t buf[4];
    stream.read(reinterpret_cast<char*>(buf), 4);
    if (!stream)
        throw ReadOutside();
    return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) | (uint32_t(buf[2]) << 8) | uint32_t(buf[3]);
}

uint64_t BinaryStreamReader::getU64() throw (ReadOutside) {
    uint8_t buf[8];
    stream.read(reinterpret_cast<char*>(buf), 8);
    if (!stream)
        throw ReadOutside();
    return (uint64_t(buf[0]) << 56) | (uint64_t(buf[1]) << 48) | (uint64_t(buf[2]) << 40) | (uint64_t(buf[3]) << 32)
         | (uint64_t(buf[4]) << 24) | (uint64_t(buf[5]) << 16) | (uint64_t(buf[6]) <<  8) |  uint64_t(buf[7]);
}

ConstDataBlockRef BinaryStreamReader::getBlock(unsigned length) throw (ReadOutside) {
    delete[] temporaryBuffer;
    temporaryBuffer = new char[length];
    stream.read(temporaryBuffer, length);
    if (!stream) {
        delete[] temporaryBuffer;
        temporaryBuffer = 0;
        throw ReadOutside();
    }
    return ConstDataBlockRef(temporaryBuffer, length);
}

void BinaryStreamReader::storeBlock(DataBlockRef buffer) throw (ReadOutside) {
    stream.read(static_cast<char*>(buffer.data()), buffer.size());
    if (!stream)
        throw ReadOutside();
}

ConstDataBlockRef BinaryStreamReader::getBlockUpTo(unsigned length) throw () {
    delete[] temporaryBuffer;
    temporaryBuffer = new char[length];
    stream.read(temporaryBuffer, length);
    return ConstDataBlockRef(temporaryBuffer, stream.gcount());
}

ConstDataBlockRef BinaryStreamReader::storeBlockUpTo(DataBlockRef buffer) throw () {
    stream.read(static_cast<char*>(buffer.data()), buffer.size());
    return ConstDataBlockRef(buffer.data(), stream.gcount());
}

void ExpandingBinaryBuffer::reallocate(unsigned capacityRequired) throw () {
    nAssert(capacityRequired > capacity);
    capacity = std::max(capacity * 2, capacityRequired);
    uint8_t* const newPtr = static_cast<uint8_t*>(realloc(data, capacity));
    if (!newPtr) {
        volatile unsigned s = 0xFFFFFFFF;
        new char[s]; // try to provoke normal out of memory behaviour
        nAssert(0);
    }
    data = newPtr;
}

ExpandingBinaryBuffer::ExpandingBinaryBuffer() throw () :
    BinaryWriter(0, 0)
{
    reallocate(100);
}

ExpandingBinaryBuffer::ExpandingBinaryBuffer(const ExpandingBinaryBuffer& o) throw () :
    BinaryWriter(o)
{
    data = static_cast<uint8_t*>(malloc(capacity));
    memcpy(data, o.data, capacity);
}

ExpandingBinaryBuffer::~ExpandingBinaryBuffer() throw () {
    free(data);
}

ExpandingBinaryBuffer& ExpandingBinaryBuffer::operator=(const ExpandingBinaryBuffer& o) throw () {
    if (capacity < o.capacity)
        reallocate(o.capacity);
    memcpy(data, o.data, o.capacity);
    pos = o.pos;
    return *this;
}
