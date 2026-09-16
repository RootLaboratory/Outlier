#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/MeleeWeaponTestActor.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Enemy/EnemyBase.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
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
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		if (!Test.TestTrue(TEXT("Transient melee world creates an authority game mode"), World->SetGameMode(FURL())))
		{
			return false;
		}
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
		const int32 NotifySequence = Weapon->GetAttackSequence();
		Weapon->HandleHitNotify();
		TestEqual(TEXT("Hit Notify enters Recovery"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
		Weapon->HandleHitNotify();
		TestEqual(TEXT("Duplicate Hit Notify is ignored"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
		Weapon->ReleaseAttack();
		Weapon->HandleRecoveryEndNotify();
		TestEqual(TEXT("Recovery Notify completes its swing"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Idle);
		TestEqual(TEXT("Notify completion retains its sequence"), Weapon->GetAttackSequence(), NotifySequence);

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

		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (TestNotNull(TEXT("Trace owner controller spawns"), Controller))
		{
			Controller->Possess(Owner);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			Weapon->SetTestTraceConfig(200.0f, 40.0f);

			FVector ViewLocation;
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
			const FVector Forward = ViewRotation.Vector();
			const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);

			auto SpawnTraceEnemy = [World](const FVector& Location)
			{
				FActorSpawnParameters EnemySpawnParameters;
				EnemySpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(Location, FRotator::ZeroRotator, EnemySpawnParameters);
				if (Enemy)
				{
					UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
					Capsule->SetCapsuleSize(10.0f, 20.0f);
					Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
					Capsule->SetCollisionObjectType(ECC_Pawn);
					Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
					Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
				}
				return Enemy;
			};

			AEnemyBase* OffCenterEnemy = SpawnTraceEnemy(ViewLocation + Forward * 120.0f + Right * 35.0f);
			AEnemyBase* CenterEnemy = SpawnTraceEnemy(ViewLocation + Forward * 180.0f);
			if (TestNotNull(TEXT("Off-center trace enemy spawns"), OffCenterEnemy)
				&& TestNotNull(TEXT("Center trace enemy spawns"), CenterEnemy))
			{
				const FVector CenterLocation = CenterEnemy->GetActorLocation();
				Weapon->SetTestMeleeTraceRadius(12.0f);
				Weapon->SetTestMeleeTraceSockets(
					true,
					CenterLocation + Right * 80.0f - FVector::UpVector * 10.0f,
					CenterLocation + Right * 80.0f + FVector::UpVector * 10.0f);
				Weapon->ResetAppliedTarget();
				Weapon->StartAttack();
				const int32 SocketTraceSequence = Weapon->GetAttackSequence();
				TestTrue(TEXT("Trace NotifyState begins with valid sockets"), Weapon->BeginMeleeTrace());
				Weapon->SetTestMeleeTraceSockets(
					true,
					CenterLocation - Right * 80.0f - FVector::UpVector * 10.0f,
					CenterLocation - Right * 80.0f + FVector::UpVector * 10.0f);
				Weapon->TickMeleeTrace();
				TestEqual(
					TEXT("Frame-to-frame socket trajectory hits the crossed enemy"),
					Weapon->GetLastAppliedTarget(),
					static_cast<AActor*>(CenterEnemy));
				TestEqual(TEXT("One swing forwards one target"), Weapon->GetAppliedTargetCount(), 1);
				Weapon->TickMeleeTrace();
				TestEqual(TEXT("The same target is damaged once per swing"), Weapon->GetAppliedTargetCount(), 1);
				Weapon->EndMeleeTrace();
				Weapon->TickMeleeTrace();
				TestEqual(TEXT("Trace End stops further socket queries"), Weapon->GetAppliedTargetCount(), 1);
				Weapon->ReleaseAttack();
				Weapon->FinishAttack(SocketTraceSequence);

				Weapon->SetTestMeleeTraceSockets(false, FVector::ZeroVector, FVector::ZeroVector);
				Weapon->StartAttack();
				const int32 MissingSocketSequence = Weapon->GetAttackSequence();
				TestFalse(TEXT("Missing sockets reject the trace window"), Weapon->BeginMeleeTrace());
				TestEqual(TEXT("Missing sockets still advance the attack to Recovery"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
				Weapon->ReleaseAttack();
				Weapon->FinishAttack(MissingSocketSequence);

				OffCenterEnemy->SetActorLocation(ViewLocation + Forward * 300.0f);
				CenterEnemy->SetActorLocation(ViewLocation + Forward * 300.0f + Right * 50.0f);
				Weapon->SetTestMeleeTraceSockets(
					true,
					CenterLocation - FVector::UpVector * 10.0f,
					CenterLocation + FVector::UpVector * 10.0f);
				Weapon->ResetAppliedTarget();
				Weapon->StartAttack();
				const int32 MissSequence = Weapon->GetAttackSequence();
				Weapon->BeginMeleeTrace();
				TestNull(TEXT("Actors outside the socket trajectory are ignored"), Weapon->GetLastAppliedTarget());
				TestEqual(TEXT("Miss still enters Recovery"), Weapon->GetAttackPhase(), EMeleeAttackPhase::Recovery);
				Weapon->EndMeleeTrace();
				Weapon->ReleaseAttack();
				Weapon->FinishAttack(MissSequence);

				CenterEnemy->SetActorLocation(ViewLocation + Forward * 150.0f);
				FActorSpawnParameters BlockerSpawnParameters;
				BlockerSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ACharacter* VisibilityBlocker = World->SpawnActor<ACharacter>(
					ViewLocation + Forward * 75.0f,
					FRotator::ZeroRotator,
					BlockerSpawnParameters);
				if (TestNotNull(TEXT("Visibility blocker spawns"), VisibilityBlocker))
				{
					UCapsuleComponent* BlockerCapsule = VisibilityBlocker->GetCapsuleComponent();
					BlockerCapsule->SetCapsuleSize(20.0f, 20.0f);
					BlockerCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
					BlockerCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
					BlockerCapsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

					const FVector BlockedTargetLocation = CenterEnemy->GetActorLocation();
					Weapon->SetTestMeleeTraceSockets(
						true,
						BlockedTargetLocation - FVector::UpVector * 10.0f,
						BlockedTargetLocation + FVector::UpVector * 10.0f);
					Weapon->ResetAppliedTarget();
					Weapon->StartAttack();
					const int32 BlockedSequence = Weapon->GetAttackSequence();
					Weapon->BeginMeleeTrace();
					TestNull(TEXT("Visibility blocker prevents melee target selection"), Weapon->GetLastAppliedTarget());
					Weapon->EndMeleeTrace();
					Weapon->ReleaseAttack();
					Weapon->FinishAttack(BlockedSequence);

					VisibilityBlocker->Destroy();
					CenterEnemy->SetActorLocation(ViewLocation + Forward * 300.0f);
					UClass* PartnerClass = LoadClass<APartnerCharacter>(
						nullptr,
						TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
					APartnerCharacter* Partner = PartnerClass
						? World->SpawnActor<APartnerCharacter>(
							PartnerClass,
							ViewLocation + Forward * 150.0f,
							FRotator::ZeroRotator,
							BlockerSpawnParameters)
						: nullptr;
					if (TestNotNull(TEXT("Partner spawns for melee team damage"), Partner))
					{
						if (!Partner->HasActorBegunPlay())
						{
							Partner->DispatchBeginPlay();
						}
						UOutlierAbilitySystemComponent* PartnerAbilitySystem = Partner->GetOutlierAbilitySystemComponent();
						PartnerAbilitySystem->SetNumericAttributeBase(
							UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
						PartnerAbilitySystem->SetNumericAttributeBase(
							UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);
						UCapsuleComponent* PartnerCapsule = Partner->GetCapsuleComponent();
						PartnerCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
						PartnerCapsule->SetCollisionObjectType(ECC_Pawn);
						PartnerCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
						PartnerCapsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
						TestNull(TEXT("Partner is excluded from indicator targeting"), Weapon->FindIndicatorTargetForTest());

						const FVector PartnerLocation = Partner->GetActorLocation();
						Weapon->SetTestMeleeTraceSockets(
							true,
							PartnerLocation - FVector::UpVector * 10.0f,
							PartnerLocation + FVector::UpVector * 10.0f);
						Weapon->ResetAppliedTarget();
						Weapon->StartAttack();
						const int32 PartnerSequence = Weapon->GetAttackSequence();
						Weapon->BeginMeleeTrace();
						TestEqual(
							TEXT("Partner participates in melee socket collision"),
							Weapon->GetLastAppliedTarget(),
							static_cast<AActor*>(Partner));
						Weapon->EndMeleeTrace();
						Weapon->ReleaseAttack();
						Weapon->FinishAttack(PartnerSequence);

						Weapon->SetTestDamage(50.0f);
						Weapon->ResetHitResults();
						Weapon->ApplyDamageToTargetForTest(Partner);
						TestEqual(TEXT("Partner receives normal melee damage"), Partner->GetVitalAttributeSet()->GetHealth(), 50.0f);
						TestEqual(TEXT("Partner damage reports PartnerDamage"), Weapon->GetLastHitContext().ResultType, EMeleeHitResultType::PartnerDamage);

						Partner->SetCanBeDamaged(false);
						Weapon->ApplyDamageToTargetForTest(Partner);
						TestEqual(TEXT("Damage-disabled Partner ignores melee damage"), Partner->GetVitalAttributeSet()->GetHealth(), 50.0f);
						Partner->SetCanBeDamaged(true);
						Weapon->ApplyDamageToTargetForTest(Partner);
						TestTrue(
							TEXT("Lethal melee damage enters Partner reboot"),
							Partner->GetPartnerVitalityComponent()->IsRebooting());

						Weapon->ResetAppliedTarget();
						Weapon->StartAttack();
						const int32 RebootingPartnerSequence = Weapon->GetAttackSequence();
						Weapon->BeginMeleeTrace();
						TestNull(TEXT("Rebooting Partner is excluded from melee targeting"), Weapon->GetLastAppliedTarget());
						Weapon->EndMeleeTrace();
						Weapon->ReleaseAttack();
						Weapon->FinishAttack(RebootingPartnerSequence);
					}
				}
			}
		}

		UClass* DamageEnemyClass = LoadClass<AEnemyBase>(
			nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		TestNotNull(TEXT("Configured Enemy class loads for melee damage"), DamageEnemyClass);
		auto SpawnDamageEnemy = [World, DamageEnemyClass](EEnemyCombatState CombatState)
		{
			if (!DamageEnemyClass)
			{
				return static_cast<AEnemyBase*>(nullptr);
			}

			FActorSpawnParameters EnemySpawnParameters;
			EnemySpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(
				DamageEnemyClass,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EnemySpawnParameters);
			if (!Enemy)
			{
				return static_cast<AEnemyBase*>(nullptr);
			}
			if (!Enemy->HasActorBegunPlay())
			{
				Enemy->DispatchBeginPlay();
			}

			UOutlierAbilitySystemComponent* AbilitySystem = Enemy->GetOutlierAbilitySystemComponent();
			AbilitySystem->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
			AbilitySystem->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);
			if (CombatState == EEnemyCombatState::Alert)
			{
				Enemy->EnterAlert(FVector::ZeroVector);
			}
			else if (CombatState == EEnemyCombatState::Combat)
			{
				Enemy->EnterCombat(FVector::ZeroVector);
			}
			else if (CombatState == EEnemyCombatState::Stun)
			{
				Enemy->EnterStun();
			}
			return Enemy;
		};

		Weapon->SetTestDamage(50.0f);
		AEnemyBase* CombatEnemy = SpawnDamageEnemy(EEnemyCombatState::Combat);
		if (TestNotNull(TEXT("Combat damage enemy spawns"), CombatEnemy))
		{
			Weapon->ResetHitResults();
			Weapon->ApplyDamageToTargetForTest(CombatEnemy);
			TestEqual(TEXT("Combat enemy receives base melee damage"), CombatEnemy->GetCurrentHealth(), 50.0f);
			TestFalse(TEXT("Non-lethal combat hit keeps the enemy alive"), CombatEnemy->IsDead());
			TestEqual(TEXT("Combat damage reports EnemyDamage"), Weapon->GetLastHitContext().ResultType, EMeleeHitResultType::EnemyDamage);
		}

		for (const EEnemyCombatState InstantKillState : {
			EEnemyCombatState::NonCombat,
			EEnemyCombatState::Alert,
			EEnemyCombatState::Stun })
		{
			AEnemyBase* InstantKillEnemy = SpawnDamageEnemy(InstantKillState);
			if (TestNotNull(TEXT("Instant-kill enemy spawns"), InstantKillEnemy))
			{
				Weapon->ResetHitResults();
				Weapon->ApplyDamageToTargetForTest(InstantKillEnemy);
				TestEqual(TEXT("Eligible melee state drains current Health"), InstantKillEnemy->GetCurrentHealth(), 0.0f);
				TestTrue(TEXT("Eligible melee state enters existing death flow"), InstantKillEnemy->IsDead());
				TestEqual(TEXT("Eligible state reports EnemyInstantKill"), Weapon->GetLastHitContext().ResultType, EMeleeHitResultType::EnemyInstantKill);
			}
		}

		AEnemyBase* DamageDisabledEnemy = SpawnDamageEnemy(EEnemyCombatState::Combat);
		if (TestNotNull(TEXT("Damage-disabled enemy spawns"), DamageDisabledEnemy))
		{
			DamageDisabledEnemy->SetCanBeDamaged(false);
			Weapon->ApplyDamageToTargetForTest(DamageDisabledEnemy);
			TestEqual(TEXT("Damage-disabled enemy ignores melee damage"), DamageDisabledEnemy->GetCurrentHealth(), 100.0f);
		}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierMeleeTargetInterfaceTest,
	"Outlier.Weapon.Melee.TargetInterface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeTargetInterfaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedMeleeTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}

	UWorld* World = TestWorld.World;
	ACharacter* Owner = World->SpawnActor<ACharacter>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AMeleeWeaponTestActor* Weapon = World->SpawnActor<AMeleeWeaponTestActor>();
	if (!TestNotNull(TEXT("Target interface owner spawns"), Owner)
		|| !TestNotNull(TEXT("Target interface controller spawns"), Controller)
		|| !TestNotNull(TEXT("Target interface weapon spawns"), Weapon))
	{
		return false;
	}

	Controller->Possess(Owner);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Weapon->SetTestOwner(Owner);
	Weapon->SetTestTraceConfig(200.0f, 40.0f);

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Forward = ViewRotation.Vector();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMeleeTargetTestEnemy* FirstTarget = World->SpawnActor<AMeleeTargetTestEnemy>(
		ViewLocation + Forward * 120.0f,
		FRotator::ZeroRotator,
		SpawnParameters);
	AMeleeTargetTestEnemy* SecondTarget = World->SpawnActor<AMeleeTargetTestEnemy>(
		ViewLocation + Forward * 300.0f,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (TestNotNull(TEXT("First indicator target spawns"), FirstTarget)
		&& TestNotNull(TEXT("Second indicator target spawns"), SecondTarget))
	{
		for (AMeleeTargetTestEnemy* Target : { FirstTarget, SecondTarget })
		{
			UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
			Capsule->SetCapsuleSize(10.0f, 20.0f);
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionObjectType(ECC_Pawn);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}

		TestEqual(TEXT("Indicator query selects the valid in-range enemy"), Weapon->FindIndicatorTargetForTest(), static_cast<AActor*>(FirstTarget));
		Weapon->SetCurrentMeleeTargetForTest(FirstTarget);
		Weapon->SetCurrentMeleeTargetForTest(FirstTarget);
		TestEqual(TEXT("Unchanged target is activated once"), FirstTarget->GetTargetedCount(), 1);
		TestEqual(TEXT("Indicator receives the owning character"), FirstTarget->GetLastIndicatorInstigator(), static_cast<AActor*>(Owner));

		FirstTarget->SetActorLocation(ViewLocation + Forward * 300.0f);
		SecondTarget->SetActorLocation(ViewLocation + Forward * 120.0f);
		AActor* ChangedTarget = Weapon->FindIndicatorTargetForTest();
		Weapon->SetCurrentMeleeTargetForTest(ChangedTarget);
		TestEqual(TEXT("Previous target receives one deactivation"), FirstTarget->GetUntargetedCount(), 1);
		TestEqual(TEXT("New target receives one activation"), SecondTarget->GetTargetedCount(), 1);

		SecondTarget->SetIndicatorEligible(false);
		TestNull(TEXT("Interface can reject an otherwise valid indicator target"), Weapon->FindIndicatorTargetForTest());
		Weapon->SetCurrentMeleeTargetForTest(nullptr);
		TestEqual(TEXT("Clearing target deactivates the current target"), SecondTarget->GetUntargetedCount(), 1);

		FMeleeHitContext ResultContext;
		ResultContext.TargetActor = FirstTarget;
		ResultContext.HitLocation = FVector(10.0f, 20.0f, 30.0f);
		ResultContext.HitNormal = FVector::UpVector;
		ResultContext.AttackSequence = 7;
		ResultContext.ResultType = EMeleeHitResultType::EnemyDamage;
		Weapon->NotifyMeleeHitResultForTest(ResultContext);
		TestEqual(TEXT("Server result reaches target interface"), FirstTarget->GetConfirmedCount(), 1);
		TestEqual(TEXT("Result preserves attack sequence"), FirstTarget->GetLastConfirmedContext().AttackSequence, 7);
		TestEqual(TEXT("Result preserves hit location"), FirstTarget->GetLastConfirmedContext().HitLocation, ResultContext.HitLocation);

		Weapon->ResetHitResults();
		Weapon->ApplyDamageToTargetForTest(nullptr);
		TestEqual(TEXT("Miss does not produce a result feedback path"), Weapon->GetHitResultCount(), 0);
	}

	constexpr float InitialInterval = 0.1f;
	constexpr float IntervalStep = 0.05f;
	constexpr float MaxInterval = 0.25f;
	float AdaptiveInterval = InitialInterval;
	AdaptiveInterval = AMeleeWeaponBase::CalculateNextTargetSearchInterval(
		AdaptiveInterval, false, false, InitialInterval, IntervalStep, MaxInterval);
	TestTrue(TEXT("First idle step backs off to 0.15 seconds"), FMath::IsNearlyEqual(AdaptiveInterval, 0.15f));
	AdaptiveInterval = AMeleeWeaponBase::CalculateNextTargetSearchInterval(
		AdaptiveInterval, false, false, InitialInterval, IntervalStep, MaxInterval);
	TestTrue(TEXT("Second idle step backs off to 0.20 seconds"), FMath::IsNearlyEqual(AdaptiveInterval, 0.20f));
	AdaptiveInterval = AMeleeWeaponBase::CalculateNextTargetSearchInterval(
		AdaptiveInterval, false, false, InitialInterval, IntervalStep, MaxInterval);
	TestTrue(TEXT("Third idle step reaches the 0.25 second cap"), FMath::IsNearlyEqual(AdaptiveInterval, MaxInterval));

	AdaptiveInterval = InitialInterval;
	float AdaptiveElapsed = 0.0f;
	int32 AdaptiveQueryCount = 0;
	while (AdaptiveElapsed < 2.0f)
	{
		AdaptiveElapsed += AdaptiveInterval;
		++AdaptiveQueryCount;
		AdaptiveInterval = AMeleeWeaponBase::CalculateNextTargetSearchInterval(
			AdaptiveInterval,
			false,
			false,
			InitialInterval,
			IntervalStep,
			MaxInterval);
	}

	const int32 FixedQueryCount = FMath::CeilToInt(2.0f / InitialInterval);
	TestTrue(TEXT("Idle backoff halves fixed 0.1 second timer queries over two seconds"), AdaptiveQueryCount <= FixedQueryCount / 2);
	TestTrue(TEXT("Idle backoff is capped at 0.25 seconds"), FMath::IsNearlyEqual(AdaptiveInterval, MaxInterval));
	TestTrue(TEXT("Worst-case idle target detection delay stays within 0.25 seconds"), AdaptiveInterval <= MaxInterval);
	TestEqual(
		TEXT("Acquiring a target restores the responsive interval"),
		AMeleeWeaponBase::CalculateNextTargetSearchInterval(
			AdaptiveInterval, true, true, InitialInterval, IntervalStep, MaxInterval),
		InitialInterval);
	TestEqual(
		TEXT("Losing a target also restores the responsive interval"),
		AMeleeWeaponBase::CalculateNextTargetSearchInterval(
			AdaptiveInterval, false, true, InitialInterval, IntervalStep, MaxInterval),
		InitialInterval);

	return true;
}

#endif
