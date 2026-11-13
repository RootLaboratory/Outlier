#include "FirstPerson/PlayerRelocationNetworkProbe.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Enemy/EnemyBase.h"
#include "Interaction/SuitInteraction.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"

namespace RelocationProbe
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Room, "Room.RelocationProbe.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Outside, "Room.RelocationProbe.2.1");
	constexpr int32 CaseCount = 8;
	const TCHAR* Names[] = {TEXT("QuantumStanding"), TEXT("QuantumCrouched"), TEXT("AutomaticStand"),
		TEXT("QuantumNoFloor"), TEXT("QuantumCancelled"), TEXT("CombatGround"), TEXT("CombatFlight"), TEXT("SuitFlightJoin")};
	bool Enabled()
	{
#if WITH_DEV_AUTOMATION_TESTS
		FString Role;
		return FParse::Value(FCommandLine::Get(), TEXT("OutlierRelocationProbe="), Role)
			&& (Role == TEXT("Server") || Role == TEXT("Shooter") || Role == TEXT("Partner"));
#else
		return false;
#endif
	}
	template<class T> void SetTag(T* Actor, FGameplayTag Tag)
	{
		*FindFProperty<FStructProperty>(T::StaticClass(), TEXT("RoomTag"))->template ContainerPtrToValuePtr<FGameplayTag>(Actor) = Tag;
	}
	APlayerRelocationNetworkProbe* Find(UWorld* World)
	{
		for (TActorIterator<APlayerRelocationNetworkProbe> It(World); It; ++It) return *It;
		return nullptr;
	}
}

void APlayerRelocationProbeController::ServerProbeReady_Implementation()
{
	if (RelocationProbe::Enabled()) bProbeReady = true;
}

void APlayerRelocationProbeController::ServerProbeObservation_Implementation(int32 Index, const FString& Json)
{
	if (RelocationProbe::Enabled())
		if (auto* Probe = RelocationProbe::Find(GetWorld())) Probe->ReceiveObservation(this, Index, Json);
}

void APlayerRelocationProbeController::ServerProbeTransitionReady_Implementation()
{
	if (!RelocationProbe::Enabled()) return;
	if (auto* Probe = RelocationProbe::Find(GetWorld()); Probe && Probe->Shooter)
	{
		AShooterCharacter* Shooter = Probe->Shooter;
		// 암전 렌더링은 이 격리 검증의 범위 밖이다. 각 실제 소유 연결이 서버의 기존 단계 응답 API를 호출한다.
		Shooter->AcknowledgeSuitTransition(this, Shooter->GetSuitTransitionId(), Shooter->GetSuitTransitionPhase());
	}
}

APlayerRelocationProbeGameMode::APlayerRelocationProbeGameMode()
{
	if (RelocationProbe::Enabled())
	{
		PlayerControllerClass = APlayerRelocationProbeController::StaticClass();
		PlayerStateClass = AOutlierPlayerState::StaticClass();
		DefaultPawnClass = nullptr;
		bStartPlayersAsSpectators = true;
	}
}

void APlayerRelocationProbeGameMode::PostLogin(APlayerController* Player)
{
	Super::PostLogin(Player);
	if (!RelocationProbe::Enabled()) return;
	APlayerRelocationNetworkProbe* Probe = RelocationProbe::Find(GetWorld());
	if (!Probe) Probe = GetWorld()->SpawnActor<APlayerRelocationNetworkProbe>();
	const bool bShooter = !Probe->Shooter;
	if (!bShooter && Probe->Partner) return;
	UClass* Class = LoadClass<ACharacter>(nullptr, bShooter
		? TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C")
		: TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Pawn = Class ? GetWorld()->SpawnActor<ACharacter>(Class, FVector(-800, bShooter ? 0 : 500, 200), FRotator::ZeroRotator, Spawn) : nullptr;
	if (!Pawn) return;
	if (bShooter) Probe->Shooter = CastChecked<AShooterCharacter>(Pawn);
	else Probe->Partner = CastChecked<APartnerCharacter>(Pawn);
	auto* State = Player->GetPlayerState<AOutlierPlayerState>();
	State->SetPairId(0);
	State->SetPlayerRole(bShooter ? EOutlierPlayerRole::Shooter : EOutlierPlayerRole::Partner);
	Player->Possess(Pawn);
	if (Probe->Shooter && Probe->Partner)
	{
		Probe->Shooter->SetPartnerCharacter(Probe->Partner);
		Probe->Partner->SetShooterCharacter(Probe->Shooter);
		for (AFirstPersonCharacter* Character : {static_cast<AFirstPersonCharacter*>(Probe->Shooter), static_cast<AFirstPersonCharacter*>(Probe->Partner)})
		{
			auto* PS = Character->GetPlayerState<AOutlierPlayerState>();
			PS->SetShooterCharacter(Probe->Shooter);
			PS->SetPartnerCharacter(Probe->Partner);
			PS->SetAcquiredSuit(true);
		}
		Probe->Shooter->GetOutlierAbilitySystemComponent()->InitializeForPawn(Probe->Shooter);
		Probe->Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Probe->Partner);
	}
	Probe->ForceNetUpdate();
}

