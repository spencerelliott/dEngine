/*
 *  quaternion.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10/08/09.
 *
 */

#include "quaternion.h"
#include "sh4_math.h"


/**
 * Basic quaternion operations.
 */

void Quat_computeW (quat4_t q)
{
	float t = 1.0f - (q[X] * q[X]) - (q[Y] * q[Y]) - (q[Z] * q[Z]);
	
	if (t < 0.0f)
		q[W] = 0.0f;
	else
		q[W] = -sqrt (t);
}

void Quat_normalize (quat4_t q)
{
	/* compute magnitude of the quaternion */
#if DE_USE_FAST_MATH
    float mag = MATH_Sum_of_Squares(q[W], q[X], q[Y], q[Z]);
#else
    float mag = sqrt ((q[X] * q[X]) + (q[Y] * q[Y])
					  + (q[Z] * q[Z]) + (q[W] * q[W]));
#endif
	
	/* check for bogus length, to protect against divide by zero */
	if (mag > 0.0f)
    {
		/* normalize it */
#if DE_USE_FAST_MATH
		float oneOverMag = MATH_fsrra(mag);
#else
        float oneOverMag = 1.0f / mag;
#endif
		
		q[X] *= oneOverMag;
		q[Y] *= oneOverMag;
		q[Z] *= oneOverMag;
		q[W] *= oneOverMag;
    }
}

void Quat_multQuat (const quat4_t qa, const quat4_t qb, quat4_t out)
{
#if DE_USE_FAST_MATH
    out[W] = MATH_fipr(qa[W], -qa[X], -qa[Y], -qa[Z], qb[W], qb[X], qb[Y], qb[Z]);
    out[X] = MATH_fipr(qa[X], qa[W], qa[Y], -qa[Z], qb[W], qb[X], qb[Z], qb[Y]);
    out[Y] = MATH_fipr(qa[Y], qa[W], qa[Z], -qa[X], qb[W], qb[Y], qb[X], qb[Z]);
    out[Z] = MATH_fipr(qa[Z], qa[W], qa[X], -qa[Y], qb[W], qb[Z], qb[Y], qb[X]);
#else
	out[W] = (qa[W] * qb[W]) - (qa[X] * qb[X]) - (qa[Y] * qb[Y]) - (qa[Z] * qb[Z]);
	out[X] = (qa[X] * qb[W]) + (qa[W] * qb[X]) + (qa[Y] * qb[Z]) - (qa[Z] * qb[Y]);
	out[Y] = (qa[Y] * qb[W]) + (qa[W] * qb[Y]) + (qa[Z] * qb[X]) - (qa[X] * qb[Z]);
	out[Z] = (qa[Z] * qb[W]) + (qa[W] * qb[Z]) + (qa[X] * qb[Y]) - (qa[Y] * qb[X]);
#endif
}

void Quat_multVec (const quat4_t q, const vec3_t v, quat4_t out)
{
	out[W] = - (q[X] * v[X]) - (q[Y] * v[Y]) - (q[Z] * v[Z]);
	out[X] =   (q[W] * v[X]) + (q[Y] * v[Z]) - (q[Z] * v[Y]);
	out[Y] =   (q[W] * v[Y]) + (q[Z] * v[X]) - (q[X] * v[Z]);
	out[Z] =   (q[W] * v[Z]) + (q[X] * v[Y]) - (q[Y] * v[X]);
}

void Quat_rotateShortPoint (const quat4_t q, const vec3short_t in, vec3_t out)
{
	vec3_t inFloat;
	
	inFloat[0] = in[0];
	inFloat[1] = in[1];
	inFloat[2] = in[2];
	
	Quat_rotatePoint(q,inFloat,out);
	
	
}

void Quat_rotatePoint (const quat4_t q, const vec3_t in, vec3_t out)
{
	quat4_t tmp, inv, final;
	
	inv[X] = -q[X]; 
	inv[Y] = -q[Y];
	inv[Z] = -q[Z]; 
	inv[W] =  q[W];
	
	//Quat_normalize (inv);
	
	Quat_multVec (q, in, tmp);
	Quat_multQuat (tmp, inv, final);
	
	out[X] = final[X];
	out[Y] = final[Y];
	out[Z] = final[Z];
}

