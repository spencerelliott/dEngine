/*
 *  math.h
 *  dEngine
 *
 *  Created by fabien sanglard on 10/08/09.
 *
 */

#ifndef DE_MATH
#define DE_MATH 1

#include <math.h>
#include "config.h"


/* Vectors */
typedef float vec2_t[2];
typedef float vec3_t[3];
typedef float vec4_t[4];
typedef short vec3short_t[3];
typedef short vec2short_t[2];
typedef int vec3int_t[3];
typedef unsigned char vec4uchar_t[4];

typedef  struct vertex_t {
	vec3_t pos;
    vec3int_t normal;
    vec4uchar_t col;
	vec3short_t tangent;
	vec2short_t text;
} vertex_t;


typedef  struct svertex_t {
	vec3_t pos;
    vec4uchar_t col;
	vec2_t text;
} svertex_t;


#define vectorClear( a )		( (a)[ 0 ] = (a)[ 1 ] = (a)[ 2 ] = 0 )
#define vectorNegate( a, b )	( (b)[ 0 ] = -(a)[ 0 ], (b)[ 1 ] = -(a)[ 1 ], (b)[ 2 ] = -(a)[ 2 ] )
#define vectorSet( v, x, y, z )	( (v)[ 0 ] = ( x ), (v)[ 1 ] = ( y ), (v)[ 2 ] = ( z ) )
#define vectorInverse( a )		( (a)[ 0 ] = (-a)[ 0 ], (a)[ 1 ] = (-a)[ 1 ], (a)[ 2 ] = (-a)[ 2 ] )

#define DotProduct( x, y )			( (x)[ 0 ] * (y)[ 0 ] + (x)[ 1 ] * (y)[ 1 ] + (x)[ 2 ] * (y)[ 2 ] )
#define vectorSubtract( a, b, c )	( (c)[ 0 ] = (a)[ 0 ] - (b)[ 0 ], (c)[ 1 ] = (a)[ 1 ] - (b)[ 1 ], (c)[ 2 ] = (a)[ 2 ] - (b)[ 2 ] )
#define vector2Subtract( a, b, c )	( (c)[ 0 ] = (a)[ 0 ] - (b)[ 0 ], (c)[ 1 ] = (a)[ 1 ] - (b)[ 1 ])
#define vectorAdd( a, b, c )		( (c)[ 0 ] = (a)[ 0 ] + (b)[ 0 ], (c)[ 1 ] = (a)[ 1 ] + (b)[ 1 ], (c)[ 2 ] = (a)[ 2 ] + (b)[ 2 ] )
#define vectorCopy( a, b )			( (b)[ 0 ] = (a)[ 0 ], (b)[ 1 ] = (a)[ 1 ], (b)[ 2 ] = (a)[ 2 ] )
#define vector2Copy( a, b )			( (b)[ 0 ] = (a)[ 0 ], (b)[ 1 ] = (a)[ 1 ] )
#define	vectorScale( v, s, o )		( (o)[ 0 ] = (v)[ 0 ] * (s),(o)[ 1 ] = (v)[ 1 ] * (s), (o)[ 2 ] = (v)[ 2 ] * (s) )
#define	vectorMA( v, s, b, o )		( (o)[ 0 ] = (v)[ 0 ] + (b)[ 0 ]*(s),(o)[ 1 ] = (v)[ 1 ] + (b)[ 1 ] * (s),(o)[ 2 ] = (v)[ 2 ] + (b)[ 2 ] * (s) )

#define vector_Interpolate( vtarget, v1, value, v2 ) ( (vtarget)[0] = (v1)[0] * (1 - (value)) + (v2)[0] * (value),   (vtarget)[1] = (v1)[1] * (1 - (value)) + (v2)[1] * (value)  , (vtarget)[2] = (v1)[2] * (1 - (value)) + (v2)[2] * (value)  )



void vectorCrossProduct( const vec3_t v1, const vec3_t v2, vec3_t cross );
void normalize(vec3_t v);
void vectorInterpolate(const vec3_t v1,const vec3_t v2,float t,vec3_t dest);

#endif