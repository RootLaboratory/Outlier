#include "PostProcess/OutlierStencilSubsystem.h"

#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "OutlierPlayerState.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"
#include "Enemy/AutoTurret.h"
#include "PostProcess/TeammateOutlineMaskComponent.h"
#include "TeammateOutlineRendering.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
AFirstPersonCharacter* ResolvePairOutlineTarget(const APlayerController* Viewer)
{
	if (!IsValid(Viewer) || !Viewer->IsLocalController()) return nullptr;
	const AOutlierPlayerState* Pair = Viewer->GetPlayerState<AOutlierPlayerState>();
	if (!Pair) return nullptr;
	AFirstPersonCharacter* Other = Pair->IsShooterPlayer()
		? static_cast<AFirstPersonCharacter*>(Pair->GetPartnerCharacter())
		: Pair->IsPartnerPlayer() ? static_cast<AFirstPersonCharacter*>(Pair->GetShooterCharacter()) : nullptr;
	return IsValid(Other) && Other != Viewer->GetPawn() && Other->GetWorld() == Viewer->GetWorld()
		&& !Other->IsActorBeingDestroyed() ? Other : nullptr;
}
}

void UOutlierStencilSubsystem::GatherCharacterOutlineMeshes(const AFirstPersonCharacter* Character,
	bool bFirstPerson, TArray<UMeshComponent*>& OutMeshes)
{
	if (!IsValid(Character) || Character->IsActorBeingDestroyed() || Character->IsHidden()) return;
	USceneComponent* Root = bFirstPerson ? Character->GetFirstPersonMesh() : Character->GetMesh();
	if (!Root) return;

	// 소켓에 붙은 무기는 별도 Actor 소유다. 실제 메시 계층을 따라가되 캐릭터와
	// 소유 무기의 해당 시점 메시만 허용하여 그림자 복제/임의 Attached Actor를 섞지 않는다.
	TArray<USceneComponent*> Components;
	Root->GetChildrenComponents(true, Components);
	Components.Add(Root);
	if (bFirstPerson)
	{
		// Partner의 FP 무기는 본체 대신 카메라의 독립 루트에 부착된다.
		if (const AWeaponBase* Weapon = Character->GetCurrentWeapon();
			IsValid(Weapon) && Weapon->GetWeaponOwner() == Character)
		{
			if (USceneComponent* WeaponRoot = Weapon->GetFirstPersonWeaponMesh())
			{
				Components.AddUnique(WeaponRoot);
				TArray<USceneComponent*> WeaponChildren;
				WeaponRoot->GetChildrenComponents(true, WeaponChildren);
				Components.Append(WeaponChildren);
			}
		}
	}
	for (USceneComponent* Component : Components)
	{
		UMeshComponent* Mesh = Cast<UMeshComponent>(Component);
		if (!IsValid(Mesh) || (!Mesh->IsA<USkinnedMeshComponent>() && !Mesh->IsA<UStaticMeshComponent>())
			|| !Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame || !Mesh->bRenderInMainPass
			|| Mesh->ComponentHasTag(TEXT("Outlier.StencilExclude"))) continue;
		const AActor* Owner = Mesh->GetOwner();
		if (!IsValid(Owner) || Owner->IsHidden() || Owner->IsActorBeingDestroyed()) continue;
		if (Owner != Character)
		{
			const AWeaponBase* Weapon = Cast<AWeaponBase>(Owner);
			if (!Weapon || Weapon->GetWeaponOwner() != Character) continue;
			const USceneComponent* WeaponRoot = bFirstPerson
				? Weapon->GetFirstPersonWeaponMesh() : Weapon->GetThirdPersonWeaponMesh();
			if (!WeaponRoot || (Mesh != WeaponRoot && !Mesh->IsAttachedTo(WeaponRoot))) continue;
		}
		if (!bFirstPerson && Mesh->bOnlyOwnerSee) continue;
		if (bFirstPerson && Mesh->bOwnerNoSee) continue;
		OutMeshes.AddUnique(Mesh);
	}
}

