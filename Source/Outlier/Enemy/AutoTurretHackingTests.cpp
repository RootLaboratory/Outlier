#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Enemy/EnemyStateTreeSchema.h"
#include "Enemy/EnemyStateTreeTurretTasks.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Components/SceneComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GameplayEffect.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/HackableInterface.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "StateTree.h"
#include "UI/HackMiniGameWidget.h"
#include "UObject/UnrealType.h"
#include "Weapon/LaserWeapon.h"
#if WITH_EDITOR
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#endif

namespace
{
struct FTurretHackingWorld
{
	UWorld* World = nullptr;
	FTurretHackingWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TurretHackingWorld")));
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
	}
	~FTurretHackingWorld()
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	void Tick(float Duration)
	{
		for (float Elapsed = 0.f; Elapsed < Duration; Elapsed += 0.02f)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.02f);
		}
	}
};

#if WITH_EDITOR
template <typename TTask>
UStateTree* MakeTurretTaskTree(FAutomationTestBase& Test, UObject* Outer, FName StateName)
{
	// Production 에셋을 수정하지 않고, 동일한 Task와 Context 바인딩을 실제 컴파일해 실행한다.
	UStateTree* Tree = NewObject<UStateTree>(Outer);
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree);
	Tree->EditorData = EditorData;
	UEnemyStateTreeSchema* Schema = NewObject<UEnemyStateTreeSchema>(EditorData);
	EditorData->Schema = Schema;
	// 테스트의 Actor Context를 실제 터렛 타입으로 좁혀 Turret 바인딩을 정적으로 검증한다.
	FindFProperty<FClassProperty>(UEnemyStateTreeSchema::StaticClass(), TEXT("ContextActorClass"))
		->SetObjectPropertyValue_InContainer(Schema, AAutoTurret::StaticClass());
	const TConstArrayView<FStateTreeExternalDataDesc> Contexts = static_cast<UStateTreeSchema*>(Schema)->GetContextDataDescs();
	const_cast<FStateTreeExternalDataDesc&>(Contexts[0]).Struct = AAutoTurret::StaticClass();
	UStateTreeState& Root = EditorData->AddSubTree(StateName);
	auto& Task = Root.AddTask<TTask>();
	Task.GetNode().Name = StateName;
	FPropertyBindingPath Source;
	Source.SetStructID(Contexts[0].ID);
	FPropertyBindingPath Target;
	Target.FromString(TEXT("Turret"));
	Target.SetStructID(Task.ID);
	EditorData->AddPropertyBinding(Source, Target);
	Root.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::Succeeded);
	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	if (!Test.TestTrue(TEXT("Transient test StateTree compiles"), Compiler.Compile(*Tree)))
	{
		for (const TSharedRef<FTokenizedMessage>& Message : Log.ToTokenizedMessages()) Test.AddInfo(Message->ToText().ToString());
		return nullptr;
	}
	return Tree;
}
#endif

ELaserWeaponPresentationPhase GetLaserPhase(const ALaserWeapon* Laser)
{
	const FStructProperty* Property = FindFProperty<FStructProperty>(ALaserWeapon::StaticClass(), TEXT("LaserPresentation"));
	return Property->ContainerPtrToValuePtr<FLaserWeaponPresentation>(Laser)->Phase;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FAutoTurretHackingTest,
	"Outlier.Enemy.Turret.Hacking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FAutoTurretHackingTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : { TEXT("NoneSameFrameDoesNotUseStun"), TEXT("PartnerNoneDuringRealLaserTask"),
		TEXT("ExistingStunClearedAtStart"), TEXT("RejectedHackPreservesStun"),
		TEXT("StateOnlyStunClearedAtStart"), TEXT("PartnerNoneClearsExistingStun"),
		TEXT("SavedAssetNoneFromBattle"), TEXT("SavedAssetNoneFromStun"), TEXT("SavedAssetQueuedStunThenNone"),
		TEXT("SavedAssetDelayedResult"), TEXT("SavedAssetStunDuringHacking"), TEXT("SavedAssetFailFromBattle"),
		TEXT("SavedAssetNoneFromHidden"), TEXT("SavedAssetRealPerceptionAfterHack"),
		TEXT("SavedAssetRifleAfterHack"), TEXT("SavedAssetRifleAfterSearch"), TEXT("SavedAssetLaserAfterSearch"),
		TEXT("SavedAssetRifleHitThenSight"), TEXT("SavedAssetRifleHitWithoutSight"),
		TEXT("SavedAssetRifleDuringBurst"), TEXT("SavedAssetRifleCloseAfterHack"), TEXT("SavedAssetRifleCombatReadiness"),
		TEXT("EnterBeforeResult"), TEXT("FailAndStaleTaskExit"), TEXT("ActualStunSuspendsCompletion"),
		TEXT("EntryWatchdogCancels"), TEXT("StopLogicCancels"), TEXT("OldAttackExitPreservesNewCharge"),
		TEXT("PartnerMiniGameStopLogicCancels"), TEXT("WaveResetRejectsOldRequester"),
		TEXT("ControllerReplacementCancels"), TEXT("ImpactPoseFreezesAndResumes") })
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FAutoTurretHackingTest::RunTest(const FString& Parameters)
{
	FTurretHackingWorld Fixture;
	const bool bSavedAsset = Parameters.StartsWith(TEXT("SavedAsset"));
	const bool bRifle = Parameters.StartsWith(TEXT("SavedAssetRifle"));
	const bool bCloseRifle = Parameters == TEXT("SavedAssetRifleCloseAfterHack");
	const bool bCombatReadiness = Parameters == TEXT("SavedAssetRifleCombatReadiness");
	const bool bHitThenSight = Parameters == TEXT("SavedAssetRifleHitThenSight");
	const bool bHitWithoutSight = Parameters == TEXT("SavedAssetRifleHitWithoutSight");
	const bool bAfterSearch = Parameters.EndsWith(TEXT("AfterSearch")) || bHitThenSight || bHitWithoutSight;
	const bool bLaserTaskFixture = Parameters == TEXT("PartnerNoneDuringRealLaserTask")
		|| Parameters == TEXT("PartnerNoneClearsExistingStun")
		|| Parameters == TEXT("OldAttackExitPreservesNewCharge");
	UClass* TurretClass = (bLaserTaskFixture || bSavedAsset) ? LoadClass<AAutoTurret>(nullptr, bRifle
		? TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C")
		: TEXT("/Game/Blueprints/Enemy/Turret/BP_LaserTurret.BP_LaserTurret_C")) : AAutoTurret::StaticClass();
	if (!TestNotNull(TEXT("Turret fixture class"), TurretClass)) return false;
	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>(TurretClass);
	if (!TestNotNull(TEXT("Turret fixture"), Turret)) return false;
	// 준비 상태 회귀는 테스트용 시야값 대신 실제 BP의 DataTable/배치 설정을 읽는다.
	if (bCombatReadiness) Turret->InitializeFromEnemyStatRow();
	Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
	Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.f);
	// BeginPlay를 생략한 격리 월드도 실제 GAS 태그 변경 → Stun 진입/해제 경로를 사용한다.
	Turret->BindGasVitalityObservers();
	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
	Turret->SetActorHiddenInGame(false);
	IHackableInterface* Hackable = Cast<IHackableInterface>(Turret);
	FActorSpawnParameters PartnerSpawn;
	PartnerSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator, PartnerSpawn);
	if (!TestNotNull(TEXT("Partner fixture"), Partner)) return false;
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	PartnerASC->InitializeForPawn(Partner);
	PartnerASC->InitializeVitalityToSelf(100.f);
	FOutlierPartnerAbilityConfig AbilityConfig;
	AbilityConfig.HackCooldown = 30.f;
	AbilityConfig.HackEffectiveRange = 3000.f;
	if (!TestTrue(TEXT("Partner hack abilities configure"), PartnerASC->ConfigurePartnerAbilities(AbilityConfig))) return false;
	UHackableComponent* TargetComponent = Hackable->GetHackableComponent();
	TargetComponent->HackTags.Reset();
	TargetComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	TargetComponent->HackTags.AddTag(HackGameplayTags::MiniGame::None());
	TargetComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::ChangeTeam());
	FHackQueryContext Query;
	Query.InstigatorActor = Partner;
	Query.RequestId = 41;
	FHackResultContext Result;
	Result.TargetActor = Turret;
	Result.InstigatorActor = Partner;
	Result.RequestId = Query.RequestId;
	Result.Result = EHackResult::Success;
	if (Parameters == TEXT("NoneSameFrameDoesNotUseStun"))
	{
		Hackable->HandleHackStarted(Query);
		TestTrue(TEXT("Start immediately selects Hacking"), Turret->GetCombatState() == EEnemyCombatState::Hacking);
		// None은 같은 프레임에 결과를 돌려준다. 그 전에 실제 Stun 상태를 빌려 쓰면 안 된다.
		TestFalse(TEXT("Hack start must not impersonate an actual Stun"),
			Turret->GetCombatState() == EEnemyCombatState::Stun);
		Hackable->DeferHackCompletion(Result, [](const FHackResultContext&) {});
		TestTrue(TEXT("Result before Task entry keeps the accepted episode"), Turret->IsHackingInProgress());
		TestFalse(TEXT("Result before Task entry does not commit team"), Turret->IsHackedToPlayerTeam());
		return true;
	}

