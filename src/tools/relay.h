/*
 *  relay.h
 *
 *  Copyright (C) 2008 - Jani Rivinoja
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

#ifndef RELAY_H_INC
#define RELAY_H_INC

#include <fstream>
#include <deque>
#include <sstream>
#include <vector>

#include "../network.h"
#include "../pointervector.h"

class Peer : private NoCopying {
public:
    Peer(const Network::Address& addr, TrashableRef<Network::TCPSocket> sock) throw () : address(addr), socket(sock) { }

    Network::Address address;
    Network::TCPSocket socket;
    std::stringstream buffer;
};

class Spectator : private NoCopying {
public:
    Spectator(const Network::Address& addr, TrashableRef<Network::TCPSocket> sock) throw () :
        address(addr),
        socket(sock),
        local(isLocalIP(addr)),
        next_frame(0),
        bytes_sent(0),
        first_buffer_sent(false)
    { }

    Network::Address address;
    Network::TCPSocket socket;
    bool local;
    unsigned  next_frame;
    unsigned  bytes_sent;
    bool first_buffer_sent;
};

class Frame {
public:
    Frame(int l, ConstDataBlockRef d, double t) throw () : len(l), time_(t) { data_.block(d); }

    void add(ConstDataBlockRef d, double t) throw () { data_.block(d); time_ = t; }

    unsigned length() const throw () { return len; }
    unsigned used() const throw () { return data_.size(); }
    unsigned remaining() const throw () { return length() - used(); }
    bool full() const throw () { return used() == length(); }
    ConstDataBlockRef data() const throw () { return data_; }
    double time() const throw () { return time_; }

private:
    unsigned    len;
    ExpandingBinaryBuffer data_;
    double      time_;
};

class Game {
public:
    Game() throw () : finished_(false) { }

    void add(const Frame& f) throw () { frames.push_back(f); }
    void finish() throw () { finished_ = true; }

    unsigned size() const throw () { return frames.size(); }

    const Frame& frame(unsigned i) const throw () { return frames[i]; }
    std::vector<Frame>& buffer() throw () { return frames; }

    bool finished() const throw () { return finished_; }

private:
    std::vector<Frame> frames;
    bool finished_;
};

class Relay {
public:
    class ArgumentException {
    public:
        ArgumentException(const std::string& msg_) throw () : msg(msg_) { }
        const std::string& message() const throw () { return msg; }

    private:
        std::string msg;
    };

    Relay() throw ();
    ~Relay() throw ();

    /// Load settings from command line parameters
    void load_settings(const std::vector<std::string>& parameters) throw (ArgumentException);

    void run() throw ();

private:
    /// Listen for new connections
    void listen() throw ();

    /// Handle connections just opened
    void check_new_connections() throw ();
    bool check_new_connection(Peer& p) throw (); // returns true if the peer was handled and can be removed

    /// Read game data from the Outgun server
    void get_server_data() throw ();

    /// Add game data to buffer
    bool add_data(SeekableBinaryReader& reader) throw ();

    /// Send data to every spectator
    void send_data() throw ();

    /// Send data to the socket
    int send_data(Network::TCPSocket& socket, ConstDataBlockRef data) const throw ();

    /// Remove the oldest game if it is not needed anymore
    void remove_oldest_game() throw ();

    const Frame* get_frame(unsigned frame_nr) const throw ();
    bool frame_data(BinaryWriter& target, unsigned frame_nr, unsigned pos) const throw ();

    void load_master_settings() throw ();
    void send_master_server() throw ();

    Network::TCPListenerSocket listen_socket;     /// Socket for all incoming connections
    unsigned short listen_port;       /// Port for all incoming connections

    Network::Address server_address;  /// Game server address
    Network::TCPSocket server_socket; /// Game server socket
    std::string hostname;

    unsigned bandwidth_limit;   /// Total bandwidth limit, bytes per second
    unsigned spectator_limit;   /// Maximum number of spectators for this relay
    unsigned game_delay;        /// Delay from the live game in seconds
    unsigned server_delay;      /// Delay from the live game on the game server, in seconds

    PointerVector<Spectator> spectators;
    PointerVector<Peer> peers;  /// Just connected "things"
    std::deque<Game> games;     /// Game data

    ExpandingBinaryBuffer waiting_data;

    Frame first_buffer;          /// Initial buffer that basically has the same data as in the start of the replay
    unsigned buffer_first_frame; /// Frame number of the start frame of the first game in list

    std::string master_name;
    std::string master_submit;
    Network::Address master_socket;
    double master_talk_time;
};

#endif // RELAY_H_INC
