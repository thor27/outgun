/*
 *  server.cpp
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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <climits>
#include <sstream>
#include <string>
#include <vector>

#include "client_interface.h"
#include "function_utility.h"
#include "incalleg.h"
#include "language.h"
#include "names.h"
#include "nassert.h"
#include "platform.h"
#include "thread.h"
#include "timer.h"
#include "version.h"

// implements:
#include "server.h"

const int minimum_positive_score_for_ranking = 100;
const int voteAnnounceInterval = 5; // in seconds, how often a changing voting status will be announced

using std::endl;
using std::find;
using std::ifstream;
using std::ios;
using std::istringstream;
using std::list;
using std::max;
using std::min;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::random_shuffle;
using std::setfill;
using std::setprecision;
using std::setw;
using std::string;
using std::stringstream;
using std::swap;
using std::vector;

Server::Server(LogSet& hostLogs, const ServerExternalSettings& config, Log& externalErrorLog, const string& errorPrefix) throw () :
    normalLog(wheregamedir + "log" + directory_separator + "serverlog.txt", true),
    errorLog(normalLog, externalErrorLog, "ERROR: ", errorPrefix),
    securityLog(normalLog, "SECURITY WARNING: ", wheregamedir + "log" + directory_separator + "server_securitylog.txt", false),
    adminActionLog(normalLog, "ADMIN ACTION: ", wheregamedir + "log" + directory_separator + "adminactionlog.txt", false),
    log(&normalLog, &errorLog, &securityLog),
    threadLock(config.threadLock),
    threadLockMutex("Server::threadLockMutex"),
    abortFlag(false),
    quit_bots(false),
    world(this, &network, log),
    network(this, settings, world, log, threadLock, threadLockMutex),
    settings(*this, config),
    authorizations(log),
    recording_started(false),
    end_game_human_count(0)
{
    hostLogs("See serverlog.txt for server's log messages");
    setMaxPlayers(MAX_PLAYERS);
    next_vote_announce_frame = 0;
    last_vote_announce_votes = last_vote_announce_needed = 0;
    fav_colors[0].resize(MAX_PLAYERS / 2, false);
    fav_colors[1].resize(MAX_PLAYERS / 2, false);
    Thread::setCallerPriority(config.priority);
}

Server::~Server() throw () { }

void Server::mutePlayer(int pid, int mode, int admin) throw () { // 0 = unmute, 1 = normal, 2 = mute silently (do not inform the player)
    if (world.player[pid].muted == mode || (world.player[pid].muted == 1 && mode == 2))
        return;
    const string adminName = (admin == shell_pid) ? "" : world.player[admin].name;
    const bool tellPlayer = (mode != 2 && (world.player[pid].muted != 2 || mode == 1));
    network.broadcast_mute_message(pid, mode, adminName, tellPlayer);
    logAdminAction(admin, (mode == 0 ? "unmuted" : mode == 1 ? "muted" : "silently muted"), pid);
    world.player[pid].muted = mode;
}

void Server::doKickPlayer(int pid, int admin, int minutes) throw () {  // if minutes > 0, it's really a ban
    const string adminName = (admin == shell_pid) ? "" : world.player[admin].name;
    network.broadcast_kick_message(pid, minutes, adminName);
    logAdminAction(admin, (minutes > 0 ? "banned for " + itoa(minutes) + " minutes" : "kicked"), pid);
    if (world.player[pid].kickTimer == 0)
        world.player[pid].kickTimer = 10 * 10;
}

void Server::kickPlayer(int pid, int admin) throw () {
    if (world.player[pid].kickTimer == 0)
        doKickPlayer(pid, admin, 0);
}

bool Server::loadAuthorizations() throw () {
    try {
        RedirectToMemFun1<SettingManager, bool, const string&> commandTest(&settings, &SettingManager::isGamemodCommandOrCategory);
        authorizations.load(commandTest);
        return true;
    } catch (const AuthorizationDatabase::FileError& e) {
        log.error(e.description);
        return false;
    }
}

void Server::saveAuthorizations() const throw () {
    try {
        authorizations.save();
    } catch (const AuthorizationDatabase::FileError& e) {
        log.error(e.description);
    }
}

void Server::banPlayer(int pid, int admin, int minutes) throw () {
    if (!loadAuthorizations())
        return;
    const Network::Address addr = network.get_client_address(world.player[pid].cid);
    if (!authorizations.isBanned(addr)) {
        authorizations.ban(addr, world.player[pid].name, minutes);
        saveAuthorizations();
        doKickPlayer(pid, admin, minutes);
    }
    else
        kickPlayer(pid, admin); // this is possible in the case of multiple players from the same IP; the time can't be changed anymore, so just kick
}

void Server::logAdminAction(int admin, const string& action, int target) throw () {
    string message;
    if (target == pid_none)
        message = (admin == shell_pid ? "Admin shell user" : world.player[admin].name) + ' ' + action;
    else
        message = world.player[target].name + " [" + network.get_client_address(world.player[target].cid).toString() + "] was "
                  + action + " by " + (admin == shell_pid ? "admin shell user" : world.player[admin].name);
    adminActionLog.put(message);
    network.sendTextToAdminShell(message);
}

void Server::logChat(int pid, const string& message) throw () {
    if (!settings.get_log_player_chat())
        return;
    adminActionLog.put("(chat) " +
                       (pid == shell_pid ?
                        "Admin shell user" :
                        world.player[pid].name + " [" + network.get_client_address(world.player[pid].cid).toString() + "]")
                       + ": " + message);
}

bool Server::check_name_password(const string& name, const string& password) const throw () {
    return authorizations.checkNamePassword(name, password);
}

void Server::ctf_game_restart() throw () {
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

    if (world.getConfig().balanceTeams()) {
        balance_teams();
        if (world.getConfig().balanceTeams() == WorldSettings::TB_balance_and_shuffle)
            shuffle_teams();
    }

    network.sendWorldReset();   // must be before world.reset() because world.reset() already sends initializations
    world.reset();
}

void Server::balance_teams() throw () {
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

void Server::shuffle_teams() throw () {  // weird system, because players table has gaps
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
void Server::check_team_changes() throw () {
    // check players in random order
    vector<int> order;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && world.player[i].want_change_teams)
            order.push_back(i);
    random_shuffle(order.begin(), order.end());
    for (vector<int>::const_iterator pi = order.begin(); pi != order.end(); ++pi)
        check_player_change_teams(*pi); // this may move a player later in the order too, but that doesn't hurt
}

// Check if a player wants to change teams and if yes, try to fullfill the wish.
void Server::check_player_change_teams(int pid) throw () {
    if (!world.player[pid].used || !world.player[pid].want_change_teams)
        return;
    if (get_time() < world.player[pid].team_change_time)
        return;

    //count players in each team
    int tc[2] = { 0, 0 };
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            tc[i / TSIZE]++;

    //check if team changing happens: calculate delta TARGET TEAM - MY TEAM
    const int teamdelta = tc[1 - pid / TSIZE] - tc[pid / TSIZE];

    // target team with 2 players less: move player without a trade
    if (teamdelta <= -2)
        for (int i = 0; i < maxplayers; i++)
            if (i / TSIZE != pid / TSIZE)
                if (!world.player[i].used) {
                    move_player(pid, i);
                    return;
                }
    // Find a trade.
    if (teamdelta <= 0)
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

    if (network.get_bot_count() == 0)
        return;

    // Switch teams with a bot.
    int move_dead       = -1;   // First with a dead one.
    int move_no_carrier = -1;   // Then with a bot without flag.
    int move_any        = -1;   // Last with any bot.
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && world.player[i].is_bot() && i / TSIZE != pid / TSIZE)
            if (world.player[i].dead && move_dead == -1) {
                move_dead = i;
                break;
            }
            else if (!world.player[i].stats().has_flag() && move_no_carrier == -1)
                move_no_carrier = i;
            else if (move_any == -1)
                move_any = i;
    if (move_dead != -1)
        swap_players(pid, move_dead);
    else if (move_no_carrier != -1)
        swap_players(pid, move_no_carrier);
    else if (move_any != -1)
        swap_players(pid, move_any);
}

//move player - move player (f rom) to empty position (t o)
void Server::move_player(int f, int t) throw () {
    fav_colors[f / TSIZE][world.player[f].color()] = false;
    world.player[f].set_color(PlayerBase::invalid_color);

    world.dropFlagIfAny(f, true);
    if (!world.player[f].dead)
        world.resetPlayer(f);   // no need to tell clients because it's inferred by team_change message

    //copy to t
    world.player[t] = world.player[f];

    //change rockets owner from f to t
    world.changeEmbeddedPids(f, t);

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
void Server::swap_players(int a, int b) throw () {
    fav_colors[a / TSIZE][world.player[a].color()] = false;
    fav_colors[b / TSIZE][world.player[b].color()] = false;
    world.player[a].set_color(PlayerBase::invalid_color);
    world.player[b].set_color(PlayerBase::invalid_color);

    world.dropFlagIfAny(a, true);
    world.dropFlagIfAny(b, true);
    if (!world.player[a].dead)
        world.resetPlayer(a);   // no need to tell clients because it's inferred by team_change message
    if (!world.player[b].dead)
        world.resetPlayer(b);   // no need to tell clients because it's inferred by team_change message

    swap(world.player[a], world.player[b]);
    world.swapEmbeddedPids(a, b);

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

void Server::set_fav_colors(int pid, const vector<char>& colors) throw () {
    if (world.player[pid].used) {
        world.player[pid].set_fav_colors(colors);
        check_fav_colors(pid);
    }
}

void Server::check_fav_colors(int pid) throw () {
    ServerPlayer& player = world.player[pid];
    if (!player.used)
        return;
    const int team = pid / TSIZE;

    // check favourite colours
    const vector<char>& player_colors = player.fav_colors();
    for (vector<char>::const_iterator col = player_colors.begin(); col != player_colors.end(); ++col) {
        nAssert(*col < static_cast<int>(fav_colors[team].size()));
        if (player.color() == *col)
            return;
        else if (!fav_colors[team][*col]) {
            if (player.color() != PlayerBase::invalid_color)
                fav_colors[team][player.color()] = false;
            player.set_color(*col);
            fav_colors[team][player.color()] = true;
            return;
        }
    }

    if (player.color() != PlayerBase::invalid_color)
        return;

    // if no favourites free, give a random colour
    vector<int> random_list;
    for (int i = 0; i < static_cast<int>(fav_colors[team].size()); i++)
        random_list.push_back(i);
    random_shuffle(random_list.begin(), random_list.end());

    for (vector<int>::const_iterator col = random_list.begin(); col != random_list.end(); ++col)
        if (!fav_colors[team][*col]) {
            player.set_color(*col);
            fav_colors[team][player.color()] = true;
            return;
        }
    nAssert(0);     // should never go here
}

void Server::sendMessage(int pid, Message_type type, const string& msg) throw () {
    network.player_message(pid, type, msg);
}

//refresh team ratings
void Server::refresh_team_score_modifiers() throw () {
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

    if (world.getConfig().respawn_balancing_time >= 5.) {   // artificial treshold for "enough"
        // assume team size to make no difference
        raw[0] /= players[0];
        raw[1] /= players[1];
    }

    //modifiers
    team_smul[0] = bound(raw[1] / raw[0], .3333, 3.);
    team_smul[1] = bound(raw[0] / raw[1], .3333, 3.);
}

//score!
void Server::score_frag(int pid, int amount, bool forTournament) throw () {
    world.player[pid].stats().add_frag(amount);

    const int cid = world.player[pid].cid;

    // add tournament scoring delta if all criteria for tournament scoring are satisfied
    if (forTournament && settings.get_tournament() && network.numDistinctClients() >= 4 && client[cid].current_participation) {
        refresh_team_score_modifiers();
        client[cid].fdp += amount * team_smul[pid / TSIZE];
        client[cid].delta_score = static_cast<int>(client[cid].fdp);
    }
}

//score! NEG FRAG (v0.4.8)
void Server::score_neg(int p, int amount, bool forTournament) throw () {
    const int cid = world.player[p].cid;

    // add tournament scoring delta if all criteria for tournament scoring are satisfied
    if (forTournament && settings.get_tournament() && network.numDistinctClients() >= 4 && client[cid].current_participation) {
        client[cid].fdn += amount;  // not affected by team modifier
        client[cid].neg_delta_score = static_cast<int>(client[cid].fdn);
    }
}

//load a map from the rotation list
bool Server::load_rotation_map(int pos) throw () {
    record_map.clear();
    string file_name;
    string dir;
    if (maprot[pos].random) {
        //const string map_title = finnish_name(10);
        //maprot[pos].title = map_title;
        dir = string() + SERVER_MAPS_DIR + directory_separator + "generated";
        file_name = "mapgen_" + itoa(rand());
        maprot[pos].file = file_name;
        world.generate_map(dir, file_name, maprot[pos].width, maprot[pos].height, maprot[pos].over_edge, maprot[pos].title, "Outgun");
    }
    else {
        dir = SERVER_MAPS_DIR;
        file_name = maprot[pos].file;
    }
    const bool ok = world.load_map(dir, file_name,
                                   settings.get_recording() || network.is_relay_used() ? &record_map : 0);
    if (!ok)
        return false;
    log("Map number %i: '%s'", pos, maprot[pos].file.c_str());
    maprot[pos].update(world.map);   // In case the map file has been modified since the map list loading.
    if (world.getConfig().random_wild_flag) {
        world.remove_team_flags(0);
        world.remove_team_flags(1);
        world.remove_team_flags(2);
        world.add_random_flag(2);
        return true;
    }
    // Check the flag settings and remove the useless flags (only if there are three kinds of flags).
    if (world.wild_flags.empty() || world.teams[0].flags().empty() || world.teams[1].flags().empty())
        return true;
    if (world.getConfig().lock_team_flags && (world.getConfig().lock_wild_flags || !world.getConfig().capture_on_team_flag)) {
        world.remove_team_flags(0);
        world.remove_team_flags(1);
        if (world.getConfig().lock_wild_flags)
            world.remove_team_flags(2);
    }
    else if (world.getConfig().lock_wild_flags && !world.getConfig().capture_on_wild_flag)
        world.remove_team_flags(2);
    return true;
}

bool Server::server_next_map(int reason, const string& currmap_title_override) throw () {
    end_game_human_count = network.get_human_count();

    network.update_serverinfo();

    nAssert(!maprot.empty());

    for (int i = 0; i < maxplayers; ++i)
        world.player[i].stats().finish_stats(get_time());

    if (settings.get_save_stats() && !gameover && network.get_human_count() >= settings.get_save_stats())    // !gameover: Don't save stats for the game that didn't start.
        world.save_stats("server_stats", currmap_title_override.empty() ? current_map().title : currmap_title_override);

    // broadcast stats to all players for stats saving
    for (int i = 0; i < maxplayers; ++i) {
        const ServerPlayer& pl = world.player[i];
        if (!pl.used)
            continue;
        if (pl.oldfrags != pl.stats().frags())
            network.sendFragUpdate(i, pl.stats().frags());
        // no need to update oldfrags, since the stats are next cleared
        network.broadcast_movements_and_shots(pl);     // player's stats to everyone
        network.send_team_movements_and_shots(pl.cid); // team stats to player
    }
    network.broadcast_stats_ready();

    vector<int> winners;
    int maxVotes = 0;
    uint32_t longest_time = world.frame;
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

    // Server is showing gameover plaque. Nobody should move or receive world frames.
    gameover = true;
    gameover_time = get_time() + settings.get_game_end_delay();        // timeout for gameover plaque

    ctf_game_restart();

    return true;
}

bool Server::recording_active() const throw () {
    return record || network.is_relay_active();
}

void Server::start_recording() throw () {
    if ((!settings.get_recording() || network.get_player_count() < 2) && !network.is_relay_used())
        return;

    record_start_frame = world.frame;
    record_messages.clear();

    ExpandingBinaryBuffer data;
    data.constLengthStr(REPLAY_IDENTIFICATION, REPLAY_IDENTIFICATION.length());
    data.U32(REPLAY_VERSION);
    data.U32(0); // reserve space for the frame count
    data.str(settings.get_hostname());
    data.U32(maxplayers);
    data.str(world.map.title); // just for easy loading of the map name

    if (settings.get_recording()) {
        const time_t tt = time(0);
        const tm* tmb = localtime(&tt);
        const int time_w = 20;
        char time_str[time_w + 1];
        strftime(time_str, time_w, "%Y-%m-%d_%H%M%S", tmb);
        record_filename = wheregamedir + "replay" + directory_separator + time_str + ".replay";
        record.clear();
        record.open(record_filename.c_str(), ios::binary);
        if (record)
            log("Recording started to %s.", record_filename.c_str());
        else
            log("Could not create record file %s.", record_filename.c_str());

        record << data;
    }

    record_init_data();
    data.U32(settings.get_spectating_delay());
    network.send_first_relay_data(data);

    recording_started = true;
    log("First data %u bytes.", data.size());
}

void Server::stop_recording() throw () {
    recording_started = false;
    if (record) {
        if (gameover && end_game_human_count >= settings.get_recording() ||
                !gameover && network.get_human_count() >= settings.get_recording()) {
            // write the length of the record
            record.seekp(16);
            {
                BinaryBuffer<4> data;
                data.U32(world.frame - record_start_frame);
                record << data;
            }
            record.close();
            record.clear();
        }
        else
            delete_recording();
    }
}

void Server::delete_recording() throw () {
    record.close();
    record.clear();
    if (remove(record_filename.c_str()))
        log("Could not delete the replay file: %s", record_filename.c_str());
    else
        log("Deleted the replay file: %s", record_filename.c_str());
}

void Server::record_init_data() throw () {
    // Welcome message
    for (vector<string>::const_iterator line = settings.get_welcome_message().begin(); line != settings.get_welcome_message().end(); ++line)
        network.player_message(pid_record, msg_server, *line);

    network.send_server_settings(pid_record);
    network.send_map_change_message(pid_record, NEXTMAP_NONE, maprot[currmap].file.c_str());

    // Player data
    network.record_players_present();
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            network.send_player_crap_update(pid_record, i);
            network.send_player_name_update(pid_record, i);
            network.send_stats(world.player[i], pid_record);
        }
}

//check map exit by vote
void Server::check_map_exit() throw () {
    int num_for = 0, num_against = 0;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && !world.player[i].is_bot()) {
            if (world.player[i].want_map_exit && (!settings.get_require_specific_map_vote() || world.player[i].mapVote != -1))
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

    if (num_for > num_against && (world.getMapTime() >= settings.get_vote_block_time() || num_against == 0))
        server_next_map(NEXTMAP_VOTE_EXIT); // ignore return value
}

//----- THE REST  ----------------

bool Server::reset_settings(bool reload) throw () {  // set reload if reset_settings has already been called to preserve map and votes, and ensure that fixed values aren't changed
    loadAuthorizations();

    string currMapFile, currMapTitle;
    list< pair<int, string> > oldVotes;    // pair<pid, map-filename>
    if (reload) {
        currMapFile = maprot[currmap].file;
        currMapTitle = maprot[currmap].title;
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && world.player[i].mapVote != -1)
                oldVotes.push_back(pair<int, string>(i, maprot[world.player[i].mapVote].file));
    }

    world.physics = PhysicalSettings(); // default values
    maprot.clear();
    settings.reset();
    currmap = 0;

    // load server configuration from gamemod.txt
    settings.loadGamemod(reload);

    bot_ping_changed = true;

    if (maprot.empty()) {
        // did not specify maps, scan "maps/" folder for .txt map files
        FileFinder* mapFiles = platMakeFileFinder(wheregamedir + SERVER_MAPS_DIR, ".txt", false);
        while (mapFiles->hasNext()) {
            const string mapName = FileName(mapFiles->next()).getBaseName();
            MapInfo mi;
            if (mi.load(log, mapName)) {
                maprot.push_back(mi);
                log("Added '%s' to map rotation", mapName.c_str());
            }
            else
                log.error(_("Can't add '$1' to map rotation.", mapName));
        }
        delete mapFiles;
        sort(maprot.begin(), maprot.end());
    }

    if (maprot.empty()) {
        log.error(_("No maps for rotation."));
        abortFlag = true;
        return false;
    }

    if (settings.get_random_maprot())
        random_shuffle(maprot.begin(), maprot.end());

    if (reload) {   // preserve selected map and restore map votes where possible
        network.broadcast_reset_map_list(); // must be before new votes are sent (right below)
        for (int i = 0; i < maxplayers; i++)
            if (world.player[i].used)
                world.player[i].current_map_list_item = 0;

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
            server_next_map(NEXTMAP_VOTE_EXIT, currMapTitle);
        else
            network.broadcast_current_map(currmap);
        // what is left are players whose voted map was erased from the list
        for (list< pair<int, string> >::iterator vi = oldVotes.begin(); vi != oldVotes.end(); ++vi)
            world.player[vi->first].mapVote = -1;   // the client knows this because of broadcast_reset_map_list above
    }
    else if (settings.get_random_first_map())
        currmap = rand() % maprot.size();
    return true;
}

void Server::init_bots() throw () {
    const int humans = network.get_human_count();
    const int playing_bots = network.get_bot_count();
    log("%d playing bots, %lu in vector.", playing_bots, static_cast<long unsigned>(bots.size()));
    int needed_bots = max(settings.get_bots_fill() - humans, settings.get_min_bots()) + extra_bots;
    if (needed_bots < 0)
        needed_bots = 0;
    else if (humans + needed_bots > maxplayers)
        needed_bots = maxplayers - humans;
    if (settings.get_balance_bot() && (humans + needed_bots) % 2)
        ++needed_bots;
    log("%d bots needed.", needed_bots);
    // Check if some bots need to be removed.
    if (playing_bots > needed_bots) {
        const int remove = playing_bots - needed_bots;
        log("Remove %d bots.", remove);
        for (int i = 0; i < remove; ++i)
            remove_bot();
        return;
    }
    if (static_cast<int>(bots.size()) >= needed_bots)
        return;
    ServerExternalSettings serverCfg;
    ClientExternalSettings clientCfg;
    int policy;
    sched_param param;
    pthread_getschedparam(pthread_self(), &policy, &param); // get priority of current thread (which is the default value)
    clientCfg.networkPriority = clientCfg.priority = clientCfg.lowerPriority = param.sched_priority;
    clientCfg.statusOutput = settings.statusOutput();
    Network::Address address;
    address.fromValidIP("127.0.0.1:" + itoa(settings.get_port()));
    static int botId = 1;
    while (bots.size() < static_cast<unsigned>(needed_bots)) {
        ClientInterface* bot = ClientInterface::newClient(clientCfg, serverCfg, botNoLog, botErrorLog);
        nAssert(bot);
        bot->set_bot_password(settings.get_server_password());
        bot->bot_start(address, settings.get_bot_ping(), settings.get_bot_name_lang(), botId++);
        bots.push_back(bot);
        log("Bot added");
    }
}

void Server::remove_bot() throw () {
    int red = 0, blue = 0;
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used)
            if (world.player[i].team() == 0)
                ++red;
            else
                ++blue;
    int remove_team;
    if (red > blue)
        remove_team = 0;
    else if (blue > red)
        remove_team = 1;
    else
        remove_team = -1;   // any team
    // First try to remove a dead bot.
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used && world.player[i].is_bot() && world.player[i].dead &&
                        (remove_team == -1 || world.player[i].team() == remove_team)) {
            disconnectPlayer(i, disconnect_kick);
            return;
        }
    // Try to remove a bot with no flag.
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used && world.player[i].is_bot() && !world.player[i].stats().has_flag() &&
                        (remove_team == -1 || world.player[i].team() == remove_team)) {
            disconnectPlayer(i, disconnect_kick);
            return;
        }
    // Just get rid of one bot, first from the bigger team.
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used && world.player[i].is_bot() &&
                        (remove_team == -1 || world.player[i].team() == remove_team)) {
            disconnectPlayer(i, disconnect_kick);
            return;
        }
    // The last effort is to remove a bot from the smaller team.
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used && world.player[i].is_bot()) {
            disconnectPlayer(i, disconnect_kick);
            return;
        }
    nAssert(0); // There should be bots if this function is called.
}

//start server
bool Server::start(int target_maxplayers) throw () {
    nAssert(target_maxplayers >= 2 && target_maxplayers <= MAX_PLAYERS && target_maxplayers % 2 == 0);

    // Set maxplayers, could be reset by gamemod setting.
    setMaxPlayers(target_maxplayers);

    //reset client_c struct (closes files...)
    for (int i = 0; i < MAX_PLAYERS; i++)
        client[i].reset();

    gameover = false;
    extra_bots = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
        world.player[i].clear(false, i, 0, "", i / TSIZE, 0);  // 0 : fake cid (and uid)

    if (!reset_settings(false))
        return false;
    if (!load_rotation_map(currmap))
        return false;
    if (!network.start())   // this must be last, because network.stop() must always be called if start() succeeds; also, priv, port and ip must be final
        return false;

    if (threadLock)
        threadLockMutex.lock();

    record.clear();
    ctf_game_restart();
    world.reset_time();

    network.update_serverinfo();

    if (threadLock)
        threadLockMutex.unlock();

    abortFlag = false;

    //start bot thread
    botthread.start_assert("Server::run_bot_thread",
                           RedirectToMemFun0<Server, void>(this, &Server::run_bot_thread),
                           settings.lowerPriority());

    return true;
}

int Server::getLessScoredTeam() const throw () {
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

void Server::game_remove_player(int pid, bool removeClient) throw () {
    if (world.player[pid].color() != PlayerBase::invalid_color)
        fav_colors[pid / TSIZE][world.player[pid].color()] = false;
    if (removeClient)
        client[world.player[pid].cid].reset();
    network.removePlayer(pid);
    world.removePlayer(pid);
}

void Server::disconnectPlayer(int pid, Disconnect_reason reason) throw () {
    network.disconnect_client(world.player[pid].cid, 2, reason);
}

void Server::nameChange(int id, int pid, string name, const string& password) throw () {
    replace_all_in_place(name, '\xA0', ' '); // 'normalize' any no-break space

    if (!world.player[pid].is_bot() && name.substr(0, 3) == "BOT" && (name.length() == 3 || name[3] == ' '))
        name = "NOB" + name.substr(3);

    if (name == world.player[pid].name)
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

    if (!check_name(name)) {
        log("Kicked player %d for client misbehavior: attempted invalid name '%s'.", pid, name.c_str());
        disconnectPlayer(pid, disconnect_client_misbehavior);
        return;
    }
    else {
        if (authorizations.checkNamePassword(name, password)) {
            world.player[pid].name = name;
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
                             name.c_str(), password.c_str(), network.get_client_address(id).toString().c_str());
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
    PlayerMessager(Server& server, int pid, Message_type mtype) throw () : host(server), player(pid), type(mtype) { }
    PlayerMessager& operator()(const string& str) throw () { host.sendMessage(player, type, str); return *this; }
};

bool Server::isLocallyAuthorized(int pid) const throw () {
    return world.player[pid].localIP || authorizations.nameAccess(world.player[pid].name).isProtected(); // must have authorized because otherwise couldn't use the name
}

bool Server::isAdmin(int pid) const throw () {
    if (world.player[pid].is_bot())
        return false;
    if (world.player[pid].localIP)
        return true;
    const AuthorizationDatabase::AccessDescriptor& access = authorizations.nameAccess(world.player[pid].name);
    if (!access.isAdmin())
        return false;
    const ClientData& cld = client[world.player[pid].cid];
    return (cld.token_have && cld.token_valid) || access.isProtected();
}

AuthorizationDatabase::AccessDescriptor Server::getAccess(int pid) throw () {
    if (pid == shell_pid)
        return authorizations.shellAccess();
    numAssert(pid >= 0 && pid < MAX_PLAYERS, pid);
    if (!world.player[pid].used || world.player[pid].is_bot())
        return authorizations.defaultAccess();
    const AuthorizationDatabase::AccessDescriptor& nameAccess = authorizations.nameAccess(world.player[pid].name);
    if (nameAccess.isAdmin()) {
        const ClientData& cld = client[world.player[pid].cid];
        if ((cld.token_have && cld.token_valid) || nameAccess.isProtected())
            return nameAccess;
    }
    if (world.player[pid].localIP)
        return authorizations.localAccess();
    return authorizations.defaultAccess();
}

void Server::chat(int pid, const string& message) throw () {
    if (message.empty())
        return;
    // handle 'console' commands
    if (message[0] == '/') {
        const AuthorizationDatabase::AccessDescriptor access = getAccess(pid);

        const string::size_type pos = message.find(' ', 1);
        const string command = message.substr(1, pos - 1);
        const string arguments = pos == string::npos ? string() : message.substr(pos + 1);

        if (command == "help") {
            network.player_message(pid, msg_header, "Console commands available on this server:");
            network.player_message(pid, msg_server, "/help       this screen");
            if (!settings.get_info_message().empty())
                network.player_message(pid, msg_server, "/info       information about this server");
            network.player_message(pid, msg_server, "/time       check server uptime, current map time and time left on the map");
            if (settings.get_sayadmin_enabled()) {
                ostringstream ostr;
                ostr << "/sayadmin   send a message to the server admin";
                if (!settings.get_sayadmin_comment().empty())
                    ostr << " (" << settings.get_sayadmin_comment() << ')';
                network.player_message(pid, msg_server, ostr.str());
            }
            if (access.isAdmin()) {
                network.player_message(pid, msg_header, "Admin commands:");
                network.player_message(pid, msg_server, "/list       get a list of player IDs");
                network.player_message(pid, msg_server, "/kick n     kick player with ID n");
                network.player_message(pid, msg_server, "/ban n t    ban player with ID n for t minutes (default: 60)");
                network.player_message(pid, msg_server, "/mute n     mute player with ID n");
                network.player_message(pid, msg_server, "/smute n    silently mute player with ID n (crude!)");
                network.player_message(pid, msg_server, "/unmute n   cancel muting of player with ID n");
                network.player_message(pid, msg_server, "/forcemap   restart the game and change map if you've voted for one");
                network.player_message(pid, msg_server, "/set        manage server settings");
                network.player_message(pid, msg_server, "/bot        manage bots");
            }
        }
        else if (command == "info" && !settings.get_info_message().empty()) {
            network.player_message(pid, msg_header, settings.get_info_message().front());
            for (vector<string>::const_iterator line = settings.get_info_message().begin() + 1; line != settings.get_info_message().end(); line++)
                network.player_message(pid, msg_server, *line);
        }
        else if (command == "sayadmin" && settings.get_sayadmin_enabled()) {
            if (arguments.find_first_not_of(" ") != string::npos) {
                ofstream log((wheregamedir + "log" + directory_separator + "sayadmin.log").c_str(), ios::out | ios::app);
                log << date_and_time() << "  " << (pid == shell_pid ? "Shell admin" : world.player[pid].name) << ": " << arguments << endl;
                network.forwardSayadminMessage(world.player[pid].cid, arguments);
                network.player_message(pid, msg_server, "Your message has been logged. Thank you for your feedback!");
            }
            else
                network.player_message(pid, msg_server, "For example to send \"Hello!\", type /sayadmin Hello!");
        }
        else if (command == "time") {
            PlayerMessager pm(*this, pid, msg_server);
            world.printTimeStatus(pm);
        }
        else if (command == "list" && access.isAdmin()) {
            network.player_message(pid, msg_header, "Remote players on server: ID, login flags, name");
            int team = -1;
            for (int ppid = 0; ppid < MAX_PLAYERS; ) {
                char buf[100];
                int bufi = 0;
                for (int onrow = 0; onrow < 2 && ppid < MAX_PLAYERS; ++ppid)
                    if (world.player[ppid].used && !world.player[ppid].localIP) {
                        if (ppid / TSIZE != team) {
                            if (onrow != 0)
                                break; // print the half-built row first, then come back here
                            team = ppid / TSIZE;
                            network.player_message(pid, msg_header, team == 0 ? "Red team:" : "Blue team:");
                        }
                        const char mute = world.player[ppid].muted == 0 ? ' ' : world.player[ppid].muted == 1 ? 'm' : 's';
                        platSnprintf(buf + bufi, 34, "%4d %4s%c %-22s", world.player[ppid].uniqueId, world.player[ppid].reg_status.strFlags().c_str(), mute, world.player[ppid].name.c_str());
                        bufi += 33;
                        ++onrow;
                    }
                if (bufi > 0)
                    network.player_message(pid, msg_server, buf);
            }
        }
        else if (access.isAdmin() && (command == "kick" || command == "ban" || command == "mute" ||
                                      command == "smute" || command == "unmute")) {
            istringstream ist(arguments);
            unsigned uid;
            int time;   // used only for bans
            ist >> uid;
            bool ok = ist;
            ist >> time;
            if (command == "ban") {
                if (!ist)
                    time = 60;  // default: 60 minutes
                if (!ist && !ist.eof())
                    ok = false;
            }
            else if (ist || !ist.eof())
                ok = false;
            int ppid = -1;
            if (ok)
                for (ppid = 0; ppid < maxplayers; ++ppid)
                    if (world.player[ppid].used && !world.player[ppid].localIP && world.player[ppid].uniqueId == uid)
                        break;
            if (!ok)
                network.plprintf(pid, msg_warning, "Syntax error. Expecting \"/%s ID%s\".", command.c_str(), command == "ban" ? " [minutes]" : "");
            else if (ppid < 0 || ppid >= maxplayers)
                network.player_message(pid, msg_warning, "No such player. Type /list for a list of IDs.");
            else {  // syntax OK
                if (command == "kick")
                    kickPlayer(ppid, pid);
                else if (command == "ban") {
                    if (ppid == pid)
                        network.player_message(pid, msg_warning, "You can't ban yourself.");
                    else if (time <= 0 || time > 60 * 24 * 7)    // allow at most a weeks ban (a bit over 10000 minutes)
                        network.player_message(pid, msg_warning, "The ban time must be more than 0 and at most 10 000 minutes (1 week).");
                    else
                        banPlayer(ppid, pid, time);
                }
                else if (command == "mute")
                    mutePlayer(ppid, 1, pid);
                else if (command == "smute")
                    mutePlayer(ppid, 2, pid);
                else if (command == "unmute")
                    mutePlayer(ppid, 0, pid);
                else
                    nAssert(0);
            }
        }
        else if (command == "forcemap" && access.isAdmin()) {
            // Make sure that these messages match with the ones in client.cpp.
            if (pid != shell_pid && world.player[pid].mapVote != -1 && world.player[pid].mapVote != currmap) {
                network.bprintf(msg_server, "%s decided it's time for a map change.", world.player[pid].name.c_str());
                logAdminAction(pid, "forced a map change");
                maprot[world.player[pid].mapVote].votes = 99;
            }
            else {
                network.bprintf(msg_server, "%s decided it's time for a restart.", pid == shell_pid ? "Admin" : world.player[pid].name.c_str());
                logAdminAction(pid, "forced a restart");
                maprot[currmap].votes = 99;
            }
            server_next_map(NEXTMAP_VOTE_EXIT); // ignore return value
        }
        else if (command == "bot" && access.isAdmin()) {
            istringstream ist(arguments);
            string option;
            ist >> option;
            if (!ist) {
                network.player_message(pid, msg_header, "Bot commands:");
                network.player_message(pid, msg_server, "/bot list        list the bots with their ID");
                network.player_message(pid, msg_server, "/bot add n       add n bots, default 1");
                network.player_message(pid, msg_server, "/bot remove n    remove n bots, default 1");
                network.player_message(pid, msg_server, "/bot fill n      set bots_fill to n");
                network.player_message(pid, msg_server, "/bot balance s   set balance_bot on or off");
                network.player_message(pid, msg_server, "/bot ping p all  show or set the bot ping");
                network.player_message(pid, msg_server, "/bot rename n s  set the name of bot with ID n to s");
                network.plprintf      (pid, msg_server, "Currently there are %d bots.", network.get_bot_count());
                network.plprintf      (pid, msg_server, "min_bots %d, bots_fill %d, extra_bots %d, balance_bot %s",
                                       settings.get_min_bots(), settings.get_bots_fill(), extra_bots, settings.get_balance_bot() ? "on" : "off");
            }
            else if (option == "list") {
                int team = -1;
                for (int ppid = 0; ppid < MAX_PLAYERS; ) {
                    char buf[100];
                    int bufi = 0;
                    for (int onrow = 0; onrow < 3 && ppid < MAX_PLAYERS; ++ppid)
                        if (world.player[ppid].used && world.player[ppid].is_bot()) {
                            if (ppid / TSIZE != team) {
                                if (onrow != 0)
                                    break; // print the half-built row first, then come back here
                                team = ppid / TSIZE;
                                network.player_message(pid, msg_header, team == 0 ? "Red team:" : "Blue team:");
                            }
                            platSnprintf(buf + bufi, 23, "%4d %-17s", world.player[ppid].uniqueId, world.player[ppid].name.c_str());
                            bufi += 22;
                            ++onrow;
                        }
                    if (bufi > 0)
                        network.player_message(pid, msg_server, buf);
                }
            }
            else if (option == "add" || option == "remove") {
                const bool add = option == "add";
                int number = 1;
                ist >> number;
                if (!ist && !ist.eof() || number < 1 || number > maxplayers)
                    network.plprintf(pid, msg_warning, "Syntax error. Valid range is 1 - %d", maxplayers);
                else if (!add && (bots.empty() || network.get_bot_count() == 0))
                    network.plprintf(pid, msg_warning, "No bots to remove.");
                else if (add && (network.get_player_count() == maxplayers))
                    network.plprintf(pid, msg_warning, "No room for a new bot.");
                else {
                    if (add) {
                        if (extra_bots + number >= maxplayers)
                            number = maxplayers - extra_bots;
                        extra_bots += number;
                    }
                    else {
                        if (network.get_bot_count() - number < 0)
                            number = network.get_bot_count();
                        extra_bots -= number;
                    }
                    network.plprintf(pid, msg_server, "%d bots will be %s.", number, add ? "added" : "removed");
                    check_bots = true;
                }
            }
            else if (option == "fill") {
                int number;
                ist >> number;
                if (!ist && ist.eof())
                    network.plprintf(pid, msg_server, "Current bot fill is %d.", settings.get_bots_fill());
                else if (ist && ist.eof() && number >= 0 && number <= maxplayers) {
                    settings.set_bots_fill(number);
                    network.plprintf(pid, msg_server, "Bot fill is now %d.", settings.get_bots_fill());
                    check_bots = true;
                }
                else
                    network.plprintf(pid, msg_warning, "Syntax error. Valid range is 0 - %d.", maxplayers);
            }
            else if (option == "balance") {
                string setting;
                ist >> setting;
                if (!ist && ist.eof())
                    network.plprintf(pid, msg_server, "Balance bot is %s.", settings.get_balance_bot() ? "on" : "off");
                else if (ist && ist.eof() && (setting == "on" || setting == "off")) {
                    settings.set_balance_bot(setting == "on");
                    network.plprintf(pid, msg_server, "Balance bot is now %s.", settings.get_balance_bot() ? "on" : "off");
                    check_bots = true;
                }
                else
                    network.plprintf(pid, msg_warning, "Syntax error. Valid settings are 'on' and 'off'.");
            }
            else if (option == "ping") {
                int ping;
                ist >> ping;
                if (!ist && ist.eof())
                    network.plprintf(pid, msg_server, "Current bot ping is %d.", settings.get_bot_ping());
                else if (ist && ping >= 0 && ping <= 500) {
                    string all;
                    ist >> all;
                    if (!ist.eof() || (ist && all != "all"))
                        network.plprintf(pid, msg_warning, "Syntax error. Invalid argument '%s'.", all.c_str());
                    else {
                        if (ist && ist.eof() && all == "all")
                            bot_ping_changed = true;
                        settings.set_bot_ping(ping);
                        network.plprintf(pid, msg_server, "Bot ping is now %d.", settings.get_bot_ping());
                    }
                }
                else
                    network.plprintf(pid, msg_warning, "Syntax error. Valid ping range is 0 - 500.");
            }
            else if (option == "rename") {
                unsigned bot_id;
                ist >> bot_id;
                string name;
                getline(ist, name);
                name = trim(name);
                if (!ist)
                    network.plprintf(pid, msg_warning, "Syntax error. Expecting \"/rename ID name\".");
                else if (!check_name(name))
                    network.plprintf(pid, msg_warning, "Invalid name, \"%s\".", name.c_str());
                else {
                    bool handled = false;
                    for (int i = 0; i < MAX_PLAYERS; ++i)
                        if (world.player[i].used && world.player[i].uniqueId == bot_id) {
                            if (world.player[i].is_bot())
                                nameChange(world.player[i].cid, i, name, string());
                            else
                                network.plprintf(pid, msg_warning, "Player %d is not a bot.", bot_id);
                            handled = true;
                            break;
                        }
                    if (!handled)
                        network.player_message(pid, msg_warning, "No such bot. Type /bot list for a list of IDs.");
                }
            }
            else
                network.plprintf(pid, msg_warning, "Syntax error. Expecting add, remove, fill, balance, or ping.");
        }
        else if (command == "set" && access.isAdmin()) {
            if (arguments == "reset") {
                if (access.canReset()) {
                    reset_settings(true);
                    network.player_message(pid, msg_server, "Server settings reset.");
                }
                else
                    network.player_message(pid, msg_server, "You aren't allowed to reset the settings");
            }
            else if (arguments.find_first_not_of(" ") != string::npos) {
                vector<string> feedback;
                if (arguments == "list") {
                    feedback = settings.listSettings(access.gamemodAccess());
                    if (feedback.empty())
                        feedback.push_back("You don't have access to any settings.");
                }
                else
                    feedback = settings.executeLine(arguments, access.gamemodAccess());
                for (vector<string>::const_iterator fi = feedback.begin(); fi != feedback.end(); ++fi)
                    network.player_message(pid, msg_server, *fi);
            }
            else {
                network.player_message(pid, msg_server, "Setting management commands:");
                network.player_message(pid, msg_server, "/set list   list all settings that you can manipulate");
                network.player_message(pid, msg_server, "/set s      show the current value of setting s");
                network.player_message(pid, msg_server, "/set s v    change the value of setting s to v");
                if (access.canReset())
                    network.player_message(pid, msg_server, "/set reset  reload all settings from gamemod");
            }
        }
        else
            network.plprintf(pid, msg_warning, "Unknown command %s. Type /help for a list.", command.c_str());
    }
    else if (message.find_first_not_of(" ") != string::npos) {  // ignore messages that are all spaces
        numAssert(pid >= 0, pid);
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
        else if (isFlood(message))
            ;   // Notify?
        else {
            ostringstream msg;
            msg << world.player[pid].name << ": ";
            if (message[0] == '.') {   // team message
                msg << trim(message.substr(1));
                if (world.player[pid].muted == 2)
                    network.player_message(pid, msg_team, msg.str());
                else {
                    network.broadcast_team_message(pid / TSIZE, msg.str());
                    logChat(pid, message);
                }
            }
            else {                  // regular message
                msg << trim(message);
                if (world.player[pid].muted == 2)
                    network.player_message(pid, msg_normal, msg.str());
                else {
                    network.broadcast_text(msg_normal, msg.str());
                    logChat(pid, message);
                }
            }
        }
    }
}

bool Server::changeRegistration(int id, const string& token) throw () {
    const int intoken = atoi(token.c_str());
    if (intoken == client[id].intoken)
        return false;

    // v0.4.9 FIX : IF HAD previous token have/valid, then FLUSH his stats
    network.client_report_status(id);

    client[id].token = token;
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

void Server::simulate_and_broadcast_frame() throw () {
    //check end of gameover plaque
    if (gameover)
        if (gameover_time < get_time()) {
            gameover = false;
            stop_recording();
            start_recording();
            world.start_game();
            network.sendStartGame();
        }
    if (!gameover)
        world.simulateFrame();

    if (world.frame >= next_vote_announce_frame) {  // announce voting status
        int votes = 0;
        int humans = 0;
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && !world.player[i].is_bot()) {
                ++humans;
                if (world.player[i].want_map_exit && (!settings.get_require_specific_map_vote() || world.player[i].mapVote != -1))
                    ++votes;
            }
        const int players = humans / 2 + 1;
        if (votes != last_vote_announce_votes || (players != last_vote_announce_needed && votes != 0)) {
            last_vote_announce_votes = votes;
            last_vote_announce_needed = players;
            next_vote_announce_frame = world.frame + voteAnnounceInterval * 10;
            int voteblock;
            if (world.getMapTime() < settings.get_vote_block_time())
                voteblock = (settings.get_vote_block_time() - world.getMapTime() + 5) / 10;
            else
                voteblock = 0;
            network.broadcast_map_change_info(votes, players, voteblock);
        }
        if (world.getMapTime() == settings.get_vote_block_time())
            check_map_exit();
    }

    if (world.getConfig().see_rockets_distance > 0 || world.physics.allowFreeTurning)
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && world.player[i].protocolExtensionsLevel < 0 && !world.player[i].toldAboutExtensionAdvantage) {
                network.warnAboutExtensionAdvantage(i);
                world.player[i].toldAboutExtensionAdvantage = true;
            }

    for (int i = 0; i < maxplayers; ++i) {
        if (!world.player[i].used)
            continue;
        if (world.player[i].kickTimer) {
            --world.player[i].kickTimer;
            if (world.player[i].kickTimer == 0)
                disconnectPlayer(i, disconnect_kick);
            else if (world.player[i].kickTimer % 10 == 0 && world.player[i].kickTimer <= 50)
                network.send_disconnecting_message(i, world.player[i].kickTimer / 10);
            continue;
        }
        if (gameover || world.player[i].dead || world.player[i].is_bot() && world.player[i].localIP)
            continue;
        const int iktime = settings.get_idlekick_time();
        const bool idle = !world.player[i].attack && !world.player[i].attackOnce && world.player[i].controls.idle();
        if (iktime != 0 && idle && network.get_human_count() >= settings.get_idlekick_playerlimit()) {
            ++world.player[i].idleFrames;
            int timeToKick = iktime - world.player[i].idleFrames;
            if (timeToKick == 0)
                disconnectPlayer(i, disconnect_idlekick);
            else if (timeToKick == 60 * 10 && iktime >= 3 * 60 * 10 ||
                     timeToKick == 30 * 10 && iktime >= 3 * 30 * 10 ||
                     timeToKick == 15 * 10 && iktime >= 2 * 15 * 10 ||
                     timeToKick ==  5 * 10 && iktime >= 2 *  5 * 10) {
                network.send_idlekick_warning(i, timeToKick / 10);
            }
        }
        else
            world.player[i].idleFrames = 0;
    }

    network.broadcast_frame(!gameover);
    if (recording_active()) {
        ExpandingBinaryBuffer recordFrame;
        recordFrame.U32(0); // leave space for frame length
        recordFrame.U32(world.frame);
        uint32_t players_present = 0;
        for (int i = 0; i < maxplayers; i++)
            if (world.player[i].used)
                players_present |= (1 << i);
        recordFrame.U32(players_present);
        for (int i = 0; i < maxplayers; i++) {
            const ServerPlayer& pl = world.player[i];
            if (!pl.used)
                continue;

            // Dead and powerup flags
            uint8_t byte = 0;
            if (pl.dead) byte |= (1 << 0);
            if (pl.item_deathbringer) byte |= (1 << 1);
            if (pl.deathbringer_end > get_time()) byte |= (1 << 2);
            if (pl.item_shield) byte |= (1 << 3);
            if (pl.item_turbo) byte |= (1 << 4);
            if (pl.item_power) byte |= (1 << 5);
            const bool preciseGundir = world.physics.allowFreeTurning;
            if (preciseGundir) byte |= (1 << 6);

            /*if (pl.record_position) */byte |= (1 << 7);
            recordFrame.U8(byte);

            if (true || pl.record_position) {
                world.player[i].record_position = false;
                // Position
                recordFrame.U8(pl.roomx);
                recordFrame.U8(pl.roomy);
                recordFrame.U16(static_cast<uint16_t>(pl.lx));
                recordFrame.U16(static_cast<uint16_t>(pl.ly));
                // Speed
                recordFrame.flt(pl.sx);
                recordFrame.flt(pl.sy);
            }

            // Controls
            if (!pl.dead) // if dead player, don't send keys
                byte = pl.controls.toNetwork(true);
            else
                byte = ClientControls().toNetwork(true);

            if (preciseGundir) {
                const uint16_t gundir = pl.gundir.toNetworkLongForm();
                recordFrame.U8(byte | (gundir >> 8) << 5);
                recordFrame.U8(gundir & 0xFF);
            }
            else
                recordFrame.U8(byte | pl.gundir.toNetworkShortForm() << 5);

            recordFrame.U8(pl.visibility);
        }
        recordFrame.U16(world.player[world.frame % maxplayers].ping);

        recordFrame.block(record_messages);

        {
            const unsigned frame_length = recordFrame.size() - 4; // the space for frame length isn't counted
            //log("Recording frame %lu, total %u bytes.", static_cast<long unsigned>(world.frame), frame_length);

            const unsigned pos = recordFrame.getPosition();
            recordFrame.setPosition(0);
            recordFrame.U32(frame_length);
            recordFrame.setPosition(pos);
        }

        if (record)
            record << recordFrame;
        network.send_relay_data(recordFrame);
        record_messages.clear();
    }
}

