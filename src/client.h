/*
 *  client.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006 - Jani Rivinoja
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

#include "client_menus.h"
#include "function_utility.h"
#include "gameserver_interface.h"
#include "graphics.h"
#include "log.h"
#include "menu.h"
#include "mutex.h"
#include "names.h"
#include "protocol.h"   // needed for possible definition of SEND_FRAMEOFFSET
#include "sounds.h"
#include "thread.h"
#include "world.h"

//server record
class ServerListEntry {
public:
    bool        refreshed;
    bool        noresponse;
    int         ping;
    std::string info;

    ServerListEntry() : refreshed(false), noresponse(true), ping(0) { }

    const NLaddress& address() const { return addr; }
    std::string addressString() const;
    bool setAddress(const std::string& address);    // returns false if address is invalid

private:
    NLaddress   addr;
};

class FileDownload {
public:
    std::string fileType, shortName, fullName;

    FileDownload(const std::string& type, const std::string& name, const std::string& filename);
    ~FileDownload();
    bool isActive() const { return (fp != 0); }
    int progress() const;
    bool start();
    bool save(const char* buf, unsigned len);
    void finish();

private:
    FILE* fp;
};

enum Menu_selection {   // screens that aren't quite menus //#fix: get rid
    menu_none,
    menu_maps,
    menu_players,
    menu_teams
};

enum ClientCfgSetting {
    CCS_PlayerName,
    CCS_Tournament,
    CCS_Favorites,
    CCS_FavoriteColors,
    CCS_LagPrediction,
    CCS_LagPredictionAmount,
    CCS_Joystick,
    CCS_MessageLogging,
    CCS_SaveStats,
    CCS_Windowed,
    CCS_GFXMode,
    CCS_Flipping,
    CCS_FPSLimit,
    CCS_GFXTheme,
    CCS_Antialiasing,
    CCS_StatsBgAlpha,
    CCS_ShowNames,
    CCS_SoundEnabled,
    CCS_Volume,
    CCS_SoundTheme,
    CCS_ShowStats,
    CCS_AutoGetServerList,
    CCS_ShowServerInfo,
    CCS_KeypadMoving,
    CCS_JoystickMove,
    CCS_JoystickShoot,
    CCS_JoystickRun,
    CCS_JoystickStrafe,
    CCS_ContinuousTextures,
    CCS_UnderlineMasterAuth,
    CCS_UnderlineServerAuth,
    CCS_ServerPublic,
    CCS_ServerPort,
    CCS_KeyboardLayout,
    CCS_ServerAddress,
    CCS_AutodetectAddress,
    CCS_ArrowKeysInStats,
    CCS_MinimapPlayers,
    CCS_AlternativeFlipping,
    CCS_StayDeadInMenus,
    CCS_MinTransp,
    CCS_UseThemeBackground,
    CCS_Background,
    CCS_Font,
    CCS_MaxCommand = CCS_Font
};

class ServerThreadOwner {
    LogSet log;
    bool threadFlag, quitFlag;  // threadFlag is true whenever the thread hasn't been joined, quitFlag false only when the server is successfully running
    int runPort;
    Thread serverThread;

    void threadFn(const ServerExternalSettings& config);

public:
    ServerThreadOwner(LogSet logs) : log(logs), threadFlag(false), quitFlag(true) { }
    ~ServerThreadOwner() { if (threadFlag) stop(); }
    bool running() { if (quitFlag && threadFlag) stop(); nAssert(quitFlag != threadFlag); return !quitFlag; }
    int port() const { return runPort; }
    void start(int port, const ServerExternalSettings& config);
    void stop();
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

    TournamentPasswordManager(LogSet logs, TokenCallbackT tokenCallbackFunction, int threadPriority);   // warning: the callback will be called from a background thread
    ~TournamentPasswordManager() { stop(); }

    void stop();
    void changeData(const std::string& newName, const std::string& newPass);
    PasswordStatus status() const { if (passStatus == PS_tokenReceived && servStatus != PS_noPassword) return servStatus; else return passStatus; }
    std::string statusAsString() const;
    std::string getToken() const { return token.read(); }

    void serverProcessingToken()    { servStatus = PS_tokenSent;        }
    void serverAcceptsToken()       { servStatus = PS_tokenAccepted;    }
    void serverRejectsToken()       { servStatus = PS_tokenRejected;    }
    void disconnectedFromServer()   { servStatus = PS_noPassword;       }   // actually, no server

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

    void start();
    void threadFn();
    void setToken(const std::string& newToken);
};

class ClientExternalSettings {
public:
    int winclient;      // windowed client? ; -1 = undefined, 0 = false, 1 = true (-win / -fs)
    int trypageflip;    // try page flipping? ; -1 = undefined, 0 = false, 1 = true (-flip / -dbuf)
    bool forceDefaultGfxMode;
    bool nosound;       // disable sound? -nosound
    int targetfps;      // target (MAX) frames-per-second ; -1 = undefined
    int lowerPriority, priority, networkPriority;   // lower is used for non-timecritical background threads
    int minLocalPort, maxLocalPort; // set to 0 0 to use any available port

    typedef void StatusOutputFnT(const std::string& str);
    StatusOutputFnT* statusOutput;

    ClientExternalSettings() : winclient(-1), trypageflip(-1), forceDefaultGfxMode(false), nosound(false), targetfps(-1), minLocalPort(0), maxLocalPort(0) { }
};

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
    virtual ~ThreadMessage() { }
    virtual void execute(Client* cl) const = 0;
};

class Client {
    friend class ClientPhysicsCallbacks;
    friend class TM_DoDisconnect;
    friend class TM_Text;
    friend class TM_Sound;
    friend class TM_MapChange;
    friend class TM_NameAuthorizationRequest;
    friend class TM_GunexploEffect;
    friend class TM_Deathbringer;
    friend class TM_ServerSettings;
    friend class TM_ConnectionUpdate;

    FileLog normalLog;
    MemoryLog& externalErrorLog;    // this is emptied to the error dialog as we go; only rare leftovers are left to caller
    DualLog errorLog;
    // currently not in use:    SupplementaryLog<FileLog> securityLog;
    mutable LogSet log; // this is the only access to logs in normal operation

    std::vector<std::vector<std::string> > load_all_player_passwords() const;
    std::string load_player_password(const std::string& name, const std::string& address) const;
    void save_player_password(const std::string& name, const std::string& address, const std::string& password) const;
    void remove_player_password(const std::string& name, const std::string& address) const;
    int remove_player_passwords(const std::string& name) const;

    ServerThreadOwner listenServer;

    // world (these variables all locked by frameMutex)
    ClientWorld fd, fx; //#fix: two maps, etc.
    std::vector<ClientPlayer*> players_sb;  // player pointers for scoreboard
    int me;
    MutexHolder frameMutex;
    int maxplayers;

    // network
    client_c *client;
    double lastpackettime;
    NLubyte clFrameSent, clFrameWorld;
    #ifdef SEND_FRAMEOFFSET
    double frameOffsetDeltaTotal;
    int frameOffsetDeltaNum;
    double netsendAdjustment;
    #endif
    double averageLag;
    double frameReceiveTime;    // when fx was received
    ClientControls controlHistory[256]; // the section between clFrameWorld and clFrameSent (circularly) is in use on a given moment
    NLulong svFrameHistory[256];    // the section between clFrameWorld and clFrameSent (circularly) is in use on a given moment
    volatile bool connected;
    bool map_ready;
    int clientReadiesWaiting;
    std::string old_map;
    std::string servermap;  //last map command from server

    std::deque<ThreadMessage*> messageQueue;    // access with frameMutex locked; delete the object when removing from the queue

    MutexHolder downloadMutex;
    std::list<FileDownload> downloads;

    TournamentPasswordManager tournamentPassword;

    NLulong fdp;
    NLulong max_world_rank;

    MutexHolder mapInfoMutex;
    std::vector<MapInfo> maps;
    std::vector<std::string> fav_maps;
    int current_map;
    int map_vote;
    bool want_change_teams;
    bool want_map_exit;
    bool want_map_exit_delayed;
    bool map_time_limit;
    int map_start_time; // in get_time() seconds -> can be negative
    int map_end_time;
    NLbyte remove_flags;

    // GUI
    Menu_main menu;
    Menu_text m_connectProgress;
    Menu_text m_dialog; // take care not to open multiple dialogs (same goes to other menus too); to allow that, a vector of Menu_text should be created
    Menu_text m_errors;
    Menu_playerPassword m_playerPassword;
    Menu_serverPassword m_serverPassword;
    Menu_text m_serverInfo;
    Menu_text m_notResponding; // not to be put to openMenus

    MenuStack openMenus;

    bool quitCommand;
    Menu_selection menusel; // a special screen rather than menu: maplist, stats
    bool gameshow;
    double FPS;
    int framecount, totalframecount;
    double frameCountStartTime;
    int gameover_plaque;
    int red_final_score, blue_final_score;
    std::string hostname;
    std::string edit_map_vote;
    int player_stats_page;
    double lastAltEnterTime;

    std::vector<ServerListEntry> gamespy;
    std::vector<ServerListEntry> mgamespy;  //gamespy of master server
    MutexHolder serverListMutex;

    std::string playername;
    NLaddress serverIP;

    volatile bool abortThreads;

    enum RefreshStatus { RS_none, RS_running, RS_failed, RS_contacting, RS_connecting, RS_receiving };
    volatile RefreshStatus refreshStatus;   // thread communication variable

    std::string password_file;

    std::string talkbuffer;
    std::list<Message> chatbuffer;
    bool show_all_messages;
    std::vector<std::string> highlight_text;

    bool stats_autoshowing;
    Graphics client_graphics;
    bool screenshot;
    volatile bool mapChanged, predrawNeeded;
    Sounds client_sounds;

    std::ofstream message_log;
    bool messageLogOpen;

    const ClientExternalSettings extConfig;
    ServerExternalSettings serverExtConfig;

    class GFXMode {
    public:
        int width, height, depth;
        bool windowed, flipping;

        GFXMode() : width(-1) { }
        GFXMode(int w, int h, int d, bool win, int flip) : width(w), height(h), depth(d), windowed(win), flipping(flip) { }
        bool used() const { return width != -1; }
    };

    GFXMode workingGfxMode;

    void initMenus();

    // menu callback functions
    void MCF_menuOpener(Menu& menu);
    void MCF_menuCloser();
    void MCF_connect(Textarea& target);
    void MCF_cancelConnect();
    void MCF_disconnect();
    void MCF_exitOutgun();
    void MCF_prepareMainMenu();
    void MCF_prepareNameMenu();
    void MCF_prepareDrawNameMenu();
    void MCF_nameMenuClose();
    void MCF_nameChange();  // only function to clear the password
    void MCF_randomName();
    void MCF_removePasswords();
    void MCF_prepareGameMenu();
    void MCF_prepareControlsMenu();
    void MCF_keyboardLayout();
    void MCF_joystick();
    void MCF_messageLogging();
    void MCF_screenDepthChange();
    void MCF_screenModeChange();
    void MCF_gfxThemeChange();
    void MCF_fontChange();
    void MCF_antialiasChange();
    void MCF_transpChange();
    void MCF_statsBgChange();
    void MCF_prepareScrModeMenu();
    void MCF_prepareDrawScrModeMenu();
    void MCF_prepareGfxMenu();
    void MCF_sndEnableChange();
    void MCF_sndVolumeChange();
    void MCF_sndThemeChange();
    void MCF_prepareSndMenu();
    void MCF_refreshLanguages();
    void MCF_acceptLanguage();
    void MCF_acceptBugReporting();
    void MCF_prepareServerMenu();
    void MCF_updateServers();
    void MCF_refreshServers();
    void MCF_prepareAddServer();
    void MCF_addServer();
    bool MCF_addressEntryKeyHandler(char scan, unsigned char chr);
    bool MCF_addRemoveServer(Textarea& target, char scan, unsigned char chr);
    void MCF_playerPasswordAccept();
    void MCF_serverPasswordAccept();
    void MCF_clearErrors();
    void MCF_prepareOwnServerMenu();
    void MCF_startServer();
    void MCF_playServer();
    void MCF_stopServer();

    void load_highlight_texts();
    void load_fav_maps();
    void apply_fav_maps();

    void loadHelp();
    void loadSplashScreen();
    void openMessageLog();
    void closeMessageLog();
    void CB_tournamentToken(std::string token); // callback called by tournamentPassword from another thread

    bool screenModeChange();    // the return value should be tested at the first call

    void addThreadMessage(ThreadMessage* msg) { messageQueue.push_back(msg); }
    void setMaxPlayers(int num) { maxplayers = num; fx.setMaxPlayers(num); fd.setMaxPlayers(num); }

    // world    //#fix: should these be moved to ClientWorld?
    void rocketHitWallCallback(int rid, bool power, double x, double y, int roomx, int roomy);
    void rocketOutOfBoundsCallback(int rid);
    void playerHitWallCallback(int pid);
    void playerHitPlayerCallback(int pid1, int pid2);
    bool shouldApplyPhysicsToPlayerCallback(int pid);

    void remove_useless_flags();

    // network
    void connect_command(bool loadPassword);    // call with frameMutex locked
    void disconnect_command();  // do not call from a network thread
    void connection_update(int connect_result, const char* data, int length);
    void client_connected(const char* data, int length);    // call with frameMutex locked
    void client_disconnected(const char* data, int length);
    void connect_failed_denied(const char* data, int length);
    void connect_failed_unreachable();
    void connect_failed_socket();
    void send_player_token();
    void send_tournament_participation();
    void issue_change_name_command();
    void change_name_command();
    void send_client_ready();
    void send_chat(const std::string& msg);
    void send_frame(bool newFrame, bool forceSend);
    void process_incoming_data(const char* data, int length);

    std::string refreshStatusAsString() const;
    void getServerListThread();
    void refreshThread();
    bool refresh_all_servers();
    bool refresh_servers(std::vector<ServerListEntry>& gamespy);
    bool getServerList();
    bool parseServerList(std::istream& response);

    void check_download();  // call with downloadMutex locked
    void process_udp_download_chunk(const char* buf, int len, bool last);
    void download_server_file(const std::string& type, const std::string& name);
    void server_map_command(const std::string& mapname, NLushort server_crc);
    bool load_map(const std::string& directory, const std::string& mapname, NLushort server_crc);

    void handlePendingThreadMessages(); // should only be called by the main thread; call with frameMutex locked

    // client callbacks
    static void cfunc_connection_update(void* customp, int connect_result, const char* data, int length);
    static void cfunc_server_data(void* customp, const char* data, int length);

    // GUI
    void erase_first_message();
    void print_message(Message_type type, const std::string& msg);

    void save_screenshot();
    void toggle_help();
    template<class MenuT> void showMenu(MenuT& menu) { openMenus.open(&menu.menu); }
    void predraw();
    void draw_game_frame();
    int calculatePlayerAlpha(int pid) const;
    void draw_player(int pid);
    void draw_game_menu();

    ClientControls readControls(bool canUseKeypad, bool useCursorKeys);

    void handleKeypress(int sc, int ch, bool withControl, bool alt_sequence);   // sc = scancode, ch = character, as returned by readkey
    bool handleInfoScreenKeypress(int sc, int ch, bool withControl, bool alt_sequence);  // sc = scancode, ch = character, as returned by readkey
    void handleGameKeypress(int sc, int ch, bool withControl, bool alt_sequence);   // sc = scancode, ch = character, as returned by readkey

public:
    Client(LogSet hostLogs, const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, MemoryLog& externalErrorLog_);
    ~Client();

    bool start();
    void loop(volatile bool* quitFlag, bool firstTimeSplash);
    void stop();
};

class Message {
public:
    Message(Message_type type_, const std::string& txt, int time_):
        msg_type(type_), msg_text(txt), msg_time(time_), is_highlighted(false) { }

    void highlight() { is_highlighted = true; }

    Message_type type() const { return msg_type; }
    const std::string& text() const { return msg_text; }
    int time() const { return msg_time; }
    bool highlighted() const { return is_highlighted; }

private:
    Message_type msg_type;
    std::string msg_text;
    int msg_time;
    bool is_highlighted;
};

#endif
