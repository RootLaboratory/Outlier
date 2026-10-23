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

	/** 표정을 유지한다. 클라이언트 요청은 서버로 전달하고, Default로 해제한다. */
	UFUNCTION(BlueprintCallable, Category = "Sprite")
	void SetEmotion(EPartnerEmotion InPartnerEmotion);

	/** 스킬 표정을 잠시 보여 준 뒤 Default로 자동 복귀한다. */
	UFUNCTION(BlueprintCallable, Category = "Sprite")
	void SetTemporaryEmotion(EPartnerEmotion InPartnerEmotion);

	/** 평상시와 대기 중의 눈 깜빡임을 서버에서 시작하거나 중단한다. */
	void SetBlinkingActive(bool bActive);

	/** 사망/재부팅 중에는 다른 표정과 타이머보다 감은 눈을 우선한다. 서버 전용이다. */
	void SetDeathEmotionActive(bool bActive);

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
	float ClosedTime = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite", meta = (ClampMin = "0.1", Units = "s"))
	float TemporaryEmotionDuration = 1.0f;

	/** 기존 BP 설정 이름은 유지하며, 이 간격은 이동 중과 대기 중 모두 적용한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite|Standby", meta = (ClampMin = "0.1", Units = "s"))
	float StandbyBlinkIntervalMin = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite|Standby", meta = (ClampMin = "0.1", Units = "s"))
	float StandbyBlinkIntervalMax = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprite|Standby", meta = (ClampMin = "0.05", Units = "s"))
	float StandbyBlinkClosedDuration = 0.15f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentEmotion, Category = "Sprite")
	EPartnerEmotion CurrentEmotion = EPartnerEmotion::Default;

private:
	friend class FPartnerStandbyTest;
	UFUNCTION(Server, Reliable)
	void ServerSetEmotion(EPartnerEmotion InPartnerEmotion, bool bTemporary);

	UFUNCTION()
	void OnRep_CurrentEmotion();

	void SetEmotionAuthority(EPartnerEmotion InPartnerEmotion, bool bFromBlink = false);
	void RequestEmotion(EPartnerEmotion InPartnerEmotion, bool bTemporary);
	void ResetTemporaryEmotion();
	void ScheduleBlink();
	void PlayBlink();
	void FinishBlink();
	void StartEmotionTransition();
	void FinishEmotionTransition();
	bool ApplyEmotionTexture(EPartnerEmotion Emotion);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMID;

	int32 SpriteMaterialIndex = INDEX_NONE;

	FTimerHandle EmotionTransitionTimerHandle;
	FTimerHandle TemporaryEmotionTimerHandle;
	FTimerHandle BlinkTimerHandle;
	bool bBlinkingActive = false;
	bool bOwnsBlink = false;
	bool bDeathEmotionActive = false;
	EPartnerEmotion DisplayedEmotion = EPartnerEmotion::Default;
	uint8 bTransitioning : 1 = false;
};
