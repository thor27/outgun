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
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
 */

/*

    A Game Server

    uses RUDP (rudp.cpp , rudp.h)
    uses HawkNL (http://www.hawksoft.com/hawknl)

    must be used to implement an actual game's server

    don't subclass it, it's an abstract interface. make your own class that has an
    internal instance of server_c

*/

#include <memory>
#include <sched.h>
#include <stdio.h>
#include "../binaryaccess.h"
#include "../commont.h" // for wheregamedir
#include "../debug.h"
#include "../debugconfig.h" // LEETNET_LOG, LEETNET_DATA_LOG
#include "../log.h"
#include "../mutex.h"
#include "../network.h"
#include "../thread.h"
#include "../utility.h" // get_time
#include "leetnet.h"
#include "server.h"
#include "rudp.h"
#include "Timer.h"
#include "../timer.h" // for platSleep
using namespace GNE;

// max (absolute) clients that can connect to a server
// change this to meet your needs
#define  MAX_CLIENTS 32

class server_ci;

// client record struct for server
struct client_t {
    volatile bool       used;               // "true" if there is a client connected in this slot

    int                         id;                 // the client's id (index on the array)

    // the 4 variables below encode the client's connection state
    //
    volatile bool       connected;  // if the client has already said "hello!" and been accepted by gameclient
                                                            // this DOES NOT mean that the client is still connected! (talk about misleading...)
                                                            // it means that the CONNECTION PROTOCOL has been completed on the server side
    volatile bool       connected_knows;    // the client knows he is connected. discard all "want connect" packets etc.
    volatile bool       told_disconnect;        // client already told that he wants to disconnect or, a "disconnect"
                                                                            // packet was already received from the client
    volatile bool       server_disconnected; // set to true when the server kicks the client. told_disconnect may
                                                                             // be false at that point.
    uint8_t disconnect_reason;  // values are user defined, except 0 is used internally for drop at timeout

    Time                    ping_start_time;        //time of last ping request from gameserver

    server_ci               *server;        // the server instance (for the thread)

    Network::Address               addr;                   //client's address, to resolve incoming packets

    Thread              thread;         // the slave thread

    Thread              discthread;     // the disconnector thread

    volatile int        discleft;           // disconnection packets left to send

    station_c               *station;       // rudp station for communicating with the client

    double                  droptime;       // time to drop / valid if told_disconnect == true OR server_disconnected == true

    double                  lastpackettime; // time of last packet, for droptimeout/lagtimeout

    bool                        in_lag;         // if client is lagged

    int customStoredData; // freely set by the server in helloCallback, to be returned to connectedCallback

    //mutex for the station object and condition variable
    Mutex   station_mutex;

    //condition variable
    ConditionVariable       station_cond_hasdata;

    //thread must quit flag
    volatile bool       quitflag;

    client_t() throw () : station_mutex("client_t::station_mutex"), station_cond_hasdata("client_t::station_cond_hasdata") { }
};


// server thread (master)
void thread_master_f(server_ci* server) throw ();

//client message processor (slaves)
void thread_slave_f(client_t* mydata) throw ();

//client disconnectors
void thread_disconnector_f(client_t* mydata) throw ();

//server_c implementation
class server_ci : public server_c {
public:
    #ifdef LEETNET_LOG
    std::auto_ptr<Log> logp;    // initialized with a FileLog or NoLog depending on g_leetnetLog
    Log& log;
    #else
    NoLog log;
    #endif

    #ifdef LEETNET_DATA_LOG
    FILE* datalog;
    Mutex datalogMutex;
    #endif

    // number of clients allocated
    int     num_clients;

    // the server UDP socket
    Network::UDPSocket            servsock;

    int minLocalPort, maxLocalPort;

    //serverinfo buffer
    char                    serverinfo[2048];

    // UDP reader thread
    Thread                  reader_thread;

    // client structures - one for each client
    client_t                    client[MAX_CLIENTS];

    //server is stopping
    volatile bool           server_stopped;

    //HACK (not too lousy one)
    double          last_hack_think;

    //timeout configs
    int lagtimeout;
    int droptimeout;

    helloCallbackT* helloCallback;
    connectedCallbackT* connectedCallback;
    disconnectedCallbackT* disconnectedCallback;
    dataCallbackT* dataCallback;
    lagStatusCallbackT* lagStatusCallback;
    pingResultCallbackT* pingResultCallback;

    void* customp;  // custom pointer passed back to callback functions

    int threadPriority;

    //------------------------
    // GAME SERVER API
    //------------------------

    //set a callback. you must set all the callbacks before calling start()
    virtual void setHelloCallback(helloCallbackT* fn) throw () { helloCallback = fn; }
    virtual void setConnectedCallback(connectedCallbackT* fn) throw () { connectedCallback = fn; }
    virtual void setDisconnectedCallback(disconnectedCallbackT* fn) throw () { disconnectedCallback = fn; }
    virtual void setDataCallback(dataCallbackT* fn) throw () { dataCallback = fn; }
    virtual void setLagStatusCallback(lagStatusCallbackT* fn) throw () { lagStatusCallback = fn; }
    virtual void setPingResultCallback(pingResultCallbackT* fn) throw () { pingResultCallback = fn; }
    virtual void setCallbackCustomPointer(void* ptr) throw () { customp = ptr; }

