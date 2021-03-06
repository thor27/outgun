/*
 *  world.cpp
 *
 *  Copyright (C) 2002, 2004 - Fabio Reis Cecin
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
#include <iterator>
#include <sstream>
#include <vector>

#include <cmath>

#include "binaryaccess.h"
#include "language.h"
#include "mapgen.h"
#include "network.h"    // for safeReadFloat, safeWriteFloat
#include "platform.h"   // for FileFinder
#include "timer.h"

#include "world.h"

static const int POWERUP_RADIUS = 15, FLAG_RADIUS = 15;  // for touch checks, mostly

//minimum time in seconds between flag steal at base and capture, to consider a map to be valid for scoring
const double minimum_grab_to_capture_time = 4.0;

const int maximum_shadow_visibility = 254;

using std::ifstream;
using std::ios;
using std::istream;
using std::istringstream;
using std::list;
using std::max;
using std::min;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::setfill;
using std::setw;
using std::stable_sort;
using std::string;
using std::swap;
using std::vector;

bool compare_players(const PlayerBase* a, const PlayerBase* b) throw () {
    if (a->team() != b->team())
        return a->team() < b->team();
    return a->stats().frags() > b->stats().frags();
}

/* subIntersection:
 * returns true if the area between lines (lx1,ly1)-(lx2,ly2) and (rx1,ry1)-(rx2,ry2) intersects the rectangle (rectx1,recty1)-(rectx2,recty2)
 * every line must be y-ordered : ly1<=ly2, ry1<=ry2, recty1<=recty2 ; additionally rectx1<=rectx2 and lx(y)<=rx(y) for all applicable y
 *
 * how it works:
 * for the appropriate range of y, if there exists an y so that lx(y)<=rectx2 AND rx(y)>=rectx1 , there is an intersection in that range
 * those ranges are solved with simple linear equations since lx and rx are linear
 */
bool subIntersection(double lx1, double ly1,  double lx2, double ly2,  double rx1, double ry1,  double rx2, double ry2,
                double rectx1, double recty1, double rectx2, double recty2) throw () {
    nAssert(ly1 <= ly2 && ry1 <= ry2);
    double miny = max(max(ly1, ry1), recty1), maxy = min(min(ly2, ry2), recty2);
    if (maxy < miny)
        return false;
    // first narrow the range by lx(y) <= rectx2
    if (lx1 == lx2) {   // can't formulate a value for intersection-y
        if (lx1 > rectx2)   // lx(y) <= rectx2 identically false => no solutions
            return false;
        // lx(y) <= rectx2 identically true => no narrowing from lx
    }
    else {
        // solve lx(y) == rectx2 , where lx(y) = lx1 + (y-ly1)*(lx2-lx1)/(ly2-ly1)
        const double intersect_y = (rectx2 - lx1) * (ly2 - ly1) / (lx2 - lx1) + ly1;
        if (lx2 > lx1) {    // the intersection is at y <= intersect_y
            if (maxy > intersect_y)
                maxy = intersect_y;
        }
        else {  // the intersection is at y >= intersect_y
            if (miny < intersect_y)
                miny = intersect_y;
        }
    }
    if (maxy < miny)
        return false;
    // now narrow the range further by rx(y) >= rectx1, similarly
    if (rx1 == rx2)
        return (rx1 >= rectx1);
    else {
        const double intersect_y = (rectx1 - rx1) * (ry2 - ry1) / (rx2 - rx1) + ry1;
        if (rx2 > rx1) {    // the intersection is at y >= intersect_y
            if (miny < intersect_y)
                miny = intersect_y;
        }
        else {  // the intersection is at y <= intersect_y
            if (maxy > intersect_y)
                maxy = intersect_y;
        }
    }
    return (maxy >= miny);
}

bool RectWall::intersects_circ(double x, double y, double r) const throw () {
    if (x - r <= c && x + r >= a && y - r <= d && y + r >= b) {
        if (x >= a && x <= c)
            return true;
        if (y >= b && y <= d)
            return true;
        // Check the corners.
        const double r2 = r * r;
        if ((x - a) * (x - a) + (y - b) * (y - b) <= r2)
            return true;
        if ((x - c) * (x - c) + (y - b) * (y - b) <= r2)
            return true;
        if ((x - a) * (x - a) + (y - d) * (y - d) <= r2)
            return true;
        if ((x - c) * (x - c) + (y - d) * (y - d) <= r2)
            return true;
    }
    return false;
}

TriWall::TriWall(double x1, double y1, double x2, double y2, double x3, double y3, int tex_, int alpha_) throw ()
        : WallBase(tex_, alpha_), p1x(x1), p1y(y1), p2x(x2), p2y(y2), p3x(x3), p3y(y3)
{
    if (p2y < p1y) { swap(p1x, p2x); swap(p1y, p2y); }  // 1, 2 sorted
    if (p3y < p2y) {
        swap(p2x, p3x); swap(p2y, p3y); // 1, 3 and 2, 3 sorted
        if (p2y < p1y) { swap(p1x, p2x); swap(p1y, p2y); }  // all sorted
    }
    boundx1 = min(p1x, min(p2x, p3x)), boundy1 = min(p1y, min(p2y, p3y));
    boundx2 = max(p1x, max(p2x, p3x)), boundy2 = max(p1y, max(p2y, p3y));
}

bool TriWall::intersects_circ(double x, double y, double r) const throw () {
    // A crude check first.
    if (!intersects_rect(x - r, y - r, x + r, y + r))
        return false;
    // Check if the circle intersects with the triangle edges.
    const double px[4] = { p1x, p2x, p3x, p1x };
    const double py[4] = { p1y, p2y, p3y, p1y };
    for (int i = 0; i < 3; i++) {
        const double a = sqr(px[i + 1] - px[i]) + sqr(py[i + 1] - py[i]);
        const double b = 2 * ((px[i + 1] - px[i]) * (px[i] - x) + (py[i + 1] - py[i]) * (py[i] - y));
        const double c = x * x + y * y + px[i] * px[i] + py[i] * py[i] - 2 * (x * px[i] + y * py[i]) - r * r;
        if (b * b - 4 * a * c >= 0.) {  // Check if the circle intersects with a line going by two triangle corners.
            const double d = (x - px[i]) * (px[i + 1] - px[i]) + (y - py[i]) * (py[i + 1] - py[i]);
            if (d / a >= 0 && d <= a || sqr(px[i] - x) + sqr(py[i] - y) <= r * r)   // Check the actual intersection.
                return true;
        }
    }
    // Check if the circle is inside the triangle though not intersecting with the edges.
    const double bc = p2x * p3y - p2y * p3x;
    const double ca = p3x * p1y - p3y * p1x;
    const double ab = p1x * p2y - p1y * p2x;
    const double ap = p1x * y   - p1y * x;
    const double bp = p2x * y   - p2y * x;
    const double cp = p3x * y   - p3y * x;
    const double abc = (bc + ca + ab < 0 ? -1 : 1);
    return abc * (bc - bp + cp) > 0 && abc * (ca - cp + ap) > 0 && abc * (ab - ap + bp) > 0;
}

bool TriWall::intersects_rect(double rx1, double ry1, double rx2, double ry2) const throw () {
    nAssert(ry1 <= ry2 && rx1 <= rx2);
    nAssert(p1y <= p2y && p2y <= p3y);
    if (rx1 > boundx2 || rx2 < boundx1 || ry1 > boundy2 || ry2 < boundy1)
        return false;
    /* idea: triangle is split in two triangles: y<=y2 and y>=y2
     * for both parts, the right and left edge are checked separately
     * for each edge there may be a region [yi0, yi1] where (for a right side edge) xr(y)>=x1
     * if those regions overlap with each other and [ry1, ry2], there exists an intersection
     */
    if (p2x < p1x + (p2y-p1y) * (p3x-p1x) / (p3y-p1y)) {    // p2 is left to the p1-p3 line
        if (subIntersection(p1x,p1y, p2x,p2y, p1x,p1y, p3x,p3y, rx1, ry1, rx2, ry2))    // part y<=p2y : L,R sides p1-p2, p1-p3
            return true;
        if (subIntersection(p2x,p2y, p3x,p3y, p1x,p1y, p3x,p3y, rx1, ry1, rx2, ry2))    // part y>=p2y : L,R sides p2-p3, p1-p3
            return true;
    }
    else {
        if (subIntersection(p1x,p1y, p3x,p3y, p1x,p1y, p2x,p2y, rx1, ry1, rx2, ry2))    // part y<=p2y : L,R sides p1-p3, p1-p2
            return true;
        if (subIntersection(p1x,p1y, p3x,p3y, p2x,p2y, p3x,p3y, rx1, ry1, rx2, ry2))    // part y>=p2y : L,R sides p1-p3, p2-p3
            return true;
    }
    return false;
}

CircWall::CircWall(double x_, double y_, double ro_, double ri_, double ang1, double ang2, int tex_, int alpha_) throw () :
    WallBase(tex_, alpha_),
    x(x_),
    y(y_),
    ro(ro_),
    ri(ri_)
{
    angle[0] = ang1;
    angle[1] = ang2;
    const double a1r = angle[0] * N_PI / 180;
    const double a2r = angle[1] * N_PI / 180;
    va1 = Coords(sin(a1r), cos(a1r));
    va2 = Coords(sin(a2r), cos(a2r));
    const double a2r_greater = (a2r >= a1r) ? a2r : (a2r + 2. * N_PI);
    if (a1r == a2r)
        anglecos = -1.; // full circle => max width
    else
        anglecos = cos((a2r_greater - a1r) / 2.);
    const double midangle = (a2r_greater + a1r) / 2;
    midvec = Coords(sin(midangle), cos(midangle));
}

/* CircWall::intersects_circ:
 * this function cheats a bit: it might return true even if they don't really intersect, but if it returns false, it's certain they don't intersect
 * - if the circle to be tested against overlaps the center of the wall, the current algorithm can't calculate the exact result
 * - the corners (where the wall's limiting angles cut the inner or outer limiting circle) aren't calculated exactly
 */
bool CircWall::intersects_circ(double rcx, double rcy, double rr) const throw () {
    const double dx = rcx - x, dy = rcy - y;
    const double dcr = sqrt( dx*dx + dy*dy );   // this is the radius of the tested circle center in relation to the wall's center of radius
    // if the circle is wholly outside the wall bounding circle (r=ro), there's no intersection
    if (dcr - rr > ro)
        return false;
    // if the circle is wholly within the wall inner bounding circle (r=ri), there's no intersection
    if (dcr + rr < ri)
        return false;
    // if the wall is a full circle, there always is an intersection
    if (angle[0] == angle[1])
        return true;
    // if the circle overlaps the wall's center of radius, the angle based approach can't be used; the safe bet is to return true
    if (dcr - rr <= 0)
        return true;
    // find out at what range of angles the circle projects to, in relation to the wall's center of radius; compare this to the wall's bounding angles
    double centerAngle = asin(-dy / dcr) * 180. / N_PI; // -dy because screen coordinates are reversed
    if (dx < 0)
        centerAngle = 180 - centerAngle;
    // now within [-90, 270] in physical coordinates (not screen coordinates)
    // convert to the weird map coordinates
    centerAngle = 90. - centerAngle;    // now within [-180, 180] in map coordinates
    if (centerAngle < 0.)
        centerAngle += 360.;
    // now within [0, 360[ in map coordinates
    const double width = asin(rr / dcr) * 180. / N_PI;  // how far from centerAngle the projection gets
    double a0 = angle[0], a1 = angle[1];
    if (a1 < a0)
        a1 += 360.;
    double ca0 = centerAngle - width, ca1 = centerAngle + width;
    if (ca0 < 0.)
        ca0 += 360.;
    while (ca1 < ca0)
        ca1 += 360.;
    if (ca0 <= a1 && ca1 >= a0) // intersection exists
        return true;
    // shift 360� in either direction to make sure the ranges match //#fix: is this needed?
    if (ca0 < a0) {
        ca0 += 360.;
        ca1 += 360.;
    }
    else {
        a0 += 360.;
        a1 += 360.;
    }
    if (ca0 <= a1 && ca1 >= a0) // intersection exists
        return true;
    return false;
}

/* CircWall::intersects_rect:
 * this function cheats a bit: it often returns true even if they don't really intersect, but if it returns false, it's certain they don't intersect
 * - the rectangle is extended: instead of it, the intersection is tested against its bounding circle
 * - the cheat in intersects_circ also applies
 */
bool CircWall::intersects_rect(double x1, double y1, double x2, double y2) const throw () {
    // more crude check against the wall's bounding rectangle would be: return x1<=x+ro && x2>=x-ro && y1<=y+ro && y2>=y-ro;
    const double rwr = (x2 - x1) / 2., rhr = (y2 - y1) / 2.;
    return intersects_circ(x1 + rwr, y1 + rhr, sqrt(rwr * rwr + rhr * rhr));
}

Room::Room(const Room& room) throw () {
    *this = room;
}

Room& Room::operator=(const Room& op) throw () {
    for (vector<WallBase*>::const_iterator i = walls.begin(); i != walls.end(); ++i)
        delete *i;
    for (vector<WallBase*>::const_iterator i = ground.begin(); i != ground.end(); ++i)
        delete *i;
    walls.clear();
    ground.clear();
    for (vector<WallBase*>::const_iterator i = op.walls.begin(); i != op.walls.end(); ++i)
        if (RectWall* rw = dynamic_cast<RectWall*>(*i))
            addWall(new RectWall(*rw));
        else if (TriWall* tw = dynamic_cast<TriWall*>(*i))
            addWall(new TriWall(*tw));
        else if (CircWall* cw = dynamic_cast<CircWall*>(*i))
            addWall(new CircWall(*cw));
        else
            nAssert(0);
    for (vector<WallBase*>::const_iterator i = op.ground.begin(); i != op.ground.end(); ++i)
        if (RectWall* rw = dynamic_cast<RectWall*>(*i))
            addGround(new RectWall(*rw));
        else if (TriWall* tw = dynamic_cast<TriWall*>(*i))
            addGround(new TriWall(*tw));
        else if (CircWall* cw = dynamic_cast<CircWall*>(*i))
            addGround(new CircWall(*cw));
        else
            nAssert(0);
    return *this;
}

Room::~Room() throw () {
    for (vector<WallBase*>::iterator i = walls.begin(); i != walls.end(); i = walls.erase(i))
        delete *i;
    for (vector<WallBase*>::iterator i = ground.begin(); i != ground.end(); i = ground.erase(i))
        delete *i;
}

bool Room::fall_on_wall(double x1, double y1, double x2, double y2) const throw () { // note: this is only a bounding-box check - no accurate checks possible for circular walls yet
    for (vector<WallBase*>::const_iterator wi = walls.begin(); wi != walls.end(); ++wi)
        if ((*wi)->intersects_rect(x1, y1, x2, y2))
            return true;
    return false;
}

bool Room::fall_on_wall(double x, double y, double r) const throw () {
    for (vector<WallBase*>::const_iterator wi = walls.begin(); wi != walls.end(); ++wi)
        if ((*wi)->intersects_circ(x, y, r))
            return true;
    return false;
}

BounceData Room::genGetTimeTillWall(double x, double y, double mx, double my, double radius, double maxFraction) const throw () {
    BounceData bd;
    bd.first = 1e99;

    if (mx == 0 && my == 0)
        return bd;

    const Coords bbox0(min(x - radius, x + mx * maxFraction - radius), min(y - radius, y + my * maxFraction - radius));
    const Coords bbox1(max(x + radius, x + mx * maxFraction + radius), max(y + radius, y + my * maxFraction + radius));

    for (vector<WallBase*>::const_iterator wi = walls.begin(); wi != walls.end(); ++wi) {
        // fast and crude bounding-box style check first
        if (!(*wi)->intersects_rect(bbox0.first, bbox0.second, bbox1.first, bbox1.second))
            continue;
        // check more carefully
        (*wi)->tryBounce(&bd, x, y, mx, my, radius);
        #ifdef EXTRA_DEBUG
        if (bd.first < 1e10) {
            const double dx = bd.second.first, dy = bd.second.second, r = radius;
            nAssert(fabs(dx * dx + dy * dy - r * r) < 1e-8);
        }
        #endif
    }

    nAssert(bd.first >= 0.);
    return bd;
}

bool Map::load(LogSet& log, const string& mapdir, const string& mapname, string* buffer) throw () {
    const string fileName = wheregamedir + mapdir + directory_separator + mapname + ".txt";

    ifstream in(fileName.c_str());
    if (!in) {
        log("Can't find mapfile '%s'!", fileName.c_str());
        return false;
    }
    if (buffer) {
        *buffer = string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); // http://www.gamedev.net/community/forums/topic.asp?topic_id=353162
        in.seekg(0);
    }
    if (!parse_file(log, in)) {
        log.error(_("Can't load: error in map '$1'.", mapname));
        return false;
    }
    return true;
}

bool Map::parse_file(LogSet& log, istream& in) throw () {
    *this = Map();
    int crx = 0, cry = 0;
    double scalex = 1., scaley = 1.;
    vector<pair<string, pair<int, int> > > labels;
    vector<string> file_lines;
    vector<pair<string, vector<string> > > label_lines;
    string crcData;
    // read lines to vectors
    while (1) {
        string line;
        if (!getline_skip_comments(in, line))
            break;
        line = trim(line);
        if (line.empty())
            continue;
        crcData += line;
        crcData += '\n';
        if (line[0] == ':') {           // new label
            const string label = line.substr(1);
            for (vector<pair<string, vector<string> > >::const_iterator li = label_lines.begin(); li != label_lines.end(); ++li)
                if (li->first == label) {   // same label again
                    log.error(_("Two identical label names not allowed: $1", line));
                    return false;
                }
            pair<string, vector<string> > new_label;
            new_label.first = label;
            label_lines.push_back(new_label);
        }
        else if (label_lines.empty())   // no labels yet
            file_lines.push_back(line);
        else                            // labels have started
            label_lines.back().second.push_back(line);
    }
    crc = CRC16(crcData.data(), crcData.length());
    crcData.clear();    // free the memory; crcData is not needed from here on
    for (vector<string>::const_iterator line = file_lines.begin(); line != file_lines.end(); ++line)
        if (!parse_line(log, *line, label_lines, crx, cry, scalex, scaley))
            return false;
    if (w == 0 || h == 0 || title.empty()) {
        log.error(_("Map is missing a width, height or title."));
        return false;
    }
    // Check that flags and spawn points are not on the walls.
    bool wallError = false;
    for (int t = 0; t < 3; t++) {
        const vector<WorldCoords>& flags = (t == 2 ? wild_flags : tinfo[t].flags);
        for (int i = 0; i < static_cast<int>(flags.size()); ++i) {
            const WorldCoords& point = flags[i];
            if (fall_on_wall(point.px, point.py, point.x, point.y, FLAG_RADIUS)) {
                log.error(_("Team $1, flag $2 on the wall.", itoa(t), itoa(i)));
                wallError = true;
            }
        }
        if (t == 2) // wild flags only; no spawn points to check
            continue;
        for (int i = 0; i < static_cast<int>(tinfo[t].spawn.size()); i++) {
            const WorldCoords& point = tinfo[t].spawn[i];
            if (fall_on_wall(point.px, point.py, point.x, point.y, PLAYER_RADIUS)) {
                log.error(_("Team $1, spawn point $2 on the wall.", itoa(t), itoa(i)));
                wallError = true;
            }
        }
        for (unsigned i = 0; i < tinfo[t].respawn.size(); ++i) {
            const WorldRect& area = tinfo[t].respawn[i];
            const int xStepSize = max(static_cast<int>(area.x2 - area.x1) / 5, PLAYER_RADIUS * 2),
                      yStepSize = max(static_cast<int>(area.y2 - area.y1) / 5, PLAYER_RADIUS * 2);
            const int xSteps = static_cast<int>(area.x2 - area.x1) / xStepSize + 1,
                      ySteps = static_cast<int>(area.y2 - area.y1) / yStepSize + 1;
            const int minFreeSpace = max(1, xSteps * ySteps / 5); // for area size 1..9, 1 free spot is enough
            int freeSpace = 0;
            bool ok = false;
            for (double y = area.y1; y <= area.y2 && !ok; y += yStepSize)
                for (double x = area.x1; x <= area.x2 && !ok; x += xStepSize)
                    if (!fall_on_wall(area.px, area.py, x, y, PLAYER_RADIUS * 5 / 4))
                        if (++freeSpace >= minFreeSpace)
                            ok = true;
            if (!ok) {
                log.error(_("Team $1, respawn area $2 does not have enough free space.", itoa(t), itoa(i)));
                return false;
            }
        }
    }
    return !wallError;
}

