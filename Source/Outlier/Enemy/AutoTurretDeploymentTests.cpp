#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

namespace
{
	struct FTurretDeploymentTestWorld
	{
		UWorld* World = nullptr;

		FTurretDeploymentTestWorld()
		{
			if (!GEngine) return;
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TurretDeploymentTest")),
				GetTransientPackage());
			if (!World) return;
			World->AddToRoot();
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FTurretDeploymentTestWorld()
		{
			if (!World) return;
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};

	UAnimInstance* MakeTestAnimInstance(USkeletalMeshComponent* Mesh, USkeleton* Skeleton)
	{
		// 렌더링 에셋 없이 몽타주 인스턴스의 재생 상태만 검증한다. 실제 Slot/본 자세는 PIE 대상이다.
		UAnimInstance* Instance = NewObject<UAnimInstance>(Mesh);
		Mesh->AnimScriptInstance = Instance;
		Instance->InitializeAnimation();
		Instance->CurrentSkeleton = Skeleton;
		return Instance;
	}

	UAnimMontage* MakeTestMontage(UObject* Outer, USkeleton* Skeleton)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(Outer);
		Montage->SetSkeleton(Skeleton);
		Montage->SlotAnimTracks.Reset();
		Montage->SlotAnimTracks.AddDefaulted_GetRef().SlotName = TEXT("DefaultSlot");
		Montage->CompositeSections.AddDefaulted_GetRef().SectionName = TEXT("Default");
		Montage->SetCompositeLength(1.0f);
		return Montage;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoTurretDeploymentPresentationTest,
	"Outlier.Enemy.Turret.DeploymentPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutoTurretDeploymentPresentationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTurretDeploymentTestWorld Fixture;
	if (!TestNotNull(TEXT("Turret deployment world"), Fixture.World)) return false;
	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Native turret"), Turret)) return false;

	USkeleton* Skeleton = NewObject<USkeleton>(Turret);
	UAnimInstance* Body = MakeTestAnimInstance(Turret->GetMesh(), Skeleton);
	UAnimInstance* Head = MakeTestAnimInstance(Turret->TurretHeadMesh, Skeleton);
	Turret->DeployMontage = MakeTestMontage(Turret, Skeleton);
	Turret->FireMontage = MakeTestMontage(Turret, Skeleton);
	UAnimMontage* Deploy = Turret->DeployMontage;
	const float FinalPosition = Deploy->GetPlayLength() - KINDA_SMALL_NUMBER;
	Turret->PrepareForRoomWaveActivation();
	TestEqual(TEXT("Waiting holds the closed frame"), Body->Montage_GetPosition(Deploy), 0.0f);
	TestFalse(TEXT("Waiting body montage is paused"), Body->Montage_IsPlaying(Deploy));
	TestTrue(TEXT("Authority starts the first activation"), Turret->BeginRoomWaveDeployment(0));
	const int32 FirstSerial = Turret->GetRoomWaveActivationSerial();
	FAnimMontageInstance* FirstInstance = Body->GetActiveInstanceForMontage(Deploy);
	if (!TestNotNull(TEXT("Deployment really creates a montage instance"), FirstInstance)) return false;
	TestTrue(TEXT("Deploying body montage plays"), Body->Montage_IsPlaying(Deploy));
	TestFalse(TEXT("Only the deployment instance disables auto blend-out"), FirstInstance->bEnableAutoBlendOut);
	TestTrue(TEXT("Shared montage asset remains unchanged"), Deploy->bEnableAutoBlendOut);
	Body->Montage_SetPosition(Deploy, 0.4f);
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	TestEqual(TEXT("Duplicate start does not restart playback"), Body->Montage_GetPosition(Deploy), 0.4f);

