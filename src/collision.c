/*
 *  collision.c
 *  dEngine
 *
 *  Sphere and axis aligned box collision primitives.
 */

#include "collision.h"

static const vec3_t colUpVector = { 0.0f, 1.0f, 0.0f };

/* ========================================================================= */
/* Collider setup                                                            */
/* ========================================================================= */

void COL_MakeSphere(collider_t* collider, float radius)
{
	memset(collider, 0, sizeof(collider_t));
	collider->type = COLLIDER_SPHERE;
	collider->radius = radius;
}

void COL_MakeBox(collider_t* collider, float halfX, float halfY, float halfZ)
{
	memset(collider, 0, sizeof(collider_t));
	collider->type = COLLIDER_AABB;
	vectorSet(collider->halfExtents, halfX, halfY, halfZ);
}

void COL_SetOffset(collider_t* collider, float x, float y, float z)
{
	vectorSet(collider->offset, x, y, z);
}

/* ========================================================================= */
/* Bounds                                                                    */
/* ========================================================================= */

void COL_GetCenter(const collider_t* collider, const vec3_t position, vec3_t center)
{
	vectorAdd(position, collider->offset, center);
}

void COL_GetAABB(const collider_t* collider, const vec3_t position, aabb_t* bounds)
{
	vec3_t center;
	int i;

	COL_GetCenter(collider, position, center);

	if (collider->type == COLLIDER_SPHERE)
	{
		for(i = 0 ; i < 3 ; i++)
		{
			bounds->min[i] = center[i] - collider->radius;
			bounds->max[i] = center[i] + collider->radius;
		}
	}
	else
	{
		for(i = 0 ; i < 3 ; i++)
		{
			bounds->min[i] = center[i] - collider->halfExtents[i];
			bounds->max[i] = center[i] + collider->halfExtents[i];
		}
	}
}

float COL_GetBoundingRadius(const collider_t* collider)
{
	if (collider->type == COLLIDER_SPHERE)
		return collider->radius;

	return DE_SQRT(DotProduct(collider->halfExtents, collider->halfExtents));
}

void COL_ExpandAABB(aabb_t* bounds, const vec3_t amount)
{
	int i;

	for(i = 0 ; i < 3 ; i++)
	{
		bounds->min[i] -= amount[i];
		bounds->max[i] += amount[i];
	}
}

void COL_EncloseAABB(aabb_t* bounds, const aabb_t* other)
{
	int i;

	for(i = 0 ; i < 3 ; i++)
	{
		if (other->min[i] < bounds->min[i])
			bounds->min[i] = other->min[i];
		if (other->max[i] > bounds->max[i])
			bounds->max[i] = other->max[i];
	}
}

uchar COL_AABBOverlap(const aabb_t* a, const aabb_t* b)
{
	if (a->max[X] < b->min[X] || a->min[X] > b->max[X])
		return 0;
	if (a->max[Y] < b->min[Y] || a->min[Y] > b->max[Y])
		return 0;
	if (a->max[Z] < b->min[Z] || a->min[Z] > b->max[Z])
		return 0;

	return 1;
}

uchar COL_AABBContainsPoint(const aabb_t* bounds, const vec3_t point)
{
	int i;

	for(i = 0 ; i < 3 ; i++)
	{
		if (point[i] < bounds->min[i] || point[i] > bounds->max[i])
			return 0;
	}

	return 1;
}

/* ========================================================================= */
/* Narrow phase                                                              */
/* ========================================================================= */

