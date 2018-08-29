#include "snd_effect_sampler.h"
#include "snd_leaf_props_cache.h"
#include "snd_effects_allocator.h"

#include "../gameshared/q_collision.h"

#include <limits>
#include <random>

static UnderwaterFlangerEffectSampler underwaterFlangerEffectSampler;

static ReverbEffectSampler reverbEffectSampler;

Effect *EffectSamplers::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	Effect *effect;
	if( ( effect = ::underwaterFlangerEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) ) {
		return effect;
	}
	if( ( effect = ::reverbEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) ) {
		return effect;
	}

	trap_Error( "Can't find an applicable effect sampler\n" );
}

// We want sampling results to be reproducible especially for leaf sampling and thus use this local implementation
static std::minstd_rand0 samplingRandom;

float EffectSamplers::SamplingRandom() {
	typedef decltype( samplingRandom ) R;
	return ( samplingRandom() - R::min() ) / (float)( R::max() - R::min() );
}

Effect *UnderwaterFlangerEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t * ) {
	if( !listenerProps.isInLiquid && !src->envUpdateState.isInLiquid ) {
		return nullptr;
	}

	float directObstruction = 0.9f;
	if( src->envUpdateState.isInLiquid && listenerProps.isInLiquid ) {
		directObstruction = ComputeDirectObstruction( listenerProps, src );
	}

	auto *effect = EffectsAllocator::Instance()->NewFlangerEffect( src );
	effect->directObstruction = directObstruction;
	effect->hasMediumTransition = src->envUpdateState.isInLiquid ^ listenerProps.isInLiquid;
	return effect;
}

