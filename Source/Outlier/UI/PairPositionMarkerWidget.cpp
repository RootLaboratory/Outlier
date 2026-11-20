#include "UI/PairPositionMarkerWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "OutlierPlayerState.h"

UPairPositionMarkerWidget::UPairPositionMarkerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), TargetRole(EOutlierPlayerRole::None)
{
	HudPresentation.MinScale = 1.0f;
	HudPresentation.MaxScale = 1.0f;
	HudPresentation.bKeepBehindCamera = true;
}

void UPairPositionMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyRoleIcon();
	SetRearIndicatorVisible(false);
	StopDamagePulse();
}

void UPairPositionMarkerWidget::SetRearIndicatorVisible(bool bVisible)
{
	if (RearIndicator)
	{
		RearIndicator->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UPairPositionMarkerWidget::NativeDestruct()
{
	StopDamagePulse();
	Super::NativeDestruct();
}

void UPairPositionMarkerWidget::PlayDamagePulse(float RemainingSeconds)
{
	UWorld* World = GetWorld();
	if (!RoleIcon || !World || !FMath::IsFinite(RemainingSeconds) || RemainingSeconds <= 0.0f)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	if (Timers.GetTimerRemaining(DamagePulseTimer) <= 0.0f)
	{
		StopDamagePulse();
		OriginalRoleIconTint = RoleIcon->GetColorAndOpacity();
		FLinearColor HitTint = DamagePulseColor;
		HitTint.A = OriginalRoleIconTint.GetValue().A;
		RoleIcon->SetColorAndOpacity(HitTint);
	}
	// 연속 피격은 현재 색에서 다시 복귀를 시작해 색이 갑자기 바뀌지 않도록 한다.
	DamagePulseStartTint = RoleIcon->GetColorAndOpacity();
	Timers.SetTimer(DamagePulseTimer, this, &ThisClass::StopDamagePulse, RemainingSeconds, false);
}

void UPairPositionMarkerWidget::StopDamagePulse()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DamagePulseTimer);
	}
	if (OriginalRoleIconTint.IsSet() && RoleIcon)
	{
		RoleIcon->SetColorAndOpacity(OriginalRoleIconTint.GetValue());
	}
	OriginalRoleIconTint.Reset();
}

void UPairPositionMarkerWidget::UpdateDamagePulse()
{
	if (!OriginalRoleIconTint.IsSet())
	{
		return;
	}
	if (const UWorld* World = GetWorld())
	{
		const FTimerManager& Timers = World->GetTimerManager();
		const float Rate = Timers.GetTimerRate(DamagePulseTimer);
		if (Rate > 0.0f)
		{
			ApplyDamagePulseTint(Timers.GetTimerRemaining(DamagePulseTimer) / Rate);
		}
	}
}

void UPairPositionMarkerWidget::ApplyDamagePulseTint(float RemainingFraction)
{
	const float Blend = FMath::Clamp(RemainingFraction, 0.0f, 1.0f);
	RoleIcon->SetColorAndOpacity(FMath::Lerp(OriginalRoleIconTint.GetValue(), DamagePulseStartTint, Blend));
}

void UPairPositionMarkerWidget::SetTargetRole(EOutlierPlayerRole Role)
{
	if (TargetRole != Role)
	{
		TargetRole = Role;
		ApplyRoleIcon();
	}
}

void UPairPositionMarkerWidget::ApplyRoleIcon()
{
	if (!RoleIcon)
	{
		return;
	}

	UTexture2D* Texture = nullptr;
	switch (TargetRole)
	{
	case EOutlierPlayerRole::Shooter:
		Texture = ShooterIcon.Get();
		break;
	case EOutlierPlayerRole::Partner:
		Texture = PartnerIcon.Get();
		break;
	default:
		break;
	}
	// WBP의 ScaleBox가 역할별 원본 비율을 유지할 수 있도록 브러시 크기도 갱신한다.
	if (Texture && RoleIcon->GetBrush().GetResourceObject() == Texture)
	{
		RoleIcon->SetBrushSize(FVector2D(Texture->GetSizeX(), Texture->GetSizeY()));
	}
	RoleIcon->SetBrushFromTexture(Texture, true);
}
