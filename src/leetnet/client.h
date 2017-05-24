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
 *  Copyright (C) 2002 - Fabio Reis Cecin <fcecin@inf.ufrgs.br>
 *  Modified by Niko Ritari 2003, 2004, 2005, 2008
 *  Copyright (C) 2006 - Jani Rivinoja
 */

/*

    a network game client engine

*/


#ifndef _client_h_
#define _client_h_

#include <string>

#include "../network.h"

//the client class
class client_c {
public:
    virtual ~client_c() throw () { }

    // the callbacks should not throw (but we can't say that in a typedef)
    typedef void connectionCallbackT(void* customp, int connect_result, ConstDataBlockRef data);
    typedef void serverDataCallbackT(void* customp, ConstDataBlockRef data);

    virtual void setConnectionCallback(connectionCallbackT* fn) throw () = 0;
    virtual void setServerDataCallback(serverDataCallbackT* fn) throw () = 0;

    virtual void setCallbackCustomPointer(void* ptr) throw () = 0;

    //set the server's address. call before connect()
    virtual void set_server_address(const char *address) throw () = 0;

    //set the custom data sent with every connection packet
    //gameserver will interpret it by server_c's SFUNC_CLIENT_HELLO callback
    virtual void set_connect_data(ConstDataBlockRef data) throw () = 0;

    //set connection status. if set to TRUE, engine will try to estabilish connection
    //with the server. if set to FALSE, will stop trying to connect or will disconnect
    //results are returned in the CFUNC_CONNENCTION_UPDATE callback
    virtual void connect(bool yes, int minLocalPort = 0, int maxLocalPort = 0) throw () = 0;

    //send reliable message
    virtual void send_message(ConstDataBlockRef data) throw () = 0;

    //dispatches the packet with the given frame (unreliable data) and all the
    //protocol overload (reliable messages, acks...)
    virtual void send_frame(ConstDataBlockRef data) throw () = 0;

    //function to be called by the CFUNC_SERVER_DATA callback
    //gets the next reliable message avaliable from the server. null if no message pending
    virtual ConstDataBlockRef receive_message() throw () = 0;

    //get a statistic from the socket. stat = HawkNL socket-stats id
    virtual int get_socket_stat(Network::Socket::StatisticType stat) throw () = 0;

    virtual double increasePacketDelay(double amount = 0.01) throw () = 0;
    virtual double decreasePacketDelay(double amount = 0.01) throw () = 0;
};


//factory
client_c* new_client_c(int thread_priority, const std::string& logPostfix) throw ();


#endif // _client_h_
