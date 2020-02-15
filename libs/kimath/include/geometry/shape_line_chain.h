/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013 CERN
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Copyright (C) 2013-2019
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef __SHAPE_LINE_CHAIN
#define __SHAPE_LINE_CHAIN


#include <algorithm>                    // for max
#include <sstream>
#include <vector>
#include <wx/gdicmn.h>                  // for wxPoint

#include <core/optional.h>

#include <clipper.hpp>
#include <geometry/seg.h>
#include <geometry/shape.h>
#include <math/box2.h>                  // for BOX2I
#include <math/vector2d.h>


/**
 * SHAPE_LINE_CHAIN
 *
 * Represents a polyline (an zero-thickness chain of connected line segments).
 * I purposedly didn't name it "polyline" to avoid confusion with the existing CPolyLine
 * in pcbnew.
 *
 * SHAPE_LINE_CHAIN class shall not be used for polygons!
 */
class SHAPE_LINE_CHAIN : public SHAPE
{
private:
    typedef std::vector<VECTOR2I>::iterator point_iter;
    typedef std::vector<VECTOR2I>::const_iterator point_citer;

public:
    /**
     * Struct INTERSECTION
     *
     * Represents an intersection between two line segments
     */
    struct INTERSECTION
    {
        /// segment belonging from the (this) argument of Intersect()
        SEG our;
        /// segment belonging from the aOther argument of Intersect()
        SEG their;
        /// point of intersection between our and their.
        VECTOR2I p;
    };

    typedef std::vector<INTERSECTION> INTERSECTIONS;


    /**
     * Constructor
     * Initializes an empty line chain.
     */
    SHAPE_LINE_CHAIN() : SHAPE( SH_LINE_CHAIN ), m_closed( false ), m_width( 0 )
    {}

    /**
     * Copy Constructor
     */
    SHAPE_LINE_CHAIN( const SHAPE_LINE_CHAIN& aShape )
            : SHAPE( SH_LINE_CHAIN ),
              m_points( aShape.m_points ),
              m_closed( aShape.m_closed ),
              m_width( aShape.m_width )
    {}

    SHAPE_LINE_CHAIN( const std::vector<wxPoint>& aV, bool aClosed = false )
            : SHAPE( SH_LINE_CHAIN ), m_closed( aClosed ), m_width( 0 )
    {
        m_points.reserve( aV.size() );

        for( auto pt : aV )
            m_points.emplace_back( pt.x, pt.y );
    }

    SHAPE_LINE_CHAIN( const std::vector<VECTOR2I>& aV, bool aClosed = false )
            : SHAPE( SH_LINE_CHAIN ), m_closed( aClosed ), m_width( 0 )
    {
        m_points = aV;
    }

    SHAPE_LINE_CHAIN( const ClipperLib::Path& aPath )
            : SHAPE( SH_LINE_CHAIN ), m_closed( true ), m_width( 0 )
    {
        m_points.reserve( aPath.size() );

        for( const auto& point : aPath )
            m_points.emplace_back( point.X, point.Y );
    }

    virtual ~SHAPE_LINE_CHAIN()
    {}

    SHAPE_LINE_CHAIN& operator=(const SHAPE_LINE_CHAIN&) = default;

    SHAPE* Clone() const override;

    /**
     * Function Clear()
     * Removes all points from the line chain.
     */
    void Clear()
    {
        m_points.clear();
        m_closed = false;
    }

    /**
     * Function SetClosed()
     *
     * Marks the line chain as closed (i.e. with a segment connecting the last point with
     * the first point).
     * @param aClosed: whether the line chain is to be closed or not.
     */
    void SetClosed( bool aClosed )
    {
        m_closed = aClosed;
    }

    /**
     * Function IsClosed()
     *
     * @return aClosed: true, when our line is closed.
     */
    bool IsClosed() const
    {
        return m_closed;
    }

    /**
     * Sets the width of all segments in the chain
     * @param aWidth width in internal units
     */
    void SetWidth( int aWidth )
    {
        m_width = aWidth;
    }

