/*
 *  admshell.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003 - Niko Ritari
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

/*

    Admin Shell

*/

#ifndef _ADMSHELL_H_
#define _ADMSHELL_H_

//admin terminal-to-server message codes
enum {
    ATS_NOOP = 0,                       //0= no-op
    ATS_GET_PLAYER_FRAGS,       //1... request the frags amount of a player <int id>
    ATS_GET_PLAYER_TOTAL_TIME,      //request the frags amount of a player <int id>
    ATS_GET_PLAYER_TOTAL_KILLS,     //request the total kills amount of a player <int id>
    ATS_GET_PLAYER_TOTAL_DEATHS,        //request the total deaths amount of a player <int id>
    ATS_GET_PLAYER_TOTAL_CAPTURES,      //request the total captures amount of a player <int id>
    ATS_SERVER_CHAT,                                    //server is saying <string chat line>
    ATS_QUIT,                           //QUIT flag, very important. on receiving, should close socket and quit
    ATS_GET_PINGS,
    ATS_KICK_PLAYER,
    ATS_BAN_PLAYER,
    ATS_MUTE_PLAYER,
    ATS_RESET_SETTINGS,

    NUMBER_OF_ATS
};

//server-to-admin terminal message codes
enum {
    STA_NOOP = 0,                           //0 = no-op
    STA_PLAYER_CONNECTED,           //1 .... player connected <int id>
    STA_PLAYER_DISCONNECTED,    //player disconnected <int id>
    STA_PLAYER_NAME_UPDATE,     //player changes name <int id> <string name>
    STA_PLAYER_KILLS,                   //player scores a kill <int id>
    STA_PLAYER_DIES,                    //player dies <int id>
    STA_PLAYER_CAPTURES,            //player captures <int id>
    STA_GAME_OVER,                      //game is over (no parameters)
    STA_PLAYER_FRAGS,                   //player's current frags amount <int id> <int frags>
    STA_PLAYER_TOTAL_TIME,      //player's total time playing since connected <int id> <int time-in-seconds>
    STA_PLAYER_TOTAL_KILLS,     //player's total kills since connected <int id> <int total-kills>
    STA_PLAYER_TOTAL_DEATHS,    //player's total deaths since connected <int id> <int total-deaths>
    STA_PLAYER_TOTAL_CAPTURES,//player's total captures since connected <int id> <int total-captures>
    STA_GAME_TEXT,                      //only one parameter: a <string> of broadcast text, identical to the one that players get
    STA_QUIT,                           //QUIT flag, very important. on receiving, should close socket and quit
    STA_PLAYER_PING,
    STA_ADMIN_MESSAGE,
    STA_PLAYER_IP,

    NUMBER_OF_STA
};

#endif
