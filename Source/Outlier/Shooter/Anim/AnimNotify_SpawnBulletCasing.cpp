// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotify_SpawnBulletCasing.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"

void UAnimNotify_SpawnBulletCasing::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference
)
{
	if (!MeshComp || !BulletCasingSystem)
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!Shooter)
	{
		return;
	}

	AWeaponBase* Weapon = Cast<AWeaponBase>(Shooter->GetCurrentWeapon());
	if (!Weapon)
	{
		return;
	}

	USkeletalMeshComponent* WeaponMesh = Weapon->GetWeaponByView(bFirstPerson);
	if (!WeaponMesh || !WeaponMesh->DoesSocketExist(SocketName))
	{
		return;
	}

	const FTransform SocketTransform = WeaponMesh->GetSocketTransform(SocketName, RTS_World);

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		MeshComp,
		BulletCasingSystem,
		SocketTransform.GetLocation(),
		SocketTransform.Rotator()
	);
}
