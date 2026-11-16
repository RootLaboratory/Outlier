#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceRecoveryTest,
	"Outlier.UI.DeveloperConvenience.TeamRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
	if (!TestNotNull(TEXT("Recovery fixture world"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!ModeClass || !ShooterClass || !PartnerClass)
	{
		AddError(TEXT("Project GameMode and pair classes are required"));
		return false;
	}
	auto* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	auto* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	auto* Partner = World->SpawnActor<APartnerCharacter>(PartnerClass);
	auto* ShooterPC = World->SpawnActor<AFirstPersonPlayerController>();
	auto* PartnerPC = World->SpawnActor<APartnerPlayerController>();
	if (!Mode || !Shooter || !Partner || !ShooterPC || !PartnerPC)
	{
		AddError(TEXT("Recovery fixture actors must spawn"));
		return false;
	}
	const auto AddState = [World](AFirstPersonPlayerController* PC, EOutlierPlayerRole Role)
	{
		auto* State = World->SpawnActor<AOutlierPlayerState>();
		if (State)
		{
			State->SetOwner(PC);
			State->SetPlayerRole(Role);
			State->SetPairId(7);
			PC->PlayerState = State;
		}
		return State;
	};
	auto* ShooterState = AddState(ShooterPC, EOutlierPlayerRole::Shooter);
	auto* PartnerState = AddState(PartnerPC, EOutlierPlayerRole::Partner);
	if (!ShooterState || !PartnerState)
	{
		return false;
	}
	ShooterPC->Possess(Shooter);
	PartnerPC->Possess(Partner);
	for (auto* State : { ShooterState, PartnerState })
	{
		State->SetShooterCharacter(Shooter);
		State->SetPartnerCharacter(Partner);
	}
	Shooter->DispatchBeginPlay();
	Partner->DispatchBeginPlay();
	ShooterState->SetAcquiredSuit(true);
	auto* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	auto* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	auto* Vitality = Partner->GetPartnerVitalityComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->InitializeForPawn(Partner);
	ShooterASC->InitializeVitalityToSelf(100.0f);
	PartnerASC->InitializeVitalityToSelf(100.0f);
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);

	FDeveloperConvenienceRecoveryRequest Request;
	const auto RefreshRequest = [&]()
	{
		const auto& State = ShooterPC->GetDeveloperConvenienceState();
		Request.RequestId = FGuid::NewGuid();
		Request.MatchId = State.MatchId;
		Request.SessionId = State.SessionId;
		Request.GameplayGeneration = State.GameplayGeneration;
	};
	RefreshRequest();
	TestEqual(TEXT("Recovery requires a menu session"), Mode->RequestDeveloperConvenienceTeamRecovery(ShooterPC, Request),
		EDeveloperConvenienceRequestResult::NoSession);
	// 입력/WBP 구성과 분리해 실제 서버 정지 상태에서 회복 및 권한을 검증한다.
	Mode->DeveloperConvenienceState.SessionId = FGuid::NewGuid();
	Mode->DeveloperConvenienceOperator = ShooterPC;
	Mode->PublishDeveloperConvenienceState();
	World->GetWorldSettings()->SetPauserPlayerState(ShooterState);
	TestTrue(TEXT("Fixture world is paused"), World->IsPaused());
	RefreshRequest();
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 30.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 5.0f);
	TestTrue(TEXT("Real Partner shield is applied"), Shooter->ApplyPartnerShield(40.0f, 30.0f, 0.0f));
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 25.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 40.0f);
	const FActiveGameplayEffectHandle ShooterCooldown = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterBulletReflectionCooldownGameplayEffect::StaticClass(), 30.0f, Shooter);
	const FActiveGameplayEffectHandle PartnerCooldown = PartnerASC->ApplyTimedGameplayEffectToSelf(
		UOutlierPartnerEMPCooldownGameplayEffect::StaticClass(), 30.0f, Partner);
	const auto ShooterEffects = ShooterASC->GetActiveEffects(FGameplayEffectQuery());
	const auto PartnerEffects = PartnerASC->GetActiveEffects(FGameplayEffectQuery());
	TMap<FActiveGameplayEffectHandle, TPair<float, float>> EffectTimes;
	for (auto* ASC : { ShooterASC, PartnerASC })
	{
		for (const auto& Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
			EffectTimes.Add(Handle, { Effect->StartWorldTime, Effect->Spec.GetDuration() });
		}
	}
	const FDeveloperConvenienceSnapshot Before = ShooterPC->GetDeveloperConvenienceState();
	TestEqual(TEXT("Waiting peer cannot heal"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::NotOperator);
	TestEqual(TEXT("Rejected request changes no health"), Shooter->GetCurHealth(), 30.0f);
	TestEqual(TEXT("Operator recovers both players while paused"), Mode->RequestDeveloperConvenienceTeamRecovery(ShooterPC, Request),
		EDeveloperConvenienceRequestResult::Accepted);
	TestEqual(TEXT("Shooter HP reaches max"), Shooter->GetCurHealth(), 100.0f);
	TestEqual(TEXT("Shooter base shield reaches max"), Shooter->GetCurShield(), 100.0f);
	TestEqual(TEXT("Partner HP reaches max"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);
	TestEqual(TEXT("Additional shield stays partially depleted"), Shooter->GetCurPartnerShield(), 25.0f);
	TestEqual(TEXT("Additional shield maximum is unchanged"), Shooter->GetMaxPartnerShield(), 40.0f);
	TestTrue(TEXT("Menu snapshot is unchanged"), ShooterPC->GetDeveloperConvenienceState() == Before);
	TestTrue(TEXT("Pause owner is preserved"), World->GetWorldSettings()->GetPauserPlayerState() == ShooterState);
	// 회복 전의 모든 효과 핸들과 시작/지속시간을 보존해야 한다. 제거 후 재생성도 허용하지 않는다.
	for (const auto& Handle : ShooterEffects)
	{
		TestNotNull(TEXT("Shooter buff/cooldown effect survives"), ShooterASC->GetActiveGameplayEffect(Handle));
	}
	for (const auto& Handle : PartnerEffects)
	{
		TestNotNull(TEXT("Partner cooldown effect survives"), PartnerASC->GetActiveGameplayEffect(Handle));
	}
	TestNotNull(TEXT("Shooter cooldown handle survives"), ShooterASC->GetActiveGameplayEffect(ShooterCooldown));
	TestNotNull(TEXT("Partner cooldown handle survives"), PartnerASC->GetActiveGameplayEffect(PartnerCooldown));
	for (auto* ASC : { ShooterASC, PartnerASC })
	{
		for (const auto& Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
			const auto* Previous = EffectTimes.Find(Handle);
			if (TestNotNull(TEXT("Recovery creates no new persistent effects"), Previous))
			{
				TestEqual(TEXT("Effect start time is unchanged"), Effect->StartWorldTime, Previous->Key);
				TestEqual(TEXT("Effect duration is unchanged"), Effect->Spec.GetDuration(), Previous->Value);
			}
		}
	}

	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("Partner enters actual Reboot"), Vitality->IsRebooting());
	const auto OtherImmunity = PartnerASC->ApplyDamageImmuneStateToSelf();
	RefreshRequest();
	TestEqual(TEXT("Recovery ends Reboot through its existing removal callback"),
		Mode->RequestDeveloperConvenienceTeamRecovery(ShooterPC, Request), EDeveloperConvenienceRequestResult::Accepted);
	TestFalse(TEXT("Reboot ended"), Vitality->IsRebooting());
	TestFalse(TEXT("Removed Reboot cannot complete a second time"), Vitality->RemoveRebootEffect());
	TestEqual(TEXT("Reboot callback restores full HP"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);
	TestNotNull(TEXT("Separate immunity survives Reboot removal"), PartnerASC->GetActiveGameplayEffect(OtherImmunity));
	TestNotNull(TEXT("Recovery leaves Partner cooldown active"), PartnerASC->GetActiveGameplayEffect(PartnerCooldown));
	TestEqual(TEXT("Reboot recovery preserves additional shield"), Shooter->GetCurPartnerShield(), 25.0f);

	// 제거한 Reboot의 원래 만료 시각을 지나도, 이후 피해가 다시 회복되지 않아야 한다.
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	for (int32 TickIndex = 0; TickIndex < FMath::CeilToInt((Vitality->GetConfiguredRebootTime() + 1.0f) / 0.1f); ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestEqual(TEXT("Old Reboot expiry cannot heal later damage"), Partner->GetVitalAttributeSet()->GetHealth(), 60.0f);
	World->GetWorldSettings()->SetPauserPlayerState(ShooterState);

	auto* Enemy = World->SpawnActor<AEnemyBase>();
	if (!TestNotNull(TEXT("Possession fixture Enemy"), Enemy))
	{
		return false;
	}
	Enemy->GetOutlierAbilitySystemComponent()->InitializeForPawn(Enemy);
	Enemy->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(80.0f);
	Enemy->GetOutlierAbilitySystemComponent()->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 20.0f);
	PartnerPC->Possess(Enemy);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	RefreshRequest();
	TestEqual(TEXT("Recovery supports possession"), Mode->RequestDeveloperConvenienceTeamRecovery(ShooterPC, Request),
		EDeveloperConvenienceRequestResult::Accepted);
	TestEqual(TEXT("Original Partner body is healed"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);
	TestEqual(TEXT("Possessed Enemy is not a player recovery target"), Enemy->GetCurrentHealth(), 20.0f);
	PartnerPC->Possess(Partner);
	Mode->DeveloperConvenienceOperator = PartnerPC;
	Mode->PublishDeveloperConvenienceState();
	RefreshRequest();
	TestEqual(TEXT("Partner operator can recover the pair"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::Accepted);

	Request.MatchId = FGuid::NewGuid();
	TestEqual(TEXT("Old match request is rejected"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::StaleContext);
	RefreshRequest();
	Request.SessionId = FGuid::NewGuid();
	TestEqual(TEXT("Old session request is rejected"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::StaleContext);
	RefreshRequest();
	++Request.GameplayGeneration;
	TestEqual(TEXT("Old gameplay generation is rejected"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::StaleContext);
	RefreshRequest();
	Mode->DeveloperConvenienceState.bTransitioning = true;
	TestEqual(TEXT("Transitioning pair cannot recover"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::Transitioning);
	Mode->DeveloperConvenienceState.bTransitioning = false;
	PartnerState->SetPairId(99);
	TestEqual(TEXT("Changed pair is rejected"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::InvalidPair);
	PartnerState->SetPairId(7);
	PartnerState->SetShooterCharacter(nullptr);
	TestEqual(TEXT("Mismatched body is rejected"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::TargetUnavailable);
	PartnerState->SetShooterCharacter(Shooter);
	// 서버 기동 없이 실제 PC -> 서버 GameMode 요청 경로도 실행한다.
	auto* AuthorityMode = FindFProperty<FObjectPropertyBase>(UWorld::StaticClass(), TEXT("AuthorityGameMode"));
	if (TestNotNull(TEXT("Authority GameMode fixture property"), AuthorityMode))
	{
		AuthorityMode->SetObjectPropertyValue_InContainer(World, Mode);
		ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
		TestTrue(TEXT("Blueprint request path returns a request identity"),
			PartnerPC->RequestDeveloperConvenienceTeamRecovery(PartnerPC->GetDeveloperConvenienceState()).IsValid());
		TestEqual(TEXT("PlayerController request reaches server recovery"), Shooter->GetCurHealth(), 100.0f);
	}
	const auto* RecoveryAPI = AFirstPersonPlayerController::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(AFirstPersonPlayerController, RequestDeveloperConvenienceTeamRecovery));
	TestTrue(TEXT("Recovery request is exposed to Blueprint"), RecoveryAPI && RecoveryAPI->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	const auto* RecoveryRPC = AFirstPersonPlayerController::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(AFirstPersonPlayerController, ServerRequestDeveloperConvenienceTeamRecovery));
	TestTrue(TEXT("Recovery RPC is reliable and server authoritative"), RecoveryRPC
		&& RecoveryRPC->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
	ShooterASC->ApplyDeadStateToSelf();
	TestEqual(TEXT("Dead Shooter is not resurrected"), Mode->RequestDeveloperConvenienceTeamRecovery(PartnerPC, Request),
		EDeveloperConvenienceRequestResult::TargetUnavailable);
	return true;
}

#endif
