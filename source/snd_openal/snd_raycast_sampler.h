#ifndef QFUSION_SND_RAYCAST_SAMPLER_H
#define QFUSION_SND_RAYCAST_SAMPLER_H

#include "snd_local.h"

class GenericRaycastSampler {
	vec3_t *primaryRayDirs;
	vec3_t *primaryHitPoints;
	mutable float *primaryHitDistances;

protected:
	unsigned numRaysHitSky;
	unsigned numRaysHitWater;
	unsigned numRaysHitMetal;
	unsigned numPrimaryRays;
	unsigned numPrimaryHits;
	float averageDistance;

	vec3_t emissionOrigin;

	virtual float GetEmissionRadius() const {
		return 999999.9f;
	}

	float ComputePrimaryHitDistanceStdDev() const;

	void EmitPrimaryRays();

	inline float ComputeGenericHitFactor( float value ) const {
		assert( value <= numPrimaryRays );
		return sqrtf( value / (float)numPrimaryRays );
	}

	inline float ComputeMetalFactor() const { return ComputeGenericHitFactor( numRaysHitMetal ); }
	inline float ComputeWaterFactor() const { return ComputeGenericHitFactor( numRaysHitWater ); }
	inline float ComputeSkyFactor() const { return ComputeGenericHitFactor( numRaysHitSky ); }
	float ComputeRoomSizeFactor() const;

	void ResetMutableState( vec3_t *primaryRayDirs_,
							vec3_t *primaryHitPoints_,
							float *primaryHitDistances_,
							const vec3_t emissionOrigin_ ) {
		numRaysHitSky = 0;
		numRaysHitWater = 0;
		numRaysHitMetal = 0;
		numPrimaryHits = 0;
		averageDistance = 0.0f;

		this->primaryRayDirs = primaryRayDirs_;
		this->primaryHitPoints = primaryHitPoints_;
		this->primaryHitDistances = primaryHitDistances_;

		VectorCopy( emissionOrigin_, this->emissionOrigin );
	}

	static void SetupSamplingRayDirs( vec3_t *rayDirs, unsigned numRays );
};

#endif