static uchar COL_SphereVsSphere(const vec3_t centerA, float radiusA,
								const vec3_t centerB, float radiusB,
								contact_t* contact)
{
	vec3_t delta;
	float distanceSquared, distance, radii, inverse;

	vectorSubtract(centerB, centerA, delta);

	radii = radiusA + radiusB;
	distanceSquared = DotProduct(delta, delta);

	if (distanceSquared >= radii * radii)
		return 0;

	if (!contact)
		return 1;

	if (distanceSquared > COL_EPSILON * COL_EPSILON)
	{
		distance = DE_SQRT(distanceSquared);
		inverse = DE_RECIP(distance);
		vectorScale(delta, inverse, contact->normal);
	}
	else
	{
		/* Concentric spheres: pick an arbitrary but stable direction. */
		distance = 0.0f;
		vectorCopy(colUpVector, contact->normal);
	}

	contact->depth = radii - distance;
	inverse = radiusA - contact->depth * 0.5f;
	vectorMA(centerA, inverse, contact->normal, contact->point);

	return 1;
}

static uchar COL_BoxVsBox(const vec3_t centerA, const vec3_t halfA,
						  const vec3_t centerB, const vec3_t halfB,
						  contact_t* contact)
{
	vec3_t delta, overlap;
	float low, high;
	int axis, i;

	vectorSubtract(centerB, centerA, delta);

	for(i = 0 ; i < 3 ; i++)
	{
		overlap[i] = (halfA[i] + halfB[i]) - DE_FABS(delta[i]);

		if (overlap[i] <= 0.0f)
			return 0;
	}

	if (!contact)
		return 1;

	/* Separate along the axis of least penetration. */
	axis = X;
	if (overlap[Y] < overlap[axis])
		axis = Y;
	if (overlap[Z] < overlap[axis])
		axis = Z;

	vectorClear(contact->normal);
	contact->normal[axis] = (delta[axis] < 0.0f) ? -1.0f : 1.0f;
	contact->depth = overlap[axis];

	/* Contact point sits in the middle of the overlapping region. */
	for(i = 0 ; i < 3 ; i++)
	{
		low = centerA[i] - halfA[i];
		if (centerB[i] - halfB[i] > low)
			low = centerB[i] - halfB[i];

		high = centerA[i] + halfA[i];
		if (centerB[i] + halfB[i] < high)
			high = centerB[i] + halfB[i];

		contact->point[i] = (low + high) * 0.5f;
	}

	return 1;
}

/* Sphere is A, box is B: the resulting normal points from the sphere to the box. */
static uchar COL_SphereVsBox(const vec3_t center, float radius,
							 const vec3_t boxCenter, const vec3_t boxHalf,
							 contact_t* contact)
{
	vec3_t closest, delta, faceDistance;
	float distanceSquared, distance, low, high, inverse;
	int axis, i;

	for(i = 0 ; i < 3 ; i++)
	{
		low = boxCenter[i] - boxHalf[i];
		high = boxCenter[i] + boxHalf[i];

		closest[i] = center[i];
		if (closest[i] < low)
			closest[i] = low;
		if (closest[i] > high)
			closest[i] = high;
	}

	vectorSubtract(closest, center, delta);
	distanceSquared = DotProduct(delta, delta);

	if (distanceSquared >= radius * radius)
		return 0;

	if (!contact)
		return 1;

	if (distanceSquared > COL_EPSILON * COL_EPSILON)
	{
		distance = DE_SQRT(distanceSquared);
		inverse = DE_RECIP(distance);
		vectorScale(delta, inverse, contact->normal);
		contact->depth = radius - distance;
		vectorCopy(closest, contact->point);

		return 1;
	}

	/*
	 * The center of the sphere is inside the box. Push it out through the
	 * closest face.
	 */
	for(i = 0 ; i < 3 ; i++)
		faceDistance[i] = boxHalf[i] - DE_FABS(center[i] - boxCenter[i]);

	axis = X;
	if (faceDistance[Y] < faceDistance[axis])
		axis = Y;
	if (faceDistance[Z] < faceDistance[axis])
		axis = Z;

	vectorClear(contact->normal);
	contact->normal[axis] = (center[axis] < boxCenter[axis]) ? 1.0f : -1.0f;
	contact->depth = faceDistance[axis] + radius;

	vectorCopy(center, contact->point);
	contact->point[axis] = boxCenter[axis] - contact->normal[axis] * boxHalf[axis];

	return 1;
}