    //set the client timeouts in seconds. lagtime = time in secs without receiving packets that generates
    // SFUNC_CLIENT_LAG_STATUS callbacks. droptime = time in secs w/o recv. packets that before kicking the client
    // to disable lag-time notification, use 0. recommended droptime is 5 to 10 seconds
    // OBS: make sure your app has the client sending packets regularly or else he might be dropped without
    //      being really unreachable. 10 times faster than the droptime is a good lower bound. if there is no
    //          frame data, just send some kind of "no-op" packet.
    virtual int set_client_timeout(int lagtime, int droptime) throw () {
        lagtimeout = lagtime;
        droptimeout = droptime;
        return 0;
    }

    //set serverinfo string
    virtual void set_server_info(const char *info) throw () {
        strcpy(serverinfo, info);
    }

    //start up the server at given port
    virtual int start(int port) throw () {
        //if not stopped, quit
        if (!server_stopped) {
            log("SERVER NOT STOPPED: CANT START SERVER_CI");
            return 0;
        }

        //free
        num_clients = 0;

        //timeout defaults
        set_client_timeout(5, 10);

        if (!servsock.tryOpen(Network::NonBlocking, port)) {
            log("server_ci::start(): cannot nlOpen server socket!");
            return 0;  // error
        }

        //not stopped now
        server_stopped = false;

        //create and start all client threads
        for (int i=0;i<MAX_CLIENTS;i++) {

            client[i].used = false;             // free player slot
            client[i].id = i;                           // id (for thread)
            client[i].server = this;            // server (for thread)

            //FIXME: do anything to client[i].station_cond_hasdata ??
            //pthread_cond_init(&client[i].station_cond_hasdata, 0);


            //client[i].station = new_station_c();      // create station -- NAO, isso cria qdo cara conecta!
            //client[i].station = 0;
            client[i].quitflag = false;         // don't quit yet
            client[i].in_lag    = false;            // not in lag

            // create the slave thread
            client[i].thread.start_assert("leetnet/server.cpp:thread_slave_f",
                                          thread_slave_f, &client[i],
                                          threadPriority);
        }

        //create and start the master thread
        reader_thread.start_assert("leetnet/server.cpp:thread_master_f",
                                   thread_master_f, this,
                                   threadPriority);

        //ok
        return 1;
    }

    //stops the server. parameter is number of seconds to wait for all clients to gently disconnect
    virtual int stop(int disconnect_clients_timeout) throw () {
        int i;

        log("server_ci::stop()");

        //if stopped, quit
        if (server_stopped)
            return 0;

        log("server_ci::stop() mesmo");

        //disconnect all clients (network "disconnect msg" / wait)
        for (i=0;i<MAX_CLIENTS;i++)
        if (client[i].used) //valid
        if ((client[i].connected) && (!client[i].told_disconnect) && (!client[i].server_disconnected)) //still connected
            disconnect_client(i, disconnect_clients_timeout, disconnect_server_shutdown, true);

        // signal threads to stop now
        server_stopped = true;
        for (i=0;i<MAX_CLIENTS;i++) {

            client[i].quitflag = true; //set flag
            log("server_ci::stop() -- signal %i", i);

            //pthread_cond_signal( &client[i].station_cond_hasdata ); //slap the thread
            client[i].station_cond_hasdata.signal(client[i].station_mutex);
        }

        log("server_ci::stop() -- joining master thread");

        // join with master thread
        reader_thread.join();

        log("server_ci::stop() - joined with master thread");

        // join with slave/disconnector threads and delete the client stuff
        for (i=0;i<MAX_CLIENTS;i++) {

            // join
            client[i].thread.join();

            log("server_ci::stop() - joined with slave %i thread", i);

            // wait all client disconnect packets to be sent
            if (client[i].discthread.isRunning()) {
                client[i].discthread.join();
                log("server_ci::stop() - joined with disconnector %i thread", i);
            }

            // cleanup
            // - aqui pode deletar, nao existe mais nenhuma thread (master,slave,disconnector...)
            //

            //MUDANDO: apenas reseta station
            client[i].station->reset_state();

            //FIXME: do something to station_cond_hasdata ?
            //pthread_cond_destroy(&client[i].station_cond_hasdata);
        }

        log("server_ci::stop() -- closing server socket");

        servsock.close();

        log("server_ci::stop() -- server socket closed");

        //ok
        return 1;
    }