bool Map::parse_line(LogSet& log, const string& line, const vector<pair<string, vector<string> > >& label_lines,
                                                        int& crx, int& cry, double& scalex, double& scaley, bool label_block) throw () {
    istringstream ist(line);
    string command;
    ist >> command;
    if (command == "W" || command == "G") { // W x1 y1 x2 y2 [tex [alpha]] : rectangular wall (x1,y1)-(x2,y2) using given texture and alpha ; G : ground texture
        double x1, y1, x2, y2;
        int texid, alpha;
        ist >> x1 >> y1 >> x2 >> y2;
        if (ist.good())
            ist >> texid;
        else
            texid = 0;
        if (ist.good())
            ist >> alpha;
        else
            alpha = 255;
        if (!ist || !ist.eof() || crx < 0 || cry < 0 || crx >= w || cry >= h || alpha < 0 || alpha > 255 || texid < 0 || texid > 7) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
        x1 *= plw / scalex; x2 *= plw / scalex;
        y1 *= plh / scaley; y2 *= plh / scaley;
        Room& rm = room[crx][cry];
        RectWall* wall = new RectWall(x1, y1, x2, y2, texid, alpha);
        if (command == "W")
            rm.addWall(wall);
        else
            rm.addGround(wall);
    }
    else if (command == "T") {  // T (W|G) x1 y1 x2 y2 x3 y3 [tex [alpha]] : triangular wall (W) or ground tex (G) (x1,y1)-(x2,y2)-(x3,y3) using given texture and alpha
        char type;
        double x1, y1, x2, y2, x3, y3;
        int texid, alpha;
        ist >> type >> x1 >> y1 >> x2 >> y2 >> x3 >> y3;
        if (ist.good())
            ist >> texid;
        else
            texid = 0;
        if (ist.good())
            ist >> alpha;
        else
            alpha = 255;
        if (!ist || !ist.eof() || (type != 'W' && type != 'G') || crx < 0 || cry < 0 || crx >= w || cry >= h || alpha < 0 || alpha > 255 || texid < 0 || texid > 7) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
        x1 *= plw / scalex; x2 *= plw / scalex; x3 *= plw / scalex;
        y1 *= plh / scaley; y2 *= plh / scaley; y3 *= plh / scaley;
        Room& rm = room[crx][cry];
        TriWall* wall = new TriWall(x1, y1, x2, y2, x3, y3, texid, alpha);
        if (type == 'W')
            rm.addWall(wall);
        else
            rm.addGround(wall);
    }
    else if (command == "C") {  // C (W|G) x y or [ir [a1 a2 [tex [alpha]]]] : circular wall (W) or ground tex (G)
        char type;
        double x, y, ro, ri, a1, a2;
        int texid, alpha;
        ist >> type >> x >> y >> ro;
        if (ist.good())
            ist >> ri;
        else
            ri = a1 = a2 = 0;
        if (ist.good())
            ist >> a1 >> a2;
        else
            a1 = a2 = 0;
        if (ist.good())
            ist >> texid;
        else
            texid = 0;
        if (ist.good())
            ist >> alpha;
        else
            alpha = 255;
        if (a1 < 0)
            a1 += 360;
        if (a2 < 0)
            a2 += 360;
        if (!ist || !ist.eof() || ro <= 0 || ri < 0 || ri >= ro || (a1 != 0 && a1 == a2) || a1 < 0 || a2 < 0 || a1 >= 360 || a2 >= 360 || alpha < 0 || alpha > 255 || texid < 0 || texid > 7) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
        x *= plw / scalex;
        y *= plh / scaley;
        ro *= plh / scaley;
        ri *= plh / scaley;
        Room& rm = room[crx][cry];
        CircWall* wall = new CircWall(x, y, ro, ri, a1, a2, texid, alpha);
        if (type == 'W')
            rm.addWall(wall);
        else
            rm.addGround(wall);
    }
    else if (command == "R") {  // R x y : set room pointer to (x,y)
        if (label_block) {
            log.error(_("Room line not allowed in label block: $1", line));
            return false;
        }
        ist >> crx >> cry;
        if (!ist || !ist.eof() || crx < 0 || crx >= w || cry < 0 || cry >= h) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
    }
    else if (command == "X") {  // X label x1 y1 [x2 y2] : add walls from label to the rectangle (x1,y1)-(x2,y2)
        if (label_block) {
            log.error(_("Label line not allowed in label block: $1", line));
            return false;
        }
        string nextlabel;
        int rx1, ry1, rx2, ry2;
        ist >> nextlabel >> rx1 >> ry1;
        if (ist.good())
            ist >> rx2 >> ry2;
        else {  // one room only
            rx2 = rx1;
            ry2 = ry1;
        }
        if (!ist || !ist.eof() || rx1 < 0 || rx2 >= w || rx2 < rx1 || ry1 < 0 || ry2 >= h || ry2 < ry1) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
        for (vector<pair<string, vector<string> > >::const_iterator label = label_lines.begin(); label != label_lines.end(); ++label)
            if (label->first == nextlabel) {
                for (int ry = ry1; ry <= ry2; ry++)
                    for (int rx = rx1; rx <= rx2; rx++) {
                        double sx = scalex;
                        double sy = scaley;
                        for (vector<string>::const_iterator label_line = label->second.begin(); label_line != label->second.end(); ++label_line)
                            if (!parse_line(log, *label_line, label_lines, rx, ry, sx, sy, true))
                                return false;
                    }
                return true;
            }
        log.error(_("Label '$1' not found: $2", nextlabel, line));
        return false;
    }
    else if (command == "P") {
        string name;
        ist >> name;
        if (name == "width") {  // P width w : set map width to w rooms
            if (w != 0) {
                log.error(_("Redefined map width."));
                return false;
            }
            ist >> w;
            if (!ist || !ist.eof() || w < 1 || w > 255) {
                log.error(_("Invalid map line: $1", line));
                return false;
            }
            room.resize(w);
            for (vector<vector<Room> >::iterator ri = room.begin(); ri != room.end(); ++ri)
                ri->resize(h);
        }
        else if (name == "height") {    // P height h : set map height to h rooms
            if (h != 0) {
                log.error(_("Redefined map height."));
                return false;
            }
            ist >> h;
            if (!ist || !ist.eof() || h < 1 || h > 255) {
                log.error(_("Invalid map line: $1", line));
                return false;
            }
            for (vector<vector<Room> >::iterator ri = room.begin(); ri != room.end(); ++ri)
                ri->resize(h);
        }
        else if (name == "title") { // P title text : set map title to text
            if (!title.empty()) {
                log.error(_("Redefined map title."));
                return false;
            }
            getline(ist, title);
            title = trim(title);
            if (title.empty()) {
                log.error(_("Invalid map line: $1", line));
                return false;
            }
        }
        else if (name == "author") {    // P author text : set map author to text
            if (!author.empty()) {
                log.error(_("Redefined map author."));
                return false;
            }
            getline(ist, author);
            author = trim(author);
            if (author.empty()) {
                log.error(_("Invalid map line: $1", line));
                return false;
            }
        }
        else {
            log.error(_("Unrecognized map line: $1", line));
            return false;
        }
    }
    else if (command == "spawn") {  // spawn t rx ry x y : make a spawn spot for team t at room (rx,ry) at (x,y)
        int team, rx, ry;
        double x, y;
        ist >> team >> rx >> ry >> x >> y;
        if (!ist || !ist.eof() || team < 0 || team > 1 || rx < 0 || rx >= w ||
                    ry < 0 || ry >= h || x < 0 || x >= scalex || y < 0 || y >= scaley) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
        const WorldCoords spot(rx, ry, static_cast<int>(x * plw / scalex), static_cast<int>(y * plh / scaley)); // cast to int to keep identical behaviour with previous versions
        tinfo[team].spawn.push_back(spot);
    }
    else if (command == "flag") {   // flag t rx ry x y : set team t's flag position to room (rx,ry) at (x,y)
        int team, rx, ry;
        double x, y;
        ist >> team >> rx >> ry >> x >> y;
        if (!ist || !ist.eof() || team < 0 || team > 2 || rx < 0 || rx >= w ||
                    ry < 0 || ry >= h || x < 0 || x >= scalex || y < 0 || y >= scaley) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
        const WorldCoords flag(rx, ry, static_cast<int>(x * plw / scalex), static_cast<int>(y * plh / scaley)); // cast to int to keep identical behaviour with previous versions
        if (team < 2)
            tinfo[team].flags.push_back(flag);
        else
            wild_flags.push_back(flag);
    }
    else if (command == "V") {  // V * : version specific extensions (originally meant as some kind of version number but ignored by all versions of Outgun)
        string subCommand;
        ist >> subCommand;
        if (subCommand == "respawn") { // V respawn t rx ry [x1 y1 [x2 y2]] : make a respawn area for team t at room (rx,ry) at (x1,y1) to (x2,y2); t=2 means both teams
            int team, rx, ry;
            double x1, y1, x2, y2;
            bool point = false;
            ist >> team >> rx >> ry;
            if (ist.good()) {
                ist >> x1 >> y1;
                if (ist.good()) {
                    ist >> x2 >> y2;
                    if (x2 < x1)
                        swap(x1, x2);
                    if (y2 < y1)
                        swap(y1, y2);
                }
                else {
                    x2 = x1;
                    y2 = y1;
                    point = true;
                }
            }
            else {
                x1 = y1 = 0;
                x2 = scalex;
                y2 = scaley;
            }
            double cx1 = x1 * plw / scalex, cy1 = y1 * plh / scaley;
            double cx2 = x2 * plw / scalex, cy2 = y2 * plh / scaley;
            if (!point) {
                cx1 += PLAYER_RADIUS; cy1 += PLAYER_RADIUS;
                cx2 -= PLAYER_RADIUS; cy2 -= PLAYER_RADIUS;
            }
            if (!ist || !ist.eof() || team < 0 || team > 2 || rx < 0 || rx >= w ||
                  ry < 0 || ry >= h || x1 < 0 || y1 < 0 ||
                  cx2 < cx1 || x2 > scalex || cy2 < cy1 || y2 > scaley) { // although initially ordered, cx2 < cx1 is possible because of PLAYER_RADIUS adjustments above
                log.error(_("Invalid map line: $1", line));
                return false;
            }
            const WorldRect area(rx, ry, cx1, cy1, cx2, cy2);
            if (team < 2)
                tinfo[team].respawn.push_back(area);
            else {
                tinfo[0].respawn.push_back(area);
                tinfo[1].respawn.push_back(area);
            }
        }
        // silently accept unknown subcommands with no action; they might be a version number or an extension in a future version of Outgun (or a typo; bad luck ;))
    }
    else if (command == "S") {  // S x y : set map scale
        ist >> scalex >> scaley;
        if (!ist || !ist.eof() || scalex <= 0 || scaley <= 0) {
            log.error(_("Invalid map line: $1", line));
            return false;
        }
    }
    else {
        log.error(_("Unrecognized map line: $1", line));
        return false;
    }
    return true;
}

MapInfo::MapInfo() throw () : random(false), over_edge(false), votes(0), sentVotes(0), last_game(0), highlight(false) { }

bool MapInfo::load(LogSet& log, const string& mapName) throw () {
    Map map;
    const bool ok = map.load(log, SERVER_MAPS_DIR, mapName);
    if (!ok)
        return false;
    file = mapName;
    title = map.title;
    author = map.author;
    width = map.w;
    height = map.h;
    random = false;
    over_edge = false;
    votes = sentVotes = 0;
    return true;
}

void MapInfo::update(const Map& map) throw () {
    title = map.title;
    author = map.author;
    width = map.w;
    height = map.h;
}

void PlayerBase::clear(bool enable, int _pid, const string& _name, int team_id) throw () {
    ping = 0;
    id = _pid;
    name = _name;
    item_deathbringer = item_power = item_turbo = false;
    item_shield = 0;
    visibility = 255;
    roomx = roomy = 0;
    lx = ly = sx = sy = 0;
    gundir.from8way(0);
    controls = ClientControls();
    dead = false;
    reg_status = ClientLoginStatus();   // clear
    score = 0;
    neg_score = 0;
    rank = 0;
    team_nr = team_id;
    stats().clear(false);
    stats().set_start_time(static_cast<int>(get_time()));
    personal_color = invalid_color;
    used = enable;
}

void ServerPlayer::clear(bool enable, int _pid, int _cid, const string& _name, int team_id, unsigned uniqueId_) throw () {
    attack = attackOnce = false;
    oldfrags = -666;
    want_map_exit = false;      //by default don't want change maps
    mapVote = -1;
    idleFrames = 0;
    kickTimer = 0;
    muted = 0;
    want_change_teams = false;  // don't want to change teams yet
    team_change_time = 0;
    next_shoot_frame = 0;
    talk_temp = 0.0;
    talk_hotness = 1.0;
    cid = _cid;
    waitnametime = get_time() - 666.0;  //can change name right now
    localIP = false;

    lastClientFrame = 0;
    frameOffset = 0;
    awaiting_client_readies = 0;
    deathbringer_end = 0;
    deathbringer_attacker = 0;
    item_power_time = item_turbo_time = item_shadow_time = 0;
    health = energy = 0;
    megabonus = 0;
    weapon = 1;
    drop_key = false;
    dropped_flag = false;
    frames_to_respawn = extra_frames_to_respawn = 0;
    respawn_to_base = false;
    fav_col.clear();
    for (char i = 0; i < 16; ++i)
        fav_col.push_back(i);
    random_shuffle(fav_col.begin(), fav_col.end());

    bot = false;
    record_position = true;

    current_map_list_item = 0;
    nextMinimapPlayer = 0;
    minimapPlayersPerFrame = 2;

    protocolExtensionsLevel = -1;
    toldAboutExtensionAdvantage = false;

    uniqueId = uniqueId_;

    PlayerBase::clear(enable, _pid, _name, team_id);
}

void ClientPlayer::clear(bool enable, int _pid, const string& _name, int team_id) throw () {
    item_power_time = item_turbo_time = item_shadow_time = 0;
    health = energy = 0;
    weapon = 1;

    next_turbo_effect_time = wall_sound_time = player_sound_time = 0;
    onscreen = false;
    deathbringer_affected = false;
    next_smoke_effect_time = 0;
    hitfx = 0;
    oldx = oldy = 0;
    posUpdated = -1e10;

    PlayerBase::clear(enable, _pid, _name, team_id);
}

/* bounceFromPoint():
 *
 * calculates how many times the vector (mx,my) can be traveled until point (dx,dy) is hit by a circle of radius r
 *
 *
 *        (mx,my)      __--
 *          \    __--^^
 *         __+-^^  + -(dx,dy)
 *     +-^^
 *   (0,0)
 *
 * | t(mx,my)-(dx,dy) | = r
 * take the smaller solution of t (if any real solution exists)
 *
 * d? = distance vector of the point, m? = movement vector of the circle, r = radius of the circle
 * returns: pair( t, pair(collisionn-centern, collisionp-centerp) ) or pair(1e99, ...) for no collision
 */
BounceData bounceFromPoint(double dx, double dy, double mx, double my, double r) throw () {
    const double m2 = mx * mx + my * my, r2 = r * r;
    const double mdotd = mx * dx + my * dy;
    const double d2 = dx * dx + dy * dy;
    const double disc = mdotd * mdotd - m2 * (d2 - r2);
    if (disc >= 0) {    // there are real solutions
        const double t = (mdotd - sqrt(disc)) / m2; // the collision with smaller t (the larger t is when going away from the point)
        if (t >= 0)
            return BounceData(t, Coords(dx - t * mx, dy - t * my));
    }
    return BounceData(1e99, Coords());  // no collision
}

/* bounceFromLine():
 *
 * calculates how many times the vector (mx,my) can be traveled until wall (dx1,dy1)-(dx2,dy2) is hit by a circle of radius r
 * hits to the end points aren't detected by this function
 *
 *
 *        (mx,my)      __--
 *          \    __--^^
 *         __+-^^  + -(dx1,dy1)
 *     +-^^      .>|
 *   (0,0)      /  + -(dx2,dy2)
 *             /
 *           wall
 *
 * the circle hits the wall proper with its center projection on the line
 * | ( t(mx,my)-(dx1,dy1) ) x ( (dx2,dy2)-(dx1,dy1) ) | / | (dx2,dy2)-(dx1,dy1) | = r
 * take the smaller solution of t and make sure the point is on the line
 *
 * d?? = distance vectors of the line's end-points, m? = movement vector of the circle, r = radius of the circle
 * returns: pair( t, pair(collisionn-centern, collisionp-centerp) ) or pair(1e99, ...) for no collision
 */
BounceData bounceFromLine(double dx1, double dy1, double dx2, double dy2, double mx, double my, double r) throw () {
    // t * ( mx(dy2-dy1) - my(dx2-dx1) ) = dx1(dy2-dy1) - dy1(dx2-dx1) +-R*|(dx2,dy2)-(dx1,dy1)|
    const double diffx = dx2 - dx1, diffy = dy2 - dy1;
    const double div = mx * diffy - my * diffx;
    if (div != 0) { // div == 0 <=> movement is parallel to the line => no collisions possible
        const double rBase = (dx1 * diffy - dy1 * diffx) / div;
        const double rAdd = r * sqrt(diffx * diffx + diffy * diffy) / div;
        const double t = rBase - fabs(rAdd);    // the collision with smaller t (the larger t is when going away from the line)
        if (t >= 0) {
            // make sure we are not off an end of the line
            // this can surely be calculated in a simpler way, but this first came to mind
            // collp = p1 + k(p2-p1)    0<=k<=1 if on the line
            // | t*m - collp |  minimum (=r)
            // | t*m - p1 - k(p2-p1) |  minimum (=r)
            // ( t*mx - dx1 - k(dx2-dx1) )^2 + ( t*my - dy1 - k(dy2-dy1) )^2  minimum (=r^2)
            // (dx2-dx1)*( t*mx - dx1 - k(dx2-dx1) ) + (dy2-dy1)*( t*my - dy1 - k(dy2-dy1) ) = 0  (derivative of the expression above *(-.5))
            // (dx2-dx1)*(t*mx-dx1) + (dy2-dy1)*(t*my-dy1) = k[ (dx2-dx1)^2 + (dy2-dy1)^2 ]
            const double k = (diffx * (t * mx - dx1) + diffy * (t * my - dy1)) / (diffx * diffx + diffy * diffy);
            if (k >= 0. && k <= 1.)
                return BounceData(t, Coords(dx1 + k * diffx - t * mx, dy1 + k * diffy - t * my));
        }
    }
    return BounceData(1e99, Coords());  // no collision
}

/* bounceFromArc():
 *
 * calculates how many times the vector (mx,my) can be traveled until the arc is hit by a circle of radius cr
 *
 *
 *        (mx,my)      __--
 *          \    __--^^
 *         __+-^^   /^^^^
 *     +-^^        /
 *   (0,0) av,____|____.<-- (dx,dy)
 *           `    |     }
 *                 \    } ar
 *                  \___}
 *
 * the circle hits the arc proper with its center at a distance of ar+cr (if outside) or ar-cr (if inside) from the arc's radial center, and within the given angle from arc center vector
 *
 * | t(mx,my)-(dx,dy) | = ar+-cr , taking the smaller solution of t and making sure the position is within the given angle from av
 *
 * d? = distance vector of the arc's radial center, m? = movement vector of the circle, ar = radius of the arc, cr = radius of the moving circle
 * av = arc center unit vector, ahwcos = cosine of half arc "width" (angle)
 * returns: pair( t, pair(collisionn-centern, collisionp-centerp) ) or pair(1e99, ...) for no collision
 */
