/*
 *  colour_def.inc
 *
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

/* Default colour definitions
 *
 * File structure
 *
 * SECTION("title")
 *     title is the name for the following section of colours
 *
 * COMMENT("text")
 *     one line comment
 *
 * COL(name, red, green, blue)
 *     colour key
 *     red, green and blue are the colour components (0 - 255)
 */

SECTION("Menu")
COL(screen_background          , 0x00, 0x00, 0x00)
COL(menu_background            , 0x30, 0x40, 0x30)
COL(menu_border_shadow         , 0x50, 0x60, 0x50)
COL(menu_border_highlight      , 0xA0, 0xB0, 0xA0)
COL(menu_caption               , 0xFF, 0xFF, 0xFF)
COL(menu_caption_bg            , 0x00, 0x77, 0x00)
COL(menu_component_caption     , 0x40, 0xFF, 0x40)
COL(menu_active                , 0xFF, 0xFF, 0x00)
COL(menu_disabled              , 0x00, 0xAA, 0x00)
COL(menu_value                 , 0xFF, 0xFF, 0xFF)
COL(menu_shortcut_disabled     , 0x50, 0x60, 0x50)
COL(menu_shortcut_enabled      , 0xB0, 0xD0, 0xB0)

SECTION("Scrollbar")
COL(scrollbar                  , 0x00, 0xFF, 0x00)
COL(scrollbar_bg               , 0x00, 0x77, 0x00)

SECTION("Teams and flags")
COL(team_red_basic             , 0xFF, 0x00, 0x00)
COL(team_red_light             , 0xFF, 0x55, 0x44)
COL(team_red_dark              , 0x66, 0x00, 0x00)
COL(team_red_flash             , 0xFF, 0xC8, 0xC8)
COL(team_blue_basic            , 0x00, 0x00, 0xFF)
COL(team_blue_light            , 0x44, 0x55, 0xFF)
COL(team_blue_dark             , 0x00, 0x00, 0x66)
COL(team_blue_flash            , 0xC8, 0xC8, 0xFF)
COL(wild_flag                  , 0x00, 0xFF, 0x00)
COL(wild_flag_flash            , 0xC8, 0xFF, 0xC8)
COL(flag_pole                  , 0xFF, 0xFF, 0x00)

SECTION("Aiming lines")
COL(aim_line_redteam           , 0xF0, 0x20, 0x20)
COL(aim_line_blueteam          , 0x20, 0x20, 0xF0)
COL(aim_dot_redteam            , 0xFF, 0x00, 0x00)
COL(aim_dot_blueteam           , 0x00, 0x00, 0xFF)

SECTION("Scoreboard")
COL(scoreboard_bg_redteam      , 0x1A, 0x00, 0x00)
COL(scoreboard_bg_blueteam     , 0x00, 0x00, 0x1A)
COL(scoreboard_line_redteam    , 0x3A, 0x3A, 0x3A)
COL(scoreboard_line_blueteam   , 0x3A, 0x3A, 0x3A)
COL(scoreboard_caption_redteam , 0xFF, 0xFF, 0xFF)
COL(scoreboard_caption_blueteam, 0xFF, 0xFF, 0xFF)

SECTION("Player texts")
COL(name                       , 0xFF, 0xFF, 0xFF)
COL(name_highlight             , 0xFF, 0xFF, 0x00)
COL(spawn_highlight            , 0xFF, 0xFF, 0x00)
COL(timer_border               , 0x30, 0x30, 0x30)

SECTION("Playground")
COL(ground                     , 0x10, 0x40, 0x00)
COL(wall                       , 0x30, 0xC0, 0x00)
COL(room_border                , 0x20, 0x20, 0x20)
COL(playfield_fog              , 0xFF, 0xFF, 0xFF)

SECTION("Minimap")
COL(map_ground                 , 0x00, 0x00, 0x00)
COL(map_wall                   , 0x00, 0x77, 0x00)
COL(map_room_border            , 0x30, 0x30, 0x30)
COL(map_pic_room_border        , 0x68, 0x68, 0x68)
COL(map_player_me_1            , 0xFF, 0xFF, 0x00)
COL(map_player_me_2            , 0x00, 0x00, 0x00)
COL(map_fog                    , 0xFF, 0xFF, 0xFF)
COL(map_info_grid              , 0xFF, 0xFF, 0xFF)
COL(map_info_grid_main         , 0xFF, 0xFF, 0x00)
COL(map_info_grid_room         , 0xFF, 0x00, 0x00)
COL(map_room_highlight         , 0xFF, 0xFF, 0x00)
COMMENT("Set temporary colour different from any other map colour")
COL(map_temporary              , 0x00, 0xFF, 0xFF)

SECTION("Bars")
COL(bar_text                   , 0xFF, 0xFF, 0xFF)
COL(bar_0                      , 0x00, 0x00, 0x00)
COL(health_100                 , 0xFF, 0x00, 0x00)
COL(health_200                 , 0xFF, 0xFF, 0x00)
COL(health_300                 , 0xFF, 0x00, 0xFF)
COL(energy_100                 , 0x00, 0x00, 0xFF)
COL(energy_200                 , 0x00, 0xFF, 0x00)
COL(energy_300                 , 0x7D, 0x64, 0xFF)

SECTION("Powerup texts")
COL(power                      , 0x00, 0xFF, 0xFF)
COL(turbo                      , 0xFF, 0xFF, 0x00)
COL(shadow                     , 0xFF, 0x00, 0xFF)
COL(weapon                     , 0xFF, 0xFF, 0xFF)

SECTION("Team and map change message and FPS")
COL(change_message_1           , 0xFF, 0x00, 0x00)
COL(change_message_2           , 0xFF, 0xFF, 0xFF)
COL(change_message_delayed     , 0x68, 0x68, 0x68)
COL(fps                        , 0x68, 0x68, 0x68)

