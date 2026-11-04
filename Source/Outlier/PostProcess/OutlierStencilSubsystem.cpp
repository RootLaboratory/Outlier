#include "PostProcess/OutlierStencilSubsystem.h"

#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "OutlierPlayerState.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"

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
	Reconcile();
}

void UOutlierStencilSubsystem::ResetTransientEffects()
{
	Requests.Reset();
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
	for (const auto& Target : TeammateTargets)
	{
		if (!Target.IsValid()) continue;
		Meshes.Reset();
		Target->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!IsEligibleMesh(Mesh)) continue;
			TrackMesh(Mesh);
			DesiredFlags.FindOrAdd(Mesh) |= OutlierStencil::Teammate;
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

void UOutlierStencilSubsystem::RefreshTeammateTargets()
{
	TeammateTargets.Reset();
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Viewer = It->Get();
		if (!IsValid(Viewer) || !Viewer->IsLocalController()) continue;
		const AOutlierPlayerState* Pair = Viewer->GetPlayerState<AOutlierPlayerState>();
		if (!Pair) continue;
		AActor* Other = Pair->IsShooterPlayer() ? static_cast<AActor*>(Pair->GetPartnerCharacter())
			: Pair->IsPartnerPlayer() ? static_cast<AActor*>(Pair->GetShooterCharacter()) : nullptr;
		if (IsValid(Other) && Other != Viewer->GetPawn() && Other->GetWorld() == World
			&& !Other->IsActorBeingDestroyed())
		{
			TeammateTargets.Add(Other);
		}
	}
}

void UOutlierStencilSubsystem::Tick(float DeltaTime)
{
	RefreshTeammateTargets();
	if (Requests.Num() || MeshStates.Num() || TeammateTargets.Num()) Reconcile();
}

TStatId UOutlierStencilSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOutlierStencilSubsystem, STATGROUP_Tickables);
}

void UOutlierStencilSubsystem::Deinitialize()
{
	for (const auto& Pair : MeshStates)
	{
		if (UMeshComponent* Mesh = Pair.Key.Get())
		{
			WriteMesh(Mesh, Pair.Value.bOriginalCustomDepth, Pair.Value.OriginalStencil, Pair.Value.OriginalWriteMask);
		}
	}
	Requests.Reset();
	TeammateTargets.Reset();
	MeshStates.Reset();
	Super::Deinitialize();
}
