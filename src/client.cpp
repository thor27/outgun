/*
 *  client.cpp
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

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cctype>
#include <cmath>

#include "incalleg.h"
#include "leetnet/client.h"
#include "commont.h"
#include "debug.h"
#include "debugconfig.h" // for LOG_MESSAGE_TRAFFIC
#include "gameserver_interface.h"
#include "language.h"
#include "names.h"
#include "nassert.h"
#include "network.h"
#include "platform.h"
#include "protocol.h"   // needed for possible definition of SEND_FRAMEOFFSET, and otherwise
#include "timer.h"
#include "utility.h"
#include "world.h"

#include "client.h"

/*
class MutexDebug {
    std::string mutexName;
    LogSet& log;

public:
    MutexDebug(const std::string& name, int line, LogSet& log_) : mutexName(name), log(log_) {
        lock(mutexName, line, log);
    }
    ~MutexDebug() {
        unlock(mutexName, 0, log);
    }
    static void lock(const std::string& name, int line, LogSet& log) {
        log("+++ %s @ %d thr %d", name.c_str(), line, pthread_self());
    }
    static void unlock(const std::string& name, int, LogSet& log) {
        log("--- %s thr %d", name.c_str(), pthread_self());
    }
};
*/
class MutexDebug {
public:
    MutexDebug(const std::string&, int, LogSet&) { }
    ~MutexDebug() { }
    static void lock(const std::string&, int, LogSet&) { }
    static void unlock(const std::string&, int, LogSet&) { }
};

using std::deque;
using std::endl;
using std::find;
using std::ifstream;
using std::ios;
using std::istream;
using std::istringstream;
using std::left;
using std::list;
using std::max;
using std::min;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::right;
using std::setfill;
using std::setw;
using std::sort;
using std::stable_sort;
using std::string;
using std::vector;

//#define ROOM_CHANGE_BENCHMARK

const int PASSBUFFER = 32;  //size of password file

#ifdef ROOM_CHANGE_BENCHMARK
int benchmarkRuns = 0;
#endif

class ClientPhysicsCallbacks : public PhysicsCallbacksBase {
    Client& c;

public:
    ClientPhysicsCallbacks(Client& c_) : c(c_) { }

    bool collideToRockets() const { return false; }
    bool gatherMovementDistance() const { return false; }
    bool allowRoomChange() const { return false; }
    void addMovementDistance(int, double) { }
    void playerScreenChange(int) { }
    void rocketHitWall(int rid, bool power, double x, double y, int roomx, int roomy) { c.rocketHitWallCallback(rid, power, x, y, roomx, roomy); }
    bool rocketHitPlayer(int, int) { return false; }
    void playerHitWall(int pid) { c.playerHitWallCallback(pid); }
    PlayerHitResult playerHitPlayer(int pid1, int pid2, double) { c.playerHitPlayerCallback(pid1, pid2); return PlayerHitResult(false, false, 1., 1.); }
    void rocketOutOfBounds(int rid) { c.rocketOutOfBoundsCallback(rid); }
    bool shouldApplyPhysicsToPlayer(int pid) { return c.shouldApplyPhysicsToPlayerCallback(pid); }
};

class TM_DoDisconnect : public ThreadMessage {
public:
    void execute(Client* cl) const { cl->disconnect_command(); }
};

class TM_Text : public ThreadMessage {
    Message_type type;
    string text;

public:
    TM_Text(Message_type type_, const string& text_) : type(type_), text(text_) { }
    void execute(Client* cl) const { cl->print_message(type, text); }
};

class TM_Sound : public ThreadMessage {
    int sample;

public:
    TM_Sound(int sample_) : sample(sample_) { }
    void execute(Client* cl) const { cl->client_sounds.play(sample); }
};

class TM_MapChange : public ThreadMessage {
    string name;
    NLushort crc;

public:
    TM_MapChange(const string& name_, NLushort crc_) : name(name_), crc(crc_) { }
    void execute(Client* cl) const { cl->server_map_command(name, crc); }
};

class TM_NameAuthorizationRequest : public ThreadMessage {
public:
    void execute(Client* cl) const { cl->m_playerPassword.setup(cl->playername, false); cl->showMenu(cl->m_playerPassword); }
};

class TM_GunexploEffect : public ThreadMessage {
    int rokx, roky, px, py, team;

public:
    TM_GunexploEffect(int rokx_, int roky_, int px_, int py_) : rokx(rokx_), roky(roky_), px(px_), py(py_) { }
    void execute(Client* cl) const { cl->client_graphics.create_gunexplo(rokx, roky, px, py, team); }
};

class TM_Deathbringer : public ThreadMessage {
    int team, hx, hy, sx, sy;
    double time;

public:
    TM_Deathbringer(int team_, double time_, int hx_, int hy_, int sx_, int sy_) : team(team_), hx(hx_), hy(hy_), sx(sx_), sy(sy_), time(time_) { }
    void execute(Client* cl) const { cl->client_graphics.create_deathbringer(team, time, hx, hy, sx, sy); }
};

class TM_ServerSettings : public ThreadMessage {
    NLubyte caplimit, timelimit, extratime;
    NLushort misc1, pupMin, pupMax, pupAddTime, pupMaxTime;

    void addLine(Client* cl, const std::string& caption, const std::string& value) const;

public:
    TM_ServerSettings(NLubyte caplimit_, NLubyte timelimit_, NLubyte extratime_, NLushort misc1_,
                      NLushort pupMin_, NLushort pupMax_, NLushort pupAddTime_, NLushort pupMaxTime_) :
        caplimit(caplimit_), timelimit(timelimit_), extratime(extratime_), misc1(misc1_),
        pupMin(pupMin_), pupMax(pupMax_), pupAddTime(pupAddTime_), pupMaxTime(pupMaxTime_) { }
    void execute(Client* cl) const;
};

class TM_ConnectionUpdate : public ThreadMessage {
    int code;
    char* data;
    int length;

public:
    TM_ConnectionUpdate(int code_, const void* data_, int length_);
    ~TM_ConnectionUpdate() { if (data) delete[] data; }
    void execute(Client* cl) const;
};

void ServerThreadOwner::threadFn(const ServerExternalSettings& config) {
    logThreadStart("ServerThreadOwner::threadFn", log);

    GameserverInterface gameserver(log, config, *log.accessError(), _("(server)") + ' ');
    if (!gameserver.start(config.server_maxplayers)) {
        log.error(_("Can't start listen server."));
        quitFlag = true;
    }
    else {
        log("Listen server running");
        gameserver.loop(&quitFlag, false);
        quitFlag = true;
        gameserver.stop();
        log("Listen server stopped");

        //restore client's windowtitle
        config.statusOutput(_("Outgun client"));    // note: this is the server's statusOutput not client's
    }

    logThreadExit("ServerThreadOwner::threadFn", log);
}

void ServerThreadOwner::start(int port, const ServerExternalSettings& config) {
    nAssert(quitFlag && !threadFlag);
    runPort = port;
    quitFlag = false;
    threadFlag = true;
    RedirectToMemFun1<ServerThreadOwner, void, const ServerExternalSettings&> rmf(this, &ServerThreadOwner::threadFn);
    serverThread.start_assert(rmf, config, config.priority);
}

void ServerThreadOwner::stop() {
    nAssert(threadFlag);
    quitFlag = true;
    threadFlag = false;
    serverThread.join();
}

void TournamentPasswordManager::start() {
    quitThread = false;
    thread.start_assert(RedirectToMemFun0<TournamentPasswordManager, void>(this, &TournamentPasswordManager::threadFn), priority);
}

void TournamentPasswordManager::setToken(const std::string& newToken) {
    if (token.read() != newToken) {
        token = newToken;
        tokenCallback(newToken);
    }
}

TournamentPasswordManager::TournamentPasswordManager(LogSet logs, TokenCallbackT tokenCallbackFunction, int threadPriority) :
    log(logs),
    tokenCallback(tokenCallbackFunction),
    quitThread(true),
    passStatus(PS_noPassword),
    priority(threadPriority),
    servStatus(PS_noPassword)   // no server
{
}

void TournamentPasswordManager::stop() {
    if (!quitThread) {
        log("Joining password-token thread");
        quitThread = true;
        thread.join();
    }
}

void TournamentPasswordManager::changeData(const string& newName, const string& newPass) {
    if (newName == name && newPass == password)
        return;

    stop();
    setToken("");
    if (newPass.empty()) {
        passStatus = PS_noPassword;
        return;
    }
    name = newName;
    password = newPass;
    passStatus = PS_starting;
    start();
}

string TournamentPasswordManager::statusAsString() const {
    switch (status()) {
    /*break;*/ case PS_noPassword:      return _("No password set");
        break; case PS_starting:        return _("Initializing...");
        break; case PS_socketError:     return _("Socket error");
        break; case PS_sending:         return _("Sending login...");
        break; case PS_sendError:       return _("Error sending");
        break; case PS_receiving:       return _("Waiting for response...");
        break; case PS_recvError:       return _("No response");
        break; case PS_invalidResponse: return _("Invalid response received");
        break; case PS_unavailable:     return _("Master server unavailable");
        break; case PS_tokenReceived:   return _("Logged in");
        break; case PS_badLogin:        return _("Login failed: check password");
        break; case PS_tokenSent:       return _("Logged in; sent to server");
        break; case PS_tokenAccepted:   return _("Logged in; server accepted");
        break; case PS_tokenRejected:   return _("Error: server rejected");
        break; default: nAssert(0); return 0;
    }
}

void TournamentPasswordManager::threadFn() {
    logThreadStart("TournamentPasswordManager::threadFn", log);

    bool newToken = true;
    int delay = 0;  // given a value in MS before each continue: this time will be waited before next round

    while (!quitThread) {
        if (delay > 0) {
            platSleep(500);
            delay -= 500;
            continue;
        }
        delay = 60000;  // default to one minute

        nlOpenMutex.lock();
        nlDisable(NL_BLOCKING_IO);
        NLsocket sock = nlOpen(0, NL_RELIABLE);
        nlOpenMutex.unlock();
        if (sock == NL_INVALID) {
            log("Password thread: Can't open socket. %s", getNlErrorString());
            passStatus = PS_socketError;
            delay = 10000;
            continue;
        }

        NLaddress tournamentServer;
        if (!nlGetAddrFromName("www.mycgiserver.com", &tournamentServer))
            nlStringToAddr("64.69.35.205", &tournamentServer);

        nlSetAddrPort(&tournamentServer, 80);
        nlConnect(sock, &tournamentServer);

        const string query =
            string() +
            "GET /servlet/fcecin.tk1/index.html?" + url_encode(TK1_VERSION_STRING) +
            '&' + (newToken?"new":"old") +
            "&name=" + url_encode(name) +
            "&password=" + url_encode(password) +
            " HTTP/1.0\r\n"
            "Host: www.mycgiserver.com\r\n"
            "\r\n";

        passStatus = PS_sending;
        if (newToken)
            log("Password thread: Sending login");
        NetworkResult result = writeToUnblockingTCP(sock, query.data(), query.length(), &quitThread, 30000);
        if (result != NR_ok) {
            nlClose(sock);
            if (quitThread)
                break;
            log("Password thread: Error sending login: %s", result == NR_timeout ? "Timeout" : getNlErrorString());
            passStatus = PS_sendError;
            continue;
        }

        passStatus = PS_receiving;
        string response;
        {
            ostringstream respStream;
            const NetworkResult result = saveAllFromUnblockingTCP(sock, respStream, &quitThread, 30000);
            nlClose(sock);
            if (result != NR_ok) {
                if (quitThread)
                    break;
                log("Password thread: Error receiving response: %s", result == NR_timeout ? "Timeout" : getNlErrorString());
                passStatus = PS_recvError;
                continue;
            }
            string fullResponse = respStream.str();

            // find the start and end of the body: after the last "<html>" and before the last "</html>"
            // the original code uses full case insensivity so response.find_last_of() can't be used
            int startPos, endPos;
            for (startPos = fullResponse.length() - 7; startPos >= 6; --startPos)   // start at length - 7 because "</html>" must fit after that
                if (!platStricmp(fullResponse.substr(startPos - 6, 6).c_str(), "<html>"))
                    break;
            for (endPos = fullResponse.length() - 7; endPos >= startPos; --endPos)
                if (!platStricmp(fullResponse.substr(endPos, 7).c_str(), "</html>"))
                    break;
            if (startPos < 6 || endPos < startPos) {
                log("Password thread: Invalid response (no <html>...</html>): \"%s\"", formatForLogging(fullResponse).c_str());
                passStatus = PS_invalidResponse;
                continue;
            }
            response = fullResponse.substr(startPos, endPos - startPos);
        }

        // parse the response
        for (string::size_type i = 0; i < response.length(); ++i) {
            if (response[i] < 32)
                response[i] = '+';  // for readability in the log
            if (!platStricmp(response.substr(i, 22).c_str(), "contact servlet runner")) {
                log("Password thread: Service unavailable: \"%s\"", formatForLogging(response).c_str());
                passStatus = PS_unavailable;
                break;
            }
        }
        if (passStatus == PS_unavailable)
            continue;
        log("Password thread: Received response: \"%s\"", formatForLogging(response).c_str());
        string::size_type cPos = response.find_first_of('@');
        if (cPos == string::npos || cPos + 1 >= response.length() || response.find_first_of('@', cPos + 1) != string::npos) {
            log("Password thread: Invalid response (expecting one @-code)");
            passStatus = PS_invalidResponse;
            continue;
        }
        ++cPos; // point to the control character after @
        if (response[cPos] == 'K' && cPos + 1 < response.length()) {    // login ok; token follows
            ++cPos;
            const string::size_type tokEnd = response.find_first_of('#', cPos);
            if (tokEnd == string::npos || tokEnd - cPos > 15 || tokEnd == cPos) {
                log("Password thread: Invalid response (invalid token)");
                passStatus = PS_invalidResponse;
            }
            else {
                log("Password thread: Login OK");
                passStatus = PS_tokenReceived;
                setToken(response.substr(cPos, tokEnd - cPos));
                delay = 10*60000;   // refresh token after 10 minutes
            }
        }
        else if (response[cPos] == 'E' || response[cPos] == 'F') {
            log("Password thread: Login failed (wrong name or password)");
            passStatus = PS_badLogin;
            setToken("");
            delay = 10*60000;   // try again after 10 minutes (will probably fail then too)
        }
        else {
            log("Password thread: Invalid response (bad @-code)");
            passStatus = PS_invalidResponse;
        }
    }

    logThreadExit("TournamentPasswordManager::threadFn", log);
}

bool ServerListEntry::setAddress(const string& address) {
    if (!isValidIP(address, true, 1))
        return false;
    if (!nlStringToAddr(address.c_str(), &addr))
        nAssert(0);
    if (nlGetPortFromAddr(&addr) == 0)
        nlSetAddrPort(&addr, DEFAULT_UDP_PORT);
    return true;
}

string ServerListEntry::addressString() const {
    if (nlGetPortFromAddr(&addr) != DEFAULT_UDP_PORT)
        return addressToString(addr);
    else {
        NLaddress cpy = addr;
        nlSetAddrPort(&cpy, 0);
        return addressToString(cpy);
    }
}

FileDownload::FileDownload(const string& type, const string& name, const string& filename) :
    fileType(type),
    shortName(name),
    fullName(filename),
    fp(0)
{ }

FileDownload::~FileDownload() {
    if (fp) {
        fclose(fp);
        remove(fullName.c_str());
    }
}

int FileDownload::progress() const {
    nAssert(fp);
    return ftell(fp);
}

bool FileDownload::start() {
    nAssert(!fp);
    fp = fopen(fullName.c_str(), "wb");
    return (fp != 0);
}

bool FileDownload::save(const char* buf, unsigned len) {
    nAssert(fp);
    return (fwrite(buf, sizeof(char), len, fp) == len);
}

void FileDownload::finish() {
    nAssert(fp);
    fclose(fp);
    fp = 0;
}

void TM_ServerSettings::addLine(Client* cl, const std::string& caption, const std::string& value) const {
    const int capWidth = 25;
    cl->m_serverInfo.addLine(pad_to_size_left(caption, capWidth) + ':', value);
}

void TM_ServerSettings::execute(Client* cl) const {
    cl->m_serverInfo.clear();
    cl->m_serverInfo.menu.setCaption(cl->hostname);

    addLine(cl, _("Capture limit"       ), ( caplimit == 0) ? _("none") :             itoa( caplimit));
    addLine(cl, _("Time limit"          ), (timelimit == 0) ? _("none") : _("$1 min", itoa(timelimit)));
    if (timelimit != 0)
        addLine(cl, _("Extra-time"      ), (extratime == 0) ? _("none") : _("$1 min", itoa(extratime)));
    addLine(cl, _("Player collisions"   ),  cl->fx.physics.player_collisions == PhysicalSettings::PC_none ? _("off") :
                                            cl->fx.physics.player_collisions == PhysicalSettings::PC_normal ? _("on") : _("special"));
    addLine(cl, _("Friendly fire"       ), (cl->fx.physics.friendly_fire == 0.) ? _("off") : (itoa(iround(100. * cl->fx.physics.friendly_fire)) + '%'));

    const string caps[] = { _("Balance teams"), _("Drop power-ups"), _("Invisible shadow"), _("Switch deathbringer"), _("One hit shield") };
    int i;
    for (i = 0; i < 5; i++)
        addLine(cl, caps[i], (misc1 & (1 << i)) ? _("on") : _("off"));

    addLine(cl, _("Maximum weapon level"), itoa((misc1 >> i) & 0x0F));
    i += 4;

    const bool pupMinP = pupMin >= 100,
               pupMaxP = pupMax >= 100;
    const int pupMinV = pupMinP ? pupMin - 100 : pupMin,
              pupMaxV = pupMaxP ? pupMax - 100 : pupMax;
    const bool constPowerups = (pupMaxV == 0 || (pupMinP == pupMaxP && pupMin >= pupMax));
    string maxTitle;
    if (!constPowerups)
        addLine(cl,                             _("Minimum powerups"), (pupMinP && pupMinV != 0) ? _("$1% of rooms", itoa(pupMinV)) : itoa(pupMinV));
    addLine(cl, constPowerups ? _("Powerups") : _("Maximum powerups"), (pupMaxP && pupMaxV != 0) ? _("$1% of rooms", itoa(pupMaxV)) : itoa(pupMaxV));

    if (pupMaxV != 0) {
        const bool constPowerupTime = (pupMaxTime <= pupAddTime);
        if (!constPowerupTime)
            addLine(cl,                                    _("Powerup add time"), _("$1 s", itoa(pupAddTime)));
        addLine(cl, constPowerupTime ? _("Powerup time") : _("Powerup max time"), _("$1 s", itoa(pupMaxTime)));
    }

    if (cl->menu.options.game.showServerInfo())
        cl->showMenu(cl->m_serverInfo);
}

TM_ConnectionUpdate::TM_ConnectionUpdate(int code_, const void* data_, int length_) :
    code(code_),
    length(length_)
{
    if (length) {
        data = new char[length];
        memcpy(data, data_, length);
    }
    else
        data = 0;
}

void TM_ConnectionUpdate::execute(Client* cl) const {
    switch (code) {
    /*break;*/ case 0: cl->client_connected(data, length);
        break; case 1: cl->client_disconnected(data, length);
        break; case 2: cl->connect_failed_denied(data, length);
        break; case 3: cl->connect_failed_unreachable();
        break; case 5: cl->connect_failed_socket();
        break; case 4: {
            const string msg = _("The server is full.");
            cl->connect_failed_denied(msg.data(), msg.length());
        }
        break; default: nAssert(0);
    }
}

Client::Client(LogSet hostLogs, const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, MemoryLog& externalErrorLog_):
    normalLog(wheregamedir + "log" + directory_separator + "clientlog.txt", true),
    externalErrorLog(externalErrorLog_),
    errorLog(normalLog, externalErrorLog, "ERROR: "),
    //securityLog(normalLog, "SECURITY WARNING: ", wheregamedir + "log" + directory_separator + "client_securitylog.txt", false),
    log(&normalLog, &errorLog, 0),
    listenServer(log),
    tournamentPassword(log, new RedirectToMemFun1<Client, void, string>(this, &Client::CB_tournamentToken), config.lowerPriority),
    current_map(-1),
    map_vote(-1),
    player_stats_page(0),
    lastAltEnterTime(0),
    abortThreads(false),
    refreshStatus(RS_none),
    password_file(wheregamedir + "config" + directory_separator + "passwd"),
    client_graphics(log),
    screenshot(false),
    mapChanged(false),
    predrawNeeded(false),
    client_sounds(log),
    messageLogOpen(false),
    extConfig(config),
    serverExtConfig(serverConfig)
{
    hostLogs("See clientlog.txt for client's log messages");

    //net client
    client = 0;

    setMaxPlayers(MAX_PLAYERS);
    //all the players to show including me
    //player_t player[MAX_PLAYERS];
    for (int p = 0; p < MAX_PLAYERS; p++)
        fx.player[p].used = false;

    //wich player I am
    me = -1;

    //time of last packet received
    lastpackettime = 0;

    initMenus();
    showMenu(menu);
    menusel = menu_none;

    //game showing?
    gameshow = false;

    //frames and seconds for FPS counter
    FPS = 0;
    framecount = 0;
    totalframecount = 0;
    frameCountStartTime = 0;

    //if player wants to changeteams
    want_change_teams = false;

    //connected? (that is, "connection accepted")
    connected = false;

    Thread::setCallerPriority(config.priority);
}

Client::~Client() {
    log("Exiting client: destructor");

    abortThreads = true;
    if (client) {
        delete client;
        client = 0;
    }
    while (refreshStatus != RS_none && refreshStatus != RS_failed)  // wait for a possible refresh thread to abort itself
        platSleep(50);

    for (deque<ThreadMessage*>::const_iterator mi = messageQueue.begin(); mi != messageQueue.end(); ++mi)
        delete *mi;

    log("Exiting client: destructor exiting");
}