    //disconnects a specific client, timeout = seconds to wait before loosing patience and just shooting the client
    virtual int disconnect_client(int client_id, int timeout, uint8_t reason, bool fromUserThread) throw () { // reason is user defined; reserved: 0 = client initiated, 1 = timeout
        log("disconnect_client(%d, %d, %d, %d)", client_id, timeout, reason, fromUserThread);

        //call the "client disconnected" callback (2 of 2 : server-initiated disconnection)
        // DO NOT CALL if client not connected
        if (client[client_id].connected_knows)
            disconnectedCallback(customp, client_id, fromUserThread);

        //disconnect the client - this flags that the client is disconnected by the server but the
        // client at first doesn't know this. will keep sending disconnect packets to client
        // until a timeout occurs or when the first disconnect packet is received from the client
        // in wich moment client[id].told_disconnect is set to TRUE (was false).
        //
        // this flag also makes any regular game-connection packets from the client to be discarded.
        client[client_id].server_disconnected = true;
        client[client_id].disconnect_reason = reason;

        //countdown for client dropping - this var is checked by the reader thread
        //
        client[client_id].droptime = get_time() + ((double)timeout);

        // send any disconnection packets only if the client ever sent a data packet (connected_knows = true)
        // because if the client never sent data, probably it must think it's not even connected
        // so we just sit idle for a couple of seconds before nuking the connection, because the client
        // may still contact us. if he does, we send a disconnect packet
        if (!client[client_id].connected_knows)
            return 1;

        // disconnection packets to send
        //
        if (timeout > 1)
            client[client_id].discleft = 10;
        else if (timeout < 1)
            client[client_id].discleft = 1;
        else
            client[client_id].discleft = 5;

        //spawn disconnector thread
        client[client_id].discthread.start_assert("leetnet/server.cpp:thread_disconnector_f",
                                                  thread_disconnector_f, &client[client_id],
                                                  threadPriority);

        log("disconnect_client %i droptime = %.2f", client_id, client[client_id].droptime);

        //ok
        return 1;
    }

    //broadcast the given game frame (along with lotsa other stuff like enqueued reliable messages and acks)
    //to all connected clients. this must be called by a "sender" thread in a fairly regular interval of time,
    //like say 100ms for a 10Hz (update freq.) server. do not load too much shit in the packet, a 300-byte
    //packet is ok I guess, a 500-byte is too much IMHO (remember to give room for the reliable messages/ack
    //protocol that introduces it's own shitload). optimize your foken data, every byte saved counts!
    virtual int broadcast_frame(ConstDataBlockRef data) throw () {
        #ifdef LEETNET_DATA_LOG
        if (datalog)
            Lock ml(datalogMutex);
        #endif

        for (int i=0;i<MAX_CLIENTS;i++)
        if (client[i].used) {
            client[i].station->write(data); // set frame data
            int packet_id;

            #ifdef LEETNET_DATA_LOG
            if (datalog) {
                static const char writeModeMarker = 'W';
                fwrite(&writeModeMarker, sizeof(char), 1, datalog);
                double currTime = get_time();
                fwrite(&currTime, sizeof(double), 1, datalog);
                fwrite(&i, sizeof(int), 1, datalog);    // which client
                client[i].station->send_packet(packet_id, datalog); // flush the packet
            }
            else
                client[i].station->send_packet(packet_id, 0);   // flush the packet
            #else
            client[i].station->send_packet(packet_id, 0);   // flush the packet
            #endif
        }

        //ok
        return 1;
    }

        //send frame method - when broadcast_frame doesn't quite cut it
    virtual int send_frame(int client_id, ConstDataBlockRef data) throw () {
        if (!client[client_id].used)
            return 0;   // client not used (?)

        client[client_id].station->write(data); //set frame data
        int packet_id;

        #ifdef LEETNET_DATA_LOG
        if (datalog) {
            Lock ml(datalogMutex);
            static const char writeModeMarker = 'W';
            fwrite(&writeModeMarker, sizeof(char), 1, datalog);
            double currTime = get_time();
            fwrite(&currTime, sizeof(double), 1, datalog);
            fwrite(&client_id, sizeof(int), 1, datalog);    // which client
            client[client_id].station->send_packet(packet_id, datalog); // flush the packet
        }
        else
            client[client_id].station->send_packet(packet_id, 0);   // flush the packet
        #else
        client[client_id].station->send_packet(packet_id, 0);   // flush the packet
        #endif

        //ok
        return 1;
    }

    //sends the given reliable message to the given client. reliable = heavy, do not use for frequent
    //world update data. use for gamestate changes, talk messages and other stuff the client can't miss, or
    //stuff he can even miss but it's better if he doesn't and the message is so infrequent and small that
    //it's worth it.
    virtual int send_message(int client_id, ConstDataBlockRef data) throw () {
        //FIXME 1. assert here: client[client_id].used == true
        //          2. use station mutex ?

        //and that's it!
        client[client_id].station->writer(data);

        //ok
        return 1;
    }


    //broadcasts the given reliable message to all active clients. for lazy people :-) like me :-))
    /* disabled in Outgun to prevent problems
    virtual int broadcast_message(const char* data, int length) throw () {

        for (int i=0;i<MAX_CLIENTS;i++)
        if (client[i].used)
            send_message(i, data, length);

        //ok
        return 1;
    }
    */