uchar COL_Collide(const collider_t* a, const vec3_t posA,
				  const collider_t* b, const vec3_t posB,
				  contact_t* contact)
{
	vec3_t centerA, centerB;
	uchar result;

	COL_GetCenter(a, posA, centerA);
	COL_GetCenter(b, posB, centerB);

	if (a->type == COLLIDER_SPHERE)
	{
		if (b->type == COLLIDER_SPHERE)
			return COL_SphereVsSphere(centerA, a->radius, centerB, b->radius, contact);

		return COL_SphereVsBox(centerA, a->radius, centerB, b->halfExtents, contact);
	}

	if (b->type == COLLIDER_SPHERE)
	{
		/* Test as sphere versus box then flip the normal back to A -> B. */
		result = COL_SphereVsBox(centerB, b->radius, centerA, a->halfExtents, contact);

		if (result && contact)
			vectorNegate(contact->normal, contact->normal);

		return result;
	}

	return COL_BoxVsBox(centerA, a->halfExtents, centerB, b->halfExtents, contact);
}

/* ========================================================================= */
/* Ray casts                                                                 */
/* ========================================================================= */

static void COL_ClearTrace(trace_t* trace)
{
	if (!trace)
		return;

	trace->hit = 0;
	trace->fraction = 1.0f;
	vectorClear(trace->point);
	vectorClear(trace->normal);
}

/* Normalizes 'direction' into 'unit'. Returns 0 when the direction is degenerate. */
static uchar COL_MakeRayDirection(const vec3_t direction, vec3_t unit)
{
	float lengthSquared, inverse;

	lengthSquared = DotProduct(direction, direction);

	if (lengthSquared < COL_EPSILON * COL_EPSILON)
		return 0;

	inverse = DE_RECIP(DE_SQRT(lengthSquared));
	vectorScale(direction, inverse, unit);

	return 1;
}

uchar COL_RayVsSphere(const vec3_t origin, const vec3_t direction, float maxDistance,
					  const vec3_t center, float radius, trace_t* trace)
{
	vec3_t unit, delta;
	float projection, closing, discriminant, distance, inverse;

	COL_ClearTrace(trace);

	if (maxDistance <= 0.0f || !COL_MakeRayDirection(direction, unit))
		return 0;

	vectorSubtract(origin, center, delta);

	projection = DotProduct(delta, unit);
	closing = DotProduct(delta, delta) - radius * radius;

	/* Origin outside the sphere and pointing away from it. */
	if (closing > 0.0f && projection > 0.0f)
		return 0;

	discriminant = projection * projection - closing;

	if (discriminant < 0.0f)
		return 0;

	distance = -projection - DE_SQRT(discriminant);

	/* Origin already inside the sphere. */
	if (distance < 0.0f)
		distance = 0.0f;

	if (distance > maxDistance)
		return 0;

	if (trace)
	{
		trace->hit = 1;
		trace->fraction = distance * DE_RECIP(maxDistance);
		vectorMA(origin, distance, unit, trace->point);

		vectorSubtract(trace->point, center, trace->normal);
		if (radius > COL_EPSILON)
		{
			inverse = DE_RECIP(radius);
			vectorScale(trace->normal, inverse, trace->normal);
		}
		else
			vectorNegate(unit, trace->normal);
	}

	return 1;
}

