// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "OutlierArenaPausePlayerState.generated.h"

/** Worker 월드의 사전 정지 상태만 복제하며 실제 참가자 목록에는 포함되지 않는다. */
UCLASS(NotBlueprintable, Transient)
class OUTLIER_API AOutlierArenaPausePlayerState final : public APlayerState
{
	GENERATED_BODY()

protected:
	virtual void PostInitializeComponents() override;
};
