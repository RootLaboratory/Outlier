// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/WeaponBase.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GameFramework/Character.h"
#include "OutlierNetUtils.h"
#include "Shooter/ShooterCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/DataTable.h"
#include "Interaction/InteractableComponent.h"
#include "Weapon/WeaponCoreRow.h"
#include "Weapon/WeaponRangeRow.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

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

	ShadowWeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ShadowWeaponMesh"));
	ShadowWeaponMesh->SetupAttachment(SceneRoot);
	ShadowWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShadowWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ShadowWeaponMesh->SetGenerateOverlapEvents(false);
	ShadowWeaponMesh->SetHiddenInGame(true);
	ShadowWeaponMesh->SetVisibility(true, true);
	ShadowWeaponMesh->SetOwnerNoSee(false);
	ShadowWeaponMesh->SetOnlyOwnerSee(false);
	ShadowWeaponMesh->SetRenderInMainPass(true);
	ShadowWeaponMesh->SetRenderInDepthPass(false);
	ShadowWeaponMesh->SetCastShadow(false);
	ShadowWeaponMesh->SetCastHiddenShadow(false);

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

	if (ShadowWeaponMesh)
	{
		if (!ShadowWeaponMesh->GetSkeletalMeshAsset() && ThirdPersonWeaponMesh)
		{
			ShadowWeaponMesh->SetSkeletalMeshAsset(ThirdPersonWeaponMesh->GetSkeletalMeshAsset());
		}
		ShadowWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShadowWeaponMesh->SetGenerateOverlapEvents(false);
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
		MovementSpeedMultiplier = FMath::Max(CoreRow->GameplayMovementSpeedMultiplier, 0.0f);
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

float AWeaponBase::GetFirstPersonProceduralRecoilMultiplier() const
{
	return FirstPersonProceduralValues
		? FMath::Max(FirstPersonProceduralValues->WeaponValues.FirstPersonRecoilMultiplier, 0.0f)
		: 1.0f;
}

float AWeaponBase::GetThirdPersonProceduralRecoilMultiplier() const
{
	return FirstPersonProceduralValues
		? FMath::Max(FirstPersonProceduralValues->WeaponValues.ThirdPersonRecoilMultiplier, 0.0f)
		: 1.0f;
}

float AWeaponBase::GetThirdPersonProceduralSprintMultiplier() const
{
	return FirstPersonProceduralValues
		? FMath::Max(FirstPersonProceduralValues->WeaponValues.ThirdPersonSprintMultiplier, 0.0f)
		: 1.0f;
}

float AWeaponBase::GetThirdPersonProceduralWallOffsetMultiplier() const
{
	return FirstPersonProceduralValues
		? FMath::Max(FirstPersonProceduralValues->WeaponValues.ThirdPersonWallOffsetMultiplier, 0.0f)
		: 1.0f;
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
	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->SetHiddenInGame(true);
		ShadowWeaponMesh->SetCastShadow(false);
		ShadowWeaponMesh->SetCastHiddenShadow(false);
	}
	SetEquippedCollisionEnabled(true);
}

void AWeaponBase::SetStowedPresentation()
{
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

	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->SetHiddenInGame(true);
		ShadowWeaponMesh->SetCastShadow(false);
		ShadowWeaponMesh->SetCastHiddenShadow(false);
	}

	SetEquippedCollisionEnabled(false);
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
	RefreshShadowWeaponPresentation();
	SetEquippedCollisionEnabled(false);
}

void AWeaponBase::CollectStealthMeshes(
	TArray<UMeshComponent*>& OutFirstPersonMeshes,
	TArray<UMeshComponent*>& OutThirdPersonMeshes) const
{
	// 은신 머티리얼/스텐실 적용과 원상복구는 UMaterialPostProcessSubsystem 이 전담한다.
	// 무기는 자기 메시가 1인칭인지 3인칭인지만 답한다.
	TArray<UMeshComponent*> MeshComponents;
	GetComponents<UMeshComponent>(MeshComponents);

	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		if (MeshComponent == FirstPersonWeaponMesh
			|| (FirstPersonWeaponMesh && MeshComponent->IsAttachedTo(FirstPersonWeaponMesh)))
		{
			OutFirstPersonMeshes.Add(MeshComponent);
		}
		else if (MeshComponent == ThirdPersonWeaponMesh
			|| (ThirdPersonWeaponMesh && MeshComponent->IsAttachedTo(ThirdPersonWeaponMesh)))
		{
			OutThirdPersonMeshes.Add(MeshComponent);
		}
	}
}

