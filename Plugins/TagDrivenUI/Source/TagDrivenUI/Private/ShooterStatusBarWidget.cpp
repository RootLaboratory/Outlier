// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterStatusBarWidget.h"

#include "AmmoUI.h"
#include "AbilitySlotUI.h"
#include "Engine/Texture2D.h"
#include "Components/SizeBox.h"
#include "ShooterCurrentWeaponIcon.h"
#include "TagDrivenUIGameplayTags.h"

void UShooterStatusBarWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshCurrentAbilityPresentation();
}

void UShooterStatusBarWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// 디자이너에서 값을 입력하거나 드래그하는 동안에도 미리보기를 갱신한다.
	if (IsDesignTime())
	{
		RefreshCurrentAbilityPresentation();
	}
}

void UShooterStatusBarWidget::RefreshCurrentAbilityPresentation()
{
	if (!CurrentAbilityUI) return;

	const FGameplayTag AbilityTag = CurrentAbilityUI->GetCurrentAbilityTag();
	// 아직 선택한 스킬이 없는 초기 미리보기는 기본 스킬인 양자 이동으로 표시한다.
	SetCurrentAbilityPresentation(AbilityTag.IsValid()
		? AbilityTag : TagDrivenUITags::Ability::Shooter::QuantumLeap());
}

UTexture2D* UShooterStatusBarWidget::GetAbilityIconTexture(const FGameplayTag& AbilityTag) const
{
	const auto* Texture = AbilityIconTextures.Find(AbilityTag);
	return Texture ? Texture->Get() : nullptr;
}

void UShooterStatusBarWidget::SetCurrentAbilityPresentation(const FGameplayTag& AbilityTag)
{
	if (!CurrentAbilityUI || !AbilityTag.IsValid()) return;

	// 매핑이 빠진 스킬도 nullptr를 전달해 이전 스킬의 이미지가 남지 않게 한다.
	CurrentAbilityUI->SetIconTexture(GetAbilityIconTexture(AbilityTag));
	CurrentAbilityUI->SetCurrentAbility(AbilityTag);

	const FShooterAbilityIconLayout* Layout = AbilityIconLayouts.Find(AbilityTag);
	CurrentAbilityUI->SetIconLayout(Layout ? Layout->RenderScale : FVector2D::UnitVector,
		Layout ? Layout->PositionOffset : FVector2D::ZeroVector);

	// 미리보기와 실제 HUD 인스턴스의 설정 차이를 확인한다. 스킬 선택 시에만 기록한다.
	if (!IsDesignTime())
	{
		const USizeBox* IconBox = CurrentAbilityUI->IconSizeBox;
		UE_LOG(LogTemp, Log,
			TEXT("[ShooterHUD][AbilityLayout] Widget=%s Class=%s Tag=%s LayoutFound=%d Entries=%d IconSizeBox=%s RequestedScale=%s RequestedOffset=%s AppliedScale=%s AppliedTranslation=%s"),
			*GetPathName(), *GetNameSafe(GetClass()), *AbilityTag.ToString(),
			Layout != nullptr, AbilityIconLayouts.Num(), *GetNameSafe(IconBox),
			*(Layout ? Layout->RenderScale : FVector2D::UnitVector).ToString(),
			*(Layout ? Layout->PositionOffset : FVector2D::ZeroVector).ToString(),
			IconBox ? *IconBox->GetRenderTransform().Scale.ToString() : TEXT("Unbound"),
			IconBox ? *IconBox->GetRenderTransform().Translation.ToString() : TEXT("Unbound"));
	}
}

void UShooterStatusBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ShooterHUD][StatusBar] Construct Widget=%s Class=%s Ammo=%s Ability=%s Weapon=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetClass()),
		*GetNameSafe(AmmoUI),
		*GetNameSafe(CurrentAbilityUI),
		*GetNameSafe(CurrentWeaponUI));
}
