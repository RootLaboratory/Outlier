// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ScannableInterface.generated.h"

UENUM(BlueprintType)
enum class EScanType : uint8
{
	Default,
	Enemy,
	Interaction,
};

// 스캔에 보이기 시작하는 정찰 업그레이드 단계. 색(GetScanStencilValue)과 별개 축이다.
UENUM()
enum class EScanRevealCondition : uint8
{
	Always,              // 기본 스캔
	RevealHackable,      // 탐색 항목 확장 I  (Upgrade.Partner.Scan.RevealHackable)
	RevealNodeFragments, // 탐색 항목 확장 II (Upgrade.Partner.Scan.RevealNodeFragments)
};

// This class does not need to be modified.
UINTERFACE(MinimalAPI, Blueprintable)
class UScannableInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IScannableInterface
{
	GENERATED_BODY()

public:
	virtual int32 GetScanStencilValue() const = 0;
	virtual EScanRevealCondition GetScanRevealCondition() const { return EScanRevealCondition::Always; }
};