bool Client::start() {
    extConfig.statusOutput(_("Outgun client"));

    totalframecount = 0;
    framecount = 0;

    clFrameSent = clFrameWorld = 0;
    fx.frame = fd.frame = -1;
    frameReceiveTime = 0;

    #ifdef SEND_FRAMEOFFSET
    frameOffsetDeltaTotal = 0;
    frameOffsetDeltaNum = 0;
    #endif
    averageLag = 0;

    netsendAdjustment = 0;

    // default map
    //load_default_map(&map);
    map_ready = false;  // NO map change commands from server yet
    clientReadiesWaiting = 0;

    //not showing gameover plaque
    gameover_plaque = NEXTMAP_NONE;

    connected = false;

    client = new_client_c(extConfig.networkPriority);
    client->setCallbackCustomPointer(this);
    client->setConnectionCallback(cfunc_connection_update);
    client->setServerDataCallback(cfunc_server_data);

    //try to load the client's password
    string fileName = wheregamedir + "config" + directory_separator + "password.bin";
    FILE* psf = fopen(fileName.c_str(), "rb");
    if (psf) {
        char pas[PASSBUFFER];
        for (int c = 0; c < PASSBUFFER; c++) {
            const int cha = fgetc(psf); // don't care about EOF yet
            pas[c] = static_cast<char>(255 - cha);
        }
        if (!feof(psf)) {
            pas[8] = 0;
            menu.options.name.password.set(pas);
        }
        fclose(psf);
    }

    //try to load client configuration
    vector<int> fav_colors;
    playername = RandomName();

    fileName = wheregamedir + "config" + directory_separator + "client.cfg";
    ifstream cfg(fileName.c_str());
    for (;;) {
        string line;
        if (!getline_skip_comments(cfg, line))
            break;

        istringstream command(line);

        int settingId;
        string args;
        command >> settingId;
        command.ignore();   // eat separator (space)
        if (!command || settingId < 0) {
            log.error(_("Invalid syntax in client.cfg (\"$1\").", line));
            continue;
        }
        if (settingId > CCS_MaxCommand) {
            log.error(_("Unknown data in client.cfg (\"$1\").", line));
            continue;
        }

        getline(command, args); // this might fail, but that only means there is an empty string
        switch (static_cast<ClientCfgSetting>(settingId)) {
            // name menu
        /*break;*/ case CCS_PlayerName:            if (check_name(args)) playername = args;
            break; case CCS_Tournament:            menu.options.name.tournament.set(args == "1");

            // connect menu
            break; case CCS_Favorites:             menu.connect.favorites.set(args == "1");

            // game menu
            break; case CCS_ShowNames:             menu.options.game.showNames.set(args == "1");
            break; case CCS_FavoriteColors: {
                istringstream ist(args);
                int col;
                while (ist >> col)
                    if (col >= 0 && col < 16 && find(fav_colors.begin(), fav_colors.end(), col) == fav_colors.end())
                        fav_colors.push_back(col);
            }
            break; case CCS_LagPrediction:         menu.options.game.lagPrediction.set(args == "1");
            break; case CCS_LagPredictionAmount:   menu.options.game.lagPredictionAmount.boundSet(atoi(args));
            break; case CCS_MessageLogging:        menu.options.game.messageLogging.set(args == "1" ? Menu_game::ML_full : args == "2" ? Menu_game::ML_chat : Menu_game::ML_none);
            break; case CCS_SaveStats:             menu.options.game.saveStats.set(args == "1");
            break; case CCS_ShowStats:             menu.options.game.showStats.set(args == "1" ? Menu_game::SS_teams : args == "2" ? Menu_game::SS_players : Menu_game::SS_none);
            break; case CCS_ShowServerInfo:        menu.options.game.showServerInfo.set(args == "1");
            break; case CCS_StayDeadInMenus:       menu.options.game.stayDead.set(args == "1");
            break; case CCS_UnderlineMasterAuth:   menu.options.game.underlineMasterAuth.set(args == "1");
            break; case CCS_UnderlineServerAuth:   menu.options.game.underlineServerAuth.set(args == "1");
            break; case CCS_AutoGetServerList:     menu.options.game.autoGetServerList.set(args == "1");

            // controls menu
            break; case CCS_KeyboardLayout:
                if (!menu.options.controls.keyboardLayout.set(args)) {  // it is possible to have a layout Outgun doesn't know about
                    menu.options.controls.keyboardLayout.addOption(_("unknown ($1)", args), args);
                    nAssert(menu.options.controls.keyboardLayout.set(args));
                }
            break; case CCS_KeypadMoving:          menu.options.controls.keypadMoving.set(args == "1");
            break; case CCS_ArrowKeysInStats:      menu.options.controls.arrowKeysInStats.set(args == "1" ? Menu_controls::AS_movePlayer : Menu_controls::AS_useMenu);
            break; case CCS_Joystick:              menu.options.controls.joystick.set(args == "1");
            break; case CCS_JoystickMove:          menu.options.controls.joyMove.boundSet(atoi(args));
            break; case CCS_JoystickShoot:         menu.options.controls.joyShoot.boundSet(atoi(args));
            break; case CCS_JoystickRun:           menu.options.controls.joyRun.boundSet(atoi(args));
            break; case CCS_JoystickStrafe:        menu.options.controls.joyStrafe.boundSet(atoi(args));

            // screen mode menu
            break; case CCS_Windowed:              menu.options.screenMode.windowed.set(args == "1");
            break; case CCS_GFXMode: {
                if (extConfig.forceDefaultGfxMode)
                    break;
                istringstream is(args);
                int width, height, depth;
                is >> width >> height >> depth;
                bool ok = is;
                char nullc;
                is >> nullc;
                if (!ok || is || width < 320 || height < 200 || (depth != 16 && depth != 24 && depth != 32))
                    log("Bad screen mode in client.cfg");
                else {
                    menu.options.screenMode.colorDepth.set(depth);    // may fail if the previous depth isn't available
                    menu.options.screenMode.update(client_graphics);  // fetch resolutions according to the new depth
                    if (!menu.options.screenMode.resolution.set(ScreenMode(width, height)))
                        log("Previous screen mode not available (%d×%d×%d)", width, height, depth);
                }
            }
            break; case CCS_Flipping:              menu.options.screenMode.flipping.set(args == "1");
            break; case CCS_AlternativeFlipping:   menu.options.screenMode.alternativeFlipping.set(args == "1");

            // graphics menu
            break; case CCS_FPSLimit:              menu.options.graphics.fpsLimit.boundSet(atoi(args));
            break; case CCS_GFXTheme:              menu.options.graphics.theme.set(args);      // ignore error
            break; case CCS_UseThemeBackground:    menu.options.graphics.useThemeBackground.set(args == "1");
            break; case CCS_Background:            menu.options.graphics.background.set(args); // ignore error
            break; case CCS_Font:                  menu.options.graphics.font.set(args);       // ignore error
            break; case CCS_Antialiasing:          menu.options.graphics.antialiasing.set(args == "2");
            break; case CCS_MinTransp:             menu.options.graphics.minTransp.set(args == "1");
            break; case CCS_ContinuousTextures:    menu.options.graphics.contTextures.set(args == "1");
            break; case CCS_MinimapPlayers:        menu.options.graphics.minimapPlayers.set(args == "1" ? Menu_graphics::MP_EarlyCut : args == "2" ? Menu_graphics::MP_LateCut : Menu_graphics::MP_Fade);
            break; case CCS_StatsBgAlpha:          menu.options.graphics.statsBgAlpha.boundSet(atoi(args));

            // sound menu
            break; case CCS_SoundEnabled:          menu.options.sounds.enabled.set(args == "1");
            break; case CCS_Volume:                menu.options.sounds.volume.boundSet(atoi(args));
            break; case CCS_SoundTheme:            menu.options.sounds.theme.set(args); // ignore error

            // local server menu
            break; case CCS_ServerPublic:          menu.ownServer.pub.set(args == "1");
            break; case CCS_ServerPort:            menu.ownServer.port.boundSet(atoi(args));
            break; case CCS_ServerAddress:         menu.ownServer.address.set(args);
            break; case CCS_AutodetectAddress:     menu.ownServer.autoIP.set(args == "1");
            break; default: nAssert(0); // must handle all values up to the highest known
        }
    }
    cfg.close();

    fileName = wheregamedir + "config" + directory_separator + "favorites.txt";
    ifstream fav(fileName.c_str());
    if (fav) {
        string addr;
        while (getline_skip_comments(fav, addr)) {
            ServerListEntry spy;
            if (spy.setAddress(addr))
                gamespy.push_back(spy);
        }
        fav.close();
    }

    // finalize and apply the settings

    // name
    tournamentPassword.changeData(playername, menu.options.name.password());

    // game
    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        openMessageLog();
    for (int i = 0; i < 16; i++)
        if (find(fav_colors.begin(), fav_colors.end(), i) == fav_colors.end())
            fav_colors.push_back(i);
    for (vector<int>::const_iterator col = fav_colors.begin(); col != fav_colors.end(); ++col)
        menu.options.game.favoriteColors.addOption(*col);

    // controls
    MCF_keyboardLayout();
    if (menu.options.controls.joystick())
        install_joystick(JOY_TYPE_AUTODETECT);

    // graphics
    if (extConfig.winclient != -1)
        menu.options.screenMode.windowed.set(extConfig.winclient);
    if (extConfig.trypageflip != -1)
        menu.options.screenMode.flipping.set(extConfig.trypageflip);
    if (extConfig.targetfps != -1)
        menu.options.graphics.fpsLimit.set(extConfig.targetfps);
    client_graphics.set_antialiasing(menu.options.graphics.antialiasing());
    client_graphics.set_min_transp(menu.options.graphics.minTransp());
    MCF_statsBgChange();
    client_graphics.select_theme(menu.options.graphics.theme(), menu.options.graphics.background(), menu.options.graphics.useThemeBackground());
    client_graphics.select_font(menu.options.graphics.font());
    if (!screenModeChange())
        return false;

    // sounds
    if (extConfig.nosound)
        menu.options.sounds.enabled.set(false);
    MCF_sndEnableChange();
    client_sounds.setVolume(menu.options.sounds.volume());
    client_sounds.select_theme(menu.options.sounds.theme());

    // local server
    if (serverExtConfig.privSettingForced)
        menu.ownServer.pub.set(!serverExtConfig.privateserver);
    if (serverExtConfig.portForced)
        menu.ownServer.port.set(serverExtConfig.port);  // assume caller to take care of limiting to proper range (1..65535)
    if (serverExtConfig.ipForced) {
        menu.ownServer.autoIP.set(false);
        menu.ownServer.address.set(serverExtConfig.ipAddress);
    }

    // message highlighting
    load_highlight_texts();

    if (menu.options.game.autoGetServerList())
        MCF_updateServers();

    return true;
}

//send "client ready" message to server (when map load and/or download completes)
void Client::send_client_ready() {
    char lebuf[256]; int count = 0;
    writeByte(lebuf, count, data_client_ready);
    client->send_message(lebuf, count);
}

