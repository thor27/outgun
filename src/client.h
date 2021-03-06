/*
 *  client.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Jani Rivinoja
 *  Copyright (C) 2006 - Peter Kosyh
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

#ifndef CLIENT_H_INC
#define CLIENT_H_INC

#ifndef DEDICATED_SERVER_ONLY
#include <map>
#include <set>
#include <sstream>

#include "client_menus.h"
#include "graphics.h"
#include "menu.h"
#include "sounds.h"
#endif

#include "client_interface.h"
#include "function_utility.h"
#include "gameserver_interface.h"
#include "log.h"
#include "mutex.h"
#include "thread.h"
#include "world.h"

class BinaryReader;

#ifndef DEDICATED_SERVER_ONLY
//server record
class ServerListEntry {
public:
    bool refreshed;
    bool noresponse;
    int ping;
    std::string info;

    ServerListEntry() throw () : refreshed(false), noresponse(true), ping(0) { }

    const Network::Address& address() const throw () { return addr; }
    std::string addressString() const throw ();
    bool setAddress(const std::string& address) throw ();    // returns false if address is invalid
    void setAddress(const Network::Address& address) throw ();

private:
    Network::Address addr;
};

class FileDownload {
public:
    std::string fileType, shortName, fullName;

    FileDownload(const std::string& type, const std::string& name, const std::string& filename) throw ();
    ~FileDownload() throw ();
    bool isActive() const throw () { return (fp != 0); }
    int progress() const throw ();
    bool start() throw ();
    bool save(ConstDataBlockRef data) throw ();
    void finish() throw ();

private:
    FILE* fp;
};

enum Menu_selection {   // screens that aren't quite menus //#fix: get rid
    menu_none,
    menu_maps,
    menu_players,
    menu_teams
};

class ServerThreadOwner {
    LogSet log;
    bool threadFlag, quitFlag;  // threadFlag is true whenever the thread hasn't been joined, quitFlag false only when the server is successfully running
    int runPort;
    Thread serverThread;

    void threadFn(const ServerExternalSettings& config) throw ();

public:
    ServerThreadOwner(LogSet logs) throw () : log(logs), threadFlag(false), quitFlag(true) { }
    ~ServerThreadOwner() throw () { if (threadFlag) stop(); }
    bool running() throw () { if (quitFlag && threadFlag) stop(); nAssert(quitFlag != threadFlag); return !quitFlag; }
    int port() const throw () { return runPort; }
    void start(int port, const ServerExternalSettings& config) throw ();
    void stop() throw ();
};

class TournamentPasswordManager {
public:
    typedef HookFunctionHolder1<void, std::string> TokenCallbackT;  // an empty string is given to indicate no token

    enum PasswordStatus {
        PS_noPassword,
        PS_starting,
        PS_socketError,
        PS_sending,
        PS_sendError,
        PS_receiving,
        PS_recvError,
        PS_invalidResponse,
        PS_unavailable,
        PS_tokenReceived,
        PS_badLogin,
        // these follow from server status and passStatus is never one of these:
        PS_tokenSent,
        PS_tokenAccepted,
        PS_tokenRejected
    };

    TournamentPasswordManager(LogSet logs, TokenCallbackT tokenCallbackFunction, int threadPriority) throw ();   // warning: the callback will be called from a background thread
    ~TournamentPasswordManager() throw () { stop(); }

    void stop() throw ();
    void changeData(const std::string& newName, const std::string& newPass) throw ();
    PasswordStatus status() const throw () { if (passStatus == PS_tokenReceived && servStatus != PS_noPassword) return servStatus; else return passStatus; }
    std::string statusAsString() const throw ();
    std::string getToken() const throw () { return token.read(); }

    void serverProcessingToken()  throw () { servStatus = PS_tokenSent;        }
    void serverAcceptsToken()     throw () { servStatus = PS_tokenAccepted;    }
    void serverRejectsToken()     throw () { servStatus = PS_tokenRejected;    }
    void disconnectedFromServer() throw () { servStatus = PS_noPassword;       }   // actually, no server

private:
    LogSet log;
    TokenCallbackT tokenCallback;

    volatile bool quitThread;   // set to quit the thread
    volatile PasswordStatus passStatus; // set by both the thread and the regular interface to indicate the status
    Thread thread;
    int priority;

    PasswordStatus servStatus;

    std::string name, password; // constant while the thread is running
    Threadsafe<std::string> token;

    void start() throw ();
    void threadFn() throw ();
    void setToken(const std::string& newToken) throw ();
};
#endif

class Client;
class client_c; // of leetnet
class client_runes_t;

/* ThreadMessage represents a class of actions that can't be performed by callbacks within a network thread in a thread-safe way.
 * These are queued for the main thread to execute.
 * Note: they will be executed in order relative to each other but out of order relative to messages processed in the network thread.
 * Handling anything that isn't protected by mutexes (frameMutex and downloadMutex) should be applied only through the ThreadMessage queue.
 */
