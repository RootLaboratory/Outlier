#include "Network/OutlierGenerationNetworkProbe.h"
#include "Network/Stage02PlacementProbe.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "OutlierGameMode.h"
#include "OutlierGameInstance.h"
#include "OutlierPlayerState.h"
#include "Network/OutlierArenaSubsystem.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Save/OutlierCheckpoint.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/Death/OutlierDeathDebrisActor.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"
#include "Event/OutlierEventSubsystem.h"
#include "Event/OutlierEventSettings.h"
#include "Event/OutlierEventDataAsset.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/DataTable.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Serialization/JsonSerializer.h"
#include "HighResScreenshot.h"
#include "Cinematic/OutlierCinematicSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Containers/Ticker.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "FirstPerson/FirstPersonInputConfig.h"
#include "EnhancedPlayerInput.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/PanelMover.h"
#include "Room/RoomCombatBarrier.h"

// 실제 게임 클래스/스트리밍을 그대로 사용한다. 명시적인 검증 인자가 없는 실행에는 등록하지 않는다.
class FOutlierGenerationNetworkProbe
{
public:
	FString Output;
	bool bBootstrapStarted = false;
	bool bContextConfigured = false;
	bool bHandoffConfigured = false;
	TWeakObjectPtr<UOutlierAudioSubsystem> ObservedAudio;
	int32 VoiceStarted = 0;
	int32 VoiceFinished = 0;
	int32 VoiceUnavailable = 0;
	FString ActiveVoice;
	double LastSampleAt = 0;
	int32 LastCommand = 0;
	bool bCommandAccepted = false;
	FString LastAction;
	TWeakObjectPtr<APlayerController> HeldPeer;
	bool bHoldUnload = false;
	bool bHoldReady = false;
	bool bOutputChecked = false;
	bool bOutputValid = false;
	TWeakObjectPtr<AFirstPersonCharacter> InputPawn;
	TWeakObjectPtr<const UInputAction> InjectedAction;
	FVector2D InputAxes = FVector2D::ZeroVector;
	double InputUntil = 0;
	FVector InputStart = FVector::ZeroVector;
	FVector InputEnd = FVector::ZeroVector;
	TWeakObjectPtr<AActor> InputBase;
	FVector InputBaseStart = FVector::ZeroVector;
	FVector InputBaseDelta = FVector::ZeroVector;
	int32 InputFrames = 0;
	bool bStage02 = FParse::Param(FCommandLine::Get(), TEXT("OutlierStage02Validation"));

