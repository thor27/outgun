/*
 *  binaryaccess.h
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

#ifndef BINARYACCESS_H_INC
#define BINARYACCESS_H_INC

#include <stdint.h>
#include <string>

#include "nassert.h"
#include "utility.h"

class BinaryReader {
public:
    class ReadOutside { };
    class DataOutOfRange { };

    virtual ~BinaryReader() throw () { }

    virtual bool hasMore() const throw () = 0;

     uint8_t U8 () throw (ReadOutside) { return getU8 (); }
      int8_t S8 () throw (ReadOutside) { return static_cast<int8_t >(U8 ()); }
    uint16_t U16() throw (ReadOutside) { return getU16(); }
     int16_t S16() throw (ReadOutside) { return static_cast<int16_t>(U16()); }
    uint32_t U24() throw (ReadOutside) { return getU24(); }
     int32_t S24() throw (ReadOutside) { return static_cast<int32_t>(U24() ^ 0x800000) - 0x800000; }
    uint32_t U32() throw (ReadOutside) { return getU32(); }
     int32_t S32() throw (ReadOutside) { return static_cast<int32_t>(U32()); }
    uint64_t U64() throw (ReadOutside) { return getU64(); }
     int64_t S64() throw (ReadOutside) { return static_cast<int64_t>(U64()); }

    // see the comment about dynamic length datatypes in BinaryWriter
    uint32_t U32dyn8 () throw (ReadOutside);
     int32_t S32dyn8 () throw (ReadOutside) { return decodeSignedDynamic(U32dyn8()); }
    uint32_t U32dyn16() throw (ReadOutside);
     int32_t S32dyn16() throw (ReadOutside) { return decodeSignedDynamic(U32dyn16()); }

    // argument choice methods for dynamic or fixed length
    uint32_t U32dyn8orU8  (bool dyn) throw (ReadOutside) { return dyn ? U32dyn8 () : U8 (); }
     int32_t S32dyn8orS8  (bool dyn) throw (ReadOutside) { return dyn ? S32dyn8 () : S8 (); }
    uint32_t U32dyn16orU16(bool dyn) throw (ReadOutside) { return dyn ? U32dyn16() : U16(); }
     int32_t S32dyn16orS16(bool dyn) throw (ReadOutside) { return dyn ? S32dyn16() : S16(); }

    float  flt() throw (ReadOutside);
    double dbl() throw (ReadOutside);

     uint8_t U8 ( uint8_t minBound,  uint8_t maxBound) throw (ReadOutside, DataOutOfRange);
      int8_t S8 (  int8_t minBound,   int8_t maxBound) throw (ReadOutside, DataOutOfRange);
    uint16_t U16(uint16_t minBound, uint16_t maxBound) throw (ReadOutside, DataOutOfRange);
     int16_t S16( int16_t minBound,  int16_t maxBound) throw (ReadOutside, DataOutOfRange);
    uint32_t U24(uint32_t minBound, uint32_t maxBound) throw (ReadOutside, DataOutOfRange);
     int32_t S24( int32_t minBound,  int32_t maxBound) throw (ReadOutside, DataOutOfRange);
    uint32_t U32(uint32_t minBound, uint32_t maxBound) throw (ReadOutside, DataOutOfRange);
     int32_t S32( int32_t minBound,  int32_t maxBound) throw (ReadOutside, DataOutOfRange);
    uint64_t U64(uint64_t minBound, uint64_t maxBound) throw (ReadOutside, DataOutOfRange);
     int64_t S64( int64_t minBound,  int64_t maxBound) throw (ReadOutside, DataOutOfRange);

    float  flt(float  minBound, float  maxBound) throw (ReadOutside, DataOutOfRange);
    double dbl(double minBound, double maxBound) throw (ReadOutside, DataOutOfRange);

    std::string constLengthStr(unsigned length) throw (ReadOutside);
    std::string str() throw (ReadOutside);

    void block(DataBlockRef buffer) throw (ReadOutside) { storeBlock(buffer); }
    ConstDataBlockRef blockUpTo(DataBlockRef buffer) throw () { return storeBlockUpTo(buffer); } // make sure to check from the return value, how much was read
    /* If you have access to the final storage for the data, the block reading methods
     * above are potentially more efficient.
     * Trying to read with the following methods much more than is likely available
     * isn't smart, since they might allocate storage for the given length. Repeat
     * reading of manageable sized blocks if a large amount is required.
     * The returned block reference is only valid until the next non-const operation.
     */
    ConstDataBlockRef block(unsigned length) throw (ReadOutside) { return getBlock(length); }
    ConstDataBlockRef blockUpTo(unsigned length) throw () { return getBlockUpTo(length); }