class ThreadMessage {   // the subclasses (named starting with TM_) are direct property of the Client class and internally declared in client.cpp
public:
    virtual ~ThreadMessage() throw () { }
    virtual void execute(Client* cl) const throw () = 0;
};

class Client : public ClientInterface {
    friend class ClientPhysicsCallbacks;
    friend class TM_DoDisconnect;
    friend class TM_Text;
    friend class TM_Sound;
    friend class TM_MapChange;
    #ifndef DEDICATED_SERVER_ONLY
    friend class TM_NameAuthorizationRequest;
    friend class TM_GunexploEffect;
    friend class TM_ServerSettings;
    #endif
    friend class TM_ConnectionUpdate;

    static const int disappearedFlagAlpha = 150;

    MemoryLog& externalErrorLog;    // this is emptied to the error dialog as we go; only rare leftovers are left to caller
    DualLog errorLog;
    // currently not in use:    SupplementaryLog<FileLog> securityLog;
    mutable LogSet log; // this is the only access to logs in normal operation

    #ifndef DEDICATED_SERVER_ONLY
    std::vector<std::vector<std::string> > load_all_player_passwords() const throw ();
    std::string load_player_password(const std::string& name, const std::string& address) const throw ();
    void save_player_password(const std::string& name, const std::string& address, const std::string& password) const throw ();
    void remove_player_password(const std::string& name, const std::string& address) const throw ();
    int remove_player_passwords(const std::string& name) const throw ();

    ServerThreadOwner listenServer;

    class SettingManager : public SettingCollector {
    public:
        typedef std::map<ClientCfgSetting, SaverLoader*> MapT;

        ~SettingManager() throw () { for (MapT::iterator i = settings.begin(); i != settings.end(); ++i) delete i->second; }

        void add(ClientCfgSetting key, SaverLoader* sl) throw () { settings[key] = sl; }

        SettingCollector::SaverLoader* find(ClientCfgSetting key) const throw () { MapT::const_iterator i = settings.find(key); return i == settings.end() ? 0 : i->second; }
        const MapT& read() const throw () { return settings; }

    private:
        MapT settings;
    };
    SettingManager settings;
    #endif

    // world (these variables all locked by frameMutex)
    ClientWorld fx; //#fix fx and fd: two maps, etc.
    #ifndef DEDICATED_SERVER_ONLY
    ClientWorld fd;
    bool mapWrapsX, mapWrapsY;
    std::vector<ClientPlayer*> players_sb;  // player pointers for scoreboard
    #endif
    int me;
    Mutex frameMutex;
    int maxplayers;

    // network
    client_c *client;
    double lastpackettime;
    uint8_t clFrameSent, clFrameWorld;
    double botReactedFrame;
    double frameOffsetDeltaTotal;
    int frameOffsetDeltaNum;
    double netsendAdjustment;
    double averageLag;
    double frameReceiveTime;    // when fx was received
    ClientControls controlHistory[256]; // the section between clFrameWorld and clFrameSent (circularly) is in use on a given moment
    ClientControls sentControls;
    double svFrameHistory[256];    // the section between clFrameWorld and clFrameSent (circularly) is in use on a given moment

    GunDirection gunDir; // used only with allowFreeTurning
    double gunDirRefreshedTime;
    double next_respawn_time;
    int flag_return_delay;
    volatile bool connected;
    bool map_ready;
    int clientReadiesWaiting;
    std::string servermap;  //last map command from server

    int protocolExtensions; // -1 means unextended protocol, 0 up are extension version numbers (<= PROTOCOL_EXTENSIONS_VERSION)

