/*
 *  client.cpp
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Jani Rivinoja
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
#include <string>
#include <vector>

#include <cctype>
#include <cmath>

#include "incalleg.h"
#include "binaryaccess.h"
#include "leetnet/client.h"
#include "commont.h"
#include "debug.h"
#include "debugconfig.h" // for LOG_MESSAGE_TRAFFIC
#include "language.h"
#include "names.h"
#include "nassert.h"
#include "network.h"
#include "platform.h"
#include "protocol.h"
#include "timer.h"
#include "utility.h"
#include "version.h"

#include "client.h"

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
using std::stringstream;
using std::vector;

#ifndef DEDICATED_SERVER_ONLY
//#define ROOM_CHANGE_BENCHMARK
//#define DEATHBRINGER_SPEED_TEST

const int PASSBUFFER = 32;  //size of password file

#ifdef ROOM_CHANGE_BENCHMARK
int benchmarkRuns = 0;
#endif
#endif

class ClientPhysicsCallbacks : public PhysicsCallbacksBase {
    Client& c;

public:
    ClientPhysicsCallbacks(Client& c_) throw () : c(c_) { }

    bool collideToRockets() const throw () { return false; }
    bool collidesToRockets(int) const throw () { return false; }
    bool collidesToPlayers(int) const throw () { return true; }
    bool gatherMovementDistance() const throw () { return false; }
    bool allowRoomChange() const throw () { return false; }
    void addMovementDistance(int, double) throw () { }
    void playerScreenChange(int) throw () { }
    void rocketHitWall(int rid, bool power, double x, double y, int roomx, int roomy) throw () { c.rocketHitWallCallback(rid, power, x, y, roomx, roomy); }
    bool rocketHitPlayer(int, int) throw () { return false; }
    void playerHitWall(int pid) throw () { c.playerHitWallCallback(pid); }
    PlayerHitResult playerHitPlayer(int pid1, int pid2, double) throw () { c.playerHitPlayerCallback(pid1, pid2); return PlayerHitResult(false, false, 1., 1.); }
    void rocketOutOfBounds(int rid) throw () { c.rocketOutOfBoundsCallback(rid); }
    bool shouldApplyPhysicsToPlayer(int pid) throw () { return c.shouldApplyPhysicsToPlayerCallback(pid); }
};

class TM_DoDisconnect : public ThreadMessage {
public:
    void execute(Client* cl) const throw () { cl->disconnect_command(); }
};

class TM_Text : public ThreadMessage {
    Message_type type;
    string text;
    int team;   // -1 for non-team messages

public:
    TM_Text(Message_type type_, const string& text_, int team_ = -1) throw () : type(type_), text(text_), team(team_) { }
    ~TM_Text() throw () { }
    void execute(Client* cl) const throw () {
        #ifndef DEDICATED_SERVER_ONLY
        cl->print_message(type, text, team);
        #else
        (void)cl;
        #endif
    }
};

class TM_Sound : public ThreadMessage {
    int sample;

public:
    TM_Sound(int sample_) throw () : sample(sample_) { }
    void execute(Client* cl) const throw () {
        #ifndef DEDICATED_SERVER_ONLY
        cl->play_sound(sample);
        #else
        (void)cl;
        #endif
    }
};

class TM_MapChange : public ThreadMessage {
    string name;
    uint16_t crc;

public:
    TM_MapChange(const string& name_, uint16_t crc_) throw () : name(name_), crc(crc_) { }
    ~TM_MapChange() throw () { }
    void execute(Client* cl) const throw () { cl->server_map_command(name, crc); }
};

#ifndef DEDICATED_SERVER_ONLY
class TM_NameAuthorizationRequest : public ThreadMessage {
public:
    void execute(Client* cl) const throw () { cl->m_playerPassword.setup(cl->playername, false); cl->showMenu(cl->m_playerPassword); }
};

class TM_GunexploEffect : public ThreadMessage {
    int team;
    WorldCoords pos;
    double time;

public:
    TM_GunexploEffect(int team_, double time_, const WorldCoords& pos_) throw () : team(team_), pos(pos_), time(time_) { }
    void execute(Client* cl) const throw () { cl->graphics.create_gunexplo(pos, team, time); }
};

class TM_ServerSettings : public ThreadMessage {
    uint8_t caplimit, timelimit, extratime;
    uint16_t misc1, pupMin, pupMax, pupAddTime, pupMaxTime;
    int flag_return_delay;

    void addLine(Client* cl, const string& caption, const string& value) const throw ();

public:
    TM_ServerSettings(uint8_t caplimit_, uint8_t timelimit_, uint8_t extratime_, uint16_t misc1_,
                      uint16_t pupMin_, uint16_t pupMax_, uint16_t pupAddTime_, uint16_t pupMaxTime_, int flag_return_delay_) throw () :
        caplimit(caplimit_), timelimit(timelimit_), extratime(extratime_), misc1(misc1_),
        pupMin(pupMin_), pupMax(pupMax_), pupAddTime(pupAddTime_), pupMaxTime(pupMaxTime_), flag_return_delay(flag_return_delay_) { }
    void execute(Client* cl) const throw ();
};
#endif

class TM_ConnectionUpdate : public ThreadMessage {
    int code;
    DataBlock data;

public:
    TM_ConnectionUpdate(int code_, ConstDataBlockRef data_) throw () : code(code_), data(data_) { }
    ~TM_ConnectionUpdate() throw () { }
    void execute(Client* cl) const throw ();
};

#ifndef DEDICATED_SERVER_ONLY
void ServerThreadOwner::threadFn(const ServerExternalSettings& config) throw () {
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
}

void ServerThreadOwner::start(int port, const ServerExternalSettings& config) throw () {
    nAssert(quitFlag && !threadFlag);
    runPort = port;
    quitFlag = false;
    threadFlag = true;
    RedirectToMemFun1<ServerThreadOwner, void, const ServerExternalSettings&> rmf(this, &ServerThreadOwner::threadFn);
    serverThread.start_assert("ServerThreadOwner::threadFn", rmf, config, config.priority);
}

void ServerThreadOwner::stop() throw () {
    nAssert(threadFlag);
    quitFlag = true;
    threadFlag = false;
    serverThread.join();
}

void TournamentPasswordManager::start() throw () {
    quitThread = false;
    thread.start_assert("TournamentPasswordManager::threadFn",
                        RedirectToMemFun0<TournamentPasswordManager, void>(this, &TournamentPasswordManager::threadFn),
                        priority);
}

void TournamentPasswordManager::setToken(const string& newToken) throw () {
    if (token.read() != newToken) {
        token = newToken;
        tokenCallback(newToken);
    }
}

TournamentPasswordManager::TournamentPasswordManager(LogSet logs, TokenCallbackT tokenCallbackFunction, int threadPriority) throw () :
    log(logs),
    tokenCallback(tokenCallbackFunction),
    quitThread(true),
    passStatus(PS_noPassword),
    priority(threadPriority),
    servStatus(PS_noPassword), // no server
    token("TournamentPasswordManager::token")
{ }

void TournamentPasswordManager::stop() throw () {
    if (!quitThread) {
        log("Joining password-token thread");
        quitThread = true;
        thread.join();
    }
}

void TournamentPasswordManager::changeData(const string& newName, const string& newPass) throw () {
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

string TournamentPasswordManager::statusAsString() const throw () {
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

void TournamentPasswordManager::threadFn() throw () {
    bool newToken = true;
    int delay = 0;  // given a value in MS before each continue: this time will be waited before next round

    while (!quitThread) {
        if (delay > 0) {
            platSleep(500);
            delay -= 500;
            continue;
        }
        delay = 60000;  // default to one minute

        string response;
        try {
            Network::TCPSocket sock(Network::NonBlocking, 0, true);

            Network::Address tournamentServer;
            if (!tournamentServer.tryResolve("www.mycgiserver.com"))
                tournamentServer.fromValidIP("64.69.35.205");

            tournamentServer.setPort(80);
            sock.connect(tournamentServer);
            const string query = build_http_request(false, "www.mycgiserver.com", "/servlet/fcecin.tk1/index.html",
                                                    url_encode(TK1_VERSION_STRING) +
                                                    '&' + (newToken ? "new" : "old") +
                                                    "&name=" + url_encode(name) +
                                                    "&password=" + url_encode(password));

            passStatus = PS_sending;
            if (newToken)
                log("Password thread: Sending login");
            sock.persistentWrite(query, &quitThread, 30000);

            passStatus = PS_receiving;
            ostringstream respStream;
            sock.readAll(respStream, &quitThread, 30000);
            sock.close();
            const string fullResponse = respStream.str();

            // find the start and end of the body: after the last "<html>" and before the last "</html>"
            // the original code uses full case insensitivity so response.find_last_of() can't be used
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
        } catch (Network::ExternalAbort) {
            break;
        } catch (const Network::Error& e) {
            log("Password thread: %s", e.str().c_str());
            passStatus = (passStatus == PS_sending ? PS_sendError : passStatus == PS_receiving ? PS_recvError : PS_socketError);
            delay = 15000; // retry faster
            continue;
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
}

bool ServerListEntry::setAddress(const string& address) throw () {
    if (!isValidIP(address, true, 1))
        return false;
    addr.fromValidIP(address);
    if (addr.getPort() == 0)
        addr.setPort(DEFAULT_UDP_PORT);
    return true;
}

void ServerListEntry::setAddress(const Network::Address& address) throw () {
    addr = address;
    if (addr.getPort() == 0)
        addr.setPort(DEFAULT_UDP_PORT);
}

string ServerListEntry::addressString() const throw () {
    if (addr.getPort() != DEFAULT_UDP_PORT)
        return addr.toString();
    else {
        Network::Address cpy = addr;
        cpy.setPort(0);
        return cpy.toString();
    }
}

FileDownload::FileDownload(const string& type, const string& name, const string& filename) throw () :
    fileType(type),
    shortName(name),
    fullName(filename),
    fp(0)
{ }

FileDownload::~FileDownload() throw () {
    if (fp) {
        fclose(fp);
        remove(fullName.c_str());
    }
}

int FileDownload::progress() const throw () {
    nAssert(fp);
    return ftell(fp);
}

bool FileDownload::start() throw () {
    nAssert(!fp);
    fp = fopen(fullName.c_str(), "wb");
    return (fp != 0);
}

bool FileDownload::save(ConstDataBlockRef data) throw () {
    nAssert(fp);
    return (fwrite(data.data(), 1, data.size(), fp) == data.size());
}

void FileDownload::finish() throw () {
    nAssert(fp);
    fclose(fp);
    fp = 0;
}

void TM_ServerSettings::addLine(Client* cl, const string& caption, const string& value) const throw () {
    const int capWidth = 25;
    cl->m_serverInfo.addLine(pad_to_size_left(caption, capWidth), value);
}

void TM_ServerSettings::execute(Client* cl) const throw () {
    cl->m_serverInfo.clear();
    cl->m_serverInfo.menu.setCaption(cl->hostname);

    addLine(cl, _("Capture limit"       ), ( caplimit == 0) ? _("none") :             itoa( caplimit));
    addLine(cl, _("Time limit"          ), (timelimit == 0) ? _("none") : _("$1 min", itoa(timelimit)));
    if (timelimit != 0)
        addLine(cl, _("Extra-time"      ), (extratime == 0) ? _("none") : _("$1 min", itoa(extratime)));
    if (flag_return_delay != -1)
        addLine(cl, _("Flag return delay"   ), _("$1 s", fcvt(flag_return_delay / 10., 1)));
    addLine(cl, _("Player collisions"   ),  cl->fx.physics.player_collisions == PhysicalSettings::PC_none ? _("off") :
                                            cl->fx.physics.player_collisions == PhysicalSettings::PC_normal ? _("on") : _("special"));
    addLine(cl, _("Friendly fire"       ), (cl->fx.physics.friendly_fire == 0.) ? _("off") : _("$1%", (itoa(iround(100. * cl->fx.physics.friendly_fire)))));

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

    if (cl->menu.options.game.showServerInfo() && !cl->replaying)
        cl->showMenu(cl->m_serverInfo);
}
#endif

void TM_ConnectionUpdate::execute(Client* cl) const throw () {
    switch (code) {
    /*break;*/ case 0: cl->client_connected(data);
        break; case 1: cl->client_disconnected(data);
        #ifndef DEDICATED_SERVER_ONLY
        break; case 2: cl->connect_failed_denied(data);
        break; case 3: cl->connect_failed_unreachable();
        break; case 5: cl->connect_failed_socket();
        break; case 4: cl->connect_failed_denied(_("The server is full."));
        break; default: nAssert(0);
        #endif
    }
    if (cl->botmode && code != 0)
        cl->stop();
}

void Client::ConstDisappearedFlagIterator::findValid() throw () {
    for (; valid(); next()) {
        const Flag& fl = flag();
        if (!fl.carried())
            continue;
        const ClientPlayer& pl = c.fx.player[fl.carrier()];
        const WorldCoords& pos = fl.position();
        nAssert(pos.px >= 0 && pos.py >= 0);
        if (pl.used && pos.px < c.fx.map.w && pos.py < c.fx.map.h && !pl.onscreen && pl.posUpdated > c.fx.frame - 300.)
            return;
    }
}

Client::Client(const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, Log& clientLog, MemoryLog& externalErrorLog_) throw () :
    externalErrorLog(externalErrorLog_),
    errorLog(clientLog, externalErrorLog, "ERROR: "),
    //securityLog(clientLog, "SECURITY WARNING: ", wheregamedir + "log" + directory_separator + "client_securitylog.txt", false),
    log(&clientLog, &errorLog, 0),
    #ifndef DEDICATED_SERVER_ONLY
    listenServer(log),
    #endif
    frameMutex("Client::frameMutex"),
    downloadMutex("Client::downloadMutex"),
    #ifndef DEDICATED_SERVER_ONLY
    tournamentPassword(log, new RedirectToMemFun1<Client, void, string>(this, &Client::CB_tournamentToken), config.lowerPriority),
    mapInfoMutex("Client::mapInfoMutex"),
    mapListSortKey(MLSK_Number),
    mapListChangedAfterSort(false),
    current_map(-1),
    map_vote(-1),
    player_stats_page(0),
    lastAltEnterTime(0),
    FPS(0),
    framecount(0),
    totalframecount(0),
    frameCountStartTime(0),
    serverListMutex("Client::serverListMutex"),
    botmode(false),
    #endif
    finished(false),
    botPrevFire(false),
    abortThreads(false),
    #ifndef DEDICATED_SERVER_ONLY
    refreshStatus(RS_none),
    password_file(wheregamedir + "config" + directory_separator + "passwd"),
    graphics(log),
    screenshot(false),
    replaying(false),
    visible_rooms(1),
    spectating(false),
    #endif
    mapChanged(false),
    #ifndef DEDICATED_SERVER_ONLY
    client_sounds(log),
    messageLogOpen(false),
    #endif
    extConfig(config)
    #ifndef DEDICATED_SERVER_ONLY
    , serverExtConfig(serverConfig)
    #endif
{
    //net client
    client = 0;

    setMaxPlayers(MAX_PLAYERS);
    for (int p = 0; p < MAX_PLAYERS; p++)
        fx.player[p].used = false;

    //wich player I am
    me = -1;

    //time of last packet received
    lastpackettime = 0;

    //game showing?
    gameshow = false;

    #ifndef DEDICATED_SERVER_ONLY
    //if player wants to changeteams
    want_change_teams = false;
    #else
    (void)serverConfig;
    #endif

    //connected? (that is, "connection accepted")
    connected = false;

    Thread::setCallerPriority(config.priority);
}

Client::~Client() throw () {
    log("Exiting client: destructor");

    abortThreads = true;
    if (client) {
        delete client;
        client = 0;
    }
    #ifndef DEDICATED_SERVER_ONLY
    while (refreshStatus != RS_none && refreshStatus != RS_failed)  // wait for a possible refresh thread to abort itself
        platSleep(50);
    #endif

    for (deque<ThreadMessage*>::const_iterator mi = messageQueue.begin(); mi != messageQueue.end(); ++mi)
        delete *mi;

    log("Exiting client: destructor exiting");
}

bool Client::start() throw () {
    #ifndef DEDICATED_SERVER_ONLY
    if (!botmode) {
        extConfig.statusOutput(_("Outgun client"));
        initMenus();
        showMenu(menu);
    }
    menusel = menu_none;

    totalframecount = 0;
    framecount = 0;
    #endif

    clFrameSent = clFrameWorld = 0;
    fx.frame = -1;
    #ifndef DEDICATED_SERVER_ONLY
    fd.frame = -1;
    #endif
    frameReceiveTime = 0;

    frameOffsetDeltaTotal = 0;
    frameOffsetDeltaNum = 0;
    averageLag = 0;

    netsendAdjustment = 0;

    // default map
    map_ready = false;  // NO map change commands from server yet
    clientReadiesWaiting = 0;

    //not showing gameover plaque
    gameover_plaque = NEXTMAP_NONE;

    connected = false;

    client = new_client_c(extConfig.networkPriority, botmode ? "_bot" + itoa(botId) : "");
    client->setCallbackCustomPointer(this);
    client->setConnectionCallback(cfunc_connection_update);
    client->setServerDataCallback(cfunc_server_data);

    #ifndef DEDICATED_SERVER_ONLY
    if (botmode)
        return true;

    if (language.code() == "fi")
        playername = finnish_name(maxPlayerNameLength);
    else
        playername = RandomName();

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
            menu.options.player.password.set(pas);
        }
        fclose(psf);
    }

    //try to load client configuration
    vector<int> fav_colors;

    fileName = wheregamedir + "config" + directory_separator + "client.cfg";
    ifstream cfg(fileName.c_str());
    for (;;) {
        string line;
        if (!getline_skip_comments(cfg, line))
            break;

        istringstream command(line);

        int iSettingId;
        command >> iSettingId;
        command.ignore();   // eat separator (space)
        if (!command || iSettingId < 0) {
            log.error(_("Invalid syntax in client.cfg (\"$1\").", line));
            continue;
        }
        if (iSettingId >= CCS_EndOfCommands) {
            log.error(_("Unknown data in client.cfg (\"$1\").", line));
            continue;
        }

        string args;
        getline(command, args); // this might fail, but that only means there is an empty string

        const ClientCfgSetting settingId = static_cast<ClientCfgSetting>(iSettingId);

        SettingCollector::SaverLoader* sl = settings.find(settingId);
        if (sl) {
            sl->load(args);
            continue;
        }

        // some more complicated settings need to be handled here
        switch (settingId) {
        /*break;*/ case CCS_PlayerName:
                if (check_name(args))
                    playername = args;

            break; case CCS_FavoriteColors: {
                istringstream ist(args);
                int col;
                while (ist >> col)
                    if (col >= 0 && col < 16 && find(fav_colors.begin(), fav_colors.end(), col) == fav_colors.end())
                        fav_colors.push_back(col);
            }

            break; case CCS_KeyboardLayout:
                if (!menu.options.controls.keyboardLayout.set(args)) {  // it is possible to have a layout Outgun doesn't know about
                    menu.options.controls.keyboardLayout.addOption(_("unknown ($1)", args), args);
                    menu.options.controls.keyboardLayout.set(args);
                }

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
                    menu.options.screenMode.update(graphics);  // fetch resolutions according to the new depth
                    if (!menu.options.screenMode.resolution.set(ScreenMode(width, height)))
                        log("Previous screen mode not available (%d×%d×%d)", width, height, depth);
                }
            }

            break; case CCS_Antialiasing:
                menu.options.graphics.antialiasing.set(args == "2");

            break; case CCS_Tournament:
                ;   // skip

            break; default: nAssert(0); // all values up to the highest known must be handled
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

    // player
    tournamentPassword.changeData(playername, menu.options.player.password());
    for (int i = 0; i < 16; i++)
        if (find(fav_colors.begin(), fav_colors.end(), i) == fav_colors.end())
            fav_colors.push_back(i);
    for (vector<int>::const_iterator col = fav_colors.begin(); col != fav_colors.end(); ++col)
        menu.options.player.favoriteColors.addOption(*col);

    // game
    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        openMessageLog();

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
    MCF_antialiasChange();
    visible_rooms = menu.options.graphics.visibleRoomsPlay();
    MCF_transpChange();
    MCF_statsBgChange();
    if (!screenModeChange())
        return false;
    MCF_gfxThemeChange();
    MCF_fontChange();

    // sounds
    if (extConfig.nosound)
        menu.options.sounds.enabled.set(false);
    MCF_sndEnableChange();
    client_sounds.setVolume(menu.options.sounds.volume());
    MCF_sndThemeChange();

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
    #endif

    return true;
}

#ifndef DEDICATED_SERVER_ONLY
void Client::language_selection_start(volatile bool* quitFlag) throw () {
    log("language_selection_start()");
    extConfig.statusOutput(_("Outgun client"));

    if (!graphics.init(640, 480, desktop_color_depth(), true, false))
        return;

    typedef MenuCallback<Client> MCB;
    m_initialLanguage.initialize(new MCB::A<Menu, &Client::MCF_menuOpener>(this), settings);

    const int menu_selection_index = refreshLanguages(m_initialLanguage);
    m_initialLanguage.addHooks(new MCB::A<Textarea, &Client::MCF_acceptInitialLanguage>(this));
    m_initialLanguage.menu.setSelection(menu_selection_index);

    showMenu(m_initialLanguage);

    while (!openMenus.empty()) {
        if (*quitFlag)
            return;
        if (keyboard_needs_poll())
            poll_keyboard();    // ignore return value
        // handle waiting keypresses
        while (keypressed()) {
            int ch = readkey();
            const int sc = (ch >> 8);
            ch &= 0xFF;
            if (sc == KEY_F11)
                screenshot = true;
            openMenus.handleKeypress(sc, ch);
        }

        // give other threads a chance (otherwise we're trying to run all the time if the FPS limit is not lower than what the machine can do)
        sched_yield();

        graphics.startDraw();
        graphics.draw_background(false);
        draw_game_menu();
        graphics.endDraw();
        graphics.draw_screen(false);
        if (screenshot) {
            save_screenshot();
            screenshot = false;
        }
    }
}
#endif

void Client::bot_start(const Network::Address& addr, int ping, const string& name_lang, int bot_id) throw () {
    Lock ml(frameMutex);
    #ifndef DEDICATED_SERVER_ONLY
    botmode = true;
    #endif
    botId = bot_id;
    serverIP = addr;

    nAssert(start());

    if (name_lang == "fi")
        playername = "BOT " + finnish_name(maxPlayerNameLength - 4);
    else
        playername = ("BOT " + RandomName()).substr(0, maxPlayerNameLength);
    botReactedFrame = -1;

    set_ping(ping);

    connect_command(false);
}

void Client::set_ping(int ping) throw () {
    while (client->decreasePacketDelay());
    for (int i = 0; i < ping / 10; ++i)
        client->increasePacketDelay();
}

//send "client ready" message to server (when map load and/or download completes)
void Client::send_client_ready() throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_client_ready);
    client->send_message(msg);
}

