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
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2006 - Jani Rivinoja
 */

/*

    a network game client engine

*/

#include "leetnet.h"

#include "client.h"

#include <memory>   // auto_ptr
#include <queue>

#include "../network.h"

#include <sched.h>
#include <stdio.h>

#include "rudp.h"

#include "server.h"

#include "../thread.h"

#include "../log.h"

#include "../debugconfig.h" // for LEETNET_LOG, LEETNET_DATA_LOG
#include "../debug.h"
#include "dlog.h"

#include "../commont.h" // for wheregamedir

#include "../timer.h" // for platSleep

#include "../binaryaccess.h"

class client_ci;

//connector thread function
void thread_connect_f(client_ci* client) throw ();
void thread_disconnect_f(client_ci* client) throw ();

//reader thread function
void thread_reader_f(client_ci* client) throw ();

class QueueSendCommand {    // base class
protected:
    DataBlock data;

public:
    QueueSendCommand(ConstDataBlockRef data_) throw () : data(data_) { }
    virtual ~QueueSendCommand() throw () { }
    virtual void execute(client_ci* client, station_c* station) throw () = 0;
};

class QWriter : public QueueSendCommand {
public:
    QWriter(ConstDataBlockRef data_) throw () : QueueSendCommand(data_) { }
    void execute(client_ci*, station_c* station) throw () { station->writer(data); }
};

class QSendRawPacket : public QueueSendCommand {
public:
    QSendRawPacket(ConstDataBlockRef data_) throw () : QueueSendCommand(data_) { }
    void execute(client_ci*, station_c* station) throw () { station->send_raw_packet(data); }
};

class QSendRawPacketToPort : public QueueSendCommand {
    int port;

public:
    QSendRawPacketToPort(ConstDataBlockRef data_, int port_) throw () : QueueSendCommand(data_), port(port_) { }
    void execute(client_ci*, station_c* station) throw () { station->send_raw_packet_to_port(data, port); }
};

class QSendFrame : public QueueSendCommand {
public:
    QSendFrame(ConstDataBlockRef data_) throw () : QueueSendCommand(data_) { }
    void execute(client_ci* client, station_c*) throw ();
};

//the client class
class client_ci : public client_c {
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

    double packetDelay; // how many seconds every sent frame is delayed to increase lag
    Mutex sendQueueMutex; // lock for operating on sendQueue
    std::queue< std::pair<double, QueueSendCommand*> > sendQueue;  // pair of sendtime, command for delayed sends (used if packetDelay != 0)

    //the server address
    Network::Address       serveraddr;

    //client station
    station_c       *station;

    //connection state
    volatile bool   want_connect;           //yes/no
    volatile int    connect_status;     //0=not connected 1=trying disconnection 2=trying connection 2=connected
    int tries_left;             //tries left
    int connect_threads_running;    // half-witted thread-safety protection; flawed but better than nothing - must recode a lot

    bool            started_disconnection;      //if disconnection was started by the client

    //the connecter/disconnecter threads
    Thread      thread_disconnect;

    //the reader thread
    Thread      thread_read;

    Mutex readerThreadManipulationMutex;

    //quit reader thread?
    bool                quit_reader_thread;

    //connect data
    DataBlock connect_data;

    connectionCallbackT* connectionCallback;
    serverDataCallbackT* serverDataCallback;

    void* customp;  // custom pointer passed back to callback functions

    int threadPriority;

    //------------------------------
    //  GAME CLIENT API
    //------------------------------

    //set a callback function.
    virtual void setConnectionCallback(connectionCallbackT* fn) throw () { connectionCallback = fn; }
    virtual void setServerDataCallback(serverDataCallbackT* fn) throw () { serverDataCallback = fn; }

    virtual void setCallbackCustomPointer(void* ptr) throw () { customp = ptr; }

    //set the server's address. call before connect()
    virtual void set_server_address(const char *address) throw () {
        serveraddr.fromIP(address);
    }

