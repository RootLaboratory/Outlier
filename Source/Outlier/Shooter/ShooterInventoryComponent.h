// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "Weapon/WeaponBase.h"
#include "Save/OutlierLoadoutSnapshot.h"
#include "ShooterInventoryComponent.generated.h"

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterInventoryComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketDefault = FName("HandGrip_R");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketDefault = FName("HandGrip_R");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketRifle = FName("HandGrip_R_Rifle");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketRifle = FName("HandGrip_R_Rifle");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketPistol = FName("HandGrip_R_Pistol_FP");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketPistol = FName("HandGrip_R_Pistol_TP");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketMelee = FName("HandGrip_R");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketMelee = FName("HandGrip_R");

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<TObjectPtr<AWeaponBase>> WeaponSlots;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	EWeaponSlot CurrentSlot = EWeaponSlot::Primary;

	TWeakObjectPtr<AWeaponBase> PendingSwitchWeapon;
	EWeaponSlot PendingSwitchSlot = EWeaponSlot::Primary;
	FTimerHandle PendingSwitchTimerHandle;
	bool bHasPendingWeaponSwitch = false;
	int32 PendingSwitchId = 0;

public:
	UShooterInventoryComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FName GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const;
	FName GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const;
	AWeaponBase* GetWeaponInSlot(EWeaponSlot Slot) const;

	void TrySwitchWeapon1();
	void TrySwitchWeapon2();
	void TrySwitchWeapon3();
	void SelectWeaponByIndex(int32 SlotIndex);

	void HandleEquipWeapon(AWeaponBase* Weapon);
	bool EquipSuitRifle(AWeaponBase* RifleWeapon);
	void SelectWeaponSlot(EWeaponSlot Slot);
	void FinishPendingWeaponSwitch(int32 SwitchId);
	void ExpirePendingWeaponSwitch();
	void CancelPendingWeaponSwitch();

	void CleanupOwnedWeapons();

	// 현재 살아 있는 Weapon Actor에서 슬롯별 클래스와 탄약을 읽는다.
	// bCaptureAmmo=false 는 기존 프리셋 계약(새 무기 기본 탄약)을 유지한다.
	void BuildLoadoutSnapshot(FOutlierLoadoutSnapshot& OutSnapshot, bool bCaptureAmmo) const;

	// 리로드로 새로 스폰된 Pawn 에 PlayerState 기록을 되살린다.
	// 상호작용이 아니므로 CanBePickedUpBy 게이트를 타지 않고, 몽타주도 쓰지 않는다.
	// Snapshot 을 값으로 받는다 — 내부에서 CaptureLoadoutToPlayerState() 가 PlayerState 의
	// 스냅샷을 통째로 덮어쓰므로, 참조로 받으면 순회 도중 대상이 재할당되어 무효화된다.
	void RestoreLoadout(FOutlierLoadoutSnapshot Snapshot, bool bRestoreAmmo = false);
public:

private:
	// 슬롯 구성이 바뀐 직후에 호출한다. WeaponSlots 를 그대로 베껴 PlayerState 에 남긴다.
	// 파괴 경로(CleanupOwnedWeapons/EndPlay)에서는 절대 부르지 않는다 — 비워진 인벤토리로
	// 기록을 덮어쓰게 되고, 그 시점에는 이미 이 기록이 유일한 복원 근거다.
	void CaptureLoadoutToPlayerState() const;

	// HandleEquipWeapon / EquipSuitRifle / RestoreLoadout 의 공통 꼬리.
	// bPlayEquipMontage=false 면 몽타주 대신 ShowEquippedPresentation() 을 직접 부른다.
	// OnEquipped 가 1P/3P/Shadow 메시를 전부 숨기고 공개는 equip 몽타주 Notify 담당이라,
	// 둘 다 생략하면 장착은 됐는데 무기가 보이지 않는 상태가 된다.
	void ApplyWeaponToSlot(AWeaponBase* Weapon, EWeaponSlot Slot, bool bPlayEquipMontage);
	void RestoreWeaponIntoSlot(
		const FOutlierWeaponSnapshot& WeaponSnapshot,
		EWeaponSlot Slot,
		bool bRestoreAmmo);

	static EWeaponSlot GetSlotForWeaponType(EWeaponType WeaponType);
	bool IsValidWeaponSlot(EWeaponSlot Slot) const;
};