    /**
     * Gets the current width of the segments in the chain
     * @return width in internal units
     */
    int Width() const
    {
        return m_width;
    }

    /**
     * Function SegmentCount()
     *
     * Returns number of segments in this line chain.
     * @return number of segments
     */
    int SegmentCount() const
    {
        int c = m_points.size() - 1;
        if( m_closed )
            c++;

        return std::max( 0, c );
    }

    /**
     * Function PointCount()
     *
     * Returns the number of points (vertices) in this line chain
     * @return number of points
     */
    int PointCount() const
    {
        return m_points.size();
    }

    /**
     * Function Segment()
     *
     * Returns a copy of the aIndex-th segment in the line chain.
     * @param aIndex: index of the segment in the line chain. Negative values are counted from
     * the end (i.e. -1 means the last segment in the line chain)
     * @return SEG - aIndex-th segment in the line chain
     */
    SEG Segment( int aIndex )
    {
        if( aIndex < 0 )
            aIndex += SegmentCount();

        if( aIndex == (int)( m_points.size() - 1 ) && m_closed )
            return SEG( m_points[aIndex], m_points[0], aIndex );
        else
            return SEG( m_points[aIndex], m_points[aIndex + 1], aIndex );
    }

    /**
     * Function CSegment()
     *
     * Returns a constant copy of the aIndex-th segment in the line chain.
     * @param aIndex: index of the segment in the line chain. Negative values are counted from
     * the end (i.e. -1 means the last segment in the line chain)
     * @return const SEG - aIndex-th segment in the line chain
     */
    const SEG CSegment( int aIndex ) const
    {
        if( aIndex < 0 )
            aIndex += SegmentCount();

        if( aIndex == (int)( m_points.size() - 1 ) && m_closed )
            return SEG( const_cast<VECTOR2I&>( m_points[aIndex] ),
                        const_cast<VECTOR2I&>( m_points[0] ), aIndex );
        else
            return SEG( const_cast<VECTOR2I&>( m_points[aIndex] ),
                        const_cast<VECTOR2I&>( m_points[aIndex + 1] ), aIndex );
    }

    /**
     * Accessor Function to move a point to a specific location
     * @param aIndex Index (wrapping) of the point to move
     * @param aPos New absolute location of the point
     */
    void SetPoint( int aIndex, const VECTOR2I& aPos )
    {
        if( aIndex < 0 )
            aIndex += PointCount();
        else if( aIndex >= PointCount() )
            aIndex -= PointCount();

        m_points[aIndex] = aPos;
    }

    /**
     * Function Point()
     *
     * Returns a const reference to a given point in the line chain.
     * @param aIndex index of the point
     * @return const reference to the point
     */
    const VECTOR2I& CPoint( int aIndex ) const
    {
        if( aIndex < 0 )
            aIndex += PointCount();
        else if( aIndex >= PointCount() )
            aIndex -= PointCount();

        return m_points[aIndex];
    }

    const std::vector<VECTOR2I>& CPoints() const
    {
        return m_points;
    }

    /**
     * Returns the last point in the line chain.
     */
    const VECTOR2I& CLastPoint() const
    {
        return m_points[PointCount() - 1];
    }

    /// @copydoc SHAPE::BBox()
    const BOX2I BBox( int aClearance = 0 ) const override
    {
        BOX2I bbox;
        bbox.Compute( m_points );

        if( aClearance != 0 || m_width != 0 )
            bbox.Inflate( aClearance + m_width );

        return bbox;
    }

    void GenerateBBoxCache()
    {
        m_bbox.Compute( m_points );

        if( m_width != 0 )
            m_bbox.Inflate( m_width );
    }

    /**
     * Function Collide()
     *
     * Checks if point aP lies closer to us than aClearance.
     * @param aP the point to check for collisions with
     * @param aClearance minimum distance that does not qualify as a collision.
     * @return true, when a collision has been found
     */
    bool Collide( const VECTOR2I& aP, int aClearance = 0 ) const override;