static bool ENV_TryReuseSourceReverbProps( src_t *src, const src_t *tryReusePropsSrc, ReverbEffect *newEffect ) {
	if( !tryReusePropsSrc ) {
		return false;
	}

	auto *reuseEffect = Effect::Cast<const ReverbEffect *>( tryReusePropsSrc->envUpdateState.effect );
	if( !reuseEffect ) {
		return false;
	}

	// We are already sure that both sources are in the same contents kind (non-liquid).
	// Check distance between sources.
	const float squareDistance = DistanceSquared( tryReusePropsSrc->origin, src->origin );
	// If they are way too far for reusing
	if( squareDistance > 96 * 96 ) {
		return false;
	}

	// If they are very close, feel free to just copy props
	if( squareDistance < 4.0f * 4.0f ) {
		newEffect->CopyReverbProps( reuseEffect );
		return true;
	}

	// Do a coarse raycast test between these two sources
	vec3_t start, end, dir;
	VectorSubtract( tryReusePropsSrc->origin, src->origin, dir );
	const float invDistance = 1.0f / sqrtf( squareDistance );
	VectorScale( dir, invDistance, dir );
	// Offset start and end by a dir unit.
	// Ensure start and end are in "air" and not on a brush plane
	VectorAdd( src->origin, dir, start );
	VectorSubtract( tryReusePropsSrc->origin, dir, end );

	trace_t trace;
	trap_Trace( &trace, start, end, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	newEffect->CopyReverbProps( reuseEffect );
	return true;
}

void ObstructedEffectSampler::SetupDirectObstructionSamplingProps( src_t *src, unsigned minSamples, unsigned maxSamples ) {
	float quality = s_environment_sampling_quality->value;
	samplingProps_t *props = &src->envUpdateState.directObstructionSamplingProps;

	// If the quality is valid and has not been modified since the pattern has been set
	if( props->quality == quality ) {
		return;
	}

	unsigned numSamples = GetNumSamplesForCurrentQuality( minSamples, maxSamples );

	props->quality = quality;
	props->numSamples = numSamples;
	props->valueIndex = (uint16_t)( EffectSamplers::SamplingRandom() * std::numeric_limits<uint16_t>::max() );
}

struct DirectObstructionOffsetsHolder {
	enum { NUM_VALUES = 256 };
	vec3_t offsets[NUM_VALUES];

	DirectObstructionOffsetsHolder() {
		for( auto *v: offsets ) {
			for( int i = 0; i < 3; ++i ) {
				v[i] = -20.0f + 40.0f * EffectSamplers::SamplingRandom();
			}
		}
	}
};

static DirectObstructionOffsetsHolder directObstructionOffsetsHolder;

float ObstructedEffectSampler::ComputeDirectObstruction( const ListenerProps &listenerProps, src_t *src ) {
	trace_t trace;
	envUpdateState_t *updateState;
	float *originOffset;
	vec3_t testedListenerOrigin;
	vec3_t testedSourceOrigin;
	float squareDistance;
	unsigned numTestedRays, numPassedRays;
	unsigned i, valueIndex;

	updateState = &src->envUpdateState;

	VectorCopy( listenerProps.origin, testedListenerOrigin );
	// TODO: We assume standard view height
	testedListenerOrigin[2] += 18.0f;

	squareDistance = DistanceSquared( testedListenerOrigin, src->origin );
	// Shortcut for sounds relative to the player
	if( squareDistance < 32.0f * 32.0f ) {
		return 0.0f;
	}

	if( !trap_LeafsInPVS( listenerProps.GetLeafNum(), trap_PointLeafNum( src->origin ) ) ) {
		return 1.0f;
	}

	trap_Trace( &trace, testedListenerOrigin, src->origin, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction == 1.0f && !trace.startsolid ) {
		// Consider zero obstruction in this case
		return 0.0f;
	}

	SetupDirectObstructionSamplingProps( src, 3, MAX_DIRECT_OBSTRUCTION_SAMPLES );

	numPassedRays = 0;
	numTestedRays = updateState->directObstructionSamplingProps.numSamples;
	valueIndex = updateState->directObstructionSamplingProps.valueIndex;
	for( i = 0; i < numTestedRays; i++ ) {
		valueIndex = ( valueIndex + 1 ) % DirectObstructionOffsetsHolder::NUM_VALUES;
		originOffset = directObstructionOffsetsHolder.offsets[ valueIndex ];

		VectorAdd( src->origin, originOffset, testedSourceOrigin );
		trap_Trace( &trace, testedListenerOrigin, testedSourceOrigin, vec3_origin, vec3_origin, MASK_SOLID );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedRays++;
		}
	}

	return 1.0f - 0.9f * ( numPassedRays / (float)numTestedRays );
}

Effect *ReverbEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	ReverbEffect *effect = EffectsAllocator::Instance()->NewReverbEffect( src );
	effect->directObstruction = ComputeDirectObstruction( listenerProps, src );
	// We try reuse props only for reverberation effects
	// since reverberation effects sampling is extremely expensive.
	// Moreover, direct obstruction reuse is just not valid,
	// since even a small origin difference completely changes it.
	if( ENV_TryReuseSourceReverbProps( src, tryReusePropsSrc, effect ) ) {
		src->envUpdateState.needsInterpolation = false;
	} else {
		ComputeReverberation( listenerProps, src, effect );
	}
	return effect;
}

float ReverbEffectSampler::GetEmissionRadius() const {
	// Do not even bother casting rays 999999 units ahead for very attenuated sources.
	// However, clamp/normalize the hit distance using the same defined threshold
	float attenuation = src->attenuation;

	if( attenuation <= 1.0f ) {
		return 999999.9f;
	}

	clamp_high( attenuation, 10.0f );
	float distance = 4.0f * REVERB_ENV_DISTANCE_THRESHOLD;
	distance -= 3.5f * SQRTFAST( attenuation / 10.0f ) * REVERB_ENV_DISTANCE_THRESHOLD;
	return distance;
}

