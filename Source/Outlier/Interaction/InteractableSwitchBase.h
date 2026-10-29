#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"
#include "Interface/InteractableInterface.h"
#include "InteractableSwitchBase.generated.h"

class AFirstPersonCharacter;
class UHackableComponent;
class UInteractableComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 스위치 공통 흐름: 상호작용 검사, 해킹 잠금, 진행 저장, 활성화 통보까지 맡고
 * 실제 대상 조작은 자식의 ActivateTarget에 넘긴다.
 *
 * 해킹 여부는 BP 데이터로 정한다. InteractableTags에 State.Locked, HackTags에 Hack.Target 태그가 있으면
 * Hack.Effect.Unblock 성공 전까지 잠기고, 태그가 없으면 해킹 후보에서 빠진 일반 스위치로 동작한다.
 */
UCLASS(Abstract)
class OUTLIER_API AInteractableSwitchBase : public AActor, public IInteractableInterface, public IHackableInterface
{
	GENERATED_BODY()

public:
	AInteractableSwitchBase();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

	virtual EScanRevealCondition GetScanRevealCondition() const override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Switch|Hack")
	bool IsInteractionBlocked() const;

	UFUNCTION(BlueprintPure, Category = "Switch|Hack")
	bool IsHacked() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 서버에서만 불린다. 대상을 조작했으면 true, false면 상호작용 자체가 거절된다. */
	virtual bool ActivateTarget() PURE_VIRTUAL(AInteractableSwitchBase::ActivateTarget, return false;);

	/** 서버 전용. ActivateTarget 성공 뒤 활성화 상태 저장, 사운드, BP 통보를 처리한다. 상호작용 외 경로(해킹 등)에서도 쓴다. */
	void CommitActivation(AFirstPersonCharacter* Interactor);

	void ApplyMaterialState(bool bHacked);

	UFUNCTION(BlueprintImplementableEvent, Category = "Switch")
	void OnSwitchActivated(AFirstPersonCharacter* Interactor);

	/** 모든 머신에서 해킹 상태를 머티리얼에 적용한 뒤 통보한다. 색상은 머티리얼에서 설정한다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Switch|Material")
	void OnHackedStateChanged(bool bHacked);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UHackableComponent> HackableComponent;

	/** 이 해킹 성공 이펙트가 오면 State.Locked를 풀어 상호작용을 허용한다. 기본은 Hack.Effect.Unblock. */
	FGameplayTag UnblockEffectTag;

	/** Material slot on SwitchMesh that the Activated / Hacked state parameters live on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	int32 SwitchMaterialSlot = 0;

	/** Shared switch material parameter: 1 after hacking, 0 while locked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	FName ActivatedScalarParamName = TEXT("Activated");

	/** Scalar (float) parameter that flags whether the switch has been hacked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	FName HackedScalarParamName = TEXT("Hacked");

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FInteractableSwitchDoorBindingTest;
#endif

	UFUNCTION()
	void OnRep_IsActivated();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnSwitchActivated(AFirstPersonCharacter* Interactor);

	UFUNCTION()
	void HandleCheckpointHackStateRestored(bool bHacked);

	void ApplySwitchActivated(AFirstPersonCharacter* Interactor);
	void CacheSwitchMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SwitchMID;

	bool bProgressIdRegistered = false;
	bool bActivationEventApplied = false;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> SwitchMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch")
	FName SwitchId = NAME_None;

	UPROPERTY(ReplicatedUsing = OnRep_IsActivated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Switch")
	bool bIsActivated = false;
};
