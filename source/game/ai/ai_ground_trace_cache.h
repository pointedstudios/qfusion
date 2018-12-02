#ifndef QFUSION_AI_GROUND_TRACE_CACHE_H
#define QFUSION_AI_GROUND_TRACE_CACHE_H

#include "../../gameshared/q_collision.h"

class AiGroundTraceCache {
	/**
	 * Declare an untyped pointer in order to prevent inclusion of g_local.h
	 */
	void *data;

	template <typename, unsigned> friend class StaticVector;

	AiGroundTraceCache();
	~AiGroundTraceCache();

	static AiGroundTraceCache *instance;
public:
	static void Init();
	static void Shutdown();

	static AiGroundTraceCache *Instance() {
		assert( instance );
		return instance;
	}

	void GetGroundTrace( const struct edict_s *ent, float depth, trace_t *trace, uint64_t maxMillisAgo = 0 );
	bool TryDropToFloor( const struct edict_s *ent, float depth, vec3_t result, uint64_t maxMillisAgo = 0 );
};

#endif