SECTION("Powerups")
COL(pup_turbo_1                , 0xBF, 0x70, 0x00)
COL(pup_turbo_2                , 0xFF, 0xA0, 0x00)
COL(pup_turbo_3                , 0xFF, 0xFF, 0x00)
COL(pup_shadow                 , 0xFF, 0x00, 0xFF)
COL(pup_power_1                , 0xFF, 0xFF, 0xFF)
COL(pup_power_2                , 0x00, 0xFF, 0xFF)
COL(pup_weapon_1               , 0x00, 0xFF, 0x00)
COL(pup_weapon_2               , 0x00, 0x00, 0xFF)
COL(pup_weapon_3               , 0xFF, 0x00, 0x00)
COL(pup_weapon_4               , 0xFF, 0xFF, 0x00)
COL(pup_health_bg              , 0xFF, 0xFF, 0xFF)
COL(pup_health_border          , 0x00, 0x00, 0x00)
COL(pup_health_cross           , 0xFF, 0x00, 0x00)
COL(pup_shield                 , 0x00, 0xFF, 0x00)
COL(pup_deathbringer           , 0x22, 0x33, 0x22)

SECTION("Deathbringer effects")
COL(deathbringer_smoke         , 0x00, 0x00, 0x00)
COL(deathbringer_carrier_circle, 0x00, 0x00, 0x00)
COL(deathbringer_affected      , 0x00, 0x00, 0x00)

SECTION("Rockets")
COL(power_rocket               , 0xFF, 0xFF, 0xFF)
COL(player_power_team          , 0xFF, 0xFF, 0xFF)
COL(player_power_personal      , 0x00, 0xFF, 0xFF)
COL(rocket_shadow              , 0x18, 0x18, 0x18)

SECTION("Dead player")
COL(blood_redteam              , 0xFF, 0x00, 0x00)
COL(blood_blueteam             , 0xFF, 0x00, 0x00)
COL(ice_cream_crisp            , 0xFF, 0xA0, 0x00)
COL(ice_cream_ball_1           , 0x00, 0x00, 0xFF)
COL(ice_cream_ball_2           , 0xFF, 0x00, 0xFF)
COL(ice_cream_ball_3           , 0x00, 0xFF, 0x00)
COL(ice_cream_text             , 0xFF, 0xFF, 0xFF)

SECTION("Game")
COL(map_time                   , 0x00, 0xFF, 0x00)
COL(map_loading_1              , 0x00, 0xFF, 0x00)
COL(map_loading_2              , 0xFF, 0xA0, 0x00)
COL(game_draw                  , 0x68, 0x68, 0x68)

SECTION("Statistics")
COL(stats_bg                   , 0x00, 0x00, 0x00)
COL(stats_caption_bg           , 0x00, 0x77, 0x00)
COL(stats_text                 , 0xFF, 0xFF, 0xFF)
COL(stats_selected             , 0xFF, 0xFF, 0x00)
COL(stats_highlight            , 0x00, 0xFF, 0x00)

SECTION("Messages")
COL(text_border                , 0x00, 0x00, 0x00)
COL(normal_message_redteam     , 0xFF, 0xA0, 0x00)
COL(normal_message_blueteam    , 0xFF, 0xA0, 0x00)
COL(normal_message_unknown     , 0xFF, 0xA0, 0x00)
COL(team_message_redteam       , 0xFF, 0xFF, 0x00)
COL(team_message_blueteam      , 0xFF, 0xFF, 0x00)
COL(team_message_unknown       , 0xFF, 0xFF, 0x00)
COL(message_info               , 0x00, 0xFF, 0x00)
COL(message_header             , 0xAA, 0xFF, 0xFF)
COL(message_server             , 0x00, 0xFF, 0xFF)
COL(message_warning            , 0xFF, 0x55, 0x44)
COL(message_highlight          , 0xFF, 0xFF, 0xFF)
COL(message_input              , 0xFF, 0xFF, 0xFF)

SECTION("Replay")
COL(replay_text                , 0xFF, 0xFF, 0xFF)
COL(replay_text_border         , 0x00, 0x00, 0x00)
COL(replay_symbol              , 0x00, 0xFF, 0x00)
COL(replay_bar                 , 0x00, 0xFF, 0x00)
COL(replay_bar_bg              , 0x00, 0x77, 0x00)

SECTION("Players")
COL(player_hit                 , 0x46, 0x46, 0x46)
COL(player01                   , 0x00, 0xFF, 0x00)
COL(player02                   , 0xFF, 0xFF, 0x00)
COL(player03                   , 0xFF, 0xFF, 0xFF)
COL(player04                   , 0xFF, 0x08, 0xFF) // can't use pure magenta, it will end up transparent in the themes
COL(player05                   , 0x00, 0xFF, 0xFF)
COL(player06                   , 0xFF, 0xA0, 0x00)
COL(player07                   , 0xFF, 0x55, 0x44)
COL(player08                   , 0x44, 0x55, 0xFF)
COL(player09                   , 0x00, 0x80, 0x00)
COL(player10                   , 0xA0, 0xA0, 0xA0)
COL(player11                   , 0x00, 0xA0, 0xA0)
COL(player12                   , 0x80, 0x00, 0x80)
COL(player13                   , 0xA0, 0x60, 0x00)
COL(player14                   , 0x00, 0x00, 0x80)
COL(player15                   , 0x80, 0x00, 0x00)
COL(player16                   , 0x66, 0x66, 0x66)
COL(player_unknown             , 0xFF, 0x00, 0x00) // used only when server does not send a player colour, which it always should do
