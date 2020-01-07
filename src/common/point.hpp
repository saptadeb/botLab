/**
* \file     point.hpp
* \author   Collin Johnson
* 
* Definition of a simple Point class.
*/

#ifndef MATH_POINT_HPP
#define MATH_POINT_HPP

#include <cmath>
#include <iostream>

/**
* Point<T> represents a simple 2D Cartesian point.
*/
template<typename T>
class Point
{
public:

    T x;
    T y;

    /**
    * Default constructor for Point.
    */
    Point(void)
    : x(0)
    , y(0)
    {
    }

    /**
    * Constructor for Point.
    */
    Point(T xPos, T yPos)
    : x(xPos)
    , y(yPos)
    {
        // Nothing to do here
    }

    /**
    * Copy constructor for a Point with a different type.
    */
    template <typename U>
    Point(const Point<U>& copy)
    : x(copy.x)
    , y(copy.y)
    {
    }
};

/**
* distance_between_points calculates the Cartesian distance between two points.
*/
template<typename T, typename U>
float distance_between_points(const Point<T>& pointA, const Point<U>& pointB)
{
    return std::sqrt((pointA.x-pointB.x)*(pointA.x-pointB.x) + (pointA.y-pointB.y)*(pointA.y-pointB.y));
}


/**
* angle_between_points finds the angle between two vectors defined by the three points provided as arguments.
*
* \param    first               Head of one of the vectors
* \param    second              Head of the other vector
* \param    center              Point shared by the two vectors
* \return   Angle between first and second in the range of [0, PI].
*/
template <typename T>
float angle_between_points(const Point<T>& first, const Point<T>& second, const Point<T>& center)
{
    // Use the vector form here to get a pretty answer:
    //  cos(theta) = a dot b / ||a||*||b||

    T xA = first.x  - center.x;
    T yA = first.y  - center.y;
    T xB = second.x - center.x;
    T yB = second.y - center.y;
    
    double acosTerm = (xA*xB + yA*yB) / (std::sqrt(xA*xA + yA*yA) * std::sqrt(xB*xB + yB*yB));
    acosTerm = std::min(1.0, std::max(-1.0, acosTerm));
    
    return std::acos(acosTerm);
}


/**
* angle_to_point finds the angle of a vector from head to tail.
*
* \param    from                Start of the vector
* \param    to                  End of the vector
* \return   Angle of vector atan2(to - from).
*/
template <typename T, typename U>
float angle_to_point(const Point<T>& from, const Point<U>& to)
{
    return std::atan2(to.y-from.y, to.x-from.x);
}


/**
* rotate applies a rotation matrix to the point, rotating the point around the origin by the specified angle.
*
* \param    point       Point to be rotated
* \param    angle       Angle by which to rotate
* \return   Rotated point.
*/
template<typename T>
Point<T> rotate(const Point<T>& point, float angle)
{
    return Point<T>(point.x*std::cos(angle) - point.y*std::sin(angle),
                    point.x*std::sin(angle) + point.y*std::cos(angle));
}


/**
* transform applies a transform to the point. The transform first adds the x and y values, then applies
* a rotation of the specified angle.
*
* \param    point       Point to be transformed
* \param    x           x shift
* \param    y           y shift
* \param    angle       Angle to rotate
* \return   Transformed point.
*/
template <typename T, typename U>
Point<T> transform(const Point<T>& point, U x, U y, U angle)
{
    Point<T> transformed(point.x+x, point.y+y);
    return rotate(transformed, angle);
}


// Various useful operator overloads
template <typename T, typename U>
bool operator==(const Point<T>& lhs, const Point<U>& rhs)
{
    return (lhs.x == rhs.x) && (lhs.y == rhs.y);
}

template <typename T, typename U>
bool operator!=(const Point<T>& lhs, const Point<U>& rhs)
{
    // Go from bottom left to top right
    return !(lhs == rhs);
}

template <typename T, typename U>
bool operator<(const Point<T>& lhs, const Point<U>& rhs)
{
    // Go from bottom left to top right
    return (lhs.x < rhs.x) || ((lhs.x == rhs.x) && (lhs.y < rhs.y));
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const Point<T>& point)
{
    out<<'('<<point.x<<','<<point.y<<')';
    return out;
}

template <typename T, typename U>
Point<T> operator-(const Point<T>& lhs, const Point<U>& rhs)
{
    return Point<T>(lhs.x - rhs.x,
                    lhs.y - rhs.y);
}

template <typename T, typename U>
Point<T> operator+(const Point<T>& lhs, const Point<U>& rhs)
{
    return Point<T>(lhs.x + rhs.x,
                    lhs.y + rhs.y);
}

template <typename T, typename U>
Point<T>& operator+=(Point<T>& lhs, const Point<U>& rhs)
{
    lhs.x += rhs.x;
    lhs.y += rhs.y;

    return lhs;
}


template <typename T, typename U>
Point<T>& operator-=(Point<T>& lhs, const Point<U>& rhs)
{
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;

    return lhs;
}

#endif // MATH_POINT_HPP
