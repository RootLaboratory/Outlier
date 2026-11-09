#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterAnimInstance.h"
#include "Weapon/MeleeWeaponBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Camera/PlayerCameraManager.h"
#include "Shooter/Anim/AnimNotifyState_MeleeTrace.h"
#include "Shooter/Anim/AnimNotify_MeleeRecoveryEnd.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Enemy/EnemyBase.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/OutputDevice.h"
#include "HAL/IConsoleManager.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "UObject/UnrealType.h"

namespace
{
struct FMeleeDedicatedWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	AShooterCharacter* Shooter = nullptr;
	AMeleeWeaponBase* Hammer = nullptr;
	APlayerController* Controller = nullptr;
	float RequestedPitch = 0;
	bool bUsePawnViewForCamera = false;

	bool Initialize(FAutomationTestBase& Test, bool bSuit, float Pitch, bool bDedicated = true)
	{
		if (!GEngine) return false;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("MeleeDedicated")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Dedicated melee world"), World)) return false;
		World->AddToRoot();
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.RunAsDedicated = bDedicated;
		Context.SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		URL.Port = 0;
		if (bDedicated) World->Listen(URL);
		World->BeginPlay();
		if (!Test.TestTrue(TEXT("Requested authoritative melee world mode"), bDedicated
			? World->GetNetMode() == NM_DedicatedServer && World->GetNetDriver()
			: World->GetNetMode() == NM_Standalone && !World->GetNetDriver())) return false;

		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* HammerClass = LoadClass<AMeleeWeaponBase>(nullptr,
			TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
		if (!Test.TestNotNull(TEXT("Production Shooter BP"), ShooterClass)
			|| !Test.TestNotNull(TEXT("Production Hammer BP"), HammerClass)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
		Hammer = World->SpawnActor<AMeleeWeaponBase>(HammerClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Production Shooter"), Shooter)
			|| !Test.TestNotNull(TEXT("Production Hammer"), Hammer)
			|| !Test.TestNotNull(TEXT("Server player controller"), Controller)) return false;
		Controller->Possess(Shooter);
		if (!bDedicated)
		{
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			Controller->SetAsLocalPlayerController();
			Controller->SetViewTarget(Shooter);
		}
		Controller->SetControlRotation(FRotator(Pitch, 0, 0));
		RequestedPitch = Pitch;
		if (!Test.TestNotNull(TEXT("Server camera manager"), Controller->PlayerCameraManager.Get())) return false;
		Controller->PlayerCameraManager->bUseClientSideCameraUpdates = true;
		Shooter->GetCharacterMovement()->DisableMovement();
		if (!Test.TestEqual(TEXT("Requested melee owner locality"), Shooter->IsLocallyControlled(), !bDedicated)) return false;
		if (!Test.TestTrue(TEXT("Requested production presentation applies"), Shooter->SetSuitPresentation(bSuit))) return false;
		Shooter->AFirstPersonCharacter::EquipWeapon(Hammer);
		Shooter->RefreshWeaponMode();
		for (int32 Frame = 0; Frame < 30; ++Frame) Tick();
		Test.TestTrue(TEXT("Actual server aim uses the requested pitch"),
			FMath::IsNearlyEqual(Shooter->GetAimPitchForAnimation(), Pitch, 0.01f));
		LogAim(Test, TEXT("Initialized"));
		Test.AddInfo(FString::Printf(TEXT("[MeleeDedicated] Suit=%d Pitch=%.1f Mesh=%s Anim=%s Montage=%s"),
			bSuit, Pitch, *GetPathNameSafe(Shooter->GetMesh()->GetSkeletalMeshAsset()),
			*GetPathNameSafe(Shooter->GetMesh()->GetAnimInstance()), *GetPathNameSafe(Shooter->GetThirdPersonMeleeAttackMontage())));
		return Test.TestNotNull(TEXT("Production TP animation instance"), Shooter->GetMesh()->GetAnimInstance())
			&& Test.TestNotNull(TEXT("Production TP melee montage"), Shooter->GetThirdPersonMeleeAttackMontage());
	}

	~FMeleeDedicatedWorld()
	{
		if (!World) return;
		if (Controller) Controller->Player = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (Instance) Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	void Tick() const
	{
		++GFrameCounter;
		// 실제 클라이언트 대신 원격 입력과 압축 POV를 공급한다. 엔진의 서버 수신 경로가
		// 카메라 캐시를 갱신하게 하며, 소켓이나 애니메이션 포즈는 월드 틱만으로 평가한다.
		Controller->SetControlRotation(FRotator(RequestedPitch, 0, 0));
		const uint32 PackedPitchYaw = FRotator::CompressAxisToShort(RequestedPitch);
		Controller->ServerUpdateCamera_Implementation(
			bUsePawnViewForCamera ? Shooter->GetPawnViewLocation() : Shooter->GetActorLocation(),
			static_cast<int32>(PackedPitchYaw));
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	}

	void LogAim(FAutomationTestBase& Test, const TCHAR* Stage) const
	{
		Test.AddInfo(FString::Printf(TEXT("[MeleeDedicatedAim] %s Requested=%.1f Control=%s Camera=%s BaseAim=%s ViewTarget=%s"),
			Stage, RequestedPitch, *Controller->GetControlRotation().ToString(),
			*Controller->PlayerCameraManager->GetCameraRotation().ToString(),
			*Shooter->GetBaseAimRotation().ToString(), *GetPathNameSafe(Controller->GetViewTarget())));
	}

	bool RunSwing(FAutomationTestBase& Test, FVector& OutContact)
	{
		UAnimInstance* Anim = Shooter->GetMesh()->GetAnimInstance();
		UAnimMontage* Montage = Shooter->GetThirdPersonMeleeAttackMontage();
		USkeletalMeshComponent* WeaponMesh = Hammer->GetThirdPersonWeaponMesh();
		if (!Test.TestTrue(TEXT("Authored hammer trace start socket"), WeaponMesh->DoesSocketExist(TEXT("MeleeTraceStart")))
			|| !Test.TestTrue(TEXT("Authored hammer trace end socket"), WeaponMesh->DoesSocketExist(TEXT("MeleeTraceEnd")))) return false;
		const int32 PreviousSequence = Hammer->GetAttackSequence();
		Hammer->StartAttack();
		Hammer->ReleaseAttack();
		if (!Test.TestTrue(TEXT("Real attack starts its TP montage"), Anim->Montage_IsPlaying(Montage))) return false;
		Test.TestTrue(TEXT("Real attack advances its sequence"), Hammer->GetAttackSequence() != PreviousSequence);
		Test.TestTrue(TEXT("Production attack enables server bone refresh"), Shooter->GetMesh()->VisibilityBasedAnimTickOption
			== EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
		bool bObservedRecovery = false;
		const int32 MaxFrames = FMath::CeilToInt((Montage->GetPlayLength() + 2.0f) * 60);
		for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
		{
			Tick();
			if (Hammer->GetAttackPhase() == EMeleeAttackPhase::Recovery && !bObservedRecovery)
			{
				bObservedRecovery = true;
				Test.TestTrue(TEXT("Attacking server aim retains the requested pitch"),
					FMath::IsNearlyEqual(Shooter->GetAimPitchForAnimation(), RequestedPitch, 0.01f));
				LogAim(Test, TEXT("Recovery"));
				const FVector Start = WeaponMesh->GetSocketLocation(TEXT("MeleeTraceStart"));
				const FVector End = WeaponMesh->GetSocketLocation(TEXT("MeleeTraceEnd"));
				OutContact = (Start + End) * 0.5f;
				const USceneComponent* Parent = WeaponMesh->GetAttachParent();
				if (Test.TestNotNull(TEXT("Hammer remains attached to production owner"), Parent))
				{
					const FTransform Expected = WeaponMesh->GetRelativeTransform() * Parent->GetSocketTransform(WeaponMesh->GetAttachSocketName());
					Test.TestTrue(TEXT("Hammer attachment follows the evaluated hand"), WeaponMesh->GetComponentTransform().Equals(Expected, 0.01f));
				}
				if (const UShooterAnimInstance* ShooterAnim = Cast<UShooterAnimInstance>(Anim))
				{
					Test.TestTrue(TEXT("Actual attacking AnimInstance retains melee AO"), ShooterAnim->GetMeleeAimOffsetAlpha() > 0.5f);
				}
				Test.AddInfo(FString::Printf(TEXT("[MeleeDedicated] Sequence=%d RecoveryFrame=%d Start=%s End=%s Pitch=%.1f"),
					Hammer->GetAttackSequence(), Frame, *Start.ToString(), *End.ToString(), Shooter->GetAimPitchForAnimation()));
			}
			if (Hammer->GetAttackPhase() == EMeleeAttackPhase::Idle) break;
		}
		Test.TestTrue(TEXT("Actual montage Notify commits the attack to Recovery"), bObservedRecovery);
		Test.TestEqual(TEXT("Released montage attack finishes in Idle"), Hammer->GetAttackPhase(), EMeleeAttackPhase::Idle);
		Test.TestTrue(TEXT("Completion preserves server locomotion notifies"), Shooter->GetMesh()->VisibilityBasedAnimTickOption
			== EVisibilityBasedAnimTickOption::AlwaysTickPose);
		return bObservedRecovery;
	}

	AEnemyBase* SpawnDamageTarget(FAutomationTestBase& Test, const FVector& Location) const
	{
		// 호출자가 지정한 월드 위치에 표적을 고정한다. 소켓/피해 범위를 늘리거나 Notify를 직접 호출하지 않는다.
		UDataTable* Stats = NewObject<UDataTable>(World);
		Stats->RowStruct = FEnemyStat::StaticStruct();
		FEnemyStat Stat;
		Stat.Health = 1000;
		Stats->AddRow(TEXT("MeleeTarget"), Stat);
		FStructProperty* StatProperty = FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("EnemyStatRow"));
		FBoolProperty* CoreProperty = FindFProperty<FBoolProperty>(AEnemyBase::StaticClass(), TEXT("bUseCoreWeakPoint"));
		if (!Test.TestNotNull(TEXT("Target stat property"), StatProperty)
			|| !Test.TestNotNull(TEXT("Target weak-point configuration"), CoreProperty)) return nullptr;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.bDeferConstruction = true;
		const FTransform Transform(Location);
		AEnemyBase* Target = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), Transform, Spawn);
		if (!Test.TestNotNull(TEXT("Actual GAS damage target"), Target)) return nullptr;
		Target->AutoPossessAI = EAutoPossessAI::Disabled;
		FDataTableRowHandle* Handle = StatProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Target);
		Handle->DataTable = Stats;
		Handle->RowName = TEXT("MeleeTarget");
		// 메시 없는 표적의 원점 코어가 캡슐보다 먼저 맞아 배율 2가 적용되지 않게, 이 표적만 몸통 판정으로 둔다.
		CoreProperty->SetPropertyValue_InContainer(Target, false);
		Target->FinishSpawning(Transform);
		Target->GetCharacterMovement()->DisableMovement();
		Target->GetCapsuleComponent()->SetCapsuleSize(10, 20);
		Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
		Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
		Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Target->EnterCombat(Shooter->GetActorLocation());
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		Test.TestFalse(TEXT("Configured target starts alive"), Target->IsDead());
		Test.TestEqual(TEXT("Combat target uses normal melee damage policy"), Target->GetCombatState(), EEnemyCombatState::Combat);
		return Target;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeDedicatedLifecycleTest,
	"Outlier.Weapon.Melee.DedicatedLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeDedicatedLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	for (bool bSuit : { false, true })
	{
		FVector PreviousContact = FVector::ZeroVector;
		bool bHasPreviousContact = false;
		for (float Pitch : { -45.0f, 0.0f, 45.0f })
		{
			FMeleeDedicatedWorld Fixture;
			if (!Fixture.Initialize(*this, bSuit, Pitch)) return false;
			FVector Contact = FVector::ZeroVector;
			if (!Fixture.RunSwing(*this, Contact)) continue;
			if (bHasPreviousContact)
			{
				TestFalse(TEXT("Different aim pitch changes the evaluated hammer contact"), Contact.Equals(PreviousContact, KINDA_SMALL_NUMBER));
			}
			PreviousContact = Contact;
			bHasPreviousContact = true;
			AEnemyBase* Target = Fixture.SpawnDamageTarget(*this, Contact);
			if (!Target) return false;
			const float HealthBefore = Target->GetCurrentHealth();
			FVector SecondContact = FVector::ZeroVector;
			Fixture.RunSwing(*this, SecondContact);
			TestEqual(TEXT("Real montage applies exactly one production hammer hit"), HealthBefore - Target->GetCurrentHealth(), 50.0f);
			AddInfo(FString::Printf(TEXT("[MeleeDedicated] Suit=%d Pitch=%.1f Target=%s HPBefore=%.1f HPAfter=%.1f"),
				bSuit, Pitch, *Contact.ToString(), HealthBefore, Target->GetCurrentHealth()));
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeDedicatedNotifyLifetimeTest,
	"Outlier.Weapon.Melee.DedicatedNotifyLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeDedicatedNotifyLifetimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FMeleeDedicatedWorld Fixture;
	if (!Fixture.Initialize(*this, true, 0)) return false;
	AMeleeWeaponBase* Hammer = Fixture.Hammer;
	USkeletalMeshComponent* Mesh = Fixture.Shooter->GetMesh();
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	UAnimMontage* Montage = Fixture.Shooter->GetThirdPersonMeleeAttackMontage();
	const FAnimNotifyEvent* TraceEvent = nullptr;
	const FAnimNotifyEvent* RecoveryEvent = nullptr;
	UAnimSequenceBase* TraceSource = nullptr;
	UAnimSequenceBase* RecoverySource = nullptr;
	auto FindEvents = [&](UAnimSequenceBase* Source)
	{
		if (!Source) return;
		for (const FAnimNotifyEvent& Event : Source->Notifies)
		{
			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAnimNotifyState_MeleeTrace>())
			{
				TraceEvent = &Event;
				TraceSource = Source;
			}
			if (Event.Notify && Event.Notify->IsA<UAnimNotify_MeleeRecoveryEnd>())
			{
				RecoveryEvent = &Event;
				RecoverySource = Source;
			}
		}
	};
	FindEvents(Montage);
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments) FindEvents(Segment.GetAnimReference());
	}
	if (!TestNotNull(TEXT("Production montage/sequence has MeleeTrace NotifyState"), TraceEvent)
		|| !TestNotNull(TEXT("Production montage/sequence has MeleeRecoveryEnd Notify"), RecoveryEvent))
	{
		AddError(FString::Printf(TEXT("Configure melee Trace/RecoveryEnd Notifies and Dedicated Server execution on %s or its source sequences."), *Montage->GetPathName()));
		return false;
	}