    std::deque<ThreadMessage*> messageQueue;    // access with frameMutex locked; delete the object when removing from the queue

    Mutex downloadMutex;
    #ifndef DEDICATED_SERVER_ONLY
    std::list<FileDownload> downloads;

    TournamentPasswordManager tournamentPassword;

    uint32_t fdp;
    uint32_t max_world_rank;
    #endif

    #ifndef DEDICATED_SERVER_ONLY
    Mutex mapInfoMutex;
    std::vector<MapInfo> maps;
    std::vector< std::pair<const MapInfo*, int> > sortedMaps;

    MapListSortKey mapListSortKey;
    bool mapListChangedAfterSort;

    std::set<std::string> fav_maps;
    int current_map;
    int map_vote;
    bool want_change_teams;
    bool want_map_exit;
    bool want_map_exit_delayed;
    bool map_time_limit;
    int map_start_time; // in get_time() seconds -> can be negative
    int map_end_time;
    bool extra_time_running;
    #endif
    int8_t remove_flags;
    bool lock_team_flags_in_effect;
    bool lock_wild_flags_in_effect;
    bool capture_on_team_flags_in_effect;
    bool capture_on_wild_flags_in_effect;

    #ifndef DEDICATED_SERVER_ONLY
    // GUI
    Menu_main menu;
    Menu_text m_connectProgress;
    Menu_text m_dialog; // take care not to open multiple dialogs (same goes to other menus too); to allow that, a vector of Menu_text should be created
    Menu_text m_errors;
    Menu_playerPassword m_playerPassword;
    Menu_serverPassword m_serverPassword;
    Menu_text m_serverInfo;
    Menu_text m_notResponding; // not to be put to openMenus

    Menu_language m_initialLanguage; // only for setting the language at startup

    MenuStack openMenus;

    bool quitCommand;
    Menu_selection menusel; // a special screen rather than menu: maplist, stats
    #endif

    bool gameshow;
    int gameover_plaque;

    #ifndef DEDICATED_SERVER_ONLY
    int red_final_score, blue_final_score;
    std::string hostname;
    std::string edit_map_vote;
    int player_stats_page;
    double lastAltEnterTime;
    bool deadAfterHighlighted;

    double FPS;
    int framecount, totalframecount;
    double frameCountStartTime;

    std::vector<ServerListEntry> gamespy;
    std::vector<ServerListEntry> mgamespy;  //gamespy of master server
    Mutex serverListMutex;

    RegisterMouseClicks mouseClicked;
    #endif

    std::string playername;
    Network::Address serverIP;

    // for bots:
    std::string bot_password;

    enum Routing {
        Route_None,
        Route_Flag,
        Route_Base,
        Route_Team,
        Route_Fog
    };

    #ifndef DEDICATED_SERVER_ONLY
    bool botmode;
    #else
    static const bool botmode = true;
    #endif
    int botId;
    bool finished;

    struct RoomCoords {
        int x, y;
        RoomCoords() throw () { }
        RoomCoords(int x_, int y_) throw () : x(x_), y(y_) { }
        bool operator==(const RoomCoords& o) const throw () { return x == o.x && y == o.y; }
        bool operator!=(const RoomCoords& o) const throw () { return x != o.x || y != o.y; }
    };

    Routing     routing[Table_Max];
    int         route_x[Table_Max];
    int         route_y[Table_Max];
    RoomCoords  routeTableCenter[Table_Max];
    bool        botPrevFire;
    int         last_seen;
    int         myGundir;

    bool        IsDefender() throw (); // am i defender? (role)
    bool        IsCarriersDef(int team) throw (); // are flags of team that we carry safe?
    bool        IsFlagsAtBases(int team) const throw (); // are flags of team at bases?
    int         GetPlayers(int team) const throw (); // get num of players
    int         Teams(int roomx, int roomy, int &en, int &fr) const throw (); // get num of en and fr for sector
    bool        IsHome(int roomx, int roomy) const throw (); //is it base