    //set the custom data sent with every connection packet
    //gameserver will interpret it by server_c's SFUNC_CLIENT_HELLO callback
    virtual void set_connect_data(ConstDataBlockRef data) throw () {
        connect_data = data;
    }

    //set connection status. if set to TRUE, engine will try to estabilish connection
    //with the server. if set to FALSE, will stop trying to connect or will disconnect
    //results are returned in the CFUNC_CONNENCTION_UPDATE callback
    virtual void connect(bool yes, int minLocalPort = 0, int maxLocalPort = 0) throw () {
        log("connect now=%i  set to=%i   constatus=%i", want_connect, yes, connect_status);

        //noop
        if (want_connect == yes) return;

        //changed
        want_connect = yes;

        //want to connect
        if (want_connect) {
            if (connect_status == 0) {
                log("starting connect sequence.");

                //start connection sequence
                start_connect(minLocalPort, maxLocalPort);
            }
            else if (connect_status == 1) {
                log("wil star connect sequence.");

                //trying disconnection -- wait until after it's done
                //this is just a hack
                while (connect_status == 1)
                    platSleep(500);  // *** NO CPU PROBLEM HERE ***

                log("starting connect sequence.");

                //now connect normally
                start_connect(minLocalPort, maxLocalPort);
            }
        }
        //want to disconnect
        else {
            if (connect_status == 3) {
                log("starting disconnect seq...");

                //start disconnecting
                start_disconnect();

                //join with disconnector thread
                log("joining disconnect thread...");
                thread_disconnect.join();
                log("disconnect thread joined.");

                //REVIEW: additional cleanup, must enable
                //        new connections later ? FIXME
            }
            else if (connect_status == 2) {
                log("stop_connect() - gave up connecting..");

                //trying connection - just stop trying. if it gets accepted, we will reply again telling to disconnect
                stop_connect();
            }
        }
    }

    void queueSend(QueueSendCommand* qsc) throw () {
        Lock ml(sendQueueMutex);
        sendQueue.push(std::pair<double, QueueSendCommand*>(get_time() + packetDelay, qsc));
    }

    void probeSendQueue() throw () {
        if (packetDelay == 0.)
            return;
        sendQueueMutex.lock();
        if (!sendQueue.empty()) {
            if (get_time() >= sendQueue.front().first) {
                QueueSendCommand* qsc = sendQueue.front().second;
                sendQueue.pop();
                sendQueueMutex.unlock();
                qsc->execute(this, station);
                delete qsc;
                return;
            }
        }
        else if (packetDelay < .001)    // set to signal not used but non-empty queue
            packetDelay = 0.;    // now queue is empty, no need for the signal
        sendQueueMutex.unlock();
    }

    void clearSendQueue() throw () {
        Lock ml(sendQueueMutex);
        while (!sendQueue.empty()) {
            delete sendQueue.front().second;
            sendQueue.pop();
        }
    }

    double increasePacketDelay(double amount = 0.01) throw () {
        packetDelay += amount;
        return packetDelay;
    }

    double decreasePacketDelay(double amount = 0.01) throw () {
        packetDelay -= amount;
        if (packetDelay <= 0.) {
            packetDelay = 1e-5; // flag for probeSendQueue that the queue is still operational, but when it's empty, packetDelay is zeroed
            return 0;
        }
        return packetDelay;
    }

    //send reliable message
    virtual void send_message(ConstDataBlockRef data) throw () {
        if (packetDelay < .001)
            station->writer(data);
        else
            queueSend(new QWriter(data));
    }

    void sendRawPacket(ConstDataBlockRef data) throw () {
        if (packetDelay < .001)
            station->send_raw_packet(data);
        else
            queueSend(new QSendRawPacket(data));
    }

    void sendRawPacketToPort(ConstDataBlockRef data, int port) throw () {
        if (packetDelay < .001)
            station->send_raw_packet_to_port(data, port);
        else
            queueSend(new QSendRawPacketToPort(data, port));
    }

