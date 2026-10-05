// Fill out your copyright notice in the Description page of Project Settings.

#include "DamageFeedbackIndicator.h"

#include "Components/Image.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogDamageFeedbackIndicator, Log, All);

void UDamageFeedbackIndicator::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UImage* Image = GetFeedbackImage();
	if (!Image)
	{
		UE_LOG(LogDamageFeedbackIndicator, Warning, TEXT("Feedback image is not bound: %s"), *GetName());
		return;
	}

	BaseImageOpacity = Image->GetRenderOpacity();
	FeedbackMID = Image->GetDynamicMaterial();
	if (!FeedbackMID)
	{
		return; // 머티리얼이 없는 브러시도 Image RenderOpacity 로 페이드한다.
	}

	float UnusedValue = 0.0f;
	bHasTimeParameter = FeedbackMID->GetScalarParameterValue(FHashedMaterialParameterInfo(TimeParameterName), UnusedValue);
	bHasAlphaParameter = FeedbackMID->GetScalarParameterValue(FHashedMaterialParameterInfo(AlphaParameterName), UnusedValue);
}

float UDamageFeedbackIndicator::GetFeedbackDuration() const
{
	return FMath::Max(0.0f, FadeInTime) + FMath::Max(0.0f, FadeOutTime);
}

void UDamageFeedbackIndicator::HandleFeedbackTick(float NormalizedElapsedTime)
{
	(void)NormalizedElapsedTime;

	const float InTime = FMath::Max(0.0f, FadeInTime);
	const float OutTime = FMath::Max(0.0f, FadeOutTime);
	const float FadeInProgress = InTime > KINDA_SMALL_NUMBER
		? FMath::Clamp(FeedbackElapsed / InTime, 0.0f, 1.0f) : 1.0f;
	const float Phase = FadeInCurve ? FadeInCurve->GetFloatValue(FadeInProgress) : FadeInProgress;

	// TimeScale 이 있는 Red 는 FadeIn 동안 머티리얼 애니메이션이 진행된다.
	// TimeScale 이 없는 Shield 는 같은 커브 값을 Alpha 에 적용해 나타나게 한다.
	float Alpha = bHasTimeParameter ? 1.0f : FMath::Clamp(Phase, 0.0f, 1.0f);
	if (FeedbackElapsed >= InTime)
	{
		const float FadeOutProgress = OutTime > KINDA_SMALL_NUMBER
			? FMath::Clamp((FeedbackElapsed - InTime) / OutTime, 0.0f, 1.0f) : 1.0f;
		Alpha *= FadeOutCurve ? FadeOutCurve->GetFloatValue(FadeOutProgress) : 1.0f - FadeOutProgress;
	}

	if (FeedbackMID)
	{
		if (bHasTimeParameter)
		{
			FeedbackMID->SetScalarParameterValue(TimeParameterName, Phase);
		}
		if (bHasAlphaParameter)
		{
			FeedbackMID->SetScalarParameterValue(AlphaParameterName, Alpha);
		}
	}

	if (!bHasAlphaParameter)
	{
		// Alpha 파라미터가 없거나 브러시가 머티리얼이 아니어도 페이드가 보이도록 한다.
		if (UImage* Image = GetFeedbackImage())
		{
			Image->SetRenderOpacity(BaseImageOpacity * FMath::Clamp(Alpha, 0.0f, 1.0f));
		}
	}
}

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
	const float Duration = FMath::Max(GetFeedbackDuration(), KINDA_SMALL_NUMBER);

	// 추적하는 인디케이터는 캐릭터가 회전하거나 이동하면 수명 동안 매 Tick 방향을 다시 계산한다.
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
