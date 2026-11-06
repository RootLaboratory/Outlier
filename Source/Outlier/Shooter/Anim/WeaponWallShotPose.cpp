#include "Shooter/Anim/WeaponWallShotPose.h"

#include "Shooter/Anim/WeaponValues.h"
#include "Shooter/Anim/WeaponWallAvoidance.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "UObject/UnrealType.h"

namespace
{
// 상태머신에 이미 지정된 무기 기준 자세를 재사용한다. 경로 문자열로 별도 애셋을 선택하지 않는다.
const UAnimSequence* FindWeaponBaseSequence(USkeletalMeshComponent& Arms, EWeaponType Type)
{
	UClass* AnimClass = Arms.GetAnimClass();
	const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(AnimClass);
	if (!Interface) return nullptr;
	const FName StateName = Type == EWeaponType::Pistol ? TEXT("Pistol") : TEXT("Rifle");
	const TArray<FStructProperty*>& Properties = Interface->GetAnimNodeProperties();
	for (const FBakedAnimationStateMachine& Machine : Interface->GetBakedStateMachines())
	{
		for (const FBakedAnimationState& State : Machine.States)
		{
			if (State.StateName != StateName || State.PlayerNodeIndices.Num() != 1) continue;
			// Baked state 인덱스는 PoseLink와 역순이다. 엔진의 GetNodeFromIndexUntyped와 같은 변환이다.
			const int32 Index = Properties.Num() - 1 - State.PlayerNodeIndices[0];
			if (!Properties.IsValidIndex(Index)) return nullptr;
			const FStructProperty* Property = Properties[Index];
			if (!Property->Struct->IsChildOf(FAnimNode_SequencePlayerBase::StaticStruct())) return nullptr;
			const FAnimNode_SequencePlayerBase* Player = Property->ContainerPtrToValuePtr<FAnimNode_SequencePlayerBase>(AnimClass->GetDefaultObject());
			return Cast<UAnimSequence>(Player->GetSequence());
		}
	}
	return nullptr;
}

bool SampleComponentBone(const UAnimSequence& Sequence, FName BoneName, FTransform& Out)
{
	const USkeleton* Skeleton = Sequence.GetSkeleton();
	if (!Skeleton) return false;
	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	int32 Bone = Reference.FindBoneIndex(BoneName);
	if (Bone == INDEX_NONE) return false;
	Out = FTransform::Identity;
	while (Bone != INDEX_NONE)
	{
		FTransform Local;
		Sequence.GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(0.0), false);
		Out = Out * Local;
		Bone = Reference.GetParentIndex(Bone);
	}
	return !Out.ContainsNaN();
}

FTransform ReferenceBone(const USkeletalMesh& Mesh, int32 Bone)
{
	FTransform Result = FTransform::Identity;
	const FReferenceSkeleton& Reference = Mesh.GetRefSkeleton();
	while (Bone != INDEX_NONE)
	{
		Result = Result * Reference.GetRefBonePose()[Bone];
		Bone = Reference.GetParentIndex(Bone);
	}
	return Result;
}

// Transform Bone의 ComponentSpace/Additive와 같은 순서다. 위치는 회전 피벗을 중심으로 돌리지 않는다.
void AddComponentOffset(FTransform& Bone, const FVector& Location, const FRotator& Rotation, float Alpha = 1.0f)
{
	FTransform Target = Bone;
	Target.SetLocation(Bone.GetLocation() + Location);
	Target.SetRotation((Rotation.Quaternion() * Bone.GetRotation()).GetNormalized());
	Bone.Blend(Bone, Target, FMath::Clamp(Alpha, 0.0f, 1.0f));
	Bone.NormalizeRotation();
}
}