BounceData bounceFromArc(double dx, double dy, double mx, double my, const Coords& av, double ahwcos, double ar, double cr, bool outside) throw () {
    const double bounceRad = ar + (outside ? cr : -cr);
    const double m2 = mx * mx + my * my, r2 = bounceRad * bounceRad;
    const double mdotd = mx * dx + my * dy;
    const double d2 = dx * dx + dy * dy;
    const double disc = mdotd * mdotd - m2 * (d2 - r2);
    if (disc >= 0) {    // there are real solutions
        double t;
        if (outside)
            t = (mdotd - sqrt(disc)) / m2;  // the collision with smaller t (the larger t is when going away from the center)
        else
            t = (mdotd + sqrt(disc)) / m2;  // the collision with larger t (the smaller t is when going towards the center)
        if (t >= 0) {
            // make sure the point is within the given angle from av
            // [ (t(mx,my) - (dx,dy)) dot av ] / [ |t(mx,my) - (dx,dy)| * |av| ] >= ahwcos
            const double xd = t * mx - dx, yd = t * my - dy;
            const double dot = xd * av.first - yd * av.second;  //NOTE: - because av.second is in reversed coordinates
            if (dot >= ahwcos * bounceRad) {    // |(dx,dy) - t(mx,my)| = bounceRad, |av| = 1
                // calc the vector from t(mx,my) to collision point:
                // length = cr
                // (xd,yd) = along that vector, direction from radial center to center of circle, length bounceRad
                const double mul = (ar - bounceRad) / bounceRad;
                return BounceData(t, Coords(xd * mul, yd * mul));
            }
        }
    }
    return BounceData(1e99, Coords());
}

void RectWall::tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw () {
    #define add_rv() if (rv.first < bd->first) *bd = rv;

    BounceData rv;
    rv.first = 1e99;
    bool onLine = false;
    if (mx > 0 && a > stx)  // check vertical wall a
        rv = bounceFromLine(a - stx, b - sty, a - stx, d - sty, mx, my, plyRadius);
    else if (mx < 0 && c < stx) // check vertical wall c
        rv = bounceFromLine(c - stx, b - sty, c - stx, d - sty, mx, my, plyRadius);
    if (rv.first < 1e10) {
        onLine = true;
        add_rv();
    }
    if (my > 0 && b > sty)  // check horizontal wall b
        rv = bounceFromLine(a - stx, b - sty, c - stx, b - sty, mx, my, plyRadius);
    else if (my < 0 && d < sty) // check horizontal wall d
        rv = bounceFromLine(a - stx, d - sty, c - stx, d - sty, mx, my, plyRadius);
    if (rv.first < 1e10) {
        onLine = true;
        add_rv();
    }
    if (!onLine) {  // check corners
        rv = bounceFromPoint(a - stx, b - sty, mx, my, plyRadius);
        add_rv();
        rv = bounceFromPoint(c - stx, b - sty, mx, my, plyRadius);
        add_rv();
        rv = bounceFromPoint(a - stx, d - sty, mx, my, plyRadius);
        add_rv();
        rv = bounceFromPoint(c - stx, d - sty, mx, my, plyRadius);
        add_rv();
    }

    #undef add_rv
}

void TriWall::tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw () {
    #define add_rv() if (rv.first < bd->first) *bd = rv;

    BounceData rv;
    rv = bounceFromLine(p1x - stx, p1y - sty, p2x - stx, p2y - sty, mx, my, plyRadius); // wall p1-p2
    add_rv();
    rv = bounceFromLine(p1x - stx, p1y - sty, p3x - stx, p3y - sty, mx, my, plyRadius); // wall p1-p3
    add_rv();
    rv = bounceFromLine(p2x - stx, p2y - sty, p3x - stx, p3y - sty, mx, my, plyRadius); // wall p2-p3
    add_rv();
    rv = bounceFromPoint(p1x - stx, p1y - sty, mx, my, plyRadius);
    add_rv();
    rv = bounceFromPoint(p2x - stx, p2y - sty, mx, my, plyRadius);
    add_rv();
    rv = bounceFromPoint(p3x - stx, p3y - sty, mx, my, plyRadius);
    add_rv();

    #undef add_rv
}

void CircWall::tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw () {
    #define add_rv() if (rv.first < bd->first) *bd = rv;

    BounceData rv;
    // outside
    rv = bounceFromArc(x - stx, y - sty, mx, my, midvec, anglecos, ro, plyRadius, true);
    add_rv();
    // inside
    if (ri > plyRadius) {
        rv = bounceFromArc(x - stx, y - sty, mx, my, midvec, anglecos, ri, plyRadius, false);
        add_rv();
    }
    if (angle[0] == angle[1])   // no sectoring
        return;
    double p1x = x + ro * va1.first - stx, p1y = y - ro * va1.second - sty; //NOTE: - ...*va1.second because va1.second is in reversed coordinates
    double p2x = x + ri * va1.first - stx, p2y = y - ri * va1.second - sty; //NOTE: - ...*va1.second because va1.second is in reversed coordinates
    // side wall at angle va1
    rv = bounceFromLine(p1x, p1y, p2x, p2y, mx, my, plyRadius);
    add_rv();
    // corners at angle va1
    rv = bounceFromPoint(p1x, p1y, mx, my, plyRadius);
    add_rv();
    rv = bounceFromPoint(p2x, p2y, mx, my, plyRadius);
    add_rv();
    // side wall at angle va2
    p1x = x + ro * va2.first - stx, p1y = y - ro * va2.second - sty;    //NOTE: - ...*va2.second because va2.second is in reversed coordinates
    p2x = x + ri * va2.first - stx, p2y = y - ri * va2.second - sty;    //NOTE: - ...*va2.second because va2.second is in reversed coordinates
    rv = bounceFromLine(p1x, p1y, p2x, p2y, mx, my, plyRadius);
    add_rv();
    // corners at angle va2
    rv = bounceFromPoint(p1x, p1y, mx, my, plyRadius);
    add_rv();
    rv = bounceFromPoint(p2x, p2y, mx, my, plyRadius);
    add_rv();

    #undef add_rv
}

BounceData WorldBase::getTimeTillBounce(const Room& room, const PlayerBase& pl, double plyRadius, double maxFraction) throw () {
    return room.genGetTimeTillWall(pl.lx, pl.ly, pl.sx, pl.sy, plyRadius, maxFraction);
}

double WorldBase::getTimeTillWall(const Room& room, const Rocket& rock, double maxFraction) throw () {
    return room.genGetTimeTillWall(rock.x, rock.y, rock.sx, rock.sy, ROCKET_RADIUS, maxFraction).first;
}

double WorldBase::getTimeTillCollision(const PlayerBase& pl, const Rocket& rock, double collRadius) throw () {
    const double dx = rock.x - pl.lx, dy = rock.y - pl.ly, r2 = collRadius * collRadius;
    if (dx * dx + dy * dy < r2)
        return 0;
    const double mx = pl.sx - rock.sx, my = pl.sy - rock.sy;

    const double m2 = mx * mx + my * my;
    const double mdotd = mx * dx + my * dy;
    const double d2 = dx * dx + dy * dy;
    const double disc = mdotd * mdotd - m2 * (d2 - r2);
    if (disc >= 0) {    // there are real solutions
        const double t = (mdotd - sqrt(disc)) / m2;   // the collision with smaller t (the larger t is when going away)
        if (t >= 0)
            return t;
    }
    return 1e99;
}

double WorldBase::getTimeTillCollision(const PlayerBase& pl1, const PlayerBase& pl2, double collRadius) throw () {
    const double dx = pl2.lx - pl1.lx, dy = pl2.ly - pl1.ly, r2 = collRadius * collRadius;
    if (dx * dx + dy * dy < r2)
        return 1e99;
    const double mx = pl1.sx - pl2.sx, my = pl1.sy - pl2.sy;

    const double m2 = mx * mx + my * my;
    const double mdotd = mx * dx + my * dy;
    const double d2 = dx * dx + dy * dy;
    const double disc = mdotd * mdotd - m2 * (d2 - r2);
    if (disc >= 0) {    // there are real solutions
        const double t = (mdotd - sqrt(disc)) / m2; // the collision with smaller t (the larger t is when going away)
        if (t >= 0)
            return t;
    }
    return 1e99;
}

void WorldBase::applyPlayerAcceleration(int pid) throw () {
    PlayerBase* h = player[pid].getPtr();

    double player_accel = physics.accel;
    if (h->item_turbo)
        player_accel *= physics.turbo_mul;
    if (h->controls.isRun()) {
        player_accel *= physics.run_mul;

        // check possible flag penalty
        bool flag_penalty = false;
        const Team& enemy = teams[1 - pid / TSIZE];
        for (vector<Flag>::const_iterator fi = enemy.flags().begin(); fi != enemy.flags().end(); ++fi)
            if (fi->carrier() == pid) {
                flag_penalty = true;
                break;
            }
        if (!flag_penalty)
            for (vector<Flag>::const_iterator fi = wild_flags.begin(); fi != wild_flags.end(); ++fi)
                if (fi->carrier() == pid) {
                    flag_penalty = true;
                    break;
                }
        if (flag_penalty)
            player_accel *= physics.flag_mul;
    }

    // apply drag: v -= v * drag
    h->sx -= h->sx * physics.drag;
    h->sy -= h->sy * physics.drag;

    // apply friction: v -= fric
    const double spd = sqrt(h->sx * h->sx + h->sy * h->sy);
    if (spd <= physics.fric || spd < .001)  // the test on .001 is because fric <= 0 is allowed but spd == 0 doesn't work in the formula below
        h->sx = h->sy = 0;
    else {
        // v = v - fric
        const double mul = 1 - physics.fric / spd;
        h->sx *= mul;
        h->sy *= mul;
    }

    if (h->under_deathbringer_effect(get_time()))
        return;

    double sideAcc = (h->controls.isRight() ? 1 : 0) - (h->controls.isLeft() ? 1 : 0);
    double forwAcc = (h->controls.isUp   () ? 1 : 0) - (h->controls.isDown() ? 1 : 0);

    if (sideAcc == 0 && forwAcc == 0)
        return;
    if (sideAcc != 0 && forwAcc != 0) {   // normalize the total acceleration vector
        sideAcc /= sqrt(2.);
        forwAcc /= sqrt(2.);
    }

    double xAcc, yAcc;
    if (h->accelerationMode == AM_World || !physics.allowFreeTurning) {
        xAcc = sideAcc;
        yAcc = -forwAcc;
    }
    else {
        const double dirX = cos(h->gundir.toRad()), dirY = sin(h->gundir.toRad());
        xAcc = dirX * forwAcc - dirY * sideAcc;
        yAcc = dirY * forwAcc + dirX * sideAcc;
    }
    if (fabs(h->sx) > .001 || fabs(h->sy) > .001) { // the player is moving in some direction (otherwise, any direction is 'forward')
        // handle different directions : scale braking component by brake_mul and turning component by turn_mul
        // acceleration component parallel to v = par = (a dot v) * v / |v|^2 ; perpendicular component perp = a - par
        const double par_mul = (xAcc * h->sx + yAcc * h->sy) / (h->sx * h->sx + h->sy * h->sy);
        double par_x = par_mul * h->sx, par_y = par_mul * h->sy;
        const double perp_x = xAcc - par_x, perp_y = yAcc - par_y;
        if (par_mul < 0) {  // par is opposite to v == braking
            par_x *= physics.brake_mul;
            par_y *= physics.brake_mul;
        }
        xAcc = perp_x * physics.turn_mul + par_x;
        yAcc = perp_y * physics.turn_mul + par_y;
    }
    h->sx += xAcc * player_accel;
    h->sy += yAcc * player_accel;
}

void WorldBase::returnAllFlags() throw () {
    for (int t = 0; t < 2; t++)
        teams[t].return_all_flags();
    for (vector<Flag>::iterator fi = wild_flags.begin(); fi != wild_flags.end(); ++fi)
        fi->return_to_base();
}

void WorldBase::returnFlag(int team, int flag) throw () {
    if (team == 2)
        wild_flags[flag].return_to_base();
    else
        teams[team].return_flag(flag);
}

void WorldBase::dropFlag(int team, int flag, int px, int py, double x, double y) throw () {
    const WorldCoords pos(px, py, x, y);
    if (team == 2) {
        wild_flags[flag].move(pos);
        wild_flags[flag].drop();
    }
    else
        teams[team].drop_flag(flag, pos);
}

void WorldBase::stealFlag(int team, int flag, int carrier) throw () {
    if (team == 2)
        wild_flags[flag].take(carrier, get_time());
    else
        teams[team].steal_flag(flag, carrier, get_time());
}

void WorldBase::remove_team_flags(int t) throw () {
    nAssert(t >= 0 && t <= 2);
    if (t == 2) {
        map.wild_flags.clear();
        wild_flags.clear();
    }
    else {
        map.tinfo[t].flags.clear();
        teams[t].remove_flags();
    }
}

void WorldBase::add_random_flag(int t) throw () {
    nAssert(t >= 0 && t <= 2);
    for (int i = 0; i < 100; ++i) {
        const int rx = rand() % map.w;
        const int ry = rand() % map.h;
        const int x = rand() % plw;
        const int y = rand() % plh;
        if (!map.fall_on_wall(rx, ry, x, y, FLAG_RADIUS)) {
            WorldCoords pos(rx, ry, x, y);
            if (t == 2) {
                wild_flags.push_back(pos);
                map.wild_flags.push_back(pos);
            }
            else {
                teams[t].add_flag(pos);
                map.tinfo[t].flags.push_back(pos);
            }
            break;
        }
    }
}

void WorldBase::addRocket(int i, int playernum, int team, int px, int py, int x, int y,
                          bool power, GunDirection dir, int xdelta, int frameAdvance, PhysicsCallbacksBase& cb) throw () {
    Rocket& r = rock[i];
    r.owner = playernum;
    r.team = team;
    r.power = power;
    r.px = px;
    r.py = py;
    r.x = x;
    r.y = y;
    r.direction = dir;

    const double deg = dir.toRad();

    if (xdelta) {
        r.sx = xdelta * shot_deltax * cos(deg + N_PI_2);
        r.sy = xdelta * shot_deltax * sin(deg + N_PI_2);
        const double wallTime = getTimeTillWall(map.room[px][py], r, 1.01);
        r.move(1);
        if (wallTime < 1.) {
            cb.rocketHitWall(i, r.power, r.x, r.y, r.px, r.py);
            return;
        }
    }

    r.sx = cos(deg) * physics.rocket_speed;
    r.sy = sin(deg) * physics.rocket_speed;
    // advance 15 pixels before really shooting -> don't hit very close by players
    const double advance = 15. / physics.rocket_speed + double(frameAdvance);
    const double wallTime = getTimeTillWall(map.room[px][py], r, advance * 1.01);
    if (wallTime <= advance) {
        r.move(wallTime);
        cb.rocketHitWall(i, r.power, r.x, r.y, r.px, r.py);
        return;
    }
    r.move(advance);
    if (r.x < 0 || r.x > plw || r.y < 0 || r.y > plh)
        cb.rocketOutOfBounds(i);
}

void WorldBase::shootRockets(PhysicsCallbacksBase& cb, int playernum, int pow, GunDirection dir, const uint8_t* rids, int frameAdvance,
                             int team, bool power, int px, int py, int x, int y) throw () {
    struct RocketFormation {
        int nForward;
        int directions[6];
    };
    static const RocketFormation formations[9] = {
        { 1, { } },
        { 2, { } },
        { 3, { } },
        { 2, { -1, +1 } },
        { 3, { -2, +2 } },
        { 2, { -1, +1, -2, +2 } },
        { 3, { -2, +2, -3, +3 } },
        { 2, { -1, +1, -2, +2, -3, +3 } },
        { 3, { -1, +1, -2, +2, -3, +3 } }
    };
    const RocketFormation& form = formations[pow - 1];

    if (form.nForward == 1)
        addRocket(rids[0], playernum, team, px, py, x, y, power, dir,  0, frameAdvance, cb);
    else if (form.nForward == 2) {
        addRocket(rids[0], playernum, team, px, py, x, y, power, dir, -1, frameAdvance, cb);
        addRocket(rids[1], playernum, team, px, py, x, y, power, dir, +1, frameAdvance, cb);
    }
    else {
        addRocket(rids[0], playernum, team, px, py, x, y, power, dir,  0, frameAdvance, cb);
        addRocket(rids[1], playernum, team, px, py, x, y, power, dir, -2, frameAdvance, cb);
        addRocket(rids[2], playernum, team, px, py, x, y, power, dir, +2, frameAdvance, cb);
    }
    const int* dirp = &form.directions[0];
    for (int ri = form.nForward; ri < pow; ++ri, ++dirp)
        addRocket(rids[ri], playernum, team, px, py, x, y, power, GunDirection(dir).adjust(*dirp), 0, frameAdvance, cb);
}

void ConstFlagIterator::findValid() throw () {
    while (iFlag >= flags->size()) {
        if (++iTeam == 3)
            break;
        setFlags();
        iFlag = 0;
    }
}

double DeathbringerExplosion::radius(double frame) const throw () {
    nAssert(frame >= frame0);
    // change expired() if this calculation changes
    const double delta = (frame - frame0) * 0.1;
    if (delta < 1.)
        return delta * 100.;
    else
        return 100. + sqr(delta - 1.) * 800.;
}

PhysicalSettings::PhysicalSettings() throw () :
    fric        (0.20),
    drag        (0.16),
    accel       (2.60),
    brake_mul   (0.10),
    turn_mul    (0.50),
    run_mul     (1.75),
    turbo_mul   (1.45),
    flag_mul    (0.91),
    rocket_speed(50.),
    friendly_fire(0.),
    friendly_db(0.),
    player_collisions(PC_normal),
    allowFreeTurning(false)
{
    calc_max_run_speed();
}

void PhysicalSettings::calc_max_run_speed() throw () {
    max_run_speed = (run_mul * accel - fric) / drag;
}

void PhysicalSettings::read(BinaryReader& reader) throw () {
    fric            = reader.flt();
    drag            = reader.flt();
    accel           = reader.flt();
    brake_mul       = reader.flt();
    turn_mul        = reader.flt();
    run_mul         = reader.flt();
    turbo_mul       = reader.flt();
    flag_mul        = reader.flt();
    friendly_fire   = reader.flt();
    friendly_db     = reader.flt();
    rocket_speed    = reader.flt();

    const uint8_t bitField = reader.U8();
    player_collisions = static_cast<PlayerCollisions>(bitField & 0x03);
    allowFreeTurning = (bitField & 0x04) != 0;

    unsigned extraBytes = bitField >> 5;
    if (extraBytes == 7)
        extraBytes = unsigned(reader.U8()) + 7;
    reader.block(extraBytes); // ignore data we don't understand

    calc_max_run_speed();
}

void PhysicalSettings::write(BinaryWriter& writer) const throw () {
    writer.flt(fric);
    writer.flt(drag);
    writer.flt(accel);
    writer.flt(brake_mul);
    writer.flt(turn_mul);
    writer.flt(run_mul);
    writer.flt(turbo_mul);
    writer.flt(flag_mul);
    writer.flt(friendly_fire);
    writer.flt(friendly_db);
    writer.flt(rocket_speed);
    const unsigned extraBytes = 0;
    writer.U8(player_collisions | (allowFreeTurning << 2) | (extraBytes << 5));
}

void PowerupSettings::reset() throw () {
    pup_add_time = 40;
    pup_max_time = 100;

    pups_min = 20;
    pups_min_percentage = true;
    pups_max = 80;
    pups_max_percentage = true;
    pups_respawn_time = 25;

    pup_chance_shield = 16;
    pup_chance_turbo = 14;
    pup_chance_shadow = 14;
    pup_chance_power = 14;
    pup_chance_weapon = 18;
    pup_chance_megahealth = 13;
    pup_chance_deathbringer = 11;

    pup_deathbringer_switch = true;
    pup_health_bonus = 160;
    pup_power_damage = 2.0;
    pup_weapon_max = 9;
    pup_shield_hits = 0;
    pup_deathbringer_time = 5.0;
    deathbringer_health_limit = deathbringer_energy_limit = 100;
    deathbringer_health_degradation = deathbringer_energy_degradation = 2.5;

    pups_drop_at_death = false;
    pups_player_max = INT_MAX;

    start_shield = false;
    start_turbo = 0;
    start_shadow = 0;
    start_power = 0;
    start_weapon = 1;
    start_deathbringer = false;
}

Powerup::Pup_type PowerupSettings::choose_powerup_kind() const throw () {
    const int max = pup_chance_shield + pup_chance_turbo + pup_chance_shadow + pup_chance_power
                        + pup_chance_weapon + pup_chance_megahealth + pup_chance_deathbringer;

    int chance = 1 + rand() % max;

    chance -= pup_chance_shield;
    if (chance <= 0) return Powerup::pup_shield;
    chance -= pup_chance_turbo;
    if (chance <= 0) return Powerup::pup_turbo;
    chance -= pup_chance_shadow;
    if (chance <= 0) return Powerup::pup_shadow;
    chance -= pup_chance_power;
    if (chance <= 0) return Powerup::pup_power;
    chance -= pup_chance_weapon;
    if (chance <= 0) return Powerup::pup_weapon;
    chance -= pup_chance_megahealth;
    if (chance <= 0) return Powerup::pup_health;
    //chance -= pup_chance_deathbringer;
    return Powerup::pup_deathbringer;
}

