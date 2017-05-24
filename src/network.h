/*
 *  network.h
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
 *  Copyright (C) 2004 - Jani Rivinoja
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

#ifndef NETWORK_H_INC
#define NETWORK_H_INC

#include <iostream>
#include <map>
#include <string>

#include "utility.h"

class Network {
public:
    // Socket related enums, declared here to avoid having to use Network::Socket::Something
    enum BlockingMode { Blocking, NonBlocking };
    enum SocketType { UDP, TCP, Broadcast };

    class Address;
    class Socket;
    class TCPListenerSocket;
    class TCPSocket;
    class UDPSocket;

    class Error {
    protected:
        Error() throw () { }

    public:
        virtual ~Error() throw () { }

        virtual std::string str() const throw () = 0;
    };

private:
    class NLError : public Error {
        int sysError;

    protected:
        int nlError;

        NLError() throw ();
        friend class Network;
        friend class Network::Socket;
        friend class Network::UDPSocket;

        std::string basicStr() const throw ();

    public:
        virtual ~NLError() throw () { }

        virtual std::string str() const throw ();
    };

public:
    class InitError : public NLError {
        InitError() throw () { }
        friend class Network;

    public:
        virtual std::string str() const throw ();
    };

    class BadIP : public NLError {
        std::string ip;

        BadIP(const std::string& ip_) throw () : ip(ip_) { }

        friend class Address;

    public:
        ~BadIP() throw () { }
        virtual std::string str() const throw ();
    };

    class ResolveError : public NLError {
        std::string name;

        ResolveError(const std::string& hostname) throw () : name(hostname) { }

        friend class Address;

    public:
        ResolveError() throw ();
        ~ResolveError() throw () { }

        virtual std::string str() const throw ();
    };

    class OpenError : public NLError {
        SocketType type;
        uint16_t port;

        OpenError(SocketType t, uint16_t port_) throw () : type(t), port(port_) { }

        friend class Socket;

    public:
        virtual std::string str() const throw ();
    };

    class ConnectError : public NLError {
        std::string addr;

        ConnectError(const std::string& addr_) throw () : addr(addr_) { }

        friend class TCPSocket;

    public:
        ~ConnectError() throw () { }
        bool connectionRefused() const throw ();
        virtual std::string str() const throw ();
    };

    class ListenError : public NLError {
        ListenError() throw () { }
        friend class TCPListenerSocket;

    public:
        virtual std::string str() const throw ();
    };

    class ReadWriteError : public NLError {
        bool inRead;
        ReadWriteError(bool read) throw () : inRead(read) { }

        friend class Socket;
        friend class TCPSocket;

    public:
        bool connectionRefused() const throw ();
        bool disconnected() const throw ();
        virtual std::string str() const throw ();
    };

    class Timeout : public Error {
        bool inRead;
        Timeout(bool read) throw () : inRead(read) { }

        friend class TCPSocket;

    public:
        virtual std::string str() const throw ();
    };

    class ExternalAbort { };

    class Address {
        class HiddenData;
        HiddenData* hidden;

        Address(HiddenData* h) throw ();

        friend class Network;
        friend class Network::Socket;
        friend class Network::TCPSocket;
        friend class Network::UDPSocket;

    public:
        Address() throw ();
        Address(const Address& a) throw ();
        Address(const std::string& ip) throw (BadIP);
        ~Address() throw ();
        Address& operator=(const Address& a) throw ();

        void clear() throw ();

        bool tryResolve(const std::string& hostname, ResolveError* errorStore = 0) throw (); // if errorStore is set, and an error occurs (false returned), it is saved in *errorStore
        bool fromIP(const std::string& ip) throw ();
        void fromValidIP(const std::string& ip) throw ();

        std::string toString() const throw ();

        void setPort(uint16_t port) throw ();
        uint16_t getPort() const throw ();

        bool valid() const throw ();
        bool operator!() const throw () { return !valid(); }

        bool operator==(const Address& a) const throw ();
        bool operator!=(const Address& a) const throw () { return !(*this == a); }
    };

    class Socket : private NoCopying {
        bool autoClose;

    protected:
        class HiddenData;
        HiddenData* hidden;

        Socket(bool autoClose_ = false) throw ();
        Socket(BlockingMode b, SocketType t, uint16_t port, bool autoClose_ = false) throw (OpenError);
        Socket(TrashableRef<Socket> s) throw (); /// "move constructor": assume the identity of s and clear s

        Socket& operator=(TrashableRef<Socket> s) throw (); /// "move assignment": assume the identity of s and clear s

        bool tryOpen(BlockingMode b, SocketType t, uint16_t port) throw ();
        void open(BlockingMode b, SocketType t, uint16_t port) throw (OpenError);

        Address getRemoteAddress() const throw (Error);

        int read(DataBlockRef buffer) throw (ReadWriteError); // returns the number of bytes read
        void write(ConstDataBlockRef data, int* writtenSize = 0) throw (ReadWriteError); //#fix: force using writtenSize, then move it to return value

    public:
        virtual ~Socket() throw ();

        virtual void close() throw ();
        void closeIfOpen() throw ();
        bool isOpen() const throw ();

        enum StatisticType { Stat_PacketsSent    , Stat_BytesSent    , Stat_AvgBytesSent    , Stat_HighBytesSent,
                             Stat_PacketsReceived, Stat_BytesReceived, Stat_AvgBytesReceived, Stat_HighBytesReceived };
        int getStat(StatisticType type) const throw ();

        Address getLocalAddress() const throw (Error);
    };

    class TCPListenerSocket : public Socket {
        void listen() throw (ListenError);
        friend class TCPSocket;

    public:
        TCPListenerSocket(bool autoClose_ = false) throw () : Socket(autoClose_) { }
        TCPListenerSocket(BlockingMode b, uint16_t port, bool autoClose_ = false) throw (OpenError, ListenError) : Socket(b, TCP, port, autoClose_) { listen(); }
        TCPListenerSocket(TrashableRef<TCPListenerSocket> s) throw () : Socket(TrashableRef<Socket>(s)) { } /// "move constructor": assume the identity of s and clear s

        TCPListenerSocket& operator=(TrashableRef<TCPListenerSocket> s) throw (); /// "move assignment": assume the identity of s and clear s

        bool tryOpen(BlockingMode b, uint16_t port) throw (ListenError) { return Socket::tryOpen(b, TCP, port) && (listen(), true); }
        void open(BlockingMode b, uint16_t port) throw (OpenError, ListenError) { Socket::open(b, TCP, port); listen(); }
    };

    class TCPSocket : public Socket {
        bool connected;

    public:
        TCPSocket(bool autoClose_ = false) throw ();
        TCPSocket(BlockingMode b, uint16_t port, bool autoClose_ = false) throw (OpenError);
        TCPSocket(TrashableRef<TCPSocket> s) throw (); /// "move constructor": assume the identity of s and clear s

        TCPSocket& operator=(TrashableRef<TCPSocket> s) throw (); /// "move assignment": assume the identity of s and clear s

        bool tryOpen(BlockingMode b, uint16_t port) throw () { return Socket::tryOpen(b, TCP, port); }
        void open(BlockingMode b, uint16_t port) throw (OpenError) { Socket::open(b, TCP, port); }

        virtual void close() throw ();

        bool acceptConnection(BlockingMode b, TCPListenerSocket& listenerSock) throw ();
        void connect(const Address& a) throw (ConnectError);

        // the rest of the interface is only available when connected

        bool connectPending() throw (ReadWriteError);

        Address getRemoteAddress() const throw (Error);

        int read(DataBlockRef buffer) throw (ReadWriteError); // returns the number of bytes read
        int read(void* buffer, unsigned size) throw (ReadWriteError) { return read(DataBlockRef(buffer, size)); }
        void write(ConstDataBlockRef data, int* writtenSize = 0) throw (ReadWriteError); //#fix: force using writtenSize, then move it to return value

        void persistentWrite(ConstDataBlockRef data, const volatile bool* abortFlag, int timeout, int roundDelay = 500) throw (ReadWriteError, ExternalAbort, Timeout);
        void readAll(std::ostream& out, const volatile bool* abortFlag, int timeout, int roundDelay = 500) throw (ReadWriteError, ExternalAbort, Timeout);

        void persistentWrite(ConstDataBlockRef data, int timeout, int roundDelay = 500) throw (ReadWriteError, Timeout);
        void readAll(std::ostream& out, int timeout, int roundDelay = 500) throw (ReadWriteError, Timeout);
    };

    class UDPSocket : public Socket {
    public:
        UDPSocket(bool autoClose_ = false) throw () : Socket(autoClose_) { }
        UDPSocket(BlockingMode b, uint16_t port, bool autoClose_ = false, bool broadcast = false) throw (OpenError) : Socket(b, broadcast ? Broadcast : UDP, port, autoClose_) { }
        UDPSocket(TrashableRef<UDPSocket> s) throw () : Socket(TrashableRef<Socket>(s)) { } /// "move constructor": assume the identity of s and clear s

        UDPSocket& operator=(TrashableRef<UDPSocket> s) throw (); /// "move assignment": assume the identity of s and clear s

        bool tryOpen(BlockingMode b, uint16_t port, bool broadcast = false) throw () { return Socket::tryOpen(b, broadcast ? Broadcast : UDP, port); }
        void open(BlockingMode b, uint16_t port, bool broadcast = false) throw (OpenError) { Socket::open(b, broadcast ? Broadcast : UDP, port); }

        struct ReadResult {
            int length;
            Address source;

            ReadResult() { }
            ReadResult(int l, const Address& s) : length(l), source(s) { }
        };
        ReadResult read(DataBlockRef buffer) throw (ReadWriteError, Error); // returns the number of bytes read and the source address
        ReadResult read(void* buffer, unsigned size) throw (ReadWriteError, Error) { return read(DataBlockRef(buffer, size)); }
        void write(const Address& addr, ConstDataBlockRef data) throw (ReadWriteError, Error);
    };

    // static members only
    static void init() throw (InitError);
    static void shutdown() throw ();
    static std::vector<Address> getAllLocalAddresses() throw ();
    static Address getDefaultLocalAddress() throw (Error);
};

bool isValidIP(const std::string& address, bool allowPort = false, unsigned int minimumPort = 0, bool requirePort = false) throw ();
bool check_private_IP(const std::string& address, bool allowAnyExternal = false) throw ();   // with allowAnyExternal only (invalid and) loopback addresses are blocked
std::string getPublicIP(LineReceiver& log, bool allowAnyExternal = false) throw ();    // with allowAnyExternal only (invalid and) loopback addresses are blocked
bool isLocalIP(Network::Address address) throw ();  // returns true if address points to this machine (nothing to do with the address being private)

std::string format_http_parameters(const std::map<std::string, std::string>& parameters) throw ();

std::string build_http_request(bool post, const std::string& host, const std::string& script, const std::string& parameters = "", const std::string& auth = "") throw ();

void post_http_data(Network::TCPSocket& socket, const volatile bool* abortFlag, int timeout, const std::string& host,
                    const std::string& script, const std::string& parameters, const std::string& auth = "")
    throw (Network::ReadWriteError, Network::ExternalAbort, Network::Timeout); // timeout in ms

void save_http_response(Network::TCPSocket& socket, std::ostream& out, const volatile bool* abortFlag, int timeout)
    throw (Network::ReadWriteError, Network::ExternalAbort, Network::Timeout);   // timeout in ms

void post_http_data(Network::TCPSocket& socket, int timeout, const std::string& host, const std::string& script, const std::string& parameters, const std::string& auth = "")
    throw (Network::ReadWriteError, Network::Timeout); // timeout in ms

void save_http_response(Network::TCPSocket& socket, std::ostream& out, int timeout)
    throw (Network::ReadWriteError, Network::Timeout);   // timeout in ms

std::string url_encode(const std::string& str) throw ();
void url_encode(char c, std::ostream& out) throw ();
bool is_url_safe(char c) throw ();
std::string base64_encode(const std::string& data) throw ();

#endif