// incoming chunk of requested file by UDP
void Client::process_udp_download_chunk(const char* buf, int len, bool last) {
    MutexDebug md("downloadMutex", __LINE__, log);
    MutexLock ml(downloadMutex);
    if (downloads.empty() || !downloads.front().isActive()) {
        log.error("Server sent a file we aren't expecting");
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    FileDownload& dl = downloads.front();
    if (!dl.save(buf, len)) {
        log.error(_("Error writing to '$1'.", dl.fullName));
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    //send the reply
    char lebuf[256]; int count = 0;
    writeByte(lebuf, count, data_file_ack);
    client->send_message(lebuf, count);
    if (last) {
        dl.finish();
        log("Download complete: %s '%s' to %s", dl.fileType.c_str(), dl.shortName.c_str(), dl.fullName.c_str());
        if (dl.fileType == "map") {
            if (dl.shortName == servermap) {
                const bool ok = fd.load_map(log, CLIENT_MAPS_DIR, dl.shortName) && fx.load_map(log, CLIENT_MAPS_DIR, dl.shortName); //#fix
                remove_useless_flags();
                if (!ok) {
                    log.error("After download: map '" + dl.shortName + "' not found");
                    addThreadMessage(new TM_DoDisconnect());
                    return;
                }
                log("Map '%s' downloaded successfully", dl.shortName.c_str());
                mapChanged = true;
                map_ready = true;
            }
            ++clientReadiesWaiting;
        }
        else
            nAssert(0);
        downloads.pop_front();
        check_download();
    }
}

/* check_download: if there is a download pending, and nothing is downloading, activate it
 * call with downloadMutex locked
 */
void Client::check_download() { // call with downloadMutex locked
    if (downloads.empty())
        return;
    FileDownload& dl = downloads.front();
    if (dl.isActive())
        return;
    if (!dl.start()) {
        log.error(_("File download: Can't open '$1' for writing.", dl.fullName));
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    // request the file from server
    char lebuf[256]; int count = 0;
    writeByte(lebuf, count, data_file_request);
    writeStr(lebuf, count, dl.fileType);
    writeStr(lebuf, count, dl.shortName);
    client->send_message(lebuf, count);
}

void Client::download_server_file(const string& type, const string& name) {
    nAssert(type == "map");
    if (name.find_first_of("./:\\") != string::npos) {
        log.error("Illegal file download request: map \"" + name + "\"");
        addThreadMessage(new TM_DoDisconnect());
        return;
    }

    MutexDebug md("downloadMutex", __LINE__, log);
    MutexLock ml(downloadMutex);
    const string fileName = wheregamedir + CLIENT_MAPS_DIR + directory_separator + name + ".txt";
    downloads.push_back(FileDownload(type, name, fileName));
    check_download();
}

// Server tells client of current map / map change.
// Client checks from the "cmaps" and "maps" directory.
// If the map file is not there, or the CRC's don't match, download the map from the server to "cmaps".
void Client::server_map_command(const string& mapname, NLushort server_crc) {
    log("Received map change: '%s'", mapname.c_str());

    servermap = mapname;

    // Try to load the map first from "cmaps" and, if not found there, from "maps".
    if (load_map(CLIENT_MAPS_DIR, mapname, server_crc) || load_map(SERVER_MAPS_DIR, mapname, server_crc)) {
        log("Map '%s' loaded successfully.", mapname.c_str());
        remove_useless_flags();
        mapChanged = true;
        map_ready = true;
        ++clientReadiesWaiting;
        return;
    }

    // start download
    const string msg = _("Downloading map \"$1\" (CRC $2)...", mapname, itoa(server_crc));
    print_message(msg_info, msg);
    log("%s", msg.c_str());

    download_server_file("map", mapname);
}

bool Client::load_map(const string& directory, const string& mapname, NLushort server_crc) {
    LogSet noLogSet(0, 0, 0);   // if there's an error with the map, don't log it

    const bool ok = fd.load_map(noLogSet, directory, mapname) && fx.load_map(noLogSet, directory, mapname); //#fix

    if (!ok)
        log("Map '%s' not found in '%s'.", mapname.c_str(), directory.c_str());
    else if (fx.map.crc != server_crc)
        log("Map '%s' found in '%s' but it's CRC %i differs from server map CRC %i.",
            mapname.c_str(), directory.c_str(), fx.map.crc, server_crc);
    else
        return true;
    return false;
}

void Client::disconnect_command() { // do not call from a network thread
    //disconnect the client here if was connected, else does nothing
    client->connect(false);
}

void Client::client_connected(const char* data, int length) {   // call with frameMutex locked
    log("Connection successful");

    (void)length;

    //"data" from connection accepted:
    //  BYTE    maxplayers
    //  STRING  hostname
    int count = 0;
    NLubyte maxpl;
    readByte(data, count, maxpl);
    setMaxPlayers(maxpl);

    readStr(data, count, hostname);
    m_serverInfo.clear();
    m_serverInfo.addLine("");   // can't draw a totally empty menu; this will be overwritten when config information

    if (!menu.options.game.favoriteColors.values().empty()) {
        char lebuf[256]; int count = 0;
        writeByte(lebuf, count, data_fav_colors);
        writeByte(lebuf, count, menu.options.game.favoriteColors.values().size());
        // send two colours in a byte
        for (vector<int>::const_iterator col = menu.options.game.favoriteColors.values().begin();
                    col != menu.options.game.favoriteColors.values().end(); ++col) {
            NLubyte byte = static_cast<NLubyte>(*col) & 0x0F;
            if (++col != menu.options.game.favoriteColors.values().end())
                byte |= static_cast<NLubyte>(*col) << 4;
            writeByte(lebuf, count, byte);
        }
        client->send_message(lebuf, count);
    }

    show_all_messages = false;
    stats_autoshowing = false;

    //set window title: the hostname
    const string caption = _("Connected to $1 ($2)", hostname.substr(0, 32), addressToString(serverIP));
    extConfig.statusOutput(caption);

    //don't want to change teams by default
    want_change_teams = false;

    //don't want to exit map by default
    want_map_exit = false;
    want_map_exit_delayed = false;

    //avoid "dropped" plaque
    lastpackettime = get_time() + 4.0;

    averageLag = 0;

    clFrameSent = clFrameWorld = 0;
    frameReceiveTime = 0;

    // reset gamestate?
    connected = true;
    gameshow = true;
    openMenus.clear();  // connect progress menu is showing; exceptions are when it's been closed and the disconnect is still pending, and when help is opened on top of it
    fx.frame = fd.frame = -1;
    fx.skipped = fd.skipped = true;
    me = -1;    // will be corrected from the first frame

    //reset chat buffer
    talkbuffer.clear();
    chatbuffer.clear();

    //reset world data
    // teams
    for (int i = 0; i < 2; i++) {
        fx.teams[i].clear_stats();
        fx.teams[i].remove_flags();
    }
    // players
    for (int i = 0; i < MAX_PLAYERS; i++)
        fx.player[i].clear(false, i, "", i / TSIZE);
    players_sb.clear();
    // powerups
    for (int i = 0; i < MAX_PICKUPS; ++i)
        fx.item[i].kind = Powerup::pup_unused;

    //reset FPS count vars
    framecount = 0;
    frameCountStartTime = get_time();
    FPS = 0;

    //reset map time
    map_time_limit = false;
    map_start_time = 0;
    map_end_time = 0;
    map_vote = -1;
    remove_flags = 0;

    //send name update request
    issue_change_name_command();
    // send registration token (if any)
    const string s = tournamentPassword.getToken();
    if (!s.empty())
        CB_tournamentToken(s);
    send_tournament_participation();

    map_ready = false;
    clientReadiesWaiting = 0;
    servermap.clear();

    {
        MutexDebug md("mapInfoMutex", __LINE__, log);
        MutexLock ml(mapInfoMutex);
        maps.clear();
    }

    //not showing gameover plaque
    gameover_plaque = NEXTMAP_NONE;

    //clear client side effects
    client_graphics.clear_fx();

    send_frame(true, true);
}

void Client::send_tournament_participation() {
    char lebuf[8]; int count = 0;
    writeByte(lebuf, count, data_tournament_participation);
    writeByte(lebuf, count, menu.options.name.tournament() ? 1 : 0);
    client->send_message(lebuf, count);
}

void Client::client_disconnected(const char* data, int length) {
    if (!connected)
        return;

    //restore window title
    extConfig.statusOutput(_("Outgun client"));

    // the gamestate?
    connected = false;
    gameshow = false;
    menusel = menu_none;

    string description;

    if (length == 1)
        switch (data[0]) {
        /*break;*/ case server_c::disconnect_client_initiated: // user knows why, so no description
            break; case server_c::disconnect_server_shutdown:  description = _("Server was shut down.");
            break; case server_c::disconnect_timeout:          description = _("Connection timed out.");
            break; case disconnect_kick:                       description = _("You were kicked.");
            break; case disconnect_idlekick:                   description = _("You were kicked for being idle.");
            break; case disconnect_client_misbehavior:         description = _("Internal error (client misbehaved).");
            break; default:;
        }
    m_connectProgress.clear();
    m_connectProgress.wrapLine(_("You have been disconnected."));
    if (!description.empty())
        m_connectProgress.wrapLine(description);
    showMenu(m_connectProgress);
    if (description.empty())
        log("Disconnection successful");
    else
        log("Disconnected: %s", description.c_str());

    tournamentPassword.disconnectedFromServer();

    {
        MutexDebug md("downloadMutex", __LINE__, log);
        MutexLock ml(downloadMutex);
        downloads.clear();
    }
}

void Client::connect_failed_denied(const char* data, int length) {
    string message;
    bool userHandled = false;
    if (length > 1) {
        int count = 0;
        readStr(data, count, message);
        const string str1 = "Protocol mismatch: server: ";
        const string str2 = ", client: " GAME_PROTOCOL;
        const string::size_type str2pos = message.length() - str2.length();
        if (message.compare(0, str1.length(), str1) == 0 && str2pos > 0 && message.compare(str2pos, str2.length(), str2) == 0) {
            const std::string serverProtocol = message.substr(str1.length(), str2pos - str1.length());
            message = _("Protocol mismatch. Server: $1, client: $2.", serverProtocol, GAME_PROTOCOL);
        }
        // otherwise leave message at its value of whatever the server sent
    }
    else if (length == 1) {
        int count = 0;
        NLubyte rb;
        readByte(data, count, rb);
        if (rb > reject_last)
            message = _("Unknown reason code ($1).", itoa(rb));
        else {
            Connect_rejection_reason reason = static_cast<Connect_rejection_reason>(rb);
            switch (reason) {
            /*break;*/ case reject_server_full:
                    message = _("The server is full.");
                break; case reject_banned:
                    message = _("You are banned from this server.");
                break; case reject_player_password_needed:
                    openMenus.close(&m_connectProgress.menu);
                    m_playerPassword.setup(playername, false);
                    showMenu(m_playerPassword);
                    userHandled = true;
                    message = "Asking for player password."; // just for logging
                break; case reject_wrong_player_password:
                    message = _("Wrong player password.");
                    remove_player_password(playername, addressToString(serverIP));
                break; case reject_server_password_needed:
                    openMenus.close(&m_connectProgress.menu);
                    showMenu(m_serverPassword);
                    userHandled = true;
                    message = "Asking for server password."; // just for logging
                break; case reject_wrong_server_password:
                    message = _("Wrong server password.");
                    m_serverPassword.password.set("");
                break; default: nAssert(0);
            }
        }
    }
    else
        message = _("No reason given.");

    log("Connecting failed: %s", message.c_str());

    if (!userHandled) {
        m_connectProgress.wrapLine(_("Connection refused."));
        m_connectProgress.wrapLine(message);
        // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    }
}

void Client::connect_failed_unreachable() {
    m_connectProgress.wrapLine(_("No response from server."));
    // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    log("Connecting failed: no response");
}

void Client::connect_failed_socket() {
    m_connectProgress.wrapLine(_("Can't open socket."));
    // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    log("Connecting failed: no response");
}

string Client::load_player_password(const string& name, const string& address) const {
    ifstream in(password_file.c_str());
    while (in) {
        string load_name, load_address, load_password;
        getline_smart(in, load_name);
        getline_smart(in, load_address);
        getline_smart(in, load_password);
        if (load_name == name && load_address == address)
            return load_password;
    }
    return string();
}

vector<vector<string> > Client::load_all_player_passwords() const {
    vector<vector<string> > passwords;
    ifstream in(password_file.c_str());
    while (1) {
        string name, address, password;
        getline_smart(in, name);
        getline_smart(in, address);
        getline_smart(in, password);
        if (in) {
            vector<string> entry;
            entry.push_back(name);
            entry.push_back(address);
            entry.push_back(password);
            passwords.push_back(entry);
        }
        else
            break;
    }
    return passwords;
}

void Client::save_player_password(const string& name, const string& address, const string& password) const {
    nAssert(!name.empty() && !address.empty() && !password.empty());    // empty lines cause trouble
    vector<vector<string> > passwd_list = load_all_player_passwords();
    // check if player already has a password
    string test = load_player_password(name, address);
    if (test.empty()) {
        vector<string> entry;
        entry.push_back(name);
        entry.push_back(address);
        entry.push_back(password);
        passwd_list.push_back(entry);
    }
    ofstream out(password_file.c_str());
    if (!out) {
        log.error(_("Can't save player password to '$1'!", password_file));
        return;
    }
    for (vector<vector<string> >::const_iterator item = passwd_list.begin(); item != passwd_list.end(); ++item) {
        out << (*item)[0] << '\n';
        out << (*item)[1] << '\n';
        if ((*item)[0] == name && (*item)[1] == address) {
            out << password;
            log("New player password saved for %s at %s.", name.c_str(), address.c_str());
        }
        else
            out << (*item)[2];
        out << '\n';
    }
}

void Client::remove_player_password(const string& name, const string& address) const {
    // check if player has a password
    const string test = load_player_password(name, address);
    if (test.empty())
        return;
    const vector<vector<string> > passwd_list = load_all_player_passwords();
    ofstream out(password_file.c_str());
    if (!out)
        return;
    for (vector<vector<string> >::const_iterator item = passwd_list.begin(); item != passwd_list.end(); ++item)
        if ((*item)[0] == name && (*item)[1] == address)
            log("%s's player password at %s removed.", name.c_str(), address.c_str());
        else
            for (int i = 0; i < 3; i++)
                out << (*item)[i] << '\n';
}

int Client::remove_player_passwords(const std::string& name) const {
    vector<vector<string> > passwd_list = load_all_player_passwords();
    ofstream out(password_file.c_str());
    if (!out)
        return 0;
    int removed = 0;
    for (vector<vector<string> >::iterator item = passwd_list.begin(); item != passwd_list.end(); ++item)
        if ((*item)[0] == name)
            removed++;
        else
            for (int i = 0; i < 3; i++)
                out << (*item)[i] << '\n';
    log("%s's player passwords removed.", name.c_str());
    return removed;
}

void Client::connect_command(bool loadPassword) {   // call with frameMutex locked
    const bool alreadyConnected = connected;

    // disconnect
    client->connect(false);

    if (alreadyConnected)   // very basic and ugly hack to let the disconnection take place at least semi-reliably; this is needed because Leetnet sucks
        platSleep(500);

    handlePendingThreadMessages();  // this is needed so that the potential disconnection message doesn't screw up the new connection
    openMenus.close(&m_connectProgress.menu);

    // start connecting to specified IP/port
    // connection results will come through the CFUNC_CONNECTION_UPDATE callback
    const string strAddress = addressToString(serverIP);
    client->set_server_address(strAddress.c_str());

    log("Connecting to %s... passwords: server %s, player %s", strAddress.c_str(),
        m_serverPassword.password().empty() ? "no" : "yes",
        m_playerPassword.password().empty() ? "no" : "yes");

    //set connect-data (goes in every connect packet): outgun game name and protocol strings
    char lebuf[256]; int count = 0;
    writeString(lebuf, count, GAME_STRING);
    writeString(lebuf, count, GAME_PROTOCOL);
    writeStr(lebuf, count, playername);
    writeStr(lebuf, count, m_serverPassword.password());    // empty or not, it's needed
    if (loadPassword)
        m_playerPassword.password.set(load_player_password(playername, strAddress));
    writeStr(lebuf, count, m_playerPassword.password());    // empty or not, it's needed

    client->set_connect_data(lebuf, count);

    client->connect(true, extConfig.minLocalPort, extConfig.maxLocalPort);

    m_connectProgress.clear();
    m_connectProgress.wrapLine(_("Trying to connect..."), true);
    showMenu(m_connectProgress);
}

void Client::issue_change_name_command() {
    if (!connected)
        return;
    //regular change name
    char lebuf[256]; int count = 0;
    writeByte(lebuf, count, data_name_update);
    nAssert(check_name(playername));
    writeStr(lebuf, count, playername); // the name
    writeStr(lebuf, count, m_playerPassword.password());    // empty or not, it's needed
    client->send_message(lebuf, count);
}

void Client::change_name_command() {
    //set new name, close menu
    menu.options.name.name.set(trim(menu.options.name.name()));
    const string& newName = menu.options.name.name();
    if (!check_name(newName))
        return;
    openMenus.close(&menu.options.name.menu);

    playername = newName;
    m_playerPassword.password.set(load_player_password(playername, addressToString(serverIP)));
    issue_change_name_command();
    tournamentPassword.changeData(playername, menu.options.name.password());
}

ClientControls Client::readControls(bool canUseKeypad, bool useCursorKeys) {
    ClientControls ctrl;
    ctrl.fromKeyboard(canUseKeypad && menu.options.controls.keypadMoving(), useCursorKeys);
    if (menu.options.controls.joystick())
        ctrl.fromJoystick(menu.options.controls.joyMove() - 1, menu.options.controls.joyRun(), menu.options.controls.joyStrafe());
    return ctrl;
}

//send the client's frame to server (keypresses)
void Client::send_frame(bool newFrame, bool forceSend) {
    static ClientControls sentControls;
    static double keyFilterTimeout = 0;

    ClientControls currentControls;
    if (openMenus.empty()) { // don't move at all when a real menu is open
        currentControls = readControls(menusel != menu_maps, menusel == menu_none || menu.options.controls.arrowKeysInStats() == Menu_controls::AS_movePlayer);  // reserve cursor keys for stats screen or similar unless forced
        currentControls.clearModifiersIfIdle();
    }

    if (!forceSend && currentControls == sentControls)
        return;

    bool filtered;
    // filtering is applied when first going diagonally and then one of the directions is dropped
    if (sentControls.isUpDown() && sentControls.isLeftRight() && currentControls.isUpDown() != currentControls.isLeftRight()) {
        if (keyFilterTimeout == 0) {
            filtered = true;
            keyFilterTimeout = get_time() + .02;
        }
        else if (get_time() < keyFilterTimeout)
            filtered = true;
        else {
            filtered = false;
            keyFilterTimeout = 0;
        }
    }
    else {
        filtered = false;
        keyFilterTimeout = 0;
    }

    if (filtered && !forceSend)
        return;

    if (!filtered)
        sentControls = currentControls;

    if (newFrame) {
        ++clFrameSent;
        controlHistory[clFrameSent] = sentControls;
        svFrameHistory[clFrameSent] = static_cast<NLulong>(fx.frame);
    }

    char lebuf[256]; int count = 0;
    writeByte(lebuf, count, clFrameSent);
    writeByte(lebuf, count, sentControls.toNetwork(false));
    client->send_frame(lebuf, count);
}

void Client::process_incoming_data(const char* data, int length) {
    MutexDebug md("frameMutex", __LINE__, log);
    MutexLock ml(frameMutex);

    if (!connected) // means that the connection notification is still in the thread message queue
        return;

    (void)length;

    // (0) update lastpackettime
    lastpackettime = get_time();

    //(1) process server frame data
    int count = 0;
    NLulong svframe;    //server's frame
    readLong(data, count, svframe);

    if (WATCH_CONNECTION && svframe != fx.frame + 1) {
        ostringstream dstr;
        if (svframe == fx.frame)
            dstr << "S>C packet duplicated: " << svframe;
        else if (svframe < fx.frame)
            dstr << "S>C packet order: prev " << fx.frame << " this " << svframe;
        else
            dstr << "S>C packet lost : prev " << fx.frame << " this " << svframe;
        addThreadMessage(new TM_Text(msg_warning, dstr.str().c_str()));
    }
    //discard older frames
    //overwrite always the newer frames
    // TARGET FRAME: just one
    if (svframe > fx.frame) {
        nAssert(fx.frame == floor(fx.frame));

        ClientPhysicsCallbacks cb(*this);
        fx.rocketFrameAdvance(static_cast<int>(svframe - fx.frame), cb);
        fx.frame = svframe;

        //----- PLAYER SPECIFIC DATA -----

        readByte(data, count, clFrameWorld);
        frameReceiveTime = get_time();
        const int currentLag = bound(svframe - svFrameHistory[clFrameWorld], 0ul, 50ul);    // bound because svFrameHistory has invalid frame# at connect to server
        averageLag = averageLag * .99 + currentLag * .01;

        #ifdef SEND_FRAMEOFFSET
        NLubyte fo;
        readByte(data, count, fo);
        const double offsetDelta = (fo / 256.) - .5;    // the deviation from aim, in frames
        frameOffsetDeltaTotal += offsetDelta;
        if (++frameOffsetDeltaNum == 10) {  // try to fix deviations every 10 frames
            frameOffsetDeltaTotal /= 10.;
            netsendAdjustment = -(frameOffsetDeltaTotal * .1); // convert to seconds
            frameOffsetDeltaTotal = 0;
            frameOffsetDeltaNum = 0;
        }
        #endif

        //extra byte of information
        // BIT 0: extra health
        // BIT 1: extra energy
        // BIT 2 (****VERY IMPORTANT****): NO MORE DATA ON PACKET BECAUSE PLAYER IS NOT READY
        NLubyte xtra;
        readByte(data, count, xtra);

        //moved below: after health assignment
        //if (xtra & 1) player[me].health += 256;
        //if (xtra & 2) player[me].energy += 256;

        const bool empty_frame_cause_not_ready_yet = ((xtra & 4) != 0);

        //read "me" (v0.3.9 tentando espantar bug com tiro de canhao!)
        // BITS 3..8 == what player id
        if (me == -1)   // only read this when just connected to the server; otherwise, changes in "me" should be taken in only with the change teams message
            me = xtra >> 3;

        //EMPTY FRAME? if yes, do something about it, if not, parse it
        if (empty_frame_cause_not_ready_yet)
            fx.skipped = true;
        else {
            if (!map_ready) {
                log.error("Server sent frame data when loading map");
                addThreadMessage(new TM_DoDisconnect());
                return;
            }
            //a regular frame
            fx.skipped = false;

            //V 0.3.9 NEW : read screen of "me" player
            NLubyte scr;
            readByte(data, count, scr);     //player.x
            fx.player[me].roomx = scr;
            readByte(data, count, scr);     //player.y
            fx.player[me].roomy = scr;

            if (fx.player[me].roomx != fx.player[me].oldx || fx.player[me].roomy != fx.player[me].oldy) {
                for (int j = 0; j < MAX_PICKUPS; j++)
                    fx.item[j].kind = Powerup::pup_unused;  // the server will send messages for all seen, others should be forgotten

                fx.player[me].oldx = fx.player[me].roomx;
                fx.player[me].oldy = fx.player[me].roomy;

                predrawNeeded = true;
            }

            //read "players onscreen" vector
            NLulong players_onscreen;
            readLong(data, count, players_onscreen);

            //decode players_onscreen and update player data
            for (int i = 0; i < maxplayers; i++) {
                //decode players_onscreen: sets if "player" record is there to be read
                if (players_onscreen & (1 << i))
                    fx.player[i].onscreen = true;
                else {
                    fx.player[i].onscreen = false;
                    continue;
                }

                ClientPlayer& h = fx.player[i];

                //V0.3.9: took out screen reading, replacing for the same screen of "me"
                // that is set above
                h.roomx = fx.player[me].roomx;  //same screen since it's on the "players on same screen" vector
                h.roomy = fx.player[me].roomy;

                //coords & speeds
                NLubyte xy;

                NLushort hx, hy;
                readByte(data, count, xy);      //first 8 bits x
                hx = xy;
                readByte(data, count, xy);
                hy = xy;
                readByte(data, count, xy);
                hx += (xy & 0x0F) << 8;
                hy += (xy & 0xF0) << 4;
                h.lx = hx * (plw / double(0xFFF));
                h.ly = hy * (plh / double(0xFFF));

                typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
                NLubyte byte;
                readByte(data, count, byte);
                h.sx = SpeedType::toDouble(byte);
                readByte(data, count, byte);
                h.sy = SpeedType::toDouble(byte);

                //EXTRA BYTE
                NLubyte byt, extra;
                readByte(data, count, extra);           //extra byte

                //FLAGS BYTE
                h.dead = (extra & 1) != 0;  //DEAD PLAYER = extra bit 0
                h.item_deathbringer = (extra & 2) != 0;     //deathbringer: extra bit 1
                h.deathbringer_affected = (extra & 4) != 0; //deathbringer-affected: extra bit 2
                // ITEMS: movido para este byte
                h.item_shield = (extra & 8) != 0;
                h.item_turbo = (extra & 16) != 0;
                h.item_power = (extra & 32) != 0;

                NLubyte ccb;
                readByte(data, count, ccb);
                h.controls.fromNetwork(ccb, true);

                //bits 5..7 : gundir= 0..7
                h.gundir = ccb >> 5;

                //read shadow byte
                readByte(data, count, byt);
                h.visibility = byt;

                if (!h.dead && (i / TSIZE == me / TSIZE || h.visibility >= 10 || h.stats().has_flag()))
                    h.posUpdated = svframe;
            }

            for (int round = 0; round < 2; ++round) {
                //read who,x,y
                NLubyte who,whox,whoy;
                readByte(data, count, who);
                if (who == 255)
                    continue;
                readByte(data, count, whox);
                readByte(data, count, whoy);

                //update this player's px,py,x,y
                //ignore self and anybody onscreen -- because then I've got better accuracy
                if (who != me && !fx.player[who].onscreen) {
                    const int xmul = 255 / fx.map.w;
                    const int ymul = 255 / fx.map.h;
                    fx.player[who].roomx = whox / xmul;
                    fx.player[who].roomy = whoy / ymul;
                    fx.player[who].lx = (whox % xmul) * plw / (xmul - 1);
                    fx.player[who].ly = (whoy % ymul) * plh / (ymul - 1);
                    fx.player[who].posUpdated = svframe;
                }
            }

            //read player's health and energy
            NLubyte healt, energ;
            readByte(data, count, healt);
            if (me >= 0) {
                fx.player[me].health = healt;
                //EXTRA BIT FROM WAYY ABOVE
                if (xtra & 1) fx.player[me].health += 256;
            }

            readByte(data, count, energ);
            if (me >= 0) {
                fx.player[me].energy = energ;
                //EXTRA BIT FROM WAYY ABOVE
                if (xtra & 2) fx.player[me].energy += 256;
            }

            //read ping of player frame % MAX_PLAYERS
            NLushort daping;
            readShort(data, count, daping);
            fx.player[svframe % maxplayers].ping = daping;
        }//frame not empty
    }

    //(2) process messages (update fx, and add the non frame-related messages to messageQueue)
    for (;;) {
        char* lebuf;
        int msglen;
        lebuf = client->receive_message(&msglen);
        if (lebuf == 0)
            break;

        int count = 0;
        NLubyte code;
        readByte(lebuf, count, code);

        if (LOG_MESSAGE_TRAFFIC)
            log("Message from server, code = %i", code);

        //parse rest of message
        switch (static_cast<Network_data_code>(code)) {
        /*break;*/ case data_name_update: {
                NLubyte pid;
                string name;
                readByte(lebuf, count, pid);
                readStr(lebuf, count, name);
                if (check_name(name)) {
                    if (fx.player[pid].name.empty()) {
                        addThreadMessage(new TM_Text(msg_info, _("$1 entered the game.", name)));
                        addThreadMessage(new TM_Sound(SAMPLE_ENTERGAME));
                    }
                    else if (fx.player[pid].name != " ")    // " " is the case with players already in game when connecting
                        addThreadMessage(new TM_Text(msg_info, _("$1 changed name to $2.", fx.player[pid].name, name)));
                    fx.player[pid].name = name;
                }
                else
                    log.error("Invalid name for player " + itoa(pid) + '.');
            }

            break; case data_text_message: {
                char byte;
                readByte(lebuf, count, byte);
                const Message_type type = static_cast<Message_type>(byte);
                string chatmsg;
                readStr(lebuf, count, chatmsg);
                if (find_nonprintable_char(chatmsg)) {
                    log.error("Server sent non-printable characters in a message.");
                    addThreadMessage(new TM_DoDisconnect());
                    break;
                }
                // This is a kludge because of compatibility. Remove in version 1.1.
                // Make sure that the messages here match with the ones in server.cpp and servnet.cpp.
                if (type == msg_server) {
                    if (chatmsg == "Your vote has no effect until you vote for a specific map.") {
                        chatmsg = _("Your vote has no effect until you vote for a specific map.");
                        want_map_exit_delayed = true;
                    }
                    string::size_type pos = chatmsg.find(" decided it's time for a map change.");
                    if (pos != string::npos) {
                        const string name = chatmsg.substr(0, pos);
                        chatmsg = _("$1 decided it's time for a map change.", name);
                    }
                    pos = chatmsg.find(" decided it's time for a restart.");
                    if (pos != string::npos) {
                        const string name = chatmsg.substr(0, pos);
                        chatmsg = _("$1 decided it's time for a restart.", name);
                    }
                }
                addThreadMessage(new TM_Text(type, chatmsg));
                if (type == msg_team || type == msg_normal)
                    addThreadMessage(new TM_Sound(SAMPLE_TALK));
            }

            break; case data_first_packet: {
                NLubyte pid;
                readByte(lebuf, count, pid);    //"who am I"
                me = pid;

                NLubyte color;
                readByte(lebuf, count, color);
                if (color < MAX_PLAYERS / 2)
                    fx.player[pid].set_color(color);
                else
                    log("Invalid colour (%d) for player %d (me).", color, pid);

                NLubyte map_nr;
                readByte(lebuf, count, map_nr); //current map number
                current_map = map_nr;

                NLubyte score;
                readByte(lebuf, count, score);
                fx.teams[0].set_score(score);
                if (fx.teams[0].captures().size() == 0) // only if just joined the server
                    fx.teams[0].set_base_score(score);
                readByte(lebuf, count, score);
                fx.teams[1].set_score(score);
                if (fx.teams[1].captures().size() == 0) // only if just joined the server
                    fx.teams[1].set_base_score(score);

                // room is probably changed
                fx.player[me].oldx = -1;
                fx.player[me].oldy = -1;
            }

            break; case data_frags_update: {
                NLubyte pid;
                NLulong frags;
                readByte(lebuf, count, pid);
                readLong(lebuf, count, frags);
                fx.player[pid].stats().set_frags(frags);
                stable_sort(players_sb.begin(), players_sb.end(), compare_players);
            }

            break; case data_flag_update: {
                NLubyte team;
                NLbyte flags;
                readByte(lebuf, count, team);
                readByte(lebuf, count, flags);
                for (int i = 0; i < flags; i++) {
                    if (team == 2) {
                        if (i >= static_cast<int>(fx.wild_flags.size()))
                            fx.wild_flags.push_back(Flag(WorldCoords()));
                    }
                    else if (i >= static_cast<int>(fx.teams[team].flags().size()))
                        fx.teams[team].add_flag(WorldCoords());
                    NLubyte carried;
                    readByte(lebuf, count, carried);    // 0==not carried 1==carried
                    if (carried == 0) {
                        //not carried: update position
                        NLubyte px, py;
                        NLshort x, y;
                        readByte(lebuf, count, px);
                        readByte(lebuf, count, py);
                        readShort(lebuf, count, x);
                        readShort(lebuf, count, y);
                        if (team == 2) {
                            fx.wild_flags[i].move(WorldCoords(px, py, x, y));
                            fx.wild_flags[i].drop();
                        }
                        else
                            fx.teams[team].drop_flag(i, WorldCoords(px, py, x, y));
                    }
                    else {
                        //carried: get carrier
                        NLubyte carrier;
                        readByte(lebuf, count, carrier);
                        if (team == 2)
                            fx.wild_flags[i].take(carrier);
                        else
                            fx.teams[team].steal_flag(i, carrier);
                        addThreadMessage(new TM_Sound(SAMPLE_CTF_GOT));
                    }
                }
            }

            break; case data_rocket_fire: {
                if (!map_ready)
                    break;

                NLubyte rpow, rdir;
                NLubyte rids[16];
                NLulong frameno;
                NLubyte rteampower;

                readByte(lebuf, count, rpow);
                readByte(lebuf, count, rdir);
                for (int k = 0; k < rpow; k++)
                    readByte(lebuf, count, rids[k]);
                readLong(lebuf, count, frameno);    // frame # of shot
                readByte(lebuf, count, rteampower); // team (bit 1) and power (bit 0)
                const bool power = ((rteampower & 1) != 0);
                const int team = (rteampower & 2) >> 1;

                NLubyte rpx, rpy;
                NLshort rx, ry;
                readByte(lebuf, count, rpx);
                readByte(lebuf, count, rpy);
                numAssert4(rpx < fx.map.w && rpy < fx.map.h, rpx, fx.map.w, rpy, fx.map.h);
                readShort(lebuf, count, rx);
                readShort(lebuf, count, ry);

                ClientPhysicsCallbacks cb(*this);
                fx.shootRockets(cb, 0, rpow, rdir, rids, static_cast<int>(fx.frame - frameno), team, power, rpx, rpy, rx, ry);

                //play sound if rocket on screen
                if (me >= 0 && rpx == fx.player[me].roomx && rpy == fx.player[me].roomy)
                    if (power)
                        addThreadMessage(new TM_Sound(SAMPLE_POWER_FIRE));
                    else
                        addThreadMessage(new TM_Sound(SAMPLE_FIRE));
            }

            break; case data_old_rocket_visible: {
                if (!map_ready)
                    break;

                NLubyte rockid, rdir;
                NLulong frameno;
                NLubyte rteampower;
                readByte(lebuf, count, rockid);
                readByte(lebuf, count, rdir);
                readLong(lebuf, count, frameno);
                readByte(lebuf, count, rteampower);
                const bool power = ((rteampower & 1) != 0);
                const int team = (rteampower & 2) >> 1;

                NLubyte rpx, rpy;
                readByte(lebuf, count, rpx);
                readByte(lebuf, count, rpy);
                numAssert4(rpx < fx.map.w && rpy < fx.map.h, rpx, fx.map.w, rpy, fx.map.h);
                NLshort rx, ry;
                readShort(lebuf, count, rx);
                readShort(lebuf, count, ry);

                ClientPhysicsCallbacks cb(*this);
                fx.shootRockets(cb, 0, 1, rdir, &rockid, static_cast<int>(fx.frame - frameno), team, power, rpx, rpy, rx, ry);
                // no sound
            }

            break; case data_rocket_delete: {
                if (!map_ready)
                    break;

                NLubyte rockid, target;
                readByte(lebuf, count, rockid); // rocket object id
                readByte(lebuf, count, target); // target player
                //hit position
                NLshort rokx, roky;
                readShort(lebuf, count, rokx);
                readShort(lebuf, count, roky);
                fx.rock[rockid].owner = -1;
                if (target != 255) {    // hit player
                    if (target != 252)  // not shield hit -> blink player
                        fx.player[target].hitfx = get_time() + .3;
                    addThreadMessage(new TM_GunexploEffect((int)rokx, (int)roky, fx.rock[rockid].px, fx.rock[rockid].py));
                    addThreadMessage(new TM_Sound(SAMPLE_HIT));
                }
            }

            break; case data_power_collision: {
                NLubyte target;
                readByte(lebuf, count, target);
                fx.player[target].hitfx = get_time() + .3;
                addThreadMessage(new TM_Sound(client_sounds.sampleExists(SAMPLE_COLLISION_DAMAGE) ? SAMPLE_COLLISION_DAMAGE : SAMPLE_HIT));
            }

            break; case data_score_update: {
                NLubyte team;
                NLubyte score;
                readByte(lebuf, count, team);       //team
                readByte(lebuf, count, score);      //new score
                fx.teams[team].set_score(score);    // update the score
            }

            break; case data_sound: {
                NLubyte sample;
                readByte(lebuf, count, sample);     // sample #
                if (sample < NUM_OF_SAMPLES)
                    addThreadMessage(new TM_Sound(sample));
            }

            break; case data_pup_visible: {
                NLubyte iid, kind, spos;
                readByte(lebuf, count, iid);        // item id
                readByte(lebuf, count, kind);       // kind
                fx.item[iid].kind = static_cast<Powerup::Pup_type>(kind);
                readByte(lebuf, count, spos);       // screen x
                fx.item[iid].px = spos;
                readByte(lebuf, count, spos);       // screen y
                fx.item[iid].py = spos;
                NLushort coord;
                readShort(lebuf, count, coord);     // pos x
                fx.item[iid].x = coord;
                readShort(lebuf, count, coord);     // pos y
                fx.item[iid].y = coord;
            }

            break; case data_pup_picked: {
                NLubyte iid;
                readByte(lebuf, count, iid);
                fx.item[iid].kind = Powerup::pup_unused;        // no more!
            }

            break; case data_pup_timer: {
                NLubyte iid;
                NLushort time;
                readByte(lebuf, count, iid);    //kind
                readShort(lebuf, count, time);  //amount of time
                if (me >= 0) {
                    if (iid == Powerup::pup_turbo)
                        fx.player[me].item_turbo_time = get_time() + time;
                    else if (iid == Powerup::pup_shadow)
                        fx.player[me].item_shadow_time = get_time() + time;
                    else if (iid == Powerup::pup_power)
                        fx.player[me].item_power_time = get_time() + time;
                }
            }

            break; case data_weapon_change: {
                NLubyte level;
                readByte(lebuf, count, level);
                if (me >= 0)
                    fx.player[me].weapon = level;
            }

            break; case data_map_change: {
                map_ready = false;  // map NOT ready anymore: must load/change
                want_map_exit = false;      // and player does not want to exit the map anymore
                want_map_exit_delayed = false;

                // make sure the server knows that want_map_exit = false (in case data_map_exit_on was sent and not yet received when the data_map_change was sent)
                {
                    char lebuf[16]; int count = 0;
                    writeByte(lebuf, count, data_map_exit_off);
                    client->send_message(lebuf, count);
                }

                fx.teams[0].remove_flags();
                fx.teams[1].remove_flags();
                fx.wild_flags.clear();
                for (int i = 0; i < MAX_ROCKETS; ++i)
                    fx.rock[i].owner = -1;
                NLushort crc;
                readShort(lebuf, count, crc);
                string mapname, maptitle;
                readStr(lebuf, count, mapname);
                readStr(lebuf, count, maptitle);
                NLubyte map_nr, total_maps;
                readByte(lebuf, count, map_nr);
                readByte(lebuf, count, total_maps);
                current_map = map_nr;
                if (map_vote == current_map)
                    map_vote = -1;
                fx.player[me].oldx = -1;
                fx.player[me].oldy = -1;
                old_map = fx.map.title;
                if (count < msglen)
                    readByte(lebuf, count, remove_flags);
                else
                    remove_flags = 0;
                addThreadMessage(new TM_MapChange(mapname, crc));
                const string msg = _("This map is $1 ($2 of $3).", maptitle, itoa(current_map + 1), itoa(total_maps));
                addThreadMessage(new TM_Text(msg_info, msg));
            }

            break; case data_world_reset:
                for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
                    pi->stats().finish_stats(get_time());
                for (int iid = 0; iid < MAX_PICKUPS; ++iid)
                    fx.item[iid].kind = Powerup::pup_unused;
                for (int i = 0; i < MAX_ROCKETS; ++i)
                    fx.rock[i].owner = -1;

            break; case data_gameover_show: {
                NLubyte plaque;
                readByte(lebuf, count, plaque);
                if (plaque == NEXTMAP_CAPTURE_LIMIT || plaque == NEXTMAP_VOTE_EXIT) {
                    NLubyte score;
                    readByte(lebuf, count, score);  //RED team final score
                    red_final_score = score;
                    readByte(lebuf, count, score);  //BLUE team final score
                    blue_final_score = score;
                    NLubyte caplimit, timelimit;
                    readByte(lebuf, count, caplimit);
                    readByte(lebuf, count, timelimit);
                    gameover_plaque = plaque;

                    string msg = _("CTF GAME OVER - FINAL SCORE: RED $1 - BLUE $2", itoa(red_final_score), itoa(blue_final_score));
                    addThreadMessage(new TM_Text(msg_info, msg));
                    addThreadMessage(new TM_Sound(SAMPLE_CTF_GAMEOVER));
                    msg.clear();
                    if (caplimit > 0)
                        msg = _("CAPTURE $1 FLAGS TO WIN THE GAME.", itoa(caplimit));
                    if (timelimit > 0) {
                        if (!msg.empty())
                            msg += ' ';
                        msg += _("TIME LIMIT IS $1 MINUTES.", itoa(timelimit));
                    }
                    if (!msg.empty())
                        addThreadMessage(new TM_Text(msg_info, msg));
                    for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
                        pi->stats().finish_stats(get_time());
                }
                else {
                    gameover_plaque = NEXTMAP_NONE;
                    if (stats_autoshowing) {
                        menusel = menu_none;
                        stats_autoshowing = false;
                    }
                }
            }

            break; case data_start_game:
                gameover_plaque = NEXTMAP_NONE;     //hide
                fx.teams[0].clear_stats();
                fx.teams[1].clear_stats();
                for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
                    pi->stats().clear(true);
                if (stats_autoshowing) {
                    menusel = menu_none;
                    stats_autoshowing = false;
                }

            break; case data_deathbringer: {
                NLubyte team;
                NLulong frameno;
                NLubyte sx, sy;
                readByte(lebuf, count, team);
                readLong(lebuf, count, frameno);    // creation frame
                readByte(lebuf, count, sx);
                readByte(lebuf, count, sy);
                NLushort hx, hy;
                readShort(lebuf, count, hx);
                readShort(lebuf, count, hy);
                addThreadMessage(new TM_Deathbringer(team, get_time() + (frameno - fx.frame) * 0.1, hx, hy, sx, sy));
                addThreadMessage(new TM_Sound(SAMPLE_USEDEATHBRINGER));
            }

            break; case data_file_download: {
                NLubyte last;
                NLushort chunkSize;
                readShort(lebuf, count, chunkSize);     //chunk size
                readByte(lebuf, count, last);       //"last chunk"?
                process_udp_download_chunk(&lebuf[count], chunkSize, (last != 0));
            }

            break; case data_registration_response: {
                NLubyte response;
                readByte(lebuf, count, response);
                if (response == 1)  // success
                    tournamentPassword.serverAcceptsToken();
                else
                    tournamentPassword.serverRejectsToken();
            }

            break; case data_crap_update: {
                NLubyte pid, color, regStatus;
                NLulong prank, pscore, nscore;
                readByte(lebuf, count, pid);
                readByte(lebuf, count, color);
                readByte(lebuf, count, regStatus);
                readLong(lebuf, count, prank);      //ranking#
                readLong(lebuf, count, pscore);     //score
                readLong(lebuf, count, nscore);     //score NEG v0.4.8
                readLong(lebuf, count, max_world_rank);     //world players count
                if (color < MAX_PLAYERS / 2)
                    fx.player[pid].set_color(color);
                else
                    log("Invalid colour (%d) for player %d.", color, pid);
                ClientLoginStatus ls;
                ls.fromNetwork(regStatus);
                const ClientLoginStatus& os = fx.player[me].reg_status;
                const bool newMePrintout =
                    pid == me &&
                    (ls.token() != os.token() ||
                     (ls.token() && (ls.masterAuth() != os.masterAuth() || ls.tournament() != os.tournament())) ||
                     ls.localAuth() != os.localAuth() ||
                     ls.admin() != os.admin());
                if (newMePrintout) {
                    ostringstream msg;
                    msg << _("Status") << ": ";
                    if (ls.token()) {
                        if (ls.masterAuth()) {
                            msg << _("master authorized") << ", ";
                            if (ls.tournament())
                                msg << _("recording");
                            else
                                msg << _("not recording");
                        }
                        else {
                            msg << _("master auth pending") << ", ";
                            if (ls.tournament())
                                msg << _("will record");
                            else
                                msg << _("will not record");
                        }
                    }
                    else
                        msg << _("no tournament login");
                    if (ls.localAuth())
                        msg << "; " << _("locally authorized");
                    if (ls.admin())
                        msg << "; " << _("administrator");
                    addThreadMessage(new TM_Text(msg_info, msg.str()));
                }
                fx.player[pid].reg_status = ls;
                fx.player[pid].rank = static_cast<int>(prank);
                fx.player[pid].score = static_cast<int>(pscore);
                fx.player[pid].neg_score = static_cast<int>(nscore);
                // update new team powers
                double power[2] = { 0, 0 };
                for (int i = 0; i < fx.maxplayers; i++)
                    if (fx.player[i].used)
                        power[fx.player[i].team()] += (fx.player[i].score + 1.) / (fx.player[i].neg_score + 1.);
                for (int t = 0; t < 2; t++)
                    fx.teams[t].set_power(power[t]);
            }

            break; case data_map_time: {
                int current_time, time_left;
                readLong(lebuf, count, current_time);
                readLong(lebuf, count, time_left);
                map_start_time = static_cast<int>(get_time()) - current_time;
                if (time_left > 0) {
                    map_end_time = static_cast<int>(get_time()) + time_left;
                    map_time_limit = true;
                }
                else
                    map_time_limit = false;
                if (LOG_MESSAGE_TRAFFIC)
                    log("Map time received. Time left %d seconds.", time_left);
            }

            break; case data_reset_map_list: {
                MutexDebug md("mapInfoMutex", __LINE__, log);
                MutexLock ml(mapInfoMutex);
                maps.clear();
                map_vote = -1;
            }

            break; case data_current_map: {
                NLubyte mapNr;
                readByte(lebuf, count, mapNr);
                current_map = mapNr;
            }

            break; case data_map_list: {
                NLubyte width, height, votes;
                MapInfo mapinfo;
                readStr(lebuf, count, mapinfo.title);
                readStr(lebuf, count, mapinfo.author);
                readByte(lebuf, count, width);
                readByte(lebuf, count, height);
                readByte(lebuf, count, votes);
                mapinfo.width = width;
                mapinfo.height = height;
                mapinfo.votes = votes;
                const vector<string>::const_iterator mi = find(fav_maps.begin(), fav_maps.end(), toupper(mapinfo.title));
                if (mi != fav_maps.end())
                    mapinfo.highlight = true;
                MutexDebug md("mapInfoMutex", __LINE__, log);
                MutexLock ml(mapInfoMutex);
                maps.push_back(mapinfo);
            }

            break; case data_map_vote: {
                NLbyte map_nr;
                readByte(lebuf, count, map_nr);
                map_vote = map_nr;
            }

            break; case data_map_votes_update: {
                NLbyte total, map_nr, votes;
                readByte(lebuf, count, total);
                MutexDebug md("mapInfoMutex", __LINE__, log);
                MutexLock ml(mapInfoMutex);
                for (int i = 0; i < total; i++) {
                    readByte(lebuf, count, map_nr);
                    readByte(lebuf, count, votes);
                    if (map_nr >= 0 && map_nr < static_cast<int>(maps.size()))
                        maps[map_nr].votes = votes;
                }
            }

            break; case data_stats_ready: {
                if (menu.options.game.showStats() != Menu_game::SS_none && menusel == menu_none && openMenus.empty()) {
                    switch (menu.options.game.showStats()) {
                    /*break;*/ case Menu_game::SS_teams:   menusel = menu_teams;
                        break; case Menu_game::SS_players: menusel = menu_players;
                        break; default: nAssert(0);
                    }
                    stats_autoshowing = true;
                }
                for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
                    pi->stats().finish_stats(get_time());
                if (menu.options.game.saveStats())
                    fx.save_stats("client_stats", old_map);
            }

            break; case data_capture: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                const bool wild_flag = pid & 0x80;
                pid &= ~0x80;
                fx.player[pid].stats().add_capture(get_time());
                const int team = pid / TSIZE;
                fx.teams[team].add_score(get_time() - map_start_time, fx.player[pid].name);
                string msg;
                if (wild_flag)
                    msg = _("$1 CAPTURED THE WILD FLAG!", fx.player[pid].name);
                else if (1 - team == 0)
                    msg = _("$1 CAPTURED THE RED FLAG!", fx.player[pid].name);
                else
                    msg = _("$1 CAPTURED THE BLUE FLAG!", fx.player[pid].name);
                addThreadMessage(new TM_Text(msg_info, msg));
                addThreadMessage(new TM_Sound(SAMPLE_CTF_CAPTURE));
            }

            break; case data_kill: {
                NLubyte attacker, target;
                readByte(lebuf, count, attacker);
                readByte(lebuf, count, target);
                const DamageType cause = ((attacker & 0x80) ? DT_deathbringer : (target & 0x20) ? DT_collision : DT_rocket);
                //const bool carrier_defended = attacker & 0x40;
                //const bool flag_defended = attacker & 0x20;
                const bool flag = target & 0x80;
                const bool wild_flag = target & 0x40;
                attacker &= 0x1F;
                target &= 0x1F;
                const bool attacker_team = attacker / TSIZE;
                const bool target_team = target / TSIZE;
                const bool same_team = (attacker_team == target_team);
                const bool known_attacker = fx.player[attacker].used;
                string msg;
                if (cause == DT_deathbringer) {
                    if (!known_attacker)
                        msg = _("$1 was choked.", fx.player[target].name);
                    else if (same_team)
                        msg = _("$1 was choked by teammate $2.", fx.player[target].name, fx.player[attacker].name);
                    else
                        msg = _("$1 was choked by $2.", fx.player[target].name, fx.player[attacker].name);
                    if (fx.player[target].onscreen)
                        addThreadMessage(new TM_Sound(SAMPLE_DIEDEATHBRINGER));
                }
                else if (cause == DT_collision) {
                    if (!known_attacker)    // this should never happen with the current code, probably not in the future either, but it's still here...
                        msg = _("$1 received a mortal blow.", fx.player[target].name);
                    else if (same_team) // this shouldn't happen with the current special collisions either, but we're ready for changes
                        msg = _("$1 received a mortal blow from teammate $2.", fx.player[target].name, fx.player[attacker].name);
                    else
                        msg = _("$1 received a mortal blow from $2.", fx.player[target].name, fx.player[attacker].name);
                    if (fx.player[target].onscreen)
                        addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
                }
                else {
                    nAssert(cause == DT_rocket);
                    if (!known_attacker)    // this should never happen with the current code, but it's here for future
                        msg = _("$1 was nailed.", fx.player[target].name);
                    else if (same_team)
                        msg = _("$1 was nailed by teammate $2.", fx.player[target].name, fx.player[attacker].name);
                    else
                        msg = _("$1 was nailed by $2.", fx.player[target].name, fx.player[attacker].name);
                    if (fx.player[target].onscreen)
                        addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
                }
                addThreadMessage(new TM_Text(msg_info, msg));
                /*if (carrier_defended && known_attacker) {
                    if (attacker_team == 0)
                        msg = _("$1 defends the red carrier.", fx.player[attacker].name);
                    else
                        msg = _("$1 defends the blue carrier.", fx.player[attacker].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }
                if (flag_defended && known_attacker) {
                    if (attacker_team == 0)
                        msg = _("$1 defends the red flag.", fx.player[attacker].name);
                    else
                        msg = _("$1 defends the blue flag.", fx.player[attacker].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }*/
                if (fx.player[target].stats().current_cons_kills() >= 10) {
                    if (!known_attacker)
                        msg = _("$1's killing spree was ended.", fx.player[target].name);
                    else
                        msg = _("$1's killing spree was ended by $2.", fx.player[target].name, fx.player[attacker].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }
                if (!same_team) {
                    if (known_attacker)
                        fx.player[attacker].stats().add_kill(cause == DT_deathbringer);
                    fx.teams[attacker_team].add_kill();
                }
                fx.player[target].stats().add_death(cause == DT_deathbringer, static_cast<int>(get_time()));
                fx.teams[target_team].add_death();
                if (flag) {
                    if (!same_team && known_attacker)
                        fx.player[attacker].stats().add_carrier_kill();
                    fx.player[target].stats().add_flag_drop(get_time());
                    fx.teams[target_team].add_flag_drop();
                    if (wild_flag)
                        msg = _("$1 LOST THE WILD FLAG!", fx.player[target].name);
                    else if (1 - target_team == 0)
                        msg = _("$1 LOST THE RED FLAG!", fx.player[target].name);
                    else
                        msg = _("$1 LOST THE BLUE FLAG!", fx.player[target].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                    addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
                }
                if (!same_team && known_attacker && fx.player[attacker].stats().current_cons_kills() % 10 == 0) {
                    if (attacker == me)
                        addThreadMessage(new TM_Sound(SAMPLE_KILLING_SPREE));
                    msg = _("$1 is on a killing spree!", fx.player[attacker].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }
            }

            break; case data_flag_take: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                const bool wild_flag = pid & 0x80;
                pid &= ~0x80;
                fx.player[pid].stats().add_flag_take(get_time(), wild_flag);
                const int team = pid / TSIZE;
                fx.teams[team].add_flag_take();
                string msg;
                if (wild_flag)
                    msg = _("$1 GOT THE WILD FLAG!", fx.player[pid].name);
                else if (1 - team == 0)
                    msg = _("$1 GOT THE RED FLAG!", fx.player[pid].name);
                else
                    msg = _("$1 GOT THE BLUE FLAG!", fx.player[pid].name);
                addThreadMessage(new TM_Text(msg_info, msg));
            }

            break; case data_flag_return: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                fx.player[pid].stats().add_flag_return();
                fx.teams[pid / TSIZE].add_flag_return();
                string msg;
                if (pid / TSIZE == 0)
                    msg = _("$1 RETURNED THE RED FLAG!", fx.player[pid].name);
                else
                    msg = _("$1 RETURNED THE BLUE FLAG!", fx.player[pid].name);
                addThreadMessage(new TM_Text(msg_info, msg));
                addThreadMessage(new TM_Sound(SAMPLE_CTF_RETURN));
            }

            break; case data_flag_drop: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                const bool wild_flag = pid & 0x80;
                pid &= ~0x80;
                fx.player[pid].stats().add_flag_drop(get_time());
                const int team = pid / TSIZE;
                fx.teams[team].add_flag_drop();
                string msg;
                if (wild_flag)
                    msg = _("$1 DROPPED THE WILD FLAG!", fx.player[pid].name);
                else if (1 - team == 0)
                    msg = _("$1 DROPPED THE RED FLAG!", fx.player[pid].name);
                else
                    msg = _("$1 DROPPED THE BLUE FLAG!", fx.player[pid].name);
                addThreadMessage(new TM_Text(msg_info, msg));
                addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
            }

            break; case data_suicide: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                const bool flag = pid & 0x80;
                const bool wild_flag = pid & 0x40;
                pid &= ~0xC0;
                const int team = pid / TSIZE;
                if (fx.player[pid].stats().current_cons_kills() >= 10) {
                    const string msg = _("$1's killing spree was ended.", fx.player[pid].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }
                fx.player[pid].stats().add_suicide(static_cast<int>(get_time()));
                fx.teams[team].add_suicide();
                if (flag) {
                    fx.player[pid].stats().add_flag_drop(get_time());
                    fx.teams[team].add_flag_drop();
                    string msg;
                    if (wild_flag)
                        msg = _("$1 LOST THE WILD FLAG!", fx.player[pid].name);
                    else if (1 - team == 0)
                        msg = _("$1 LOST THE RED FLAG!", fx.player[pid].name);
                    else
                        msg = _("$1 LOST THE BLUE FLAG!", fx.player[pid].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                    addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
                }
                addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
            }

            break; case data_players_present: {    // this is only sent immediately after connecting to the server
                NLulong pp;
                readLong(lebuf, count, pp);
                for (int i = 0; i < MAX_PLAYERS; ++i) {
                    if (fx.player[i].used)  // this shouldn't happen except for i == me; either way, the player is already initialized
                        continue;
                    if (pp & (1 << i)) {
                        fx.player[i].clear(true, i, " ", i / TSIZE);  // hack... use " " for name to suppress announcement when the name is received
                        players_sb.push_back(&fx.player[i]);
                    }
                }
            }

            break; case data_new_player: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                nAssert(!fx.player[pid].used);
                fx.player[pid].clear(true, pid, "", pid / TSIZE);
                players_sb.push_back(&fx.player[pid]);
            }

            break; case data_player_left: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                const string msg = _("$1 left the game with $2 frags.", fx.player[pid].name, itoa(fx.player[pid].stats().frags()));
                addThreadMessage(new TM_Text(msg_info, msg));
                addThreadMessage(new TM_Sound(SAMPLE_LEFTGAME));
                vector<ClientPlayer*>::iterator rm = find(players_sb.begin(), players_sb.end(), &fx.player[pid]);
                nAssert(rm != players_sb.end());
                players_sb.erase(rm);
                nAssert(fx.player[pid].used);
                fx.player[pid].used = false;
            }

            break; case data_team_change: {
                NLubyte from, to, col1, col2;
                readByte(lebuf, count, from);
                readByte(lebuf, count, to);
                readByte(lebuf, count, col1);
                readByte(lebuf, count, col2);
                const bool swap = (col2 != 255);
                nAssert(fx.player[from].used && swap == fx.player[to].used);

                string msg;
                if (swap)
                    msg = _("$1 and $2 swapped teams.", fx.player[from].name, fx.player[to].name);
                else if (to / TSIZE == 0)
                    msg = _("$1 moved to red team.", fx.player[from].name);
                else
                    msg = _("$1 moved to blue team.", fx.player[from].name);
                addThreadMessage(new TM_Text(msg_info, msg));
                addThreadMessage(new TM_Sound(SAMPLE_CHANGETEAM));

                if (swap) {
                    std::swap(fx.player[from], fx.player[to]);
                    fx.player[from].id = from;
                    fx.player[to  ].id =   to;
                    fx.player[from].set_team(from / TSIZE);
                    fx.player[to  ].set_team(  to / TSIZE);
                    // both players already exist in players_sb -> no changes
                }
                else {
                    fx.player[to] = fx.player[from];
                    fx.player[from].used = false;
                    fx.player[to].id = to;
                    fx.player[to].set_team(to / TSIZE);
                    vector<ClientPlayer*>::iterator rm = find(players_sb.begin(), players_sb.end(), &fx.player[from]);
                    nAssert(rm != players_sb.end());
                    players_sb.erase(rm);
                    players_sb.push_back(&fx.player[to]);
                }

                if (from == me || to == me) {
                    want_change_teams = false;
                    me = (me == from) ? to : from;
                }

                if (col1 < MAX_PLAYERS / 2)
                    fx.player[to].set_color(col1);
                else
                    log("Invalid colour (%d) for player %d.", col1, to);
                fx.player[to].stats().kill(static_cast<int>(get_time()), true);
                fx.player[to].dead = true;  // this was already read from the frame data but overwritten by the team change
                if (swap) {
                    if (col2 < MAX_PLAYERS / 2)
                        fx.player[from].set_color(col2);
                    else
                        log("Invalid colour (%d) for player %d.", col2, from);
                    fx.player[from].stats().kill(static_cast<int>(get_time()), true);
                    fx.player[from].dead = true;    // this was already read from the frame data but overwritten by the team change
                }
            }

            break; case data_spawn: {
                NLubyte pid;
                readByte(lebuf, count, pid);
                fx.player[pid].stats().spawn(get_time());
                if (!fx.player[pid].onscreen)   // this information is after the spawn
                    fx.player[pid].posUpdated = 0;  // (probably) not seen in this life, if seen before spawning, not valid anymore
            }

            break; case data_team_movements_shots: {
                for (int i = 0; i < 2; i++) {
                    NLlong movement;
                    readLong(lebuf, count, movement);
                    fx.teams[i].set_movement(movement);
                    NLshort data;
                    readShort(lebuf, count, data);
                    fx.teams[i].set_shots(data);
                    readShort(lebuf, count, data);
                    fx.teams[i].set_hits(data);
                    readShort(lebuf, count, data);
                    fx.teams[i].set_shots_taken(data);
                }
            }

            break; case data_team_stats: {
                for (int i = 0; i < 2; i++) {
                    NLubyte data;
                    readByte(lebuf, count, data);
                    fx.teams[i].set_kills(data);
                    readByte(lebuf, count, data);
                    fx.teams[i].set_deaths(data);
                    readByte(lebuf, count, data);
                    fx.teams[i].set_suicides(data);
                    readByte(lebuf, count, data);
                    fx.teams[i].set_flags_taken(data);
                    readByte(lebuf, count, data);
                    fx.teams[i].set_flags_dropped(data);
                    readByte(lebuf, count, data);
                    fx.teams[i].set_flags_returned(data);
                }
            }

            break; case data_movements_shots: {
                NLubyte id;
                readByte(lebuf, count, id);
                // todo: check id
                NLlong movement;
                readLong(lebuf, count, movement);
                fx.player[id].stats().set_movement(movement);
                fx.player[id].stats().save_speed(get_time());
                NLshort data;
                readShort(lebuf, count, data);
                fx.player[id].stats().set_shots(data);
                readShort(lebuf, count, data);
                fx.player[id].stats().set_hits(data);
                readShort(lebuf, count, data);
                fx.player[id].stats().set_shots_taken(data);
            }

            break; case data_stats: {
                NLubyte id;
                readByte(lebuf, count, id);
                const bool flag = (id & 0x80);
                const bool wild_flag = (id & 0x40);
                const bool dead = (id & 0x20);
                id &= 0x1F;
                // todo: check id
                Statistics& stats = fx.player[id].stats();
                stats.set_flag(flag, wild_flag);
                stats.set_dead(dead);
                NLubyte data;
                readByte(lebuf, count, data);
                stats.set_kills(data);
                readByte(lebuf, count, data);
                stats.set_deaths(data);
                readByte(lebuf, count, data);
                stats.set_cons_kills(data);
                readByte(lebuf, count, data);
                stats.set_current_cons_kills(data);
                readByte(lebuf, count, data);
                stats.set_cons_deaths(data);
                readByte(lebuf, count, data);
                stats.set_current_cons_deaths(data);
                readByte(lebuf, count, data);
                stats.set_suicides(data);
                readByte(lebuf, count, data);
                stats.set_captures(data);
                readByte(lebuf, count, data);
                stats.set_flags_taken(data);
                readByte(lebuf, count, data);
                stats.set_flags_dropped(data);
                readByte(lebuf, count, data);
                stats.set_flags_returned(data);
                readByte(lebuf, count, data);
                stats.set_carriers_killed(data);
                int ldata;
                readLong(lebuf, count, ldata);
                stats.set_start_time(get_time() - ldata);
                readLong(lebuf, count, ldata);
                stats.set_lifetime(ldata);
                stats.set_spawn_time(get_time());
                readLong(lebuf, count, ldata);
                stats.set_flag_carrying_time(ldata);
                stats.set_flag_take_time(get_time());
            }

            break; case data_name_authorization_request:
                addThreadMessage(new TM_NameAuthorizationRequest());

            break; case data_server_settings: {
                NLubyte caplimit, timelimit, extratime;
                NLushort misc1, pupMin, pupMax, pupAddTime, pupMaxTime;
                readByte(lebuf, count, caplimit);
                readByte(lebuf, count, timelimit);
                readByte(lebuf, count, extratime);
                readShort(lebuf, count, misc1);
                readShort(lebuf, count, pupMin);
                readShort(lebuf, count, pupMax);
                readShort(lebuf, count, pupAddTime);
                readShort(lebuf, count, pupMaxTime);
                fx.physics.read(lebuf, count);
                fd.physics = fx.physics;

                log("Server friction/drag/acceleration %f/%f/%f",
                    fx.physics.fric, fx.physics.drag, fx.physics.accel);
                log("Server brake/turn/run/turbo/flag-modifier %f/%f/%f/%f/%f",
                    fx.physics.brake_mul, fx.physics.turn_mul, fx.physics.run_mul, fx.physics.turbo_mul, fx.physics.flag_mul);
                log("Server ff/dbff/rocketspeed %f/%f/%f",
                    fx.physics.friendly_fire, fx.physics.friendly_db, fx.physics.rocket_speed);

                ofstream out((wheregamedir + "log" + directory_separator + "physics.log").c_str());
                out << hostname << '\n';
                out << "friction     " << fx.physics.fric << '\n';
                out << "drag         " << fx.physics.drag << '\n';
                out << "acceleration " << fx.physics.accel << '\n';
                out << "brake_acceleration " << fx.physics.brake_mul << '\n';
                out << "turn_acceleration  " << fx.physics.turn_mul << '\n';
                out << "run_acceleration   " << fx.physics.run_mul << '\n';
                out << "turbo_acceleration " << fx.physics.turbo_mul << '\n';
                out << "flag_acceleration  " << fx.physics.flag_mul << '\n';
                out << "rocket_speed " << fx.physics.rocket_speed << '\n';
                out.close();

                addThreadMessage(new TM_ServerSettings(caplimit, timelimit, extratime, misc1, pupMin, pupMax, pupAddTime, pupMaxTime));
            }

            break; case data_5_min_left:
                addThreadMessage(new TM_Text(msg_info, _("*** Five minutes remaining")));
                addThreadMessage(new TM_Sound(SAMPLE_5_MIN_LEFT));

            break; case data_1_min_left:
                addThreadMessage(new TM_Text(msg_info, _("*** One minute remaining")));
                addThreadMessage(new TM_Sound(SAMPLE_1_MIN_LEFT));

            break; case data_30_s_left:
                addThreadMessage(new TM_Text(msg_info, _("*** 30 seconds remaining")));

            break; case data_time_out:
                addThreadMessage(new TM_Text(msg_info, _("*** Time out - CTF game over")));

            break; case data_extra_time_out:
                addThreadMessage(new TM_Text(msg_info, _("*** Extra-time out - CTF game over")));

            break; case data_normal_time_out: {
                NLubyte sudden_death;
                readByte(lebuf, count, sudden_death);
                string msg = _("*** Normal time out - extra-time started");
                if (sudden_death & 0x01)
                    msg += " " + _("(sudden death)");
                addThreadMessage(new TM_Text(msg_info, msg));
            }

            break; case data_map_change_info: {
                NLubyte votes, needed;
                NLshort vote_block_time;
                readByte(lebuf, count, votes);
                readByte(lebuf, count, needed);
                readShort(lebuf, count, vote_block_time);
                string msg = _("*** $1/$2 votes for mapchange.", itoa(votes), itoa(needed));
                if (vote_block_time > 0)
                    msg += ' ' + _("(All players needed for $1 more seconds.)", itoa(vote_block_time));
                addThreadMessage(new TM_Text(msg_info, msg));
            }

            break; case data_too_much_talk:
                addThreadMessage(new TM_Text(msg_warning, _("Too much talk. Chill...")));

            break; case data_mute_notification:
                addThreadMessage(new TM_Text(msg_warning, _("You are muted. You can't send messages.")));

            break; case data_tournament_update_failed:
                addThreadMessage(new TM_Text(msg_warning, _("Updating your tournament score failed!")));

            break; case data_player_mute: {
                NLubyte pid, mode;
                string admin;
                readByte(lebuf, count, pid);
                readByte(lebuf, count, mode);
                readStr(lebuf, count, admin);
                if (admin.empty())
                    admin = _("The admin");
                if (pid == me) {
                    string msg;
                    if (mode == 0)
                        msg = _("You have been unmuted (you can send messages again).");
                    else if (mode == 1)
                        msg = _("You have been muted by $1 (you can't send messages).", admin);
                    else
                        nAssert(0);     // The silent mute should not be known by the muted player.
                    addThreadMessage(new TM_Text(msg_warning, msg));
                }
                else {
                    string msg;
                    if (mode == 0)
                        msg = _("$1 has unmuted $2.", admin, fx.player[pid].name);
                    else
                        msg = _("$1 has muted $2.", admin, fx.player[pid].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }
            }

            break; case data_player_kick: {
                NLubyte pid;
                NLlong minutes;
                string admin;
                readByte(lebuf, count, pid);
                readLong(lebuf, count, minutes);
                readStr(lebuf, count, admin);
                if (admin.empty())
                    admin = _("The admin");
                if (pid == me) {
                    string msg;
                    if (minutes == 0)
                        msg = _("You are being kicked from this server by $1!", admin);
                    else
                        msg = _("$1 has BANNED you from this server for $2!", admin, approxTime(minutes * 60));
                    addThreadMessage(new TM_Text(msg_warning, msg));
                }
                else {
                    string msg;
                    if (minutes == 0)
                        msg = _("$1 has kicked $2 (disconnect in 10 seconds).", admin, fx.player[pid].name);
                    else
                        msg = _("$1 has banned $2 (disconnect in 10 seconds).", admin, fx.player[pid].name);
                    addThreadMessage(new TM_Text(msg_info, msg));
                }
            }

            break; case data_disconnecting: {
                NLubyte time;
                readByte(lebuf, count, time);
                const string msg = _("Disconnecting in $1...", itoa(time));
                addThreadMessage(new TM_Text(msg_warning, msg));
            }

            break; case data_idlekick_warning: {
                NLubyte time;
                readByte(lebuf, count, time);
                const string msg = _("*** Idle kick: move or be kicked in $1 seconds.", itoa(time));
                addThreadMessage(new TM_Text(msg_warning, msg));
            }

            break; case data_broken_map:
                addThreadMessage(new TM_Text(msg_warning, _("This map is broken. There is an instantly capturable flag. Avoid it.")));

            break; default:
                if (code < data_reserved_range_first || code > data_reserved_range_last) {
                    log.error("Server sent an unknown message code: " + itoa(code) + ", length " + itoa(msglen));
                    addThreadMessage(new TM_DoDisconnect());
                    return; // don't process the rest of the messages
                }
                // just ignore commands in reserved range: they're probably some extension we don't have to care about
        }
    }
}

//send chat message
void Client::send_chat(const string& msg) {
    if (msg.empty() || msg == "." || isFlood(msg))
        return;
    char lebuf[256]; int count = 0;
    writeByte(lebuf, count, data_text_message);
    writeStr(lebuf, count, msg);
    client->send_message(lebuf, count);
}

//print message to "console"
void Client::print_message(Message_type type, const string& msg) {
    if (menu.options.game.messageLogging() != Menu_game::ML_none) {
        if (menu.options.game.messageLogging() == Menu_game::ML_full || type == msg_normal || type == msg_team)
            message_log << date_and_time() << "  " << msg << endl;
    }

    bool highlight = false;
    if (type == msg_normal || type == msg_team) {
        const string uppercase = toupper(msg);
        for (vector<string>::const_iterator hi = highlight_text.begin(); hi != highlight_text.end(); ++hi)
            if (uppercase.find(*hi) != string::npos) {
                highlight = true;
                break;
            }
    }
    const vector<string> lines = split_to_lines(msg, 79, 4);
    while (chatbuffer.size() > client_graphics.chat_max_lines() + lines.size())
        chatbuffer.pop_front();
    for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li) {
        Message message(type, *li, static_cast<int>(get_time()));
        if (highlight)
            message.highlight();
        chatbuffer.push_back(message);
    }
}

void Client::save_screenshot() {
    string filename;
    for (int i = 0; i < 1000; i++) {
        // filename: screens/outgxxx.pcx
        ostringstream fname;
        fname << wheregamedir << "screens" << directory_separator;
        fname << "outg" << setfill('0') << setw(3) << i << ".pcx";
        if (!file_exists(fname.str().c_str(), FA_ARCH | FA_DIREC | FA_HIDDEN | FA_RDONLY | FA_SYSTEM, 0)) {
            filename = fname.str();
            break;
        }
    }

    string message;
    if (client_graphics.save_screenshot(filename))
        message = _("Saved screenshot to $1.", filename);
    else
        message = _("Could not save screenshot to $1.", filename);
    print_message(msg_warning, message);
}

//toggle help screen
void Client::toggle_help() {
    if (openMenus.safeTop() == &menu.help.menu)
        openMenus.close();
    else
        showMenu(menu.help);
}

void Client::handlePendingThreadMessages() {    // should only be called by the main thread
    while (!messageQueue.empty()) {
        ThreadMessage* msg = messageQueue.front();
        messageQueue.pop_front();
        msg->execute(this);
        delete msg;
    }
}

string Client::refreshStatusAsString() const {
    switch (refreshStatus) {
    /*break;*/ case RS_none:       return _("Inactive");
        break; case RS_running:    return _("Running");
        break; case RS_failed:     return _("Failed");
        break; case RS_contacting: return _("Contacting the servers...");
        break; case RS_connecting: return _("Getting server list: connecting...");
        break; case RS_receiving:  return _("Getting server list: receiving...");
        break; default: nAssert(0);
    }
    nAssert(0); return 0;
}

void Client::getServerListThread() {
    logThreadStart("getServerListThread", log);

    nAssert(refreshStatus == RS_running);

    // get server list and refresh
    bool ok = getServerList();
    if (!abortThreads)
        if (!refresh_all_servers())
            ok = false;

    logThreadExit("getServerListThread", log);
    refreshStatus = ok ? RS_none : RS_failed;
}

void Client::refreshThread() {
    logThreadStart("refreshThread", log);

    nAssert(refreshStatus == RS_running);
    const bool ok = refresh_all_servers();

    logThreadExit("refreshThread", log);
    refreshStatus = ok ? RS_none : RS_failed;
}

//refresh servers command
bool Client::refresh_all_servers() {
    bool ok = refresh_servers(gamespy);
    if (!abortThreads && !menu.connect.favorites())
        if (!refresh_servers(mgamespy))
            ok = false;
    return ok;
}

class TempPingData {    // internal to Client::refresh_servers
    double st[4];   // send time
    int rc;         // count of received packets
    double rt;      // sum of pings (for averaging)

public:
    TempPingData() : rc(0), rt(0) { }
    void send(int pack) { g_timeCounter.refresh(); st[pack] = get_time(); }
    void receive(int pack) { g_timeCounter.refresh(); ++rc; rt += get_time() - st[pack]; }
    int received() const { return rc; }
    int ping() const { return static_cast<int>(1000 * rt / rc); }
};

//refresh servers command
bool Client::refresh_servers(vector<ServerListEntry>& gamespy) {
    refreshStatus = RS_contacting;

    MutexDebug::lock("serverListMutex", __LINE__, log);
    serverListMutex.lock();

    const int nServers = static_cast<int>(gamespy.size());
    vector<TempPingData> tempd(nServers);
    int pending = nServers;         // count of valid entries still waiting for a response

    for (int i = 0; i < nServers; i++) {
        gamespy[i].refreshed = true;
        gamespy[i].ping = 0;
    }

    serverListMutex.unlock();
    MutexDebug::unlock("serverListMutex", __LINE__, log);

    if (pending == 0)
        return true;

    nlOpenMutex.lock();
    nlDisable(NL_BLOCKING_IO);
    NLsocket sock = nlOpen(0, NL_UNRELIABLE);
    nlOpenMutex.unlock();

    if (sock == NL_INVALID) {
        log.error(_("Can't open socket for refreshing servers. $1", getNlErrorString()));
        return false;
    }

    char lebuf[512];

    for (int round = 0; round < 20; round++) {  // each round takes .1 seconds
        if (abortThreads) {
            log("Refreshing servers aborted: client exiting.");
            nlClose(sock);
            return false;
        }

        if (round < 4) {    // on first 4 rounds, packets are sent to each server
            MutexDebug md("serverListMutex", __LINE__, log);
            MutexLock ml(serverListMutex);
            for (int i = 0; i < nServers; i++) {
                int count = 0;
                writeLong(lebuf, count, 0);         //special packet
                writeLong(lebuf, count, 200);       //serverinfo request
                writeByte(lebuf, count, (NLubyte)i);        //connect entry (am I lazy or what)
                writeByte(lebuf, count, (NLubyte)round);        //packet number

                nlSetRemoteAddr(sock, &gamespy[i].address());
                nlWrite(sock, lebuf, count);
                tempd[i].send(round);
            }
        }

        if (pending == 0)
            break;

        // parse received responses
        for (int subRound = 0; subRound < 20; subRound++) {
            platSleep(5);

            for (;;) {  // continue while there are new packets
                int len = nlRead(sock, lebuf, 512);
                if (len <= 0)
                    break;

                int count = 0;
                NLulong dw1, dw2;
                readLong(lebuf, count, dw1);
                readLong(lebuf, count, dw2);
                if (dw1 != 0 || dw2 != 200)
                    continue;

                NLubyte index, pack;
                readByte(lebuf, count, index);  // gamespy entry number echoed by the server
                readByte(lebuf, count, pack);   // packet #

                if (index >= nServers || pack >= 4 || pack > round || len < count)  // don't have to worry about < 0 because they're unsigned
                    continue;

                MutexDebug md("serverListMutex", __LINE__, log);
                MutexLock ml(serverListMutex);

                NLaddress from;
                nlGetRemoteAddr(sock, &from);
                if (!nlAddrCompare(&from, &gamespy[index].address()))
                    continue;

                readStr(lebuf, count, gamespy[index].info);

                if (tempd[index].received() == 0)   // first reply -> server has changed to being valid
                    pending--;

                tempd[index].receive(pack);
                gamespy[index].ping = tempd[index].ping();

                gamespy[index].noresponse = false;  // set here in advance so that the main thread will already show it
            }
        }
    }

    // mark those that got no responses
    {
        MutexDebug md("serverListMutex", __LINE__, log);
        MutexLock ml(serverListMutex);
        for (int i = 0; i < nServers; i++)
            if (tempd[i].received() == 0)
                gamespy[i].noresponse = true;
    }

    nlClose(sock);
    return true;
}

bool Client::getServerList() {
    refreshStatus = RS_connecting;

    //open a nonblocking socket
    nlOpenMutex.lock();
    nlDisable(NL_BLOCKING_IO);
    NLsocket sock = nlOpen(0, NL_RELIABLE);
    nlOpenMutex.unlock();
    if (sock == NL_INVALID) {
        log.error(_("Can't open socket to connect to master server. $1", getNlErrorString()));
        return false;
    }

    //connect the nonblocking way
    if (nlConnect(sock, &g_masterSettings.address()) == NL_FALSE) {
        log.error(_("Can't connect to master server. $1", getNlErrorString()));
        nlClose(sock);
        sock = NL_INVALID;
        return false;
    }

    //build query
    ostringstream request;
    request << "GET " << g_masterSettings.query() << "?simple&branch=" << url_encode(GAME_BRANCH) << "&master=" << itoa(g_masterSettings.crc())
            << "&protocol=" << url_encode(GAME_PROTOCOL) << " HTTP/1.0\r\n";
    request << "User-Agent: " << GAME_STRING << '/' << GAME_BRANCH << ' ' << GAME_VERSION << "\r\n";
    request << "Connection: close\r\n\r\n";

    NetworkResult result = writeToUnblockingTCP(sock, request.str().data(), request.str().length(), &abortThreads, 30000);
    if (result != NR_ok) {
        nlClose(sock);
        if (!abortThreads)
            log("Client can't connect to master server. %s", result == NR_timeout ? "Timeout" : getNlErrorString());
        return false;
    }

    refreshStatus = RS_receiving;

    log("Successfully sent query to master: '%s'", formatForLogging(request.str()).c_str());

    std::stringstream response;
    result = saveAllFromUnblockingTCP(sock, response, &abortThreads, 30000);
    nlClose(sock);
    if (result != NR_ok) {
        if (!abortThreads)
            log("Error receiving server list from master. %s", result == NR_timeout ? "Timeout" : getNlErrorString());
        return false;
    }

    log("Full response: '%s'", formatForLogging(response.str()).c_str());

    if (parseServerList(response))
        return true;
    else {
        log.error(_("Incorrect data received from master server."));
        return false;
    }
}

bool Client::parseServerList(istream& response) {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();

    string line, empty;

    // Skip HTTP headers.
    while (getline(response, line, '\r') && getline(response, empty, '\n'))
        if (line.empty())
            break;

    // The first line is the newest version.
    getline_smart(response, line);
    if (line.empty())
        return false;
    if (line != GAME_VERSION)
        menu.newVersion.set(_("New version: $1", line));

    // The second line is the number of lines in the new master.txt, or 0 if there is no new master.txt.
    getline_smart(response, line);
    int masterTxtLen;
    {
        istringstream is(line);
        is >> masterTxtLen;
        if (!is || is.peek() != eof_ch || masterTxtLen < 0 || masterTxtLen > 30)
            return false;
    }
    if (masterTxtLen != 0) {
        vector<string> masterTxt;
        for (int i = 0; i < masterTxtLen; ++i) {
            getline_smart(response, line);
            masterTxt.push_back(line);
        }
        if (!getline_smart(response, line) || line != "--- end")
            return false;
        ofstream os((wheregamedir + "config" + directory_separator + "master.txt").c_str());
        if (os) {
            for (vector<string>::const_iterator li = masterTxt.begin(); li != masterTxt.end(); ++li)
                os << *li << '\n';
            const bool err = !os;
            os.close();
            if (err)    // try to remove the failed file and therefore return to default settings (which may be different from what were previously, but what can we do)
                delete_file((wheregamedir + "config" + directory_separator + "master.txt").c_str());
            else
                g_masterSettings.load(log);
        }
    }

    // After master.txt is the total number of servers.
    getline_smart(response, line);
    istringstream is(line);
    int total_servers;
    is >> total_servers;
    if (!is || is.peek() != eof_ch || total_servers < 0 || total_servers > 10000)
        return false;

    MutexDebug md("serverListMutex", __LINE__, log);
    MutexLock ml(serverListMutex);

    // Parse the successful response into the gamespy screen.

    mgamespy.clear();

    int servers_read;
    for (servers_read = 0; getline_smart(response, line); servers_read++) {
        ServerListEntry spy;
        if (spy.setAddress(line))
            mgamespy.push_back(spy);
        else
            return false;
    }

    return (servers_read == total_servers);
}

void Client::handleKeypress(int sc, int ch, bool withControl, bool alt_sequence) {  // sc = scancode, ch = character, as returned by readkey
    // handle global keys first
    bool handled = true;
    switch (sc) {   // if the key isn't handled, set handled = false
    /*break;*/ case KEY_F1:
            toggle_help();
        break; case KEY_F11:
            screenshot = true;
        break; case KEY_F5:
            if (openMenus.safeTop() == &m_serverInfo.menu)
                openMenus.close();
            else if (connected)
                showMenu(m_serverInfo);
            stats_autoshowing = false;
        break; case KEY_F3:
            if (withControl)
                menu.options.game.showNames.toggle();
            else
                handled = false;
        break; case KEY_ENTER:
            if (ch == 0) {  // Alt+Enter
                if (get_time() > lastAltEnterTime + .5) {
                    menu.options.screenMode.windowed.toggle();
                    screenModeChange(); // ignore error
                    lastAltEnterTime = get_time();
                }
            }
            else
                handled = false;
        break; default:
            handled = false;
    }
    if (handled)
        return;
    if (!openMenus.empty()) {
        MutexLock ml(frameMutex);   // some menus need access
        if (!openMenus.handleKeypress(sc, ch)) {
            if (sc == KEY_ESC)
                MCF_menuCloser();
        }
        return;
    }
    handled = true;
    switch (sc) {
    /*break;*/ case KEY_ESC:
            if (menusel != menu_none) {
                menusel = menu_none;
                stats_autoshowing = false;
            }
            else if (!talkbuffer.empty()) // cancel chat
                talkbuffer.clear();
            else
                showMenu(menu);
        break; case KEY_F2:
            menusel = (menusel == menu_maps ? menu_none : menu_maps);
            stats_autoshowing = false;
            if (menusel == menu_maps) {
                load_fav_maps();
                apply_fav_maps();
            }
        break; case KEY_F3:
            menusel = (menusel == menu_teams ? menu_none : menu_teams);
            stats_autoshowing = false;
        break; case KEY_F4:
            menusel = (menusel == menu_players ? menu_none : menu_players);
            stats_autoshowing = false;
        break; case KEY_F8: {
            want_map_exit = !want_map_exit;
            want_map_exit_delayed = false;

            char lebuf[16]; int count = 0;
            if (want_map_exit)
                writeByte(lebuf, count, data_map_exit_on);
            else
                writeByte(lebuf, count, data_map_exit_off);
            client->send_message(lebuf, count);
        }
        break; default:
            handled = false;
    }
    if (handled)
        return;
    if (menusel != menu_none)
        if (handleInfoScreenKeypress(sc, ch, withControl, alt_sequence))
            return;
    handleGameKeypress(sc, ch, withControl, alt_sequence);
}

bool Client::handleInfoScreenKeypress(int sc, int ch, bool withControl, bool alt_sequence) {  // sc = scancode, ch = character, as returned by readkey
    (void)(withControl&alt_sequence);
    if (menu.options.controls.arrowKeysInStats() != Menu_controls::AS_useMenu && (sc == KEY_UP || sc == KEY_DOWN || sc == KEY_LEFT || sc == KEY_RIGHT))
        return false;
    switch (menusel) {
    /*break;*/ case menu_maps:
            switch (sc) {
            /*break;*/ case KEY_UP:     client_graphics.map_list_prev();
                break; case KEY_DOWN:   client_graphics.map_list_next();
                break; case KEY_PGUP:   client_graphics.map_list_prev_page();
                break; case KEY_PGDN:   client_graphics.map_list_next_page();
                break; case KEY_HOME:   client_graphics.map_list_begin();
                break; case KEY_END:    client_graphics.map_list_end();
                break; case KEY_BACKSPACE:
                    if (!edit_map_vote.empty())
                        edit_map_vote.erase(edit_map_vote.end() - 1);
                break; case KEY_ENTER: case KEY_ENTER_PAD: {
                    int new_vote = atoi(edit_map_vote) - 1;
                    if (new_vote >= 255)
                        new_vote = -1;
                    edit_map_vote.clear();
                    if (new_vote != map_vote && (new_vote >= 0 || map_vote >= 0)) {
                        map_vote = new_vote;
                        want_map_exit_delayed = false;
                        // send map vote
                        char lebuf[16];
                        int count = 0;
                        writeByte(lebuf, count, data_map_vote);
                        writeByte(lebuf, count, map_vote);
                        client->send_message(lebuf, count);
                    }
                }
                break; default:
                    if (!isdigit(ch))
                        return false;
                    if (edit_map_vote.size() < 3)
                        edit_map_vote += ch;
            }
            return true;
        break; case menu_players:
            if (sc == KEY_UP || sc == KEY_LEFT || sc == KEY_PGUP)
                player_stats_page = max(0, player_stats_page - 1);
            else if (sc == KEY_DOWN || sc == KEY_RIGHT || sc == KEY_PGDN)
                player_stats_page = min(3, player_stats_page + 1);
            else if (sc == KEY_TAB)
                player_stats_page = (player_stats_page + (key[KEY_LSHIFT] || key[KEY_RSHIFT] ? -1 + 4 : +1)) % 4;
            else
                return false;
            return true;
        break; case menu_teams:
            if (sc == KEY_UP || sc == KEY_PGUP)
                client_graphics.team_captures_prev();
            else if (sc == KEY_DOWN || sc == KEY_PGDN)
                client_graphics.team_captures_next();
            else
                return false;
            return true;
        break; case menu_none: // regular menu, if any, handled elsewhere
            return false;
        break; default:
            nAssert(0);
            return false;
    }
}

void Client::handleGameKeypress(int sc, int ch, bool withControl, bool alt_sequence) {  // sc = scancode, ch = character, as returned by readkey
    switch (sc) {
    /*break;*/ case KEY_HOME:   // change colours
            if (withControl)
                client_graphics.reset_playground_colors();
            else
                client_graphics.random_playground_colors();
            predrawNeeded = true;
        break; case KEY_INSERT:
            show_all_messages = !show_all_messages;
        break; case KEY_BACKSPACE:
            if (!talkbuffer.empty())
                talkbuffer.erase(talkbuffer.end() - 1);
        break; case KEY_ENTER: case KEY_ENTER_PAD:
            if (!talkbuffer.empty()) {
                send_chat(trim(talkbuffer));
                talkbuffer.clear();
            }
        break; case KEY_DEL: {
            char lebuf[16]; int count = 0;
            writeByte(lebuf, count, data_suicide);
            client->send_message(lebuf, count);
        }
        break; case KEY_END: {
            want_change_teams = !want_change_teams;

            char lebuf[16]; int count = 0;
            writeByte(lebuf, count, want_change_teams ? data_change_team_on : data_change_team_off);
            client->send_message(lebuf, count);
        }
        break; case KEY_TAB:    // Prevent annoying Control+Tab character.
        break; case KEY_PLUS_PAD:
            if (key[KEY_P])
                print_message(msg_info, "Ping +" + itoa(iround(client->increasePacketDelay() * 1000)));
        break; case KEY_MINUS_PAD:
            if (key[KEY_P])
                print_message(msg_info, "Ping +" + itoa(iround(client->decreasePacketDelay() * 1000)));
        break; default:
            // Add character to text
            if (talkbuffer.length() < max_chat_message_length && !is_nonprintable_char(ch) &&
                    (!menu.options.controls.keypadMoving() || (!is_keypad(sc) && !alt_sequence)))
                talkbuffer += static_cast<char>(ch);
    }
}

void Client::loop(volatile bool* quitFlag, bool firstTimeSplash) {
    nAssert(quitFlag);
    quitCommand = false;

    menusel = menu_none;
    openMenus.clear();
    if (firstTimeSplash) {
        menu.options.bugReports.policy.set(ABR_minimal);
        showMenu(menu.options.bugReports);
    }
    else
        showMenu(menu);
    gameshow = false;

    g_timeCounter.refresh();
    double nextSend = get_time();
    double nextClientFrame = get_time();

    bool prevFire = false, prevDropFlag = false;
    while (!quitCommand && !*quitFlag) {
        // (1) loop doing input/sleep before next send or draw time
        for (;;) {
            const bool controlPressed = (key[KEY_LCONTROL] || key[KEY_RCONTROL]);

            //quit key Control-F12
            if (controlPressed && key[KEY_F12]) {
                quitCommand = true;
                break;
            }

            static bool alt_sequence = false;

            if (keyboard_needs_poll())
                poll_keyboard();    // ignore return value

            if (menu.options.controls.keypadMoving()) {
                // Check Alt+keypad sequences
                if (key_shifts & KB_INALTSEQ_FLAG)
                    alt_sequence = true;
            }

            // handle waiting keypresses
            while (keypressed()) {
                int ch = readkey();
                handleKeypress(ch >> 8, ch & 0xFF, controlPressed, alt_sequence);
            }

            if (!(key_shifts & KB_INALTSEQ_FLAG))
                alt_sequence = false;

            // handle current keypresses (only used in game)
            if (openMenus.empty()) {
                bool sendnow = false;

                // control == fire
                const bool fire = controlPressed || (menu.options.controls.joystick() && readJoystickButton(menu.options.controls.joyShoot()));
                if (fire != prevFire) {
                    prevFire = fire;

                    char lebuf[16]; int count = 0;
                    writeByte(lebuf, count, fire ? data_fire_on : data_fire_off);
                    client->send_message(lebuf, count);

                    //send early keys packet
                    sendnow = true;
                }

                if (menusel == menu_none) { // page down is reserved for stats screens
                    const bool dropFlag = key[KEY_PGDN];
                    if (dropFlag != prevDropFlag) {
                        prevDropFlag = dropFlag;
                        char lebuf[16]; int count = 0;
                        writeByte(lebuf, count, dropFlag ? data_drop_flag : data_stop_drop_flag);
                        client->send_message(lebuf, count);
                    }
                }

                send_frame(false, sendnow);
            }

            while (clientReadiesWaiting > 1 ||
                   (clientReadiesWaiting && (!menu.options.game.stayDead() ||
                                             (openMenus.empty() && menusel == menu_none)))) {
                send_client_ready();
                --clientReadiesWaiting;
            }

            {
                MutexLock ml(frameMutex);
                handlePendingThreadMessages();

                if (GlobalDisplaySwitchHook::readAndClear() && menu.options.screenMode.flipping()) {
                    client_graphics.videoMemoryCorrupted();
                    predraw();
                }
            }

            g_timeCounter.refresh();
            if (get_time() >= nextSend || get_time() >= nextClientFrame)
                break;

            platSleep(2);
        }

        if (get_time() >= nextSend) {
            nextSend += .1; // match 10 Hz frame frequency of server
            #ifdef SEND_FRAMEOFFSET
            nextSend += netsendAdjustment;
            netsendAdjustment = 0;  // losing a value due to concurrency is vaguely possible but affordable
            #endif
            if (get_time() > nextSend)   // don't accumulate lag
                nextSend = get_time();
            send_frame(true, true);
        }

        if (get_time() < nextClientFrame)
            continue;

        // give other threads a chance (otherwise we're trying to run all the time if the FPS limit is not lower than what the machine can do)
        sched_yield();

        int fpsLimit = menu.options.graphics.fpsLimit();
        if (!gameshow && fpsLimit > 30)
            fpsLimit = 30;
        nextClientFrame += 1. / fpsLimit;
        if (get_time() > nextClientFrame)    // don't accumulate lag
            nextClientFrame = get_time();

        // the rest is drawing

        if (gameshow) {
            MutexDebug md("frameMutex", __LINE__, log);
            MutexLock ml(frameMutex);

            ClientPhysicsCallbacks cb(*this);
            if (menu.options.game.lagPrediction()) {
                const double lagWanted = 2. * (1. - menu.options.game.lagPredictionAmount() / 10.); // lagPredictionAmount() is in range [0, 10]
                double timeDelta = max<double>(0., averageLag - lagWanted) + (get_time() - frameReceiveTime) * 10.;
                NLubyte firstFrame, lastFrame;
                if (clFrameSent == clFrameWorld)
                    firstFrame = lastFrame = clFrameWorld;
                else {
                    firstFrame = lastFrame = clFrameWorld + 1;
                    while (lastFrame != clFrameSent && timeDelta > 1.) {
                        ++lastFrame;
                        timeDelta -= 1.;
                    }
                }
                if (timeDelta > 3.)
                    timeDelta = 3.;
                fd.extrapolate(fx, cb, me, controlHistory, firstFrame, lastFrame, timeDelta);
            }
            else
                fd.extrapolate(fx, cb, me, controlHistory, clFrameWorld, clFrameWorld, (get_time() - frameReceiveTime) * 10.);

            if (mapChanged) {
                mapChanged = false;
                client_graphics.update_minimap_background(fx.map);
                predrawNeeded = true;
            }
            if (predrawNeeded) {
                predrawNeeded = false;
                predraw();
            }

            client_graphics.startDraw();
            draw_game_frame();

            #ifdef ROOM_CHANGE_BENCHMARK
            if (benchmarkRuns >= 500)
                quitCommand = true;
            #endif
        } else {
            client_graphics.startDraw();
            client_graphics.clear();
            if (!gameshow && openMenus.empty())
                showMenu(menu);
        }

        const int errors = externalErrorLog.size();
        if (errors) {
            for (int count = 0; count < errors; ++count)
                m_errors.wrapLine(externalErrorLog.pop());
            if (openMenus.safeTop() != &m_errors.menu)
                showMenu(m_errors);
        }

        if (menusel != menu_none || !openMenus.empty()) {
            MutexLock ml(frameMutex);   // some menus need access
            draw_game_menu();
        }

        client_graphics.endDraw();
        client_graphics.draw_screen(!menu.options.screenMode.alternativeFlipping());
        if (screenshot) {
            save_screenshot();
            screenshot = false;
        }
    }

    //client exit cleanup: done at stop wich needs to be called after loop
}

void Client::stop() {
    log("Client exiting: stop() called");

    abortThreads = true;

    //at least disconnect
    disconnect_command();

    tournamentPassword.stop();

    //save configuration file
    string fileName = wheregamedir + "config" + directory_separator + "client.cfg";
    log("Saving client configuration in %s", fileName.c_str());
    ofstream cfg(fileName.c_str());
    if (cfg) {
        // save name menu settings
        cfg << CCS_PlayerName           << ' ' << playername << '\n';
        cfg << CCS_Tournament           << ' ' << (menu.options.name.tournament() ? 1 : 0) << '\n';

        // save connect menu settings
        cfg << CCS_Favorites            << ' ' << (menu.connect.favorites() ? 1 : 0) << '\n';

        // save game menu settings
        cfg << CCS_ShowNames            << ' ' << (menu.options.game.showNames() ? 1 : 0) << '\n';
        {   // favorite colors
            cfg << CCS_FavoriteColors   << ' ';
            if (menu.options.game.favoriteColors.values().empty())
                cfg << -1;
            else {
                const vector<int>& colVec = menu.options.game.favoriteColors.values();
                for (vector<int>::const_iterator col = colVec.begin(); col != colVec.end(); ++col)
                    cfg << *col << ' ';
            }
            cfg << '\n';
        }
        cfg << CCS_LagPrediction        << ' ' << (menu.options.game.lagPrediction() ? 1 : 0) << '\n';
        cfg << CCS_LagPredictionAmount  << ' ' <<  menu.options.game.lagPredictionAmount() << '\n';
        cfg << CCS_MessageLogging       << ' ' << ((menu.options.game.messageLogging() == Menu_game::ML_full) ? 1 : (menu.options.game.messageLogging() == Menu_game::ML_chat) ? 2 : 0) << '\n';
        cfg << CCS_SaveStats            << ' ' << (menu.options.game.saveStats() ? 1 : 0) << '\n';
        cfg << CCS_ShowStats            << ' ' << ((menu.options.game.showStats() == Menu_game::SS_teams) ? 1 : (menu.options.game.showStats() == Menu_game::SS_players) ? 2 : 0) << '\n';
        cfg << CCS_ShowServerInfo       << ' ' << (menu.options.game.showServerInfo() ? 1 : 0) << '\n';
        cfg << CCS_StayDeadInMenus      << ' ' << (menu.options.game.stayDead() ? 1 : 0) << '\n';
        cfg << CCS_UnderlineMasterAuth  << ' ' << (menu.options.game.underlineMasterAuth() ? 1 : 0) << '\n';
        cfg << CCS_UnderlineServerAuth  << ' ' << (menu.options.game.underlineServerAuth() ? 1 : 0) << '\n';
        cfg << CCS_AutoGetServerList    << ' ' << (menu.options.game.autoGetServerList() ? 1 : 0) << '\n';

        // save controls menu settings
        cfg << CCS_KeyboardLayout       << ' ' <<  menu.options.controls.keyboardLayout() << '\n';
        cfg << CCS_KeypadMoving         << ' ' << (menu.options.controls.keypadMoving() ? 1 : 0) << '\n';
        cfg << CCS_ArrowKeysInStats     << ' ' << (menu.options.controls.arrowKeysInStats() == Menu_controls::AS_movePlayer ? 1 : 0) << '\n';
        cfg << CCS_Joystick             << ' ' << (menu.options.controls.joystick() ? 1 : 0) << '\n';
        cfg << CCS_JoystickMove         << ' ' <<  menu.options.controls.joyMove() << '\n';
        cfg << CCS_JoystickShoot        << ' ' <<  menu.options.controls.joyShoot() << '\n';
        cfg << CCS_JoystickRun          << ' ' <<  menu.options.controls.joyRun() << '\n';
        cfg << CCS_JoystickStrafe       << ' ' <<  menu.options.controls.joyStrafe() << '\n';

        // save screen mode menu settings
        cfg << CCS_Windowed             << ' ' << (menu.options.screenMode.windowed() ? 1 : 0) << '\n';
        ScreenMode mode = menu.options.screenMode.resolution();
        cfg << CCS_GFXMode              << ' ' <<  mode.width << ' ' << mode.height << ' ' << menu.options.screenMode.colorDepth() << '\n';
        cfg << CCS_Flipping             << ' ' << (menu.options.screenMode.flipping() ? 1 : 0) << '\n';
        cfg << CCS_AlternativeFlipping  << ' ' << (menu.options.screenMode.alternativeFlipping() ? 1 : 0) << '\n';

        // save graphics menu settings
        cfg << CCS_FPSLimit             << ' ' <<  menu.options.graphics.fpsLimit() << '\n';
        cfg << CCS_GFXTheme             << ' ' <<  menu.options.graphics.theme() << '\n';
        cfg << CCS_UseThemeBackground   << ' ' << (menu.options.graphics.useThemeBackground() ? 1 : 0) << '\n';
        cfg << CCS_Background           << ' ' <<  menu.options.graphics.background() << '\n';
        cfg << CCS_Font                 << ' ' <<  menu.options.graphics.font() << '\n';
        cfg << CCS_Antialiasing         << ' ' << (menu.options.graphics.antialiasing() ? 2 : 1) << '\n';
        cfg << CCS_MinTransp            << ' ' << (menu.options.graphics.minTransp() ? 1 : 0) << '\n';
        cfg << CCS_ContinuousTextures   << ' ' << (menu.options.graphics.contTextures() ? 1 : 0) << '\n';
        cfg << CCS_MinimapPlayers       << ' ' << (menu.options.graphics.minimapPlayers() == Menu_graphics::MP_EarlyCut ? 1 : menu.options.graphics.minimapPlayers() == Menu_graphics::MP_LateCut ? 2 : 0) << '\n';
        cfg << CCS_StatsBgAlpha         << ' ' <<  menu.options.graphics.statsBgAlpha() << '\n';

        // save sound menu settings
        cfg << CCS_SoundEnabled         << ' ' << (menu.options.sounds.enabled() ? 1 : 0) << '\n';
        cfg << CCS_Volume               << ' ' <<  menu.options.sounds.volume() << '\n';
        cfg << CCS_SoundTheme           << ' ' <<  menu.options.sounds.theme() << '\n';

        // save local server menu settings
        cfg << CCS_ServerPublic         << ' ' << (menu.ownServer.pub() ? 1 : 0) << '\n';
        cfg << CCS_ServerPort           << ' ' <<  menu.ownServer.port() << '\n';
        cfg << CCS_ServerAddress        << ' ' <<  menu.ownServer.address() << '\n';
        cfg << CCS_AutodetectAddress    << ' ' << (menu.ownServer.autoIP() ? 1 : 0) << '\n';

        cfg.close();
    }
    else
        log("Can't open %s for writing", fileName.c_str());
    log("Client configuration saved");
    fileName = wheregamedir + "config" + directory_separator + "favorites.txt";
    ofstream fav(fileName.c_str());
    if (fav) {
        for (vector<ServerListEntry>::const_iterator spy = gamespy.begin(); spy != gamespy.end(); ++spy)
            fav << spy->addressString() << '\n';
        fav.close();
    }

    //save client's password
    log("Saving password file...");
    fileName = wheregamedir + "config" + directory_separator + "password.bin";
    FILE *psf = fopen(fileName.c_str(), "wb");
    if (psf) {
        const string& password = menu.options.name.password();
        for (int c = 0; c < PASSBUFFER; c++) {
            if (c < (int)password.length())
                fputc(static_cast<unsigned char>(255 - password[c]), psf);
            else
                fputc(255, psf);    // 255 = 0 toggled (important)
        }
        fclose(psf);
    }
    else
        log.error(_("Can't open $1 for writing.", fileName));

    {
        MutexDebug md("downloadMutex", __LINE__, log);
        MutexLock ml(downloadMutex);
        downloads.clear();
    }

    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        closeMessageLog();

    if (listenServer.running())
        listenServer.stop();

    log("Client stop() completed");
}

void Client::rocketHitWallCallback(int rid, bool power, double x, double y, int roomx, int roomy) {
    if (power) {
        client_graphics.create_powerwallexplo(static_cast<int>(x), static_cast<int>(y), roomx, roomy, fx.rock[rid].team);
        client_sounds.play(SAMPLE_POWERWALLHIT);
    }
    else {
        client_graphics.create_wallexplo(static_cast<int>(x), static_cast<int>(y), roomx, roomy, fx.rock[rid].team);
        client_sounds.play(SAMPLE_WALLHIT);
    }
    fd.rock[rid].owner = fx.rock[rid].owner = -1;   // erase from clientside simulation
}

void Client::rocketOutOfBoundsCallback(int rid) {
    fd.rock[rid].owner = fx.rock[rid].owner = -1;   // erase from clientside simulation
}

void Client::playerHitWallCallback(int pid) {
    // play bounce sample if minimum time elapsed
    const double currTime = get_time(); //#fix
    if (currTime > fx.player[pid].wall_sound_time) {
        fx.player[pid].wall_sound_time = currTime + 0.2;
        client_sounds.play(SAMPLE_WALLBOUNCE);
    }
}

void Client::playerHitPlayerCallback(int pid1, int pid2) {
    // play bounce sample if minimum time elapsed
    const double currTime = get_time(); //#fix
    if (currTime > fx.player[pid1].player_sound_time || currTime > fx.player[pid2].player_sound_time) {
        fx.player[pid1].player_sound_time = fx.player[pid2].player_sound_time = currTime + 0.2;
        client_sounds.play(SAMPLE_PLAYERBOUNCE);
    }
}

bool Client::shouldApplyPhysicsToPlayerCallback(int pid) {
    return fx.player[pid].onscreen && !fx.player[pid].dead;
}

void Client::remove_useless_flags() {
    for (int i = 0; i < 3; i++)
        if (remove_flags & (0x01 << i)) {
            fx.remove_team_flags(i);
            fd.remove_team_flags(i);
        }
}

void Client::predraw() {
    if (me < 0 || fx.player[me].roomx < 0 || fx.player[me].roomx >= fx.map.w ||
            fx.player[me].roomy < 0 || fx.player[me].roomy >= fx.map.h)
        return; //#fix: this shouldn't be needed, or should be checked from a simple flag
    vector< pair<int, const WorldCoords*> > flags;
    vector< pair<int, const WorldCoords*> > spawns;

    for (int team = 0; team <= 2; team++) {
        const vector<WorldCoords>& tflags = (team == 2 ? fx.map.wild_flags : fx.map.tinfo[team].flags);
        for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi)     // flags
            if (fx.player[me].roomx == pi->px && fx.player[me].roomy == pi->py)
                flags.push_back(pair<int, const WorldCoords*>(team, &(*pi)));
        if (menu.options.graphics.mapInfoMode() && team < 2) {
            const vector<WorldCoords>& tspawn = fx.map.tinfo[team].spawn;
            for (vector<WorldCoords>::const_iterator pi = tspawn.begin(); pi != tspawn.end(); ++pi) // spawns
                if (fx.player[me].roomx == pi->px && fx.player[me].roomy == pi->py)
                    spawns.push_back(pair<int, const WorldCoords*>(team, &(*pi)));
        }
    }

    int texRoomX, texRoomY; // the room is textured as in these coordinates
    if (menu.options.graphics.contTextures()) {
        texRoomX = fx.player[me].roomx; // use real coordinates -> textures continue from a room to the next one
        texRoomY = fx.player[me].roomy;
    }
    else
        texRoomX = texRoomY = 0;    // this way the texturing always starts from the top left corner (classic look)
    client_graphics.predraw(fx.map.room[fx.player[me].roomx][fx.player[me].roomy], texRoomX, texRoomY, flags, spawns, menu.options.graphics.mapInfoMode());
}

//draw the whole game screen
void Client::draw_game_frame() {    // call with frameMutex locked
    // hide stuff if frame skipped
    const bool hide_game = !map_ready || gameover_plaque != NEXTMAP_NONE || fx.skipped || me < 0 || me >= maxplayers;

    // the playground: border, walls and pits
    if (hide_game) {
        client_graphics.draw_empty_background(map_ready);

        // game over message
        if (gameover_plaque == NEXTMAP_CAPTURE_LIMIT || gameover_plaque == NEXTMAP_VOTE_EXIT) {
            if (red_final_score > blue_final_score)
                client_graphics.draw_scores(_("RED TEAM WINS"), 0, red_final_score, blue_final_score);
            else if (blue_final_score > red_final_score)
                client_graphics.draw_scores(_("BLUE TEAM WINS"), 1, blue_final_score, red_final_score);
            else
                client_graphics.draw_scores(_("GAME TIED"), -1, blue_final_score, red_final_score);
        }

        if (map_ready)
            client_graphics.draw_waiting_map_message(_("Waiting game start - next map is"), fx.map.title);
        else {
            MutexDebug md("downloadMutex", __LINE__, log);
            MutexLock ml(downloadMutex);
            if (!downloads.empty() && downloads.front().isActive()) {
                const string text = _("Loading map: $1 bytes", itoa(downloads.front().progress()));
                client_graphics.draw_loading_map_message(text);
            }
        }
    }
    else {
        #ifdef ROOM_CHANGE_BENCHMARK
        predraw();
        ++benchmarkRuns;
        #endif
        client_graphics.draw_background();
    }
    // frame is valid?
    if (!hide_game && fd.frame >= 0) {
        client_graphics.startPlayfieldDraw();

        // draw dead players, except ice creams
        for (int i = 0; i < maxplayers; i++) {
            if (fx.player[i].used && fx.player[i].onscreen && fx.player[i].dead) {
                if (fx.player[i].stats().frags() % 10 == 0 && fx.player[i].stats().frags() >= 10)
                    ;   // draw later
                else
                    client_graphics.draw_player_dead(fx.player[i]);
            }
        }

        // draw any item pickups
        if (me >= 0)
            for (int i = 0; i < MAX_PICKUPS; i++)
                // used power-ups, not respawning, on my screen
                if (fx.item[i].kind != Powerup::pup_unused && fx.item[i].kind != Powerup::pup_respawning &&
                        fx.item[i].px == fx.player[me].roomx && fx.item[i].py == fx.player[me].roomy) {
                    client_graphics.draw_pup(fx.item[i], get_time());
                    if (fx.item[i].kind == Powerup::pup_deathbringer)
                        client_graphics.create_smoke(fx.item[i].x + rand() % 30 - 15, fx.item[i].y + rand() % 30 - 5,
                            fx.item[i].px, fx.item[i].py);
                }

        // draw turbo effect
        client_graphics.draw_turbofx(fx.player[me].roomx, fx.player[me].roomy, get_time());

        // draw any dropped flags (use fx since flags don't move)
        for (int t = 0; t < 2; t++)
            for (vector<Flag>::const_iterator fi = fx.teams[t].flags().begin(); fi != fx.teams[t].flags().end(); ++fi)
                // not carried, on same screen
                if (!fi->carried() && fi->position().px == fx.player[me].roomx && fi->position().py == fx.player[me].roomy)
                    client_graphics.draw_flag(t, fi->position().x, fi->position().y);

        for (vector<Flag>::const_iterator fi = fx.wild_flags.begin(); fi != fx.wild_flags.end(); ++fi)
            if (!fi->carried() && fi->position().px == fx.player[me].roomx && fi->position().py == fx.player[me].roomy)
                client_graphics.draw_flag(2, fi->position().x, fi->position().y);

        // draw any rockets
        for (int i = 0; i < MAX_ROCKETS; i++)
            if (fx.rock[i].owner != -1 && fx.rock[i].px == fx.player[me].roomx && fx.rock[i].py == fx.player[me].roomy) {
                fd.rock[i].team = fx.rock[i].team;
                fd.rock[i].power = fx.rock[i].power;
                const int radius = fd.rock[i].power ? ROCKET_RADIUS : POWER_ROCKET_RADIUS;
                const bool shadow = !fd.map.room[fx.player[me].roomx][fx.player[me].roomy].fall_on_wall(
                    static_cast<int>(fd.rock[i].x), static_cast<int>(fd.rock[i].y) + radius + 8, radius / 2);
                client_graphics.draw_rocket(fd.rock[i], shadow, get_time());
            }

        // the PLAY AREA: the players!
        for (int k = 0; k < maxplayers; k++) {
            const int i = (me / TSIZE == 0 ? k : maxplayers - k - 1);   // own team first

            //HACK REMENDEX: predict item_shadow
            if (fx.player[i].onscreen && fx.player[i].item_shadow()) {
                const int hspd = static_cast<int>((fd.frame - fx.frame) * 10.);
                fd.player[i].visibility = fx.player[i].visibility - hspd;
                const int limit = (fx.player[i].visibility >= 7) ? 7 : 0;   // this produces an error of at most one server frame if total invisibility is enabled
                if (fd.player[i].visibility < limit)
                    fd.player[i].visibility = limit;
            }

            if (fx.player[i].onscreen && i != me)   // draw only players on my screen
                draw_player(i);
            if (k == maxplayers - 1)                // last draw me
                draw_player(me);
        }

        for (int i = 0; i < maxplayers; i++)
            if (fx.player[i].used && fx.player[i].roomx == fx.player[me].roomx && fx.player[i].roomy == fx.player[me].roomy &&
                fx.player[i].onscreen && fx.player[i].item_deathbringer)
                    client_graphics.draw_deathbringer_carrier_effect((int)fd.player[i].lx, (int)fd.player[i].ly, calculatePlayerAlpha(i));

        client_graphics.draw_effects(fx.player[me].roomx, fx.player[me].roomy, get_time());

        if (menu.options.game.showNames())  // Draw player names but not for invisible enemies.
            for (int i = 0; i < maxplayers; i++) {
                if (!fx.player[i].used || !fx.player[i].onscreen || fx.player[i].dead ||
                    fx.player[i].roomx != fx.player[me].roomx || fx.player[i].roomy != fx.player[me].roomy ||
                    (fx.player[i].visibility < 200 && i / TSIZE != me / TSIZE))
                        continue;
                const int ttx = static_cast<int>(fd.player[i].lx);
                const int tty = static_cast<int>(fd.player[i].ly);
                client_graphics.draw_player_name(fx.player[i].name, ttx, tty, i / TSIZE, i == me);
            }

        client_graphics.endPlayfieldDraw();
    }

    //do not draw stuff below if map not ready to show
    if (!hide_game) {
        vector<NLubyte> roomvis(fx.map.w * fx.map.h, (me >= 0 && fx.player[me].item_shadow()) ? 255 : 0);   // how "well" the room is seen (according to the most visible player there)

        int max_time, start_fadeout;    // in frames
        switch (menu.options.graphics.minimapPlayers()) {
        /*break;*/ case Menu_graphics::MP_EarlyCut: max_time =     start_fadeout = 12;
            break; case Menu_graphics::MP_LateCut:  max_time =     start_fadeout = 20;
            break; case Menu_graphics::MP_Fade:     max_time = 20; start_fadeout = 10;
            break; default: nAssert(0); max_time = start_fadeout = 0;
        }
        // check how the rooms should be drawn
        if (me >= 0 && fx.frame >= 0)
            for (int i = 0; i < maxplayers; i++) {
                ClientPlayer& pl = fx.player[i];
                if (pl.used && pl.roomx >= 0 && pl.roomy >= 0 && pl.roomx < fx.map.w && pl.roomy < fx.map.h && pl.posUpdated > fx.frame - max_time) {
                    int alpha;
                    if (fx.frame > pl.posUpdated + start_fadeout)
                        alpha = 255 - static_cast<int>((fx.frame - pl.posUpdated - start_fadeout) * 255 / (max_time - start_fadeout));
                    else
                        alpha = 255;
                    pl.alpha = alpha;
                    if (roomvis[pl.roomy * fx.map.w + pl.roomx] < alpha)
                        roomvis[pl.roomy * fx.map.w + pl.roomx] = alpha;
                }
            }

        // paint fog of war in all invisible rooms
        for (int ry = 0; ry < fx.map.h; ry++)
            for (int rx = 0; rx < fx.map.w; rx++)
                client_graphics.draw_minimap_room(fx.map, rx, ry, roomvis[ry * fx.map.w + rx] / 255.);

        // draw all teammates and enemies on screens where there are teammates
        if (me >= 0 && fx.frame >= 0)
            for (int i = 0; i < maxplayers; i++) {
                const ClientPlayer& pl = fx.player[i];
                if (pl.used && pl.roomx >= 0 && pl.roomy >= 0 && pl.roomx < fx.map.w && pl.roomy < fx.map.h && pl.posUpdated > fx.frame - max_time) {
                    const int alpha = pl.alpha;
                    if (alpha != 255) {
                        set_trans_blender(0, 0, 0, alpha);
                        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
                    }
                    const int enemy = 1 - i / TSIZE;
                    int f = 0;
                    for (vector<Flag>::const_iterator fi = fx.teams[enemy].flags().begin(); fi != fx.teams[enemy].flags().end(); ++fi, ++f)
                        if (fi->carrier() == i) {
                            // update flag position for draw
                            fx.teams[enemy].move_flag(f, WorldCoords(pl.roomx, pl.roomy, static_cast<int>(pl.lx), static_cast<int>(pl.ly)));
                            client_graphics.draw_mini_flag(enemy, *fi, fx.map);
                        }

                    for (vector<Flag>::iterator fi = fx.wild_flags.begin(); fi != fx.wild_flags.end(); ++fi)
                        if (fi->carrier() == i) {
                            // update flag position for draw
                            fi->move(WorldCoords(pl.roomx, pl.roomy, static_cast<int>(pl.lx), static_cast<int>(pl.ly)));
                            client_graphics.draw_mini_flag(2, *fi, fx.map);
                        }

                    if (i != me) {
                        if (pl.color() >= 0 && pl.color() < MAX_PLAYERS / 2)    // Check because the server may have sent invalid colour.
                            client_graphics.draw_minimap_player(fx.map, pl);
                    }
                    else // myself: draw differently
                        client_graphics.draw_minimap_me(fx.map, pl, get_time());

                    solid_mode();
                }
            }

        // draw the miniflags (in the base and on the ground but not carried)
        for (int t = 0; t < 2; t++)
            for (vector<Flag>::const_iterator fi = fx.teams[t].flags().begin(); fi != fx.teams[t].flags().end(); ++fi)
                if (!fi->carried())
                    client_graphics.draw_mini_flag(t, *fi, fx.map);

        for (vector<Flag>::const_iterator fi = fx.wild_flags.begin(); fi != fx.wild_flags.end(); ++fi)
            if (!fi->carried())
                client_graphics.draw_mini_flag(2, *fi, fx.map);
    }//!hide_game

    client_graphics.draw_scoreboard(players_sb, fx.teams, maxplayers, key[KEY_TAB], menu.options.game.underlineMasterAuth(), menu.options.game.underlineServerAuth());

    client_graphics.draw_fps(FPS);

    // Time left if time limit is on and the game is running.
    if (map_time_limit && gameover_plaque == NEXTMAP_NONE && players_sb.size() > 1)
        if (map_end_time > get_time())
            client_graphics.map_time(map_end_time - static_cast<int>(get_time()));
        else
            client_graphics.map_time(0);

    // player's power-ups
    if (me >= 0) {
        if (fx.player[me].item_power) {
            double val = fx.player[me].item_power_time - get_time();
            if (val < 0) val = 0;
            client_graphics.draw_player_power(val);
        }
        if (fx.player[me].item_turbo) {
            double val = fx.player[me].item_turbo_time - get_time();
            if (val < 0) val = 0;
            client_graphics.draw_player_turbo(val);
        }
        if (fx.player[me].item_shadow()) {
            double val = fx.player[me].item_shadow_time - get_time();
            if (val < 0) val = 0;
            client_graphics.draw_player_shadow(val);
        }

        client_graphics.draw_player_weapon(fx.player[me].weapon);
    }

    if (want_change_teams)
        client_graphics.draw_change_team_message(get_time());
    if (want_map_exit)
        client_graphics.draw_change_map_message(get_time(), want_map_exit_delayed);

    // the STATUSBAR : health energy, bars ....
    if (me >= 0) {
        client_graphics.draw_player_health(fx.player[me].health);
        client_graphics.draw_player_energy(fx.player[me].energy);
    }

    // the HUD: message output
    const int chat_visible = show_all_messages ? client_graphics.chat_max_lines() : client_graphics.chat_lines();
    int start = static_cast<int>(chatbuffer.size()) - static_cast<int>(chat_visible);
    if (start < 0)
        start = 0;
    list<Message>::const_iterator msg = chatbuffer.begin();
    for (int i = 0; i < start; ++i, ++msg);
    if (!show_all_messages) // drop old messages
        for (; msg != chatbuffer.end(); ++msg)
            if (get_time() < msg->time() + 80)
                break;
    client_graphics.print_chat_messages(msg, chatbuffer.end(), talkbuffer);

    //"server not responding... connection may have dropped" plaque
    if (get_time() > lastpackettime + 1.0)
        m_notResponding.menu.draw(client_graphics.drawbuffer());

    // debug panel
    if (key[KEY_F9]) {
        const int bpsin = client->get_socket_stat(NL_AVE_BYTES_RECEIVED);
        const int bpsout = client->get_socket_stat(NL_AVE_BYTES_SENT);

        vector<vector<pair<int, int> > > sticks;
        vector<int> buttons;
        if (menu.options.controls.joystick()) {
            const JOYSTICK_INFO& joystick = joy[0];
            for (int i = 0; i < joystick.num_sticks; i++) {
                vector<pair<int, int> > axes;
                for (int j = 0; j < joystick.stick[i].num_axis; j++)
                    axes.push_back(pair<int, int>(joystick.stick[i].axis[j].d1, joystick.stick[i].axis[j].d2));
                sticks.push_back(axes);
            }
            for (int i = 0; i < joystick.num_buttons; i++)
                buttons.push_back(joystick.button[i].b);
        }

        client_graphics.debug_panel(fx.player, me, bpsin, bpsout, sticks, buttons);
    }

    // another frame, calculate FPS
    totalframecount++;
    framecount++;
    const double baixo = get_time() - frameCountStartTime;
    if (baixo > 1.0) {
        FPS = ((double)framecount) / baixo;
        frameCountStartTime = get_time();
        framecount = 0;
    }
}

int Client::calculatePlayerAlpha(int pid) const {
    static const int min_alpha_friends = 128;
    const int baseAlpha = fd.player[pid].visibility;
    if (fx.player[pid].team() == fx.player[me].team() && baseAlpha < min_alpha_friends)
        return min_alpha_friends;
    else
        return baseAlpha;
}

void Client::draw_player(int pid) {
    ClientPlayer& player = fx.player[pid];
    const int alpha = calculatePlayerAlpha(pid);
    // draw flag if player is carrier of a flag
    for (int t = 0; t < 2; t++)
        for (vector<Flag>::const_iterator fi = fx.teams[t].flags().begin(); fi != fx.teams[t].flags().end(); ++fi)
            if (fi->carrier() == pid)
                client_graphics.draw_flag(t, static_cast<int>(fd.player[pid].lx), static_cast<int>(fd.player[pid].ly));
    for (vector<Flag>::const_iterator fi = fx.wild_flags.begin(); fi != fx.wild_flags.end(); ++fi)
        if (fi->carrier() == pid)
            client_graphics.draw_flag(2, static_cast<int>(fd.player[pid].lx), static_cast<int>(fd.player[pid].ly));
    if (player.dead) {  // draw only ice creams
        if (player.stats().frags() % 10 == 0 && player.stats().frags() >= 10)
            client_graphics.draw_virou_sorvete(static_cast<int>(player.lx), static_cast<int>(player.ly));
    }
    else {
        if (player.color() >= 0 && player.color() < MAX_PLAYERS / 2) {  // Check because the server may have sent invalid colour.
            // turbo effect
            if (player.item_turbo && player.sx * player.sx + player.sy * player.sy > fx.physics.max_run_speed * fx.physics.max_run_speed &&
                        get_time() > player.next_turbo_effect_time) {
                player.next_turbo_effect_time = get_time() + 0.05;
                client_graphics.create_turbofx(static_cast<int>(fd.player[pid].lx), static_cast<int>(fd.player[pid].ly), player.roomx, player.roomy, player.team(), player.color(), player.gundir, alpha);
            }

            //draw player
            client_graphics.draw_player(static_cast<int>(fd.player[pid].lx), static_cast<int>(fd.player[pid].ly), player.team(), player.color(), player.gundir, player.hitfx, player.item_power, alpha, get_time());
        }

        //draw deathbringer carrier effect
        if (player.item_deathbringer && get_time() > player.next_smoke_effect_time) {
            player.next_smoke_effect_time = get_time() + 0.01;
            for (int i = 0; i < 2; i++)
                client_graphics.create_deathcarrier(static_cast<int>(fd.player[pid].lx) + rand() % 40 - 20, static_cast<int>(fd.player[pid].ly) + rand() % 40, player.roomx, player.roomy, alpha);
        }
        // draw deathbringer affected effect
        if (player.deathbringer_affected)
            client_graphics.draw_deathbringer_affected(static_cast<int>(fd.player[pid].lx), static_cast<int>(fd.player[pid].ly), player.team(), alpha);
        // shield
        if (player.item_shield)
            client_graphics.draw_shield(static_cast<int>(fd.player[pid].lx), static_cast<int>(fd.player[pid].ly), PLAYER_RADIUS + SHIELD_RADIUS_ADD, alpha, player.team(), player.gundir);
    }
}

//draws the game menu
void Client::draw_game_menu() {
    switch (menusel) {
    /*break;*/ case menu_maps: {
            MutexDebug md("mapInfoMutex", __LINE__, log);
            MutexLock ml(mapInfoMutex);
            client_graphics.map_list(maps, current_map, map_vote, edit_map_vote);
        }
        break; case menu_players:
            client_graphics.draw_statistics(players_sb, player_stats_page, static_cast<int>(get_time()), maxplayers, max_world_rank);
        break; case menu_teams:
            client_graphics.team_statistics(fx.teams);
        break; case menu_none: // regular menus are drawn below, regardless of menusel
        break; default:
            numAssert(0, menusel);
    }
    if (!openMenus.empty())
        openMenus.draw(client_graphics.drawbuffer());
}

void Client::initMenus() {
    typedef MenuCallback<Client> MCB;
    typedef MenuKeyCallback<Client> MKC;
    menu.connect.addHooks(new MCB::A<Textarea, &Client::MCF_connect>(this),
                          new MKC::A<Textarea, &Client::MCF_addRemoveServer>(this));

    menu.recursiveSetMenuOpener                 (new MCB::A<Menu,           &Client::MCF_menuOpener             >(this));

    menu.menu                       .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareMainMenu        >(this));

    menu.disconnect                     .setHook(new MCB::N<Textarea,       &Client::MCF_disconnect             >(this));
    menu.exitOutgun                     .setHook(new MCB::N<Textarea,       &Client::MCF_exitOutgun             >(this));

    menu.connect.menu               .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareServerMenu      >(this));
    menu.connect.menu               .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareServerMenu      >(this));   //#fix: inefficient!
    menu.connect.favorites              .setHook(new MCB::N<Checkbox,       &Client::MCF_prepareServerMenu      >(this));
    menu.connect.update                 .setHook(new MCB::N<Textarea,       &Client::MCF_updateServers          >(this));
    menu.connect.refresh                .setHook(new MCB::N<Textarea,       &Client::MCF_refreshServers         >(this));
    menu.connect.manualEntry         .setKeyHook(new MKC::N<IPfield,        &Client::MCF_addressEntryKeyHandler >(this));

    menu.connect.addServer.menu     .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareAddServer       >(this));
    menu.connect.addServer.menu       .setOkHook(new MCB::N<Menu,           &Client::MCF_addServer              >(this));

    menu.options.name.menu          .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareNameMenu        >(this));
    menu.options.name.menu          .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareDrawNameMenu    >(this));
    menu.options.name.menu         .setCloseHook(new MCB::N<Menu,           &Client::MCF_nameMenuClose          >(this));
    menu.options.name.name              .setHook(new MCB::N<Textfield,      &Client::MCF_nameChange             >(this));
    menu.options.name.randomName        .setHook(new MCB::N<Textarea,       &Client::MCF_randomName             >(this));
    menu.options.name.removePasswords   .setHook(new MCB::N<Textarea,       &Client::MCF_removePasswords        >(this));

    menu.options.game.menu          .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareGameMenu        >(this));
    typedef Select<Menu_game::MessageLoggingMode> mlComponentT;
    menu.options.game.messageLogging    .setHook(new MCB::N<mlComponentT,   &Client::MCF_messageLogging         >(this));

    menu.options.controls.menu      .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareControlsMenu    >(this));
    menu.options.controls.keyboardLayout.setHook(new MCB::N<Select<string>, &Client::MCF_keyboardLayout         >(this));
    menu.options.controls.joystick      .setHook(new MCB::N<Checkbox,       &Client::MCF_joystick               >(this));

    menu.options.screenMode.menu    .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareScrModeMenu     >(this));
    menu.options.screenMode.menu    .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareDrawScrModeMenu >(this));
    menu.options.screenMode.menu   .setCloseHook(new MCB::N<Menu,           &Client::MCF_screenModeChange       >(this));
    menu.options.screenMode.menu      .setOkHook(new MCB::N<Menu,           &Client::MCF_screenModeChange       >(this));
    menu.options.screenMode.colorDepth  .setHook(new MCB::N<Select<int>,    &Client::MCF_screenDepthChange      >(this));
    menu.options.screenMode.apply       .setHook(new MCB::N<Textarea,       &Client::MCF_screenModeChange       >(this));

    menu.options.graphics.menu      .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareGfxMenu         >(this));
    menu.options.graphics.theme         .setHook(new MCB::N<Select<string>, &Client::MCF_gfxThemeChange         >(this));
    menu.options.graphics.useThemeBackground.setHook(new MCB::N<Checkbox,   &Client::MCF_gfxThemeChange         >(this));
    menu.options.graphics.background    .setHook(new MCB::N<Select<string>, &Client::MCF_gfxThemeChange         >(this));
    menu.options.graphics.font          .setHook(new MCB::N<Select<string>, &Client::MCF_fontChange             >(this));
    menu.options.graphics.antialiasing  .setHook(new MCB::N<Checkbox,       &Client::MCF_antialiasChange        >(this));
    menu.options.graphics.minTransp     .setHook(new MCB::N<Checkbox,       &Client::MCF_transpChange           >(this));
    menu.options.graphics.contTextures  .setHook(new MCB::N<Checkbox,       &Client::predraw                    >(this));
    menu.options.graphics.statsBgAlpha  .setHook(new MCB::N<Slider,         &Client::MCF_statsBgChange          >(this));
    menu.options.graphics.mapInfoMode   .setHook(new MCB::N<Checkbox,       &Client::predraw                    >(this));

    menu.options.sounds.menu        .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareSndMenu         >(this));
    menu.options.sounds.enabled         .setHook(new MCB::N<Checkbox,       &Client::MCF_sndEnableChange        >(this));
    menu.options.sounds.volume          .setHook(new MCB::N<Slider,         &Client::MCF_sndVolumeChange        >(this));
    menu.options.sounds.theme           .setHook(new MCB::N<Select<string>, &Client::MCF_sndThemeChange         >(this));

    menu.options.language.menu      .setOpenHook(new MCB::N<Menu,           &Client::MCF_refreshLanguages       >(this));
    menu.options.language.menu     .setCloseHook(new MCB::N<Menu,           &Client::MCF_acceptLanguage         >(this));
    menu.options.language.menu        .setOkHook(new MCB::N<Menu,           &Client::MCF_menuCloser             >(this));

    menu.options.bugReports.menu   .setCloseHook(new MCB::N<Menu,           &Client::MCF_acceptBugReporting     >(this));   // save instantly because it has its own file
    menu.options.bugReports.menu      .setOkHook(new MCB::N<Menu,           &Client::MCF_menuCloser             >(this));

    menu.ownServer.menu             .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareOwnServerMenu   >(this));
    menu.ownServer.start                .setHook(new MCB::N<Textarea,       &Client::MCF_startServer            >(this));
    menu.ownServer.play                 .setHook(new MCB::N<Textarea,       &Client::MCF_playServer             >(this));
    menu.ownServer.stop                 .setHook(new MCB::N<Textarea,       &Client::MCF_stopServer             >(this));

    m_playerPassword.menu             .setOkHook(new MCB::N<Menu,           &Client::MCF_playerPasswordAccept   >(this));
    m_serverPassword.menu             .setOkHook(new MCB::N<Menu,           &Client::MCF_serverPasswordAccept   >(this));
    m_connectProgress.accept            .setHook(new MCB::N<Textarea,       &Client::MCF_menuCloser             >(this));
    m_connectProgress.cancel            .setHook(new MCB::N<Textarea,       &Client::MCF_menuCloser             >(this));
    m_connectProgress.menu         .setCloseHook(new MCB::N<Menu,           &Client::MCF_cancelConnect          >(this));
    m_dialog.accept                     .setHook(new MCB::N<Textarea,       &Client::MCF_menuCloser             >(this));   // cancel not used
    m_errors.accept                     .setHook(new MCB::N<Textarea,       &Client::MCF_clearErrors            >(this));   // cancel not used
    m_serverInfo.accept                 .setHook(new MCB::N<Textarea,       &Client::MCF_menuCloser             >(this));   // cancel not used

    m_errors.menu.setCaption(_("Errors"));

    m_notResponding.menu.setCaption(_("Server not responding"));
    m_notResponding.addLine(_("May be heavy packet loss,"));
    m_notResponding.addLine(_("or the server disconnected."), "", false, true); // make the dialog passive

    loadHelp();
    loadSplashScreen();

    menu.options.screenMode.init(client_graphics);
    menu.options.graphics.init(client_graphics);
    menu.options.sounds.init(client_sounds);
    menu.ownServer.init(serverExtConfig.ipAddress);
}