	void Tick(UWorld* World, ELevelTick, float)
	{
		if (!bOutputChecked)
		{
			bOutputChecked = true;
			Output = FPaths::ConvertRelativePathToFull(Output);
			FPaths::NormalizeDirectoryName(Output);
			FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved"));
			FPaths::NormalizeDirectoryName(Saved);
			bOutputValid = Output.StartsWith(Saved + TEXT("/"));
			UE_LOG(LogTemp, Display, TEXT("[GenerationProbe] OutputValid=%d Output=%s Saved=%s"), bOutputValid, *Output, *Saved);
			if (bOutputValid) IFileManager::Get().MakeDirectory(*Output, true);
		}
		if (!bOutputValid) return;
		if (!World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
		if (bStage02) TickStage02(World);
		if (FPlatformTime::Seconds() - LastSampleAt < 0.5) return;
		LastSampleAt = FPlatformTime::Seconds();
		if (UOutlierAudioSubsystem* Audio = World->GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>();
			Audio && ObservedAudio.Get() != Audio)
		{
			ObservedAudio = Audio;
			Audio->OnVoiceStarted.AddLambda([this](ULocalPlayer*, FGameplayTag Tag)
			{
				++VoiceStarted;
				ActiveVoice = Tag.ToString();
			});
			Audio->OnVoiceFinished.AddLambda([this](ULocalPlayer*, FGameplayTag, bool)
			{
				++VoiceFinished;
				ActiveVoice.Empty();
			});
			Audio->OnVoiceUnavailable.AddLambda([this](ULocalPlayer*, FGameplayTag) { ++VoiceUnavailable; });
		}
		UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
		AOutlierGameMode* Mode = World->GetAuthGameMode<AOutlierGameMode>();
		if (!Arena)
		{
			// 로비 Dedicated에는 Arena 서브시스템이 없다. 복귀한 연결 수는 같은 관찰기로 기록한다.
			WriteState(World, Mode, nullptr);
			return;
		}
		if (!bHandoffConfigured && World->GetNetMode() == NM_Client)
		{
			FString HandoffUrl;
			if (FParse::Value(FCommandLine::Get(), TEXT("OutlierGenerationHandoff="), HandoffUrl))
			{
				// 로비에서 Worker로 넘어갈 때의 실제 GI 진입점만 대체한다. 재접속 판정은 변경하지 않는다.
				World->GetGameInstance<UOutlierGameInstance>()->NotifyArenaHandoffStarted(HandoffUrl);
			}
			bHandoffConfigured = true;
		}
		if (Mode && !bContextConfigured && Mode->ActiveMatchPairId == INDEX_NONE)
		{
			FOutlierMatchSaveContext Context;
			FString ContextPath;
			if (FParse::Value(FCommandLine::Get(), TEXT("OutlierGenerationSaveContext="), ContextPath))
			{
				ContextPath = FPaths::ConvertRelativePathToFull(ContextPath);
				FPaths::NormalizeFilename(ContextPath);
				FString Text;
				TSharedPtr<FJsonObject> SavedContext;
				if (!ContextPath.StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved/")))
					|| !FFileHelper::LoadFileToString(Text, *ContextPath)
					|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), SavedContext)
					|| !SavedContext.IsValid()
					|| !FGuid::Parse(SavedContext->GetStringField(TEXT("OwnerId")), Context.OwnerId)
					|| !FGuid::Parse(SavedContext->GetStringField(TEXT("SaveId")), Context.SaveId)) return;
				Context.KeyVerifier = SavedContext->GetStringField(TEXT("KeyVerifier"));
				Context.bContinue = true;
			}
			else
			{
				Context.OwnerId = FGuid::NewGuid();
				Context.SaveId = FGuid::NewGuid();
				Context.KeyVerifier = UOutlierSaveSubSystem::MakeKeyVerifier(FGuid::NewGuid());
				TSharedRef<FJsonObject> SavedContext = MakeShared<FJsonObject>();
				SavedContext->SetStringField(TEXT("OwnerId"), Context.OwnerId.ToString());
				SavedContext->SetStringField(TEXT("SaveId"), Context.SaveId.ToString());
				SavedContext->SetStringField(TEXT("KeyVerifier"), Context.KeyVerifier);
				FString Text;
				FJsonSerializer::Serialize(SavedContext, TJsonWriterFactory<>::Create(&Text));
				FFileHelper::SaveStringToFile(Text, *(Output / TEXT("SaveContext.json")));
			}
			Mode->SetMatchSaveContext(Context);
			bContextConfigured = true;
		}
		// Listen에서 로비 UI 선택만 대체한다. 이후 PC 교체, 로딩 RPC, ACK와 Possess는 생산 경로다.
		if (Mode && World->GetNetMode() == NM_ListenServer && !bBootstrapStarted
			&& Mode->ActiveMatchPairId == INDEX_NONE && Arena->IsArenaContentReady())
		{
			TArray<APlayerController*> Players;
			for (auto It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get(); PC && PC->GetPlayerState<AOutlierPlayerState>()) Players.Add(PC);
			}
			if (Players.Num() == 2)
			{
				bBootstrapStarted = true;
				Mode->StartMatchedPair(Players[0], Players[1], 0,
					EOutlierPlayerRole::Shooter, EOutlierPlayerRole::Partner);
			}
		}

		FString CommandText;
		TSharedPtr<FJsonObject> Command;
		if (FFileHelper::LoadFileToString(CommandText, *(Output / TEXT("Command.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CommandText), Command) && Command.IsValid()
			&& Command->GetIntegerField(TEXT("Id")) > LastCommand)
		{
			LastCommand = Command->GetIntegerField(TEXT("Id"));
			LastAction = Command->GetStringField(TEXT("Action"));
			bCommandAccepted = Execute(World, Mode, Arena, LastAction, Command);
			UE_LOG(LogTemp, Display, TEXT("[GenerationProbe] Command=%d Action=%s Accepted=%d Gen=%u"),
				LastCommand, *LastAction, bCommandAccepted, Arena->GetGameplayGeneration());
			if (bStage02) { GLog->FlushThreadedLogs(); GLog->Flush(); }
		}
		WriteState(World, Mode, Arena);
	}

private:
	void TickStage02(UWorld* World)
	{
		// 소유 클라이언트의 Enhanced Input을 통과시킨다. 서버 위치를 써서 이동 성공을 만들지 않는다.
		if (AFirstPersonCharacter* Pawn = InputPawn.Get(); Pawn && Pawn->GetWorld() == World)
		{
			if (auto* PC = Cast<APlayerController>(Pawn->GetController()))
				if (auto* Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput); Input && InjectedAction.IsValid())
				{
					const bool bActive = FPlatformTime::Seconds() < InputUntil;
					Input->InjectInputForAction(InjectedAction.Get(), FInputActionValue(bActive ? InputAxes : FVector2D::ZeroVector));
					InputEnd = Pawn->GetActorLocation();
					InputBaseDelta = InputBase.IsValid() ? InputBase->GetActorLocation() - InputBaseStart : FVector::ZeroVector;
					if (bActive) ++InputFrames;
					else { InputPawn.Reset(); InjectedAction.Reset(); }
				}
		}
		auto Frame = MakeShared<FJsonObject>();
		Frame->SetNumberField(TEXT("At"), FPlatformTime::Seconds());
		Frame->SetNumberField(TEXT("CommandId"), LastCommand);
		Frame->SetBoolField(TEXT("InputActive"), InputPawn.IsValid());
		Frame->SetNumberField(TEXT("Generation"), World->GetSubsystem<UOutlierArenaSubsystem>() ? World->GetSubsystem<UOutlierArenaSubsystem>()->GetGameplayGeneration() : 0);
		TArray<TSharedPtr<FJsonValue>> Pawns;
		for (TActorIterator<AFirstPersonCharacter> It(World); It; ++It)
		{
			if (!Cast<AShooterCharacter>(*It) && !Cast<APartnerCharacter>(*It)) continue;
			auto Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("Role"), Cast<AShooterCharacter>(*It) ? 1 : 2);
			Row->SetBoolField(TEXT("Local"), It->IsLocallyControlled());
			Row->SetStringField(TEXT("Base"), It->GetMovementBase() ? GetPathNameSafe(It->GetMovementBase()->GetOwner()) : TEXT(""));
			const FVector P = It->GetActorLocation();
			Row->SetArrayField(TEXT("Position"), {MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)});
			bool bPenetrating = false;
			for (TActorIterator<APanelMover> Panel(World); Panel; ++Panel)
				for (const auto* Mesh : TInlineComponentArray<UStaticMeshComponent*>(*Panel))
					if (Mesh->IsQueryCollisionEnabled() && Mesh->GetCollisionResponseToChannel(It->GetCapsuleComponent()->GetCollisionObjectType()) == ECR_Block)
						bPenetrating |= Mesh->OverlapComponent(P, It->GetActorQuat(), It->GetCapsuleComponent()->GetCollisionShape(-2));
			Row->SetBoolField(TEXT("PanelPenetration"), bPenetrating);
			// 입력 중의 이탈 실패가 발판 부착인지 주변 지형인지 구분한다.
			// 현재 캡슐로 네 방향을 조회할 뿐 위치나 충돌 응답은 바꾸지 않는다.
			if (It->IsLocallyControlled() && InputPawn.Get() == *It)
			{
				TArray<TSharedPtr<FJsonValue>> Exits;
				const auto* Capsule = It->GetCapsuleComponent();
				FCollisionQueryParams Query(SCENE_QUERY_STAT(Stage02ExitDiagnostic), false, *It);
				const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
				for (const FVector Direction : {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector})
				{
					FHitResult Hit;
					const bool bHit = World->SweepSingleByChannel(Hit, P, P + Direction * 150, It->GetActorQuat(),
						Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(-2), Query, Response);
					auto Exit = MakeShared<FJsonObject>();
					Exit->SetArrayField(TEXT("Direction"), {MakeShared<FJsonValueNumber>(Direction.X), MakeShared<FJsonValueNumber>(Direction.Y)});
					Exit->SetBoolField(TEXT("Blocked"), bHit);
					Exit->SetNumberField(TEXT("Distance"), Hit.Distance);
					Exit->SetStringField(TEXT("Actor"), GetPathNameSafe(Hit.GetActor()));
					Exit->SetStringField(TEXT("Component"), GetPathNameSafe(Hit.GetComponent()));
					Exits.Add(MakeShared<FJsonValueObject>(Exit));
				}
				Row->SetArrayField(TEXT("ExitSweeps"), Exits);
			}
			bool bBarrierPenetration = false;
			for (TActorIterator<ARoomCombatBarrier> Barrier(World); Barrier; ++Barrier)
				bBarrierPenetration |= Barrier->IsBlocked() && Barrier->OverlapsJoinCapsule(P,
					It->GetCapsuleComponent()->GetScaledCapsuleRadius() - 2, It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 2);
			Row->SetBoolField(TEXT("BarrierPenetration"), bBarrierPenetration);
			Row->SetStringField(TEXT("Room"), It->GetCurrentRoomTag().ToString());
			Pawns.Add(MakeShared<FJsonValueObject>(Row));
		}
		Frame->SetArrayField(TEXT("Pawns"), Pawns);
		TArray<TSharedPtr<FJsonValue>> Panels;
		for (TActorIterator<APanelMover> It(World); It; ++It)
		{
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("Path"), It->GetPathName());
			const FVector P = It->GetActorLocation();
			Row->SetArrayField(TEXT("Position"), {MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)});
			const auto* Property = FindFProperty<FStructProperty>(It->GetClass(), TEXT("ReplicatedMotion"));
			const auto* Motion = Property ? Property->ContainerPtrToValuePtr<FPanelMoverMotionState>(*It) : nullptr;
			Row->SetBoolField(TEXT("Moving"), Motion && Motion->bMoving);
			Row->SetBoolField(TEXT("Paused"), Motion && Motion->bPaused);
			Panels.Add(MakeShared<FJsonValueObject>(Row));
		}
		Frame->SetArrayField(TEXT("Panels"), Panels);
		FString Text;
		FJsonSerializer::Serialize(Frame, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		FFileHelper::SaveStringToFile(Text + TEXT("\n"), *(Output / TEXT("Motion.jsonl")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
	}

	bool Execute(UWorld* World, AOutlierGameMode* Mode, UOutlierArenaSubsystem* Arena,
		const FString& Action, const TSharedPtr<FJsonObject>& Command)
	{
		if (bStage02 && Action == TEXT("MoveInput"))
		{
			for (auto It = World->GetPlayerControllerIterator(); It; ++It)
			{
				auto* Pawn = Cast<AFirstPersonCharacter>(It->Get()->GetPawn());
				if (!Pawn || !Pawn->IsLocallyControlled()) continue;
				const auto* Property = FindFProperty<FObjectProperty>(Pawn->GetClass(), TEXT("InputConfig"));
				auto* Config = Property ? Cast<UFirstPersonInputConfig>(Property->GetObjectPropertyValue_InContainer(Pawn)) : nullptr;
				if (!Config || !Config->MoveAction || !Cast<UEnhancedPlayerInput>(It->Get()->PlayerInput)) return false;
				InputPawn = Pawn;
				InjectedAction = Config->MoveAction;
				InputAxes = FVector2D(Command->GetNumberField(TEXT("Right")), Command->GetNumberField(TEXT("Forward")));
				double WorldX = 0, WorldY = 0;
				if (Command->TryGetNumberField(TEXT("WorldX"), WorldX) && Command->TryGetNumberField(TEXT("WorldY"), WorldY))
				{
					// 호스트와 원격 Pawn의 시작 시선이 달라도 같은 실제 출구를 시험한다.
					// 월드 방향을 입력 축으로만 변환하며 캐릭터 회전/위치는 변경하지 않는다.
					const FVector Direction(WorldX, WorldY, 0);
					InputAxes = FVector2D(FVector::DotProduct(Direction, Pawn->GetActorRightVector()),
						FVector::DotProduct(Direction, Pawn->GetActorForwardVector()));
				}
				InputUntil = FPlatformTime::Seconds() + FMath::Clamp(Command->GetNumberField(TEXT("Seconds")), 0.05, 5.0);
				InputStart = InputEnd = Pawn->GetActorLocation();
				InputBase = Pawn->GetMovementBase() ? Pawn->GetMovementBase()->GetOwner() : nullptr;
				InputBaseStart = InputBase.IsValid() ? InputBase->GetActorLocation() : FVector::ZeroVector;
				InputBaseDelta = FVector::ZeroVector;
				InputFrames = 0;
				return true;
			}
			return false;
		}
		if (bStage02 && Stage02PlacementProbe::Execute(World, Action, Command)) return true;
		if (Action == TEXT("Capture"))
		{
			FScreenshotRequest::RequestScreenshot(Output / FString::Printf(TEXT("Frame-%d.png"), LastCommand), false, false);
			return World->GetNetMode() != NM_DedicatedServer;
		}
		if (Action == TEXT("Disconnect"))
		{
			UNetDriver* Driver = World->GetNetDriver();
			if (World->GetNetMode() != NM_Client || !Driver || !Driver->ServerConnection) return false;
			// 엔진의 장애 통보가 실제 연결 종료/월드 복귀를 수행한다. GI의 재접속 상태/키는 조작하지 않는다.
			GEngine->BroadcastNetworkFailure(World, Driver, ENetworkFailure::ConnectionLost, TEXT("Generation regression disconnect"));
			return true;
		}
		if (!Mode) return false;
		AFirstPersonPlayerController* Shooter = Cast<AFirstPersonPlayerController>(Mode->GetControllerFromPlayerState(
			Mode->FindPairPlayerState(Mode->ActiveMatchPairId, EOutlierPlayerRole::Shooter)));
		AFirstPersonPlayerController* Partner = Cast<AFirstPersonPlayerController>(Mode->GetControllerFromPlayerState(
			Mode->FindPairPlayerState(Mode->ActiveMatchPairId, EOutlierPlayerRole::Partner)));
		if (!Shooter || !Partner) return false;
		if (Action == TEXT("AdvanceTurretCombat"))
		{
			URoomCombatSubsystem* Rooms = World->GetSubsystem<URoomCombatSubsystem>();
			if (!Rooms) return false;
			// 적의 실제 사망 통보로 앞 Wave를 완료한다. Room/Wave/세대 값을 강제로 변경하지 않는다.
			for (TActorIterator<AEnemyBase> It(World); It; ++It)
			{
				if (Cast<AAutoTurret>(*It) || It->IsDead() || !It->CanBeDamaged()
					|| It->GetCombatGameplayGeneration() != int32(Arena->GetGameplayGeneration())) continue;
				FOutlierDamageRequest Damage;
				Damage.DamageAmount = 100000.0f;
				Damage.DamageTag = OutlierGameplayTags::Damage::Weapon();
				Damage.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Pistol;
				Damage.EventInstigator = Shooter;
				It->ReceiveOutlierDamage(Damage);
			}
			for (TActorIterator<AAutoTurret> It(World); It; ++It)
			{
				TArray<FGameplayTag> Groups = { FGameplayTag() };
				for (TActorIterator<ARoomCombatSpawnPoint> Spawn(World); Spawn; ++Spawn)
					if (Spawn->GetRoomTag() == It->GetDefaultRoomTag()) Groups.AddUnique(Spawn->GetActivationGroupTag());
				for (FGameplayTag Group : Groups)
				{
					FRoomCombatTriggerContext Context;
					if (Rooms->CreateTriggerContext(Shooter, It->GetDefaultRoomTag(), Group, Context))
						Rooms->StartTriggeredSequence(Shooter, Context);
				}
			}
			return true;
		}
		if (Action == TEXT("ViewTurret") || Action == TEXT("DamageTurret"))
		{
			for (TActorIterator<AAutoTurret> It(World); It; ++It)
			{
				if (!It->IsDeployed() || It->IsDead()) continue;
				if (Action == TEXT("DamageTurret"))
				{
					FOutlierDamageRequest Damage;
					Damage.DamageAmount = Command->GetNumberField(TEXT("Amount"));
					Damage.DamageTag = OutlierGameplayTags::Damage::Weapon();
					Damage.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Pistol;
					Damage.StunDurationSeconds = 0.2f;
					Damage.EventInstigator = Shooter;
					Damage.DamageOrigin = Shooter->GetPawn()->GetPawnViewLocation();
					Damage.HitResult.bBlockingHit = true;
					Damage.HitResult.ImpactPoint = It->GetPawnViewLocation();
					Damage.HitResult.ImpactNormal = (Damage.DamageOrigin - Damage.HitResult.ImpactPoint).GetSafeNormal();
					const bool bApplied = It->ReceiveOutlierDamage(Damage) > 0.0f;
					if (bApplied) FScreenshotRequest::RequestScreenshot(Output / FString::Printf(TEXT("HitImmediate-%d.png"), LastCommand), false, false);
					return bApplied;
				}
				// Partner의 추적 카메라가 랙 뒤로 밀려 VFX를 가리지 않도록 관찰용 카메라를 공유한다.
				// 카메라만 교체하며 터렛 전개·피해·사망·네트워크 연출은 실제 게임 경로를 유지한다.
				const FVector CameraPosition = It->GetActorLocation() + FVector(-400, 0, 100);
				ACameraActor* Camera = World->SpawnActor<ACameraActor>(CameraPosition,
					(It->GetPawnViewLocation() - CameraPosition).Rotation());
				if (!Camera) return false;
				Camera->SetReplicates(true);
				Camera->bAlwaysRelevant = true;
				Camera->SetLifeSpan(40.0f);
				for (AFirstPersonPlayerController* PC : { Shooter, Partner })
				{
					APawn* Pawn = PC->GetPawn();
					if (!Pawn) return false;
					Pawn->SetCanBeDamaged(false);
					const FVector Position = It->GetActorLocation() + FVector(-400, PC == Shooter ? 0 : 150, 40);
					Pawn->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
					const FRotator Rotation = (It->GetActorLocation() + FVector(0, 0, 60) - (Position + FVector(0, 0, 60))).Rotation();
					PC->SetControlRotation(Rotation);
					PC->ClientSetRotation(Rotation);
					PC->SetViewTarget(Camera);
					PC->ClientSetViewTarget(Camera);
				}
				return true;
			}
			return false;
		}
		if (Action == TEXT("PresentEvent") || Action == TEXT("VerifyDuplicateEvent"))
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Command->GetStringField(TEXT("Tag"))), false);
			if (!Tag.IsValid()) return false;
			if (Action == TEXT("PresentEvent")) return UOutlierEventSubsystem::EventCallOnce(World, Tag);
			UOutlierSaveSubSystem* Saves = World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>();
			return Saves && Saves->HasFiredTutorialEvent(Tag) && !UOutlierEventSubsystem::EventCallOnce(World, Tag);
		}
		if (Action == TEXT("ViewGuide"))
		{
			const FName Widget(*Command->GetStringField(TEXT("Widget")));
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				for (UOutlierWorldGuideTargetComponent* Target : TInlineComponentArray<UOutlierWorldGuideTargetComponent*>(*It))
				{
					if (Target->WidgetID.GetTagName() != Widget || !Shooter->GetPawn()) continue;
					const FVector Anchor = Target->GetComponentLocation();
					const FVector Position = Anchor + FVector(-400, 0, 50);
					Shooter->GetPawn()->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
					const FRotator Rotation = (Anchor - (Position + FVector(0, 0, 60))).Rotation();
					Shooter->SetControlRotation(Rotation);
					Shooter->ClientSetRotation(Rotation);
					return true;
				}
			}
			return false;
		}
		if (Action == TEXT("HoldUnload") || Action == TEXT("HoldReady") || Action == TEXT("ReleaseAck"))
		{
			HeldPeer = Partner;
			bHoldUnload = Action == TEXT("HoldUnload");
			bHoldReady = Action == TEXT("HoldReady");
			return true;
		}
		if (Action == TEXT("Pause")) return Mode->SetPause(Shooter);
		if (Action == TEXT("Unpause")) return Mode->ClearPause();
		if (Action == TEXT("Restart"))
		{
			return Mode->RequestCheckpointRestart(Shooter);
		}
		if (Action == TEXT("Continue") || Action == TEXT("UnavailableOne") || Action == TEXT("UnavailableBoth"))
		{
			if (!Mode->IsPairGameplayReady() && !Mode->GameOverWorldPause.IsActive()) return false;
			if (!Mode->GameOverWorldPause.IsActive())
			{
				Mode->GameOverActivePairs.Add(Mode->ActiveMatchPairId);
				Mode->GameOverWorldPause.Begin({ Shooter, Partner });
			}
			const FGuid Round = Mode->GameOverWorldPause.GetRoundId();
			if (Action != TEXT("Continue"))
			{
				Mode->OnClientGameOverUnavailable(Shooter, Round);
				if (Action == TEXT("UnavailableBoth")) Mode->OnClientGameOverUnavailable(Partner, Round);
				return true;
			}
			Mode->OnClientGameOverReady(Shooter, Round);
			Mode->OnClientGameOverReady(Partner, Round);
			FGameOverPendingRequest Request;
			Request.RoundId = Round;
			Request.Choice = EGameOverPendingChoice::Continue;
			return Mode->ExecuteGameOverSelection(Shooter, Partner, Request);
		}
		if (Action == TEXT("Preset")) return Mode->RequestPresetRespawn(Shooter, FName(Command->GetStringField(TEXT("Stage"))));
		if (Action == TEXT("Save"))
		{
			for (TActorIterator<AOutlierCheckpoint> It(World); It; ++It)
			{
				if (It->GetCheckpointId() == FName(Command->GetStringField(TEXT("Checkpoint"))))
					return It->RefreshSnapshot(Shooter);
			}
			return false;
		}
		if (Action == TEXT("StaleAck"))
		{
			if (!Mode->bArenaReloadInProgress) return false;
			const uint32 Generation = Arena->GetGameplayGeneration();
			const int32 Pending = Mode->PendingPossessions.Num();
			const int32 Unloaded = Mode->PendingGameplayUnloadPlayers.Num();
			const int32 Ready = Mode->ReadyGameplayPossessPlayers.Num();
			const auto PendingPawn = Mode->PendingPossessions.FindRef(Partner);
			const bool bWaitingForUnload = Mode->PendingGameplayUnloadPlayers.Contains(Partner);
			const bool bReadyForPossess = Mode->ReadyGameplayPossessPlayers.Contains(Partner);
			APawn* Pawn = Partner->GetPawn();
			const int32 Attempts = Mode->ArenaReloadRecoveryAttempts;
			const double StartedAt = Mode->ArenaReloadStartedAt;
			const double ProgressAt = Mode->ArenaReloadLastProgressAt;
			// 보류 훅 자체가 구세대 ACK를 삼키면 세대 검사를 검증한 것이 아니다.
			TGuardValue<bool> UnloadGuard(bHoldUnload, false);
			TGuardValue<bool> ReadyGuard(bHoldReady, false);
			Mode->OnClientArenaReady(Partner, Generation - 1);
			Mode->OnClientArenaGameplayUnloaded(Partner, Generation - 1);
			return Arena->GetGameplayGeneration() == Generation && Mode->PendingPossessions.Num() == Pending
				&& Mode->PendingGameplayUnloadPlayers.Num() == Unloaded && Mode->ArenaReloadRecoveryAttempts == Attempts
				&& Mode->ReadyGameplayPossessPlayers.Num() == Ready && Partner->GetPawn() == Pawn
				&& Mode->PendingPossessions.FindRef(Partner) == PendingPawn
				&& Mode->PendingGameplayUnloadPlayers.Contains(Partner) == bWaitingForUnload
				&& Mode->ReadyGameplayPossessPlayers.Contains(Partner) == bReadyForPossess
				&& Mode->ArenaReloadStartedAt == StartedAt && Mode->ArenaReloadLastProgressAt == ProgressAt;
		}
		return false;
	}

	void WriteState(UWorld* World, AOutlierGameMode* Mode, UOutlierArenaSubsystem* Arena)
	{
		TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
		State->SetNumberField(TEXT("SampleAt"), LastSampleAt);
		State->SetStringField(TEXT("Map"), World->GetMapName());
		State->SetNumberField(TEXT("NetMode"), World->GetNetMode());
		State->SetNumberField(TEXT("Generation"), Arena ? Arena->GetGameplayGeneration() : 0);
		State->SetStringField(TEXT("Phase"), Arena ? StaticEnum<EOutlierGameplayReloadPhase>()->GetNameStringByValue(int64(Arena->GetGameplayReloadPhase())) : TEXT("Lobby"));
		State->SetBoolField(TEXT("LevelsReady"), !Arena || Arena->IsGameplayLevelsReady());
		State->SetBoolField(TEXT("PairReady"), Mode && Mode->IsPairGameplayReady());
		State->SetBoolField(TEXT("Paused"), World->IsPaused());
		State->SetNumberField(TEXT("CommandId"), LastCommand);
		State->SetBoolField(TEXT("CommandAccepted"), bCommandAccepted);
		State->SetNumberField(TEXT("RecoveryAttempts"), Mode ? Mode->ArenaReloadRecoveryAttempts : 0);
		State->SetNumberField(TEXT("ReloadElapsed"), Mode && Mode->bArenaReloadInProgress ? FPlatformTime::Seconds() - Mode->ArenaReloadStartedAt : 0);
		State->SetBoolField(TEXT("PendingIntro"), Mode && Mode->bPendingInitialIntro);
		State->SetBoolField(TEXT("Reloading"), Mode && Mode->bArenaReloadInProgress);
		State->SetBoolField(TEXT("GameOverActive"), Mode && Mode->GameOverWorldPause.IsActive());
		State->SetNumberField(TEXT("VoiceStarted"), VoiceStarted);
		State->SetNumberField(TEXT("VoiceFinished"), VoiceFinished);
		State->SetNumberField(TEXT("VoiceUnavailable"), VoiceUnavailable);
		State->SetStringField(TEXT("ActiveVoice"), ActiveVoice);
		if (UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>())
		{
			State->SetNumberField(TEXT("RegisteredEnemies"), Adaptation->GetRegisteredEnemyCount());
			State->SetNumberField(TEXT("AdaptationStack"), Adaptation->GetCurrentGunAdaptationStack());
		}
		TArray<TSharedPtr<FJsonValue>> Players;
		for (auto It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC) continue;
			TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
			Player->SetStringField(TEXT("Controller"), PC->GetClass()->GetName());
			Player->SetStringField(TEXT("Pawn"), GetNameSafe(PC->GetPawn()));
			Player->SetBoolField(TEXT("Local"), PC->IsLocalController());
			if (APawn* Pawn = PC->GetPawn())
			{
				Player->SetStringField(TEXT("Location"), Pawn->GetActorLocation().ToString());
				const FVector Location = Pawn->GetActorLocation();
				Player->SetArrayField(TEXT("Position"), { MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y), MakeShared<FJsonValueNumber>(Location.Z) });
			}
			if (const AOutlierPlayerState* PS = PC->GetPlayerState<AOutlierPlayerState>())
			{
				Player->SetNumberField(TEXT("PairId"), PS->GetPairId());
				Player->SetNumberField(TEXT("Role"), int32(PS->GetPlayerRole()));
				Player->SetNumberField(TEXT("NodeCount"), PS->GetNodeCount());
				Player->SetBoolField(TEXT("Suit"), PS->GetAcquiredSuit());
				Player->SetBoolField(TEXT("GameplayPawn"),
					(PS->GetPlayerRole() == EOutlierPlayerRole::Shooter && Cast<AShooterCharacter>(PC->GetPawn()))
					|| (PS->GetPlayerRole() == EOutlierPlayerRole::Partner && Cast<APartnerCharacter>(PC->GetPawn())));
			}
			if (ULocalPlayer* Local = PC->GetLocalPlayer())
			{
				if (UOutlierCinematicSubsystem* Intro = Local->GetSubsystem<UOutlierCinematicSubsystem>())
					Player->SetNumberField(TEXT("CinematicState"), int32(Intro->GetState()));
			}
			Players.Add(MakeShared<FJsonValueObject>(Player));
		}
		State->SetArrayField(TEXT("Players"), Players);
		if (bStage02)
		{
			Stage02PlacementProbe::Observe(World, State);
			State->SetBoolField(TEXT("InputActive"), InputPawn.IsValid());
			State->SetNumberField(TEXT("InputFrames"), InputFrames);
			State->SetNumberField(TEXT("InputDistance"), (InputEnd - InputStart - InputBaseDelta).Size2D());
		}
		TArray<TSharedPtr<FJsonValue>> Checkpoints;
		for (TActorIterator<AOutlierCheckpoint> It(World); It; ++It)
			Checkpoints.Add(MakeShared<FJsonValueString>(It->GetCheckpointId().ToString()));
		State->SetArrayField(TEXT("Checkpoints"), Checkpoints);
		int32 DebrisCount = 0;
		for (TActorIterator<AOutlierDeathDebrisActor> It(World); It; ++It) ++DebrisCount;
		State->SetNumberField(TEXT("DebrisCount"), DebrisCount);
		int32 ActiveTurrets = 0;
		for (TActorIterator<AAutoTurret> It(World); It; ++It)
			if (It->IsDeployed() && !It->IsDead()) ++ActiveTurrets;
		State->SetNumberField(TEXT("ActiveTurrets"), ActiveTurrets);
		if (AOutlierWorldGuideManager* Guides = AOutlierWorldGuideManager::Find(World))
		{
			State->SetNumberField(TEXT("PendingGuides"), Guides->GetPendingCount());
			State->SetStringField(TEXT("ActiveGuide"), Guides->ActiveContext.WidgetID.ToString());
			TArray<TSharedPtr<FJsonValue>> Candidates;
			const UOutlierEventSettings* Settings = GetDefault<UOutlierEventSettings>();
			for (UOutlierEventDataAsset* Asset : { Settings->ShooterDataAsset.LoadSynchronous(), Settings->PartnerDataAsset.LoadSynchronous() })
			{
				UDataTable* Table = Asset ? Asset->EventTable.LoadSynchronous() : nullptr;
				if (!Table || Table->GetRowStruct() != FOutlierEventDataRow::StaticStruct()) continue;
				TArray<FOutlierEventDataRow*> Rows;
				Table->GetAllRows(TEXT("GenerationPresentation"), Rows);
				for (const FOutlierEventDataRow* Row : Rows)
				{
					if (!Row->EventTag.IsValid()) continue;
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("Tag"), Row->EventTag.ToString());
					Entry->SetStringField(TEXT("TTS"), Row->TTSTag.ToString());
					Entry->SetStringField(TEXT("Widget"), Row->WorldGuideID.ToString());
					Entry->SetNumberField(TEXT("Targets"), Guides->GetTargetCount(Row->WorldGuideID));
					UOutlierSaveSubSystem* Saves = World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>();
					Entry->SetBoolField(TEXT("Fired"), Saves && Saves->HasFiredTutorialEvent(Row->EventTag));
					Candidates.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
			State->SetArrayField(TEXT("PresentationCandidates"), Candidates);
		}
		int32 VisibleGuides = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
			for (UOutlierWorldGuideTargetComponent* Target : TInlineComponentArray<UOutlierWorldGuideTargetComponent*>(*It))
				if (Target->IsGuideVisible()) ++VisibleGuides;
		State->SetNumberField(TEXT("VisibleGuides"), VisibleGuides);
		if (UOutlierSaveSubSystem* Saves = World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
		{
			State->SetBoolField(TEXT("InitialSnapshot"), Saves->HasInitialSnapshot());
			State->SetBoolField(TEXT("DurableSnapshot"), Saves->HasLatestCheckpointSnapshot());
			State->SetNumberField(TEXT("FiredEvents"), Saves->GetFiredTutorialEvents().Num());
			FOutlierCheckpointSnapshot Snapshot;
			if (Saves->GetRestoreSnapshot(Snapshot))
			{
				FString SnapshotText;
				FOutlierCheckpointSnapshot::StaticStruct()->ExportText(SnapshotText, &Snapshot, nullptr, nullptr, PPF_None, nullptr);
				State->SetStringField(TEXT("SnapshotText"), SnapshotText);
				State->SetStringField(TEXT("SnapshotCheckpoint"), Snapshot.CheckpointId.ToString());
				State->SetBoolField(TEXT("SnapshotInitial"), Snapshot.bInitialSnapshot);
				for (const auto& Spawn : { TPair<const TCHAR*, FVector>(TEXT("ShooterSpawn"), Snapshot.ShooterSpawnTransform.GetLocation()),
					TPair<const TCHAR*, FVector>(TEXT("PartnerSpawn"), Snapshot.PartnerSpawnTransform.GetLocation()) })
					State->SetArrayField(Spawn.Key, { MakeShared<FJsonValueNumber>(Spawn.Value.X), MakeShared<FJsonValueNumber>(Spawn.Value.Y), MakeShared<FJsonValueNumber>(Spawn.Value.Z) });
			}
		}
		FString Text;
		FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Text));
		const FString Temporary = Output / TEXT("State.tmp");
		if (FFileHelper::SaveStringToFile(Text, *Temporary))
			IFileManager::Get().Move(*(Output / TEXT("State.json")), *Temporary, true, true);
		FFileHelper::SaveStringToFile(Text.Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT("")) + TEXT("\n"),
			*(Output / TEXT("Samples.jsonl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
	}
};

