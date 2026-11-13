#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "OutlierPlayerState.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "TimerManager.h"

namespace
{
struct FQuantumLeapEQSWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	AShooterCharacter* Shooter = nullptr;
	APartnerCharacter* Partner = nullptr;
	UOutlierAbilitySystemComponent* ASC = nullptr;
	UShooterMovementComponent* Intent = nullptr;
	UShooterCharacterMovementComponent* Movement = nullptr;
	FVector Start;
	static constexpr float CastSeconds = 0.3f;

	bool Initialize()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("QuantumLeapEQS")), GetTransientPackage());
		if (!World) return false;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		if (!World->SetGameMode(URL)) return false;
		World->InitializeActorsForPlay(FURL());

		// 두 Pawn은 abstract native class이므로 기존 GAS 검증과 같은 실제 BP를 사용한다.
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
			TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
		if (!ShooterClass || !PartnerClass) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(-800, 0, 110), FRotator::ZeroRotator, Spawn);
		Partner = World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(0, 0, 110), FRotator::ZeroRotator, Spawn);
		if (!Shooter || !Partner) return false;
		if (!Shooter->HasActorBegunPlay()) Shooter->DispatchBeginPlay();
		if (!Partner->HasActorBegunPlay()) Partner->DispatchBeginPlay();
		Shooter->SetPartnerCharacter(Partner);
		Partner->SetShooterCharacter(Shooter);
		Partner->SetActorEnableCollision(false);
		ASC = Shooter->GetOutlierAbilitySystemComponent();
		ASC->InitializeForPawn(Shooter);
		Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);
		if (!ASC->IsShooterSuitConfigured()) return false;
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		if (!State) return false;
		Shooter->SetPlayerState(State);
		State->SetAcquiredSuit(true);
		ASC->AddLooseGameplayTag(OutlierGameplayTags::Ability::Shooter::QuantumLeap());
		FOutlierShooterSuitConfig Config = ASC->GetShooterSuitConfig();
		Config.MaxPartnerDistance = 2000;
		Config.QuantumLeap.CastTimeSeconds = CastSeconds;
		Config.QuantumLeap.CooldownSeconds = 8;
		if (!ASC->UpdateShooterSuitConfig(Config)) return false;
		Intent = Shooter->FindComponentByClass<UShooterMovementComponent>();
		Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
		if (!Intent || !Movement) return false;
		Movement->SetCrouchedHalfHeight(40);
		Movement->SetMovementMode(MOVE_Walking);
		Shooter->SetActorLocation(FVector(-800, 0, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
		Start = Shooter->GetActorLocation();
		return Shooter->IsSuitUsable();
	}

	~FQuantumLeapEQSWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		if (Instance) Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	AActor* Box(FVector Location, FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Shape);
		Shape->SetBoxExtent(Extent);
		Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Shape->SetCollisionObjectType(ECC_WorldStatic);
		Shape->SetCollisionResponseToAllChannels(ECR_Block);
		Shape->RegisterComponent();
		Shape->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}
	AActor* Floor() { return Box(FVector(0, 0, -10), FVector(2000, 2000, 10)); }
	AActor* Ceiling() { return Box(FVector(0, 0, 150), FVector(350, 350, 20)); }
	bool Activate() { return ASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()); }
	bool Casting() const { return ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::QuantumLeaping()); }
	bool Immune() const { return ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()); }
	int32 Charges() const { return ASC->GetShooterQuantumLeapChargeState().CurrentCharges; }
	void Queries()
	{
		// EQS와 시전 시간을 분리해 대기/시전/재탐색 경계를 실제 물리 이동 없이 검사한다.
		for (int32 Index = 0; Index < 100; ++Index)
			UEnvQueryManager::GetCurrent(World)->Tick(0.01f);
	}
	void Time(float Seconds)
	{
		for (float Elapsed = 0; Elapsed < Seconds; Elapsed += 0.01f)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(0.01f);
		}
	}
	void FinishCast()
	{
		Time(CastSeconds + 0.05f);
		Queries();
	}
	void UpdatePosture()
	{
		// 실제 CMC 자세 갱신을 호출한다. 무조종 authority fixture도 바닥/기립을 처리한다.
		Movement->bRunPhysicsWithNoController = true;
		Movement->PerformMovement(0.01f);
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FQuantumLeapEQSTest, "Outlier.GAS.Shooter.QuantumLeapEQS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FQuantumLeapEQSTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("SuccessfulAbility"), TEXT("NoFloor"), TEXT("PartnerSnapshot"), TEXT("ExpandedInitial"),
		TEXT("AutomaticCrouchAndStand"), TEXT("HeldCrouch"), TEXT("CrouchDuringCast"), TEXT("ReleaseDuringCast"),
		TEXT("ObstacleRescan"), TEXT("FloorRemoved"), TEXT("CancelPending"), TEXT("CancelCasting")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FQuantumLeapEQSTest::RunTest(const FString& Parameters)
{
	FQuantumLeapEQSWorld F;
	if (!TestTrue(TEXT("Real Shooter/Partner ability fixture initializes"), F.Initialize())) return false;
	AActor* Ground = Parameters == TEXT("NoFloor") ? nullptr
		: Parameters == TEXT("ExpandedInitial") ? F.Box(FVector(0, 0, -310), FVector(500, 500, 10)) : F.Floor();
	const bool bLowCeiling = Parameters == TEXT("AutomaticCrouchAndStand") || Parameters == TEXT("HeldCrouch");
	AActor* Roof = bLowCeiling ? F.Ceiling() : nullptr;
	const bool bHeld = Parameters == TEXT("HeldCrouch") || Parameters == TEXT("ReleaseDuringCast");
	if (bHeld)
	{
		F.Intent->RequestCrouchOrSlide();
		F.UpdatePosture();
		TestTrue(TEXT("Fixture uses the real held crouch input"), F.Intent->WantsToCrouch());
		F.Start = F.Shooter->GetActorLocation();
	}
	const FRotator Facing = F.Shooter->GetActorRotation();
	TestTrue(TEXT("Authoritative ability accepts the request"), F.Activate());
	TestEqual(TEXT("Input does not spend a charge"), F.Charges(), 1);
	TestFalse(TEXT("Pending initial EQS does not grant cast immunity"), F.Immune());
	if (Parameters == TEXT("CancelPending"))
	{
		TestTrue(TEXT("Pending query is cancellable"), F.Shooter->CancelActiveQuantumLeap());
		F.Queries();
		F.Time(1);
		TestTrue(TEXT("Late query cannot move the cancelled Pawn"), F.Shooter->GetActorLocation().Equals(F.Start, 0.1f));
		TestFalse(TEXT("Late query cannot grant immunity"), F.Immune());
		TestEqual(TEXT("Late query cannot consume a charge"), F.Charges(), 1);
		TestFalse(TEXT("Late query cannot crouch the Pawn"), F.Shooter->bIsCrouched);
		TestFalse(TEXT("Cancellation preserves standing input"), F.Intent->WantsToCrouch());
		return true;
	}
	F.Queries();
	if (Parameters == TEXT("NoFloor"))
	{
		TestFalse(TEXT("No safe floor never begins casting"), F.Casting());
		TestFalse(TEXT("No safe floor never grants immunity"), F.Immune());
		TestEqual(TEXT("No safe floor preserves the charge"), F.Charges(), 1);
		TestTrue(TEXT("No safe floor starts the separate retry delay"), F.ASC->IsShooterQuantumLeapRetryActive());
		const float Remaining = F.ASC->GetTimedCooldownRemaining(
			OutlierGameplayTags::Cooldown::Shooter::QuantumLeapRetry(), F.Shooter);
		TestTrue(TEXT("Failure delay remains 0.5 seconds"), Remaining > 0.49f && Remaining <= 0.5f);
		TestFalse(TEXT("Failure does not start charge regeneration"), F.ASC->IsShooterQuantumLeapCooldownActive());
		TestTrue(TEXT("No safe floor leaves the Pawn in place"), F.Shooter->GetActorLocation().Equals(F.Start, 0.1f));
		return true;
	}
	if (!TestTrue(TEXT("A safe initial EQS result starts the cast"), F.Casting())) return false;
	TestTrue(TEXT("Cast retains damage immunity"), F.Immune());
	TestEqual(TEXT("Casting still preserves the charge"), F.Charges(), 1);
	TestFalse(TEXT("A cast rejects duplicate activation"), F.Activate());
	if (Parameters == TEXT("CancelCasting"))
	{
		TestTrue(TEXT("Casting is cancellable"), F.Shooter->CancelActiveQuantumLeap());
		F.FinishCast();
		TestTrue(TEXT("Cancelled timer cannot teleport"), F.Shooter->GetActorLocation().Equals(F.Start, 0.1f));
		TestFalse(TEXT("Cancel removes cast immunity"), F.Immune());
		TestEqual(TEXT("Cancel preserves the charge"), F.Charges(), 1);
		return true;
	}
	if (Parameters == TEXT("PartnerSnapshot"))
		F.Partner->SetActorLocationAndRotation(FVector(600, 0, 110), FRotator(0, 180, 0));
	if (Parameters == TEXT("CrouchDuringCast")) F.Intent->RequestCrouchOrSlide();
	if (Parameters == TEXT("ReleaseDuringCast")) F.Intent->RequestUncrouch();
	if (Parameters == TEXT("ObstacleRescan")) Roof = F.Ceiling();
	if (Parameters == TEXT("FloorRemoved")) Ground->SetActorEnableCollision(false);
	F.FinishCast();
	if (Parameters == TEXT("FloorRemoved"))
	{
		TestTrue(TEXT("Lost floor rejects the cached destination"), F.Shooter->GetActorLocation().Equals(F.Start, 0.1f));
		TestEqual(TEXT("Failed completion rescan preserves the charge"), F.Charges(), 1);
		TestTrue(TEXT("Failed completion rescan applies retry delay"), F.ASC->IsShooterQuantumLeapRetryActive());
		TestFalse(TEXT("Failed completion rescan ends immunity"), F.Immune());
		// 복구 가능한 바닥을 나중에 추가해도 이미 종료한 시도가 다시 탐색/이동하지 않는다.
		Ground->SetActorEnableCollision(true);
		F.Queries();
		F.Time(1);
		F.Queries();
		TestTrue(TEXT("Terminal failure does not run another rescan"), F.Shooter->GetActorLocation().Equals(F.Start, 0.1f));
		TestEqual(TEXT("Terminal failure cannot later spend a charge"), F.Charges(), 1);
		return true;
	}
	TestTrue(TEXT("Success relocates to the saved Partner neighborhood"),
		FVector::Dist2D(F.Shooter->GetActorLocation(), FVector::ZeroVector) <= 300.1f);
	TestFalse(TEXT("Success actually moves the Pawn"), F.Shooter->GetActorLocation().Equals(F.Start, 0.1f));
	TestTrue(TEXT("Success preserves Shooter facing"), F.Shooter->GetActorRotation().Equals(Facing, 0.01f));
	TestEqual(TEXT("Success consumes exactly one charge"), F.Charges(), 0);
	TestTrue(TEXT("Successful movement starts charge regeneration"), F.ASC->IsShooterQuantumLeapCooldownActive());
	TestFalse(TEXT("Success ends the cast"), F.Casting());
	TestFalse(TEXT("Success removes cast immunity"), F.Immune());
	TestFalse(TEXT("Relocation posture never starts a slide"), F.Intent->IsSliding());
	if (Parameters == TEXT("ExpandedInitial"))
		TestTrue(TEXT("Initial failure expands below the original 3m floor limit"), F.Shooter->GetActorLocation().Z < -150);
	if (bLowCeiling || Parameters == TEXT("ObstacleRescan"))
	{
		TestTrue(TEXT("Low ceiling uses the actual crouched capsule"), F.Shooter->bIsCrouched);
		TestTrue(TEXT("Capsule is below the ceiling"), F.Shooter->GetActorLocation().Z
			+ F.Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() < 130);
		Roof->SetActorEnableCollision(false);
		F.UpdatePosture();
		TestEqual(TEXT("After clearance actual posture follows preserved input"), bool(F.Shooter->bIsCrouched), bHeld);
	}
	if (Parameters == TEXT("ReleaseDuringCast")) F.UpdatePosture();
	const bool bExpectedCrouchIntent = Parameters == TEXT("HeldCrouch") || Parameters == TEXT("CrouchDuringCast");
	TestEqual(TEXT("Relocation preserves the latest crouch input"), F.Intent->WantsToCrouch(), bExpectedCrouchIntent);
	if (Parameters == TEXT("CrouchDuringCast") || Parameters == TEXT("ReleaseDuringCast"))
		TestEqual(TEXT("Open-ground landing uses the latest input posture"), bool(F.Shooter->bIsCrouched), bExpectedCrouchIntent);
	// 완료 재탐색은 새 시전 시간을 붙이지 않는다. 추가 EQS/timer 구동에도 두 번째 완료가 없다.
	F.Queries();
	F.Time(FQuantumLeapEQSWorld::CastSeconds + 0.05f);
	TestEqual(TEXT("No delayed duplicate completion consumes another charge"), F.Charges(), 0);
	TestFalse(TEXT("Completion cannot restart cast immunity"), F.Immune());
	return true;
}

#endif
