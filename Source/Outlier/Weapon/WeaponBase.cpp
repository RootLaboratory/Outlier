// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/WeaponBase.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/Character.h"
#include "OutlierNetUtils.h"
#include "Shooter/ShooterCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/DataTable.h"
#include "Interaction/InteractableComponent.h"
#include "Weapon/WeaponCoreRow.h"
#include "Weapon/WeaponRangeRow.h"
#include "Weapon/Spawn/WeaponSpawnPoint.h"

namespace
{
	const FName DefaultWeaponSocketName(TEXT("HandGrip_R"));

	void AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner)
	{
		if (!Weapon || !NewOwner)
		{
			return;
		}

		AFirstPersonCharacter* FirstPersonOwner = Cast<AFirstPersonCharacter>(NewOwner);
		if (!FirstPersonOwner)
		{
			return;
		}

		FName FirstPersonSocketName = DefaultWeaponSocketName;
		FName ThirdPersonSocketName = DefaultWeaponSocketName;
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(NewOwner))
		{
			const EWeaponType EquippedWeaponType = Weapon->GetWeaponType();
			FirstPersonSocketName = Shooter->GetFirstPersonWeaponSocketByType(EquippedWeaponType);
			ThirdPersonSocketName = Shooter->GetThirdPersonWeaponSocketByType(EquippedWeaponType);
		}

		if (USkeletalMeshComponent* FirstPersonParent = FirstPersonOwner->GetFirstPersonMesh())
		{
			Weapon->GetFirstPersonWeaponMesh()->AttachToComponent(
				FirstPersonParent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				FirstPersonSocketName
			);
		}

		if (USkeletalMeshComponent* ThirdPersonParent = FirstPersonOwner->GetMesh())
		{
			Weapon->GetThirdPersonWeaponMesh()->AttachToComponent(
				ThirdPersonParent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				ThirdPersonSocketName
			);
		}
	}
}

AWeaponBase::AWeaponBase()
{
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FirstPersonWeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonWeaponMesh"));
	FirstPersonWeaponMesh->SetupAttachment(SceneRoot);
	FirstPersonWeaponMesh->SetOnlyOwnerSee(true);
	FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	FirstPersonWeaponMesh->SetGenerateOverlapEvents(false);
	FirstPersonWeaponMesh->SetHiddenInGame(true);
	FirstPersonWeaponMesh->SetCastShadow(false);
	FirstPersonWeaponMesh->SetCastHiddenShadow(false);

	ThirdPersonWeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ThirdPersonWeaponMesh"));
	ThirdPersonWeaponMesh->SetupAttachment(SceneRoot);
	ThirdPersonWeaponMesh->SetOwnerNoSee(true);
	ThirdPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ThirdPersonWeaponMesh->SetGenerateOverlapEvents(false);
	ThirdPersonWeaponMesh->SetCastShadow(true);
	ThirdPersonWeaponMesh->SetCastHiddenShadow(false);

	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractionCollision->SetSphereRadius(40.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	InteractionCollision->SetHiddenInGame(true);

	SetEquippedCollisionEnabled(false);
}

void AWeaponBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (FirstPersonWeaponMesh)
	{
		FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FirstPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		FirstPersonWeaponMesh->SetGenerateOverlapEvents(false);
	}

	if (ThirdPersonWeaponMesh)
	{
		ThirdPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ThirdPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		ThirdPersonWeaponMesh->SetGenerateOverlapEvents(false);
	}

	if (InteractionCollision)
	{
		InteractionCollision->SetSphereRadius(40.0f);
		InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InteractionCollision->SetCollisionObjectType(ECC_WorldDynamic);
		InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
		InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		InteractionCollision->SetGenerateOverlapEvents(false);
		InteractionCollision->SetHiddenInGame(true);
	}

	SetEquippedCollisionEnabled(!bIsEquipped);
}

