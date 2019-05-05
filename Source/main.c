#include <stdint.h>
#include <stdlib.h>

#include <stdio.h>

#include <assert.h>

#define CRANBERRY_SSE
#include "cranberry_math.h"

#define MIST_PROFILE_ENABLED

#define CRANBERRY_DEBUG
#define CRANBERRY_PROCEDURAL_TESTS
#define CRANBERRY_PROCEDURAL_IMPLEMENTATION
#include "cranberry_procedural.h"

#define MIST_PROFILE_IMPLEMENTATION
#include "Mist_Profiler.h"

int main()
{
	Mist_ProfileInit();

	cranp_init();

	MIST_PROFILE_BEGIN("main", "cranp_test");
	cranp_test();
	MIST_PROFILE_END("main", "cranp_test");

	Mist_FlushThreadBuffer();
	Mist_WriteToFile("game_profile.json");
	Mist_ProfileTerminate();

	return 0;
}
