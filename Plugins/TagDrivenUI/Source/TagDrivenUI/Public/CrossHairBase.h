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
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	 void SpawnAttckSign(EAttackSign InAttackSign) ;

	virtual void OnAiming() {}

	virtual void OnAimingOff(){}

public:
	bool IsAiming() { return bAiming; }

protected:
	uint8 bAiming : 1 = false;

};