int PowerupSettings::pups_by_percent(int percentage, const Map& map) const throw () {
    const int result = (map.w * map.h * percentage + 50) / 100; // +50 to round properly
    if (result == 0 && percentage > 0)
        return 1;
    if (result > MAX_POWERUPS)
        return MAX_POWERUPS;
    return result;
}

#include "server.h"
//#fix: include needed for funny callback activities - get rid!

const int WorldSettings::shadow_minimum_normal = 7;

void WorldSettings::reset() throw () {
    respawn_time = 2.0;
    extra_respawn_time_alone = 0;
    waiting_time_deathbringer = 4.0;
    respawn_balancing_time = 0;
    respawn_on_capture = false;
    shadow_minimum = shadow_minimum_normal;
    rocket_damage = 70;
    start_health = start_energy = 100;
    health_max = energy_max = 300;
    min_health_for_run_penalty = 40;
    health_regeneration_0_to_100 = 10.;
    energy_regeneration_0_to_100 = 10.;
    health_regeneration_100_to_200 = 1.;
    energy_regeneration_100_to_200 = 5.;
    health_regeneration_200_to_max = 0.;
    energy_regeneration_200_to_max = 0.;
    run_health_degradation = run_energy_degradation = 15.;
    shooting_energy_base = 7.;
    shooting_energy_per_extra_rocket = 1.;
    hit_stun_time = 1.;
    spawn_safe_time = 0.;
    shoot_interval = shoot_interval_with_energy = .5;
    time_limit = 0;     // no time limit
    extra_time = 0;
    sudden_death = false;
    capture_limit = 8;
    win_score_difference = 1;
    flag_return_delay = 1.0;
    balance_teams = TB_disabled;

    random_wild_flag = false;

    lock_team_flags = false;
    lock_wild_flags = false;
    capture_on_team_flag = true;
    capture_on_wild_flag = false;

    see_rockets_distance = 0;

    carrying_score_time = 0;
}

pair<double, double> WorldSettings::getRespawnTime(int playerTeamSize, int enemyTeamSize) const throw () {
    double extraTime = extra_respawn_time_alone;
    if (playerTeamSize > enemyTeamSize && enemyTeamSize != 0)
        extraTime += respawn_balancing_time * (playerTeamSize - enemyTeamSize) / (double)enemyTeamSize;
    return pair<double, double>(respawn_time, extraTime);
}

class ServerPhysicsCallbacks : public PhysicsCallbacksBase {
    ServerWorld& w;

public:
    ServerPhysicsCallbacks(ServerWorld& w_) throw () : w(w_) { }

    bool collideToRockets() const throw () { return true; }
    bool collidesToRockets(int pid) const throw () { return w.frame >= w.player[pid].start_take_damage_frame; }
    bool collidesToPlayers(int pid) const throw () { return w.frame >= w.player[pid].start_take_damage_frame; }
    bool gatherMovementDistance() const throw () { return true; }
    bool allowRoomChange() const throw () { return true; }
    void addMovementDistance(int pid, double dist) throw () { w.addMovementDistanceCallback(pid, dist); }
    void playerScreenChange(int pid) throw () { w.playerScreenChangeCallback(pid); }
    void rocketHitWall(int rid, bool, double, double, int, int) throw () { w.rocketHitWallCallback(rid); }
    bool rocketHitPlayer(int rid, int pid) throw () { return w.rocketHitPlayerCallback(rid, pid); }
    void playerHitWall(int) throw () { }
    PlayerHitResult playerHitPlayer(int pid1, int pid2, double speed) throw () { return w.playerHitPlayerCallback(pid1, pid2, speed); }
    void rocketOutOfBounds(int rid) throw () { w.rocketOutOfBoundsCallback(rid); }
    bool shouldApplyPhysicsToPlayer(int pid) throw () { return w.shouldApplyPhysicsToPlayerCallback(pid); }
};

void ServerWorld::reset() throw () {
    returnAllFlags();
    teams[0].clear_stats();
    teams[1].clear_stats();

    WorldBase::reset();

    for (int i = 0; i < maxplayers; i++)
        if (player[i].used) {
            player[i].stats().clear(true);
            // prepare for respawn
            player[i].respawn_to_base = true;
            respawnPlayer(i, true);   // move to a spawn spot to wait for the game
            resetPlayer(i, -1e10);  // kill; negative delay to cancel default delay, so that the player spawns as soon as he is ready; no need to tell clients because of suppressing the spawn message above
            player[i].respawn_to_base = true;   // always spawn in the base at the beginning of a map
            // don't actually spawn until the client has loaded the map and is in the game
        }
}

void ServerWorld::printTimeStatus(LineReceiver& printer) throw () {
    // server uptime
    const unsigned long uptime = frame / 10 / 60;   // minutes
    const int days = uptime / 60 / 24;
    ostringstream server_time;
    server_time << "The server has been up for ";
    if (days > 0)
        server_time << days << " day" << (days > 1 ? "s " : " ");
    server_time << uptime / 60 % 24 << ':' << setfill('0') << setw(2) << uptime % 60;
    if (days == 0)
        server_time << " hours";
    server_time << '.';
    printer(server_time.str());
    // map time
    const int seconds = getMapTime() / 10;
    ostringstream map_time;
    map_time << "Map time: " << seconds / 60 << ':' << setfill('0') << setw(2) << seconds % 60 << '.';
    if (config.getTimeLimit() == 0)
        map_time << " There is no time limit.";
    else {
        const int remaining_seconds = getTimeLeft() / 10;
        // time limit not very useful when only one player
        if (host->get_player_count() == 1)
            map_time << " No time limit at the moment as there is only one player.";
        else if (remaining_seconds < 0) {
            const int extra_time_seconds = getExtraTimeLeft() / 10;
            if (extra_time_seconds > 0) {
                map_time << " Extra-time left: " << extra_time_seconds / 60 << ':';
                map_time << setfill('0') << setw(2) << extra_time_seconds % 60 << '.';
            }
            if (config.suddenDeath())
                map_time << " Sudden death.";
        }
        else {
            map_time << " Time left: " << remaining_seconds / 60 << ':';
            map_time << setfill('0') << setw(2) << remaining_seconds % 60 << '.';
        }
    }
    printer(map_time.str());
}

void ServerWorld::generate_map(const string& mapdir, const string& file_name, int width, int height, float over_edge, const string& title, const string& author) throw () {
    MapGenerator generator;
    generator.generate(width, height, rand() % 1000 < 1000 * over_edge);
    ofstream out((mapdir + directory_separator + file_name + ".txt").c_str(), ios::binary);
    generator.save_map(out, title, author);
}

bool ServerWorld::load_map(const string& mapdir, const string& mapname, string* buffer) throw () {
    map_start_time = frame;
    const bool success = WorldBase::load_map(log, mapdir, mapname, buffer);
    for (int t = 0; t < 2; t++) {
        teams[t].remove_flags();
        for (vector<WorldCoords>::const_iterator pi = map.tinfo[t].flags.begin(); pi != map.tinfo[t].flags.end(); ++pi)
            teams[t].add_flag(*pi);
    }
    wild_flags.clear();
    for (vector<WorldCoords>::const_iterator pi = map.wild_flags.begin(); pi != map.wild_flags.end(); ++pi)
        wild_flags.push_back(*pi);
    return success;
}

void ServerWorld::returnAllFlags() throw () {
    WorldBase::returnAllFlags();
    net->ctf_net_flag_status(pid_all, 0);
    net->ctf_net_flag_status(pid_all, 1);
    net->ctf_net_flag_status(pid_all, 2);
}

void ServerWorld::returnFlag(int team, int flag) throw () {
    WorldBase::returnFlag(team, flag);
    net->ctf_net_flag_status(pid_all, team);
}

void ServerWorld::dropFlag(int team, int flag, int roomx, int roomy, double lx, double ly) throw () {
    WorldBase::dropFlag(team, flag, roomx, roomy, lx, ly);
    if (team != 2)
        teams[team].set_flag_drop_time(flag, frame / 10.);
    net->ctf_net_flag_status(pid_all, team);
}

void ServerWorld::stealFlag(int team, int flag, int carrier) throw () {
    WorldBase::stealFlag(team, flag, carrier);
    net->ctf_net_flag_status(pid_all, team);
}

bool ServerWorld::dropFlagIfAny(int pid, bool purpose) throw () {
    if (!player[pid].stats().has_flag())
        return false;
    int flag = -1;
    int i = 0;
    int team = 1 - pid / TSIZE;
    for (vector<Flag>::const_iterator fi = teams[team].flags().begin(); fi != teams[team].flags().end(); ++fi, ++i)
        if (fi->carrier() == pid) {
            flag = i;
            break;
        }
    if (flag == -1) {
        team = 2;
        i = 0;
        for (vector<Flag>::const_iterator fi = wild_flags.begin(); fi != wild_flags.end(); ++fi, ++i)
            if (fi->carrier() == pid) {
                flag = i;
                break;
            }
    }
    nAssert(flag != -1);
    player[pid].stats().add_flag_drop(get_time());  // before dropFlag in hopes to alleviate the assertion above
    teams[pid / TSIZE].add_flag_drop();
    dropFlag(team, flag, player[pid].roomx, player[pid].roomy, player[pid].lx, player[pid].ly);
    if (purpose) {  // Otherwise, the reason is dying, and in that case clients know the flag is dropped.
        net->broadcast_flag_drop(player[pid], team);
        host->score_frag(pid, -1);  // undo the bonus from taking the flag
    }
    return true;
}

void ServerWorld::start_game() throw () {
    net->ctf_net_flag_status(pid_record, 0);
    net->ctf_net_flag_status(pid_record, 1);
    net->ctf_net_flag_status(pid_record, 2);
    reset_time();
    // Regenerate powerups
    check_powerup_creation(true);
}

void ServerWorld::respawnPlayer(int pid, bool dontInformClients) throw () {
    const int team = pid / TSIZE;

    WorldCoords pos;
    if (map.tinfo[team].spawn.empty())
        player[pid].respawn_to_base = false;

    if (player[pid].respawn_to_base) {
        //choose a team spawn point
        if (++map.tinfo[team].lastspawn >= map.tinfo[team].spawn.size())
            map.tinfo[team].lastspawn = 0;
        pos = map.tinfo[team].spawn[map.tinfo[team].lastspawn]; // the point
    }
    else if (!map.tinfo[team].respawn.empty()) {
        // choose a team respawn point
        const WorldRect& area = map.tinfo[team].respawn[rand() % map.tinfo[team].respawn.size()];
        pos.px = area.px;
        pos.py = area.py;
        do { // since the areas are checked not to contain too much wall, we are sure to find a space soon enough
            pos.x = area.x1 + rand() % (static_cast<int>(area.x2 - area.x1) + 1);
            pos.y = area.y1 + rand() % (static_cast<int>(area.y2 - area.y1) + 1);
        } while (map.fall_on_wall(pos.px, pos.py, pos.x, pos.y, PLAYER_RADIUS));
    }
    else {
        // generate a random spot for respawn:
        // - unnocupied screen
        // - away from walls

        //calculate room touch matrix
        vector<bool> roompop(map.w * map.h, false);
        for (int i = 0; i < maxplayers; i++)
            if (player[i].used && player[i].roomx >= 0 && player[i].roomy >= 0 && player[i].roomx < map.w && player[i].roomy < map.h)
                roompop[player[i].roomy * map.w + player[i].roomx] = true;

        int runaway = 400;
        do {
            //find screen
            int ridx;
            do {
                ridx = rand() % (map.w * map.h);
            } while (runaway-- > 200 && roompop[ridx]); //keep trying until unnocupied (==false)
            pos.px = ridx % map.w;
            pos.py = ridx / map.w;

            //find suitable coordinates
            pos.x = PLAYER_RADIUS + rand() % (plw - 2 * PLAYER_RADIUS);
            pos.y = PLAYER_RADIUS + rand() % (plh - 2 * PLAYER_RADIUS);

            //do a check for walls, maybe retrying another screen if hits a wall
            if (!map.fall_on_wall(pos.px, pos.py, pos.x, pos.y, PLAYER_RADIUS))
                break;  //success!

            //fall on wall true, keep trying...

        } while (runaway-- > 0);
    }

    //put player there
    player[pid].roomx = pos.px; //screen
    player[pid].roomy = pos.py;
    player[pid].lx = pos.x; //screen position
    player[pid].ly = pos.y;

    //reset speeds / z
    player[pid].sx = 0;
    player[pid].sy = 0;

    //reset player attributes
    player[pid].health = config.start_health;
    player[pid].energy = config.start_energy;
    player[pid].megabonus = 0;

    player[pid].weapon = pupConfig.start_weapon;

    net->sendWeaponPower(pid);

    player[pid].item_shield = pupConfig.start_shield;
    player[pid].item_power = pupConfig.start_power;
    player[pid].item_power_time = get_time() + pupConfig.start_power;
    player[pid].item_turbo = pupConfig.start_turbo;
    player[pid].item_turbo_time = get_time() + pupConfig.start_turbo;
    player[pid].visibility = pupConfig.start_shadow ? config.getShadowMinimum() : 255;
    player[pid].item_shadow_time = get_time() + pupConfig.start_shadow;
    player[pid].item_deathbringer = pupConfig.start_deathbringer;
    player[pid].deathbringer_end = 0;

    player[pid].respawn_to_base = false;

    player[pid].next_shoot_frame = player[pid].start_take_damage_frame = frame + config.get_spawn_safe_time_frames();
    player[pid].attackOnce = false;

    player[pid].dead = false;

    player[pid].stats().spawn(get_time());

    if (!dontInformClients)
        net->broadcast_spawn(player[pid]);

    if (pupConfig.start_weapon > 1)
        net->sendWeaponPower(pid);
    if (pupConfig.start_power)
        net->sendPupTime(pid, Powerup::pup_power, pupConfig.start_power);
    if (pupConfig.start_turbo)
        net->sendPupTime(pid, Powerup::pup_turbo, pupConfig.start_turbo);
    if (pupConfig.start_shadow)
        net->sendPupTime(pid, Powerup::pup_shadow, pupConfig.start_shadow);

    //for all effects, player screen changed
    game_player_screen_change(pid);
}

//flag touched by player?
bool ServerWorld::check_flag_touch(const Flag& flag, int px, int py, double x, double y) throw () {
    //carried and in different screen can't be touched
    if (flag.carried() || flag.position().px != px || flag.position().py != py)
        return false;

    const double dx = flag.position().x - x;
    const double dy = flag.position().y - y;
    const int touchRadius = PLAYER_RADIUS + FLAG_RADIUS;

    return dx * dx + dy * dy < touchRadius * touchRadius;
}

// drop shield, turbo, shadow, power or weapon power-up if player has some of them
void ServerWorld::drop_powerup(const ServerPlayer& player) throw () {
    vector<Powerup::Pup_type> player_items;
    if (player.item_shield)         // only at suicides
        player_items.push_back(Powerup::pup_shield);
    if (player.item_turbo && player.item_turbo_time - get_time() >= pupConfig.pup_add_time / 2)
        player_items.push_back(Powerup::pup_turbo);
    if (player.item_shadow() && player.item_shadow_time - get_time() >= pupConfig.pup_add_time / 2)
        player_items.push_back(Powerup::pup_shadow);
    if (player.item_power && player.item_power_time - get_time() >= pupConfig.pup_add_time / 2)
        player_items.push_back(Powerup::pup_power);
    if (player.weapon >= 2)
        player_items.push_back(Powerup::pup_weapon);

    if (player_items.empty())       // nothing to drop
        return;

    for (int p = 0; p < MAX_POWERUPS; p++)
        if (item[p].kind == Powerup::pup_unused) {
            item[p].kind = player_items[rand() % player_items.size()];
            item[p].px = player.roomx;
            item[p].py = player.roomy;
            item[p].x = static_cast<int>(player.lx);
            item[p].y = static_cast<int>(player.ly);
            // inform players
            for (int i = 0; i < maxplayers; i++)
                if (this->player[i].used && this->player[i].roomx == item[p].px && this->player[i].roomy == item[p].py)
                    net->sendPowerupVisible(i, p, item[p]);
            if (host->recording_active())
                net->sendPowerupVisible(pid_record, p, item[p]);
            break;
        }
}

void ServerWorld::respawn_powerup(int p) throw () {
    item[p].kind = Powerup::pup_unused;

    //find a screen with no players and no other powerups
    int px, py, itemx, itemy;
    for (int runaway = 300; ; --runaway) {
        bool hit = false;
        px = rand() % map.w;
        py = rand() % map.h;

        //check for players if not tried a 100 times yet

        //check players
        if (runaway > 200)
            for (int i = 0; i < maxplayers; i++)
                if (player[i].used && player[i].roomx == px && player[i].roomy == py) {
                    hit = true;
                    break;
                }
        if (hit)
            continue;

        //check for items if not tried 200 times yet

        //check items if no players found
        if (runaway > 100)
            for (int i = 0; i < MAX_POWERUPS; i++)
                if (item[i].kind != Powerup::pup_unused && item[i].px == px && item[i].py == py) {
                    hit = true;
                    break;
                }
        if (hit)
            continue;

        //find a suitable coordinate -- middle square
        //itemx = plw / 8 + rand() % (3 * plw / 4);
        //itemy = plh / 8 + rand() % (3 * plh / 4);
        itemx = POWERUP_RADIUS + rand() % (plw - 2 * POWERUP_RADIUS);
        itemy = POWERUP_RADIUS + rand() % (plh - 2 * POWERUP_RADIUS);

        //do a check for walls, maybe retrying another screen if hits a wall
        hit = map.fall_on_wall(px, py, itemx, itemy, POWERUP_RADIUS);
        if (!hit)
            break;
        if (--runaway < 0)
            return;
    }
    item[p].kind = pupConfig.choose_powerup_kind();
    item[p].px = px;
    item[p].py = py;
    item[p].x = itemx;
    item[p].y = itemy;
    for (int i = 0; i < maxplayers; i++)
        if (player[i].used && player[i].roomx == px && player[i].roomy == py)
            net->sendPowerupVisible(i, p, item[p]);

    if (host->recording_active())
        net->sendPowerupVisible(pid_record, p, item[p]);
}

void ServerWorld::check_powerup_creation(bool instant) throw () {
    //count number of items
    int ic = 0;
    for (int i = 0; i < MAX_POWERUPS; i++)
        if (item[i].kind != Powerup::pup_unused)
            ic++;
    const int pc = host->get_player_count();

    int real_min = pupConfig.getMinPups(map);
    const int real_max = pupConfig.getMaxPups(map);
    if (pc > real_min)
        real_min = pc;
    if (real_min > real_max)
        real_min = real_max;
    if (ic >= real_min)
        return;
    //while number of players > number of powerups: create a powerup and ic++
    for (int i = 0; i < MAX_POWERUPS;  i++)
        if (item[i].kind == Powerup::pup_unused) {
            item[i].kind = Powerup::pup_respawning;
            if (instant)
                respawn_powerup(i);
            else
                item[i].respawn_time = get_time() + pupConfig.getRespawnTime();
            if (++ic >= real_min)
                break;
        }
}

