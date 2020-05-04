#include "qcommon.h"
#include "cm_local.h"
#include "cm_trace.h"

#ifdef CM_USE_SSE

/**
 * Create this object in scopes that match boundaries
 * of transition between regular SSE2 and VEX-encoded binary code.
 * Compiling this file using MSVC requires AVX support and the code
 * is VEX-encoded contrary to the rest of the codebase.
 * This fence inserts instructions that help to avoid transition penalties.
 */
struct VexEncodingFence {
#ifdef _MSC_VER
	VexEncodingFence() {
		_mm256_zeroupper();
	}
	~VexEncodingFence() {
		_mm256_zeroupper();
	}
#endif
};

static inline bool CM_BoundsIntersect_SSE42( __m128 traceAbsmins, __m128 traceAbsmaxs,
											 __m128 shapeMins, __m128 shapeMaxs ) {
	__m128 cmp1 = _mm_cmpge_ps( shapeMins, traceAbsmaxs );
	__m128 cmp2 = _mm_cmpge_ps( traceAbsmins, shapeMaxs );
	__m128 orCmp = _mm_or_ps( cmp1, cmp2 );

	return _mm_movemask_epi8( _mm_cmpeq_epi32( _mm_castps_si128( orCmp ), _mm_setzero_si128() ) ) == 0xFFFF;
}

static inline bool CM_BoundsIntersect_SSE42( __m128 traceAbsmins, __m128 traceAbsmaxs,
											 const vec4_t shapeMins, const vec4_t shapeMaxs ) {
	// This version relies on fast unaligned loads, that's why it requires SSE4.
	__m128 xmmShapeMins = _mm_loadu_ps( shapeMins );
	__m128 xmmShapeMaxs = _mm_loadu_ps( shapeMaxs );

	return CM_BoundsIntersect_SSE42( traceAbsmins, traceAbsmaxs, xmmShapeMins, xmmShapeMaxs );
}

static inline bool CM_MightCollide_SSE42( const vec_bounds_t shapeMins,
										  const vec_bounds_t shapeMaxs,
										  const CMTraceContext *tlc ) {
	return CM_BoundsIntersect_SSE42( tlc->xmmAbsmins, tlc->xmmAbsmaxs, shapeMins, shapeMaxs );
}

static inline bool CM_MightCollideInLeaf_SSE42( const vec_bounds_t shapeMins,
												const vec_bounds_t shapeMaxs,
												const vec_bounds_t shapeCenter,
												float shapeRadius,
												const CMTraceContext *tlc ) {
	if( !CM_MightCollide_SSE42( shapeMins, shapeMaxs, tlc ) ) {
		return false;
	}

	// TODO: Vectorize this part. This task is not completed for various reasons.

	vec3_t centerToStart;
	vec3_t proj, perp;

	VectorSubtract( tlc->start, shapeCenter, centerToStart );
	float projMagnitude = DotProduct( centerToStart, tlc->traceDir );
	VectorScale( tlc->traceDir, projMagnitude, proj );
	VectorSubtract( centerToStart, proj, perp );
	float distanceThreshold = shapeRadius + tlc->boxRadius;
	return VectorLengthSquared( perp ) <= distanceThreshold * distanceThreshold;
}