    bool        AmILast() const throw ();
    bool        IsMission(RouteTable num) const throw (); // have i mission? (No agression mode)
    int         GetEasyEnemy(double mex, double mey) const throw (); // get easy enemy to kill
    bool        IsMassive() const throw (); // am i berserker? (No rocket avoiding)
    int         HaveFlag(int n) const throw (); // 0 if n isn't carrying a flag, 1 if n carries an enemy flag, 2 if n carries a wild flag
    bool        IsFlagAtBase(const Flag& f, int team) const throw ();
    int         IsAimed(double mex, double mey, int i) const throw (); // return 2 if in hit point, 1 if almost in the gun direction and not behind a wall, 0 if elsewhere
    std::pair<bool, GunDirection> TryAim(double mex, double mey, int target) const throw (); // for free turning; returns (shoot?, direction)
    double      GetHitTime(double mex, double mey, const GunDirection& dir, int iTarget) const throw (); // approximate time until a rocket shoot towards dir from (mex,mey) would hit player iTarget assuming no walls ("big" if no hit)
    double      GetHitTeammateTime(double mex, double mey, const GunDirection& dir) const throw (); // approximate time until a rocket shoot towards dir from (mex,mey) would hit first teammate assuming no walls ("big" if no hit, including if friendly fire is off)

    bool        IsBehindWall(double mex, double mey, double dx, double dy) const throw ();
    double      ScanDir(double mex, double mey, GunDirection dir) const throw (); // return length to wall
    std::pair<bool, GunDirection> NeedShoot(double mex, double mey, const GunDirection& defaultDir) throw (); // shoot or not to shoot? if free turning is set, also tells the gunDir required (same as old gunDir if there's no one to aim at)
    GunDirection GetDir(double dx, double dy) const throw (); // 0 - 0, 2 - Pi/2, 3 - Pi...
    int         GetDangerousRocket() const throw (); // get danger rocket index
    int         GetDangerousEnemy(double mex, double mey) const throw (); // same for enemy
    int         GetNearestEnemy(double mex, double mey) const throw (); // get nearest enemy
    int         FreeDir(double mex, double mey) const throw (); // maximum free space at front

    ClientControls Aim(double mex, double mey, int i) const throw ();
    ClientControls EscapeRocket(double mex, double mey, int mrock) const throw ();
    ClientControls GetFlag(double mex, double mey) const throw ();
    ClientControls FollowFlag(double mex, double mey) const throw ();
    ClientControls GetPowerup(double mex, double mey) const throw ();
    ClientControls MoveDirNoAggregate(int dir) const throw ();
    ClientControls MoveTo(double mex, double mey, double dx, double dy) const throw ();
    ClientControls MoveToDoor(double mex, double mey, int d) const throw ();
    ClientControls MoveToNoAggregate(double mex, double mey, double dx, double dy) const throw ();
    ClientControls MoveDir(int dir) const throw ();
    ClientControls Escape(double mex, double mey) const throw ();
    ClientControls FreeWalk(double mex, double mey) const throw ();
    ClientControls Route(double mex, double mey, RouteTable num) const throw (); // follow route

    bool scan_door(Room& room, int x, int y, int dx, int dy, int len) const throw ();
    void BuildMap() throw ();
    void BuildRouteTable(int roomx, int roomy, RouteTable num) throw (); // build route table (labeled) from me point
    void BuildRouteTable(const std::vector<RoomCoords>& startPoints, RouteTable num) throw (); // build route table (labeled) from multiple points
    int  BuildRoute(int tox, int toy, RouteTable num) throw (); // build route on route table tox(y), return 0 if not needed, -1 if no path
    bool RouteLogic(RouteTable num) throw (); // build route on route table using AI, -1 if not builded

    void next_room(int& x, int& y, int i) const throw (); // chose ith door
    int  route_room(int &x, int &y, RouteTable num) throw (); // go one step to lower label and label it as route , return 1 if step is done
    // Build Route to nearest enemy flag, enemy flag carry, me flag, .... enemy, friend
    // -1 if no target labeled
    int TargetNearestBase(int& m_label, int& x, int& y, int team, RouteTable num) throw ();
    int TargetNearestTeam(int& m_label, int& x, int& y, int team, RouteTable num) throw ();
    int TargetNearestFlag(int& m_label, int& x, int& y, int team, int state, RouteTable num) throw ();
    int TargetFog(RouteTable num) throw ();

    int TargetRoute(int efb, int efd, int efc,
                    int mfb, int mfd, int mfc,
                    int wfb, int wfd, int wfce, int wfcf,
                    int en,  int fr,
                    int eb,  int fb, int wb,
                    RouteTable num) throw ();