void UOutlierStencilSubsystem::GatherOutlineMeshes(const APlayerController* Viewer,
	TArray<UMeshComponent*>& OutTeammateMeshes, TArray<UMeshComponent*>& OutFirstPersonMeshes) const
{
	OutTeammateMeshes.Reset();
	OutFirstPersonMeshes.Reset();
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !IsValid(Viewer)
		|| !Viewer->IsLocalController() || Viewer->GetWorld() != World) return;
	GatherCharacterOutlineMeshes(ResolvePairOutlineTarget(Viewer), false, OutTeammateMeshes);
	GatherCharacterOutlineMeshes(Cast<AFirstPersonCharacter>(Viewer->GetPawn()), true, OutFirstPersonMeshes);
}

void UOutlierStencilSubsystem::SetIndependentPairOutlineEnabled(bool bEnabled)
{
	if (bIndependentPairOutlineEnabled == bEnabled) return;
	bIndependentPairOutlineEnabled = bEnabled;
	Reconcile();
	MarkTeammatesDirty();
}

void UOutlierStencilSubsystem::SetIndependentTurretOutlineState(
	const TArray<TWeakObjectPtr<AActor>>& ReadyTargets, bool bEnabled, bool bReleaseMasks)
{
	if (bReleaseMasks) ReleaseTurretMasks();
	TSet<TWeakObjectPtr<AActor>> Ready;
	for (const auto& Target : ReadyTargets) if (Target.IsValid()) Ready.Add(Target);
	if (bTurretOutlineManaged && bTurretOutlineEnabled == bEnabled
		&& Ready.Num() == ReadyTurretOutlines.Num() && Ready.Includes(ReadyTurretOutlines)) return;
	bTurretOutlineManaged = true;
	bTurretOutlineEnabled = bEnabled;
	ReadyTurretOutlines = MoveTemp(Ready);
	Reconcile();
}

void UOutlierStencilSubsystem::ReleaseTurretMasks(AActor* Target)
{
	for (auto It = TurretMasks.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* Source = It.Key().Get();
		if (!Target || !Source || Source->GetOwner() == Target)
		{
			if (auto* Proxy = It.Value().Proxy.Get()) Proxy->DestroyComponent();
			It.RemoveCurrent();
		}
	}
	if (Target)
	{
		ReadyTurretOutlines.Remove(Target);
		ExcludedTurretOutlines.Remove(Target);
		InvalidTurretConfigurations.Remove(Target);
	}
	else
	{
		ReadyTurretOutlines.Reset();
		ExcludedTurretOutlines.Reset();
		InvalidTurretConfigurations.Reset();
	}
}

