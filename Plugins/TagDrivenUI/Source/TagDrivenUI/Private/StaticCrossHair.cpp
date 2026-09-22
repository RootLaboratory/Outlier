// Fill out your copyright notice in the Description page of Project Settings.


#include "StaticCrossHair.h"
#include "Components/Image.h"

void UStaticCrossHair::SpawnReloadingTimer_Implementation()
{
	UE_LOG(LogTemp, Log, TEXT("SpawnReloadingTimer triggered."));
}

void UStaticCrossHair::NativeConstruct()
{
	Super::NativeConstruct();

	DefaultIconBrush = CrossHairImage->GetBrush();
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
	if (InCoolTime <= 0.0f || !CrossHairImage || !M_ReloadingTimeUI || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	AccumulatedTime = FMath::Clamp(InElapsedTime, 0.0f, CoolTime);
	CooldownStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() - AccumulatedTime : 0.0f;
	bCooldowning = true;

	UE_LOG(LogTemp, Log, TEXT("[PistolCrosshairCooldown] Started Duration=%.3f Elapsed=%.3f StartTime=%.3f"),
		CoolTime, AccumulatedTime, CooldownStartTime);

	CrossHairImage->SetBrushFromMaterial(M_ReloadingTimeUI);
	ReloadingTimeMID = CrossHairImage->GetDynamicMaterial();

	UObject* Resource = DefaultIconBrush.GetResourceObject();

	if (UTexture* IconTexture = Cast<UTexture>(Resource))
	{
		ReloadingTimeMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
		ReloadingTimeMID->SetScalarParameterValue(TEXT("IsCoolDown"), static_cast<float>(bCooldowning));
		ReloadingTimeMID->SetScalarParameterValue(TEXT("CooldownProgress"), AccumulatedTime / CoolTime);
	}

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
	UE_LOG(LogTemp, Log, TEXT("[PistolCrosshairCooldown] Completed"));
	if (ReloadingTimeMID)
	{
		ReloadingTimeMID->SetScalarParameterValue(TEXT("IsCoolDown"), 0.0f);
	}

	bCooldowning = false;
	AccumulatedTime = 0.f;
	CooldownStartTime = 0.0f;
	CoolTime = 0.f;
	ReloadingTimeMID = nullptr;
	CrossHairImage->SetBrush(DefaultIconBrush);

}