void AWeaponBase::RefreshShadowWeaponPresentation()
{
	if (!ShadowWeaponMesh)
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	const bool bLocalView = Shooter && Shooter->IsLocallyControlled();

	if (!ShadowWeaponMesh->GetSkeletalMeshAsset() && ThirdPersonWeaponMesh)
	{
		ShadowWeaponMesh->SetSkeletalMeshAsset(ThirdPersonWeaponMesh->GetSkeletalMeshAsset());
	}

	if (ThirdPersonWeaponMesh)
	{
		ShadowWeaponMesh->SetLeaderPoseComponent(ThirdPersonWeaponMesh);
		ThirdPersonWeaponMesh->SetCastShadow(!bLocalView);
		ThirdPersonWeaponMesh->SetCastHiddenShadow(false);
	}

	ShadowWeaponMesh->SetHiddenInGame(true);
	ShadowWeaponMesh->SetVisibility(true, true);
	ShadowWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShadowWeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ShadowWeaponMesh->SetGenerateOverlapEvents(false);
	ShadowWeaponMesh->SetOwnerNoSee(false);
	ShadowWeaponMesh->SetOnlyOwnerSee(false);
	ShadowWeaponMesh->SetRenderInMainPass(true);
	ShadowWeaponMesh->SetRenderInDepthPass(false);
	ShadowWeaponMesh->SetCastShadow(bLocalView);
	ShadowWeaponMesh->SetCastHiddenShadow(bLocalView);
	ShadowWeaponMesh->SetComponentTickEnabled(bLocalView);
}

void AWeaponBase::ApplyReplicatedPresentation()
{
	if (bIsEquipped)
	{
		AttachWeaponMeshesToOwner(this, WeaponOwner);

		// SetEquippedPresentation() 이 아니라 virtual 인 ShowEquippedPresentation() 을 부른다.
		// 전자는 AWeaponBase 의 무기 메시 3개만 건드리고, ARangedWeaponBase 의 sight / 탄창 /
		// 과충전 동기화는 후자에만 있다. 원래 클라에서 그걸 켜주던 유일한 경로가
		// equip 몽타주의 AnimNotify_AttachWeapon 이었는데, 리로드 복원은 몽타주를
		// 건너뛰므로 아무도 안 켜줘서 sight 가 사라진 채로 남았다.
		ShowEquippedPresentation();
		return;
	}

	// 소유자는 있는데 손에 들지 않은 상태 (비활성 슬롯).
	// OnUnequipped 가 bIsEquipped 만 내리고 WeaponOwner 는 남겨두므로 이 조합이 "권총집"을 뜻한다.
	// (OnDropped / OnOwnerLost 는 WeaponOwner 까지 지우므로 아래 월드 픽업 경로로 간다.)
	// 이 분기가 없으면 아래로 내려가 메시를 캐릭터에서 떼고 액터 루트에 붙인 뒤
	// SetPickupPresentation() 으로 보이게 만들어서, 소유 중인 무기가 액터 위치
	// (= 주웠거나 복원된 자리)에 떨어져 있는 것처럼 보인다.
	if (WeaponOwner)
	{
		AttachWeaponMeshesToOwner(this, WeaponOwner);
		SetStowedPresentation();
		return;
	}


	FirstPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	ThirdPersonWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}

	FirstPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);

	ThirdPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);
	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale
		);
	}

	SetPickupPresentation();
}

