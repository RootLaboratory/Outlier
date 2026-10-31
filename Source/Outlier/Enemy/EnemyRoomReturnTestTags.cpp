#include "Enemy/EnemyRoomReturnTestTags.h"

#if WITH_DEV_AUTOMATION_TESTS

// Native tags must be registered by a Runtime module, including tags used by Editor tests.
namespace EnemyRoomReturnTestTags
{
	UE_DEFINE_GAMEPLAY_TAG(ReturnTreeHome, "Room.ReturnTreeTest.1.1");
	UE_DEFINE_GAMEPLAY_TAG(ReturnTreeSibling, "Room.ReturnTreeTest.1.2");
}

#endif
