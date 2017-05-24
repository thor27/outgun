/*
 *  server_settings.cpp - implementation of Server::SettingManager
 *
 *  Copyright (C) 2008 - Niko Ritari
 *  Copyright (C) 2008 - Jani Rivinoja
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

#include "gamemod.h"

#include "server.h"

using std::ifstream;
using std::istringstream;
using std::string;
using std::vector;

bool Server::SettingManager::setForceIP(const string& val) throw () { ipAddress = val; return true; }
void Server::SettingManager::setRandomMaprot(int val) throw () { random_maprot = (val == 1); random_first_map = (val == 2); }

const string& Server::SettingManager::getForceIP() const throw () { return ipAddress; }
int Server::SettingManager::getMaxplayers() const throw () { return server.maxplayers; } //#fix?
int Server::SettingManager::getRandomMaprot() const throw () { return random_maprot ? 1 : random_first_map ? 2 : 0; }

bool Server::SettingManager::checkForceIpValue(const string& val) throw () {
    return isValidIP(val);
}

bool Server::SettingManager::trySetMaxplayers(int val) throw () {
    if (val != server.maxplayers && server.network.get_player_count() != 0) {
        server.log.error(_("Can't change max_players while players are connected."));
        return false;
    }
    server.setMaxPlayers(val);
    return true;
}

void Server::SettingManager::processLine(const string& line, LogSet& argLogs, bool allowGet, const GamemodAccessDescriptor& access) const throw () {
    string cmd, value;
    istringstream ist(line);
    ist >> cmd;
    ist.ignore();
    getline(ist, value);
    for (vector<Category>::const_iterator ci = categories.begin(); ci != categories.end(); ++ci)
        for (vector<GamemodSetting*>::const_iterator si = ci->settings.begin(); si != ci->settings.end(); ++si) {
            GamemodSetting& setting = **si;
            if (!setting.matchCommand(cmd))
                continue;
            if (!access.isAllowed(ci->identifier, cmd))
                argLogs("You don't have permission to access the setting %s.", cmd.c_str());
            else {
                if (value.empty() && allowGet) {
                    const string value = setting.get();
                    if (value.empty())
                        argLogs("The setting %s can't be queried.", cmd.c_str());
                    else
                        argLogs("%s = %s.", cmd.c_str(), value.c_str());
                }
                else {
                    if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"') {
                        value.erase(value.size() - 1);
                        value.erase(0, 1);
                    }
                    setting.set(argLogs, value);  // ignore return value; the status is logged
                }
            }
            return;
        }
    argLogs.error(_("Unrecognized gamemod setting: '$1'.", cmd));
}

void Server::SettingManager::free() throw () {
    for (vector<Category>::const_iterator ci = categories.begin(); ci != categories.end(); ++ci)
        for (vector<GamemodSetting*>::const_iterator si = ci->settings.begin(); si != ci->settings.end(); ++si)
            delete *si;
    categories.clear();
    for (vector<DisposerBase*>::const_iterator di = redirectFnDisposers.begin(); di != redirectFnDisposers.end(); ++di) {
        (*di)->dispose();
        delete *di;
    }
    redirectFnDisposers.clear();
}

void Server::SettingManager::build(bool reload) throw () {
    if (built && builtForReload == reload)
        return;
    built = true;
    builtForReload = reload;

    free();

    // create redirections to functions, for those gamemod settings that require more than just setting a variable

    // abbreviations to keep the list manageable
    typedef const string& CSR;
    typedef SettingManager This;
    typedef ServerNetworking Network;

    #define    FUN0            RedirectToFun0
    #define   NFUN0         newRedirectToFun0
    #define    FUN1            RedirectToFun1
    #define   NFUN1         newRedirectToFun1

    #define   MFUN1         RedirectToMemFun1
    #define  NMFUN1      newRedirectToMemFun1

    #define  CMFUN0    RedirectToConstMemFun0
    #define NCMFUN0 newRedirectToConstMemFun0

    #define  STCSR0      HookFnStripConstRef0
    #define NSTCSR0   newHookFnStripConstRef0

    // checkers
    FUN1 <         bool, CSR> &checkForceIP        = *addFn(NFUN1  (          &         checkForceIpValue     ));
    FUN1 <         bool, int> &checkMaxplayer      = *addFn(NFUN1  (          &         checkMaxplayerSetting ));

    // setters
    MFUN1<This,    bool, CSR> &setForceIP          = *addFn(NMFUN1 ( this,    &   This::setForceIP            ));
    MFUN1<This,    bool, int> &tryMaxplayer        = *addFn(NMFUN1 ( this,    &   This::trySetMaxplayers      ));
    MFUN1<This,    void, int> &setRandomMaprot     = *addFn(NMFUN1 ( this,    &   This::setRandomMaprot       ));
    MFUN1<Network, void, CSR> &setRelayServer      = *addFn(NMFUN1 (&network, &Network::set_relay_server      ));

    // getters that require changing of return type from const string& -> string
    STCSR0<         string>   &getForceIP          = *addFn(NSTCSR0(*addFn(NCMFUN0( this,    &   This::getForceIP            ))));

    // regular getters
    CMFUN0<This,    int>      &getMaxplayers       = *addFn(NCMFUN0( this,    &   This::getMaxplayers         ));
    CMFUN0<This,    int>      &getRandomMaprot     = *addFn(NCMFUN0( this,    &   This::getRandomMaprot       ));
    CMFUN0<Network, string>   &getRelayServer      = *addFn(NCMFUN0(&network, &Network::get_relay_server      ));

    #undef FUN0
    #undef NFUN0
    #undef FUN1
    #undef NFUN1
    #undef MFUN1
    #undef NMFUN1
    #undef CMFUN0
    #undef NCMFUN0
    #undef STCSR0
    #undef NSTCSR0

    // create and categorize settings

    GamemodSetting* portSetting, * ipSetting, * privSetting, * srvmonitSetting;
    if (reload) {
        portSetting = new GS_DisallowRunning("server_port");
        ipSetting   = new GS_DisallowRunning("server_ip");
        srvmonitSetting = new GS_DisallowRunning("srvmonit_port");
    }
    else {
        if (extConfig.portForced)
            portSetting = new GS_Ignore ("server_port");
        else
            portSetting = new GS_Int    ("server_port",      &port, 1, 65535);
        if (extConfig.ipForced)
            ipSetting = new GS_Ignore   ("server_ip");
        else
            ipSetting = new GS_CheckForwardStr("server_ip",  _("IP address without :port"), checkForceIP, setForceIP, getForceIP);
        srvmonitSetting = new GS_Int    ("srvmonit_port",    &srvmonit_port, 1, 65535);
    }
    if (reload || !extConfig.privSettingForced)
        privSetting = new GS_Boolean("private_server",       &privateserver);
    else
        privSetting = new GS_Ignore ("private_server");

    Category cat("general" , "General settings");
    cat.add(new GS_String    ("server_name",                 &hostname, false));
    cat.add(new GS_CheckForwardInt("max_players",            _("an even integer between 2 and $1", itoa(MAX_PLAYERS)), checkMaxplayer, tryMaxplayer, getMaxplayers));
    cat.add(new GS_AddString ("welcome_message",             &welcome_message));
    cat.add(new GS_AddString ("info_message",                &info_message));
    cat.add(new GS_Boolean   ("sayadmin_enabled",            &sayadmin_enabled));
    cat.add(new GS_String    ("sayadmin_comment",            &sayadmin_comment));
    cat.add(new GS_String    ("server_password",             &server_password));
    cat.add(new GS_Boolean   ("tournament",                  &tournament));
    cat.add(new GS_Int       ("save_stats",                  &save_stats, 0, MAX_PLAYERS));
    cat.add(new GS_Int       ("idlekick_time",               &idlekick_time, 10, GS_Int::lim::max(), 10, 0, true));  // convert seconds to frames; special setting: allow 0 that is outside the normal range
    cat.add(new GS_Int       ("idlekick_playerlimit",        &idlekick_playerlimit, 1, MAX_PLAYERS));
    cat.add(portSetting);
    cat.add(ipSetting);
    cat.add(privSetting);
    cat.add(new GS_Int       ("join_start",                  &join_start, 0, 24 * 3600 - 1));
    cat.add(new GS_Int       ("join_end",                    &join_end, 0, 24 * 3600 - 1));
    cat.add(new GS_String    ("join_limit_message",          &join_limit_message));
    cat.add(srvmonitSetting);
    cat.add(new GS_Int       ("recording",                   &recording, 0, MAX_PLAYERS));
    cat.add(new GS_ForwardStr("relay_server",                setRelayServer, getRelayServer));
    cat.add(new GS_IntT<unsigned>("spectating_delay",        &spectating_delay, 0, GS_IntT<unsigned>::lim::max()));
    cat.add(new GS_Boolean   ("log_player_chat",             &log_player_chat));
    categories.push_back(cat);

    cat = Category("website" , "Server web site");
    cat.add(new GS_String    ("server_website",              &server_website_url));
    cat.add(new GS_AddString ("web_server",                  &web_servers));
    cat.add(new GS_String    ("web_script",                  &web_script));
    cat.add(new GS_String    ("web_auth",                    &web_auth));
    cat.add(new GS_Int       ("web_refresh",                 &web_refresh, 1));
    categories.push_back(cat);

    cat = Category("bots"    , "Bot settings");
    cat.add(new GS_Int       ("min_bots",                    &min_bots, 0, MAX_PLAYERS));
    cat.add(new GS_Int       ("bots_fill",                   &bots_fill, 0, MAX_PLAYERS));
    cat.add(new GS_Boolean   ("balance_bot",                 &balance_bot));
    cat.add(new GS_Int       ("bot_ping",                    &bot_ping, 0, 500));
    cat.add(new GS_String    ("bot_name_lang",               &bot_name_lang));
    categories.push_back(cat);

    cat = Category("maps"    , "Map rotation");
    cat.add(new GS_Map       ("map",                         &server.maprot)); //#fix?
    cat.add(new GS_RandomMap ("random_map",                  &server.maprot));
    cat.add(new GS_ForwardInt("random_maprot",               setRandomMaprot, getRandomMaprot, 0, 2));
    cat.add(new GS_Int       ("vote_block_time",             &vote_block_time, 0, GS_Int::lim::max(), 60 * 10)); // convert minutes to frames
    cat.add(new GS_Boolean   ("require_specific_map_vote",   &require_specific_map_vote));
    categories.push_back(cat);

    cat = Category("game"    , "Game config");
    cat.add(new GS_Int       ("capture_limit",               &worldConfig.capture_limit, 0));
    cat.add(new GS_Int       ("win_score_difference",        &worldConfig.win_score_difference, 1));
    cat.add(new GS_Ulong     ("time_limit",                  &worldConfig.time_limit, 0, GS_Ulong::lim::max(), 60 * 10));    // convert minutes to frames
    cat.add(new GS_Ulong     ("extra_time",                  &worldConfig.extra_time, 0, GS_Ulong::lim::max(), 60 * 10));    // convert minutes to frames
    cat.add(new GS_Boolean   ("sudden_death",                &worldConfig.sudden_death));
    cat.add(new GS_Int       ("game_end_delay",              &game_end_delay, 0));
    cat.add(new GS_Double    ("flag_return_delay",           &worldConfig.flag_return_delay, 0));
    cat.add(new GS_Int       ("carrying_score_time",         &worldConfig.carrying_score_time, 0));
    cat.add(new GS_Boolean   ("random_wild_flag",            &worldConfig.random_wild_flag));
    cat.add(new GS_Boolean   ("lock_team_flags",             &worldConfig.lock_team_flags));
    cat.add(new GS_Boolean   ("lock_wild_flags",             &worldConfig.lock_wild_flags));
    cat.add(new GS_Boolean   ("capture_on_team_flag",        &worldConfig.capture_on_team_flag));
    cat.add(new GS_Boolean   ("capture_on_wild_flag",        &worldConfig.capture_on_wild_flag));
    cat.add(new GS_Balance   ("balance_teams",               &worldConfig.balance_teams));
    cat.add(new GS_Double    ("respawn_time",                &worldConfig.respawn_time, 0.));
    cat.add(new GS_Double    ("extra_respawn_time_alone",    &worldConfig.extra_respawn_time_alone, 0.));
    cat.add(new GS_Double    ("respawn_balancing_time",      &worldConfig.respawn_balancing_time, 0.));
    cat.add(new GS_Double    ("waiting_time_deathbringer",   &worldConfig.waiting_time_deathbringer, 0.));
    cat.add(new GS_Double    ("spawn_safe_time",             &worldConfig.spawn_safe_time, 0.));
    cat.add(new GS_Boolean   ("respawn_on_capture",          &worldConfig.respawn_on_capture));
    cat.add(new GS_Boolean   ("free_turning",                &world.physics.allowFreeTurning));
    cat.add(new GS_Int       ("minimap_send_limit",          &minimap_send_limit, 0, 32));
    cat.add(new GS_Int       ("see_rockets_distance",        &worldConfig.see_rockets_distance, 0));
    categories.push_back(cat);

    cat = Category("health"  , "Health, energy and shooting");
    cat.add(new GS_Percentage("friendly_fire",               &world.physics.friendly_fire));
    cat.add(new GS_Percentage("friendly_deathbringer",       &world.physics.friendly_db));
    cat.add(new GS_Int       ("rocket_damage",               &worldConfig.rocket_damage, 0));
    cat.add(new GS_Int       ("start_health",                &worldConfig.start_health, 1, 500));
    cat.add(new GS_Int       ("start_energy",                &worldConfig.start_energy, 0, 500));
    cat.add(new GS_Int       ("health_max",                  &worldConfig.health_max, 1, 500));
    cat.add(new GS_Int       ("energy_max",                  &worldConfig.energy_max, 0, 500));
    cat.add(new GS_Double    ("health_regeneration_0_to_100",   &worldConfig.health_regeneration_0_to_100, 0.));
    cat.add(new GS_Double    ("health_regeneration_100_to_200", &worldConfig.health_regeneration_100_to_200, 0.));
    cat.add(new GS_Double    ("health_regeneration_200_to_max", &worldConfig.health_regeneration_200_to_max, 0.));
    cat.add(new GS_Double    ("energy_regeneration_0_to_100",   &worldConfig.energy_regeneration_0_to_100, 0.));
    cat.add(new GS_Double    ("energy_regeneration_100_to_200", &worldConfig.energy_regeneration_100_to_200, 0.));
    cat.add(new GS_Double    ("energy_regeneration_200_to_max", &worldConfig.energy_regeneration_200_to_max, 0.));
    cat.add(new GS_Int       ("min_health_for_run_penalty",  &worldConfig.min_health_for_run_penalty, 1, 500));
    cat.add(new GS_Double    ("run_health_degradation",      &worldConfig.run_health_degradation));
    cat.add(new GS_Double    ("run_energy_degradation",      &worldConfig.run_energy_degradation));
    cat.add(new GS_Double    ("shooting_energy_base",        &worldConfig.shooting_energy_base, -500., 500.));
    cat.add(new GS_Double    ("shooting_energy_per_extra_rocket", &worldConfig.shooting_energy_per_extra_rocket, -500., 500.));
    cat.add(new GS_Double    ("hit_stun_time",               &worldConfig.hit_stun_time, 0.));
    cat.add(new GS_Double    ("shoot_interval",              &worldConfig.shoot_interval, .1));
    cat.add(new GS_Double    ("shoot_interval_with_energy",  &worldConfig.shoot_interval_with_energy, .1));
    categories.push_back(cat);

    cat = Category("powerups", "Powerups");
    cat.add(new GS_PowerupNum("pups_min",                    &pupConfig.pups_min, &pupConfig.pups_min_percentage));
    cat.add(new GS_PowerupNum("pups_max",                    &pupConfig.pups_max, &pupConfig.pups_max_percentage));
    cat.add(new GS_Int       ("pups_respawn_time",           &pupConfig.pups_respawn_time, 0));
    cat.add(new GS_Int       ("pup_add_time",                &pupConfig.pup_add_time, 1, 999));
    cat.add(new GS_Int       ("pup_max_time",                &pupConfig.pup_max_time, 1, 999));
    cat.add(new GS_Boolean   ("pups_drop_at_death",          &pupConfig.pups_drop_at_death));
    cat.add(new GS_Int       ("pups_player_max",             &pupConfig.pups_player_max, 1));
    cat.add(new GS_Int       ("pup_weapon_max",              &pupConfig.pup_weapon_max, 1, 9));
    cat.add(new GS_Int       ("pup_health_bonus",            &pupConfig.pup_health_bonus, 1));
    cat.add(new GS_Double    ("pup_power_damage",            &pupConfig.pup_power_damage, 0.));
    cat.add(new GS_Double    ("pup_deathbringer_time",       &pupConfig.pup_deathbringer_time, 1.));
    cat.add(new GS_Boolean   ("pup_deathbringer_switch",     &pupConfig.pup_deathbringer_switch));
    cat.add(new GS_Int       ("pup_deathbringer_health_limit", &pupConfig.deathbringer_health_limit, 1, 500));
    cat.add(new GS_Int       ("pup_deathbringer_energy_limit", &pupConfig.deathbringer_energy_limit, 0, 500));
    cat.add(new GS_Double    ("pup_deathbringer_health_degradation", &pupConfig.deathbringer_health_degradation, 0.));
    cat.add(new GS_Double    ("pup_deathbringer_energy_degradation", &pupConfig.deathbringer_energy_degradation, 0.));
    cat.add(new GS_Int       ("pup_shadow_invisibility",     &worldConfig.shadow_minimum, 0, 1, -WorldSettings::shadow_minimum_normal, +WorldSettings::shadow_minimum_normal));  // 0->smn, 1->0
    cat.add(new GS_Int       ("pup_shield_one_hit",          &pupConfig.pup_shield_hits, 0));
    cat.add(new GS_Int       ("pup_chance_shield",           &pupConfig.pup_chance_shield,       0));
    cat.add(new GS_Int       ("pup_chance_turbo",            &pupConfig.pup_chance_turbo,        0));
    cat.add(new GS_Int       ("pup_chance_shadow",           &pupConfig.pup_chance_shadow,       0));
    cat.add(new GS_Int       ("pup_chance_power",            &pupConfig.pup_chance_power,        0));
    cat.add(new GS_Int       ("pup_chance_weapon",           &pupConfig.pup_chance_weapon,       0));
    cat.add(new GS_Int       ("pup_chance_megahealth",       &pupConfig.pup_chance_megahealth,   0));
    cat.add(new GS_Int       ("pup_chance_deathbringer",     &pupConfig.pup_chance_deathbringer, 0));
    cat.add(new GS_Boolean   ("pup_start_shield",            &pupConfig.start_shield));
    cat.add(new GS_Int       ("pup_start_turbo",             &pupConfig.start_turbo, 0, 999));
    cat.add(new GS_Int       ("pup_start_shadow",            &pupConfig.start_shadow, 0, 999));
    cat.add(new GS_Int       ("pup_start_power",             &pupConfig.start_power, 0, 999));
    cat.add(new GS_Int       ("pup_start_weapon",            &pupConfig.start_weapon, 1, 9));
    cat.add(new GS_Boolean   ("pup_start_deathbringer",      &pupConfig.start_deathbringer));
    categories.push_back(cat);

    cat = Category("physics" , "Physics");
    cat.add(new GS_Double    ("friction",                    &world.physics.fric));
    cat.add(new GS_Double    ("drag",                        &world.physics.drag));
    cat.add(new GS_Double    ("acceleration",                &world.physics.accel));
    cat.add(new GS_Double    ("brake_acceleration",          &world.physics.brake_mul, .01));
    cat.add(new GS_Double    ("turn_acceleration",           &world.physics.turn_mul,  .01));
    cat.add(new GS_Double    ("run_acceleration",            &world.physics.run_mul));
    cat.add(new GS_Double    ("turbo_acceleration",          &world.physics.turbo_mul));
    cat.add(new GS_Double    ("flag_acceleration",           &world.physics.flag_mul));
    cat.add(new GS_Collisions("player_collisions",           &world.physics.player_collisions));
    cat.add(new GS_Double    ("rocket_speed",                &world.physics.rocket_speed));
    categories.push_back(cat);
}

void Server::SettingManager::commit(bool reload) throw () {
    if (srvmonit_port == -1) {
        srvmonit_port = port - 500;
        if (srvmonit_port < 1)
            srvmonit_port += 65535;
    }

    const int chanceSum = pupConfig.pup_chance_shield + pupConfig.pup_chance_turbo + pupConfig.pup_chance_shadow + pupConfig.pup_chance_power
            + pupConfig.pup_chance_weapon + pupConfig.pup_chance_megahealth + pupConfig.pup_chance_deathbringer;
    if (chanceSum == 0)
        pupConfig.pups_max = 0;

    if ((pupConfig.pups_min_percentage == pupConfig.pups_max_percentage && pupConfig.pups_min > pupConfig.pups_max) ||
          pupConfig.pups_max == 0)    // if they are in different units, only the value of 0 is comparable
        pupConfig.pups_min = pupConfig.pups_max;

    world.setConfig(worldConfig, pupConfig);
    if (reload)
        world.check_powerup_creation(true);

    server.check_bots = true;
    for (int i = 0; i < server.maxplayers; i++)
        if (world.player[i].used)
            network.send_server_settings(world.player[i]);
    if (recording)
        network.send_server_settings(pid_record);
    network.send_map_time(pid_all);  // broadcast time to all, in case time limit has been changed
}

vector<string> Server::SettingManager::listSettings(const GamemodAccessDescriptor& access) throw () {
    vector<string> ret;
    for (vector<Category>::const_iterator ci = categories.begin(); ci != categories.end(); ++ci) {
        string categorySettings;
        for (vector<GamemodSetting*>::const_iterator si = ci->settings.begin(); si != ci->settings.end(); ++si) {
            const string& name = (*si)->getName();
            if (!access.isAllowed(ci->identifier, name))
                continue;
            if (!categorySettings.empty())
                categorySettings += ", ";
            categorySettings += name;
        }
        if (!categorySettings.empty())
            ret.push_back(string() + ci->descriptiveName + ": " + categorySettings);
    }
    return ret;
}

vector<string> Server::SettingManager::executeLine(const string& line, const GamemodAccessDescriptor& access) throw () {
    static const bool reload = true;
    build(reload);
    MemoryLog tmpLog;
    LogSet tmpLogset(&tmpLog, &tmpLog, 0);
    processLine(line, tmpLogset, true, access);
    vector<string> ret;
    while (tmpLog.size())
        ret.push_back(tmpLog.pop());
    commit(reload);
    return ret;
}

void Server::SettingManager::loadGamemod(bool reload) throw () {
    build(reload);
    const string filename = wheregamedir + "config" + directory_separator + "gamemod.txt";
    ifstream in(filename.c_str());
    if (in) {
        server.log("Loading game mod: '%s'", filename.c_str());
        string line;
        while (getline_skip_comments(in, line))
            processLine(line, server.log, false, AuthorizationDatabase::AccessDescriptor::GamemodAccessDescriptor(true));
        server.log("Game mod file read.");
        in.close();
    }
    else
        server.log.error(_("Can't open game mod file '$1'.", filename));
    if (!server_website_url.empty())
        info_message.push_back(string() + "Website: " + server_website_url);
    commit(reload);
}

bool Server::SettingManager::isGamemodCommand(const string& cmd, bool includeCategoryNames) throw () {
    build(true);
    for (vector<Category>::const_iterator ci = categories.begin(); ci != categories.end(); ++ci) {
        if (includeCategoryNames && cmd == ci->identifier)
            return true;
        for (vector<GamemodSetting*>::const_iterator si = ci->settings.begin(); si != ci->settings.end(); ++si)
            if ((*si)->matchCommand(cmd))
                return true;
    }
    return false;
}

void Server::SettingManager::reset() throw () {
    pupConfig.reset();
    worldConfig.reset();

    privateserver = extConfig.privateserver;

    srvmonit_port = -1;

    game_end_delay = 5;

    vote_block_time = 0;    // no limit
    require_specific_map_vote = false;
    random_maprot = false;

    idlekick_time = 120 * 10;   // 2 minutes in frames
    idlekick_playerlimit = 4;

    welcome_message.clear();
    info_message.clear();

    sayadmin_comment.clear();
    sayadmin_enabled = false;

    server_website_url.clear();

    tournament = false;
    save_stats = 0;

    recording = 0;
    spectating_delay = 120;

    log_player_chat = false;

    minimap_send_limit = 32;

    min_bots = 0;
    bots_fill = 0;
    bot_ping = 300;
    balance_bot = false;

    server_password.clear();
    hostname = "Anonymous host"; // not translated - a server's language is irrelevant to clients

    join_start = 0;
    join_end = 0;
    join_limit_message.clear();

    web_servers.clear();
    web_servers.clear();
    web_script.clear();
    web_auth.clear();
    web_refresh = 2;
}
