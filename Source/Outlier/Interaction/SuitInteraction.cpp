#include "Interaction/SuitInteraction.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Interaction/InteractableComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "TimerManager.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/WeaponBase.h"
#include "OutlierPlayerState.h"

ASuitInteraction::ASuitInteraction()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));

	SuitDisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SuitDisplayMesh"));
	SuitDisplayMesh->SetupAttachment(SceneRoot);
	SuitDisplayMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SuitDisplayMesh->SetCollisionObjectType(ECC_WorldDynamic);
	SuitDisplayMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SuitDisplayMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SuitDisplayMesh->SetGenerateOverlapEvents(false);
}

void ASuitInteraction::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SpawnStoredWeapons();
	}
}

void ASuitInteraction::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		DestroyStoredWeapons();
	}

	Super::EndPlay(EndPlayReason);
}

UInteractableComponent* ASuitInteraction::GetInteractableComponent() const
{
	return InteractableComponent;
}

bool ASuitInteraction::Interact(AFirstPersonCharacter* Interactor)
{
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(Interactor);
	if (!HasAuthority()
		|| bConsumed
		|| !ShooterCharacter
		|| !InteractableComponent
		|| !InteractableComponent->CanInteract(ShooterCharacter->GetOwnedGameplayTagsForQuery()))
	{
		return false;
	}

	if (!ApplySuit(ShooterCharacter))
	{
		return false;
	}

	ConsumeInteraction();
	return true;
}

bool ASuitInteraction::SpawnStoredWeapons()
{
	if (!ensureMsgf(SuitDisplayMesh && SuitDisplayMesh->GetStaticMesh(),
			TEXT("SuitInteraction %s is missing its world display Suit mesh."), *GetName())
		|| !ensureMsgf(ShooterFirstPersonMesh, TEXT("SuitInteraction %s is missing ShooterFirstPersonMesh."), *GetName())
		|| !ensureMsgf(ShooterThirdPersonMesh, TEXT("SuitInteraction %s is missing ShooterThirdPersonMesh."), *GetName())
		|| !ensureMsgf(ShooterRifleClass, TEXT("SuitInteraction %s is missing ShooterRifleClass."), *GetName())
		|| !ensureMsgf(PartnerWeaponClass, TEXT("SuitInteraction %s is missing PartnerWeaponClass."), *GetName()))
	{
		return false;
	}

	const AWeaponBase* ShooterWeaponDefault = ShooterRifleClass->GetDefaultObject<AWeaponBase>();
	if (!ensureMsgf(
		ShooterWeaponDefault && ShooterWeaponDefault->GetWeaponType() == EWeaponType::Rifle,
		TEXT("SuitInteraction %s requires ShooterRifleClass to use EWeaponType::Rifle. Class=%s"),
		*GetName(),
		*GetNameSafe(ShooterRifleClass.Get())))
	{
		return false;
	}

	StoredShooterRifle = SpawnStoredWeapon(ShooterRifleClass.Get());
	StoredPartnerWeapon = Cast<ARangedWeaponBase>(SpawnStoredWeapon(PartnerWeaponClass.Get()));

	if (!ensureMsgf(StoredShooterRifle, TEXT("SuitInteraction %s failed to spawn the Shooter Rifle."), *GetName())
		|| !ensureMsgf(StoredPartnerWeapon, TEXT("SuitInteraction %s failed to spawn the Partner weapon."), *GetName()))
	{
		DestroyStoredWeapons();
		return false;
	}

	return true;
}

AWeaponBase* ASuitInteraction::SpawnStoredWeapon(UClass* WeaponClass)
{
	if (!HasAuthority() || !WeaponClass || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.OverrideLevel = GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AWeaponBase* Weapon = GetWorld()->SpawnActor<AWeaponBase>(
		WeaponClass,
		GetActorTransform(),
		SpawnParams);
	if (Weapon)
	{
		Weapon->OnUnequipped();
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Weapon->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent)
			{
				PrimitiveComponent->SetCastHiddenShadow(false);
			}
		}
		Weapon->SetActorHiddenInGame(true);
		Weapon->SetActorEnableCollision(false);
	}

	return Weapon;
}