    //function to be called by the SFUNC_CLIENT_DATA callback
    //gets the next reliable message avaliable from the given client. null if no message pending
    virtual ConstDataBlockRef receive_message(int client_id) throw () {
        ConstDataBlockRef data = client[client_id].station->read_reliable();

        if (data.size() == 0)  // no messages
            return ConstDataBlockRef(0, 0);

        //debug
        log("server->receive_message(clid=%i) length = %i", client_id, data.size());

        return data;
    }

    //ping a client. results come in the SFUNC_PING_RESULT callback
    virtual int ping_client(int client_id) throw () {
        BinaryBuffer<32> msg;

        msg.U32(0);            //special packet
        msg.U32(666);      // ping request

        client[client_id].station->send_raw_packet(msg);

        client[client_id].ping_start_time = Timer::getCurrentTime();

        //ok
        return 1;
    }

    //get a statistic from sockets. stat = HawkNL socket-stats id
    //this function returns the sum of all sockets active in the server. no per-client
    //results available for now.
    virtual int get_socket_stat(Network::Socket::StatisticType stat) throw () {

        int thestat = 0;

        if (!servsock.isOpen()) return 0;
        if (server_stopped) return 0;

        //add serversocket
        thestat = servsock.getStat(stat);

        //add all active client sockets
        for (int i=0;i<MAX_CLIENTS;i++)
        if (client[i].used)
        if (client[i].station)
        {
            const Network::UDPSocket& clsock = client[i].station->get_nl_socket();
            if (clsock.isOpen())
                thestat += clsock.getStat(stat);
        }

        return thestat;
    }

    //------------------------
    // server slave-disconnector thread API (temp thread that sends disconnection packets to the client
    //------------------------
    bool try_send_disconnect(int id) throw () {

        //check client not used for whatever reason -- this is PARANOIA
        if (!client[id].used)
            return true;

        //check client acked already
        if (client[id].told_disconnect)
            return true;

        //check stop now
        if (client[id].discleft <= 0)
            return true;

        //send the disconnection packet
        log("sent in try_send_disconnect(%i)...", id);
        BinaryBuffer<32> msg;
        msg.U32(0);      //"special packet"
        msg.U32(2);      //"you are now disconnected"
        msg.U32(client[id].disconnect_reason);
        client[id].station->send_raw_packet(msg);

        //keep running if packets left to send, else stop
        //
        return ((--client[id].discleft) <= 0);
    }

    //------------------------
    // server master thread API (thread that read all incoming stuff from network and hands it to a slave)
    //------------------------