void AWeaponBase::EnsureWeaponDataInitialized()
{
	if (bWeaponDataInitialized)
	{
		return;
	}

	InitializeFromDataTables();
	bWeaponDataInitialized = true;
}

void AWeaponBase::InitializeFromDataTables()
{
	if (const FWeaponCoreRow* CoreRow = WeaponCoreRow.GetRow<FWeaponCoreRow>(TEXT("InitializeWeaponCore")))
	{
		if (!CoreRow->WeaponId.IsNone())
		{
			WeaponName = CoreRow->WeaponId;
		}

		WeaponType = CoreRow->WeaponType;
		FireType = CoreRow->FireType;
		FireMode = CoreRow->FireMode;
		Damage = CoreRow->Damage;
		MovementSpeedMultiplier = FMath::Max(CoreRow->MovementSpeedMultiplier, 0.0f);
		RangeProfileId = CoreRow->RangeProfileId;

		if (CoreRow->FireRateRpm > 0.0f)
		{
			AttackInterval = 60.0f / CoreRow->FireRateRpm;
		}
	}

	InitializeRangeFromDataTable();
}

void AWeaponBase::InitializeRangeFromDataTable()
{
	const FWeaponRangeRow* RangeRow = WeaponRangeRow.GetRow<FWeaponRangeRow>(TEXT("InitializeWeaponRange"));
	if (!RangeRow && WeaponRangeTable && !RangeProfileId.IsNone())
	{
		RangeRow = WeaponRangeTable->FindRow<FWeaponRangeRow>(RangeProfileId, TEXT("InitializeWeaponRange"));
	}

	if (!RangeRow)
	{
		return;
	}

	DamageFalloffStartRange = FMath::Max(RangeRow->FalloffStartRangeCm, 0.0f);
	DamageFalloffMaxRange = FMath::Max(RangeRow->MaxRangeCm, DamageFalloffStartRange);
	MinDamageMultiplier = FMath::Clamp(RangeRow->MinDamageMultiplier, 0.0f, 1.0f);

	if (DamageFalloffMaxRange > 0.0f)
	{
		EffectiveRange = DamageFalloffMaxRange;
	}
}

float AWeaponBase::GetDamageAtDistance(float DistanceCm) const
{
	if (DamageFalloffMaxRange <= DamageFalloffStartRange || DistanceCm <= DamageFalloffStartRange)
	{
		return Damage;
	}

	const float FalloffAlpha = FMath::Clamp(
		(DistanceCm - DamageFalloffStartRange) / (DamageFalloffMaxRange - DamageFalloffStartRange),
		0.0f,
		1.0f);

	return Damage * FMath::Lerp(1.0f, MinDamageMultiplier, FalloffAlpha);
}

void AWeaponBase::SetEquippedCollisionEnabled(bool bEnabled)
{
	const ECollisionEnabled::Type CollisionType = bEnabled
		? ECollisionEnabled::QueryOnly
		: ECollisionEnabled::NoCollision;

	InteractionCollision->SetCollisionEnabled(CollisionType);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, bEnabled ? ECR_Block : ECR_Ignore);
	InteractionCollision->SetGenerateOverlapEvents(false);

	ThirdPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ThirdPersonWeaponMesh->SetGenerateOverlapEvents(false);
}

void AWeaponBase::SetPickupPresentation()
{
	FirstPersonWeaponMesh->SetHiddenInGame(true);
	FirstPersonWeaponMesh->SetCastShadow(false);
	FirstPersonWeaponMesh->SetCastHiddenShadow(false);
	FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	ThirdPersonWeaponMesh->SetHiddenInGame(false);
	ThirdPersonWeaponMesh->SetCastShadow(true);
	ThirdPersonWeaponMesh->SetCastHiddenShadow(false);
	SetEquippedCollisionEnabled(true);
}