bool ASuitInteraction::ApplySuit(AShooterCharacter* ShooterCharacter)
{
	APartnerCharacter* PartnerCharacter = ShooterCharacter
		? ShooterCharacter->GetPartnerCharacter()
		: nullptr;
	UShooterInventoryComponent* InventoryComponent = ShooterCharacter
		? ShooterCharacter->GetInventoryComponent()
		: nullptr;

	if (!ensureMsgf(PartnerCharacter, TEXT("SuitInteraction %s requires a paired Partner."), *GetName())
		|| !ensureMsgf(InventoryComponent, TEXT("SuitInteraction %s requires Shooter InventoryComponent."), *GetName())
		|| !ensureMsgf(StoredShooterRifle, TEXT("SuitInteraction %s has no stored Shooter Rifle."), *GetName())
		|| !ensureMsgf(StoredPartnerWeapon, TEXT("SuitInteraction %s has no stored Partner weapon."), *GetName())
		|| !ensureMsgf(!PartnerCharacter || !PartnerCharacter->GetCurrentWeapon(),
			TEXT("SuitInteraction %s requires Partner %s to be unarmed."),
			*GetName(),
			*GetNameSafe(PartnerCharacter)))
	{
		return false;
	}

	ShooterCharacter->ApplySuitMeshes(ShooterFirstPersonMesh, ShooterThirdPersonMesh);

	StoredShooterRifle->SetActorHiddenInGame(false);
	StoredShooterRifle->SetActorEnableCollision(true);
	if (!InventoryComponent->EquipSuitRifle(StoredShooterRifle))
	{
		ensureMsgf(false, TEXT("SuitInteraction %s failed to equip Shooter Rifle."), *GetName());
		return false;
	}

	StoredPartnerWeapon->SetActorHiddenInGame(false);
	StoredPartnerWeapon->SetActorEnableCollision(true);
	PartnerCharacter->EquipWeapon(StoredPartnerWeapon);
	StoredPartnerWeapon->ShowEquippedPresentation();

	if (!ensureMsgf(
		ShooterCharacter->GetCurrentWeapon() == StoredShooterRifle,
		TEXT("SuitInteraction %s did not commit Shooter CurrentWeapon."),
		*GetName())
		|| !ensureMsgf(
			PartnerCharacter->GetCurrentWeapon() == StoredPartnerWeapon,
			TEXT("SuitInteraction %s did not commit Partner CurrentWeapon."),
			*GetName()))
	{
		return false;
	}

	if (AOutlierPlayerState* PS = ShooterCharacter->GetPlayerState<AOutlierPlayerState>())
	{
		PS->SetAcquiredSuit(true);

		// 이 액터는 소비 직후 스스로 Destroy 하므로, 어떤 메시를 입혔는지
		// 여기서 PlayerState 에 남겨야 리로드 후 다시 입힐 수 있다.
		PS->SetSuitMeshes(ShooterFirstPersonMesh, ShooterThirdPersonMesh);

		// Partner 무기는 슈트 지급이 유일한 경로이고 Partner 쪽에는 InventoryComponent 가
		// 없으므로, 리로드 후 다시 만들 수 있도록 클래스를 Shooter PlayerState 에 남긴다.
		// 이 액터는 소비 직후 스스로 Destroy 하므로 여기서 안 적으면 역산할 곳이 없다.
		FOutlierLoadoutSnapshot Snapshot = PS->GetLoadoutSnapshot();
		Snapshot.PartnerWeaponClass = StoredPartnerWeapon->GetClass();
		PS->SetLoadoutSnapshot(Snapshot);
	}

	// 슈트는 페어 단위 해금이다. Partner PlayerState 에도 같은 플래그를 세워두면
	// Partner 쪽(능력 게이트, UI)이 짝의 Shooter PS 를 매번 거슬러 올라가지 않아도 된다.
	// 복제가 갱신을 대신하므로 별도 캐시 무효화가 필요 없다.
	if (AOutlierPlayerState* PartnerPS = PartnerCharacter->GetPlayerState<AOutlierPlayerState>())
	{
		PartnerPS->SetAcquiredSuit(true);
	}

	StoredShooterRifle = nullptr;
	StoredPartnerWeapon = nullptr;
	return true;
}

void ASuitInteraction::ConsumeInteraction()
{
	bConsumed = true;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	ForceNetUpdate();

	GetWorldTimerManager().SetTimerForNextTick(
		this,
		&ASuitInteraction::DestroyAfterInteraction);
}

void ASuitInteraction::DestroyAfterInteraction()
{
	if (HasAuthority())
	{
		Destroy();
	}
}

void ASuitInteraction::DestroyStoredWeapons()
{
	if (IsValid(StoredShooterRifle))
	{
		StoredShooterRifle->Destroy();
	}
	StoredShooterRifle = nullptr;

	if (IsValid(StoredPartnerWeapon))
	{
		StoredPartnerWeapon->Destroy();
	}
	StoredPartnerWeapon = nullptr;
}
