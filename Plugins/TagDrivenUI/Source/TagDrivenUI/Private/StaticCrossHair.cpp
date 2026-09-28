// Fill out your copyright notice in the Description page of Project Settings.


#include "StaticCrossHair.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

void UStaticCrossHair::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!PistolCoolTime)
	{
		return;
	}

	// 브러시 교체는 여기서 한 번만. 디자이너 텍스처를 쿨타임 머티리얼의 아이콘으로 넘긴다.
	if (M_ReloadingTimeUI)
	{
		UTexture* IconTexture = Cast<UTexture>(PistolCoolTime->GetBrush().GetResourceObject());

		PistolCoolTime->SetBrushFromMaterial(M_ReloadingTimeUI);
		ReloadingTimeMID = PistolCoolTime->GetDynamicMaterial();

		if (ReloadingTimeMID)
		{
			if (IconTexture)
			{
				ReloadingTimeMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
			}
			ReloadingTimeMID->SetScalarParameterValue(TEXT("IsCoolDown"), 1.f);
		}
	}

	PistolCoolTime->SetVisibility(ESlateVisibility::Collapsed);
}

void UStaticCrossHair::NativeTick(const FGeometry& MyGeometry, float Indelta)
{
	Super::NativeTick(MyGeometry, Indelta);

	if (IsCooldowning())
	{
		UpdateCoolTime(Indelta);
	}
}

void UStaticCrossHair::SetCoolTime(float InCoolTime)
{
	if (!PistolCoolTime || !ReloadingTimeMID || InCoolTime <= 0.f)
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	AccumulatedTime = 0.f;
	bCooldowning = true;

	ReloadingTimeMID->SetScalarParameterValue(TEXT("CooldownProgress"), 0.f);
	PistolCoolTime->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UStaticCrossHair::UpdateCoolTime(float InCoolTime)
{
	AccumulatedTime += InCoolTime;

	if (AccumulatedTime >= CoolTime)
	{
		CooldownDone();
		return;
	}

	float Progress = AccumulatedTime / CoolTime;
	ReloadingTimeMID->SetScalarParameterValue(TEXT("CooldownProgress"), Progress);
}

bool UStaticCrossHair::IsCooldowning()
{
	return bCooldowning;
}

void UStaticCrossHair::CooldownDone()
{
	bCooldowning = false;
	AccumulatedTime = 0.f;
	CoolTime = 0.f;

	// 브러시는 되돌리지 않는다. 숨겨두면 다음 SetCoolTime 전까지 갱신도 멈춘다.
	if (PistolCoolTime)
	{
		PistolCoolTime->SetVisibility(ESlateVisibility::Collapsed);
	}
}
