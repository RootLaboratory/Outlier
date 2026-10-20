// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilityIconUI.h"
#include "Components/Image.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

FGameplayTag UAbilityIconUI::GetAbilityTag() const
{
	return AbilityTag;
}

void UAbilityIconUI::SetAbility(FGameplayTag InAbility)
{
	if (AbilityTag != InAbility)
	{
		ResetChargeFeedback();
		bChargePresentation = false;
	}
	AbilityTag = InAbility;
}

void UAbilityIconUI::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyIconTexture();
}

void UAbilityIconUI::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyIconTexture();
	if (GlowImage)
	{
		GlowBaseScale = GlowImage->GetRenderTransform().Scale;
		GlowImage->SetRenderOpacity(0.0f);
		GlowImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (AbilityIcon)
	{
		AbilityIcon->SetVisibility(bAbilityUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UAbilityIconUI::NativeDestruct()
{
	ResetChargePresentation();
	Super::NativeDestruct();
}

void UAbilityIconUI::ApplyIconTexture()
{
	if (IconTexture)
	{
		SetIconTexture(IconTexture);
	}
	else if (AbilityIcon && Cast<UTexture>(AbilityIcon->GetBrush().GetResourceObject()))
	{
		// 기존 WBP는 Image의 Texture를 사용한다. 쿨타임 MID는 원본으로 저장하지 않는다.
		SetIconBrush(AbilityIcon->GetBrush());
	}
}

void UAbilityIconUI::SetIconTexture(UTexture2D* InTexture)
{
	IconTexture = InTexture;
	if (!InTexture || !AbilityIcon)
	{
		return;
	}

	// Texture만 교체하고 Designer에서 정한 Brush 크기와 표현 설정은 유지한다.
	FSlateBrush Brush = DefaultIconBrush.GetResourceObject() ? DefaultIconBrush : AbilityIcon->GetBrush();
	Brush.SetResourceObject(InTexture);
	SetIconBrush(Brush);
}

void UAbilityIconUI::SetIconBrush(const FSlateBrush& Brush)
{
	// Image 자신의 Brush가 전달되어도 이후 Image 변경에 영향을 받지 않도록 먼저 복사한다.
	DefaultIconBrush = Brush;
	IconTexture = Cast<UTexture2D>(DefaultIconBrush.GetResourceObject());
	if (GlowImage)
	{
		GlowImage->SetBrush(DefaultIconBrush);
	}
	if (bCooldowning || (bChargePresentation && !bChargeAvailable))
	{
		// 진행 중인 MID는 재사용한다. Texture 변경이 쿨타임이나 사용 가능 상태를 초기화하지 않는다.
		if (AbilityMID)
		{
			if (UTexture* SourceTexture = Cast<UTexture>(DefaultIconBrush.GetResourceObject()))
			{
				AbilityMID->SetTextureParameterValue(TEXT("IconTexture"), SourceTexture);
			}
			if (AbilityIcon)
			{
				AbilityIcon->SetBrushFromMaterial(AbilityMID);
			}
			SyncMaterialState();
			return;
		}
	}
	AbilityMID = nullptr;
	if (AbilityIcon)
	{
		AbilityIcon->SetBrush(DefaultIconBrush);
	}
	if (bCooldowning || (bChargePresentation && !bChargeAvailable))
	{
		EnsureMasterMaterial();
	}
}

void UAbilityIconUI::SetChargePresentation(bool bAvailable, float RechargeDuration, float RechargeRemaining)
{
	bChargePresentation = true;
	bChargeAvailable = bAvailable;
	if (RechargeDuration > 0.0f)
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		SetCoolTimeAt(RechargeDuration, Now - (RechargeDuration - FMath::Clamp(RechargeRemaining, 0.0f, RechargeDuration)));
	}
	else
	{
		CooldownDone();
	}
	if (!bAvailable)
	{
		EnsureMasterMaterial();
	}
	SyncMaterialState();
}

void UAbilityIconUI::ResetChargePresentation()
{
	bChargePresentation = false;
	bChargeAvailable = true;
	CooldownDone();
	ResetChargeFeedback();
}

void UAbilityIconUI::PlayFullChargeFeedback()
{
	ResetChargeFeedback();
	if (GlowImage)
	{
		FullChargeFeedbackElapsed = 0.0f;
		GlowImage->SetRenderScale(GlowBaseScale * FullChargeFeedbackScale);
	}
}

void UAbilityIconUI::UpdateChargeFeedback(float DeltaTime)
{
	if (!GlowImage || FullChargeFeedbackElapsed < 0.0f)
	{
		return;
	}
	FullChargeFeedbackElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(FullChargeFeedbackElapsed / FMath::Max(FullChargeFeedbackDuration, 0.01f), 0.0f, 1.0f);
	GlowImage->SetRenderOpacity(1.0f - FMath::Abs(2.0f * Alpha - 1.0f));
	if (Alpha >= 1.0f)
	{
		ResetChargeFeedback();
	}
}

void UAbilityIconUI::ResetChargeFeedback()
{
	FullChargeFeedbackElapsed = -1.0f;
	if (GlowImage)
	{
		GlowImage->SetRenderScale(GlowBaseScale);
		GlowImage->SetRenderOpacity(0.0f);
	}
}

void UAbilityIconUI::EnsureMasterMaterial()
{
	if (AbilityMID)
	{
		return;
	}

	if (!AbilityIcon || !M_AbilityIconMaster || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	AbilityIcon->SetBrushFromMaterial(M_AbilityIconMaster);
	AbilityMID = AbilityIcon->GetDynamicMaterial();

	if (!AbilityMID)
	{
		return;
	}

	if (UTexture* SourceTexture = Cast<UTexture>(DefaultIconBrush.GetResourceObject()))
	{
		AbilityMID->SetTextureParameterValue(TEXT("IconTexture"), SourceTexture);
	}

	SyncMaterialState();
}

void UAbilityIconUI::SyncMaterialState()
{
	if (!AbilityMID)
	{
		return;
	}

	const float CooldownProgress = bCooldowning && CoolTime > 0.0f
		? FMath::Clamp(AccumulatedTime / CoolTime, 0.0f, 1.0f)
		: 0.0f;

	AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), CooldownProgress);
	AbilityMID->SetScalarParameterValue(TEXT("IsCoolDown"), bCooldowning ? 1.0f : 0.0f);
	AbilityMID->SetScalarParameterValue(TEXT("IsUnavailable"),
		(bChargePresentation ? !bChargeAvailable : bCooldowning) ? 1.0f : 0.0f);
}

void UAbilityIconUI::TryRestoreDefaultBrush()
{
	if (bCooldowning || (bChargePresentation && !bChargeAvailable))
	{
		return;
	}

	AbilityMID = nullptr;

	if (AbilityIcon)
	{
		AbilityIcon->SetBrush(DefaultIconBrush);
	}
}

void UAbilityIconUI::AbilityUnLock()
{
	bAbilityUnlocked = true;

	if (AbilityIcon)
	{
		if (!bCooldowning && !(bChargePresentation && !bChargeAvailable))
		{
			AbilityIcon->SetBrush(DefaultIconBrush);
		}
		AbilityIcon->SetVisibility(ESlateVisibility::Visible);
	}
}

void UAbilityIconUI::AbilityLock()
{
	bAbilityUnlocked = false;
	bChargePresentation = false;
	ResetChargeFeedback();
	CooldownDone();

	if (AbilityIcon)
	{
		AbilityIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UAbilityIconUI::VisibilityControl(bool InFlag)
{
	if (InFlag)
	{
		this->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		this->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UAbilityIconUI::IsUnLock() const
{
	return bAbilityUnlocked;
}

void UAbilityIconUI::SetCoolTime(float InCoolTime)
{
	const UWorld* World = GetWorld();
	SetCoolTimeAt(InCoolTime, World ? World->GetTimeSeconds() : 0.0f);
}

void UAbilityIconUI::SetCoolTimeAt(float InCoolTime, float InStartTime)
{
	if (!AbilityIcon || !M_AbilityIconMaster || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime;
	CooldownStartTime = InStartTime;

	const UWorld* World = GetWorld();
	AccumulatedTime = World
		? FMath::Max(0.0f, World->GetTimeSeconds() - CooldownStartTime)
		: 0.0f;
	bCooldowning = true;

	EnsureMasterMaterial();
	SyncMaterialState();
}

void UAbilityIconUI::UpdateCoolTime(float Delta)
{
	if (!bCooldowning || !AbilityMID || CoolTime <= 0.0f)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		AccumulatedTime = FMath::Max(0.0f, World->GetTimeSeconds() - CooldownStartTime);
	}
	else
	{
		AccumulatedTime += Delta;
	}

	if (AccumulatedTime >= CoolTime)
	{
		if (bChargePresentation)
		{
			// 로컬 예상 시간이 끝나도 충전 완료를 확정하지 않는다. 서버의 다음 주기/완충 상태를 기다린다.
			AccumulatedTime = CoolTime;
			SyncMaterialState();
			return;
		}
		CooldownDone();
		return;
	}

	SyncMaterialState();
}

bool UAbilityIconUI::IsCooldowning() const
{
	if (!IsUnLock())
	{
		return false;
	}

	return bCooldowning;
}

void UAbilityIconUI::CooldownDone()
{
	bCooldowning = false;
	AccumulatedTime = 0.0f;
	CooldownStartTime = 0.0f;
	CoolTime = 0.0f;

	SyncMaterialState();
	TryRestoreDefaultBrush();
}