    //dispatches the packet with the given frame (unreliable data) and all the
    //protocol overload (reliable messages, acks...)
    virtual void send_frame(ConstDataBlockRef data) throw () {
        if (packetDelay < .001)
            doSendFrame(data);
        else
            queueSend(new QSendFrame(data));
    }

    void doSendFrame(ConstDataBlockRef data) throw () {
        //do not send if not connected
        if (connect_status != 3)
            return;

        if (data.size() > 0)
            station->write(data);
        int packet_id;  // unused

        #ifdef LEETNET_DATA_LOG
        if (datalog) {
            Lock ml(datalogMutex);
            static const char writeModeMarker = 'W';
            fwrite(&writeModeMarker, sizeof(char), 1, datalog);
            double currTime = get_time();
            fwrite(&currTime, sizeof(double), 1, datalog);
            station->send_packet(packet_id, datalog);
        }
        else
            station->send_packet(packet_id, 0);
        #else
        station->send_packet(packet_id, 0);
        #endif
    }

    //function to be called by the CFUNC_SERVER_DATA callback
    //gets the next reliable message avaliable from the server. null if no message pending
    virtual ConstDataBlockRef receive_message() throw () {
        return station->read_reliable();
    }

    //get a statistic from the socket. stat = HawkNL socket-stats id
    virtual int get_socket_stat(Network::Socket::StatisticType stat) throw () {
        return station->get_nl_socket().getStat(stat);
    }

    //------------------------------
    //  internal stuff
    //------------------------------

    //READER thread wants to read from the station
    int read_station(DataBlockRef buf) throw () {
        if (!station) {
            log("WOW REALLY FUCKED UP!!");
            return -666;
        }
        else {

            //log("ST=%s", station->debug_info());

            return station->receive_packet(buf);
        }
    }

    //start disconnection sequence
    void start_disconnect() throw () {
        log("start_disconnect()");

        //start thread
        started_disconnection = true;       // client started it
        connect_status = 1; //trying nice disconnection
        tries_left = 10;        //try 10 times
        thread_disconnect.start_assert("leetnet/client.cpp:thread_disconnect_f",
                                       thread_disconnect_f, this,
                                       threadPriority);
    }

    //disconnect try - return TRUE to stop
    bool disconnect_try() throw () {
        log("client disconnect_try()");

        // check stopped trying to disconnect
        if (connect_status != 1)
            return true;

        // check tries
        if (tries_left-- <= 0)
            return true;

        log("client trying (disconnect)...");

        // send disconnect packet via station!
        //
        BinaryBuffer<32> msg;
        msg.U32(0); //special packet
        msg.U32(2); //disconnect ACK
        sendRawPacket(msg);  // FIXME: deal with send erros?

        //stop now if done
        return (tries_left <= 0);
    }

    //disconnection done (timeout or reply received)
    void nice_disconnect_done(char reason) throw () {
        log("nice_disconnect_done() - delete station - connectstatus = 0");

        //connection callback w/ status = 1 (disconnected)
        //FIXME: DISCARDING EXTRA DATA ON THE INCOMING DISCONNECT PACKET (nao tem nada mesmo...)
        //
        const int connect_result = 1;
        connectionCallback(customp, connect_result, ConstDataBlockRef(&reason, 1));

        Lock ml(readerThreadManipulationMutex);

        if (thread_read.isRunning()) {
            //quit reader thread
            quit_reader_thread = true;
            thread_read.join(true); // the executing thread might be thread_read; "recursive" join works in this case

            //close the socket/station
            station->reset_state();
            clearSendQueue();
        }

        connect_status = 0;
        want_connect = false;       //this var sucks
    }

