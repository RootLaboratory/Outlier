#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
class UWorld;
class FJsonObject;

// Generation probe의 명시적 검증 실행에서만 실제 배치 정보를 관찰하고 준비한다.
namespace Stage02PlacementProbe
{
	void Observe(UWorld* World, TSharedRef<FJsonObject> State);
	bool Execute(UWorld* World, const FString& Action, TSharedPtr<FJsonObject> Command);
}
#endif
