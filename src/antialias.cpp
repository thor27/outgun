/*
 *  antialias.cpp
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

#include <algorithm>
#include <cmath>
#include "world.h"

#include "antialias_internal.h"
#include "antialias.h"

using std::vector;
using std::list;
using std::min;
using std::max;
using std::pair;
using std::swap;

double CurveFunction::operator()(double y) const throw () {
    numAssert3(r2 - (y - cy)*(y - cy) >= 0, r2 * 1000., y * 1000., cy * 1000.);
    return cx + sqrt(r2 - (y - cy)*(y - cy)) * sideMul;
}

ChangePoints CurveFunction::getChangePoints(double x) const throw () {
    ChangePoints ret;
    if ((x - cx) * sideMul < 0) {   // x is on the undefined side
        ret.startSide = sideMul>0 ? ChangePoints::S_Right : ChangePoints::S_Left;
        ret.points[0] = 1e99;
    }
    else {
        ret.startSide = sideMul>0 ? ChangePoints::S_Left : ChangePoints::S_Right;
        // change points are where x*x+y*y=r*r
        double rt = r2 - (x - cx)*(x - cx);
        if (rt > 0.) {
            rt = sqrt(rt);
            ret.points[0] = cy - rt;
            ret.points[1] = cy + rt;
            ret.points[2] = 1e99;
        }
        else
            ret.points[0] = 1e99;
    }
    return ret;
}

double CurveFunction::spanLeftSideIntegral(double x0, double y0, double y1) const throw () {
    // this function computes the integral from y0 to y1 of (x(y) - x0)dy ; derive the expression yourself ;)
    y0 -= cy; y1 -= cy;
    numAssert3(fabs(y0) <= r, y0, r, (y0 - r) * 1e20);
    numAssert3(fabs(y1) <= r, y1, r, (y1 - r) * 1e20);
    const double val = (y1 - y0)*(cx - x0) + .5*sideMul*( y1*sqrt(r2 - y1*y1) - y0*sqrt(r2 - y0*y0) + r2*(asin(y1/r) - asin(y0/r)) );
    numAssert(val >= -.00001 && val <= 1.00001, val * 10000.);
    return val;
}

bool CurveFunction::operator==(const BorderFunctionBase& o) throw () {
    const CurveFunction* cfp = dynamic_cast<const CurveFunction*>(&o);
    if (!cfp)
        return false;
    return cx == cfp->cx && cy == cfp->cy && r == cfp->r && sideMul == cfp->sideMul;
}

void CurveFunction::debug() const throw () {
    #ifdef CERR_DEBUG
    cerr.precision(12);
    cerr << "Curve @" << cx << ',' << cy << " R=" << r << (sideMul>0 ? " right\n" : " left\n");
    #endif
}

double LineFunction::operator()(double y) const throw () {
    if (py1 == py2) {   // in this case, ratio is invalid
        nAssert(y == py1);
        return px1;
    }
    return px1 + (y - py1) * ratio;
}

ChangePoints LineFunction::getChangePoints(double x) const throw () {
    ChangePoints ret;
    // change point is where x = px1 + (px2 - px1)*(y - py1)/(py2 - py1) ; y = py1 + (py2 - py1)*(x - px1)/(px2 - px1)
    if (py1 == py2) {   // in this case, ratio is invalid
        ret.startSide = ChangePoints::S_Left;   // arbitrary
        ret.points[0] = 1e99;
    }
    else if (ratio == 0) {
        ret.startSide = px1 < x ? ChangePoints::S_Left : ChangePoints::S_Right;
        ret.points[0] = 1e99;
    }
    else {
        ret.startSide = ratio > 0 ? ChangePoints::S_Left : ChangePoints::S_Right;
        ret.points[0] = py1 + (x - px1) / ratio;
        ret.points[1] = 1e99;
    }
    return ret;
}

double LineFunction::spanLeftSideIntegral(double x0, double y0, double y1) const throw () {
    // this function computes the integral from y0 to y1 of (x(y) - x0)dy ; derive the expression yourself ;)
    if (py1 == py2) {   // in this case, ratio is invalid
        nAssert(y0 == y1 && y0 == py1);
        return 0;
    }
    const double val = (y1 - y0)*(px1 - x0 - py1 * ratio) + .5*(y1*y1 - y0*y0)*ratio;
    numAssert(val >= -.0001 && val <= 1.00001, val * 1000000.);
    return val;
}

bool LineFunction::operator==(const BorderFunctionBase& o) throw () {
    if (!dynamic_cast<const LineFunction*>(&o))
        return false;
    if (fabs(px1 - o(py1)) > .0001 || fabs(px2 - o(py2)) > .0001)
        return false;
    return true;
}

void LineFunction::debug() const throw () {
    #ifdef CERR_DEBUG
    cerr.precision(12);
    cerr << "Line (" << px1 << ',' << py1 << ") - (" << px2 << ',' << py2 << ")\n";
    #endif
}

// swap used by pixelLeftSideIntegral (and elsewhere) ; ideally s = !s;
void swap(ChangePoints::Side& s) throw () { s = (s == ChangePoints::S_Left) ? ChangePoints::S_Right : ChangePoints::S_Left; }

double pixelLeftSideIntegral(double x0, double y0, double y1, const BorderFunctionBase& fn) throw () {
    const ChangePoints lc = fn.getChangePoints(x0 + X_EXTREMECUT), rc = fn.getChangePoints(x0 + 1. - X_EXTREMECUT); // leave outer edges off to make sure the result is within [0,1]
    ChangePoints::Side ls = lc.startSide, rs = rc.startSide;
    const double* lcpi = lc.points, * rcpi = rc.points;
    double totalPixel = 0;
    double y = y0;
    // update border markers to the status around the current y
    while (*lcpi <= y) { swap(ls); ++lcpi; }
    while (*rcpi <= y) { swap(rs); ++rcpi; }
    for (;;) {
        if (ls == ChangePoints::S_Left) {   // out on the left side (until *lcpi)
            if (*lcpi >= y1)
                return totalPixel;
            y = *lcpi++; swap(ls);
            while (*rcpi <= y) { swap(rs); ++rcpi; }
        }
        else if (rs == ChangePoints::S_Right) { // out on the right side (until *rcpi)
            if (*rcpi >= y1)
                return totalPixel + 1. * (y1 - y);
            totalPixel += 1. * (*rcpi - y);
            y = *rcpi++; swap(rs);
            while (*lcpi <= y) { swap(ls); ++lcpi; }
        }
        else if (*lcpi < *rcpi) {   // within the clipping region until *lcpi
            if (*lcpi >= y1)
                return totalPixel + fn.spanLeftSideIntegral(x0, y, y1);
            totalPixel += fn.spanLeftSideIntegral(x0, y, *lcpi);
            y = *lcpi++; swap(ls);
        }
        else {  // within the clipping region until *rcpi
            if (*rcpi >= y1)
                return totalPixel + fn.spanLeftSideIntegral(x0, y, y1);
            totalPixel += fn.spanLeftSideIntegral(x0, y, *rcpi);
            y = *rcpi++; swap(rs);
        }
        numAssert(totalPixel >= -.0001 && totalPixel <= 1.0001, totalPixel * 100000.);
    }
}

template<class Texturizer>
void renderLine(double y0, double y1, const BorderFunctionBase& fl, const BorderFunctionBase& fr, Texturizer& tex) throw () {
    double minxl = min(fl(y0), fl(y1)), minxr = min(fr(y0), fr(y1));
    double maxxl = max(fl(y0), fl(y1)), maxxr = max(fr(y0), fr(y1));
    if (fl.centerExtremes() && fl.extremeY() > y0 && fl.extremeY() < y1) {
        minxl = min(minxl, fl.extremeX());
        maxxl = max(maxxl, fl.extremeX());
    }
    if (fr.centerExtremes() && fr.extremeY() > y0 && fr.extremeY() < y1) {
        minxr = min(minxr, fr.extremeX());
        maxxr = max(maxxr, fr.extremeX());
    }
    int l0 = static_cast<int>(floor(minxl)), l1 = static_cast<int>(ceil(maxxl));
    int r0 = static_cast<int>(floor(minxr)), r1 = static_cast<int>(ceil(maxxr));
    if (l1 < r0)
        tex.putSpan(l1, r0, 1. * (y1 - y0));
    else if (r0 < l1) {
        tex.startPixSpan(r0);
        for (int x = r0; x < l1; ++x)
            tex.putPix(pixelLeftSideIntegral(x, y0, y1, fr) - pixelLeftSideIntegral(x, y0, y1, fl));
        swap(r0, l1);
    }
    if (l0 < l1) {  // optimization: don't do the quite costly startPixSpan unnecessarily; also empty spans aren't tolerated
        tex.startPixSpan(l0);
        for (int lx = l0; lx < l1; ++lx)
            tex.putPix((y1 - y0) - pixelLeftSideIntegral(lx, y0, y1, fl));
    }
    if (r0 < r1) {  // optimization: see above
        tex.startPixSpan(r0);
        for (int rx = r0; rx < r1; ++rx)
            tex.putPix(pixelLeftSideIntegral(rx, y0, y1, fr));
    }
}

template<class Texturizer>
void renderBlock(double y0, double y1, const BorderFunctionBase& fl, const BorderFunctionBase& fr, Texturizer& tex) throw () {
    tex.setLine(static_cast<int>(floor(y0)));
    if (ceil(y0) >= y1) {
        if (y1 > y0)
            renderLine(y0, y1, fl, fr, tex);
        return;
    }
    renderLine(y0, ceil(y0), fl, fr, tex);
    tex.nextLine();
    const double y1f = floor(y1);
    for (y0 = ceil(y0); y0 < y1f; ++y0) {
        renderLine(y0, y0 + 1, fl, fr, tex);
        tex.nextLine();
    }
    renderLine(y1f, y1, fl, fr, tex);
}

DrawElement::DrawElement(BorderFunctionBase* flp, BorderFunctionBase* frp, double y0_, double y1_, vector<int> tex) throw () :
    fLeft(flp),
    fRight(frp),
    y0(y0_),
    y1(y1_),
    texid(tex)
{
    nAssert(flp && frp);
}

bool DrawElement::isJoinable(const DrawElement& o) const throw () {
    return o.y0 > y0 && fabs(y1 - o.y0) < JOIN_TRESHOLD && *fLeft == *o.fLeft && *fRight == *o.fRight && texid == o.texid;
}

bool YSegment::BorderCompare::operator()(const BorderFunctionBase* o1, const BorderFunctionBase* o2) throw () {
    double c = (*o1)(my1) - (*o2)(my1);
    if (c != 0.)
        return c < 0;
    c =        (*o1)(my2) - (*o2)(my2);
    if (c != 0.)
        return c < 0;
    return     (*o1)(my3) < (*o2)(my3);
}

void YSegment::debug(bool verbose) const throw () {
    #ifdef CERR_DEBUG
    cerr.precision(12);
    cerr << y0 << " -> " << y1 << ": " << build.size() << " (" << final.size() << ")\n";
    if (!verbose)
        return;
    for (BorderListT::const_iterator bi = build.begin(); bi != build.end(); ++bi) {
        cerr << "B     ";
        (*bi)->debug();
    }
    for (TexBorderListT::const_iterator fi = final.begin(); fi != final.end(); ++fi) {
        cerr << "T"; cerr.width(2); cerr << fi->getBaseTex() << '+' << fi->getAllTextures().size() - 1 << ' ';
        fi->getFn()->debug();
    }
    #else
    (void)verbose;
    #endif
}

double getIntersection(LineFunction* f1, LineFunction* f2) throw () {
    // f1->px1 + (y - f1->py1) * f1->ratio = f2->px1 + (y - f2->py1) * f2->ratio
    if (f2->ratio == f1->ratio)
        return 1e99;    // no intersection
    return (f1->px1 - f2->px1 - f1->py1 * f1->ratio + f2->py1 * f2->ratio) / (f2->ratio - f1->ratio);
}

double getIntersection(LineFunction* f1, CurveFunction* f2, double miny) throw () {
    // | f1->(px1,py1) + t * f1->(px2-px1,py2-py1) - f2->(cx,cy) |^2 = f2->r2
    // (x - f2->cx) * f2->sideMul > 0
    // t is calculated from the first equation just like in bounceFromPoint in world.cpp (code is copied from there)
    const double dx = f2->cx  - f1->px1, dy = f2->cy  - f1->py1;
    const double mx = f1->px2 - f1->px1, my = f1->py2 - f1->py1;
    const double r2 = f2->r2;
    const double m2 = mx*mx + my*my;
    const double mdotd = mx*dx + my*dy;
    const double d2 = dx*dx + dy*dy;
    double disc = mdotd*mdotd - m2*(d2-r2);
    if (disc <= 0)
        return 1e99;    // no intersection
    disc = sqrt(disc);
    double t = (mdotd - disc) / m2; // smaller t
    double besty = 1e99;
    double y = f1->py1 + t * my;
    if (y >= miny) {
        const double xside = t * mx - dx;
        if (xside * f2->sideMul > 0)    // same sign -> is on the 'active' side of the circle
            besty = y;
    }
    t = (mdotd + disc) / m2;    // larger t
    y = f1->py1 + t * my;
    if (y >= miny && y < besty) {
        const double xside = t * mx - dx;
        if (xside * f2->sideMul > 0)    // same sign -> is on the 'active' side of the circle
            besty = y;
    }
    return besty;
}

double getIntersection(CurveFunction* f1, CurveFunction* f2, double miny) throw () {
    // | (x,y) - f1->(cx,cy) |^2 = f1->r2
    // | (x,y) - f2->(cx,cy) |^2 = f2->r2
    // it's a tricky one; the solution is from http://mathforum.org/library/drmath/view/51836.html
    const double dx = f2->cx - f1->cx;
    const double dy = f2->cy - f1->cy;
    const double sr2 = dx*dx + dy*dy;
    if (sr2 == 0)
        return 1e99;    // no intersection
    const double t = .5 * (sr2 + f1->r2 - f2->r2);
    const double xb = f1->cx + dx * t / sr2;
    const double yb = f1->cy + dy * t / sr2;
    const double srt = f1->r2 - t*t/sr2;
    if (srt <= 0)
        return 1e99;    // no intersection
    const double sr = sqrt(sr2);
    const double mul = sqrt(srt) / sr;
    // now the points are (xb +- dy*mul , yb -+ dx*mul)
    double y1 = yb - dx*mul;
    double besty = 1e99;
    if (y1 >= miny) {
        const double x1 = xb + dy*mul;
        const double dx1 = x1 - f1->cx, dx2 = x1 - f2->cx;
        #ifndef NDEBUG
        const double dy1 = y1 - f1->cy, dy2 = y1 - f2->cy;
        nAssert(fabs(dx1*dx1 + dy1*dy1 - f1->r2) < .00001);
        nAssert(fabs(dx2*dx2 + dy2*dy2 - f2->r2) < .00001);
        #endif
        if (dx1 * f1->sideMul > 0 && dx2 * f2->sideMul > 0)
            besty = y1;
    }
    y1 = yb + dx*mul;
    if (y1 >= miny && y1 < besty) {
        const double x1 = xb - dy*mul;
        const double dx1 = x1 - f1->cx, dx2 = x1 - f2->cx;
        #ifndef NDEBUG
        const double dy1 = y1 - f1->cy, dy2 = y1 - f2->cy;
        nAssert(fabs(dx1*dx1 + dy1*dy1 - f1->r2) < .00001);
        nAssert(fabs(dx2*dx2 + dy2*dy2 - f2->r2) < .00001);
        #endif
        if (dx1 * f1->sideMul > 0 && dx2 * f2->sideMul > 0)
            besty = y1;
    }
    return besty;
}

// getFirstIntersection gets the first y coordinate within the segment, with an intersection between bfn and a border in the final list
// extreme values of y (that might [would the math be exact] actually be at the extreme coordinate or even outside the segment) are ignored
bool YSegment::getFirstIntersection(BorderFunctionBase* bfn, double* splity) throw () {
    *splity = 1e99;
    const double miny = y0 + INTERSECTION_TRESHOLD;
    LineFunction* lfp1 = dynamic_cast<LineFunction*>(bfn);
    if (lfp1) {
        for (TexBorderListT::const_iterator bi = final.begin(); bi != final.end(); ++bi) {
            LineFunction* lfp2 = dynamic_cast<LineFunction*>(bi->getFn());
            if (lfp2) {
                const double y = getIntersection(lfp1, lfp2);
                if (y > miny && y < *splity)
                    *splity = y;
                continue;
            }
            CurveFunction* cfp2 = dynamic_cast<CurveFunction*>(bi->getFn());
            nAssert(cfp2);
            const double y = getIntersection(lfp1, cfp2, miny);
            if (y < *splity)
                *splity = y;
            continue;
        }
    }
    else {
        CurveFunction* cfp1 = dynamic_cast<CurveFunction*>(bfn);
        nAssert(cfp1);
        for (TexBorderListT::const_iterator bi = final.begin(); bi != final.end(); ++bi) {
            LineFunction* lfp2 = dynamic_cast<LineFunction*>(bi->getFn());
            if (lfp2) {
                const double y = getIntersection(lfp2, cfp1, miny);
                if (y > miny && y < *splity)
                    *splity = y;
                continue;
            }
            CurveFunction* cfp2 = dynamic_cast<CurveFunction*>(bi->getFn());
            nAssert(cfp2);
            const double y = getIntersection(cfp1, cfp2, miny);
            if (y < *splity)
                *splity = y;
            continue;
        }
    }
    return *splity < y1 - INTERSECTION_TRESHOLD;
}

YSegment YSegment::split(double midy) throw () {
    nAssert(midy > y0 && midy < y1);
    YSegment ret(midy, y1);
    ret.build = build;
    ret.final = final;
    y1 = midy;
    return ret;
}

void YSegment::sort() throw () { // sorts the build list borders in increasing x-order
    nAssert((build.size() & 1) == 0);
    std::sort(build.begin(), build.end(), BorderCompare(y0, y1));
}

void YSegment::simplify() throw () { // removes double borders from build list (assuming it's sorted)
    nAssert((build.size() & 1) == 0);
    if (build.empty())
        return;
    BorderListT::iterator bi = build.begin();
    BorderListT::iterator bin = bi; ++bin;
    while (bin != build.end()) {
        if (*bi == *bin) {
            bi = build.erase(bi, ++bin);
            if (bi == build.end())
                break;
            bin = bi;
            ++bin;
        }
        else {
            ++bi;
            ++bin;
        }
    }
}

void YSegment::moveElements(int texid) throw () {    // moves all borders from build list to final list (use only when the final list is empty)
    nAssert(final.empty());
    for (BorderListT::iterator bi = build.begin(); bi != build.end(); ) {
        final.push_back(TexBorder(*bi, texid));
        bi = build.erase(bi);
        nAssert(bi != build.end());
        final.push_back(TexBorder(*bi, -1));
        bi = build.erase(bi);
    }
    build.clear();
}

/* how moveElementsWithOverlap works:
 *
 * the build list is assumed to be sorted, so that each pair of consequent borders forms a wall segment
 * (the final list already has tex information; nevertheless it must be sorted too; this function maintains the sort)
 * it is assumed that no walls intersect the walls to be added
 *
 * taking one wall element (pair of borders) at a time from the build list, the final list is modified so the new wall overlaps old ones:
 * - search on while an old border is to the left left from the new left border
 * - add the new left border
 * - remove old borders while they are left from the new right border
 */