void CMSse42TraceComputer::ClipBoxToLeaf( CMTraceContext *tlc, const cbrush_t *brushes,
										  int numbrushes, const cface_t *markfaces, int nummarkfaces ) {
	volatile VexEncodingFence fence;
	(void)fence;

	// Save the exact address to avoid pointer chasing in loops
	const float *fraction = &tlc->trace->fraction;

	// trace line against all brushes
	for( int i = 0; i < numbrushes; i++ ) {
		const auto *__restrict b = &brushes[i];
		if( !( b->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollideInLeaf_SSE42( b->mins, b->maxs, b->center, b->radius, tlc ) ) {
			continue;
		}
		// Specify the "overridden" method explicitly
		CMSse42TraceComputer::ClipBoxToBrush( tlc, b );
		if( !*fraction ) {
			return;
		}
	}

	// trace line against all patches
	for( int i = 0; i < nummarkfaces; i++ ) {
		const auto *__restrict patch = &markfaces[i];
		if( !( patch->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollideInLeaf_SSE42( patch->mins, patch->maxs, patch->center, patch->radius, tlc ) ) {
			continue;
		}
		for( int j = 0; j < patch->numfacets; j++ ) {
			const auto *__restrict facet = &patch->facets[j];
			if( !CM_MightCollideInLeaf_SSE42( facet->mins, facet->maxs, facet->center, facet->radius, tlc ) ) {
				continue;
			}
			// Specify the "overridden" method explicitly
			CMSse42TraceComputer::ClipBoxToBrush( tlc, facet );
			if( !*fraction ) {
				return;
			}
		}
	}
}

void CMSse42TraceComputer::ClipBoxToBrush( CMTraceContext *tlc, const cbrush_t *brush ) {
	cm_plane_t *p, *clipplane;
	cbrushside_t *side, *leadside;
	float d1, d2, f;

	if( !brush->numsides ) {
		return;
	}

	float enterfrac = -1;
	float leavefrac = 1;
	clipplane = NULL;

	bool getout = false;
	bool startout = false;
	leadside = NULL;
	side = brush->brushsides;

	const float *startmins = tlc->startmins;
	const float *endmins = tlc->endmins;

	for( int i = 0, end = brush->numsides; i < end; i++, side++ ) {
		p = &side->plane;
		int type = p->type;
		float dist = p->dist;

		// push the plane out apropriately for mins/maxs
		if( type < 3 ) {
			d1 = startmins[type] - dist;
			d2 = endmins[type] - dist;
		} else {
			// It looks ugly but its better to inline two "DotProductSSE" calls to group similar ops together
			__m128 *lookup = tlc->xmmClipBoxLookup + p->signbits * 2;
			__m128 xmmNormal = _mm_loadu_ps( p->normal );
			__m128 xmmDot1 = _mm_mul_ps( lookup[0], xmmNormal );
			__m128 xmmDot2 = _mm_mul_ps( lookup[1], xmmNormal );
			// https://stackoverflow.com/a/35270026
			__m128 xmmShuf1 = _mm_movehdup_ps( xmmDot1 );    // broadcast elements 3,1 to 2,0
			__m128 xmmShuf2 = _mm_movehdup_ps( xmmDot2 );
			__m128 xmmSums1 = _mm_add_ps( xmmDot1, xmmShuf1 );
			__m128 xmmSums2 = _mm_add_ps( xmmDot2, xmmShuf2 );
			xmmShuf1 = _mm_movehl_ps( xmmShuf1, xmmSums1 );          // high half -> low half
			xmmShuf2 = _mm_movehl_ps( xmmShuf2, xmmSums2 );
			xmmSums1 = _mm_add_ss( xmmSums1, xmmShuf1 );
			xmmSums2 = _mm_add_ss( xmmSums2, xmmShuf2 );
			d1 = _mm_cvtss_f32( xmmSums1 ) - dist;
			d2 = _mm_cvtss_f32( xmmSums2 ) - dist;
		}

		if( d2 > 0 ) {
			getout = true; // endpoint is not in solid
		}
		if( d1 > 0 ) {
			startout = true;
		}

		// if completely in front of face, no intersection
		if( d1 > 0 && d2 >= d1 ) {
			return;
		}
		if( d1 <= 0 && d2 <= 0 ) {
			continue;
		}
		// crosses face
		f = d1 - d2;
		if( f > 0 ) {   // enter
			f = ( d1 - DIST_EPSILON ) / f;
			if( f > enterfrac ) {
				enterfrac = f;
				clipplane = p;
				leadside = side;
			}
		} else if( f < 0 ) {   // leave
			f = ( d1 + DIST_EPSILON ) / f;
			if( f < leavefrac ) {
				leavefrac = f;
			}
		}
	}

	if( !startout ) {
		// original point was inside brush
		tlc->trace->startsolid = true;
		tlc->trace->contents = brush->contents;
		if( !getout ) {
			tlc->trace->allsolid = true;
			tlc->trace->fraction = 0;
		}
		return;
	}
	if( enterfrac - ( 1.0f / 1024.0f ) <= leavefrac ) {
		if( enterfrac > -1 && enterfrac < tlc->trace->fraction ) {
			if( enterfrac < 0 ) {
				enterfrac = 0;
			}
			tlc->trace->fraction = enterfrac;
			CM_CopyCMToRawPlane( clipplane, &tlc->trace->plane );
			tlc->trace->surfFlags = leadside->surfFlags;
			tlc->trace->contents = brush->contents;
		}
	}
}

void CMSse42TraceComputer::SetupClipContext( CMTraceContext *tlc ) {
	volatile VexEncodingFence fence;
	(void)fence;

	// Note: Using setR is important here, otherwise components order is ... surprising
	// (We're going to compute dot products with vectors loaded via _mm_loadu_ps that preserve array elements order)

	tlc->xmmClipBoxLookup[0] = _mm_setr_ps( tlc->startmins[0], tlc->startmins[1], tlc->startmins[2], 0 );
	tlc->xmmClipBoxLookup[1] = _mm_setr_ps( tlc->endmins[0], tlc->endmins[1], tlc->endmins[2], 0 );

	tlc->xmmClipBoxLookup[2] = _mm_setr_ps( tlc->startmaxs[0], tlc->startmins[1], tlc->startmins[2], 0 );
	tlc->xmmClipBoxLookup[3] = _mm_setr_ps( tlc->endmaxs[0], tlc->endmins[1], tlc->endmins[2], 0 );

	tlc->xmmClipBoxLookup[4] = _mm_setr_ps( tlc->startmins[0], tlc->startmaxs[1], tlc->startmins[2], 0 );
	tlc->xmmClipBoxLookup[5] = _mm_setr_ps( tlc->endmins[0], tlc->endmaxs[1], tlc->endmins[2], 0 );

	tlc->xmmClipBoxLookup[6] = _mm_setr_ps( tlc->startmaxs[0], tlc->startmaxs[1], tlc->startmins[2], 0 );
	tlc->xmmClipBoxLookup[7] = _mm_setr_ps( tlc->endmaxs[0], tlc->endmaxs[1], tlc->endmins[2], 0 );

	tlc->xmmClipBoxLookup[8] = _mm_setr_ps( tlc->startmins[0], tlc->startmins[1], tlc->startmaxs[2], 0 );
	tlc->xmmClipBoxLookup[9] = _mm_setr_ps( tlc->endmins[0], tlc->endmins[1], tlc->endmaxs[2], 0 );

	tlc->xmmClipBoxLookup[10] = _mm_setr_ps( tlc->startmaxs[0], tlc->startmins[1], tlc->startmaxs[2], 0 );
	tlc->xmmClipBoxLookup[11] = _mm_setr_ps( tlc->endmaxs[0], tlc->endmins[1], tlc->endmaxs[2], 0 );

	tlc->xmmClipBoxLookup[12] = _mm_setr_ps( tlc->startmins[0], tlc->startmaxs[1], tlc->startmaxs[2], 0 );
	tlc->xmmClipBoxLookup[13] = _mm_setr_ps( tlc->endmins[0], tlc->endmaxs[1], tlc->endmaxs[2], 0 );

	tlc->xmmClipBoxLookup[14] = _mm_setr_ps( tlc->startmaxs[0], tlc->startmaxs[1], tlc->startmaxs[2], 0 );
	tlc->xmmClipBoxLookup[15] = _mm_setr_ps( tlc->endmaxs[0], tlc->endmaxs[1], tlc->endmaxs[2], 0 );
}

void CMSse42TraceComputer::SetupCollideContext( CMTraceContext *tlc, trace_t *tr,
												const vec_t *start, const vec3_t end,
												const vec3_t mins, const vec3_t maxs, int brushmask ) {
	CMTraceComputer::SetupCollideContext( tlc, tr, start, end, mins, maxs, brushmask );
	// Put the fence after the super method call (that does not use VEX encoding)
	volatile VexEncodingFence fence;
	(void)fence;

	// Always set xmm trace bounds since it is used by all code paths, leaf-optimized and generic
	tlc->xmmAbsmins = _mm_setr_ps( tlc->absmins[0], tlc->absmins[1], tlc->absmins[2], 0 );
	tlc->xmmAbsmaxs = _mm_setr_ps( tlc->absmaxs[0], tlc->absmaxs[1], tlc->absmaxs[2], 1 );
}

void CMSse42TraceComputer::BuildShapeList( CMShapeList *list, const float *mins, const float *maxs, int clipMask ) {
	int leafNums[1024], topNode;
	// TODO: This can be optimized
	const int numLeaves = CM_BoxLeafnums( cms, mins, maxs, leafNums, 1024, &topNode );

	int numShapes = 0;
	const auto *leaves = cms->map_leafs;

	__m128 testedMins = _mm_setr_ps( mins[0], mins[1], mins[2], 0 );
	__m128 testedMaxs = _mm_setr_ps( maxs[0], maxs[1], maxs[2], 1 );

	auto *__restrict destShapes = list->shapes;
	for( int i = 0; i < numLeaves; ++i ) {
		const auto *__restrict leaf = &leaves[leafNums[i]];
		const auto *brushes = leaf->brushes;
		for( int j = 0; j < leaf->numbrushes; ++j ) {
			const auto *__restrict b = &brushes[j];
			if( !( b->contents & clipMask ) ) {
				continue;
			}
			if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, b->mins, b->maxs ) ) {
				continue;
			}
			destShapes[numShapes++] = b;
		}

		const auto *faces = leaf->faces;
		for( int j = 0; j < leaf->numfaces; ++j ) {
			const auto *__restrict f = &faces[j];
			if( !( f->contents & clipMask ) ) {
				continue;
			}
			if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, f->mins, f->maxs ) ) {
				continue;
			}
			for( int k = 0; k < f->numfacets; ++k ) {
				const auto *__restrict b = &f->facets[k];
				if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, b->mins, b->maxs ) ) {
					continue;
				}
				destShapes[numShapes++] = b;
			}
		}
	}

	list->hasBounds = false;
	list->numShapes = numShapes;
}