bool FWeaponWallShotPose::Initialize(const AShooterCharacter& Shooter, const AWeaponBase& Weapon)
{
	Reset();
	const ARangedWeaponBase* Ranged = Cast<ARangedWeaponBase>(&Weapon);
	USkeletalMeshComponent* Arms = Shooter.GetFirstPersonMesh();
	const USkeletalMeshComponent* Gun = Weapon.GetFirstPersonWeaponMesh();
	if (!Ranged || !Arms || !Gun || !Arms->GetSkeletalMeshAsset() || !Gun->GetSkeletalMeshAsset())
	{
		Diagnostic = TEXT("Missing ranged weapon or skeletal asset");
		return false;
	}
	const UAnimSequence* BaseSequence = FindWeaponBaseSequence(*Arms, Weapon.GetWeaponType());
	const USkeletalMeshSocket* Attachment = Arms->GetSkeletalMeshAsset()->FindSocket(
		Shooter.GetFirstPersonWeaponSocketByType(Weapon.GetWeaponType()));
	const USkeletalMeshSocket* Muzzle = Gun->GetSkeletalMeshAsset()->FindSocket(Ranged->GetMuzzleSocketName());
	if (!BaseSequence || BaseSequence->IsValidAdditive() || !Attachment || !Muzzle)
	{
		Diagnostic = TEXT("Missing non-additive weapon base pose, attachment socket or muzzle socket");
		return false;
	}
	if (!SampleComponentBone(*BaseSequence, TEXT("ik_hand_gun"), BaseGun)
		|| Attachment->BoneName != TEXT("ik_hand_gun"))
	{
		Diagnostic = FString::Printf(TEXT("Unsupported weapon attachment (%s) or missing gun IK bone"), *Attachment->BoneName.ToString());
		return false;
	}
	const int32 MuzzleBone = Gun->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(Muzzle->BoneName);
	if (MuzzleBone == INDEX_NONE)
	{
		Diagnostic = TEXT("Muzzle parent bone is missing");
		return false;
	}
	MuzzleFromHand = Muzzle->GetSocketLocalTransform()
		* ReferenceBone(*Gun->GetSkeletalMeshAsset(), MuzzleBone)
		* Gun->GetRelativeTransform() * Attachment->GetSocketLocalTransform();
	const AShooterCharacter* Defaults = Shooter.GetClass()->GetDefaultObject<AShooterCharacter>();
	if (!Defaults->GetFirstPersonMesh() || !Defaults->GetFirstPersonViewModelRoot())
	{
		Diagnostic = TEXT("Missing authored first-person framing");
		return false;
	}
	MeshToView = Defaults->GetFirstPersonMesh()->GetRelativeTransform()
		* Defaults->GetFirstPersonViewModelRoot()->GetRelativeTransform();
	bReady = !MuzzleFromHand.ContainsNaN() && !MeshToView.ContainsNaN();
	return bReady;
}

bool FWeaponWallShotPose::Evaluate(const FWeaponWallAvoidanceState& State, const FWeaponValues& Values,
	float AimAlpha, const FTransform& View, FTransform& OutMuzzle) const
{
	if (!bReady || !FMath::IsFinite(AimAlpha) || View.ContainsNaN() || !View.GetRotation().IsNormalized()
		|| State.ViewModelWallOffsetLoc.ContainsNaN() || State.ViewModelWallOffsetRot.ContainsNaN()
		|| Values.HipPoseLoc.ContainsNaN() || Values.HipPoseRot.ContainsNaN()
		|| Values.AimPoseLoc.ContainsNaN() || Values.AimPoseRot.ContainsNaN()) return false;

	// 실제 소켓은 ik_hand_gun에 부착된다. 회피 포즈가 바꾸는 팔 모양과 달리 이 본은
	// 보존된 VB hand_gun 타깃을 복사한다. 실제 에셋 대조 테스트로 이 경로를 검증한다.
	FTransform Gun = BaseGun;
	AddComponentOffset(Gun, Values.HipPoseLoc, Values.HipPoseRot);
	AddComponentOffset(Gun, Values.AimPoseLoc, Values.AimPoseRot, AimAlpha);
	AddComponentOffset(Gun, State.ViewModelWallOffsetLoc, State.ViewModelWallOffsetRot);
	OutMuzzle = MuzzleFromHand * Gun * MeshToView * View;
	OutMuzzle.NormalizeRotation();
	return !OutMuzzle.ContainsNaN();
}