#ifndef DEDICATED_SERVER_ONLY
// incoming chunk of requested file by UDP
void Client::process_udp_download_chunk(ConstDataBlockRef data, bool last) throw () {
    Lock ml(downloadMutex);
    if (downloads.empty() || !downloads.front().isActive()) {
        log.error("Server sent a file we aren't expecting");
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    FileDownload& dl = downloads.front();
    if (!dl.save(data)) {
        log.error(_("Error writing to '$1'.", dl.fullName));
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    //send the reply
    BinaryBuffer<256> msg;
    msg.U8(data_file_ack);
    client->send_message(msg);
    if (last) {
        dl.finish();
        log("Download complete: %s '%s' to %s", dl.fileType.c_str(), dl.shortName.c_str(), dl.fullName.c_str());
        if (dl.fileType == "map") {
            if (dl.shortName == servermap) {
                const bool ok = fd.load_map(log, CLIENT_MAPS_DIR, dl.shortName) && fx.load_map(log, CLIENT_MAPS_DIR, dl.shortName);
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
void Client::check_download() throw () { // call with downloadMutex locked
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
    BinaryBuffer<256> msg;
    msg.U8(data_file_request);
    msg.str(dl.fileType);
    msg.str(dl.shortName);
    client->send_message(msg);
}

void Client::download_server_file(const string& type, const string& name) throw () {
    nAssert(type == "map");
    if (name.find_first_of("./:\\") != string::npos) {
        log.error("Illegal file download request: map \"" + name + "\"");
        addThreadMessage(new TM_DoDisconnect());
        return;
    }

    Lock ml(downloadMutex);
    const string fileName = wheregamedir + CLIENT_MAPS_DIR + directory_separator + name + ".txt";
    downloads.push_back(FileDownload(type, name, fileName));
    check_download();
}
#endif

// Server tells client of current map / map change.
// Client checks from the "cmaps" and "maps" directory.
// If the map file is not there, or the CRC's don't match, download the map from the server to "cmaps".
void Client::server_map_command(const string& mapname, uint16_t server_crc) throw () {
    log("Received map change: '%s'", mapname.c_str());

    servermap = mapname;

    // Try to load the map from "cmaps", "maps" or even from "maps/generated" if necessary.
    if (load_map(CLIENT_MAPS_DIR, mapname, server_crc) || load_map(SERVER_MAPS_DIR, mapname, server_crc) ||
          load_map(string() + SERVER_MAPS_DIR + directory_separator + "generated", mapname, server_crc)) {
        log("Map '%s' loaded successfully.", mapname.c_str());
        remove_useless_flags();
        mapChanged = true;
        map_ready = true;
        ++clientReadiesWaiting;
        return;
    }

    nAssert(!botmode); // ### FIX: Disconnect bot or something.

    #ifndef DEDICATED_SERVER_ONLY
    // start download
    const string msg = _("Downloading map \"$1\" (CRC $2)...", mapname, itoa(server_crc));
    print_message(msg_info, msg);
    log("%s", msg.c_str());

    download_server_file("map", mapname);
    #endif
}

bool Client::load_map(const string& directory, const string& mapname, uint16_t server_crc) throw () {
    LogSet noLogSet(0, 0, 0);   // if there's an error with the map, don't log it

    const bool ok =
        #ifndef DEDICATED_SERVER_ONLY
        fd.load_map(noLogSet, directory, mapname) &&
        #endif
        fx.load_map(noLogSet, directory, mapname);

    if (!ok)
        log("Map '%s' not found in '%s'.", mapname.c_str(), directory.c_str());
    else if (fx.map.crc != server_crc)
        log("Map '%s' found in '%s' but its CRC %i differs from server map CRC %i.",
            mapname.c_str(), directory.c_str(), fx.map.crc, server_crc);
    else
        return true;
    return false;
}

void Client::sendMinimapBandwidthAny(int players) throw () {
    if (protocolExtensions >= 0) {
        BinaryBuffer<256> msg;
        msg.U8(data_set_minimap_player_bandwidth);
        msg.U8(players);
        client->send_message(msg);
    }
}

#ifndef DEDICATED_SERVER_ONLY
void Client::sendFavoriteColors() throw () {
    if (menu.options.player.favoriteColors.values().empty())
        return;
    BinaryBuffer<256> msg;
    msg.U8(data_fav_colors);
    msg.U8(menu.options.player.favoriteColors.values().size());
    // send two colours in a byte
    for (vector<int>::const_iterator col = menu.options.player.favoriteColors.values().begin();
                                    col != menu.options.player.favoriteColors.values().end(); ++col) {
        uint8_t byte = static_cast<uint8_t>(*col) & 0x0F;
        if (++col != menu.options.player.favoriteColors.values().end())
            byte |= static_cast<uint8_t>(*col) << 4;
        msg.U8(byte);
    }
    client->send_message(msg);
}

void Client::sendMinimapBandwidth() throw () {
    sendMinimapBandwidthAny(menu.options.game.minimapBandwidth() / 20);
}
#endif // DEDICATED_SERVER_ONLY

void Client::disconnect_command() throw () { // do not call from a network thread
    //disconnect the client here if was connected, else does nothing
    client->connect(false);
}

void Client::client_connected(ConstDataBlockRef data) throw () {   // call with frameMutex locked
    log("Connection successful");

    connected = true;
    gameshow = true;

    BinaryDataBlockReader read(data);

    setMaxPlayers(read.U8(0, MAX_PLAYERS));

    #ifndef DEDICATED_SERVER_ONLY
    hostname = read.str();
    #else
    read.str();
    #endif

    if (read.hasMore()) {
        const int protoExt = read.U8();
        log("Protocol extensions enabled. Server: %d (client: %d; using the smaller)", protoExt, PROTOCOL_EXTENSIONS_VERSION);
        protocolExtensions = min(protoExt, PROTOCOL_EXTENSIONS_VERSION);
    }
    else
        protocolExtensions = -1;

    while (read.hasMore()) {
        const uint32_t extensionId = read.U32();
        BinaryDataBlockReader extData(read.block(read.U8()));
        switch (extensionId) {
            /* To negotiate unofficial extension "example" at connection time, insert something like this: (search for "unofficial extension" for other relevant parts)
             * break; case EXAMPLE_IDENTIFIER:
             *    exampleLevel = min(extData.U8(), EXAMPLE_VERSION); // or whatever else you sent in servnet.cpp; also remember to flag the extension disabled before this "while (read.hasMore())"
             */
        }
    }

    if (botmode) {
        // Tell server that I am a bot.
        BinaryBuffer<256> msg;
        msg.U8(data_bot);
        client->send_message(msg);

        sendMinimapBandwidthAny(32);
    }

    //avoid "dropped" plaque
    lastpackettime = get_time() + 4.0;

    averageLag = 0;

    clFrameSent = clFrameWorld = 0;
    frameReceiveTime = 0;

    remove_flags = 0;

    issue_change_name_command();

    map_ready = false;
    clientReadiesWaiting = 0;
    servermap.clear();

    gameover_plaque = NEXTMAP_NONE;

    gunDir.from8way(0);
    gunDirRefreshedTime = get_time();
    next_respawn_time = 0;
    flag_return_delay = -1;

    me = -1;    // will be corrected from the first frame

    for (int i = 0; i < 2; i++) {
        fx.teams[i].clear_stats();
        fx.teams[i].remove_flags();
    }
    for (int i = 0; i < MAX_PLAYERS; i++)
        fx.player[i].clear(false, i, "", i / TSIZE);
    fx.reset();

    fx.frame = -1;
    fx.skipped = true;
    fx.physics = PhysicalSettings(); // to be filled later by a message

    if (botmode) {
        bot_send_frame(ClientControls());
        return;
    }

    lock_team_flags_in_effect = false;
    lock_wild_flags_in_effect = false;
    capture_on_team_flags_in_effect = true;
    capture_on_wild_flags_in_effect = false;

    #ifndef DEDICATED_SERVER_ONLY

    fd.reset();
    fd.frame = -1;
    fd.skipped = true;
    fd.physics = fx.physics;

    m_serverInfo.clear();
    m_serverInfo.addLine("");   // can't draw a totally empty menu; this will be overwritten with config information

    if (!botmode) {
        sendFavoriteColors();
        sendMinimapBandwidth();

        extConfig.statusOutput(_("Connected to $1 ($2)", hostname.substr(0, 32), serverIP.toString()));
    }

    show_all_messages = false;
    stats_autoshowing = false;

    //don't want to change teams by default
    want_change_teams = false;

    //don't want to exit map by default
    want_map_exit = false;
    want_map_exit_delayed = false;

    deadAfterHighlighted = true;

    openMenus.clear();  // connect progress menu is showing; exceptions are when it's been closed and the disconnect is still pending, and when help is opened on top of it

    talkbuffer.clear();
    talkbuffer_cursor = 0;
    chatbuffer.clear();

    players_sb.clear();

    //reset FPS count vars
    framecount = 0;
    frameCountStartTime = get_time();
    FPS = 0;

    //reset map time
    map_time_limit = false;
    map_start_time = 0;
    map_end_time = 0;
    extra_time_running = false;
    map_vote = -1;

    // send registration token (if any)
    const string s = tournamentPassword.getToken();
    if (!s.empty())
        CB_tournamentToken(s);
    send_tournament_participation();

    {
        Lock ml(mapInfoMutex);
        maps.clear();
        mapListChangedAfterSort = true;
    }

    //clear client side effects
    graphics.clear_fx();

    mouseClicked.clear();

    send_frame(true, true);
    #endif
}

#ifndef DEDICATED_SERVER_ONLY
void Client::send_tournament_participation() throw () {
    BinaryBuffer<8> msg;
    msg.U8(data_tournament_participation);
    msg.U8(menu.options.player.tournament() ? 1 : 0);
    client->send_message(msg);
}
#endif

void Client::client_disconnected(ConstDataBlockRef data) throw () {
    if (!connected)
        return;

    connected = false;
    gameshow = false;

    BinaryDataBlockReader read(data);

    if (botmode) {
        const uint8_t reason = read.U8();
        numAssert2(!read.hasMore() && (reason == server_c::disconnect_client_initiated || reason == server_c::disconnect_server_shutdown
                                       || reason == server_c::disconnect_timeout || reason == disconnect_kick),
                   data.size(), reason);
        return;
    }

    #ifndef DEDICATED_SERVER_ONLY
    //restore window title
    extConfig.statusOutput(_("Outgun client"));

    menusel = menu_none;

    string description;
    if (data.size() == 1)
        switch (read.U8()) {
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
        Lock ml(downloadMutex);
        downloads.clear();
    }
    #endif
}

#ifndef DEDICATED_SERVER_ONLY
void Client::connect_failed_denied(ConstDataBlockRef data) throw () {
    string message;
    bool userHandled = false;
    if (data.size() > 1) {
        BinaryDataBlockReader read(data);
        message = read.str();
        const string str1 = "Protocol mismatch: server: ";
        const string str2 = ", client: " + GAME_PROTOCOL;
        const string::size_type str2pos = message.length() - str2.length();
        if (message.compare(0, str1.length(), str1) == 0 && str2pos > 0 && message.compare(str2pos, str2.length(), str2) == 0) {
            const string serverProtocol = message.substr(str1.length(), str2pos - str1.length());
            message = _("Protocol mismatch. Server: $1, client: $2.", serverProtocol, GAME_PROTOCOL);
        }
        // otherwise leave message at its value of whatever the server sent
    }
    else if (data.size() == 1) {
        BinaryDataBlockReader read(data);
        const uint8_t rb = read.U8();
        if (rb > reject_last)
            message = _("Unknown reason code ($1).", itoa(rb));
        else {
            switch (static_cast<Connect_rejection_reason>(rb)) {
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
                    remove_player_password(playername, serverIP.toString());
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

void Client::connect_failed_unreachable() throw () {
    m_connectProgress.wrapLine(_("No response from server."));
    // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    log("Connecting failed: no response");
}

void Client::connect_failed_socket() throw () {
    m_connectProgress.wrapLine(_("Can't open socket."));
    // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    log("Connecting failed: no response");
}

string Client::load_player_password(const string& name, const string& address) const throw () {
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

vector<vector<string> > Client::load_all_player_passwords() const throw () {
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

void Client::save_player_password(const string& name, const string& address, const string& password) const throw () {
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

void Client::remove_player_password(const string& name, const string& address) const throw () {
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

int Client::remove_player_passwords(const string& name) const throw () {
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
#endif

void Client::connect_command(bool loadPassword) throw () {   // call with frameMutex locked
    const bool alreadyConnected = connected;

    // disconnect
    client->connect(false);
    #ifndef DEDICATED_SERVER_ONLY
    stop_replay();
    #endif

    if (alreadyConnected)   // very basic and ugly hack to let the disconnection take place at least semi-reliably; this is needed because Leetnet sucks
        platSleep(500);

    handlePendingThreadMessages();  // this is needed so that the potential disconnection message doesn't screw up the new connection
    #ifndef DEDICATED_SERVER_ONLY
    openMenus.close(&m_connectProgress.menu);
    #endif

    // start connecting to specified IP/port
    // connection results will come through the CFUNC_CONNECTION_UPDATE callback
    const string strAddress = serverIP.toString();
    client->set_server_address(strAddress.c_str());

    #ifndef DEDICATED_SERVER_ONLY
    if (loadPassword && !botmode)
        m_playerPassword.password.set(load_player_password(playername, strAddress));

    log("Connecting to %s... passwords: server %s, player %s", strAddress.c_str(),
        m_serverPassword.password().empty() ? "no" : "yes",
        m_playerPassword.password().empty() ? "no" : "yes");
    #endif

    //set connect-data (goes in every connect packet): outgun game name and protocol strings
    BinaryBuffer<256> msg;
    msg.str(GAME_STRING);
    msg.str(GAME_PROTOCOL);
    msg.str(playername);
    if (botmode) {
        msg.str(bot_password);
        msg.str("");
    }
    #ifndef DEDICATED_SERVER_ONLY
    else {
        msg.str(m_serverPassword.password());    // empty or not, it's needed
        msg.str(m_playerPassword.password());    // empty or not, it's needed
    }
    #endif
    msg.U8(PROTOCOL_EXTENSIONS_VERSION);
    /* To negotiate unofficial extension "example" at connection time, insert something like this: (search for "unofficial extension" for other relevant parts)
     * msg.U32(EXAMPLE_IDENTIFIER);
     * msg.U8(1); // the number of bytes of what is added to msg by this extension after this
     * msg.U8(EXAMPLE_VERSION); // or whatever else you want to send (here's a possible idea: send the value of data_example_first this client wants to use with the extension, to make it changeable if necessary for mixing extensions)
     */
    client->set_connect_data(msg);
    client->connect(true, extConfig.minLocalPort, extConfig.maxLocalPort);

    #ifndef DEDICATED_SERVER_ONLY
    if (!botmode) {
        m_connectProgress.clear();
        m_connectProgress.wrapLine(_("Trying to connect..."), true);
        showMenu(m_connectProgress);
    }
    #else
    (void)loadPassword;
    #endif
}

void Client::issue_change_name_command() throw () {
    if (!connected)
        return;
    //regular change name
    BinaryBuffer<256> msg;
    msg.U8(data_name_update);
    nAssert(check_name(playername));
    msg.str(playername);
    #ifndef DEDICATED_SERVER_ONLY
    msg.str(m_playerPassword.password());    // empty or not, it's needed
    #else
    msg.str("");
    #endif
    client->send_message(msg);
}

void Client::bot_send_frame(ClientControls controls) throw () {
    ++clFrameSent;
    controlHistory[clFrameSent] = sentControls = controls;
    svFrameHistory[clFrameSent] = fx.frame + (get_time() - frameReceiveTime) * 10.;
    BinaryBuffer<256> msg;
    msg.U8(clFrameSent);
    msg.U8(sentControls.toNetwork(false));
    if (fx.physics.allowFreeTurning)
        msg.U16(gunDir.toNetworkLongForm());
    client->send_frame(msg);
}

#ifndef DEDICATED_SERVER_ONLY
void Client::change_name_command() throw () {
    //set new name, close menu
    menu.options.player.name.set(trim(menu.options.player.name()));
    const string& newName = menu.options.player.name();
    if (!check_name(newName))
        return;
    openMenus.close(&menu.options.player.menu);

    playername = newName;
    if (serverIP.valid())
        m_playerPassword.password.set(load_player_password(playername, serverIP.toString()));
    issue_change_name_command();
    tournamentPassword.changeData(playername, menu.options.player.password());
}

ClientControls Client::readControls(bool canUseKeypad, bool useCursorKeys) const throw () {
    ClientControls ctrl;
    ctrl.fromKeyboard(canUseKeypad && menu.options.controls.keypadMoving(), useCursorKeys);
    if (menu.options.controls.joystick())
        ctrl.fromJoystick(menu.options.controls.joyMove() - 1, menu.options.controls.joyRun(), menu.options.controls.joyStrafe());
    if (mouse_b & (1 << menu.options.controls.mouseRun() - 1))
        ctrl.setRun();
    return ctrl;
}

ClientControls Client::readControlsInGame() const throw () {
    if (!openMenus.empty()) // don't move at all when a real menu is open
        return ClientControls();
    const bool text_input_in_use = !talkbuffer.empty() && menu.options.controls.arrowKeysInTextInput();
    const bool forceUseCursorKeys = menu.options.controls.arrowKeysInStats() == Menu_controls::AS_movePlayer;
    ClientControls ctrl = readControls(menusel != menu_maps,
                                       menusel == menu_none && !text_input_in_use || forceUseCursorKeys); // reserve cursor keys for stats screen or similar unless forced
    ctrl.clearModifiersIfIdle();
    return ctrl;
}

bool Client::firePressed() throw () {
    static double checkTime = 0;
    if (get_time() > checkTime + 1.)
        mouseClicked.clear();
    checkTime = get_time();
    const bool click = mouseClicked.wasClicked(menu.options.controls.mouseShoot() - 1);
    return key[KEY_LCONTROL] || key[KEY_RCONTROL] || (menu.options.controls.joystick() && readJoystickButton(menu.options.controls.joyShoot())) ||
           (mouse_b & (1 << menu.options.controls.mouseShoot() - 1)) || click;
}

//send the client's frame to server (keypresses)
void Client::send_frame(bool newFrame, bool forceSend) throw () {
    static double keyFilterTimeout = 0;

    ClientControls currentControls = readControlsInGame();
    if (menu.options.controls.aimMode() == Menu_controls::AM_Turn && !currentControls.isStrafe()) {
        currentControls.clearLeft();
        currentControls.clearRight();
    }

    if (!forceSend && currentControls == sentControls)
        return;

    bool filtered;
    // filtering is applied when first going diagonally and then one of the directions is dropped
    if (sentControls.isUpDown() && sentControls.isLeftRight() && currentControls.isUpDown() != currentControls.isLeftRight()) {
        if (currentControls.isUpDown() ? currentControls.isUp() != sentControls.isUp() : currentControls.isLeft() != sentControls.isLeft())
            filtered = false;
        else if (keyFilterTimeout == 0) {
            filtered = true;
            keyFilterTimeout = get_time() + .02;
        }
        else
            filtered = get_time() < keyFilterTimeout;
    }
    else
        filtered = false;

    if (!filtered) {
        keyFilterTimeout = 0;
        sentControls = currentControls;
    }
    else if (!forceSend)
        return;

    if (newFrame) {
        ++clFrameSent;
        controlHistory[clFrameSent] = sentControls;
        svFrameHistory[clFrameSent] = fx.frame + (get_time() - frameReceiveTime) * 10.;
    }
    else if (fx.frame + (get_time() - frameReceiveTime) * 10. < svFrameHistory[clFrameSent] + .5) // guess that these controls get to the server during the same frame that the first sent controls do
        controlHistory[clFrameSent] = sentControls;

    BinaryBuffer<256> msg;
    msg.U8(clFrameSent);
    msg.U8(sentControls.toNetwork(false));
    if (fx.physics.allowFreeTurning && menu.options.controls.aimMode() != Menu_controls::AM_8way) {
        refreshGunDir();
        const AccelerationMode am = menu.options.controls.aimMode() == Menu_controls::AM_Mouse ? menu.options.controls.moveRelativity() : AM_Gun;
        msg.U16((am == AM_Gun ? 0x800 : 0) | gunDir.toNetworkLongForm());
    }
    client->send_frame(msg);
}

void Client::refreshGunDir() throw () {
    if (!fx.physics.allowFreeTurning)
        return;
    if (menu.options.controls.aimMode() == Menu_controls::AM_Mouse) {
        int mx, my;
        get_mouse_mickeys(&mx, &my);
        gunDir.adjust(mx * menu.options.controls.mouseSensitivity() / 5000.);
    }
    else if (menu.options.controls.aimMode() == Menu_controls::AM_Turn) {
        g_timeCounter.refresh();
        const double time = get_time() - gunDirRefreshedTime;
        gunDirRefreshedTime = get_time();
        const ClientControls ctrl = readControlsInGame();
        if (!ctrl.isStrafe() && ctrl.isLeftRight()) {
            // turningSpeed ranges from 0 to 100; we want to scale it so that the minimal speed is 1/4 of a revolution per second (2 in GunDirection units); a reasonable maximum is 4 revolutions (32)
            static const double sMin = 1, sMax = 32;
            // we want to have e^100x == sMax/sMin => 100x == ln(sMax/sMin)
            static const double X = ::log(sMax / sMin) / 100.;
            const double mul = ctrl.isLeft() ? -1 : +1;
            gunDir.adjust(mul * time * sMin * exp(menu.options.controls.turningSpeed() * X));
        }
    }
}

#endif // DEDICATED_SERVER_ONLY

void Client::readMinimapPlayerPosition(BinaryReader& reader, int pid) throw () {
    const uint8_t whox = reader.U8(), whoy = reader.U8();
    if (pid == me || fx.player[pid].onscreen)
        return;
    const double oldx = fx.player[pid].roomx * plw + fx.player[pid].lx;
    const double oldy = fx.player[pid].roomy * plh + fx.player[pid].ly;
    const int xmul = 255 / fx.map.w;
    const int ymul = 255 / fx.map.h;
    const double xStep = double(plw) / xmul;
    const double yStep = double(plh) / ymul;
    fx.player[pid].roomx = whox / xmul;
    fx.player[pid].roomy = whoy / ymul;
    fx.player[pid].lx = (whox % xmul + .5) * xStep;
    fx.player[pid].ly = (whoy % ymul + .5) * yStep;
    fx.player[pid].posUpdated = fx.frame;
    double newx = fx.player[pid].roomx * plw + fx.player[pid].lx;
    double newy = fx.player[pid].roomy * plh + fx.player[pid].ly;
    if (fabs(newx - oldx) > fx.map.w * plw / 2)
        newx += fx.map.w * plw * (newx < oldx ? +1 : -1);
    if (fabs(newy - oldy) > fx.map.h * plh / 2)
        newy += fx.map.h * plh * (newy < oldy ? +1 : -1);
    const double dx = newx - oldx, dy = newy - oldy;
    if (fabs(dx) >= xStep || fabs(dy) >= yStep)
        fx.player[pid].gundir.fromRad(atan2(dy, dx));
}

bool Client::process_live_frame_data(ConstDataBlockRef data) throw () { // returns false if an error occured that requires disconnecting
    BinaryDataBlockReader read(data);

    const uint32_t svframe = read.U32();    //server's frame

    #ifndef DEDICATED_SERVER_ONLY
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
    #endif
    if (svframe < fx.frame)
        return true;

    ClientPhysicsCallbacks cb(*this);
    fx.rocketFrameAdvance(static_cast<int>(svframe - fx.frame), cb);
    fx.frame = svframe;

    frameReceiveTime = get_time();

    clFrameWorld = read.U8();
    /* svframe - .5 is roughly when clFrameWorld was received in server (using '- 1. + offsetDelta' instead of -.5 here would be possible but not necessarily really better)
     * svFrameHistory[clFrameWorld] is the apparent server frame when clFrameSent was sent
     */
    const double currentLag = bound(svframe - .5 - svFrameHistory[clFrameWorld], 0., 50.);    // bound because svFrameHistory has invalid frame# at connect to server
    averageLag = averageLag * .99 + currentLag * .01;

    const uint8_t frameOffset = read.U8();
    const double offsetDelta = (frameOffset / 256.) - .5;    // the deviation from aim, in frames
    frameOffsetDeltaTotal += offsetDelta;
    if (++frameOffsetDeltaNum == 10) {  // try to fix deviations every 10 frames
        frameOffsetDeltaTotal /= 10.;
        netsendAdjustment = -(frameOffsetDeltaTotal * .1); // convert to seconds
        frameOffsetDeltaTotal = 0;
        frameOffsetDeltaNum = 0;
    }

    const uint8_t xtra = read.U8();

    const int extraHealth = (xtra & 1) ? 256 : 0;
    const int extraEnergy = (xtra & 2) ? 256 : 0;
    const bool empty_frame_cause_not_ready_yet = (xtra & 4) != 0;

    if (me == -1)   // only read this when just connected to the server; otherwise, changes in "me" should be taken in only with the change teams message
        me = xtra >> 3;

    if (empty_frame_cause_not_ready_yet) {
        fx.skipped = true;
        return true;
    }

    if (!map_ready) {
        log.error("Server sent frame data when loading map");
        return false;
    }
    fx.skipped = false;

    fx.player[me].roomx = read.U8(0, fx.map.w - 1);
    fx.player[me].roomy = read.U8(0, fx.map.h - 1);

    if (fx.player[me].roomx != fx.player[me].oldx || fx.player[me].roomy != fx.player[me].oldy) {
        for (int j = 0; j < MAX_POWERUPS; j++)
            fx.item[j].kind = Powerup::pup_unused;  // the server will send messages for all seen, others should be forgotten

        fx.player[me].oldx = fx.player[me].roomx;
        fx.player[me].oldy = fx.player[me].roomy;
    }

    const uint32_t players_onscreen = read.U32();

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

        h.roomx = fx.player[me].roomx;  //same screen since it's on the "players on same screen" vector
        h.roomy = fx.player[me].roomy;

        {
            const uint8_t xLowBits = read.U8(), yLowBits = read.U8(), highBits = read.U8();
            h.lx = ((highBits & 0x0F) << 8 | xLowBits) * (plw / double(0xFFF));
            h.ly = ((highBits & 0xF0) << 4 | yLowBits) * (plh / double(0xFFF));
        }

        if (protocolExtensions < 0) {
            typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
            h.sx = SpeedType::toDouble(read.U8());
            h.sy = SpeedType::toDouble(read.U8());
        }

        const uint8_t extra = read.U8();
        h.dead = (extra & 1) != 0;
        h.item_deathbringer = (extra & 2) != 0;
        h.deathbringer_affected = (extra & 4) != 0;
        h.item_shield = (extra & 8) != 0;
        h.item_turbo = (extra & 16) != 0;
        h.item_power = (extra & 32) != 0;
        const bool preciseGundir = (extra & 64) != 0;

        if (protocolExtensions >= 0) {
            if (h.dead)
                h.sx = h.sy = 0;
            else {
                typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
                h.sx = SpeedType::toDouble(read.U8());
                h.sy = SpeedType::toDouble(read.U8());
            }
        }

        const uint8_t ccb = read.U8();
        h.controls.fromNetwork(ccb, true);

        if (preciseGundir)
            h.gundir.fromNetworkLongForm(((ccb >> 5) << 8) | read.U8());
        else
            h.gundir.fromNetworkShortForm(ccb >> 5);

        if (protocolExtensions < 0 || !h.dead)
            h.visibility = read.U8();
        else
            h.visibility = 255;

        if (i == me) {
            if (!h.item_turbo)
                fx.player[me].item_turbo_time = 0;
            if (!h.item_power)
                fx.player[me].item_power_time = 0;
            if (h.visibility == 255)
                fx.player[me].item_shadow_time = 0;
        }

        if (i / TSIZE == me / TSIZE || h.visibility >= 10 || h.stats().has_flag())
            h.posUpdated = svframe;
    }

    // see servnet.cpp for a short documentation of the minimap player position protocol
    if (protocolExtensions < 0) {
        uint8_t pid = read.U8();
        if (pid < MAX_PLAYERS)
            readMinimapPlayerPosition(read, pid);
        pid = read.U8();
        if (pid < MAX_PLAYERS)
            readMinimapPlayerPosition(read, pid);
    }
    else {
        const uint8_t mmByte = read.U8();
        const int pos = 4 * (mmByte >> 5);
        const int extraBytes = ((mmByte & 0x18) >> 3) + 1;
        uint32_t rotP = mmByte & 0x07;
        for (int i = 0, rotPpos = 3; i < extraBytes; ++i, rotPpos += 8)
            rotP |= read.U8() << rotPpos;
        for (int pid = pos; rotP; pid = (pid + 1) % 32, rotP >>= 1)
            if (rotP & 1)
                readMinimapPlayerPosition(read, pid);
    }

    fx.player[me].health = read.U8() + extraHealth;
    fx.player[me].energy = read.U8() + extraEnergy;

    if (read.hasMore())
        fx.player[svframe % maxplayers].ping = max<int16_t>(read.S16(), 0); // Server versions up to 1.0.3 using a multicore processor can send negative pings.

    return true;
}

#ifndef DEDICATED_SERVER_ONLY
int Client::process_replay_frame_data(ConstDataBlockRef data) throw () { // returns number of bytes read - not necessarily all of data
    BinaryDataBlockReader read(data);

    const uint32_t svframe = read.U32(static_cast<unsigned>(fx.frame) + 1, uint32_t(-1));    //server's frame

    ClientPhysicsCallbacks cb(*this);
    fx.rocketFrameAdvance(static_cast<int>(svframe - fx.frame), cb);
    fx.frame = svframe;

    frameReceiveTime = get_time();

    if (!replay_first_frame_loaded) {
        replay_start_frame = svframe;
        replay_first_frame_loaded = true;
    }

    fx.skipped = false;
    const uint32_t players_present = read.U32();
    for (int i = 0; i < maxplayers; i++) {
        ClientPlayer& pl = fx.player[i];
        if (!(players_present & (1 << i)))
            continue;

        // Dead and powerup flags
        const uint8_t byte = read.U8();
        pl.dead = (byte & (1 << 0)) != 0;
        pl.item_deathbringer = (byte & (1 << 1)) != 0;
        pl.deathbringer_affected = (byte & (1 << 2)) != 0;
        pl.item_shield = (byte & (1 << 3)) != 0;
        pl.item_turbo = (byte & (1 << 4)) != 0;
        pl.item_power = (byte & (1 << 5)) != 0;
        const bool preciseGundir = (byte & (1 << 6)) != 0;

        const bool position_data = (byte & (1 << 7)) != 0;
        if (position_data) {
            pl.roomx = read.U8();
            pl.roomy = read.U8();
            pl.lx = read.U16();
            pl.ly = read.U16();

            pl.sx = read.flt();
            pl.sy = read.flt();
        }

        // Controls
        const uint8_t controlByte = read.U8();
        pl.controls.fromNetwork(controlByte, true);

        if (preciseGundir)
            pl.gundir.fromNetworkLongForm(((controlByte >> 5) << 8) | read.U8());
        else
            pl.gundir.fromNetworkShortForm(controlByte >> 5);

        pl.visibility = read.U8();

        pl.posUpdated = svframe;
    }
    fx.player[svframe % maxplayers].ping = read.U16();

    return read.getPosition();
}
#endif

// process a message (update fx, and add the non frame-related messages to messageQueue)
bool Client::process_message(ConstDataBlockRef data) throw () {
    const double time = fx.frame / 10;
    BinaryDataBlockReader read(data);

    const uint8_t code = read.U8();

    if (LOG_MESSAGE_TRAFFIC)
        log("Message from server, code = %i, length = %i bytes", code, data.size());

    switch (static_cast<Network_data_code>(code)) {
    /*break;*/ case data_name_update: {
        const uint8_t pid = read.U8();
        const string name = read.str();
        if (check_name(name)) {
            if (fx.player[pid].name.empty()) {
                addThreadMessage(new TM_Text(msg_info, _("$1 entered the game.", name)));
                addThreadMessage(new TM_Sound(SAMPLE_ENTERGAME));
            }
            else if (fx.player[pid].name != " " && fx.player[pid].name != name)    // " " is the case with players already in game when connecting
                addThreadMessage(new TM_Text(msg_info, _("$1 changed name to $2.", fx.player[pid].name, name)));
            fx.player[pid].name = name;
        }
        else
            log.error("Invalid name for player " + itoa(pid) + '.');
    }

    break; case data_text_message: {
        #ifndef DEDICATED_SERVER_ONLY
        const Message_type type = static_cast<Message_type>(read.U8(0, Message_types - 1));
        string chatmsg = read.str();
        if (find_nonprintable_char(chatmsg)) {
            log.error("Server sent non-printable characters in a message.");
            nAssert(!botmode);
            addThreadMessage(new TM_DoDisconnect());
            break;
        }
        // This is a kludge because of compatibility.
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
        int8_t sender_team = -1;
        if (protocolExtensions >= 0 || replaying) {
            if (type == msg_team || type == msg_normal)
                sender_team = read.S8();
        }
        else if (type == msg_team)
            sender_team = team();
        addThreadMessage(new TM_Text(type, chatmsg, sender_team));
        if (type == msg_team || type == msg_normal)
            addThreadMessage(new TM_Sound(SAMPLE_TALK));
        #endif
    }

    break; case data_first_packet: {
        me = read.U8();

        fx.player[me].set_color(read.U8(0, PlayerBase::invalid_color - 1));

        #ifndef DEDICATED_SERVER_ONLY
        current_map = read.U8();
        #else
        read.U8();
        #endif

        const bool e = protocolExtensions >= 0;

        uint8_t score = read.U32dyn8orU8(e);
        fx.teams[0].set_score(score);
        if (fx.teams[0].captures().size() == 0) // only if just joined the server
            fx.teams[0].set_base_score(score);
        score = read.U32dyn8orU8(e);
        fx.teams[1].set_score(score);
        if (fx.teams[1].captures().size() == 0) // only if just joined the server
            fx.teams[1].set_base_score(score);

        // room is probably changed
        fx.player[me].oldx = -1;
        fx.player[me].oldy = -1;
    }

    break; case data_frags_update: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8();
        fx.player[pid].stats().set_frags(read.U32());
        stable_sort(players_sb.begin(), players_sb.end(), compare_players);
        #endif
    }

    break; case data_flag_update: {
        const uint8_t team = read.U8();
        const int8_t flags = read.S8();
        bool new_flag = false;
        for (int i = 0; i < flags; i++) {
            if (team == 2) {
                if (i >= static_cast<int>(fx.wild_flags.size())) {
                    fx.wild_flags.push_back(Flag(WorldCoords()));
                    new_flag = true;
                }
            }
            else if (i >= static_cast<int>(fx.teams[team].flags().size())) {
                fx.teams[team].add_flag(WorldCoords());
                new_flag = true;
            }
            const uint8_t carried = read.U8();
            if (carried == 0) {
                //not carried: update position
                const uint8_t px = read.U8(), py = read.U8();
                const int16_t x = read.S16(), y = read.S16();
                const WorldCoords pos(px, py, x, y);
                bool was_carried;
                if (team == 2) {
                    was_carried = fx.wild_flags[i].carried();
                    fx.wild_flags[i].move(pos);
                    fx.wild_flags[i].drop();
                }
                else {
                    was_carried = fx.teams[team].flag(i).carried();
                    fx.teams[team].drop_flag(i, pos);
                }
                if (!new_flag && was_carried)
                    if (team == 2)
                        fx.wild_flags[i].set_return_time(time);
                    else {
                        fx.teams[team].set_flag_drop_time(i, time);
                        fx.teams[team].set_flag_return_time(i, time);
                    }
            }
            else {
                //carried: get carrier
                const uint8_t carrier = read.U8();
                if (!new_flag) {
                    if (!fx.player[carrier].onscreen && !replaying) {
                        const WorldCoords& flagPos = (team == 2 ? fx.wild_flags[i] : fx.teams[team].flag(i)).position();
                        if (!flagPos.unknown()) {
                            fx.player[carrier].roomx = flagPos.px;
                            fx.player[carrier].roomy = flagPos.py;
                            fx.player[carrier].lx = flagPos.x;
                            fx.player[carrier].ly = flagPos.y;
                            fx.player[carrier].posUpdated = fx.frame;
                        }
                    }
                    addThreadMessage(new TM_Sound(SAMPLE_CTF_GOT));
                }
                if (team == 2)
                    fx.wild_flags[i].take(carrier);
                else
                    fx.teams[team].steal_flag(i, carrier);
            }
        }
    }

    break; case data_rocket_fire: {
        if (!map_ready)
            break;

        const uint8_t rpow = read.U8(1, 9), rdir = read.U8();

        GunDirection dir;
        if (rdir & 0x80) {
            if (protocolExtensions < 0)
                return false;
            dir.fromNetworkLongForm(((rdir & 0x7F) << 8) | read.U8());
        }
        else
            dir.fromNetworkShortForm(rdir);

        uint8_t rids[16];
        for (int i = 0; i < rpow; i++)
            rids[i] = read.U8();

        const uint32_t frameno = read.U32();
        const uint8_t rteampower = read.U8(); // power (bit 0) and shooter pid/team
        const bool power = rteampower & 1;

        const uint8_t rpx = read.U8(0, fx.map.w - 1), rpy = read.U8(0, fx.map.h - 1);
        const int16_t rx = read.S16(), ry = read.S16();

        int team;
        if (protocolExtensions >= 0) {
            if (rteampower & 2) { // with player id
                const int pid = (rteampower & 0x7C) >> 2;
                if (pid >= maxplayers || !fx.player[pid].used) {
                    log("Bad pid in data_rocket_fire: %d.", pid);
                    return false;
                }
                team = pid / TSIZE;
                if (fx.player[pid].posUpdated < frameno) {
                    fx.player[pid].roomx = rpx;
                    fx.player[pid].roomy = rpy;
                    fx.player[pid].lx = rx;
                    fx.player[pid].ly = ry;
                    fx.player[pid].gundir = dir;
                    fx.player[pid].posUpdated = frameno;
                }
            }
            else
                team = (rteampower & 4) >> 2;
        }
        else
            team = (rteampower & 2) >> 1;

        ClientPhysicsCallbacks cb(*this);
        fx.shootRockets(cb, 0, rpow, dir, rids, static_cast<int>(fx.frame - frameno), team, power, rpx, rpy, rx, ry);

        #ifndef DEDICATED_SERVER_ONLY
        if (on_screen_exact(rpx, rpy, rx, ry))
            addThreadMessage(new TM_Sound(power ? SAMPLE_POWER_FIRE : SAMPLE_FIRE));
        #endif
    }

    break; case data_old_rocket_visible: {
        if (!map_ready)
            break;

        const uint8_t rockid = read.U8(), rdir = read.U8();

        GunDirection dir;
        if (rdir & 0x80) {
            if (protocolExtensions < 0)
                return false;
            dir.fromNetworkLongForm(((rdir & 0x7F) << 8) | read.U8());
        }
        else
            dir.fromNetworkShortForm(rdir);

        const uint32_t frameno = read.U32();
        const uint8_t rteampower = read.U8();

        const bool power = rteampower & 1;
        const int team = (rteampower & 2) >> 1;

        const uint8_t rpx = read.U8(0, fx.map.w - 1), rpy = read.U8(0, fx.map.h - 1);
        const int16_t rx = read.S16(), ry = read.S16();

        ClientPhysicsCallbacks cb(*this);
        fx.shootRockets(cb, 0, 1, dir, &rockid, static_cast<int>(fx.frame - frameno), team, power, rpx, rpy, rx, ry);
        // no sound
    }

    break; case data_rocket_delete: {
        if (!map_ready)
            break;

        const uint8_t rockid = read.U8();
        fx.rock[rockid].owner = -1;
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t target = read.U8();
        if (target != 255) {    // hit player
            if (target != 252) {  // not shield hit -> blink player
                if (target >= maxplayers)
                    return false;
                fx.player[target].hitfx = time + .3;
            }
            //hit position
            const int16_t rokx = read.S16(), roky = read.S16();
            addThreadMessage(new TM_GunexploEffect(fx.rock[rockid].team, time, WorldCoords(fx.rock[rockid].px, fx.rock[rockid].py, rokx, roky)));
            if (on_screen_exact(fx.rock[rockid].px, fx.rock[rockid].py, rokx, roky))
                addThreadMessage(new TM_Sound(SAMPLE_HIT));
        }
        #endif
    }

    break; case data_power_collision: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t target = read.U8(0, maxplayers - 1);
        fx.player[target].hitfx = time + .3;
        if (!replaying || player_on_screen(target))
            addThreadMessage(new TM_Sound(client_sounds.sampleExists(SAMPLE_COLLISION_DAMAGE) ? SAMPLE_COLLISION_DAMAGE : SAMPLE_HIT));
        #endif
    }

    break; case data_score_update: {
        const uint8_t team = read.U8(0, 1);
        fx.teams[team].set_score(read.U32dyn8orU8(protocolExtensions >= 0));
    }

    break; case data_sound: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t sample = read.U8();
        if (replaying && read.hasMore()) {
            const uint8_t rx = read.U8(0, fx.map.w - 1), ry = read.U8(0, fx.map.h - 1);
            if (!on_screen(rx, ry))
                break;
        }
        if (sample < NUM_OF_SAMPLES)
            addThreadMessage(new TM_Sound(sample));
        #endif
    }

    break; case data_pup_visible: {
        const uint8_t iid = read.U8();
        fx.item[iid].kind = static_cast<Powerup::Pup_type>(read.U8(0, Powerup::pup_last_real));
        fx.item[iid].px = read.U8(0, fx.map.w - 1);
        fx.item[iid].py = read.U8(0, fx.map.h - 1);
        fx.item[iid].x = read.U16();
        fx.item[iid].y = read.U16();
    }

    break; case data_pup_picked:
        fx.item[read.U8()].kind = Powerup::pup_unused;

    break; case data_pup_timer: {
        const uint8_t type = read.U8();
        const double pup_time = time + read.U16();
        if (type == Powerup::pup_turbo)
            fx.player[me].item_turbo_time = pup_time;
        else if (type == Powerup::pup_shadow)
            fx.player[me].item_shadow_time = pup_time;
        else if (type == Powerup::pup_power)
            fx.player[me].item_power_time = pup_time;
    }

    break; case data_weapon_change:
        fx.player[me].weapon = read.U8(1, 9);

    break; case data_map_change: {
        map_ready = false;  // map NOT ready anymore: must load/change
        #ifndef DEDICATED_SERVER_ONLY
        want_map_exit = false;      // and player does not want to exit the map anymore
        want_map_exit_delayed = false;
        deadAfterHighlighted = true;

        // make sure the server knows that want_map_exit = false (in case data_map_exit_on was sent and not yet received when the data_map_change was sent)
        if (!replaying) {
            BinaryBuffer<16> msg;
            msg.U8(data_map_exit_off);
            client->send_message(msg);
        }
        #endif

        fx.teams[0].remove_flags();
        fx.teams[1].remove_flags();
        fx.wild_flags.clear();
        for (int i = 0; i < MAX_ROCKETS; ++i)
            fx.rock[i].owner = -1;
        const uint16_t crc = read.U16();
        const string mapname = read.str(), maptitle = read.str();
        #ifndef DEDICATED_SERVER_ONLY
        current_map = read.U8();
        if (map_vote == current_map)
            map_vote = -1;
        const uint8_t total_maps = read.U8();
        #else
        read.U8();
        read.U8();
        #endif
        if (me != -1) {
            fx.player[me].oldx = -1;
            fx.player[me].oldy = -1;
        }
        if (read.hasMore())
            remove_flags = read.S8();
        else
            remove_flags = 0;
        #ifndef DEDICATED_SERVER_ONLY
        string msg;
        #endif
        if (!replaying) {
            addThreadMessage(new TM_MapChange(mapname, crc));
            #ifndef DEDICATED_SERVER_ONLY
            msg = _("This map is $1 ($2 of $3).", maptitle, itoa(current_map + 1), itoa(total_maps));
            #endif
        }
        #ifndef DEDICATED_SERVER_ONLY
        else { // The map is saved with the message
            stringstream mapStream;
            const ConstDataBlockRef mapData = read.block(read.U32());
            mapStream.write(static_cast<const char*>(mapData.data()), mapData.size());
            if (!fx.map.parse_file(log, mapStream)) {
                log("Problem with map data.");
                return false;
            }
            fd.map = fx.map;
            log("Map loaded from the replay: %s", fx.map.title.c_str());
            remove_useless_flags();
            mapChanged = true;
            map_ready = true;
            msg = _("This map is $1.", maptitle);
        }
        addThreadMessage(new TM_Text(msg_info, msg));
        #endif
    }

    break; case data_world_reset:
        #ifndef DEDICATED_SERVER_ONLY
        if (replaying && !spectating)
            break;
        #endif
        for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
            pi->stats().finish_stats(time);
        fx.reset();
        #ifndef DEDICATED_SERVER_ONLY
        fd.reset();
        #endif

    break; case data_gameover_show: {
        #ifndef DEDICATED_SERVER_ONLY
        extra_time_running = false;
        #endif
        const uint8_t plaque = read.U8();
        if (plaque == NEXTMAP_CAPTURE_LIMIT || plaque == NEXTMAP_VOTE_EXIT) {
            gameover_plaque = plaque;
            #ifndef DEDICATED_SERVER_ONLY
            const bool e = protocolExtensions >= 0;
            red_final_score = read.U32dyn8orU8(e);
            blue_final_score = read.U32dyn8orU8(e);
            const uint8_t caplimit = read.U8(), timelimit = read.U8();

            string msg = _("CTF GAME OVER - FINAL SCORE: RED $1 - BLUE $2", itoa(red_final_score), itoa(blue_final_score));
            addThreadMessage(new TM_Text(msg_info, msg));
            addThreadMessage(new TM_Sound(SAMPLE_CTF_GAMEOVER));
            if (!replay) {
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
            }
            #endif
            for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
                pi->stats().finish_stats(time);
        }
        else {
            gameover_plaque = NEXTMAP_NONE;
            #ifndef DEDICATED_SERVER_ONLY
            if (stats_autoshowing) {
                menusel = menu_none;
                stats_autoshowing = false;
            }
            #endif
        }
    }

    break; case data_start_game:
        fx.teams[0].clear_stats();
        fx.teams[1].clear_stats();
        for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
            pi->stats().clear(true);
        gameover_plaque = NEXTMAP_NONE;
        #ifndef DEDICATED_SERVER_ONLY
        if (stats_autoshowing) {
            menusel = menu_none;
            stats_autoshowing = false;
        }
        deadAfterHighlighted = true;
        #endif

    break; case data_deathbringer: {
        const uint8_t team = read.U8();
        const uint32_t frameno = read.U32();
        const uint8_t roomx = read.U8(0, fx.map.w - 1), roomy = read.U8(0, fx.map.h - 1);
        const uint16_t lx = read.U16(), ly = read.U16();
        fx.addDeathbringerExplosion(DeathbringerExplosion(frameno, WorldCoords(roomx, roomy, lx, ly), team));
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Sound(SAMPLE_USEDEATHBRINGER));
        #endif
    }

    break; case data_file_download: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint16_t chunkSize = read.U16();
        const uint8_t last = read.U8();
        process_udp_download_chunk(read.block(chunkSize), (last != 0));
        #endif
    }

    break; case data_registration_response:
        #ifndef DEDICATED_SERVER_ONLY
        if (read.U8() == 1)  // success
            tournamentPassword.serverAcceptsToken();
        else
            tournamentPassword.serverRejectsToken();
        #endif

    break; case data_crap_update: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8();
        fx.player[pid].set_color(read.U8(0, PlayerBase::invalid_color - 1));
        ClientLoginStatus ls;
        ls.fromNetwork(read.U8());
        const uint32_t prank = read.U32(0, INT_MAX), pscore = read.U32(0, INT_MAX), nscore = read.U32(0, INT_MAX);
        max_world_rank = read.U32();

        const ClientLoginStatus& os = fx.player[me].reg_status;
        const bool newMePrintout =
            !replaying &&
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
        fx.player[pid].rank = prank;
        fx.player[pid].score = pscore;
        fx.player[pid].neg_score = nscore;
        // update new team powers
        double power[2] = { 0, 0 };
        for (int i = 0; i < fx.maxplayers; i++)
            if (fx.player[i].used)
                power[fx.player[i].team()] += (fx.player[i].score + 1.) / (fx.player[i].neg_score + 1.);
        for (int t = 0; t < 2; t++)
            fx.teams[t].set_power(power[t]);
        #endif
    }

    break; case data_map_time: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint32_t current_time = read.U32(), time_left = read.U32();
        map_start_time = static_cast<int>(time) - current_time;
        if (time_left > 0) {
            map_end_time = static_cast<int>(time) + time_left;
            map_time_limit = true;
        }
        else
            map_time_limit = false;
        if (LOG_MESSAGE_TRAFFIC)
            log("Map time received. Time left %d seconds.", time_left);
        #endif
    }

    break; case data_reset_map_list: {
        #ifndef DEDICATED_SERVER_ONLY
        Lock ml(mapInfoMutex);
        maps.clear();
        mapListChangedAfterSort = true;
        map_vote = -1;
        #endif
    }

    break; case data_current_map:
        #ifndef DEDICATED_SERVER_ONLY
        current_map = read.U8();
        #endif

    break; case data_map_list: {
        #ifndef DEDICATED_SERVER_ONLY
        MapInfo mapinfo;
        mapinfo.title = read.str();
        mapinfo.author = read.str();
        mapinfo.width = read.U8();
        mapinfo.height = read.U8();
        mapinfo.votes = read.U8();
        mapinfo.random = read.hasMore() ? read.U8() : false;
        mapinfo.highlight = !!fav_maps.count(toupper(mapinfo.title));
        Lock ml(mapInfoMutex);
        maps.push_back(mapinfo);
        mapListChangedAfterSort = true;
        #endif
    }

    break; case data_map_vote: {
        #ifndef DEDICATED_SERVER_ONLY
        map_vote = read.S8();
        #endif
    }

    break; case data_map_votes_update: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t total = read.U8();
        Lock ml(mapInfoMutex);
        for (int i = 0; i < total; i++) {
            const uint8_t map_nr = read.U8(), votes = read.U8();
            if (map_nr < maps.size()) {
                maps[map_nr].votes = votes;
                mapListChangedAfterSort = true;
            }
        }
        #endif
    }

    break; case data_stats_ready: {
        #ifndef DEDICATED_SERVER_ONLY
        if (menu.options.game.showStats() != Menu_game::SS_none && menusel == menu_none && openMenus.empty()) {
            switch (menu.options.game.showStats()) {
            /*break;*/ case Menu_game::SS_teams:   menusel = menu_teams;
                break; case Menu_game::SS_players: menusel = menu_players;
                break; default: nAssert(0);
            }
            stats_autoshowing = true;
        }
        #endif
        for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
            pi->stats().finish_stats(time);
        #ifndef DEDICATED_SERVER_ONLY
        if (menu.options.game.saveStats())
            fx.save_stats("client_stats", fx.map.title);
        #endif
    }

    break; case data_capture: {
        uint8_t pid = read.U8();
        #ifndef DEDICATED_SERVER_ONLY
        const bool wild_flag = pid & 0x80;
        #endif
        pid &= ~0x80;
        if (pid >= maxplayers)
            return false;
        fx.player[pid].stats().add_capture(time);
        #ifndef DEDICATED_SERVER_ONLY
        const int team = pid / TSIZE;
        fx.teams[team].add_score(time - map_start_time, fx.player[pid].name);
        string msg;
        if (wild_flag)
            msg = _("$1 CAPTURED THE WILD FLAG!", fx.player[pid].name);
        else if (1 - team == 0)
            msg = _("$1 CAPTURED THE RED FLAG!", fx.player[pid].name);
        else
            msg = _("$1 CAPTURED THE BLUE FLAG!", fx.player[pid].name);
        addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CTF_CAPTURE));
        #endif
    }

    break; case data_kill: {
        uint8_t attacker = read.U8(), target = read.U8();
        const DamageType cause = ((attacker & 0x80) ? DT_deathbringer : (target & 0x20) ? DT_collision : DT_rocket);
        #ifdef DEFENDING_MESSAGES
        const bool carrier_defended = attacker & 0x40;
        const bool flag_defended = attacker & 0x20;
        #endif
        const bool flag = target & 0x80;
        #ifndef DEDICATED_SERVER_ONLY
        const bool wild_flag = target & 0x40;
        #endif
        attacker &= 0x1F;
        target &= 0x1F;
        if (attacker >= maxplayers && attacker != MAX_PLAYERS - 1 || target >= maxplayers) // attacker = MAX_PLAYERS - 1 if attacker already left the server
            return false;
        const bool attacker_team = attacker / TSIZE;
        const bool target_team = target / TSIZE;
        const bool same_team = (attacker_team == target_team);
        const bool known_attacker = fx.player[attacker].used;
        #ifndef DEDICATED_SERVER_ONLY
        string msg;
        if (cause == DT_deathbringer) {
            if (!known_attacker)
                msg = _("$1 was choked.", fx.player[target].name);
            else if (same_team)
                msg = _("$1 was choked by teammate $2.", fx.player[target].name, fx.player[attacker].name);
            else
                msg = _("$1 was choked by $2.", fx.player[target].name, fx.player[attacker].name);
            if (player_on_screen_exact(target))
                addThreadMessage(new TM_Sound(SAMPLE_DIEDEATHBRINGER));
        }
        else if (cause == DT_collision) {
            if (!known_attacker)    // this should never happen with the current code, probably not in the future either, but it's still here...
                msg = _("$1 received a mortal blow.", fx.player[target].name);
            else if (same_team) // this shouldn't happen with the current special collisions either, but we're ready for changes
                msg = _("$1 received a mortal blow from teammate $2.", fx.player[target].name, fx.player[attacker].name);
            else
                msg = _("$1 received a mortal blow from $2.", fx.player[target].name, fx.player[attacker].name);
            if (player_on_screen_exact(target))
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
            if (player_on_screen_exact(target))
                addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
        }
        if (menu.options.game.showKillMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
        #ifdef DEFENDING_MESSAGES
        if (carrier_defended && known_attacker) {
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
        }
        #endif // DEFENDING_MESSAGES
        if (fx.player[target].stats().current_cons_kills() >= 10) {
            if (!known_attacker)
                msg = _("$1's killing spree was ended.", fx.player[target].name);
            else
                msg = _("$1's killing spree was ended by $2.", fx.player[target].name, fx.player[attacker].name);
            if (menu.options.game.showKillMessages())
                addThreadMessage(new TM_Text(msg_info, msg));
        }
        if (target == me)
            deadAfterHighlighted = true;
        #endif // !DEDICATED_SERVER_ONLY
        if (!same_team) {
            if (known_attacker)
                fx.player[attacker].stats().add_kill(cause == DT_deathbringer);
            fx.teams[attacker_team].add_kill();
        }
        fx.player[target].stats().add_death(cause == DT_deathbringer, static_cast<int>(time));
        fx.player[target].dead = true;
        fx.teams[target_team].add_death();
        if (flag) {
            if (!same_team && known_attacker)
                fx.player[attacker].stats().add_carrier_kill();
            fx.player[target].stats().add_flag_drop(time);
            fx.teams[target_team].add_flag_drop();
            #ifndef DEDICATED_SERVER_ONLY
            if (wild_flag)
                msg = _("$1 LOST THE WILD FLAG!", fx.player[target].name);
            else if (1 - target_team == 0)
                msg = _("$1 LOST THE RED FLAG!", fx.player[target].name);
            else
                msg = _("$1 LOST THE BLUE FLAG!", fx.player[target].name);
            if (menu.options.game.showFlagMessages())
                addThreadMessage(new TM_Text(msg_info, msg));
            addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
            #endif
        }
        #ifndef DEDICATED_SERVER_ONLY
        if (!same_team && known_attacker && fx.player[attacker].stats().current_cons_kills() % 10 == 0) {
            if (attacker == me)
                addThreadMessage(new TM_Sound(SAMPLE_KILLING_SPREE));
            msg = _("$1 is on a killing spree!", fx.player[attacker].name);
            if (menu.options.game.showKillMessages())
                addThreadMessage(new TM_Text(msg_info, msg));
        }
        #endif
    }

    break; case data_flag_take: {
        uint8_t pid = read.U8();
        const bool wild_flag = pid & 0x80;
        pid &= ~0x80;
        if (pid >= maxplayers)
            return false;
        fx.player[pid].stats().add_flag_take(time, wild_flag);
        const int team = pid / TSIZE;
        fx.teams[team].add_flag_take();
        #ifndef DEDICATED_SERVER_ONLY
        string msg;
        if (wild_flag)
            msg = _("$1 GOT THE WILD FLAG!", fx.player[pid].name);
        else if (1 - team == 0)
            msg = _("$1 GOT THE RED FLAG!", fx.player[pid].name);
        else
            msg = _("$1 GOT THE BLUE FLAG!", fx.player[pid].name);
        if (menu.options.game.showFlagMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
        #endif
    }

    break; case data_flag_return: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        fx.player[pid].stats().add_flag_return();
        fx.teams[pid / TSIZE].add_flag_return();
        #ifndef DEDICATED_SERVER_ONLY
        string msg;
        if (pid / TSIZE == 0)
            msg = _("$1 RETURNED THE RED FLAG!", fx.player[pid].name);
        else
            msg = _("$1 RETURNED THE BLUE FLAG!", fx.player[pid].name);
        if (menu.options.game.showFlagMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CTF_RETURN));
        #endif
    }

    break; case data_flag_drop: {
        uint8_t pid = read.U8();
        #ifndef DEDICATED_SERVER_ONLY
        const bool wild_flag = pid & 0x80;
        #endif
        pid &= ~0x80;
        if (pid >= maxplayers)
            return false;
        fx.player[pid].stats().add_flag_drop(time);
        const int team = pid / TSIZE;
        fx.teams[team].add_flag_drop();
        #ifndef DEDICATED_SERVER_ONLY
        string msg;
        if (wild_flag)
            msg = _("$1 DROPPED THE WILD FLAG!", fx.player[pid].name);
        else if (1 - team == 0)
            msg = _("$1 DROPPED THE RED FLAG!", fx.player[pid].name);
        else
            msg = _("$1 DROPPED THE BLUE FLAG!", fx.player[pid].name);
        if (menu.options.game.showFlagMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
        #endif
    }

    break; case data_suicide: {
        uint8_t pid = read.U8();
        const bool flag = pid & 0x80;
        #ifndef DEDICATED_SERVER_ONLY
        const bool wild_flag = pid & 0x40;
        #endif
        pid &= ~0xC0;
        if (pid >= maxplayers)
            return false;
        const int team = pid / TSIZE;
        #ifndef DEDICATED_SERVER_ONLY
        if (fx.player[pid].stats().current_cons_kills() >= 10 && menu.options.game.showKillMessages())
            addThreadMessage(new TM_Text(msg_info, _("$1's killing spree was ended.", fx.player[pid].name)));
        if (pid == me)
            deadAfterHighlighted = true;
        #endif
        fx.player[pid].stats().add_suicide(static_cast<int>(time));
        fx.player[pid].dead = true;
        fx.teams[team].add_suicide();
        if (flag) {
            fx.player[pid].stats().add_flag_drop(time);
            fx.teams[team].add_flag_drop();
            #ifndef DEDICATED_SERVER_ONLY
            string msg;
            if (wild_flag)
                msg = _("$1 LOST THE WILD FLAG!", fx.player[pid].name);
            else if (1 - team == 0)
                msg = _("$1 LOST THE RED FLAG!", fx.player[pid].name);
            else
                msg = _("$1 LOST THE BLUE FLAG!", fx.player[pid].name);
            if (menu.options.game.showFlagMessages())
                addThreadMessage(new TM_Text(msg_info, msg));
            addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
            #endif
        }
        #ifndef DEDICATED_SERVER_ONLY
        if (player_on_screen_exact(pid))
            addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
        #endif
    }

    break; case data_players_present: {    // this is only sent immediately after connecting to the server
        const uint32_t pp = read.U32();
        for (int i = 0; i < maxplayers; ++i) {
            if (fx.player[i].used)  // this shouldn't happen except for i == me; either way, the player is already initialized
                continue;
            if (pp & (1 << i)) {
                fx.player[i].clear(true, i, " ", i / TSIZE);  // hack... use " " for name to suppress announcement when the name is received
                #ifndef DEDICATED_SERVER_ONLY
                players_sb.push_back(&fx.player[i]);
                #endif
            }
        }
    }

    break; case data_new_player: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        nAssert(!fx.player[pid].used || replaying);
        fx.player[pid].clear(true, pid, "", pid / TSIZE);
        fx.player[pid].stats().set_start_time(time);
        #ifndef DEDICATED_SERVER_ONLY
        players_sb.push_back(&fx.player[pid]);
        #endif
    }

    break; case data_player_left: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        #ifndef DEDICATED_SERVER_ONLY
        const string msg = _("$1 left the game with $2 frags.", fx.player[pid].name, itoa(fx.player[pid].stats().frags()));
        addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_LEFTGAME));
        const vector<ClientPlayer*>::iterator rm = find(players_sb.begin(), players_sb.end(), &fx.player[pid]);
        if (rm == players_sb.end())
            return false;
        players_sb.erase(rm);
        #endif
        nAssert(fx.player[pid].used);
        fx.player[pid].used = false;
    }

    break; case data_team_change: {
        const uint8_t from = read.U8(0, maxplayers - 1), to = read.U8(0, maxplayers - 1), col1 = read.U8(), col2 = read.U8();
        const bool swap = (col2 != 255);
        nAssert(fx.player[from].used && swap == fx.player[to].used);

        #ifndef DEDICATED_SERVER_ONLY
        string msg;
        if (swap)
            msg = _("$1 and $2 swapped teams.", fx.player[from].name, fx.player[to].name);
        else if (to / TSIZE == 0)
            msg = _("$1 moved to red team.", fx.player[from].name);
        else
            msg = _("$1 moved to blue team.", fx.player[from].name);
        addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CHANGETEAM));
        #endif

        if (swap) {
            std::swap(fx.player[from], fx.player[to]);
            fx.player[from].id = from;
            fx.player[to  ].id =   to;
            fx.player[from].set_team(from / TSIZE);
            fx.player[to  ].set_team(  to / TSIZE);
            // both players already exist in players_sb -> no changes except resorting
            #ifndef DEDICATED_SERVER_ONLY
            stable_sort(players_sb.begin(), players_sb.end(), compare_players);
            #endif
        }
        else {
            fx.player[to] = fx.player[from];
            fx.player[from].used = false;
            fx.player[to].id = to;
            fx.player[to].set_team(to / TSIZE);
            #ifndef DEDICATED_SERVER_ONLY
            const vector<ClientPlayer*>::iterator rm = find(players_sb.begin(), players_sb.end(), &fx.player[from]);
            if (rm == players_sb.end())
                return false;
            players_sb.erase(rm);
            players_sb.push_back(&fx.player[to]);
            #endif
        }

        if (from == me || to == me) {
            #ifndef DEDICATED_SERVER_ONLY
            want_change_teams = false;
            #endif
            me = (me == from) ? to : from;
        }

        #ifndef DEDICATED_SERVER_ONLY
        if (col1 >= PlayerBase::invalid_color)
            return false;
        fx.player[to].set_color(col1);
        #else
        (void)col1;
        #endif
        fx.player[to].stats().kill(static_cast<int>(time), true);
        fx.player[to].dead = true;  // this was already read from the frame data but overwritten by the team change
        if (swap) {
            #ifndef DEDICATED_SERVER_ONLY
            if (col2 >= PlayerBase::invalid_color)
                return false;
            fx.player[from].set_color(col2);
            #endif
            fx.player[from].stats().kill(static_cast<int>(time), true);
            fx.player[from].dead = true;    // this was already read from the frame data but overwritten by the team change
        }
        #ifndef DEDICATED_SERVER_ONLY
        if (from == me || to == me)
            deadAfterHighlighted = true;
        #endif
    }

    break; case data_spawn: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        fx.player[pid].stats().spawn(time);
        if (fx.player[pid].posUpdated != fx.frame)   // this information is after the spawn
            fx.player[pid].posUpdated = -1e10;  // (probably) not seen in this life; if seen before spawning, not valid anymore
        fx.player[pid].dead = false;
    }

    break; case data_team_movements_shots: {
        const bool e = protocolExtensions >= 0;
        for (int i = 0; i < 2; i++) {
            fx.teams[i].set_movement(read.U32());
            fx.teams[i].set_shots(read.U32dyn16orU16(e));
            fx.teams[i].set_hits(read.U32dyn16orU16(e));
            fx.teams[i].set_shots_taken(read.U32dyn16orU16(e));
        }
    }

    break; case data_team_stats: {
        const bool e = protocolExtensions >= 0;
        for (int i = 0; i < 2; i++) {
            fx.teams[i].set_kills(read.U32dyn8orU8(e));
            fx.teams[i].set_deaths(read.U32dyn8orU8(e));
            fx.teams[i].set_suicides(read.U32dyn8orU8(e));
            fx.teams[i].set_flags_taken(read.U32dyn8orU8(e));
            fx.teams[i].set_flags_dropped(read.U32dyn8orU8(e));
            fx.teams[i].set_flags_returned(read.U32dyn8orU8(e));
        }
    }

    break; case data_movements_shots: {
        const bool e = protocolExtensions >= 0;
        const uint8_t pid = read.U8(0, maxplayers - 1);
        fx.player[pid].stats().set_movement(read.U32());
        fx.player[pid].stats().save_speed(time);
        fx.player[pid].stats().set_shots(read.U32dyn16orU16(e));
        fx.player[pid].stats().set_hits(read.U32dyn16orU16(e));
        fx.player[pid].stats().set_shots_taken(read.U32dyn16orU16(e));
    }

    break; case data_stats: {
        uint8_t pid = read.U8();
        const bool flag = (pid & 0x80);
        const bool wild_flag = (pid & 0x40);
        const bool dead = (pid & 0x20);
        pid &= 0x1F;
        if (pid >= maxplayers)
            return false;
        Statistics& stats = fx.player[pid].stats();
        stats.set_flag(flag, wild_flag);
        fx.player[pid].dead = dead;
        stats.set_dead               (dead);
        const bool e = protocolExtensions >= 0;
        stats.set_kills              (read.U32dyn8orU8(e));
        stats.set_deaths             (read.U32dyn8orU8(e));
        stats.set_cons_kills         (read.U32dyn8orU8(e));
        stats.set_current_cons_kills (read.U32dyn8orU8(e));
        stats.set_cons_deaths        (read.U32dyn8orU8(e));
        stats.set_current_cons_deaths(read.U32dyn8orU8(e));
        stats.set_suicides           (read.U32dyn8orU8(e));
        stats.set_captures           (read.U32dyn8orU8(e));
        stats.set_flags_taken        (read.U32dyn8orU8(e));
        stats.set_flags_dropped      (read.U32dyn8orU8(e));
        stats.set_flags_returned     (read.U32dyn8orU8(e));
        stats.set_carriers_killed    (read.U32dyn8orU8(e));
        stats.set_start_time         (time - read.U32());
        stats.set_lifetime           (read.U32());
        stats.set_spawn_time         (time);
        stats.set_flag_carrying_time (read.U32());
        stats.set_flag_take_time     (time);
    }

    break; case data_name_authorization_request:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_NameAuthorizationRequest());
        #endif

    break; case data_server_settings: {
        const uint8_t caplimit = read.U8(), timelimit = read.U8(), extratime = read.U8();
        const uint16_t misc1 = read.U16(), pupMin = read.U16(), pupMax = read.U16(), pupAddTime = read.U16(), pupMaxTime = read.U16();
        fx.physics.read(read);
        if (read.hasMore())
            flag_return_delay = read.U16();
        else
            flag_return_delay = -1;
        #ifndef DEDICATED_SERVER_ONLY
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

        addThreadMessage(new TM_ServerSettings(caplimit, timelimit, extratime, misc1, pupMin, pupMax, pupAddTime, pupMaxTime, flag_return_delay));
        #else
        (void)(caplimit && timelimit && extratime && misc1 && pupMin && pupMax && pupAddTime && pupMaxTime);
        #endif
    }

    break; case data_5_min_left:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** Five minutes remaining")));
        addThreadMessage(new TM_Sound(SAMPLE_5_MIN_LEFT));
        #endif

    break; case data_1_min_left:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** One minute remaining")));
        addThreadMessage(new TM_Sound(SAMPLE_1_MIN_LEFT));
        #endif

    break; case data_30_s_left:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** 30 seconds remaining")));
        #endif

    break; case data_time_out:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** Time out - CTF game over")));
        #endif

    break; case data_extra_time_out:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** Extra-time out - CTF game over")));
        #endif

    break; case data_normal_time_out: {
        #ifndef DEDICATED_SERVER_ONLY
        extra_time_running = true;
        string msg = _("*** Normal time out - extra-time started");
        if (read.U8() & 0x01)
            msg += " " + _("(sudden death)");
        addThreadMessage(new TM_Text(msg_info, msg));
        #endif
    }

    break; case data_map_change_info: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t votes = read.U8(), needed = read.U8();
        const uint16_t vote_block_time = read.U16();
        string msg = _("*** $1/$2 votes for mapchange.", itoa(votes), itoa(needed));
        if (vote_block_time > 0)
            msg += ' ' + _("(All players needed for $1 more seconds.)", itoa(vote_block_time));
        addThreadMessage(new TM_Text(msg_info, msg));
        #endif
    }

    break; case data_too_much_talk:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Too much talk. Chill...")));
        #endif

    break; case data_mute_notification:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("You are muted. You can't send messages.")));
        #endif

    break; case data_tournament_update_failed:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Updating your tournament score failed!")));
        #endif

    break; case data_player_mute: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8(0, maxplayers - 1), mode = read.U8();
        string admin = read.str();
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
        #endif
    }

    break; case data_player_kick: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8(0, maxplayers - 1);
        const uint32_t minutes = read.U32();
        string admin = read.str();
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
        #endif
    }

    break; case data_disconnecting:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Disconnecting in $1...", itoa(read.U8()))));
        #endif

    break; case data_idlekick_warning:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("*** Idle kick: move or be kicked in $1 seconds.", itoa(read.U8()))));
        #endif

    break; case data_broken_map:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("This map is broken. There is an instantly capturable flag. Avoid it.")));
        #endif

    break; case data_acceleration_modes: {
        const uint32_t mask = read.U32();
        for (int i = 0; i < maxplayers; ++i)
            fx.player[i].accelerationMode = (mask & (1 << i)) ? AM_Gun : AM_World;
    }

    break; case data_flag_modes: {
        const uint8_t mask = read.U8();
        lock_team_flags_in_effect = mask & 8;
        lock_wild_flags_in_effect = mask & 4;
        capture_on_team_flags_in_effect = mask & 2;
        capture_on_wild_flags_in_effect = mask & 1;
    }

    break; case data_waiting_time: {
        const uint32_t waiting_time = read.U32dyn8();
        if (waiting_time >= 10)
            next_respawn_time = get_time() + waiting_time / 10.;
    }

    break; case data_extension_advantage:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Warning: This server has extensions enabled that give an advantage over you to players with a supporting Outgun client.")));
        #endif

    break; default:
        if (code < data_reserved_range_first || code > data_reserved_range_last) {
            log("Unknown message code: %d, length %d", code, data.size());
            return false;
        }
        // just ignore commands in reserved range: they're probably some extension we don't have to care about
    }
    return true;
}