protected:
    virtual  uint8_t getU8 () throw (ReadOutside) = 0;
    virtual uint16_t getU16() throw (ReadOutside) = 0;
    virtual uint32_t getU24() throw (ReadOutside) = 0;
    virtual uint32_t getU32() throw (ReadOutside) = 0;
    virtual uint64_t getU64() throw (ReadOutside) = 0;
    virtual ConstDataBlockRef getBlock(unsigned length) throw (ReadOutside) = 0;
    virtual void storeBlock(DataBlockRef buffer) throw (ReadOutside) = 0;
    virtual ConstDataBlockRef getBlockUpTo(unsigned length) throw () = 0;
    virtual ConstDataBlockRef storeBlockUpTo(DataBlockRef buffer) throw () = 0;

private:
    int32_t decodeSignedDynamic(uint32_t value);
};

class SeekableBinaryReader : public BinaryReader {
public:
    virtual void setPosition(unsigned position) throw () = 0;
    virtual unsigned getPosition() const throw () = 0;
};

class BinaryWriter {
protected:
    uint8_t* data;
    unsigned capacity;
    unsigned pos;

    void reserve(unsigned capacityRequired) throw () { if (capacityRequired > capacity) reallocate(capacityRequired); }
    virtual void reallocate(unsigned capacityRequired) throw () { numAssert2(0, capacity, capacityRequired); }

public:
    BinaryWriter(void* buffer, unsigned bufSize) throw () : data(static_cast<uint8_t*>(buffer)), capacity(bufSize), pos(0) { }
    BinaryWriter(DataBlockRef block) throw () : data(static_cast<uint8_t*>(block.data())), capacity(block.size()), pos(0) { }
    virtual ~BinaryWriter() throw () { }

          uint8_t* accessData()       throw () { return data; }
    const uint8_t* accessData() const throw () { return data; }

    void setPosition(unsigned position) throw () { if (position > capacity) reallocate(position + 100); pos = position; }
    unsigned getPosition() const throw () { return pos; }
    unsigned getCapacity() const throw () { return capacity; }

    void clear() throw () { pos = 0; }
    bool empty() const throw () { return pos == 0; }
    unsigned size() const throw () { return pos; }

    operator      DataBlockRef()       throw () { return ref(); }
    operator ConstDataBlockRef() const throw () { return ref(); }

         DataBlockRef ref()       throw () { return      DataBlockRef(data, pos); }
    ConstDataBlockRef ref() const throw () { return ConstDataBlockRef(data, pos); }

    void uncheckedU8 ( uint8_t wData) throw ();
    void uncheckedS8 (  int8_t wData) throw () { uncheckedU8 (static_cast< uint8_t>(wData)); }
    void uncheckedU16(uint16_t wData) throw ();
    void uncheckedS16( int16_t wData) throw () { uncheckedU16(static_cast<uint16_t>(wData)); }
    void uncheckedU24(uint32_t wData) throw (); // high 8 bits of wData are silently ignored
    void uncheckedS24( int32_t wData) throw () { uncheckedU24(static_cast<uint32_t>(wData)); }
    void uncheckedU32(uint32_t wData) throw ();
    void uncheckedS32( int32_t wData) throw () { uncheckedU32(static_cast<uint32_t>(wData)); }

