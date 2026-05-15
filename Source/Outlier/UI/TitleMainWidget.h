// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TitleMainWidget.generated.h"

/**
 * 
 */

class UTitleWidget;
class ULobbyWidget;

UCLASS()
class OUTLIER_API UTitleMainWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

public:

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTitleWidget> TitleWidget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<ULobbyWidget> LobbyWidget;

private:
	UFUNCTION()
	void ShowLobby();

	UFUNCTION()
	void RequestExit();
};