void Client::process_incoming_data(ConstDataBlockRef data) throw () {
    Lock ml(frameMutex);

    if (!connected && !replaying) // means that the connection notification is still in the thread message queue
        return;

    lastpackettime = get_time();

    if (replaying) {
        #ifndef DEDICATED_SERVER_ONLY
        const int frameSize = process_replay_frame_data(data);
        BinaryDataBlockReader read(data);
        read.block(frameSize);
        while (read.hasMore())
            if (!process_message(read.block(read.U32()))) {
                log.error(_("Format error in replay file."));
                stop_replay();
                return;
            }
        #endif
    }
    else {
        if (!process_live_frame_data(data)) {
            nAssert(!botmode);
            addThreadMessage(new TM_DoDisconnect());
            return;
        }
        for (;;) {
            const ConstDataBlockRef message = client->receive_message();
            if (!message.data())
                break;
            if (!process_message(message)) {
                nAssert(!botmode);
                log.error(_("Format error in data received from the server."));
                addThreadMessage(new TM_DoDisconnect());
                return;
            }
        }
    }
}

#ifndef DEDICATED_SERVER_ONLY
//send chat message
void Client::send_chat(const string& text) throw () {
    if (text.empty() || text == "." || isFlood(text))
        return;
    BinaryBuffer<256> msg;
    msg.U8(data_text_message);
    msg.str(text);
    client->send_message(msg);
}

