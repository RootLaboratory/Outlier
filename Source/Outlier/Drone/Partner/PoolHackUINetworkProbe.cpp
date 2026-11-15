#include "Drone/Partner/HackTargetPointNetworkProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Components/StateTreeComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UI/HackMiniGameWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UnrealClient.h"

void AHackTargetPointNetworkProbe::SetPoolHackUIPhase(int32 NextPhase)
{
	Phase = NextPhase;
	PhaseStart = GetWorld()->GetTimeSeconds();
	OwnerObservation.Reset();
	ForceNetUpdate();
}

bool AHackTargetPointNetworkProbe::LeasePoolHackUITarget()
{
	APartnerPlayerController* PC = Cast<APartnerPlayerController>(GetOwner());
	if (!PC || PC->GetPawn() != Partner) return false;
	PC->SetControlRotation(FRotator::ZeroRotator);
	FVector View;
	FRotator Rotation;
	PC->GetPlayerViewPoint(View, Rotation);
	FEnemyPoolLeaseContext Context;
	Context.GameplayGeneration = 8100 + CaseIndex;
	UEnemyPoolSubsystem* Pool = GetWorld()->GetSubsystem<UEnemyPoolSubsystem>();
	UClass* Class = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	AEnemyBase* Enemy = Pool->LeaseEnemy(Class, FTransform((View + FVector(350, 0, 0)).GridSnap(1)), Context);
	if (!Enemy || (Target && Target != Enemy)) return false;
	Target = Enemy;
	Enemy->bAlwaysRelevant = true;
	Enemy->SetReplicateMovement(true);
	Enemy->CompletePoolSpawnPresentation(Context.GameplayGeneration, Enemy->GetPoolLeaseSerial());
	Enemy->GetStateTreeComponent()->StopLogic(TEXT("Pool hack UI fixture: stationary target"));
	Enemy->GetCharacterMovement()->DisableMovement();
	// 미니게임 종류/시간만 격리 인스턴스에 지정한다. 대상 자격과 시작/성공 판정은 기존 RPC가 소유한다.
	UHackableComponent* Hackable = Enemy->GetHackableComponent();
	Hackable->HackTags.RemoveTag(HackGameplayTags::MiniGame::None());
	Hackable->HackTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Hack.MiniGame.ClickCircle")));
	Enemy->ForceNetUpdate();
	return true;
}

