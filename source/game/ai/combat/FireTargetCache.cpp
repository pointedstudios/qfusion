#include "FireTargetCache.h"
#include "../navigation/AasElementsMask.h"
#include "../navigation/AasAreasWalker.h"
#include "../ai_trajectory_predictor.h"
#include "../bot.h"

class ClosestFacePointSolver final : public SharedFaceAreasWalker<ArrayBasedFringe<>> {
	const aas_vertex_t *const __restrict aasVertices;
	const aas_edge_t *const __restrict aasEdges;
	const aas_edgeindex_t *const __restrict aasEdgeIndex;
	const aas_plane_t *const __restrict aasPlanes;

	edict_t *const ignoreEnt;
	edict_t *const targetEnt;
	const float *result { nullptr };

	Vec3 candidatePoint { 0, 0, 0 };
	Vec3 fireOrigin;
	Vec3 fireTarget;
	const float splashRadius;

	bool ProcessAreaTransition( int currArea, int nextArea, const aas_face_t *face ) override;
public:
	ClosestFacePointSolver( const float *fireOrigin_,
							const float *fireTarget_,
							float splashRadius_,
							const edict_t *ignoreEnt_,
							const edict_t *targetEnt_ )
		: SharedFaceAreasWalker(
			AiAasWorld::Instance()->FindAreaNum( fireTarget_ ),
			AasElementsMask::AreasMask(),
			AasElementsMask::FacesMask() )
		, aasVertices( aasWorld->Vertexes() )
		, aasEdges( aasWorld->Edges() )
		, aasEdgeIndex( aasWorld->EdgeIndex() )
		, aasPlanes( aasWorld->Planes() )
		, ignoreEnt( const_cast<edict_t *>( ignoreEnt_ ) )
		, targetEnt( const_cast<edict_t *>( targetEnt_ ) )
		, fireOrigin( fireOrigin_ )
		, fireTarget( fireTarget_ )
		, splashRadius( splashRadius_ ) {}

	void Exec() override;

	const float *Result() const { return result; }
};

bool ClosestFacePointSolver::ProcessAreaTransition( int currArea, int nextArea, const aas_face_t *face ) {
	const auto *__restrict facePlane = aasPlanes + face->planenum;
	// Check whether the face is within the splash radius
	if( std::fabs( fireTarget.Dot( facePlane->normal ) - facePlane->dist ) > splashRadius ) {
		return true;
	}

	// If there is a non-solid area behind face
	if( nextArea ) {
		if( visitedAreas->TrySet( nextArea ) ) {
			queue.Add( nextArea );
		}
		return true;
	}

	// TODO: Using raycasts for closest face point determination is pretty bad but operating on AAS edge is error-prone...

	Vec3 testedOffsetVec( facePlane->normal );
	testedOffsetVec *= -splashRadius;
	Vec3 testedPoint( testedOffsetVec );
	testedPoint += fireTarget;

	trace_t trace;
	SolidWorldTrace( &trace, fireTarget.Data(), testedPoint.Data() );
	// If we can't find a point on a face in the solid world
	if( trace.fraction == 1.0f ) {
		return true;
	}

	candidatePoint.Set( testedOffsetVec );
	candidatePoint *= trace.fraction;
	candidatePoint += fireTarget;

	// Set result as defined by default
	result = candidatePoint.Data();

	G_Trace( &trace, fireOrigin.Data(), nullptr, nullptr, candidatePoint.Data(), ignoreEnt, MASK_AISOLID );

	// If there's no obstacles between the fire target and the candidate point
	if( trace.fraction == 1.0f ) {
		return false;
	}

	// If we hit the target directly while trying to check contents between the fire target and the candidate point
	if( targetEnt == game.edicts + trace.ent ) {
		return false;
	}

	// We might hit solid as the suggested point is probably at a solid surface.
	// Make sure we really have hit the solid world closely to the suggested point.
	if( candidatePoint.SquareDistanceTo( trace.endpos ) < 8 * 8 ) {
		return false;
	}

	// Reset the result address on failure
	result = nullptr;
	// Continue testing candidates
	return true;
}

