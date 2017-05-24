/*
 *  antialias.h
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

#ifndef ANTIALIAS_H_INC
#define ANTIALIAS_H_INC

#include <vector>
#include <list>
#include "incalleg.h"
#include "nassert.h"
#include "utility.h"

// // // // internal definitions

class PartialPixelSegment {
public:
    enum { scale = 20 };    // scale alphas by 1 << scale ((1 << scale) / 2 equals half transparent)

    PartialPixelSegment(int x0) throw () : startx(x0) { }
    int x0() const throw () { return startx; }
    int len() const throw () { return pixels.size(); }
    void add(int index, int color, int alpha) throw () { pixels[index].add(color, alpha); }
    void extend(int color, int alpha) throw () { pixels.push_back(PartPix(color, alpha)); }
    void draw(BITMAP* buf, int y) const throw ();

private:
    class PartPix {
        int r, g, b, alphaTotal;
        enum { scale = PartialPixelSegment::scale, scaleVal = 1 << PartialPixelSegment::scale };

    public:
        PartPix(int color, int alpha) throw () :
            r(getr(color) * alpha),
            g(getg(color) * alpha),
            b(getb(color) * alpha),
            alphaTotal(alpha) { }
        void add(int color, int alpha) throw () {
            r += getr(color) * alpha;
            g += getg(color) * alpha;
            b += getb(color) * alpha;
            alphaTotal += alpha;
        }
        bool draw() const throw () { return alphaTotal >= scaleVal / 100; }
        int color() const throw () {
            // this ensures that only whole pixels are written; enable if that's true:  numAssert(alphaTotal >= .999, alphaTotal * 10000.);
            numAssert(alphaTotal <= scaleVal * 1.001, alphaTotal);
            return makecol((r + scaleVal / 2) >> scale, (g + scaleVal / 2) >> scale, (b + scaleVal / 2) >> scale);
        }
        int flexColor() const throw () { // allows more than 1 alphaTotal, with a cut on high intensities
            int rc, gc, bc;
            if (alphaTotal > scaleVal * 1.001) {
                rc = (r + scaleVal / 2) / alphaTotal;
                gc = (g + scaleVal / 2) / alphaTotal;
                bc = (b + scaleVal / 2) / alphaTotal;
            }
            else {
                rc = (r + scaleVal / 2) >> scale;
                gc = (g + scaleVal / 2) >> scale;
                bc = (b + scaleVal / 2) >> scale;
            }
/*
            int rc = std::min(255, (r + scaleVal / 2) >> scale);
            int gc = std::min(255, (g + scaleVal / 2) >> scale);
            int bc = std::min(255, (b + scaleVal / 2) >> scale);
*/
/*
            int rc = (r + scaleVal / 2) >> scale;
            int gc = (g + scaleVal / 2) >> scale;
            int bc = (b + scaleVal / 2) >> scale;
            if (alphaTotal >= scaleVal && (rc > 255 || gc > 255 || bc > 255)) { // the alphaTotal check is an optimization
                int cut = max(max(rc, gc), bc) - 255;
                rc = std::max(0, rc - cut);
                gc = std::max(0, gc - cut);
                bc = std::max(0, bc - cut);
            }
*/
            return makecol(rc, gc, bc);
        }
    };

    int startx;
    std::vector<PartPix> pixels;
};

class RectWall;
class TriWall;
class CircWall;
class WallBase;
class BorderFunctionBase;
class LineFunction;
class DrawElement;

struct WallBorderSegment {
    BorderFunctionBase* fn;
    double y0, y1;
    WallBorderSegment() throw () { }
    WallBorderSegment(BorderFunctionBase* fn_, double y0_, double y1_) throw () : fn(fn_), y0(y0_), y1(y1_) { }
};

struct ObjectSource {
    int texid;
    bool overlay;
    std::vector<WallBorderSegment> borders;
};

// // // // public interface

struct SolidTexdata {
    int color;
    int alpha;

    void set(int color_, int alpha_ = 256) throw () { color = color_; alpha = alpha_; }
};

struct TextureTexdata {
    BITMAP* image;
    int x0, y0;
    int alpha;

    void set(BITMAP* texture, int x0_ = 0, int y0_ = 0, int alpha_ = 256) throw () { image = texture; alpha = alpha_; x0 = x0_; y0 = y0_; }
};