    //incoming datagram from UDP socket
    virtual int process_incoming_datagram(const Network::Address& remoteaddr, ConstDataBlockRef data) throw () {
        //MAKEIT
        //
        //o que pode acontecer
        // - forward de mensagem para existing client -- verifica remoteaddress da serversocket
        //    do client, foi updateada agora. -- e signal client
        // - se client unknown e SERVER FULL, dah reply aqui mesmo
        // - se client unknown, "aloca" novo client para uma thread agora mesmo e j� passa a mensagem
        //   pra ela. a thread lida com conexao tambem.
        // mensagem 0 666 = ping request
        // mensagem 0 200 = serverinfo request

        BinaryDataBlockReader read(data);
        const uint32_t packid = read.U32(); //packet id
        const uint32_t smsgid = read.U32(); // special message id (if packet id == 0)

        // verifica se a mensagem eh de algum client conhecido
      int i;
        for (i=0;i<MAX_CLIENTS;i++)
        if (client[i].used)
        if (remoteaddr == client[i].addr) {
            log("DO CLIENT %i",i);

            #ifdef LEETNET_DATA_LOG
            if (datalog) {
                Lock ml(datalogMutex);
                static const char readModeMarker = 'R';
                fwrite(&readModeMarker, sizeof(char), 1, datalog);
                double currTime = get_time();
                fwrite(&currTime, sizeof(double), 1, datalog);
                fwrite(&i, sizeof(int), 1, datalog);    // which client
                const int size = data.size();
                fwrite(&size, sizeof(int), 1, datalog);
                fwrite(data.data(), 1, size, datalog);
            }
            #endif

            //achou: pertence a um client conectado, copia para a station, que a thread
            // ira' process�-lo. obs: "station" precisa ser locket

            //set packet, slap slave
            client[i].station_mutex.lock();

            //pode ser null aqui  (free slave  lock/delete/unlock)
            if (client[i].station)
                client[i].station->set_incoming_packet(data);

            //pthread_cond_signal ( &client[i].station_cond_hasdata );  //slap the slave
            client[i].station_cond_hasdata.signal();

            client[i].station_mutex.unlock();

            // ok
            return 1;
        }
        // ==== nao eh de client conhecido: aceita soh alguns special packets ====

        #ifdef LEETNET_DATA_LOG
        if (datalog) {
            Lock ml(datalogMutex);
            static const char readModeMarker = 'R';
            fwrite(&readModeMarker, sizeof(char), 1, datalog);
            double currTime = get_time();
            fwrite(&currTime, sizeof(double), 1, datalog);
            const int clid = -1;
            fwrite(&clid, sizeof(int), 1, datalog);    // which client
            const int size = data.size();
            fwrite(&size, sizeof(int), 1, datalog);
            fwrite(data.data(), 1, size, datalog);
        }
        #endif

        //se nao for special packet, nao aceita
        if (packid != 0) {  //special packet
            log(" NOT SPECIAL PACKET");
            return 1;
        }

        //serverinfo request : answer
        if (smsgid == 200) {
            const uint8_t a = read.U8(); //clientside gamespy entry (lazyness)
            const uint8_t b = read.U8(); //packet try #

            ExpandingBinaryBuffer msg;
            msg.U32(0);
            msg.U32(200);
            msg.U8(a);
            msg.U8(b);
            msg.str(serverinfo);
            //send
            try {
                log("SENDING REPLY TO CLIENT AT %s", remoteaddr.toString().c_str());
                servsock.write(remoteaddr, msg);
            } catch (Network::Error&) {
                return 0;
            }
            return 1;
        }

        // broadcasted server request
        if (smsgid == 0x4F757467) { // "Outg"
            if (read.str() == "un") {
                BinaryBuffer<512> msg;
                msg.str("Outgun");
                try {
                    log("SENDING REPLY TO CLIENT AT %s", remoteaddr.toString().c_str());
                    servsock.write(remoteaddr, msg);
                } catch (Network::Error&) {
                    return 0;
                }
            }
            return 1;
        }

        // se aqui nao for pedido de conexao, nao aceita
        //
        if (smsgid != 1) {  //"hello! I want to connect!"
            log(" NOT HELLO PACKET");
            return 1;
        }

        //nao eh de client conhecido - verifica server full
        //"server full" reply message
        if (num_clients >= MAX_CLIENTS) {

            //send ENGINE SERVER FULL to client
            BinaryBuffer<64> msg;
            msg.U32(0);             //"special packet"
            msg.U32(201);           //"connection rejected - engine server FULL"

            //send
            try {
                servsock.write(remoteaddr, msg);
                log("*** SENT SERVER-FULL (%i clients) REPLY TO CLIENT AT %s ***", num_clients, remoteaddr.toString().c_str());
                return 1;
            } catch (Network::Error&) {
                return 0;
            }
        }

        //verifica se LEETNET_VERSION match
        const uint32_t leetversion = read.U32(); // leetnet version
        if (leetversion != LEETNET_VERSION) {
            log("Client connection ignored: LEETNET_VERSION mismatch. c=%u s=%u", (unsigned)leetversion, LEETNET_VERSION);
            return 1;
        }

        //server com espaco, aloca um cara pra ele
        for (i=0;i<MAX_CLIENTS;i++)
        {
            //lock client
            client[i].station_mutex.lock();

            if (!client[i].used)
            {
                //zero'ing state
                client[i].id = i;                   // the client's id (index on the array)
                client[i].ping_start_time = Time(0, 0);     //time of last ping request from gameserver
                client[i].server = this;        // the server instance (for the thread)
                client[i].discleft = 0;         // disconnection packets left to send
                client[i].droptime = 0;     // time to drop / valid if told_disconnect == true OR server_disconnected == true
                client[i].quitflag = false; //thread must quit flag

                // aloca jogador para thread
                client[i].addr = remoteaddr;       //set address
                client[i].connected = false;                // must negotiate connection (client must first say "hello" :-)
                client[i].connected_knows = false;      // did not receive game data packet yet when TRUE the
                                                                                            // server knows that the client knows that he was accepted
                client[i].told_disconnect = false;      // set to true when a packet "I want to disconnect" is first received
                                                                                            // from the client
                client[i].server_disconnected = false;  //set to true when the server takes the initiative and kicks the
                                                                                                // client. now must wait the ack (disconnect packet) from the client also (a.k.a. told_disconnect == true condition)

                client[i].in_lag = false;           // not in lag
                client[i].lastpackettime = get_time();     // last packet time is this one plus a bonus...

                // new station - create & init
                // MUDANDO: apenas reseta client station
                client[i].station->reset_state();

                //set station remote address
                //char  adrstr[NL_MAX_STRING_LENGTH];
                //nlAddrToString(&client[i].addr, adrstr);
                //client[i].station->set_remote_address(adrstr);
                if (client[i].station->set_remote_address(client[i].addr, minLocalPort, maxLocalPort) == 0) {
                    log("process_incoming_datagram() ERROR: SET_REMOTE_ADDRESS RETURNED == 0!!!");
                    client[i].station_mutex.unlock();
                    return 1;       //abort connection
                }

                // mais um jogador
                num_clients++;
                log("NEW CLIENT %i  (total=%i)", i, num_clients);

                //set packet & slap slave
                //pthread_mutex_lock( &client[i].station_mutex );
                client[i].station->set_incoming_packet(data); //set packet

                //pthread_cond_signal ( &client[i].station_cond_hasdata ); //slap the slave
                client[i].station_cond_hasdata.signal();

                //pthread_mutex_unlock( &client[i].station_mutex );

                // agora ta valido p/ outras threads
                client[i].used = true;

                //ok - unlock client
                client[i].station_mutex.unlock();
                return 1;
            }

            //unlock client
            client[i].station_mutex.unlock();
        }

        //WEIRD WEIRD fail: num_clients esta mentindo para baixo
        return 0;
    }

