// Fill out your copyright notice in the Description page of Project Settings.

#include "DamageFeedBackWidget.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "GameFramework/Actor.h"

void UDamageFeedBackWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// MainWidget의 Canvas에서 화면 중앙을 회전 원점으로 사용한다.
	// Anchor/Alignment는 내부 Root가 아니라 이 UserWidget의 바깥 Canvas 슬롯에 적용해야 한다.
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
	}

	if (Root)
	{
		// Red/Shield를 합친 VerticalBox의 하단 중앙을 실제 회전축으로 사용한다.
		Root->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));
	}

	ClearDamageFeedback();
}

void UDamageFeedBackWidget::ShowDamageFeedback(
	AActor* InCharacter,
	const FVector& InDamageOrigin)
{
	if (!IsValid(InCharacter))
	{
		return;
	}

	FeedbackCharacter = InCharacter;
	DamageOrigin = InDamageOrigin;

	if (!UpdateFeedbackRotation())
	{
		FeedbackCharacter.Reset();
		return;
	}

	if (RedFeedback)
	{
		RedFeedback->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (ShieldFeedback)
	{
		ShieldFeedback->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);

	bFeedbackActive = true;
	FeedbackElapsed = 0.0f;
}

void UDamageFeedBackWidget::ClearDamageFeedback()
{
	bFeedbackActive = false;
	FeedbackElapsed = 0.0f;
	FeedbackCharacter.Reset();
	DamageOrigin = FVector::ZeroVector;

	if (RedFeedback)
	{
		RedFeedback->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (ShieldFeedback)
	{
		ShieldFeedback->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UDamageFeedBackWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bFeedbackActive)
	{
		return;
	}

	FeedbackElapsed += InDeltaTime;
	const float Duration = FMath::Max(FeedbackDuration, KINDA_SMALL_NUMBER);
	HandleDamageFeedbackTick(FMath::Clamp(FeedbackElapsed / Duration, 0.0f, 1.0f));
	if (!bFeedbackActive)
	{
		return;
	}

	if (FeedbackElapsed >= Duration)
	{
		ClearDamageFeedback();
	}
}

void UDamageFeedBackWidget::HandleDamageFeedbackTick(const float NormalizedElapsedTime)
{
	// Material 효과 추가 시 사용할 수 있도록 정규화 시간 인자는 C++ 경로에 유지한다.
	(void)NormalizedElapsedTime;

	if (!UpdateFeedbackRotation())
	{
		ClearDamageFeedback();
	}
}

bool UDamageFeedBackWidget::UpdateFeedbackRotation()
{
	const AActor* Character = FeedbackCharacter.Get();
	if (!IsValid(Character))
	{
		return false;
	}

	const FVector2D PlayerForward = FVector2D(Character->GetActorForwardVector()).GetSafeNormal();
	const FVector2D ToDamageSource = FVector2D(DamageOrigin - Character->GetActorLocation()).GetSafeNormal();
	if (PlayerForward.IsNearlyZero() || ToDamageSource.IsNearlyZero())
	{
		return false;
	}

	const float Dot = FVector2D::DotProduct(PlayerForward, ToDamageSource);
	const float CrossZ = PlayerForward.X * ToDamageSource.Y - PlayerForward.Y * ToDamageSource.X;
	const float DamageAngle = FMath::RadiansToDegrees(FMath::Atan2(CrossZ, Dot));

	// 기본 아트가 화면 위를 향한다고 가정한다. 캐릭터가 회전하거나 이동하면
	// FeedbackDuration 동안 현재 방향을 기준으로 매 Tick 다시 계산한다.
	SetFeedbackRotation(DamageAngle);
	return true;
}

void UDamageFeedBackWidget::SetFeedbackRotation(const float RenderAngle)
{
	if (Root)
	{
		Root->SetRenderTransformAngle(RenderAngle);
	}
}