//print message to "console"
void Client::print_message(Message_type type, const string& msg, int sender_team) throw () {
    if (botmode)
        return;
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
    while (chatbuffer.size() > graphics.chat_max_lines() + lines.size())
        chatbuffer.pop_front();
    for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li) {
        Message message(type, *li, static_cast<int>(fx.frame / 10), sender_team);
        if (highlight)
            message.highlight();
        chatbuffer.push_back(message);
    }
}

void Client::save_screenshot() throw () {
    string filename;
    for (int i = 0; i < 1000; i++) {
        // filename: screens/outgxxx.ext
        ostringstream fname;
        fname << wheregamedir << "screens" << directory_separator;
        fname << "outg" << setfill('0') << setw(3) << i << Graphics::save_extension;
        if (!file_exists(fname.str().c_str(), FA_ARCH | FA_DIREC | FA_HIDDEN | FA_RDONLY | FA_SYSTEM, 0)) {
            filename = fname.str();
            break;
        }
    }

    string message;
    if (graphics.save_screenshot(filename))
        message = _("Saved screenshot to $1.", filename);
    else
        message = _("Could not save screenshot to $1.", filename);
    print_message(msg_warning, message);
}

//toggle help screen
void Client::toggle_help() throw () {
    if (openMenus.safeTop() == &menu.help.menu)
        openMenus.close();
    else
        showMenu(menu.help);
}
#endif