    //HACK (a better one): called by reader thread to do some thinking for the server
    void server_think() throw () {
        //FIXME: THIS (was) JUST PLAIN WASTE OF CPU!
        //          but we can do better....
        double curr_time = get_time();
        if (curr_time - last_hack_think < 0.5)      //500ms resolution is enough
            return;
        last_hack_think = curr_time;

        for (int i=0;i<MAX_CLIENTS;i++)
            if (client[i].used) {
                //HACK: check when it's droptime for a client
                if ((client[i].told_disconnect) || (client[i].server_disconnected)) // disconnection started by either side
                if (client[i].droptime < curr_time) {
                    //bye
                    log("droptime: client %i's slave freed.", i);
                    client[i].station_mutex.lock();
                    free_slave(i);
                    client[i].station_mutex.unlock();
                }

                //HACK: check for lagged call
                if (lagtimeout > 0)
                if (!client[i].in_lag)
                if ( ((int)(curr_time - client[i].lastpackettime)) > lagtimeout ) {
                    //lag flag
                    client[i].in_lag = true;

                    //lag on callback - only if CONNECTED! (called SFUNC_CLIENT_CONNECTED callback...)
                    if (client[i].connected_knows)
                        lagStatusCallback(customp, i, 1);
                }

                //HACK: check for client dropped due to timeout
                if (droptimeout > 0)
                if (! ((client[i].told_disconnect) || (client[i].server_disconnected)) )    // disconnection not started by either side
                if ( ((int)(curr_time - client[i].lastpackettime)) > droptimeout ) // drop timeout
                {
                    //dropped callback - only if CONNECTED! (called SFUNC_CLIENT_CONNECTED callback...)
                    if (client[i].connected_knows)
                        lagStatusCallback(customp, i, 2);

                    //disconnect the client - 3 sec timeout
                    disconnect_client(i, 3, disconnect_timeout, false);
                }
            }
    }

    //returns the serversocket
    Network::UDPSocket& get_server_socket() throw () {
        return servsock;
    }

    //------------------------
    // server slave thread API (threads that read from one client)
    //------------------------