APlayerRelocationNetworkProbe::APlayerRelocationNetworkProbe()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30);
	PrimaryActorTick.bCanEverTick = RelocationProbe::Enabled();
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	DestinationFloor = CreateDefaultSubobject<UBoxComponent>(TEXT("DestinationFloor"));
	DestinationFloor->SetupAttachment(RootComponent);
	DestinationFloor->SetBoxExtent(FVector(450, 450, 10));
	DestinationFloor->SetRelativeLocation(FVector(0, 0, -10));
	DestinationFloor->SetCollisionProfileName(TEXT("BlockAll"));
	StartFloor = CreateDefaultSubobject<UBoxComponent>(TEXT("StartFloor"));
	StartFloor->SetupAttachment(RootComponent);
	StartFloor->SetBoxExtent(FVector(200, 200, 10));
	StartFloor->SetRelativeLocation(FVector(-800, 0, -10));
	StartFloor->SetCollisionProfileName(TEXT("BlockAll"));
	Ceiling = CreateDefaultSubobject<UBoxComponent>(TEXT("Ceiling"));
	Ceiling->SetupAttachment(RootComponent);
	Ceiling->SetBoxExtent(FVector(350, 350, 20));
	Ceiling->SetRelativeLocation(FVector(0, 0, 150));
	Ceiling->SetCollisionProfileName(TEXT("BlockAll"));
	Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void APlayerRelocationNetworkProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, Shooter);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, Partner);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, Barrier);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, CaseIndex);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, Phase);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, ExpectedShooter);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, ExpectedPartner);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, bExpectedCrouch);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, bExpectedBarrier);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, bAllPassed);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, ExpectedShooterMode);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, ExpectedPartnerMode);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, ExpectedCharges);
	DOREPLIFETIME(APlayerRelocationNetworkProbe, bExpectedSuit);
}

void APlayerRelocationNetworkProbe::Write(const TSharedRef<FJsonObject>& Row) const
{
	FString Output, ProbeRole;
	FParse::Value(FCommandLine::Get(), TEXT("OutlierRelocationOutput="), Output);
	FParse::Value(FCommandLine::Get(), TEXT("OutlierRelocationProbe="), ProbeRole);
	const FString Absolute = FPaths::ConvertRelativePathToFull(Output);
	const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	if (Output.IsEmpty() || !FPaths::IsUnderDirectory(Absolute, Saved))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RelocationProbe] Rejected output path Output=%s Saved=%s"), *Absolute, *Saved);
		return;
	}
	Row->SetStringField(TEXT("role"), ProbeRole);
#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = GetWorld()->GetNetDriver())
	{
		Row->SetNumberField(TEXT("lag"), Driver->PacketSimulationSettings.PktLag);
		Row->SetNumberField(TEXT("loss"), Driver->PacketSimulationSettings.PktLoss);
	}
#endif
	Row->SetNumberField(TEXT("netMode"), GetNetMode());
	Row->SetBoolField(TEXT("authority"), HasAuthority());
	Row->SetNumberField(TEXT("case"), CaseIndex);
	if (CaseIndex >= 0 && CaseIndex < RelocationProbe::CaseCount) Row->SetStringField(TEXT("name"), RelocationProbe::Names[CaseIndex]);
	FString Json;
	FJsonSerializer::Serialize(Row, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Absolute), true);
	FFileHelper::SaveStringToFile(Json + LINE_TERMINATOR, *Absolute, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
}