void YSegment::moveElementsWithOverlap(int texid, bool overlay) throw () {   // moves all borders from build list to final list overlapping the old walls (or overlay)
    nAssert((build.size() & 1) == 0);
    BorderCompare bcmp(y0, y1);
    for (BorderListT::iterator sbi = build.begin(); sbi != build.end(); ) {
        #ifdef DEBUG_OVERLAP
        debug(true);
        cerr << '\n';
        #endif
        TexBorderListT::iterator dbi = final.begin();
        for (; dbi != final.end(); ++dbi)   //#opt: skipped entries are skipped on the next round too
            if (!bcmp(dbi->getFn(), *sbi))
                break;
        // dbi points to the first border that will be overwritten
        if (overlay) {
            vector<int> prevTex;
            if (dbi == final.begin())
                prevTex.push_back(-1);
            else {
                TexBorderListT::iterator si = dbi;
                prevTex = (--si)->getAllTextures();
            }

            dbi = final.insert(dbi, TexBorder(*sbi, prevTex));
            dbi->addTex(texid);
            ++dbi;  // point dbi to the same item as before

            sbi = build.erase(sbi); // the ending border
            nAssert(sbi != build.end());

            while (dbi != final.end()) {
                if (bcmp(*sbi, dbi->getFn()))
                    break;
                prevTex = dbi->getAllTextures();
                dbi->addTex(texid);
                ++dbi;
            }
            // dbi points to the first border that will not be overwritten
            final.insert(dbi, TexBorder(*sbi, prevTex));
        }
        else {
            vector<int> prevTex;
            if (dbi == final.begin())
                prevTex.push_back(-1);
            else {
                TexBorderListT::iterator si = dbi;
                prevTex = (--si)->getAllTextures();
            }

            if (texid != prevTex.front() || prevTex.size() > 1) {
                dbi = final.insert(dbi, TexBorder(*sbi, texid));
                ++dbi;  // point dbi to the same item as before
            }

            sbi = build.erase(sbi); // the ending border
            nAssert(sbi != build.end());

            while (dbi != final.end()) {
                if (bcmp(*sbi, dbi->getFn()))
                    break;
                prevTex = dbi->getAllTextures();
                dbi = final.erase(dbi);
            }
            // dbi points to the first border that will not be overwritten
            if (texid != prevTex.front() || prevTex.size() > 1) // note: new prevTex
                final.insert(dbi, TexBorder(*sbi, prevTex));
        }
        sbi = build.erase(sbi); // the next beginning border (if any)
    }
    #ifdef DEBUG_OVERLAP
    debug(true);
    cerr << "\n\n";
    #endif
    nAssert(final.empty() || final.back().getBaseTex() == -1);
}

