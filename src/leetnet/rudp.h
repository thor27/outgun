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
 *  Copyright (C) 2003, 2005 - Niko Ritari
 */

/*

    rudp: UDP with reliable message extensions, for action games

    requires HawkNL from htpp://www.hawksoft.com/hawknl

*/

#ifndef _rudp_h_
#define _rudp_h_

//hawknl
#include <nl.h>

// gets the IP of this machine
void get_self_IP(NLaddress *addr);

/*

    data_c

    this encapsulates a char* buffer with a int length parameter. it grows and
    shrinks. not much to do with it really, I'm thinking about just nuking it.

    call new_data_c() to get a new object

*/
class data_c {
public:
    virtual ~data_c() { }

    //add data
    virtual void add(const void* data, int len) = 0;

    //add long: watch endianess
    virtual void addlong(unsigned long data) = 0;

    //set data
    virtual void set(const data_c *data) = 0;

    //set data
    virtual void set(const char *data, short len) = 0;

    //clear
    virtual void clear() = 0;

    //get data: use buf/alen/ulen with HawkNL packet reading functions
    virtual char* getbuf() = 0;
    virtual const char* getbuf() const = 0;

    //get length (used)
    virtual int getlen() const = 0;
};

/*

    station_c

    call new_station_c() to get a new station object

    call set_remote_address() before using the other methods

    this is like a "socket" but I didn't wanted to call it a socket because it has a lot of
    buffers and ack/reliable message stuff inside. it looked like a socket with brains so it
    looked like a "processing facility" or something. it's like a socket in the sense that
    there is one in each side of a connection. the station can be on server-side or client-side,
    it doesn't matter (much like an UDP socket)

    the reliable messages arrive reliably, in order, and duplicates should never occur.

    the send_packet() function basically dispatches three kinds of information:
    1 - unreliable data : data specific to this packet. ideal for "realtime" data wich, if
                lost, doesn't make sense to retransmit as a new version of the data wich replaces
                the old one is available
    2 - outgoing reliable messages : reliable messages that have not yet been acknowledged
    3 - incoming reliable message acks : IDs of the incoming reliable messages, so the other
                side may stop transmitting and retransmitting them.

*/
class station_c {
public:
    virtual ~station_c() { }

    // resets the state of the object. so you don't have to delete and create a new one
    // every time you want to use it for a different client/server.
    virtual void reset_state() = 0;

    // set the station's remote address for sending.
    // returns 0 if (nlOpen(0, NL_UNRELIABLE) == NL_INVALID), returns 1 otherwise
    // if any local port is OK, set localPortMin = localPortMax = 0
    virtual int set_remote_address(char* address, int localPortMin = 0, int localPortMax = 0) = 0; //notation is "bla.bla.bla.bla:portnum"
    virtual int set_remote_address(NLaddress *some_addr, int localPortMin = 0, int localPortMax = 0) = 0; //NL address struct

    virtual int getLocalPort() const = 0;

    // non-blocking call: attempt to read data from the socket
    // buffer/bufsize: buffer given to the routine
    // int return: value of nlRead()... :-)
    virtual int receive_packet(char *buffer, int bufsize) = 0;

    // sets UDP raw packet that arrived from network (received_packet call). this is used
    // because a thread may set the packet so that other thread processes it (see below)
    virtual int set_incoming_packet(char *udp_data, int udp_size) = 0;

    virtual void enablePortSearch() = 0;

    // process UDP raw packet that was set in set_incoming_packet. returns data block pointer
    // and size of block for the unreliable data block of the packet
    // returns special == true if it's a "connection" packet (id (first long) == 0)
    // returns 0 and length==-1 if called twice before calling set_incoming_packet again (this
    // means that there is no new data to be processed)
    virtual char* process_incoming_packet(int *size, bool *special) = 0;

    // returns a message if there is a reliable message to be read, or null. messages may
    // be placed in the queue for read after a packet is processed (process_incoming_packet()),
    // but not necessarily. call this in a loop until it returns 0 (no more messages)
    virtual data_c* read_reliable() = 0;

    // append reliable message to the queue. this will be sent as many times as needed until
    // it's acknowledged by the other side.
    virtual int writer(const char *data, int length) = 0;

    // appends unreliable data to the packet buffer. all these calls are collapsed and the
    // unreliable data is sent as a big chunk when send_packet() is called (see below).
    virtual int write(const char *data, int length) = 0;

    // flush the packet buffers as an UDP packet to the remote address, returns "id"
    // for the assigned packet id. this call resets the unreliable data buffer (see
    // write() above).
    // datalog is an added debugging help: pass it a valid file pointer and send_packet will
    // write the packet length followed by data to the file
    virtual int send_packet(int& id, FILE* datalog) = 0;

    // send a raw UDP packet to the destination. use this for implementing connection,
    // disconnection, or some authentication etc. schemes. raw packets with the first
    // long == 0 are "special" and not treated as rudp packets (see process_incoming_packet())
    // - returns 1 if ok, 0 if nlWrite failed
    virtual int send_raw_packet(const data_c* data) = 0;
    virtual int send_raw_packet_to_port(const data_c* data, int port) = 0;

    // return the socket for get_socket_stat purposes
    virtual NLsocket get_nl_socket() = 0;

    // get debug info
    virtual char* debug_info() = 0;
};


// factory functions
//
data_c          *new_data_c();
station_c       *new_station_c();



#endif // _rudp_h_

