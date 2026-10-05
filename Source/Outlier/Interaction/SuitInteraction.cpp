#include "Interaction/SuitInteraction.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Interaction/InteractableComponent.h"
#include "Save/OutlierSaveSubSystem.h"
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
		// Gameplay 리로드 전에 서버 PS에서 주입한 현재 상태로 판단한다.
		// Preset은 PS를 유지하므로 과거 체크포인트의 슈트 상태를 직접 읽으면 안 된다.
		// 서버의 제거가 클라이언트에도 복제되어 표시와 상호작용이 함께 사라진다.
		const UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr;
		const bool bCurrentSuitAcquired = SaveSubsystem && SaveSubsystem->IsCurrentSuitAcquired();
		if (bCurrentSuitAcquired)
		{
			Destroy();
			return;
		}

		SpawnStoredWeapons();
	}
}

void ASuitInteraction::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (AShooterCharacter* Shooter = ReservedShooter.Get())
		{
			Shooter->CancelSuitTransition();
		}
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

	// Interaction은 예약 요청만 시작한다. 암전 완료 대기/취소의 수명은 Shooter가 관리하고 지급은 나중에 commit한다.
	return ShooterCharacter->BeginSuitTransition(this, ShooterFirstPersonMesh, ShooterThirdPersonMesh);
}

bool ASuitInteraction::CanReserveFor(AShooterCharacter* ShooterCharacter) const
{
	const APartnerCharacter* Partner = ShooterCharacter ? ShooterCharacter->GetPartnerCharacter() : nullptr;
	return HasAuthority() && !bConsumed && !ReservedShooter.IsValid()
		&& IsValid(ShooterCharacter) && IsValid(Partner) && !Partner->GetCurrentWeapon()
		&& ShooterCharacter->GetInventoryComponent() && InteractableComponent
		&& InteractableComponent->CanInteract(ShooterCharacter->GetOwnedGameplayTagsForQuery())
		&& IsValid(StoredShooterRifle) && IsValid(StoredPartnerWeapon)
		&& StoredShooterRifle->GetWeaponType() == EWeaponType::Rifle
		&& StoredShooterRifle->CanBePickedUpBy(ShooterCharacter)
		&& StoredShooterRifle->GetOwner() == this && StoredPartnerWeapon->GetOwner() == this;
}

bool ASuitInteraction::ReserveFor(AShooterCharacter* ShooterCharacter)
{
	if (!CanReserveFor(ShooterCharacter))
	{
		return false;
	}
	// 예약 중에는 다른 요청을 거부하지만 보관 무기의 소유권/획득 상태는 그대로 유지한다.
	ReservedShooter = ShooterCharacter;
	return true;
}

bool ASuitInteraction::IsReservedFor(const AShooterCharacter* ShooterCharacter) const
{
	return !bConsumed && ReservedShooter.Get() == ShooterCharacter;
}

void ASuitInteraction::ReleaseReservation(AShooterCharacter* ShooterCharacter)
{
	if (HasAuthority() && ReservedShooter.Get() == ShooterCharacter)
	{
		ReservedShooter.Reset();
	}
}

bool ASuitInteraction::CommitReservedSuit(AShooterCharacter* ShooterCharacter)
{
	// 암전 대기 사이 무기 소유권이나 상호작용 조건이 바뀔 수 있으므로 실제 외형 변경/지급 직전에 다시 확인한다.
	if (!HasAuthority() || !IsValid(ShooterCharacter) || !IsReservedFor(ShooterCharacter)
		|| !IsValid(StoredShooterRifle) || !IsValid(StoredPartnerWeapon)
		|| StoredShooterRifle->GetWeaponType() != EWeaponType::Rifle
		|| !InteractableComponent
		|| !InteractableComponent->CanInteract(ShooterCharacter->GetOwnedGameplayTagsForQuery())
		|| (InteractableComponent->RequiresHoldInteract()
			&& !InteractableComponent->CanCommitHoldInteraction(ShooterCharacter))
		|| StoredShooterRifle->GetOwner() != this || StoredPartnerWeapon->GetOwner() != this
		|| !StoredShooterRifle->CanBePickedUpBy(ShooterCharacter) || !ApplySuit(ShooterCharacter))
	{
		return false;
	}
	// 성공은 예약 시점이 아닌 지급 완료 시점에 한 번만 확정한다. 통지 전부터 재진입을 막는다.
	bConsumed = true;
	ReservedShooter.Reset();
	ShooterCharacter->CompleteDeferredInteraction(this, true);
	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Object.Get.Suit")));
	// 여기서 Interaction은 소비된다. 이후 Applying/FadingIn 대기는 이 Actor가 아닌 Shooter에 남아 있다.
	ConsumeInteraction();
	return true;
}

bool ASuitInteraction::SpawnStoredWeapons()
{
	if (!ensureMsgf(SuitDisplayMesh && SuitDisplayMesh->GetStaticMesh(),
			TEXT("SuitInteraction %s is missing its world display Suit mesh."), *GetName())
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

	// Shooter BP의 Mesh/ABP 전체 구성을 먼저 검증한다. 실패하면 무기와 Interaction을 보존한다.
	// 기존 Interaction Mesh는 BP 이전 기간의 호환 입력일 뿐, 명시된 Suit 구성을 덮어쓰지 않는다.
	if (!ShooterCharacter->ApplySuitMeshes(ShooterFirstPersonMesh, ShooterThirdPersonMesh))
	{
		return false;
	}

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
		// 이전 저장 형식에는 실제 적용된 Mesh를 남긴다. 획득 상태 투영 전에 호환 참조를 준비한다.
		PS->SetSuitMeshes(ShooterCharacter->GetFirstPersonMesh()->GetSkeletalMeshAsset(),
			ShooterCharacter->GetMesh()->GetSkeletalMeshAsset());

		// Partner 무기는 슈트 지급이 유일한 경로이고 Partner 쪽에는 InventoryComponent 가
		// 없으므로, 리로드 후 다시 만들 수 있도록 클래스를 Shooter PlayerState 에 남긴다.
		// 이 액터는 소비 직후 스스로 Destroy 하므로 여기서 안 적으면 역산할 곳이 없다.
		FOutlierLoadoutSnapshot Snapshot = PS->GetLoadoutSnapshot();
		Snapshot.PartnerWeaponClass = StoredPartnerWeapon->GetClass();
		PS->SetLoadoutSnapshot(Snapshot);
		PS->SetAcquiredSuit(true);
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
