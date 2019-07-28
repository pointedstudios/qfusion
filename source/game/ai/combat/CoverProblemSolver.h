#ifndef QFUSION_COVERPROBLEMSOLVER_H
#define QFUSION_COVERPROBLEMSOLVER_H

#include "TacticalSpotsProblemSolver.h"

class CoverProblemSolver: public TacticalSpotsProblemSolver {
public:
	class ProblemParams: public BaseProblemParams {
		friend class CoverProblemSolver;
		const edict_t *attackerEntity;
		vec3_t attackerOrigin;
		float harmfulRayThickness;
	public:
		ProblemParams( const edict_t *attackerEntity_, float harmfulRayThickness_ )
			: attackerEntity( attackerEntity_ ), harmfulRayThickness( harmfulRayThickness_ ) {
			VectorCopy( attackerEntity_->s.origin, this->attackerOrigin );
		}

		ProblemParams( const vec3_t attackerOrigin_, float harmfulRayThickness_ )
			: attackerEntity( nullptr ), harmfulRayThickness( harmfulRayThickness_ ) {
			VectorCopy( attackerOrigin_, this->attackerOrigin );
		}
	};
private:
	using EntNumsVector = StaticVector<int, MAX_EDICTS>;

	int FindTopNodeAndEntNums( SpotsAndScoreVector &spotsAndScores, EntNumsVector &entNums );

	void FilterRawEntNums( EntNumsVector &entNums );

	SpotsAndScoreVector &FilterByAreaVisTables( SpotsAndScoreVector &spotsAndScores );

	SpotsAndScoreVector &FilterByCoarseRayTests( SpotsAndScoreVector &spotsAndScores,
												 int collisionTopNodeHint,
												 const EntNumsVector &entNums );

	SpotsAndScoreVector &SelectCoverSpots( SpotsAndScoreVector &candidateSpots,
		                                   int collisionTopNodeHint,
		                                   const EntNumsVector &entNums );

	bool LooksLikeACoverSpot( const TacticalSpot &spot, const vec3_t *rayBounds, int topNode, const EntNumsVector &entNums );

	bool CastRay( const float *from, const float *to, int topNode, const EntNumsVector &entNums );

	const ProblemParams &problemParams;
public:
	CoverProblemSolver( const OriginParams &originParams_, const ProblemParams &problemParams_ )
		: TacticalSpotsProblemSolver( originParams_, problemParams_ ), problemParams( problemParams_ ) {}

	int FindMany( vec3_t *spots, int numSpots ) override;
};

#endif