void AWeaponBase::OnRep_EquippedState()
{
	ApplyReplicatedPresentation();
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
	if (!Interactor || bIsEquipped || bPickupConsumed || WeaponOwner != nullptr || IsPendingKillPending())
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

	SetOwner(NewOwner);
	AttachWeaponMeshesToOwner(this, NewOwner);

	// Equip 몽타주 Notify 전까지 1P 무기는 숨겨둘 수도 있음
	if (FirstPersonWeaponMesh)
	{
		FirstPersonWeaponMesh->SetHiddenInGame(true);
	}

	if (ThirdPersonWeaponMesh)
	{
		ThirdPersonWeaponMesh->SetHiddenInGame(true);
	}

	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->SetHiddenInGame(true);
		ShadowWeaponMesh->SetCastShadow(false);
		ShadowWeaponMesh->SetCastHiddenShadow(false);
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] OnEquipped Owner=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(NewOwner));
	ForceNetUpdate();
}

void AWeaponBase::AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner)
{
	if (!Weapon || !NewOwner)
	{
		return;
	}

	if (APartnerCharacter* Partner = Cast<APartnerCharacter>(NewOwner))
	{
		const FAttachmentTransformRules PartnerAttachRules =
			FAttachmentTransformRules::SnapToTargetNotIncludingScale;
		const FName FirstPersonSocketName = Partner->GetFirstPersonWeaponAttachSocketName();
		const FName ThirdPersonSocketName = Partner->GetThirdPersonWeaponAttachSocketName();

		if (USkeletalMeshComponent* FirstPersonReferenceMesh = Partner->GetFirstPersonMesh())
		{
			USceneComponent* FirstPersonParent = Partner->GetFirstPersonWeaponRoot();
			const bool bSocketExists = FirstPersonReferenceMesh->DoesSocketExist(FirstPersonSocketName);
			if (FirstPersonParent && bSocketExists)
			{
				FirstPersonParent->SetWorldTransform(
					FirstPersonReferenceMesh->GetSocketTransform(FirstPersonSocketName, RTS_World));
			}

			if (FirstPersonParent)
			{
				Weapon->GetFirstPersonWeaponMesh()->AttachToComponent(
					FirstPersonParent,
					PartnerAttachRules);
			}

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[PartnerWeaponAttach][FP] Weapon=%s Mesh=%s StableRoot=%s ReferenceMesh=%s Socket=%s Exists=%d"),
				*GetNameSafe(Weapon),
				*GetNameSafe(Weapon->GetFirstPersonWeaponMesh()),
				*GetNameSafe(FirstPersonParent),
				*GetNameSafe(FirstPersonReferenceMesh),
				*FirstPersonSocketName.ToString(),
				bSocketExists ? 1 : 0);
		}

		if (USkeletalMeshComponent* ThirdPersonParent = Partner->GetMesh())
		{
			Weapon->GetThirdPersonWeaponMesh()->AttachToComponent(
				ThirdPersonParent,
				PartnerAttachRules,
				ThirdPersonSocketName);

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[PartnerWeaponAttach][TP] Weapon=%s Mesh=%s Parent=%s Socket=%s Exists=%d"),
				*GetNameSafe(Weapon),
				*GetNameSafe(Weapon->GetThirdPersonWeaponMesh()),
				*GetNameSafe(ThirdPersonParent),
				*ThirdPersonSocketName.ToString(),
				ThirdPersonParent->DoesSocketExist(ThirdPersonSocketName) ? 1 : 0);
		}

		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(NewOwner);
	if (!Shooter)
	{
		return;
	}

	const EWeaponType EquippedWeaponType = Weapon->GetWeaponType();
	FName FirstPersonSocketName = Shooter->GetFirstPersonWeaponSocketByType(EquippedWeaponType);
	FName ThirdPersonSocketName = Shooter->GetThirdPersonWeaponSocketByType(EquippedWeaponType);
	const FAttachmentTransformRules WeaponAttachRules(
		EAttachmentRule::KeepRelative,
		EAttachmentRule::KeepRelative,
		EAttachmentRule::SnapToTarget,
		false
	);

	if (USkeletalMeshComponent* FirstPersonParent = Shooter->GetFirstPersonMesh())
	{
		const bool bHasFirstPersonSocket = FirstPersonParent->DoesSocketExist(FirstPersonSocketName);
		Weapon->GetFirstPersonWeaponMesh()->AttachToComponent(
			FirstPersonParent,
			WeaponAttachRules,
			FirstPersonSocketName
		);

		const FTransform FirstPersonSocketTransform =
			FirstPersonParent->GetSocketTransform(FirstPersonSocketName, RTS_Component);
		const FTransform FirstPersonWeaponRelativeTransform =
			Weapon->GetFirstPersonWeaponMesh()->GetRelativeTransform();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[WeaponAttach][FP] Weapon=%s Type=%d Parent=%s Socket=%s Exists=%d SocketLoc=%s SocketRot=%s WeaponRelLoc=%s WeaponRelRot=%s"),
			*GetNameSafe(Weapon),
			static_cast<int32>(EquippedWeaponType),
			*GetNameSafe(FirstPersonParent),
			*FirstPersonSocketName.ToString(),
			bHasFirstPersonSocket ? 1 : 0,
			*FirstPersonSocketTransform.GetLocation().ToCompactString(),
			*FirstPersonSocketTransform.Rotator().ToCompactString(),
			*FirstPersonWeaponRelativeTransform.GetLocation().ToCompactString(),
			*FirstPersonWeaponRelativeTransform.Rotator().ToCompactString()
		);
	}

	if (USkeletalMeshComponent* ThirdPersonParent = Shooter->GetMesh())
	{
		const bool bHasThirdPersonSocket = ThirdPersonParent->DoesSocketExist(ThirdPersonSocketName);
		Weapon->GetThirdPersonWeaponMesh()->AttachToComponent(
			ThirdPersonParent,
			WeaponAttachRules,
			ThirdPersonSocketName
		);

		const FTransform ThirdPersonSocketTransform =
			ThirdPersonParent->GetSocketTransform(ThirdPersonSocketName, RTS_Component);
		const FTransform ThirdPersonWeaponRelativeTransform =
			Weapon->GetThirdPersonWeaponMesh()->GetRelativeTransform();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[WeaponAttach][TP] Weapon=%s Type=%d Parent=%s Socket=%s Exists=%d SocketLoc=%s SocketRot=%s WeaponRelLoc=%s WeaponRelRot=%s"),
			*GetNameSafe(Weapon),
			static_cast<int32>(EquippedWeaponType),
			*GetNameSafe(ThirdPersonParent),
			*ThirdPersonSocketName.ToString(),
			bHasThirdPersonSocket ? 1 : 0,
			*ThirdPersonSocketTransform.GetLocation().ToCompactString(),
			*ThirdPersonSocketTransform.Rotator().ToCompactString(),
			*ThirdPersonWeaponRelativeTransform.GetLocation().ToCompactString(),
			*ThirdPersonWeaponRelativeTransform.Rotator().ToCompactString()
		);
	}

	if (USkeletalMeshComponent* ShadowParent = Shooter->GetShadowMesh())
	{
		Weapon->GetShadowWeaponMesh()->AttachToComponent(
			ShadowParent,
			WeaponAttachRules,
			ThirdPersonSocketName
		);
		Weapon->RefreshShadowWeaponPresentation();
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[WeaponAttach] FP Socket=%s TP Socket=%s FPParent=%s TPParent=%s"),
		*FirstPersonSocketName.ToString(),
		*ThirdPersonSocketName.ToString(),
		*GetNameSafe(Shooter->GetFirstPersonMesh()),
		*GetNameSafe(Shooter->GetMesh())
	);
}

