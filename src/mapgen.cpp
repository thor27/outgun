/*
 *  mapgen.cpp
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

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

#include "platform.h"
#include "commont.h"
#include "nassert.h"
#include "utility.h"

#include "mapgen.h"

using std::ifstream;
using std::ios;
using std::max;
using std::min;
using std::ostream;
using std::pair;
using std::string;
using std::vector;

void MapGenerator::generate(int w, int h, bool allow_over_edge) throw () {
    over_edge = allow_over_edge;
    room.clear();
    room.resize(w);
    for (vector<vector<SimpleRoom> >::iterator vi = room.begin(); vi != room.end(); vi++)
        *vi = vector<SimpleRoom>(h, true);

    if (rand() % 4)
        symmetry = rotational;
    else do
        symmetry = Symmetry(rand() % 3 + 1);
    while (symmetry == vertical && h == 1 && w > 1 || symmetry == horizontal && w == 1 && h > 1);

    int rx = rand() % width(), ry = rand() % height();
    room[rx][ry].visited = true;
    int visited_rooms = 1;
    while (visited_rooms < width() * height()) {
        SimpleRoom& current_room = room[rx][ry];
        if (  (!over_edge && ry == 0 || room[rx][(ry + height() - 1) % height()].visited) &&
              (!over_edge && ry == height() - 1 || room[rx][(ry + 1) % height()].visited) &&
              (!over_edge && rx == 0 || room[(rx + width() - 1) % width()][ry].visited) &&
              (!over_edge && rx == width() - 1 || room[(rx + 1) % width()][ry].visited)) {
            current_room.checked_through = true;
            while (1) {
                rx = rand() % width();
                ry = rand() % height();
                if (room[rx][ry].visited && !room[rx][ry].checked_through)
                    break;
            }
            continue;
        }
        while (1) {
            const int dir = rand() % 4;
            int dx = 0, dy = 0;
            switch (dir) {
            /*break;*/ case up:    dy = -1;
                break; case down:  dy = +1;
                break; case left:  dx = -1;
                break; case right: dx = +1;
            }
            if (remove_wall(rx, ry, dx, dy, visited_rooms))
                break;
        }
    }

    const pair<int, int> base = max_distance();
    const int x1 = base.first, y1 = base.second;
    const int x2 = symmetry == vertical   ? x1 : width()  - 1 - x1;
    const int y2 = symmetry == horizontal ? y1 : height() - 1 - y1;
    room[x1][y1].flag = true;
    room[x2][y2].flag = true;
    if (x1 == x2 && y1 == y2)
        flags = 1;
    else
        flags = 2;
}

bool MapGenerator::remove_wall(int rx, int ry, int dx, int dy, int& visited_rooms, bool mirror) throw () {
    if (dx == dy)
        return false;
    const int nx = rx + dx, ny = ry + dy;
    if (!over_edge && (nx < 0 || nx >= width() || ny < 0 || ny >= height()))
        return false;
    SimpleRoom& next = room[(nx + width()) % width()][(ny + height()) % height()];
    SimpleRoom& current = room[rx][ry];
    if (!mirror && next.visited)
        return false;
    if (!mirror && !next.visited) {
        visited_rooms++;
        next.visited = true;
    }
    if (dy < 0)
        current.top = next.bottom = false;
    else if (dy > 0)
        current.bottom = next.top = false;
    else if (dx < 0)
        current.left = next.right = false;
    else if (dx > 0)
        current.right = next.left = false;
    if (symmetry != asymmetric && !mirror) {
        const int mx1 = symmetry == vertical   ? rx : width()  - 1 - rx;
        const int my1 = symmetry == horizontal ? ry : height() - 1 - ry;
        const int mx2 = mx1 + (symmetry == vertical   ? +dx : -dx);
        const int my2 = my1 + (symmetry == horizontal ? +dy : -dy);
        remove_wall(mx1, my1, mx2 - mx1, my2 - my1, visited_rooms, true);
    }
    return true;
}

void MapGenerator::draw(ostream& out) const throw () {
    for (int y = 0; y < height(); y++)
        for (int i = 0; i < 3; i++) {
            if (i == 0 && y > 0)
                continue;
            for (int x = 0; x < width(); x++) {
                const SimpleRoom& current = room[x][y];
                if (i == 1) {
                    if (x == 0)
                        if (current.left)
                            out << '|';
                        else
                            out << ' ';
                    //out << (current.visited ? current.checked_through ? " x " : " o " : "   ");
                    if (current.flag)
                        out << " P ";
                    else
                        out << "   ";
                    out << (current.right ? '|' : ' ');
                }
                else {
                    if (x == 0)
                        out << '+';
                    if (i == 0 && current.top || i == 2 && current.bottom)
                        out << "---+";
                    else
                        out << "   +";
                }
            }
            out << '\n';
        }
}

pair<int, int> MapGenerator::max_distance() throw () {
    vector<Dist> distances;
    int max_dist = 0;
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) {
            const int gx = symmetry == vertical   ? x : width()  - 1 - x;
            const int gy = symmetry == horizontal ? y : height() - 1 - y;
            const int dist = distance(x, y, gx, gy);
            Dist d;
            d.coords = pair<int, int>(x, y);
            d.dist = dist;
            distances.push_back(d);
            if (dist > max_dist)
                max_dist = dist;
        }
    const int min_dist = min(max_dist, max(7 * max_dist / 10, 3));
    for (vector<Dist>::iterator di = distances.begin(); di != distances.end(); )
        if (di->dist < min_dist)
            di = distances.erase(di);
        else
            ++di;
    nAssert(!distances.empty());
    //cout << "Maximum " << max_dist << '\n';
    const Dist& selected = distances[rand() % distances.size()];
    //cout << "Selected " << selected.dist << '\n';
    return selected.coords;
}

