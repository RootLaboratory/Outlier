// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FrontendPlayerController.generated.h"

/**
 * 
 */

class UTitleMainWidget;

UCLASS()
class OUTLIER_API AFrontendPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	virtual void BeginPlay() override;

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr< UTitleMainWidget> TitleWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UTitleMainWidget> TitleWidgetClass;
};