void YSegment::extractDrawElements(list<DrawElement>& dst) const throw () {
    nAssert(build.empty());
    if (final.empty())
        return;
    for (TexBorderListT::const_iterator bi = final.begin();; ++bi) {
        nAssert(bi != final.end());
        TexBorderListT::const_iterator bi1 = bi;
        ++bi1;
        if (bi1 == final.end()) {
            nAssert(bi->getBaseTex() == -1);
            break;
        }
        if (bi->getBaseTex() != -1)
            dst.push_back(DrawElement(bi->getFn(), bi1->getFn(), y0 + FINAL_EXTREMECUT, y1 - FINAL_EXTREMECUT, bi->getAllTextures()));
    }
}

// splitOnIntersect splits *si (which must be part of list "segs") to two parts if bfn intersects some of the borders in si's final array
// the split is done in the first intersection y-wise, and to handle multiple intersections, splitOnIntersect must also be called on the new segment
// returns true if si was split
bool splitOnIntersect(SegListT::iterator si, BorderFunctionBase* bfn, SegListT& segs) throw () {
    double splity;
    if (!si->getFirstIntersection(bfn, &splity))    // relies on getFirstIntersection not to return an intersection at the segment's extremes
        return false;
    nAssert(splity > si->getY0() && splity < si->getY1());
    SegListT::iterator insPos = si;
    segs.insert(++insPos, si->split(splity));
    return true;
}

