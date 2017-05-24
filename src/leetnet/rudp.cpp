/*
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
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2008 - Niko Ritari
 */

/*

    rudp: UDP with reliable message extensions, for action games

    requires HawkNL from htpp://www.hawksoft.com/hawknl

*/

#include <cstring>
#include <cstdio>
#include "../debugconfig.h" // for LEETNET_SIMULATED_PACKET_LOSS, LEETNET_DATA_LOG
#include "dlog.h"

#include "../binaryaccess.h"
#include "../mutex.h"
#include "../nassert.h"
#include "../network.h"

#include "rudp.h"

// buffer size limitations (stupid hardcoded but works)
//

#define EXTRA_RELIABLE_STORAGE
/* if EXTRA_RELIABLE_STORAGE is defined, reliable messages queue is split to two when more than MAXMSG messages are being sent
 * - the extra ones are separately stored in a queue
 * - only the first MAXMSG messages are transmitted until some of them receive acks
 * it should definitely be used if losing reliable messages can't be afforded
 */

#define MAXMSG                64        // capacity of to-be-acked message buffer
#define BIG_UDPBUF          8192        // a bigger UDP buffer
#define MAX_PACKET_SIZE      256        // this is not a fixed limit; it just limits the sending of >1 reliable message

#define MAX_INCOMING_MESSAGES     64        // size of incoming msg buffer (64 is already overkill)
#define MAX_MESSAGE_SIZE         256        // maximum size of a single reliable message (using more than this trashes the retransmit scheme anyways)


// 256 x 10 = 2560 = 2,5K/s
// obs: n�o tem controle se vai ser enviado 10 pacotes por segundo ou mais. ver caso do envio de
//      teclas pelo client

#ifdef EXTRA_RELIABLE_STORAGE
#include <queue>
#endif


// data class implementation
//

#define DATA_BUF_SIZE 1024

// a message record for the message buffer
//
class msgrec {
    int id_;            // the message id, -1 = unused
    uint32_t sent;       // id of first packet that sent this message
    DataBlock message_;   // the message's contents

public:
    msgrec() throw () { clear(); }

    void clear() throw () { id_ = -1; message_ = DataBlock(); }
    void set(int id, ConstDataBlockRef data) throw () { nAssert(!used()); sent = 0; id_ = id; message_ = data; }
    void send(uint32_t frame) throw () { nAssert(frame != 0); if (sent == 0) sent = frame; else nAssert(sent < frame); }

    bool used() const throw () { return id_ != -1; }
    bool sentBefore(uint32_t id) const throw () { return sent != 0 && sent <= id; } // sent == 0 means not sent at all
    int id() const throw () { return id_; }
    ConstDataBlockRef message() const throw () { return message_; }
    int msgSize() const throw () { return message_.size(); }
};

// station class implementation
//
class station_ci : public station_c {
public:

    char debug[256];

    // send socket -- ALSO the receive socket now.
    Network::UDPSocket sendsock;

    // the address set with set_remote_addr() -- must be set in the socket before all sending
    // cause receiving erases it
    Network::Address netaddr;

    // the packet buffer's unreliable data
    ExpandingBinaryBuffer unreliable;

    // return buffer of read_reliable()
    BinaryBuffer<MAX_MESSAGE_SIZE> reldata;

    // outgoing reliable messages id generator
    uint32_t idgen_reliable_send;

    // outgoing packet id generator
    uint32_t idgen_packet_send;

    // for process_incoming_packet()
    bool is_packet_set;

    // the last packet received, to be acked
    uint32_t ack;

    uint32_t nextPortChange; // at which client frame if there's still zero ack, a port change is attempted; 0 means disabled

    //the UDP packet set
    char    udp_data[BIG_UDPBUF];
    int     udp_size;

    //the UDP send buffer
    ExpandingBinaryBuffer sendbuf;