void ServerWorld::game_touch_powerup(int pid, int pk) throw () {
    Powerup& it = item[pk];
    ServerPlayer& pl = player[pid];

    net->broadcastPowerupPicked(it.px, it.py, pk);

    // Check which powerups player has.
    bool pups[Powerup::pup_last_real + 1];
    int pup_count = 0;
    if (pups[Powerup::pup_shield      ] = pl.item_shield)        pup_count++;
    if (pups[Powerup::pup_turbo       ] = pl.item_turbo)         pup_count++;
    if (pups[Powerup::pup_shadow      ] = pl.item_shadow())      pup_count++;
    if (pups[Powerup::pup_power       ] = pl.item_power)         pup_count++;
    if (pups[Powerup::pup_weapon      ] = pl.weapon > 1)         pup_count++;
        pups[Powerup::pup_health      ] = true;
    if (pups[Powerup::pup_deathbringer] = pl.item_deathbringer)  pup_count++;

    if (!pups[it.kind] && pup_count >= pupConfig.pups_player_max)   // Drop one if necessary.
        drop_worst_powerup(pl);

    switch (it.kind) {
    /*break;*/ case Powerup::pup_shield: {
            pl.item_shield = pupConfig.pup_shield_hits ? pupConfig.pup_shield_hits : 1;

            //increase health to minimum of 100
            if (pl.health < 100)
                pl.health = 100;        //full health

            //increase energy +100
            if (pl.energy < 200) {
                pl.energy += 100;
                if (pl.energy > 200)
                    pl.energy = 200;
            }

            net->broadcast_screen_sample(pl.id, SAMPLE_SHIELD_POWERUP);
        }
        break; case Powerup::pup_turbo: {
            double itemTime = pl.item_turbo_time - get_time();
            if (!pl.item_turbo || itemTime < 0)
                itemTime = 0;
            itemTime = pupConfig.addTime(itemTime);

            pl.item_turbo = true;
            pl.item_turbo_time = get_time() + itemTime;

            net->sendPupTime(pl.id, it.kind, itemTime);
            net->broadcast_screen_sample(pl.id, SAMPLE_TURBO_ON);
        }
        break; case Powerup::pup_shadow: {
            double itemTime = pl.item_shadow_time - get_time();
            if (!pl.item_shadow() || itemTime < 0)
                itemTime = 0;
            itemTime = pupConfig.addTime(itemTime);

            pl.visibility = config.getShadowMinimum();
            pl.item_shadow_time = get_time() + itemTime;

            net->sendPupTime(pl.id, it.kind, itemTime);
            net->broadcast_screen_sample(pl.id, SAMPLE_SHADOW_ON);
        }
        break; case Powerup::pup_power: {
            double itemTime = pl.item_power_time - get_time();
            if (!pl.item_power || itemTime < 0)
                itemTime = 0;
            itemTime = pupConfig.addTime(itemTime);

            pl.item_power = true;
            pl.item_power_time = get_time() + itemTime;

            net->sendPupTime(pl.id, it.kind, itemTime);
            net->broadcast_screen_sample(pl.id, SAMPLE_POWER_ON);
        }
        break; case Powerup::pup_weapon: {
            if (pl.energy < 200) {
                pl.energy += 100;
                if (pl.energy > 200)
                    pl.energy = 200;
            }
            if (pl.weapon < pupConfig.pup_weapon_max) {
                pl.weapon++;
                net->sendWeaponPower(pl.id);
            }
            net->broadcast_screen_sample(pl.id, SAMPLE_WEAPON_UP);
        }
        break; case Powerup::pup_health: {
            pl.megabonus += pupConfig.pup_health_bonus;
            net->broadcast_screen_sample(pl.id, SAMPLE_MEGAHEALTH);
        }
        break; case Powerup::pup_deathbringer: {
            if (pupConfig.getDeathbringerSwitch())
                pl.item_deathbringer = !pl.item_deathbringer;
            else
                pl.item_deathbringer = true;

            net->broadcast_screen_sample(pl.id, SAMPLE_GETDEATHBRINGER);
        }
        break; default: nAssert(0);
    }

    // unused item
    it.kind = Powerup::pup_unused;

    // check powerup creation
    check_powerup_creation(false);
}

void ServerWorld::drop_worst_powerup(ServerPlayer& pl) throw () {
    if (pl.item_turbo || pl.item_shadow() || pl.item_power) {
        double mintime = 1e50;
        if (pl.item_turbo)
            mintime = pl.item_turbo_time;
        if (pl.item_shadow() && pl.item_shadow_time < mintime)
            mintime = pl.item_shadow_time;
        if (pl.item_power && pl.item_power_time < mintime)
            pl.item_power_time = 0;
        else if (pl.item_turbo && pl.item_turbo_time == mintime)
            pl.item_turbo_time = 0;
        else {
            nAssert(pl.item_shadow() && pl.item_shadow_time == mintime);
            pl.item_shadow_time = 0;
        }
        return; // simulateFrame() informs the client
    }

    if (pl.item_deathbringer) {
        pl.item_deathbringer = false;
        net->broadcast_screen_sample(pl.id, SAMPLE_GETDEATHBRINGER);
    }
    else if (pl.item_shield) {
        pl.item_shield = 0;
        net->broadcast_screen_sample(pl.id, SAMPLE_SHIELD_LOST);
    }
    else {
        pl.weapon = 1;
        net->sendWeaponPower(pl.id);
    }
}

void ServerWorld::game_player_screen_change(int p) throw () {
    player[p].record_position = true;
    //check for new powerups visible
    for (int i = 0; i < MAX_POWERUPS; i++) {
        const Powerup& it = item[i];
        if (it.kind != Powerup::pup_unused && it.kind != Powerup::pup_respawning &&
            it.px == player[p].roomx && it.py == player[p].roomy)
                net->sendPowerupVisible(p, i, item[i]);
    }
    // check for rockets visible to the new room
    for (int i = 0; i < MAX_ROCKETS; ++i)
        if (rock[i].owner != -1 && !(rock[i].vislist & (1u << p)) && doesPlayerSeeRocket(player[p], rock[i].px, rock[i].py)) {
            rock[i].vislist |= (1u << p);
            net->sendOldRocketVisible(p, i, rock[i]);
        }
}

void ServerWorld::resetPlayer(int target, double time_penalty) throw () {    // take the player out of the game; the clients must be informed and this function doesn't do that
    player[target].health = player[target].energy = 0;

    player[target].visibility = 255;
    player[target].item_power = false;
    player[target].item_turbo = false;
    // deathbringer is not removed until respawn because the flag is needed

    //stop all speed
    player[target].sx = 0;
    player[target].sy = 0;

    dropFlagIfAny(target);

    int ts[2] = { 0, 0 };
    for (int i = 0; i < maxplayers; ++i)
        if (player[i].used)
            ++ts[i / TSIZE];
    const int plTeam = target / TSIZE;
    pair<double, double> respawnTime = config.getRespawnTime(ts[plTeam], ts[1 - plTeam]);
    respawnTime.first += time_penalty;
    if (respawnTime.first < 0) { // a negative time_penalty can cause this; in that case we want to eliminate extra waiting time too
        respawnTime.second += respawnTime.first;
        respawnTime.first = 0;
    }
    player[target].frames_to_respawn = iround_bound(respawnTime.first * 10.);
    player[target].extra_frames_to_respawn = iround_bound(max(0., respawnTime.second) * 10.);
    player[target].stats().kill(get_time(), true);
    player[target].dead = true;
    net->send_waiting_time(player[target]);
}

void ServerWorld::killPlayer(int target, bool time_penalty) throw () {   // kill the player in the usual way with score penalties and deathbringer effect; the clients must be informed and this function doesn't do that
    host->score_neg(target, 1); // score neg points because of death
    if (dropFlagIfAny(target))
        host->score_neg(target, 1); // score neg points because of losing the flag

    if (player[target].item_deathbringer) {
        addDeathbringerExplosion(DeathbringerExplosion(frame, player[target]));
        net->sendDeathbringer(target, player[target]);
    }

    if (pupConfig.pups_drop_at_death)
        drop_powerup(player[target]);

    resetPlayer(target, (player[target].item_deathbringer || time_penalty) ? config.getDeathbringerWaitingTime() : 0);  // clients must be informed by the caller
}

void ServerWorld::damagePlayer(int target, int attacker, int damage, DamageType type) throw () {   // inflict damage on target
    if (player[target].dead || frame < player[target].start_take_damage_frame)
        return;

    // shadow powerup: show player
    if (player[target].item_shadow())
        player[target].visibility = maximum_shadow_visibility;

    if (player[target].item_shield) {
        if (pupConfig.pup_shield_hits) {
            player[target].item_shield--;
            if (!player[target].item_shield && type != DT_deathbringer)
                net->broadcast_screen_sample(target, SAMPLE_SHIELD_LOST);
        }
        else {
            player[target].energy -= damage;
            if (player[target].energy <= 0) {
                player[target].energy = 0;
                player[target].item_shield = 0;
                if (type != DT_deathbringer)
                    net->broadcast_screen_sample(target, SAMPLE_SHIELD_LOST);
            }
            else if (type != DT_deathbringer)
                net->broadcast_screen_sample(target, SAMPLE_SHIELD_DAMAGE);
        }
    }
    //else do the regular body damage
    else {
        player[target].health -= damage;
        //freeze target's gun
        if (type != DT_collision)
            player[target].next_shoot_frame = frame + config.get_hit_stun_time_frames();
    }
    if (player[target].health > 0)
        return;

    const bool flag = player[target].stats().has_flag();
    const bool wild_flag = player[target].stats().has_wild_flag();
    bool carrier_defended = false, flag_defended = false;
    const int tateam = target / TSIZE;
    if (attacker < maxplayers) { // the reverse happens with deathbringers whose owner has left the server
        const int atteam = attacker / TSIZE;
        const bool same_team = (tateam == atteam);
        if (!same_team)
            host->score_frag(attacker, 1);      // frag to attacker for the kill
        else
            host->score_frag(attacker, -2);     // take two frags for killing own player

        if (!same_team) {
            // Check if an enemy flag or a wild flag is carried in the target's screen by a teammate.
            for (int i = atteam * TSIZE; i < (atteam + 1) * TSIZE; i++)
                if (player[i].used && player[i].stats().has_flag() && i != attacker && player[i].roomx == player[target].roomx && player[i].roomy == player[target].roomy) {
                    carrier_defended = true;
                    host->score_frag(attacker, 1);
                    break;  // only one frag even for defending multiple carriers
                }
            // Check if an own or enemy flag is lying on the ground in the target's screen.
            if (!lock_team_flags_in_effect()) {   // Not much defending or attacking if the flag couldn't be moved anyway.
                for (vector<Flag>::const_iterator fi = teams[atteam].flags().begin(); fi != teams[atteam].flags().end(); ++fi)
                    if (!fi->carried() && fi->position().px == player[target].roomx && fi->position().py == player[target].roomy) {
                        flag_defended = true;
                        if (!carrier_defended)
                            host->score_frag(attacker, 1);
                        break;  // only one frag even for defending multiple flags
                    }
                if (!carrier_defended && !flag_defended)
                    for (vector<Flag>::const_iterator fi = teams[tateam].flags().begin(); fi != teams[tateam].flags().end(); ++fi)
                        if (!fi->carried() && fi->position().px == player[target].roomx && fi->position().py == player[target].roomy) {
                            host->score_frag(attacker, 1);
                            break;  // only one frag even for attacking multiple flags
                        }
            }
        }
        if (flag) {
            if (!same_team) {
                player[attacker].stats().add_carrier_kill();
                host->score_frag(attacker, 1);  // extra frag for fragging a carrier
            }
            else
                host->score_frag(attacker, -1); // extra penalty for fragging own carrier
        }

        if (!same_team) {
            player[attacker].stats().add_kill(type == DT_deathbringer);
            teams[atteam].add_kill();
        }
    }
    player[target].stats().add_death(type == DT_deathbringer, static_cast<int>(get_time()));
    teams[tateam].add_death();

    net->broadcast_kill(player[attacker], player[target], type, flag, wild_flag, carrier_defended, flag_defended);
    killPlayer(target, false);
}

void ServerWorld::removePlayer(int pid) throw () {
    for (int r = 0; r < MAX_ROCKETS; r++) { // remove rockets, and player from their vislists
        if (rock[r].owner == pid)
            deleteRocket(r, 0, 0, 255);
        else
            rock[r].vislist &= ~(1u << pid);
    }
    if (maxplayers < MAX_PLAYERS) { // disown deathbringers if there is a convenient pseudo-pid to assign; otherwise just hope that no one gets the same pid soon (data_kill needs some player id for killer)
        for (list<DeathbringerExplosion>::iterator dbi = dbExplosions.begin(); dbi != dbExplosions.end(); ++dbi)
            if (dbi->player() == pid)
                dbi->pidChange(MAX_PLAYERS - 1);
        for (int i = 0; i < maxplayers; ++i)
            if (player[i].deathbringer_attacker == pid)
                player[i].deathbringer_attacker = MAX_PLAYERS - 1;
    }

    dropFlagIfAny(pid, true);

    player[pid].used = false;
}

void ServerWorld::suicide(int pid) throw () {
    if (player[pid].dead)
        return;
    const bool flag = player[pid].stats().has_flag();
    bool wild_flag = false;
    if (flag)
        for (vector<Flag>::const_iterator fi = wild_flags.begin(); fi != wild_flags.end(); ++fi)
            if (fi->carrier() == pid) {
                wild_flag = true;
                break;
            }
    host->score_frag(pid, -1);
    player[pid].stats().add_suicide(static_cast<int>(get_time()));
    teams[pid / TSIZE].add_suicide();
    net->broadcast_suicide(player[pid], flag, wild_flag);
    killPlayer(pid, true);
}

uint8_t ServerWorld::getFreeRocket() throw () {
    for (int i = 0; i < MAX_ROCKETS; i++)
        if (rock[i].owner == -1) {
            rock[i].owner = 0;
            return i;
        }
    log("Rocket overwrite!");
    const int i = rand() % MAX_ROCKETS;
    rock[i].owner = 0;
    return i;
}

bool ServerWorld::doesPlayerSeeRocket(ServerPlayer& pl, int roomx, int roomy) const throw () {
    if (pl.protocolExtensionsLevel < 0) // older clients can't show other rooms anyway, and will play some sounds for all rockets
        return pl.roomx == roomx && pl.roomy == roomy;
    int dx = positiveModulo(pl.roomx - roomx, map.w),
        dy = positiveModulo(pl.roomy - roomy, map.h);
    if (dx > map.w / 2)
        dx = map.w - dx;
    if (dy > map.h / 2)
        dy = map.h - dy;
    return dx + dy <= config.see_rockets_distance;
}

void ServerWorld::shootRockets(int pid, int shots) throw () {
    const int px = player[pid].roomx, py = player[pid].roomy, x = static_cast<int>(player[pid].lx), y = static_cast<int>(player[pid].ly); // cast to be in sync: the coords need to be the same as client receives

    player[pid].stats().add_shot();
    teams[pid / TSIZE].add_shot();

    uint8_t sid[16];
    for (int i = 0; i < shots; ++i)
        sid[i] = getFreeRocket();

    ServerPhysicsCallbacks cb(*this);
    WorldBase::shootRockets(cb, pid, shots, player[pid].attackGunDir, sid, 0, pid/TSIZE, player[pid].item_power, px, py, x, y);

    //build people-that-know DOUBLE WORD (32bits == 32players max)
    //send message to players on the same screen
    uint32_t vislist = 0;
    for (int p = 0; p < maxplayers; p++)
        if (player[p].used && doesPlayerSeeRocket(player[p], px, py))
            vislist |= (1u << p);

    //mark all created rockets with the vislist
    for (int k = 0; k < shots; k++)
        rock[sid[k]].vislist = vislist;

    net->sendRocketMessage(shots, player[pid].attackGunDir, sid, pid, player[pid].item_power, px, py, x, y, vislist);
}

void ServerWorld::deleteRocket(int rid, int16_t hitx, int16_t hity, int targ) throw () {
    Rocket& r = rock[rid];
    net->sendRocketDeletion(r.vislist, rid, hitx, hity, targ);
    r.owner = -1;
}

void ServerWorld::changeEmbeddedPids(int source, int target) throw () {
    for (int i = 0; i < MAX_ROCKETS; i++) {
        if (rock[i].owner == source)
            rock[i].owner = target;
        if (rock[i].vislist & (1u << source))
            rock[i].vislist = rock[i].vislist & ~(1u << source) | (1u << target);
    }
    for (list<DeathbringerExplosion>::iterator dbi = dbExplosions.begin(); dbi != dbExplosions.end(); ++dbi)
        if (dbi->player() == source)
            dbi->pidChange(target);
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (player[i].deathbringer_attacker == source)
            player[i].deathbringer_attacker = target;
}

void ServerWorld::swapEmbeddedPids(int a, int b) throw () {
    for (int i = 0; i < MAX_ROCKETS; i++) {
        if (rock[i].owner == a)
            rock[i].owner = b;
        else if (rock[i].owner == b)
            rock[i].owner = a;
        if (((rock[i].vislist & (1u << a)) != 0) != ((rock[i].vislist & (1u << b)) != 0))
            rock[i].vislist ^= (1u << a) | (1u << b);
    }
    for (list<DeathbringerExplosion>::iterator dbi = dbExplosions.begin(); dbi != dbExplosions.end(); ++dbi) {
        if (dbi->player() == a)
            dbi->pidChange(b);
        else if (dbi->player() == b)
            dbi->pidChange(a);
    }
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (player[i].deathbringer_attacker == a)
            player[i].deathbringer_attacker = b;
        else if (player[i].deathbringer_attacker == b)
            player[i].deathbringer_attacker = a;
    }
}

void ServerWorld::addMovementDistanceCallback(int pid, double dist) throw () {
    player[pid].stats().add_movement(dist);
    teams[pid / TSIZE].add_movement(dist);
}

void ServerWorld::playerScreenChangeCallback(int pid) throw () {
    game_player_screen_change(pid);
}

void ServerWorld::rocketHitWallCallback(int rid) throw () {
    rock[rid].owner = -1;
}

bool ServerWorld::rocketHitPlayerCallback(int rid, int pid) throw () {
    //record wether the player had shield, if yes, will not blink him
    const bool had_shield = player[pid].item_shield;

    int damage = config.rocket_damage;
    if (rock[rid].power)
        damage = static_cast<int>(pupConfig.pup_power_damage * damage);
    if (rock[rid].team == pid / TSIZE)
        damage = static_cast<int>(physics.friendly_fire * damage);

    damagePlayer(pid, rock[rid].owner, damage, DT_rocket);

    if (rock[rid].team != pid / TSIZE) {    // hitting a friend is not considered as a hit
        player[rock[rid].owner].stats().add_hit();
        teams[rock[rid].team].add_hit();
    }
    player[pid].stats().add_shot_take();
    teams[pid / TSIZE].add_shot_take();

    //if player not dead, push him
    if (!player[pid].dead) {
        const double mul = 15. / physics.rocket_speed * (rock[rid].team == pid / TSIZE ? physics.friendly_fire : 1.); // divide by rocket_speed to remove its effect in rock.sx,sy
        player[pid].sx += rock[rid].sx * mul;
        player[pid].sy += rock[rid].sy * mul;
    }

    if (had_shield)
        deleteRocket(rid, (int16_t)rock[rid].x, (int16_t)rock[rid].y, 252);     //do not blink
    else
        deleteRocket(rid, (int16_t)rock[rid].x, (int16_t)rock[rid].y, pid);     //blink
    return player[pid].dead;
}

