// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerCombatComponent.generated.h"

class ARangedWeaponBase;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerCombatComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerCombatComponent();

	// 로컬 입력/APartnerCharacter 공개 API가 사용하는 공격 진입점.
	// 클라이언트에서는 서버 RPC만 요청하고 실제 무기 상태 변경은 서버에서 수행한다.
	void TryStartAttack(bool bRequireFreshPress = true);
	void TryStopAttack();
	void StartAutoReload();
	void ResetAttackInput();
	void AllowAttackInput() { bAttackNeedsFreshPress = false; }
	bool IsAttackInputHeld() const { return bAttackInputHeld; }
	void SuspendForReboot();
	void CompleteReboot();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 빙의 해제/슈트 전환은 입력도 폐기한다. 재부팅만 SuspendForReboot로 의도를 보존한다.
	void ForceStopAttack();
	void CancelForReboot();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Weapon", meta = (ClampMin = "0.0"))
	float ReloadDurationSeconds = 1.0f;

	// 입력 세대와 요청 순번을 검증하고 최신 홀드만 보관한다. 실제 연사는 무기 타이머가 담당한다.
	UFUNCTION(Server, Reliable)
	void ServerSetAttackHeld(bool bHeld, uint32 Request, uint32 Epoch);

private:
	friend class FPartnerRebootInputTest;
	void SendAttackInput(bool bHeld);
	void TryResumeAttack();
	void ClearAttackRetry();
	void StopCurrentAttack(bool bPreserveCadence);
	void CancelReload();
	void LogAttackBlock(FName Reason);
	void FinishReload();

	bool bAttackInputHeld = false;
	bool bAttackNeedsFreshPress = false;
	uint8 LastDebugAttackBlockMask = 0;
	bool bAttackStartPending = false;
	bool bRebootSuspended = false;
	uint32 NextAttackInputRequest = 0;
	uint32 ProcessedAttackInputRequest = 0;
	uint32 ProcessedAttackInputEpoch = 0;
	FName LastAttackBlockReason;
	FTimerHandle AttackRetryTimerHandle;
	FTimerHandle ReloadTimerHandle;
	TWeakObjectPtr<ARangedWeaponBase> ReloadingWeapon;
};
