// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/TitleWidget.h"
#include "Components/Button.h"


void UTitleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (StartButton)
	{
		StartButton->OnClicked.AddDynamic(this, &UTitleWidget::HandleStartButtonEvent);
	}

	if (ExitButton)
	{
		ExitButton->OnClicked.AddDynamic(this, &UTitleWidget::HandleExitButtonEvent);
	}
}

void UTitleWidget::HandleStartButtonEvent()
{
	// TitleWidget, base에 해당 Widget을 끄고 LobbyWidget을 활성화 시키게 해댤라고 요청 해야함.
	OnStartRequested.Broadcast();
}

void UTitleWidget::HandleExitButtonEvent()
{
	// Process 종료;
	OnExitRequested.Broadcast();
}