void Client::MCF_menuOpener(Menu& menu) {
    openMenus.open(&menu);
}

void Client::MCF_menuCloser() {
    openMenus.close();
}

void Client::MCF_prepareMainMenu() {
    menu.ownServer.refreshCaption(listenServer.running());
    menu.disconnect.setEnable(connected);
}

void Client::MCF_disconnect() {
    disconnect_command();
}

void Client::MCF_exitOutgun() {
    quitCommand = true;
}

void Client::MCF_cancelConnect() {
    if (!connected)
        disconnect_command();   // will cancel the (probably) ongoing connect attempt
}

void Client::MCF_prepareNameMenu() {
    menu.options.name.name.set(playername);
}

void Client::MCF_prepareDrawNameMenu() {
    menu.options.name.namestatus.set(tournamentPassword.statusAsString());
}

void Client::MCF_nameMenuClose() {
    change_name_command();
    send_tournament_participation();
}

void Client::MCF_nameChange() { // only function to clear the password
    menu.options.name.password.set("");
    tournamentPassword.changeData(playername, "");
}

void Client::MCF_randomName() {
    menu.options.name.name.set(RandomName());
    MCF_nameChange();
}

void Client::MCF_removePasswords() {
    const int removed = remove_player_passwords(menu.options.name.name());
    string dialog;
    if (removed == 1)
        dialog = _("1 password removed.");
    else if (removed > 1)
        dialog = _("$1 passwords removed.", itoa(removed));
    else
        dialog = _("No passwords found.");
    m_dialog.clear();
    m_dialog.addLine(dialog);
    showMenu(m_dialog);
}

