/*
 *  servnet.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2006, 2008 - Jani Rivinoja
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

#ifndef SERVNET_H_INC
#define SERVNET_H_INC

#include <map>
#include <queue>

#include "mutex.h"
#include "network.h"    // for NetworkResult
#include "protocol.h"
#include "thread.h"
#include "utility.h"

class GunDirection;
class MasterQuery;
class Powerup;
class Server;
class server_c;
class ServerHelloResult;
class ServerPlayer;
class ServerWorld;
class Rocket;

static const int pid_none = -1, pid_record = -2, pid_all = -3, shell_pid = -4; // pseudo pids used for no one, record only, everyone (includes record where appropriate), and admin shell user

class ServerNetworking {
public:
    class Settings {
    public:
        virtual ~Settings() throw () { }

        typedef HookFunctionHolder1<void, const std::string&> StatusOutputFnT;
        virtual StatusOutputFnT statusOutput() const throw () = 0;
        virtual bool showErrorCount() const throw () = 0;
        virtual int lowerPriority() const throw () = 0;
        virtual int networkPriority() const throw () = 0;
        virtual int minLocalPort() const throw () = 0;
        virtual int maxLocalPort() const throw () = 0;
        virtual bool dedicated() const throw () = 0;

        virtual bool privateServer() const throw () = 0;
        virtual const std::string& ip() const throw () = 0;
        virtual int get_port() const throw () = 0;
        virtual int get_srvmonit_port() const throw () = 0;
        virtual const std::string& get_hostname() const throw () = 0;

        virtual int get_join_start() const throw () = 0;
        virtual int get_join_end() const throw () = 0;
        virtual const std::string& get_join_limit_message() const throw () = 0;

        virtual unsigned get_spectating_delay() const throw () = 0;

        virtual const std::vector<std::string>& get_web_servers() const throw () = 0;
        virtual const std::string& get_web_script() const throw () = 0;
        virtual const std::string& get_web_auth() const throw () = 0;
        virtual int get_web_refresh() const throw () = 0;

        virtual int minimapSendLimit() const throw () = 0;

        virtual const std::string& get_server_password() const throw () = 0;
    };

private:
    class ClientTransferData {
    public:
        bool        serving_udp_file;
        std::string data;
        uint32_t     dp, old_dp;

    public:
        ClientTransferData() throw () {
            serving_udp_file = false;
        }
        void reset() throw () {
            data.clear();
            serving_udp_file = false;
        }
    };

    // server callbacks
    static void sfunc_client_hello          (void* customp, int client_id, ConstDataBlockRef data, ServerHelloResult* res) throw ();
    static void sfunc_client_connected      (void* customp, int client_id, int customStoredData) throw ();
    static void sfunc_client_disconnected   (void* customp, int client_id, bool reentrant) throw ();
    static void sfunc_client_data           (void* customp, int client_id, ConstDataBlockRef data) throw ();
    static void sfunc_client_lag_status     (void* customp, int client_id, int status) throw ();
    static void sfunc_client_ping_result    (void* customp, int client_id, int pingtime) throw ();

    const bool threadLock;    // if true, all concurrency is eliminated; its benefits are lost but there are many opportunities for bad timing to trigger problems
    Mutex& threadLockMutex;    // used to implement threadLock, if it is enabled; the mutex is external

    std::map<std::string, std::string> master_parameters(const std::string& address, bool quitting = false) const throw ();
    std::map<std::string, std::string> website_parameters(const std::string& address) const throw ();
    std::string website_maplist() const throw ();

    Server* host;
    const Settings& settings;
    ServerWorld&    world;
    int             maxplayers;

    server_c*       server;

    mutable LogSet  log;

    double          frameSentTime;  // at what time the last frame was sent

    volatile bool   mjob_exit;              //flag for all pending master jobs to quit now
    volatile bool   mjob_fastretry;         //flag for all pending master jobs to stop waiting and retry immediately
    volatile int    mjob_count;
    Mutex           mjob_mutex;             //mutex for socket list

    int             max_world_rank;

    ClientTransferData fileTransfer[MAX_PLAYERS];
    volatile bool   file_threads_quit;      //#fix: this is used by all kinds of threads even though file threads no longer exist

    mutable Network::TCPSocket shellssock; // if open, admin shell messages are sent to this socket
    Thread          shellmthread;

    Thread          mthread;
    Thread          webthread;

    std::string     server_identification;
    int             ping_send_client;
    int             ctop[256];          // client id-to-player id index
    int             player_count;       // number of players including bots
    int             bot_count;
    std::vector< std::pair<Network::Address, int> > distinctRemotePlayers;
    int             localPlayers;
    Mutex           addPlayerMutex;
    unsigned        newUniqueId;
    std::queue< std::pair<unsigned, double> > freedUniqueIds; // pair of id, time of allowed reuse

    uint32_t         accelerationModeMask;
    uint8_t         flagModeMask;

    int             maplist_revision;   // used by website thread to determine when to resend maplist

    class RelayThread {
        struct RelayData {
            RelayData(int t, ConstDataBlockRef d) throw () : time(t), data(d) { }
            int time;
            DataBlock data;
        };

        Thread thread;
        volatile bool& quitFlag;
        Network::TCPSocket socket;
        bool newGame;
        int delay;
        std::queue<RelayData> dataQueue;
        ConditionVariable wakeup;
        mutable Mutex mutex;
        mutable LogSet log;

        void threadMain() throw ();
        void pushData_locked(ConstDataBlockRef data) throw ();
        bool isConnected_locked() const throw () { return socket.isOpen(); }

    public:
        RelayThread(LogSet logs, volatile bool& quitFlag_) throw ();
        ~RelayThread() throw () { socket.closeIfOpen(); }

        void start(int priority) throw ();
        void stop() throw ();

        void startNewGame(const Network::Address& relayAddress, ConstDataBlockRef initData, int gameDelay) throw ();
        void pushFrame(ConstDataBlockRef frame) throw ();

        bool isConnected() const throw () { Lock ml(mutex); return isConnected_locked(); }
    };

    Network::Address relay_address;
    RelayThread relayThread;

    double playerSlotReservationTime; // the last time reservedPlayerSlots was bumped, used to erase unused reservations
    int reservedPlayerSlots; // number of clients that have been seen (in clientHello) but not yet connected

    void upload_next_file_chunk(int i) throw ();
    std::string get_download_file(const std::string& ftype, const std::string& fname) throw ();

    void clientHello(int client_id, ConstDataBlockRef data, ServerHelloResult* res) throw ();
    int  client_connected(int id, int customStoredData) throw ();
    void client_disconnected(int id) throw ();
    void ping_result(int client_id, int ping_time) throw ();
    bool processMessage(int pid, ConstDataBlockRef data) throw ();
    void incoming_client_data(int id, ConstDataBlockRef data) throw ();

    void logTCPThreadError(const Network::Error& error, const std::string& text) throw ();

    void master_job_response(MasterQuery *j) throw ();
    void run_masterjob_thread(MasterQuery* job) throw ();
    void run_mastertalker_thread() throw ();
    void send_master_quit(const std::string& localAddress) const throw ();

    bool writeToAdminShell(ConstDataBlockRef data) const throw ();

    bool read_string_from_TCP(Network::TCPSocket& sock, std::string& resultStr) throw (Network::ReadWriteError);
    void handleNewAdminShell(Thread& slaveThread, volatile bool& slaveRunning) throw (Network::Error);
    void run_shellmaster_thread(int port) throw ();
    void executeAdminCommand(uint32_t code, uint32_t cid, int pid, uint32_t dwArg, BinaryWriter& answer) throw (Network::Error);
    bool handleAdminCommand() throw (Network::Error);
    void run_shellslave_thread(volatile bool* quitFlag) throw ();

    void run_website_thread() throw ();

    void broadcast_message(ConstDataBlockRef data) const throw ();
    void send_simple_message(Network_data_code code, int pid) const throw ();
    void broadcast_simple_message(Network_data_code code) const throw ();
    void broadcast_screen_message(int px, int py, ConstDataBlockRef msg) const throw ();

    void record_message(ConstDataBlockRef data) const throw ();

    void writeMinimapPlayerPosition(BinaryWriter& writer, int pid) const throw ();

public:

    ServerNetworking(Server* hostp, const Settings& settings, ServerWorld& w, LogSet logs, bool threadLock, Mutex& threadLockMutex) throw ();
    ~ServerNetworking() throw ();
    void setMaxPlayers(int num) throw () { maxplayers = num; }

    bool start() throw ();
    void stop() throw ();

    void update_serverinfo() throw ();
    double getTraffic() const throw ();

    void removePlayer(int pid) throw (); // call only when moving players around; this actually does close to nothing
    void disconnect_client(int cid, int timeout, Disconnect_reason reason) throw ();
    int getPid(int cid) const throw () { return ctop[cid]; }   //#fix: this shouldn't be necessary

    void send_me_packet(int pid) const throw ();
    void send_player_name_update(int cid, int pid) const throw ();
    void broadcast_player_name(int pid) const throw ();
    void send_player_crap_update(int cid, int pid) throw ();
    void broadcast_player_crap(int pid) throw ();
    void broadcast_team_change(int from, int to, bool swap) const throw ();
    void warnAboutExtensionAdvantage(int pid) const throw ();
    void send_acceleration_modes(int pid) const throw ();
    void send_flag_modes(int pid) const throw ();

    void broadcast_reset_map_list() throw ();
    void broadcast_current_map(int mapNr) const throw ();
    void broadcast_stats_ready() const throw ();
    void broadcast_5_min_left() const throw ();
    void broadcast_1_min_left() const throw ();
    void broadcast_30_s_left() const throw ();
    void broadcast_time_out() const throw ();
    void broadcast_extra_time_out() const throw ();
    void broadcast_normal_time_out(bool sudden_death) const throw ();
    void broadcast_capture(const ServerPlayer& player, int flag_team) const throw ();
    void broadcast_flag_take(const ServerPlayer& player, int flag_team) const throw ();
    void broadcast_flag_return(const ServerPlayer& player) const throw ();
    void broadcast_flag_drop(const ServerPlayer& player, int flag_team) const throw ();
    void broadcast_kill(const ServerPlayer& attacker, const ServerPlayer& target,
                        DamageType cause, bool flag, bool wild_flag, bool carrier_defended, bool flag_defended) const throw ();
    void broadcast_suicide(const ServerPlayer& player, bool flag, bool wild_flag) const throw ();
    void send_waiting_time(const ServerPlayer& player) const throw ();
    void broadcast_new_player(const ServerPlayer& player) const throw ();
    void new_player_to_admin_shell(int pid) const throw ();  // called when the player name is known (unlike at broadcast_new_player)
    void broadcast_player_left(const ServerPlayer& player) const throw ();
    void broadcast_spawn(const ServerPlayer& player) const throw ();
    void broadcast_movements_and_shots(const ServerPlayer& player) const throw ();   // Send player's movement and shots to everyone.
    void broadcast_stats(const ServerPlayer& player) const throw ();             // Send player's stats to everyone.
    void send_stats(const ServerPlayer& player) const throw ();                  // Send everyone's stats to player.
    void send_stats(const ServerPlayer& player, int cid) const throw ();         // Send player's stats to client cid.
    void send_team_movements_and_shots(int cid) const throw ();
    void send_team_stats(const ServerPlayer& player) const throw ();

    void send_map_info(const ServerPlayer& player) const throw ();
    void send_map_vote(const ServerPlayer& player) const throw ();
    void broadcast_map_votes_update() throw ();
    void send_map_change_message(int pid, int reason, const char* mapname) const throw ();
    void broadcast_map_change_message(int reason, const char* mapname) const throw ();
    void send_map_time(int cid) const throw ();
    void send_server_settings(const ServerPlayer& player) const throw ();
    void send_server_settings(int cid) const throw ();
    void broadcast_map_change_info(int votes, int needed, int vote_block_time) const throw ();

    void send_too_much_talk(int pid) const throw ();
    void send_mute_notification(int pid) const throw ();
    void send_tournament_update_failed(int pid) const throw ();
    void broadcast_mute_message(int pid, int mode, const std::string& admin, bool inform_target) const throw ();
    void broadcast_kick_message(int pid, int minutes, const std::string& admin) const throw ();
    void send_idlekick_warning(int pid, int seconds) const throw ();
    void send_disconnecting_message(int pid, int seconds) const throw ();
    void broadcast_broken_map() const throw ();

    void ctf_net_flag_status(int cid, int team) const throw ();
    void ctf_update_teamscore(int t) const throw ();
    void move_update_player(int a) throw (); // call after moving, a = pid after move
    void client_report_status(int id) throw ();
    void sendWorldReset() const throw ();
    void sendStartGame() const throw ();
    void sendWeaponPower(int pid) const throw ();
    void sendRocketMessage(int shots, GunDirection gundir, uint8_t* sid, int pid, bool power, int px, int py, int x, int y, uint32_t vislist) const throw (); // sid = shot-id: array of uint8_t[shots]
    void sendOldRocketVisible(int pid, int rid, const Rocket& rocket) const throw ();
    void sendRocketDeletion(uint32_t plymask, int rid, int16_t hitx, int16_t hity, int targ) const throw ();
    void sendDeathbringer(int pid, const ServerPlayer& ply) const throw ();
    void sendPowerupVisible(int pid, int pup_id, const Powerup& it) const throw ();
    void broadcastPowerupPicked(int roomx, int roomy, int pup_id) const throw ();
    void sendPupTime(int pid, uint8_t pupType, double timeLeft) const throw ();
    void sendFragUpdate(int pid, uint32_t frags) const throw ();
    void sendNameAuthorizationRequest(int pid) const throw ();

    void broadcast_sample(int code) const throw ();
    void broadcast_screen_sample(int p, int code) const throw ();
    void broadcast_screen_power_collision(int p) const throw ();
    void broadcast_team_message(int team, const std::string& text) const throw ();
    void bprintf(Message_type type, const char *fs, ...) const throw () PRINTF_FORMAT(3, 4);
    void plprintf(int pid, Message_type type, const char* fmt, ...) const throw () PRINTF_FORMAT(4, 5);
    void player_message(int pid, Message_type type, const std::string& text) const throw ();
    void broadcast_text(Message_type type, const std::string& text) const throw ();
    void record_players_present() const throw ();

    void set_relay_server(const std::string& address) throw ();
    std::string get_relay_server() const throw ();
    bool is_relay_used() const throw ();
    bool is_relay_active() const throw ();
    void send_first_relay_data(ConstDataBlockRef data) throw ();
    void send_relay_data(ConstDataBlockRef data) throw ();

    void forwardSayadminMessage(int cid, const std::string& message) const throw ();
    void sendTextToAdminShell(const std::string& text) const throw ();

    void broadcast_frame(bool gameRunning) throw ();

    Network::Address get_client_address(int cid) const throw ();
    int get_player_count() const throw () { return player_count; }
    int get_human_count() const throw () { return player_count - bot_count; }
    int get_bot_count() const throw () { return bot_count; }
    int numDistinctClients() const throw () { return distinctRemotePlayers.size() + (localPlayers > 0 ? 1 : 0); }
};

#endif
