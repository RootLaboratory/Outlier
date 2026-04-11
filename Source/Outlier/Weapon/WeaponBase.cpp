// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/WeaponBase.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/Character.h"
#include "Shooter/ShooterCharacter.h"

AWeaponBase::AWeaponBase()
{
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
	ThirdPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ThirdPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ThirdPersonWeaponMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ThirdPersonWeaponMesh->SetGenerateOverlapEvents(false);

	SetEquippedCollisionEnabled(true);
}

void AWeaponBase::SetEquippedCollisionEnabled(bool bEnabled)
{
	const ECollisionEnabled::Type CollisionType = bEnabled
		? ECollisionEnabled::QueryOnly
		: ECollisionEnabled::NoCollision;

	ThirdPersonWeaponMesh->SetCollisionEnabled(CollisionType);
	ThirdPersonWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ThirdPersonWeaponMesh->SetCollisionResponseToChannel(ECC_Visibility, bEnabled ? ECR_Block : ECR_Ignore);
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
		UE_LOG(LogTemp, Warning, TEXT("[%s] CanAttack failed"), *GetName());
		return;
	}

	bIsAttacking = true;
}

void AWeaponBase::StopAttack()
{
	if (!bIsAttacking)
	{
		return;
	}

	bIsAttacking = false;

	UE_LOG(LogTemp, Log, TEXT("[%s] Stop Attack"), *GetName());
}

void AWeaponBase::PerformAttack()
{
	UE_LOG(LogTemp, Warning, TEXT("[%s] PerformAttack called on base weapon"), *GetName());

	bIsAttacking = false;
}

void AWeaponBase::OnEquipped(ACharacter* NewOwner)
{
	if (!NewOwner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] OnEquipped failed : NewOwner is null"), *GetName());
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
		if (USkeletalMeshComponent* FirstPersonParent = Shooter->GetFirstPersonMesh())
		{
			FirstPersonWeaponMesh->AttachToComponent(
				FirstPersonParent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				Shooter->FirstPersonWeaponSocket
			);
		}

		if (USkeletalMeshComponent* ThirdPersonParent = Shooter->GetMesh())
		{
			ThirdPersonWeaponMesh->AttachToComponent(
				ThirdPersonParent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				Shooter->ThirdPersonWeaponSocket
			);
		}
	}


	UE_LOG(LogTemp, Log, TEXT("[%s] Equipped by %s"), *GetName(), *GetNameSafe(NewOwner));
}

void AWeaponBase::OnUnequipped()
{
	StopAttack();

	bIsEquipped = false;
	WeaponOwner = nullptr;

	FirstPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	ThirdPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	// 바닥 아이템으로 둘 거면 여기서 다시 충돌 켜기
	SetPickupPresentation();
	SetOwner(nullptr);

	UE_LOG(LogTemp, Log, TEXT("[%s] Unequipped"), *GetName());
}

void AWeaponBase::Interact(class AFirstPersonCharacter* Interactor)
{
	if (!Interactor)
	{
		return;
	}

	Interactor->EquipWeapon(this);
}
