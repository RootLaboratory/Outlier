#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "GameOverPauseTestLocalPlayer.generated.h"

// 뷰포트나 렌더러를 생성하지 않고 실제 정지 월드를 후처리 서브시스템에 연결한다.
UCLASS(Transient, NotBlueprintable)
class UGameOverPauseTestLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override { return TestWorld; }

	UPROPERTY()
	TObjectPtr<UWorld> TestWorld;
};