	Hammer->StartAttack();
	Hammer->ReleaseAttack();
	FAnimMontageInstance* FirstInstance = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Original production montage instance"), FirstInstance)) return false;
	const int32 FirstInstanceId = FirstInstance->GetInstanceID();
	for (int32 Frame = 0; Frame < 180 && !Hammer->bMeleeTraceActive; ++Frame) Fixture.Tick();
	if (!TestTrue(TEXT("Original montage naturally begins its trace"), Hammer->bMeleeTraceActive)) return false;
	const int32 FirstSequence = Hammer->GetAttackSequence();
	UAnimNotifyState* TraceNotify = TraceEvent->NotifyStateClass;
	FAnimNotifyEventReference FirstTrace(TraceEvent, TraceSource);
	FirstTrace.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(FirstInstanceId);
	FAnimNotifyEventReference MissingContext(TraceEvent, TraceSource);
	FAnimNotifyEventReference WrongContext(TraceEvent, TraceSource);
	WrongContext.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(INDEX_NONE);
	TestTrue(TEXT("Current TP montage context is accepted"), Hammer->IsCurrentAttackNotify(Mesh, FirstTrace));
	TestFalse(TEXT("FP mesh cannot authorize server melee Notifies"), Hammer->IsCurrentAttackNotify(Fixture.Shooter->GetFirstPersonMesh(), FirstTrace));
	TestFalse(TEXT("Missing montage context is rejected"), Hammer->IsCurrentAttackNotify(Mesh, MissingContext));
	TestFalse(TEXT("Wrong montage context is rejected"), Hammer->IsCurrentAttackNotify(Mesh, WrongContext));
	// 동일한 Notify 객체도 FP 메시나 잘못된 재생 인스턴스로 전달되면 현재 TP 판정을 닫지 못한다.
	TraceNotify->NotifyEnd(Fixture.Shooter->GetFirstPersonMesh(), TraceSource, FirstTrace);
	TraceNotify->NotifyEnd(Mesh, TraceSource, MissingContext);
	TraceNotify->NotifyEnd(Mesh, TraceSource, WrongContext);
	TestTrue(TEXT("Rejected mesh/context End preserves the active trace"), Hammer->bMeleeTraceActive);
	FBranchingPointNotifyPayload BranchTrace;
	BranchTrace.SkelMeshComponent = Mesh;
	BranchTrace.SequenceAsset = TraceSource;
	BranchTrace.NotifyEvent = const_cast<FAnimNotifyEvent*>(TraceEvent);
	BranchTrace.MontageInstanceID = FirstInstanceId;
	TraceNotify->BranchingPointNotifyTick(BranchTrace, 1.0f / 60.0f);
	TestTrue(TEXT("Valid branching-point Tick preserves the current trace"), Hammer->bMeleeTraceActive);