struct FlagmarkerTexdata {
    int color;
    double cx, cy;
    double radius;

    void set(int color_, double cx_, double cy_, double r) throw () { color = color_; cx = cx_; cy = cy_; radius = r; }
};

class TextureData {
public:
    void setSolid(int color, int alpha = 256) throw () { t = T_solid; d.s.set(color, alpha); }
    void setTexture(BITMAP* texture, int x0 = 0, int y0 = 0, int alpha = 256) throw () { t = T_texture; d.t.set(texture, x0, y0, alpha); }
    void setFlagmarker(int color, double cx, double cy, double r) throw () { t = T_flagmarker; d.f.set(color, cx, cy, r); }

    enum TexType { T_solid, T_texture, T_flagmarker };
    typedef union {
        SolidTexdata s;
        TextureTexdata t;
        FlagmarkerTexdata f;
    } TexdataUnion;

    TexType type() const throw () { return t; }
    const TexdataUnion& data() const throw () { return d; }

private:
    TexType t;
    TexdataUnion d;
};

class Texturizer {
public:
    // caution: because of Allegro's limitations, textures used as non-overlays must have their width and height a power of two; this is not checked!
    // also, textures (overlay or not) may not be used in areas where x < tex.x0 || y < tex.y0 ; by selecting x0 and y0 < 0 you can avoid this problem
    Texturizer(BITMAP* buffer, int x0, int y0, const std::vector<TextureData>& textures) throw ()
                    : buf(buffer), bx0(x0), by0(y0), texTab(textures), partials(buffer->h) { }

    void render(const std::vector<int>& textures, const DrawElement* elp) throw ();
    void finalize() throw ();    // draws all buffered pixels (use only when no longer drawing)

// semi-private: for use by rendering functions called by render() only
    void setLine(int y) throw ();
    void nextLine() throw ();

    inline void startPixSpan(int x) throw ();
    inline void putPix(int color, int alpha) throw ();   // draws at current x coord and increases it

    inline BITMAP* getBuf() throw () { return buf; }
    inline int getbx() const throw () { return bx; }
    inline int getby() const throw () { return by; }
    inline int getbx0() const throw () { return bx0; }
    inline int getby0() const throw () { return by0; }

private:
    BITMAP* buf;
    int bx, by; // active pixel in buf
    int bx0, by0;   // buffer pixel offset

    const std::vector<TextureData>& texTab;

    std::vector< std::list<PartialPixelSegment> > partials;
    PartialPixelSegment* partSpan;
    int spanIndex;  // index in partSpan
    int spanEnd;    // when we must move to the next segment to continue adding pixels
};

class SceneAntialiaser : private NoCopying {
public:
    SceneAntialiaser() throw () { }
    ~SceneAntialiaser() throw ();

    void setScaling(double x0_ = 0, double y0_ = 0, double scale_ = 1.) throw ();    // call before add*

    void addRectangle(double x1, double y1, double x2, double y2, int texture, bool overlay = false) throw ();
    void addRectWall(const RectWall& wall, int texture) throw ();
    void addTriWall (const  TriWall& wall, int texture) throw ();
    void addCircWall(const CircWall& wall, int texture) throw ();
    void addWall    (const WallBase* wall, int texture) throw ();

    void setClipping(double x1, double y1, double x2, double y2) throw ();   // setClipping applies scaling to the coords
    void addRectWallClipped(const RectWall& wall, int texture) throw ();
    void addTriWallClipped (const  TriWall& wall, int texture) throw ();
    void addCircWallClipped(const CircWall& wall, int texture) throw ();
    void addWallClipped    (const WallBase* wall, int texture) throw ();
    void clipAll() throw () { clip(0); } // clips all added objects to the current clipping rectangle

    int getClipPos() const throw () { return objects.size(); }
    void clipFrom(int base) throw () { clip(base); }

    void render(Texturizer& tex) const throw ();

private:
    void createClipFns() throw ();
    void clip(int i0) throw ();
    template<class Texturizer> void renderTemplate(Texturizer& tex) const throw ();

    std::vector<BorderFunctionBase*> bfns;
    std::vector<ObjectSource> objects;
    LineFunction* clipLeft, * clipRight;
    bool clipFunctionsValid;
    double x0, y0, scale;
    double clipx1, clipy1, clipx2, clipy2;
};

#endif  // ANTIALIAS_H_INC
