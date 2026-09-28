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

void UStaticCrossHair::Activate()
{
	Super::Activate();

	// 다른 무기를 들고 있는 동안 Tick이 멈췄더라도, 다시 표시되는 프레임에
	// 실제 경과 시각을 기준으로 쿨타임 진행도를 즉시 복원한다.
	if (IsCooldowning())
	{
		UpdateCoolTime(0.0f);
	}
}

void UStaticCrossHair::SetCoolTime(float InCoolTime, float InElapsedTime)
{
	if (InCoolTime <= 0.0f || !PistolCoolTime || !ReloadingTimeMID)
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	AccumulatedTime = FMath::Clamp(InElapsedTime, 0.0f, CoolTime);
	CooldownStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() - AccumulatedTime : 0.0f;
	bCooldowning = true;

	ReloadingTimeMID->SetScalarParameterValue(TEXT("CooldownProgress"), AccumulatedTime / CoolTime);
	PistolCoolTime->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UStaticCrossHair::UpdateCoolTime(float DeltaTime)
{
	if (const UWorld* World = GetWorld())
	{
		AccumulatedTime = FMath::Max(0.0f, World->GetTimeSeconds() - CooldownStartTime);
	}
	else
	{
		AccumulatedTime += DeltaTime;
	}

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
	CooldownStartTime = 0.0f;
	CoolTime = 0.f;

	// 브러시는 되돌리지 않는다. 숨겨두면 다음 SetCoolTime 전까지 갱신도 멈춘다.
	if (PistolCoolTime)
	{
		PistolCoolTime->SetVisibility(ESlateVisibility::Collapsed);
	}
}