void Client::handlePendingThreadMessages() throw () {    // should only be called by the main thread
    while (!messageQueue.empty()) {
        ThreadMessage* msg = messageQueue.front();
        messageQueue.pop_front();
        msg->execute(this);
        delete msg;
    }
}

#ifndef DEDICATED_SERVER_ONLY
string Client::refreshStatusAsString() const throw () {
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

void Client::getServerListThread() throw () {
    nAssert(refreshStatus == RS_running);

    // get server list and refresh
    bool ok = getServerList();
    if (!get_local_servers())
        ok = false;
    if (!abortThreads)
        if (!refresh_all_servers())
            ok = false;

    refreshStatus = ok ? RS_none : RS_failed;
}

void Client::refreshThread() throw () {
    nAssert(refreshStatus == RS_running);
    refreshStatus = refresh_all_servers() ? RS_none : RS_failed;
}

class TempPingData {    // internal to Client::refresh_all_servers
    double st[4];   // send time
    int rc;         // count of received packets
    double rt;      // sum of pings (for averaging)

public:
    TempPingData() throw () : rc(0), rt(0) { }
    void send(int pack) throw () { g_timeCounter.refresh(); st[pack] = get_time(); }
    void receive(int pack) throw () { g_timeCounter.refresh(); ++rc; rt += get_time() - st[pack]; }
    int received() const throw () { return rc; }
    int ping() const throw () { return static_cast<int>(1000 * rt / rc); }
};

bool Client::refresh_all_servers() throw () {
    refreshStatus = RS_contacting;

    serverListMutex.lock();

    vector<ServerListEntry*> servers;
    for (vector<ServerListEntry>::iterator si = gamespy.begin(); si != gamespy.end(); ++si)
        servers.push_back(&*si);
    if (!menu.connect.favorites())
        for (vector<ServerListEntry>::iterator si = mgamespy.begin(); si != mgamespy.end(); ++si)
            servers.push_back(&*si);

    const int nServers = static_cast<int>(servers.size());
    vector<TempPingData> tempd(nServers);
    int pending = nServers;         // count of valid entries still waiting for a response

    for (int i = 0; i < nServers; i++) {
        servers[i]->refreshed = true;
        servers[i]->ping = 0;
    }

    serverListMutex.unlock();

    if (pending == 0)
        return true;

    Network::UDPSocket sock(true);
    try {
        sock.open(Network::NonBlocking, 0);
    } catch (const Network::Error& e) {
        log.error(_("Can't open socket for refreshing servers. $1", e.str()));
        return false;
    }

    for (int round = 0; round < 20; round++) {  // each round takes .1 seconds
        if (abortThreads) {
            log("Refreshing servers aborted: client exiting.");
            return false;
        }

        if (round < 4) {    // on first 4 rounds, packets are sent to each server
            Lock ml(serverListMutex);
            for (int i = 0; i < nServers; i++) {
                BinaryBuffer<512> msg;
                msg.U32(0);         //special packet
                msg.U32(200);       //serverinfo request
                msg.U8(i);        //connect entry (am I lazy or what)
                msg.U8(round);        //packet number

                try {
                    sock.write(servers[i]->address(), msg);
                    tempd[i].send(round);
                } catch (Network::Error&) { } //#fix: report?
            }
        }

        if (pending == 0)
            break;

        // parse received responses
        for (int subRound = 0; subRound < 20; subRound++) {
            platSleep(5);

            for (;;) {  // continue while there are new packets
                char buffer[512];
                Network::UDPSocket::ReadResult result;
                try {
                    result = sock.read(buffer, 512);
                } catch (Network::Error&) {
                    break; //#fix: report?
                }
                if (result.length == 0)
                    break;
                if (result.length < 10)
                    continue;

                BinaryDataBlockReader msg(buffer, result.length);

                const uint32_t dw1 = msg.U32(), dw2 = msg.U32();
                if (dw1 != 0 || dw2 != 200)
                    continue;

                const uint8_t index = msg.U8(); // entry number echoed by the server
                const uint8_t pack = msg.U8();  // packet #

                if (index >= nServers || pack >= 4 || pack > round)  // don't have to worry about < 0 because they're unsigned
                    continue;

                Lock ml(serverListMutex);

                if (result.source != servers[index]->address())
                    continue;

                servers[index]->info = msg.str();

                if (tempd[index].received() == 0)   // first reply -> server has changed to being valid
                    pending--;

                tempd[index].receive(pack);
                servers[index]->ping = tempd[index].ping();

                servers[index]->noresponse = false;  // set here in advance so that the main thread will already show it
            }
        }
    }

    // mark those that got no responses
    {
        Lock ml(serverListMutex);
        for (int i = 0; i < nServers; i++)
            if (tempd[i].received() == 0)
                servers[i]->noresponse = true;
    }

    return true;
}

bool Client::getServerList() throw () {
    if (!g_masterSettings.address().valid())
        return false;

    refreshStatus = RS_connecting;

    try {
        Network::TCPSocket sock(Network::NonBlocking, 0, true);
        sock.connect(g_masterSettings.address());

        const string request = build_http_request(false, g_masterSettings.host(), g_masterSettings.query(),
                                                  "simple"
                                                  "&branch=" + url_encode(GAME_BRANCH) +
                                                  "&master=" + itoa(g_masterSettings.crc()) +
                                                  "&protocol=" + url_encode(GAME_PROTOCOL));
        sock.persistentWrite(request, &abortThreads, 30000);
        log("Successfully sent query to master: '%s'", formatForLogging(request).c_str());

        refreshStatus = RS_receiving;

        stringstream response;
        sock.readAll(response, &abortThreads, 30000);
        sock.close();

        log("Full response: '%s'", formatForLogging(response.str()).c_str());
        if (parseServerList(response))
            return true;
        else {
            log.error(_("Incorrect data received from master server."));
            return false;
        }
    } catch (Network::ExternalAbort) {
        return false;
    } catch (const Network::Error& e) {
        log.error(_("Getting server list: $1", e.str()));
        return false;
    }
}

bool Client::get_local_servers() throw () {
    refreshStatus = RS_connecting;

    try {
        Network::UDPSocket sock(Network::NonBlocking, 0, true, true);

        static const char broadcast_string[] = "Outgun";

        BinaryBuffer<512> msg;
        msg.U32(0);
        msg.str(broadcast_string);
        sock.write("255.255.255.255:" + itoa(DEFAULT_UDP_PORT), msg);
        log("Successfully sent broadcast query.");

        refreshStatus = RS_receiving;

        platSleep(500);
        for (;;) {
            char buffer[512];
            const Network::UDPSocket::ReadResult result = sock.read(buffer, sizeof(buffer));
            if (result.length == 0)
                break;

            log("Response from %s: '%s'", result.source.toString().c_str(), formatForLogging(buffer).c_str());

            BinaryDataBlockReader read(buffer, result.length);
            if (read.str() != broadcast_string)
                continue;   // Not an Outgun server.

            ServerListEntry spy;
            spy.setAddress(result.source);
            mgamespy.push_back(spy);
        }
        return true;
    } catch (const Network::Error& e) {
        log("Querying LAN servers: %s", e.str().c_str());
        return false;
    }
}

bool Client::parseServerList(istream& response) throw () {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();

    string line, empty;

    // Skip HTTP headers.
    while (getline(response, line, '\r') && getline(response, empty, '\n'))
        if (line.empty())
            break;

    // The first line is the newest version.
    string newest_version;
    getline_smart(response, newest_version);
    if (newest_version.empty())
        return false;

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

    Lock ml(serverListMutex);

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

    if (servers_read != total_servers)
        return false;

    if (newest_version != GAME_RELEASED_VERSION)
        menu.newVersion.set(_("New version: $1", newest_version));
    else
        menu.newVersion.set("");

    return true;
}

void Client::handleKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw () {  // sc = scancode, ch = character, as returned by readkey
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
            else if (connected || replaying)
                showMenu(m_serverInfo);
            stats_autoshowing = false;
        break; case KEY_F3: case KEY_N:
            if (withControl)
                menu.options.graphics.showNames.set(menu.options.graphics.showNames() == Menu_graphics::N_Never ? Menu_graphics::N_SameRoom : Menu_graphics::N_Never);
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
        break; case KEY_M:
            if (withControl) {
                menu.options.sounds.enabled.toggle();
                MCF_sndEnableChange();
            }
            else
                handled = false;
        break; default:
            handled = false;
    }
    if (handled)
        return;
    if (!openMenus.empty()) {
        Lock ml(frameMutex);   // some menus need access
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
            else if (!talkbuffer.empty()) { // cancel chat
                talkbuffer.clear();
                talkbuffer_cursor = 0;
            }
            else
                showMenu(menu);
        break; case KEY_F2:
            if (!replaying) {
                menusel = (menusel == menu_maps ? menu_none : menu_maps);
                stats_autoshowing = false;
                if (menusel == menu_maps) {
                    load_fav_maps();
                    apply_fav_maps();
                }
            }
        break; case KEY_F3:
            menusel = (menusel == menu_teams ? menu_none : menu_teams);
            stats_autoshowing = false;
        break; case KEY_F4:
            menusel = (menusel == menu_players ? menu_none : menu_players);
            stats_autoshowing = false;
        break; case KEY_F8: {
            if (!replaying) {
                want_map_exit = !want_map_exit;
                want_map_exit_delayed = false;

                BinaryBuffer<16> msg;
                if (want_map_exit)
                    msg.U8(data_map_exit_on);
                else
                    msg.U8(data_map_exit_off);
                client->send_message(msg);
            }
        }
        break; case KEY_F12:
            graphics.toggle_full_playfield();
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

bool Client::handleInfoScreenKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw () {  // sc = scancode, ch = character, as returned by readkey
    (void)(withControl & alt_sequence);
    if (menu.options.controls.arrowKeysInStats() != Menu_controls::AS_useMenu && (sc == KEY_UP || sc == KEY_DOWN || sc == KEY_LEFT || sc == KEY_RIGHT))
        return false;
    switch (menusel) {
    /*break;*/ case menu_maps:
            switch (sc) {
            /*break;*/ case KEY_UP:     graphics.map_list_prev();
                break; case KEY_DOWN:   graphics.map_list_next();
                break; case KEY_PGUP:   graphics.map_list_prev_page();
                break; case KEY_PGDN:   graphics.map_list_next_page();
                break; case KEY_HOME:   graphics.map_list_begin();
                break; case KEY_END:    graphics.map_list_end();
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
                        BinaryBuffer<16> msg;
                        msg.U8(data_map_vote);
                        msg.S8(map_vote);
                        client->send_message(msg);
                    }
                }
                break; case KEY_SPACE: case KEY_RIGHT:
                    do {
                        mapListSortKey = static_cast<MapListSortKey>((mapListSortKey + 1) % MLSK_COUNT);
                    } while (mapListSortKey == MLSK_Favorite && fav_maps.empty());
                    mapListChangedAfterSort = true;
                break; case KEY_LEFT:
                    do {
                        mapListSortKey = static_cast<MapListSortKey>((mapListSortKey - 1 + MLSK_COUNT) % MLSK_COUNT);
                    } while (mapListSortKey == MLSK_Favorite && fav_maps.empty());
                    mapListChangedAfterSort = true;
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
                graphics.team_captures_prev();
            else if (sc == KEY_DOWN || sc == KEY_PGDN)
                graphics.team_captures_next();
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

void Client::handleGameKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw () {  // sc = scancode, ch = character, as returned by readkey
    if (key[KEY_P] && !replaying)         // ping setting
        if (sc == KEY_PLUS_PAD) {
            print_message(msg_info, "Ping +" + itoa(iround(client->increasePacketDelay() * 1000)));
            return;
        }
        else if (sc == KEY_MINUS_PAD) {
            print_message(msg_info, "Ping +" + itoa(iround(client->decreasePacketDelay() * 1000)));
            return;
        }

    if (toupper(ch) == 'P' && replaying) {
        replay_paused = !replay_paused;
        return;
    }

    switch (sc) {   // Allow these keys to be used also for typing text.
    /*break;*/ case KEY_MINUS_PAD:
        if (!replaying && !withControl)
            break;
        if (visible_rooms < fx.map.w || visible_rooms < fx.map.h || menu.options.graphics.repeatMap() && visible_rooms < 100) {
            if (replaying) {
                ++visible_rooms;
                if (replayTopLeftRoom.first == fx.map.w + 1 - visible_rooms && replayTopLeftRoom.first > 0) // if map border wasn't broken, don't break it either
                    --replayTopLeftRoom.first;
                if (replayTopLeftRoom.second == fx.map.h + 1 - visible_rooms && replayTopLeftRoom.second > 0)
                    --replayTopLeftRoom.second;
            }
            else
                visible_rooms += visible_rooms < 3 ? .25 : visible_rooms < 5 ? .5 : 1.;
        }
        return;
    break; case KEY_PLUS_PAD:
        if (!replaying && !withControl)
            break;
        if (visible_rooms > 1) {
            if (replaying)
                --visible_rooms;
            else
                visible_rooms -= visible_rooms <= 3 ? .25 : visible_rooms <= 5 ? .5 : 1.;
        }
        return;
    }

    switch (sc) {
    /*break;*/ case KEY_HOME:   // change colours
            if (replaying)
                replay_rate = 1;
            else {
                if (withControl)
                    graphics.reset_playground_colors();
                else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty())
                    talkbuffer_cursor = 0;
                else
                    graphics.random_playground_colors();
            }
        break; case KEY_INSERT:
            show_all_messages = !show_all_messages;
        break; case KEY_BACKSPACE:
            if (!talkbuffer.empty() && talkbuffer_cursor > 0) {
                talkbuffer.erase(talkbuffer_cursor - 1, 1);
                talkbuffer_cursor--;
            }
        break; case KEY_ENTER: case KEY_ENTER_PAD:
            if (!talkbuffer.empty()) {
                send_chat(trim(talkbuffer));
                talkbuffer.clear();
                talkbuffer_cursor = 0;
            }
        break; case KEY_DEL: {
            if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty())
                talkbuffer.erase(talkbuffer_cursor, 1);
            else if (!replaying) {
                BinaryBuffer<16> msg;
                msg.U8(data_suicide);
                client->send_message(msg);
            }
        }
        break; case KEY_LEFT: {
            if (replaying)
                replayTopLeftRoom.first = (replayTopLeftRoom.first - 1 + fx.map.w) % fx.map.w;
            else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty() && talkbuffer_cursor > 0)
                talkbuffer_cursor--;
        }
        break; case KEY_RIGHT: {
            if (replaying)
                replayTopLeftRoom.first = (replayTopLeftRoom.first + 1) % fx.map.w;
            else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty() && talkbuffer_cursor < static_cast<int>(talkbuffer.size()))
                talkbuffer_cursor++;
        }
        break; case KEY_UP: {
            if (replaying)
                replayTopLeftRoom.second = (replayTopLeftRoom.second - 1 + fx.map.h) % fx.map.h;
        }
        break; case KEY_DOWN: {
            if (replaying)
                replayTopLeftRoom.second = (replayTopLeftRoom.second + 1) % fx.map.h;
        }
        break; case KEY_PGUP: {
            if (replaying && (replay_rate *= 2) > 128)
                replay_rate = 128;
        }
        break; case KEY_PGDN: {
            if (replaying && (replay_rate /= 2) < 1 / 32.)
                replay_rate = 1 / 32.;
        }
        break; case KEY_END: {
            if (!replaying && withControl) {
                want_change_teams = !want_change_teams;
                BinaryBuffer<16> msg;
                msg.U8(want_change_teams ? data_change_team_on : data_change_team_off);
                client->send_message(msg);
            }
            else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty())
                talkbuffer_cursor = talkbuffer.size();
        }
        break; case KEY_TAB:    // This also prevents annoying Control+Tab character from chat input.
            if (replaying) {
                const int dir = key[KEY_LSHIFT] || key[KEY_RSHIFT] ? -1 : 1;
                me += dir;
                if (me < -1)
                    me = maxplayers - 1;
                for (; me >= 0 && me < maxplayers; me += dir)
                    if (fx.player[me].used)
                        break;
                if (me >= maxplayers)
                    me = -1;
            }
        break; default:
            // Add character to text
            if (!replaying && talkbuffer.length() < max_chat_message_length && !is_nonprintable_char(ch) &&
                    (!menu.options.controls.keypadMoving() || (!is_keypad(sc) && !alt_sequence))) {
                talkbuffer.insert(talkbuffer_cursor, 1, static_cast<char>(ch));
                talkbuffer_cursor++;
            }
    }
}