void ReverbEffectSampler::ResetMutableState( const ListenerProps &listenerProps_, src_t *src_, ReverbEffect *effect_ ) {
	this->listenerProps = &listenerProps_;
	this->src = src_;
	this->effect = effect_;

	GenericRaycastSampler::ResetMutableState( primaryRayDirs, reflectionPoints, primaryHitDistances, src->origin );

	VectorCopy( listenerProps_.origin, testedListenerOrigin );
	testedListenerOrigin[2] += 18.0f;
}

void ReverbEffectSampler::ComputeReverberation( const ListenerProps &listenerProps_,
												src_t *src_,
												ReverbEffect *effect_ ) {
	ResetMutableState( listenerProps_, src_, effect_ );

	// A "realistic obstruction" requires a higher amount of secondary (and thus primary) rays
	if( s_realistic_obstruction->integer ) {
		numPrimaryRays = GetNumSamplesForCurrentQuality( 16, MAX_REVERB_PRIMARY_RAY_SAMPLES );
	} else {
		numPrimaryRays = GetNumSamplesForCurrentQuality( 16, MAX_REVERB_PRIMARY_RAY_SAMPLES / 2 );
	}

	SetupPrimaryRayDirs();

	EmitPrimaryRays();

	if( !numPrimaryHits ) {
		SetMinimalReverbProps();
		return;
	}

	ProcessPrimaryEmissionResults();
	EmitSecondaryRays();
}

void ReverbEffectSampler::SetupPrimaryRayDirs() {
	assert( numPrimaryRays );

	SetupSamplingRayDirs( primaryRayDirs, numPrimaryRays );
}

void ReverbEffectSampler::ProcessPrimaryEmissionResults() {
	// Instead of trying to compute these factors every sampling call,
	// reuse pre-computed properties of CM map leafs that briefly resemble rooms/convex volumes.
	assert( src->envUpdateState.leafNum >= 0 );
	const LeafProps &leafProps = LeafPropsCache::Instance()->GetPropsForLeaf( src->envUpdateState.leafNum );

	const float roomSizeFactor = leafProps.RoomSizeFactor();
	const float metalFactor = leafProps.MetalFactor();
	const float skyFactor = leafProps.SkyFactor();

	// It should be default.
	// Secondary rays obstruction is the only modulation we apply.
	// See EmitSecondaryRays()
	effect->gain = 0.32f;

	effect->density = 1.0f - 0.7f * metalFactor;

	effect->diffusion = 1.0f;
	if( !skyFactor ) {
		effect->diffusion = 1.0f - 0.7f * roomSizeFactor;
	}

	effect->decayTime = 0.60f + 2.0f * roomSizeFactor + 0.5f * skyFactor;
	assert( effect->decayTime <= EaxReverbEffect::MAX_REVERB_DECAY );

	// Let reflections (early reverb) gain be larger for small rooms
	const float reflectionsGainFactor = ( 1.0f - roomSizeFactor ) * ( 1.0f - roomSizeFactor ) * ( 1.0f - roomSizeFactor );
	assert( reflectionsGainFactor >= 0.0f && reflectionsGainFactor <= 1.0f );
	// Let late reverb gain be larger for huge rooms
	const float lateReverbGainFactor = roomSizeFactor * roomSizeFactor;
	assert( lateReverbGainFactor >= 0.0f && lateReverbGainFactor <= 1.0f );

	effect->lateReverbGain = 0.125f + 0.105f * lateReverbGainFactor * ( 1.0f - 0.7f * skyFactor );
	effect->reflectionsGain = 0.05f + 0.75f * reflectionsGainFactor;

	if( !skyFactor ) {
		// Hack: try to detect sewers/caves
		if( leafProps.WaterFactor() ) {
			effect->reflectionsGain += 0.50f + 0.75f * reflectionsGainFactor;
			effect->lateReverbGain += 0.50f * lateReverbGainFactor;
		}
	}

	// Keep it default... it's hard to tweak
	effect->reflectionsDelay = 0.007f;
	effect->lateReverbDelay = 0.011f + 0.088f * roomSizeFactor;

	if( auto *eaxEffect = Effect::Cast<EaxReverbEffect *>( effect ) ) {
		if( skyFactor ) {
			eaxEffect->echoTime = 0.075f + 0.125f * roomSizeFactor;
			// Raise echo depth until sky factor reaches 0.5f, then lower it.
			// So echo depth is within [0.25f, 0.5f] bounds and reaches its maximum at skyFactor = 0.5f
			if( skyFactor < 0.5f ) {
				eaxEffect->echoDepth = 0.25f + 0.5f * 2.0f * skyFactor;
			} else {
				eaxEffect->echoDepth = 0.75f - 0.3f * 2.0f * ( skyFactor - 0.5f );
			}
		} else {
			eaxEffect->echoTime = 0.25f;
			eaxEffect->echoDepth = 0.0f;
		}
	}
}