void ClosestFacePointSolver::Exec() {
	// If the start area num is zero
	if( !queue.Peek() ) {
		return;
	}

	SharedFaceAreasWalker::Exec();
}

void BotFireTargetCache::AdjustAimParams( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
										  const GenericFireDef &fireDef, AimParams *aimParams ) {
	SetupCoarseFireTarget( selectedEnemies, fireDef, aimParams->fireOrigin, aimParams->fireTarget );

	switch( fireDef.AimType() ) {
		case AI_WEAPON_AIM_TYPE_PREDICTION_EXPLOSIVE:
			AdjustPredictionExplosiveAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
		case AI_WEAPON_AIM_TYPE_PREDICTION:
			AdjustPredictionAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
		case AI_WEAPON_AIM_TYPE_DROP:
			AdjustDropAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
		default:
			AdjustInstantAimTypeParams( selectedEnemies, selectedWeapons, fireDef, aimParams );
			break;
	}
}

constexpr float GENERIC_PROJECTILE_COORD_AIM_ERROR = 75.0f;
constexpr float GENERIC_INSTANTHIT_COORD_AIM_ERROR = 100.0f;

void BotFireTargetCache::AdjustForShootableEnvironment( const SelectedEnemies &selectedEnemies,
														float splashRaidus,
														AimParams *aimParams ) {
	const edict_t *ignoreEnt = game.edicts + bot->EntNum();
	const edict_t *targetEnt = selectedEnemies.TraceKey();
	ClosestFacePointSolver solver( aimParams->fireOrigin, aimParams->fireTarget, splashRaidus, ignoreEnt, targetEnt );
	solver.Exec();
	if( const float *p = solver.Result() ) {
		VectorCopy( p, aimParams->fireTarget );
	}
}

void BotFireTargetCache::AdjustPredictionExplosiveAimTypeParams( const SelectedEnemies &selectedEnemies,
																 const SelectedWeapons &selectedWeapons,
																 const GenericFireDef &fireDef,
																 AimParams *aimParams ) {
	bool wasCached = cachedFireTarget.IsValidFor( selectedEnemies, selectedWeapons );
	GetPredictedTargetOrigin( selectedEnemies, selectedWeapons, fireDef.ProjectileSpeed(), aimParams );
	// If new generic predicted target origin has been computed, adjust it for target environment
	if( !wasCached ) {
		// First, modify temporary `target` value
		AdjustForShootableEnvironment( selectedEnemies, fireDef.SplashRadius(), aimParams );
		// Copy modified `target` value to cached value
		cachedFireTarget.origin.Set( aimParams->fireTarget );
	}
	// Accuracy for air rockets is worse anyway (movement prediction in gravity field is approximate)
	aimParams->suggestedBaseCoordError = 1.3f * ( 1.01f - bot->Skill() ) * GENERIC_PROJECTILE_COORD_AIM_ERROR;
}


void BotFireTargetCache::AdjustPredictionAimTypeParams( const SelectedEnemies &selectedEnemies,
														const SelectedWeapons &selectedWeapons,
														const GenericFireDef &fireDef,
														AimParams *aimParams ) {
	aimParams->suggestedBaseCoordError = GENERIC_PROJECTILE_COORD_AIM_ERROR;
	if( fireDef.IsBuiltin() ) {
		if( fireDef.WeaponNum() == WEAP_PLASMAGUN ) {
			aimParams->suggestedBaseCoordError *= 0.5f * ( 1.0f - bot->Skill() );
		} else if( fireDef.WeaponNum() == WEAP_ELECTROBOLT ) {
			// This is not a mistake, the code is for projectile EB.
			// Do not apply any error in this case (set it to some very low feasible value)
			aimParams->suggestedBaseCoordError = 1.0f;
		}
	}

	GetPredictedTargetOrigin( selectedEnemies, selectedWeapons, fireDef.ProjectileSpeed(), aimParams );
}

