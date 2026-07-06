// Fill out your copyright notice in the Description page of Project Settings.


#include "PartnerHPUI.h"

#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TagDrivenUIGameplayTags.h"

void UPartnerHPUI::NativeConstruct()
{
	Super::NativeConstruct();

	if (!DefaultShooterConditionTag.IsValid())
	{
		DefaultShooterConditionTag = TagDrivenUITags::Condition::Shooter::HP();
	}

	if (!CurShooterConditionTag.IsValid())
	{
		CurShooterConditionTag = DefaultShooterConditionTag;
	}

	InitializeShooterConditionMaterial();
	UpdateShooterConditionMaterial();
}

void UPartnerHPUI::SetShooterCondition(FGameplayTag InConditionTag)
{
	CurShooterConditionTag = InConditionTag.IsValid()
		? InConditionTag
		: DefaultShooterConditionTag;

	UpdateShooterConditionMaterial();
}

FGameplayTag UPartnerHPUI::GetShooterCondition() const
{
	return CurShooterConditionTag;
}

void UPartnerHPUI::RefreshShooterConditionUI()
{
	SetShooterCondition(DefaultShooterConditionTag);
}

void UPartnerHPUI::InitializeShooterConditionMaterial()
{
	if (ShooterConditionMID)
	{
		return;
	}

	if (ShooterConditionUI)
	{
		ShooterConditionMID = ShooterConditionUI->GetDynamicMaterial();
	}

	if (!ShooterConditionMID && ShooterConditionMaterial)
	{
		ShooterConditionMID = UMaterialInstanceDynamic::Create(ShooterConditionMaterial, this);

		if (ShooterConditionUI)
		{
			ShooterConditionUI->SetBrushFromMaterial(ShooterConditionMID);
		}
	}
}

void UPartnerHPUI::UpdateShooterConditionMaterial()
{
	InitializeShooterConditionMaterial();

	if (!ShooterConditionMID)
	{
		return;
	}

	const float ConditionValue = GetShooterConditionMaterialValue(CurShooterConditionTag);
	ShooterConditionMID->SetScalarParameterValue(ShooterConditionParameterName, ConditionValue);
}

float UPartnerHPUI::GetShooterConditionMaterialValue(const FGameplayTag& InConditionTag) const
{
	if (const float* FoundValue = ConditionMaterialValues.Find(InConditionTag))
	{
		return *FoundValue;
	}

	if (InConditionTag.MatchesTagExact(TagDrivenUITags::Condition::Shooter::Shield()))
	{
		return 0.0f;
	}

	if (InConditionTag.MatchesTagExact(TagDrivenUITags::Condition::Shooter::HP()))
	{
		return 1.0f;
	}

	if (InConditionTag.MatchesTagExact(TagDrivenUITags::Condition::Shooter::PartnerShield()))
	{
		return 2.0f;
	}

	return 1.0f;
}