void UOutlierStencilSubsystem::GatherTurretOutlineTargets(const APlayerController* Viewer,
	TArray<FTeammateOutlineTargetData>& OutTargets)
{
	OutTargets.Reset();
	if (!IsValid(Viewer) || !Viewer->IsLocalController() || Viewer->GetWorld() != GetWorld()
		|| GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> UsedMasks;
	const TSet<TWeakObjectPtr<AActor>> PreviousExclusions = ExcludedTurretOutlines;
	ExcludedTurretOutlines.Reset();
	for (const auto& WeakTarget : RegisteredTeammates)
	{
		AAutoTurret* Turret = Cast<AAutoTurret>(WeakTarget.Get());
		if (!Turret || !Turret->IsEligibleForTeammateOutline() || Turret == Viewer->GetPawn()) continue;
		const auto* Controller = Cast<APlayerController>(Turret->GetController());
		if (Controller && Controller->IsLocalController()) continue;
		FTeammateOutlineTargetData& Target = OutTargets.AddDefaulted_GetRef();
		Target.Owner = Turret;
		const int32 Generation = Turret->GetRoomWaveGameplayGeneration();
		const int32 Activation = Turret->GetRoomWaveActivationSerial();
		Target.IsEligible.BindWeakLambda(Turret, [Turret, Generation, Activation]()
		{
			const auto* LocalController = Cast<APlayerController>(Turret->GetController());
			// 풀의 같은 Actor가 다시 아군이 되어도 이전 수명의 메시 스냅샷은 재사용하지 않는다.
			return Turret->IsEligibleForTeammateOutline() && Turret->GetRoomWaveGameplayGeneration() == Generation
				&& Turret->GetRoomWaveActivationSerial() == Activation && !(LocalController && LocalController->IsLocalController());
		});
		TArray<UMeshComponent*> Meshes;
		Turret->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (IsEligibleMesh(Mesh)) Target.Meshes.Add(Mesh);
		}

		// 슬롯 이름은 정상 갱신 시에만 해석한다. 렌더 제출에서는 이 캐시와 생존 자격만 읽는다.
		TMap<USkeletalMeshComponent*, TSet<int32>> ExcludedSlots;
		uint32 InvalidHash = 0;
		for (const auto& Setting : Turret->GetTeammateOutlineExclusions())
		{
			if (Setting.MaterialSlotNames.IsEmpty()) continue;
			auto* Source = Cast<USkeletalMeshComponent>(Setting.MeshComponent.GetComponent(Turret));
			if (!Source || Source->GetOwner() != Turret || !IsEligibleMesh(Source))
			{
				InvalidHash = HashCombine(InvalidHash, GetTypeHash(Setting.MeshComponent.ComponentProperty));
				InvalidHash = HashCombine(InvalidHash, 1u);
				continue;
			}
			const TArray<FName> Slots = Source->GetMaterialSlotNames();
			for (FName Name : Setting.MaterialSlotNames)
			{
				const int32 Slot = Slots.IndexOfByKey(Name);
				if (Slot != INDEX_NONE) ExcludedSlots.FindOrAdd(Source).Add(Slot);
				else InvalidHash = HashCombine(HashCombine(InvalidHash, GetTypeHash(Name)), 1u);
			}
		}
		if (InvalidHash && InvalidTurretConfigurations.FindRef(Turret) != InvalidHash)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TeammateOutline] %s: invalid mesh/slot entries ignored; select an owned mesh and its material slot name."), *Turret->GetName());
			InvalidTurretConfigurations.Add(Turret, InvalidHash);
		}
		if (!InvalidHash) InvalidTurretConfigurations.Remove(Turret);
		Target.bHasExclusions = !ExcludedSlots.IsEmpty();
		Target.bExclusionResourcesReady = true;
		if (Target.bHasExclusions) ExcludedTurretOutlines.Add(Turret);
		for (const auto& Pair : ExcludedSlots)
		{
			USkeletalMeshComponent* Source = Pair.Key;
			UsedMasks.Add(Source);
			const FSkeletalMeshRenderData* RenderData = Source->GetSkeletalMeshRenderData();
			USkeletalMesh* Asset = Source->GetSkeletalMeshAsset();
			uint32 Hash = 0;
			// 순서가 다른 중복 설정도 같은 결과를 내도록 실제 슬롯 순서로 해시한다.
			for (int32 Slot = 0; Slot < Source->GetNumMaterials(); ++Slot)
			{
				Hash = HashCombine(Hash, Pair.Value.Contains(Slot) ? 1u : 0u);
				Hash = HashCombine(Hash, GetTypeHash(Source->GetMaterial(Slot)));
			}
			if (RenderData)
			{
				for (int32 LOD = 0; LOD < RenderData->LODRenderData.Num(); ++LOD)
				{
					const FSkeletalMeshLODInfo* Info = Asset->GetLODInfo(LOD);
					for (const auto& Section : RenderData->LODRenderData[LOD].RenderSections)
						Hash = HashCombine(Hash, Section.MaterialIndex);
					if (Info) for (int32 Material : Info->LODMaterialMap) Hash = HashCombine(Hash, GetTypeHash(Material));
				}
			}
			FOutlierTurretOutlineMaskState& State = TurretMasks.FindOrAdd(Source);
			if (State.Asset.Get() != Asset || State.ConfigurationHash != Hash) State.bFailureReported = false;
			if (State.Asset.Get() != Asset || State.ConfigurationHash != Hash || !State.Proxy.IsValid())
			{
				if (auto* Previous = State.Proxy.Get()) Previous->DestroyComponent();
				State.Proxy.Reset();
				State.Asset = Asset;
				State.ConfigurationHash = Hash;
				auto* Proxy = NewObject<UTeammateOutlineMaskComponent>(Turret, NAME_None, RF_Transient);
				if (RenderData && Proxy->InitializeFrom(Source))
				{
					// 원본 섹션은 그대로 둔다. 프록시에는 제외 슬롯만 남겨 전체 깊이와 비교한다.
					for (int32 LOD = 0; LOD < RenderData->LODRenderData.Num(); ++LOD)
					{
						const auto& Sections = RenderData->LODRenderData[LOD].RenderSections;
						const FSkeletalMeshLODInfo* Info = Asset->GetLODInfo(LOD);
						for (int32 Section = 0; Section < Sections.Num(); ++Section)
						{
							int32 Material = Sections[Section].MaterialIndex;
							if (Info && Info->LODMaterialMap.IsValidIndex(Section) && Info->LODMaterialMap[Section] != INDEX_NONE)
								Material = Info->LODMaterialMap[Section];
							Proxy->ShowMaterialSection(Sections[Section].MaterialIndex, Section, Pair.Value.Contains(Material), LOD);
						}
					}
					Proxy->RegisterComponent();
					State.Proxy = Proxy;
				}
				else
				{
					Proxy->DestroyComponent();
					if (!State.bFailureReported)
					{
						UE_LOG(LogTemp, Warning, TEXT("[TeammateOutline] %s: excluded-slot mask unavailable; teammate output suppressed until recovery."), *Source->GetPathName());
						State.bFailureReported = true;
					}
				}
			}
			if (State.Proxy.IsValid()) Target.ExcludedMeshes.Add(State.Proxy);
			else Target.bExclusionResourcesReady = false;
		}
	}
	// 메시 교체·풀 재사용·설정 삭제에서 이전 마스크가 새 수명에 남지 않게 정리한다.
	for (auto It = TurretMasks.CreateIterator(); It; ++It)
	{
		if (!UsedMasks.Contains(It.Key()))
		{
			if (auto* Proxy = It.Value().Proxy.Get()) Proxy->DestroyComponent();
			It.RemoveCurrent();
		}
	}
	// 준비 결과가 계속 빈 집합이어도 제외 설정이 바뀌면 fallback 자격은 즉시 다시 계산한다.
	if (PreviousExclusions.Num() != ExcludedTurretOutlines.Num()
		|| !PreviousExclusions.Includes(ExcludedTurretOutlines)) Reconcile();
}