    //start connection sequence
    void start_connect(int minLocalPort, int maxLocalPort) throw () {
        //trying. this should be the FIRST THING!
        int old_status = connect_status;
        connect_status = 2;

        log("start_connect()");

        //CLEAR station
        //station = new_station_c();
        station->reset_state();
        clearSendQueue();

        //set station remote address (opens the client's ONLY socket)
        //char adrstr[NL_MAX_STRING_LENGTH];
        //nlAddrToString(&serveraddr, adrstr);
        //station->set_remote_address(adrstr);
        if (station->set_remote_address(serveraddr, minLocalPort, maxLocalPort) == 0) {
            log("start_connect() ERROR: SET_REMOTE_ADDRESS RETURNED == 0!!!");
            connect_status = old_status;    //no idea if this is needed...
            // "socket problem"
            connectionCallback(customp, 5, ConstDataBlockRef(0, 0));
            return;
        }

        readerThreadManipulationMutex.lock();

        for (;;) {
            if (thread_read.isRunning()) {
                readerThreadManipulationMutex.unlock();
                platSleep(100);
                readerThreadManipulationMutex.lock();
            }
            else
                break;
        }
        //create reader thread
        quit_reader_thread = false;
        thread_read.start_assert("leetnet/client.cpp:thread_reader_f",
                                 thread_reader_f, this,
                                 threadPriority);

        readerThreadManipulationMutex.unlock();

        //start connection tries
        started_disconnection   = false;        //init "started_disconnection" flag for this connection session
        tries_left = 4;                 //number of tries
        ++connect_threads_running;
        Thread::startDetachedThread_assert("leetnet/client.cpp:thread_connect_f",
                                           thread_connect_f, this,
                                           threadPriority);
    }

    //cleanup connect sequence
    void stop_connect() throw () {
        //disconnected state -- THIS MUST BE THE FIRST THING
        connect_status = 0;

        log("stop_connect() - deletes station - connect status = 0");

        Lock ml(readerThreadManipulationMutex);

        if (thread_read.isRunning()) {
            //quit reader thread
            quit_reader_thread = true;
            thread_read.join(true); // the executing thread might be thread_read; "recursive" join works in this case

            //delete station -- CLOSES the socket
            station->reset_state();
            clearSendQueue();
        }

        //cancel
        want_connect = false;   //you don't want to connect anymore
    }