	// 실제 몽타주를 중단하고 재공격한다. 아래 주입은 이미 큐에 들어간 이전 Notify의 지연 도착만 재현한다.
	Hammer->StopAttack();
	Anim->Montage_Stop(0.1f, Montage);
	Hammer->StartAttack();
	Hammer->ReleaseAttack();
	FAnimMontageInstance* NextInstance = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Restart creates a production montage instance"), NextInstance)) return false;
	TestTrue(TEXT("Restart advances montage instance identity"), NextInstance->GetInstanceID() != FirstInstanceId);
	TestTrue(TEXT("Restart advances weapon attack identity"), Hammer->GetAttackSequence() != FirstSequence);
	FAnimNotifyEventReference OldTrace(TraceEvent, TraceSource);
	OldTrace.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(FirstInstanceId);
	FAnimNotifyEventReference OldRecovery(RecoveryEvent, RecoverySource);
	OldRecovery.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(FirstInstanceId);
	TraceNotify->NotifyBegin(Mesh, TraceSource, TraceEvent->GetDuration(), OldTrace);
	TraceNotify->NotifyTick(Mesh, TraceSource, 1.0f / 60.0f, OldTrace);
	TraceNotify->BranchingPointNotifyBegin(BranchTrace);
	TraceNotify->BranchingPointNotifyTick(BranchTrace, 1.0f / 60.0f);
	TestEqual(TEXT("Old Begin/Tick cannot commit the new preparation"), Hammer->GetAttackPhase(), EMeleeAttackPhase::Attack);
	for (int32 Frame = 0; Frame < 180 && !Hammer->bMeleeTraceActive; ++Frame) Fixture.Tick();
	if (!TestTrue(TEXT("Restarted montage naturally begins its own trace"), Hammer->bMeleeTraceActive)) return false;
	TraceNotify->NotifyEnd(Mesh, TraceSource, OldTrace);
	BranchTrace.bReachedEnd = true;
	TraceNotify->BranchingPointNotifyEnd(BranchTrace);
	TestTrue(TEXT("Old End cannot close the new trace"), Hammer->bMeleeTraceActive);
	RecoveryEvent->Notify->Notify(Mesh, RecoverySource, OldRecovery);
	FBranchingPointNotifyPayload BranchRecovery;
	BranchRecovery.SkelMeshComponent = Mesh;
	BranchRecovery.SequenceAsset = RecoverySource;
	BranchRecovery.NotifyEvent = const_cast<FAnimNotifyEvent*>(RecoveryEvent);
	BranchRecovery.MontageInstanceID = FirstInstanceId;
	RecoveryEvent->Notify->BranchingPointNotify(BranchRecovery);
	TestEqual(TEXT("Old RecoveryEnd cannot finish the new swing"), Hammer->GetAttackPhase(), EMeleeAttackPhase::Recovery);
	for (int32 Frame = 0; Frame < 180 && Hammer->GetAttackPhase() != EMeleeAttackPhase::Idle; ++Frame) Fixture.Tick();
	TestEqual(TEXT("Current montage completes normally after rejecting stale events"), Hammer->GetAttackPhase(), EMeleeAttackPhase::Idle);
	TestFalse(TEXT("Completion closes the current trace"), Hammer->bMeleeTraceActive);

