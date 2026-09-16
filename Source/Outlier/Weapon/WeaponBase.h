// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Templates/SubclassOf.h"
#include "Interface/InteractableInterface.h"
#include "PostProcess/OutlierStealthVisualTarget.h"
#include "Weapon/WeaponDataTypes.h"
#include "WeaponBase.generated.h"

class USkeletalMeshComponent;
class UMeshComponent;
class USceneComponent;
class USphereComponent;
class AFirstPersonCharacter;
class UInteractableComponent;
class UProceduralAnimValues;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	Unarmed,
	Pistol,
	Rifle,
	Melee
};

UCLASS(Abstract)
class OUTLIER_API AWeaponBase : public AActor, public IInteractableInterface, public IOutlierStealthVisualTarget
{
	GENERATED_BODY()

public:
	AWeaponBase();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USkeletalMeshComponent> FirstPersonWeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USkeletalMeshComponent> ThirdPersonWeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USkeletalMeshComponent> ShadowWeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USphereComponent> InteractionCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	FName WeaponName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	EWeaponType WeaponType = EWeaponType::Unarmed;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	EWeaponFireType FireType = EWeaponFireType::HitScan;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float Damage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float AttackInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float EffectiveRange = 1000.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	float MovementSpeedMultiplier = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Range")
	float DamageFalloffStartRange = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Range")
	float DamageFalloffMaxRange = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Range")
	float MinDamageMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = IK)
	FName LeftHandIKSocketName = FName("LeftHandIK");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = IK)
	FName LeftHandSprintIKSocketName = FName("LeftHandIK_Sprint");

	UPROPERTY(ReplicatedUsing = OnRep_EquippedState, VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<ACharacter> WeaponOwner;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedState, VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsEquipped : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsAttacking : 1 = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FDataTableRowHandle WeaponCoreRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UDataTable> WeaponRangeTable;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FName RangeProfileId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FDataTableRowHandle WeaponRangeRow;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	uint8 bWeaponDataInitialized : 1 = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Pickup")
	float DropInstigatorPickupBlockDuration = 0.35f;

	UPROPERTY(Transient)
	TWeakObjectPtr<AFirstPersonCharacter> DropPickupBlockedInteractor;

	UPROPERTY(Transient)
	float DropPickupBlockedUntilTime = 0.0f;

	// 픽업이 성사된 뒤 실제 파괴(다음 틱)까지의 짧은 구간을 막는다.
	// 이 사이에는 bIsEquipped/WeaponOwner 가 여전히 비어 있어서
	// 같은 틱에 들어온 두 번째 상호작용이 그대로 통과해버린다.
	UPROPERTY(Transient)
	uint8 bPickupConsumed : 1 = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Animation")
	TObjectPtr<UProceduralAnimValues> FirstPersonProceduralValues = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Noise")
	bool bReportArenaWideNoise = false;

protected:
	virtual void EnsureWeaponDataInitialized();
	virtual void InitializeFromDataTables();
	virtual void InitializeRangeFromDataTable();

	void SetEquippedCollisionEnabled(bool bEnabled);
	void SetPickupPresentation();
	// 소유자는 있는데 손에는 들지 않은 상태 (비활성 슬롯) 의 표현.
	// 메시만 숨기고 캐릭터 부착은 유지한다 — 여기서 떼면 액터 루트로 스냅백해서
	// 월드에 떨어진 것처럼 보인다.
	void SetStowedPresentation();
	void SetEquippedPresentation();
	void ApplyReplicatedPresentation();

	// 배치 액터를 즉시 파괴하지 않고 숨긴 뒤 다음 틱에 지운다.
	// AFirstPersonCharacter::ServerInteract 는 Interact() 가 true 를 돌려준 뒤에도
	// 이 액터의 InteractableComponent 와 액터 참조를 계속 쓴다.
	// (ASuitInteraction::ConsumeInteraction 이 같은 이유로 같은 형태를 쓴다.)
	void ConsumePickup();

	UFUNCTION()
	void DestroyAfterPickup();

	UFUNCTION()
	virtual void OnRep_EquippedState();

public:
	virtual void BeginPlay() override;

	virtual bool CanAttack() const;

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void StartAttack();

	virtual void StopAttack();

	virtual void PerformAttack();

	virtual void OnEquipped(ACharacter* NewOwner);

	virtual void AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner);
	void AttachWeaponMeshesToOwnerMeshes();
	virtual void ShowEquippedPresentation();
	virtual void RefreshShadowWeaponPresentation();

	// IOutlierStealthVisualTarget : 은신 적용 대상 메시만 알려준다 ( 적용/복구는 서브시스템 담당 ).
	virtual void CollectStealthMeshes(
		TArray<UMeshComponent*>& OutFirstPersonMeshes,
		TArray<UMeshComponent*>& OutThirdPersonMeshes) const override;

	virtual void OnUnequipped();

	virtual void OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy = nullptr);

	virtual UInteractableComponent* GetInteractableComponent() const override;

	virtual bool Interact(class AFirstPersonCharacter* Interactor) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void OnOwnerLost();

	// 플레이어가 소유하게 될 무기를 만드는 유일한 경로.
	// OverrideLevel 을 의도적으로 주지 않는다 -> PersistentLevel 에 들어간다.
	// 레벨에 배치된 무기 액터는 ULevel 이 WP 셀로 고정돼 있고 런타임에 바꿀 수 없어서,
	// 그대로 쥐여주면 플레이어가 그 셀에서 멀어질 때 손에 든 채로 사라진다.
	// 픽업/슈트/복원 세 경로가 모두 이걸 거쳐 같은 수명 규칙을 갖는다.
	static AWeaponBase* SpawnLoadoutWeapon(
		UWorld* World,
		TSubclassOf<AWeaponBase> WeaponClass,
		ACharacter* OwnerCharacter);

	EWeaponType GetWeaponType() const { return WeaponType; }
	EWeaponFireType GetFireType() const { return FireType; }
	EWeaponFireMode GetFireMode() const { return FireMode; }
	bool IsAttacking() const { return bIsAttacking; }
	bool IsEquipped() const { return bIsEquipped; }
	bool CanBePickedUpBy(const AFirstPersonCharacter* Interactor) const;
	float GetDamageAtDistance(float DistanceCm) const;
	float GetMovementSpeedMultiplier() const { return MovementSpeedMultiplier; }
	float GetFirstPersonProceduralRecoilMultiplier() const;
	float GetThirdPersonProceduralRecoilMultiplier() const;
	float GetThirdPersonProceduralSprintMultiplier() const;
	float GetThirdPersonProceduralWallOffsetMultiplier() const;

	USkeletalMeshComponent* GetFirstPersonWeaponMesh() const { return FirstPersonWeaponMesh; }
	USkeletalMeshComponent* GetThirdPersonWeaponMesh() const { return ThirdPersonWeaponMesh; }
	USkeletalMeshComponent* GetShadowWeaponMesh() const { return ShadowWeaponMesh; }

	UFUNCTION(BlueprintCallable, Category = IK)
	FName GetLeftHandIKSocketName() const
	{
		return LeftHandIKSocketName;
	}

	UFUNCTION(BlueprintCallable, Category = IK)
	FName GetLeftHandSprintIKSocketName() const
	{
		return LeftHandSprintIKSocketName;
	}

	UFUNCTION(BlueprintCallable, Category = IK)
	USkeletalMeshComponent* GetWeaponByView(bool bFirstPerson) const
	{
		return bFirstPerson ? FirstPersonWeaponMesh : ThirdPersonWeaponMesh;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Animation")
	const UProceduralAnimValues* GetFirstPersonProceduralValues() const
	{
		return FirstPersonProceduralValues;
	}
};
