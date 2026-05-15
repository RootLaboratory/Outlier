// Fill out your copyright notice in the Description page of Project Settings.


#include "FrontendPlayerController.h"
#include "UI/TitleMainWidget.h"


void AFrontendPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (TitleWidgetClass)
	{
		TitleWidget = CreateWidget<UTitleMainWidget>(this, TitleWidgetClass);
	}

	if (TitleWidget)
	{
		TitleWidget->AddToViewport();
	}

	FInputModeUIOnly InputMode;
	//InputMode.SetWidgetToFocus(TitleWidget->TakeWidget());
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}
