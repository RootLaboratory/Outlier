// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/SkeletalMeshComponent.h"

void AShooterCharacter::RecordWeaponActionMontage(USkeletalMeshComponent* MeshComp, UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	const FAnimMontageInstance* Instance = AnimInstance && Montage
		? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!Instance || !CurrentWeapon)
	{
		return;
	}

	// 재생 시점의 무기와 세대를 묶는다. 종료된 인스턴스는 다음 재생에서 정리한다.
	WeaponActionNotifyContexts.RemoveAll([this, MeshComp, Montage, Instance](const FWeaponActionNotifyContext& Context)
	{
		USkeletalMeshComponent* ContextMesh = Context.Mesh.Get();
		UAnimInstance* ContextAnimInstance = ContextMesh ? ContextMesh->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* ContextInstance = ContextAnimInstance
			? ContextAnimInstance->GetMontageInstanceForID(Context.InstanceId) : nullptr;
		return Context.Weapon.Get() != CurrentWeapon || Context.EquipGeneration != EquipActionGeneration
			|| !ContextInstance || (ContextMesh == MeshComp && ContextInstance->Montage == Montage && ContextInstance != Instance);
	});
	const int32 InstanceId = Instance->GetInstanceID();
	if (WeaponActionNotifyContexts.ContainsByPredicate([MeshComp, InstanceId](const FWeaponActionNotifyContext& Context)
	{
		return Context.Mesh.Get() == MeshComp && Context.InstanceId == InstanceId;
	}))
	{
		return;
	}

	FWeaponActionNotifyContext& Context = WeaponActionNotifyContexts.AddDefaulted_GetRef();
	Context.Mesh = MeshComp;
	Context.Weapon = CurrentWeapon;
	Context.InstanceId = InstanceId;
	Context.EquipGeneration = EquipActionGeneration;
}

bool AShooterCharacter::IsCurrentWeaponActionNotify(
	USkeletalMeshComponent* MeshComp, const FAnimNotifyEventReference& EventReference) const
{
	const auto* MontageContext = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	if (!MeshComp || !CurrentWeapon || !MontageContext || IsDead() || IsWeaponEquipInputBlocked()
		|| IsSuitTransitionBlocked())
	{
		return false;
	}

	// 공유 Notify의 현재 무기 조회만으로는 이전 몽타주의 지연 호출을 구분할 수 없다.
	// 원래 메시/인스턴스/무기/장착 세대가 모두 일치할 때만 새 무기에 적용한다.
	return WeaponActionNotifyContexts.ContainsByPredicate([this, MeshComp, MontageContext](const FWeaponActionNotifyContext& Context)
	{
		return Context.Mesh.Get() == MeshComp && Context.Weapon.Get() == CurrentWeapon
			&& Context.EquipGeneration == EquipActionGeneration
			&& Context.InstanceId == MontageContext->MontageInstanceID;
	});
}