bool UOutlierStencilSubsystem::IsEligibleMesh(const UMeshComponent* Mesh)
{
	if (!IsValid(Mesh) || (!Mesh->IsA<USkinnedMeshComponent>() && !Mesh->IsA<UStaticMeshComponent>())
		|| Mesh->bOnlyOwnerSee || !Mesh->bRenderInMainPass || Mesh->ComponentHasTag(TEXT("Outlier.StencilExclude")))
	{
		return false;
	}
	// 플레이어 캐릭터에는 본체와 별도로 그림자 및 1인칭 메시가 있다. 적 캐릭터의
	// 별도 본체 부위(예: 터렛 머리)는 정상적인 구성 요소이므로 제외하지 않는다.
	if (const AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(Mesh->GetOwner()))
	{
		const USceneComponent* Body = Character->GetMesh();
		return Mesh == Body || (Body && Mesh->IsAttachedTo(Body));
	}
	return true;
}

FOutlierStencilMeshState& UOutlierStencilSubsystem::TrackMesh(UMeshComponent* Mesh)
{
	const TWeakObjectPtr<UMeshComponent> Key(Mesh);
	if (FOutlierStencilMeshState* Existing = MeshStates.Find(Key)) return *Existing;
	FOutlierStencilMeshState State;
	State.bOriginalCustomDepth = Mesh->bRenderCustomDepth;
	State.OriginalStencil = Mesh->CustomDepthStencilValue;
	State.OriginalWriteMask = Mesh->CustomDepthStencilWriteMask;
	return MeshStates.Add(Key, State);
}

