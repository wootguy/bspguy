// Copyright (C) 2000  Sean Cavanaugh
// This file is licensed under the terms of the Lesser GNU Public License
// (see LPGL.txt, or http://www.gnu.org/copyleft/lesser.txt)

// AJM:
#pragma warning(disable: 4305)                             // truncation from 'const double' to 'float'


#ifndef BOUNDINGBOX_H__
#define BOUNDINGBOX_H__

#if _MSC_VER >= 1000
#pragma once
#endif

class BoundingBox
{
public:
    typedef enum
    {
        eDisjoint,      // neither boxes touch
        eUnion,         // this box intersects with the other box
        eSubset,        // this box is inside the other box
        eSuperset       // this box is completly envelops the other box
    } eBoundingState;

    // Tests if other box is completely outside of this box
    bool testDisjoint(const BoundingBox& other) const
    {
        if ((m_Mins[0] > other.m_Maxs[0] + ON_EPSILON) ||
            (m_Mins[1] > other.m_Maxs[1] + ON_EPSILON) ||
            (m_Mins[2] > other.m_Maxs[2] + ON_EPSILON) ||
            (m_Maxs[0] < other.m_Mins[0] - ON_EPSILON) ||
            (m_Maxs[1] < other.m_Mins[1] - ON_EPSILON) ||
            (m_Maxs[2] < other.m_Mins[2] - ON_EPSILON))
        {
            return true;
        }
        return false;
    }
    // returns true if this box is completely inside other box
    bool testSubset(const BoundingBox& other) const
    {
        if (
                (m_Mins[0] >= other.m_Mins[0]) &&
                (m_Maxs[0] <= other.m_Maxs[0]) &&
                (m_Mins[1] >= other.m_Mins[1]) &&
                (m_Maxs[1] <= other.m_Maxs[1]) &&
                (m_Mins[2] >= other.m_Mins[2]) &&
                (m_Maxs[2] <= other.m_Maxs[2])
           )
        {
            return true;
        }
        return false;
    }
    // returns true if this box contains the other box completely
    bool testSuperset(const BoundingBox& other) const
    {
        return other.testSubset(*this);
    }
    // returns true if this box partially intersects the other box
    bool testUnion(const BoundingBox& other) const
    {
        BoundingBox tmpBox;
        tmpBox.m_Mins[0] = qmax(m_Mins[0], other.m_Mins[0]);
        tmpBox.m_Mins[1] = qmax(m_Mins[1], other.m_Mins[1]);
        tmpBox.m_Mins[2] = qmax(m_Mins[2], other.m_Mins[2]);
        tmpBox.m_Maxs[0] = qmin(m_Maxs[0], other.m_Maxs[0]);
        tmpBox.m_Maxs[1] = qmin(m_Maxs[1], other.m_Maxs[1]);
        tmpBox.m_Maxs[2] = qmin(m_Maxs[2], other.m_Maxs[2]);

        if ((tmpBox.m_Mins[0] > tmpBox.m_Maxs[0]) ||
            (tmpBox.m_Mins[1] > tmpBox.m_Maxs[1]) ||
            (tmpBox.m_Mins[2] > tmpBox.m_Maxs[2]))
        {
            return false;
        }   
        return true;
    }
    eBoundingState test(const BoundingBox& other) const
    {
        eBoundingState rval;
        if (testDisjoint(other))
        {
            rval = eDisjoint;
        }
        else if (testSubset(other))
        {
            rval = eSubset;
        }
        else if (testSuperset(other))
        {
            rval = eSuperset;
        }
        else
        {
            rval = eUnion;
        }
        return rval;
    }

    void set(const vec3_t mins, const vec3_t maxs)
    {
        VectorCopy(mins, m_Mins);
        VectorCopy(maxs, m_Maxs);
    }
    void reset()
    {
        VectorFill(m_Mins,  999999999.999);
        VectorFill(m_Maxs, -999999999.999);
    }
    void add(const vec3_t point)
    {
        m_Mins[0] = qmin(m_Mins[0], point[0]);
        m_Maxs[0] = qmax(m_Maxs[0], point[0]);
        m_Mins[1] = qmin(m_Mins[1], point[1]);
        m_Maxs[1] = qmax(m_Maxs[1], point[1]);
        m_Mins[2] = qmin(m_Mins[2], point[2]);
        m_Maxs[2] = qmax(m_Maxs[2], point[2]);
    }
    void add(const BoundingBox& other)
    {
        add(other.m_Mins);
        add(other.m_Maxs);
    }

public:
    // BoundingBox(const BoundingBox& other) // Default copy constructor ok
    // BoundingBox& operator=(const BoundingBox& other); // Default copy operator ok
    BoundingBox()
    {
        reset();
    }
    BoundingBox(const vec3_t& mins, const vec3_t& maxs)
    {
        VectorCopy(mins, m_Mins);
        VectorCopy(maxs, m_Maxs);
    }
    ~BoundingBox() {}

public:
    // Bounding box
    vec3_t m_Mins;
    vec3_t m_Maxs;
};

#endif