    /**
     * Function Collide()
     *
     * Checks if segment aSeg lies closer to us than aClearance.
     * @param aSeg the segment to check for collisions with
     * @param aClearance minimum distance that does not qualify as a collision.
     * @return true, when a collision has been found
     */
    bool Collide( const SEG& aSeg, int aClearance = 0 ) const override;

    /**
     * Function Distance()
     *
     * Computes the minimum distance between the line chain and a point aP.
     * @param aP the point
     * @return minimum distance.
     */
    int Distance( const VECTOR2I& aP, bool aOutlineOnly = false ) const;

    /**
     * Function Reverse()
     *
     * Reverses point order in the line chain.
     * @return line chain with reversed point order (original A-B-C-D: returned D-C-B-A)
     */
    const SHAPE_LINE_CHAIN Reverse() const;

    /**
     * Function Length()
     *
     * Returns length of the line chain in Euclidean metric.
     * @return length of the line chain
     */
    long long int Length() const;

    /**
     * Function Append()
     *
     * Appends a new point at the end of the line chain.
     * @param aX is X coordinate of the new point
     * @param aY is Y coordinate of the new point
     * @param aAllowDuplication = true to append the new point
     * even it is the same as the last entered point
     * false (default) to skip it if it is the same as the last entered point
     */
    void Append( int aX, int aY, bool aAllowDuplication = false )
    {
        VECTOR2I v( aX, aY );
        Append( v, aAllowDuplication );
    }

    /**
     * Function Append()
     *
     * Appends a new point at the end of the line chain.
     * @param aP the new point
     * @param aAllowDuplication = true to append the new point
     * even it is the same as the last entered point
     * false (default) to skip it if it is the same as the last entered point
     */
    void Append( const VECTOR2I& aP, bool aAllowDuplication = false )
    {
        if( m_points.size() == 0 )
            m_bbox = BOX2I( aP, VECTOR2I( 0, 0 ) );

        if( m_points.size() == 0 || aAllowDuplication || CPoint( -1 ) != aP )
        {
            m_points.push_back( aP );
            m_bbox.Merge( aP );
        }
    }

    /**
     * Function Append()
     *
     * Appends another line chain at the end.
     * @param aOtherLine the line chain to be appended.
     */
    void Append( const SHAPE_LINE_CHAIN& aOtherLine )
    {
        if( aOtherLine.PointCount() == 0 )
            return;

        else if( PointCount() == 0 || aOtherLine.CPoint( 0 ) != CPoint( -1 ) )
        {
            const VECTOR2I p = aOtherLine.CPoint( 0 );
            m_points.push_back( p );
            m_bbox.Merge( p );
        }

        for( int i = 1; i < aOtherLine.PointCount(); i++ )
        {
            const VECTOR2I p = aOtherLine.CPoint( i );
            m_points.push_back( p );
            m_bbox.Merge( p );
        }
    }

    void Insert( int aVertex, const VECTOR2I& aP )
    {
        m_points.insert( m_points.begin() + aVertex, aP );
    }

    /**
     * Function Replace()
     *
     * Replaces points with indices in range [start_index, end_index] with a single
     * point aP.
     * @param aStartIndex start of the point range to be replaced (inclusive)
     * @param aEndIndex end of the point range to be replaced (inclusive)
     * @param aP replacement point
     */
    void Replace( int aStartIndex, int aEndIndex, const VECTOR2I& aP );

    /**
     * Function Replace()
     *
     * Replaces points with indices in range [start_index, end_index] with the points from
     * line chain aLine.
     * @param aStartIndex start of the point range to be replaced (inclusive)
     * @param aEndIndex end of the point range to be replaced (inclusive)
     * @param aLine replacement line chain.
     */
    void Replace( int aStartIndex, int aEndIndex, const SHAPE_LINE_CHAIN& aLine );

    /**
     * Function Remove()
     *
     * Removes the range of points [start_index, end_index] from the line chain.
     * @param aStartIndex start of the point range to be replaced (inclusive)
     * @param aEndIndex end of the point range to be replaced (inclusive)
     */
    void Remove( int aStartIndex, int aEndIndex );

    /**
     * Function Remove()
     * removes the aIndex-th point from the line chain.
     * @param aIndex is the index of the point to be removed.
     */
    void Remove( int aIndex )
    {
        Remove( aIndex, aIndex );
    }