    // the data is verified (asserted) to be within range
    void U8 (unsigned wData) throw ();
    void S8 (  signed wData) throw ();
    void U16(unsigned wData) throw ();
    void S16(  signed wData) throw ();
    void U24(unsigned wData) throw ();
    void S24(  signed wData) throw ();
    void U32(unsigned wData) throw ();
    void S32(  signed wData) throw ();
    void U64(uint64_t wData) throw ();
    void S64( int64_t wData) throw () { U64(static_cast<uint64_t>(wData)); }

    /* Dynamic length datatypes:
     *
     * U32dyn8 is equal in range to U32 (0 .. 2^32-1), but packed with the expectation of approximately the range of U8 (0 .. 255).
     *
     * An U32dyn8 packed value takes the following amount of storage depending on the actual value:
     *   1 byte:     0 ..  239
     *   2 bytes:  240 .. 3071
     *   3 bytes: 3072 .. 128k
     *   4 bytes: 128k ..  16M
     *   5 bytes:  16M ..   4G
     *
     * An U32dyn16 packed value takes the following amount of storage depending on the actual value:
     *   2 bytes:    0 ..  48k
     *   3 bytes:  48k ..   2M
     *   4 bytes:   2M .. 496M
     *   5 bytes: 496M ..   4G
     *
     * Signed types have essentially halved limits on both sides of 0: 240 .. 3071 becomes -121 .. -1536 and 120 .. 1535.
     * Be careful with the signedness: data written signed is corrupted if read unsigned and vice versa,
     * even if the actual value is within the range of both int32_t and uint32_t.
     */
    void U32dyn8 (uint32_t wData) throw ();
    void S32dyn8 ( int32_t wData) throw () { U32dyn8 (encodeSignedDynamic(wData)); }
    void U32dyn16(uint32_t wData) throw ();
    void S32dyn16( int32_t wData) throw () { U32dyn16(encodeSignedDynamic(wData)); }

    // argument choice methods for dynamic or fixed length
    void U32dyn8orU8  (uint32_t wData, bool dyn) throw () { if (dyn) U32dyn8 (wData); else U8 (bound(wData,      0U,   255U)); }
    void S32dyn8orS8  ( int32_t wData, bool dyn) throw () { if (dyn) S32dyn8 (wData); else S8 (bound(wData,   -128 ,   127 )); }
    void U32dyn16orU16(uint32_t wData, bool dyn) throw () { if (dyn) U32dyn16(wData); else U16(bound(wData,      0U, 65535U)); }
    void S32dyn16orS16( int32_t wData, bool dyn) throw () { if (dyn) S32dyn16(wData); else S16(bound(wData, -32768 , 32767 )); }

    void flt(float  wData) throw ();
    void dbl(double wData) throw ();

    // the data is verified (asserted) to be within range
    void U8 (unsigned wData,  uint8_t minBound,  uint8_t maxBound) throw ();
    void S8 (  signed wData,   int8_t minBound,   int8_t maxBound) throw ();
    void U16(unsigned wData, uint16_t minBound, uint16_t maxBound) throw ();
    void S16(  signed wData,  int16_t minBound,  int16_t maxBound) throw ();
    void U24(unsigned wData, uint32_t minBound, uint32_t maxBound) throw (); // minBound and maxBound must be within the 24-bit range
    void S24(  signed wData,  int32_t minBound,  int32_t maxBound) throw (); // minBound and maxBound must be within the 24-bit range
    void U32(unsigned wData, uint32_t minBound, uint32_t maxBound) throw ();
    void S32(  signed wData,  int32_t minBound,  int32_t maxBound) throw ();
    void U64(uint64_t wData, uint64_t minBound, uint64_t maxBound) throw ();
    void S64( int64_t wData,  int64_t minBound,  int64_t maxBound) throw ();

    void flt(float  wData, float  minBound, float  maxBound) throw ();
    void dbl(double wData, double minBound, double maxBound) throw ();

