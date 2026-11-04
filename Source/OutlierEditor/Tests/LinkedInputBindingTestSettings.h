#pragma once

#include "CoreMinimal.h"
#include "Settings/OutlierInputUserSettings.h"
#include "LinkedInputBindingTestSettings.generated.h"

// 실제 UserSettings API의 두 번째 요청을 실패시켜 부분 적용 복구를 검증한다.
UCLASS(Transient, NotBlueprintable)
class ULinkedInputBindingTestSettings : public UOutlierInputUserSettings
{
	GENERATED_BODY()

public:
	int32 RequestCount = 0;
	int32 FailOnRequest = INDEX_NONE;
	int32 SaveCount = 0;
	int32 ApplyCount = 0;

	virtual void MapPlayerKey(const FMapPlayerKeyArgs& Args, FGameplayTagContainer& Failure) override
	{
		if (++RequestCount != FailOnRequest) Super::MapPlayerKey(Args, Failure);
	}
	virtual void UnMapPlayerKey(const FMapPlayerKeyArgs& Args, FGameplayTagContainer& Failure) override
	{
		if (++RequestCount != FailOnRequest) Super::UnMapPlayerKey(Args, Failure);
	}
	virtual void SaveSettings() override { ++SaveCount; }
	virtual void ApplySettings() override { ++ApplyCount; }
};
