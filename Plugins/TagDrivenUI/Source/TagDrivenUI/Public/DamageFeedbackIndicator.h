// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageFeedbackIndicator.generated.h"

class AActor;

/**
 * Pooled directional damage indicator owned by UDamageFeedbackLayer.
 * The WBP art should face up at zero degrees; the layer places it at screen center and this widget rotates itself.
 * Ticking is driven by the layer, so native tick is disabled.
 */
UCLASS(Abstract, Blueprintable, meta = (DisableNativeTick))
class TAGDRIVENUI_API UDamageFeedbackIndicator : public UUserWidget
{
	GENERATED_BODY()

public:
	// 풀에서 꺼내 쓸 때 Layer 가 호출한다. 방향을 계산할 수 없으면 정리하고 false 를 돌려준다.
	bool StartFeedback(AActor* InCharacter, const FVector& InDamageOrigin);

	// Layer Tick 에서 호출한다. 수명이 끝났거나 대상이 사라지면 스스로 정리하고 false 를 돌려준다.
	bool TickFeedback(float DeltaTime);

	void ClearFeedback();

	bool IsFeedbackActive() const { return bFeedbackActive; }
	float GetFeedbackElapsed() const { return FeedbackElapsed; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback", meta = (ClampMin = "0.0"))
	float FeedbackDuration = 0.5f;

	// 피드백이 활성화된 동안에만 호출된다. Material 효과는 서브클래스에서 여기 처리한다.
	virtual void HandleFeedbackTick(float NormalizedElapsedTime) {}

	// false 면 피격 순간에만 방향을 잡고, 이후 플레이어 회전에 따른 Tick 갱신은 하지 않는다.
	virtual bool ShouldTrackDamageDirection() const { return true; }

private:
	bool UpdateFeedbackRotation();

	TWeakObjectPtr<AActor> FeedbackCharacter;
	FVector DamageOrigin = FVector::ZeroVector;
	bool bFeedbackActive = false;
	float FeedbackElapsed = 0.0f;
};
