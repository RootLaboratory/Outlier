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
#include "Weapon/Spawn/WeaponSpawnPoint.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Materials/MaterialInterface.h"

namespace
{
UMaterialInterface* ResolveFirstPersonStealthGlassMaterial(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	const UMaterialPostProcessSubsystem* MaterialSubsystem =
		World->GetSubsystem<UMaterialPostProcessSubsystem>();
	return MaterialSubsystem
		? MaterialSubsystem->GetFirstPersonStealthGlassMaterial()
		: nullptr;
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

void AWeaponBase::SetStealthVisualState(
	bool bUseFirstPersonGlass,
	bool bWriteThirdPersonStencil,
	UMaterialInterface* FirstPersonGlassMaterial,
	int32 StencilValue)
{
	TArray<UMeshComponent*> MeshComponents;
	GetComponents<UMeshComponent>(MeshComponents);

	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		const bool bIsFirstPersonMesh = MeshComponent == FirstPersonWeaponMesh
			|| (FirstPersonWeaponMesh && MeshComponent->IsAttachedTo(FirstPersonWeaponMesh));
		const bool bIsThirdPersonMesh = MeshComponent == ThirdPersonWeaponMesh
			|| (ThirdPersonWeaponMesh && MeshComponent->IsAttachedTo(ThirdPersonWeaponMesh));
		if (!bIsFirstPersonMesh && !bIsThirdPersonMesh)
		{
			continue;
		}

		const bool bNeedsOverride = (bIsFirstPersonMesh
			&& bUseFirstPersonGlass
			&& FirstPersonGlassMaterial)
			|| (bIsThirdPersonMesh && bWriteThirdPersonStencil);
		if (bNeedsOverride)
		{
			if (!StealthMeshRestoreStates.Contains(MeshComponent))
			{
				FWeaponStealthMeshRestoreState RestoreState;
				RestoreState.bRenderCustomDepth = MeshComponent->bRenderCustomDepth;
				RestoreState.CustomDepthStencilValue = MeshComponent->CustomDepthStencilValue;
				const int32 MaterialCount = MeshComponent->GetNumMaterials();
				RestoreState.Materials.Reserve(MaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					RestoreState.Materials.Add(MeshComponent->GetMaterial(MaterialIndex));
				}
				StealthMeshRestoreStates.Add(MeshComponent, MoveTemp(RestoreState));
			}

			if (bIsFirstPersonMesh && bUseFirstPersonGlass && FirstPersonGlassMaterial)
			{
				for (int32 MaterialIndex = 0;
					MaterialIndex < MeshComponent->GetNumMaterials();
					++MaterialIndex)
				{
					MeshComponent->SetMaterial(MaterialIndex, FirstPersonGlassMaterial);
				}
			}

			if (bIsThirdPersonMesh && bWriteThirdPersonStencil)
			{
				MeshComponent->SetCustomDepthStencilValue(StencilValue);
				MeshComponent->SetRenderCustomDepth(true);
			}
			continue;
		}

		if (const FWeaponStealthMeshRestoreState* RestoreState =
			StealthMeshRestoreStates.Find(MeshComponent))
		{
			MeshComponent->SetCustomDepthStencilValue(RestoreState->CustomDepthStencilValue);
			MeshComponent->SetRenderCustomDepth(RestoreState->bRenderCustomDepth);
			for (int32 MaterialIndex = 0;
				MaterialIndex < RestoreState->Materials.Num();
				++MaterialIndex)
			{
				MeshComponent->SetMaterial(MaterialIndex, RestoreState->Materials[MaterialIndex]);
			}
			StealthMeshRestoreStates.Remove(MeshComponent);
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
		// Notify 전까지 숨김 상태만 유지
		AttachWeaponMeshesToOwner(this, WeaponOwner);
		SetEquippedPresentation();
		if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
			Shooter && Shooter->IsStealthed())
		{
			SetStealthVisualState(
				true,
				true,
				ResolveFirstPersonStealthGlassMaterial(GetWorld()));
		}
		return;
	}

	SetStealthVisualState(false, false, nullptr);

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

	AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(NewOwner);
	if (Character)
	{
		Character->CaptureComponentWeaponNotIncluded(this);
	}

	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(NewOwner);
		Shooter && Shooter->IsStealthed())
	{
		SetStealthVisualState(
			true,
			true,
			ResolveFirstPersonStealthGlassMaterial(GetWorld()));
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
				TEXT("[PartnerWeaponToggle][Attach][FP] Weapon=%s Mesh=%s StableRoot=%s ReferenceMesh=%s Socket=%s Exists=%d"),
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
				TEXT("[PartnerWeaponToggle][Attach][TP] Weapon=%s Mesh=%s Parent=%s Socket=%s Exists=%d"),
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
	SetStealthVisualState(false, false, nullptr);

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

	if (ShadowWeaponMesh)
	{
		ShadowWeaponMesh->SetHiddenInGame(true);
		ShadowWeaponMesh->SetCastShadow(false);
		ShadowWeaponMesh->SetCastHiddenShadow(false);
	}

	SetEquippedCollisionEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("%s [%s] OnUnequipped"), OutlierNet::GetNetPrefix(this), *GetName());
}

void AWeaponBase::OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy)
{
	StopAttack();
	SetStealthVisualState(false, false, nullptr);

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

	Interactor->EquipWeapon(this);
	return Interactor->GetCurrentWeapon() == this;
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