void BotFireTargetCache::AdjustDropAimTypeParams( const SelectedEnemies &selectedEnemies,
												  const SelectedWeapons &selectedWeapons,
												  const GenericFireDef &fireDef,
												  AimParams *aimParams ) {
	// This kind of weapons is not precise by its nature, do not add any more noise.
	aimParams->suggestedBaseCoordError = 0.3f * ( 1.01f - bot->Skill() ) * GENERIC_PROJECTILE_COORD_AIM_ERROR;

	const bool wasCached = cachedFireTarget.IsValidFor( selectedEnemies, selectedWeapons );
	GetPredictedTargetOrigin( selectedEnemies, selectedWeapons, fireDef.ProjectileSpeed(), aimParams );
	if( wasCached ) {
		return;
	}

	// If new generic predicted target origin has been computed, adjust it for gravity (changes will be cached)
	// This is not very accurate but satisfactory
	Vec3 fireOriginToTarget = Vec3( aimParams->fireTarget ) - aimParams->fireOrigin;
	Vec3 fireOriginToTarget2D( fireOriginToTarget.X(), fireOriginToTarget.Y(), 0 );
	const float squareDistance2D = fireOriginToTarget2D.SquaredLength();
	if( squareDistance2D < 8 * 8 ) {
		return;
	}

	Vec3 velocity2DVec( fireOriginToTarget );
	velocity2DVec.NormalizeFast();
	velocity2DVec *= fireDef.ProjectileSpeed();
	velocity2DVec.Z() = 0;
	const float squareVelocity2D = velocity2DVec.SquaredLength();
	if( squareVelocity2D < 32 * 32 ) {
		return;
	}

	const float distance2D = SQRTFAST( squareDistance2D );
	const float velocity2D = SQRTFAST( squareVelocity2D );
	const float time = distance2D / velocity2D;
	const float height = std::max( 0.0f, 0.5f * level.gravity * time * time - 32.0f );

	// Modify both cached and temporary values
	cachedFireTarget.origin.Z() += height;
	aimParams->fireTarget[2] += height;
}

void BotFireTargetCache::AdjustInstantAimTypeParams( const SelectedEnemies &selectedEnemies,
													 const SelectedWeapons &selectedWeapons,
													 const GenericFireDef &fireDef, AimParams *aimParams ) {
	aimParams->suggestedBaseCoordError = GENERIC_INSTANTHIT_COORD_AIM_ERROR;
}

void BotFireTargetCache::SetupCoarseFireTarget( const SelectedEnemies &selectedEnemies,
												const GenericFireDef &fireDef,
												vec3_t fire_origin, vec3_t target ) {
	// For hard bots use actual enemy origin
	// (last seen one may be outdated up to 3 frames, and it matter a lot for fast-moving enemies)
	if( bot->Skill() < 0.66f ) {
		VectorCopy( selectedEnemies.LastSeenOrigin().Data(), target );
	} else {
		VectorCopy( selectedEnemies.ActualOrigin().Data(), target );
	}

	// For hitscan weapons we try to imitate a human-like aiming.
	// We get a weighted last seen enemy origin/velocity and extrapolate origin a bit.
	// Do not add extra aiming error for other aim styles (these aim styles are not precise by their nature).
	if( fireDef.AimType() == AI_WEAPON_AIM_TYPE_INSTANT_HIT ) {
		AddHitscanAimingError( selectedEnemies, target );
	}

	const edict_t *self = game.edicts + bot->EntNum();

	fire_origin[0] = self->s.origin[0];
	fire_origin[1] = self->s.origin[1];
	fire_origin[2] = self->s.origin[2] + self->viewheight;
}