void UOutlierStencilSubsystem::WriteMesh(UMeshComponent* Mesh, bool bEnabled, int32 Stencil,
	ERendererStencilMask WriteMask)
{
	if (Mesh->CustomDepthStencilValue != Stencil) Mesh->SetCustomDepthStencilValue(Stencil);
	if (Mesh->CustomDepthStencilWriteMask != WriteMask) Mesh->SetCustomDepthStencilWriteMask(WriteMask);
	if (!!Mesh->bRenderCustomDepth != bEnabled) Mesh->SetRenderCustomDepth(bEnabled);
}

void UOutlierStencilSubsystem::RegisterReadability(UMeshComponent* Mesh)
{
	if (GetWorld()->GetNetMode() == NM_DedicatedServer || !IsEligibleMesh(Mesh)) return;
	TrackMesh(Mesh).bReadability = true;
	Reconcile();
}

void UOutlierStencilSubsystem::SetEffect(AActor* Target, UObject* Source,
	EOutlierStencilEffect Effect, int32 ScanType)
{
	if (GetWorld()->GetNetMode() == NM_DedicatedServer || !IsValid(Target) || !IsValid(Source)
		|| Target->GetWorld() != GetWorld() || Target->IsActorBeingDestroyed()) return;
	if (Effect == EOutlierStencilEffect::Scan && ScanType != 1 && ScanType != 2) return;
	FOutlierStencilRequest* Request = Requests.FindByPredicate([=](const auto& Item)
	{
		return Item.Target.Get() == Target && Item.Source.Get() == Source && Item.Effect == Effect;
	});
	if (!Request)
	{
		Request = &Requests.AddDefaulted_GetRef();
		Request->Target = Target;
		Request->Source = Source;
		Request->Effect = Effect;
	}
	Request->ScanType = ScanType;
	Reconcile();
}

void UOutlierStencilSubsystem::ClearEffect(AActor* Target, UObject* Source, EOutlierStencilEffect Effect)
{
	Requests.RemoveAll([=](const auto& Item)
	{
		return Item.Target.Get() == Target && Item.Source.Get() == Source && Item.Effect == Effect;
	});
	Reconcile();
}

void UOutlierStencilSubsystem::ClearEffectsOfType(EOutlierStencilEffect Effect)
{
	Requests.RemoveAll([=](const auto& Item) { return Item.Effect == Effect; });
	Reconcile();
}

void UOutlierStencilSubsystem::ClearSourceEffects(UObject* Source, EOutlierStencilEffect Effect)
{
	Requests.RemoveAll([=](const auto& Item) { return Item.Source.Get() == Source && Item.Effect == Effect; });
	Reconcile();
}

void UOutlierStencilSubsystem::ClearActorEffects(AActor* Target)
{
	Requests.RemoveAll([=](const auto& Item) { return Item.Target.Get() == Target; });
	// 사망/풀 반납 경로다. 해킹된 터렛은 사망 후 풀 리셋 전까지 팀 플래그가 남으므로 여기서 함께 해제한다.
	RegisteredTeammates.Remove(TWeakObjectPtr<AActor>(Target));
	if (Target) ReleaseTurretMasks(Target);
	MarkTeammatesDirty();
	Reconcile();
}

void UOutlierStencilSubsystem::ResetTransientEffects()
{
	Requests.Reset();
	MarkTeammatesDirty();
	Reconcile();
}