void Client::loop(volatile bool* quitFlag, bool firstTimeSplash) throw () {
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
    replaying = false;
    spectating = false;

    g_timeCounter.refresh();
    double nextSend = get_time();
    double nextClientFrame = get_time();

    if (!extConfig.autoPlay.empty()) {
        Network::ResolveError err;
        if (!serverIP.tryResolve(extConfig.autoPlay, &err))
            log.error(err.str());
        else {
            if (serverIP.getPort() == 0)
                serverIP.setPort(DEFAULT_UDP_PORT);
            connect_command(true);
        }
    }
    else if (!extConfig.autoSpectate.empty()) {
        Network::Address addr;
        Network::ResolveError err;
        if (!addr.tryResolve(extConfig.autoSpectate, &err))
            log.error(err.str());
        else if (addr.getPort() == 0)
            log.error(_("Port is missing from $1.", extConfig.autoSpectate));
        else
            start_spectating(addr);
    }
    else if (!extConfig.autoReplay.empty())
        start_replay(extConfig.autoReplay);

    bool prevFire = false, prevDropFlag = false;
    while (!quitCommand && !*quitFlag) {
        // (1) loop doing input/sleep before next send or draw time
        for (;;) {
            if (keyboard_needs_poll())
                poll_keyboard();    // ignore return value
            if (mouse_needs_poll())
                poll_mouse();

            const bool controlPressed = key[KEY_LCONTROL] || key[KEY_RCONTROL];

            //quit key Control-F12
            if (controlPressed && key[KEY_F12]) {
                quitCommand = true;
                break;
            }

            static bool alt_sequence = false;

            if (menu.options.controls.keypadMoving()) {
                // Check Alt+keypad sequences
                if (key_shifts & KB_INALTSEQ_FLAG)
                    alt_sequence = true;
            }

            // handle waiting keypresses
            while (keypressed()) {
                const int ch = readkey();
                handleKeypress(ch >> 8, ch & 0xFF, controlPressed, alt_sequence);
            }

            if (!(key_shifts & KB_INALTSEQ_FLAG))
                alt_sequence = false;

            // handle current keypresses (only used in game)
            if (openMenus.empty() && !replaying) {
                bool sendnow = false;

                // control == fire
                const bool fire = firePressed();
                if (fire != prevFire) {
                    prevFire = fire;

                    BinaryBuffer<16> msg;
                    msg.U8(fire ? data_fire_on : data_fire_off);
                    client->send_message(msg);

                    //send early keys packet
                    sendnow = true;
                }

                if (menusel == menu_none) { // page down is reserved for stats screens
                    const bool dropFlag = key[KEY_PGDN];
                    if (dropFlag != prevDropFlag) {
                        prevDropFlag = dropFlag;
                        BinaryBuffer<16> msg;
                        msg.U8(dropFlag ? data_drop_flag : data_stop_drop_flag);
                        client->send_message(msg);
                    }
                }

                send_frame(false, sendnow);
            }

            while (!replaying && (clientReadiesWaiting > 1 ||
                   (clientReadiesWaiting && (!menu.options.game.stayDead() ||
                                             openMenus.empty() && menusel == menu_none)))) {
                send_client_ready();
                --clientReadiesWaiting;
            }

            {
                Lock ml(frameMutex);
                handlePendingThreadMessages();

                if (GlobalDisplaySwitchHook::readAndClear() && menu.options.screenMode.flipping())
                    graphics.videoMemoryCorrupted();
            }

            g_timeCounter.refresh();
            if (get_time() >= nextSend || get_time() >= nextClientFrame)
                break;

            platSleep(2);
        }

        if (get_time() >= nextSend) {
            nextSend += .1; // match 10 Hz frame frequency of server
            nextSend += netsendAdjustment;
            netsendAdjustment = 0;  // losing a value due to concurrency is vaguely possible but affordable
            if (get_time() > nextSend)   // don't accumulate lag
                nextSend = get_time();
            if (!replaying)
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

        if (replaying && !replay_stopped) {
            if (spectating)
                continue_spectating();
            const double time = get_time();
            if (!replay_paused)
                replaySubFrame += (time - replayTime) * 10. * replay_rate;
            replayTime = time;
            for (; replaySubFrame >= 1.; replaySubFrame -= 1.)
                continue_replay();
        }

        // the rest is drawing

        if (gameshow && (replaying || me >= 0)) {
            Lock ml(frameMutex);

            ClientPhysicsCallbacks cb(*this);
            if (replaying)
                fd.extrapolate(fx, cb, -1, controlHistory, 0, 0, replaySubFrame);
            else if (menu.options.game.lagPrediction()) {
                const double lagWanted = 2. * (1. - menu.options.game.lagPredictionAmount() / 10.); // lagPredictionAmount() is in range [0, 10]
                double timeDelta = max<double>(0., averageLag - lagWanted) + (get_time() - frameReceiveTime) * 10.;
                uint8_t firstFrame, lastFrame;
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
                if (!fx.player[me].dead) {
                    if (fx.physics.allowFreeTurning && menu.options.controls.aimMode() != Menu_controls::AM_8way)
                        fx.player[me].gundir = gunDir;
                    else
                        for (uint8_t controlFrame = lastFrame; controlFrame != clFrameWorld; --controlFrame) {
                            if (controlHistory[controlFrame].isStrafe())
                                continue;
                            const int dir = controlHistory[controlFrame].getDirection();
                            if (dir != -1) {
                                fx.player[me].gundir.from8way(dir);
                                break;
                            }
                        }
                }
                fd.extrapolate(fx, cb, me, controlHistory, firstFrame, lastFrame, timeDelta);
            }
            else {
                if (fx.physics.allowFreeTurning && !fx.player[me].dead && menu.options.controls.aimMode() != Menu_controls::AM_8way)
                    fx.player[me].gundir = gunDir;
                double timeDelta = (get_time() - frameReceiveTime) * 10.;
                fd.extrapolate(fx, cb, me, controlHistory, clFrameWorld, clFrameWorld, timeDelta);
            }

            if (mapChanged) {
                mapChanged = false;
                if (current_map < int(maps.size()))
                    maps[current_map].update(fx.map);
                graphics.mapChanged();
                if (replaying)
                    visible_rooms = menu.options.graphics.visibleRoomsReplay();
                else
                    visible_rooms = menu.options.graphics.visibleRoomsPlay();
                if (visible_rooms > fx.map.w && visible_rooms > fx.map.h)
                    visible_rooms = max(fx.map.w, fx.map.h);
                if (replaying) {
                    const int team = rand() % 2;
                    replayTopLeftRoom = pair<int, int>();
                    if (!fx.map.tinfo[team].flags.empty()) {
                        const WorldCoords& pos = fx.map.tinfo[team].flags[rand() % fx.map.tinfo[team].flags.size()];
                        replayTopLeftRoom = pair<int, int>(max(0, pos.px + 1 - static_cast<int>(visible_rooms)), max(0, pos.py + 1 - static_cast<int>(visible_rooms)));
                    }
                    else if (!fx.map.wild_flags.empty()) {
                        const WorldCoords& pos = fx.map.wild_flags[rand() % fx.map.wild_flags.size()];
                        replayTopLeftRoom = pair<int, int>(max(0, pos.px + 1 - static_cast<int>(visible_rooms)), max(0, pos.py + 1 - static_cast<int>(visible_rooms)));
                    }
                }

                mapWrapsX = mapWrapsY = false;
                static const int testSkip = PLAYER_RADIUS / 2;
                for (int side = 0; side < 2; ++side) {
                    for (int rx = 0; rx < fx.map.w && !mapWrapsY; ++rx) {
                        const int ry = side ? fx.map.h - 1 : 0;
                        const int ly = side ? plh : 0;
                        for (int lx = 0; lx < plw; lx += testSkip)
                            if (!fx.map.fall_on_wall(rx, ry, lx, ly, PLAYER_RADIUS)) {
                                mapWrapsY = true;
                                break;
                            }
                    }
                    for (int ry = 0; ry < fx.map.h && !mapWrapsX; ++ry) {
                        const int rx = side ? fx.map.w - 1 : 0;
                        const int lx = side ? plw : 0;
                        for (int ly = 0; ly < plh; ly += testSkip)
                            if (!fx.map.fall_on_wall(rx, ry, lx, ly, PLAYER_RADIUS)) {
                                mapWrapsX = true;
                                break;
                            }
                    }
                }
            }

            if (graphics.needRedrawMap())
                graphics.update_minimap_background(fx.map);

            refreshGunDir();

            // update carried flags' positions
            for (int t = 0; t < 3; t++) {
                const vector<Flag>& flags = t == 2 ? fx.wild_flags : fx.teams[t].flags();
                int f = 0;
                for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi, ++f) {
                    if (!fi->carried())
                        continue;
                    const ClientPlayer& pl = fx.player[fi->carrier()];
                    const WorldCoords pos = playerPos(fi->carrier());
                    nAssert(pos.px >= 0 && pos.py >= 0);
                    if (!pl.used || pos.px >= fx.map.w || pos.py >= fx.map.h)
                        continue;
                    if (t < 2)
                        fx.teams[t].move_flag(f, pos);
                    else
                        fx.wild_flags[f].move(pos);
                }
            }

            graphics.startDraw();
            draw_game_frame();

            #ifdef ROOM_CHANGE_BENCHMARK
            if (benchmarkRuns >= 500)
                quitCommand = true;
            #endif
        } else {
            graphics.startDraw();
            graphics.draw_background(false);
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
            Lock ml(frameMutex);   // some menus need access
            draw_game_menu();
        }

        graphics.endDraw();
        graphics.draw_screen(!menu.options.screenMode.alternativeFlipping());
        if (screenshot) {
            save_screenshot();
            screenshot = false;
        }
    }

    //client exit cleanup: done at stop wich needs to be called after loop
}

void Client::start_replay(const std::string& filename) throw () {
    disconnect_command();
    stop_replay();
    openMenus.clear();
    replay.clear();
    replay.open(filename.c_str(), ios::binary);
    if (!replay)
        log.error(_("Could not open replay file $1.", filename.c_str()));
    else if (!start_replay(replay))
        replay.close();
}

bool Client::start_replay(istream& replay) throw () {
    BinaryStreamReader read(replay);

    const string identification = read.constLengthStr(REPLAY_IDENTIFICATION.length());
    log("Identification: %s", identification.c_str());
    if (identification != REPLAY_IDENTIFICATION) {
        log.error(_("This is not an Outgun replay."));
        return false;
    }

    unsigned replay_version = read.U32();
    log("Replay version: %u", replay_version);
    if (replay_version > REPLAY_VERSION) {   // incompatible replay
        log.error(_("This is a newer replay version ($1).", itoa(replay_version)));
        return false;
    }

    replay_length = read.U32();
    replay_first_frame_loaded = false;

    hostname = read.str();
    string caption;
    if (spectating)
        caption = _("Spectating on $1", hostname.substr(0, 32));
    else
        caption = _("Replay on $1", hostname.substr(0, 32));
    extConfig.statusOutput(caption);

    setMaxPlayers(read.U32());
    read.str(); // ignore map name

    replaying = true;
    replay_rate = 1;
    replay_paused = false;
    replay_stopped = false;
    replayTime = get_time();
    replaySubFrame = 0;
    replayTopLeftRoom = pair<int, int>();

    show_all_messages = false;
    stats_autoshowing = false;

    m_serverInfo.clear();
    m_serverInfo.addLine("");   // can't draw a totally empty menu; this will be overwritten with config information

    lastpackettime = get_time() + 4.0;
    averageLag = 0;
    clFrameSent = clFrameWorld = 0;
    frameReceiveTime = 0;

    gameshow = false;

    fd.frame = -1;
    fd.skipped = true;
    fx.frame = -1;
    fx.skipped = true;
    me = -1;

    talkbuffer.clear();
    talkbuffer_cursor = 0;
    chatbuffer.clear();

    for (int i = 0; i < 2; i++) {
        fx.teams[i].clear_stats();
        fx.teams[i].remove_flags();
    }
    remove_flags = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
        fx.player[i].clear(false, i, "", i / TSIZE);
    players_sb.clear();

    fx.reset();
    fd.reset();

    framecount = 0;
    frameCountStartTime = get_time();
    FPS = 0;

    map_time_limit = false;
    map_start_time = 0;
    map_end_time = 0;

    map_ready = false;
    clientReadiesWaiting = 0;

    gameover_plaque = NEXTMAP_NONE;

    graphics.clear_fx();
    gameshow = true;

    return true;
}

void Client::continue_replay() throw () {
    if (spectating)
        continue_replay(spectate_buffer);
    else
        continue_replay(replay);
}

void Client::continue_replay(istream& in) throw () {
    const istream::pos_type pos = in.tellg();
    BinaryStreamReader read(in);
    try {
        process_incoming_data(read.block(read.U32()));
    } catch (BinaryReader::ReadOutside) {
        in.clear();
        in.seekg(pos);
        if (replay_length > 0)
            replay_stopped = true;
        else if (replay_rate > 1)
            replay_rate = 1;
    }
}

void Client::stop_replay() throw () {
    if (!replaying)
        return;

    replay.close();
    spectate_socket.closeIfOpen();

    if (spectating)
        spectate_buffer.str("");

    replaying = false;
    spectating = false;
    gameshow = false;

    extConfig.statusOutput(_("Outgun client"));

    menusel = menu_none;
}

void Client::start_spectating(const Network::Address& address) throw () {
    disconnect_command();
    stop_replay();

    log("Start spectating.");
    serverIP = address;

    try {
        spectate_socket.open(Network::NonBlocking, 0);
        spectate_socket.connect(serverIP);

        BinaryBuffer<512> request;
        request.str(GAME_STRING);
        request.U32dyn8(RELAY_PROTOCOL);
        request.U32dyn8(RELAY_PROTOCOL_EXTENSIONS_VERSION);
        request.str("SPECTATOR");
        request.U32dyn8(REPLAY_VERSION);
        request.str(string()); // username
        request.str(string()); // password
        request.U32dyn8(0); // no extension data

        spectate_socket.persistentWrite(request, 500, 5);
        log("Init data sent to the relay (%u bytes).", request.size());
    } catch (const Network::Error& e) {
        spectate_socket.closeIfOpen();
        log.error(_("Connecting to relay: $1", e.str()));
        return;
    }

    spectating = true;
    replaying = true;
    replay_rate = 1;
    spectate_data_received = false;

    openMenus.clear();
    m_connectProgress.clear();
    m_connectProgress.wrapLine(_("Waiting for the game to start."));
    showMenu(m_connectProgress);
}

void Client::continue_spectating() throw () {
    if (!spectate_socket.isOpen()) {
        log.error(_("Connection to the server closed."));
        openMenus.close(&m_connectProgress.menu);
        stop_replay();
        return;
    }

    const int max_buffer_size = 20000;
    char buffer[max_buffer_size];
    int result;
    try {
        result = spectate_socket.read(buffer, max_buffer_size);
    } catch (const Network::Error& e) {
        log.error(_("Connection to the server closed: $1", e.str()));
        openMenus.close(&m_connectProgress.menu);
        stop_replay();
        return;
    }
    if (result == 0)
        return;

    if (!spectate_data_received) {
        log("First data from relay, %d bytes: %s", result, buffer);
        spectate_data_received = true;
        spectate_buffer.write(buffer + 1, result - 1); // the first byte is the relay's protocol extensions level, about which we don't have to care so far
        if (!start_replay(spectate_buffer)) {
            log.error(_("Could not start spectating."));
            stop_replay();
        }
        // Keep the waiting dialog still on as the relay may delay the actual game frames.
    }
    else {
        openMenus.close(&m_connectProgress.menu);
        spectate_buffer.write(buffer, result);
    }
}
#endif // !DEDICATED_SERVER_ONLY

void Client::bot_loop() throw () {
    Lock ml(frameMutex);

    handlePendingThreadMessages();

    if (!connected || fx.frame == botReactedFrame)
        return;

    botReactedFrame = fx.frame;

    while (clientReadiesWaiting > 0) {
        send_client_ready();
        --clientReadiesWaiting;
    }

    if (mapChanged) {
        mapChanged = false;
        BuildMap();
    }

    fx.cleanOldDeathbringerExplosions();

    ClientControls controls = Robot();
    controls.clearModifiersIfIdle();
    bot_send_frame(controls);
}