PhysicsCallbacksBase::PlayerHitResult ServerWorld::playerHitPlayerCallback(int pid1, int pid2, double speed) throw () {
    if (physics.player_collisions != PhysicalSettings::PC_special)
        return PhysicsCallbacksBase::PlayerHitResult(false, false, 1., 1.);

    speed /= physics.max_run_speed; // make the speed -> damage relatively constant at top speed, regardless of the physics

    ServerPlayer& pl1 = player[pid1];
    ServerPlayer& pl2 = player[pid2];

    nAssert(!pl1.dead && !pl2.dead);

    bool toss_a = false;
    bool toss_b = false;

    // when an invisible player collides with an enemy player, it becomes visible for a while.
    // also, colliding with a teammate that doesn't have the shadow will give the shadow bonus for a short time (3 secs)
    const double shadowTransferTime = 3.0;
    if (pl1.item_shadow()) {
        if (pl1.team() != pl2.team())
            pl1.visibility = maximum_shadow_visibility;
        else if (!pl2.item_shadow() && pl1.item_shadow_time > get_time() + shadowTransferTime) {
            // share free shadow if has enough shadow time left (greater than the bonus time)
            pl2.visibility = config.getShadowMinimum();
            pl2.item_shadow_time = get_time() + shadowTransferTime;
            net->sendPupTime(pid2, Powerup::pup_shadow, shadowTransferTime);
            net->broadcast_screen_sample(pid2, SAMPLE_SHADOW_ON);
        }
    }
    if (pl2.item_shadow()) {
        if (pl1.team() != pl2.team())
            pl2.visibility = maximum_shadow_visibility;
        else if (!pl1.item_shadow() && pl2.item_shadow_time > get_time() + shadowTransferTime) {
            pl1.visibility = config.getShadowMinimum();
            pl1.item_shadow_time = get_time() + shadowTransferTime;
            net->sendPupTime(pid1, Powerup::pup_shadow, shadowTransferTime);
            net->broadcast_screen_sample(pid1, SAMPLE_SHADOW_ON);
        }
    }

    // when a turbo player collides with another, the target's speed resulting from the collision is amplified somewhat (just for fun... maybe)
    if (pl1.item_turbo)
        toss_b = true;
    if (pl2.item_turbo)
        toss_a = true;

    // the remaining effects are active only between opponents
    const double deathbringerEffectTime = 1.0;
    if (pl1.team() != pl2.team()) {
        // deathbringer player colliding with an enemy player without deathbringer causes a short "deathbringer infection"
        if (pl1.item_deathbringer && !pl2.item_deathbringer) {
            pl2.deathbringer_end = get_time() + deathbringerEffectTime;
            pl2.next_shoot_frame = frame + iround(deathbringerEffectTime * 10.);
            pl2.deathbringer_attacker = pid1;
            // amplify the collision result to help on casting both players apart
            toss_a = true;
            toss_b = true;
            // FIXME: maybe should discard the "bounce" sound of the player collision in this case, so they don't overlap.
            net->broadcast_screen_sample(pid2, SAMPLE_HITDEATHBRINGER);
        }
        else if (pl2.item_deathbringer && !pl1.item_deathbringer) {
            pl1.deathbringer_end = get_time() + deathbringerEffectTime;
            pl1.next_shoot_frame = frame + iround(deathbringerEffectTime * 10.);
            pl1.deathbringer_attacker = pid2;
            toss_a = true;
            toss_b = true;
            net->broadcast_screen_sample(pid1, SAMPLE_HITDEATHBRINGER);
        }

        // shielded player (S) that is not infected by the deathbringer and collides with another (t) enemy player that is not carrying the deathbringer powerup, will cause:
        //  - S's shield is damaged by amount coldam (shield-hit / shield-down fx is played)
        //  - t is damaged by amount coldam and "tossed"
        // where coldam is proportional to the "strength" of the collision
        const bool shieldHitBy1 = (pl1.item_shield && pl1.deathbringer_end < get_time() && !pl2.item_deathbringer);
        const bool shieldHitBy2 = (pl2.item_shield && pl2.deathbringer_end < get_time() && !pl1.item_deathbringer);
        const int shieldColdam = static_cast<int>(speed * 60);  // 60 at top running speed without turbo - this only applies to the shielded player
        if (shieldHitBy1) {
            toss_b = true;
            pl2.next_shoot_frame = frame + config.get_hit_stun_time_frames();
            if (shieldHitBy2) {
                toss_a = true;
                pl1.next_shoot_frame = frame + config.get_hit_stun_time_frames();
                net->broadcast_screen_sample(pid1, SAMPLE_SHIELD_LOST); // applies to both players
                pl1.energy = pl2.energy = 0;
                pl1.item_shield = pl2.item_shield = 0;
            }
            else
                damagePlayer(pid1, pid2, shieldColdam, DT_collision);   // damage to the shield
        }
        else if (shieldHitBy2) {
            toss_a = true;
            pl1.next_shoot_frame = frame + config.get_hit_stun_time_frames();
            damagePlayer(pid2, pid1, shieldColdam, DT_collision);
        }
        // works both ways
        if (pl2.item_shield && pl2.deathbringer_end < get_time() && !pl1.item_deathbringer) {
            toss_a = true;

            if (!pl1.item_shield) // if target is not shielded, make it blink and play hit sound
                net->broadcast_screen_power_collision(pid1);

            damagePlayer(pid1, pid2, shieldColdam, DT_collision);
            damagePlayer(pid2, pid1, shieldColdam, DT_collision);
        }

        // humiliation hit/kill:
        // non-shielded, non-deathbringer infected and power carrying "player X" that runs into an "enemy player Y" that is
        // non-shielded, non-deathbringer carrier, and non-power carrying will cause about the same effect as Y being hit by a non-power rocket fired by X
        //  - blink target / freeze gun / do damage
        //  - play power-rocket or rocket-hit sample
        const int powColdam = static_cast<int>(speed * 80);    // 80 at top running speed without turbo
        if (pl1.item_power && !pl1.item_shield && pl1.deathbringer_end < get_time() && !pl2.item_deathbringer &&
                !pl2.item_shield && !pl2.item_power) {
            damagePlayer(pid2, pid1, powColdam, DT_collision);
            toss_b = true;
            net->broadcast_screen_power_collision(pid2);
        }
        // same thing but inverting a / b
        if (pl2.item_power && !pl2.item_shield && pl2.deathbringer_end < get_time() && !pl1.item_deathbringer &&
                !pl1.item_shield && !pl1.item_power) {
            damagePlayer(pid1, pid2, powColdam, DT_collision);
            toss_a = true;
            net->broadcast_screen_power_collision(pid1);
        }
    }

    return PhysicsCallbacksBase::PlayerHitResult(pl1.dead, pl2.dead, toss_a ? 2. : 1., toss_b ? 2. : 1.);
}

void ServerWorld::rocketOutOfBoundsCallback(int rid) throw () {
    rock[rid].owner = -1;
}

bool ServerWorld::shouldApplyPhysicsToPlayerCallback(int pid) throw () {
    return !player[pid].dead;
}

void WorldBase::executeBounce(PlayerBase& ply, const Coords& bounceVec, double plyRadius) throw () { // needs plyRadius as a shortcut to bounceVec's length
    // bounce: speed component parallel with bounceVec ( (S dot b / |b|) * b / |b| ) is reversed, while perpendicular component is kept
    // : S -= 2* ( (S dot b) * b / |b|^2 )  ; |b| is always plyRadius
    // to add a specific speed loss only in the bounce direction, reduce from the 2.
    const double mul = 2. * (ply.sx * bounceVec.first + ply.sy * bounceVec.second) / (plyRadius * plyRadius);
    ply.sx -= mul * bounceVec.first;
    ply.sy -= mul * bounceVec.second;
    // lose some speed too
    ply.sx *= .95;
    ply.sy *= .95;
}

// Bounce two players from each other.
pair<bool, bool> WorldBase::executeBounce(PlayerBase& pl1, PlayerBase& pl2, PhysicsCallbacksBase& callback) const throw () {
    // the formulas come simplified from a more complex bounce physics system, so the comments here aren't very descriptive
    const Coords ds(pl2.lx - pl1.lx, pl2.ly - pl1.ly);
    const double r = sqrt(ds.first * ds.first + ds.second * ds.second);

    // player 1
    double tVar = (pl1.sy * ds.second + pl1.sx * ds.first) / (r * r);   // (vx1�rx + vy1�ry)/r = k1
    const double k1 = r * tVar;
    const Coords v1(pl1.sx - ds.first * tVar, pl1.sy - ds.second * tVar);
    // player 2
    tVar = (-pl2.sy * ds.second - pl2.sx * ds.first) / (r * r);
    const double k2 = r * tVar;
    const Coords v2(pl2.sx + ds.first * tVar, pl2.sy + ds.second * tVar);

    const PhysicsCallbacksBase::PlayerHitResult res = callback.playerHitPlayer(pl1.id, pl2.id, fabs(k2 - k1));

    const double newk1 = k1 + (-k2 - k1) * res.bounceStrength1;   // should there be a mass difference this would be more complicated
    const double newk2 = k2 + (-k1 - k2) * res.bounceStrength2;

    static const double baseSpeedMul = .9;

    // new speed components
    pl1.sx = baseSpeedMul * (v1.first  + newk1 * ds.first  / r);    // vx=sx+kx, kx/k = rx/r
    pl1.sy = baseSpeedMul * (v1.second + newk1 * ds.second / r);

    pl2.sx = baseSpeedMul * (v2.first  - newk2 * ds.first  / r);
    pl2.sy = baseSpeedMul * (v2.second - newk2 * ds.second / r);

    limitPlayerSpeed(pl1);  // required because baseSpeedMul and bounceStrengths can be set to physically incorrect values
    limitPlayerSpeed(pl2);

    return res.deaths;
}

void WorldBase::limitPlayerSpeed(PlayerBase& pl) const throw () {
    const double playerSpeedHardLimit = physics.max_run_speed * physics.turbo_mul * 4.;
    if (fabs(pl.sx) > playerSpeedHardLimit || fabs(pl.sy) > playerSpeedHardLimit) { // no need to be exact here; per-axis view is adequate
        const double spd = sqrt(sqr(pl.sx) + sqr(pl.sy));
        const double mul = playerSpeedHardLimit / spd;
        pl.sx *= mul;
        pl.sy *= mul;
    }
}

void WorldBase::applyPhysics(PhysicsCallbacksBase& callback, double plyRadius, double fraction) throw () {
    if (fraction < .001)
        return;

    // note: design decision: vector is used extensively instead of list, to provide access by index
    //       it shouldn't harm since the vectors are short and anything is rarely erased
    vector< vector< vector<int> > > roomPly, roomRock;  // player id's for players in room, rocket id's for rockets in room

    roomPly.resize(map.w);
    roomRock.resize(map.w);
    for (int rx = 0; rx < map.w; ++rx) {
        roomPly[rx].resize(map.h);
        roomRock[rx].resize(map.h);
    }

    // add players and rockets to room structs for physics run
    for (int i = 0; i < maxplayers; i++) {
        PlayerBase& pl = player[i];
        if (!pl.used)
            continue;
        if (callback.shouldApplyPhysicsToPlayer(i)) {
            if (pl.roomx < 0 || pl.roomy < 0 || pl.roomx >= map.w || pl.roomy >= map.h)
                continue;   //#fix: remove this and track why these are given sometimes
            applyPlayerAcceleration(i);
            roomPly[pl.roomx][pl.roomy].push_back(i);
        }
    }
    for (int i = 0; i < MAX_ROCKETS; i++) {
        if (rock[i].owner == -1)
            continue;
        nAssert(rock[i].px >= 0 && rock[i].py >= 0 && rock[i].px < map.w && rock[i].py < map.h);
        roomRock[rock[i].px][rock[i].py].push_back(i);
    }

    // apply physics to each room separately
    for (int rx = 0; rx < map.w; ++rx)
        for (int ry = 0; ry < map.h; ++ry)
            applyPhysicsToRoom(map.room[rx][ry], roomPly[rx][ry], roomRock[rx][ry], callback, plyRadius, fraction);
}

void WorldBase::applyPhysicsToPlayerInIsolation(PlayerBase& pl, double plyRadius, double fraction) throw () {
    // this function is heavily copied from applyPhysicsToRoom; any changes should be made primarily there //#fix: refactor?

    double subFrame = 0.;   // signifies current time within frame, goes from 0 to fraction (0 <= fraction <= 1)
    for (;;) {
        const BounceData bounce = getTimeTillBounce(map.room[pl.roomx][pl.roomy], pl, plyRadius, fraction);
        const double bounceTime = bounce.first + subFrame;
        const double mt = min(fraction, max(bounceTime, subFrame + .01)); // mt is where subFrame will be advanced to for the next round
        const double plTime = min(bounceTime - .001, mt);
        if (plTime > subFrame) {
            pl.move(plTime - subFrame);
            int rdx = 0, rdy = 0; // room change in either direction (-1 or +1 if changed)
            double tbx = -1, tby = -1; // time to take back; used if room changed in the respective direction
            if      (pl.lx <   0) { rdx = -1; nAssert(pl.sx < 0); tbx =  pl.lx        / pl.sx; }
            else if (pl.lx > plw) { rdx = +1; nAssert(pl.sx > 0); tbx = (pl.lx - plw) / pl.sx; }
            if      (pl.ly <   0) { rdy = -1; nAssert(pl.sy < 0); tby =  pl.ly        / pl.sy; }
            else if (pl.ly > plh) { rdy = +1; nAssert(pl.sy > 0); tby = (pl.ly - plh) / pl.sy; }
            if (rdx || rdy) {
                double tb;
                if (tbx > tby) {
                    tb = tbx;
                    pl.roomx = positiveModulo(pl.roomx + rdx, map.w);
                    pl.lx -= rdx * plw;
                }
                else {
                    tb = tby;
                    pl.roomy = positiveModulo(pl.roomy + rdy, map.h);
                    pl.ly -= rdy * plh;
                }
                // take back the time by which the player went over the edge and reapply in the new room
                pl.move(-tb);
                #ifdef EXTRA_DEBUG
                nAssert(fabs(fmod(pl.lx + plw / 2, plw) - plw / 2) < .001 || fabs(fmod(pl.ly + plh / 2, plh) - plh / 2) < .001);
                #endif
                subFrame = max(plTime - tb, subFrame + .01); // ensure progress
                continue;
            }
        }
        subFrame = mt;
        if (subFrame > fraction - .001)
            break;
        executeBounce(pl, bounce.second, plyRadius);
        //callback.playerHitWall(bPly); // should be enabled (maybe other callbacks too) if the client ever needs to (indirectly) execute this method
    }
}

void WorldBase::applyPhysicsToRoom(const Room& room, vector<int>& rply, vector<int>& rrock, PhysicsCallbacksBase& callback, double plyRadius, double fraction) throw () {
    // many changes in this method should also be made in applyPhysicsToPlayerInIsolation

    vector<BounceData> plyMoveMax;  // plyMoveMax changes when player bounces
    vector<double> rockMoveMax; // rockMoveMax is fixed

    typedef unsigned int uint;  // for loop counters, to avoid the brainless 'signed vs unsigned comparison' warning by G++

    for (vector<int>::const_iterator pi = rply.begin(); pi != rply.end(); ++pi)
        plyMoveMax.push_back(getTimeTillBounce(room, player[*pi], plyRadius, fraction));
    for (vector<int>::const_iterator ri = rrock.begin(); ri != rrock.end(); ++ri)
        rockMoveMax.push_back(getTimeTillWall(room, rock[*ri], fraction));

    double subFrame = 0.;   // signifies current time within frame, goes from 0 to fraction (0 <= fraction <= 1)
    #ifndef NDEBUG
    int round = 0;
    #endif
    for (;;) {  //#fix: optimize this loop, esp. for client
        nAssert(++round < fraction * 100. + 100);   // allow for fraction full of "minimal increments" of .01 frames, and 100 rocket collisions
        // find out next player-wall collision
        double minBounce = fraction + 1.;   // at what time the first player bounces (absolute frame time: 1 is end of frame)
        int bPly = 0, bPlyI = -1;   // which player it is, pid and room-table-index
        for (uint pi = 0; pi < rply.size(); ++pi) {
            const double bt = plyMoveMax[pi].first;
            if (bt < minBounce) {
                minBounce = bt;
                bPlyI = pi;
                bPly = rply[pi];
            }
        }
        // minBounce might even be less than subFrame in the case a bounce was skipped (due to the code avoiding infinite bouncing); that's ok

        // find out next player-rocket collision
        double minCollision = fraction + 1.;    // at what time the first player-rocket collision occurs (forward time: 1-subFrame is end of frame)
        int cPly = 0, cPlyI = -1, cRock = 0, cRockI = -1;   // which player and rocket they are, pid/rid and room-table-indices
        if (callback.collideToRockets()) {
            for (uint pi = 0; pi < rply.size(); ++pi) {
                const int pid = rply[pi];
                if (!callback.collidesToRockets(pid))
                    continue;
                for (uint ri = 0; ri < rrock.size(); ++ri) {
                    const int rid = rrock[ri];
                    if (rock[rid].team == pid / TSIZE && (physics.friendly_fire == 0. || rock[rid].owner == pid))   // friendly rocket
                        continue;
                    const bool shield = static_cast<PlayerBase&>(player[pid]).item_shield;
                    const double time = getTimeTillCollision(player[pid], rock[rid], ROCKET_RADIUS + plyRadius + (shield ? SHIELD_RADIUS_ADD : 0));
                    if (time < minCollision && time < rockMoveMax[ri]) {
                        minCollision = time;
                        cPlyI = pi;
                        cPly = rply[pi];
                        cRockI = ri;
                        cRock = rrock[ri];
                    }
                }
            }
            nAssert(minCollision >= 0.);
            minCollision += subFrame;   // it was calculated in forward time, now it's in absolute frame time as are player movements
        }

        // find out next player-player collision
        double minPlyCollision = fraction + 1.; // at what time the first player-player collision occurs (forward time: 1-subFrame is end of frame)
        int pcPly1 = 0, pcPly1I = -1, pcPly2 = 0, pcPly2I = -1; // which players they are, pids and room-table-indices
        if (physics.player_collisions != PhysicalSettings::PC_none) {
            for (uint pi = 0; pi < rply.size(); ++pi) {
                const int pid = rply[pi];
                if (!callback.collidesToPlayers(pid))
                    continue;
                for (uint ti = pi + 1; ti < rply.size(); ++ti) {
                    const int tid = rply[ti];
                    if (!callback.collidesToPlayers(tid))
                        continue;
                    const double time = getTimeTillCollision(player[pid], player[tid], 2. * plyRadius);
                    if (time < minPlyCollision) {
                        minPlyCollision = time;
                        pcPly1I = pi;
                        pcPly1 = rply[pi];
                        pcPly2I = ti;
                        pcPly2 = rply[ti];
                    }
                }
            }
            nAssert(minPlyCollision >= 0.);
            minPlyCollision += subFrame;    // it was calculated in forward time, now it's in absolute frame time as are player movements
        }

        // execute movement

        double moveCeiling = minPlyCollision - .001;    // -.001 to not go and overlap; this is applied to irrelevant players too which could be eliminated
        // find the next event
        if (minBounce < subFrame + .01) // avoid infinite bouncing; this could be done more delicately (based on rounds spent)
            minBounce = subFrame + .01; // it's also possible that this causes some bugs
        if (minPlyCollision < subFrame + .01)   // do the same for player collisions
            minPlyCollision = subFrame + .01;
        const double eventTime = min(min(minCollision, minBounce), minPlyCollision);
        const double mt = min<double>(fraction, eventTime); // mt is where subFrame will be advanced to for the next round
        nAssert(mt >= subFrame - .0001);
        if (mt < moveCeiling)
            moveCeiling = mt;
        for (int pi = 0; pi < static_cast<int>(rply.size()); ) {
            // don't move more than plyMoveMax-.001 (-.001 to stay out of walls)
            const double plTime = min(plyMoveMax[pi].first - .001, moveCeiling);
            if (plTime <= subFrame) {   // we are waiting to bounce: nothing can be done
                ++pi;
                continue;
            }
            PlayerBase& pl = player[rply[pi]];
            pl.move(plTime - subFrame);
            if (callback.gatherMovementDistance())
                callback.addMovementDistance(rply[pi], (plTime - subFrame) * sqrt( pl.sx*pl.sx + pl.sy*pl.sy ));
            int rdx = 0, rdy = 0; // room change in either direction (-1 or +1 if changed)
            double tbx = -1, tby = -1; // time to take back; used if room changed in the respective direction
            if (callback.allowRoomChange()) {
                if      (pl.lx <   0) { rdx = -1; nAssert(pl.sx < 0); tbx =  pl.lx        / pl.sx; }
                else if (pl.lx > plw) { rdx = +1; nAssert(pl.sx > 0); tbx = (pl.lx - plw) / pl.sx; }
                if      (pl.ly <   0) { rdy = -1; nAssert(pl.sy < 0); tby =  pl.ly        / pl.sy; }
                else if (pl.ly > plh) { rdy = +1; nAssert(pl.sy > 0); tby = (pl.ly - plh) / pl.sy; }
            }
            if (rdx || rdy) {
                double tb;
                if (tbx > tby) {
                    tb = tbx;
                    pl.roomx = positiveModulo(pl.roomx + rdx, map.w);
                    pl.lx -= rdx * plw;
                }
                else {
                    tb = tby;
                    pl.roomy = positiveModulo(pl.roomy + rdy, map.h);
                    pl.ly -= rdy * plh;
                }
                // take back the time by which the player went over the edge and reapply in the new room
                pl.move(-tb);
                #ifdef EXTRA_DEBUG
                nAssert(fabs(fmod(pl.lx + plw / 2, plw) - plw / 2) < .001 || fabs(fmod(pl.ly + plh / 2, plh) - plh / 2) < .001);
                #endif
                applyPhysicsToPlayerInIsolation(pl, plyRadius, tb);

                callback.playerScreenChange(rply[pi]);

                // remove from this simulation
                rply.erase(rply.begin() + pi);
                plyMoveMax.erase(plyMoveMax.begin() + pi);
                if (  bPlyI >= pi) { if (  bPlyI == pi)   bPlyI = -1; else --  bPlyI; }
                if (  cPlyI >= pi) { if (  cPlyI == pi)   cPlyI = -1; else --  cPlyI; }
                if (pcPly1I >= pi) { if (pcPly1I == pi) pcPly1I = -1; else --pcPly1I; }
                if (pcPly2I >= pi) { if (pcPly2I == pi) pcPly2I = -1; else --pcPly2I; }
                // continue with the same index (which points to the next player now)
            }
            else
                ++pi;
        }
        for (int ri = 0; ri < static_cast<int>(rrock.size()); ) {
            Rocket& r = rock[rrock[ri]];
            if (mt > rockMoveMax[ri]) {
                r.move(rockMoveMax[ri] - subFrame);
                callback.rocketHitWall(rrock[ri], r.power, r.x, r.y, r.px, r.py);
                rrock.erase(rrock.begin() + ri);
                rockMoveMax.erase(rockMoveMax.begin() + ri);
                if (cRockI >= ri) { if (cRockI == ri) cRockI = -1; else --cRockI; }
                // continue with the same index (which points to the next rocket now)
            }
            else {
                r.move(mt - subFrame);
                ++ri;
            }
        }
        subFrame = mt;
        if (subFrame > fraction - .001)
            break;

        // execute collision or bounce
        if (minPlyCollision < minBounce && minPlyCollision < minCollision) {    // the event is a player-player collision
            if (pcPly1I != -1 && pcPly2I != -1) {
                nAssert(pcPly1I < static_cast<int>(rply.size()));
                nAssert(pcPly2I < static_cast<int>(rply.size()));
                const pair<bool, bool> dead = executeBounce(player[pcPly1], player[pcPly2], callback);
                if (dead.first) {
                    rply.erase(rply.begin() + pcPly1I);
                    plyMoveMax.erase(plyMoveMax.begin() + pcPly1I);
                    if (pcPly2I > pcPly1I)
                        --pcPly2I;
                }
                else {
                    plyMoveMax[pcPly1I] = getTimeTillBounce(room, player[rply[pcPly1I]], plyRadius, fraction - subFrame);
                    plyMoveMax[pcPly1I].first += subFrame;  // keep the table in absolute frame time
                }
                if (dead.second) {
                    rply.erase(rply.begin() + pcPly2I);
                    plyMoveMax.erase(plyMoveMax.begin() + pcPly2I);
                }
                else {
                    plyMoveMax[pcPly2I] = getTimeTillBounce(room, player[rply[pcPly2I]], plyRadius, fraction - subFrame);
                    plyMoveMax[pcPly2I].first += subFrame;  // keep the table in absolute frame time
                }
            }
        }
        else if (minCollision < minBounce) {    // the event is a player-rocket collision
            if (cRockI != -1 && cPlyI != -1) {
                nAssert(cRockI < static_cast<int>(rrock.size()));
                nAssert(cPlyI < static_cast<int>(rply.size()));
                if (callback.rocketHitPlayer(cRock, cPly)) {    // true if player is dead
                    rply.erase(rply.begin() + cPlyI);
                    plyMoveMax.erase(plyMoveMax.begin() + cPlyI);
                }
                else {
                    plyMoveMax[cPlyI] = getTimeTillBounce(room, player[rply[cPlyI]], plyRadius, fraction - subFrame);
                    plyMoveMax[cPlyI].first += subFrame;    // keep the table in absolute frame time
                }
                rrock.erase(rrock.begin() + cRockI);
                rockMoveMax.erase(rockMoveMax.begin() + cRockI);
            }
        }
        else {  // the event is a bounce
            if (bPlyI != -1) {
                nAssert(bPlyI < static_cast<int>(rply.size()));
                executeBounce(player[bPly], plyMoveMax[bPlyI].second, plyRadius);
                callback.playerHitWall(bPly);
                plyMoveMax[bPlyI] = getTimeTillBounce(room, player[rply[bPlyI]], plyRadius, fraction - subFrame);
                plyMoveMax[bPlyI].first += subFrame;    // keep the table in absolute frame time
            }
        }
    }
    for (vector<int>::const_iterator ri = rrock.begin(); ri != rrock.end(); ++ri) {
        const Rocket& r = rock[*ri];
        if (r.x < 0 || r.x > plw || r.y < 0 || r.y > plh)
            callback.rocketOutOfBounds(*ri);    // don't bother with removing it from rrock since the simulation is over
    }
}

