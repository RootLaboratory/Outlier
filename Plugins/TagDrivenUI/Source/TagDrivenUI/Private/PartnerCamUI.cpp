// Fill out your copyright notice in the Description page of Project Settings.


#include "PartnerCamUI.h"

void UPartnerCamUI::EventCall()
{
	bFlag = !bFlag;

}


void UPartnerCamUI::EventBind(const FGameplayTag& InTag)
{
	Super::EventBind(InTag);




}
