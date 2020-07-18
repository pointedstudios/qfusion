#ifndef QFUSION_SND_RAYCAST_SAMPLER_H
#define QFUSION_SND_RAYCAST_SAMPLER_H

#include "snd_local.h"

class GenericRaycastSampler {
	vec3_t *primaryRayDirs;
	vec3_t *primaryHitPoints;
	mutable float *primaryHitDistances;

protected:
	unsigned numPrimaryRays { 0 };
	unsigned numPrimaryHits { 0 };
	float averageDistance { 0.0f };

	vec3_t emissionOrigin { 0.0f, 0.0f, 0.0f };

	virtual float GetEmissionRadius() const {
		return 999999.9f;
	}

	float ComputePrimaryHitDistanceStdDev() const;

	void EmitPrimaryRays();

	[[nodiscard]]
	virtual bool CheckAndAddHitSurfaceProps( const trace_t &trace );

	float ComputeRoomSizeFactor() const;

	void ResetMutableState( vec3_t *primaryRayDirs_,
							vec3_t *primaryHitPoints_,
							float *primaryHitDistances_,
							const vec3_t emissionOrigin_ ) {
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