#if WITH_EDITOR
	UStateTree* HackingTree = bSavedAsset
		? LoadObject<UStateTree>(nullptr, TEXT("/Game/Blueprints/AI/Turret/ST_AutoTurretCommon.ST_AutoTurretCommon"))
		: MakeTurretTaskTree<FEnemyTurretHackingTask>(*this, Turret, TEXT("Hacking"));
	if (!HackingTree) return false;
	UStateTreeComponent* StateTree = Turret->GetStateTreeComponent();
	StateTree->StopLogic(TEXT("Fixture setup"));
	StateTree->SetStateTree(HackingTree);
	int32 CompletionCount = 0;
	EHackResult TerminalResult = EHackResult::Success;
	const auto Completion = [&](const FHackResultContext& Terminal)
	{
		++CompletionCount;
		TerminalResult = Terminal.Result;
	};
	const auto TickTask = [&]()
	{
		// World 시간을 진행하지 않고 실제 Task 처리 경계만 실행한다.
		++GFrameCounter;
		StateTree->TickComponent(0.f, LEVELTICK_All, nullptr);
	};
	const auto CheckSuccess = [&]()
	{
		TestFalse(TEXT("Finished episode releases Hacking suppression"), Turret->IsHackingInProgress());
		TestTrue(TEXT("Successful result commits Player team"), Turret->IsHackedToPlayerTeam());
		TestTrue(TEXT("Successful commit starts the existing timed effect"), Turret->IsHackEffectActive());
		TestTrue(TEXT("Effect begins at commit with full duration"), Turret->GetHackEffectRemainingTime() > 59.f);
		TestEqual(TEXT("Terminal callback occurs exactly once"), CompletionCount, 1);
		TestTrue(TEXT("Actual Hacking tree completes its Task"), StateTree->GetStateTreeRunStatus() == EStateTreeRunStatus::Succeeded);
	};
	if (bSavedAsset)
	{
		AEnemyAIController* PerceptionController = nullptr;
		AAutoTurret* HostileTurret = nullptr;
		ARangedWeaponBase* ResumeWeapon = nullptr;
		if (Parameters == TEXT("SavedAssetRealPerceptionAfterHack") || bRifle || bAfterSearch)
		{
			// 팀 변경만 직접 호출하지 않고 실제 Hacking Task의 성공 확정 경계를 통과한다.
			PerceptionController = Fixture.World->SpawnActor<AEnemyAIController>();
			PerceptionController->Possess(Turret);
			FEnemyStat& Stat = const_cast<FEnemyStat&>(Turret->GetRuntimeStat());
			if (!bCombatReadiness)
			{
				Stat.SightRadius = Stat.LoseSightRadius = 3000.f;
				Stat.PeripheralVisionAngle = Stat.BattlePeripheralVisionAngle = 90.f;
			}
			PerceptionController->RefreshTeamAndPerceptionFromPawn();
			HostileTurret = Fixture.World->SpawnActor<AAutoTurret>();
			HostileTurret->GetOutlierAbilitySystemComponent()->InitializeForPawn(HostileTurret);
			HostileTurret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(10000.f);
			HostileTurret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
			HostileTurret->SetActorHiddenInGame(false);
			const float TargetX = bCombatReadiness ? 100000.f : bCloseRifle ? 400.f : (bHitWithoutSight || bHitThenSight) ? -6000.f : bAfterSearch ? 6000.f : 1200.f;
			HostileTurret->SetActorLocation(FVector(TargetX, bCloseRifle ? 50.f : 300.f, 0.f));
			USkeletalMesh* Head = LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/Characters/Enemy/Turret/Meshes/Laser/SKM_LaserTurretHead.SKM_LaserTurretHead"));
			HostileTurret->TurretHeadMesh->SetSkeletalMeshAsset(Head);
			HostileTurret->TurretHeadMesh->RefreshBoneTransforms();
			HostileTurret->ApplyTurretCollisionState();
			USkeletalMesh* ObserverHead = bRifle ? LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/Characters/Enemy/Turret/Meshes/Rifle/SKM_RifleTurretHead.SKM_RifleTurretHead")) : Head;
			Turret->TurretHeadMesh->SetSkeletalMeshAsset(ObserverHead);
			Turret->TurretHeadMesh->RefreshBoneTransforms();
			Turret->CacheAimOriginMuzzleSockets();
			if (bCloseRifle)
			{
				// 근거리 표적은 평행한 좌우 총열의 시차가 커진다. BP의 실제 독립 총구를
				// 사용해야 머리 조준은 끝났지만 모든 총구가 발사를 거부하는 회귀를 잡는다.
				TArray<FName> Sockets;
				Turret->GetWeaponMuzzleSocketNames(false, Sockets);
				if (!TestTrue(TEXT("Close rifle uses the Blueprint independent muzzle group"),
					Turret->UsesIndependentMuzzleShots() && Sockets.Num() >= 2)) return false;
			}
			UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr, bRifle
				? TEXT("/Game/Blueprints/Enemy/Turret/Weapon/BP_TurretRifle.BP_TurretRifle_C")
				: TEXT("/Game/Blueprints/Enemy/Turret/Weapon/BP_TurretLaser.BP_TurretLaser_C"));
			ResumeWeapon = Fixture.World->SpawnActor<ARangedWeaponBase>(WeaponClass);
			FindFProperty<FObjectPropertyBase>(WeaponClass, TEXT("WeaponBloomTable"))->SetObjectPropertyValue_InContainer(ResumeWeapon, nullptr);
			for (const TCHAR* Property : { TEXT("BloomCurrent"), TEXT("BloomMin"), TEXT("BloomMax"), TEXT("BloomPerShot") })
				FindFProperty<FFloatProperty>(WeaponClass, Property)->SetPropertyValue_InContainer(ResumeWeapon, 0.f);
			Turret->CurrentWeapon = ResumeWeapon;
			ResumeWeapon->OnEquipped(Turret);
			CastChecked<UEnemyStateTreeComponent>(StateTree)->AddLinkedStateTreeOverrides(
				FGameplayTag::RequestGameplayTag(TEXT("Enemy.StateTree.Battle")), Turret->BattleStateTreeReference);
			UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), HostileTurret);
		}
		// 실제 저장된 공용 트리의 Hidden → Deploy → Battle과 부모 전이를 그대로 실행한다.
		const bool bFromHidden = Parameters == TEXT("SavedAssetNoneFromHidden");
		if (!bFromHidden) Turret->EnterCombatFromRoom(Partner->GetActorLocation(), false, true);
		StateTree->StartLogic();
		if (!bFromHidden) Turret->SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Combat.Entered")));
		for (int32 Index = 0; Index < 3; ++Index) TickTask();
		if (!TestTrue(TEXT("Saved common tree starts the requested active branch"),
			StateTree->GetActiveStateNames().Contains(bFromHidden ? TEXT("Hidden") : TEXT("Battle")))) return false;
		if (Parameters == TEXT("SavedAssetRifleDuringBurst"))
		{
			// 이전 공격의 시야 준비만 격리한다. 해킹 후 새 적 획득에는 실제 Sight만 사용한다.
			PerceptionController->SetVisibleTargetsForTesting({ Partner });
			Turret->SetPlayerCurrentlyVisible(true);
			Turret->ObserveTrackedTarget(Partner);
			for (int32 Frame = 0; Frame < 150 && !ResumeWeapon->IsAttacking(); ++Frame)
			{
				Fixture.Tick(0.02f);
				PerceptionController->UpdateControlRotation(0.02f, false);
				StateTree->TickComponent(0.02f, LEVELTICK_All, nullptr);
			}
			if (!TestTrue(TEXT("Saved rifle tree starts a real burst before hacking"), ResumeWeapon->IsAttacking())) return false;
		}
		LoadClass<AActor>(nullptr, TEXT("/Game/GameplayCues/GC_Stun.GC_Stun_C"));
		UOutlierAbilitySystemComponent* TurretASC = Turret->GetOutlierAbilitySystemComponent();
		FActiveGameplayEffectHandle Stun;
		if (Parameters == TEXT("SavedAssetNoneFromStun") || Parameters == TEXT("SavedAssetQueuedStunThenNone"))
		{
			Stun = TurretASC->ApplyStunStateToSelf(10.f, Partner);
			if (!TestTrue(TEXT("Saved tree fixture receives actual Stun"), Stun.IsValid())) return false;
			if (Parameters == TEXT("SavedAssetNoneFromStun"))
			{
				TickTask();
				TestTrue(TEXT("Saved common tree actually enters Stun"), StateTree->GetActiveStateNames().Contains(TEXT("Stun")));
			}
		}
		if (bHitWithoutSight)
		{
			// 이 사례는 비전투 상태에서 받은 단서의 Alert 만료를 검사한다. 기존 Combat을
			// 해킹으로 없애는 동작에 기대지 않고, 해킹 전부터 비전투 조건을 명시한다.
			Turret->CombatState = EEnemyCombatState::NonCombat;
			Turret->bInCombat = false;
			PerceptionController->RefreshPerceptionConfigFromPawn();
		}
		const FEnemyStat PreHackStat = Turret->GetRuntimeStat();
		const auto GetSightConfig = [&]() -> const UAISenseConfig_Sight*
		{
			return PerceptionController ? Cast<UAISenseConfig_Sight>(PerceptionController->GetPerceptionComponent()
				->GetSenseConfig(UAISense::GetSenseID<UAISense_Sight>())) : nullptr;
		};
		const float PreHackVisionAngle = GetSightConfig() ? GetSightConfig()->PeripheralVisionAngleDegrees : 0.f;
		Hackable->HandleHackStarted(Query);
		TestTrue(TEXT("Saved tree hack immediately changes C++ state"), Turret->GetCombatState() == EEnemyCombatState::Hacking);
		if (Parameters == TEXT("SavedAssetRifleDuringBurst"))
			TestFalse(TEXT("Accepted hacking immediately cancels the old real burst"), ResumeWeapon->IsAttacking());
		if (Stun.IsValid()) TestNull(TEXT("Saved tree hack removes existing Stun effect"), TurretASC->GetActiveGameplayEffect(Stun));
		const bool bDelayed = Parameters == TEXT("SavedAssetDelayedResult") || Parameters == TEXT("SavedAssetStunDuringHacking");
		if (bDelayed)
		{
			TickTask();
			TestTrue(TEXT("Saved common tree actually enters Hacking"), StateTree->GetActiveStateNames().Contains(TEXT("Hacking")));
			TestEqual(TEXT("Saved Hacking state waits for result"), CompletionCount, 0);
		}
		if (Parameters == TEXT("SavedAssetStunDuringHacking"))
		{
			Stun = TurretASC->ApplyStunStateToSelf(0.5f, Partner);
			if (!TestTrue(TEXT("New Stun applies during Hacking"), Stun.IsValid())) return false;
			TickTask();
			TestTrue(TEXT("Saved tree gives actual Stun priority"), StateTree->GetActiveStateNames().Contains(TEXT("Stun")));
		}
		if (Parameters == TEXT("SavedAssetFailFromBattle")) Result.Result = EHackResult::Fail;
		Hackable->DeferHackCompletion(Result, Completion);
		if (Parameters == TEXT("SavedAssetStunDuringHacking"))
		{
			TestEqual(TEXT("New Stun retains result without early commit"), CompletionCount, 0);
			// 강제 Restore 호출 없이 실제 GAS 만료와 저장된 Stun 종료 전이를 검증한다.
			Fixture.Tick(0.6f);
		}
		for (int32 Index = 0; Index < 2; ++Index) TickTask();
		TestEqual(TEXT("Saved tree commits the result before advancing additional World time"), CompletionCount, 1);
		// 이 격리 월드는 StateTree를 수동 평가한다. Stun 만료 후 재진입한 경우도
		// 실제 Enter를 실행한 뒤 Ended 알림 재전달에 필요한 월드 경계만 지난다.
		Fixture.Tick(0.02f);
		for (int32 Index = 0; Index < 4; ++Index) TickTask();
		TestEqual(TEXT("Saved tree delivers one terminal result"), CompletionCount, 1);
		TestFalse(TEXT("Saved tree releases hacking suppression"), Turret->IsHackingInProgress());
		TestEqual(TEXT("Saved tree commits only successful hacks"), Turret->IsHackedToPlayerTeam(), Result.Result == EHackResult::Success);
		TestTrue(TEXT("Saved common tree keeps running"), StateTree->GetStateTreeRunStatus() == EStateTreeRunStatus::Running);
		TestTrue(TEXT("Saved common tree reselects Battle after Hacking"), StateTree->GetActiveStateNames().Contains(TEXT("Battle")));
		TestFalse(TEXT("Saved common tree does not return to Hidden"), StateTree->GetActiveStateNames().Contains(TEXT("Hidden")));
		TestFalse(TEXT("Saved common tree leaves Hacking"), StateTree->GetActiveStateNames().Contains(TEXT("Hacking")));
		TestFalse(TEXT("Saved common tree leaves Stun"), StateTree->GetActiveStateNames().Contains(TEXT("Stun")));
		const bool bExpectedCombat = !bFromHidden && !bHitWithoutSight;
		TestEqual(TEXT("Hacking preserves existing combat readiness"), Turret->IsInCombat(), bExpectedCombat);
		TestTrue(TEXT("Hacking restores the matching combat state"), Turret->GetCombatState()
			== (bExpectedCombat ? EEnemyCombatState::Combat : EEnemyCombatState::NonCombat));
		if (!PerceptionController || bCombatReadiness)
		{
			const FRotator SearchBefore = Turret->TurretHeadPivot->GetRelativeRotation();
			for (int32 Frame = 0; Frame < 50; ++Frame)
			{
				Fixture.Tick(0.02f);
				StateTree->TickComponent(0.02f, LEVELTICK_All, nullptr);
			}
			TestTrue(TEXT("Saved tree keeps searching without a target"),
				!Turret->TurretHeadPivot->GetRelativeRotation().Equals(SearchBefore, 0.1f));
			TestTrue(TEXT("No-target search keeps the saved tree running"), StateTree->GetStateTreeRunStatus() == EStateTreeRunStatus::Running);
		}
		if (bCombatReadiness)
		{
			TestTrue(TEXT("Configured rifle has distinct combat and noncombat sight angles"),
				PreHackStat.BattlePeripheralVisionAngle > PreHackStat.PeripheralVisionAngle);
			TestEqual(TEXT("Pre-hack listener uses the configured combat angle"), PreHackVisionAngle, PreHackStat.BattlePeripheralVisionAngle);
			TestEqual(TEXT("Hacking preserves configured sight radius"), Turret->GetRuntimeStat().SightRadius, PreHackStat.SightRadius);
			TestEqual(TEXT("Hacking preserves configured noncombat angle"), Turret->GetRuntimeStat().PeripheralVisionAngle, PreHackStat.PeripheralVisionAngle);
			TestEqual(TEXT("Hacking preserves configured combat angle"), Turret->GetRuntimeStat().BattlePeripheralVisionAngle, PreHackStat.BattlePeripheralVisionAngle);
			if (!TestNotNull(TEXT("Restored real sight listener"), GetSightConfig())) return false;
			TestEqual(TEXT("Team change keeps the same active sight angle"), GetSightConfig()->PeripheralVisionAngleDegrees, PreHackVisionAngle);
			TestFalse(TEXT("Distant hostile remains outside sight"), Turret->IsPlayerCurrentlyVisible());
			return true;
		}
		if (PerceptionController)
		{
			UAIPerceptionComponent* PerceptionComponent = PerceptionController->GetPerceptionComponent();
			TestTrue(TEXT("Hack finalization reactivates perception"), PerceptionComponent->IsActive());
			UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
			bool bResumedCharge = false;
			const float HealthBefore = HostileTurret->GetCurrentHealth();
			const FRotator AimBefore = Turret->TurretHeadPivot->GetRelativeRotation();
			bool bRotated = false;
			bool bSearchRotated = false;
			for (int32 Frame = 0; Frame < 600; ++Frame)
			{
				if (bAfterSearch && Frame == 50)
				{
					bSearchRotated = bRotated;
					TestFalse(TEXT("Search cannot perceive the distant turret"), Turret->IsPlayerCurrentlyVisible());
					if (bHitThenSight || bHitWithoutSight)
					{
						if (bHitThenSight)
						{
							// 전투 시야를 보존해도 피격 전에는 범위 밖이다. 피격과 함께 후방 감지 범위로 들어온다.
							HostileTurret->SetActorLocation(FVector(-1200.f, 300.f, 0.f));
							HostileTurret->TurretHeadMesh->UpdateBounds();
						}
						// 보이는 적을 주입하지 않고 후방의 실제 피해 출처로 회전시킨다.
						FOutlierDamageRequest Hit;
						Hit.DamageAmount = 1.f;
						Hit.DamageTag = OutlierGameplayTags::Damage::Ranged();
						Hit.bHasDamageOrigin = true;
						Hit.DamageOrigin = HostileTurret->GetPawnViewLocation();
						Hit.DamageCauser = HostileTurret;
						TestTrue(TEXT("Hostile rear hit applies real damage"), Turret->ReceiveOutlierDamage(Hit) > 0.f);
					}
					else
					{
						HostileTurret->SetActorLocation(FVector(1200.f, 300.f, 0.f));
						HostileTurret->TurretHeadMesh->UpdateBounds();
					}
				}
				Fixture.Tick(0.02f);
				// BeginPlay 없는 격리 월드에서는 Controller의 기존 회전/피격 갱신도 명시적으로 진행한다.
				PerceptionController->UpdateControlRotation(0.02f, false);
				Perception->Tick(0.02f);
				StateTree->TickComponent(0.02f, LEVELTICK_All, nullptr);
				if (const ALaserWeapon* Laser = Cast<ALaserWeapon>(ResumeWeapon))
					bResumedCharge |= GetLaserPhase(Laser) == ELaserWeaponPresentationPhase::Telegraph;
				bRotated |= !Turret->TurretHeadPivot->GetRelativeRotation().Equals(AimBefore, 0.1f);
				if (HostileTurret->GetCurrentHealth() < HealthBefore) break;
			}
			TArray<AActor*> SeenActors;
			PerceptionComponent->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), SeenActors);
			if (bHitWithoutSight)
			{
				TestTrue(TEXT("No-target search rotates before the hit"), bSearchRotated);
				TestFalse(TEXT("Distant rear attacker remains unseen"), SeenActors.Contains(HostileTurret));
				TestTrue(TEXT("Hit without sight eventually releases Alert"), Turret->GetCombatState() == EEnemyCombatState::NonCombat);
				TestFalse(TEXT("Expired hit releases direction confirmation"), Turret->HasHitResponse());
				TestFalse(TEXT("Expired hit releases guard rotation"), Turret->IsTurretGuardRotationActive());
				TestTrue(TEXT("After guard expiry the saved tree keeps searching"), StateTree->GetActiveStateNames().Contains(TEXT("Idle")));
				return true;
			}
			TestTrue(TEXT("Real Sight acquires a hostile turret after the completed hack"), SeenActors.Contains(HostileTurret));
			TestTrue(TEXT("Real perception restores a tracked hostile target"), Turret->GetTrackedTarget() == HostileTurret);
			TestFalse(TEXT("Real perception leaves the idle fallback"), StateTree->GetActiveStateNames().Contains(TEXT("Idle")));
			if (!bRifle) TestTrue(TEXT("Saved common and linked sniper tree resume actual laser charging"), bResumedCharge);
			if (bAfterSearch) TestTrue(TEXT("Without a target the saved tree keeps searching by rotation"), bSearchRotated);
			TestTrue(TEXT("New hostile target resumes head rotation"), bRotated);
			AddInfo(FString::Printf(TEXT("Resume boundary Rifle=%d States=%s CombatState=%d Block=%s TargetHP=%.2f AimReady=%d CanAttackTarget=%d"),
				bRifle, *FString::JoinBy(StateTree->GetActiveStateNames(), TEXT("/"), [](FName Name) { return Name.ToString(); }), int32(Turret->GetCombatState()),
				ResumeWeapon->GetAttackBlockReason(), HostileTurret->GetCurrentHealth(), Turret->IsTurretAttackAimReady(HostileTurret),
				Turret->CanAttackTrackedTarget(HostileTurret)));
			if (bCloseRifle)
			{
				const FVector AimPoint = Turret->GetCombatAimPoint(HostileTurret);
				TArray<FName> Sockets;
				Turret->GetWeaponMuzzleSocketNames(false, Sockets);
				for (FName Socket : Sockets)
				{
					const FTransform Shot = Turret->TurretHeadMesh->GetSocketTransform(Socket);
					const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						FVector::DotProduct((AimPoint - Shot.GetLocation()).GetSafeNormal(), Shot.GetUnitAxis(EAxis::X)), -1.0, 1.0)));
					AddInfo(FString::Printf(TEXT("Close rifle muzzle=%s origin=%s aim=%s error=%.3f tolerance=%.3f headReady=%d"),
						*Socket.ToString(), *Shot.GetLocation().ToCompactString(), *AimPoint.ToCompactString(), Angle,
						Turret->GetTurretBehavior().AimToleranceDegrees, Turret->IsTurretAttackAimReady(HostileTurret)));
				}
			}
			TestTrue(TEXT("Resumed saved tree weapon deals authoritative damage"), HostileTurret->GetCurrentHealth() < HealthBefore);
		}
		if (Parameters == TEXT("SavedAssetDelayedResult"))
		{
			Turret->EnterCombatFromRoom(Partner->GetActorLocation(), false, true);
			Turret->SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Combat.Entered")));
			for (int32 Index = 0; Index < 3; ++Index) TickTask();
			TestTrue(TEXT("New combat information wakes the saved tree"), StateTree->GetActiveStateNames().Contains(TEXT("Battle")));
			TestFalse(TEXT("New combat information exits Idle"), StateTree->GetActiveStateNames().Contains(TEXT("Idle")));
		}
		return true;
	}
	if (Parameters == TEXT("ExistingStunClearedAtStart") || Parameters == TEXT("RejectedHackPreservesStun")
		|| Parameters == TEXT("StateOnlyStunClearedAtStart"))
	{
		LoadClass<AActor>(nullptr, TEXT("/Game/GameplayCues/GC_Stun.GC_Stun_C"));
		UOutlierAbilitySystemComponent* TurretASC = Turret->GetOutlierAbilitySystemComponent();
		const FGameplayTag StunTag = OutlierGameplayTags::State::Stunned();
		const FActiveGameplayEffectHandle Immune = TurretASC->ApplyDamageImmuneStateToSelf();
		UGameplayEffect* AssetTaggedEffect = NewObject<UGameplayEffect>();
		AssetTaggedEffect->DurationPolicy = EGameplayEffectDurationType::Infinite;
		FGameplayEffectSpec AssetTaggedSpec(AssetTaggedEffect, TurretASC->MakeEffectContext(), 1.f);
		AssetTaggedSpec.AddDynamicAssetTag(StunTag);
		const FActiveGameplayEffectHandle AssetTagged = TurretASC->ApplyGameplayEffectSpecToSelf(AssetTaggedSpec);
		TestTrue(TEXT("Unrelated effects are active"), Immune.IsValid() && AssetTagged.IsValid());
		Turret->CombatState = EEnemyCombatState::Combat;
		Turret->bInCombat = true;
		const bool bStateOnly = Parameters == TEXT("StateOnlyStunClearedAtStart");
		FActiveGameplayEffectHandle FirstStun, SecondStun;
		if (bStateOnly)
		{
			Turret->EnterStun();
		}
		else
		{
			FirstStun = TurretASC->ApplyStunStateToSelf(0.3f, Partner);
			SecondStun = TurretASC->ApplyStunStateToSelf(0.5f, Turret);
			TestTrue(TEXT("Both existing stun sources are active"), FirstStun.IsValid() && SecondStun.IsValid());
			TestEqual(TEXT("Both effects contribute the stun tag"), TurretASC->GetGameplayTagCount(StunTag), 2);
			TestTrue(TEXT("Existing stun owns its shared cue"),
				TurretASC->IsGameplayCueActive(OutlierGameplayTags::Cue::Status::Stun()));
		}
		TestTrue(TEXT("Fixture starts in Stun"), Turret->GetCombatState() == EEnemyCombatState::Stun);
		if (Parameters == TEXT("RejectedHackPreservesStun"))
		{
			Query.RequestId = 0;
			Hackable->HandleHackStarted(Query);
			TestFalse(TEXT("Rejected request does not create a hacking episode"), Turret->IsHackingInProgress());
			TestEqual(TEXT("Rejected request preserves both stun tags"), TurretASC->GetGameplayTagCount(StunTag), 2);
			TestTrue(TEXT("Rejected request preserves both effects"),
				TurretASC->GetActiveGameplayEffect(FirstStun) && TurretASC->GetActiveGameplayEffect(SecondStun));
			TestTrue(TEXT("Rejected request stays stunned"), Turret->GetCombatState() == EEnemyCombatState::Stun);
			return true;
		}
		Hackable->HandleHackStarted(Query);
		TestTrue(TEXT("Accepted request immediately enters Hacking"),
			Turret->IsHackingInProgress() && Turret->GetCombatState() == EEnemyCombatState::Hacking);
		TestEqual(TEXT("Hack start removes every existing stun tag"), TurretASC->GetGameplayTagCount(StunTag), 0);
		TestFalse(TEXT("Hack start removes the old shared stun cue"),
			TurretASC->IsGameplayCueActive(OutlierGameplayTags::Cue::Status::Stun()));
		TestFalse(TEXT("Stun cleanup does not restore the old combat flag"), Turret->bInCombat);
		if (!bStateOnly)
		{
			TestNull(TEXT("First existing stun GE is removed"), TurretASC->GetActiveGameplayEffect(FirstStun));
			TestNull(TEXT("Second existing stun GE is removed"), TurretASC->GetActiveGameplayEffect(SecondStun));
		}
		TestNotNull(TEXT("Hack preserves unrelated immunity"), TurretASC->GetActiveGameplayEffect(Immune));
		TestNotNull(TEXT("Hack preserves asset-tag-only effect"), TurretASC->GetActiveGameplayEffect(AssetTagged));
		Hackable->DeferHackCompletion(Result, Completion);
		TestEqual(TEXT("Stun cleanup cannot commit before Task Enter"), CompletionCount, 0);
		StateTree->StartLogic();
		TickTask();
		CheckSuccess();
		Fixture.Tick(0.7f);
		TestEqual(TEXT("Old stun expiration cannot restore Stun"), TurretASC->GetGameplayTagCount(StunTag), 0);
		TestTrue(TEXT("Old stun expiration preserves completed state"),
			Turret->GetCombatState() != EEnemyCombatState::Stun && Turret->GetCombatState() != EEnemyCombatState::Hacking);
		TestEqual(TEXT("Old expiration cannot complete twice"), CompletionCount, 1);
		return true;
	}
	if (Parameters == TEXT("PartnerMiniGameStopLogicCancels"))
	{
		UPartnerHackComponent* PartnerHack = Partner->FindComponentByClass<UPartnerHackComponent>();
		if (!TestNotNull(TEXT("Actual Partner hack component"), PartnerHack)) return false;
		PartnerHack->RefreshCharacterRefsFromPlayerState();
		FPartnerHackAbilityData HackData;
		HackData.EffectiveRange = 3000.f;
		PartnerHack->CacheAbilityData(HackData);
		TargetComponent->HackTags.RemoveTag(HackGameplayTags::MiniGame::None());
		TargetComponent->HackTags.AddTag(HackGameplayTags::MiniGame::ClickCircle());
		PartnerHack->OnHackFinished.AddLambda([&](EHackResult FinishedResult, bool)
		{
			++CompletionCount;
			TerminalResult = FinishedResult;
		});
		PartnerHack->bHackCandidateSearchActive = true;
		PartnerHack->ServerHeldHackActor = Turret;
		PartnerHack->ServerHoldId = 8;
		PartnerHack->ServerTryStartHack_Implementation(Turret, 8);
		if (!TestTrue(TEXT("Actual mini-game start owns a request"), PartnerHack->ActiveHackRequestId != 0)) return false;
		const uint32 PartnerRequest = PartnerHack->ActiveHackRequestId;
		StateTree->StartLogic();
		TestTrue(TEXT("Real Task waits for mini-game result"), Turret->IsHackingInProgress());
		StateTree->StopLogic(TEXT("Automation interrupt before mini-game result"));
		TestFalse(TEXT("StopLogic releases target Hacking"), Turret->IsHackingInProgress());
		TestEqual(TEXT("Partner receives cancellation at StopLogic boundary"), CompletionCount, 1);
		TestTrue(TEXT("Partner terminal result is Cancelled"), TerminalResult == EHackResult::Cancelled);
		TestEqual(TEXT("Partner releases active request without a UI result"), PartnerHack->ActiveHackRequestId, 0u);
		TestNull(TEXT("Partner releases active target"), PartnerHack->ActiveHackableComponent.Get());
		Result.RequestId = PartnerRequest;
		PartnerHack->ServerCompleteHack_Implementation(Result);
		TestEqual(TEXT("Late UI success cannot complete again"), CompletionCount, 1);
		TestFalse(TEXT("Late UI success cannot ally the turret"), Turret->IsHackedToPlayerTeam());
		PartnerHack->bHackCandidateSearchActive = true;
		PartnerHack->ServerHeldHackActor = Turret;
		PartnerHack->ServerHoldId = 9;
		PartnerHack->ServerTryStartHack_Implementation(Turret, 9);
		const uint32 NewPartnerRequest = PartnerHack->ActiveHackRequestId;
		if (!TestTrue(TEXT("Same Actor new request owns a distinct ID"),
			NewPartnerRequest != 0 && NewPartnerRequest != PartnerRequest)) return false;
		// 실제 로컬 handler의 파괴 경계를 검증한다. viewport/UI 입력 자체는 이 fixture 범위 밖이다.
		UHackMiniGameWidget* CurrentWidget = NewObject<UHackMiniGameWidget>(PartnerHack);
		CurrentWidget->SetHackRequestId(NewPartnerRequest);
		PartnerHack->HackMiniGameWidget = CurrentWidget;
		PartnerHack->HandleHackMiniGameFinished(Result);
		TestTrue(TEXT("Old UI result preserves new request widget"), PartnerHack->HackMiniGameWidget == CurrentWidget);
		TestEqual(TEXT("Old UI result preserves new active request"), PartnerHack->ActiveHackRequestId, NewPartnerRequest);
		PartnerHack->ServerCompleteHack_Implementation(Result);
		TestEqual(TEXT("Old server result cannot complete same Actor new request"), CompletionCount, 1);
		TestEqual(TEXT("Old server result preserves current request ID"), PartnerHack->ActiveHackRequestId, NewPartnerRequest);
		TestTrue(TEXT("Old server result leaves new target episode active"), Turret->IsHackingInProgress());
		PartnerHack->CancelForReboot();
		return true;
	}
	if (Parameters == TEXT("ControllerReplacementCancels"))
	{
		AEnemyAIController* FirstController = Fixture.World->SpawnActor<AEnemyAIController>();
		FirstController->Possess(Turret);
		Hackable->HandleHackStarted(Query);
		const uint32 FirstEpisode = Turret->GetHackingRequestId();
		Hackable->DeferHackCompletion(Result, Completion);
		FirstController->UnPossess();
		TestFalse(TEXT("Actual unpossession cancels pending episode"), Turret->IsHackingInProgress());
		TestEqual(TEXT("Controller loss returns one terminal callback"), CompletionCount, 1);
		TestTrue(TEXT("Controller loss result is Cancelled"), TerminalResult == EHackResult::Cancelled);
		TestFalse(TEXT("Controller loss cannot commit queued Success"), Turret->IsHackedToPlayerTeam());
		AEnemyAIController* SecondController = Fixture.World->SpawnActor<AEnemyAIController>();
		SecondController->Possess(Turret);
		Query.RequestId = 42;
		Hackable->HandleHackStarted(Query);
		TestTrue(TEXT("Replacement Controller accepts a distinct episode"),
			Turret->IsHackingInProgress() && Turret->GetHackingRequestId() != FirstEpisode);
		int32 RejectedCallbacks = 0;
		Hackable->DeferHackCompletion(Result, [&](const FHackResultContext& Rejected)
		{
			++RejectedCallbacks;
			TestTrue(TEXT("Old Controller result is rejected"), Rejected.Result == EHackResult::Cancelled);
		});
		TestEqual(TEXT("Old result receives a single rejection"), RejectedCallbacks, 1);
		TestTrue(TEXT("Old result leaves replacement episode active"), Turret->IsHackingInProgress());
		SecondController->UnPossess();
		return true;
	}
	if (Parameters == TEXT("ImpactPoseFreezesAndResumes"))
	{
		Turret->bEnableImpactReaction = true;
		Turret->ApplyExplosionReaction(Turret->GetActorLocation() - FVector(100.f, 100.f, 0.f), 1.f, 1.f, 1.f);
		const FRotator ImpactBefore = Turret->ImpactRotationOffset;
		const FRotator PoseBefore = Turret->TurretHeadPivot->GetRelativeRotation();
		if (!TestFalse(TEXT("Actual explosion creates a nonzero impact pose"), ImpactBefore.IsNearlyZero())) return false;
		Hackable->HandleHackStarted(Query);
		StateTree->StartLogic();
		Fixture.Tick(0.6f);
		TestTrue(TEXT("Hacking holds existing impact offset"), Turret->ImpactRotationOffset.Equals(ImpactBefore));
		TestTrue(TEXT("Hacking holds head pose"), Turret->TurretHeadPivot->GetRelativeRotation().Equals(PoseBefore));
		Turret->ApplyExplosionReaction(Turret->GetActorLocation() + FVector(100.f, 100.f, 0.f), 1.f, 1.f, 1.f);
		TestTrue(TEXT("New explosion does not rotate a hacking head"), Turret->ImpactRotationOffset.Equals(ImpactBefore));
		Result.Result = EHackResult::Fail;
		Hackable->DeferHackCompletion(Result, Completion);
		TickTask();
		Fixture.Tick(0.6f);
		TestTrue(TEXT("Impact recovery resumes after Hacking releases pose"),
			Turret->ImpactRotationOffset.GetManhattanDistance(FRotator::ZeroRotator)
			< ImpactBefore.GetManhattanDistance(FRotator::ZeroRotator));
		return true;
	}

	if (bLaserTaskFixture)
	{
		UClass* WeaponClass = LoadClass<ALaserWeapon>(nullptr,
			TEXT("/Game/Blueprints/Enemy/Turret/Weapon/BP_TurretLaser.BP_TurretLaser_C"));
		USkeletalMesh* Head = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Enemy/Turret/Meshes/Laser/SKM_LaserTurretHead.SKM_LaserTurretHead"));
		if (!TestNotNull(TEXT("Production laser turret BP"), TurretClass)
			|| !TestNotNull(TEXT("Production laser weapon BP"), WeaponClass)
			|| !TestNotNull(TEXT("Production laser head sockets"), Head)) return false;
		// 실제 BP의 lifecycle만 fixture에서 활성화한다. production StateTree 연결 검증과 구분한다.
		ALaserWeapon* Laser = Fixture.World->SpawnActor<ALaserWeapon>(WeaponClass);
		Turret->TurretHeadMesh->SetSkeletalMeshAsset(Head);
		Turret->TurretHeadMesh->RefreshBoneTransforms();
		Turret->CacheAimOriginMuzzleSockets();
		Turret->CurrentWeapon = Laser;
		Laser->OnEquipped(Turret);
		Partner->SetActorLocation(FVector(1000.f, 0.f, 0.f));
		AEnemyAIController* AttackController = Fixture.World->SpawnActor<AEnemyAIController>();
		AttackController->Possess(Turret);
		Turret->EnterCombatFromRoom(Partner->GetActorLocation(), false, true);
		AttackController->SetVisibleTargetsForTesting({ Partner });
		Turret->SetPlayerCurrentlyVisible(true);
		Turret->ObserveTrackedTarget(Partner);
		UStateTree* AttackTree = MakeTurretTaskTree<FEnemyTurretSniperShotTask>(*this, Turret, TEXT("LaserAttack"));
		if (!AttackTree) return false;
		FStateTreeInstanceData OldAttackData;
		FStateTreeExecutionContext OldAttack(*Turret, *AttackTree, OldAttackData);
		OldAttack.SetContextDataByName(TEXT("Actor"), FStateTreeDataView(Turret));
		TestTrue(TEXT("Real sniper Task starts"), OldAttack.Start() == EStateTreeRunStatus::Running);
		if (!TestTrue(TEXT("Real sniper Enter creates charge presentation"),
			GetLaserPhase(Laser) == ELaserWeaponPresentationPhase::Telegraph)) return false;
		const int32 AmmoBefore = Laser->GetCurrentAmmo();
		const float HealthBefore = Partner->GetVitalAttributeSet()->GetHealth();

		if (Parameters == TEXT("PartnerNoneDuringRealLaserTask") || Parameters == TEXT("PartnerNoneClearsExistingStun"))
		{
			UOutlierAbilitySystemComponent* TurretASC = Turret->GetOutlierAbilitySystemComponent();
			FActiveGameplayEffectHandle ExistingStun;
			if (Parameters == TEXT("PartnerNoneClearsExistingStun"))
			{
				LoadClass<AActor>(nullptr, TEXT("/Game/GameplayCues/GC_Stun.GC_Stun_C"));
				ExistingStun = TurretASC->ApplyStunStateToSelf(10.f, Partner);
				if (!TestTrue(TEXT("Real laser turret begins stunned"), ExistingStun.IsValid()
					&& Turret->GetCombatState() == EEnemyCombatState::Stun)) return false;
			}
			UPartnerHackComponent* PartnerHack = Partner->FindComponentByClass<UPartnerHackComponent>();
			if (!TestNotNull(TEXT("Actual Partner hack component"), PartnerHack)) return false;
			PartnerHack->RefreshCharacterRefsFromPlayerState();
			FPartnerHackAbilityData HackData;
			HackData.EffectiveRange = 3000.f;
			PartnerHack->CacheAbilityData(HackData);
			PartnerHack->OnHackFinished.AddLambda([&](EHackResult FinishedResult, bool)
			{
				++CompletionCount;
				TerminalResult = FinishedResult;
			});
			// 조준 UI/홀드 시간만 fixture로 대체하고, 실제 서버 None 분기는 그대로 실행한다.
			PartnerHack->bHackCandidateSearchActive = true;
			PartnerHack->ServerHeldHackActor = Turret;
			PartnerHack->ServerHoldId = 7;
			const double BeforeTime = Fixture.World->GetTimeSeconds();
			PartnerHack->ServerTryStartHack_Implementation(Turret, 7);
			TestTrue(TEXT("Actual None branch retains deferred completion"), PartnerHack->bHackCompletionPending);
			TestTrue(TEXT("Accepted None immediately suppresses turret"), Turret->IsHackingInProgress());
			TestTrue(TEXT("None immediately enters Hacking without waiting for Stun"),
				Turret->GetCombatState() == EEnemyCombatState::Hacking);
			TestFalse(TEXT("None does not restore the old combat flag"), Turret->bInCombat);
			TestFalse(TEXT("None leaves no old stun tag"), TurretASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stunned()));
			if (ExistingStun.IsValid()) TestNull(TEXT("None removes old stun effect"), TurretASC->GetActiveGameplayEffect(ExistingStun));
			TestTrue(TEXT("None cancels actual charging before StateTree entry"), GetLaserPhase(Laser) == ELaserWeaponPresentationPhase::Idle);
			TestEqual(TEXT("Partner terminal callback waits for real Task entry"), CompletionCount, 0);
			TestFalse(TEXT("No early team commit"), Turret->IsHackedToPlayerTeam());
			StateTree->StartLogic();
			TickTask();
			CheckSuccess();
			TestTrue(TEXT("Partner receives Success"), TerminalResult == EHackResult::Success);
			TestFalse(TEXT("Partner releases its deferred result"), PartnerHack->bHackCompletionPending);
			TestEqual(TEXT("No artificial World-time wait"), Fixture.World->GetTimeSeconds(), BeforeTime);
			OldAttack.Tick(10.f);
			OldAttack.Stop();
			TestEqual(TEXT("Cancelled old Task cannot consume ammo"), Laser->GetCurrentAmmo(), AmmoBefore);
			TestEqual(TEXT("Cancelled old Task cannot damage its stored target"), Partner->GetVitalAttributeSet()->GetHealth(), HealthBefore);
			return true;
		}

		Hackable->HandleHackStarted(Query);
		TestTrue(TEXT("Hack start cancels charge without old Task Exit"), GetLaserPhase(Laser) == ELaserWeaponPresentationPhase::Idle);
		Result.Result = EHackResult::Fail;
		Hackable->DeferHackCompletion(Result, Completion);
		StateTree->StartLogic();
		TickTask();
		TestFalse(TEXT("Failed hack releases suppression"), Turret->IsHackingInProgress());
		Turret->EnterCombatFromRoom(Partner->GetActorLocation(), false, true);
		AttackController->SetVisibleTargetsForTesting({ Partner });
		Turret->SetPlayerCurrentlyVisible(true);
		Turret->ObserveTrackedTarget(Partner);
		FStateTreeInstanceData NewAttackData;
		FStateTreeExecutionContext NewAttack(*Turret, *AttackTree, NewAttackData);
		NewAttack.SetContextDataByName(TEXT("Actor"), FStateTreeDataView(Turret));
		TestTrue(TEXT("New sniper Task begins a full charge"), NewAttack.Start() == EStateTreeRunStatus::Running);
		TestTrue(TEXT("New execution is charging"), GetLaserPhase(Laser) == ELaserWeaponPresentationPhase::Telegraph);
		OldAttack.Tick(10.f);
		OldAttack.Stop();
		TestTrue(TEXT("Stale actual Task Tick/Exit preserves new charge"), GetLaserPhase(Laser) == ELaserWeaponPresentationPhase::Telegraph);
		TestTrue(TEXT("Stale Exit preserves new attack phase"), Turret->GetAttackPhase() == EEnemyAttackPhase::Telegraph);
		TestEqual(TEXT("Old stored shot adds no damage"), Partner->GetVitalAttributeSet()->GetHealth(), HealthBefore);
		NewAttack.Stop();
		return true;
	}

	Hackable->HandleHackStarted(Query);
	const uint32 FirstEpisode = Turret->GetHackingRequestId();
	if (!TestTrue(TEXT("Accepted hack creates a nonzero target episode"), FirstEpisode != 0)) return false;
	if (Parameters == TEXT("WaveResetRejectsOldRequester"))
	{
		Hackable->DeferHackCompletion(Result, Completion);
		Turret->PrepareForRoomWaveActivation();
		TestFalse(TEXT("Wave reset cancels pending episode"), Turret->IsHackingInProgress());
		TestEqual(TEXT("Wave reset returns one terminal result"), CompletionCount, 1);
		TestTrue(TEXT("Wave reset result is Cancelled"), TerminalResult == EHackResult::Cancelled);
		TestFalse(TEXT("Wave reset cannot commit queued team change"), Turret->IsHackedToPlayerTeam());
		Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
		APartnerCharacter* NewPartner = Fixture.World->SpawnActor<APartnerCharacter>(FVector(1000.f, 500.f, 0.f), FRotator::ZeroRotator, PartnerSpawn);
		NewPartner->GetOutlierAbilitySystemComponent()->InitializeForPawn(NewPartner);
		NewPartner->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.f);
		Query.InstigatorActor = NewPartner;
		// 새 요청자가 같은 Partner ID를 써도 대상의 회차와 요청자 검증은 분리된다.
		Hackable->HandleHackStarted(Query);
		const uint32 SecondEpisode = Turret->GetHackingRequestId();
		TestTrue(TEXT("Reactivated Actor allocates a distinct episode"), SecondEpisode != FirstEpisode);
		int32 RejectedCallbacks = 0;
		Hackable->DeferHackCompletion(Result, [&](const FHackResultContext& Rejected)
		{
			++RejectedCallbacks;
			TestTrue(TEXT("Old requester result is rejected"), Rejected.Result == EHackResult::Cancelled);
		});
		TestEqual(TEXT("Old requester gets one rejection"), RejectedCallbacks, 1);
		TestTrue(TEXT("Same Partner ID does not finish a new requester episode"), Turret->IsHackingInProgress());
		TestFalse(TEXT("Old Task acknowledgement cannot enter a new lease"), Turret->EnterHackingState(FirstEpisode));
		Turret->LeaveHackingState(FirstEpisode);
		TestTrue(TEXT("Old Task Exit cannot cancel reactivated episode"), Turret->IsHackingInProgress());
		Turret->PrepareForRoomWaveActivation();
		return true;
	}
	if (Parameters == TEXT("EntryWatchdogCancels"))
	{
		Hackable->DeferHackCompletion(Result, Completion);
		Fixture.Tick(1.2f);
		TestFalse(TEXT("Missing Task entry has finite recovery"), Turret->IsHackingInProgress());
		TestTrue(TEXT("Watchdog closes as Cancelled"), TerminalResult == EHackResult::Cancelled);
		TestEqual(TEXT("Watchdog returns one terminal callback"), CompletionCount, 1);
		TestFalse(TEXT("Watchdog never forces Success"), Turret->IsHackedToPlayerTeam());
		StateTree->StartLogic();
		TickTask();
		TestFalse(TEXT("Late Task Enter does not resurrect episode"), Turret->IsHackingInProgress());
		return true;
	}
	StateTree->StartLogic();
	TestTrue(TEXT("Actual Hacking Task waits for result"), StateTree->GetStateTreeRunStatus() == EStateTreeRunStatus::Running);