	Head->Montage_Play(Turret->FireMontage);
	// 완료가 blend-in 도중 도착해도 가중치가 덜 오른 자세를 고정하거나 이전 인스턴스를 남기지 않는다.
	Turret->CompleteTurretDeployment();
	TestTrue(TEXT("Completion stops the replaced blend-in instance"), FirstInstance->IsStopped());
	TestEqual(TEXT("Active body holds the deployed frame"), Body->Montage_GetPosition(Deploy), FinalPosition);
	TestFalse(TEXT("Active body montage is paused"), Body->Montage_IsPlaying(Deploy));
	FAnimMontageInstance* HeldInstance = Body->GetActiveInstanceForMontage(Deploy);
	if (!TestNotNull(TEXT("Active pose has a live montage instance"), HeldInstance)) return false;
	TestEqual(TEXT("Held pose has full weight immediately"), HeldInstance->GetWeight(), 1.0f);
	int32 ActiveDeploymentCount = 0;
	for (const FAnimMontageInstance* Instance : Body->MontageInstances)
	{
		if (Instance && Instance->Montage == Deploy && !Instance->IsStopped())
		{
			++ActiveDeploymentCount;
		}
	}
	TestEqual(TEXT("Completion leaves exactly one active deployment instance"), ActiveDeploymentCount, 1);
	TestTrue(TEXT("Body completion does not pause head fire"), Head->Montage_IsPlaying(Turret->FireMontage));
	TestFalse(TEXT("Body skeletal animation remains enabled"), Turret->GetMesh()->bPauseAnims);
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	Turret->OnRep_TurretLifecycleState();
	TestTrue(TEXT("Duplicate Active and late start keep the same held instance"),
		Body->GetActiveInstanceForMontage(Deploy) == HeldInstance);

	// 관련성 복귀에서 전개 RPC를 못 받았거나 몽타주가 사라져도 Active 스냅샷만으로 복원한다.
	Body->Montage_Stop(0.0f, Deploy);
	Turret->PresentedDeploymentGeneration = INDEX_NONE;
	Turret->PresentedDeploymentSerial = 0;
	Turret->bDeploymentCompletionPresented = false;
	Turret->OnRep_TurretLifecycleState();
	TestEqual(TEXT("Active snapshot restores directly to the deployed frame"),
		Body->Montage_GetPosition(Deploy), FinalPosition);
	TestFalse(TEXT("Restored Active never starts deployment playback"), Body->Montage_IsPlaying(Deploy));

	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPresentation);
	TestEqual(TEXT("Death presentation keeps the deployed body"), Body->Montage_GetPosition(Deploy), FinalPosition);
	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPersistent);
	TestEqual(TEXT("Persistent death keeps the deployed body"), Body->Montage_GetPosition(Deploy), FinalPosition);
	Turret->PrepareForRoomWaveActivation();
	TestEqual(TEXT("Explicit Waiting closes the body"), Body->Montage_GetPosition(Deploy), 0.0f);
	TestFalse(TEXT("Waiting cancels head fire"), Head->Montage_IsPlaying(Turret->FireMontage));
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	TestFalse(TEXT("Old start cannot play while Waiting"), Body->Montage_IsPlaying(Deploy));
	TestTrue(TEXT("A reused turret starts a new activation"), Turret->BeginRoomWaveDeployment(0));
	TestNotEqual(TEXT("Reuse advances activation serial"), Turret->GetRoomWaveActivationSerial(), FirstSerial);
	Body->Montage_SetPosition(Deploy, 0.3f);
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	Turret->NotifyDeploySequenceFinished(0, FirstSerial);
	TestTrue(TEXT("Old completion cannot activate the reused turret"), Turret->IsDeploying());
	TestEqual(TEXT("Old start cannot rewind the reused deployment"), Body->Montage_GetPosition(Deploy), 0.3f);

	// 속성보다 먼저 온 RPC는 보류하고, 이후 Deploying 스냅샷으로 한 번만 재생한다.
	Turret->PrepareForRoomWaveActivation();
	const int32 NextSerial = Turret->GetRoomWaveActivationSerial() + 1;
	Turret->MulticastBeginTurretDeployment_Implementation(1, NextSerial);
	TestFalse(TEXT("RPC before its snapshot cannot start the old activation"), Body->Montage_IsPlaying(Deploy));
	Turret->ReplicatedLifecycle.State = EAutoTurretLifecycleState::Deploying;
	Turret->ReplicatedLifecycle.GameplayGeneration = 1;
	Turret->ReplicatedLifecycle.ActivationSerial = NextSerial;
	Turret->OnRep_TurretLifecycleState();
	TestTrue(TEXT("Deploying snapshot recovers an early or missed RPC"), Body->Montage_IsPlaying(Deploy));
	TestEqual(TEXT("Snapshot restores the activation generation"), Turret->GetRoomWaveGameplayGeneration(), 1);
	Body->Montage_SetPosition(Deploy, 0.6f);
	Turret->OnRep_TurretLifecycleState();
	TestEqual(TEXT("Duplicate Deploying snapshot does not rewind"), Body->Montage_GetPosition(Deploy), 0.6f);
	return true;
}

#endif