void AWeaponBase::AttachWeaponMeshesToOwnerMeshes()
{
	ACharacter* CharacterOwner = Cast<ACharacter>(WeaponOwner);
	if (!CharacterOwner)
	{
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[WeaponAttach] Weapon=%s Owner=%s FP=%s TP=%s"),
		*GetNameSafe(this),
		*GetNameSafe(CharacterOwner),
		*GetNameSafe(FirstPersonWeaponMesh),
		*GetNameSafe(ThirdPersonWeaponMesh)
	);

	AttachWeaponMeshesToOwner(this, CharacterOwner);
}

void AWeaponBase::ShowEquippedPresentation()
{
	SetEquippedPresentation();
}

void AWeaponBase::OnUnequipped()
{
	StopAttack();

	bIsEquipped = false;
	bIsAttacking = false;

	SetStowedPresentation();

	// OnEquipped / OnDropped 와 달리 여기만 빠져 있었다. 스토우 전이가 원격 클라에
	// 늦게(혹은 안) 도착하면 그 클라에서는 무기가 손에 든 상태로 남는다.
	ForceNetUpdate();

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
	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}

	FirstPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);

	ThirdPersonWeaponMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale
	);
	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale
		);
	}

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

bool AWeaponBase::Interact(class AFirstPersonCharacter* Interactor)
{
	if (!Interactor)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] Interact blocked: interactor is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return false;
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
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Interact Interactor=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(Interactor));

	// 이 액터를 그대로 넘기지 않는다. 레벨 배치 액터의 ULevel 은 WP 셀로 고정이고
	// 런타임에 옮길 수 없어서, 그대로 장착시키면 플레이어가 그 셀에서 멀어질 때
	// 손에 든 채로 사라진다. 같은 클래스로 PersistentLevel 에 하나 만들어 넘기고,
	// 원본은 소비 처리한다 (일회성 획득이므로 월드에 남아서도 안 된다).
	AWeaponBase* GrantedWeapon = SpawnLoadoutWeapon(GetWorld(), GetClass(), Interactor);
	if (!GrantedWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("%s [%s] Interact failed: could not spawn owned copy"),
			OutlierNet::GetNetPrefix(this), *GetName());
		return false;
	}

	Interactor->EquipWeapon(GrantedWeapon);

	if (Interactor->GetCurrentWeapon() != GrantedWeapon)
	{
		// 장착이 거절됐다. 방금 만든 사본만 정리하고 원본은 월드에 그대로 둔다.
		UE_LOG(LogTemp, Warning,
			TEXT("%s [%s] Interact rejected by %s; discarding spawned copy"),
			OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(Interactor));
		GrantedWeapon->Destroy();
		return false;
	}

	ConsumePickup();
	return true;
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