void BotFireTargetCache::AddHitscanAimingError( const SelectedEnemies &selectedEnemies, vec3_t target ) {
	const float skill = bot->Skill();

	vec3_t velocity;
	if( skill < 0.66f ) {
		VectorCopy( selectedEnemies.LastSeenVelocity().Data(), velocity );
	} else {
		VectorCopy( selectedEnemies.ActualVelocity().Data(), velocity );
	}

	const int64_t levelTime = level.time;
	// Skilled bots have this value lesser (this means target will be closer to an actual origin)
	const auto maxTimeDelta = (unsigned)( 900 - 800 * skill );
	const float weightTimeDeltaScale = 1.0f / maxTimeDelta;
	float weightsSum = 1.0f;

	// Iterate from oldest to newest snapshot
	// This is not so bad as reverse iteration steps are more expensive
	for( const auto &snapshot : selectedEnemies.LastSeenSnapshots() ) {
		auto timeDelta = (unsigned)( levelTime - snapshot.Timestamp() );
		if( timeDelta > maxTimeDelta ) {
			continue;
		}

		// Recent snapshots have greater weight
		float weight = 1.0f - timeDelta * weightTimeDeltaScale;
		// We have to store these temporarily unpacked values in locals
		Vec3 snapshotOrigin( snapshot.Origin() );
		Vec3 snapshotVelocity( snapshot.Velocity() );
		// Accumulate snapshot target origin using the weight
		VectorMA( target, weight, snapshotOrigin.Data(), target );
		// Accumulate snapshot target velocity using the weight
		VectorMA( velocity, weight, snapshotVelocity.Data(), velocity );
		weightsSum += weight;
	}

	const float invWeightsSum = 1.0f / weightsSum;
	// Make `target` contain a weighted sum of enemy snapshot origin
	VectorScale( target, invWeightsSum, target );
	// Make `velocity` contain a weighted sum of enemy snapshot velocities
	VectorScale( velocity, invWeightsSum, velocity );

	if( extrapolationRandomTimeoutAt < levelTime ) {
		// Make constant part lesser for higher skill
		extrapolationRandom = random();
		extrapolationRandomTimeoutAt = levelTime + 250;
	}

	const float extrapolationTimeSeconds = 0.001f * ( 900 - 800 * skill ) * extrapolationRandom;
	// Add some extrapolated target movement
	VectorMA( target, extrapolationTimeSeconds, velocity, target );
}

// This is a port of public domain projectile prediction code by Kain Shin
// http://ringofblades.com/Blades/Code/PredictiveAim.cs
// This function assumes that target velocity is constant and gravity is not applied to projectile and target.
bool PredictProjectileNoClip( const Vec3 &fireOrigin, float projectileSpeed, vec3_t target, const Vec3 &targetVelocity ) {
	constexpr float EPSILON = 0.0001f;

	float projectileSpeedSq = projectileSpeed * projectileSpeed;
	float targetSpeedSq = targetVelocity.SquaredLength();
	float targetSpeed = sqrtf( targetSpeedSq );
	Vec3 targetToFire = fireOrigin - target;
	float targetToFireDistSq = targetToFire.SquaredLength();
	float targetToFireDist = sqrtf( targetToFireDistSq );
	Vec3 targetToFireDir( targetToFire );
	targetToFireDir.Normalize();

	Vec3 targetVelocityDir( targetVelocity );
	targetVelocityDir.Normalize();

	float cosTheta = targetToFireDir.Dot( targetVelocityDir );

	float t;
	if( fabsf( projectileSpeedSq - targetSpeedSq ) < EPSILON ) {
		if( cosTheta <= 0 ) {
			return false;
		}

		t = 0.5f * targetToFireDist / ( targetSpeed * cosTheta );
	} else {
		float a = projectileSpeedSq - targetSpeedSq;
		float b = 2.0f * targetToFireDist * targetSpeed * cosTheta;
		float c = -targetToFireDistSq;
		float discriminant = b * b - 4.0f * a * c;

		if( discriminant < 0 ) {
			return false;
		}

		float uglyNumber = sqrtf( discriminant );
		float t0 = 0.5f * ( -b + uglyNumber ) / a;
		float t1 = 0.5f * ( -b - uglyNumber ) / a;
		t = std::min( t0, t1 );
		if( t < EPSILON ) {
			t = std::max( t0, t1 );
		}

		if( t < EPSILON ) {
			return false;
		}
	}

	Vec3 move = targetVelocity * t;
	VectorAdd( target, move.Data(), target );
	return true;
}

void BotFireTargetCache::GetPredictedTargetOrigin( const SelectedEnemies &selectedEnemies,
												   const SelectedWeapons &selectedWeapons,
												   float projectileSpeed, AimParams *aimParams ) {
	if( bot->Skill() < 0.33f || selectedEnemies.IsStaticSpot() ) {
		return;
	}

	// Check whether we are shooting the same enemy and cached predicted origin is not outdated
	if( cachedFireTarget.IsValidFor( selectedEnemies, selectedWeapons ) ) {
		VectorCopy( cachedFireTarget.origin.Data(), aimParams->fireTarget );
	} else {
		PredictProjectileShot( selectedEnemies, projectileSpeed, aimParams, true );
		cachedFireTarget.invalidAt = level.time + 66;
		cachedFireTarget.CacheFor( selectedEnemies, selectedWeapons, aimParams->fireTarget );
	}
}

