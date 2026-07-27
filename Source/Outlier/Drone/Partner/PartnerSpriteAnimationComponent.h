// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerSpriteAnimationComponent.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;

UENUM(BlueprintType)
enum class EPartnerEmotion : uint8
{
	Default,
	Sad,
	Happy,
	Angry,
	Surprised,
	Closed // Transition
};

USTRUCT(BlueprintType)
struct FPartnerSpriteAnimationData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 Rows = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 Columns = 1;
};

UCLASS()
class OUTLIER_API UPartnerSpriteAnimationComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerSpriteAnimationComponent();

	/** Requests an emotion change. Client calls are forwarded to the server. */
	UFUNCTION(BlueprintCallable, Category = "Sprite")
	void SetEmotion(EPartnerEmotion InPartnerEmotion);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite")
	TMap<EPartnerEmotion, FPartnerSpriteAnimationData> EmotionSprites;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite|Material")
	FName MaterialSlotName = TEXT("Eye");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite|Material")
	FName TextureParameterName = TEXT("SpriteTexture");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprite|Transition", meta = (ClampMin = "0.0"))
	float ClosedTime = 2.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentEmotion, Category = "Sprite")
	EPartnerEmotion CurrentEmotion = EPartnerEmotion::Default;

private:
	UFUNCTION(Server, Reliable)
	void ServerSetEmotion(EPartnerEmotion InPartnerEmotion);

	UFUNCTION()
	void OnRep_CurrentEmotion();

	void SetEmotionAuthority(EPartnerEmotion InPartnerEmotion);
	void StartEmotionTransition();
	void FinishEmotionTransition();
	bool ApplyEmotionTexture(EPartnerEmotion Emotion);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMID;

	FTimerHandle EmotionTransitionTimerHandle;
	EPartnerEmotion DisplayedEmotion = EPartnerEmotion::Default;
	uint8 bTransitioning : 1 = false;
};
