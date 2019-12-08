#ifndef QFUSION_ENVIRONMENTTRACECACHE_H
#define QFUSION_ENVIRONMENTTRACECACHE_H

#include "../ai_local.h"

class MovementPredictionContext;

/**
 * Provides lazily-computed results of full-height or jumpable-height
 * short traces in 8 directions (front, left, back-left, front-right...)
 */
class EnvironmentTraceCache {
public:
	static constexpr float TRACE_DEPTH = 32.0f;

	/**
	 * Represents a cached trace result
	 */
	struct TraceResult {
		vec3_t traceDir;
		trace_t trace;

		inline bool IsEmpty() const { return trace.fraction == 1.0f; }
	};
private:
	// Precache this reference as it is used on every prediction step
	const aas_areasettings_t *aasAreaSettings;

	TraceResult results[16];
	unsigned resultsMask;
	bool didAreaTest;
	bool hasNoFullHeightObstaclesAround;

	template <typename T>
	static inline void Assert( T condition, const char *message = nullptr ) {
#ifndef PUBLIC_BUILD
		if( !condition ) {
			if( message ) {
				AI_FailWith( "EnvironmentTraceCache::Assert()", "%s\n", message );
			} else {
				AI_FailWith( "EnvironmentTraceCache::Assert()", "An assertion has failed\n" );
			}
		}
#endif
	}

	/**
	 * Sets both full-height and jumpable-height cached results to empty
	 * @param front2DDir a front direction for a bot in predicted state
	 * @param right2DDir a right direction for a bot in predicted state
	 */
	void SetAllResultsToEmpty( const vec3_t front2DDir, const vec3_t right2DDir );
	/**
	 * Sets only jumpable-height cached results to empty
	 * @param front2DDir a front direction for a bot in predicted state
	 * @param right2DDir a right direction for a bot in predicted state
	 */
	void SetAllJumpableToEmpty( const vec3_t front2DDir, const vec3_t right2DDir );

	bool TrySkipTracingForCurrOrigin( class MovementPredictionContext *context,
									  const vec3_t front2DDir, const vec3_t right2DDir );

	int ComputeCollisionTopNodeHint( class MovementPredictionContext *context ) const;

	/**
	 * Selects indices of non-blocked dirs among 8 full-height ones.
	 * @param context a current state of movement prediction context
	 * @param nonBlockedDirIndices a buffer for results
	 * @return a number of non-blocked dirs
	 */
	inline unsigned SelectNonBlockedDirs( class MovementPredictionContext *context, unsigned *nonBlockedDirIndices );
public:
	struct Query {
		unsigned mask;
		int index;

		static Query Front() { return { 1u << 0, 0 }; }
		static Query Back() { return { 1u << 1, 1 }; }
		static Query Left() { return { 1u << 2, 2 }; }
		static Query Right() { return { 1u << 3, 3 }; }
		static Query FrontLeft() { return { 1u << 4, 4 }; }
		static Query FrontRight() { return { 1u << 5, 5 }; }
		static Query BackLeft() { return { 1u << 6, 6 }; }
		static Query BackRight() { return { 1u << 7, 7 }; }

		Query &JumpableHeight() {
			// Check whether the current mask is for a full height
			if( mask <= ( 1u << 7 ) ) {
				mask <<= 8;
			}
			return *this;
		}
	};

	const TraceResult ResultForQuery( const Query &query ) {
		Assert( query.mask & this->resultsMask, "A result is not present for the index" );
		return results[query.index];
	}

	EnvironmentTraceCache() {
		// Shut an analyzer up
		memset( this, 0, sizeof( EnvironmentTraceCache ) );
		this->aasAreaSettings = AiAasWorld::Instance()->AreaSettings();
	}

	void TestForResultsMask( class MovementPredictionContext *context, unsigned requiredResultsMask );

	void TestForQuery( class MovementPredictionContext *context, const Query &query ) {
		TestForResultsMask( context, query.mask );
	}

	bool CanSkipPMoveCollision( class MovementPredictionContext *context );

	void MakeRandomizedKeyMovesToTarget( MovementPredictionContext *context, const Vec3 &intendedMoveDir, int *keyMoves );
	void MakeKeyMovesToTarget( MovementPredictionContext *context, const Vec3 &intendedMoveDir, int *keyMoves );
	void MakeRandomKeyMoves( MovementPredictionContext *context, int *keyMoves );
};

#endif