void assembleSegments(const vector<WallBorderSegment>& borders, SegListT& segDest) throw () {
    nAssert(!segDest.empty());  // it must be pre-filled with (possibly empty) segments that cover all possible y's (one seg from -1e99 to 1e99 is good)
//  nAssert(borders.size() >= 2);   // using clipping with objects wholly outside the clip area (or otherwise invisible) will violate this; disable this line if that's possible

    #ifdef DEBUG_SPLIT
    cerr << '\n';
    #endif
    for (vector<WallBorderSegment>::const_iterator bi = borders.begin(); bi != borders.end(); ++bi) {
        #ifdef DEBUG_SPLIT
        cerr.precision(25);
        cerr << bi->y0 << " -> " << bi->y1 << '\n';
        #endif
        if (bi->y0 == bi->y1)
            continue;
        nAssert(bi->y0 < bi->y1);

        SegListT::iterator si;
        for (si = segDest.begin(); nAssert(si != segDest.end()), si->getY1() <= bi->y0; ++si);

        // si points to first segment whose y1 > bi->y0
        if (si->getY1() < bi->y0 + SPLIT_TRESHOLD) {    // in this case, this segment is ignored (too little of bi is in this segment)
            if (bi->y0 - si->getY0() < SPLIT_TRESHOLD)  // bi->y0 is the new si->y1; this test also matches when bi->y0 < si->getY0()
                si = segDest.erase(si);
            else {
                si->setY1(bi->y0);
                ++si;
            }
        }
        else if (si->getY0() < bi->y0 - SPLIT_TRESHOLD) {   // in this case, the segment must be split (too much of the segment is outside bi)
            SegListT::iterator insPos = si;
            ++insPos;
            si = segDest.insert(insPos, si->split(bi->y0));
        }
        else if (si->getY0() < bi->y0) {    // in this case, the segment fits bi nicely and is only trimmed
            si->setY0(bi->y0);
            if (si->width() < SPLIT_TRESHOLD)
                si = segDest.erase(si);
        }
        nAssert(si != segDest.end());
        nAssert(fabs(bi->y0 - si->getY0()) <= 5. * SPLIT_TRESHOLD);

        for (; si->getY1() <= bi->y1; ) {
            nAssert(si != segDest.end());
            if (splitOnIntersect(si, bi->fn, segDest) && si->width() < SPLIT_TRESHOLD)  // the next round will handle the newly created segment if any
                si = segDest.erase(si);
            else {
                si->add(bi->fn);
                ++si;
            }
        }

        // si points to first segment whose y1 > bi->y1
        if (si->getY0() > bi->y1 - SPLIT_TRESHOLD) {    // in this case, the segment is ignored (too little of bi is in this segment)
            if (si->getY0() < bi->y1) {
                si->setY0(bi->y1);
                if (si->width() < SPLIT_TRESHOLD)
                    segDest.erase(si);
            }
            continue;   // nothing more to do - this border fully inserted
        }
        if (si->getY1() > bi->y1 + SPLIT_TRESHOLD) {    // in this case, the segment must be split (too much of the segment is outside bi)
            SegListT::iterator insPos = si;
            ++insPos;
            segDest.insert(insPos, si->split(bi->y1));  // the new, inserted part is not modified from here on, it's outside bi; from previous ifs, we know that both parts are larger than SPLIT_TRESHOLD, so no deletions needed
        }
        else    // in this case, the segment fits bi nicely and is only trimmed
            si->setY1(bi->y1);  // from first if, we know that si still is larger than SPLIT_TRESHOLD, so no deletions needed
        // now, the border only needs to be inserted to all of si and we're done
        si->add(bi->fn);
        for (;;) {
            if (!splitOnIntersect(si, bi->fn, segDest))
                break;
            if (si->width() < SPLIT_TRESHOLD)
                si = segDest.erase(si);
            else
                ++si;
            nAssert(si != segDest.end());
        }
        if (si->width() < SPLIT_TRESHOLD)
            segDest.erase(si);
    }
    #ifdef DEBUG_SPLIT
    cerr << "- - - split into: - - -\n";
    for (SegListT::iterator dsi = segDest.begin(); dsi != segDest.end(); ++dsi)
        dsi->debug();
    #endif
}