TSharedRef<FJsonObject> AHackTargetPointNetworkProbe::ObservePoolHackUI()
{
	APartnerPlayerController* PC = Cast<APartnerPlayerController>(GetWorld()->GetFirstPlayerController());
	ULocalPlayerPostProcessSubsystem* PP = PC && PC->GetLocalPlayer()
		? PC->GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>() : nullptr;
	ULocalPlayerUILayerSubsystem* UILayers = PC && PC->GetLocalPlayer()
		? PC->GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("event"), TEXT("UIObservation"));
	Row->SetNumberField(TEXT("phase"), Phase);
	Row->SetNumberField(TEXT("request"), HackComponent->ActiveHackRequestId);
	Row->SetNumberField(TEXT("generation"), HackComponent->ActiveHackGeneration);
	Row->SetNumberField(TEXT("lease"), HackComponent->ActiveHackLease);
	// 미니게임은 viewport 루트의 자식이다. 자식에 IsInViewport를 묻지 않고 실제 최상위 레이어/부모를 확인한다.
	Row->SetBoolField(TEXT("miniVisible"), HackComponent->HackMiniGameWidget && UILayers
		&& UILayers->GetTopLayerWidget() == HackComponent->HackMiniGameWidget
		&& HackComponent->HackMiniGameWidget->GetParent() && HackComponent->HackMiniGameWidget->IsVisible());
	Row->SetStringField(TEXT("miniClass"), HackComponent->HackMiniGameWidget
		? HackComponent->HackMiniGameWidget->GetClass()->GetPathName() : TEXT("None"));
	const FVector2D MiniSize = HackComponent->HackMiniGameWidget
		? HackComponent->HackMiniGameWidget->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
	Row->SetNumberField(TEXT("miniWidth"), MiniSize.X);
	Row->SetNumberField(TEXT("miniHeight"), MiniSize.Y);
	Row->SetBoolField(TEXT("oldMiniVisible"), OldMiniGameWidget.IsValid()
		&& (OldMiniGameWidget->IsInViewport() || OldMiniGameWidget->GetParent()));
	Row->SetBoolField(TEXT("interaction"), HackComponent->IsHackInteractionActive());
	Row->SetBoolField(TEXT("transition"), PP && PP->IsHackPossessionTransitionActive());
	Row->SetBoolField(TEXT("hasPostProcess"), PP != nullptr);
	Row->SetNumberField(TEXT("possessionState"), PC ? int32(PC->GetPartnerPossessionState()) : -1);
	Row->SetStringField(TEXT("pawn"), PC ? GetNameSafe(PC->GetPawn()) : TEXT("None"));
	Row->SetStringField(TEXT("target"), GetNameSafe(Target));
	Row->SetBoolField(TEXT("requestLost"), bUIRequestLost);
	Row->SetBoolField(TEXT("sawNewTransition"), bSawNewTransition);
	const UOutlierAbilitySystemComponent* ASC = Partner->GetOutlierAbilitySystemComponent();
	Row->SetBoolField(TEXT("canAcceptInput"), Partner->CanAcceptInput());
	Row->SetBoolField(TEXT("abilityConfigured"), ASC && ASC->IsPartnerAbilitiesConfigured());
	Row->SetBoolField(TEXT("hackChargeReady"), ASC && ASC->CanUsePartnerHackCharge());
	return Row;
}