TSharedRef<FJsonObject> APlayerRelocationNetworkProbe::Observe() const
{
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("event"), TEXT("Observation"));
	Row->SetStringField(TEXT("shooterPath"), Shooter->GetPathName());
	Row->SetStringField(TEXT("partnerPath"), Partner->GetPathName());
	for (const auto& Item : {TPair<FString, ACharacter*>(TEXT("shooter"), Shooter), TPair<FString, ACharacter*>(TEXT("partner"), Partner)})
	{
		const FVector Location = Item.Value->GetActorLocation();
		Row->SetNumberField(Item.Key + TEXT("X"), Location.X);
		Row->SetNumberField(Item.Key + TEXT("Y"), Location.Y);
		Row->SetNumberField(Item.Key + TEXT("Z"), Location.Z);
		Row->SetNumberField(Item.Key + TEXT("Mode"), Item.Value->GetCharacterMovement()->MovementMode);
		Row->SetNumberField(Item.Key + TEXT("HalfHeight"), Item.Value->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		Row->SetBoolField(Item.Key + TEXT("Owner"), Item.Value->IsLocallyControlled());
	}
	Row->SetBoolField(TEXT("crouched"), Shooter->bIsCrouched);
	Row->SetBoolField(TEXT("barrier"), Barrier && Barrier->IsBlocked());
	Row->SetBoolField(TEXT("suit"), Shooter->HasAcquiredSuit());
	Row->SetBoolField(TEXT("blocked"), Shooter->IsSuitTransitionBlocked() || Partner->IsSuitTransitionBlocked());
	Row->SetNumberField(TEXT("charges"), Shooter->GetOutlierAbilitySystemComponent()->GetShooterQuantumLeapChargeState().CurrentCharges);
	return Row;
}