void joinElements(list<DrawElement>& els) throw () {
    for (list<DrawElement>::iterator i1 = els.begin(); i1 != els.end(); ++i1) {
        list<DrawElement>::iterator i2 = i1;
        ++i2;
        for (; i2 != els.end(); ) {    // i2.y0 >= i1.y0 because of the ordering
            if (i2->getY0() > i1->getY1() + JOIN_TRESHOLD)  // all elements from i2 on have a greater y0, so no point in continuing
                break;
            if (i1->isJoinable(*i2)) {
                i1->extendDown(i2->getY1());
                i2 = els.erase(i2);
            }
            else
                ++i2;
        }
    }
}

list<DrawElement> assembleWall(const vector<WallBorderSegment>& borders, int texid) throw () {
    SegListT segs;
    segs.push_back(YSegment(-1e99, 1e99));  // this makes the splitting routine simpler, since the new borders will always be within an existing segment

    // split borders into segs
    assembleSegments(borders, segs);

    // finalize segments and extract a DrawElement list
    list<DrawElement> ret;
    for (SegListT::iterator si = segs.begin(); si != segs.end(); ++si) {
        si->sort();
        si->simplify();
        si->moveElements(texid);
        si->extractDrawElements(ret);
    }

    // finalize the DrawElement list by joining y-neighboring elements with same border functions
    joinElements(ret);
    return ret;
}

list<DrawElement> assembleScene(const vector<ObjectSource>& objects) throw () {
    SegListT segs;
    segs.push_back(YSegment(-1e99, 1e99));  // this makes the splitting routine simpler, since the new borders will always be within an existing segment

    // finalize segments
    for (vector<ObjectSource>::const_iterator oi = objects.begin(); oi != objects.end(); ++oi) {
        // split borders into segs
        assembleSegments(oi->borders, segs);
        // overlap the old segments
        for (SegListT::iterator si = segs.begin(); si != segs.end(); ++si) {
            si->sort();
            si->simplify();
            si->moveElementsWithOverlap(oi->texid, oi->overlay);
        }
    }
    // extract a DrawElement list
    list<DrawElement> ret;
    for (SegListT::iterator si = segs.begin(); si != segs.end(); ++si)
        si->extractDrawElements(ret);

    // finalize the DrawElement list by joining y-neighboring elements with same border functions
    joinElements(ret);
    return ret;
}

void PartialPixelSegment::draw(BITMAP* buf, int y) const throw () {
    for (size_t i = 0; i < pixels.size(); ++i)
        if (pixels[i].draw())
            putpixel(buf, startx + i, y, pixels[i].flexColor());
}

void Texturizer::render(const vector<int>& textures, const DrawElement* elp) throw () {
    if (textures.size() == 1) {
        const int texid = textures[0];
        numAssert2(texid >= 0 && texid < (int)texTab.size(), texid, texTab.size());
        const TextureData::TexdataUnion& data = texTab[texid].data();
        switch (texTab[texid].type()) {
        /*break;*/ case TextureData::T_solid: {
                SolidTexturizer tex(*this, data.s);
                renderBlock(elp->getY0(), elp->getY1(), elp->getLeft(), elp->getRight(), tex);
            }
            break; case TextureData::T_texture: {
                TextureTexturizer tex(*this, data.t);
                renderBlock(elp->getY0(), elp->getY1(), elp->getLeft(), elp->getRight(), tex);
            }
            break; case TextureData::T_flagmarker: default: nAssert(0);
        }
    }
    else {
        nAssert(textures.size() > 1);
        MultiLayerTexturizer tex(*this, textures.size());
        for (vector<int>::const_iterator ti = textures.begin(); ti != textures.end(); ++ti) {
            const int texid = *ti;
            numAssert2(texid >= 0 && texid < (int)texTab.size(), texid, texTab.size());
            const TextureData::TexdataUnion& data = texTab[texid].data();
            switch (texTab[texid].type()) {
            /*break;*/ case TextureData::T_solid:      tex.addLayer(new SolidPixelSource(data.s));
                break; case TextureData::T_texture:    tex.addLayer(new TexturePixelSource(data.t));
                break; case TextureData::T_flagmarker: tex.addLayer(new FlagmarkerPixelSource(data.f));
                break; default: nAssert(0);
            }
        }
        renderBlock(elp->getY0(), elp->getY1(), elp->getLeft(), elp->getRight(), tex);
    }
}

inline void Texturizer::setLine(int y) throw () {
    nAssert(y >= 0 && y < buf->h);  // can't rely on Allegro's clipping since PartialPixelSegment-containers are only allocated for on-screen rows
    by = by0 + y;
}

inline void Texturizer::nextLine() throw () {
    ++by;
    nAssert(by < buf->h);   // can't rely on Allegro's clipping since PartialPixelSegment-containers are only allocated for on-screen rows
}

void Texturizer::startPixSpan(int x) throw () {
    bx = bx0 + x;
    list<PartialPixelSegment>& row = partials[by];
    for (list<PartialPixelSegment>::iterator si = row.begin();; ++si) {
        if (si == row.end()) {
            partSpan = &(*row.insert(row.end(), PartialPixelSegment(x)));
            spanEnd = INT_MAX;
            spanIndex = 0;
            break;
        }
        spanIndex = x - si->x0();
        if (spanIndex < 0) {
            const int nextStart = si->x0();
            partSpan = &(*row.insert(si, PartialPixelSegment(x)));  // keep them sorted
            spanEnd = nextStart - partSpan->x0();
            spanIndex = 0;
            break;
        }
        if (spanIndex > si->len())
            continue;
        if (spanIndex == si->len()) {
            list<PartialPixelSegment>::iterator tsi = si;
            ++tsi;
            if (tsi != row.end() && tsi->x0() == x) // this means this pixel belongs to the next span; otherwise extend this one
                continue;
        }
        partSpan = &(*si);
        ++si;
        if (si == row.end())
            spanEnd = INT_MAX;
        else
            spanEnd = si->x0() - partSpan->x0();
        // spanIndex already set
        break;
    }
}

