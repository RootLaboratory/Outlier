// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_AttachWeapon.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"
#include "OutlierNetUtils.h"

void UAnimNotify_AttachWeapon::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !Animation)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnimNotify_Attach] skipped Mesh=%s Animation=%s"),
			*GetNameSafe(MeshComp),
			*GetNameSafe(Animation));
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!Shooter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnimNotify_Attach] skipped: owner is not Shooter Mesh=%s Owner=%s Anim=%s"),
			*GetNameSafe(MeshComp),
			*GetNameSafe(MeshComp->GetOwner()),
			*GetNameSafe(Animation));
		return;
	}

	// 전환 commit은 직접 부착한다. 이전 Equip의 늦은 Notify로 새 무기를 다시 완료하지 않는다.
	if (Shooter->IsSuitTransitionBlocked())
	{
		return;
	}
	AWeaponBase* Weapon = Cast<AWeaponBase>(Shooter->GetCurrentWeapon());
	if (!Weapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [AnimNotify_Attach] skipped: no current weapon Mesh=%s Anim=%s"),
			OutlierNet::GetNetPrefix(Shooter),
			*GetNameSafe(MeshComp),
			*GetNameSafe(Animation));
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("%s [AnimNotify_Attach] Mesh=%s Owner=%s Anim=%s Weapon=%s FPHiddenBefore=%d TPHiddenBefore=%d"),
		OutlierNet::GetNetPrefix(Shooter),
		*GetNameSafe(MeshComp),
		*GetNameSafe(MeshComp ? MeshComp->GetOwner() : nullptr),
		*GetNameSafe(Animation),
		*GetNameSafe(Weapon),
		Weapon->GetFirstPersonWeaponMesh() ? (Weapon->GetFirstPersonWeaponMesh()->bHiddenInGame ? 1 : 0) : -1,
		Weapon->GetThirdPersonWeaponMesh() ? (Weapon->GetThirdPersonWeaponMesh()->bHiddenInGame ? 1 : 0) : -1
	);

	Weapon->AttachWeaponMeshesToOwnerMeshes();
	Weapon->ShowEquippedPresentation();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("%s [AnimNotify_Attach] applied Weapon=%s FPHiddenAfter=%d TPHiddenAfter=%d"),
		OutlierNet::GetNetPrefix(Shooter),
		*GetNameSafe(Weapon),
		Weapon->GetFirstPersonWeaponMesh() ? (Weapon->GetFirstPersonWeaponMesh()->bHiddenInGame ? 1 : 0) : -1,
		Weapon->GetThirdPersonWeaponMesh() ? (Weapon->GetThirdPersonWeaponMesh()->bHiddenInGame ? 1 : 0) : -1
	);
}