AWeaponBase* AWeaponBase::SpawnLoadoutWeapon(
	UWorld* World, TSubclassOf<AWeaponBase> WeaponClass, ACharacter* OwnerCharacter)
{
	if (!World || !WeaponClass || !OwnerCharacter)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// OverrideLevel 을 주지 않는다 -> PersistentLevel.
	// 플레이어 소유 무기는 Gameplay Data Layer 리로드에도, 거리 기반 셀 스트리밍에도
	// 살아남아야 한다. (AWeaponSpawnPoint 는 반대로 GetLevel()=WP 셀에 넣는다 —
	//  그쪽은 월드와 함께 죽어도 되는 배치물이다.)

	return World->SpawnActor<AWeaponBase>(
		WeaponClass, OwnerCharacter->GetActorTransform(), SpawnParams);
}

void AWeaponBase::ConsumePickup()
{
	bPickupConsumed = true;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	ForceNetUpdate();

	// 즉시 Destroy 하지 않는다. ServerInteract 는 Interact() 가 true 를 돌려준 뒤에도
	// 이 액터의 InteractableComponent 로 CommitHoldInteraction 을 부르고, 액터 참조를
	// ClientOnInteractSucceeded 로 복제한다. 한 틱 미뤄야 그 뒷단이 살아 있는 객체를 본다.
	GetWorldTimerManager().SetTimerForNextTick(this, &AWeaponBase::DestroyAfterPickup);
}

void AWeaponBase::DestroyAfterPickup()
{
	if (HasAuthority() && !IsActorBeingDestroyed())
	{
		Destroy();
	}
}

void AWeaponBase::OnOwnerLost()
{
	StopAttack();

	bIsEquipped = false;
	bIsAttacking = false;
	WeaponOwner = nullptr;
	SetOwner(nullptr);

	// 무기는 일회성이다 — 소유자를 잃으면 월드에 남기지 않고 파괴한다.
	// 다시 필요해지면 PlayerState의 로드아웃 스냅샷이 클래스로부터 재생성한다.
	// 예전에는 스폰포인트 유무로 파괴/은닉이 갈렸는데, 은닉 쪽은 되돌리는 경로가 없어서
	// 리로드 한 번이면 보이지도 주울 수도 없는 액터가 그 자리에 영구히 남았다.
	if (!IsActorBeingDestroyed())
	{
		Destroy();
	}
}
