/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

 /*
  * Standard C/C++ definitions
  */
#if defined __cplusplus
  /* Language: C++ */
#else
  /* Language: C */
#   if __STDC_VERSION__ >= 199901L
      /* C version C99 or higher */
#   else
      /* C version lower than C99 */
      typedef _Bool bool;
#   endif
#endif

#include <stdint.h>

/**
 * \ingroup API
 * \brief C-compatible 2d integer vector representation
 */
typedef union {
    int64_t p[2]; //!< Array of two integer values to represent a vector or point in 2d space
    struct {
        int64_t x; //!< First value in the 2d vector
        int64_t y; //!< Second value in the 2d vector
    }; //!< \private
} SR_vector2i, SR_point2i;

/**
 * \ingroup API
 * \brief C-compatible 2d double vector representation
 */
typedef union {
    double p[2]; //!< Array of two doubles to represent a vector or point in 2d space
    struct {
        double x; //!< First value in the 2d vector
        double y; //!< Second value in the 2d vector
    }; //!< \private
} SR_vector2d, SR_point2d;

/*
 * Operator overloading is only possible in C++
 */
#if defined __cplusplus
inline SR_vector2d operator+(const SR_vector2d& l, const SR_vector2d& r) {
    return SR_vector2d{ l.x + r.x, l.y + r.y };
}
inline SR_vector2d operator-(const SR_vector2d& l, const SR_vector2d& r) {
    return SR_vector2d{ l.x - r.x, l.y - r.y };
}
inline SR_vector2d operator/(const SR_vector2d& l, const double& r) {
    return SR_vector2d{ l.x / r, l.y / r };
}
inline SR_vector2d operator*(const SR_vector2d& l, const double& r) {
    return SR_vector2d{ l.x * r, l.y * r };
}
inline SR_vector2d operator*(const SR_vector2d& l, const SR_vector2d& r) {
    return SR_vector2d{ l.x * r.x, l.y * r.y };
}
#endif

/**
 * \ingroup API
 * \brief C-compatible 3d double vector representation
 */
typedef union {
    double p[3]; //!< Array of three doubles to represent a vector or point in 3d space
    struct {
        double x; //!< First value in the 3d vector
        double y; //!< Second value in the 3d vector
        double z; //!< Third value in the 3d vector
    }; //!< \private
} SR_vector3d, SR_point3d;

/*
 * Operator overloading is only possible in C++
 */
#if defined __cplusplus
inline SR_vector3d operator+(const SR_vector3d& l, const SR_vector3d& r) {
    return SR_vector3d{ l.x + r.x, l.y + r.y, l.z + r.z };
}
inline SR_vector3d operator-(const SR_vector3d& l, const SR_vector3d& r) {
    return SR_vector3d{ l.x - r.x, l.y - r.y, l.z - r.z };
}
inline SR_vector3d operator/(const SR_vector3d& l, const double& r) {
    return SR_vector3d{ l.x / r, l.y / r, l.z / r };
}
inline SR_vector3d operator*(const SR_vector3d& l, const double& r) {
    return SR_vector3d{ l.x * r, l.y * r, l.z * r };
}
inline SR_vector3d operator*(const SR_vector3d& l, const SR_vector3d& r) {
    return SR_vector3d{ l.x * r.x, l.y * r.y, l.z * r.z };
}
#endif

/**
 * \ingroup API
 * \brief C-compatible integer rectangle representation
 */
typedef union {
    int64_t p[4]; //! Array of 4 integer values to represent a rectangle
    struct {
        SR_point2i topLeft; // Top-left corner of the rectangle
        SR_point2i bottomRight; // Bottom-right corner of the rectangle
    };
    struct {
        int64_t left;   //!< Left-most border in the rectangle
        int64_t top;    //!< Top-most border in the rectangle
        int64_t right;  //!< Right-most border in the rectangle
        int64_t bottom; //!< Bottom-most border in the rectangle
    };
} SR_recti;