    /**
     * Function Split()
     *
     * Inserts the point aP belonging to one of the our segments, splitting the adjacent
     * segment in two.
     * @param aP the point to be inserted
     * @return index of the newly inserted point (or a negative value if aP does not lie on
     * our line)
     */
    int Split( const VECTOR2I& aP );

    /**
     * Function Find()
     *
     * Searches for point aP.
     * @param aP the point to be looked for
     * @return index of the correspoinding point in the line chain or negative when not found.
     */
    int Find( const VECTOR2I& aP ) const;

    /**
     * Function FindSegment()
     *
     * Searches for segment containing point aP.
     * @param aP the point to be looked for
     * @return index of the correspoinding segment in the line chain or negative when not found.
     */
    int FindSegment( const VECTOR2I& aP ) const;

    /**
     * Function Slice()
     *
     * Returns a subset of this line chain containing the [start_index, end_index] range of points.
     * @param aStartIndex start of the point range to be returned (inclusive)
     * @param aEndIndex end of the point range to be returned (inclusive)
     * @return cut line chain.
     */
    const SHAPE_LINE_CHAIN Slice( int aStartIndex, int aEndIndex = -1 ) const;

    struct compareOriginDistance
    {
        compareOriginDistance( const VECTOR2I& aOrigin ):
            m_origin( aOrigin )
        {}

        bool operator()( const INTERSECTION& aA, const INTERSECTION& aB )
        {
            return ( m_origin - aA.p ).EuclideanNorm() < ( m_origin - aB.p ).EuclideanNorm();
        }

        VECTOR2I m_origin;
    };

    bool Intersects( const SHAPE_LINE_CHAIN& aChain ) const;

    /**
     * Function Intersect()
     *
     * Finds all intersection points between our line chain and the segment aSeg.
     * @param aSeg the segment chain to find intersections with
     * @param aIp reference to a vector to store found intersections. Intersection points
     * are sorted with increasing distances from point aSeg.a.
     * @return number of intersections found
     */
    int Intersect( const SEG& aSeg, INTERSECTIONS& aIp ) const;

    /**
     * Function Intersect()
     *
     * Finds all intersection points between our line chain and the line chain aChain.
     * @param aChain the line chain to find intersections with
     * @param aIp reference to a vector to store found intersections. Intersection points
     * are sorted with increasing path lengths from the starting point of aChain.
     * @return number of intersections found
     */
    int Intersect( const SHAPE_LINE_CHAIN& aChain, INTERSECTIONS& aIp ) const;

    /**
     * Function PathLength()
     *
     * Computes the walk path length from the beginning of the line chain and
     * the point aP belonging to our line.
     * @return: path length in Euclidean metric or negative if aP does not belong to
     * the line chain.
     */
    int PathLength( const VECTOR2I& aP ) const;

    /**
     * Function PointInside()
     *
     * Checks if point aP lies inside a polygon (any type) defined by the line chain.
     * For closed shapes only.
     * @param aPt point to check
     * @param aUseBBoxCache gives better peformance if the bounding boxe caches have been
     *                      generated.
     * @return true if the point is inside the shape (edge is not treated as being inside).
     */
     bool PointInside( const VECTOR2I& aPt, int aAccuracy = 0, bool aUseBBoxCache = false ) const;

    /**
     * Function PointOnEdge()
     *
     * Checks if point aP lies on an edge or vertex of the line chain.
     * @param aP point to check
     * @return true if the point lies on the edge.
     */
    bool PointOnEdge( const VECTOR2I& aP, int aAccuracy = 0 ) const;

    /**
     * Function EdgeContainingPoint()
     *
     * Checks if point aP lies on an edge or vertex of the line chain.
     * @param aP point to check
     * @return index of the first edge containing the point, otherwise negative
     */
    int EdgeContainingPoint( const VECTOR2I& aP, int aAccuracy = 0 ) const;