static inline void TryAimingAtGround( trace_t *trace, AimParams *aimParams, edict_t *traceKey ) {
	// In this case try fallback to the ground below the target.
	Vec3 endPoint( 0, 0, -128 );
	endPoint += aimParams->fireTarget;
	G_Trace( trace, aimParams->fireTarget, nullptr, nullptr, endPoint.Data(), traceKey, MASK_AISOLID );
	if( trace->fraction != 1.0f ) {
		VectorCopy( trace->endpos, aimParams->fireTarget );
		// Add some offset from the ground (enviroment tests probably expect this input).
		aimParams->fireTarget[2] += 1.0f;
	}
}

class HitPointPredictor final: private AiTrajectoryPredictor {
public:
	struct ProblemParams {
		const Vec3 fireOrigin;
		const Vec3 targetVelocity;
		const edict_t *enemyEnt;
		const float projectileSpeed;

		ProblemParams( const vec3_t fireOrigin_, const vec3_t targetVelocity_,
					   const edict_t *enemyEnt_, const float projectileSpeed_ )
			: fireOrigin( fireOrigin_ ), targetVelocity( targetVelocity_ ),
			  enemyEnt( enemyEnt_ ), projectileSpeed( projectileSpeed_ ) {}
	};

	static bool Exec( const ProblemParams &problemParams_, float skill, vec3_t fireTarget_ );
private:
	HitPointPredictor( const ProblemParams &problemParams_, vec3_t fireTarget_ )
		: problemParams( problemParams_ ), fireTarget( fireTarget_ ) {}

	const ProblemParams &problemParams;
	float *const fireTarget;
	bool tryHitInAir { true };
	bool hasFailed { false };

	bool OnPredictionStep( const Vec3 &segmentStart, const Results *results ) override;
};

bool HitPointPredictor::Exec( const ProblemParams &problemParams_, float skill, vec3_t fireTarget_ ) {
	HitPointPredictor predictor( problemParams_, fireTarget_ );

	predictor.SetStepMillis( (unsigned)( 200.0f - 100.0f * skill ) );
	predictor.SetNumSteps( (unsigned)( 6 + 12 * skill ) );

	predictor.SetEntitiesCollisionProps( true, ENTNUM( problemParams_.enemyEnt ) );
	predictor.SetColliderBounds( vec3_origin, playerbox_stand_maxs );
	predictor.AddStopEventFlags( HIT_SOLID );
	predictor.SetExtrapolateLastStep( true );

	Vec3 startOrigin( fireTarget_ );
	AiTrajectoryPredictor::Results baseResults;
	auto stopEvents = predictor.Run( problemParams_.targetVelocity, startOrigin, &baseResults );

	// If a hit point has been found in OnPredictionStep(), a prediction gets interrupted
	return ( stopEvents & INTERRUPTED ) && !predictor.hasFailed;
}

