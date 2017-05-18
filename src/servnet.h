/*
 *  servnet.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2006 - Jani Rivinoja
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

#include "mutex.h"
#include "network.h"    // for NetworkResult
#include "protocol.h"   // needed for possible definition of SEND_FRAMEOFFSET, and otherwise
#include "thread.h"
#include "utility.h"

#include <map>

class Server;
class MasterQuery;
class Powerup;
class Rocket;
class server_c;
class ServerHelloResult;
class ServerPlayer;
class ServerWorld;

class ServerNetworking {
    class ClientTransferData {
    public:
        //v0.4.4 UDP FILE transfer
        bool        serving_udp_file;           //if TRUE, already serving a file
        char        *data;                  //the file data
        NLulong     dp,old_dp,fsize;                //the file pointer and the total size

    public:
        ClientTransferData() {
            serving_udp_file = false;
            data = 0;
        }
        void reset() {
            if ((serving_udp_file) && (data)) {
                delete data;
                data = 0;
            }
            serving_udp_file = false;
        }
    };

    // server callbacks
    static void sfunc_client_hello          (void* customp, int client_id, char* data, int length, ServerHelloResult* res);
    static void sfunc_client_connected      (void* customp, int client_id);
    static void sfunc_client_disconnected   (void* customp, int client_id, bool reentrant);
    static void sfunc_client_data           (void* customp, int client_id, char* data, int length);
    static void sfunc_client_lag_status     (void* customp, int client_id, int status);
    static void sfunc_client_ping_result    (void* customp, int client_id, int pingtime);

    const bool threadLock;    // if true, all concurrency is eliminated; its benefits are lost but there are many opportunities for bad timing to trigger problems
    MutexHolder& threadLockMutex;    // used to implement threadLock, if it is enabled; the mutex is external

    std::map<std::string, std::string> master_parameters(const std::string& address, bool quitting = false) const;
    std::map<std::string, std::string> website_parameters(const std::string& address) const;
    std::string website_maplist() const;
    std::string build_http_data(const std::map<std::string, std::string>& parameters) const;
    NetworkResult post_http_data(NLsocket& socket, const volatile bool* abortFlag, int timeout,
                            const std::string& script, const std::string& parameters, const std::string& auth = "") const;  // timeout in ms
    NetworkResult save_http_response(NLsocket& socket, std::ostream& out, const volatile bool* abortFlag, int timeout) const;   // timeout in ms

    Server* host;
    ServerWorld&    world;
    int             maxplayers;

    server_c*       server;

    LogSet          log;

    #ifdef SEND_FRAMEOFFSET
    double          frameSentTime;  // at what time the last frame was sent
    #endif

    volatile bool   mjob_exit;              //flag for all pending master jobs to quit now
    volatile bool   mjob_fastretry;         //flag for all pending master jobs to stop waiting and retry immediately
    volatile int    mjob_count;
    MutexHolder     mjob_mutex;             //mutex for socket list

    int             max_world_rank;

    ClientTransferData fileTransfer[MAX_PLAYERS];
    volatile bool   file_threads_quit;      //#fix: this is used by all kinds of threads even though file threads no longer exist

    NLsocket        shellssock; // set NL_INVALID when no connection; otherwise admin shell messages can be sent to this socket
    Thread          shellmthread;

    Thread          mthread;
    Thread          webthread;

    std::string     hostname;
    std::string     server_password;
    std::string     server_identification;
    int             ping_send_client;
    int             ctop[256];          // client id-to-player id index
    int             player_count;
    std::vector< std::pair<NLaddress, int> > distinctRemotePlayers;
    int             localPlayers;
    MutexHolder     addPlayerMutex;

    int             maplist_revision;   // used by website thread to determine when to resend maplist

    int             join_start;         // allow joining from this time of a day (in seconds)
    int             join_end;           // disallow joining; set both same to allow always (default)
    std::string     join_limit_message; // when joining is disallowed, this message is sent to asking clients in addition to information about the open times

    // web site settings
    std::vector<std::string> web_servers;
    std::string web_script;
    std::string web_auth;
    int web_refresh;

    double playerSlotReservationTime; // the last time reservedPlayerSlots was bumped, used to erase unused reservations
    int reservedPlayerSlots; // number of clients that have been seen (in clientHello) but not yet connected

    void upload_next_file_chunk(int i);
    int  get_download_file(char *lebuf, char *ftype, char *fname);

    void clientHello(int client_id, char* data, int length, ServerHelloResult* res);
    int  client_connected(int id);
    void client_disconnected(int id);
    void ping_result(int client_id, int ping_time);
    void incoming_client_data(int id, char *data, int length);

    void master_job_response(MasterQuery *j);
    void run_masterjob_thread(MasterQuery* job);
    void run_mastertalker_thread();

    bool read_string_from_TCP(NLsocket sock, char *buf);
    void run_shellmaster_thread(int port);
    void run_shellslave_thread(volatile bool* quitFlag);

    void run_website_thread();

    void broadcast_message(const char* data, int length) const;
    void send_simple_message(Network_data_code code, int pid) const;
    void broadcast_simple_message(Network_data_code code) const;

public:
    ServerNetworking(Server* hostp, ServerWorld& w, LogSet logs, bool threadLock, MutexHolder& threadLockMutex);
    ~ServerNetworking();
    void setMaxPlayers(int num) { maxplayers = num; }

    bool start();
    void stop();

    void update_serverinfo();
    double getTraffic() const;

    void removePlayer(int pid); // call only when moving players around; this actually does close to nothing
    void disconnect_client(int cid, int timeout, Disconnect_reason reason);
    int getPid(int cid) const { return ctop[cid]; }   //#fix: this shouldn't be necessary

    void send_me_packet(int pid) const;
    void send_player_name_update(int cid, int pid) const;
    void broadcast_player_name(int pid) const;
    void send_player_crap_update(int cid, int pid);
    void broadcast_player_crap(int pid);
    void broadcast_team_change(int from, int to, bool swap) const;

    void broadcast_reset_map_list();
    void broadcast_current_map(int mapNr) const;
    void broadcast_stats_ready() const;
    void broadcast_5_min_left() const;
    void broadcast_1_min_left() const;
    void broadcast_30_s_left() const;
    void broadcast_time_out() const;
    void broadcast_extra_time_out() const;
    void broadcast_normal_time_out(bool sudden_death) const;
    void broadcast_capture(const ServerPlayer& player, int flag_team) const;
    void broadcast_flag_take(const ServerPlayer& player, int flag_team) const;
    void broadcast_flag_return(const ServerPlayer& player) const;
    void broadcast_flag_drop(const ServerPlayer& player, int flag_team) const;
    void broadcast_kill(const ServerPlayer& attacker, const ServerPlayer& target,
                        DamageType cause, bool flag, bool wild_flag, bool carrier_defended, bool flag_defended) const;
    void broadcast_suicide(const ServerPlayer& player, bool flag, bool wild_flag) const;
    void broadcast_new_player(const ServerPlayer& player) const;
    void new_player_to_admin_shell(int pid) const;  // called when the player name is known (unlike at broadcast_new_player)
    void broadcast_player_left(const ServerPlayer& player) const;
    void broadcast_spawn(const ServerPlayer& player) const;
    void broadcast_movements_and_shots(const ServerPlayer& player) const;   // Send player's movement and shots to everyone.
    void broadcast_stats(const ServerPlayer& player) const;             // Send player's stats to everyone.
    void send_stats(const ServerPlayer& player) const;                  // Send everyone's stats to player.
    void send_stats(const ServerPlayer& player, int cid) const;         // Send player's stats to client cid.
    void send_team_movements_and_shots(const ServerPlayer& player) const;
    void send_team_stats(const ServerPlayer& player) const;

    void send_map_info(const ServerPlayer& player) const;
    void send_map_vote(const ServerPlayer& player) const;
    void broadcast_map_votes_update();
    void send_map_change_message(int pid, int reason, const char* mapname) const;
    void broadcast_map_change_message(int reason, const char* mapname) const;
    void send_map_time(int cid) const;
    void send_server_settings(const ServerPlayer& player) const;
    void broadcast_map_change_info(int votes, int needed, int vote_block_time) const;

    void send_too_much_talk(int pid) const;
    void send_mute_notification(int pid) const;
    void send_tournament_update_failed(int pid) const;
    void broadcast_mute_message(int pid, int mode, const std::string& admin, bool inform_target) const;
    void broadcast_kick_message(int pid, int minutes, const std::string& admin) const;
    void send_idlekick_warning(int pid, int seconds) const;
    void send_disconnecting_message(int pid, int seconds) const;
    void broadcast_broken_map() const;

    void ctf_net_flag_status(int cid, int team) const;
    void ctf_update_teamscore(int t) const;
    void move_update_player(int a); // call after moving, a = pid after move
    void client_report_status(int id);
    void sendWorldReset() const;
    void sendStartGame() const;
    void sendWeaponPower(int pid) const;
    void sendRocketMessage(int shots, int gundir, NLubyte* sid, int team, bool power, int px, int py, int x, int y) const; // sid = shot-id: array of NLubyte[shots]
    void sendOldRocketVisible(int pid, int rid, const Rocket& rocket) const;
    void sendRocketDeletion(NLulong plymask, int rid, NLshort hitx, NLshort hity, int targ) const;
    void sendDeathbringer(int pid, const ServerPlayer& ply) const;
    void sendPickupVisible(int pid, int pup_id, const Powerup& it) const;
    void sendPupTime(int pid, NLubyte pupType, double timeLeft) const;
    void sendFragUpdate(int pid, NLulong frags) const;
    void sendNameAuthorizationRequest(int pid) const;

    void broadcast_sample(int code) const;
    void broadcast_screen_sample(int p, int code) const;
    void broadcast_screen_power_collision(int p) const;
    void broadcast_team_message(int team, const std::string& text) const;
    void broadcast_screen_message(int px, int py, char *lebuf, int count) const;
    void bprintf(Message_type type, const char *fs, ...) const PRINTF_FORMAT(3, 4);
    void plprintf(int pid, Message_type type, const char* fmt, ...) const PRINTF_FORMAT(4, 5);
    void player_message(int pid, Message_type type, const std::string& text) const;
    void broadcast_text(Message_type type, const std::string& text) const;

    void forwardSayadminMessage(int cid, const std::string& message) const;
    void sendTextToAdminShell(const std::string& text) const;

    void broadcast_frame(bool gameRunning);

    void set_hostname(const std::string& name);
    NLaddress get_client_address(int cid) const;
    int get_player_count() const { return player_count; }
    int numDistinctClients() const { return distinctRemotePlayers.size() + (localPlayers > 0 ? 1 : 0); }

    void set_join_start(int val) { join_start = val; }
    void set_join_end  (int val) { join_end   = val; }
    void set_join_limit_message(const std::string& msg) { join_limit_message = msg; }

    void clear_web_servers() { web_servers.clear(); }
    void add_web_server(const std::string& server) { web_servers.push_back(server); }
    void set_web_script(const std::string& script) { web_script = script; }
    void set_web_auth(const std::string& auth) { web_auth = auth; }
    void set_web_refresh(int refresh);

    void set_server_password(const std::string& passwd) { server_password = passwd; }
};

#endif