//run something after simulate_and_broadcast
void Server::server_think_after_broadcast() throw () {
    int tc[2] = { 0, 0 };

    //check players with pending team changes
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            ++tc[i / TSIZE];
            if (world.player[i].want_change_teams &&
                world.player[i].team_change_time < get_time())
                    check_player_change_teams(i);
        }

    // If the teams are unbalanced, try to move a bot to the smaller team.
    const int diff = tc[0] - tc[1];
    if ((diff <= -2 || diff >= 2) && network.get_bot_count() > 0) {
        const int bigger_team = diff > 0 ? 0 : 1;
        int move_dead       = -1;
        int move_no_carrier = -1;
        int move_any        = -1;
        for (int i = 0; i < maxplayers; i++)
            if (world.player[i].used && world.player[i].is_bot() && i / TSIZE == bigger_team)
                if (world.player[i].dead && move_dead == -1) {
                    move_dead = i;
                    break;
                }
                else if (!world.player[i].stats().has_flag() && move_no_carrier == -1)
                    move_no_carrier = i;
                else if (move_any == -1)
                    move_any = i;
        int pid;
        if (move_dead != -1)
            pid = move_dead;
        else if (move_no_carrier != -1)
            pid = move_no_carrier;
        else if (move_any != -1)
            pid = move_any;
        else
            return;
        world.player[pid].want_change_teams = true;
        world.player[pid].team_change_time = 0;
        check_player_change_teams(pid);
    }
}