void Client::stop() throw () {
    log("Client exiting: stop() called");

    abortThreads = true;

    //at least disconnect
    disconnect_command();
    #ifndef DEDICATED_SERVER_ONLY
    stop_replay();
    #endif

    if (botmode) {
        finished = true;
        return;
    }

    #ifndef DEDICATED_SERVER_ONLY
    tournamentPassword.stop();

    //save configuration file
    string fileName = wheregamedir + "config" + directory_separator + "client.cfg";
    log("Saving client configuration in %s", fileName.c_str());
    ofstream cfg(fileName.c_str());
    if (cfg) {
        for (SettingManager::MapT::const_iterator si = settings.read().begin(); si != settings.read().end(); ++si) {
            nAssert(si->second);
            cfg << si->first << ' ';
            si->second->save(cfg);
            cfg << '\n';
        }

        // some more complicated settings need to be handled here
        cfg << CCS_PlayerName << ' ' << playername << '\n';
        {   // favorite colors
            cfg << CCS_FavoriteColors << ' ';
            if (menu.options.player.favoriteColors.values().empty())
                cfg << -1;
            else {
                const vector<int>& colVec = menu.options.player.favoriteColors.values();
                for (vector<int>::const_iterator col = colVec.begin(); col != colVec.end(); ++col)
                    cfg << *col << ' ';
            }
            cfg << '\n';
        }
        cfg << CCS_KeyboardLayout << ' ' << menu.options.controls.keyboardLayout() << '\n';
        {
            ScreenMode mode = menu.options.screenMode.resolution();
            cfg << CCS_GFXMode << ' ' <<  mode.width << ' ' << mode.height << ' ' << menu.options.screenMode.colorDepth() << '\n';
        }
        cfg << CCS_Antialiasing << ' ' << (menu.options.graphics.antialiasing() ? 2 : 1) << '\n';

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
    FILE* psf = fopen(fileName.c_str(), "wb");
    if (psf) {
        const string& password = menu.options.player.password();
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
        Lock ml(downloadMutex);
        downloads.clear();
    }

    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        closeMessageLog();

    if (listenServer.running())
        listenServer.stop();

    log("Client stop() completed");
    #endif
}

void Client::rocketHitWallCallback(int rid, bool power, double x, double y, int roomx, int roomy) throw () {
    fx.rock[rid].owner = -1;   // erase from clientside simulation
    #ifndef DEDICATED_SERVER_ONLY
    fd.rock[rid].owner = -1;
    if (botmode)
        return;
    const double time = fx.frame / 10;
    const bool sound = on_screen_exact(roomx, roomy, x, y);
    if (power) {
        graphics.create_powerwallexplo(WorldCoords(roomx, roomy, x, y), fx.rock[rid].team, time);
        if (sound)
            play_sound(SAMPLE_POWERWALLHIT);
    }
    else {
        graphics.create_wallexplo(WorldCoords(roomx, roomy, x, y), fx.rock[rid].team, time);
        if (sound)
            play_sound(SAMPLE_WALLHIT);
    }
    #else
    (void)power; (void)x; (void)y; (void)roomx; (void)roomy;
    #endif
}

void Client::rocketOutOfBoundsCallback(int rid) throw () {
    fx.rock[rid].owner = -1;   // erase from clientside simulation
    #ifndef DEDICATED_SERVER_ONLY
    fd.rock[rid].owner = -1;
    #endif
}

void Client::playerHitWallCallback(int pid) throw () {
    #ifndef DEDICATED_SERVER_ONLY
    // play bounce sample if minimum time elapsed
    const double currTime = fx.frame / 10.;
    if (currTime > fx.player[pid].wall_sound_time && (!replaying || player_on_screen(pid))) {
        fx.player[pid].wall_sound_time = currTime + 0.2;
        play_sound(SAMPLE_WALLBOUNCE);
    }
    #else
    (void)pid;
    #endif
}

void Client::playerHitPlayerCallback(int pid1, int pid2) throw () {
    #ifndef DEDICATED_SERVER_ONLY
    // play bounce sample if minimum time elapsed
    const double currTime = fx.frame / 10.;
    if ((currTime > fx.player[pid1].player_sound_time || currTime > fx.player[pid2].player_sound_time) &&
            (!replaying || player_on_screen(pid1) || player_on_screen(pid2))) {
        fx.player[pid1].player_sound_time = fx.player[pid2].player_sound_time = currTime + 0.2;
        play_sound(SAMPLE_PLAYERBOUNCE);
    }
    #else
    (void)pid1; (void)pid2;
    #endif
}

bool Client::shouldApplyPhysicsToPlayerCallback(int pid) throw () {
    return (fx.player[pid].onscreen || replaying) && !fx.player[pid].dead;
}

void Client::remove_useless_flags() throw () {
    for (int i = 0; i < 3; i++)
        if (remove_flags & (0x01 << i)) {
            fx.remove_team_flags(i);
            #ifndef DEDICATED_SERVER_ONLY
            fd.remove_team_flags(i);
            #endif
        }
}

#ifndef DEDICATED_SERVER_ONLY
void Client::play_sound(int sample) throw () {
    int freq = 1000;
    if (replaying) {
        if (replay_rate > 10)
            return;
        freq = int(freq * replay_rate);
    }
    client_sounds.play(sample, freq);
}

WorldCoords Client::playerPos(int pid) const throw () {
    const ClientWorld& world = fx.player[pid].onscreen || replaying ? fd : fx;
    return WorldCoords(world.player[pid].roomx,
                       world.player[pid].roomy,
                       world.player[pid].lx,
                       world.player[pid].ly);
}

static double getViewStartCoord(int myRoom, double myLocal, int roomSize, int mapSize, bool mapWraps, double visibleRooms, Menu_graphics::ViewOverBorderMode view, bool scroll) throw () {
    double coordBase = 0;
    if (visibleRooms >= mapSize) {
        coordBase = - (visibleRooms - mapSize) * .5; // as this is added to the view start coordinates, effectively that position then starts the map sized region in the middle of the screen
        visibleRooms = mapSize; // since we are now operating on the map-sized region
        if (view != Menu_graphics::VOB_Always && (view != Menu_graphics::VOB_MapWraps || !mapWraps))
            return coordBase;
    }
    const double center = myRoom + (scroll ? myLocal / roomSize : .5);
    const double halfw = visibleRooms * .5;
    if (view == Menu_graphics::VOB_Never || view != Menu_graphics::VOB_Always && !mapWraps) {
        if (center - halfw < 0)
            return coordBase;
        if (center + halfw >= mapSize)
            return coordBase + mapSize - visibleRooms;
    }
    return coordBase + center - halfw;
}

static pair<int, double> splitCompositeCoord(double comp, int roomSize, int mapSize) throw () {
    const double local = positiveFmod(comp, 1.);
    return pair<int, double>(positiveModulo(iround(comp - local), mapSize), local * roomSize); // iround because of inaccuracies; it should be very close to integral really
}

WorldCoords Client::viewTopLeft() const throw () {
    if (!map_ready)
        return WorldCoords(0, 0, 0, 0);
    else if (me < 0)
        return WorldCoords(replayTopLeftRoom.first, replayTopLeftRoom.second, 0, 0);
    else {
        const Menu_graphics::ViewOverBorderMode view = menu.options.graphics.viewOverMapBorder();
        const bool scroll = menu.options.graphics.scroll();
        const bool repeat = menu.options.graphics.repeatMap();
        const double xVis = graphics.get_visible_rooms_x(), yVis = graphics.get_visible_rooms_y();
        const double x = getViewStartCoord(fd.player[me].roomx, fd.player[me].lx, plw, fx.map.w, mapWrapsX, repeat ? xVis : min<double>(fx.map.w, xVis), view, scroll);
        const double y = getViewStartCoord(fd.player[me].roomy, fd.player[me].ly, plh, fx.map.h, mapWrapsY, repeat ? yVis : min<double>(fx.map.h, yVis), view, scroll);
        const pair<int, double> xp = splitCompositeCoord(x, plw, fx.map.w), yp = splitCompositeCoord(y, plh, fx.map.h);
        return WorldCoords(xp.first, yp.first, xp.second, yp.second);
    }
}

pair<int, int> Client::topLeftRoom() const throw () {
    const WorldCoords c = viewTopLeft();
    return pair<int, int>(c.px, c.py);
}

//draw the whole game screen
void Client::draw_game_frame() throw () {    // call with frameMutex locked
    // hide stuff if frame skipped
    const bool hide_game = !map_ready || gameover_plaque != NEXTMAP_NONE || fx.skipped || !replaying && me < 0;

    const double time = fd.frame / 10;

    if (hide_game) {
        graphics.draw_background(map_ready);

        if (gameover_plaque == NEXTMAP_CAPTURE_LIMIT || gameover_plaque == NEXTMAP_VOTE_EXIT) {
            if (red_final_score > blue_final_score)
                graphics.draw_scores(_("RED TEAM WINS"), 0, red_final_score, blue_final_score);
            else if (blue_final_score > red_final_score)
                graphics.draw_scores(_("BLUE TEAM WINS"), 1, blue_final_score, red_final_score);
            else
                graphics.draw_scores(_("GAME TIED"), -1, blue_final_score, red_final_score);
        }

        if (map_ready)
            graphics.draw_waiting_map_message(_("Waiting game start - next map is"), fx.map.title);
        else {
            Lock ml(downloadMutex);
            if (!downloads.empty() && downloads.front().isActive()) {
                const string text = _("Loading map: $1 bytes", itoa(downloads.front().progress()));
                graphics.draw_loading_map_message(text);
            }
        }
    }
    else {
        #ifdef ROOM_CHANGE_BENCHMARK
        graphics.videoMemoryCorrupted(); // evil trick to invalidate room cache
        ++benchmarkRuns;
        #endif
        const VisibilityMap roomVis = calculateVisibilities();
        graphics.setRoomLayout(fx.map, visible_rooms, menu.options.graphics.repeatMap());
        graphics.draw_background(fx.map,
                                 roomVis,
                                 viewTopLeft(),
                                 menu.options.graphics.contTextures(),
                                 menu.options.graphics.mapInfoMode());
        draw_playfield();
        draw_map(roomVis);
    }

    graphics.draw_scoreboard(players_sb, fx.teams, maxplayers, key[KEY_TAB], menu.options.game.underlineMasterAuth(), menu.options.game.underlineServerAuth());

    graphics.draw_fps(FPS);

    // Time left if time limit is on and the game is running.
    if (map_time_limit && gameover_plaque == NEXTMAP_NONE && players_sb.size() > 1)
        if (map_end_time > time)
            graphics.draw_map_time(map_end_time - static_cast<int>(time), extra_time_running);
        else
            graphics.draw_map_time(0);

    if (replaying && replay_first_frame_loaded)
        graphics.draw_replay_info(replay_paused ? 0 : replay_rate, static_cast<unsigned>(fx.frame - replay_start_frame), replay_length, replay_stopped);
    else if (me >= 0) {
        // player's power-ups
        double val = fx.player[me].item_power_time - time;
        if (val > 0)
            graphics.draw_player_power(val);
        val = fx.player[me].item_turbo_time - time;
        if (val > 0)
            graphics.draw_player_turbo(val);
        val = fx.player[me].item_shadow_time - time;
        if (val > 0)
            graphics.draw_player_shadow(val);
        graphics.draw_player_weapon(fx.player[me].weapon);

        if (want_change_teams)
            graphics.draw_change_team_message(time);
        if (want_map_exit)
            graphics.draw_change_map_message(time, want_map_exit_delayed);
        graphics.draw_player_health(fx.player[me].health);
        graphics.draw_player_energy(fx.player[me].energy);
    }

    // the HUD: message output
    const int chat_visible = show_all_messages ? graphics.chat_max_lines() : graphics.chat_lines();
    int start = static_cast<int>(chatbuffer.size()) - static_cast<int>(chat_visible);
    if (start < 0)
        start = 0;
    list<Message>::const_iterator msg = chatbuffer.begin();
    for (int i = 0; i < start; ++i, ++msg);
    if (!show_all_messages) // drop old messages
        for (; msg != chatbuffer.end(); ++msg)
            if (time < msg->time() + 80)
                break;
    graphics.print_chat_messages(msg, chatbuffer.end(), talkbuffer, talkbuffer_cursor);

    //"server not responding... connection may have dropped" plaque
    if (get_time() > lastpackettime + 1.0 && !replaying)
        m_notResponding.menu.draw(graphics.drawbuffer(), graphics.colours());

    // debug panel
    if (key[KEY_F9]) {
        const int bpsin = client->get_socket_stat(Network::Socket::Stat_AvgBytesReceived) + spectate_socket.getStat(Network::Socket::Stat_AvgBytesReceived);
        const int bpsout = client->get_socket_stat(Network::Socket::Stat_AvgBytesSent) + spectate_socket.getStat(Network::Socket::Stat_AvgBytesSent);

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

        graphics.debug_panel(fx.player, me, bpsin, bpsout, sticks, buttons);
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

bool Client::on_screen(int x, int y) const throw () {
    const WorldCoords topLeft = viewTopLeft();
    return positiveModulo(x - topLeft.px, fx.map.w) < graphics.get_visible_rooms_x() + topLeft.x / plw &&
           positiveModulo(y - topLeft.py, fx.map.h) < graphics.get_visible_rooms_y() + topLeft.y / plh;
}

bool Client::on_screen(int rx, int ry, double lx, double ly, double fudge) const throw () {
    const int mapw = fx.map.w * plw,
              maph = fx.map.h * plh;
    const double x = rx * plw + lx,
                 y = ry * plh + ly;
    const WorldCoords topLeft = viewTopLeft();
    const double tlx = topLeft.px * plw + topLeft.x - fudge,
                 tly = topLeft.py * plh + topLeft.y - fudge;
    const double relX = positiveFmod(x - tlx, mapw),
                 relY = positiveFmod(y - tly, maph);
    return relX < graphics.get_visible_rooms_x() * plw + 2 * fudge && relY < graphics.get_visible_rooms_y() * plh + 2 * fudge;
}

bool Client::on_screen_exact(int x, int y) const throw () {
    if (replaying)
        return on_screen(x, y);
    else
        return me >= 0 && x == fx.player[me].roomx && y == fx.player[me].roomy;
}

bool Client::on_screen_exact(int rx, int ry, double lx, double ly, double fudge) const throw () {
    if (!on_screen(rx, ry, lx, ly, fudge))
        return false;
    return replaying || me >= 0 && rx == fx.player[me].roomx && ry == fx.player[me].roomy;
}

bool Client::player_on_screen(int pid) const throw () {
    if (!fx.player[pid].used)
        return false;
    else if (fx.player[pid].posUpdated >= fx.frame - 20 || fx.player[pid].dead && fx.player[pid].posUpdated >= 0)
        return on_screen(fx.player[pid].roomx, fx.player[pid].roomy, fx.player[pid].lx, fx.player[pid].ly, Graphics::extended_player_max_size_in_world / 2);
    else
        return false;
}

bool Client::player_on_screen_exact(int pid) const throw () {
    if (!fx.player[pid].used)
        return false;
    else if (replaying)
        return on_screen(fx.player[pid].roomx, fx.player[pid].roomy, fx.player[pid].lx, fx.player[pid].ly, PLAYER_RADIUS);
    else
        return fx.player[pid].onscreen;
}

void Client::draw_map(const VisibilityMap& roomVis) throw () {
    const double time = fd.frame / 10;

    // paint fog of war in all invisible rooms
    for (int ry = 0; ry < fx.map.h; ry++)
        for (int rx = 0; rx < fx.map.w; rx++)
            graphics.draw_minimap_room(fx.map, rx, ry, roomVis[rx][ry] / 255.);

    if (replaying || (graphics.get_visible_rooms_x() > 1 || graphics.get_visible_rooms_y() > 1 || menu.options.graphics.scroll()) && menu.options.graphics.boxRoomsWhenPlaying())
        graphics.highlight_minimap_rooms();

    if (!replaying && menu.options.graphics.oldFlagPositions()) {
        set_trans_mode(disappearedFlagAlpha);
        for (ConstDisappearedFlagIterator fi(this); fi; ++fi)
            if (fx.player[fi->carrier()].alpha != 255)
                graphics.draw_mini_flag(fi.team(), *fi, fx.map, false, true);
        solid_mode();
    }

    // draw all teammates and enemies on screens where there are teammates
    if ((me >= 0 || replaying) && fx.frame >= 0)
        for (int i = 0; i < maxplayers; i++) {
            const ClientPlayer& pl = fx.player[i];
            const WorldCoords pos = playerPos(i);
            if (!pl.used || pos.px >= fx.map.w || pos.py >= fx.map.h || pl.alpha <= 0)
                continue;
            set_trans_mode(pl.alpha);
            for (ConstFlagIterator fi(fx); fi; ++fi)
                if (fi->carrier() == i)
                    graphics.draw_mini_flag(fi.team(), *fi, fx.map);
            const ClientPlayer& exactPl = replaying || fx.player[i].onscreen ? fd.player[i] : pl;
            if (i != me)
                graphics.draw_minimap_player(fx.map, exactPl);
            else // myself: draw differently
                graphics.draw_minimap_me(fx.map, exactPl, time);
            solid_mode();
        }

    // draw the miniflags (in the base and on the ground but not carried)
    for (ConstFlagIterator fi(fx); fi; ++fi)
        if (!fi->carried()) {
            const bool flash = menu.options.graphics.highlightReturnedFlag() &&
                               time < fi->return_time() + 2 && static_cast<int>(fmod(time * 15, 3)) == 0;
            graphics.draw_mini_flag(fi.team(), *fi, fx.map, flash);
        }
}

void Client::draw_playfield() throw () {
    const double time = fd.frame / 10;
    const bool live = !replaying || !replay_paused && !replay_stopped;

    graphics.startPlayfieldDraw();

    // draw dead players
    for (int i = 0; i < maxplayers; i++)
        if (fx.player[i].dead && player_on_screen(i)) {
            const double respawn_delay = i == me ? max(next_respawn_time - get_time(), 0.) : 0.;
            if (fx.player[i].stats().frags() % 100 == 0 && fx.player[i].stats().frags() >= 100)
                graphics.draw_virou_sorvete(playerPos(i), respawn_delay);
            else
                graphics.draw_player_dead(fx.player[i], respawn_delay);
        }

    // draw disappeared flags
    if (!replaying && menu.options.graphics.oldFlagPositions())
        for (ConstDisappearedFlagIterator fi(this); fi; ++fi) {
            const WorldCoords& pos = fi->position();
            if (on_screen(pos.px, pos.py, pos.x, pos.y, Graphics::extended_flag_max_size_in_world / 2))
                graphics.draw_flag(fi.team(), pos, false, 100, false, (fx.frame - fx.player[fi->carrier()].posUpdated) * .1);
        }

    // draw powerups
    for (int i = 0; i < MAX_POWERUPS; i++)
        if (fx.item[i].kind != Powerup::pup_unused && fx.item[i].kind != Powerup::pup_respawning && on_screen_exact(fx.item[i].px, fx.item[i].py, fx.item[i].x, fx.item[i].y, Graphics::extended_item_max_size_in_world / 2))
            graphics.draw_pup(fx.item[i], time, live);

    // draw turbo effects
    graphics.draw_turbofx(time);

    // draw dropped flags (use fx since flags don't move)
    for (ConstFlagIterator fi(fx); fi; ++fi) {
        if (fi->carried())
            continue;
        const WorldCoords& pos = fi->position();
        if (on_screen(pos.px, pos.py, pos.x, pos.y, Graphics::extended_flag_max_size_in_world / 2)) {
            const bool flash = menu.options.graphics.highlightReturnedFlag() &&
                               time < fi->return_time() + 2 && static_cast<int>(fmod(time * 15, 3)) == 0;
            const double return_delay = fi.team() != 2 ? fi->drop_time() + flag_return_delay / 10 - time : 0;
            graphics.draw_flag(fi.team(), pos, flash, 255, menu.options.graphics.emphasizeFlag(visible_rooms), return_delay);
        }
    }

    // draw rockets
    for (int i = 0; i < MAX_ROCKETS; i++)
        if (fx.rock[i].owner != -1 && on_screen(fx.rock[i].px, fx.rock[i].py, fd.rock[i].x, fd.rock[i].y, Graphics::extended_rocket_max_size_in_world / 2)) {
            fd.rock[i].team = fx.rock[i].team;
            fd.rock[i].power = fx.rock[i].power;
            const int radius = fd.rock[i].power ? ROCKET_RADIUS : POWER_ROCKET_RADIUS;
            const bool shadow = fd.rock[i].y + radius + 8 < plh && !fd.map.room[fx.rock[i].px][fx.rock[i].py].fall_on_wall(fd.rock[i].x, fd.rock[i].y + radius + 8, radius / 2);
            graphics.draw_rocket(fd.rock[i], shadow, time);
        }

    // draw players
    for (int k = 0; k < maxplayers; k++) {
        const int i = (me / TSIZE == 0 ? k : maxplayers - k - 1);   // own team first

        if (!player_on_screen(i))
            continue;
        if (!replaying && !fx.player[i].onscreen && fx.player[i].roomx == fx.player[me].roomx && fx.player[i].roomy == fx.player[me].roomy)
            continue; // don't draw players whose last known location is in this room but who aren't really here

        //HACK REMENDEX: predict item_shadow
        if (player_on_screen_exact(i) && fx.player[i].item_shadow()) {
            const int hspd = static_cast<int>((fd.frame - fx.frame) * 10.);
            fd.player[i].visibility = fx.player[i].visibility - hspd;
            const int limit = (fx.player[i].visibility >= 7) ? 7 : 0;   // this produces an error of at most one server frame if total invisibility is enabled
            if (fd.player[i].visibility < limit)
                fd.player[i].visibility = limit;
        }

        if (i != me && !fx.player[i].dead)
            draw_player(i, time, live);
    }
    // last draw me
    if (me != -1) {
        if (fx.player[me].dead)
            deadAfterHighlighted = true;
        else {
            draw_player(me, time, live);
            static double spawnTime = 0;
            if (deadAfterHighlighted) {
                deadAfterHighlighted = false;
                spawnTime = time;
            }
            static const double highlightTime = .5;
            if (menu.options.graphics.spawnHighlight() && time - spawnTime < highlightTime)
                graphics.draw_me_highlight(playerPos(me), 1. - (time - spawnTime) / highlightTime);
            if (fx.physics.allowFreeTurning && menu.options.controls.aimMode() != Menu_controls::AM_8way && !replaying)
                graphics.draw_aim(fx.map.room[fx.player[me].roomx][fx.player[me].roomy], playerPos(me), gunDir, me / TSIZE);
        }
    }

    for (int i = 0; i < maxplayers; i++)
        if (player_on_screen_exact(i) && fx.player[i].item_deathbringer)
            graphics.draw_deathbringer_carrier_effect(playerPos(i), calculatePlayerAlpha(i));

    graphics.draw_effects(time);

    fx.cleanOldDeathbringerExplosions();
    #ifdef DEATHBRINGER_SPEED_TEST
    DeathbringerExplosion dbe(0, WorldCoords(0, 0, 0, 0), 0);
    for (int i = 0; i < 50; ++i)
        graphics.draw_deathbringer(dbe, 14);
    #else
    for (list<DeathbringerExplosion>::const_iterator dbi = fx.deathbringerExplosions().begin(); dbi != fx.deathbringerExplosions().end(); ++dbi)
        graphics.draw_deathbringer(*dbi, fd.frame);
    #endif

    if (menu.options.graphics.showNeighborMarkers(replaying, visible_rooms)) {
        // neighbor markers: disappeared flags first
        set_trans_mode(disappearedFlagAlpha);
        for (ConstDisappearedFlagIterator fi(this); fi; ++fi)
            graphics.draw_neighbor_marker(true, fi->position(), fi.team(), true);
        solid_mode();
        // players
        for (int i = 0; i < maxplayers; ++i) {
            const ClientPlayer& pl = fx.player[i];
            if (!pl.used || pl.alpha <= 0)
                continue;
            set_trans_mode(pl.alpha);
            graphics.draw_neighbor_marker(false, playerPos(i), pl.team());
        }
        solid_mode();
        // flags
        for (ConstFlagIterator fi(fx); fi; ++fi) {
            if (fi->carried()) {
                if (fx.player[fi->carrier()].alpha <= 0)
                    continue;
                set_trans_mode(fx.player[fi->carrier()].alpha);
            }
            else
                solid_mode();
            graphics.draw_neighbor_marker(true, fi->position(), fi.team());
        }
        solid_mode();
    }

    if (menu.options.graphics.showName(true))
        for (int i = 0; i < maxplayers; i++) {
            if (fx.player[i].dead || !player_on_screen(i))
                continue;
            if (!replaying && !fx.player[i].onscreen && fx.player[i].roomx == fx.player[me].roomx && fx.player[i].roomy == fx.player[me].roomy)
                continue; // don't draw players whose last known location is in this room but who aren't really here
            bool visible;
            if (replaying)
                visible = true;
            else if (fx.player[i].onscreen)
                visible = fx.player[i].visibility >= 200 || i / TSIZE == me / TSIZE;
            else
                visible = fx.player[i].alpha >= 200 && menu.options.graphics.showName(false);
            if (visible)
                graphics.draw_player_name(fx.player[i].name, playerPos(i), i / TSIZE, i == me);
        }

    graphics.endPlayfieldDraw();
}

Client::VisibilityMap Client::calculateVisibilities() throw () {
    const double time = fd.frame / 10;

    VisibilityMap roomVis(fx.map.w);
    uint8_t initVal = (replaying || me >= 0 && fx.player[me].item_shadow_time > time) ? 255 : 0;
    for (int x = 0; x < fx.map.w; ++x)
        roomVis[x].resize(fx.map.h, initVal);

    if (me < 0 && !replaying || fx.frame < 0)
        return roomVis;

    int max_time, start_fadeout;    // in frames
    switch (menu.options.graphics.minimapPlayers()) {
    /*break;*/ case Menu_graphics::MP_EarlyCut: max_time =     start_fadeout = 12;
        break; case Menu_graphics::MP_LateCut:  max_time =     start_fadeout = 20;
        break; case Menu_graphics::MP_Fade:     max_time = 20; start_fadeout = 10;
        break; default: nAssert(0); max_time = start_fadeout = 0;
    }
    for (int i = 0; i < maxplayers; i++) {
        ClientPlayer& pl = fx.player[i];
        if (pl.used && (!pl.dead && pl.posUpdated > fx.frame - max_time || i == me) && pl.roomx < fx.map.w && pl.roomy < fx.map.h) {
            if (fx.frame > pl.posUpdated + start_fadeout && i != me)
                pl.alpha = 255 - static_cast<int>((fx.frame - pl.posUpdated - start_fadeout) * 255 / (max_time - start_fadeout));
            else
                pl.alpha = 255;
            if (roomVis[pl.roomx][pl.roomy] < pl.alpha)
                roomVis[pl.roomx][pl.roomy] = pl.alpha;
        }
        else
            pl.alpha = 0;
    }
    return roomVis;
}

int Client::calculatePlayerAlpha(int pid) const throw () {
    static const int min_alpha_friends = 128;
    const int baseAlpha = fd.player[pid].visibility;
    if ((replaying || fx.player[pid].team() == fx.player[me].team()) && baseAlpha < min_alpha_friends)
        return min_alpha_friends;
    else
        return baseAlpha;
}

void Client::draw_player(int pid, double time, bool live) throw () {
    ClientPlayer& player = fx.player[pid];
    const bool fullyVisible = player_on_screen_exact(pid);
    const int alpha = fullyVisible ? calculatePlayerAlpha(pid) : fx.player[pid].alpha * 2 / 3;
    const int flagAlpha = fullyVisible ? 255 : alpha;
    const WorldCoords pos = playerPos(pid);
    // draw flag if player is carrier of a flag
    for (ConstFlagIterator fi(fx); fi; ++fi)
        if (fi->carrier() == pid) {
            graphics.draw_flag(fi.team(), pos, false, flagAlpha, menu.options.graphics.emphasizeFlag(visible_rooms));
            break;
        }
    if (!fullyVisible) {
        graphics.draw_player(pos, player.team(), player.color(), player.gundir, 0, false, alpha, time);
        return;
    }

    // turbo effect
    if (player.item_turbo && player.sx * player.sx + player.sy * player.sy > fx.physics.max_run_speed * fx.physics.max_run_speed &&
        time > player.next_turbo_effect_time) {
        player.next_turbo_effect_time = time + 0.05;
        graphics.create_turbofx(pos, player.team(), player.color(), player.gundir, alpha, time);
    }

    //draw player
    graphics.draw_player(pos, player.team(), player.color(), player.gundir, player.hitfx, player.item_power, alpha, time);

    //draw deathbringer carrier effect
    if (player.item_deathbringer && time > player.next_smoke_effect_time) {
        player.next_smoke_effect_time = time + 0.01;
        for (int i = 0; i < 2; i++)
            graphics.create_deathcarrier(pos, alpha, time);
    }
    // draw deathbringer affected effect
    if (player.deathbringer_affected)
        graphics.draw_deathbringer_affected(pos, player.team(), alpha);
    // shield
    if (player.item_shield)
        graphics.draw_shield(pos, PLAYER_RADIUS + SHIELD_RADIUS_ADD, live, alpha, player.team(), player.gundir);
}

class MapListSorter { // helper for draw_game_menu
public:
    MapListSorter(MapListSortKey key_) throw () : key(key_) { }
    bool operator()(const std::pair<const MapInfo*, int>& m1, const std::pair<const MapInfo*, int>& m2) const throw ();

private:
    MapListSortKey key;
};

bool MapListSorter::operator()(const pair<const MapInfo*, int>& m1, const pair<const MapInfo*, int>& m2) const throw () {
    const MapInfo& m1mi = *m1.first, &m2mi = *m2.first;
    switch (key) {
    /*break;*/ case MLSK_Votes: return m1mi.votes > m2mi.votes; // reverse: get high vote counts first
        break; case MLSK_Title: return cmp_case_ins(m1mi.title, m2mi.title);
        break; case MLSK_Size: {
            const int m1s = m1mi.width * m1mi.height;
            const int m2s = m2mi.width * m2mi.height;
            return m1s < m2s || m1s == m2s && m1mi.width < m2mi.width;
        }
        break; case MLSK_Author:   return cmp_case_ins(m1mi.author, m2mi.author);
        break; case MLSK_Favorite: return m1mi.highlight && !m2mi.highlight; // highlighted first
        break; default: nAssert(0);
    }
}

//draws the game menu
void Client::draw_game_menu() throw () {
    switch (menusel) {
    /*break;*/ case menu_maps: {
            Lock ml(mapInfoMutex);
            if (mapListChangedAfterSort) {
                mapListChangedAfterSort = false;
                sortedMaps.clear();
                sortedMaps.reserve(maps.size());
                for (unsigned mi = 0; mi < maps.size(); ++mi)
                    sortedMaps.push_back(pair<MapInfo*, int>(&maps[mi], mi));
                if (mapListSortKey != MLSK_Number) {
                    MapListSorter sorter(mapListSortKey);
                    stable_sort(sortedMaps.begin(), sortedMaps.end(), sorter);
                }
            }
            graphics.map_list(sortedMaps, mapListSortKey, current_map, map_vote, edit_map_vote);
        }
        break; case menu_players:
            graphics.draw_statistics(players_sb, player_stats_page, static_cast<int>(fx.frame / 10), maxplayers, max_world_rank);
        break; case menu_teams:
            graphics.team_statistics(fx.teams);
        break; case menu_none: // regular menus are drawn below, regardless of menusel
        break; default:
            numAssert(0, menusel);
    }
    if (!openMenus.empty())
        openMenus.draw(graphics.drawbuffer(), graphics.colours());
}

void Client::initMenus() throw () {
    typedef MenuCallback<Client> MCB;
    typedef MenuKeyCallback<Client> MKC;
    menu.connect.addHooks(new MCB::A<Textarea, &Client::MCF_connect>(this),
                          new MKC::A<Textarea, &Client::MCF_addRemoveServer>(this));

    menu.initialize(new MCB::A<Menu, &Client::MCF_menuOpener>(this), settings);

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

    menu.options.player.menu        .setOpenHook(new MCB::N<Menu,           &Client::MCF_preparePlayerMenu      >(this));
    menu.options.player.menu        .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareDrawPlayerMenu  >(this));
    menu.options.player.menu       .setCloseHook(new MCB::N<Menu,           &Client::MCF_playerMenuClose        >(this));
    menu.options.player.name            .setHook(new MCB::N<Textfield,      &Client::MCF_nameChange             >(this));
    menu.options.player.randomName      .setHook(new MCB::N<Textarea,       &Client::MCF_randomName             >(this));
    menu.options.player.favoriteColors  .setHook(new MCB::N<Colorselect,    &Client::sendFavoriteColors         >(this));
    menu.options.player.removePasswords .setHook(new MCB::N<Textarea,       &Client::MCF_removePasswords        >(this));

    menu.options.game.menu          .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareGameMenu        >(this));
    menu.options.game.minimapBandwidth  .setHook(new MCB::N<Slider,         &Client::sendMinimapBandwidth       >(this));
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

    menu.options.theme.menu          .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareGfxThemeMenu     >(this));
    menu.options.theme.theme             .setHook(new MCB::N<Select<string>, &Client::MCF_gfxThemeChange          >(this));
    menu.options.theme.useThemeBackground.setHook(new MCB::N<Checkbox,       &Client::MCF_gfxThemeChange          >(this));
    menu.options.theme.background        .setHook(new MCB::N<Select<string>, &Client::MCF_gfxThemeChange          >(this));
    menu.options.theme.colours           .setHook(new MCB::N<Select<string>, &Client::MCF_gfxThemeChange          >(this));
    menu.options.theme.useThemeColours   .setHook(new MCB::N<Checkbox,       &Client::MCF_gfxThemeChange          >(this));
    menu.options.theme.font              .setHook(new MCB::N<Select<string>, &Client::MCF_fontChange              >(this));

    menu.options.graphics.visibleRoomsPlay  .setHook(new MCB::N<Slider,         &Client::MCF_visibleRoomsPlayChange  >(this));
    menu.options.graphics.visibleRoomsReplay.setHook(new MCB::N<Slider,         &Client::MCF_visibleRoomsReplayChange>(this));
    menu.options.graphics.antialiasing      .setHook(new MCB::N<Checkbox,       &Client::MCF_antialiasChange         >(this));
    menu.options.graphics.minTransp         .setHook(new MCB::N<Checkbox,       &Client::MCF_transpChange            >(this));
    menu.options.graphics.statsBgAlpha      .setHook(new MCB::N<Slider,         &Client::MCF_statsBgChange           >(this));

    menu.options.sounds.menu        .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareSndMenu         >(this));
    menu.options.sounds.enabled         .setHook(new MCB::N<Checkbox,       &Client::MCF_sndEnableChange        >(this));
    menu.options.sounds.volume          .setHook(new MCB::N<Slider,         &Client::MCF_sndVolumeChange        >(this));
    menu.options.sounds.theme           .setHook(new MCB::N<Select<string>, &Client::MCF_sndThemeChange         >(this));

    menu.options.language.menu      .setOpenHook(new MCB::N<Menu,           &Client::MCF_refreshLanguages       >(this));

    menu.options.bugReports.menu   .setCloseHook(new MCB::N<Menu,           &Client::MCF_acceptBugReporting     >(this));   // save instantly because it has its own file
    menu.options.bugReports.menu      .setOkHook(new MCB::N<Menu,           &Client::MCF_menuCloser             >(this));

    menu.ownServer.menu             .setDrawHook(new MCB::N<Menu,           &Client::MCF_prepareOwnServerMenu   >(this));
    menu.ownServer.start                .setHook(new MCB::N<Textarea,       &Client::MCF_startServer            >(this));
    menu.ownServer.play                 .setHook(new MCB::N<Textarea,       &Client::MCF_playServer             >(this));
    menu.ownServer.stop                 .setHook(new MCB::N<Textarea,       &Client::MCF_stopServer             >(this));

    menu.replays.menu               .setOpenHook(new MCB::N<Menu,           &Client::MCF_prepareReplayMenu      >(this));

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

    menu.options.screenMode.init(graphics);
    menu.options.theme.init(graphics);
    menu.options.sounds.init(client_sounds);
    menu.ownServer.init(serverExtConfig.ipAddress);
}