void multiplyByInvertQuaternion( const vec3_t v1, const quat4_t quat, vec3_t dest)
{
	static quat4_t inv;
	
	inv[0] = -quat[0]; 
	inv[1] = -quat[1];
	inv[2] = -quat[2]; 
	inv[3] =  quat[3];
	
	//printf("joint inverse : x=%.3f,y=%.3f,z=%.3f  w=%.3f \n",inv[0],inv[1],inv[2],inv[3]);				
	
	Quat_rotatePoint (inv,v1,dest);
	
}

void Quat_CreateFromMat3x3(const matrix3x3_t matrix,quat4_t out)
{
	float trace;
	float s;
	
	
	trace = 1.0f + matrix[0] + matrix[4] + matrix[8];
	
	if (trace > 0)
	{
#if DE_USE_FAST_MATH
		s =  MATH_fsrra(trace) * 0.5f;
#else
        s =  1.0f / (2 * sqrt(trace)) ;
#endif
		out[0] = ( matrix[7] - matrix[5] ) * s;
		out[1] = ( matrix[2] - matrix[6] ) * s;
		out[2] = ( matrix[3] - matrix[1] ) * s;
		out[3] = s * 4;
		
	}
	else if ( matrix[0] > matrix[4] && matrix[0] > matrix[8] ) 
	{	// Column 0:
#if DE_USE_FAST_MATH
        s = MATH_fsrra(1.0f + matrix[0] - matrix[4] - matrix[8]) * 0.5f;
#else
		s  = 1.0f / (sqrt( 1.0 + matrix[0] - matrix[4] - matrix[8] ) * 2);
#endif
		out[0] = s * 4;
		out[1] = (matrix[1] + matrix[3] ) * s;
		out[2] = (matrix[2] + matrix[6] ) * s;
		out[3] = (matrix[7] - matrix[5] ) * s;
	} 
	else if ( matrix[4] > matrix[8] ) 
	{	// Column 1:
#if DE_USE_FAST_MATH
        s = MATH_fsrra(1.0f + matrix[4] - matrix[0] - matrix[8]) * 0.5f;
#else
		s  = 1.0f / (sqrt( 1.0 + matrix[4] - matrix[0] - matrix[8] ) * 2);
#endif
		out[0] = (matrix[1] + matrix[3] ) * s;
		out[1] =  s * 4;
		out[2] = (matrix[5] + matrix[7] ) * s;
		out[3] = (matrix[2] - matrix[6] ) * s;
	} 
	else 
	{	// Column 2:
#if DE_USE_FAST_MATH
        s = MATH_fsrra(1.0 + matrix[8] - matrix[0] - matrix[4]) * 0.5f;
#else
		s  = 1.0 / (sqrt( 1.0 + matrix[8] - matrix[0] - matrix[4] ) * 2);
#endif
		out[0] = (matrix[2] + matrix[6] ) * s;
		out[1] = (matrix[5] + matrix[7] ) * s;
		out[2] =  s * 4;
		out[3] = (matrix[3] - matrix[1] ) * s;
		
	}
	
	Quat_normalize(out);
	
}

void Quat_ConvertToMat3x3(matrix3x3_t matrix,const quat4_t out)
{
	matrix[0] = 1-2* (out[Y]*out[Y] + out[Z]*out[Z]);	matrix[1] =   2* (out[X]*out[Y] - out[W]*out[Z]);	matrix[2] =   2* (out[X]*out[Z] + out[W]*out[Y]);
	matrix[3] =   2* (out[X]*out[Y] + out[W]*out[Z]);	matrix[4] = 1-2* (out[X]*out[X] + out[Z]*out[Z]);	matrix[5] =   2* (out[Y]*out[Z] - out[W]*out[X]);
	matrix[6] =   2* (out[X]*out[Z] - out[W]*out[Y]);	matrix[7] =   2* (out[Y]*out[Z] + out[W]*out[X]);	matrix[8] = 1-2* (out[X]*out[X] + out[Y]*out[Y]);	
}