    ClientControls getRobotControls() throw ();

    ClientControls Robot() throw ();

    volatile bool abortThreads;

    #ifndef DEDICATED_SERVER_ONLY
    enum RefreshStatus { RS_none, RS_running, RS_failed, RS_contacting, RS_connecting, RS_receiving };
    volatile RefreshStatus refreshStatus;   // thread communication variable

    std::string password_file;

    std::string talkbuffer;
    int talkbuffer_cursor;
    std::list<Message> chatbuffer;
    bool show_all_messages;
    std::vector<std::string> highlight_text;

    bool stats_autoshowing;
    Graphics graphics;
    bool screenshot;
    bool replaying;
    std::ifstream replay;
    double replay_rate, replayTime, replaySubFrame;
    bool replay_paused;
    bool replay_stopped;
    bool replay_first_frame_loaded;
    unsigned replay_start_frame;
    unsigned replay_length;
    std::pair<int, int> replayTopLeftRoom;
    double visible_rooms;

    bool spectating;
    Network::TCPSocket spectate_socket;
    bool spectate_data_received;
    std::stringstream spectate_buffer;
    #else
    static const bool replaying = false; // To avoid lots of ifdefs.
    #endif
    volatile bool mapChanged;
    #ifndef DEDICATED_SERVER_ONLY
    Sounds client_sounds;

    std::pair<int, int> predrawnRoom;
    int predrawnVisibleRooms;
    bool predrawnWithScroll;

    std::ofstream message_log;
    bool messageLogOpen;
    #endif

    const ClientExternalSettings extConfig;
    #ifndef DEDICATED_SERVER_ONLY
    ServerExternalSettings serverExtConfig;

    class GFXMode {
    public:
        int width, height, depth;
        bool windowed, flipping;

        GFXMode() throw () : width(-1) { }
        GFXMode(int w, int h, int d, bool win, int flip) throw () : width(w), height(h), depth(d), windowed(win), flipping(flip) { }
        bool used() const throw () { return width != -1; }
    };

    GFXMode workingGfxMode;

    void initMenus() throw ();

    // menu callback functions
    void MCF_menuOpener(Menu& menu) throw ();
    void MCF_menuCloser() throw ();
    void MCF_connect(Textarea& target) throw ();
    void MCF_cancelConnect() throw ();
    void MCF_disconnect() throw ();
    void MCF_exitOutgun() throw ();
    void MCF_replay(Textarea& target) throw ();
    void MCF_prepareReplayMenu() throw ();
    void MCF_prepareMainMenu() throw ();
    void MCF_preparePlayerMenu() throw ();
    void MCF_prepareDrawPlayerMenu() throw ();
    void MCF_playerMenuClose() throw ();
    void MCF_nameChange() throw ();  // only function to clear the password
    void MCF_randomName() throw ();
    void MCF_removePasswords() throw ();
    void MCF_prepareGameMenu() throw ();
    void MCF_prepareControlsMenu() throw ();
    void MCF_keyboardLayout() throw ();
    void MCF_joystick() throw ();
    void MCF_messageLogging() throw ();
    void MCF_screenDepthChange() throw ();
    void MCF_screenModeChange() throw ();
    void MCF_gfxThemeChange() throw ();
    void MCF_fontChange() throw ();
    void MCF_visibleRoomsPlayChange() throw ();
    void MCF_visibleRoomsReplayChange() throw ();
    void MCF_antialiasChange() throw ();
    void MCF_transpChange() throw ();
    void MCF_statsBgChange() throw ();
    void MCF_prepareScrModeMenu() throw ();
    void MCF_prepareDrawScrModeMenu() throw ();
    void MCF_prepareGfxThemeMenu() throw ();
    void MCF_sndEnableChange() throw ();
    void MCF_sndVolumeChange() throw ();
    void MCF_sndThemeChange() throw ();
    void MCF_prepareSndMenu() throw ();
    void MCF_refreshLanguages() throw ();
    void MCF_acceptLanguage(Textarea& target) throw ();
    void MCF_acceptInitialLanguage(Textarea& target) throw ();
    void MCF_acceptBugReporting() throw ();
    void MCF_prepareServerMenu() throw ();
    void MCF_updateServers() throw ();
    void MCF_refreshServers() throw ();
    void MCF_prepareAddServer() throw ();
    void MCF_addServer() throw ();
    bool MCF_addressEntryKeyHandler(char scan, unsigned char chr) throw ();
    bool MCF_addRemoveServer(Textarea& target, char scan, unsigned char chr) throw ();
    void MCF_playerPasswordAccept() throw ();
    void MCF_serverPasswordAccept() throw ();
    void MCF_clearErrors() throw ();
    void MCF_prepareOwnServerMenu() throw ();
    void MCF_startServer() throw ();
    void MCF_playServer() throw ();
    void MCF_stopServer() throw ();