void WorldBase::rocketFrameAdvance(int frames, PhysicsCallbacksBase& callback) throw () {
    for (int i = 0; i < MAX_ROCKETS; ++i)
        if (rock[i].owner != -1) {
            const double wallTime = getTimeTillWall(map.room[rock[i].px][rock[i].py], rock[i], frames);
            if (wallTime < frames) {
                rock[i].move(wallTime);
                callback.rocketHitWall(i, rock[i].power, rock[i].x, rock[i].y, rock[i].px, rock[i].py);
            }
            else {
                rock[i].move(frames);
                if (rock[i].x < 0 || rock[i].x > plw || rock[i].y < 0 || rock[i].y > plh)
                    callback.rocketOutOfBounds(i);
            }
        }
}

void WorldBase::reset() throw () {
    for (int i = 0; i < MAX_POWERUPS; ++i)
        item[i].kind = Powerup::pup_unused;
    for (int i = 0; i < MAX_ROCKETS; ++i)
        rock[i].owner = -1;
    dbExplosions.clear();
}

static bool doRegenerate(double& var, double limit, double speed, double& timeLeft) throw () {
    if (var >= limit || speed == 0)
        return false;
    var += speed * timeLeft;
    if (var <= limit)
        return true;
    timeLeft = (var - limit) / speed;
    var = limit;
    return false;
}

void ServerWorld::regenerateHealthOrEnergy(ServerPlayer& pl) throw () {
    double timeLeft = .1; // 1 frame
    if (doRegenerate(pl.health, min(config.health_max, 100), config.health_regeneration_0_to_100, timeLeft))
        return;
    if (doRegenerate(pl.energy, min(config.energy_max, 100), config.energy_regeneration_0_to_100, timeLeft))
        return;
    if (doRegenerate(pl.energy, min(config.energy_max, 200), config.energy_regeneration_100_to_200, timeLeft))
        return;
    if (doRegenerate(pl.health, min(config.health_max, 200), config.health_regeneration_100_to_200, timeLeft))
        return;
    if (doRegenerate(pl.energy,     config.energy_max      , config.energy_regeneration_200_to_max, timeLeft))
        return;
    doRegenerate(    pl.health,     config.health_max      , config.health_regeneration_200_to_max, timeLeft);
}

void ServerWorld::degradeHealthOrEnergyForRunning(ServerPlayer& pl) throw () {
    double timeLeft = .1; // 1 frame
    if (pl.energy > 0.) {
        pl.energy -= config.run_energy_degradation * timeLeft;
        if (pl.energy >= 0.)
            return;
        timeLeft = (0. - pl.energy) / config.run_energy_degradation;
        pl.energy = 0.;
    }
    if (pl.health > config.min_health_for_run_penalty)
        pl.health = max<double>(config.min_health_for_run_penalty, pl.health - config.run_health_degradation / 10.);
}

static bool sortByExtraFramesToRespawn(ServerPlayer* p1, ServerPlayer* p2) throw () {
    return p1->extra_frames_to_respawn < p2->extra_frames_to_respawn
        || p1->extra_frames_to_respawn == p2->extra_frames_to_respawn && p1->frames_to_respawn < p2->frames_to_respawn;
}

void ServerWorld::simulateFrame() throw () {
    // (-1) check powerup respawn
    for (int i = 0; i < MAX_POWERUPS; i++)
        if (item[i].kind == Powerup::pup_respawning && get_time() > item[i].respawn_time)
            respawn_powerup(i);

    // (0) do stuff for every player
    for (int i = 0; i < maxplayers; i++) {
        if (!player[i].used)
            continue;

        //dec talk flood protect counter
        player[i].talk_temp -= 0.1;
        if (player[i].talk_temp < 0.0)
            player[i].talk_temp = 0.0;
        player[i].talk_hotness -= 0.1;
        if (player[i].talk_hotness < 1.0)
            player[i].talk_hotness = 1.0;

        // check powerups expired
        if (player[i].item_turbo)
            if (get_time() > player[i].item_turbo_time) {
                player[i].item_turbo = false;
                net->broadcast_screen_sample(i, SAMPLE_TURBO_OFF);
            }
        if (player[i].item_power)
            if (get_time() > player[i].item_power_time) {
                player[i].item_power = false;
                net->broadcast_screen_sample(i, SAMPLE_POWER_OFF);
            }
        if (player[i].item_shadow())
            if (get_time() > player[i].item_shadow_time) {
                player[i].visibility = 255;
                net->broadcast_screen_sample(i, SAMPLE_SHADOW_OFF);
            }

        // shadow alpha down
        if (player[i].item_shadow()) {
            player[i].visibility -= 10;     //slowly fades....
            if (player[i].visibility < config.getShadowMinimum())   // minimum
                player[i].visibility = config.getShadowMinimum();
        }

        // check deathbringer effect
        if (player[i].deathbringer_end > get_time()) {
            //check if still alive
            if (!player[i].dead) {
                //has shield: do big damage to it, in order to remove the shield
                if (player[i].item_shield)
                    damagePlayer(i, player[i].deathbringer_attacker, 12, DT_deathbringer);
                else
                    damagePlayer(i, player[i].deathbringer_attacker, 3, DT_deathbringer); // 30 / s, 150 / 5 s
            }
        }
    }

    cleanOldDeathbringerExplosions();

    // check for deathbringer explosion hits
    for (list<DeathbringerExplosion>::iterator dbi = dbExplosions.begin(); dbi != dbExplosions.end(); ++dbi) {
        DeathbringerExplosion& db = *dbi;
        const WorldCoords& pos = db.position();
        const double radius = db.radius(frame);

        uint32_t newOutsideMask = 0;
        for (int ti = 0; ti < maxplayers; ++ti) {
            ServerPlayer& target = player[ti];
            if (!target.used || target.dead || target.roomx != pos.px || target.roomy != pos.py)
                continue;
            const bool sameTeam = target.team() == db.team();
            if (sameTeam && !physics.friendly_db)
                continue;

            const double dx = target.lx - pos.x;
            const double dy = target.ly - pos.y;

            const double dist = sqrt(sqr(dx) + sqr(dy));
            if (dist > radius + 60) {
                newOutsideMask |= 1u << ti; // player is now in the same room but outside the db
                continue;
            }
            if (dist < radius - 10 && !(db.playersOutsideMask & (1u << ti))) // player is now inside the db and previously either in another room (thus avoiding the deathbringer) or already inside
                continue;

            if (target.deathbringer_end >= get_time() || frame < target.start_take_damage_frame)
                continue;

            net->broadcast_screen_sample(target.id, SAMPLE_HITDEATHBRINGER);
            target.deathbringer_attacker = db.player();

            // time of effect ; also freeze his gun for this same amount of time
            const double time = pupConfig.pup_deathbringer_time * (sameTeam ? physics.friendly_db : 1.) * (9000 + rand() % 2000) / 10000.;
            target.deathbringer_end = get_time() + time;
            target.next_shoot_frame = frame + iround(time * 10.);

            // push player away from db center
            if (dist != 0) {
                const double mul = 40. / dist; // set speed to 40
                target.sx = dx * mul;
                target.sy = dy * mul;
            }
        }
        db.playersOutsideMask = newOutsideMask;
    }

    ServerPhysicsCallbacks cb(*this);
    applyPhysics(cb, PLAYER_RADIUS, 1.);    // 1. means apply the whole frame at once

    const bool extra_time_and_sudden_death = config.suddenDeath() && getTimeLeft() < 0;

    // check player respawn
    vector<ServerPlayer*> respawners[2];
    for (int i = 0; i < maxplayers; ++i) {
        ServerPlayer& pl = player[i];
        if (!pl.used || !pl.dead)
            continue;
        if (pl.frames_to_respawn)
            --pl.frames_to_respawn;
        else if (pl.extra_frames_to_respawn)
            --pl.extra_frames_to_respawn;
        if (!pl.awaiting_client_readies)
            respawners[i / TSIZE].push_back(&pl);
    }
    for (int i = 0; i < 2; ++i)
        sort(respawners[i].begin(), respawners[i].end(), sortByExtraFramesToRespawn);
    unsigned i0 = 0, i1 = 0;
    for (;;) {
        ServerPlayer* p0 = i0 < respawners[0].size() ? respawners[0][i0] : 0;
        ServerPlayer* p1 = i1 < respawners[1].size() ? respawners[1][i1] : 0;
        if (!p0 && !p1)
            break;
        if (p1 && (!p0 || p1->extra_frames_to_respawn < p0->extra_frames_to_respawn)) {
            swap(p0, p1);
            ++i1;
        }
        else
            ++i0;
        if (p1) {
            p1->extra_frames_to_respawn -= p0->extra_frames_to_respawn;
            p0->extra_frames_to_respawn = 0;
        }
        if (p0->extra_frames_to_respawn == 0 && p0->frames_to_respawn == 0)
            respawnPlayer(p0->id);
    }

    // for each player, do misc stuff
    for (int i = 0; i < maxplayers; i++) {
        ServerPlayer& pl = player[i];
        if (!pl.used || pl.dead)
            continue;

        // check for player weapons fire time
        if ((player[i].attack || player[i].attackOnce) && !player[i].dead && frame >= player[i].next_shoot_frame) {
            int numshots = 1;
            player[i].energy -= config.shooting_energy_base;
            if (player[i].energy < 0)
                player[i].energy = 0;
            else {
                for (int k = 1; k < player[i].weapon && player[i].energy >= config.shooting_energy_per_extra_rocket; k++) {
                    player[i].energy -= config.shooting_energy_per_extra_rocket;
                    ++numshots;
                }
            }

            player[i].next_shoot_frame = frame + (player[i].energy > 0 ? config.get_shoot_interval_with_energy_frames() : config.get_shoot_interval_frames());

            //show shadow
            if (player[i].item_shadow())
                player[i].visibility = maximum_shadow_visibility;

            shootRockets(i, numshots);
        }
        player[i].attackOnce = false;

        // adjust health and energy for carrying deathbringer, running, and plain time passing
        const bool deathbringer_penalty = (pl.item_deathbringer && pl.health >= pupConfig.deathbringer_health_limit && pl.energy >= pupConfig.deathbringer_energy_limit) || pl.deathbringer_end > get_time();
        if (!deathbringer_penalty)
            regenerateHealthOrEnergy(pl);
        if (pl.controls.isRun())
            degradeHealthOrEnergyForRunning(pl);
        if (pl.item_deathbringer && pl.health > pupConfig.deathbringer_health_limit)
            pl.health = max<double>(pupConfig.deathbringer_health_limit, pl.health - pupConfig.deathbringer_health_degradation / 10.);
        if (pl.item_deathbringer && pl.energy > pupConfig.deathbringer_energy_limit)
            pl.energy = max<double>(pupConfig.deathbringer_energy_limit, pl.energy - pupConfig.deathbringer_energy_degradation / 10.);
        //megahealth bonus:
        if (pl.megabonus > 0)
            for (int mh = 0; mh < 5; mh++) {
                if (pl.megabonus > 0 && pl.health < config.health_max) {
                    pl.health++;
                    pl.megabonus--;
                }
                if (pl.megabonus > 0 && pl.energy < config.energy_max) {
                    pl.energy++;
                    pl.megabonus--;
                }
            }
        // new limit - don't store megabonuses
        if (pl.health == config.health_max && pl.energy == config.energy_max)
            pl.megabonus = 0;

        //---------------------------------
        // check game object collisions
        //---------------------------------

        const int myteam = i / TSIZE;
        const int enemyteam = 1 - myteam;

        // --> ITEM POWERUP
        const int touchRadius = POWERUP_RADIUS + PLAYER_RADIUS;

        for (int k = 0; k < MAX_POWERUPS; k++)
            if (item[k].kind <= Powerup::pup_last_real && item[k].px == pl.roomx && item[k].py == pl.roomy) {
                const double dx = item[k].x - pl.lx;
                const double dy = item[k].y - pl.ly;
                if (dx * dx + dy * dy < touchRadius * touchRadius)
                    game_touch_powerup(i, k);
            }

        // limit health and energy (after powerups because they might have an effect)
        nAssert(pl.health > 0);
        if (pl.health > config.health_max)
            pl.health = config.health_max;
        if (pl.energy < 0)
            pl.energy = 0;
        else if (pl.energy > config.energy_max)
            pl.energy = config.energy_max;

        // Flag steal - touch other team's flag or wild flag
        // ft = 0 => Touch enemy flag
        // ft = 1 => Touch wild flag
        bool touches_flag = false;
        for (int ft = 0; ft < 2; ft++) {
            if (pl.stats().has_flag())
                break;
            if (ft == 0 && lock_team_flags_in_effect())
                continue;
            if (ft == 1 && lock_wild_flags_in_effect())
                break;
            const vector<Flag>* flags;
            int flag_team;
            if (ft == 0) {
                flag_team = enemyteam;
                flags = &teams[enemyteam].flags();
            }
            else {
                flag_team = 2;
                flags = &wild_flags;
            }
            int f = 0;
            for (vector<Flag>::const_iterator fi = flags->begin(); fi != flags->end(); ++fi, ++f)
                if (!fi->carried() && check_flag_touch(*fi, pl.roomx, pl.roomy, pl.lx, pl.ly)) {
                    touches_flag = true;
                    // Has player just dropped the flag or not?
                    if (!pl.dropped_flag && !pl.drop_key) {
                        player_steals_flag(i, flag_team, f);
                        break;  // only take one flag
                    }
                }
        }
        if (!pl.drop_key && !touches_flag)  // Player who dropped the flag has now moved outside it.
            pl.dropped_flag = false;

        // Flag return - wild flags can't be returned
        int f = 0;
        for (vector<Flag>::const_iterator fi = teams[myteam].flags().begin(); fi != teams[myteam].flags().end(); ++fi, ++f)
            if (!fi->carried() && !fi->at_base() && check_flag_touch(*fi, pl.roomx, pl.roomy, pl.lx, pl.ly) &&
                             frame / 10. >= fi->drop_time() + config.flag_return_delay) {
                //FLAG RETURNED!
                host->score_frag(i, 1); // just add some frags
                pl.stats().add_flag_return();
                teams[myteam].add_flag_return();
                net->broadcast_flag_return(pl);
                returnFlag(myteam, f);  //flag returned
            }

        // Flag captures
        // ft = 0 => Take enemy or wild flag to own flag
        // ft = 1 => Take enemy or wild flag to wild flag
        for (int ft = 0; ft < 2; ft++) {
            if (ft == 0 && !capture_on_team_flags_in_effect())
                continue;
            if (ft == 1 && !capture_on_wild_flags_in_effect())
                continue;
            const vector<Flag>& flags = ft == 0 ? teams[myteam].flags() : wild_flags;
            for (vector<Flag>::const_iterator fmy = flags.begin(); fmy != flags.end(); ++fmy) {
                if (!fmy->at_base())
                    continue;
                for (int t = 0; t < 2; ++t) {
                    const vector<Flag>& flags = t == 0 ? teams[enemyteam].flags() : wild_flags;
                    const int flagTeam = t == 0 ? enemyteam : 2;
                    int f = 0;
                    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi, ++f)
                        if (fi->carrier() == i && check_flag_touch(*fmy, pl.roomx, pl.roomy, pl.lx, pl.ly)) {
                            player_captures_flag(i, flagTeam, f);
                            if (teams[myteam].score() >= config.getCaptureLimit() && config.getCaptureLimit() > 0 &&
                                        teams[myteam].score() - teams[enemyteam].score() >= config.getWinScoreDifference() ||
                                        extra_time_and_sudden_death) {
                                host->server_next_map(NEXTMAP_CAPTURE_LIMIT);   // ignore return value
                                return;
                            }
                        }
                }
            }
        }
    }

    // check for score for carrying a wild flag
    if (config.carrying_score_time >= minimum_grab_to_capture_time && teams[0].flags().empty() && teams[1].flags().empty()) {
        for (vector<Flag>::iterator fi = wild_flags.begin(); fi != wild_flags.end(); ++fi)
            if (fi->carried()) {
                const int team = fi->carrier() / TSIZE;
                fi->add_carrying_time(team);
                if (fi->carrying_time() >= 10 * config.carrying_score_time) {
                    fi->reset_carrying_time();
                    team_gets_carrying_point(team, config.carrying_score_time >= minimum_grab_to_capture_time);
                    if (teams[team].score() >= config.getCaptureLimit() && config.getCaptureLimit() > 0 &&
                                teams[team].score() - teams[1 - team].score() >= config.getWinScoreDifference() ||
                                extra_time_and_sudden_death) {
                        host->server_next_map(NEXTMAP_CAPTURE_LIMIT);   // ignore return value
                        return;
                    }
                }
            }
    }

    // check frags changed
    for (int i = 0; i < maxplayers; ++i)
        if (player[i].used && player[i].oldfrags != player[i].stats().frags()) {
            player[i].oldfrags = player[i].stats().frags();
            net->sendFragUpdate(i, player[i].stats().frags());
        }

    // check time limit
    const uint32_t time_limit = config.getTimeLimit();
    if (host->get_player_count() > 1 && time_limit > 0) {
        const int timeLeft = getTimeLeft();
        if      (time_limit >= 10*60 * 10 && timeLeft == 5*60 * 10)
            net->broadcast_5_min_left();
        else if (time_limit >=  2*60 * 10 && timeLeft ==   60 * 10)
            net->broadcast_1_min_left();
        else if (time_limit >=    60 * 10 && timeLeft ==   30 * 10)
            net->broadcast_30_s_left();
        // game ends if time is over and (the game is not tied or there is no extra-time)
        else if (timeLeft == 0 && (teams[0].score() != teams[1].score() || (config.extra_time == 0 && !config.suddenDeath()))) {
            net->broadcast_time_out();
            host->server_next_map(NEXTMAP_CAPTURE_LIMIT);   // ignore return value
        }
        else if (getExtraTimeLeft() == 0 && config.extra_time > 0) {
            net->broadcast_extra_time_out();
            host->server_next_map(NEXTMAP_CAPTURE_LIMIT);   // ignore return value
        }
        else if (timeLeft == 0) {
            net->broadcast_normal_time_out(config.suddenDeath());
            net->send_map_time(pid_all);
        }
    }
}