bool UOutlierStencilSubsystem::HasEffect(EOutlierStencilEffect Effect) const
{
	return Requests.ContainsByPredicate([=](const auto& Item)
	{
		return Item.Effect == Effect && Item.Target.IsValid() && Item.Source.IsValid();
	});
}

void UOutlierStencilSubsystem::Reconcile()
{
	Requests.RemoveAll([](const auto& Item)
	{
		return !Item.Target.IsValid() || !Item.Source.IsValid() || Item.Target->IsActorBeingDestroyed();
	});
	TMap<TWeakObjectPtr<UMeshComponent>, int32> DesiredFlags;
	TArray<UMeshComponent*> Meshes;
	for (const FOutlierStencilRequest& Request : Requests)
	{
		Meshes.Reset();
		Request.Target->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!IsEligibleMesh(Mesh)) continue;
			TrackMesh(Mesh);
			int32& Flags = DesiredFlags.FindOrAdd(Mesh);
			if (Request.Effect == EOutlierStencilEffect::Scan)
			{
				// 여러 Source가 같은 대상을 다르게 분류해도 결과를 일정하게 유지한다.
				const int32 Previous = Flags & OutlierStencil::ScanMask;
				Flags = (Flags & ~OutlierStencil::ScanMask)
					| (Previous == 0 ? Request.ScanType : FMath::Min(Previous, Request.ScanType));
			}
			else Flags |= Request.Effect == EOutlierStencilEffect::Melee ? OutlierStencil::Melee : OutlierStencil::Guide;
		}
	}
	for (auto It = RegisteredTeammates.CreateIterator(); It; ++It)
	{
		if (!It->IsValid() || (*It)->IsActorBeingDestroyed()) It.RemoveCurrent();
	}
	for (const TSet<TWeakObjectPtr<AActor>>* Teammates : { &PairTeammates, &RegisteredTeammates })
	{
		// 독립 pair 마스크만 전환한다. 해킹된 팀원과 다른 스텐실 효과는 그대로 유지한다.
		if (bIndependentPairOutlineEnabled && Teammates == &PairTeammates) continue;
		for (const auto& Target : *Teammates)
		{
			if (!Target.IsValid()) continue;
			if (const AAutoTurret* Turret = Cast<AAutoTurret>(Target.Get()))
			{
				if (!Turret->IsEligibleForTeammateOutline()) continue;
				// OFF/사망은 legacy로 되살리지 않는다. 제외 마스크 실패도 해치를 다시 그리지 않는다.
				// 제외 설정 없는 대상만 RDG 준비 실패 시 기존 경로로 복구할 수 있다.
				if (bTurretOutlineManaged && (!bTurretOutlineEnabled
					|| ReadyTurretOutlines.Contains(Target) || ExcludedTurretOutlines.Contains(Target))) continue;
			}
			// 해킹으로 빙의한 드론처럼 내가 조종 중인 Pawn에는 팀원 외곽선을 그리지 않는다.
			// IsLocallyControlled는 서버의 AIController도 로컬로 판정하므로(해킹된 터렛) 로컬 PC만 본다.
			if (const APawn* Pawn = Cast<APawn>(Target.Get()))
			{
				const APlayerController* Controller = Cast<APlayerController>(Pawn->GetController());
				if (Controller && Controller->IsLocalController()) continue;
			}
			Meshes.Reset();
			Target->GetComponents<UMeshComponent>(Meshes);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (!IsEligibleMesh(Mesh)) continue;
				TrackMesh(Mesh);
				DesiredFlags.FindOrAdd(Mesh) |= OutlierStencil::Teammate;
			}
		}
	}
	for (auto It = MeshStates.CreateIterator(); It; ++It)
	{
		UMeshComponent* Mesh = It.Key().Get();
		if (!IsValid(Mesh)) { It.RemoveCurrent(); continue; }
		const FOutlierStencilMeshState& State = It.Value();
		int32 Flags = DesiredFlags.FindRef(It.Key());
		if (State.bReadability) Flags |= OutlierStencil::Readability;
		if (Flags != 0 && IsEligibleMesh(Mesh))
		{
			WriteMesh(Mesh, true, OutlierStencil::Managed | Flags, ERendererStencilMask::ERSM_Default);
		}
		else
		{
			WriteMesh(Mesh, State.bOriginalCustomDepth, State.OriginalStencil, State.OriginalWriteMask);
			if (!State.bReadability) It.RemoveCurrent();
		}
	}
}