    /**
     * Function CheckClearance()
     *
     * Checks if point aP is closer to (or on) an edge or vertex of the line chain.
     * @param aP point to check
     * @param aDist distance in internal units
     * @return true if the point is equal to or closer than aDist to the line chain.
     */
    bool CheckClearance( const VECTOR2I& aP, const int aDist) const;

    /**
     * Function SelfIntersecting()
     *
     * Checks if the line chain is self-intersecting.
     * @return (optional) first found self-intersection point.
     */
    const OPT<INTERSECTION> SelfIntersecting() const;

    /**
     * Function Simplify()
     *
     * Simplifies the line chain by removing colinear adjacent segments and duplicate vertices.
     * @return reference to self.
     */
    SHAPE_LINE_CHAIN& Simplify();

    /**
     * Creates a new Clipper path from the SHAPE_LINE_CHAIN in a given orientation
     *
     */
    ClipperLib::Path convertToClipper( bool aRequiredOrientation ) const;

    /**
     * Find the segment nearest the given point.
     *
     * @param aP point to compare with
     * @return the index of the segment closest to the point
     */
    int NearestSegment( const VECTOR2I& aP ) const;

    /**
     * Function NearestPoint()
     *
     * Finds a point on the line chain that is closest to point aP.
     * @return the nearest point.
     */
    const VECTOR2I NearestPoint( const VECTOR2I& aP ) const;

    /**
     * Function NearestPoint()
     *
     * Finds a point on the line chain that is closest to the line defined
     * by the points of segment aSeg, also returns the distance.
     * @param aSeg Segment defining the line.
     * @param dist reference receiving the distance to the nearest point.
     * @return the nearest point.
     */
    const VECTOR2I NearestPoint( const SEG& aSeg, int& dist ) const;

    /// @copydoc SHAPE::Format()
    const std::string Format() const override;

    /// @copydoc SHAPE::Parse()
    bool Parse( std::stringstream& aStream ) override;

    bool operator!=( const SHAPE_LINE_CHAIN& aRhs ) const
    {
        if( PointCount() != aRhs.PointCount() )
            return true;

        for( int i = 0; i < PointCount(); i++ )
        {
            if( CPoint( i ) != aRhs.CPoint( i ) )
                return true;
        }

        return false;
    }

    bool CompareGeometry( const SHAPE_LINE_CHAIN& aOther ) const;

    void Move( const VECTOR2I& aVector ) override
    {
        for( std::vector<VECTOR2I>::iterator i = m_points.begin(); i != m_points.end(); ++i )
            (*i) += aVector;
    }

    /**
     * Mirrors the line points about y or x (or both)
     * @param aX If true, mirror about the y axis (flip X coordinate)
     * @param aY If true, mirror about the x axis (flip Y coordinate)
     * @param aRef sets the reference point about which to mirror
     */
    void Mirror( bool aX = true, bool aY = false, const VECTOR2I& aRef = { 0, 0 } )
    {
        for( auto& pt : m_points )
        {
            if( aX )
                pt.x = -pt.x + 2 * aRef.x;

            if( aY )
                pt.y = -pt.y + 2 * aRef.y;
        }
    }

    /**
     * Function Rotate
     * rotates all vertices by a given angle
     * @param aCenter is the rotation center
     * @param aAngle rotation angle in radians
     */
    void Rotate( double aAngle, const VECTOR2I& aCenter = VECTOR2I( 0, 0 ) );

    bool IsSolid() const override
    {
        return false;
    }

    const VECTOR2I PointAlong( int aPathLength ) const;

    double Area() const;

private:
    /// array of vertices
    std::vector<VECTOR2I> m_points;

    /// is the line chain closed?
    bool m_closed;

    /// Width of the segments (for BBox calculations in RTree)
    /// TODO Adjust usage of SHAPE_LINE_CHAIN to account for where we need a width and where not
    /// Alternatively, we could split the class into a LINE_CHAIN (no width) and SHAPE_LINE_CHAIN that derives from
    /// SHAPE as well that does have a width.  Not sure yet on the correct path.
    int m_width;

    /// cached bounding box
    BOX2I m_bbox;
};

#endif // __SHAPE_LINE_CHAIN