void CMSse42TraceComputer::ClipShapeList( CMShapeList *list, const CMShapeList *baseList, const float *mins, const float *maxs ) {
	const int numSrcShapes = baseList->numShapes;
	const auto *srcShapes = baseList->shapes;

	int numDestShapes = 0;
	auto *destShapes = list->shapes;

	__m128 testedMins = _mm_setr_ps( mins[0], mins[1], mins[2], 0 );
	__m128 testedMaxs = _mm_setr_ps( maxs[0], maxs[1], maxs[2], 1 );

	BoundsBuilder builder;
	for( int i = 0; i < numSrcShapes; ++i ) {
		const cbrush_t *__restrict b = srcShapes[i];
		__m128 shapeMins = _mm_loadu_ps( b->mins );
		__m128 shapeMaxs = _mm_loadu_ps( b->maxs );
		if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, shapeMins, shapeMaxs ) ) {
			continue;
		}

		destShapes[numDestShapes++] = b;
		builder.addPoint( shapeMins );
		builder.addPoint( shapeMaxs );
	}

	list->numShapes = numDestShapes;
	list->hasBounds = numDestShapes > 0;
	if( list->hasBounds ) {
		builder.storeTo( list->mins, list->maxs );
	}
}

void CMSse42TraceComputer::ClipToShapeList( const CMShapeList *list, trace_t *tr,
					  const float *start, const float *end,
					  const float *mins, const float *maxs, int clipMask ) {
	alignas( 16 ) CMTraceContext tlc;
	SetupCollideContext( &tlc, tr, start, end, mins, maxs, clipMask );

	if( list->hasBounds ) {
		if( !BoundsIntersect( list->mins, list->maxs, tlc.absmins, tlc.absmaxs ) ) {
			assert( tr->fraction == 1.0f );
			VectorCopy( end, tr->endpos );
			return;
		}
	}

	// Make sure the virtual call address gets resolved here
	auto clipFn = &CMSse42TraceComputer::ClipBoxToBrush;
	if( !VectorCompare( start, end ) ) {
		SetupClipContext( &tlc );
	} else {
		clipFn = &CMTraceComputer::TestBoxInBrush;
	}

	const int numShapes = list->numShapes;
	const auto *__restrict shapes = list->shapes;
	float *const __restrict fraction = &tlc.trace->fraction;
	for( int i = 0; i < numShapes; ++i ) {
		const cbrush_t *__restrict b = shapes[i];
		if( !CM_BoundsIntersect_SSE42( tlc.xmmAbsmins, tlc.xmmAbsmaxs, b->mins, b->maxs ) ) {
			continue;
		}
		( this->*clipFn )( &tlc, b );
		if( !*fraction ) {
			break;
		}
	}

	if( tr->fraction == 1.0f ) {
		VectorCopy( end, tr->endpos );
	} else {
		VectorLerp( start, tr->fraction, end, tr->endpos );
#ifdef TRACE_NOAXIAL
		if( PlaneTypeForNormal( tr->plane.normal ) == PLANE_NONAXIAL ) {
			VectorMA( tr->endpos, TRACE_NOAXIAL_SAFETY_OFFSET, tr->plane.normal, tr->endpos );
		}
#endif
	}
}

#endif