// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCombatComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "TimerManager.h"

UPartnerCombatComponent::UPartnerCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPartnerCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (PartnerCharacter && PartnerCharacter->HasAuthority() && bEquipDefaultWeaponOnBeginPlay)
	{
		EquipDefaultWeapon_Server();
	}
}

void UPartnerCombatComponent::TryStartAttack()
{
	// 서버 RPC가 다시 이 함수로 들어오므로 입력 가능 여부는 서버에서도 동일하게 검증된다.
	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority())
	{
		// 이 컴포넌트의 Owner인 Partner를 소유한 클라이언트만 서버 RPC를 전송할 수 있다.
		ServerStartAttack();
		return;
	}

	if (AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->StartAttack();
	}
}

void UPartnerCombatComponent::TryStopAttack()
{
	if (!PartnerCharacter)
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority())
	{
		// 공격 중지는 상태 변화 중에도 필요하므로 CanAcceptInput 검사를 하지 않는다.
		ServerStopAttack();
		return;
	}

	if (AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->StopAttack();
	}
}

void UPartnerCombatComponent::StartAutoReload()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	ARangedWeaponBase* Weapon = Cast<ARangedWeaponBase>(PartnerCharacter->GetCurrentWeapon());
	if (!Weapon || !Weapon->CanReload())
	{
		return;
	}

	Weapon->StopAttack();
	Weapon->BeginReload();
	if (!Weapon->IsReloading())
	{
		return;
	}

	ReloadingWeapon = Weapon;
	if (ReloadDurationSeconds <= 0.0f || !GetWorld())
	{
		FinishReload();
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ReloadTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimerHandle,
		this,
		&UPartnerCombatComponent::FinishReload,
		ReloadDurationSeconds,
		false);
}

void UPartnerCombatComponent::ForceStopAttack()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	if (AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->StopAttack();
	}
}

void UPartnerCombatComponent::ServerStartAttack_Implementation()
{
	TryStartAttack();
}

void UPartnerCombatComponent::ServerStopAttack_Implementation()
{
	TryStopAttack();
}

void UPartnerCombatComponent::FinishReload()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		ReloadingWeapon.Reset();
		return;
	}

	ARangedWeaponBase* Weapon = ReloadingWeapon.Get();
	if (Weapon && Weapon == PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->FinishReload();
	}
	else if (Weapon)
	{
		Weapon->CancelReload();
	}

	ReloadingWeapon.Reset();
}

void UPartnerCombatComponent::EquipDefaultWeapon_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !DefaultWeaponClass || PartnerCharacter->GetCurrentWeapon())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = PartnerCharacter;
	SpawnParams.Instigator = PartnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARangedWeaponBase* DefaultWeapon = GetWorld()->SpawnActor<ARangedWeaponBase>(
		DefaultWeaponClass,
		PartnerCharacter->GetActorTransform(),
		SpawnParams
	);

	if (DefaultWeapon)
	{
		PartnerCharacter->EquipWeapon(DefaultWeapon);
	}
}