    int refreshLanguages(Menu_language& lang_menu) throw ();
    void acceptLanguage(const std::string& lang, bool restart_message) throw ();

    void load_highlight_texts() throw ();
    void load_fav_maps() throw ();
    void apply_fav_maps() throw ();

    void loadHelp() throw ();
    void addSplashLine(std::string line) throw (); // internal to loadSplashScreen
    void loadSplashScreen() throw ();
    void openMessageLog() throw ();
    void closeMessageLog() throw ();
    void CB_tournamentToken(std::string token) throw (); // callback called by tournamentPassword from another thread

    bool screenModeChange() throw ();    // the return value should be tested at the first call
    #endif

    void addThreadMessage(ThreadMessage* msg) throw () { messageQueue.push_back(msg); }
    void setMaxPlayers(int num) throw () {
        maxplayers = num;
        fx.setMaxPlayers(num);
        #ifndef DEDICATED_SERVER_ONLY
        fd.setMaxPlayers(num);
        #endif
    }

    // world    //#fix: should these be moved to ClientWorld?
    void rocketHitWallCallback(int rid, bool power, double x, double y, int roomx, int roomy) throw ();
    void rocketOutOfBoundsCallback(int rid) throw ();
    void playerHitWallCallback(int pid) throw ();
    void playerHitPlayerCallback(int pid1, int pid2) throw ();
    bool shouldApplyPhysicsToPlayerCallback(int pid) throw ();

    void remove_useless_flags() throw ();

    // network
    void connect_command(bool loadPassword) throw ();    // call with frameMutex locked
    void disconnect_command() throw ();  // do not call from a network thread
    void connection_update(int connect_result, ConstDataBlockRef data) throw ();
    void client_connected(ConstDataBlockRef data) throw ();    // call with frameMutex locked
    void client_disconnected(ConstDataBlockRef data) throw ();
    void connect_failed_denied(ConstDataBlockRef data) throw ();
    void connect_failed_unreachable() throw ();
    void connect_failed_socket() throw ();
    void sendMinimapBandwidthAny(int players) throw ();
    #ifndef DEDICATED_SERVER_ONLY
    void send_player_token() throw ();
    void send_tournament_participation() throw ();
    void sendFavoriteColors() throw ();
    void sendMinimapBandwidth() throw ();
    #endif
    void issue_change_name_command() throw ();
    #ifndef DEDICATED_SERVER_ONLY
    void change_name_command() throw ();
    #endif
    void send_client_ready() throw ();
    #ifndef DEDICATED_SERVER_ONLY
    void send_chat(const std::string& msg) throw ();
    void send_frame(bool newFrame, bool forceSend) throw ();
    #endif
    void bot_send_frame(ClientControls controls) throw ();
    void readMinimapPlayerPosition(BinaryReader& reader, int pid) throw ();
    bool process_live_frame_data(ConstDataBlockRef data) throw (); // returns false if an error occured that requires disconnecting
    #ifndef DEDICATED_SERVER_ONLY
    int process_replay_frame_data(ConstDataBlockRef data) throw (); // returns number of bytes read - not necessarily all of data
    #endif
    bool process_message(ConstDataBlockRef data) throw (); // if returns false, discard the server/replay
    void process_incoming_data(ConstDataBlockRef data) throw ();

    #ifndef DEDICATED_SERVER_ONLY
    std::string refreshStatusAsString() const throw ();
    void getServerListThread() throw ();
    void refreshThread() throw ();
    bool refresh_all_servers() throw ();
    bool getServerList() throw ();
    bool get_local_servers() throw ();
    bool parseServerList(std::istream& response) throw ();