inline void Texturizer::putPix(int color, int alpha) throw () {
    if (spanIndex == partSpan->len()) {
        if (spanIndex < spanEnd)
            partSpan->extend(color, alpha);
        else {
            nAssert(spanIndex == spanEnd);
            startPixSpan(bx - bx0);
            nAssert(spanIndex == 0);
            nAssert(partSpan->len() > 0);
            partSpan->add(0, color, alpha); // this opt. is the main reason empty spans aren't tolerated
        }
    }
    else {
        nAssert(spanIndex >= 0 && spanIndex < partSpan->len());
        partSpan->add(spanIndex, color, alpha);
    }
    ++spanIndex;
    ++bx;
}

void Texturizer::finalize() throw () {
    for (int y = 0; y < buf->h; ++y) {
        list<PartialPixelSegment>& row = partials[y];
        for (list<PartialPixelSegment>::const_iterator si = row.begin(); si != row.end(); ++si)
            si->draw(buf, y);
        row.clear();
    }
}

pair<int, int> SolidPixelSource::getPixel() throw () {
    return pair<int, int>(color, alpha);
}

void SolidTexturizer::putPix(double alpha) throw () {
    putPixI(static_cast<int>(ldexp(alpha, PartialPixelSegment::scale)));
}

void SolidTexturizer::putSpan(int x0, int x1, double alpha) throw () { // fills the range [x0,x1[
    nAssert(x0 < x1);   // empty spans aren't tolerated
    if (alpha >= .999)
        hline(host.getBuf(), x0 + host.getbx0(), host.getby(), x1 + host.getbx0() - 1, color);
    else {
        startPixSpan(x0);
        const int iAlpha = static_cast<int>(ldexp(alpha, PartialPixelSegment::scale));
        for (int x = x0; x < x1; ++x)
            putPixI(iAlpha);
    }
}

void TexturePixelSource::setLine(int y) throw () {
    ty = (y - ty0) % tex->h;
}

void TexturePixelSource::nextLine() throw () {
    if (++ty == tex->h)
        ty = 0;
}

void TexturePixelSource::startPixSpan(int x) throw () {
    tx = (x - tx0) % tex->w;
}

pair<int, int> TexturePixelSource::getPixel() throw () {
    const int col = getpixel(tex, tx, ty);
    if (++tx == tex->w)
        tx = 0;
    return pair<int, int>(col, alpha);
}

void TextureTexturizer::setLine(int y) throw () {
    host.setLine(y);
    ty = (y - ty0) % tex->h;
}

void TextureTexturizer::nextLine() throw () {
    host.nextLine();
    if (++ty == tex->h)
        ty = 0;
}

void TextureTexturizer::putSpan(int x0, int x1, double alpha) throw () {   // fills the range [x0,x1[
    nAssert(x0 < x1);   // empty spans aren't tolerated
    if (alpha >= .999) {
        drawing_mode(DRAW_MODE_COPY_PATTERN, tex, tx0, ty0);
        hline(host.getBuf(), x0 + host.getbx0(), host.getby(), x1 + host.getbx0() - 1, 0);
        solid_mode();
    }
    else {
        startPixSpan(x0);
        const int iAlpha = static_cast<int>(ldexp(alpha, PartialPixelSegment::scale));
        for (int x = x0; x < x1; ++x)
            putPixI(iAlpha);
    }
}

void TextureTexturizer::startPixSpan(int x) throw () {
    host.startPixSpan(x);
    tx = (x - tx0) % tex->w;
}

void TextureTexturizer::putPix(double alpha) throw () {
    putPixI(static_cast<int>(ldexp(alpha, PartialPixelSegment::scale)));
}

void TextureTexturizer::putPixI(int alpha) throw () {
    host.putPix(getpixel(tex, tx, ty), alpha);
    if (++tx == tex->w)
        tx = 0;
}

FlagmarkerPixelSource::FlagmarkerPixelSource(const FlagmarkerTexdata& td) throw () :
    color(td.color),
    markRadius(td.radius),
    intensityMul(300 / td.radius),
    cx(td.cx),
    cy(td.cy)
{ }

void FlagmarkerPixelSource::setLine(int y) throw () {
    dy = y - cy;
    dy2 = dy * dy;
}

void FlagmarkerPixelSource::nextLine() throw () {
    ++dy;
    dy2 = dy * dy;
}

void FlagmarkerPixelSource::startPixSpan(int x) throw () {
    dx = x - cx;
}

pair<int, int> FlagmarkerPixelSource::getPixel() throw () {
    const double intensity = markRadius - sqrt(dx * dx + dy2);
    ++dx;
    if (intensity <= 0)
        return pair<int, int>(0, 0);
    else
        return pair<int, int>(color, min(256, static_cast<int>(intensity * intensityMul)));
}

MultiLayerTexturizer::~MultiLayerTexturizer() throw () {
    for (vector<PixelSource*>::iterator li = layers.begin(); li != layers.end(); ++li)
        delete *li;
}

void MultiLayerTexturizer::setLine(int y) throw () {
    host.setLine(y);
    for (vector<PixelSource*>::iterator li = layers.begin(); li != layers.end(); ++li)
        (*li)->setLine(y);
}

void MultiLayerTexturizer::nextLine() throw () {
    host.nextLine();
    for (vector<PixelSource*>::iterator li = layers.begin(); li != layers.end(); ++li)
        (*li)->nextLine();
}

void MultiLayerTexturizer::putSpan(int x0, int x1, double alpha) throw () {  // fills the range [x0,x1[
    startPixSpan(x0);
    for (int x = x0; x < x1; ++x)
        putPix(alpha);
}

void MultiLayerTexturizer::startPixSpan(int x) throw () {
    host.startPixSpan(x);
    for (vector<PixelSource*>::iterator li = layers.begin(); li != layers.end(); ++li)
        (*li)->startPixSpan(x);
}

void MultiLayerTexturizer::putPix(double alpha) throw () {   // draws at current x coord and increases it
    vector<PixelSource*>::iterator li = layers.begin();
    const int color1 = (*li)->getPixel().first;
    int r = getr(color1), g = getg(color1), b = getb(color1);
    for (++li; li != layers.end(); ++li) {
        pair<int, int> layerPix = (*li)->getPixel();
        const int newColor = layerPix.first;
        const int newAlpha = layerPix.second;
        const int oldAlpha = 256 - newAlpha;
        r = (r * oldAlpha + getr(newColor) * newAlpha + 127) / 256;
        g = (g * oldAlpha + getg(newColor) * newAlpha + 127) / 256;
        b = (b * oldAlpha + getb(newColor) * newAlpha + 127) / 256;
    }
    host.putPix(makecol(r, g, b), static_cast<int>(ldexp(alpha, PartialPixelSegment::scale)));
}

