// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OutlierSpawnVisualTarget.generated.h"

class UMeshComponent;

UINTERFACE(MinimalAPI)
class UOutlierSpawnVisualTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * 등장 연출 대상이 "디졸브를 받을 메시"만 알려주는 계약.
 * 머티리얼 / 연출 시간 / 디졸브 진행 범위는 AOutlierPostProcessVolume 이,
 * 머티리얼 교체 / 진행 / 원상복구 / 완료 통보는 UMaterialPostProcessSubsystem 이 전담한다.
 * 은신의 IOutlierStealthVisualTarget 과 같은 구조다.
 */
class IOutlierSpawnVisualTarget
{
	GENERATED_BODY()

public:
	// 장착 무기처럼 부속 액터가 있으면 여기서 같이 채워 넣는다 ( 배열을 Reset 하지 말 것 ).
	virtual void CollectSpawnPresentationMeshes(TArray<UMeshComponent*>& OutMeshes) const = 0;
};
