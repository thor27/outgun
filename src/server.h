/*
 *  server.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2006, 2008 - Niko Ritari
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

#ifndef SERVER_H_INC
#define SERVER_H_INC

#include "binaryaccess.h"
#include "world.h"
#include "gameserver_interface.h"
#include "log.h"
#include "auth.h"
#include "servnet.h"
#include "utility.h"

class ClientInterface; // bots are Clients
class GamemodSetting;

//per-client struct (statically allocated to a single client)
class ClientData {
public:
    //v0.4.4 PLAYER REGISTRATION STATUS
    bool        token_have;                 //player claims to be registered with (name,token)
    bool        token_valid;        //player (name,token) is validated
    std::string token;                  //the player's token
    int         intoken;                        //integer version of token

    //v0.4.4 client statistics
    int         delta_score;        //the player's score accumulator
    int         neg_delta_score;        //NEG score accum 0.4.8

    double fdp;     //DOUBLE delta accums. os acima sao apenas o "trunc atual"
    double fdn;

    int         rank;                       //current ranking position
    int         score;                  //current score POS -- SOMATORIO (né?!?!?)
    int         neg_score;                  //current score NEG 0.4.8 -- SOMATORIO (né?!?!?)

    bool        current_participation;
    bool        next_participation;
    bool        participation_info_received;

    ClientData() throw () {
        reset();
    }

    void reset() throw () {
        delta_score = 0;
        neg_delta_score = 0;
        fdp = 0.0;
        fdn = 0.0;
        score = 0;
        neg_score = 0;
        rank = 0;

        token_have = false;
        token_valid = false;
        token.clear();
        intoken = 666;

        current_participation = false;
        next_participation = false;
        participation_info_received = false;
    }
};

class Server : private NoCopying {
    FileLog normalLog;
    DualLog errorLog;
    SupplementaryLog<FileLog> securityLog;
    SupplementaryLog<FileLog> adminActionLog;
    mutable LogSet log;

    const bool threadLock;    // if true, all concurrency is eliminated; its benefits are lost but there are many opportunities for bad timing to trigger problems so it's often wise
    Mutex threadLockMutex;    // used to implement threadLock, if it is enabled

    bool abortFlag;

    // client control
    double          team_smul[2];
    uint32_t         next_vote_announce_frame;
    int             last_vote_announce_votes, last_vote_announce_needed;
    ClientData      client[MAX_PLAYERS];
    std::vector<bool> fav_colors[2];

    Thread          botthread;
    std::vector<ClientInterface*> bots;
    int extra_bots;
    volatile bool quit_bots;
    NoLog botNoLog;
    MemoryLog botErrorLog;
    bool check_bots;
    bool bot_ping_changed;

    void init_bots() throw ();
    void run_bot_thread() throw ();

    // world
    ServerWorld     world;
    bool            gameover;
    double          gameover_time;      //timeout for gameover plaque
    int             maxplayers;

    // networking
    ServerNetworking network;

    class SettingManager : public ServerNetworking::Settings {
    public:
        typedef AuthorizationDatabase::AccessDescriptor::GamemodAccessDescriptor GamemodAccessDescriptor;

    private:
        Server& server;
        // handy aliases to server.*:
        ServerNetworking& network;
        ServerWorld& world;

        const ServerExternalSettings extConfig;
        // values copied from extConfig that may be changed:
        std::string     ipAddress;
        int             port;
        bool            privateserver;

        PowerupSettings pupConfig;
        WorldSettings   worldConfig;
        int             game_end_delay;
        int             vote_block_time;    // how long a mapchange can't be voted (except unanimously), in frames (in gamemod, it is minutes)
        bool            require_specific_map_vote;
        std::vector<std::string> welcome_message;   // welcome message line by line
        std::vector<std::string> info_message;      // the message /info shows, line by line
        std::string     sayadmin_comment;
        bool            sayadmin_enabled;
        int             idlekick_time, idlekick_playerlimit;
        int             min_bots;
        int             bots_fill;
        int             bot_ping;
        bool            balance_bot;
        std::string     bot_name_lang;
        bool            tournament;
        int             save_stats;
        bool            random_maprot;
        bool            random_first_map;
        std::string     server_website_url; // the URL of the server website to be sent to master server
        int             srvmonit_port;
        int             recording;
        unsigned        spectating_delay;
        int             minimap_send_limit;

        int             join_start;         // allow joining from this time of a day (in seconds)
        int             join_end;           // disallow joining; set both same to allow always (default)
        std::string     join_limit_message; // when joining is disallowed, this message is sent to asking clients in addition to information about the open times

        std::vector<std::string> web_servers;
        std::string     web_script, web_auth;
        int             web_refresh;

        std::string     server_password;
        std::string     hostname;

        bool            log_player_chat;

        class DisposerBase {
        public:
            virtual ~DisposerBase() throw () { }
            virtual void dispose() throw () = 0;
        };

        template<class T> class Disposer : public DisposerBase {
            T* ptr;
        public:
            Disposer(T* ptr_) throw () : ptr(ptr_) { }
            void dispose() throw () { delete ptr; }
        };

        std::vector<DisposerBase*> redirectFnDisposers;

        template<class T> T* addFn(T* ptr) throw () { redirectFnDisposers.push_back(new Disposer<T>(ptr)); return ptr; }

        struct Category { // no pointers contained are owned by this object
            const char* identifier;
            const char* descriptiveName;
            std::vector<GamemodSetting*> settings;

            Category(const char* id, const char* name) throw () : identifier(id), descriptiveName(name) { }
            void add(GamemodSetting* setting) throw () { settings.push_back(setting); }
        };
        std::vector<Category> categories;
        bool built, builtForReload;

        static bool checkMaxplayerSetting(int val) throw () { return (val >= 2 && val <= MAX_PLAYERS && val % 2 == 0); }
        static bool checkForceIpValue(const std::string& val) throw ();
        static std::string returnEmptyString() throw () { return std::string(); }
        bool trySetMaxplayers(int val) throw ();
        bool setForceIP(const std::string& val) throw ();
        void setRandomMaprot(int val) throw ();
        const std::string& getForceIP() const throw ();
        int getMaxplayers() const throw ();
        int getRandomMaprot() const throw ();

        void free() throw ();
        void build(bool reload) throw ();
        void commit(bool reload) throw ();
        void processLine(const std::string& line, LogSet& argLogs, bool allowGet, const GamemodAccessDescriptor& access) const throw ();

    public:
        SettingManager(Server& server_, const ServerExternalSettings& extConfig_) throw () : server(server_), network(server_.network), world(server_.world),
                extConfig(extConfig_), ipAddress(extConfig.ipAddress), port(extConfig.port), built(false) { }
        ~SettingManager() throw () { free(); }

        std::vector<std::string> listSettings(const GamemodAccessDescriptor& access) throw ();
        std::vector<std::string> executeLine(const std::string& line, const GamemodAccessDescriptor& access) throw ();
        void loadGamemod(bool reload) throw ();

        bool isGamemodCommand(const std::string& cmd, bool includeCategories) throw (); // can't be const because might need to build()

        bool isGamemodCommand(const std::string& cmd) throw () { return isGamemodCommand(cmd, false); }
        bool isGamemodCommandOrCategory(const std::string& cmd) throw () { return isGamemodCommand(cmd, true); }

        void reset() throw ();

        void set_min_bots(int val) throw () { min_bots = val; }
        void set_bots_fill(int val) throw () { bots_fill = val; }
        void set_bot_ping(int val) throw () { bot_ping = val; }
        void set_balance_bot(bool val) throw () { balance_bot = val; }

        bool ownScreen() const throw () { return extConfig.ownScreen; }
        ServerExternalSettings::StatusOutputFnT statusOutput() const throw () { return extConfig.statusOutput; }
        bool showErrorCount() const throw () { return extConfig.showErrorCount; }
        int lowerPriority() const throw () { return extConfig.lowerPriority; }
        int networkPriority() const throw () { return extConfig.networkPriority; }
        int minLocalPort() const throw () { return extConfig.minLocalPort; }
        int maxLocalPort() const throw () { return extConfig.maxLocalPort; }
        bool dedicated() const throw () { return extConfig.dedserver; }

        bool privateServer() const throw () { return privateserver; }
        const std::string& ip() const throw () { return ipAddress; }
        int get_port() const throw () { return port; }
        int get_srvmonit_port() const throw () { return srvmonit_port; }

        int minimapSendLimit() const throw () { return minimap_send_limit; }

        int  get_game_end_delay() const throw () { return game_end_delay; }
        int  get_vote_block_time() const throw () { return vote_block_time; }
        bool get_require_specific_map_vote() const throw () { return require_specific_map_vote; }

        const std::vector<std::string>& get_welcome_message() const throw () { return welcome_message; }
        const std::vector<std::string>& get_info_message() const throw () { return info_message; }
        const std::string& get_sayadmin_comment() const throw () { return sayadmin_comment; }
        bool get_sayadmin_enabled() const throw () { return sayadmin_enabled; }

        int  get_idlekick_time() const throw () { return idlekick_time; }
        int  get_idlekick_playerlimit() const throw () { return idlekick_playerlimit; }

        int  get_min_bots() const throw () { return min_bots; }
        int  get_bots_fill() const throw () { return bots_fill; }
        int  get_bot_ping() const throw () { return bot_ping; }
        bool get_balance_bot() const throw () { return balance_bot; }
        const std::string& get_bot_name_lang() const throw () { return bot_name_lang; }

        bool get_tournament() const throw () { return tournament; }
        int  get_save_stats() const throw () { return save_stats; }

        bool get_random_maprot() const throw () { return random_maprot; }
        bool get_random_first_map() const throw () { return random_first_map; }

        const std::string& get_server_website_url() const throw () { return server_website_url; }

        int  get_recording() const throw () { return recording; }
        unsigned get_spectating_delay() const throw () { return spectating_delay; }

        bool get_log_player_chat() const throw () { return log_player_chat; }

        int get_join_start() const throw () { return join_start; }
        int get_join_end() const throw () { return join_end; }
        const std::string& get_join_limit_message() const throw () { return join_limit_message; }

        const std::vector<std::string>& get_web_servers() const throw () { return web_servers; }
        const std::string& get_web_script() const throw () { return web_script; }
        const std::string& get_web_auth() const throw () { return web_auth; }
        int get_web_refresh() const throw () { return web_refresh; }

        const std::string& get_hostname() const throw () { return hostname; }
        const std::string& get_server_password() const throw () { return server_password; }
    };

    SettingManager settings;

    std::vector<MapInfo> maprot;
    int currmap;        // current map in maprot
    AuthorizationDatabase authorizations;

    // recording
    bool recording_started;
    std::string record_filename;
    mutable std::ofstream record;
    mutable ExpandingBinaryBuffer record_messages;
    uint32_t record_start_frame;
    std::string record_map;
    int end_game_human_count;  // used for deciding whether to keep the record file

    bool loadAuthorizations() throw ();
    void saveAuthorizations() const throw ();

    AuthorizationDatabase::AccessDescriptor getAccess(int pid) throw ();

    void doKickPlayer(int pid, int admin, int minutes) throw ();   // if minutes > 0, it's really a ban

    bool trySetMaxplayers(int val) throw (); // checks that no players are connected, if that fails, logs an error and returns false
    void setMaxPlayers(int num) throw () { maxplayers = num; world.setMaxPlayers(num); network.setMaxPlayers(num); }

    void start_recording() throw ();
    void stop_recording() throw ();
    void delete_recording() throw ();
    void record_init_data() throw ();

public:
    Server(LogSet& hostLogs, const ServerExternalSettings& config, Log& externalErrorLog, const std::string& errorPrefix) throw ();  // externalErrorLog must outlive the Server object
    virtual ~Server() throw ();

    bool start(int target_maxplayers) throw ();
    void loop(volatile bool *quitFlag, bool quitOnEsc) throw ();
    void stop() throw ();

    void ctf_game_restart() throw ();
    void simulate_and_broadcast_frame() throw ();
    void server_think_after_broadcast() throw ();
    bool game_running() const throw () { return !gameover; }

    int get_player_count() const throw () { return network.get_player_count(); }
    void mutePlayer(int pid, int mode, int admin) throw ();
    void kickPlayer(int pid, int admin) throw ();
    void banPlayer(int pid, int admin, int minutes) throw ();
    bool isBanned(int cid) const throw () { return authorizations.isBanned(network.get_client_address(cid)); }
    bool check_name_password(const std::string& name, const std::string& password) const throw ();
    void disconnectPlayer(int pid, Disconnect_reason reason) throw ();
    void sendMessage(int pid, Message_type type, const std::string& msg) throw ();

    void remove_bot() throw ();
    void set_check_bots() throw () { check_bots = true; }

    void logAdminAction(int admin, const std::string& action, int target = pid_none) throw ();
    void logChat(int pid, const std::string& message) throw ();

    void balance_teams() throw ();
    void shuffle_teams() throw ();
    void check_team_changes() throw ();
    void check_player_change_teams(int pid) throw ();
    void move_player(int f, int t) throw ();
    void swap_players(int a, int b) throw ();
    void game_remove_player(int pid, bool removeClient) throw ();
    void check_fav_colors(int pid) throw ();
    void set_fav_colors(int pid, const std::vector<char>& colors) throw ();

    void nameChange(int id, int pid, std::string name, const std::string& password) throw ();
    void chat(int pid, const std::string& sbuf) throw ();   //#fix: separate console handling

    const ClientData& getClientData(int cid) const throw () { return client[cid]; }
          ClientData& getClientData(int cid) throw ()       { return client[cid]; }
    bool changeRegistration(int id, const std::string& token) throw ();  // returns true if the token is different from before and non-empty
    void resetClient(int cid) throw () { client[cid].reset(); }
    void refresh_team_score_modifiers() throw ();
    void check_map_exit() throw ();
    bool specific_map_vote_required() const throw () { return settings.get_require_specific_map_vote(); } //#fix
    void score_frag(int p, int amount, bool forTournament = true) throw ();
    void score_neg(int p, int amount, bool forTournament = true) throw ();
    int getLessScoredTeam() const throw ();  // using team_smul ; call refresh_team_score_modifiers before calling this
    bool isLocallyAuthorized(int pid) const throw ();
    bool isAdmin(int pid) const throw ();

    bool load_rotation_map(int pos) throw ();
    bool server_next_map(int reason, const std::string& currmap_title_override = std::string()) throw ();
    const MapInfo& current_map() const throw () { return maprot[currmap]; }
    int current_map_nr() const throw () { return currmap; }
    const std::string& getCurrentMapFile() const throw () { return maprot[currmap].file; }
    const std::vector<MapInfo>& maplist() const throw () { return maprot; }
    std::vector<MapInfo>& maplist() throw () { return maprot; }

    const std::vector<std::string>& getWelcomeMessage() const throw () { return settings.get_welcome_message(); } //#fix?

    const std::string& server_website() const throw () { return settings.get_server_website_url(); } //#fix?

    bool tournament_active() const throw () { return settings.get_tournament(); }

    bool reset_settings(bool reload) throw ();   // set reload if reset_settings has already been called to preserve map and ensure fixed values aren't changed

    bool recording_active() const throw ();
    BinaryWriter& recordMessageWriter() const throw () { return record_messages; }
    const std::string& record_map_data() const throw () { return record_map; }
};

#endif