void Client::MCF_prepareGameMenu() {
    menu.options.game.favoriteColors.setGraphicsCallBack(client_graphics);
}

void Client::MCF_prepareControlsMenu() {
    ClientControls ctrl = readControls(true, true);
    string active;
    if (ctrl.isUp())
        active += _("up")     + ' ';
    if (ctrl.isDown())
        active += _("down")   + ' ';
    if (ctrl.isLeft())
        active += _("left")   + ' ';
    if (ctrl.isRight())
        active += _("right")  + ' ';
    if (ctrl.isRun())
        active += _("run")    + ' ';
    if (ctrl.isStrafe())
        active += _("strafe") + ' ';
    if (key[KEY_LCONTROL] || key[KEY_RCONTROL] || (menu.options.controls.joystick() && readJoystickButton(menu.options.controls.joyShoot())))
        active += _("shoot")  + ' ';
    if (menu.options.controls.joystick()) {
        for (int button = 1; button <= 16; ++button)
            if (readJoystickButton(button))
                active += itoa(button) + ' ';
    }
    menu.options.controls.activeControls.set(active);
}

void Client::MCF_keyboardLayout() {
    const string cfg = string("[system]\nkeyboard=") + menu.options.controls.keyboardLayout() + '\n';
    remove_keyboard();
    override_config_data(cfg.data(), cfg.length());
    install_keyboard();
}

