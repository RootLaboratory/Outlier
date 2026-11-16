#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PostProcess/OutlierStencilSubsystem.h"
#include "TeammateOutlineRendering.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoTurretOutlineEligibilityTest,
	"Outlier.Enemy.Turret.OutlineEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutoTurretOutlineEligibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!GEngine) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TurretOutlineTest")));
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AAutoTurret* Turret = World->SpawnActor<AAutoTurret>();
	if (TestNotNull(TEXT("Native turret"), Turret))
	{
		Turret->TurretLifecycleState = EAutoTurretLifecycleState::Active;
		TestFalse(TEXT("Enemy turret is not a teammate"), Turret->IsEligibleForTeammateOutline());
		Turret->bHackedToPlayerTeam = true;
		TestTrue(TEXT("Alive hacked turret is eligible"), Turret->IsEligibleForTeammateOutline());

		// 수명 복제가 먼저 도착하고 해킹 복제가 나중에 와도 사망 표시를 다시 켜지 않는다.
		Turret->ReplicatedLifecycle.State = EAutoTurretLifecycleState::DeadPresentation;
		Turret->OnRep_TurretLifecycleState();
		Turret->OnRep_HackedTeam();
		TestFalse(TEXT("Late hacked replication cannot reenable death presentation"),
			Turret->IsEligibleForTeammateOutline());
		Turret->ReplicatedLifecycle.State = EAutoTurretLifecycleState::DeadPersistent;
		Turret->OnRep_TurretLifecycleState();
		TestFalse(TEXT("Restored corpse is ineligible"), Turret->IsEligibleForTeammateOutline());

		// 새 수명은 사망 제외를 영구 보관하지 않는다. 아군 상태가 다시 유효해지면 복구한다.
		Turret->TurretLifecycleState = EAutoTurretLifecycleState::Active;
		Turret->bHackedToPlayerTeam = false;
		TestFalse(TEXT("New enemy lifetime stays off"), Turret->IsEligibleForTeammateOutline());
		Turret->bHackedToPlayerTeam = true;
		TestTrue(TEXT("New alive friendly lifetime can recover"), Turret->IsEligibleForTeammateOutline());
		UOutlierAbilitySystemComponent* ASC = Turret->GetOutlierAbilitySystemComponent();
		if (TestNotNull(TEXT("Native turret ability system"), ASC))
		{
			ASC->AddLooseGameplayTag(OutlierGameplayTags::State::Dead());
			TestFalse(TEXT("Dead tag arriving before lifecycle is sufficient"), Turret->IsEligibleForTeammateOutline());
			ASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::Dead());
			TestTrue(TEXT("Removed dead tag permits valid next lifetime"), Turret->IsEligibleForTeammateOutline());
		}

		UOutlierStencilSubsystem* Stencils = World->GetSubsystem<UOutlierStencilSubsystem>();
		APlayerController* Viewer = World->SpawnActor<APlayerController>();
		if (TestNotNull(TEXT("Stencil subsystem"), Stencils) && TestNotNull(TEXT("Local viewer"), Viewer))
		{
			Viewer->Player = NewObject<ULocalPlayer>();
			TestTrue(TEXT("Viewer is local without a network driver"), Viewer->IsLocalController());
			USkeletalMeshComponent* Body = Turret->GetMesh();
			USkeletalMeshComponent* Head = Turret->TurretHeadMesh;
			Stencils->SetActorTeammate(Turret, true);
			Stencils->SetEffect(Turret, Viewer, EOutlierStencilEffect::Scan, 1);
			TestTrue(TEXT("Legacy team bit starts enabled"), (Body->CustomDepthStencilValue & OutlierStencil::Teammate) != 0);
			const TArray<TWeakObjectPtr<AActor>> Ready = {Turret};
			Stencils->SetIndependentTurretOutlineState(Ready, true);
			TestFalse(TEXT("Ready RDG removes only the legacy team bit"), (Body->CustomDepthStencilValue & OutlierStencil::Teammate) != 0);
			TestEqual(TEXT("Scan remains after RDG transition"), Body->CustomDepthStencilValue & OutlierStencil::ScanMask, 1);
			Stencils->SetIndependentTurretOutlineState({}, true);
			TestTrue(TEXT("No-exclusion RDG failure permits legacy fallback"), (Body->CustomDepthStencilValue & OutlierStencil::Teammate) != 0);
			Stencils->SetIndependentTurretOutlineState({}, false);
			TestFalse(TEXT("Effect OFF cannot revive legacy team output"), (Body->CustomDepthStencilValue & OutlierStencil::Teammate) != 0);
			TestEqual(TEXT("Effect OFF still preserves Scan"), Body->CustomDepthStencilValue & OutlierStencil::ScanMask, 1);

			// 렌더 LOD가 없는 자기 완결 에셋으로 슬롯 해석과 자원 실패 분기를 분리해서 검증한다.
			// 실제 해치 스키닝/섹션 경계는 렌더링 에셋을 사용하는 PIE 검증 대상이다.
			auto MakeSlotAsset = [Turret](FName Slot)
			{
				USkeletalMesh* Asset = NewObject<USkeletalMesh>(Turret);
				Asset->GetMaterials().AddDefaulted_GetRef().MaterialSlotName = Slot;
				return Asset;
			};
			Body->UnregisterComponent();
			Head->UnregisterComponent();
			Body->SetSkeletalMeshAsset(MakeSlotAsset(TEXT("BodyOnly")));
			Head->SetSkeletalMeshAsset(MakeSlotAsset(TEXT("Hatch")));
			FAutoTurretOutlineExclusion& Setting = Turret->TeammateOutlineExclusions.AddDefaulted_GetRef();
			Setting.MeshComponent.OverrideComponent = Body;
			Setting.MaterialSlotNames = {TEXT("Hatch")};
			TArray<FTeammateOutlineTargetData> Targets;
			AddExpectedMessage(TEXT("invalid mesh/slot entries ignored"), ELogVerbosity::Warning);
			Stencils->GatherTurretOutlineTargets(Viewer, Targets);
			if (TestEqual(TEXT("One live friendly target"), Targets.Num(), 1))
			{
				TestFalse(TEXT("A Head slot cannot resolve on the selected Body"), Targets[0].bHasExclusions);
				TestTrue(TEXT("The other component remains in full coverage"), Targets[0].Meshes.Contains(Head));
				++Turret->RoomWaveActivationSerial;
				TestFalse(TEXT("A cached target cannot cross into a new pooled lifetime"), Targets[0].IsEligible.Execute());
			}

			Body->SetSkeletalMeshAsset(MakeSlotAsset(TEXT("Hatch")));
			Setting.MaterialSlotNames = {TEXT("Hatch"), TEXT("Hatch"), TEXT("Missing")};
			AddExpectedMessage(TEXT("invalid mesh/slot entries ignored"), ELogVerbosity::Warning);
			AddExpectedMessage(TEXT("excluded-slot mask unavailable"), ELogVerbosity::Warning);
			Stencils->GatherTurretOutlineTargets(Viewer, Targets);
			if (TestEqual(TEXT("Valid entries survive invalid and duplicate names"), Targets.Num(), 1))
			{
				TestTrue(TEXT("A valid selected slot activates exclusion"), Targets[0].bHasExclusions);
				TestFalse(TEXT("Missing render resources are reported as unready"), Targets[0].bExclusionResourcesReady);
				TestTrue(TEXT("Failed proxy is not submitted"), Targets[0].ExcludedMeshes.IsEmpty());
			}
			Stencils->SetIndependentTurretOutlineState({}, true);
			TestFalse(TEXT("Valid exclusion failure cannot fall back to an unfiltered team outline"),
				(Body->CustomDepthStencilValue & OutlierStencil::Teammate) != 0);
			TestEqual(TEXT("Exclusion resource failure preserves Scan"), Body->CustomDepthStencilValue & OutlierStencil::ScanMask, 1);

			// 같은 실패 설정을 재해석해도 경고를 매번 반복하지 않고, 설정 제거 시 fallback을 회복한다.
			Stencils->GatherTurretOutlineTargets(Viewer, Targets);
			Turret->TeammateOutlineExclusions.Reset();
			Stencils->GatherTurretOutlineTargets(Viewer, Targets);
			Stencils->SetIndependentTurretOutlineState(Ready, true);
			Stencils->SetIndependentTurretOutlineState({}, true);
			TestTrue(TEXT("Removing exclusions restores no-exclusion fallback"),
				(Body->CustomDepthStencilValue & OutlierStencil::Teammate) != 0);
		}
	}
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

#endif
