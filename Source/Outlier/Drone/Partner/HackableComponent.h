#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "HackType.h"
#include "HackableComponent.generated.h"

class UHackableComponent;
class USceneComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnHackTargetUnavailable, UHackableComponent*);

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnHackTargetInvalidated,
	UHackableComponent*,
	EEndPlayReason::Type
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnHackEffectTriggered,
	FGameplayTag, EffectTag,
	const FHackResultContext&, Context
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnCheckpointHackStateRestored,
	bool, bHacked
);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UHackableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHackableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_HackTags, Category = "Hack")
	FGameplayTagContainer HackTags;  // Target/State/Query

	// 같은 Actor의 일반 SceneComponent를 선택한다. 미지정이면 Actor별 기존 계산을 유지한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Target",
		meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
	FComponentReference HackTargetPointReference;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Effect", meta = (Categories = "Hack.Effect"))
	FGameplayTagContainer SuccessEffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Effect", meta = (Categories = "Hack.Effect"))
	FGameplayTagContainer FailEffectTags;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackEffectTriggered OnHackEffectTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnCheckpointHackStateRestored OnCheckpointHackStateRestored;

	// Enemy처럼 체크포인트 진행에 포함되지 않는 Hack 대상은 비워둔다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Hack|Checkpoint")
	FName CheckpointProgressId = NAME_None;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRegister() override;
	virtual void InitializeComponent() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 생성/재등록 또는 명시적인 참조 변경 후 호출한다. 위치 조회 중에는 이름 검색을 하지 않는다.
	void RefreshHackTargetPoint();
	bool TryGetHackTargetLocation(FVector& OutLocation) const;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	FOnHackTargetInvalidated OnHackTargetInvalidated;
	FOnHackTargetUnavailable OnHackTargetUnavailable;
	void NotifyHackTargetUnavailable();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool CanBeHackTarget(const FHackQueryContext& Context) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool MatchesHackQuery(const FGameplayTagQuery& Query) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void CompleteHack(const FHackResultContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool HasHackTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackTargetType() const;

	// Hack.MiniGame 하위 태그가 없거나 None이면 미니게임 없이 바로 성공 처리한다.
	bool RequiresMiniGame() const;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastTriggerHackEffects(const FGameplayTagContainer& EffectTags, const FHackResultContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void MarkAsHackedOnce();

private:
	bool HasHackTargetPointReference() const;
	USceneComponent* ResolveHackTargetPoint() const;
	TWeakObjectPtr<USceneComponent> CachedHackTargetPoint;

	UFUNCTION()
	void OnRep_HackTags();

	const FGameplayTagContainer& ResolveHackEffectTags(EHackResult Result) const;
	mutable bool bLoggedHackedOnceBlock = false;
	bool bProgressIdRegistered = false;

};
