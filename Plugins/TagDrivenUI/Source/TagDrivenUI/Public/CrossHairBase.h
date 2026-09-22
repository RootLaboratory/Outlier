// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "CrossHairBase.generated.h"

/**
 * 
 */


//Client 쪽에서는 Enum만 던져줌.

// Enum class
// Type받아서, Texture
//

UENUM(BlueprintType)
enum class EAttackSign : uint8
{
	Default UMETA(DisplayName = "Default"),
	Adjusted UMETA(DisplayName = "Adjusted"),
	Critical UMETA(DisplayName = "Critical"),
	Kill UMETA(DisplayName = "Kill"),

	None UMETA(DisplayName = "None")
};

UCLASS(Blueprintable)
class TAGDRIVENUI_API UCrossHairBase : public UEventDrivenUI
{
	GENERATED_BODY()


protected:
	float Duration =0;

public:
	virtual void SpawnAttackSign(EAttackSign InAttackSign) {};

	virtual void OnAiming() {}

	virtual void OnAimingOff(){}

	// 모듈 활성화가 ADS 숨김을 덮어쓰지 않게 한다.
	// SetModuleActive / 무기 전환 등 개별 활성화 경로가
	// SetVisibility 를 그대로 밀어버려서, 조준 중에 켜지면 크로스헤어가 다시 보였다.
	virtual void Activate() override;

	// 조준/사격으로 쌓인 표시 상태를 초기값으로 되돌린다.
	// 슈트 획득 전에 쏴서 남은 확산/회전이 획득 후 무기 전환 때 되살아나는 걸 막는다.
	virtual void ResetCrossHairState() { bAiming = false; }

public:
	bool IsAiming() { return bAiming; }

protected:
	uint8 bAiming : 1 = false;

};