    //process data from a client (on the client's station)
    virtual int process_client_data(int cid) throw () {
        //FIXME: no futuro: READ, UNLOCK, PROCESS e nao READ, PROCESS, UNLOCK

        //FIXME: read and process all the stuff from the station
        //           - process_incoming_packet() -- unreliable client's frame
        //           - read_reliable() -- process all decoded client messages (new messages
        //                  are created on process...() so call this later


        //get the client's unreliable data frame from the station
        //
        bool is_special; //check if packet is a special packet (connection packet)
        int len;
        const char *data = client[cid].station->process_incoming_packet(&len, &is_special);

        // HACK: no new packet in the station
        //
        if (len == -1)
            return 1;

        //check lag off, if client connected_knows and not disconnected
        //
        if (client[cid].connected)  // this if is just paranoia
        if (client[cid].connected_knows)
        if (!client[cid].server_disconnected)
        if (!client[cid].told_disconnect)
        {
            client[cid].lastpackettime = get_time();   //update last packet receive time
            if (client[cid].in_lag) {
                //"your lag's off!"
                client[cid].in_lag = false;

                //callback lag status update
                lagStatusCallback(customp, cid, 0);
            }
        }

        //special packet?
        //

        if (is_special) {
            BinaryDataBlockReader read(data, len);
            read.U32(); //skip "0"
            const uint32_t code = read.U32();

            //switch
            switch (code) {
            case 666:   // "pong"  (from previous ping request)
                {
                    Time pt=Timer::getCurrentTime()-client[cid].ping_start_time;
                    pingResultCallback(customp, cid, pt.getSec()*1000+pt.getuSec()/1000);
                }
                break;
            case 1:

                //connection request discard if:
                //      client knows he is connected
                //      client disconnected somehow
                log("client %i CONNECTION (I)", cid);
                if (!client[cid].connected_knows)
                if (!client[cid].server_disconnected)
                if (!client[cid].told_disconnect)
                {

                    //log("CALLING SFUNC CALLBACK LEN = %i", len);
                    ServerHelloResult res;
                    res.accepted = false;
                    res.customDataLength = 0;
                    helloCallback(customp, cid, ConstDataBlockRef(&data[16], len-16), &res);
                    log("client %i CONNECTION (II)", cid);
                    if (res.accepted) {
                        //connected!
                        client[cid].connected = true;

                        client[cid].customStoredData = res.customStoredData;

                        //send hello packet back to the client
                        log("SENT CONNECTION ACCEPTED 0/3 to client_ci");
                        ExpandingBinaryBuffer msg;
                        msg.U32(0);  //"special packet"
                        msg.U32(3);  //"connection accepted"
                        msg.U32(client[cid].station->getLocalPort());
                        msg.block(ConstDataBlockRef(res.customData, res.customDataLength));   // custom game data

//                      log("station debuginfo = %s", client[cid].station->debug_info());

                        // send using the server socket from where the originating message was received: to make sure the reply gets through any firewalls/NATs
                        try {
                            servsock.write(client[cid].addr, msg);
                        } catch (Network::Error&) { }

                        client[cid].station->enablePortSearch();
                    }
                    else {

                        //send CONNECTION_REJECTED to client
                        BinaryBuffer<32> msg;
                        msg.U32(0);      //"special packet"
                        msg.U32(4);      //"connection rejected"
                        msg.block(ConstDataBlockRef(res.customData, res.customDataLength));   // custom "connection denied" information
                        try {
                            servsock.write(client[cid].addr, msg);
                        } catch (Network::Error&) { }

                        //return this thread/client slot to the free pool
                        if (client[cid].used == true) {
                            log("client %i's slave freed : receive CONNECT but REJECT!", cid);
                            free_slave(cid);
                        }
                        else
                            log("free_slave(%i) with used == false!", cid);
                    }
                }
                break;

            case 2:
                //if disconnection initiated by server
                if (client[cid].server_disconnected) {

                    //disconnection request
                    log("(server_disconnected) client 0-2 received");

                    //client now knows
                    client[cid].told_disconnect = true;

                    // callback already called
                    // no reply needed
                    // client droptime : now/soon (paranoia)
                    client[cid].droptime = get_time() + 0.5;
                }
                //client-initiated disconnection
                else {

                    //disconnection request
                    log("(client disconnection) client 0-2 received, replying.");

                    //set "client is disconnected"
                    if (!client[cid].told_disconnect) {

                        //disconnection request
                        log("(client disconnection) told_disconnect == true");

                        client[cid].told_disconnect = true;

                        //call the "client disconnected" callback (1 of 2 : client-initiated disconnection)
                        // DO NOT CALL if client not connected
                        if (client[cid].connected_knows)
                            disconnectedCallback(customp, cid, false);

                        //mark 3 second countdown for client dropping
                        //this var is checked by the reader thread
                        // this is already set if server_disconnected == true
                        client[cid].droptime = get_time() + 3.0;
                    }

                    //reply: ok, you are disconnected
                    log("sent disconnect that client %i initiated..", cid);
                    client[cid].disconnect_reason = disconnect_client_initiated;    // client initiated disconnection
                    BinaryBuffer<32> msg;
                    msg.U32(0);      //"special packet"
                    msg.U32(2);      //"you are now disconnected"
                    msg.U32(client[cid].disconnect_reason);
                    client[cid].station->send_raw_packet(msg);

                }
                break;

            default:
                //FIXME: unknown code!
                log("WTF!?!? %u", (unsigned)code);
                break;
            }
        }
        //regular packet - must be already connected and not disconnected
        //AND the slot must be used, OF COURSE!
        else if (client[cid].used) {
            if (client[cid].connected == false) {
            }
            else if (client[cid].server_disconnected == true) {

                // do nothing, a thread should be already spitting "disconnect" packets to the client
            }
            else if (client[cid].told_disconnect == true) {

                // this could occur if 1) the client is nuts 2) a data packet sent before the
                // disconnection request arrived first. in any case just ignore it since the
                // client connection is already doomed.
            }
            else {

                //-- it's a regular data packet --

                //if disconnected state, reply the disconnect packet
                if ((client[cid].server_disconnected) || (client[cid].told_disconnect)) {

                    // make a favour for the client
                    log("client %i datapacket - replying 'disconnected already'", cid);
                    BinaryBuffer<32> msg;
                    msg.U32(0);      //"special packet"
                    msg.U32(2);      //"you are now disconnected"
                    msg.U32(client[cid].disconnect_reason);
                    client[cid].station->send_raw_packet(msg);
                }
                else {

                    //it's a data packet, so the client knows he is connected already
                    //- discard "want connect" incoming packets (don't reply)
                    //- send the "client connected" event to the gameserver
                    if (client[cid].connected_knows == false) {

                        //we know that the client knows that his connection was accepted
                        client[cid].connected_knows = true;

                        //call gameserver "client connected" callback
                        connectedCallback(customp, cid, client[cid].customStoredData);
                    }

                    // send the data to the gameserver
                    // call SFUNC_CLIENT_DATA callback
                    dataCallback(customp, cid, ConstDataBlockRef(data, len));
                }
            }
        }

        //ok
        return 1;
    }

    Network::Address get_client_address(int client_id) const throw () {
        return client[client_id].addr;
    }

    //-------- internal functions --------

    //free slave thread
    void free_slave(int id) throw () {
        //if disconnector alive, join with it
        if (client[id].discthread.isRunning()) {
            client[id].discleft = 0;    //paranoia
            client[id].discthread.join();
        }

        //free slave
        client[id].used = false;

        //delete the station. a new one will be created when other client connects
        //MUDANDO: apenas reset
#ifndef STATION_PANIC
            client[id].station->reset_state();
#else
            if (client[id].station)
                delete client[id].station;
            client[id].station = new_station_c();
#endif

        //if (client[id].station) {
//          delete client[id].station;
//          client[id].station = 0;
//      }
//      else {
//          log("WARNING: free_slave %i STATION ALREADY ZERO!!", id);
//      }

        num_clients--;
        log("slave %i freed, clients now = %i", id, num_clients);
    }