    // INCOMING messages queue
    // - fixed size & circular (fastest insert & retrieve, waste memory but memory is cheap)
    // - "sliding window": can only always retrieve the next message in the number sequence ("current")
    // - too many messages: messages start to be ignored until app picks them up
    //   OR: receiving message greater than current+bufcap : discard
    // - old messages (< current) discarded
    // - dup messages : will be discovered when trying to put it into the queue (will already inserted)
    char            message[MAX_INCOMING_MESSAGES][MAX_MESSAGE_SIZE];
    int             message_size[MAX_INCOMING_MESSAGES];    //array of sizes. -1 == UNUSED
    uint32_t     msg_current;        // current expected message id

    // the packet buffer's reliable messages (OUTGOING)
    // also works as the "messages yet to be acked" list
    // since all unacked messages are always sent, this includes those that were not sent a single time yet.
    msgrec reliable[MAXMSG];  // FIXME: access to "reliable" must be sinchronized
    uint8_t  reliable_count;     //count of reliable messages in buffer
    Mutex relmsg_mutex;

    #ifdef EXTRA_RELIABLE_STORAGE
    uint32_t reliable_size;  // total size of reliable messages in reliable[], plus 6 bytes extra for each
    std::queue<DataBlock*> extra_reliables;
    void erase_extra_reliables() throw () {
        while (!extra_reliables.empty()) {
            delete extra_reliables.front();
            extra_reliables.pop();
        }
    }
    bool can_add_reliable(uint32_t msgsize) const throw () { return reliable_size==0 || reliable_size + msgsize < MAX_PACKET_SIZE; }
    #endif

    // resets the state of the object. so you don't have to delete and create a new one
    // every time you want to use it for a different client/server.
    virtual void reset_state() throw () {

        //disconnect the socket (if any)
        sendsock.closeIfOpen();

        relmsg_mutex.lock();
        //reset msgrecs!
        for (int m=0;m<MAXMSG;m++)
            reliable[m].clear();
        #ifdef EXTRA_RELIABLE_STORAGE
        reliable_size = 0;
        erase_extra_reliables();
        #endif
        relmsg_mutex.unlock();

        //reset all the internal state for object initialization or reuse
        sendbuf.setPosition(0);
        reldata.setPosition(0);
        unreliable.setPosition(0);
        udp_size = 0;
        is_packet_set = false;
        idgen_reliable_send = 1;
        idgen_packet_send = 1;
        ack = 0;
        reliable_count = 0;
        nextPortChange = 0;

        //clear incoming messages
        msg_current = 1;
        for (int i=0;i<MAX_INCOMING_MESSAGES;i++)
            message_size[i] = -1;   // unused
    }

    //ctor
    station_ci() throw () : relmsg_mutex("station_ci::relmsg_mutex") {
        reset_state();
    }

    //dtor
    virtual ~station_ci() throw () {
        #ifdef EXTRA_RELIABLE_STORAGE
        relmsg_mutex.lock();
        erase_extra_reliables();
        relmsg_mutex.unlock();
        #endif

        //FIXME -- what else?

        //disconnect the socket
        sendsock.closeIfOpen();
    }

    // process incoming reliable message from network
    // - discard duplicates and old messages
    // - implement "sliding window" for reliable message total ordering
    // messages are internally enqueued for later retrieval
    void process_incoming_message(uint32_t msgid, ConstDataBlockRef data) throw () {
DLOG_Scope s("UPIM");
        //printf("process_incoming_message id=%i cur=%i siz=%i\n", msgid, msg_current, msgsize);

        //check message old discard
        if (msgid < msg_current)
            return;

        //printf("not old\n");

        //check buffer(window) overflow
        if (msgid - msg_current >= MAX_INCOMING_MESSAGES)
            return; //just drop it

        //calc index of the message in the array
        // 1st message is "1", arrays start at 0 (so, -1)
        int index = (msgid-1) % MAX_INCOMING_MESSAGES;

        //printf("not overflow index= %i\n", index);

        //check for duplicate discard
        if (message_size[index] != -1)
            return;

        //printf("not duplicate, saved at index.\n");
        //save the message
        message_size[index] = data.size();
        memcpy(&(message[index][0]), data.data(), data.size());
    }