bool UOutlierStencilSubsystem::RefreshPairTeammates()
{
	TSet<TWeakObjectPtr<AActor>> Found;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		const bool bHadAny = PairTeammates.Num() > 0;
		PairTeammates.Reset();
		return bHadAny;
	}
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Viewer = It->Get();
		if (!IsValid(Viewer) || !Viewer->IsLocalController()) continue;
		if (AFirstPersonCharacter* Other = ResolvePairOutlineTarget(Viewer))
		{
			Found.Add(Other);
		}
	}
	const bool bChanged = Found.Num() != PairTeammates.Num() || !Found.Includes(PairTeammates);
	if (bChanged) PairTeammates = MoveTemp(Found);
	return bChanged;
}

void UOutlierStencilSubsystem::MarkTeammatesDirty()
{
	bTeammatesDirty = true;
}

void UOutlierStencilSubsystem::SetActorTeammate(AActor* Target, bool bTeammate)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Target) return;
	const TWeakObjectPtr<AActor> Key(Target);
	bool bChanged = false;
	if (!bTeammate)
	{
		bChanged = RegisteredTeammates.Remove(Key) > 0;
		ReleaseTurretMasks(Target);
	}
	else if (IsValid(Target) && Target->GetWorld() == World && !Target->IsActorBeingDestroyed())
	{
		bool bAlreadyRegistered = false;
		RegisteredTeammates.Add(Key, &bAlreadyRegistered);
		bChanged = !bAlreadyRegistered;
	}
	if (bChanged)
	{
		MarkTeammatesDirty();
		Reconcile();
	}
}

void UOutlierStencilSubsystem::Tick(float DeltaTime)
{
	FallbackElapsed += DeltaTime;
	const bool bFallback = FallbackElapsed >= FallbackReconcileInterval;
	// 빙의 변경은 쌍 집합이 같아도 "내 Pawn 제외" 결과를 바꾸므로 dirty면 항상 다시 맞춘다.
	const bool bDirty = bTeammatesDirty;
	bool bChanged = false;
	if (bDirty || bFallback)
	{
		bTeammatesDirty = false;
		bChanged = RefreshPairTeammates();
	}
	if (bDirty || bChanged || bFallback)
	{
		FallbackElapsed = 0.f;
		if (Requests.Num() || MeshStates.Num() || PairTeammates.Num() || RegisteredTeammates.Num()) Reconcile();
		// Reconcile 바깥에서 알린다. 구독자가 새 마스크 상태를 적용해도 스텐실 순회에 재진입하지 않는다.
		OnTeammateMeshesChanged.Broadcast();
	}
}

TStatId UOutlierStencilSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOutlierStencilSubsystem, STATGROUP_Tickables);
}

void UOutlierStencilSubsystem::Deinitialize()
{
	ReleaseTurretMasks();
	for (const auto& Pair : MeshStates)
	{
		if (UMeshComponent* Mesh = Pair.Key.Get())
		{
			WriteMesh(Mesh, Pair.Value.bOriginalCustomDepth, Pair.Value.OriginalStencil, Pair.Value.OriginalWriteMask);
		}
	}
	Requests.Reset();
	PairTeammates.Reset();
	RegisteredTeammates.Reset();
	MeshStates.Reset();
	OnTeammateMeshesChanged.Clear();
	Super::Deinitialize();
}