    //------------------------
    // etc.
    //------------------------

    //ctor
    server_ci(int thread_priority, int minLocalPort_, int maxLocalPort_) throw () :
        #ifdef LEETNET_LOG
        logp(g_leetnetLog ?
             static_cast<Log*>(new FileLog((wheregamedir + "log" + directory_separator + "leetserverlog.txt").c_str(), true)) :
             static_cast<Log*>(new NoLog())),
        log(*logp),
        #else
        log(),
        #endif
        #ifdef LEETNET_DATA_LOG
        datalogMutex("server_ci::datalogMutex"),
        #endif
        minLocalPort(minLocalPort_),
        maxLocalPort(maxLocalPort_)
    {
        #ifdef LEETNET_DATA_LOG
        if (g_leetnetDataLog)
            datalog = fopen((wheregamedir + "log" + directory_separator + "leetserverdata.bin").c_str(), "wb");
        else
            datalog = 0;
        #endif

        strcpy(serverinfo, "default serverinfo");

        //it's true...
        server_stopped = true;

        //init'ing var
        last_hack_think = 0.0;

        //create all station objects
        for (int i=0;i<MAX_CLIENTS;i++)
            client[i].station = new_station_c();

        threadPriority = thread_priority;
    }

    //dtor
    virtual ~server_ci() throw () {
        //stop if was running - isso deve garantir que nao tem mais nenhuma
        //thread maluca mexendo com os objetos tipo client[i].station
        stop(3);

        //delete all station objects
        for (int i=0;i<MAX_CLIENTS;i++) {
            delete client[i].station;
            client[i].station = 0;
        }

        #ifdef LEETNET_DATA_LOG
        if (datalog)
            fclose(datalog);
        #endif
    }
};


//reader (master) thread - one per server
#define THREAD_READER_BUFSIZE 1024 // to protect bad code in later stages from too long packets, packets this long won't be sent anyway
void thread_master_f(server_ci* server) throw ()
{
    //get socket to read from
    Network::UDPSocket& servsock = server->get_server_socket();

    //read buffer
    char    buffer[THREAD_READER_BUFSIZE];

    //loop
    while (1) {
        //read from socket
        Network::UDPSocket::ReadResult result;
        try {
            result = servsock.read(buffer, THREAD_READER_BUFSIZE);
        } catch (const Network::Error& e) {
            result.length = -1;
            server->log("Master thread: trouble reading socket: %s", e.str().c_str());
        }

        //HACK (a better one): think for the server
        server->server_think();

        // test quit
        if (server->server_stopped)
            break;

        // if no data, keep reading
        if (result.length == 0) {
            platSleep(2);
            continue;
        }

        // check for error
        if (result.length < 0)
            platSleep(100);
        else
            server->process_incoming_datagram(result.source, ConstDataBlockRef(buffer, result.length));
    }
}

//client message processor (slave) thread - one for each client
//arg: pointer to thread_client_arg_t
void thread_slave_f(client_t* mydata) throw ()
{
    //server
    server_ci *server = mydata->server;

    //my id
    int myid = mydata->id;

    mydata->station_mutex.lock();    // timedWait releases it for the waiting period

    //loop
    while (mydata->quitflag == false) {
        // LOOP 0: stop server
        // LOOP 1: work (with the station etc.) only if client structure is set to "used"
        //                  if not used, just jump to the sleep part again

        //lock (to condvar)
        //pthread_mutex_lock ( &mydata->station_mutex );

        //wait for "work now!" signal from master
        //pthread_cond_wait ( &mydata->station_cond_hasdata, &mydata->station_mutex );

        mydata->station_cond_hasdata.wait(mydata->station_mutex);

        //SLEEP(5);  // IMPROVED WITH CONDITION VARIABLE! THANKS TO GNE!

        //check quit
        //if (mydata->quitflag)
        //  break;

        //log("SLAVE %i slapped...", myid);

        //check if the thread/client slot is still being used
        if (mydata->used) {
            //log("SLAVE %i working...", myid);

            //process the data -- it's on the station
            server->process_client_data(myid);

            //unlock
            //pthread_mutex_unlock( &mydata->station_mutex );
        }
    }
    mydata->station_mutex.unlock();
}

//client disconnector auxiliary thread. bombards
// client with "disconnect now!" packets
void thread_disconnector_f(client_t* mydata) throw () {
    //server
    server_ci *server = mydata->server;

    //loop
    while (1) {

        //try / should quit?
        if (server->try_send_disconnect(mydata->id))
            break;

        //sleep a bit
        platSleep(100);    //*** NO CPU PROBLEM HERE ***
    }
}


// server factory
server_c *new_server_c(int thread_priority, int minLocalPort, int maxLocalPort) throw () {
    return new server_ci(thread_priority, minLocalPort, maxLocalPort);
}
