// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerCombatComponent.generated.h"

class ARangedWeaponBase;
class AWeaponBase;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerCombatComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerCombatComponent();

	virtual void BeginPlay() override;

	// 로컬 입력/APartnerCharacter 공개 API가 사용하는 공격 진입점.
	// 클라이언트에서는 서버 RPC만 요청하고 실제 무기 상태 변경은 서버에서 수행한다.
	void TryStartAttack();
	void TryStopAttack();
	void StartAutoReload();
	void ToggleTestWeaponEquipped();

	// 빙의 해제, 리부트처럼 입력과 무관하게 공격을 끝내야 하는 서버 전용 정리 함수.
	void ForceStopAttack();
	void CancelForReboot();

protected:
	// Partner BP의 CombatComponent 기본값에서 ARangedWeaponBase 파생 BP를 지정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Weapon")
	TSubclassOf<ARangedWeaponBase> DefaultWeaponClass;

	// true면 BeginPlay에서 서버가 기본 무기를 한 번 스폰해 CurrentWeapon으로 장착한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Weapon")
	uint8 bEquipDefaultWeaponOnBeginPlay : 1 = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Weapon", meta = (ClampMin = "0.0"))
	float ReloadDurationSeconds = 1.0f;

	// 소유 클라이언트의 공격 시작/종료 상태만 서버로 전달한다.
	// 연사 처리는 서버의 Weapon 타이머가 담당하므로 발사마다 RPC를 보내지 않는다.
	// 구현부는 TryStartAttack/TryStopAttack을 다시 호출해 서버에서도 동일한 검증을 거친다.
	UFUNCTION(Server, Reliable)
	void ServerStartAttack();

	UFUNCTION(Server, Reliable)
	void ServerStopAttack();

	UFUNCTION(Server, Reliable)
	void ServerToggleTestWeaponEquipped();

private:
	// 무기 스폰과 장착은 서버에서만 수행한다.
	void EquipDefaultWeapon_Server();
	void FinishReload();

	FTimerHandle ReloadTimerHandle;
	TWeakObjectPtr<ARangedWeaponBase> ReloadingWeapon;
	TWeakObjectPtr<AWeaponBase> TestUnequippedWeapon;
};
