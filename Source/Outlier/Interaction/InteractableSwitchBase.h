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

	UFUNCTION(BlueprintImplementableEvent, Category = "Switch")
	void OnSwitchActivated(AFirstPersonCharacter* Interactor);

	/**
	 * Fires whenever the hacked visual state is (re)applied - BeginPlay sync and on hack
	 * success - on every machine. Left as an extra hook in case material parameter access
	 * ends up moving to BP instead of the C++ MID push below.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Switch|Material")
	void OnHackedStateChanged(bool bHacked);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UHackableComponent> HackableComponent;

	/** Material slot on SwitchMesh that the Color / Hacked parameters live on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	int32 SwitchMaterialSlot = 0;

	/** Scalar (float) parameter that flags whether the switch has been hacked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	FName HackedScalarParamName = TEXT("Hacked");

	/** Vector/color parameter on the switch mesh's material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	FName SwitchColorParamName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	FLinearColor LockedColor = FLinearColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Material")
	FLinearColor HackedColor = FLinearColor::Green;

private:
	UFUNCTION()
	void OnRep_IsActivated();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnSwitchActivated(AFirstPersonCharacter* Interactor);

	UFUNCTION()
	void HandleCheckpointHackStateRestored(bool bHacked);

	void ApplySwitchActivated(AFirstPersonCharacter* Interactor);
	void CacheSwitchMaterial();
	void ApplyMaterialState(bool bHacked);

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
