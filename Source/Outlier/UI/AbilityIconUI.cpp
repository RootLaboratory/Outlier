// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/AbilityIconUI.h"
#include "Components/Image.h"

 FGameplayTag& UAbilityIconUI::GetAbilityTag()
{
	return AbilityTag;
}
 void UAbilityIconUI::SetAbility(FGameplayTag InaAbility)
 {
	 AbilityTag = InaAbility;
 }


void UAbilityIconUI::NativeConstruct()
{
	DefaultIconBrush = AbilityIcon->GetBrush();
	AbilityIcon->SetVisibility(bAbilityUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UAbilityIconUI::SetCoolTime(float InCoolTime)
{
	UE_LOG(LogTemp, Error, TEXT("SetCoolTime"));

	if (!AbilityIcon || !M_AbilityCoolTimeUI || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	AbilityIcon->SetBrushFromMaterial(M_AbilityCoolTimeUI);
	AbilityMID = AbilityIcon->GetDynamicMaterial();

	UObject* Resource = DefaultIconBrush.GetResourceObject();

	if (UTexture* IconTexture = Cast<UTexture>(Resource))
	{
		AbilityMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
		UE_LOG(LogTemp, Error, TEXT("SetTextureParameterValue"));

	}
	bCooldowning = true;
}

void UAbilityIconUI::UpdateCoolTime(float delta)
{
	AccumulatedTime += delta;

	UE_LOG(LogTemp, Error, TEXT("UpdateCoolTime: %f") , AccumulatedTime);

	if (AccumulatedTime >= CoolTime)
	{
		CooldownDone();
		return;
	}

	float Progress = AccumulatedTime / CoolTime;
	AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), Progress);
}

bool UAbilityIconUI::IsCooldowning()
{
	if (!IsUnLock()) return false;

	return bCooldowning;
}

void UAbilityIconUI::CooldownDone()
{

	UE_LOG(LogTemp, Error, TEXT("CooldownDone"));

	bCooldowning = false;
	AccumulatedTime = 0.f;
	CoolTime = 0.f;
	AbilityMID = nullptr;
	AbilityIcon->SetBrush(DefaultIconBrush);
}

void UAbilityIconUI::AbilityUnLock()
{
	bAbilityUnlocked = true;
	AbilityIcon->SetBrush(DefaultIconBrush);
	AbilityIcon->SetVisibility(ESlateVisibility::Visible);
}

bool UAbilityIconUI::IsUnLock()
{
	return bAbilityUnlocked;
}


