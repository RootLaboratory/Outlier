// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VisualEventType.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectKey.h"
#include "VisualEventSettings.h"
#include "VisualEventSubsystem.generated.h"


/**
 * 
 */

class UProjectionMarkDefinition;
class UTrailEffectDefinition;
class USoundDefinition;
class UDecalComponent;
class UNiagaraSystem;
class UNiagaraComponent;
struct FStreamableHandle;

USTRUCT()
struct FPendingTaggedVisualEvent
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGameplayTag> Contexts;
	UPROPERTY()
	FVector Location = FVector::ZeroVector;
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY()
	FVisualEventSet Fallback;
};

UCLASS()
class VISUALEVENT_API UVisualEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void BeginEndingPresentation();

	// 사운드는 Audio Subsystem이 처리하며 여기서는 데칼/VFX만 독립적으로 선택한다.
	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void PlayTaggedEffectsAtLocation(const TArray<FGameplayTag>& Contexts, const FVector& Location,
		const FRotator& Rotation, const FVisualEventSet& Fallback);

	static FVisualEventSet ResolveTaggedEffects(const TArray<FGameplayTag>& Contexts,
		const TMap<FGameplayTag, FVisualEventSet>& Definitions, const FVisualEventSet& Fallback);

public:


	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnMarkAtLocation(UProjectionMarkDefinition* Def, FVector Location, FRotator Rotation); 

	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnBeamTrail(const UTrailEffectDefinition* Def, const FVector& Start, const FVector& End);

	// User.Trigger( bool )가 있는 트레이서는 Trigger 가 바뀌어야 발사한다. Definition 의 TriggerMode 에 따라 스폰할 때 넣어 준다.
	static const FName TriggerParameterName;
	static bool HasUserParameter(const UNiagaraSystem* System, FName ParameterName);

	// HasUserParameter( System, TriggerParameterName ) 결과를 시스템별로 캐시한다.
	bool SupportsTrigger(const UNiagaraSystem* System);


	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnProjectileTrail(const UTrailEffectDefinition* Def, USceneComponent* AttachTarget);

	UFUNCTION(BlueprintCallable, Category = "Sound Event")
	void PlaySoundAtLocation(USoundDefinition* SoundDefinition, FVector Location);


	UFUNCTION(BlueprintCallable, Category = "Set Event")
	void FeaturesEffect(FVector Location, FRotator Rotation, FVisualEventSet& EffectSet);

	UFUNCTION(BlueprintCallable, Category = "Visual Event")

	// 호출자는 시점만 넘긴다. 실제 크기 보정값은 Definition 이 갖는다
	// ( UTrailEffectDefinition::GetViewpointScale ).
	void SpawnMuzzleEffect(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation, bool bFirstPerson = false);

	//일단 Trail으로 받지만 따로 분리할 것임.
	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnEffectAtLocation(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation);

private:
	bool bEndingPresentation = false;
	void FinishLoadingTaggedEffects();

	UPROPERTY(Transient)
	TMap<FGameplayTag, FTaggedVisualEventDefinition> TaggedDefinitions;

	UPROPERTY(Transient)
	TMap<FGameplayTag, FVisualEventSet> LoadedTaggedEffects;

	UPROPERTY(Transient)
	TArray<FPendingTaggedVisualEvent> PendingTaggedEffects;

	TSharedPtr<FStreamableHandle> TaggedEffectsLoadHandle;
	bool bTaggedEffectsLoading = false;
	bool bAcceptTaggedEffects = false;

	// Trigger 가 있는 시스템이면 TriggerMode 에 맞춰 true / false 를 넣는다. Always 인데 Trigger 가 없으면 한 번만 경고한다.
	void ApplyTrigger(UNiagaraComponent* Comp, const UNiagaraSystem* System, const UTrailEffectDefinition* Def);

	// 키는 약한 참조라 에셋이 언로드돼도 붙잡지 않는다.
	TMap<TObjectKey<UNiagaraSystem>, bool> TriggerSupportCache;
	TSet<TObjectKey<UNiagaraSystem>> WarnedMissingTrigger;

	static constexpr int32 MaxActiveMarks = 128;
	TArray<TWeakObjectPtr<UDecalComponent>> ActiveMarks;

};
