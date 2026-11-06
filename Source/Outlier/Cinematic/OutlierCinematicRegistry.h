#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "OutlierCinematicRegistry.generated.h"

class ALevelSequenceActor;
class UCinematicHintkey;

/** 아레나 퍼시스턴트 레벨 또는 타이틀 맵에 하나 배치하고 레벨의 Sequence Actor를 할당한다. */
UCLASS(Blueprintable)
class OUTLIER_API AOutlierCinematicRegistry : public AActor
{
	GENERATED_BODY()

public:
	AOutlierCinematicRegistry();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Cinematic")
	TMap<FGameplayTag, TObjectPtr<ALevelSequenceActor>> Cinematics;

	// 각 플레이어가 로컬에서 스페이스를 위젯의 HoldSeconds만큼 누르면 두 플레이어의 인트로를 함께 완료한다.
	// 각 플레이어가 로컬 UI 레이어에 재생 시작부터 종료까지 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cinematic|UI")
	TSubclassOf<UCinematicHintkey> CinematicHintkeyClass;
};