void Server::loop(volatile bool *quitFlag, bool quitOnEsc) throw () {
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
            if (errors && settings.showErrorCount())
                status << _("ERRORS:$1", itoa(errors)) << "  ";
            status << _("$1/$2p $3k/s v$4 port:$5",
                        itoa(network.get_human_count()), itoa(maxplayers), fcvt(network.getTraffic() / 1024, 1), getVersionString(false), itoa(settings.get_port()));
            if (quitOnEsc)
                status << ' ' << _("Esc:quit");
            settings.statusOutput()(status.str());
            #ifndef DEDICATED_SERVER_ONLY
            // update (re-clear) window too, if there's the possibility it has been corrupted
            if (settings.ownScreen() && GlobalDisplaySwitchHook::readAndClear())
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

void Server::stop() throw () {
    stop_recording();

    network.stop();

    quit_bots = true;
    settings.statusOutput()(_("Shutdown: bot thread"));
    botthread.join();
}

void Server::run_bot_thread() throw () {
    log("run_bot_thread");

    check_bots = true;
    bot_ping_changed = false;

    while (!quit_bots) {
        if (bots.empty() && !check_bots) {
            platSleep(1000);
            continue;
        }
        else
            platSleep(15);
        if (check_bots) {
            check_bots = false;
            if (threadLock)
                threadLockMutex.lock();
            init_bots();
            if (threadLock)
                threadLockMutex.unlock();
        }
        g_timeCounter.refresh();
        const bool adjust_pings = bot_ping_changed;
        if (adjust_pings)
            bot_ping_changed = false;
        for (vector<ClientInterface*>::iterator bi = bots.begin(); bi != bots.end(); ) {
            nAssert(*bi);
            if ((*bi)->bot_finished()) {
                delete *bi;
                bi = bots.erase(bi);
                check_bots = true; // a needed bot might not have been added because of the now removed one which was already out of the server
            }
            else {
                if (adjust_pings)
                    (*bi)->set_ping(settings.get_bot_ping());
                (*bi)->bot_loop();
                ++bi;
            }
        }
    }
    for (vector<ClientInterface*>::iterator bi = bots.begin(); bi != bots.end(); ++bi) {
        nAssert(*bi);
        (*bi)->stop();
        delete *bi;
    }
    bots.clear();
}


GameserverInterface::GameserverInterface(LogSet& hostLog, const ServerExternalSettings& settings, Log& externalErrorLog, const string& errorPrefix) throw () {
    host = new Server(hostLog, settings, externalErrorLog, errorPrefix);
}

GameserverInterface::~GameserverInterface() throw () {
    delete host;
}

bool GameserverInterface::start(int maxplayers) throw () {
    return host->start(maxplayers);
}

void GameserverInterface::loop(volatile bool *quitFlag, bool quitOnEsc) throw () {
    host->loop(quitFlag, quitOnEsc);
}

void GameserverInterface::stop() throw () {
    host->stop();
}