#if WITH_GAMEPLAY_DEBUGGER
	TestTrue(TEXT("Actual active StateTree path contains Hacking"), StateTree->GetActiveStateNames().Contains(TEXT("Hacking")));
#endif
	if (Parameters == TEXT("ActualStunSuspendsCompletion"))
	{
		// 비동기 cue가 파괴된 테스트 월드에 뒤늦게 생성되지 않도록 실제 Stun cue를 먼저 로드한다.
		LoadClass<AActor>(nullptr, TEXT("/Game/GameplayCues/GC_Stun.GC_Stun_C"));
		UOutlierAbilitySystemComponent* TurretASC = Turret->GetOutlierAbilitySystemComponent();
		const FActiveGameplayEffectHandle Stun = TurretASC->ApplyStunStateToSelf(10.f, Partner);
		if (!TestTrue(TEXT("Actual GAS Stun effect applied"), Stun.IsValid())) return false;
		TestTrue(TEXT("Actual Stun has priority over Hacking"), Turret->GetCombatState() == EEnemyCombatState::Stun);
		// 최소 트리는 production Stun 전이가 없으므로 StopLogic으로 그 State Exit 경계만 실행한다.
		StateTree->StopLogic(TEXT("Fixture actual Stun transition"));
		Hackable->DeferHackCompletion(Result, Completion);
		Fixture.Tick(1.2f);
		TestTrue(TEXT("Stun wait does not consume entry watchdog"), Turret->IsHackingInProgress());
		TestEqual(TEXT("Stun retains deferred terminal result"), CompletionCount, 0);
		TestFalse(TEXT("Stun never commits team early"), Turret->IsHackedToPlayerTeam());
		TestTrue(TEXT("Hacking does not clear actual GAS Stun"), TurretASC->GetActiveGameplayEffect(Stun) != nullptr);
		TurretASC->RemoveActiveGameplayEffect(Stun);
		TestTrue(TEXT("GAS Stun removal resumes Hacking"), Turret->GetCombatState() == EEnemyCombatState::Hacking);
		StateTree->StartLogic();
		TickTask();
		CheckSuccess();
		return true;
	}
	if (Parameters == TEXT("StopLogicCancels"))
	{
		StateTree->StopLogic(TEXT("Automation unexpected stop"));
		TestFalse(TEXT("StopLogic closes entered pending episode"), Turret->IsHackingInProgress());
		Hackable->DeferHackCompletion(Result, Completion);
		TestEqual(TEXT("StopLogic emits one cancellation"), CompletionCount, 1);
		TestTrue(TEXT("StopLogic terminal result is Cancelled"), TerminalResult == EHackResult::Cancelled);
		TestFalse(TEXT("StopLogic cannot commit queued Success"), Turret->IsHackedToPlayerTeam());
		return true;
	}
	if (Parameters == TEXT("FailAndStaleTaskExit"))
	{
		FStateTreeInstanceData OldData;
		FStateTreeExecutionContext OldTask(*Turret, *HackingTree, OldData);
		OldTask.SetContextDataByName(TEXT("Actor"), FStateTreeDataView(Turret));
		TestTrue(TEXT("Second real Task captures first episode"), OldTask.Start() == EStateTreeRunStatus::Running);
		Result.Result = EHackResult::Fail;
		Hackable->DeferHackCompletion(Result, Completion);
		TickTask();
		TestFalse(TEXT("Failure releases first episode"), Turret->IsHackingInProgress());
		TestFalse(TEXT("Failure preserves enemy team"), Turret->IsHackedToPlayerTeam());
		TestEqual(TEXT("Failure callback once"), CompletionCount, 1);
		Query.RequestId = 42;
		Hackable->HandleHackStarted(Query);
		const uint32 SecondEpisode = Turret->GetHackingRequestId();
		TestTrue(TEXT("New target episode cannot reuse old ID"), SecondEpisode != FirstEpisode);
		OldTask.Stop();
		TestTrue(TEXT("Old actual Hacking Task Exit cannot cancel new episode"), Turret->IsHackingInProgress());
		TestEqual(TEXT("New episode identity survives stale Exit"), Turret->GetHackingRequestId(), SecondEpisode);
		int32 StaleCallbacks = 0;
		Hackable->DeferHackCompletion(Result, [&](const FHackResultContext& Stale)
		{
			++StaleCallbacks;
			TestTrue(TEXT("Stale request is rejected as Cancelled"), Stale.Result == EHackResult::Cancelled);
		});
		TestEqual(TEXT("Old request cannot return a second result"), CompletionCount, 1);
		TestEqual(TEXT("Stale submitter receives one rejection"), StaleCallbacks, 1);
		TestFalse(TEXT("Old success/failure cannot finish new episode"), Turret->IsHackedToPlayerTeam());
		return true;
	}
	const double BeforeTime = Fixture.World->GetTimeSeconds();
	Hackable->DeferHackCompletion(Result, Completion);
	TickTask();
	CheckSuccess();
	TestEqual(TEXT("Enter-before-result finishes without World-time delay"), Fixture.World->GetTimeSeconds(), BeforeTime);
	Hackable->DeferHackCompletion(Result, [](const FHackResultContext&) {});
	TickTask();
	TestEqual(TEXT("Duplicate result cannot commit twice"), CompletionCount, 1);
#else
	AddWarning(TEXT("Transient StateTree integration fixtures require an Editor build."));
#endif
	return true;
}

#endif
