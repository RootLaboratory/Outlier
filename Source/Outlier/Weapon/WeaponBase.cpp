// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/WeaponBase.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/Character.h"
#include "OutlierNetUtils.h"
#include "Shooter/ShooterCharacter.h"
#include <Net/UnrealNetwork.h>

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

	ThirdPersonWeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ThirdPersonWeaponMesh"));
	ThirdPersonWeaponMesh->SetupAttachment(SceneRoot);
	ThirdPersonWeaponMesh->SetOwnerNoSee(true);
	ThirdPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ThirdPersonWeaponMesh->SetGenerateOverlapEvents(false);

	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);
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
	FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	ThirdPersonWeaponMesh->SetHiddenInGame(false);
	SetEquippedCollisionEnabled(true);
}

void AWeaponBase::SetEquippedPresentation()
{
	FirstPersonWeaponMesh->SetHiddenInGame(false);
	FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	ThirdPersonWeaponMesh->SetHiddenInGame(false);
	SetEquippedCollisionEnabled(false);
}

bool AWeaponBase::CanAttack() const
{
	return WeaponOwner != nullptr
		&& bIsEquipped;
}

void AWeaponBase::StartAttack()
{
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

	UE_LOG(LogTemp, Log, TEXT("%s [%s] StopAttack"), OutlierNet::GetNetPrefix(this), *GetName());
}

void AWeaponBase::PerformAttack()
{
	UE_LOG(LogTemp, Warning, TEXT("%s [%s] PerformAttack called on base weapon"), OutlierNet::GetNetPrefix(this), *GetName());

	bIsAttacking = false;
}

void AWeaponBase::OnEquipped(ACharacter* NewOwner)
{
	if (!NewOwner)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] OnEquipped failed: owner is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	WeaponOwner = NewOwner;
	bIsEquipped = true;
	bIsAttacking = false;

	SetOwner(NewOwner);

	// 장착 중에는 캐릭터를 밀지 않도록 충돌 비활성화
	SetEquippedPresentation();

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
}

void AWeaponBase::OnUnequipped()
{
	StopAttack();

	bIsEquipped = false;
	WeaponOwner = nullptr;

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

	// 바닥 아이템으로 둘 거면 여기서 다시 충돌 켜기
	SetPickupPresentation();
	SetOwner(nullptr);

	UE_LOG(LogTemp, Log, TEXT("%s [%s] OnUnequipped"), OutlierNet::GetNetPrefix(this), *GetName());
}

void AWeaponBase::OnDropped(const FTransform& DropTransform)
{
	OnUnequipped();
	SetActorTransform(DropTransform, false, nullptr, ETeleportType::TeleportPhysics);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s [%s] OnDropped Location=%s Rotation=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		*DropTransform.GetLocation().ToString(),
		*DropTransform.Rotator().ToString()
	);
}

void AWeaponBase::Interact(class AFirstPersonCharacter* Interactor)
{
	if (!Interactor)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] Interact blocked: interactor is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Interact Interactor=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(Interactor));
	Interactor->EquipWeapon(this);
}

void AWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeaponBase, WeaponOwner);
	DOREPLIFETIME(AWeaponBase, bIsEquipped);
}
