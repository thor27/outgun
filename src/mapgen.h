/*
 *  mapgen.h
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

#include <iostream>
#include <string>
#include <vector>

class MapGenerator {
    enum Direction { up, down, left, right };
    enum Symmetry { asymmetric, rotational, horizontal, vertical };

    class SimpleRoom {
    public:
        SimpleRoom(bool walls = false) throw () : top(walls), bottom(walls), left(walls), right(walls), visited(false), checked_through(false), flag(false), mirror(false) { }

        bool top, bottom, left, right;  // walls
        bool visited;
        bool checked_through;
        bool flag;
        bool mirror;
    };

    class Node {
    public:
        Node() throw () : cost(INT_MAX), score(INT_MAX) { }
        int cost, score;
    };

    struct Dist { std::pair<int, int> coords; int dist; };

public:
    void generate(int w, int h, bool allow_over_edge = false) throw ();
    void draw(std::ostream& out) const throw ();
    void save_map(std::ostream& out, const std::string& title, const std::string& author) const throw ();

private:
    bool remove_wall(int rx, int ry, int dx, int dy, int& visited_rooms, bool mirror = false) throw ();

    std::pair<int, int> max_distance() throw ();
    int distance(int sx, int sy, int gx, int gy) throw ();
    const std::pair<int, int>& find_best(const std::vector<std::vector<Node> >& node, const std::vector<std::pair<int, int> >& open) throw ();

    int width() const throw () { return room.size(); }
    int height() const throw () { return room.front().size(); }

    std::vector<std::vector<SimpleRoom> > room; // accessible by room[x][y]
    Symmetry symmetry;
    bool over_edge;
    int flags;
};