void AWeaponBase::SetEquippedPresentation()
{
	FirstPersonWeaponMesh->SetHiddenInGame(false);
	FirstPersonWeaponMesh->SetCastShadow(false);
	FirstPersonWeaponMesh->SetCastHiddenShadow(false);
	FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	ThirdPersonWeaponMesh->SetHiddenInGame(false);
	ThirdPersonWeaponMesh->SetCastShadow(true);
	ThirdPersonWeaponMesh->SetCastHiddenShadow(true);
	SetEquippedCollisionEnabled(false);
}

void AWeaponBase::ApplyReplicatedPresentation()
{
	if (bIsEquipped)
	{
		SetEquippedPresentation();

		AttachWeaponMeshesToOwner(this, WeaponOwner);

		return;
	}

	FirstPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	ThirdPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	FirstPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);

	ThirdPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);

	SetPickupPresentation();
}

void AWeaponBase::OnRep_EquippedState()
{
	ApplyReplicatedPresentation();
}

void AWeaponBase::SetOwningSpawnPoint(AWeaponSpawnPoint* SpawnPoint)
{
	OwningSpawnPoint = SpawnPoint;
}

void AWeaponBase::BeginPlay()
{
	Super::BeginPlay();

	EnsureWeaponDataInitialized();
}

bool AWeaponBase::CanAttack() const
{
	return WeaponOwner != nullptr
		&& bIsEquipped;
}

bool AWeaponBase::CanBePickedUpBy(const AFirstPersonCharacter* Interactor) const
{
	if (!Interactor || bIsEquipped || WeaponOwner != nullptr || IsPendingKillPending())
	{
		return false;
	}

	if (DropPickupBlockedInteractor.Get() == Interactor
		&& GetWorld()
		&& GetWorld()->GetTimeSeconds() < DropPickupBlockedUntilTime)
	{
		return false;
	}
	const FGameplayTagContainer PlayerCharacterInteractTag = Interactor->GetOwnedGameplayTagsForQuery();

	if(!PlayerCharacterInteractTag.IsEmpty() && !InteractableComponent->CanInteract(PlayerCharacterInteractTag))
	{
		return false;
	}
	return true;
}

void AWeaponBase::StartAttack()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] StartAttack blocked: client call"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!CanAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] StartAttack blocked Owner=%s Equipped=%d"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(WeaponOwner), bIsEquipped ? 1 : 0);
		return;
	}

	bIsAttacking = true;
	UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack Owner=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(WeaponOwner));
}

void AWeaponBase::StopAttack()
{
	if (!bIsAttacking)
	{
		return;
	}

	bIsAttacking = false;

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		Shooter->HandleWeaponAttackStoppedInternal();
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] StopAttack"), OutlierNet::GetNetPrefix(this), *GetName());
}

void AWeaponBase::PerformAttack()
{
	UE_LOG(LogTemp, Warning, TEXT("%s [%s] PerformAttack called on base weapon"), OutlierNet::GetNetPrefix(this), *GetName());

	bIsAttacking = false;
}