void ReverbEffectSampler::SetMinimalReverbProps() {
	effect->gain = 0.1f;
	effect->density = 1.0f;
	effect->diffusion = 1.0f;
	effect->decayTime = 0.60f;
	effect->reflectionsGain = 0.05f;
	effect->reflectionsDelay = 0.007f;
	effect->lateReverbGain = 0.15f;
	effect->lateReverbDelay = 0.011f;
	effect->gainHf = 0.0f;
	if( auto *eaxEffect = Effect::Cast<EaxReverbEffect *>( effect ) ) {
		eaxEffect->echoTime = 0.25f;
		eaxEffect->echoDepth = 0.0f;
	}
}

void ReverbEffectSampler::EmitSecondaryRays() {
	int listenerLeafNum = listenerProps->GetLeafNum();

	auto *const eaxEffect = Effect::Cast<EaxReverbEffect *>( effect );
	auto *const panningUpdateState = &src->panningUpdateState;

	trace_t trace;

	unsigned numPassedSecondaryRays = 0;
	if( eaxEffect ) {
		panningUpdateState->numReflectionPoints = 0;
		for( unsigned i = 0; i < numPrimaryHits; i++ ) {
			// Cut off by PVS system early, we are not interested in actual ray hit points contrary to the primary emission.
			if( !trap_LeafsInPVS( listenerLeafNum, trap_PointLeafNum( reflectionPoints[i] ) ) ) {
				continue;
			}

			trap_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction == 1.0f && !trace.startsolid ) {
				numPassedSecondaryRays++;
				float *savedPoint = panningUpdateState->reflectionPoints[panningUpdateState->numReflectionPoints++];
				VectorCopy( reflectionPoints[i], savedPoint );
			}
		}
	} else {
		for( unsigned i = 0; i < numPrimaryHits; i++ ) {
			if( !trap_LeafsInPVS( listenerLeafNum, trap_PointLeafNum( reflectionPoints[i] ) ) ) {
				continue;
			}

			trap_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction == 1.0f && !trace.startsolid ) {
				numPassedSecondaryRays++;
			}
		}
	}

	if( numPrimaryHits ) {
		float frac = numPassedSecondaryRays / (float)numPrimaryHits;
		// The secondary rays obstruction is complement to the `frac`
		effect->secondaryRaysObstruction = 1.0f - frac;
		// A absence of a HF attenuation sounds poor, metallic/reflective environments should be the only exception.
		const LeafProps &leafProps = ::LeafPropsCache::Instance()->GetPropsForLeaf( src->envUpdateState.leafNum );
		effect->gainHf = ( 0.4f + 0.5f * leafProps.MetalFactor() ) * frac;
		// We also modify effect gain by a fraction of secondary rays passed to listener.
		// This is not right in theory, but is inevitable in the current game sound model
		// where you can hear across the level through solid walls
		// in order to avoid messy echoes coming from everywhere.
		effect->gain *= 0.75f + 0.25f * frac;
	} else {
		// Set minimal feasible values
		effect->secondaryRaysObstruction = 1.0f;
		effect->gainHf = 0.0f;
		effect->gain *= 0.5f;
	}
}