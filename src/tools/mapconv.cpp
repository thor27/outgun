/*
 *  mapconv.cpp
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
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

#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cassert>

#ifndef __GNUC__
#define __attribute__(a)
#endif

static int mapconv_stricmp(const char* s1, const char* s2) throw () {
    for (;; ++s1, ++s2) {
        if (toupper(*s1) != toupper(*s2))
            return int(toupper(*s1)) - int(toupper(*s2));
        if (*s1 == '\0')
            return 0;
    }
}

using std::string;
using std::vector;
using std::for_each;
using std::mem_fun_ref;

bool compatible = false, verbose = false, pack = false, duplicateRooms = false;

class MapObject {
public:
    virtual ~MapObject() throw () { }
    virtual void write(FILE* dst, int rx, int ry, float xscale, float yscale) const throw () = 0;
};

const char* fmtFloat(float value, bool forceFloat = true) throw () {
    static char buf[5][10];
    static int bufi = 0;
    if (value < 0 || value > 999)
        return "ERR";
    char* cbuf = buf[bufi];
    if (++bufi == 5)
        bufi = 0;
    if (compatible || forceFloat) {
        int si;
        char fill;
        if (pack) {
            si = sprintf(cbuf, "%.3f", value) - 1;
            fill = '\0';
        }
        else {
            si = sprintf(cbuf, "%7.3f", value) - 1;
            assert(si == 6);
            fill = ' ';
        }
        while (cbuf[si] == '0') {
            cbuf[si] = fill;
            --si;
        }
        if (cbuf[si] == '.')
            cbuf[si] = fill;
    }
    else {
        assert(value == static_cast<int>(value));
        sprintf(cbuf, "%2d", static_cast<int>(value));
    }
    return cbuf;
}

class Flag : public MapObject {
    int team;
    float x, y;

public:
    Flag(int team_, float x_, float y_) throw () : team(team_), x(x_), y(y_) { }
    void write(FILE* dst, int rx, int ry, float xscale, float yscale) const throw () { fprintf(dst, "flag  %d %2d %2d %s %s\n", team, rx, ry, fmtFloat(x * xscale), fmtFloat(y * yscale)); }
};

class Spawn : public MapObject {
    int team;
    float x, y;

public:
    Spawn(int team_, float x_, float y_) throw () : team(team_), x(x_), y(y_) { }
    void write(FILE* dst, int rx, int ry, float xscale, float yscale) const throw () { fprintf(dst, "spawn %d %2d %2d %s %s\n", team, rx, ry, fmtFloat(x * xscale), fmtFloat(y * yscale)); }
};

class Wall {
    float x0, y0, x1, y1;

public:
    Wall(float x0_, float y0_, float x1_, float y1_) throw () : x0(x0_), y0(y0_), x1(x1_), y1(y1_) { }
    void scale(float xs, float ys) throw () { x0 /= xs; y0 /= ys; x1 /= xs; y1 /= ys; }
    void write(FILE* dst) const throw () { fprintf(dst, "W %s %s %s %s\n", fmtFloat(x0, false), fmtFloat(y0, false), fmtFloat(x1, false), fmtFloat(y1, false)); }
};

void debugMem(void* ptr) throw () {
    printf("%p  ", ptr);
    const unsigned char* cp = static_cast<const unsigned char*>(ptr);
    for (int i = 0; i < 16; ++i)
        printf("%02X ", cp[i]);
    for (int i = 0; i < 16; ++i)
        printf("%c", isprint(cp[i]) ? cp[i] : '.');
    printf("\n");
}

void delObject(MapObject* ptr) throw () { delete ptr; }

class Room {
    int scalex, scaley;
    vector<Wall> walls;
    vector<MapObject*> objects;

public:
    Room() throw () { }
    void setScaling(int sx, int sy) throw () { scalex = sx; scaley = sy; }
    void addWall(Wall wall) throw () { if (compatible) wall.scale(scalex, scaley); walls.push_back(wall); }
    bool empty() const throw () { return walls.empty(); } // don't care about objects
    void addObject(MapObject* obj) throw () { objects.push_back(obj); }
    void freeObjects() throw () { for_each(objects.begin(), objects.end(), delObject); }
    void writeObjects(FILE* dst, int rx, int ry, bool writeScale) const throw () {
        if (objects.empty())
            return;
        float objScaleX, objScaleY;
        if (compatible)
            objScaleX = objScaleY = 1;
        else {
            if (writeScale)
                fprintf(dst, "S %2d %2d\n", scalex, scaley);
            objScaleX = scalex;
            objScaleY = scaley;
        }
        for (vector<MapObject*>::const_iterator oi = objects.begin(); oi != objects.end(); ++oi)
            (*oi)->write(dst, rx, ry, objScaleX, objScaleY);
    }
    void writeWalls(FILE* dst, int rx, int ry, bool writeScale) const throw () {
        fprintf(dst, "R %2d %2d\n", rx, ry);
        writeWalls(dst, writeScale);
    }
    void writeWalls(FILE* dst, bool writeScale) const throw () {
        if (!compatible && writeScale)
            fprintf(dst, "S %2d %2d\n", scalex, scaley);
        for (vector<Wall>::const_iterator wi = walls.begin(); wi != walls.end(); ++wi)
            wi->write(dst);
    }
    void writeBoth(FILE* dst, int rx, int ry, bool writeScale) const throw () {
        writeWalls(dst, rx, ry, writeScale); // writes both R and S lines
        writeObjects(dst, rx, ry, false);
    }
};

class Map {
    string name;
    int width, height;
    int roomw, roomh; // room scaling factor; roomw negative if not uniform
    vector<Room> roomTemplates;
    vector< vector<int> > roomIndices;

public:
    ~Map() throw () { for_each(roomTemplates.begin(), roomTemplates.end(), mem_fun_ref(&Room::freeObjects)); }
    const char* load085(FILE* src) throw (); // returns error message or 0 for success
    void write050(FILE* dst) const throw ();
};

void optPrintf(const char* fmt, ...) throw () __attribute__ ((format (printf, 1, 2)));

void optPrintf(const char* fmt, ...) throw () {
    if (!verbose)
        return;
    va_list arglist;
    va_start(arglist, fmt);
    vprintf(fmt, arglist);
    va_end(arglist);
}

const char* Map::load085(FILE* src) throw () {
    int lastRoom = -1;
    width = height = 0;
    roomw = -1, roomh = -1;
    for (;;) {
        char buf[100];
        char endch;
        int n = fscanf(src, "%30s", buf);
        if (feof(src)) {
            optPrintf("End of file reached\n");
            if (name.empty() || width == 0 || height == 0 || roomTemplates.empty() || roomIndices.empty())
                return "Incomplete file";
            return 0;
        }
        if (n != 1)
            return "Missing entry header";
        optPrintf("Entry '%s'\n", buf);
        if (!mapconv_stricmp(buf, "MapName")) {
            n = fscanf(src, " %99[^;\n]%c", buf, &endch);
            if (n != 2 || endch != ';' || !name.empty())
                return "Bad MapName entry";
            name = buf;
            while (!name.empty() && name[name.length() - 1] == ' ')
                name.erase(name.length() - 1);
        }
        else if (!mapconv_stricmp(buf, "Map")) {
            n = fscanf(src, " %d %d", &width, &height);
            if (n != 2 || width <= 0 || height <= 0 || width > 40 || height > 40 || !roomIndices.empty())
                return "Bad Map entry header";
            roomIndices.resize(width);
            for (int i = 0; i < width; ++i)
                roomIndices[i].resize(height);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    int roomi;
                    n = fscanf(src, " %d", &roomi);
                    --roomi;
                    if (n != 1 || roomi < 0 || roomi >= (int)roomTemplates.size())
                        return "Bad Map entry room index";
                    roomIndices[x][y] = roomi;
                }
            n = fscanf(src, " %c", &endch);
            if (n != 1 || endch != ';')
                return "Bad Map entry (length)";
        }
        else if (!mapconv_stricmp(buf, "Objects")) {
            int numObj;
            n = fscanf(src, " %d", &numObj);
            if (n != 1 || lastRoom < 0)
                return "Bad Objects entry header";
            optPrintf("Adding %d objects to room template %d\n", numObj, lastRoom + 1);
            Room& roomDst = roomTemplates[lastRoom];
            for (int obj = 0; obj < numObj; ++obj) {
                const int numRecog = 11;
                typedef char nameBuf[6];
                const nameBuf name[numRecog] = { "BF", "RF", "SPB", "SPR", "GAC", "GFC", "GMSC", "GQBSC", "GSBSC", "GSSC", "GWBSC" };
                const int  numArgs[numRecog] = {    2,    2,     5,     5,     4,     4,      5,       1,       1,      1,       1 };
                int nulli;
                n = fscanf(src, " %10s %d", buf, &nulli);
                if (n != 2)
                    return "Bad Objects entry object line";
                optPrintf("Object '%s'\n", buf);
                int type;
                for (type = 0; mapconv_stricmp(buf, name[type]); ++type)
                    if (type == numRecog - 1)
                        return "Unknown Objects entry object type";
                int nargs = numArgs[type];
                float args[5];
                for (int argi = 0; argi < nargs; ++argi) {
                    n = fscanf(src, " %f", &args[argi]);
                    if (n != 1)
                        return "Bad Objects entry object argument";
                }
                switch (type) {
                    case 0: roomDst.addObject(new Flag(1, args[0], args[1])); break;
                    case 1: roomDst.addObject(new Flag(0, args[0], args[1])); break;
                    case 2: roomDst.addObject(new Spawn(1, args[0], args[1])); break;
                    case 3: roomDst.addObject(new Spawn(0, args[0], args[1])); break;
                    default: break;
                }
            }
            n = fscanf(src, " %c", &endch);
            if (n != 1 || endch != ';')
                return "Bad Objects entry (length)";
        }
        else if (!mapconv_stricmp(buf, "Version")) {
            n = fscanf(src, " %*[^;]%c", &endch);
            if (n != 1 || endch != ';')
                return "Bad Version entry";
        }
        else if (!mapconv_stricmp(buf, "Rooms")) {
            int nrooms;
            n = fscanf(src, " %d %c", &nrooms, &endch);
            if (n != 2 || endch != ';')
                return "Bad Rooms entry";
        }
        else if (!mapconv_stricmp(buf, "Room")) {
            int id, w, h;
            n = fscanf(src, " %d %d %d", &id, &w, &h);
            --id;
            if (n != 3 || w < 0 || h < 0 || w > 200 || h > 200 || id < 0 || id > 50)
                return "Bad Room entry header";
            if (roomw == -1) {
                roomw = w;
                roomh = h;
            }
            else if (w != roomw || h != roomh)
                roomw = -9;
            if (id >= (int)roomTemplates.size())
                roomTemplates.resize(id + 1);
            vector< vector<int> > matrix;
            matrix.resize(w);
            for (int i = 0; i < w; ++i)
                matrix[i].resize(h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    int tile;
                    n = fscanf(src, " %d", &tile);
                    if (n != 1 || (tile != 0 && tile != 1))
                        return "Bad Room entry tilemap tile";
                    matrix[x][y] = tile;
                }
            n = fscanf(src, " %c", &endch);
            if (n != 1 || endch != ';')
                return "Bad Room entry (length)";
            Room& roomDst = roomTemplates[id];
            roomDst.setScaling(w, h);
            // the fun part: try to make the most sensible collection of walls out of the matrix (not really)
            for (int y0 = 0; y0 < h; ++y0)
                for (int x0 = 0; x0 < w; ++x0)
                    if (matrix[x0][y0] == 1) { // a wall starts here
                        int x1, y1;
                        // extend x direction as far as possible
                        for (x1 = x0; x1 < w; ++x1)
                            if (matrix[x1][y0] == 0)
                                break;
                        --x1;
                        // extend y direction as far as possible
                        for (y1 = y0; y1 < h; ++y1)
                            for (int x = x0; x <= x1; ++x)
                                if (matrix[x][y1] == 0)
                                    goto bady;
                        bady:
                        --y1;
                        // take back those rows/columns that contain no new wall-pixels
                        bool change;
                        do {
                            change = false;
                            for (int x = x0; matrix[x][y1] == -1; ++x)
                                if (x == x1) {
                                    --y1;
                                    change = true;
                                    break;
                                }
                            for (int y = y0; matrix[x1][y] == -1; ++y)
                                if (y == y1) {
                                    --x1;
                                    change = true;
                                    break;
                                }
                        } while (change);
                        // mark as used
                        for (int y = y0; y <= y1; ++y)
                            for (int x = x0; x <= x1; ++x)
                                matrix[x][y] = -1;
                        roomDst.addWall(Wall(x0, y0, x1 + 1, y1 + 1));
                    }
            lastRoom = id;
        }
        else if (!mapconv_stricmp(buf, "Walls") || !mapconv_stricmp(buf, "ObjectResources")) {
            while (fgetc(src) != ';')
                if (feof(src))
                    return buf[0]=='W'?"Bad Walls entry":"Bad ObjectResources entry";
        }
        else
            return "Unknown entry type";
    }
}

void Map::write050(FILE* dst) const throw () {
    const bool uniformScale = roomw > 0;
    fprintf(dst, "; This map has been automatically converted from 0.8.5 format\n");
    fprintf(dst, "; with Mapconv (http://koti.mbnet.fi/outgun/tools/mapconv.html)\n");
    fprintf(dst, "\n");
    fprintf(dst, "P title %s\n", name.c_str());
    fprintf(dst, "P width %d\n", width);
    fprintf(dst, "P height %d\n", height);
    if (uniformScale && !compatible)
        fprintf(dst, "S %d %d\n", roomw, roomh);
    if (duplicateRooms)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                fprintf(dst, "\n");
                roomTemplates[ roomIndices[x][y] ].writeBoth(dst, x, y, !uniformScale);
            }
    else {
        vector<int> uses(roomTemplates.size(), 0);
        fprintf(dst, "\n");
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                roomTemplates[ roomIndices[x][y] ].writeObjects(dst, x, y, !uniformScale);
                ++uses[ roomIndices[x][y] ];
            }
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                fprintf(dst, "\n");
                if (uses[ roomIndices[x][y] ] == 1)
                    roomTemplates[ roomIndices[x][y] ].writeWalls(dst, x, y, !uniformScale);
                else
                    fprintf(dst, "X room%d %d %d\n", roomIndices[x][y], x, y);
            }
        for (int i = 0; i < (int)roomTemplates.size(); ++i)
            if (uses[i] > 1) {
                fprintf(dst, "\n:room%d\n", i);
                roomTemplates[i].writeWalls(dst, !uniformScale);
            }
    }
}

int main(int argc, const char* argv[]) {
    const char* prog = argv[0];
    ++argv; --argc;
    while (argc && strchr("-/", (*argv)[0]) && (*argv)[2] == '\0') {
        if (strchr("cC", (*argv)[1]))
            compatible = true;
        else if (strchr("vV", (*argv)[1]))
            verbose = true;
        else if (strchr("pP", (*argv)[1]))
            pack = true;
        else if (strchr("dD", (*argv)[1]))
            duplicateRooms = true;
        else {
            argc = -1; // signal error
            break;
        }
        ++argv; --argc;
    }
    if (argc != 2) {
        printf("Syntax: %s\n", prog);
        printf("            [-c] [-p] [-v] [-d] <085-map-source> <destination>\n");
        printf(" -c compatibility    make the map 0.5.0 compatible (but less readable)\n");
        printf(" -p pack             pack lines to make the file smaller (and less readable)\n");
        printf(" -v verbose          show diagnostic messages while reading the map\n");
        printf(" -d duplicate rooms  duplicate identical rooms instead of using X directives\n");
        return 9;
    }
    FILE* fp = fopen(argv[0], "rt");
    if (!fp) {
        printf("Can't read '%s'\n", argv[0]);
        return 2;
    }
    Map m;
    const char* result = m.load085(fp);
    fclose(fp);
    if (result) {
        printf("Map format error reading '%s': %s\n", argv[0], result);
        return 1;
    }
    fp = fopen(argv[1], "wt");
    if (!fp) {
        printf("Can't write '%s'\n", argv[1]);
        return 2;
    }
    m.write050(fp);
    fclose(fp);
    printf("OK\n");
    return 0;
}