SceneAntialiaser::~SceneAntialiaser() throw () {
    for (vector<BorderFunctionBase*>::iterator bi = bfns.begin(); bi != bfns.end(); ++bi)
        delete *bi;
}

void SceneAntialiaser::setScaling(double x0_, double y0_, double scale_) throw () {  // call before add*
    x0 = x0_;
    y0 = y0_;
    scale = scale_;
}

void SceneAntialiaser::addRectangle(double x1, double y1, double x2, double y2, int texture, bool overlay) throw () {
    numAssert2(y1 <= y2, y1 * 10., y2 * 10.);

    objects.push_back(ObjectSource());
    objects.back().texid = texture;
    objects.back().overlay = overlay;
    vector<WallBorderSegment>& borders = objects.back().borders;

    x1 = x0 + x1 * scale;
    y1 = y0 + y1 * scale;
    x2 = x0 + x2 * scale;
    y2 = y0 + y2 * scale;

    bfns.push_back(new LineFunction(x1, y1, x1, y2));
    borders.push_back(WallBorderSegment(bfns.back(), y1, y2));

    bfns.push_back(new LineFunction(x2, y1, x2, y2));
    borders.push_back(WallBorderSegment(bfns.back(), y1, y2));
}

void SceneAntialiaser::addRectWall(const RectWall& wall, int texture) throw () {
    numAssert2(wall.y1() <= wall.y2(), wall.y1() * 10., wall.y2() * 10.);
    addRectangle(wall.x1(), wall.y1(), wall.x2(), wall.y2(), texture);
}

void SceneAntialiaser::addTriWall (const  TriWall& wall, int texture) throw () {
    objects.push_back(ObjectSource());
    objects.back().texid = texture;
    objects.back().overlay = false;
    vector<WallBorderSegment>& borders = objects.back().borders;

    const double x1 = x0 + wall.x1() * scale;
    const double y1 = y0 + wall.y1() * scale;
    const double x2 = x0 + wall.x2() * scale;
    const double y2 = y0 + wall.y2() * scale;
    const double x3 = x0 + wall.x3() * scale;
    const double y3 = y0 + wall.y3() * scale;

    bfns.push_back(new LineFunction(x1, y1, x2, y2));
    borders.push_back(WallBorderSegment(bfns.back(), y1, y2));

    bfns.push_back(new LineFunction(x1, y1, x3, y3));
    borders.push_back(WallBorderSegment(bfns.back(), y1, y3));

    bfns.push_back(new LineFunction(x2, y2, x3, y3));
    borders.push_back(WallBorderSegment(bfns.back(), y2, y3));
}

void SceneAntialiaser::addCircWall(const CircWall& wall, int texture) throw () {
    objects.push_back(ObjectSource());
    objects.back().texid = texture;
    objects.back().overlay = false;
    vector<WallBorderSegment>& borders = objects.back().borders;

    const double cx = x0 + wall.X() * scale, cy = y0 + wall.Y() * scale;
    const double ro = wall.radius() * scale;
    const double ri = wall.radius_in() * scale;

    if (wall.angles()[0] == wall.angles()[1]) { // a whole circle/ring
        bfns.push_back(new CurveFunction(cx, cy, ro, false));
        borders.push_back(WallBorderSegment(bfns.back(), cy - ro, cy + ro));
        bfns.push_back(new CurveFunction(cx, cy, ro, true));
        borders.push_back(WallBorderSegment(bfns.back(), cy - ro, cy + ro));
        if (ri > 0) {   // a ring
            bfns.push_back(new CurveFunction(cx, cy, ri, false));
            borders.push_back(WallBorderSegment(bfns.back(), cy - ri, cy + ri));
            bfns.push_back(new CurveFunction(cx, cy, ri, true));
            borders.push_back(WallBorderSegment(bfns.back(), cy - ri, cy + ri));
        }
        return;
    }

    const Coords& va1 = wall.angle_vector_1();
    const Coords& va2 = wall.angle_vector_2();

    double ar[2];
    for (int i = 0; i < 2; ++i)
        ar[i] = wall.angles()[i] * N_PI / 180.;
    if (ar[1] < ar[0])
        ar[1] += 2. * N_PI;
    nAssert(ar[1] >= ar[0]);
    nAssert(ar[0] >= 0.);

    const double yeo = cy - ro * va2.second;    // - belongs to va2.second
    const double yei = cy - ri * va2.second;    // - belongs to va2.second
    double ang = ar[0];
    const int pi_i = static_cast<int>(ang / N_PI) + 1;
    bool rightSide = (pi_i & 1) != 0;
    double npi = N_PI * pi_i;

    for (;;) {
        const double yao = cy - ro * cos(ang);  // - belongs to cos
        const double yai = cy - ri * cos(ang);  // - belongs to cos
        if (npi < ar[1]) {  // draw from ang to npi
            bfns.push_back(new CurveFunction(cx, cy, ro, rightSide));
            if (rightSide)
                borders.push_back(WallBorderSegment(bfns.back(), yao, cy + ro));
            else
                borders.push_back(WallBorderSegment(bfns.back(), cy - ro, yao));
            if (ri > 0) {
                bfns.push_back(new CurveFunction(cx, cy, ri, rightSide));
                if (rightSide)
                    borders.push_back(WallBorderSegment(bfns.back(), yai, cy + ri));
                else
                    borders.push_back(WallBorderSegment(bfns.back(), cy - ri, yai));
            }
        }
        else {
            bfns.push_back(new CurveFunction(cx, cy, ro, rightSide));
            if (rightSide)
                borders.push_back(WallBorderSegment(bfns.back(), yao, yeo));
            else
                borders.push_back(WallBorderSegment(bfns.back(), yeo, yao));
            if (ri > 0) {
                bfns.push_back(new CurveFunction(cx, cy, ri, rightSide));
                if (rightSide)
                    borders.push_back(WallBorderSegment(bfns.back(), yai, yei));
                else
                    borders.push_back(WallBorderSegment(bfns.back(), yei, yai));
            }
            break;
        }
        ang = npi;
        npi += N_PI;
        rightSide = !rightSide;
    }

    double x1 = cx + va1.first * ri, y1 = cy - va1.second * ri; // - belongs to va1.second
    double x2 = cx + va1.first * ro, y2 = cy - va1.second * ro; // - belongs to va1.second
    if (va1.second > 0) {   // this is reversed, too
        swap(x1, x2);
        swap(y1, y2);
    }
    bfns.push_back(new LineFunction(x1, y1, x2, y2));
    borders.push_back(WallBorderSegment(bfns.back(), y1, y2));

    x1 = cx + va2.first * ri; y1 = cy - va2.second * ri;    // - belongs to va2.second
    x2 = cx + va2.first * ro; y2 = cy - va2.second * ro;    // - belongs to va2.second
    if (va2.second > 0) {   // this is reversed, too
        swap(x1, x2);
        swap(y1, y2);
    }
    bfns.push_back(new LineFunction(x1, y1, x2, y2));
    borders.push_back(WallBorderSegment(bfns.back(), y1, y2));
}

