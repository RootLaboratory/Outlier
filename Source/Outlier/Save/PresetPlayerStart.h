// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Upgrade/OutlierPresetStageIds.h"
#include "PresetPlayerStart.generated.h"

/**
 * 볼륨 오버랩 기반 AOutlierCheckpoint를 대체하는 위치 기반 프리셋 지점.
 * APlayerStart를 상속해 위치/방향은 액터 트랜스폼 자체를 쓰고, FName PresetId로만 식별한다.
 * PresetId는 OutlierPresetStageIds.h 의 상수(=DT_PresetNode 행 이름)와 반드시 일치해야 한다.
 */
UCLASS()
class OUTLIER_API APresetPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	FName GetPresetId() const { return PresetId; }

	// PresetId 프로퍼티의 meta=(GetOptions=...)가 참조하는 드롭다운 후보 목록.
	// OutlierPresetStageIds::All()을 그대로 노출 — 자유 입력이 아니라 헤더에 정의된 것 중에서만 고르게 한다.
	UFUNCTION()
	static TArray<FName> GetPresetIdOptions();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset", meta = (GetOptions = "GetPresetIdOptions"))
	FName PresetId = OutlierPresetStageIds::Start;
};