bool HitPointPredictor::OnPredictionStep( const Vec3 &segmentStart, const Results *results ) {
	if( results->trace->fraction != 1.0f ) {
		if( ( results->trace->contents & CONTENTS_NODROP ) || ( results->trace->surfFlags & SURF_NOIMPACT ) ) {
			hasFailed = true;
		} else {
			VectorCopy( results->trace->endpos, fireTarget );
			if( ISWALKABLEPLANE( &results->trace->plane ) ) {
				// There are sophisticated algorithms applied on top of this result
				// for determining where to shoot an explosive, don't stick to a ground.
				fireTarget[2] += -playerbox_stand_mins[2];
			}
		}

		// Interrupt base trajectory prediction
		return false;
	}

	// Wait for hitting a solid
	if( !tryHitInAir ) {
		return true;
	}

	// const float zPartAtSegmentStart = 0.001f * ( results->millisAhead - stepMillis ) * level.gravity;
	// const float zPartAtSegmentEnd = 0.001f * results->millisAhead * level.gravity;
	// segmentTargetVelocity.Z() -= 0.5f * ( zPartAtSegmentStart + zPartAtSegmentEnd );
	float newTargetZSpeed = problemParams.targetVelocity.Z();
	newTargetZSpeed -= 0.5f * 0.001f * ( 2.0f * results->millisAhead - stepMillis ) * level.gravity;

	// Wait for a negative target velocity or for hitting a solid.
	// Do not try to hit in-air in this case (it yields poor results).
	if( newTargetZSpeed > 0 ) {
		return true;
	}

	Vec3 segmentTargetVelocity( problemParams.targetVelocity );
	segmentTargetVelocity.Z() = newTargetZSpeed;

	// TODO: Projectile speed used in PredictProjectileNoClip() needs correction
	// We can't offset fire origin since we do not know direction to target yet
	// Instead, increase projectile speed used in calculations according to currTime
	// Exact formula is to be proven yet
	Vec3 predictedTarget( segmentStart );
	if( !PredictProjectileNoClip( problemParams.fireOrigin, problemParams.projectileSpeed,
								  predictedTarget.Data(), segmentTargetVelocity ) ) {
		tryHitInAir = false;
		// Wait for hitting a solid
		return true;
	}

	// Check whether predictedTarget is within [currPoint, nextPoint]
	// where extrapolation that use currTargetVelocity is assumed to be valid.
	Vec3 segmentEnd( results->origin );
	Vec3 segmentVec( segmentEnd );
	segmentVec -= segmentStart;
	Vec3 predictedTargetToEndVec( segmentEnd );
	predictedTargetToEndVec -= predictedTarget;
	Vec3 predictedTargetToStartVec( segmentStart );
	predictedTargetToStartVec -= predictedTarget;

	if( segmentVec.Dot( predictedTargetToEndVec ) >= 0 && segmentVec.Dot( predictedTargetToStartVec ) <= 0 ) {
		// Can hit in air
		predictedTarget.CopyTo( fireTarget );
		// Interrupt the base trajectory prediction
		return false;
	}

	// Continue a base trajectory prediction
	return true;
}

void BotFireTargetCache::PredictProjectileShot( const SelectedEnemies &selectedEnemies,
												float projectileSpeed,
												AimParams *aimParams,
												bool applyTargetGravity ) {
	if( projectileSpeed <= 0.0f ) {
		return;
	}

	trace_t trace;
	auto *traceKey = const_cast<edict_t*>( selectedEnemies.TraceKey() );

	if( applyTargetGravity ) {
		typedef HitPointPredictor::ProblemParams ProblemParams;
		ProblemParams predictionParams( aimParams->fireOrigin,
										selectedEnemies.LastSeenVelocity().Data(),
										selectedEnemies.TraceKey(),
										projectileSpeed );

		if( !HitPointPredictor::Exec( predictionParams, bot->Skill(), aimParams->fireTarget ) ) {
			TryAimingAtGround( &trace, aimParams, traceKey );
		}
		return;
	}

	Vec3 predictedTarget( aimParams->fireTarget );
	if( !PredictProjectileNoClip( Vec3( aimParams->fireOrigin ),
								  projectileSpeed,
								  predictedTarget.Data(),
								  selectedEnemies.LastSeenVelocity() ) ) {
		TryAimingAtGround( &trace, aimParams, traceKey );
		return;
	}

	// Test a segment between predicted target and initial target
	// Aim at the trace hit point if there is an obstacle.
	// Aim at the predicted target otherwise.
	G_Trace( &trace, aimParams->fireTarget, nullptr, nullptr, predictedTarget.Data(), traceKey, MASK_AISOLID );
	if( trace.fraction == 1.0f ) {
		VectorCopy( predictedTarget.Data(), aimParams->fireTarget );
	} else {
		VectorCopy( trace.endpos, aimParams->fireTarget );
		if( ISWALKABLEPLANE( &trace.plane ) ) {
			// There are sophisticated algorithms applied on top of this result
			// for determining where to shoot an explosive, don't stick to a ground.
			aimParams->fireTarget[2] += -playerbox_stand_mins[2];
		}
	}
}