int MapGenerator::distance(int sx, int sy, int gx, int gy) throw () {
    if (gx == sx && gy == sy)
        return 0;
    // 
    // vector<vector<Node> > node(width(), height());
    // node[sx][sy].cost = 0;
    // node[sx][sy].score = node[sx][sy].cost + abs(gx - sx) + abs(gy - sy);
    //
    // const pair<int, int> target = pair<int, int>(gx, gy);
    // vector<pair<int, int> > open;
    // open.push_back(pair<int, int>(sx, sy));
    // while (!open.empty()) {
    //     const pair<int, int> current = find_best(node, open);
    //     open.erase(find(open.begin(), open.end(), current));
    //     const int rx = current.first, ry = current.second;
    //     if (current == target)
    //         return node[rx][ry].cost;
    //     const int cost = node[rx][ry].cost + 1;
    //     for (int i = 0; i < 4; i++) {
    //         int dx = 0, dy = 0;
    //         switch (i) {
    //         /*break;*/ case up:
    //                 if (room[rx][ry].top)
    //                     continue;
    //                 dy = -1;
    //             break; case down:
    //                 if (room[rx][ry].bottom)
    //                     continue;
    //                 dy = +1;
    //             break; case left:
    //                 if (room[rx][ry].left)
    //                     continue;
    //                 dx = -1;
    //             break; case right:
    //                 if (room[rx][ry].right)
    //                     continue;
    //                 dx = +1;
    //         }
    //         const int nx = (rx + width() + dx) % width();
    //         const int ny = (ry + height() + dy) % height();
    //         Node& neighbour = node[nx][ny];
    //         if (cost >= neighbour.cost)     // worse route
    //             continue;
    //         neighbour.cost = cost;
    //         int min_dx = abs(gx - nx);
    //         int min_dy = abs(gy - ny);
    //         if (over_edge) {
    //             if (min_dx > width() / 2)
    //                 min_dx = width() - min_dx;
    //             if (min_dy > height() / 2)
    //                 min_dy = height() - min_dy;
    //         }
    //         neighbour.score = neighbour.cost + min_dx + min_dy;
    //         if (find(open.begin(), open.end(), pair<int, int>(nx, ny)) == open.end())
    //             open.push_back(pair<int, int>(nx, ny));
    //     }
    // }
    // return 0;
}

const pair<int, int>& MapGenerator::find_best(const vector<vector<Node> >& node, const vector<pair<int, int> >& open) throw () {
    // int score = INT_MAX;
    // const pair<int, int>* best_node = 0;
    // for (vector<pair<int, int> >::const_iterator ni = open.begin(); ni != open.end(); ++ni)
    //     if (node[ni->first][ni->second].score < score) {
    //         score = node[ni->first][ni->second].score;
    //         best_node = &*ni;
    //     }
    // nAssert(best_node);
    // return *best_node;
}

void MapGenerator::save_map(ostream& out, const string& title, const string& author) const throw () {
    //out << "; This map is generated by Outgun " << GAME_VERSION << ".\n";
    // out << "; This map is generated by Outgun.\n";
    // out << "; " << date_and_time() << '\n';
    // out << "P width " << width() << '\n';
    // out << "P height " << height() << '\n';
    // out << "P title " << title << '\n';
    // out << "P author " << author << '\n';
    // out << "S 16 12" << '\n';
    // out << "X room 0 0 " << width() - 1 << ' ' << height() - 1 << '\n';
    // int flag_team = rand() % 2;
    // for (int y = 0; y < height(); y++)
    //     for (int x = 0; x < width(); x++) {
    //         const SimpleRoom& current = room[x][y];
    //         if (current.flag) {
    //             out << "flag " << (flags == 1 ? 2 : flag_team) << ' ' << x << ' ' << y << " 8 6\n";
    //             flag_team = 1 - flag_team;
    //         }
    //         vector<string> walls;
    //         if (current.top)
    //             walls.push_back("top");
    //         if (current.bottom)
    //             walls.push_back("bottom");
    //         if (current.left)
    //             walls.push_back("left");
    //         if (current.right)
    //             walls.push_back("right");
    //         for (vector<string>::const_iterator wi = walls.begin(); wi != walls.end(); ++wi)
    //             out << "X " << *wi << ' ' << x << ' ' << y << '\n';
    //     }
    // vector<string> files;
    // const string dir = wheregamedir + "mapgen" + directory_separator;
    // FileFinder* finder = platMakeFileFinder(dir, ".txt", false);
    // string filename;
    // for (int files = 1; finder->hasNext(); files++)
    //     if (rand() % files == 0)
    //         filename = finder->next();
    //     else
    //         finder->next();
    // delete finder;
    // if (filename.empty()) {
    //     out << ":room\nW 0 0 5 1\nW 16 0 11 1\nW 0 1 1 4\nW 16 1 15 4\nW 0 12 5 11\nW 16 12 11 11\nW 0 11 1 8\nW 16 11 15 8\n";
    //     out << ":top\nW 5 0 11 1\n";
    //     out << ":bottom\nW 5 12 11 11\n";
    //     out << ":left\nW 0 4 1 8\n";
    //     out << ":right\nW 16 4 15 8\n";
    // }
    // else {
    //     ifstream in((dir + filename).c_str(), ios::binary);
    //     std::copy(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), std::ostreambuf_iterator<char>(out));
    // }
}