    // set the station's remote address for sending (IP:PORT)
    virtual int set_remote_address(const Network::Address& some_addr, int localPortMin, int localPortMax) throw () {
        netaddr = some_addr;

        static int localPortLastTry = -1;
        if (localPortLastTry < localPortMin || localPortLastTry > localPortMax)
            localPortLastTry = localPortMin;
        const int firstTry = localPortLastTry;
        for (;;) {
            if (sendsock.tryOpen(Network::NonBlocking, localPortLastTry))
                break;
            ++localPortLastTry;
            if (localPortLastTry > localPortMax)
                localPortLastTry = localPortMin;
            if (localPortLastTry == firstTry)
                return 0;   // ERROR
        }
        return 1;   // ok
    }

    virtual int getLocalPort() const throw () {
        try {
            return sendsock.getLocalAddress().getPort();
        } catch (Network::Error&) {
            return 0;
        }
    }

    // read a reliable message from the queue
    virtual ConstDataBlockRef read_reliable() throw () {
DLOG_Scope s("URR");

        //calc index of the message in the array
        // 1st message is "1", arrays start at 0 (so, -1)
        int index = (msg_current - 1) % MAX_INCOMING_MESSAGES;

        //printf("read_reliable index=%i\n", index);

        //check if current is loaded yet
        if (message_size[index] == -1)
            return ConstDataBlockRef(0, 0);       //not yet

        //printf("msg ready!\n");

        //message present - create return data
        reldata.setPosition(0);
        reldata.block(ConstDataBlockRef(&(message[index][0]), message_size[index]));

        //clear msg (slide window)
        relmsg_mutex.lock();
        message_size[index] = -1;
        relmsg_mutex.unlock();

        //update msg_current (slide window)
        msg_current++;

        //return data
        return reldata;
    }

    // sets UDP raw packet that arrived from network
    virtual int set_incoming_packet(ConstDataBlockRef data) throw () {
DLOG_Scope s("USIP");

        // ok!
        is_packet_set = true;

        memcpy(udp_data, data.data(), data.size());
        udp_size = data.size();
        return 1; //ok
    }

    virtual void enablePortSearch() throw () {
        nextPortChange = 30;    // try 3 seconds with the real port
    }