void APlayerRelocationNetworkProbe::ApplyGeometry()
{
	if (GeometryCase == CaseIndex) return;
	GeometryCase = CaseIndex;
	if (Shooter) Shooter->GetCharacterMovement()->SetCrouchedHalfHeight(40);
	DestinationFloor->SetCollisionEnabled(CaseIndex == 3 ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	Ceiling->SetCollisionEnabled(CaseIndex == 1 ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void APlayerRelocationNetworkProbe::SetupCase()
{
	Observers.Reset();
	Phase = 0;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ApplyGeometry();
	auto* ASC = Shooter->GetOutlierAbilitySystemComponent();
	ASC->CancelActiveShooterQuantumLeap(false);
	ASC->ResetShooterQuantumLeapTransientState();
	ASC->AddLooseGameplayTag(OutlierGameplayTags::Ability::Shooter::QuantumLeap());
	auto Config = ASC->GetShooterSuitConfig();
	Config.MaxPartnerDistance = 3000;
	Config.QuantumLeap.CastTimeSeconds = 0.2f;
	Config.QuantumLeap.CooldownSeconds = 8;
	ASC->UpdateShooterSuitConfig(Config);
	Shooter->GetCharacterMovement()->SetCrouchedHalfHeight(40);
	// 자세 복원 사례만 이전 착지를 유지한다. 다른 사례는 실제 Teleport 복제로 출발 상태를 준비한다.
	if (CaseIndex != 2)
	{
		Shooter->UnCrouch();
		Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Shooter->TeleportTo(FVector(-800, 0, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2), FRotator::ZeroRotator, false, true);
		Partner->TeleportTo(FVector(0, 0, 110), FRotator::ZeroRotator, false, true);
	}
	else
	{
		// 천장뿐 아니라 머리 위의 파트너 캡슐도 비워야 실제 기립 공간이 생긴다.
		// Shooter 자세는 건드리지 않고 기존 CMC의 자동 기립을 관측한다.
		Partner->TeleportTo(FVector(250, 0, 110), FRotator::ZeroRotator, false, true);
	}
	Partner->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Partner->GetCharacterMovement()->StopMovementImmediately();
	Shooter->GetCharacterMovement()->StopMovementImmediately();
	if (CaseIndex == 5 || CaseIndex == 6)
	{
#if WITH_DEV_AUTOMATION_TESTS
		auto* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
		Combat->ResetRuntimeCombatState();
		if (!DestinationRoom)
		{
			DestinationRoom = GetWorld()->SpawnActor<ARoomVolume>();
			RelocationProbe::SetTag(DestinationRoom.Get(), RelocationProbe::Room.GetTag());
			DestinationRoom->SetActorLocation(FVector(0, 0, 250));
			CastChecked<UBoxComponent>(DestinationRoom->GetRootComponent())->SetBoxExtent(FVector(450, 450, 500));
			OutsideRoom = GetWorld()->SpawnActor<ARoomVolume>();
			RelocationProbe::SetTag(OutsideRoom.Get(), RelocationProbe::Outside.GetTag());
			OutsideRoom->SetActorLocation(FVector(-800, 0, 250));
			CastChecked<UBoxComponent>(OutsideRoom->GetRootComponent())->SetBoxExtent(FVector(250, 250, 500));
			Barrier = GetWorld()->SpawnActor<ARoomCombatBarrier>();
			RelocationProbe::SetTag(Barrier.Get(), RelocationProbe::Room.GetTag());
			Barrier->SetActorLocation(FVector(440, 0, 200));
		}
		auto* Definition = NewObject<URoomCombatDefinition>(Combat);
		auto& Room = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Room.RoomTag = RelocationProbe::Room;
		auto& CombatPhase = Room.CombatPhases.AddDefaulted_GetRef();
		CombatPhase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		CombatPhase.Waves.AddDefaulted();
		Combat->SetCombatDefinitionForTesting(Definition);
		Combat->RegisterRoom(DestinationRoom, RelocationProbe::Room);
		Combat->RegisterRoom(OutsideRoom, RelocationProbe::Outside);
		Combat->RegisterBarrier(Barrier);
		FActorSpawnParameters Spawn;
		Spawn.bDeferConstruction = true;
		auto* Enemy = GetWorld()->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FVector(10000), FRotator::ZeroRotator, Spawn);
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(RelocationProbe::Room);
		Combat->RegisterPreplacedEnemy(Enemy);
		if (CaseIndex == 6)
		{
			Shooter->TeleportTo(FVector(0, 0, 110), FRotator::ZeroRotator, false, true);
			Partner->TeleportTo(FVector(-800, 0, 200), FRotator::ZeroRotator, false, true);
		}
		for (AFirstPersonCharacter* Pawn : {static_cast<AFirstPersonCharacter*>(Shooter), static_cast<AFirstPersonCharacter*>(Partner)})
		{
			Pawn->GetCapsuleComponent()->UpdateOverlaps();
			Pawn->GetRoomTagComp()->ClearRuntimeRoomAssignment();
			Pawn->GetRoomTagComp()->AssignDefaultRoomTag(Pawn->GetActorLocation().X < -500 ? RelocationProbe::Outside.GetTag() : RelocationProbe::Room.GetTag());
		}
		DestinationRoom->RefreshOverlappingRoomAssignments();
#endif
	}
	if (CaseIndex == 7)
	{
		GetWorld()->GetSubsystem<URoomCombatSubsystem>()->ResetRuntimeCombatState();
		for (AFirstPersonCharacter* Pawn : {static_cast<AFirstPersonCharacter*>(Shooter), static_cast<AFirstPersonCharacter*>(Partner)})
			Pawn->GetPlayerState<AOutlierPlayerState>()->SetAcquiredSuit(false);
		UClass* SuitClass = LoadClass<ASuitInteraction>(nullptr, TEXT("/Game/Blueprints/Interaction/BP_SuitInteraction.BP_SuitInteraction_C"));
		Suit = SuitClass ? GetWorld()->SpawnActor<ASuitInteraction>(SuitClass, FVector(-800, 0, 110), FRotator::ZeroRotator) : nullptr;
		if (Suit)
			*FindFProperty<FStructProperty>(ASuitInteraction::StaticClass(), TEXT("PartnerTeleportOffset"))->ContainerPtrToValuePtr<FVector>(Suit) = FVector(0, 300, 120);
		// 기본 암전 시간을 0으로 지정해도 실제 두 연결의 단계 응답과 비동기 EQS 완료를 모두 기다린다.
		auto* Timing = GetWorld()->SpawnActor<AOutlierPostProcessVolume>();
		Timing->bUnbound = true;
		Timing->ScreenTransitionTiming.FadeOutDuration = 0;
		Timing->ScreenTransitionTiming.BlackHoldDuration = 0;
		Timing->ScreenTransitionTiming.FadeInDuration = 0;
	}
	ForceNetUpdate();
}

void APlayerRelocationNetworkProbe::PerformAction()
{
	BeforeShooter = Shooter->GetActorLocation();
	BeforePartner = Partner->GetActorLocation();
	if (CaseIndex == 2) bActionAccepted = true;
	else if (CaseIndex < 5)
	{
		auto* ASC = Shooter->GetOutlierAbilitySystemComponent();
		bActionAccepted = ASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap());
		if (CaseIndex == 4) ASC->CancelActiveShooterQuantumLeap(false);
	}
	else if (CaseIndex == 5 || CaseIndex == 6)
	{
		GetWorld()->GetSubsystem<URoomCombatSubsystem>()->TryStartInitialDetectionForPlayers(RelocationProbe::Room);
		bActionAccepted = true; // 비동기 수락 반환을 완료로 간주하지 않는다. 아래 실제 위치/전투 상태로 판정한다.
	}
	else bActionAccepted = Suit && Suit->Interact(Shooter);
	Phase = 1;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
}

void APlayerRelocationNetworkProbe::CaptureExpected()
{
	ExpectedShooter = Shooter->GetActorLocation();
	ExpectedPartner = Partner->GetActorLocation();
	bExpectedCrouch = Shooter->bIsCrouched;
	bExpectedBarrier = Barrier && Barrier->IsBlocked();
	ExpectedShooterMode = Shooter->GetCharacterMovement()->MovementMode;
	ExpectedPartnerMode = Partner->GetCharacterMovement()->MovementMode;
	ExpectedCharges = Shooter->GetOutlierAbilitySystemComponent()->GetShooterQuantumLeapChargeState().CurrentCharges;
	bExpectedSuit = Shooter->HasAcquiredSuit();
	bServerPassed = bActionAccepted;
	if (CaseIndex == 0 || CaseIndex == 1)
		bServerPassed &= FVector::Dist2D(ExpectedShooter, BeforeShooter) > 400
			&& bExpectedCrouch == (CaseIndex == 1) && ExpectedCharges == 0;
	else if (CaseIndex == 2) bServerPassed &= !bExpectedCrouch;
	else if (CaseIndex == 3 || CaseIndex == 4)
		bServerPassed &= FVector::Dist2D(ExpectedShooter, BeforeShooter) < 1
			&& Shooter->GetOutlierAbilitySystemComponent()->GetShooterQuantumLeapChargeState().CurrentCharges == 1;
	else if (CaseIndex == 5 || CaseIndex == 6) bServerPassed &= bExpectedBarrier
		&& GetWorld()->GetSubsystem<URoomCombatSubsystem>()->GetRoomState(RelocationProbe::Room) == ERoomCombatState::Combat
		&& FVector::Dist2D(CaseIndex == 5 ? ExpectedShooter : ExpectedPartner, CaseIndex == 5 ? BeforeShooter : BeforePartner) > 400;
	else bServerPassed &= bExpectedSuit && !Shooter->IsSuitTransitionBlocked() && !Partner->IsSuitTransitionBlocked()
		&& ExpectedPartnerMode == MOVE_Flying && FVector::Dist(ExpectedPartner, BeforePartner) > 400;
	auto Row = Observe();
	Row->SetStringField(TEXT("event"), TEXT("ServerDecision"));
	Row->SetBoolField(TEXT("passed"), bServerPassed);
	Write(Row);
	Phase = 2;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
}

void APlayerRelocationNetworkProbe::ReceiveObservation(APlayerRelocationProbeController* Sender, int32 Index, const FString& Json)
{
	if (!RelocationProbe::Enabled() || !HasAuthority() || Phase != 2 || Index != CaseIndex || !Sender || Observers.Contains(Sender)) return;
	TSharedPtr<FJsonObject> Row;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Row) || !Row) return;
	bool bPassed = false;
	if (!Row->TryGetBoolField(TEXT("passed"), bPassed)) return;
	Observers.Add(Sender);
	bAllPassed &= bPassed;
}

void APlayerRelocationNetworkProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!RelocationProbe::Enabled() || bFinished) return;
	ApplyGeometry();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority())
	{
		auto* PC = Cast<APlayerRelocationProbeController>(GetWorld()->GetFirstPlayerController());
		if (!PC || !PC->GetPawn()) return;
		if (!bConnected)
		{
			auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("Connected")); Write(Row);
			PC->ServerProbeReady(); bConnected = true;
		}
		if (Phase == 3)
		{
			auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("Complete"));
			Row->SetBoolField(TEXT("passed"), bAllPassed); Row->SetNumberField(TEXT("expectedCases"), RelocationProbe::CaseCount); Write(Row);
			bFinished = true; return;
		}
		if (CaseIndex == 7 && Phase == 1)
		{
			if (Now - StableSince > 0.25f) { PC->ServerProbeTransitionReady(); StableSince = Now; }
			return;
		}
		if (!Shooter || !Partner || Phase != 2 || ObservedCase == CaseIndex) { StableSince = -1; return; }
		const UCapsuleComponent* Capsule = Shooter->GetCapsuleComponent();
		const float ExpectedHalfHeight = FMath::Max(Capsule->GetScaledCapsuleRadius(), Capsule->GetShapeScale()
			* (bExpectedCrouch ? 40.0f : Shooter->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
		const bool bSame = Shooter->GetActorLocation().Equals(ExpectedShooter, 3) && Partner->GetActorLocation().Equals(ExpectedPartner, 3)
			&& Shooter->bIsCrouched == bExpectedCrouch && (Barrier ? Barrier->IsBlocked() : false) == bExpectedBarrier
			&& FMath::IsNearlyEqual(Capsule->GetScaledCapsuleHalfHeight(), ExpectedHalfHeight, 0.1f)
			&& Shooter->GetCharacterMovement()->MovementMode == ExpectedShooterMode
			&& Partner->GetCharacterMovement()->MovementMode == ExpectedPartnerMode
			&& Shooter->HasAcquiredSuit() == bExpectedSuit
			&& (!Shooter->IsLocallyControlled() || Shooter->GetOutlierAbilitySystemComponent()->GetShooterQuantumLeapChargeState().CurrentCharges == ExpectedCharges)
			&& !Shooter->IsSuitTransitionBlocked() && !Partner->IsSuitTransitionBlocked();
		if (!bSame) { StableSince = -1; return; }
		if (StableSince < 0) StableSince = Now;
		if (Now - StableSince < 0.4f) return;
		auto Row = Observe(); Row->SetBoolField(TEXT("passed"), true); Write(Row);
		FString Json; FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Json));
		PC->ServerProbeObservation(CaseIndex, Json); ObservedCase = CaseIndex; StableSince = -1;
		return;
	}
	if (!Shooter || !Partner) return;
	if (CaseIndex < 0)
	{
		int32 Ready = 0;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (auto* PC = Cast<APlayerRelocationProbeController>(It->Get()); PC && PC->bProbeReady) ++Ready;
		if (Ready != 2) return;
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("Connected")); Write(Row);
		CaseIndex = 0; SetupCase(); return;
	}
	// 설정 복제 -> 실제 동작 -> 서버 결과 -> 두 실제 연결의 안정 관측 순으로 진행한다.
	if (Phase == 0 && Now - PhaseStart > 1) PerformAction();
	else if (Phase == 1 && Now - PhaseStart > 1
		&& (CaseIndex != 7 || Shooter->GetSuitTransitionPhase() == ESuitTransitionPhase::Idle || Now - PhaseStart > 12)) CaptureExpected();
	else if (Phase == 2 && (Observers.Num() == 2 || Now - PhaseStart > 12))
	{
		const bool bPassed = bServerPassed && Observers.Num() == 2;
		bAllPassed &= bPassed;
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("CaseResult"));
		Row->SetBoolField(TEXT("passed"), bPassed); Row->SetNumberField(TEXT("observers"), Observers.Num()); Write(Row);
		if (++CaseIndex == RelocationProbe::CaseCount)
		{
			Phase = 3; PhaseStart = Now; ForceNetUpdate();
			auto Complete = MakeShared<FJsonObject>(); Complete->SetStringField(TEXT("event"), TEXT("Complete"));
			Complete->SetBoolField(TEXT("passed"), bAllPassed); Complete->SetNumberField(TEXT("expectedCases"), RelocationProbe::CaseCount); Write(Complete);
		}
		else SetupCase();
	}
	else if (Phase == 3 && Now - PhaseStart > 3)
	{
		bFinished = true;
		FPlatformMisc::RequestExitWithStatus(false, bAllPassed ? 0 : 1);
	}
}
