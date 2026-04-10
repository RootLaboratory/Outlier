// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/WeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Shooter/ShooterCharacter.h"

AWeaponBase::AWeaponBase()
{
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
}

bool AWeaponBase::CanAttack() const
{
	return WeaponOwner != nullptr
		&& bIsEquipped
		&& !bIsAttacking
		&& !bAttackOnCooldown;
}

void AWeaponBase::StartAttack()
{
	if (!CanAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] CanAttack failed"), *GetName());
		return;
	}

	bIsAttacking = true;
	PerformAttack();
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

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(NewOwner);
	if (Shooter)
	{
		AttachToComponent(
			NewOwner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			Shooter->FirstPersonWeaponSocket
		);	
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] Equipped by %s"), *GetName(), *GetNameSafe(NewOwner));
}

void AWeaponBase::OnUnequipped()
{
	StopAttack();

	bIsEquipped = false;
	WeaponOwner = nullptr;

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetOwner(nullptr);

	UE_LOG(LogTemp, Log, TEXT("[%s] Unequipped"), *GetName());
}