    // process UDP raw packet that was set in set_incoming_packet
    // returns data block pointer and size of block for the unreliable
    // data block of the packet
    // returns special == true if it's a "connection" packet (id == 0)
    // returns 0/length==-1 if called twice before calling set_incoming_packet again
    virtual const char* process_incoming_packet(int *size, bool *special) throw () {
DLOG_Scope s("UPIP");

        int i;

        //error: must set packet first
        //
        if (!is_packet_set) {
            (*size) = -1;
            (*special) = false;
            return 0;
        }

        // must set packet again next time
        //
        is_packet_set = false;


        // (1) parse the packet:
        //
        // uint32_t                          packet_id
        // uint32_t                          acked packet (latest received by remote)
        // uint8_t                          number of reliable messages
        // for each reliable message:
        //      uint32_t                     message id
        //      uint16_t                    message size
        //      int8_t[message size]        the reliable message data
        // int8_t[unreliable data size]     all the unreliable data glued in a big chunk
        //

        BinaryDataBlockReader read(udp_data, udp_size);

        const uint32_t packet_id = read.U32();

        //SPECIAL CASE: packet_id == 0 means a special "connection" packet or something like that
        //do not process it, pass unchanged to caller
        if (packet_id == 0) {
            (*size) = udp_size;
            (*special) = true;
            return udp_data;
        }

        const uint32_t packet_ack = read.U32();
        const uint8_t nreliable = read.U8(); //number of reliable msgs

        if (nextPortChange != 0) {
            if (packet_ack != 0)
                nextPortChange = 0;
            else if (packet_id >= nextPortChange) {
                const int port = netaddr.getPort();
                if (port > 0 && port < 65535)
                    netaddr.setPort(port + 1);
                nextPortChange += 30;
                if (nextPortChange >= 150)   // 15 seconds, equals 5 ports, is max connect sequence length
                    nextPortChange = 0; // stop changing
            }
        }

        for (i=0; i<nreliable; i++) {       // read all reliable msgs
            const uint32_t msgid = read.U32();
            const uint16_t msgsize = read.U16();
            process_incoming_message(msgid, read.block(msgsize));
        }

        // return this
        const uint16_t unreliable_size = udp_size - read.getPosition();
        const char* const unreliable = udp_data + read.getPosition();

        ack = packet_id;

        //(3) for every reliable message in the buffer, check if it was acked by
        //    this incoming data. if yes, delete it from the buffer (id = -1 and clear buffers)
        //

        // for every message in the outgoing buffer...
        //
        relmsg_mutex.lock();
        for (i=0; i<MAXMSG; i++)
            if (reliable[i].used() && reliable[i].sentBefore(packet_ack)) {
                DLOG_Scope s("UPIP_A");
                // acked! remove message from buffer
                #ifdef EXTRA_RELIABLE_STORAGE
                nAssert((int)reliable_size >= reliable[i].msgSize() + 6);
                reliable_size -= reliable[i].msgSize() + 6;
                reliable[i].clear();
                // check if there's a message on the extra queue that can be sent now
                if (!extra_reliables.empty() && can_add_reliable(extra_reliables.front()->size())) {
                    DataBlock* msg = extra_reliables.front();
                    extra_reliables.pop();
                    reliable[i].set(idgen_reliable_send++, *msg);
                    delete msg;
                    reliable_size += reliable[i].msgSize() + 6;
                    if (reliable_count < MAXMSG &&
                            !extra_reliables.empty() &&
                            can_add_reliable(extra_reliables.front()->size()))
                    {
                        for (int rel = 0; rel < MAXMSG; ++rel)  // fill empty spots from queue while possible
                            if (!reliable[rel].used()) {
                                DataBlock* msg = extra_reliables.front();
                                extra_reliables.pop();
                                reliable[rel].set(idgen_reliable_send++, *msg);
                                delete msg;
                                reliable_size += reliable[rel].msgSize() + 6;
                                if (++reliable_count == MAXMSG ||
                                        extra_reliables.empty() ||
                                        !can_add_reliable(extra_reliables.front()->size()))
                                    break;
                            }
                    }
                }
                else
                    reliable_count--;
                #else
                reliable[i].clear();
                reliable_count--;                   // less one
                #endif
            }
        relmsg_mutex.unlock();

        //ok -return stuff
        (*size) = unreliable_size;
        (*special) = false;
        return unreliable;
    }

    // append reliable message to the packet buffer
    virtual int writer(ConstDataBlockRef data) throw () {
DLOG_Scope s("UWR");
        nAssert(data.size() <= MAX_MESSAGE_SIZE);

        relmsg_mutex.lock();
        #ifdef EXTRA_RELIABLE_STORAGE
        if (reliable_count<MAXMSG && can_add_reliable(data.size()) && extra_reliables.empty())
        #endif
        {
            //find slot in reliable
            //
            for (int i=0; i<MAXMSG; i++)
                if (!reliable[i].used()) {
                    reliable[i].set(idgen_reliable_send++, data);
                    reliable_count++;                                               // another one
                    reliable_size += data.size() + 6;
                    relmsg_mutex.unlock();
                    return 1;                       //ok
                }
            #ifdef EXTRA_RELIABLE_STORAGE
            nAssert(0);
            #endif
        }

        // can't add to the standard send buffer
        #ifdef EXTRA_RELIABLE_STORAGE
        extra_reliables.push(new DataBlock(data));
        relmsg_mutex.unlock();
        return 1;
        #else
        relmsg_mutex.unlock();
        return 0;
        #endif
    }

    // append unreliable data to the packet buffer
    //virtual int write(data_c* data) {
    virtual int write(ConstDataBlockRef data) throw () {
DLOG_Scope s("UW");

        //piece o'cake
        //unreliable.add(((data_ci*)data)->buf, ((data_ci*)data)->ulen);

        unreliable.block(data);

        return 1;
    }

