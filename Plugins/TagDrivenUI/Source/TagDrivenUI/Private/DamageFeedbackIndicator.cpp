// Fill out your copyright notice in the Description page of Project Settings.

#include "DamageFeedbackIndicator.h"

#include "GameFramework/Actor.h"

bool UDamageFeedbackIndicator::StartFeedback(AActor* InCharacter, const FVector& InDamageOrigin)
{
	if (!IsValid(InCharacter))
	{
		return false;
	}

	FeedbackCharacter = InCharacter;
	DamageOrigin = InDamageOrigin;

	if (!UpdateFeedbackRotation())
	{
		ClearFeedback();
		return false;
	}

	bFeedbackActive = true;
	FeedbackElapsed = 0.0f;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	HandleFeedbackTick(0.0f);
	return true;
}

bool UDamageFeedbackIndicator::TickFeedback(const float DeltaTime)
{
	if (!bFeedbackActive)
	{
		return false;
	}

	FeedbackElapsed += DeltaTime;
	const float Duration = FMath::Max(FeedbackDuration, KINDA_SMALL_NUMBER);

	// 추적하는 인디케이터는 캐릭터가 회전하거나 이동하면 FeedbackDuration 동안 매 Tick 방향을 다시 계산한다.
	// 추적하지 않는 인디케이터는 피격 순간의 방향을 유지하고 캐릭터 유효성만 확인한다.
	const bool bStillValid = ShouldTrackDamageDirection()
		? UpdateFeedbackRotation()
		: IsValid(FeedbackCharacter.Get());
	if (FeedbackElapsed >= Duration || !bStillValid)
	{
		ClearFeedback();
		return false;
	}

	HandleFeedbackTick(FeedbackElapsed / Duration);
	return true;
}

void UDamageFeedbackIndicator::ClearFeedback()
{
	bFeedbackActive = false;
	FeedbackElapsed = 0.0f;
	FeedbackCharacter.Reset();
	DamageOrigin = FVector::ZeroVector;
	SetVisibility(ESlateVisibility::Collapsed);
}

bool UDamageFeedbackIndicator::UpdateFeedbackRotation()
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

	// Pivot 은 Layer 가 슬롯 Alignment 와 같은 값으로 잡아두므로 앵커 지점을 축으로 돈다.
	SetRenderTransformAngle(DamageAngle);
	return true;
}
