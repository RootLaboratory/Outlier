// Fill out your copyright notice in the Description page of Project Settings.


#include "AmmoUI.h"
void UAmmoUI::AmmoCountChanged_Implementation(int32 InAmmoCount)
{
//	UE_LOG(LogTemp, Error, TEXT("Received, But c++ Function worked: %d "), InAmmoCount);
	Temp_AmmoCount = InAmmoCount;
}

