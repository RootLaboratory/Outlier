// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "OutlierGameInstance.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UOutlierGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init() override;

private:
	void HandlePostLoadMap(UWorld* LoadedWorld);

	bool bTriedConnect = false;
};