void SceneAntialiaser::addWall(const WallBase* wall, int texture) throw () {
    const RectWall* rwp = dynamic_cast<const RectWall*>(wall);
    if (rwp) {
        addRectWall(*rwp, texture);
        return;
    }
    const TriWall*  twp = dynamic_cast<const TriWall *>(wall);
    if (twp) {
        addTriWall (*twp, texture);
        return;
    }
    const CircWall* cwp = dynamic_cast<const CircWall*>(wall);
    nAssert(cwp);
    addCircWall    (*cwp, texture);
}

void SceneAntialiaser::setClipping(double x1, double y1, double x2, double y2) throw () {
    nAssert(x1 < x2 && y1 < y2);
    clipx1 = x0 + x1 * scale;
    clipy1 = y0 + y1 * scale;
    clipx2 = x0 + x2 * scale;
    clipy2 = y0 + y2 * scale;
    clipFunctionsValid = false;
}

void SceneAntialiaser::createClipFns() throw () {
    if (!clipFunctionsValid) {
        clipLeft = new LineFunction(clipx1, clipy1, clipx1, clipy2);
        clipRight = new LineFunction(clipx2, clipy1, clipx2, clipy2);
        bfns.push_back(clipLeft);
        bfns.push_back(clipRight);
        clipFunctionsValid = true;
    }
}

void SceneAntialiaser::clip(int i0) throw () {
    for (vector<ObjectSource>::iterator object = objects.begin() + i0; object != objects.end(); ++object) {
        int handleBorders = object->borders.size(); // must save this since new borders may be added that don't need clipping
        for (int bi = 0; bi < handleBorders; ++bi) {
            WallBorderSegment* border = &object->borders[bi];

            // clip y direction
            if (border->y0 < clipy1)
                border->y0 = clipy1;
            if (border->y1 > clipy2)
                border->y1 = clipy2;
            if (border->y1 <= border->y0) { // nothing is visible after clipping
                object->borders.erase(object->borders.begin() + bi);
                --handleBorders;
                --bi;
                continue;
            }

            // clip x direction
            // note: the code from here on is highly of the same structure as pixelLeftSideIntegral
            const ChangePoints lc = border->fn->getChangePoints(clipx1), rc = border->fn->getChangePoints(clipx2);
            ChangePoints::Side ls = lc.startSide, rs = rc.startSide;
            const double* lcpi = lc.points, * rcpi = rc.points;
            double y = border->y0;
            // update border markers to the status around the current y
            while (*lcpi <= y) { swap(ls); ++lcpi; }
            while (*rcpi <= y) { swap(rs); ++rcpi; }
            for (;;) {
                if (ls == ChangePoints::S_Left) {   // out on the left side (until *lcpi)
                    createClipFns();
                    if (*lcpi >= border->y1) {
                        border->fn = clipLeft;
                        break;
                    }
                    const WallBorderSegment newSeg(border->fn, *lcpi, border->y1);
                    border->fn = clipLeft;
                    border->y1 = *lcpi;
                    object->borders.push_back(newSeg);
                    border = &object->borders.back();   // continue splitting with this new segment
                    y = *lcpi++; swap(ls);
                    while (*rcpi <= y) { swap(rs); ++rcpi; }
                }
                else if (rs == ChangePoints::S_Right) { // out on the right side (until *rcpi)
                    createClipFns();
                    if (*rcpi >= border->y1) {
                        border->fn = clipRight;
                        break;
                    }
                    const WallBorderSegment newSeg(border->fn, *rcpi, border->y1);
                    border->fn = clipRight;
                    border->y1 = *rcpi;
                    object->borders.push_back(newSeg);
                    border = &object->borders.back();   // continue splitting with this new segment
                    y = *rcpi++; swap(rs);
                    while (*lcpi <= y) { swap(ls); ++lcpi; }
                }
                else if (*lcpi < *rcpi) {   // within the clipping region until *lcpi
                    if (*lcpi >= border->y1)
                        break;
                    const WallBorderSegment newSeg(border->fn, *lcpi, border->y1);
                    border->y1 = *lcpi;
                    object->borders.push_back(newSeg);
                    border = &object->borders.back();   // continue splitting with this new segment
                    y = *lcpi++; swap(ls);
                }
                else {  // within the clipping region until *rcpi
                    if (*rcpi >= border->y1)
                        break;
                    const WallBorderSegment newSeg(border->fn, *rcpi, border->y1);
                    border->y1 = *rcpi;
                    object->borders.push_back(newSeg);
                    border = &object->borders.back();   // continue splitting with this new segment
                    y = *rcpi++; swap(rs);
                }
            }
        }
    }
}

void SceneAntialiaser::addRectWallClipped(const RectWall& wall, int texture) throw () {
    const int startNew = objects.size();
    addRectWall(wall, texture);
    clip(startNew);
}

void SceneAntialiaser::addTriWallClipped (const  TriWall& wall, int texture) throw () {
    const int startNew = objects.size();
    addTriWall (wall, texture);
    clip(startNew);
}

void SceneAntialiaser::addCircWallClipped(const CircWall& wall, int texture) throw () {
    const int startNew = objects.size();
    addCircWall(wall, texture);
    clip(startNew);
}

void SceneAntialiaser::addWallClipped    (const WallBase* wall, int texture) throw () {
    const int startNew = objects.size();
    addWall(wall, texture);
    clip(startNew);
}

void SceneAntialiaser::render(Texturizer& tex) const throw () {
    const list<DrawElement> drawEls = assembleScene(objects);
    #ifdef DEBUG_RENDER
    if (drawEls.size() < 50) {
        cerr << "rendering " << drawEls.size() << " elements:\n\n";
        int elInd = 1;
        for (list<DrawElement>::const_iterator ei = drawEls.begin(); ei != drawEls.end(); ++ei, ++elInd) {
            const DrawElement& el = *ei;
            cerr << "Element " << elInd << ":\n";
            cerr << "y-range [" << el.getY0() << ".." << el.getY1() << "] tex: " << el.getBaseTex() << '+' << el.getAllTextures().size() - 1 << '\n';
            cerr << "left:  "; el.getLeft().debug();
            cerr << "right: "; el.getRight().debug();
            cerr << '\n';
        }
    }
    #endif
    for (list<DrawElement>::const_iterator ei = drawEls.begin(); ei != drawEls.end(); ++ei)
        tex.render(ei->getAllTextures(), &*ei);
}
