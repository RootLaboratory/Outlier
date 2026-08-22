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
	if (!PartnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][Combat] Attack blocked: PartnerCharacter is null."));
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PartnerWeaponVFX][Combat] Attack request. Authority=%d CanAcceptInput=%d CurrentWeapon=%s"),
		PartnerCharacter->HasAuthority() ? 1 : 0,
		PartnerCharacter->CanAcceptInput() ? 1 : 0,
		*GetNameSafe(PartnerCharacter->GetCurrentWeapon()));

	if (!PartnerCharacter->CanAcceptInput())
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][Combat] Attack blocked: CanAcceptInput returned false."));
		return;
	}

	if (!PartnerCharacter->HasAuthority())
	{
		// 이 컴포넌트의 Owner인 Partner를 소유한 클라이언트만 서버 RPC를 전송할 수 있다.
		UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponVFX][Combat] Sending ServerStartAttack RPC."));
		ServerStartAttack();
		return;
	}

	if (AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponVFX][Combat] Calling StartAttack on %s."), *GetNameSafe(Weapon));
		Weapon->StartAttack();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][Combat] Attack blocked: CurrentWeapon is null."));
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
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !PartnerCharacter->CanAcceptInput())
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

void UPartnerCombatComponent::CancelForReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	ForceStopAttack();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	if (ARangedWeaponBase* Weapon = ReloadingWeapon.Get())
	{
		Weapon->CancelReload();
	}
	else if (ARangedWeaponBase* CurrentRangedWeapon = Cast<ARangedWeaponBase>(PartnerCharacter->GetCurrentWeapon()))
	{
		CurrentRangedWeapon->CancelReload();
	}
	ReloadingWeapon.Reset();
}

void UPartnerCombatComponent::ToggleTestWeaponEquipped()
{
	if (!PartnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponToggle][Combat] Failed: PartnerCharacter is null."));
		return;
	}

	if (!PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PartnerWeaponToggle][Combat] Character=%s Authority=%d CurrentWeapon=%s SavedWeapon=%s DefaultClass=%s"),
		*GetNameSafe(PartnerCharacter),
		PartnerCharacter->HasAuthority() ? 1 : 0,
		*GetNameSafe(PartnerCharacter->GetCurrentWeapon()),
		*GetNameSafe(TestUnequippedWeapon.Get()),
		*GetNameSafe(DefaultWeaponClass));

	if (!PartnerCharacter->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponToggle][Client] Sending ServerToggleTestWeaponEquipped RPC."));
		ServerToggleTestWeaponEquipped();
		return;
	}

	if (AWeaponBase* CurrentWeapon = PartnerCharacter->GetCurrentWeapon())
	{
		TestUnequippedWeapon = CurrentWeapon;
		PartnerCharacter->EquipWeapon(nullptr);
		CurrentWeapon->ForceNetUpdate();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponToggle][Server] Unequipped Weapon=%s CurrentWeapon=%s"),
			*GetNameSafe(CurrentWeapon),
			*GetNameSafe(PartnerCharacter->GetCurrentWeapon()));
		return;
	}

	if (AWeaponBase* PreviousWeapon = TestUnequippedWeapon.Get())
	{
		PartnerCharacter->EquipWeapon(PreviousWeapon);
		PreviousWeapon->ShowEquippedPresentation();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponToggle][Server] Re-equipped Weapon=%s CurrentWeapon=%s"),
			*GetNameSafe(PreviousWeapon),
			*GetNameSafe(PartnerCharacter->GetCurrentWeapon()));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponToggle][Server] No saved weapon. Trying DefaultWeaponClass spawn."));
	EquipDefaultWeapon_Server();
}

void UPartnerCombatComponent::ServerStartAttack_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponVFX][ServerRPC] ServerStartAttack received."));
	TryStartAttack();
}

void UPartnerCombatComponent::ServerStopAttack_Implementation()
{
	TryStopAttack();
}

void UPartnerCombatComponent::ServerToggleTestWeaponEquipped_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponToggle][ServerRPC] RPC received."));
	ToggleTestWeaponEquipped();
}

void UPartnerCombatComponent::FinishReload()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		ReloadingWeapon.Reset();
		return;
	}

	ARangedWeaponBase* Weapon = ReloadingWeapon.Get();
	if (!PartnerCharacter->CanAcceptInput())
	{
		if (Weapon)
		{
			Weapon->CancelReload();
		}
		ReloadingWeapon.Reset();
		return;
	}

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
	if (!PartnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponToggle][Spawn] Failed: PartnerCharacter is null."));
		return;
	}

	if (!PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponToggle][Spawn] Failed: called without authority."));
		return;
	}

	if (!DefaultWeaponClass)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[PartnerWeaponToggle][Spawn] Failed: DefaultWeaponClass is not set on CombatComponent of %s."),
			*GetNameSafe(PartnerCharacter));
		return;
	}

	if (PartnerCharacter->GetCurrentWeapon())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponToggle][Spawn] Skipped: CurrentWeapon already exists (%s)."),
			*GetNameSafe(PartnerCharacter->GetCurrentWeapon()));
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
		DefaultWeapon->ShowEquippedPresentation();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponToggle][Spawn] Spawned and equipped Weapon=%s Class=%s CurrentWeapon=%s"),
			*GetNameSafe(DefaultWeapon),
			*GetNameSafe(DefaultWeaponClass),
			*GetNameSafe(PartnerCharacter->GetCurrentWeapon()));
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[PartnerWeaponToggle][Spawn] Failed: SpawnActor returned null for Class=%s."),
			*GetNameSafe(DefaultWeaponClass));
	}
}