    //process datagram read by reader thread
    void process_incoming_datagram(ConstDataBlockRef fullData) throw () {
DLOG_Scope s("CPIDg");
        #ifdef LEETNET_DATA_LOG
        if (datalog) {
            Lock ml(datalogMutex);
            static const char readModeMarker = 'R';
            fwrite(&readModeMarker, sizeof(char), 1, datalog);
            double currTime = get_time();
            fwrite(&currTime, sizeof(double), 1, datalog);
            const int size = fullData.size();
            fwrite(&size, sizeof(int), 1, datalog);
            fwrite(fullData.data(), 1, size, datalog);
        }
        #endif
        //set datagram
        station->set_incoming_packet(fullData);

        //process data
        int length;
        bool special;
        const char *data = station->process_incoming_packet(&length, &special);

        //special packet? (connection accepted/rejected , disconnected ...)
        if (special) {
            BinaryDataBlockReader read(data, length);
            read.U32(); //discard the "0"
            const uint32_t code = read.U32();

            // ping request - send a reply immediately
            if (code == 666) {
                BinaryBuffer<32> msg;
                msg.U32(0);
                msg.U32(666);      //pong!
                sendRawPacket(msg);
            }
            // connection refused by special motive: engine server doesn't support
            // additional clients. this is similar to a "server full" custom
            // connection denied message from the gameserver ( 0/4/custom ), but in this case the
            // connection fails before the gameserver has right to any opinion on the matter.
            // it's a net-engserver limitation.
            else if (code == 201) {

                //this is similar to  (CODE == 4)  case below
                //------------------------------------------------

                //check if still trying
                if (connect_status == 2) {

                    //connection callback w/ status = 2 (failed)
                    //also handle the rest of the packet to the gameclient
                    connectionCallback(customp, 4, ConstDataBlockRef(0, 0));   // denied-by-engserver-full
                }

                //stop connect - also quits reader thread
                stop_connect();

                //not trying anymore -- done by call above
                //connect_status = 0;   //dead
            }
            // disconnected
            else if (code == 2) {

                log("special packet 0,2 arrived my connect_status == %i started_disc = %i", connect_status, started_disconnection);

                const uint32_t reason = read.U32();

                // if was not already disconnected
                //if (connect_status != 0) {

                    //connection callback w/ status = 1 (disconnected)
                    //also handle the rest of the packet to the gameclient
                    //
                    // MOVIDO PARA NICE_DISCONNECT_DONE()

                    //disconnected state
                    connect_status = 0;
                //}

                //if I didn't start it, send reply
                //send raw by station!!!
                if (!started_disconnection) {

                    BinaryBuffer<32> msg;
                    msg.U32(0); //special packet
                    msg.U32(2); //disconnect ACK
                    sendRawPacket(msg);  // FIXME: deal with send erros?

                    //REMENDÃO: o cliente se suicida assim que recebe noticia que o server
                    //                quer detonar ele
                    // TEM QUE CHAMAR NICE_DISCONNECT_DONE porque é esse cara que chama
                    //   o callback "client disconnected!"
                    nice_disconnect_done(static_cast<char>(reason));
                }
            }
            // connection accepted
            else if (code == 3) {

                // check if callback called already
                if (connect_status != 3) {
                    const uint32_t port = read.U32();
                    if (port > 0 && port < 65536) {
                        // send a dummy packet to the server port in order to get the local firewall open and/or NAT tunnel active (may not work if the server is also behind a NAT)
                        BinaryBuffer<32> msg;
                        sendRawPacketToPort(msg, port);
                    }

                    //connection callback w/ status = 0  (connected)
                    //also handle the rest of the packet to the gameclient
                    const int connect_result = 0;
                    connectionCallback(customp, connect_result, ConstDataBlockRef(data + 12, length - 12)); //skip 0,3,port

                    //connected!
                    connect_status = 3;
                }
            }
            // connection rejected
            else if (code == 4) {

                //check if still trying
                if (connect_status == 2) {

                    //connection callback w/ status = 2 (failed)
                    //also handle the rest of the packet to the gameclient
                    const int connect_result = 2;

                    log("INCOMING 0,4 REJECTION length = %i", length);

                    connectionCallback(customp, connect_result, ConstDataBlockRef(data + 8, length - 8)); //skip 0,4
                }

                //stop connect - also quits reader thread
                stop_connect();

                //not trying anymore -- done by call above
                //connect_status = 0;   //dead
            }
            //wtf?
            else {
                    //FIXME: error
                log("WTF!! 777 666 !!!!");
            }

        }
        //a regular packet from the server
        else {

            //int count = 0;
            //uint32_t a,b,c;
            //readLong(udp_data, count, a);
            //readLong(udp_data, count, b);
            //readLong(udp_data, count, c);

            //log("not a special packet. %i : %i %i %i",udp_length,a,b,c);

            //if client already disconnecting -- discard
            //else:
            if (want_connect == true)
                serverDataCallback(customp, ConstDataBlockRef(data, length));
        }
    }

    //called by thread_connect - try to connect. return true if should stop trying
    bool connect_try() throw () {

        //send a "want to connect" packet
        //FIXME: game client must specify data to send in the connection packet
        //           (like client version etc.)

        log("connect_try()");

        //test if already connected
        //
        if (connect_status != 2)
            return true;        // stop trying, already connected OR disconnected (?)

        //test enough tries
        //
        if (tries_left-- <= 0) {

            //stop connection
            stop_connect();

            //"no response"
            connectionCallback(customp, 3, ConstDataBlockRef(0, 0));

            //stop trying
            return true;
        }

        //send the packet
        ExpandingBinaryBuffer msg;
        msg.U32(0); //special packet
        msg.U32(1); //want to connect
        msg.U32(LEETNET_VERSION);     // LEETNET protocol/build version - must match server's

        //custom data?
        msg.U32(connect_data.size());      //amound of customdata, goes anyway
        msg.block(connect_data);

        sendRawPacket(msg);  // FIXME: deal with send erros?

        //keep trying
        return false;
    }