uchar COL_RayVsAABB(const vec3_t origin, const vec3_t direction, float maxDistance,
					const aabb_t* bounds, trace_t* trace)
{
	vec3_t unit;
	float enter, leave, inverse, first, last, temp, sign, hitSign;
	int hitAxis, i;

	COL_ClearTrace(trace);

	if (maxDistance <= 0.0f || !COL_MakeRayDirection(direction, unit))
		return 0;

	enter = 0.0f;
	leave = maxDistance;
	hitAxis = -1;
	hitSign = -1.0f;

	for(i = 0 ; i < 3 ; i++)
	{
		if (DE_FABS(unit[i]) < COL_EPSILON)
		{
			/* Parallel to this slab: the origin has to already be inside it. */
			if (origin[i] < bounds->min[i] || origin[i] > bounds->max[i])
				return 0;

			continue;
		}

		inverse = DE_RECIP(unit[i]);
		first = (bounds->min[i] - origin[i]) * inverse;
		last = (bounds->max[i] - origin[i]) * inverse;
		sign = -1.0f;

		if (first > last)
		{
			temp = first;
			first = last;
			last = temp;
			sign = 1.0f;
		}

		if (first > enter)
		{
			enter = first;
			hitAxis = i;
			hitSign = sign;
		}

		if (last < leave)
			leave = last;

		if (enter > leave)
			return 0;
	}

	if (trace)
	{
		trace->hit = 1;
		trace->fraction = enter * DE_RECIP(maxDistance);
		vectorMA(origin, enter, unit, trace->point);

		if (hitAxis < 0)
		{
			/* Started inside the box. */
			vectorNegate(unit, trace->normal);
		}
		else
		{
			vectorClear(trace->normal);
			trace->normal[hitAxis] = hitSign;
		}
	}

	return 1;
}

uchar COL_RayVsCollider(const vec3_t origin, const vec3_t direction, float maxDistance,
						const collider_t* collider, const vec3_t position, trace_t* trace)
{
	vec3_t center;
	aabb_t bounds;

	if (collider->type == COLLIDER_SPHERE)
	{
		COL_GetCenter(collider, position, center);
		return COL_RayVsSphere(origin, direction, maxDistance, center, collider->radius, trace);
	}

	COL_GetAABB(collider, position, &bounds);

	return COL_RayVsAABB(origin, direction, maxDistance, &bounds, trace);
}

/* ========================================================================= */
/* Shape casts                                                               */
/* ========================================================================= */

uchar COL_CastCollider(const collider_t* moving, const vec3_t from, const vec3_t to,
					   const collider_t* fixed, const vec3_t fixedPosition, trace_t* trace)
{
	vec3_t start, delta, expansion, fixedCenter;
	aabb_t bounds;
	float distance, distanceSquared;
	int i;

	COL_ClearTrace(trace);

	COL_GetCenter(moving, from, start);
	vectorSubtract(to, from, delta);

	distanceSquared = DotProduct(delta, delta);

	if (distanceSquared < COL_EPSILON * COL_EPSILON)
		return 0;

	distance = DE_SQRT(distanceSquared);

	if (moving->type == COLLIDER_SPHERE && fixed->type == COLLIDER_SPHERE)
	{
		/* Exact: ray against a sphere grown by the radius of the caster. */
		COL_GetCenter(fixed, fixedPosition, fixedCenter);

		if (!COL_RayVsSphere(start, delta, distance, fixedCenter,
							 moving->radius + fixed->radius, trace))
			return 0;

		if (trace)
		{
			/* Report the touch on the surface of the fixed sphere. */
			vectorMA(trace->point, -moving->radius, trace->normal, trace->point);
		}

		return 1;
	}

	/*
	 * Everything else is handled as a ray against the Minkowski sum of the two
	 * shapes, approximated by an axis aligned box. Exact for box versus box,
	 * conservative around the corners of the mixed cases.
	 */
	COL_GetAABB(fixed, fixedPosition, &bounds);

	if (moving->type == COLLIDER_SPHERE)
		vectorSet(expansion, moving->radius, moving->radius, moving->radius);
	else
		vectorCopy(moving->halfExtents, expansion);

	COL_ExpandAABB(&bounds, expansion);

	if (!COL_RayVsAABB(start, delta, distance, &bounds, trace))
		return 0;

	if (trace)
	{
		/* Pull the impact point back onto the surface of the fixed collider. */
		COL_GetAABB(fixed, fixedPosition, &bounds);

		for(i = 0 ; i < 3 ; i++)
		{
			if (trace->point[i] < bounds.min[i])
				trace->point[i] = bounds.min[i];
			if (trace->point[i] > bounds.max[i])
				trace->point[i] = bounds.max[i];
		}
	}

	return 1;
}