void Client::MCF_menuOpener(Menu& menu) throw () {
    openMenus.open(&menu);
}

void Client::MCF_menuCloser() throw () {
    openMenus.close();
}

void Client::MCF_prepareMainMenu() throw () {
    menu.ownServer.refreshCaption(listenServer.running());
    menu.disconnect.setEnable(connected || spectating);
}

void Client::MCF_disconnect() throw () {
    disconnect_command();
    stop_replay();
}

void Client::MCF_exitOutgun() throw () {
    quitCommand = true;
}

void Client::MCF_cancelConnect() throw () {
    if (!connected)
        disconnect_command();   // will cancel the (probably) ongoing connect attempt
}

void Client::MCF_preparePlayerMenu() throw () {
    menu.options.player.name.set(playername);
    menu.options.player.favoriteColors.setGraphicsCallBack(graphics);
}

void Client::MCF_prepareDrawPlayerMenu() throw () {
    menu.options.player.namestatus.set(tournamentPassword.statusAsString());
}

void Client::MCF_playerMenuClose() throw () {
    change_name_command();
    send_tournament_participation();
}

void Client::MCF_nameChange() throw () { // only function to clear the password
    menu.options.player.password.set("");
    tournamentPassword.changeData(playername, "");
}

void Client::MCF_randomName() throw () {
    const string name = language.code() == "fi" ? finnish_name(maxPlayerNameLength) : RandomName();
    menu.options.player.name.set(name);
    MCF_nameChange();
}

void Client::MCF_removePasswords() throw () {
    const int removed = remove_player_passwords(menu.options.player.name());
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

void Client::MCF_prepareGameMenu() throw () {
}

void Client::MCF_prepareControlsMenu() throw () {
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
    if (firePressed())
        active += _("shoot")  + ' ';
    menu.options.controls.activeControls.set(active);
    active.clear();
    if (menu.options.controls.joystick()) {
        for (int button = 1; button <= 16; ++button)
            if (readJoystickButton(button))
                active += itoa(button) + ' ';
    }
    menu.options.controls.activeJoystick.set(active);
    active.clear();
    for (int button = 1; button <= 16; ++button)
        if (mouse_b & (1 << button - 1))
            active += itoa(button) + ' ';
    menu.options.controls.activeMouse.set(active);
}

void Client::MCF_keyboardLayout() throw () {
    const string cfg = string("[system]\nkeyboard=") + menu.options.controls.keyboardLayout() + '\n';
    remove_keyboard();
    override_config_data(cfg.data(), cfg.length());
    install_keyboard();
}

void Client::MCF_joystick() throw () {
    if (menu.options.controls.joystick())
        install_joystick(JOY_TYPE_AUTODETECT);
    else
        remove_joystick();
}

void Client::MCF_messageLogging() throw () {
    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        openMessageLog();
    else
        closeMessageLog();
}

void Client::MCF_prepareScrModeMenu() throw () {
    menu.options.screenMode.update(graphics);
}

void Client::MCF_prepareDrawScrModeMenu() throw () {
    menu.options.screenMode.flipping.setEnable(!menu.options.screenMode.windowed());
    menu.options.screenMode.alternativeFlipping.setEnable(!menu.options.screenMode.windowed() && menu.options.screenMode.flipping());
}

void Client::MCF_prepareGfxThemeMenu() throw () {
    menu.options.theme.update(graphics);
}

void Client::MCF_gfxThemeChange() throw () {
    graphics.select_theme(menu.options.theme.theme(),
                          menu.options.theme.background(), menu.options.theme.useThemeBackground(),
                          menu.options.theme.colours(), menu.options.theme.useThemeColours());
}

void Client::MCF_fontChange() throw () {
    graphics.select_font(menu.options.theme.font());
}

void Client::MCF_screenDepthChange() throw () {
    menu.options.screenMode.update(graphics);  // fetch resolutions according to the new depth
}

void Client::MCF_screenModeChange() throw () {   // used to lose the return value
    const bool ret = screenModeChange();
    nAssert(ret); // it should return true unless it's out of memory, because this function is only used when there is a working mode to revert to
}

bool Client::screenModeChange() throw () {   // returns true whenever Graphics is usable (even when reverted back to current (workingGfxMode) mode)
    if (!menu.options.screenMode.newMode())
        return true;

    const ScreenMode res = menu.options.screenMode.resolution();
    const int depth = menu.options.screenMode.colorDepth();

    Checkbox& win  = menu.options.screenMode.windowed;
    Checkbox& flip = menu.options.screenMode.flipping;
    const bool owin = win(), oflip = flip();

    for (int nTry = 0;; ++nTry) {
        if (graphics.init(res.width, res.height, depth, win(), flip())) {
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
                menu.options.screenMode.update(graphics);  // fetch resolutions according to the new depth
                menu.options.screenMode.resolution.set(ScreenMode(wm.width, wm.height));  // ignore potential error here; we couldn't do anything about it anyway
                win.set(wm.windowed);
                flip.set(wm.flipping);
                return graphics.init(wm.width, wm.height, wm.depth, wm.windowed, wm.flipping);
            }
            return false;
        }
    }
    workingGfxMode = GFXMode(res.width, res.height, depth, win(), flip());
    const int rate = get_refresh_rate();
    ostringstream ost;
    if (rate == 0)
        ost << _("unknown");
    else
        ost << _("$1 Hz", itoa(rate));
    menu.options.screenMode.refreshRate.set(ost.str());
    return true;
}

void Client::MCF_visibleRoomsPlayChange() throw () {
    if (!replaying) {
        visible_rooms = menu.options.graphics.visibleRoomsPlay();
        if (visible_rooms > fx.map.w && visible_rooms > fx.map.h && !menu.options.graphics.repeatMap())
            visible_rooms = max(fx.map.w, fx.map.h);
    }
}

void Client::MCF_visibleRoomsReplayChange() throw () {
    if (replaying) {
        visible_rooms = menu.options.graphics.visibleRoomsReplay();
        if (visible_rooms > fx.map.w && visible_rooms > fx.map.h && !menu.options.graphics.repeatMap())
            visible_rooms = max(fx.map.w, fx.map.h);
    }
}

void Client::MCF_antialiasChange() throw () {
    graphics.set_antialiasing(menu.options.graphics.antialiasing());
}

void Client::MCF_transpChange() throw () {
    graphics.set_min_transp(menu.options.graphics.minTransp());
}

void Client::MCF_statsBgChange() throw () {
    graphics.set_stats_alpha(menu.options.graphics.statsBgAlpha());
}

void Client::MCF_prepareSndMenu() throw () {
    menu.options.sounds.update(client_sounds);
}

void Client::MCF_sndEnableChange() throw () {
    client_sounds.setEnable(menu.options.sounds.enabled());
}

void Client::MCF_sndVolumeChange() throw () {
    client_sounds.setVolume(menu.options.sounds.volume());
    client_sounds.play(SAMPLE_POWER_FIRE, 1000);
}

void Client::MCF_sndThemeChange() throw () {
    client_sounds.select_theme(menu.options.sounds.theme());
}

void Client::MCF_refreshLanguages() throw () {
    const int menu_selection_index = refreshLanguages(menu.options.language);
    typedef MenuCallback<Client> MCB;
    menu.options.language.addHooks(new MCB::A<Textarea, &Client::MCF_acceptLanguage>(this));
    menu.options.language.menu.setSelection(menu_selection_index);
}

// Helper to refreshLanguages. Each pair consists of language code and name, in that order.
bool translationSort(const pair<string, string>& t1, const pair<string, string>& t2) throw () {
    // don't care about the language code (it isn't visible anyway), and use a case insensitive order
    return platStricmp(t1.second.c_str(), t2.second.c_str()) < 0;
}

int Client::refreshLanguages(Menu_language& lang_menu) throw () {
    lang_menu.reset();

    // search the languages directory for translations to add
    vector< pair<string, string> > translations;
    translations.push_back(pair<string, string>("en", "English"));   // global default when there's nothing in language.txt
    FileFinder* languageFiles = platMakeFileFinder(wheregamedir + "languages", ".txt", false);
    while (languageFiles->hasNext()) {
        string code = FileName(languageFiles->next()).getBaseName();
        if (code.find('.') != string::npos || code == "en")   // skip help.language.txt and possible similar files, and of course English which was added first
            continue;
        // fetch language name
        const string langFile = wheregamedir + "languages" + directory_separator + code + ".txt";
        ifstream in(langFile.c_str());
        string name;
        if (getline_skip_comments(in, name))
            translations.push_back(pair<string, string>(code, name));
        else
            log.error(_("Translation $1 can't be read.", langFile));
    }
    delete languageFiles;

    // fetch the currently chosen language from language.txt (because after changing it can be different from the loaded language)
    string lang;
    ifstream langConfig((wheregamedir + "config" + directory_separator + "language.txt").c_str());
    if (!getline_skip_comments(langConfig, lang))
        lang = "en";
    langConfig.close();

    // add found languages to options
    sort(translations.begin(), translations.end(), translationSort);
    int i = lang_menu.menu.selection();
    int current_lang_index = i;
    for (vector< pair<string, string> >::const_iterator ti = translations.begin(); ti != translations.end(); ++ti, ++i) {
        lang_menu.add(ti->first, ti->second);
        if (ti->first == lang)
            current_lang_index = i;
    }
    return current_lang_index;
}

void Client::MCF_acceptLanguage(Textarea& target) throw () {
    const string lang_code = menu.options.language.getCode(target);
    MCF_menuCloser();
    acceptLanguage(lang_code, true);
}

void Client::MCF_acceptInitialLanguage(Textarea& target) throw () {
    const string lang_code = m_initialLanguage.getCode(target);
    MCF_menuCloser();
    acceptLanguage(lang_code, false);
}

void Client::acceptLanguage(const string& lang, bool restart_message) throw () {
    Language newLang;
    if (!newLang.load(lang, log))
        return; // load already logs an error message
    ofstream langConfig((wheregamedir + "config" + directory_separator + "language.txt").c_str());
    if (langConfig) {
        langConfig << lang << '\n';
        langConfig.close();
        if (lang != language.code() && restart_message) {  // what is currently loaded; what was previously in language.txt has no significance
            m_dialog.clear();
            m_dialog.wrapLine(newLang.get_text("Please close and restart Outgun to complete the change of language."));
            showMenu(m_dialog);
        }
    }
    else
        log.error(_("config/language.txt can't be written."));
}

void Client::MCF_acceptBugReporting() throw () {
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

void Client::MCF_playerPasswordAccept() throw () {
    if (m_playerPassword.password().empty()) // if no password is needed, we're never asked for one (even if we gave a wrong one)
        return;
    openMenus.close(&m_playerPassword.menu);
    if (m_playerPassword.save())
        save_player_password(playername, serverIP.toString(), m_playerPassword.password());
    if (connected)
        issue_change_name_command();
    else
        connect_command(false);
}

void Client::MCF_serverPasswordAccept() throw () {
    openMenus.close(&m_serverPassword.menu);
    nAssert(!connected);
    connect_command(false);
}

void Client::MCF_clearErrors() throw () {
    openMenus.close(&m_errors.menu);
    m_errors.clear();
}

void Client::MCF_prepareServerMenu() throw () {
    const int oldSel = menu.connect.menu.selection();

    menu.connect.reset();
    vector<Network::Address> addresses;
    const vector<ServerListEntry>& servers = (menu.connect.favorites() ? gamespy : mgamespy);
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

void Client::MCF_prepareAddServer() throw () {
    menu.connect.addServer.save.set(menu.connect.favorites());
    menu.connect.addServer.address.set("");
}

void Client::MCF_addServer() throw () {
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

bool Client::MCF_addressEntryKeyHandler(char scan, unsigned char chr) throw () {
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

bool Client::MCF_addRemoveServer(Textarea& target, char scan, unsigned char chr) throw () {
    (void)chr;
    if (scan == KEY_DEL) {
        vector<ServerListEntry>& servers = (menu.connect.favorites() ? gamespy : mgamespy);
        const Network::Address address = menu.connect.getAddress(target);
        for (vector<ServerListEntry>::iterator spy = servers.begin(); spy != servers.end(); ++spy)
            if (address == spy->address()) {
                servers.erase(spy);
                break;
            }
        return true;
    }
    else if (scan == KEY_INSERT && !menu.connect.favorites()) {
        const Network::Address address = menu.connect.getAddress(target);
        for (vector<ServerListEntry>::const_iterator spy = mgamespy.begin(); spy != mgamespy.end(); ++spy)
            if (address == spy->address()) {
                gamespy.push_back(*spy);
                break;
            }
        return true;
    }
    return false;
}

void Client::MCF_connect(Textarea& target) throw () {
    serverIP = menu.connect.getAddress(target);
    m_serverPassword.password.set("");
    connect_command(true);
}

void Client::MCF_updateServers() throw () {
    if (refreshStatus == RS_none || refreshStatus == RS_failed) {
        refreshStatus = RS_running;
        Thread::startDetachedThread_assert("Client::getServerListThread",
                                           RedirectToMemFun0<Client, void>(this, &Client::getServerListThread),
                                           extConfig.lowerPriority);
    }
}

void Client::MCF_refreshServers() throw () {
    if (refreshStatus == RS_none || refreshStatus == RS_failed) {
        refreshStatus = RS_running;
        Thread::startDetachedThread_assert("Client::refreshThread",
                                           RedirectToMemFun0<Client, void>(this, &Client::refreshThread),
                                           extConfig.lowerPriority);
    }
}

void Client::MCF_prepareOwnServerMenu() throw () {
    menu.ownServer.refreshCaption(listenServer.running());
    menu.ownServer.refreshEnables(listenServer.running(), connected);
}

void Client::MCF_startServer() throw () {
    if (!listenServer.running()) {
        serverExtConfig.privateserver = menu.ownServer.pub() ? 0 : 1;
        serverExtConfig.port = menu.ownServer.port();
        serverExtConfig.ipAddress = menu.ownServer.address();
        serverExtConfig.privSettingForced = serverExtConfig.portForced = serverExtConfig.ipForced = true;
        serverExtConfig.showErrorCount = false;
        listenServer.start(serverExtConfig.port, serverExtConfig);
    }
}

void Client::MCF_playServer() throw () {
    if (listenServer.running()) {
        serverIP.fromValidIP("127.0.0.1");
        serverIP.setPort(listenServer.port());
        openMenus.clear();
        m_serverPassword.password.set("");
        connect_command(true);
    }
}

void Client::MCF_stopServer() throw () {
    if (listenServer.running())
        listenServer.stop();
}

void Client::MCF_replay(Textarea& target) throw () {
    const string& replay_name = menu.replays.getFile(target);
    const string filename = wheregamedir + "replay" + directory_separator + replay_name + ".replay";
    start_replay(filename);
}

void Client::MCF_prepareReplayMenu() throw () {
    menu.replays.reset();
    vector<pair<string, string> > replays;
    FileFinder* replay_files = platMakeFileFinder(wheregamedir + "replay", ".replay", false);
    while (replay_files->hasNext()) {
        const string name = FileName(replay_files->next()).getBaseName();
        const string replay_file = wheregamedir + "replay" + directory_separator + name + ".replay";
        ifstream in(replay_file.c_str(), ios::binary);
        BinaryStreamReader read(in);
        try {
            if (read.constLengthStr(REPLAY_IDENTIFICATION.length()) != REPLAY_IDENTIFICATION) {
                log.error(_("$1 is not an Outgun replay.", replay_file));
                continue;
            }
            const unsigned replay_version = read.U32();
            if (replay_version > REPLAY_VERSION) {
                log.error(_("Replay $1 is a newer version ($2).", replay_file, itoa(replay_version)));
                continue;
            }
            const unsigned length = read.U32();
            const string server_name = read.str();
            read.U32(); // skip maxplayers
            const string map_name = read.str();

            ostringstream text;
            text << name << ' ' << server_name << " - " << map_name;
            if (length > 0)
                text << ' ' << length / 600 << ':' << setw(2) << setfill('0') << length / 10 % 60;
            replays.push_back(pair<string, string>(name, text.str()));
        } catch (BinaryReader::ReadOutside) {
            log("Replay file %s is invalid.", replay_file.c_str());
        }
    }
    delete replay_files;
    log("%lu replays found.", static_cast<long unsigned>(replays.size()));

    sort(replays.begin(), replays.end());
    for (vector<pair<string, string> >::reverse_iterator ri = replays.rbegin(); ri != replays.rend(); ++ri) // const_reverse_iterator does not work in GCC 3.4.2
        menu.replays.add(ri->first, ri->second);

    typedef MenuCallback<Client> MCB;
    typedef MenuKeyCallback<Client> MKC;
    menu.replays.addHooks(new MCB::A<Textarea, &Client::MCF_replay>(this));
}

void Client::load_highlight_texts() throw () {
    highlight_text.clear();
    const string configFile = wheregamedir + "config" + directory_separator + "texts.txt";
    ifstream in(configFile.c_str());
    string line;
    while (getline_skip_comments(in, line))
        highlight_text.push_back(toupper(trim(line)));
}

void Client::load_fav_maps() throw () {
    fav_maps.clear();
    const string configFile = wheregamedir + "config" + directory_separator + "maps.txt";
    ifstream in(configFile.c_str());
    string line;
    while (getline_skip_comments(in, line))
        fav_maps.insert(toupper(trim(line)));
}

void Client::apply_fav_maps() throw () {
    for (vector<MapInfo>::iterator mi = maps.begin(); mi != maps.end(); ++mi)
        mi->highlight = !!fav_maps.count(toupper(mi->title));
    mapListChangedAfterSort = true;
}

void Client::loadHelp() throw () {
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

void Client::addSplashLine(string line) throw () { // internal to loadSplashScreen
    replace_all_in_place(line, "@VERSION@", getVersionString());
    replace_all_in_place(line, "@YEAR@", GAME_COPYRIGHT_YEAR);
    menu.options.bugReports.addLine(line);
}

void Client::loadSplashScreen() throw () {
    menu.options.bugReports.clear();
    const string splashFile = wheregamedir + "languages" + directory_separator + "splash." + language.code() + ".txt";
    ifstream in(splashFile.c_str());
    if (in) {
        string line;
        while (getline_smart(in, line))
            addSplashLine(line);
    }
    else {
        static const char* msg[] = {
            "Outgun @VERSION@, copyright © 2002-@YEAR@ multiple authors.",
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
            addSplashLine(*line);
    }
}

void Client::openMessageLog() throw () {
    if (!messageLogOpen) {
        message_log.clear();    // necessary: http://gcc.gnu.org/onlinedocs/libstdc++/faq/index.html#4_4_iostreamclear
        message_log.open((wheregamedir + "log" + directory_separator + "message.log").c_str(), ios::app);
        messageLogOpen = true;
    }
}

void Client::closeMessageLog() throw () {
    if (messageLogOpen) {
        message_log.close();
        messageLogOpen = false;
    }
}

void Client::CB_tournamentToken(string token) throw () { // callback called by tournamentPassword from another thread
    if (connected) {
        BinaryBuffer<256> msg;
        msg.U8(data_registration_token);
        msg.str(token);
        client->send_message(msg);
        tournamentPassword.serverProcessingToken();
    }
}
#endif

void Client::cfunc_connection_update(void* customp, int connect_result, ConstDataBlockRef data) throw () {
    Client* cl = static_cast<Client*>(customp);
    cl->connection_update(connect_result, data);
}

void Client::connection_update(int connect_result, ConstDataBlockRef data) throw () {
    addThreadMessage(new TM_ConnectionUpdate(connect_result, data));
}

void Client::cfunc_server_data(void* customp, ConstDataBlockRef data) throw () {
    Client* cl = static_cast<Client*>(customp);
    cl->process_incoming_data(data);
}

ClientInterface* ClientInterface::newClient(const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, Log& clientLog, MemoryLog& externalErrorLog_) throw () {
    return new Client(config, serverConfig, clientLog, externalErrorLog_);
}