void Client::MCF_joystick() {
    if (menu.options.controls.joystick())
        install_joystick(JOY_TYPE_AUTODETECT);
    else
        remove_joystick();
}

void Client::MCF_messageLogging() {
    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        openMessageLog();
    else
        closeMessageLog();
}

void Client::MCF_prepareScrModeMenu() {
    menu.options.screenMode.update(client_graphics);
}

void Client::MCF_prepareDrawScrModeMenu() {
    menu.options.screenMode.flipping.setEnable(!menu.options.screenMode.windowed());
    menu.options.screenMode.alternativeFlipping.setEnable(!menu.options.screenMode.windowed() && menu.options.screenMode.flipping());
}

void Client::MCF_prepareGfxMenu() {
    menu.options.graphics.update(client_graphics);
}

void Client::MCF_gfxThemeChange() {
    client_graphics.select_theme(menu.options.graphics.theme(), menu.options.graphics.background(), menu.options.graphics.useThemeBackground());
    predrawNeeded = true;
}

void Client::MCF_fontChange() {
    client_graphics.select_font(menu.options.graphics.font());
    client_graphics.make_layout();
    predrawNeeded = true;
    mapChanged = true;  // just to get minimap updated
}

void Client::MCF_screenDepthChange() {
    menu.options.screenMode.update(client_graphics);  // fetch resolutions according to the new depth
}