void AHackTargetPointNetworkProbe::TickPoolHackUI()
{
	const float Now = GetWorld()->GetTimeSeconds();
	APartnerPlayerController* PC = Cast<APartnerPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !HackComponent) return;
	if (HasAuthority())
	{
		UEnemyPoolSubsystem* Pool = GetWorld()->GetSubsystem<UEnemyPoolSubsystem>();
		if (!bStarted)
		{
			int32 Ready = 0;
			for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
				if (auto* Client = Cast<AHackTargetPointProbeController>(It->Get()); Client && Client->bProbeReady) ++Ready;
			if (Ready < 2) return;
			UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(this);
			auto& Entry = Definition->Entries.AddDefaulted_GetRef();
			Entry.EnemyClass = LoadClass<AEnemyBase>(nullptr,
				TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
			Entry.PrewarmCount = Entry.MaxCount = 1;
			bStarted = true;
			CaseIndex = 0;
			if (!Pool->PrewarmPool(Definition) || !LeasePoolHackUITarget())
			{
				FinishResult(false, TEXT("Pool fixture failed"));
				FPlatformMisc::RequestExit(false);
				return;
			}
			SetPoolHackUIPhase(0);
		}
		if (Now - PhaseStart > 40)
		{
			FinishResult(false, FString::Printf(TEXT("UI phase timeout: %d"), Phase));
			FPlatformMisc::RequestExit(false);
			return;
		}
		if (!OwnerObservation) return;
		AEnemyBase* Enemy = Cast<AEnemyBase>(Target);
		auto ReturnCurrent = [&]() { return Pool->ReturnEnemy(Enemy, Enemy->GetPoolGameplayGeneration(), Enemy->GetPoolLeaseSerial()); };
		if (Phase == 0)
		{
			OldUIRequest = HackComponent->ActiveHackRequestId;
			OldUIGeneration = Enemy->GetPoolGameplayGeneration();
			OldUILease = Enemy->GetPoolLeaseSerial();
			if (CaseIndex == 2)
			{
				SetPoolHackUIPhase(1);
				ClientFinishHack(true);
			}
			else
			{
				bAllPassed &= ReturnCurrent();
				if (CaseIndex == 1) bAllPassed &= LeasePoolHackUITarget();
				SetPoolHackUIPhase(2);
			}
		}
		else if (Phase == 1)
		{
			// 소유 클라이언트가 실제 전환 화면을 관측한 뒤 반환한다. 연출 완료를 직접 호출하지 않는다.
			bAllPassed &= ReturnCurrent();
			bAllPassed &= LeasePoolHackUITarget();
			SetPoolHackUIPhase(2);
		}
		else if (Phase == 2)
		{
			if (CaseIndex == 0) bAllPassed &= LeasePoolHackUITarget();
			SetPoolHackUIPhase(3);
		}
		else if (Phase == 3)
		{
			bAllPassed &= HackComponent->ActiveHackRequestId != 0 && HackComponent->ActiveHackRequestId != OldUIRequest;
			SetPoolHackUIPhase(4);
		}
		else if (Phase == 4)
		{
			bAllPassed &= !OwnerObservation->GetBoolField(TEXT("requestLost"))
				&& HackComponent->ActiveHackRequestId != 0 && HackComponent->ActiveHackRequestId != OldUIRequest;
			SetPoolHackUIPhase(5);
			ClientFinishHack(true);
		}
		else if (Phase == 5)
		{
			// 실제 Pawn 복제와 화면 reveal/focus가 끝나 해킹 수명이 시작될 때까지 기다린다.
			if (!Enemy->IsHackEffectActive() || Enemy->GetController() != GetOwner()) return;
			bAllPassed &= OwnerObservation->GetBoolField(TEXT("sawNewTransition"));
			bAllPassed &= ReturnCurrent();
			SetPoolHackUIPhase(6);
		}
		else if (Phase == 6)
		{
			FinishResult(bAllPassed, TEXT("Real owner minigame/transition cleanup, reuse and stale RPC isolation"));
			if (++CaseIndex < 3)
			{
				bAllPassed &= LeasePoolHackUITarget();
				SetPoolHackUIPhase(0);
			}
			else
			{
				auto Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("event"), TEXT("Complete"));
				Row->SetBoolField(TEXT("passed"), bAllPassed);
				Write(Row);
				FPlatformMisc::RequestExit(false);
				SetActorTickEnabled(false);
			}
		}
		return;
	}

	if (!bConnected)
	{
		if (auto* ProbePC = Cast<AHackTargetPointProbeController>(PC)) ProbePC->ServerProbeReady();
		bConnected = true;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), TEXT("Connected"));
		Write(Row);
	}
	if (Role != TEXT("Owner") || CaseIndex < 0 || !Target) return;
	if (LocalPhase != Phase || LocalCase != CaseIndex)
	{
		LocalCase = CaseIndex;
		LocalPhase = Phase;
		LocalStart = Now;
		bObservationSent = bSearchRequested = false;
		bRestoreObserved = false;
		if (Phase == 0) { bUIRequestLost = false; bSawNewTransition = false; }
		// 네트워크 왕복과 관측 대기 중 자연 시간초과가 섞이지 않도록 검증 인스턴스의 제한만 늘린다.
		if (Phase == 0 || Phase == 3) HackComponent->CachedAbilityData.MiniGameTime = 60.f;
	}
	const float Age = Now - LocalStart;
	auto Row = ObservePoolHackUI();
	const bool bMini = Row->GetBoolField(TEXT("miniVisible"));
	const bool bTransition = Row->GetBoolField(TEXT("transition"));
	const bool bClean = !Row->GetBoolField(TEXT("interaction")) && !Row->GetBoolField(TEXT("oldMiniVisible"))
		&& !bTransition && PC->GetPawn() == Partner && PC->GetPartnerPossessionState() == EPartnerPossessionState::PartnerControlled;
	if (Phase == 5 && bTransition) bSawNewTransition = true;
	if (bObservationSent) return;
	if ((Phase == 0 || Phase == 3) && !bSearchRequested && Partner->IsLocallyControlled() && Age > 1)
	{
		AEnemyBase* Enemy = Cast<AEnemyBase>(Target);
		if (!Enemy || Enemy->GetEnemyPoolState() != EEnemyPoolState::CombatActive
			|| (Phase == 3 && Enemy->GetPoolLeaseSerial() <= OldUILease)) return;
		HackComponent->TryHack();
		auto Attempt = ObservePoolHackUI();
		Attempt->SetStringField(TEXT("event"), TEXT("SearchAttempt"));
		Write(Attempt);
		bSearchRequested = true;
	}
	// 빙의 복귀 직후 ASC/충전 복제가 늦을 수 있다. UI를 강제로 열지 않고 기존 입력 경로로 재시도한다.
	if ((Phase == 0 || Phase == 3) && bSearchRequested && !bMini
		&& !HackComponent->IsHackCandidateSearchActive() && Age > 4)
	{
		bSearchRequested = bRestoreObserved = false;
		LocalStart = Now;
	}
	if ((Phase == 0 || Phase == 3) && bSearchRequested && !bRestoreObserved
		&& HackComponent->IsHackCandidateSearchActive() && Age > 2)
	{
		const uint32 Hold = uint32(100 + CaseIndex * 10 + Phase);
		HackComponent->ServerBeginHackHold(Target, Hold);
		HackComponent->ServerTryStartHack(Target, Hold);
		bRestoreObserved = true;
	}
	bool bReady = false;
	if (Phase == 0 && bMini)
	{
		OldUIRequest = HackComponent->ActiveHackRequestId;
		OldUIGeneration = HackComponent->ActiveHackGeneration;
		OldUILease = HackComponent->ActiveHackLease;
		OldMiniGameWidget = HackComponent->HackMiniGameWidget;
		bReady = Age > 2.5f;
	}
	if (Phase == 1) bReady = bTransition && PC->GetPartnerPossessionState() == EPartnerPossessionState::Transitioning;
	if (Phase == 2 || Phase == 6) bReady = bClean && Age > 0.5f;
	if (Phase == 3) bReady = bMini && HackComponent->ActiveHackRequestId != OldUIRequest && !Row->GetBoolField(TEXT("oldMiniVisible"));
	if (Phase == 4)
	{
		if (!bSearchRequested)
		{
			// 소유 연결에서 실제 옛 RPC를 보낸다. 새 UI/RequestId는 건드리지 않고 지연 패킷만 재현한다.
			FHackResultContext OldResult;
			OldResult.TargetActor = Target;
			OldResult.RequestId = OldUIRequest;
			OldResult.Result = EHackResult::Success;
			HackComponent->ServerCompleteHack(OldResult);
			OldResult.Result = EHackResult::Cancelled;
			HackComponent->ServerCompleteHack(OldResult);
			PC->ServerNotifyHackTransitionCovered(Cast<AEnemyBase>(Target), OldUIGeneration, OldUILease);
			PC->ServerNotifyHackPossessionRevealFinished(Cast<AEnemyBase>(Target), OldUIGeneration, OldUILease);
			bSearchRequested = true;
		}
		bUIRequestLost |= !bMini || HackComponent->ActiveHackRequestId == 0;
		bReady = Age > 2;
	}
	if (Phase == 5) bReady = bSawNewTransition && !bTransition && !bMini
		&& PC->GetPawn() == Target && PC->GetPartnerPossessionState() == EPartnerPossessionState::EnemyPossessed;
	if (!bReady) return;
	Row = ObservePoolHackUI();
	Write(Row);
	FScreenshotRequest::RequestScreenshot(FPaths::GetPath(Output) / FString::Printf(TEXT("Case%d_Phase%d.png"), CaseIndex, Phase), true, false);
	FString Json;
	FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Json));
	ServerObserve(CaseIndex, Json);
	bObservationSent = true;
}
#endif