	// 별도 실제 재생에서 유효한 BranchingPoint 종료 경로도 확인한다. 위 자연 종료 검증과 분리한다.
	Hammer->StartAttack();
	Hammer->ReleaseAttack();
	FAnimMontageInstance* BranchInstance = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Branching-point verification montage instance"), BranchInstance)) return false;
	BranchTrace.MontageInstanceID = BranchInstance->GetInstanceID();
	BranchRecovery.MontageInstanceID = BranchInstance->GetInstanceID();
	for (int32 Frame = 0; Frame < 180 && !Hammer->bMeleeTraceActive; ++Frame) Fixture.Tick();
	if (!TestTrue(TEXT("Branching-point verification naturally begins its trace"), Hammer->bMeleeTraceActive)) return false;
	TraceNotify->BranchingPointNotifyEnd(BranchTrace);
	TestFalse(TEXT("Valid branching-point End closes its trace"), Hammer->bMeleeTraceActive);
	RecoveryEvent->Notify->BranchingPointNotify(BranchRecovery);
	TestEqual(TEXT("Valid branching-point RecoveryEnd completes its swing"), Hammer->GetAttackPhase(), EMeleeAttackPhase::Idle);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeDedicatedAimCoverageTest,
	"Outlier.Weapon.Melee.DedicatedAimCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeDedicatedAimCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	for (bool bSuit : { false, true })
	{
		FMeleeDedicatedWorld Fixture;
		if (!Fixture.Initialize(*this, bSuit, 0)) return false;
		Fixture.bUsePawnViewForCamera = true;
		Fixture.Tick();
		const FVector Eye = Fixture.Shooter->GetPawnViewLocation();
		const FRotationMatrix Aim(Fixture.Shooter->GetBaseAimRotation());
		const FVector Forward = Aim.GetUnitAxis(EAxis::X);
		const FVector Right = Aim.GetUnitAxis(EAxis::Y);
		const FVector Up = Aim.GetUnitAxis(EAxis::Z);
		UAnimMontage* Montage = Fixture.Shooter->GetThirdPersonMeleeAttackMontage();
		USkeletalMeshComponent* Mesh = Fixture.Hammer->GetThirdPersonWeaponMesh();
		int32 LeftHits = 0;
		int32 CenterHits = 0;
		int32 RightHits = 0;
		int32 Cases = 0;
		AddInfo(FString::Printf(TEXT("[MeleeCoverageReference] Suit=%d Eye=%s Camera=%s Forward=%s Right=%s"),
			bSuit, *Eye.ToString(), *Fixture.Controller->PlayerCameraManager->GetCameraLocation().ToString(),
			*Forward.ToString(), *Right.ToString()));

		// 조준선에서 독립적으로 정한 고정 격자다. 소켓을 보고 표적을 옮기지 않으며,
		// 단일 대상 제한이 좌/중/우 비교를 가리지 않도록 매 공격에 표적 하나만 둔다.
		for (bool bBodySize : { false, true })
		for (float Distance : { 30.0f, 60.0f, 90.0f, 120.0f, 150.0f, 180.0f })
		for (float Height : { 0.0f, -30.0f, -60.0f })
		for (float Lateral : { -60.0f, -30.0f, 0.0f, 30.0f, 60.0f })
		{
			const FVector Location = Eye + Forward * Distance + Right * Lateral + Up * Height;
			AEnemyBase* Target = Fixture.SpawnDamageTarget(*this, Location);
			if (!Target) return false;
			if (bBodySize)
			{
				Target->GetCapsuleComponent()->SetCapsuleSize(34, 96);
				Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
				Fixture.World->GetPhysicsScene()->Flush();
			}
			const float HealthBefore = Target->GetCurrentHealth();
			Fixture.Hammer->StartAttack();
			Fixture.Hammer->ReleaseAttack();
			bool bSawRecovery = false;
			const int32 MaxFrames = FMath::CeilToInt((Montage->GetPlayLength() + 2) * 60);
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				Fixture.Tick();
				if (Fixture.Hammer->GetAttackPhase() == EMeleeAttackPhase::Recovery)
				{
					bSawRecovery = true;
					// 각 Suit의 첫 공격만 회복 구간 전체를 기록한다. 활성 판정의 확정 근거는 아래 실제 HP 변화다.
					if (Cases == 0)
					{
						const FVector Start = Mesh->GetSocketLocation(TEXT("MeleeTraceStart")) - Eye;
						const FVector End = Mesh->GetSocketLocation(TEXT("MeleeTraceEnd")) - Eye;
						AddInfo(FString::Printf(TEXT("[MeleeCoveragePose] Suit=%d Frame=%d Start=%s End=%s"),
							bSuit, Frame, *Start.ToString(), *End.ToString()));
					}
				}
				if (Fixture.Hammer->GetAttackPhase() == EMeleeAttackPhase::Idle) break;
			}
			TestTrue(TEXT("Coverage swing uses real montage Notify"), bSawRecovery);
			TestEqual(TEXT("Coverage swing completes"), Fixture.Hammer->GetAttackPhase(), EMeleeAttackPhase::Idle);
			const float Damage = HealthBefore - Target->GetCurrentHealth();
			TestTrue(TEXT("Coverage cell takes either zero or one normal hit"),
				FMath::IsNearlyZero(Damage) || FMath::IsNearlyEqual(Damage, 50.0f));
			if (Damage > 0)
			{
				if (Lateral < 0) ++LeftHits;
				else if (Lateral > 0) ++RightHits;
				else ++CenterHits;
			}
			++Cases;
			AddInfo(FString::Printf(TEXT("[MeleeCoverage] Suit=%d Body=%d Distance=%.0f Lateral=%.0f Height=%.0f Damage=%.0f Target=%s"),
				bSuit, bBodySize, Distance, Lateral, Height, Damage, *Location.ToString()));
			Target->Destroy();
			// 다음 공격의 시작 포즈를 동일하게 비교한다. 이전 몽타주의 블렌드 아웃만 정상 틱으로 끝낸다.
			for (int32 Frame = 0; Frame < 30; ++Frame) Fixture.Tick();
		}
		AddInfo(FString::Printf(TEXT("[MeleeCoverageSummary] Suit=%d Cases=%d LeftHits=%d CenterHits=%d RightHits=%d"),
			bSuit, Cases, LeftHits, CenterHits, RightHits));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeTPModeComparisonTest,
	"Outlier.Weapon.Melee.TPModeComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeTPModeComparisonTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	IConsoleVariable* PoseDiagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Weapon.MeleePoseDiagnostics"));
	const int32 PreviousDiagnostics = PoseDiagnostics ? PoseDiagnostics->GetInt() : 0;
	if (PoseDiagnostics) PoseDiagnostics->Set(1, ECVF_SetByCode);
	ON_SCOPE_EXIT { if (PoseDiagnostics) PoseDiagnostics->Set(PreviousDiagnostics, ECVF_SetByCode); };
	for (bool bSuit : { false, true })
	{
		TArray<FVector> DedicatedStarts;
		TArray<FVector> DedicatedEnds;
		for (bool bDedicated : { true, false })
		{
			FMeleeDedicatedWorld Fixture;
			if (!Fixture.Initialize(*this, bSuit, 0, bDedicated)) return false;
			Fixture.bUsePawnViewForCamera = true;
			Fixture.Tick();
			USkeletalMeshComponent* Mesh = Fixture.Hammer->GetThirdPersonWeaponMesh();
			UAnimMontage* Montage = Fixture.Shooter->GetThirdPersonMeleeAttackMontage();
			UE_LOG(LogTemp, Log, TEXT("[MeleeTPModeBegin] Suit=%d Dedicated=%d OwnerLocal=%d NetMode=%d Montage=%s"),
				bSuit, bDedicated, Fixture.Shooter->IsLocallyControlled(), static_cast<int32>(Fixture.World->GetNetMode()), *GetPathNameSafe(Montage));
			Fixture.Hammer->StartAttack();
			Fixture.Hammer->ReleaseAttack();
			float MaxSocketDifference = 0;
			int32 Frames = 0;
			const int32 MaxFrames = FMath::CeilToInt((Montage->GetPlayLength() + 2) * 60);
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				Fixture.Tick();
				const FVector Eye = Fixture.Shooter->GetPawnViewLocation();
				const FVector Start = Mesh->GetSocketLocation(TEXT("MeleeTraceStart")) - Eye;
				const FVector End = Mesh->GetSocketLocation(TEXT("MeleeTraceEnd")) - Eye;
				if (bDedicated)
				{
					DedicatedStarts.Add(Start);
					DedicatedEnds.Add(End);
				}
				else if (DedicatedStarts.IsValidIndex(Frame))
				{
					MaxSocketDifference = FMath::Max(MaxSocketDifference,
						FMath::Max(FVector::Distance(Start, DedicatedStarts[Frame]), FVector::Distance(End, DedicatedEnds[Frame])));
				}
				++Frames;
				// 같은 시각의 TP 포즈 비교와 실제 Notify 내부 Sweep 로그를 함께 남긴다. 포즈를 강제로 평가하지 않는다.
				UE_LOG(LogTemp, Log, TEXT("[MeleeTPModeFrame] Suit=%d Dedicated=%d Frame=%d Time=%.6f Phase=%d Start=%s End=%s"),
					bSuit, bDedicated, Frame, (Frame + 1) / 60.0f, static_cast<int32>(Fixture.Hammer->GetAttackPhase()), *Start.ToString(), *End.ToString());
				if (Fixture.Hammer->GetAttackPhase() == EMeleeAttackPhase::Idle) break;
			}
			TestEqual(TEXT("TP comparison swing completes"), Fixture.Hammer->GetAttackPhase(), EMeleeAttackPhase::Idle);
			if (!bDedicated)
			{
				TestEqual(TEXT("Both world modes complete at the same attack frame"), Frames, DedicatedStarts.Num());
				AddInfo(FString::Printf(TEXT("[MeleeTPModeComparison] Suit=%d Frames=%d MaxSocketDifferenceCm=%.6f"), bSuit, Frames, MaxSocketDifference));
			}
			UE_LOG(LogTemp, Log, TEXT("[MeleeTPModeEnd] Suit=%d Dedicated=%d Frames=%d"), bSuit, bDedicated, Frames);
		}
	}
	return !HasAnyErrors();
}