void AWeaponBase::OnEquipped(ACharacter* NewOwner)
{
	EnsureWeaponDataInitialized();

	if (!NewOwner)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] OnEquipped failed: owner is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	WeaponOwner = NewOwner;
	bIsEquipped = true;
	bIsAttacking = false;
	DropPickupBlockedInteractor = nullptr;
	DropPickupBlockedUntilTime = 0.0f;

	if (OwningSpawnPoint)
	{
		OwningSpawnPoint->NotifyWeaponPickedUp(this);
	}

	SetOwner(NewOwner);

	// 장착 중에는 캐릭터를 밀지 않도록 충돌 비활성화
	SetEquippedPresentation();
	AttachWeaponMeshesToOwner(this, NewOwner);

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(NewOwner))
	{
		const EWeaponType EquippedWeaponType = GetWeaponType();
		const FName FirstPersonSocketName = Shooter->GetFirstPersonWeaponSocketByType(EquippedWeaponType);
		const FName ThirdPersonSocketName = Shooter->GetThirdPersonWeaponSocketByType(EquippedWeaponType);

		if (USkeletalMeshComponent* FirstPersonParent = Shooter->GetFirstPersonMesh())
		{
			FirstPersonWeaponMesh->AttachToComponent(
				FirstPersonParent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				FirstPersonSocketName
			);
		}

		if (USkeletalMeshComponent* ThirdPersonParent = Shooter->GetMesh())
		{
			ThirdPersonWeaponMesh->AttachToComponent(
				ThirdPersonParent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				ThirdPersonSocketName
			);
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s [%s] EquipSockets WeaponType=%d FP=%s TP=%s FPParent=%s TPParent=%s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			static_cast<int32>(EquippedWeaponType),
			*FirstPersonSocketName.ToString(),
			*ThirdPersonSocketName.ToString(),
			*GetNameSafe(Shooter->GetFirstPersonMesh()),
			*GetNameSafe(Shooter->GetMesh())
		);
	}


	UE_LOG(LogTemp, Log, TEXT("%s [%s] OnEquipped Owner=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(NewOwner));
	ForceNetUpdate();
}

void AWeaponBase::OnUnequipped()
{
	StopAttack();

	bIsEquipped = false;
	bIsAttacking = false;

	if (FirstPersonWeaponMesh)
	{
		FirstPersonWeaponMesh->SetHiddenInGame(true);
		FirstPersonWeaponMesh->SetCastShadow(false);
		FirstPersonWeaponMesh->SetCastHiddenShadow(false);
	}

	if (ThirdPersonWeaponMesh)
	{
		ThirdPersonWeaponMesh->SetHiddenInGame(true);
		ThirdPersonWeaponMesh->SetCastShadow(false);
		ThirdPersonWeaponMesh->SetCastHiddenShadow(false);
	}

	SetEquippedCollisionEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("%s [%s] OnUnequipped"), OutlierNet::GetNetPrefix(this), *GetName());
}

void AWeaponBase::OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy)
{
	StopAttack();

	bIsEquipped = false;
	bIsAttacking = false;
	WeaponOwner = nullptr;
	DropPickupBlockedInteractor = DroppedBy;
	DropPickupBlockedUntilTime = GetWorld()
		? GetWorld()->GetTimeSeconds() + DropInstigatorPickupBlockDuration
		: 0.0f;

	FirstPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	ThirdPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	FirstPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);

	ThirdPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);

	SetActorTransform(DropTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetPickupPresentation();
	SetOwner(nullptr);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s [%s] OnDropped BlockedInteractor=%s Until=%.2f"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		*GetNameSafe(DroppedBy),
		DropPickupBlockedUntilTime);
	ForceNetUpdate();
}

void AWeaponBase::Interact(class AFirstPersonCharacter* Interactor)
{
	if (!Interactor)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] Interact blocked: interactor is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!CanBePickedUpBy(Interactor))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s [%s] Interact blocked Owner=%s Equipped=%d Interactor=%s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(WeaponOwner),
			bIsEquipped ? 1 : 0,
			*GetNameSafe(Interactor));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Interact Interactor=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(Interactor));

	Interactor->EquipWeapon(this);
}

UInteractableComponent* AWeaponBase::GetInteractableComponent() const
{
	return InteractableComponent;
}

void AWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeaponBase, WeaponOwner);
	DOREPLIFETIME(AWeaponBase, bIsEquipped);
}

void AWeaponBase::OnOwnerLost()
{
	StopAttack();

	bIsEquipped = false;
	bIsAttacking = false;
	WeaponOwner = nullptr;
	SetOwner(nullptr);

	if (!IsActorBeingDestroyed() && IsValid(OwningSpawnPoint))
	{
		OwningSpawnPoint->NotifyWeaponRemoved(this);
	}

	OwningSpawnPoint= nullptr;
	Destroy();
}
