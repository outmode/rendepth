/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "sr/types.h"

#include <math.h>

#ifdef __cplusplus
/**
 * \brief Enum used to specify a left or right hand type pose
 *
 * \ingroup HandTracker
 */
enum SR_handSide : uint64_t {
    LeftHand = 0, //!< Left hand
    RightHand = 1 //!< Right hand
};
#else
typedef uint64_t SR_handSide;
#endif

/**
 * \brief C-compatible struct containing the part of the pose representing a thumb
 *
 * \ingroup HandTracker API
 */
typedef union {
    struct {
        SR_point3d metacarpal; //!< Absolute position of the joint between the metacarpal and proximal bone (the base)
        SR_point3d proximal;   //!< Absolute position of the joint between the proximal and distal bone
        SR_point3d distal;     //!< Absolute position of the end of the distal bone (the tip)
    }; //!< \private
    struct {
        SR_point3d a; //!< @link SR_thumb.metacarpal metacarpal @endlink alias
        SR_point3d b; //!< @link SR_thumb.proximal proximal @endlink alias
        SR_point3d tip; //!< @link SR_thumb.distal distal @endlink alias
    }; //!< \private
    SR_point3d joints[3]; //!< Array of joints in the SR_thumb
} SR_thumb;

/**
 * \brief C-compatible struct containing a part of the pose representing a finger
 *
 * \ingroup HandTracker API
 */
typedef union {
    struct {
        SR_point3d metacarpal;   //!< Absolute position of the joint between the metacarpal and proximal bone (the base)
        SR_point3d proximal;     //!< Absolute position of the joint between the proximal and intermediate bone
        SR_point3d intermediate; //!< Absolute position of the joint between the intermediate and distal bone
        SR_point3d distal;       //!< Absolute position of the end of the distal bone (the tip)
    }; //!< \private
    struct {
        SR_point3d a; //!< @link SR_finger.metacarpal metacarpal @endlink alias 
        SR_point3d b; //!< @link SR_finger.proximal proximal @endlink alias 
        SR_point3d c; //!< @link SR_finger.intermediate intermediate @endlink alias 
        SR_point3d tip; //!< @link SR_finger.distal distal @endlink alias 
    }; //!< \private
    SR_point3d joints[4]; //!< Array of joints in the SR_finger
} SR_finger;

/**
 * \brief Enum used to identify joints in the hand
 */
#ifdef __cplusplus
enum SR_handJoints : uint64_t {
    Wrist               = 0, //!< The wrist
    Palm                = 1, //!< The center of the palm
    Thumb_Metacarpal    = 2, //!< The joint between the metacarpal and proximal bone on the thumb (the thumb base)
    Thumb_Proximal      = 3, //!< The joint between the proximal and distal bone on the thumb
    Thumb_Distal        = 4, //!< The end of the distal bone on the thumb (the thumb tip)
    Index_Metacarpal    = 5, //!< The joint between the metacarpal and proximal bone on the index finger (the index finger base)
    Index_Proximal      = 6, //!< The joint between the proximal and intermediate bone on the index finger
    Index_Intermediate  = 7, //!< The joint between the intermediate and distal bone on the index finger
    Index_Distal        = 8, //!< The end of the distal bone on the index finger (the index finger tip)
    Middle_Metacarpal   = 9, //!< The joint between the metacarpal and proximal bone on the middle finger (the middle finger base)
    Middle_Proximal     = 10, //!< The joint between the proximal and intermediate bone on the middle finger
    Middle_Intermediate = 11, //!< The joint between the intermediate and distal bone on the middle finger
    Middle_Distal       = 12, //!< The end of the distal bone on the middle finger (the middle finger tip)
    Ring_Metacarpal     = 13, //!< The joint between the metacarpal and proximal bone on the ring finger (the ring finger base)
    Ring_Proximal       = 14, //!< The joint between the proximal and intermediate bone on the ring finger
    Ring_Intermediate   = 15, //!< The joint between the intermediate and distal bone on the ring finger
    Ring_Distal         = 16, //!< The end of the distal bone on the ring finger (the ring finger tip)
    Pinky_Metacarpal    = 17, //!< The joint between the metacarpal and proximal bone on the pinky (the pinky base)
    Pinky_Proximal      = 18, //!< The joint between the proximal and intermediate bone on the pinky
    Pinky_Intermediate  = 19, //!< The joint between the intermediate and distal bone on the pinky
    Pinky_Distal        = 20  //!< The end of the distal bone on the pinky (the pinky tip)
};
#else
typedef uint64_t SR_handJoints;
#endif

/**
 * \brief C-compatible struct containing the pose of a hand
 * 
 \verbatim

           _16_ _12_
      _20_|    |    |__8_
     |    | 15 | 11 |    |
     | 19 |    |    |  7 |
     |    | 14 | 10 |    |
     | 18 |    |    |  6 |
     |    | 13 |  9 |    |   __4_
     | 17              5 |  /    |
     |                   | /    /
     |                   |/  3 /
     |          1             /
     \                   2  _/
      \__                __/
         \      0       /

 \endverbatim
 *
 * \ingroup HandTracker API
 */
typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time; //!< Time from epoch in microseconds
    uint64_t handId; //!< Hand identifier
    SR_handSide side; //!< Left or right hand
    union {
        struct {
            SR_point3d wrist; //!< Absolute position of the wrist
            SR_point3d palm; //!< Absolute position of the center of the palm
            SR_thumb thumb; //!< Thumb
            union {
                struct {
                    SR_finger index; //!< Index finger
                    SR_finger middle; //!< Middle finger
                    SR_finger ring; //!< Ring finger
                    SR_finger pinky; //!< Pinky
                };
                SR_finger fingers[4]; //!< Fingers (excluding thumb)
            };
        };
        SR_point3d joints[21]; //!< Positions with respect tot he display for joints in the hand
    }; //!< \private Hand pose data can be adressed in multiple ways
} SR_handPose;

/**
 * \brief Determine whether a pose represents a grabbing hand
 */
static float getGrabbing(SR_handPose pose) {
    return 0;
}

/**
 * \brief Determine whether a pose represents a pinching hand
 */
static float getPinching(SR_handPose pose) {
    SR_point3d d = {pose.index.tip.x - pose.thumb.tip.x, pose.index.tip.y - pose.thumb.tip.y, pose.index.tip.z - pose.thumb.tip.z};
    return (float)sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
}