    //called by reader thread to check for quit condition
    bool reader_thread_quit() throw () {
        return quit_reader_thread;
    }

    //ctor
    client_ci(int thread_priority, const std::string& logPostfix) throw () :
        #ifdef LEETNET_LOG
        logp(g_leetnetLog ?
             static_cast<Log*>(new FileLog((wheregamedir + "log" + directory_separator + "leetclientlog" + logPostfix + ".txt").c_str(), true)) :
             static_cast<Log*>(new NoLog())),
        log(*logp),
        #else
        log(),
        #endif
        #ifdef LEETNET_DATA_LOG
        datalogMutex("client_ci::datalogMutex"),
        #endif
        packetDelay(0.),
        sendQueueMutex("client_ci::sendQueueMutex"),
        readerThreadManipulationMutex("client_ci::readerThreadManipulationMutex")
    {
        #ifdef LEETNET_DATA_LOG
        if (g_leetnetDataLog)
            datalog = fopen((wheregamedir + "log" + directory_separator + "leetclientdata" + logPostfix + ".bin").c_str(), "wb");
        else
            datalog = 0;
        #endif
        station = 0;
        want_connect = false;
        connect_status = 0;

        started_disconnection = false;      //if disconnection was started by the client
        quit_reader_thread = false;

        connect_threads_running = 0;

        threadPriority = thread_priority;

        //station agora tem reset_state(), entao cria no construtor e deleta no destrutor
        station = new_station_c();
    }

    //dtor
    virtual ~client_ci() throw () {
        //disconnect if connected
        connect(false);

        while (connect_threads_running) // added thread safety thing
            platSleep(100);

        //delete station
        if (station) {
            delete station;
            station = 0;
        }

        clearSendQueue();

        #ifdef LEETNET_DATA_LOG
        if (datalog)
            fclose(datalog);
        #endif
    }
};

//connector thread function
void thread_connect_f(client_ci* client) throw () {
    for (;;) {
        if (client->connect_try())
            break;
        platSleep(1000); // *** NO CPU PROBLEM HERE ***
    }
    --client->connect_threads_running;
}

//disconnector thread function
void thread_disconnect_f(client_ci* client) throw () {
    //repeat
    bool stop = false;
    while (!stop) {

        //try
        stop = client->disconnect_try();

        //sleep a bit before sending next try
        platSleep(100); // *** NO CPU PROBLEM HERE ***
    }

    //nice disconnect done
    client->nice_disconnect_done(server_c::disconnect_client_initiated);
}

//reader thread function
#define THREAD_READER_BUFSIZE 1024 // to protect bad code in later stages from too long packets, packets this long won't be sent anyway
void thread_reader_f(client_ci* client) throw () {
DLOG_ScopeNegStart("CTR");
    //read buffer
    char    buffer[THREAD_READER_BUFSIZE];
    int amount; //amount read

    while (!client->reader_thread_quit()) {
        //read from socket
        amount = client->read_station(DataBlockRef(buffer, THREAD_READER_BUFSIZE)); //nlRead(clsock, buffer, THREAD_READER_BUFSIZE);

        client->probeSendQueue();

        if (amount == 0) {
DLOG_ScopeNeg s("CTR");
            platSleep(2);  //alternativa, usar BLOCKING I/O
            continue;
        }

        // check for error
        if (amount < 0) {
            //DEBUG FIXME: error in nlGetError
            static volatile int menosmenos = 0;
            if (menosmenos == 0) {
                client->log("CLIENT READER: NL_INVALID!!");
            }
            menosmenos++;
            if (menosmenos > 3000)
                menosmenos = 0;
        }
        // process da packet
        else {
            //SLEEP(50); // lag

            client->process_incoming_datagram(ConstDataBlockRef(buffer, amount));
        }
    }
}


//factory
client_c* new_client_c(int thread_priority, const std::string& logPostfix) throw () {
    return new client_ci(thread_priority, logPostfix);
}

void QSendFrame::execute(client_ci* client, station_c*) throw () {
    client->doSendFrame(data);
}