namespace
{
// 실제 Sweep 로그를 관찰한다. 테스트가 Notify나 무기 내부 Trace 상태를 직접 조작하지 않는다.
struct FMeleeSweepObservation : FOutputDevice
{
	FString WeaponFilter;
	TSet<uint64> SweepFrames;
	TArray<FVector> Starts;
	TArray<FVector> Ends;
	int32 TraceBegins = 0;
	int32 TraceEnds = 0;

	explicit FMeleeSweepObservation(const AMeleeWeaponBase* Weapon)
		: WeaponFilter(FString::Printf(TEXT("Weapon=%s "), *Weapon->GetName()))
	{
		GLog->AddOutputDevice(this);
	}
	~FMeleeSweepObservation() { GLog->RemoveOutputDevice(this); }

	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (!IsInGameThread() || Category != TEXT("LogMeleeDiagnostics")) return;
		const FString Line(Message);
		if (!Line.Contains(WeaponFilter)) return;
		if (Line.Contains(TEXT("[TraceBegin]"))) ++TraceBegins;
		if (Line.Contains(TEXT("[TraceEnd]"))) ++TraceEnds;
		if (!Line.Contains(TEXT("[SweepPath]"))) return;
		const int32 StartIndex = Line.Find(TEXT("CurrentStart="));
		const int32 EndIndex = Line.Find(TEXT(" CurrentEnd="));
		const int32 RadiusIndex = Line.Find(TEXT(" Radius="));
		if (StartIndex == INDEX_NONE || EndIndex == INDEX_NONE || RadiusIndex == INDEX_NONE) return;
		FVector Start, End;
		if (Start.InitFromString(Line.Mid(StartIndex + 13, EndIndex - StartIndex - 13))
			&& End.InitFromString(Line.Mid(EndIndex + 12, RadiusIndex - EndIndex - 12)))
		{
			SweepFrames.Add(GFrameCounter);
			Starts.Add(Start);
			Ends.Add(End);
		}
	}

