/*
 *  antialias_internal.h
 *
 *  Copyright (C) 2004 - Niko Ritari
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

#ifndef ANTIALIAS_INTERNAL_H_INC
#define ANTIALIAS_INTERNAL_H_INC

#include <vector>
#include <list>
#include "antialias.h"

//#define DEBUG_SPLIT
//#define DEBUG_OVERLAP
//#define DEBUG_RENDER
//#define CERR_DEBUG

#ifdef CERR_DEBUG
#include <iostream>
using std::cerr;
#endif

// INTERSECTION_TRESHOLD    is how much inside a segment an intersection is allowed (must allow for rounding errors)
// SPLIT_TRESHOLD           is how much y-borders are allowed to deviate (must exceed the precision of the given y coords)
// FINAL_EXTREMECUT         is how much of each element is removed y-wise to protect from exceeding function range
// X_EXTREMECUT             is how much of each pixel edge is removed x-wise to counter imprecision (a too little cut is not fatal but does trigger an assertion)
// JOIN_TRESHOLD            is how much gap is allowed when joining two adjacent objects to one
// all tresholds' unit is a pixel
// these settings currently tune the module to work with values of y up to approximately +-1e7
// they have to be relaxed in order to gain more range
const double INTERSECTION_TRESHOLD = .0000001;  // roughly 2^-23
const double SPLIT_TRESHOLD        = .0000002;
const double FINAL_EXTREMECUT      = .000001;
const double X_EXTREMECUT          = .000001;
const double JOIN_TRESHOLD         = .001;

struct ChangePoints {
    enum Side { S_Left, S_Right };  // tells on which side of the given line the function currently is

    Side startSide;
    double points[3];   // 3 is the logical maximum of points used; increase if more are needed by complex functions
                        // convention: has one extra item at the end that is big; set at 1e99
    // the C array is an optimization: using a vector would significantly slow things down and wouldn't help at all
};

class BorderFunctionBase {
protected:
    BorderFunctionBase() throw () { }

public:
    virtual ~BorderFunctionBase() throw () { }
    virtual double operator()(double y) const throw () = 0;
    virtual ChangePoints getChangePoints(double x) const throw () = 0;
    virtual double spanLeftSideIntegral(double x0, double y0, double y1) const throw () = 0;
    virtual bool centerExtremes() const throw () = 0;    // must return true if the extreme X coordinate of some y-interval is not at either of the borders
    virtual double extremeY() const throw () = 0;    // if centerExtremes() is true, must return the Y coordinate at which the extreme X is
    virtual double extremeX() const throw () = 0;    // if centerExtremes() is true, must return the extreme X coordinate
    virtual bool operator==(const BorderFunctionBase& o) throw () = 0;
    virtual void debug() const throw () { }
};

class LineFunction;

class CurveFunction : public BorderFunctionBase {
    double cx, cy, r, r2;
    double sideMul;

    friend double getIntersection(LineFunction* f1, CurveFunction* f2, double miny) throw ();
    friend double getIntersection(CurveFunction* f1, CurveFunction* f2, double miny) throw ();

public:
    CurveFunction(double cx_, double cy_, double r_, bool rightSide) throw () : cx(cx_), cy(cy_), r(r_), r2(r_*r_), sideMul(rightSide?+1:-1) { }
    double operator()(double y) const throw ();
    ChangePoints getChangePoints(double x) const throw ();
    double spanLeftSideIntegral(double x0, double y0, double y1) const throw ();
    bool centerExtremes() const throw () { return true; }
    double extremeY() const throw () { return cy; }
    double extremeX() const throw () { return cx + sideMul * r; }
    bool operator==(const BorderFunctionBase& o) throw ();
    void debug() const throw ();
};

class LineFunction : public BorderFunctionBase {
    double px1, py1, px2, py2;
    double ratio;

    friend double getIntersection(LineFunction* f1, LineFunction* f2) throw ();
    friend double getIntersection(LineFunction* f1, CurveFunction* f2, double miny) throw ();

public:
    LineFunction(double x1, double y1, double x2, double y2) throw () : px1(x1), py1(y1), px2(x2), py2(y2), ratio((x2 - x1)/(y2 - y1)) { }
    double operator()(double y) const throw ();
    ChangePoints getChangePoints(double x) const throw ();
    double spanLeftSideIntegral(double x0, double y0, double y1) const throw ();
    bool centerExtremes() const throw () { return false; }
    double extremeY() const throw () { nAssert(0); return 0; }
    double extremeX() const throw () { nAssert(0); return 0; }
    bool operator==(const BorderFunctionBase& o) throw ();
    void debug() const throw ();
};

class DrawElement {
    BorderFunctionBase* fLeft, * fRight;    // these are not owned: the object pointed to must live as long as this DrawElement is used, and must be manually deleted
    double y0, y1;
    std::vector<int> texid;

public:
    DrawElement(BorderFunctionBase* flp, BorderFunctionBase* frp, double y0_, double y1_, std::vector<int> tex) throw ();
    void extendDown(double y1_) throw () { nAssert(y1_ > y1); y1 = y1_; }
    const BorderFunctionBase& getLeft() const throw () { return *fLeft; }
    const BorderFunctionBase& getRight() const throw () { return *fRight; }
    double getY0() const throw () { return y0; }
    double getY1() const throw () { return y1; }
    int getBaseTex() const throw () { return texid.front(); }
    const std::vector<int>& getAllTextures() const throw () { return texid; }
    bool isJoinable(const DrawElement& o) const throw ();
};

class YSegment {
    class TexBorder {
        BorderFunctionBase* bfp;
        std::vector<int> texid;

    public:
        TexBorder(BorderFunctionBase* bf, int tex) throw () : bfp(bf), texid(1, tex) { }
        TexBorder(BorderFunctionBase* bf, std::vector<int> tex) throw () : bfp(bf), texid(tex) { }
        BorderFunctionBase* getFn() const throw () { return bfp; }
        int getBaseTex() const throw () { return texid.front(); }
        const std::vector<int>& getAllTextures() const throw () { return texid; }
        void setTex(int tex) throw () { texid.clear(); texid.push_back(tex); }
        void addTex(int tex) throw () { texid.push_back(tex); }
    };

    typedef std::vector<BorderFunctionBase*> BorderListT;
    typedef std::list<TexBorder> TexBorderListT;

    double y0, y1;
    BorderListT build;
    TexBorderListT final;

    class BorderCompare {
        double my1, my2, my3;

    public:
        BorderCompare(double y0, double y1) throw () : my1(y0*.8 + y1*.2), my2(y0*.5 + y1*.5), my3(y0*.2 + y1*.8) { nAssert(fabs(my3-my1)>SPLIT_TRESHOLD*.1); }
        bool operator()(const BorderFunctionBase* o1, const BorderFunctionBase* o2) throw ();
    };

public:
    YSegment(double y0_, double y1_) throw () : y0(y0_), y1(y1_) { nAssert(y1 >= y0); }
    double getY0() const throw () { return y0; }
    double getY1() const throw () { return y1; }
    double width() const throw () { nAssert(y1 >= y0); return y1 - y0; }
    void setY0(double y) throw () { nAssert(y >= y0); nAssert(y1 >= y); y0 = y; }    // only allow shrinking the segment
    void setY1(double y) throw () { nAssert(y <= y1); nAssert(y >= y0); y1 = y; }
    void add(BorderFunctionBase* border) throw () { build.push_back(border); }   // ownership of the pointed object is not transferred!
    bool getFirstIntersection(BorderFunctionBase* bfn, double* splity) throw ();
    YSegment split(double midy) throw ();    // the segment starting with midy is the returned one
    void sort() throw ();    // sorts the build list borders in increasing x-order
    void simplify() throw ();    // removes double borders from build list (assuming it's sorted)
    void moveElements(int texid) throw ();   // moves all borders from build list to final list (use only when the final list is empty)
    void moveElementsWithOverlap(int texid, bool overlay) throw ();  // moves all borders from build list to final list overlapping the old walls
    void extractDrawElements(std::list<DrawElement>& dst) const throw ();
    void debug(bool verbose = false) const throw ();

    YSegment(const YSegment& o) throw () { *this = o; }
    YSegment& operator=(const YSegment& o) throw () { y0 = o.y0; y1 = o.y1; build = o.build; final = o.final; return *this; }
};

typedef std::list<YSegment> SegListT;

class PixelSource : private NoCopying { // base class
protected:
    PixelSource() throw () { }

public:
    virtual ~PixelSource() throw () { }
    virtual void setLine(int y) throw () = 0;
    virtual void nextLine() throw () = 0;
    virtual void startPixSpan(int x) throw () = 0;
    virtual std::pair<int, int> getPixel() throw () = 0;  // returns (color, alpha) of the current x coord and increases it
};

class SolidPixelSource : public PixelSource {
    int color, alpha;

public:
    SolidPixelSource(const SolidTexdata& td) throw () : color(td.color), alpha(td.alpha) { }
    void setLine(int) throw () { }
    void nextLine() throw () { }
    void startPixSpan(int) throw () { }
    std::pair<int, int> getPixel() throw ();
};

class SolidTexturizer { // includes inlined the same operations as SolidPixelSource
    Texturizer& host;
    int color;

    void putPixI(int alpha) throw () { host.putPix(color, alpha); }

public:
    SolidTexturizer(Texturizer& host_, const SolidTexdata& td) throw () : host(host_), color(td.color) { nAssert(td.alpha == 256); }

    void setLine(int y) throw () { host.setLine(y); }
    void nextLine() throw () { host.nextLine(); }
    void putSpan(int x0, int x1, double alpha) throw ();   // fills the range [x0,x1[
    void startPixSpan(int x) throw () { host.startPixSpan(x); }
    void putPix(double alpha) throw ();  // draws at current x coord and increases it
};

class TexturePixelSource : public PixelSource {
    BITMAP* tex;    // can't set const because it can be fed to Allegro
    int alpha;
    int tx0, ty0;
    int tx, ty; // active pixel in tex

public:
    TexturePixelSource(const TextureTexdata& td) throw () : tex(td.image), alpha(td.alpha), tx0(td.x0), ty0(td.y0) { }

    void setLine(int y) throw ();
    void nextLine() throw ();
    void startPixSpan(int x) throw ();
    std::pair<int, int> getPixel() throw ();
};

class TextureTexturizer { // includes inlined the same operations as TexturePixelSource
    Texturizer& host;
    BITMAP* tex;    // can't set const because it can be fed to Allegro
    int tx0, ty0;
    int tx, ty; // active pixel in tex

    void putPixI(int alpha) throw ();

public:
    TextureTexturizer(Texturizer& host_, const TextureTexdata& td) throw () : host(host_), tex(td.image), tx0(td.x0), ty0(td.y0) { nAssert(td.alpha == 256); }

    void setLine(int y) throw ();
    void nextLine() throw ();
    void putSpan(int x0, int x1, double alpha) throw ();   // fills the range [x0,x1[
    void startPixSpan(int x) throw ();
    void putPix(double alpha) throw ();  // draws at current x coord and increases it
};

class FlagmarkerPixelSource : public PixelSource {
    int color;
    double markRadius;
    double intensityMul;
    double cx, cy;
    double dy, dy2;
    double dx;

public:
    FlagmarkerPixelSource(const FlagmarkerTexdata& td) throw ();

    void setLine(int y) throw ();
    void nextLine() throw ();
    void startPixSpan(int x) throw ();
    std::pair<int, int> getPixel() throw ();
};

class MultiLayerTexturizer {
    Texturizer& host;
    std::vector<PixelSource*> layers;

public:
    MultiLayerTexturizer(Texturizer& host_, int layersReserve = 2) throw () : host(host_) { layers.reserve(layersReserve); }
    ~MultiLayerTexturizer() throw ();
    void addLayer(PixelSource* layerSource) throw () { layers.push_back(layerSource); }  // ownership is transferred
    // at least one layer is assumed, don't try to draw without calling addLayer first

    void setLine(int y) throw ();
    void nextLine() throw ();
    void putSpan(int x0, int x1, double alpha) throw ();   // fills the range [x0,x1[
    void startPixSpan(int x) throw ();
    void putPix(double alpha) throw ();  // draws at current x coord and increases it
};

#endif