    void check_download() throw ();  // call with downloadMutex locked
    void process_udp_download_chunk(ConstDataBlockRef, bool last) throw ();
    void download_server_file(const std::string& type, const std::string& name) throw ();
    #endif
    void server_map_command(const std::string& mapname, uint16_t server_crc) throw ();
    bool load_map(const std::string& directory, const std::string& mapname, uint16_t server_crc) throw ();

    void handlePendingThreadMessages() throw (); // should only be called by the main thread; call with frameMutex locked

    // client callbacks
    static void cfunc_connection_update(void* customp, int connect_result, ConstDataBlockRef data) throw ();
    static void cfunc_server_data(void* customp, ConstDataBlockRef data) throw ();

    #ifndef DEDICATED_SERVER_ONLY
    WorldCoords playerPos(int pid) const throw ();
    WorldCoords viewTopLeft() const throw ();
    std::pair<int, int> topLeftRoom() const throw ();

    // GUI
    void erase_first_message() throw ();
    void print_message(Message_type type, const std::string& msg, int sender_team = -1) throw ();

    void save_screenshot() throw ();
    void toggle_help() throw ();
    template<class MenuT> void showMenu(MenuT& menu) throw () { openMenus.open(&menu.menu); }
    void predraw() throw ();

    bool on_screen(int x, int y) const throw (); // returns true if any part of room (x,y) is on screen
    bool on_screen(int rx, int ry, double lx, double ly, double fudge = 0) const throw (); // coordinates within "fudge" local units from screen border are also considered on screen
    bool on_screen_exact(int x, int y) const throw ();
    bool on_screen_exact(int rx, int ry, double lx, double ly, double fudge = 0) const throw ();
    bool player_on_screen(int pid) const throw ();
    bool player_on_screen_exact(int pid) const throw ();

    typedef Graphics::VisibilityMap VisibilityMap;

    void draw_game_frame() throw ();
    void draw_map(const VisibilityMap& roomVis) throw ();
    void draw_playfield() throw ();
    VisibilityMap calculateVisibilities() throw (); // calculates how well each player's position is known (applies to out-of-screen players only) and returns a map of how well each room is known (according to the most visible player there)
    int calculatePlayerAlpha(int pid) const throw ();
    void draw_player(int pid, double time, bool live) throw ();
    void draw_game_menu() throw ();

    ClientControls readControls(bool canUseKeypad, bool useCursorKeys) const throw ();
    ClientControls readControlsInGame() const throw ();
    bool firePressed() throw ();
    void refreshGunDir() throw ();

    void handleKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw ();   // sc = scancode, ch = character, as returned by readkey
    bool handleInfoScreenKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw ();  // sc = scancode, ch = character, as returned by readkey
    void handleGameKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw ();   // sc = scancode, ch = character, as returned by readkey

    void play_sound(int sample) throw ();

    void start_replay(const std::string& filename) throw ();
    bool start_replay(std::istream& in) throw ();
    void continue_replay() throw ();
    void continue_replay(std::istream& in) throw ();
    void stop_replay() throw ();
    void start_spectating(const Network::Address& address) throw ();
    void continue_spectating() throw ();
    #endif

    class ConstDisappearedFlagIterator : public ConstFlagIterator {
        const Client& c;

        void findValid() throw ();

    public:
        ConstDisappearedFlagIterator(const Client* host) throw () : ConstFlagIterator(host->fx), c(*host) { findValid(); }
        ConstDisappearedFlagIterator& operator++() throw () { next(); findValid(); return *this; }
    };

public:
    Client(const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, Log& clientLog, MemoryLog& externalErrorLog_) throw ();
    ~Client() throw ();

    bool start() throw ();
    #ifndef DEDICATED_SERVER_ONLY
    void loop(volatile bool* quitFlag, bool firstTimeSplash) throw ();
    void language_selection_start(volatile bool* quitFlag) throw ();
    #endif
    void stop() throw ();

    void bot_start(const Network::Address& addr, int ping, const std::string& name_lang, int botId) throw ();
    void bot_loop() throw ();
    void set_ping(int ping) throw ();
    bool is_connected() const throw () { return connected; }
    bool bot_finished() const throw () { return finished; }

    void set_bot_password(const std::string& pass) throw () { bot_password = pass; }

    int team() const throw () { return me / TSIZE; }
};

#endif
