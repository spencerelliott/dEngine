/*
 *  collision.h
 *  dEngine
 *
 *  Collision primitives for the dEngine physics module.
 *
 *  Only two shapes are supported: spheres and axis aligned boxes. Both are
 *  cheap enough to be tested by the thousands on a 200MHz SH4 and neither
 *  requires an orientation, which keeps the whole physics pipeline free of
 *  rotational dynamics.
 *
 *  Conventions
 *  -----------
 *  - A collider is expressed in local space. Its world position is the
 *    position of the body owning it, plus the collider offset.
 *  - A contact normal always points from shape A towards shape B. To separate
 *    the pair, A moves along -normal and B along +normal.
 *  - Casts return a fraction in [0,1] of the requested motion (rays use the
 *    fraction of maxDistance).
 */

#ifndef DE_COLLISION
#define DE_COLLISION

#include "globals.h"
#include "internal_math.h"

#define COLLIDER_SPHERE 0
#define COLLIDER_AABB   1

/* Below this distance two features are considered coincident. */
#define COL_EPSILON 0.0001f

/*
 * The SH4 has single cycle reciprocal square root and no fast divider, so
 * route the few divisions and square roots the collision code needs through
 * the DreamHAL helpers when building for the Dreamcast.
 */
#if defined(DREAMCAST)
	#include "sh4_math.h"
	#define DE_SQRT( x )	MATH_Fast_Sqrt( x )
	#define DE_FABS( x )	MATH_fabs( x )
	#define DE_RECIP( x )	MATH_Fast_Invert( x )
#else
	#include <math.h>
	#define DE_SQRT( x )	sqrtf( x )
	#define DE_FABS( x )	fabsf( x )
	#define DE_RECIP( x )	( 1.0f / ( x ) )
#endif

typedef struct collider_t {
	uchar type;			/* COLLIDER_SPHERE or COLLIDER_AABB */
	vec3_t offset;		/* local offset from the body origin */
	vec3_t halfExtents;	/* COLLIDER_AABB only */
	float radius;		/* COLLIDER_SPHERE only */
} collider_t;

typedef struct aabb_t {
	vec3_t min;
	vec3_t max;
} aabb_t;

/* Output of a narrow phase test. */
typedef struct contact_t {
	vec3_t normal;	/* unit length, points from A to B */
	vec3_t point;	/* world space contact point */
	float depth;	/* penetration along the normal, always >= 0 */
} contact_t;

/* Output of a ray or shape cast. */
typedef struct trace_t {
	vec3_t point;	/* world space impact point */
	vec3_t normal;	/* surface normal at the impact, faces the caster */
	float fraction;	/* [0,1] of the requested motion */
	uchar hit;
} trace_t;

/* Collider setup */
void COL_MakeSphere(collider_t* collider, float radius);
void COL_MakeBox(collider_t* collider, float halfX, float halfY, float halfZ);
void COL_SetOffset(collider_t* collider, float x, float y, float z);

/* Bounds */
void COL_GetCenter(const collider_t* collider, const vec3_t position, vec3_t center);
void COL_GetAABB(const collider_t* collider, const vec3_t position, aabb_t* bounds);
float COL_GetBoundingRadius(const collider_t* collider);
void COL_ExpandAABB(aabb_t* bounds, const vec3_t amount);
void COL_EncloseAABB(aabb_t* bounds, const aabb_t* other);
uchar COL_AABBOverlap(const aabb_t* a, const aabb_t* b);
uchar COL_AABBContainsPoint(const aabb_t* bounds, const vec3_t point);

/*
 * Narrow phase. Returns 1 when the two colliders intersect. 'contact' may be
 * NULL when only a boolean answer is needed.
 */
uchar COL_Collide(const collider_t* a, const vec3_t posA,
				  const collider_t* b, const vec3_t posB,
				  contact_t* contact);

/* Ray casts. 'direction' does not need to be normalized. */
uchar COL_RayVsSphere(const vec3_t origin, const vec3_t direction, float maxDistance,
					  const vec3_t center, float radius, trace_t* trace);
uchar COL_RayVsAABB(const vec3_t origin, const vec3_t direction, float maxDistance,
					const aabb_t* bounds, trace_t* trace);
uchar COL_RayVsCollider(const vec3_t origin, const vec3_t direction, float maxDistance,
						const collider_t* collider, const vec3_t position, trace_t* trace);

/*
 * Shape cast: moves 'moving' from 'from' to 'to' and reports the first touch
 * against the collider 'fixed' sitting at 'fixedPosition'.
 *
 * Box versus box and sphere versus sphere casts are exact. Mixed pairs use the
 * bounding box of the sphere (or a box expanded by the sphere radius), which is
 * conservative: it can report an impact slightly early near a corner, never
 * late. That trade keeps the cast down to a handful of compares, which is what
 * the SH4 wants.
 */
uchar COL_CastCollider(const collider_t* moving, const vec3_t from, const vec3_t to,
					   const collider_t* fixed, const vec3_t fixedPosition, trace_t* trace);

#endif
