// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OutlierStealthVisualTarget.generated.h"

class UMeshComponent;

UINTERFACE(MinimalAPI)
class UOutlierStealthVisualTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * 은신 비주얼 대상이 "내 메시가 어떻게 구성돼 있는지"만 알려주는 계약.
 * 머티리얼 교체 / 페이드 / 원상복구는 전부 UMaterialPostProcessSubsystem 이 전담한다.
 *
 * 1인칭과 3인칭 메시가 한 액터에 섞여 있는 경우( 캐릭터, 무기 )에만 구현하면 된다.
 * 구현하지 않은 액터는 전 UMeshComponent 가 3인칭으로 취급된다 ( ApplyScanStencil 과 동일 ).
 */
class IOutlierStealthVisualTarget
{
	GENERATED_BODY()

public:
	// 둘 다 머티리얼 교체를 받는다. 1인칭 메시는 나에게만, 3인칭 메시는 상대에게만 보이므로
	// 볼륨에 지정한 각각의 은신 머티리얼이 꽂힌다.
	// 장착 무기처럼 부속 액터가 있으면 여기서 같이 채워 넣는다 ( 배열을 Reset 하지 말 것 ).
	virtual void CollectStealthMeshes(
		TArray<UMeshComponent*>& OutFirstPersonMeshes,
		TArray<UMeshComponent*>& OutThirdPersonMeshes) const = 0;
};
