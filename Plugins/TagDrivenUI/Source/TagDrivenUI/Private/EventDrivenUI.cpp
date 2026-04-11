// Fill out your copyright notice in the Description page of Project Settings.


#include "EventDrivenUI.h"


void UEventDrivenUI::Activate()
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	HandleActivatedVisual();
}

void UEventDrivenUI::Deactivate()
{
	SetVisibility(ESlateVisibility::Collapsed);
	HandleDeactivatedVisual();
}


