project (game)

include_directories(${ANGELSCRIPT_INCLUDE_DIR})
include_directories("../../third-party/recastnavigation/Recast/Include")
include_directories("../../third-party/recastnavigation/Detour/Include")

file(GLOB GAME_HEADERS
	"*.h"
	"ai/*.h"
	"ai/awareness/*.h"
	"ai/combat/*.h"
	"ai/movement/*.h"
	"ai/navigation/*.h"
	"ai/planning/*.h"
	"ai/teamplay/*.h"
	"../gameshared/*.h"
	"../qalgo/base64.h"
	"../qalgo/md5.h"
	"../qalgo/Links.h"
	"../../third-party/recastnavigation/Recast/Include/*.h"
	"../../third-party/recastnavigation/Detour/Include/*.h"
	"../qcommon/net.h" # constants and types for address parsing
	"../qcommon/snap.h" # constants for the antihack system
)

file(GLOB GAME_SOURCES
	"*.cpp"
	"ai/*.cpp"
	"ai/awareness/*.cpp"
	"ai/combat/*.cpp"
	"ai/movement/*.cpp"
	"ai/navigation/*.cpp"
	"ai/planning/*.cpp"
	"ai/teamplay/*.cpp"
	"../gameshared/*.cpp"
	"../matchmaker/mm_rating.cpp"
	"../qalgo/base64.cpp"
	"../qalgo/md5.cpp"
	"../qcommon/cjson.cpp"
	"../../third-party/recastnavigation/Recast/Source/*.cpp"
	"../../third-party/recastnavigation/Detour/Source/*.cpp"
)

add_library(game SHARED ${GAME_SOURCES} ${GAME_HEADERS})
target_link_libraries(game PRIVATE angelwrap)
set_property(TARGET game PROPERTY CXX_STANDARD 11)
qf_set_output_dir(game ${QFUSION_GAME_DIR})