    void constLengthStr(const std::string& wData, unsigned length) throw (); // the data is verified (asserted) to be of the given length
    void str(const std::string& wData) throw ();

    void block(ConstDataBlockRef wData) throw ();

private:
    uint32_t encodeSignedDynamic(int32_t value);
};

class BinaryDataBlockReader : public SeekableBinaryReader {
    const uint8_t* data;
    unsigned dataLength;
    unsigned pos;

    virtual  uint8_t getU8 () throw (ReadOutside);
    virtual uint16_t getU16() throw (ReadOutside);
    virtual uint32_t getU24() throw (ReadOutside);
    virtual uint32_t getU32() throw (ReadOutside);
    virtual uint64_t getU64() throw (ReadOutside);

    virtual ConstDataBlockRef getBlock(unsigned length) throw (ReadOutside);
    virtual void storeBlock(DataBlockRef buffer) throw (ReadOutside);
    virtual ConstDataBlockRef getBlockUpTo(unsigned length) throw ();
    virtual ConstDataBlockRef storeBlockUpTo(DataBlockRef buffer) throw ();

public:
    BinaryDataBlockReader(const void* data_, unsigned size) throw () : data(static_cast<const uint8_t*>(data_)), dataLength(size), pos(0) { }
    BinaryDataBlockReader(ConstDataBlockRef block) throw () : data(static_cast<const uint8_t*>(block.data())), dataLength(block.size()), pos(0) { }

    // a read exception invalidates the position
    virtual void setPosition(unsigned position) throw () { pos = position; }
    virtual unsigned getPosition() const throw () { return pos; }
    virtual bool hasMore() const throw () { return pos < dataLength; }

    ConstDataBlockRef unreadPart() const throw () { return ConstDataBlockRef(data + pos, dataLength - pos); }
};

/** Read binary data off an istream.
 * The stream needs to be seekable in order to use setPosition/getPosition.
 */
class BinaryStreamReader : public SeekableBinaryReader {
    std::istream& stream; // read position always at bufStartOffset + dataLength
    char* temporaryBuffer;

    virtual  uint8_t getU8 () throw (ReadOutside);
    virtual uint16_t getU16() throw (ReadOutside);
    virtual uint32_t getU24() throw (ReadOutside);
    virtual uint32_t getU32() throw (ReadOutside);
    virtual uint64_t getU64() throw (ReadOutside);

    virtual ConstDataBlockRef getBlock(unsigned length) throw (ReadOutside); // this uses temporaryBuffer and isn't efficient
    virtual void storeBlock(DataBlockRef buffer) throw (ReadOutside);
    virtual ConstDataBlockRef getBlockUpTo(unsigned length) throw (); // this uses temporaryBuffer and isn't efficient
    virtual ConstDataBlockRef storeBlockUpTo(DataBlockRef buffer) throw ();

public:
    BinaryStreamReader(std::istream& is) throw () : stream(is), temporaryBuffer(0) { }
    ~BinaryStreamReader() throw () { delete[] temporaryBuffer; }

    // a read exception invalidates the (stream) position
    virtual void setPosition(unsigned position) throw ();
    virtual unsigned getPosition() const throw ();
    virtual bool hasMore() const throw ();

    void releaseTemporaryBuffer() { delete[] temporaryBuffer; temporaryBuffer = 0; }
};

template<unsigned sz> class BinaryBuffer : public BinaryWriter {
    uint8_t buffer[sz];

public:
    BinaryBuffer() throw () : BinaryWriter(buffer, sz) { }
};

class ExpandingBinaryBuffer : public BinaryWriter {
    virtual void reallocate(unsigned capacityRequired) throw ();

public:
    ExpandingBinaryBuffer() throw ();
    ExpandingBinaryBuffer(const ExpandingBinaryBuffer& o) throw ();
    ~ExpandingBinaryBuffer() throw ();

    ExpandingBinaryBuffer& operator=(const ExpandingBinaryBuffer& o) throw ();
};

#endif