namespace
{
	FOutlierGenerationNetworkProbe GenerationProbe;
	struct FRegisterGenerationProbe
	{
		FTSTicker::FDelegateHandle Handle;

		FRegisterGenerationProbe()
		{
			if (!FParse::Value(FCommandLine::Get(), TEXT("OutlierGenerationOutput="), GenerationProbe.Output)) return;
			// 모듈 정적 초기화 시점에는 ProjectDir가 아직 확정되지 않을 수 있다.
			// 쓰기 경로 검사는 첫 월드 Tick에서, 파일 접근보다 먼저 수행한다.
			// Actor Tick은 pause 중 멈춘다. 장애 검증 명령/관찰은 생산 감시기와 같이 실제 시간을 사용한다.
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime)
			{
				if (GEngine)
				{
					for (const FWorldContext& Context : GEngine->GetWorldContexts())
					{
						GenerationProbe.Tick(Context.World(), LEVELTICK_All, DeltaTime);
					}
				}
				return true;
			}), GenerationProbe.bStage02 ? 0.0f : 0.5f);
		}

		~FRegisterGenerationProbe()
		{
			if (Handle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Handle);
		}
	} RegisterGenerationProbe;
}

bool OutlierGenerationTesting::ShouldHoldAck(const APlayerController* Controller, bool bUnloadAck)
{
	return GenerationProbe.HeldPeer.Get() == Controller
		&& (bUnloadAck ? GenerationProbe.bHoldUnload : GenerationProbe.bHoldReady);
}

#endif