void Client::MCF_screenModeChange() {   // used to lose the return value
    nAssert(screenModeChange());    // it should return true unless it's out of memory, because this function is only used when there is a working mode to revert to
}

bool Client::screenModeChange() {   // returns true whenever Graphics is usable (even when reverted back to current (workingGfxMode) mode)
    if (!menu.options.screenMode.newMode())
        return true;

    const ScreenMode res = menu.options.screenMode.resolution();
    const int depth = menu.options.screenMode.colorDepth();

    Checkbox& win  = menu.options.screenMode.windowed;
    Checkbox& flip = menu.options.screenMode.flipping;
    const bool owin = win(), oflip = flip();

    for (int nTry = 0;; ++nTry) {
        if (client_graphics.init(res.width, res.height, depth, win(), flip())) {
            if (nTry != 0)
                log.error(_("Couldn't initialize resolution $1×$2×$3 in $4 mode; reverted to $5.",
                            itoa(res.width), itoa(res.height), itoa(depth),
                            owin  ? _("windowed") : (oflip  ? _("flipped fullscreen") : _("backbuffered fullscreen")),
                            win() ? _("windowed") : (flip() ? _("flipped fullscreen") : _("backbuffered fullscreen"))));
            break;
        }
        switch (nTry) { // try in order: [switch flip], switch windowed, [switch flip]
        /*break;*/ case 0:
                if (!win()) {
                    flip.set(!flip());
                    break;
                }
                nTry = 1;   // no point in changing flipping when windowed, skip round
            /*no break*/ case 1:
                win.set(!win());
            break; case 2:
                if (!win()) {
                    flip.set(!flip());
                    break;
                }
                nTry = 3;   // no point in changing flipping when windowed, skip round
            /*no break*/ case 3:
                log.error(_("Couldn't initialize resolution $1×$2×$3 in any mode.", itoa(res.width), itoa(res.height), itoa(depth)));
                if (workingGfxMode.used()) {    // revert to working mode
                    const GFXMode& wm = workingGfxMode;
                    nAssert(menu.options.screenMode.colorDepth.set(wm.depth));
                    menu.options.screenMode.update(client_graphics);  // fetch resolutions according to the new depth
                    menu.options.screenMode.resolution.set(ScreenMode(wm.width, wm.height));  // ignore potential error here; we couldn't do anything about it anyway
                    win.set(wm.windowed);
                    flip.set(wm.flipping);
                    return client_graphics.init(wm.width, wm.height, wm.depth, wm.windowed, wm.flipping);
                }
                return false;
        }
    }
    workingGfxMode = GFXMode(res.width, res.height, depth, win(), flip());
    client_graphics.update_minimap_background(fx.map);
    predrawNeeded = true;
    const int rate = get_refresh_rate();
    ostringstream ost;
    if (rate == 0)
        ost << _("unknown");
    else
        ost << _("$1 Hz", itoa(rate));
    menu.options.screenMode.refreshRate.set(ost.str());
    return true;
}

