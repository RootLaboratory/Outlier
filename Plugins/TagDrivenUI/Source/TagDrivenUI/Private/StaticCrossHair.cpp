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
	//UE_LOG(LogTemp, Error, TEXT("NativeTick"));

	if (IsCooldowning())
	{
		//UE_LOG(LogTemp, Error, TEXT("NativeTick") );

		UpdateCoolTime(Indelta);
	}
}

void UStaticCrossHair::SetCoolTime(float InCoolTime)
{
	UE_LOG(LogTemp, Error, TEXT("SetCoolTime"));

	if (!CrossHairImage || !M_ReloadingTimeUI || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	CrossHairImage->SetBrushFromMaterial(M_ReloadingTimeUI);
	ReloadingTimeMID = CrossHairImage->GetDynamicMaterial();

	UObject* Resource = DefaultIconBrush.GetResourceObject();

	if (UTexture* IconTexture = Cast<UTexture>(Resource))
	{
		ReloadingTimeMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
		UE_LOG(LogTemp, Error, TEXT("SetTextureParameterValue"));

	}
	bCooldowning = true;
}

void UStaticCrossHair::UpdateCoolTime(float InCoolTime)
{
	AccumulatedTime += InCoolTime;

	UE_LOG(LogTemp, Error, TEXT("UpdateCoolTime: %f"), AccumulatedTime);

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
	UE_LOG(LogTemp, Error, TEXT("CooldownDone"));

	bCooldowning = false;
	AccumulatedTime = 0.f;
	CoolTime = 0.f;
	ReloadingTimeMID = nullptr;
	CrossHairImage->SetBrush(DefaultIconBrush);
}
