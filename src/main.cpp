/*
 *  main.cpp
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

#include <fstream>
#include <sstream>
#include <string>

#include "incalleg.h"
#include "allegro_icon.h"
#include "debug.h"
#include "debugconfig.h"    // some globals are set from commandline arguments
#include "gameserver_interface.h"
#include "log.h"
#include "language.h"
#include "network.h"
#include "platform.h"
#include "timer.h"
#include "utility.h"

#ifndef DEDICATED_SERVER_ONLY
#include "client.h"
#include "mappic.h"
#endif

using std::ifstream;
using std::ostringstream;
using std::string;

#ifndef DEDICATED_SERVER_ONLY

bool set_shitty_mode(LogSet log) {
    int DTC = desktop_color_depth();

    if (DTC == 0)   // no windowing supported
        DTC = 8;    // try something anyway (with 0, set_color_depth chokes)

    set_color_depth(DTC);

    if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0))
        log("Could not set gfx mode 320×240 windowed. Try 1 with %i.", DTC);
    else
        return true;

    if (DTC == 16 || DTC == 15) {
        if (DTC == 15)
            DTC = 16;
        else
            DTC = 15;

        set_color_depth(DTC);

        if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0))
            log("Could not set gfx mode 320×240 windowed. Try 2 with %i.", DTC);
        else
            return true;
    }

    // WARNING: this can be buggy for multiple dedicated servers.
    DTC = 8;

    set_color_depth(DTC);

    if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0))
        log("Could not set gfx mode 320×240 windowed. Tried with %i.", DTC);
    else
        return true;

    // try safe mode
    if (set_gfx_mode(GFX_SAFE, 320, 240, 0, 0)) {
        log("Could not set a safe gfx mode.");
        return false;
    }
    else
        return true;
}

#endif

// Make directory if it does not already exist.
bool check_dir(const string& dir, LogSet& log) {
    const string directory = wheregamedir + dir;

    if (platIsDirectory(directory) || !platMkdir(directory.c_str()))
        return true;
    log.error(_("The directory '$1' was not found and could not be created.", directory));
    return false;
}

#ifndef DEDICATED_SERVER_ONLY

class GlobalCloseButtonHook {
    static volatile bool flag;
    friend void GlobalCloseButtonHook__closeCallback();

public:
    static void install() {
        LOCK_VARIABLE(flag);
        LOCK_FUNCTION(GlobalCloseButtonHook__closeCallback);
        set_close_button_callback(GlobalCloseButtonHook__closeCallback);
    }
    static volatile bool* flagPtr() { return &flag; }
};

volatile bool GlobalCloseButtonHook::flag = false;

void GlobalCloseButtonHook__closeCallback() {
    GlobalCloseButtonHook::flag = true;
} END_OF_FUNCTION(GlobalCloseButtonHook__closeCallback)

#endif

#ifdef DEDICATED_SERVER_ONLY

void statusOutputText(const string& str) {
    std::cout << str << '\n';
}

#else

void statusOutputWindow(const string& str) {
    set_window_title(str.c_str());
}

void statusOutputText(const string& str) {
    #ifndef ALLEGRO_WINDOWS
    std::cout << str << '\n';
    #else
    statusOutputWindow(str);
    #endif
}

#endif

void innerMain(int argc, const char* argv[], LogSet& log, MemoryLog& memoryErrorLog);

int wrappedMain(int argc, const char* argv[]);

int main(int argc, const char* argv[]) {
    unsigned long stackGuard = STACK_GUARD; stackGuardHackPtr = &stackGuard;
    srand((unsigned)time(0));

    platInit();
    int result = wrappedMain(argc, argv);
    platUninit();

    return result;
}
#ifndef DEDICATED_SERVER_ONLY
END_OF_MAIN()
#endif

int wrappedMain(int argc, const char* argv[]) {
    g_timeCounter.setZero();

    #ifndef DEDICATED_SERVER_ONLY

    // Set the text encoding format for Allegro as 8 bit Ascii
    set_uformat(U_ASCII);

    // APbuild messes up the automation -> call by hand
    setAllegroIcon();

    three_finger_flag = FALSE;

    if (allegro_init()) {   // rely on unspecified behavior: 4.0.3 exit()s on error and doesn't get here but returns 0 on success, newer Allegro returns status
        fprintf(stderr, "Initializing Allegro failed.\n");
        return 1;
    }
    install_keyboard();

    // find out where we are
    char* path = new char[2048];
    get_executable_name(path, 2048);
    replace_filename(path, path, "", 256);
    wheregamedir = path;
    delete[] path;

    #else // !DEDICATED_SERVER_ONLY

    wheregamedir = "./";

    #endif // !DEDICATED_SERVER_ONLY

    NoLog noLog;
    LogSet noLogSet(&noLog, &noLog, &noLog);
    if (!check_dir("log", noLogSet)) {
        messageBox("Error", "The directory 'log' was not found and could not be created.");
        return 1;
    }

    FileLog logFile(wheregamedir + "log" + directory_separator + "log.txt", true);
    MemoryLog memoryErrorLog;
    DualLog errorLog(logFile, memoryErrorLog, "ERROR: ");
    LogSet log(&logFile, &errorLog, &logFile);

    innerMain(argc, argv, log, memoryErrorLog);

    bool err = memoryErrorLog.size() != 0;
    errorMessage(_("Errors"), memoryErrorLog, '\n' + _("See the 'log' directory for more information."));
    return err;
}

void innerMain(int argc, const char* argv[], LogSet& log, MemoryLog& memoryErrorLog) {
    log("Outgun log file. %s. Game string: %s, protocol: %s, version: %s", date_and_time().c_str(), GAME_STRING, GAME_PROTOCOL, GAME_VERSION);
    logThreadStart("main", log);

    bool showFirstTimeSplash = true;
    {
        const string main_cfg_file = wheregamedir + "config" + directory_separator + "maincfg.txt";
        ifstream in(main_cfg_file.c_str());
        string line;
        if (getline_skip_comments(in, line)) {
            showFirstTimeSplash = false;
            if (line == "autobugreporting disabled")
                g_autoBugReporting = ABR_disabled;
            else if (line == "autobugreporting minimal")
                g_autoBugReporting = ABR_minimal;
            else if (line == "autobugreporting complete")
                g_autoBugReporting = ABR_withDump;
            else {
                showFirstTimeSplash = true;
                g_autoBugReporting = ABR_disabled;
            }
        }
    }

    check_dir("config", log);
    check_dir("languages", log);

    int acceptedErrorCount = memoryErrorLog.size(); // this is just a flag here; final value of acceptedErrorCount is set after loading the language

    {
        const string lang_file = wheregamedir + "config" + directory_separator + "language.txt";
        ifstream in(lang_file.c_str());
        string lang_str;
        if (getline_skip_comments(in, lang_str)) {
            if (!lang_str.empty() && lang_str.find_first_of(".:/\\") == string::npos)
                language.load(lang_str, log);   // load() will log.error() if something goes wrong; we're not aborting if that happens, and usefully the client will pick up and show the error message
            else
                log.error("Invalid language '" + lang_str + "' in " + lang_file + '.');
        }
        in.close();
    }

    if (acceptedErrorCount == 0)    // no errors before loading the language
        acceptedErrorCount = memoryErrorLog.size(); // accept errors in loading the language
    else
        acceptedErrorCount = 0;

    bool textserver = false;
    bool showinfo = false;
    bool defaultprio = false;   //select default server threads priority
    int targetprio = 0;
    bool targetprio_specified = false;
    ServerExternalSettings serverCfg;
    #ifndef DEDICATED_SERVER_ONLY
    ClientExternalSettings clientCfg;
    #endif

    // check args
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-ded"))
            serverCfg.dedserver = true;
        else if (!strcmp(argv[i], "-text") || !strcmp(argv[i], "-nowindow"))
            textserver = true;
        else if (!strcmp(argv[i], "-priv")) {
            serverCfg.privateserver = true;
            serverCfg.privSettingForced = true;
        }
        else if (!strcmp(argv[i], "-public")) {
            serverCfg.privateserver = false;
            serverCfg.privSettingForced = true;
        }
        else if (!strcmp(argv[i], "-info"))
            showinfo = true;
        else if (!strcmp(argv[i], "-defaultprio"))
            defaultprio = true;
        else if (!strcmp(argv[i], "-prio")) {
            if (++i < argc) {
                targetprio = strtol(argv[i], NULL, 10);
                targetprio_specified = true;
            }
        }
        else if (!strcmp(argv[i], "-unsafeserver"))
            serverCfg.threadLock = false;
        #ifndef DEDICATED_SERVER_ONLY
        else if (!strcmp(argv[i], "-win"))
            clientCfg.winclient = 1;
        else if (!strcmp(argv[i], "-flip"))
            clientCfg.trypageflip = 1;
        else if (!strcmp(argv[i], "-dbuf"))
            clientCfg.trypageflip = 0;
        else if (!strcmp(argv[i], "-fs"))
            clientCfg.winclient = 0;
        else if (!strcmp(argv[i], "-defmode"))
            clientCfg.forceDefaultGfxMode = true;
        else if (!strcmp(argv[i], "-fps")) {
            if (++i<argc) {
                int fps = strtol(argv[i], NULL, 10);
                if (fps < 1 || fps > 1000)
                    log.error(_("-fps X: X must be in the range of 1 to 1000."));
                else
                    clientCfg.targetfps = fps;
            }
            else
                log.error(_("-fps must be followed by a space and a number."));
        }
        else if (!strcmp(argv[i], "-nosound"))
            clientCfg.nosound = true;
        #endif
        else if (!strcmp(argv[i], "-maxp")) {
            if (++i < argc) {
                int maxp = strtol(argv[i], NULL, 10);
                if (maxp < 2 || maxp > MAX_PLAYERS || (maxp % 2 == 1))
                    log.error(_("-maxp X: X must be an even number in the range of 2 to $1.", itoa(MAX_PLAYERS)));
                else
                    serverCfg.server_maxplayers = maxp;
            }
            else
                log.error(_("-maxp must be followed by a space and a player count."));
        }
        else if (!strcmp(argv[i], "-port")) {
            if (++i < argc) {
                serverCfg.port = strtol(argv[i], NULL, 10);
                if (serverCfg.port < 1 || serverCfg.port > 65535)
                    log.error(_("-port X: X must be in the range of 1 to 65535."));
                else
                    serverCfg.portForced = true;
            }
            else
                log.error(_("-port must be followed by a space and a port number."));
        }
        #ifndef DEDICATED_SERVER_ONLY
        else if (!strcmp(argv[i], "-cport")) {
            if (++i < argc) {
                int p1, p2;
                char c1, c2;
                const int count = sscanf(argv[i], "%d%c%d%c", &p1, &c1, &p2, &c2);
                if (count != 1 && !(count == 3 && c1 == ':'))
                    log.error(_("-cport must be followed by a space and either a port number or minport:maxport."));
                else if (p1 < 1 || p1 > 65535 || (count == 3 && (p2 < 1 || p2 > 65535)))
                    log.error(_("-cport X or -cport X:Y: X and Y must be in the range of 1 to 65535."));
                else if (count == 3 && p2 <= p1)
                    log.error(_("-cport X:Y: Y must be greater than X."));
                else {
                    clientCfg.minLocalPort = p1;
                    clientCfg.maxLocalPort = (count == 3) ? p2 : p1;
                }
            }
            else
                log.error(_("-cport must be followed by a space and either a port number or minport:maxport."));
        }
        #endif
        else if (!strcmp(argv[i], "-sport")) {
            if (++i < argc) {
                int p1, p2;
                char c1, c2;
                const int count = sscanf(argv[i], "%d%c%d%c", &p1, &c1, &p2, &c2);
                if (!(count == 3 && c1 == ':'))
                    log.error(_("-sport must be followed by a space and minport:maxport."));
                else if (p1 < 1 || p1 > 65535 || p2 < 1 || p2 > 65535)
                    log.error(_("-sport X:Y: X and Y must be in the range of 1 to 65535."));
                else if (p2 <= p1)
                    log.error(_("-sport X:Y: Y must be greater than X."));
                else {
                    serverCfg.minLocalPort = p1;
                    serverCfg.maxLocalPort = p2;
                }
            }
            else
                log.error(_("-sport must be followed by a space and minport:maxport."));
        }
        else if (!strcmp(argv[i], "-ip")) {
            if (++i < argc) {
                if (isValidIP(argv[i])) {
                    serverCfg.ipAddress = argv[i];
                    serverCfg.ipForced = true;
                }
                else
                    log.error(_("-ip X: X must be a valid IP address without :port."));
            }
            else
                log.error(_("-ip must be followed by a space and an IP address."));
        }
        #ifndef DEDICATED_SERVER_ONLY
        else if (!strcmp(argv[i], "-mappic")) {
            check_dir("mappic", log);
            if (argc != 2)
                log.error(_("-mappic can't be combined with other command line options."));

            if (memoryErrorLog.size() != acceptedErrorCount)    // no point in continuing if there were errors
                return;

            log("Saving map pictures");
            set_window_title(_("Outgun - Saving map pictures").c_str());
            Mappic mappic(log);
            try {
                mappic.run();
                messageBox("Outgun", _("Saved map pictures to the directory 'mappic'."));
            } catch (const Mappic::Save_error& s) {
                log.error(_("Could not save map pictures to the directory 'mappic'."));
            }
            return;
        }
        #endif
        else if (!strcmp(argv[i], "-suppressmessages"))
            g_allowBlockingMessages = false;
        else if (!strcmp(argv[i], "-debug")) {
            if (++i < argc) {
                int level = strtol(argv[i], NULL, 10);
                if (level < 0 || level > 2)
                    log.error(_("-debug X: X must be in the range of 0 to 2."));
                else {
                    g_leetnetLog     = (level >= 1);
                    g_leetnetDataLog = (level >= 2);
                }
            }
            else
                log.error(_("-debug must be followed by a space and a number."));
        }
        else
            log.error(_("Unknown command-line argument '$1'.", argv[i]));
    }
    if (nlInit() == NL_FALSE)
        log.error(_("Can't init HawkNL. $1", getNlErrorString()));
    if (nlSelectNetwork(NL_IP) == NL_FALSE)
        log.error(_("No IP network."));
    // enable statistics
    nlEnable(NL_SOCKET_STATS);

    if (serverCfg.ipAddress.empty())
        serverCfg.ipAddress = getPublicIP(log, false);
    if (serverCfg.ipAddress.empty()) {
        LogSet noLogSet(0, 0, 0);   // don't log this second round which would just duplicate information
        serverCfg.ipAddress = getPublicIP(noLogSet, true);
    }

    g_masterSettings.load(log);

    // get system thread priorities
    int         pmin = sched_get_priority_min(SCHED_OTHER);
    int         pmax = sched_get_priority_max(SCHED_OTHER);
    int         policy;
    sched_param param;
    int         rc = pthread_getschedparam(pthread_self(), &policy, &param); // get priority of current thread (which is the default value)
    int         pdef = param.sched_priority;
    log("Thread priorities:");
    log("   rc = %i policy = %i (%i)", rc, policy, SCHED_OTHER);
    log("   pmin %i pmax %i pdef = %i", pmin, pmax, pdef);

    //show info
    if (showinfo) {
        if (memoryErrorLog.size() != acceptedErrorCount)  // not continuing if there were errors
            return;

        //get all local addresses
        NLint locsize;
        const NLaddress *locals = nlGetAllLocalAddr(&locsize);

        string infobuf =
            _("Possible thread priorities (-prio <val>):") + '\n' +
            _("* Minimum: $1", itoa(pmin)) + '\n' +
            _("* Maximum: $1", itoa(pmax)) + '\n' +
            _("* System default (use -defaultprio): $1", itoa(pdef)) + '\n' +
            _("* Outgun default: $1", itoa((pmax - 1 < pmin) ? pdef : pmax - 1)) + "\n\n" +
            _("IP addresses:") + '\n';

        for (int i = 0; i < locsize; i++) {
            infobuf += addressToString(locals[i]);
            infobuf += '\n';
        }

        messageBox(_("Information"), infobuf);
        return;
    }

    int highPriority;
    if (!defaultprio) {
        int ptarg;
        if (targetprio_specified)
            ptarg = targetprio;
        else
            ptarg = pmax - 1;   // guess one below system maximum (which usually means realtime and should never be used really)

        if (ptarg < pmin || ptarg > pmax) {
            if (targetprio_specified)
                log.error(_("The given priority $1 isn't within system limits. Run with -info for more information.", itoa(ptarg)));
            else    // this mostly happens if pmin == pmax
                log("Couldn't set a higher priority. Using default.");
            ptarg = pdef;
        }

        highPriority = ptarg;
        log("Priority set for server: %i", ptarg);
    }
    else {
        highPriority = pdef;
        log("-defaultprio: Leaving thread priorities on their default values");
    }

    serverCfg.lowerPriority = pdef;
    serverCfg.priority = serverCfg.networkPriority = highPriority;

    #ifndef DEDICATED_SERVER_ONLY
    clientCfg.priority = clientCfg.lowerPriority = pdef;
    clientCfg.networkPriority = highPriority;

    GlobalCloseButtonHook::install();
    GlobalDisplaySwitchHook::init();
    #endif

    check_dir(SERVER_MAPS_DIR, log);    // the client might run a server, so check these in any case
    check_dir("server_stats" , log);

    #ifdef DEDICATED_SERVER_ONLY
    serverCfg.dedserver = textserver = true;
    #endif

    // run dedicated server
    if (serverCfg.dedserver) {
        if (textserver)
            serverCfg.statusOutput = statusOutputText;
        #ifndef DEDICATED_SERVER_ONLY
        else {
            if (!set_shitty_mode(log))  // if 320×240 mode can't be set, use textserver
                serverCfg.statusOutput = statusOutputText;
            else
                serverCfg.statusOutput = statusOutputWindow;
        }

        if (set_display_switch_mode(SWITCH_BACKAMNESIA) == -1) {
            if (set_display_switch_mode(SWITCH_BACKGROUND) == -1)
                log.error(_("Can't set server to run in the background."));
            else
                log("Switch_background set ok.");
        }
        else
            log("Switch_backamnesia set ok.");

        if (serverCfg.statusOutput == statusOutputWindow) {
            GlobalDisplaySwitchHook::install();
            serverCfg.ownScreen = true;
        }
        #endif

        if (memoryErrorLog.size() != acceptedErrorCount)  // no point in continuing if there were errors
            return;

        // run server
        GameserverInterface* gameserver = new GameserverInterface(log, serverCfg, memoryErrorLog, "");
        if (gameserver->start(serverCfg.server_maxplayers)) {
            #ifndef DEDICATED_SERVER_ONLY
            gameserver->loop(GlobalCloseButtonHook::flagPtr(), true);
            #else
            bool quit = false;
            gameserver->loop(&quit, false);
            #endif
            gameserver->stop();
        }
        else
            log.error(_("Can't start the server."));
        delete gameserver;
    }
    // run client
    #ifndef DEDICATED_SERVER_ONLY
    else {
        check_dir(CLIENT_MAPS_DIR, log);
        check_dir("screens"      , log);
        check_dir("graphics"     , log);
        check_dir("sound"        , log);
        check_dir("client_stats" , log);

        if (memoryErrorLog.size() != acceptedErrorCount)  // no point in continuing if there were errors
            return;

        // run client
        clientCfg.statusOutput = statusOutputWindow;
        serverCfg.statusOutput = statusOutputWindow;
        Client* gameclient = new Client(log, clientCfg, serverCfg, memoryErrorLog);
        if (gameclient->start()) {
            gameclient->loop(GlobalCloseButtonHook::flagPtr(), showFirstTimeSplash);
            gameclient->stop();
        }
        else
            log.error(_("Can't start the client."));
        delete gameclient;
    }
    #endif

    log("Exiting");
    // exit HawkNL
    nlShutdown();

    return;
}
