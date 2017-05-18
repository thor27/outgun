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
 *  Copyright (C) 2003, 2004, 2005 - Niko Ritari
 */

/*

    a network game server engine

*/

#ifndef _server_h_
#define _server_h_

#include <nl.h>

struct ServerHelloResult {
    bool accepted;
    char customData[512];
    int customDataLength;
};

// server class interface
class server_c {
public:
    virtual ~server_c() { }

    typedef void helloCallbackT         (void* customp, int client_id, char* data, int length, ServerHelloResult* res);
    typedef void connectedCallbackT     (void* customp, int client_id);
    typedef void disconnectedCallbackT  (void* customp, int client_id, bool reentrant); // reentrant basically means that the calling thread is an user one
    typedef void dataCallbackT          (void* customp, int client_id, char* data, int length);
    typedef void lagStatusCallbackT     (void* customp, int client_id, int status);
    typedef void pingResultCallbackT    (void* customp, int client_id, int pingtime);

    //set a callback. you must set all the callbacks before calling start()
    virtual void setHelloCallback(helloCallbackT* fn) = 0;
    virtual void setConnectedCallback(connectedCallbackT* fn) = 0;
    virtual void setDisconnectedCallback(disconnectedCallbackT* fn) = 0;
    virtual void setDataCallback(dataCallbackT* fn) = 0;
    virtual void setLagStatusCallback(lagStatusCallbackT* fn) = 0;
    virtual void setPingResultCallback(pingResultCallbackT* fn) = 0;

    virtual void setCallbackCustomPointer(void* ptr) = 0;

    //set the client timeouts in seconds. lagtime = time in secs without receiving packets that generates
    // SFUNC_CLIENT_LAG_STATUS callbacks. droptime = time in secs w/o recv. packets that before kicking the client
    // to disable lag-time notification, use 0. recommended droptime is 5 to 10 seconds
    // OBS: make sure your app has the client sending packets regularly or else he might be dropped without
    //      being really unreachable. 10 times faster than the droptime is a good lower bound. if there is no
    //          frame data, just send some kind of "no-op" packet.
    virtual int set_client_timeout(int lagtime, int droptime) = 0;

    //set serverinfo string
    virtual void set_server_info(const char *info) = 0;

    //start up the server at given port
    virtual int start(int port) = 0;

    //stops the server. parameter is number of seconds to wait for all clients to gently disconnect
    // disconnect_reason is passed to disconnect_client if there are clients to disconnect
    virtual int stop(int disconnect_clients_timeout) = 0;

    enum Disconnect_reason { disconnect_client_initiated, disconnect_server_shutdown, disconnect_timeout, disconnect_first_user_defined };
    //disconnects a specific client, timeout = seconds to wait before loosing patience and just shooting the client
    virtual int disconnect_client(int client_id, int timeout, NLubyte reason) = 0;

    //broadcast the given game frame (along with lotsa other stuff like enqueued reliable messages and acks)
    //to all connected clients. this must be called by a "sender" thread in a fairly regular interval of time,
    //like say 100ms for a 10Hz (update freq.) server. do not load too much shit in the packet, a 300-byte
    //packet is ok I guess, a 500-byte is too much IMHO (remember to give room for the reliable messages/ack
    //protocol that introduces it's own shitload)
    virtual int broadcast_frame(const char* data, int length) = 0;

    //send frame method - when broadcast_frame doesn't quite cut it
    virtual int send_frame(int client_id, const char* data, int length) = 0;

    //sends the given reliable message to the given client. reliable = heavy, do not use for frequent
    //world update data. use for gamestate changes, talk messages and other stuff the client can't miss, or
    //stuff he can even miss but it's better if he doesn't and the message is so infrequent and small that
    //it's worth it.
    virtual int send_message(int client_id, const char* data, int length) = 0;

    //broadcasts the given reliable message to all active clients. for lazy people :-) like me :-))
// disabled in Outgun to prevent problems    virtual int broadcast_message(const char* data, int length) = 0;

    //function to be called by the SFUNC_CLIENT_DATA callback
    //gets the next reliable message avaliable from the given client. null if no message pending
    virtual char* receive_message(int client_id, int *length) = 0;

    //ping a client. results come in the SFUNC_PING_RESULT callback
    virtual int ping_client(int client_id) = 0;

    //get a statistic from sockets. stat = HawkNL socket-stats id
    //this function returns the sum of all sockets active in the server. no per-client
    //results available for now.
    virtual int get_socket_stat(int stat) = 0;

    virtual NLaddress get_client_address(int client_id) const = 0;
};


// server factory
server_c *new_server_c(int thread_priority, int minLocalPort = 0, int maxLocalPort = 0);



#endif // _server_h_