void Client::MCF_antialiasChange() {
    client_graphics.set_antialiasing(menu.options.graphics.antialiasing());
    client_graphics.update_minimap_background(fx.map);
    predrawNeeded = true;
}

void Client::MCF_transpChange() {
    client_graphics.set_min_transp(menu.options.graphics.minTransp());
}

void Client::MCF_statsBgChange() {
    client_graphics.set_stats_alpha(menu.options.graphics.statsBgAlpha());
}

void Client::MCF_prepareSndMenu() {
    menu.options.sounds.update(client_sounds);
}

void Client::MCF_sndEnableChange() {
    client_sounds.setEnable(menu.options.sounds.enabled());
}

void Client::MCF_sndVolumeChange() {
    client_sounds.setVolume(menu.options.sounds.volume());
    client_sounds.play(SAMPLE_POWER_FIRE);
}

void Client::MCF_sndThemeChange() {
    client_sounds.select_theme(menu.options.sounds.theme());
}

bool translationSort(const pair<string, string>& t1, const pair<string, string>& t2) {  // helper to MCF_refreshLanguages
    // don't care about the language code (it isn't visible anyway), and use a case insensitive order
    return platStricmp(t1.first.c_str(), t2.first.c_str()) < 0;
}

void Client::MCF_refreshLanguages() {
    menu.options.language.language.clearOptions();
    menu.options.language.language.addOption("English", "en");   // global default when there's nothing in language.txt

    // search the languages directory for translations to add
    vector< pair<string, string> > translations;
    FileFinder* languageFiles = platMakeFileFinder(wheregamedir + "languages", ".txt", false);
    while (languageFiles->hasNext()) {
        string name = FileName(languageFiles->next()).getBaseName();
        if (name.find('.') != string::npos || name == "en")   // skip help.language.txt and possible similar files, and of course English which was added first
            continue;
        // fetch language name
        string langName;
        const string langFile = wheregamedir + "languages" + directory_separator + name + ".txt";
        ifstream lang(langFile.c_str());
        if (lang && getline_skip_comments(lang, langName))
            translations.push_back(pair<string, string>(langName, name));
        else
            log.error(_("Translation $1 can't be read.", langFile));
    }
    delete languageFiles;

    // add found languages to options
    sort(translations.begin(), translations.end(), translationSort);
    for (vector< pair<string, string> >::const_iterator ti = translations.begin(); ti != translations.end(); ++ti)
        menu.options.language.language.addOption(ti->first, ti->second);

    // fetch the currently chosen language from language.txt (because after changing it can be different from the loaded language)
    string lang;
    ifstream langConfig((wheregamedir + "config" + directory_separator + "language.txt").c_str());
    if (langConfig && getline_skip_comments(langConfig, lang))
        menu.options.language.language.set(lang); // ignore possible failure
}

void Client::MCF_acceptLanguage() {
    Language newLang;
    const string lang = menu.options.language.language();
    if (!newLang.load(lang, log))
        return; // load already logs an error message
    ofstream langConfig((wheregamedir + "config" + directory_separator + "language.txt").c_str());
    if (langConfig) {
        langConfig << lang << '\n';
        langConfig.close();
        if (lang != language.code()) {  // what is currently loaded; what was previously in language.txt has no significance
            m_dialog.clear();
            m_dialog.wrapLine(newLang.get_text("Please close and restart Outgun to complete the change of language."));
            showMenu(m_dialog);
        }
    }
    else
        log.error(_("config/language.txt can't be written."));
}

void Client::MCF_acceptBugReporting() {
    g_autoBugReporting = menu.options.bugReports.policy();
    const string main_cfg_file = wheregamedir + "config" + directory_separator + "maincfg.txt";
    ofstream os(main_cfg_file.c_str());
    if (os) {
        switch (g_autoBugReporting) {
        /*break;*/ case ABR_disabled: os << "autobugreporting disabled";
            break; case ABR_minimal:  os << "autobugreporting minimal" ;
            break; case ABR_withDump: os << "autobugreporting complete";
            break; default: nAssert(0);
        }
        os << '\n';
    }
    else
        log.error(_("Can't open $1 for writing.", main_cfg_file));
}

void Client::MCF_playerPasswordAccept() {
    openMenus.close(&m_playerPassword.menu);
    if (m_playerPassword.save())
        save_player_password(playername, addressToString(serverIP), m_playerPassword.password());
    if (connected)
        issue_change_name_command();
    else
        connect_command(false);
}

void Client::MCF_serverPasswordAccept() {
    openMenus.close(&m_serverPassword.menu);
    nAssert(!connected);
    connect_command(false);
}

void Client::MCF_clearErrors() {
    openMenus.close(&m_errors.menu);
    m_errors.clear();
}

void Client::MCF_prepareServerMenu() {
    const int oldSel = menu.connect.menu.selection();

    menu.connect.reset();
    vector<NLaddress> addresses;
    const vector<ServerListEntry>& servers = (menu.connect.favorites() ? gamespy : mgamespy);
    MutexDebug::lock("serverListMutex", __LINE__, log);
    serverListMutex.lock();
    for (vector<ServerListEntry>::const_iterator spy = servers.begin(); spy != servers.end(); ++spy) {
        ostringstream info;
        info << setw(21) << left << spy->addressString() << right << ' ';
        info << setw(4);
        if (spy->ping > 0)
            info << spy->ping;
        else
            info << '?';
        if (spy->refreshed) {
            info << ' ';
            if (spy->noresponse)
                info << _("no response");
            else
                info << spy->info;
        }
        menu.connect.add(spy->address(), info.str());
        addresses.push_back(spy->address());
    }
    if (!menu.connect.favorites())
        for (vector<ServerListEntry>::const_iterator spy = gamespy.begin(); spy != gamespy.end(); ++spy)
            if (!spy->noresponse && find(addresses.begin(), addresses.end(), spy->address()) == addresses.end()) {
                ostringstream info;
                info << setw(21) << left << spy->addressString() << right << ' ';
                info << setw(4);
                if (spy->ping > 0)
                    info << spy->ping;
                else
                    info << '?';
                if (spy->refreshed) {
                    info << ' ';
                    if (spy->noresponse)
                        info << _("no response");
                    else
                        info << spy->info;
                }
                menu.connect.add(spy->address(), info.str());
                addresses.push_back(spy->address());
            }
    serverListMutex.unlock();
    MutexDebug::unlock("serverListMutex", __LINE__, log);

    typedef MenuCallback<Client> MCB;
    typedef MenuKeyCallback<Client> MKC;
    menu.connect.addHooks(new MCB::A<Textarea, &Client::MCF_connect>(this),
                          new MKC::A<Textarea, &Client::MCF_addRemoveServer>(this));
    const bool refreshActive = (refreshStatus != RS_none && refreshStatus != RS_failed);
    menu.connect.update.setEnable(!menu.connect.favorites() && !refreshActive);
    menu.connect.refresh.setEnable(!refreshActive);
    menu.connect.refreshStatus.set(refreshStatusAsString());

    menu.connect.menu.setSelection(oldSel);
}

void Client::MCF_prepareAddServer() {
    menu.connect.addServer.save.set(menu.connect.favorites());
    menu.connect.addServer.address.set("");
}

void Client::MCF_addServer() {
    if (!menu.connect.addServer.address().empty()) {
        ServerListEntry spy;
        if (!spy.setAddress(menu.connect.addServer.address())) {
            m_dialog.clear();
            m_dialog.addLine(_("Invalid IP address."));
            showMenu(m_dialog);
            return;
        }
        mgamespy.push_back(spy);
        if (menu.connect.addServer.save())
            gamespy.push_back(spy);
        MCF_prepareServerMenu();
    }
    MCF_menuCloser();
}

bool Client::MCF_addressEntryKeyHandler(char scan, unsigned char chr) {
    (void)chr;
    if (scan != KEY_ENTER && scan != KEY_INSERT)
        return false;
    if (menu.connect.manualEntry().empty())
        return true;    // the key is considered handled even if it has no effect in this case
    ServerListEntry spy;
    if (!spy.setAddress(menu.connect.manualEntry())) {
        m_dialog.clear();
        m_dialog.addLine(_("Invalid IP address."));
        showMenu(m_dialog);
        return true;
    }
    if (scan == KEY_ENTER) {    // connect to the address
        serverIP = spy.address();
        m_serverPassword.password.set("");
        connect_command(true);
    }
    else if (scan == KEY_INSERT) {  // add the server to the list shown below
        if (menu.connect.favorites())
            gamespy.push_back(spy);
        else
            mgamespy.push_back(spy);
    }
    return true;
}

bool Client::MCF_addRemoveServer(Textarea& target, char scan, unsigned char chr) {
    (void)chr;
    if (scan == KEY_DEL) {
        vector<ServerListEntry>& servers = (menu.connect.favorites() ? gamespy : mgamespy);
        const NLaddress address = menu.connect.getAddress(target);
        for (vector<ServerListEntry>::iterator spy = servers.begin(); spy != servers.end(); ++spy)
            if (nlAddrCompare(&address, &spy->address())) {
                servers.erase(spy);
                break;
            }
        return true;
    }
    else if (scan == KEY_INSERT && !menu.connect.favorites()) {
        const NLaddress address = menu.connect.getAddress(target);
        for (vector<ServerListEntry>::const_iterator spy = mgamespy.begin(); spy != mgamespy.end(); ++spy)
            if (nlAddrCompare(&address, &spy->address())) {
                gamespy.push_back(*spy);
                break;
            }
        return true;
    }
    return false;
}

void Client::MCF_connect(Textarea& target) {
    serverIP = menu.connect.getAddress(target);
    m_serverPassword.password.set("");
    connect_command(true);
}

void Client::MCF_updateServers() {
    if (refreshStatus == RS_none || refreshStatus == RS_failed) {
        refreshStatus = RS_running;
        Thread::startDetachedThread_assert(RedirectToMemFun0<Client, void>(this, &Client::getServerListThread), extConfig.lowerPriority);
    }
}

void Client::MCF_refreshServers() {
    if (refreshStatus == RS_none || refreshStatus == RS_failed) {
        refreshStatus = RS_running;
        Thread::startDetachedThread_assert(RedirectToMemFun0<Client, void>(this, &Client::refreshThread), extConfig.lowerPriority);
    }
}

void Client::MCF_prepareOwnServerMenu() {
    menu.ownServer.refreshCaption(listenServer.running());
    menu.ownServer.refreshEnables(listenServer.running(), connected);
}

void Client::MCF_startServer() {
    if (!listenServer.running()) {
        serverExtConfig.privateserver = menu.ownServer.pub() ? 0 : 1;
        serverExtConfig.port = menu.ownServer.port();
        serverExtConfig.ipAddress = menu.ownServer.address();
        serverExtConfig.privSettingForced = serverExtConfig.portForced = serverExtConfig.ipForced = true;
        serverExtConfig.showErrorCount = false;
        listenServer.start(serverExtConfig.port, serverExtConfig);
    }
}

void Client::MCF_playServer() {
    if (listenServer.running()) {
        nAssert(nlStringToAddr("127.0.0.1", &serverIP));
        nAssert(nlSetAddrPort(&serverIP, listenServer.port()));
        openMenus.clear();
        m_serverPassword.password.set("");
        connect_command(true);
    }
}

void Client::MCF_stopServer() {
    if (listenServer.running())
        listenServer.stop();
}

void Client::load_highlight_texts() {
    highlight_text.clear();
    const string configFile = wheregamedir + "config" + directory_separator + "texts.txt";
    ifstream in(configFile.c_str());
    string line;
    while (getline_skip_comments(in, line))
        highlight_text.push_back(toupper(trim(line)));
}

void Client::load_fav_maps() {
    fav_maps.clear();
    const string configFile = wheregamedir + "config" + directory_separator + "maps.txt";
    ifstream in(configFile.c_str());
    string line;
    while (getline_skip_comments(in, line))
        fav_maps.push_back(toupper(trim(line)));
}

void Client::apply_fav_maps() {
    for (vector<MapInfo>::iterator mi = maps.begin(); mi != maps.end(); ++mi) {
        const vector<string>::const_iterator mf = find(fav_maps.begin(), fav_maps.end(), toupper(mi->title));
        if (mf != fav_maps.end())
            mi->highlight = true;
    }
}

void Client::loadHelp() {
    menu.help.clear();
    const string configFile = wheregamedir + "languages" + directory_separator + "help." + language.code() + ".txt";
    ifstream in(configFile.c_str());
    if (!in) {
        menu.help.addLine(_("No help found. It should be in $1", configFile));
        return;
    }
    string line;
    while (getline_smart(in, line))
        menu.help.addLine(line);
}

void Client::loadSplashScreen() {
    menu.options.bugReports.clear();
    const string splashFile = wheregamedir + "languages" + directory_separator + "splash." + language.code() + ".txt";
    ifstream in(splashFile.c_str());
    if (in) {
        string line;
        while (getline_smart(in, line))
            menu.options.bugReports.addLine(line);
    }
    else {
        static const char* msg[] = {
            GAME_STRING " " GAME_VERSION ", copyright © 2002-2006 multiple authors.",
            "",
            "Outgun is free software under the GNU GPL, and you are welcome to "
            "redistribute it under certain conditions. Outgun comes with ABSOLUTELY "
            "NO WARRANTY. For details, see the accompanying file COPYING.",
            "",
            "To help us remove any remaining bugs, you can let Outgun automatically "
            "send us a notification when an unexpected failure occurs. You can choose "
            "between no reporting, minimal information, and a complete report. The "
            "minimal information includes no more than the file name and line number "
            "of the failing assertion, and the version of Outgun. The complete report "
            "also includes a copy of Outgun's stack. This information is only used to "
            "find the cause of the failure. We can't contact you for more information, "
            "so it is recommended to also send an e-mail with more details.",
            "",
            "Choose the preferred mode below with left and right arrow keys, and close "
            "the menu with Enter or Esc. After the first time of starting Outgun, you "
            "can find this screen in the Options menu.",
            0
        };
        for (const char** line = msg; *line; ++line)
            menu.options.bugReports.addLine(*line);
    }
}

void Client::openMessageLog() {
    if (!messageLogOpen) {
        message_log.clear();    // necessary: http://gcc.gnu.org/onlinedocs/libstdc++/faq/index.html#4_4_iostreamclear
        message_log.open((wheregamedir + "log" + directory_separator + "message.log").c_str(), ios::app);
        messageLogOpen = true;
    }
}

void Client::closeMessageLog() {
    if (messageLogOpen) {
        message_log.close();
        messageLogOpen = false;
    }
}

void Client::CB_tournamentToken(string token) { // callback called by tournamentPassword from another thread
    if (connected) {
        char lebuf[256]; int count = 0;
        writeByte(lebuf, count, data_registration_token);
        writeStr(lebuf, count, token);
        client->send_message(lebuf, count);
        tournamentPassword.serverProcessingToken();
    }
}

void Client::cfunc_connection_update(void* customp, int connect_result, const char* data, int length) {
    Client* cl = static_cast<Client*>(customp);
    cl->connection_update(connect_result, data, length);
}

void Client::connection_update(int connect_result, const char* data, int length) {
    addThreadMessage(new TM_ConnectionUpdate(connect_result, data, length));
}

void Client::cfunc_server_data(void* customp, const char* data, int length) {
    Client* cl = static_cast<Client*>(customp);
    cl->process_incoming_data(data, length);
}
