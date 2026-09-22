// Fill out your copyright notice in the Description page of Project Settings.


#include "AmmoUI.h"
#include "Components/TextBlock.h"

void UAmmoUI::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshAmmoTexts();
}

void UAmmoUI::AmmoCountChanged_Implementation(int32 InAmmoCount)
{
//	UE_LOG(LogTemp, Error, TEXT("Received, But c++ Function worked: %d "), InAmmoCount);
	CurrentAmmo = InAmmoCount;
	Temp_AmmoCount = InAmmoCount;
	RefreshAmmoTexts();
}

void UAmmoUI::AmmoStateChanged_Implementation(int32 InCurrentAmmo, int32 InMaxAmmo)
{
	CurrentAmmo = InCurrentAmmo;
	MaxAmmo = InMaxAmmo;
	Temp_AmmoCount = InCurrentAmmo;
	RefreshAmmoTexts();
}

void UAmmoUI::SetAmmoState(int32 InCurrentAmmo, int32 InMaxAmmo)
{
	CurrentAmmo = InCurrentAmmo;
	MaxAmmo = InMaxAmmo;
	Temp_AmmoCount = InCurrentAmmo;

	// Current/Max를 함께 받는 경로만 사용한다. 구형 단일 이벤트를 뒤이어 호출하면
	// WBP 구현에서 Max 텍스트까지 Current 값으로 다시 덮을 수 있다.
	AmmoStateChanged(InCurrentAmmo, InMaxAmmo);

	// Blueprint 이벤트가 텍스트를 건드려도 BindWidget 값이 최종 상태가 되도록 보장한다.
	RefreshAmmoTexts();
}

void UAmmoUI::RefreshAmmoTexts()
{
	if (CurrentAmmoText)
	{
		CurrentAmmoText->SetText(FText::AsNumber(CurrentAmmo));
	}

	if (MaxAmmoText)
	{
		MaxAmmoText->SetText(FText::AsNumber(MaxAmmo));
	}
}
