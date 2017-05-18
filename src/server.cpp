/*
 *  server.cpp
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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>   // auto_ptr
#include <sstream>
#include <string>
#include <vector>

#include "incalleg.h"
#include "gamemod.h"
#include "language.h"
#include "nassert.h"
#include "platform.h"
#include "thread.h"
#include "timer.h"
#include "world.h"

// implements:
#include "server.h"
#include "gameserver_interface.h"

const int minimum_positive_score_for_ranking = 100;
const int voteAnnounceInterval = 5; // in seconds, how often a changing voting status will be announced

using std::endl;
using std::find;
using std::ifstream;
using std::ios;
using std::istringstream;
using std::list;
using std::max;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::random_shuffle;
using std::setfill;
using std::setprecision;
using std::setw;
using std::string;
using std::swap;
using std::vector;

Server::Server(LogSet& hostLogs, const ServerExternalSettings& config, Log& externalErrorLog, const std::string& errorPrefix) :
    normalLog(wheregamedir + "log" + directory_separator + "serverlog.txt", true),
    errorLog(normalLog, externalErrorLog, "ERROR: ", errorPrefix),
    securityLog(normalLog, "SECURITY WARNING: ", wheregamedir + "log" + directory_separator + "server_securitylog.txt", false),
    adminActionLog(normalLog, "ADMIN ACTION: ", wheregamedir + "log" + directory_separator + "adminactionlog.txt", false),
    log(&normalLog, &errorLog, &securityLog),
    threadLock(config.threadLock),
    threadLockMutex(),
    abortFlag(false),
    world(this, &network, log),
    network(this, world, log, threadLock, threadLockMutex),
    extConfig(config),
    authorizations(log)
{
    hostLogs("See serverlog.txt for server's log messages");
    setMaxPlayers(MAX_PLAYERS);
    next_vote_announce_frame = 0;
    last_vote_announce_votes = last_vote_announce_needed = 0;
    fav_colors[0].resize(16, false);
    fav_colors[1].resize(16, false);
    Thread::setCallerPriority(config.priority);
}

Server::~Server() { }

void Server::mutePlayer(int pid, int mode, int admin) { // 0 = unmute, 1 = normal, 2 = mute silently (do not inform the player)
    if (world.player[pid].muted == mode || (world.player[pid].muted == 1 && mode == 2))
        return;
    const string adminName = (admin == -1) ? "" : world.player[admin].name;
    const bool tellPlayer = (mode != 2 && (world.player[pid].muted != 2 || mode == 1));
    network.broadcast_mute_message(pid, mode, adminName, tellPlayer);
    logAdminAction(admin, (mode == 0 ? "unmuted" : mode == 1 ? "muted" : "silently muted"), pid);
    world.player[pid].muted = mode;
}

void Server::doKickPlayer(int pid, int admin, int minutes) {  // if minutes > 0, it's really a ban
    const string adminName = (admin == -1) ? "" : world.player[admin].name;
    network.broadcast_kick_message(pid, minutes, adminName);
    logAdminAction(admin, (minutes > 0 ? "banned for " + itoa(minutes) + " minutes" : "kicked"), pid);
    if (world.player[pid].kickTimer == 0)
        world.player[pid].kickTimer = 10 * 10;
}

void Server::kickPlayer(int pid, int admin) {
    if (world.player[pid].kickTimer == 0)
        doKickPlayer(pid, admin, 0);
}

void Server::banPlayer(int pid, int admin, int minutes) {
    authorizations.load();
    const NLaddress addr = network.get_client_address(world.player[pid].cid);
    if (!authorizations.isBanned(addr)) {
        authorizations.ban(addr, world.player[pid].name, minutes);
        authorizations.save();
        doKickPlayer(pid, admin, minutes);
    }
    else
        kickPlayer(pid, admin); // this is possible in the case of multiple players from the same IP; the time can't be changed anymore, so just kick
}

void Server::logAdminAction(int admin, const string& action, int target) {
    string message;
    if (target == -1)
        message = (admin == -1 ? "Admin shell user" : world.player[admin].name) + ' ' + action;
    else
        message = world.player[target].name + " [" + addressToString(network.get_client_address(world.player[target].cid)) + "] was "
                  + action + " by " + (admin == -1 ? "admin shell user" : world.player[admin].name);
    adminActionLog.put(message);
    network.sendTextToAdminShell(message);
}

bool Server::check_name_password(const string& name, const string& password) const {
    return authorizations.checkNamePassword(name, password);
}

void Server::ctf_game_restart() {
    //submit all pending reports and update tournament participation flags
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            network.client_report_status(world.player[i].cid);
            if (client[world.player[i].cid].current_participation != client[world.player[i].cid].next_participation) {
                client[world.player[i].cid].current_participation = client[world.player[i].cid].next_participation;
                network.broadcast_player_crap(i);
            }
        }

    world.returnAllFlags();

    if (worldConfig.balanceTeams()) {
        balance_teams();
        if (worldConfig.balanceTeams() == WorldSettings::TB_balance_and_shuffle)
            shuffle_teams();
    }

    network.sendWorldReset();   // must be before world.reset() because world.reset() already sends initializations
    world.reset();
}

void Server::balance_teams() {
    vector<int> team[2];
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            team[i / TSIZE].push_back(i);
    int difference = team[0].size() - team[1].size();
    const int bigger_team = (difference > 0 ? 0 : 1);
    while (difference > 1 || difference < -1) {
        const int victim = rand() % team[bigger_team].size();
        // Find a free slot in another team and move victim there.
        for (int i = (1 - bigger_team) * TSIZE; i < (2 - bigger_team) * TSIZE; i++)
            if (!world.player[i].used) {
                move_player(team[bigger_team][victim], i);
                team[1 - bigger_team].push_back(team[bigger_team][victim]);
                team[bigger_team].erase(team[bigger_team].begin() + victim);
                break;
            }
        difference = team[0].size() - team[1].size();
    }
}

void Server::shuffle_teams() {  // weird system, because players table has gaps
    vector<int> players;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            players.push_back(i);
    if (players.size() <= 2)
        return;
    random_shuffle(players.begin(), players.end()); // after this, indicates the new player id for each player
    // swap all red team players with the player of their new id (only do inter-team swaps)
    for (int i = 0, pi = 0; i < TSIZE; i++)
        if (world.player[i].used) {
            if (players[pi] >= TSIZE)
                swap_players(i, players[pi]);
            pi++;
        }
}

//check if team change requests can be satisfied
void Server::check_team_changes() {
    // check players in random order
    for (int i = 0; i < maxplayers; i++)
        check[i] = 0;
    checount = maxplayers;
    while (checount > 0) {
        const int p = rand() % maxplayers;
        if (!check[p]) {
            check[p] = 1;
            checount--;
            check_player_change_teams(p);
        }
    }
}

// Check if a player wants to change teams and if yes, try to fullfill the wish.
void Server::check_player_change_teams(int pid) {
    if (!world.player[pid].used || !world.player[pid].want_change_teams)
        return;
    if (get_time() < world.player[pid].team_change_time) {
        world.player[pid].team_change_pending = true;
        return;
    }

    //count players in each team
    int tc[2] = { 0, 0 };
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            tc[i / TSIZE]++;

    //check if team changing happens: calculate delta TARGET TEAM - MY TEAM
    const int teamdelta = tc[1 - pid / TSIZE] - tc[pid / TSIZE];

    // target team with MORE players: do not move
    if (teamdelta > 0)
        return;
    // target team with 2 players less: move player without a trade
    if (teamdelta <= -2)
        for (int i = 0; i < maxplayers; i++)
            if (i / TSIZE != pid / TSIZE)
                if (!world.player[i].used) {
                    move_player(pid, i);
                    return;
                }
    // Find a trade.
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && i / TSIZE != pid / TSIZE && world.player[i].want_change_teams) {
            swap_players(pid, i);
            return;
        }

    // If the old team has one player more, move the player.
    if (teamdelta == -1)
        for (int i = 0; i < maxplayers; i++)
            if (i / TSIZE != pid / TSIZE && !world.player[i].used) {
                move_player(pid, i);
                return;
            }
}

//move player - move player (f rom) to empty position (t o)
void Server::move_player(int f, int t) {
    //UGLY HACK
    if (!check[t]) {
        check[t] = 1;
        checount--;
    }

    fav_colors[f / TSIZE][world.player[f].color()] = false;
    world.player[f].set_color(-1);

    world.dropFlagIfAny(f, true);
    if (world.player[f].health > 0)
        world.resetPlayer(f);   // no need to tell clients because it's inferred by team_change message

    //copy to t
    world.player[t] = world.player[f];

    //change rockets owner from f to t
    world.changeRocketsOwner(f, t);

    //remove f
    game_remove_player(f, false);

    world.player[t].id = t;
    world.player[t].set_team(t / TSIZE);

    //I really don't want to change teams anymore.
    world.player[t].want_change_teams = false;
    world.player[t].team_change_time = get_time() + 10.0;       //10 secs interval

    check_fav_colors(t);

    //update t
    network.move_update_player(t);
    network.broadcast_team_change(f, t, false);
}

//swap players - both are valid players
void Server::swap_players(int a, int b) {
    fav_colors[a / TSIZE][world.player[a].color()] = false;
    fav_colors[b / TSIZE][world.player[b].color()] = false;
    world.player[a].set_color(-1);
    world.player[b].set_color(-1);

    world.dropFlagIfAny(a, true);
    world.dropFlagIfAny(b, true);
    if (world.player[a].health > 0)
        world.resetPlayer(a);   // no need to tell clients because it's inferred by team_change message
    if (world.player[b].health > 0)
        world.resetPlayer(b);   // no need to tell clients because it's inferred by team_change message

    swap(world.player[a], world.player[b]);
    world.swapRocketOwners(a, b);

    world.player[a].id = a;
    world.player[b].id = b;
    world.player[a].set_team(a / TSIZE);
    world.player[b].set_team(b / TSIZE);

    // either don't want to change teams anymore
    world.player[a].want_change_teams = false;
    world.player[a].team_change_time = get_time() + 10.0;       //10 secs interval
    world.player[b].want_change_teams = false;
    world.player[b].team_change_time = get_time() + 10.0;       //10 secs interval

    check_fav_colors(a);
    check_fav_colors(b);

    network.move_update_player(a);
    network.move_update_player(b);
    network.broadcast_team_change(a, b, true);
}

void Server::set_fav_colors(int pid, const vector<char>& colors) {
    if (world.player[pid].used)
        world.player[pid].set_fav_colors(colors);
}

void Server::check_fav_colors(int pid) {
    ServerPlayer& player = world.player[pid];
    if (!player.used)
        return;
    const int team = pid / TSIZE;
    const vector<char>& player_colors = player.fav_colors();
    // check favourite colours
    for (vector<char>::const_iterator col = player_colors.begin(); col != player_colors.end(); col++) {
        nAssert(*col < static_cast<int>(fav_colors[team].size()));
        if (player.color() == *col)
            return;
        else if (!fav_colors[team][*col]) {
            if (player.color() != -1)
                fav_colors[team][player.color()] = false;
            player.set_color(*col);
            fav_colors[team][player.color()] = true;
            return;
        }
    }
    // if no favourites free, check all colours
    for (int i = 0; i < static_cast<int>(fav_colors[team].size()); i++)
        if (!fav_colors[team][i]) {
            if (player.color() != -1)
                fav_colors[team][player.color()] = false;
            player.set_color(i);
            fav_colors[team][player.color()] = true;
            return;
        }
    nAssert(0);     // should never go here
}

void Server::sendMessage(int pid, Message_type type, const std::string& msg) {
    network.player_message(pid, type, msg);
}

//refresh team ratings
void Server::refresh_team_score_modifiers() {
    double raw[2] = { 0.0, 0.0 };
    int players[2] = { 0, 0 };

    //somatorio raw ratings
    for (int p = 0; p < maxplayers; p++)
        if (world.player[p].used) {
            // use "1.0" rating for anybody with less than 100 positive points
            if (client[world.player[p].cid].score < minimum_positive_score_for_ranking)
                raw[p / TSIZE] += 1.0;
            else
                raw[p / TSIZE] += (client[world.player[p].cid].score + 1.0) / (client[world.player[p].cid].neg_score + 1.0);
            ++players[p / TSIZE];
        }

    if (!players[0] || !players[1]) {
        team_smul[0] = team_smul[1] = 0;
        return;
    }

    if (worldConfig.respawn_balancing_time >= 5.) {   // artificial treshold for "enough"
        // assume team size to make no difference
        raw[0] /= players[0];
        raw[1] /= players[1];
    }

    //modifiers
    team_smul[0] = bound(raw[1] / raw[0], .3333, 3.);
    team_smul[1] = bound(raw[0] / raw[1], .3333, 3.);
}

//score!
void Server::score_frag(int pid, int amount) {
    world.player[pid].stats().add_frag(amount);

    const int cid = world.player[pid].cid;

    // add tournament scoring delta if all criteria for tournament scoring are satisfied
    if (tournament && network.numDistinctClients() >= 4 && client[cid].current_participation) {
        refresh_team_score_modifiers();
        client[cid].fdp += amount * team_smul[pid / TSIZE];
        client[cid].delta_score = static_cast<int>(client[cid].fdp);
    }
}

//score! NEG FRAG (v0.4.8)
void Server::score_neg(int p, int amount) {
    const int cid = world.player[p].cid;

    // add tournament scoring delta if all criteria for tournament scoring are satisfied
    if (tournament && network.numDistinctClients() >= 4 && client[cid].current_participation) {
        client[cid].fdn += amount;  // not affected by team modifier
        client[cid].neg_delta_score = static_cast<int>(client[cid].fdn);
    }
}

bool Server::trySetMaxplayers(int val) {
    if (val != maxplayers && network.get_player_count() != 0) {
        log.error(_("Can't change max_players while players are connected."));
        return false;
    }
    setMaxPlayers(val);
    return true;
}

bool checkMaxplayerSetting(int val) { return (val >= 2 && val <= MAX_PLAYERS && val % 2 == 0); }    // helper for load_game_mod
bool checkForceIpValue(const std::string& val) { return isValidIP(val); }

bool Server::setForceIP(const std::string& val) { extConfig.ipAddress = val; return true; }
void Server::setRandomMaprot(int val) { random_maprot = (val == 1); random_first_map = (val == 2); }

void Server::load_game_mod(bool reload) {
    RedirectToFun1<bool, const std::string&> checkForceIP(checkForceIpValue);
    RedirectToMemFun1<Server, bool, const std::string&> setForceIP(this, &Server::setForceIP);

    RedirectToMemFun1<ServerNetworking, void, const string&> setHostname(&network, &ServerNetworking::set_hostname);
    RedirectToMemFun1<ServerNetworking, void, const string&> setServerPassword(&network, &ServerNetworking::set_server_password);

    RedirectToFun1<bool, int> checkMaxplayer(checkMaxplayerSetting);
    RedirectToMemFun1<Server, bool, int> tryMaxplayer(this, &Server::trySetMaxplayers);

    RedirectToMemFun1<ServerNetworking, void, const string&> addWebServer(&network, &ServerNetworking::add_web_server);
    RedirectToMemFun1<ServerNetworking, void, const string&> setWebScript(&network, &ServerNetworking::set_web_script);
    RedirectToMemFun1<ServerNetworking, void, const string&> setWebAuth(&network, &ServerNetworking::set_web_auth);
    RedirectToMemFun1<ServerNetworking, void, int> setWebRefresh(&network, &ServerNetworking::set_web_refresh);

    RedirectToMemFun1<Server, void, int> setRandomMaprot(this, &Server::setRandomMaprot);

    RedirectToMemFun1<ServerNetworking, void, int> setJoinStart(&network, &ServerNetworking::set_join_start);
    RedirectToMemFun1<ServerNetworking, void, int> setJoinEnd(&network, &ServerNetworking::set_join_end);
    RedirectToMemFun1<ServerNetworking, void, const string&> setJoinLimitMessage(&network, &ServerNetworking::set_join_limit_message);

    GamemodSetting* portSetting, * ipSetting, * privSetting;
    if (reload) {
        portSetting = new GS_DisallowRunning("server_port");
        ipSetting   = new GS_DisallowRunning("server_ip");
        privSetting = new GS_DisallowRunning("private_server");
    }
    else {
        if (extConfig.portForced)
            portSetting = new GS_Ignore ("server_port");
        else
            portSetting = new GS_Int    ("server_port",     &extConfig.port, 1, 65535);
        if (extConfig.ipForced)
            ipSetting = new GS_Ignore   ("server_ip");
        else
            ipSetting = new GS_CheckForwardStr("server_ip", _("IP address without :port"), checkForceIP, setForceIP);
        if (extConfig.privSettingForced)
            privSetting = new GS_Ignore ("private_server");
        else
            privSetting = new GS_Boolean("private_server",  &extConfig.privateserver);
    }

    typedef std::auto_ptr<GamemodSetting> PT;
    PT hack(0); // avoid GCC bug http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12883
    PT settings[] = {
        PT(portSetting),
        PT(ipSetting),
        PT(privSetting),
        PT(new GS_Double    ("friction",                &world.physics.fric)),
        PT(new GS_Double    ("drag",                    &world.physics.drag)),
        PT(new GS_Double    ("acceleration",            &world.physics.accel)),
        PT(new GS_Double    ("brake_acceleration",      &world.physics.brake_mul, .01)),
        PT(new GS_Double    ("turn_acceleration",       &world.physics.turn_mul,  .01)),
        PT(new GS_Double    ("run_acceleration",        &world.physics.run_mul)),
        PT(new GS_Double    ("turbo_acceleration",      &world.physics.turbo_mul)),
        PT(new GS_Double    ("flag_acceleration",       &world.physics.flag_mul)),
        PT(new GS_Double    ("rocket_speed",            &world.physics.rocket_speed)),
        PT(new GS_Collisions("player_collisions",       &world.physics.player_collisions)),
        PT(new GS_Percentage("friendly_fire",           &world.physics.friendly_fire)),
        PT(new GS_Percentage("friendly_deathbringer",   &world.physics.friendly_db)),
        PT(new GS_Map       ("map",                     &maprot)),
        PT(new GS_PowerupNum("pups_min",                &pupConfig.pups_min, &pupConfig.pups_min_percentage)),
        PT(new GS_PowerupNum("pups_max",                &pupConfig.pups_max, &pupConfig.pups_max_percentage)),
        PT(new GS_Int       ("pups_respawn_time",       &pupConfig.pups_respawn_time,       0)),
        PT(new GS_Int       ("pup_chance_shield",       &pupConfig.pup_chance_shield,       0)),
        PT(new GS_Int       ("pup_chance_turbo",        &pupConfig.pup_chance_turbo,        0)),
        PT(new GS_Int       ("pup_chance_shadow",       &pupConfig.pup_chance_shadow,       0)),
        PT(new GS_Int       ("pup_chance_power",        &pupConfig.pup_chance_power,        0)),
        PT(new GS_Int       ("pup_chance_weapon",       &pupConfig.pup_chance_weapon,       0)),
        PT(new GS_Int       ("pup_chance_megahealth",   &pupConfig.pup_chance_megahealth,   0)),
        PT(new GS_Int       ("pup_chance_deathbringer", &pupConfig.pup_chance_deathbringer, 0)),
        PT(new GS_Ulong     ("time_limit",              &worldConfig.time_limit, 0, GS_Ulong::lim::max(), 60 * 10)),    // convert minutes to frames
        PT(new GS_Ulong     ("extra_time",              &worldConfig.extra_time, 0, GS_Ulong::lim::max(), 60 * 10)),    // convert minutes to frames
        PT(new GS_Boolean   ("sudden_death",            &worldConfig.sudden_death)),
        PT(new GS_Int       ("game_end_delay",          &game_end_delay, 0)),
        PT(new GS_Int       ("capture_limit",           &worldConfig.capture_limit, 0)),
        PT(new GS_Int       ("win_score_difference",    &worldConfig.win_score_difference, 1)),
        PT(new GS_Double    ("flag_return_delay",       &worldConfig.flag_return_delay, 0)),
        PT(new GS_Boolean   ("lock_team_flags",         &worldConfig.lock_team_flags)),
        PT(new GS_Boolean   ("lock_wild_flags",         &worldConfig.lock_wild_flags)),
        PT(new GS_Boolean   ("capture_on_team_flag",    &worldConfig.capture_on_team_flag)),
        PT(new GS_Boolean   ("capture_on_wild_flag",    &worldConfig.capture_on_wild_flag)),
        PT(new GS_Balance   ("balance_teams",           &worldConfig.balance_teams)),
        PT(new GS_ForwardStr("server_name",             setHostname)),
        PT(new GS_CheckForwardInt("max_players",        _("an even integer between 2 and $1", itoa(MAX_PLAYERS)), checkMaxplayer, tryMaxplayer)),
        PT(new GS_AddString ("welcome_message",         &welcome_message)),
        PT(new GS_AddString ("info_message",            &info_message)),
        PT(new GS_ForwardStr("server_password",         setServerPassword)),
        PT(new GS_Int       ("pup_add_time",            &pupConfig.pup_add_time, 1, 999)),
        PT(new GS_Int       ("pup_max_time",            &pupConfig.pup_max_time, 1, 999)),
        PT(new GS_Boolean   ("pup_deathbringer_switch", &pupConfig.pup_deathbringer_switch)),
        PT(new GS_Double    ("pup_deathbringer_time",   &pupConfig.pup_deathbringer_time, 1.)),
        PT(new GS_Boolean   ("pups_drop_at_death",      &pupConfig.pups_drop_at_death)),
        PT(new GS_Int       ("pups_player_max",         &pupConfig.pups_player_max, 1)),
        PT(new GS_Int       ("pup_health_bonus",        &pupConfig.pup_health_bonus, 1)),
        PT(new GS_Double    ("pup_power_damage",        &pupConfig.pup_power_damage, 0.)),
        PT(new GS_Int       ("pup_weapon_max",          &pupConfig.pup_weapon_max, 1, 9)),
        PT(new GS_Boolean   ("pup_shield_one_hit",      &pupConfig.pup_shield_one_hit)),
        PT(new GS_ForwardInt("random_maprot",           setRandomMaprot, 0, 2)),
        PT(new GS_Int       ("vote_block_time",         &vote_block_time, 0, GS_Int::lim::max(), 60 * 10)), // convert minutes to frames
        PT(new GS_Boolean   ("require_specific_map_vote",   &require_specific_map_vote)),
        PT(new GS_Int       ("idlekick_time",           &idlekick_time, 10, GS_Int::lim::max(), 10, 0, true)),  // convert seconds to frames; special setting: allow 0 that is outside the normal range
        PT(new GS_Int       ("idlekick_playerlimit",    &idlekick_playerlimit, 1, MAX_PLAYERS)),
        PT(new GS_Double    ("respawn_time",            &worldConfig.respawn_time, 0.)),
        PT(new GS_Double    ("waiting_time_deathbringer",   &worldConfig.waiting_time_deathbringer, 0.)),
        PT(new GS_Double    ("respawn_balancing_time",  &worldConfig.respawn_balancing_time, 0.)),
        PT(new GS_Int       ("pup_shadow_invisibility", &worldConfig.shadow_minimum, 0, 1, -WorldSettings::shadow_minimum_normal, +WorldSettings::shadow_minimum_normal)),  // 0->smn, 1->0
        PT(new GS_Int       ("rocket_damage",           &worldConfig.rocket_damage, 0)),
        PT(new GS_Boolean   ("sayadmin_enabled",        &sayadmin_enabled)),
        PT(new GS_String    ("sayadmin_comment",        &sayadmin_comment)),
        PT(new GS_Boolean   ("tournament",              &tournament)),
        PT(new GS_Int       ("save_stats",              &save_stats, 0, MAX_PLAYERS)),
        PT(new GS_ForwardInt("join_start",              setJoinStart, 0, 24 * 3600 - 1)),
        PT(new GS_ForwardInt("join_end",                setJoinEnd, 0, 24 * 3600 - 1)),
        PT(new GS_ForwardStr("join_limit_message",      setJoinLimitMessage)),
        PT(new GS_String    ("server_website",          &server_website_url)),
        PT(new GS_ForwardStr("web_server",              addWebServer)),
        PT(new GS_ForwardStr("web_script",              setWebScript)),
        PT(new GS_ForwardStr("web_auth",                setWebAuth)),
        PT(new GS_ForwardInt("web_refresh",             setWebRefresh, 1)),
        PT(0)
    };
    const string filename = wheregamedir + "config" + directory_separator + "gamemod.txt";
    ifstream in(filename.c_str());
    if (in) {
        log("Loading game mod: '%s'", filename.c_str());
        string line;
        while (getline_skip_comments(in, line)) {
            string cmd, value;
            istringstream ist(line);
            ist >> cmd;
            ist.ignore();
            getline(ist, value);
            for (int si = 0;; ++si) {
                if (&*settings[si] == 0) {  // end of settings marker
                    log.error(_("Unrecognized gamemod setting: '$1'.", cmd));
                    break;
                }
                if (settings[si]->matchCommand(cmd)) {
                    settings[si]->set(log, value);  // ignore return value; the status is logged
                    break;
                }
            }
        }

        const int chanceSum = pupConfig.pup_chance_shield + pupConfig.pup_chance_turbo + pupConfig.pup_chance_shadow + pupConfig.pup_chance_power
                        + pupConfig.pup_chance_weapon + pupConfig.pup_chance_megahealth + pupConfig.pup_chance_deathbringer;
        if (chanceSum == 0)
            pupConfig.pups_max = 0;

        if ((pupConfig.pups_min_percentage == pupConfig.pups_max_percentage && pupConfig.pups_min > pupConfig.pups_max) ||
                pupConfig.pups_max == 0)    // if they are in different units, only the value of 0 is comparable
            pupConfig.pups_min = pupConfig.pups_max;

        if (!server_website_url.empty())
            info_message.push_back(string() + "Website: " + server_website_url);

        log("Game mod file read.");
        in.close();
    }
    else
        log.error(_("Can't open game mod file '$1'.", filename));
    world.setConfig(worldConfig, pupConfig);
}

//load a map from the rotation list
bool Server::load_rotation_map(int pos) {
    const bool ok = world.load_map(SERVER_MAPS_DIR, maprot[pos].file);
    if (!ok)
        return false;
    log("Map number %i: '%s'", pos, maprot[pos].file.c_str());
    // Check the flag settings and remove the useless flags (only if there are three kinds of flags).
    if (world.wild_flags.empty() || world.teams[0].flags().empty() || world.teams[1].flags().empty())
        return true;
    if (worldConfig.lock_team_flags && (worldConfig.lock_wild_flags || !worldConfig.capture_on_team_flag)) {
        world.remove_team_flags(0);
        world.remove_team_flags(1);
        world.remove_team_flags(2);
    }
    else if (worldConfig.lock_wild_flags && !worldConfig.capture_on_wild_flag)
        world.remove_team_flags(2);
    return true;
}

bool Server::server_next_map(int reason) {
    network.update_serverinfo();

    nAssert(!maprot.empty());

    for (int i = 0; i < maxplayers; ++i)
        world.player[i].stats().finish_stats(get_time());

    if (save_stats && network.get_player_count() >= save_stats)
        world.save_stats("server_stats", current_map().title);

    vector<int> winners;
    int maxVotes = 0;
    NLulong longest_time = world.frame;
    for (int m = 0; m < static_cast<int>(maprot.size()); ++m) {
        if (maprot[m].votes < maxVotes)
            continue;
        if (maprot[m].votes > maxVotes) {
            maxVotes = maprot[m].votes;
            winners.clear();
            longest_time = maprot[m].last_game;
        }
        if (maprot[m].last_game > longest_time)
            continue;
        if (maprot[m].last_game < longest_time) {
            winners.clear();
            longest_time = maprot[m].last_game;
        }
        winners.push_back(m);
    }
    if (maxVotes == 0)
        currmap = (currmap + 1) % maprot.size();
    else {
        if (winners.size() > 1) {
            vector<int>::iterator it = find(winners.begin(), winners.end(), currmap);
            if (it != winners.end())
                winners.erase(it);
        }
        currmap = winners[rand() % winners.size()];
    }
    // clear votes for the current map
    for (int p = 0; p < maxplayers; ++p) {
        world.player[p].want_map_exit = false;
        if (world.player[p].mapVote == currmap)
            world.player[p].mapVote = -1;
    }
    maprot[currmap].votes = 0;
    last_vote_announce_votes = last_vote_announce_needed = 0;
    next_vote_announce_frame = 0;   // let a new announcement be made as soon as someone votes
    maprot[currmap].last_game = world.frame;

    if (!load_rotation_map(currmap))
        return reset_settings(true);    // re-initialize map-list (and other settings as a side-effect); it calls back this function so the end-part has already executed

    // notify all players
    network.broadcast_map_change_message(reason, maprot[currmap].file.c_str());
    // broadcast stats to all players for stats saving
    for (int i = 0; i < maxplayers; ++i) {
        const ServerPlayer& pl = world.player[i];
        if (!pl.used)
            continue;
        if (pl.oldfrags != pl.stats().frags())
            network.sendFragUpdate(i, pl.stats().frags());
        // no need to update oldfrags, since the stats are next cleared
        network.broadcast_movements_and_shots(pl); // player's stats to everyone
        network.send_team_movements_and_shots(pl); // team stats to player
    }
    network.broadcast_stats_ready();

    // Server is showing gameover plaque. Nobody should move or receive world frames.
    gameover = true;
    gameover_time = get_time() + game_end_delay;        // timeout for gameover plaque

    ctf_game_restart();

    return true;
}

//check map exit by vote
void Server::check_map_exit() {
    int num_for = 0, num_against = 0;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            if (world.player[i].want_map_exit && (!require_specific_map_vote || world.player[i].mapVote != -1))
                num_for++;
            else
                num_against++;
        }

    // this could be done elsewhere, but this function is called whenever votes change
    for (int m = 0; m < static_cast<int>(maprot.size()); ++m)
        maprot[m].votes = 0;
    for (int p = 0; p < maxplayers; ++p)
        if (world.player[p].used && world.player[p].mapVote != -1)
            ++maprot[world.player[p].mapVote].votes;

    if (num_for > num_against && (world.getMapTime() >= vote_block_time || num_against == 0))
        server_next_map(NEXTMAP_VOTE_EXIT); // ignore return value
}

//----- THE REST  ----------------

bool Server::reset_settings(bool reload) {  // set reload if reset_settings has already been called to preserve map and votes, and ensure that fixed values aren't changed
    authorizations.load();

    string currMapFile;
    list< pair<int, string> > oldVotes;    // pair<pid, map-filename>
    if (reload) {
        currMapFile = maprot[currmap].file;
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && world.player[i].mapVote != -1)
                oldVotes.push_back(pair<int, string>(i, maprot[world.player[i].mapVote].file));
    }

    world.physics = PhysicalSettings(); // default values
    maprot.clear();
    pupConfig.reset();
    worldConfig.reset();
    currmap = 0;

    network.set_hostname("");
    network.set_server_password("");

    game_end_delay = 5;
    random_maprot = false;
    vote_block_time = 0;    // no limit
    require_specific_map_vote = false;
    idlekick_time = 120 * 10;   // 2 minutes in frames
    idlekick_playerlimit = 4;

    welcome_message.clear();
    info_message.clear();

    sayadmin_comment.clear();
    sayadmin_enabled = false;

    server_website_url.clear();

    tournament = true;
    save_stats = 0;

    network.clear_web_servers();
    network.set_web_refresh(2);

    // load server configuration from gamemod.txt
    load_game_mod(reload);

    if (maprot.empty()) {
        // did not specify maps, scan "maps/" folder for .txt map files
        FileFinder* mapFiles = platMakeFileFinder(wheregamedir + SERVER_MAPS_DIR, ".txt", false);
        while (mapFiles->hasNext()) {
            string mapName = FileName(mapFiles->next()).getBaseName();
            MapInfo mi;
            if (mi.load(log, mapName)) {
                maprot.push_back(mi);
                log("Added '%s' to map rotation", mapName.c_str());
            }
            else
                log.error(_("Can't add '$1' to map rotation.", mapName));
        }
        delete mapFiles;
    }

    if (maprot.empty()) {
        log.error(_("No maps for rotation."));
        abortFlag = true;
        return false;
    }

    if (random_maprot)
        random_shuffle(maprot.begin(), maprot.end());

    if (reload) {   // preserve selected map and restore map votes where possible
        network.broadcast_reset_map_list(); // must be before new votes are sent (right below)
        for (int i = 0; i < maxplayers; i++)
            if (world.player[i].used) {
                world.player[i].current_map_list_item = 0;
                network.send_server_settings(world.player[i]);
            }

        currmap = -1;   // flag so we know if it has changed or not
        for (int mapi = 0; mapi < (int)maprot.size(); ++mapi) {
            if (maprot[mapi].file == currMapFile)
                currmap = mapi;
            for (list< pair<int, string> >::iterator vi = oldVotes.begin(); vi != oldVotes.end(); ) {
                if (vi->second == maprot[mapi].file) {
                    ServerPlayer& pl = world.player[vi->first];
                    pl.mapVote = mapi;
                    network.send_map_vote(pl);  // don't care if index changed: client has zeroed its vote at reset_map_list and must be re-told it
                    ++maprot[mapi].votes;
                    vi = oldVotes.erase(vi);
                }
                else
                    ++vi;
            }
        }
        if (currmap == -1)  // not found
            server_next_map(NEXTMAP_VOTE_EXIT);
        else
            network.broadcast_current_map(currmap);
        // what is left are players whose voted map was erased from the list
        for (list< pair<int, string> >::iterator vi = oldVotes.begin(); vi != oldVotes.end(); ++vi)
            world.player[vi->first].mapVote = -1;   // the client knows this because of broadcast_reset_map_list above
    }
    else if (random_first_map)
        currmap = rand() % maprot.size();
    network.send_map_time(-1);  // broadcast time to all, in case time limit has been changed
    return true;
}

//start server
bool Server::start(int target_maxplayers) {
    nAssert(target_maxplayers >= 2 && target_maxplayers <= MAX_PLAYERS && target_maxplayers % 2 == 0);

    // Set maxplayers, could be reset by gamemod setting.
    setMaxPlayers(target_maxplayers);

    //reset client_c struct (closes files...)
    for (int i = 0; i < MAX_PLAYERS; i++)
        client[i].reset();

    gameover = false;

    for (int i = 0; i < MAX_PLAYERS; i++)
        world.player[i].clear(false, i, 0, "", i / TSIZE);  // 0 : fake cid

    if (!reset_settings(false))
        return false;
    if (!load_rotation_map(currmap))
        return false;
    if (!network.start())   // this must be last, because network.stop() must always be called if start() succeeds; also, priv, port and ip must be final
        return false;

    if (threadLock)
        threadLockMutex.lock();

    ctf_game_restart();
    world.reset_time();

    network.update_serverinfo();

    if (threadLock)
        threadLockMutex.unlock();

    abortFlag = false;
    return true;
}

int Server::getLessScoredTeam() const {
    if (team_smul[0] > team_smul[1])
        return 0;
    else if (team_smul[1] > team_smul[0])
        return 1;
    else if (world.teams[0].score() < world.teams[1].score())
        return 0;
    else if (world.teams[1].score() < world.teams[0].score())
        return 1;
    else
        return rand() % 2;
}

void Server::game_remove_player(int pid, bool removeClient) {
    if (world.player[pid].color() != -1)
        fav_colors[pid / TSIZE][world.player[pid].color()] = false;
    if (removeClient)
        client[world.player[pid].cid].reset();
    network.removePlayer(pid);
    world.removePlayer(pid);
}

void Server::disconnectPlayer(int pid, Disconnect_reason reason) {
    network.disconnect_client(world.player[pid].cid, 2, reason);
}

void Server::nameChange(int id, int pid, const string& tempname, const std::string& password) {
    if (tempname == world.player[pid].name)
        return;
    //name change flooding protection
    if (get_time() < world.player[pid].waitnametime)
        return;

    //FLUSH PENDING REPORTS TO MASTER IF token_have/token_valid !!!
    network.client_report_status(id);

    //name changed -- this means that the player is NOT REGISTERED
    //  anymore for recording statistics
    client[id].token_have = false;

    //check if it's the first name information from client. then it
    // must have just entered the game
    const bool entered_game = world.player[pid].name.empty();

    if (!check_name(tempname)) {
        log("Kicked player %d for client misbehavior: attempted invalid name '%s'.", pid, tempname.c_str());
        disconnectPlayer(pid, disconnect_client_misbehavior);
        return;
    }
    else {
        if (authorizations.checkNamePassword(tempname, password)) {
            world.player[pid].name = tempname;
            world.player[pid].waitnametime = get_time() + 1.0;
        }
        else if (entered_game) {
            log("Kicked player %d for client misbehavior: authorization changed between entering the game and first name change.", pid);
            disconnectPlayer(pid, disconnect_client_misbehavior);
            return;
        }
        else {
            if (!password.empty())
                log.security("Wrong player password. Name \"%s\", password \"%s\" tried from %s.",
                                tempname.c_str(), password.c_str(), addressToString(network.get_client_address(id)).c_str());
            network.sendNameAuthorizationRequest(pid);
            return;
        }
    }

    network.broadcast_player_name(pid); // must be before new_player_to_admin_shell
    if (entered_game)
        network.new_player_to_admin_shell(pid);

    // token removed; possibly authorized and/or admin
    network.broadcast_player_crap(pid);
}

class PlayerMessager : public LineReceiver {
    Server& host;
    int player;
    Message_type type;

public:
    PlayerMessager(Server& server, int pid, Message_type mtype) : host(server), player(pid), type(mtype) { }
    PlayerMessager& operator()(const std::string& str) { host.sendMessage(player, type, str); return *this; }
};

bool Server::isLocallyAuthorized(int pid) const {
    return world.player[pid].localIP || authorizations.isProtected(world.player[pid].name); // must have authorized because otherwise couldn't use the name
}

bool Server::isAdmin(int pid) const {
    if (world.player[pid].localIP)
        return true;
    if (!authorizations.isAdmin(world.player[pid].name))
        return false;
    const ClientData& cld = client[world.player[pid].cid];
    return (cld.token_have && cld.token_valid) || isLocallyAuthorized(pid);
}

void Server::chat(int pid, const char* sbuf) {
    // handle 'console' commands
    if (sbuf[0] == '/') {
        const bool admin = isAdmin(pid);

        const char* pCommand = sbuf + 1;
        char cbuf[30];
        for (int ci = 0;; ++ci, ++pCommand) {
            if (ci == 29) {
                cbuf[29] = '\0';
                break;
            }
            if (*pCommand == ' ') {
                cbuf[ci] = '\0';
                ++pCommand;
                break;
            }
            cbuf[ci] = *pCommand;
            if (*pCommand == '\0')
                break;
        }
        // cbuf contains the first word, pCommand points to arguments, if any
        if (!strcmp(cbuf, "help")) {
            network.player_message(pid, msg_header, "Console commands available on this server:");
            network.player_message(pid, msg_server, "/help       this screen");
            if (!info_message.empty())
                network.player_message(pid, msg_server, "/info       information about this server");
            network.player_message(pid, msg_server, "/time       check server uptime, current map time and time left on the map");
            if (sayadmin_enabled) {
                ostringstream ostr;
                ostr << "/sayadmin   send a message to the server admin";
                if (sayadmin_comment.length())
                    ostr << " (" << sayadmin_comment << ')';
                network.player_message(pid, msg_server, ostr.str());
            }
            if (admin) {
                network.player_message(pid, msg_header, "Admin commands:");
                network.player_message(pid, msg_server, "/list       get a list of player IDs");
                network.player_message(pid, msg_server, "/kick n     kick player with ID n");
                network.player_message(pid, msg_server, "/ban n t    ban player with ID n for t minutes (default: 60)");
                network.player_message(pid, msg_server, "/mute n     mute player with ID n");
                network.player_message(pid, msg_server, "/smute n    silently mute player with ID n (crude!)");
                network.player_message(pid, msg_server, "/unmute n   cancel muting of player with ID n");
                network.player_message(pid, msg_server, "/forcemap   restart the game and change map if you've voted for one");
            }
        }
        else if (!strcmp(cbuf, "info") && !info_message.empty()) {
            network.player_message(pid, msg_header, info_message.front());
            for (vector<string>::const_iterator line = info_message.begin() + 1; line != info_message.end(); line++)
                network.player_message(pid, msg_server, *line);
        }
        else if (!strcmp(cbuf, "sayadmin") && sayadmin_enabled) {
            if (strspnp(pCommand, " ")) {
                ofstream log((wheregamedir + "log" + directory_separator + "sayadmin.log").c_str(), ios::out | ios::app);
                log << date_and_time() << "  " << world.player[pid].name << ": " << pCommand << endl;
                network.forwardSayadminMessage(world.player[pid].cid, pCommand);
                network.player_message(pid, msg_server, "Your message has been logged. Thank you for your feedback!");
            }
            else
                network.player_message(pid, msg_server, "For example to send \"Hello!\", type /sayadmin Hello!");
        }
        else if (!strcmp(cbuf, "time")) {
            PlayerMessager pm(*this, pid, msg_server);
            world.printTimeStatus(pm);
        }
        else if (!strcmp(cbuf, "list") && admin) {
            network.player_message(pid, msg_header, "Players on server: ID, login flags, name");
            for (int ppid = 0; ppid < MAX_PLAYERS; ) {
                char buf[100];
                int bufi = 0;
                for (int onrow = 0; onrow < 3 && ppid < MAX_PLAYERS; ++ppid)
                    if (world.player[ppid].used) {
                        const char mute = world.player[ppid].muted == 0 ? ' ' : world.player[ppid].muted == 1 ? 'm' : 's';
                        platSnprintf(buf + bufi, 27, "%2d %4s%c %-17s", ppid, world.player[ppid].reg_status.strFlags().c_str(), mute, world.player[ppid].name.c_str());
                        bufi += 26;
                        ++onrow;
                    }
                if (bufi > 0)
                    network.player_message(pid, msg_server, buf);
            }
        }
        else if (admin && (!strcmp(cbuf, "kick") || !strcmp(cbuf, "ban") ||
                    !strcmp(cbuf, "mute") || !strcmp(cbuf, "smute") || !strcmp(cbuf, "unmute"))) {
            istringstream command(pCommand);
            int ppid;
            int time;   // used only for bans
            command >> ppid;
            bool ok = command;
            command >> time;
            if (!strcmp(cbuf, "ban")) {
                if (!command)
                    time = 60;  // default: 60 minutes
                if (!command && !command.eof())
                    ok = false;
            }
            else if (command || !command.eof())
                ok = false;
            if (!ok)
                network.plprintf(pid, msg_warning, "Syntax error. Expecting \"/%s ID%s\".", cbuf, !strcmp(cbuf, "ban") ? " [minutes]" : "");
            else if (ppid < 0 || ppid >= MAX_PLAYERS || !world.player[ppid].used)
                network.player_message(pid, msg_warning, "No such player. Type /list for a list of IDs.");
            else {  // syntax OK
                if (!strcmp(cbuf, "kick"))
                    kickPlayer(ppid, pid);
                else if (!strcmp(cbuf, "ban")) {
                    if (time <= 0 || time > 60 * 24 * 7)    // allow at most a weeks ban (a bit over 10000 minutes)
                        network.player_message(pid, msg_warning, "The ban time must be more than 0 and at most 10 000 minutes (1 week).");
                    else
                        banPlayer(ppid, pid, time);
                }
                else if (!strcmp(cbuf, "mute"))
                    mutePlayer(ppid, 1, pid);
                else if (!strcmp(cbuf, "smute"))
                    mutePlayer(ppid, 2, pid);
                else if (!strcmp(cbuf, "unmute"))
                    mutePlayer(ppid, 0, pid);
                else
                    nAssert(0);
            }
        }
        else if (!strcmp(cbuf, "forcemap") && admin) {
            // Make sure that these messages match with the ones in client.cpp.
            if (world.player[pid].mapVote != -1 && world.player[pid].mapVote != currmap) {
                network.bprintf(msg_server, "%s decided it's time for a map change.", world.player[pid].name.c_str());
                logAdminAction(pid, "forced a map change");
                maprot[world.player[pid].mapVote].votes = 99;
            }
            else {
                network.bprintf(msg_server, "%s decided it's time for a restart.", world.player[pid].name.c_str());
                logAdminAction(pid, "forced a restart");
                maprot[currmap].votes = 99;
            }
            server_next_map(NEXTMAP_VOTE_EXIT); // ignore return value
        }
        else
            network.plprintf(pid, msg_warning, "Unknown command %s. Type /help for a list.", cbuf);
    }
    else if (strspnp(sbuf, " ") != NULL) {  // ignore messages that are all spaces
        //talk flood protection
        world.player[pid].talk_temp += world.player[pid].talk_hotness;
        world.player[pid].talk_hotness += 3.0;
        if (world.player[pid].talk_temp > 18.0)
            world.player[pid].talk_temp = 18.0;
        if (world.player[pid].talk_hotness > 6.0)
            world.player[pid].talk_hotness = 6.0;
        if (world.player[pid].talk_temp > 10.0) {
            world.player[pid].talk_temp = 18.0;
            network.send_too_much_talk(pid);
        }
        else if (world.player[pid].muted == 1)
            network.send_mute_notification(pid);
        else if (isFlood(sbuf))
            ;   // Notify?
        else {
            ostringstream msg;
            msg << world.player[pid].name << ": ";
            if (sbuf[0] == '.') {   // team message
                msg << trim(sbuf + 1);
                if (world.player[pid].muted == 2)
                    network.player_message(pid, msg_team, msg.str());
                else
                    network.broadcast_team_message(pid / TSIZE, msg.str());
            }
            else {                  // regular message
                msg << trim(sbuf);
                if (world.player[pid].muted == 2)
                    network.player_message(pid, msg_normal, msg.str());
                else
                    network.broadcast_text(msg_normal, msg.str());
            }
        }
    }
}

bool Server::changeRegistration(int id, const string& token) {
    const int intoken = atoi(token.c_str());
    if (intoken == client[id].intoken)
        return false;

    // v0.4.9 FIX : IF HAD previous token have/valid, then FLUSH his stats
    network.client_report_status(id);

    strcpy(client[id].token, token.c_str());
    client[id].intoken = intoken;

    // NEW (or first) REGISTRATION -- reset player report / stop reporting his old ID
    client[id].neg_delta_score = 0;
    client[id].delta_score = 0;
    client[id].fdp = 0.0;
    client[id].fdn = 0.0;
    client[id].score = 0;
    client[id].neg_score = 0;
    client[id].rank = 0;

    client[id].token_have = !token.empty(); //token set
    client[id].token_valid = false; //BUT not validated yet

    network.broadcast_player_crap(network.getPid(id));

    return client[id].token_have;
}

void Server::simulate_and_broadcast_frame() {
    //check end of gameover plaque
    if (gameover)
        if (gameover_time < get_time()) {
            gameover = false;
            world.reset_time();
            network.sendStartGame();
        }
    if (!gameover)
        world.simulateFrame();

    if (world.frame >= next_vote_announce_frame) {  // announce voting status
        int votes = 0;
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && world.player[i].want_map_exit && (!require_specific_map_vote || world.player[i].mapVote != -1))
                ++votes;
        const int players = get_player_count() / 2 + 1;
        if (votes != last_vote_announce_votes || (players != last_vote_announce_needed && votes != 0)) {
            last_vote_announce_votes = votes;
            last_vote_announce_needed = players;
            next_vote_announce_frame = world.frame + voteAnnounceInterval * 10;
            int voteblock;
            if (world.getMapTime() < vote_block_time)
                voteblock = (vote_block_time - world.getMapTime() + 5) / 10;
            else
                voteblock = 0;
            network.broadcast_map_change_info(votes, players, voteblock);
        }
        if (world.getMapTime() == vote_block_time)
            check_map_exit();
    }
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used) {
            if (world.player[i].kickTimer) {
                --world.player[i].kickTimer;
                if (world.player[i].kickTimer == 0)
                    disconnectPlayer(i, disconnect_kick);
                else if (world.player[i].kickTimer % 10 == 0 && world.player[i].kickTimer <= 50)
                    network.send_disconnecting_message(i, world.player[i].kickTimer / 10);
                continue;
            }
            if (idlekick_time != 0 && !world.player[i].attack && world.player[i].controls.idle() && get_player_count() >= idlekick_playerlimit) {
                ++world.player[i].idleFrames;
                int timeToKick = idlekick_time - world.player[i].idleFrames;
                if (timeToKick == 0)
                    disconnectPlayer(i, disconnect_idlekick);
                else if ((timeToKick == 60*10 && idlekick_time >= 3*60*10) ||
                         (timeToKick == 30*10 && idlekick_time >= 3*30*10) ||
                         (timeToKick == 15*10 && idlekick_time >= 2*15*10) ||
                         (timeToKick ==  5*10 && idlekick_time >= 2* 5*10)) {
                    network.send_idlekick_warning(i, timeToKick / 10);
                }
            }
            else
                world.player[i].idleFrames = 0;
        }
    network.broadcast_frame(!gameover);
}

//run something after simulate_and_broadcast
void Server::server_think_after_broadcast() {
    //check players with pending team changes
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used &&
            world.player[i].team_change_pending &&
            world.player[i].want_change_teams &&
            world.player[i].team_change_time < get_time())
                check_player_change_teams(i);
}

void Server::loop(volatile bool *quitFlag, bool quitOnEsc) {
    if (threadLock)
        threadLockMutex.lock();

    nAssert(quitFlag);
    log("at gameserver::loop()");

    world.frame = 0;    //frame to generate next

    g_timeCounter.refresh();
    double nextFrameTime = get_time() + .1;

    while (!*quitFlag && !abortFlag) {
        // generate and send frame
        simulate_and_broadcast_frame();

        // next frame
        world.frame++;

        //update wintitle
        if (world.frame % 10 == 0) {
            //update bar
            ostringstream status;
            const int errors = errorLog.numLines();
            if (errors && extConfig.showErrorCount)
                status << _("ERRORS:$1", itoa(errors)) << "  ";
            status << _("$1/$2p $3k/s v$4 port:$5",
                        itoa(network.get_player_count()), itoa(maxplayers), fcvt(network.getTraffic() / 1024, 1), GAME_VERSION, itoa(extConfig.port));
            if (quitOnEsc)
                status << ' ' << _("ESC:quit");
            extConfig.statusOutput(status.str());
            #ifndef DEDICATED_SERVER_ONLY
            // update (re-clear) window too, if there's the possibility it has been corrupted
            if (extConfig.ownScreen && GlobalDisplaySwitchHook::readAndClear())
                clear_bitmap(screen);
            #endif
        }

        // executa algo para todos os players
        server_think_after_broadcast();

        if (threadLock)
            threadLockMutex.unlock();

        // sleep while not time to send again
        for (;;) {
            g_timeCounter.refresh();
            if (get_time() >= nextFrameTime)
                break;
            platSleep(2); //#opt
        }
        nextFrameTime += .1;

        if (threadLock)
            threadLockMutex.lock();

        #ifndef DEDICATED_SERVER_ONLY
        if (quitOnEsc && key[KEY_ESC])
            break;
        #endif
    }

    if (threadLock)
        threadLockMutex.unlock();

    log("exiting gameserver::loop()");
}

void Server::stop() {
    network.stop();
}


GameserverInterface::GameserverInterface(LogSet& hostLog, const ServerExternalSettings& settings, Log& externalErrorLog, const std::string& errorPrefix) {
    host = new Server(hostLog, settings, externalErrorLog, errorPrefix);
}

GameserverInterface::~GameserverInterface() {
    delete host;
}

bool GameserverInterface::start(int maxplayers) {
    return host->start(maxplayers);
}

void GameserverInterface::loop(volatile bool *quitFlag, bool quitOnEsc) {
    host->loop(quitFlag, quitOnEsc);
}

void GameserverInterface::stop() {
    host->stop();
}
