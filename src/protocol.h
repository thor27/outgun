/*
 *  protocol.h
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
 *  Copyright (C) 2004, 2008 - Jani Rivinoja
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

#ifndef PROTOCOL_H_INC
#define PROTOCOL_H_INC

#include <string>

#include "leetnet/server.h"

extern const std::string GAME_STRING;
extern const std::string GAME_PROTOCOL;
static const int PROTOCOL_EXTENSIONS_VERSION = 0;

extern const std::string REPLAY_IDENTIFICATION;
static const unsigned REPLAY_VERSION = 0; // increase when the replay structure changes
static const unsigned RELAY_PROTOCOL = 0;
static const unsigned RELAY_PROTOCOL_EXTENSIONS_VERSION = 0;

enum Network_data_code {
    data_name_update,
    data_text_message,
    data_first_packet,
    data_frags_update,
    data_flag_update,
    data_rocket_fire,
    data_old_rocket_visible,
    data_rocket_delete,
    data_power_collision,
    data_score_update,
    data_sound,
    data_pup_visible,
    data_pup_picked,
    data_pup_timer,
    data_weapon_change,
    data_map_change,
    data_world_reset,
    data_gameover_show,
    data_start_game,
    data_deathbringer,
    data_file_request,
    data_file_download,
    data_file_ack,
    data_registration_token,
    data_registration_response,
    data_tournament_participation,
    data_crap_update,
    data_map_time,
    data_fire_on,
    data_fire_off,
    data_suicide,
    data_drop_flag,
    data_stop_drop_flag,
    data_change_team_on,
    data_change_team_off,
    data_map_exit_on,
    data_map_exit_off,
    data_client_ready,
    data_map_list,
    data_map_votes_update,
    data_map_vote,
    data_stats,
    data_team_stats,
    data_capture,
    data_kill,
    data_flag_take,
    data_flag_return,
    data_flag_drop,
    data_players_present,
    data_new_player,
    data_spawn,
    data_movements_shots,
    data_team_movements_shots,
    data_fav_colors,
    data_name_authorization_request,
    data_server_settings,
    data_reset_map_list,
    data_stats_ready,
    data_player_left,
    data_team_change,
    data_5_min_left,
    data_1_min_left,
    data_30_s_left,
    data_time_out,
    data_extra_time_out,
    data_normal_time_out,
    data_too_much_talk,
    data_mute_notification,
    data_tournament_update_failed,
    data_player_mute,
    data_player_kick,
    data_disconnecting,
    data_idlekick_warning,
    data_map_change_info,
    data_broken_map,
    data_reserved_range_first,  // reserve some codes for extensions that are otherwise protocol compatible
    data_reserved_range_last = data_reserved_range_first + 20,  // make sure you don't use more!
    data_current_map = data_reserved_range_first,
    data_bot,
    data_negotiate_third_party_extensions, // this message is reserved for unofficial extensions; it's guaranteed to be ignored by official versions, but to gain compatibility across different 3rd party extensions, the extension to be negotiated should be identified, and unrecognized messages ignored
    data_negotiated_extensions_first = data_reserved_range_last + 1, // from here on, messages are only sent when an extension level has been negotiated and it is therefore known that the remote will understand the message
    // available from negotiated extensions level 0:
    data_acceleration_modes = data_negotiated_extensions_first,
    data_set_minimap_player_bandwidth,
    data_extension_advantage,
    data_waiting_time,
    data_flag_modes,
    data_negotiated_third_party_extensions_first = 200 // from here on, codes are guaranteed to not be used by official versions present or future, and can be used after successful negotiation with data_negotiate_third_party_extensions
};

enum Disconnect_reason {
    disconnect_kick = server_c::disconnect_first_user_defined,
    disconnect_idlekick,
    disconnect_client_misbehavior
};

enum Connect_rejection_reason {
    reject_server_full,
    reject_banned,
    reject_player_password_needed,
    reject_wrong_player_password,
    reject_server_password_needed,
    reject_wrong_server_password,
    reject_last = reject_wrong_server_password
};

enum Relay_data_code {
    relay_data_frame,
    relay_data_game_start
};

#endif