bool ServerWorld::lock_team_flags_in_effect() const throw () {
    return config.lock_team_flags && all_kind_of_flags_exist();
}

bool ServerWorld::lock_wild_flags_in_effect() const throw () {
    return config.lock_wild_flags && all_kind_of_flags_exist();
}

bool ServerWorld::capture_on_team_flags_in_effect() const throw () {
    return config.capture_on_team_flag || !all_kind_of_flags_exist();
}

bool ServerWorld::capture_on_wild_flags_in_effect() const throw () {
    return config.capture_on_wild_flag || !all_kind_of_flags_exist();
}

bool ServerWorld::all_kind_of_flags_exist() const throw () {
    return !wild_flags.empty() && !teams[0].flags().empty() && !teams[1].flags().empty();
}

void ServerWorld::player_steals_flag(int pid, int team, int flag) throw () {
    host->score_frag(pid, 1);
    player[pid].stats().add_flag_take(get_time(), team == 2);
    teams[pid / TSIZE].add_flag_take();
    net->broadcast_flag_take(player[pid], team);
    stealFlag(team, flag, pid);
    if (player[pid].item_shadow())
        player[pid].visibility = maximum_shadow_visibility;
}

void ServerWorld::player_captures_flag(int pid, int team, int flag) throw () {
    const Flag& capt_flag = (team == 2 ? wild_flags[flag] : teams[team].flag(flag));
    const int myteam = pid / TSIZE;
    const double timeDiff = get_time() - capt_flag.grab_time();
    if (host->tournament_active() && timeDiff <= minimum_grab_to_capture_time) {    // can't capture yet
        if (timeDiff <= .1) {   // being able to capture flags without moving is a too easy way to cheat
            log.error(_("This map is invalid: instant flag capture is possible."));
            host->score_frag(pid, -10);
            suicide(pid);
            returnFlag(team, flag);
            net->broadcast_broken_map();
        }
        return;
    }
    // add frags to all players of the team and
    // penalise every player of the other team
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (player[i].used) {
            if (i / TSIZE == myteam)
                host->score_frag(i, 2); // small two-frag bonus
            else if (team != 2)
                host->score_neg(i, 1);  // small neg point penalty for your flag being captured
        }
    host->score_frag(pid, 3);
    player[pid].stats().add_capture(get_time());
    teams[myteam].add_score(getMapTime() / 10, player[pid].name);
    returnFlag(team, flag);

    net->broadcast_capture(player[pid], team);

    net->ctf_update_teamscore(myteam);

    if (config.respawn_on_capture)
        for (int i = 0; i < maxplayers; ++i)
            player[i].frames_to_respawn = player[i].extra_frames_to_respawn = 0; // will respawn on next frame (only relevant for dead players, obviously)
}

void ServerWorld::team_gets_carrying_point(int team, bool forTournament) throw () {
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (player[i].used) {
            if (i / TSIZE == team)
                host->score_frag(i, 2, forTournament);
            else
                host->score_neg(i, 1, forTournament);
        }
    teams[team].add_point();
    net->ctf_update_teamscore(team);
}

// extrapolate : advances from source, a frame per every ctrl listed except the last one which gets subFrameAfter, controls are for player me
void ClientWorld::extrapolate(ClientWorld& source, PhysicsCallbacksBase& physCallbacks, int me,
                              ClientControls* ctrlTab, uint8_t ctrlFirst, uint8_t ctrlLast, double subFrameAfter) throw () {
    if (source.skipped) {
        skipped = true;
        return;
    }
    nAssert(source.frame >= 0);

    frame = source.frame;

    for (int i = 0; i < 2; ++i)
        teams[i] = source.teams[i]; //#fix: needed?

    for (int i = 0; i < maxplayers; ++i) {
        if (source.player[i].onscreen || i == me || (me == -1 && source.player[i].used))
            player[i] = source.player[i];
        else
            player[i].used = false;
    }
    for (int i = 0; i < MAX_ROCKETS; ++i) {
        if (source.rock[i].owner == -1)
            rock[i].owner = -1;
        else
            rock[i] = source.rock[i];
    }

    static const double playerPosAccuracy = plw / double(0xFFF) / 2.; // used to counter problems in bouncing caused by inaccurate positions over network
    for (uint8_t ctrli = ctrlFirst; ctrli != ctrlLast; ++ctrli) {   // note: it is OK to wrap around in the middle of the sequence
        if (me != -1)
            player[me].controls = ctrlTab[ctrli];
        applyPhysics(physCallbacks, PLAYER_RADIUS - playerPosAccuracy, 1.); // 1 is full frame
        ++frame;
    }
    if (me != -1)
        player[me].controls = ctrlTab[ctrlLast];
    applyPhysics(physCallbacks, PLAYER_RADIUS - playerPosAccuracy, subFrameAfter);
    frame += subFrameAfter;
}

// Save stats in HTML file.
void WorldBase::save_stats(const string& dir, const string& map_name) const throw () {
    const string date_time = date_and_time();
    const string date = date_time.substr(0, date_time.find(' '));
    const string time = date_time.substr(date_time.find(' ') + 1);
    const string filename = wheregamedir + dir + directory_separator + date + ".html";
    // Check if the stats file exists.
    ifstream in(filename.c_str());
    const bool print_html_begin = !in;
    in.close();

    ofstream out(filename.c_str(), ios::app);
    if (!out) {
        //log.error("Could not open the statistics file: %s", filename.c_str());
        return;
    }

    if (print_html_begin) {
        out << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n";
        out << "<TITLE>Outgun statistics " << date << "</TITLE>\n";
        out << "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=ISO-8859-1\">\n";
        out << "<LINK REL=\"stylesheet\" HREF=\"stats.css\" TYPE=\"text/css\" TITLE=\"Outgun statistics style\">\n\n";
        out << "<H1>Outgun statistics " << date << "</H1>\n\n";
    }
    out << "<H2 ID=\"d" << date << 'T' << time << "\">" << time << ' ' << escape_for_html(map_name) << "</H2>\n\n";

    out << "<H3>Team stats</H3>\n\n";
    const Team& red = teams[0];
    const Team& blue = teams[1];
    out << "<TABLE BORDER CLASS=\"teams\">\n <TR><TH>Team<TH>Red<TH>Blue\n";
    print_team_stats_row(out, "Captures",       red.score(), blue.score());
    print_team_stats_row(out, "Kills",          red.kills(), blue.kills());
    print_team_stats_row(out, "Deaths",         red.deaths(), blue.deaths());
    print_team_stats_row(out, "Suicides",       red.suicides(), blue.suicides());
    print_team_stats_row(out, "Flags taken",    red.flags_taken(), blue.flags_taken());
    print_team_stats_row(out, "Flags dropped",  red.flags_dropped(), blue.flags_dropped());
    print_team_stats_row(out, "Flags returned", red.flags_returned(), blue.flags_returned());
    print_team_stats_row(out, "Shots",          red.shots(), blue.shots());
    print_team_stats_row(out, "Hit accuracy",   static_cast<int>(100. * red.accuracy() + 0.5), static_cast<int>(100. * blue.accuracy() + 0.5), "%");
    print_team_stats_row(out, "Shots taken",    red.shots_taken(), blue.shots_taken());
    print_team_stats_row(out, "Total movement", static_cast<int>(red.movement()), static_cast<int>(blue.movement()), "�u");
    out << "</TABLE>\n\n";

    out << "<H3>Player stats</H3>\n\n";
    out << "<TABLE BORDER CLASS=\"players\">\n <TR CLASS=\"pl-stats-thr\"><TH>Player<TH>Frags<TH>Captures<TH>Kills<TH>Deaths<TH>Suicides<TH>Flags taken<TH>Flags dropped<TH>Flags returned<TH>Carriers killed<TH>Carry time<TH>Cons. kills<TH>Cons. deaths<TH>Shots<TH>Accuracy<TH>Shots taken<TH>Movement\n";
    vector<const PlayerBase*> players;
    for (vector<PointerAsReference<PlayerBase> >::const_iterator pl = player.begin(); pl != player.end(); ++pl)
        if (pl->getPtr()->used)
            players.push_back(pl->getPtr());
    stable_sort(players.begin(), players.end(), compare_players);
    int team = 0;
    for (vector<const PlayerBase*>::const_iterator pl = players.begin(); pl != players.end(); ++pl) {
        if (!(*pl)->used)
            continue;
        if (team == 0 && (*pl)->team() == 0) {      // red players should be first
            out << " <TR><TH COLSPAN=\"17\" ALIGN=\"left\">Red team\n";
            team++;
        }
        else if (team <= 1 && (*pl)->team() == 1) {
            out << " <TR><TH COLSPAN=\"17\" ALIGN=\"left\">Blue team\n";
            team++;
        }
        out << " <TR ALIGN=\"right\"><TD ALIGN=\"left\">" << escape_for_html((*pl)->name);
        const Statistics& stats = (*pl)->stats();
        out << "<TD>" << stats.frags();
        out << "<TD>" << stats.captures();
        out << "<TD>" << stats.kills();
        out << "<TD>" << stats.deaths();
        out << "<TD>" << stats.suicides();
        out << "<TD>" << stats.flags_taken();
        out << "<TD>" << stats.flags_dropped();
        out << "<TD>" << stats.flags_returned();
        out << "<TD>" << stats.carriers_killed();
        const int carry_time = static_cast<int>(stats.flag_carrying_time(0));
        out << "<TD>" << carry_time / 60 << ':' << setw(2) << setfill('0') << carry_time % 60;
        out << "<TD>" << stats.cons_kills();
        out << "<TD>" << stats.cons_deaths();
        out << "<TD>" << stats.shots();
        out << "<TD>" << std::setprecision(0) << std::fixed << stats.accuracy() * 100. << '%';
        out << "<TD>" << stats.shots_taken();
        out << "<TD>" << std::setprecision(0) << std::fixed << stats.movement() << "�u";
    }
    out << "\n</TABLE>\n\n";
    if (red.score() == 0 && blue.score() == 0)
        return;
    else if (teams[0].captures().empty() && teams[1].captures().empty()) {  // only in client
        out << "<P>No captures when you were playing.</P>\n\n";
        return;
    }

    out << "<H3>Captures</H3>\n\n";
    out << "<TABLE BORDER CLASS=\"captures\">\n";
    out << " <TR><TH>Time<TH>Team<TH>Score<TH>Capturer\n";

    int red_score = teams[0].base_score(), blue_score = teams[1].base_score();
    for (vector<pair<int, string> >::const_iterator red = teams[0].captures().begin(), blue = teams[1].captures().begin();;) {
        string time, team, capturer;
        int team_nr;
        if (red != teams[0].captures().end() && (blue == teams[1].captures().end() || red->first <= blue->first)) {
            ++red_score;
            team = "Red";
            team_nr = 1;
            ostringstream ost;
            ost << red->first / 60 << ':' << setw(2) << setfill('0') << red->first % 60;
            time = ost.str();
            capturer = red->second;
            ++red;
        }
        else if (blue != teams[1].captures().end() && (red == teams[0].captures().end() || blue->first <= red->first)) {
            ++blue_score;
            team = "Blue";
            team_nr = 2;
            ostringstream ost;
            ost << blue->first / 60 << ':' << setw(2) << setfill('0') << blue->first % 60;
            time = ost.str();
            capturer = blue->second;
            ++blue;
        }
        else
            break;
        out << " <TR CLASS=\"team" << team_nr << "\"><TD ALIGN=\"right\">" << time;
        out << "<TD>" << team;
        out << "<TD ALIGN=\"center\">" << red_score << "&ndash;" << blue_score;
        out << "<TD>" << escape_for_html(capturer);
        out << '\n';
    }
    out << "</TABLE>\n\n";
}

void WorldBase::print_team_stats_row(ostream& out, const string& header, int amount1, int amount2, const string& postfix) const throw () {
    out << " <TR><TH>" << header;
    out << "<TD ALIGN=\"center\">" << amount1 << postfix;
    out << "<TD ALIGN=\"center\">" << amount2 << postfix;
    out << '\n';
}

void WorldBase::cleanOldDeathbringerExplosions() throw () {
    // only erase from the front of the list since new ones are added to the back and the lifetime is constant
    for (std::list<DeathbringerExplosion>::iterator dbi = dbExplosions.begin();
         dbi != dbExplosions.end() && dbi->expired(get_frame());
         dbi = dbExplosions.erase(dbi));
}

// Team

Team::Team() throw () {
    clear_stats();
}

void Team::clear_stats() throw () {
    points = 0;
    total_kills = 0;
    total_deaths = 0;
    total_suicides = 0;
    total_flags_taken = 0;
    total_flags_dropped = 0;
    total_flags_returned = 0;
    total_shots = 0;
    total_hits = 0;
    total_shots_taken = 0;
    total_movement = 0;
    tournament_power = 0;
    caps.clear();
    start_score = 0;
}

void Team::add_flag(const WorldCoords& pos) throw () {
    team_flags.push_back(Flag(pos));
}

void Team::remove_flags() throw () {
    team_flags.clear();
}

void Team::add_score(double time, const string& player) throw () {
    ++points;
    caps.push_back(pair<int, string>(static_cast<int>(time), player));
}

void Team::steal_flag(int n, int carrier) throw () {
    team_flags[n].take(carrier);
}

void Team::steal_flag(int n, int carrier, double time) throw () {
    team_flags[n].take(carrier, time);
}

void Team::return_all_flags() throw () {
    for (vector<Flag>::iterator fi = team_flags.begin(); fi != team_flags.end(); ++fi)
        fi->return_to_base();
}

void Team::return_flag(int n) throw () {
    team_flags[n].return_to_base();
}

void Team::drop_flag(int n, const WorldCoords& pos) throw () {
    team_flags[n].move(pos);
    team_flags[n].drop();
}

void Team::move_flag(int n, const WorldCoords& pos) throw () {
    team_flags[n].move(pos);
}

void Team::set_flag_drop_time(int n, double time) throw () {
    team_flags[n].set_drop_time(time);
}

void Team::set_flag_return_time(int n, double time) throw () {
    team_flags[n].set_return_time(time);
}

double Team::accuracy() const throw () {
    if (total_shots == 0)
        return 0;
    else
        return static_cast<double>(total_hits) / total_shots;
}

// Flag

Flag::Flag(const WorldCoords& pos_) throw () :
    status(status_at_base),
    carrier_id(-1),
    return_t(-1e10),
    home_pos(pos_),
    pos(pos_),
    cteam(-1),
    ctime(0)
{ }

void Flag::take(int carr) throw () {
    status = status_carried;
    carrier_id = carr;
}

void Flag::take(int carr, double time) throw () {
    if (status == status_at_base)
        grab_t = time;
    status = status_carried;
    carrier_id = carr;
}

void Flag::return_to_base() throw () {
    status = status_at_base;
    pos = home_pos;
    carrier_id = -1;
    cteam = -1;
}

void Flag::drop() throw () {
    status = status_dropped;
    carrier_id = -1;
}

void Flag::add_carrying_time(int team) throw () {
    nAssert(team == 0 || team == 1);
    if (cteam != team) {
        cteam = team;
        ctime = 0;
    }
    ++ctime;
}

// Statistics

Statistics::Statistics() throw () :
    total_frags(0),
    total_kills(0),
    total_deaths(0),
    total_deathbringer_kills(0),
    total_deathbringer_deaths(0),
    most_consecutive_kills(0),
    current_consecutive_kills(0),
    most_consecutive_deaths(0),
    current_consecutive_deaths(0),
    total_suicides(0),
    total_captures(0),
    total_flags_taken(0),
    total_flags_dropped(0),
    total_flags_returned(0),
    total_flag_carriers_killed(0),
    total_shots(0),
    total_hits(0),
    total_shots_taken(0),
    last_spawn_time(0),
    total_lifetime(0),
    total_movement(0),
    saved_speed(0),
    starttime(0),
    dead(true),
    flag(false),
    wild_flag(false),
    total_flag_carrying_time(0),
    flag_taking_time(0)
{ }

void Statistics::clear(bool preserveTime) throw () {
    const double time = starttime;
    *this = Statistics();
    if (preserveTime)
        starttime = time;
}

void Statistics::kill(double time, bool allowAlreadyDead) throw () {
    if (!allowAlreadyDead)
        nAssert(!dead);
    if (dead)
        return;
    dead = true;
    total_lifetime += time - last_spawn_time;
}

void Statistics::add_kill(bool deathbringer) throw () {
    ++total_kills;
    if (++current_consecutive_kills > most_consecutive_kills)
        most_consecutive_kills = current_consecutive_kills;
    current_consecutive_deaths = 0;
    if (deathbringer)
        ++total_deathbringer_kills;
}

void Statistics::add_death(bool deathbringer, double time) throw () {
    ++total_deaths;
    if (++current_consecutive_deaths > most_consecutive_deaths)
        most_consecutive_deaths = current_consecutive_deaths;
    current_consecutive_kills = 0;
    if (deathbringer)
        ++total_deathbringer_deaths;
    kill(time);
}

void Statistics::add_suicide(double time) throw () {
    add_death(false, time);
    ++total_suicides;
}

void Statistics::add_capture(double time) throw () {
    nAssert(flag);
    flag = wild_flag = false;
    ++total_captures;
    total_flag_carrying_time += time - flag_taking_time;
}

void Statistics::add_flag_take(double time, bool wild) throw () {
    nAssert(!flag);
    flag = true;
    wild_flag = wild;
    ++total_flags_taken;
    flag_taking_time = time;
}

void Statistics::add_flag_drop(double time) throw () {
    nAssert(flag);
    flag = wild_flag = false;
    ++total_flags_dropped;
    total_flag_carrying_time += time - flag_taking_time;
}

void Statistics::finish_stats(double time) throw () {
    if (!dead) {
        dead = true;
        total_lifetime += time - last_spawn_time;
    }
    if (flag) {
        flag = wild_flag = false;
        total_flag_carrying_time += time - flag_taking_time;
    }
}

double Statistics::accuracy() const throw () {
    if (total_shots == 0)
        return 0;
    else
        return static_cast<double>(total_hits) / total_shots;
}

double Statistics::lifetime(double time) const throw () {
    if (dead)
        return total_lifetime;
    else
        return total_lifetime + (time - last_spawn_time);
}

double Statistics::average_lifetime(double time) const throw () {
    return lifetime(time) / (total_deaths + 1);
}

double Statistics::playtime(double time) const throw () {
    return time - starttime;
}

double Statistics::movement() const throw () {
    return total_movement;
}

double Statistics::speed(double time) const throw () {
    const double lt = lifetime(time);
    if (lt == 0.)
        return 0.;
    return movement() / lt / PLAYER_RADIUS / 2.;
}

double Statistics::flag_carrying_time(double time) const throw () {
    if (!flag)
        return total_flag_carrying_time;
    else
        return total_flag_carrying_time + (time - flag_taking_time);
}
