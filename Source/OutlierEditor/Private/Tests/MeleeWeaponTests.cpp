#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/MeleeWeaponTestActor.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
struct FScopedMeleeTestWorld
{
	UWorld* World = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Transient melee world is created"), World))
		{
			return false;
		}

		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return true;
	}

	void Shutdown()
	{
		if (!World)
		{
			return;
		}

		// Synthetic worlds need EndPlay routed before renderer resources are released.
		if (World->AreActorsInitialized())
		{
			for (AActor* Actor : FActorRange(World))
			{
				if (Actor)
				{
					Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
				}
			}
		}

		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		if (World->IsRooted())
		{
			World->RemoveFromRoot();
		}
		World = nullptr;
	}

	~FScopedMeleeTestWorld()
	{
		Shutdown();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierMeleeAttackLifecycleTest,
	"Outlier.Weapon.Melee.AttackLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeAttackLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedMeleeTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}

	UWorld* World = TestWorld.World;
	ACharacter* Owner = World->SpawnActor<ACharacter>();
	AMeleeWeaponTestActor* Weapon = World->SpawnActor<AMeleeWeaponTestActor>();
	if (TestNotNull(TEXT("Owner spawns"), Owner) && TestNotNull(TEXT("Melee weapon spawns"), Weapon))
	{
		Weapon->DispatchBeginPlay();
		TestFalse(TEXT("Unequipped weapon cannot attack"), Weapon->CanAttack());
		Weapon->SetTestOwner(Owner);
		Weapon->StartAttack();
		const int32 FirstSequence = Weapon->GetAttackSequence();
		TestEqual(TEXT("Start enters Attack"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Attack);
		TestFalse(TEXT("Active swing blocks another attack"), Weapon->CanAttack());
		Weapon->StartAttack();
		TestEqual(TEXT("Repeated input does not restart the current swing"), Weapon->GetAttackSequence(), FirstSequence);
		Weapon->ReleaseAttack();

		Weapon->FinishAttack(FirstSequence);
		TestEqual(TEXT("Completion before impact is ignored"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Attack);
		Weapon->CommitAttack(FirstSequence);
		TestEqual(TEXT("Impact enters Recovery"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
		TestTrue(TEXT("Recovery remains part of the active attack"), Weapon->IsAttacking());
		TestFalse(TEXT("Recovery blocks another attack"), Weapon->CanAttack());
		Weapon->CommitAttack(FirstSequence);
		TestEqual(TEXT("Duplicate impact stays in Recovery"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
		Weapon->FinishAttack(FirstSequence);
		TestTrue(TEXT("Completion permits the next attack"), Weapon->CanAttack());
		TestFalse(TEXT("Completion clears the attack flag"), Weapon->IsAttacking());

		Weapon->StartAttack();
		const int32 CancelledSequence = Weapon->GetAttackSequence();
		Weapon->StopAttack();
		Weapon->CommitAttack(CancelledSequence);
		TestEqual(TEXT("Cancelled swing cannot commit"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Idle);
		Weapon->StartAttack();
		const int32 NewSequence = Weapon->GetAttackSequence();
		Weapon->CommitAttack(CancelledSequence);
		Weapon->FinishAttack(CancelledSequence);
		TestEqual(TEXT("Old callbacks cannot advance a new swing"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Attack);
		Weapon->CommitAttack(NewSequence);
		Weapon->StopAttack();
		Weapon->FinishAttack(NewSequence);
		TestEqual(TEXT("Recovery can be cancelled"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Idle);

		Weapon->StartAttack();
		Weapon->OnUnequipped();
		TestEqual(TEXT("Unequip cancels the swing"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Idle);
		TestFalse(TEXT("Unequipped weapon cannot restart"), Weapon->CanAttack());
		Weapon->SetTestOwner(Owner);
		Weapon->StartAttack();
		const int32 TimerStartSequence = Weapon->GetAttackSequence();
		TestTrue(TEXT("Attack schedules its fallback timer"), Weapon->HasPendingAttackTimers());
		Weapon->CommitAttack(TimerStartSequence);
		TestTrue(TEXT("Recovery schedules its fallback timer"), Weapon->HasPendingAttackTimers());
		Weapon->FinishAttack(TimerStartSequence);
		TestEqual(TEXT("Held input repeats after a completed cycle"), Weapon->GetAttackSequence(), TimerStartSequence + 1);
		Weapon->ReleaseAttack();
		const int32 ReleasedSequence = Weapon->GetAttackSequence();
		Weapon->CommitAttack(ReleasedSequence);
		Weapon->FinishAttack(ReleasedSequence);
		TestEqual(TEXT("Release prevents another completed-cycle swing"), Weapon->GetAttackSequence(), ReleasedSequence);
		TestEqual(TEXT("Fallback timers finish without animation assets"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Idle);
		TestTrue(TEXT("Completed timer cycle permits another click"), Weapon->CanAttack());
		Weapon->StartAttack();
		Weapon->Destroy();
		TestFalse(TEXT("Destroy clears bound attack timers"), Weapon->HasPendingAttackTimers());
	}

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform::Identity, SpawnParameters)
		: nullptr;
	AMeleeWeaponTestActor* InputWeapon = World->SpawnActor<AMeleeWeaponTestActor>();
	if (TestNotNull(TEXT("Shooter spawns for input policy"), Shooter)
		&& TestNotNull(TEXT("Input weapon spawns"), InputWeapon))
	{
		Shooter->AFirstPersonCharacter::EquipWeapon(InputWeapon);
		Shooter->RefreshCombatState();
		UShooterCombatComponent* Combat = Shooter->FindComponentByClass<UShooterCombatComponent>();
		UShooterMovementComponent* Movement = Shooter->FindComponentByClass<UShooterMovementComponent>();
		UShooterInventoryComponent* Inventory = Shooter->FindComponentByClass<UShooterInventoryComponent>();
		if (TestNotNull(TEXT("Combat component exists"), Combat)
			&& TestNotNull(TEXT("Movement component exists"), Movement)
			&& TestNotNull(TEXT("Inventory component exists"), Inventory))
		{
			if (!Inventory->HasBegunPlay())
			{
				Inventory->BeginPlay();
			}

			Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			Shooter->GetCharacterMovement()->Velocity = FVector(600.0f, 0.0f, 0.0f);
			Movement->HandleSprintPressed();
			TestTrue(TEXT("Fixture starts sprinting"), Shooter->IsSprinting());
			Combat->TryStartAttack();
			Combat->ResolveStateConflicts();
			TestTrue(TEXT("Melee keeps sprint active"), Shooter->IsSprinting());
			TestFalse(TEXT("Melee does not latch firearm intent"), Combat->WantsToFire());
			Combat->TryStopAttack();
			TestTrue(TEXT("Button release keeps the swing active"), InputWeapon->IsAttacking());
			Combat->TryReload();
			TestTrue(TEXT("Reload input does not cancel melee"), InputWeapon->IsAttacking());
			InputWeapon->PerformAttack();
			TestEqual(TEXT("Shooter exposes Recovery"), Shooter->GetCombatState(), ECombatState::Recovery);
			InputWeapon->FinishAttack(InputWeapon->GetAttackSequence());
			TestFalse(TEXT("Released click completes without repeating"), InputWeapon->IsAttacking());

			Combat->TryStartAttack();
			for (int32 Swing = 0; Swing < 3; ++Swing)
			{
				const int32 HeldSequence = InputWeapon->GetAttackSequence();
				InputWeapon->PerformAttack();
				InputWeapon->FinishAttack(HeldSequence);
				TestEqual(TEXT("Held input starts the next swing after Recovery"), InputWeapon->GetAttackSequence(), HeldSequence + 1);
				TestTrue(TEXT("Repeated swing keeps sprint active"), Shooter->IsSprinting());
			}
			Combat->TryStopAttack();
			InputWeapon->PerformAttack();
			const int32 RecoverySequence = InputWeapon->GetAttackSequence();
			Combat->TryStartAttack();
			TestEqual(TEXT("Press during Recovery does not shorten it"), InputWeapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
			TestEqual(TEXT("Press during Recovery preserves the current sequence"), InputWeapon->GetAttackSequence(), RecoverySequence);
			InputWeapon->FinishAttack(RecoverySequence);
			TestEqual(TEXT("Holding a new press during Recovery resumes repeating"), InputWeapon->GetAttackSequence(), RecoverySequence + 1);
			InputWeapon->PerformAttack();
			Combat->TryStopAttack();
			InputWeapon->FinishAttack(InputWeapon->GetAttackSequence());
			TestFalse(TEXT("Release during Recovery stops repetition"), InputWeapon->IsAttacking());

			Combat->TryStartAttack();
			InputWeapon->PerformAttack();
			const int32 SwitchCancelledSequence = InputWeapon->GetAttackSequence();
			Inventory->SelectWeaponByIndex(static_cast<int32>(EWeaponSlot::Primary));
			TestFalse(TEXT("Weapon slot input cancels Recovery even with an empty slot"), InputWeapon->IsAttacking());
			InputWeapon->FinishAttack(SwitchCancelledSequence);
			TestEqual(TEXT("Cancelled recovery cannot restart held input"), InputWeapon->GetAttackSequence(), SwitchCancelledSequence);
			TestFalse(TEXT("Cancellation clears both repeat-cycle timers"), InputWeapon->HasPendingAttackTimers());

			Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			Combat->TryStartAttack();
			TestTrue(TEXT("Melee can start in the air"), InputWeapon->IsAttacking());
			UFunction* UseSuitFunction = Shooter->FindFunction(TEXT("ServerUseSuitAbility"));
			if (TestNotNull(TEXT("Suit RPC exists"), UseSuitFunction))
			{
				struct FUseSuitParameters
				{
					FGameplayTag AbilityTag;
				};
				FUseSuitParameters SuitParameters { OutlierGameplayTags::Ability::Shooter::Stealth() };
				Shooter->ProcessEvent(UseSuitFunction, &SuitParameters);
			}
			TestFalse(TEXT("Suit input cancels melee before impact"), InputWeapon->IsAttacking());
			Combat->TryStartAttack();
			Shooter->HandleDeath();
			TestFalse(TEXT("Death cancels melee"), InputWeapon->IsAttacking());
		}
	}

	return true;
}

#endif