    // flush the pending packet buffer as an UDP packet to the remote address, returns "id"
    // for the assigned packet id
    virtual int send_packet(int& id, FILE* datalog) throw () {
DLOG_Scope s("USP");

        int i;

        // assign id to the outgoing packet
        id = idgen_packet_send++;

        // build the packet:
        //
        // uint32_t                          packet_id
        // uint32_t                          acked packet (latest received by remote)
        // uint8_t                          number of reliable messages
        // for each reliable message:
        //      uint32_t                     message id
        //      uint16_t                    message size
        //      int8_t[message size]        the reliable message data
        // int8_t[unreliable data size]     all the unreliable data glued in a big chunk

        sendbuf.setPosition(0);

        sendbuf.U32(id);  //packet id
        sendbuf.U32(ack);

        //if (debug) printf(" rc=%i", reliable_count);

        relmsg_mutex.lock();
        sendbuf.U8(reliable_count);  // number of reliable messages
        for (i=0;i<MAXMSG;i++)  // reliable messages in queue
            if (reliable[i].used()) {
                sendbuf.U32(reliable[i].id());
                sendbuf.U16(reliable[i].message().size());
                sendbuf.block(reliable[i].message());
                //add this send packet id to the message's outgoing sends
                reliable[i].send(id);
            }
        relmsg_mutex.unlock();

        // FIXED: o "unreliable size" eh inferido do tamanho do datagrama UDP
        //              ou seja o "resto" do pacote eh o unreliable data! (nao precisa enviar size)
        //writeShort(sendbuf, count, unreliable.ulen);      // unreliable size
        sendbuf.block(unreliable);        // unreliable data

        //if (debug) printf(" fr=%i TOT=%i\n",unreliable.ulen,count);

        // send the packet
        //
        try {
            #if LEETNET_SIMULATED_PACKET_LOSS != 0
            if (rand() % 100 < LEETNET_SIMULATED_PACKET_LOSS)
                ; // packet simulated as lost; sent ok though
            else
            #endif
{DLOG_Scope s("USPw");
                sendsock.write(netaddr, sendbuf);
}

            #ifdef LEETNET_DATA_LOG
            if (datalog) {
                const int size = sendbuf.size();
                fwrite(&size, sizeof(int), 1, datalog);
                fwrite(sendbuf.accessData(), 1, size, datalog);
            }
            #endif
        } catch (Network::Error&) { } //FIXME: deal with error

        // reset the unreliable buffer (don't delete, just invalidate)
        //
        unreliable.setPosition(0);

        //ok
        return 1;
    }

    // send a raw UDP packet to the destination. returns 1 if ok, 0 if nlWrite failed
    virtual int send_raw_packet(ConstDataBlockRef data) throw () {

        try {
{DLOG_Scope s("SRPw");
            sendsock.write(netaddr, data);
}
            return 1;
        } catch (Network::Error&) {
            return 0;
        }
    }

    virtual int send_raw_packet_to_port(ConstDataBlockRef data, int port) throw () {
        try {
            Network::Address addr = netaddr;
            addr.setPort(port);
            sendsock.write(addr, data);
            return 1;
        } catch (Network::Error&) {
            return 0;
        }
    }

    // non-blocking call: attempt to read data from the socket
    // buffer/bufsize: buffer given to the routine
    // int return: value of nlRead()... :-)
    virtual int receive_packet(DataBlockRef buffer) throw () {
        try {
DLOG_Scope s("URPr");
            return sendsock.read(buffer).length;
        } catch (Network::Error&) {
            return -1;
        }
    }

    // return the socket for get_socket_stat purposes
    virtual const Network::UDPSocket& get_nl_socket() throw () {
        return sendsock;
    }

    // get debug info
    virtual char* debug_info() throw () {
        nAssert(0); return 0;
    }


};


// factory functions
//
station_c       *new_station_c() throw () {
    station_c* x = new station_ci();
    return x;
}