	float GetTravel() const
	{
		float Travel = 0;
		for (int32 Index = 1; Index < Starts.Num(); ++Index)
		{
			Travel = FMath::Max(Travel, FMath::Max(
				FVector::Distance(Starts[0], Starts[Index]), FVector::Distance(Ends[0], Ends[Index])));
		}
		return Travel;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeRemoteMoveNotifyContinuityTest,
	"Outlier.Weapon.Melee.RemoteMoveNotifyContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeRemoteMoveNotifyContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	IConsoleVariable* QueuedEvents = IConsoleManager::Get().FindConsoleVariable(TEXT("a.EnableQueuedAnimEventsOnServer"));
	if (!TestNotNull(TEXT("Engine server animation event switch"), QueuedEvents)) return false;
	const int32 OriginalQueuedEvents = QueuedEvents->GetInt();
	ON_SCOPE_EXIT { QueuedEvents->Set(OriginalQueuedEvents, ECVF_SetByCode); };

	for (bool bSuit : { false, true })
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		// 0: 기존 월드 Tick 대조군. 1: 원격 Move 처리. 2: 같은 경로의 큐 조기 전달 OFF.
		// 3: 한 서버 프레임에 두 Move가 도착한 경우. 패킷 전송 자체는 이 격리 테스트의 범위 밖이다.
		const bool bRemoteMove = Mode != 0;
		const int32 MovesPerFrame = Mode == 3 ? 2 : 1;
		QueuedEvents->Set(Mode == 2 ? 0 : 1, ECVF_SetByCode);
		FMeleeDedicatedWorld Fixture;
		if (!Fixture.Initialize(*this, bSuit, 0)) return false;
		USkeletalMeshComponent* Mesh = Fixture.Shooter->GetMesh();
		UCharacterMovementComponent* Movement = Fixture.Shooter->GetCharacterMovement();
		// ACharacter::PossessedBy가 실제 원격 연결에서 설정하는 애니메이션 스케줄 조건이다.
		// 연결/직렬화 이후의 엔진 ServerMove 진입점을 직접 구동한다.
		Mesh->bOnlyAllowAutonomousTickPose = bRemoteMove;
		Fixture.Controller->AcknowledgedPawn = Fixture.Shooter;
		// 조준선 전방의 고정 표적이다. 실패한 원격 궤적의 첫 소켓으로 표적을 맞추지 않는다.
		AEnemyBase* Target = Fixture.SpawnDamageTarget(*this,
			Fixture.Shooter->GetPawnViewLocation() + FVector(90, 0, 0));
		if (!Target) return false;
		const float HealthBefore = Target->GetCurrentHealth();
		FMeleeSweepObservation Observation(Fixture.Hammer);
		Fixture.Hammer->StartAttack();
		Fixture.Hammer->ReleaseAttack();
		float MoveTime = 0;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			if (bRemoteMove)
			{
				for (int32 Move = 0; Move < MovesPerFrame; ++Move)
				{
					const float Delta = 1.0f / (60 * MovesPerFrame);
					MoveTime += Delta;
					FCharacterNetworkMoveData MoveData;
					MoveData.TimeStamp = MoveTime;
					MoveData.ControlRotation = FRotator::ZeroRotator;
					MoveData.Location = Fixture.Shooter->GetActorLocation();
					MoveData.MovementMode = Movement->PackNetworkMovementMode();
					Movement->ServerMove_PerformMovement(MoveData);
				}
			}
			Fixture.Tick();
			if (Fixture.Hammer->GetAttackPhase() == EMeleeAttackPhase::Idle) break;
		}
		const FString Case = FString::Printf(TEXT("Suit=%d Mode=%d Queued=%d MovesPerFrame=%d"),
			bSuit, Mode, QueuedEvents->GetInt(), MovesPerFrame);
		AddInfo(FString::Printf(TEXT("[MeleeRemoteMove] %s Sweeps=%d SweepFrames=%d TravelCm=%.3f Begins=%d Ends=%d Damage=%.1f"),
			*Case, Observation.Starts.Num(), Observation.SweepFrames.Num(), Observation.GetTravel(),
			Observation.TraceBegins, Observation.TraceEnds, HealthBefore - Target->GetCurrentHealth()));
		TestEqual(*Case, Fixture.Hammer->GetAttackPhase(), EMeleeAttackPhase::Idle);
		TestTrue(*(Case + TEXT(" trace spans multiple frames")), Observation.SweepFrames.Num() >= 3);
		TestTrue(*(Case + TEXT(" trace follows moving hammer sockets")), Observation.GetTravel() > 20.0f);
		TestEqual(*(Case + TEXT(" one TraceBegin per attack")), Observation.TraceBegins, 1);
		TestEqual(*(Case + TEXT(" one TraceEnd per attack")), Observation.TraceEnds, 1);
		TestEqual(*(Case + TEXT(" fixed forward target takes one hammer hit")), HealthBefore - Target->GetCurrentHealth(), 50.0f);
	}
	return !HasAnyErrors();
}

#endif
