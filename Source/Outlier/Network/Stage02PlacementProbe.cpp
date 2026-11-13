#include "Network/Stage02PlacementProbe.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/PanelMover.h"
#include "Interaction/PanelMoverSwitch.h"
#include "Interaction/InteractableDoor.h"
#include "OutlierPlayerState.h"
#include "Misc/PackagePath.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Save/OutlierCheckpoint.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Stage02PlacementProbe
{
	namespace
	{
		FString FixtureDiagnostic;
		TSharedRef<FJsonObject> VectorJson(const FVector& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("X"), Value.X);
			Result->SetNumberField(TEXT("Y"), Value.Y);
			Result->SetNumberField(TEXT("Z"), Value.Z);
			return Result;
		}

		template<class T> T* ObjectProperty(UObject* Owner, FName Name)
		{
			const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name);
			return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
		}

		FName PanelId(APanelMover* Panel)
		{
			const FNameProperty* Property = FindFProperty<FNameProperty>(Panel->GetClass(), TEXT("PanelId"));
			return Property ? Property->GetPropertyValue_InContainer(Panel) : NAME_None;
		}

		bool IsPlacement(AActor* Actor)
		{
			// 검증용으로 Spawn한 복제물이 실제 배치 결과에 섞이지 않게 한다.
			return IsValid(Actor) && Actor->HasAnyFlags(RF_WasLoaded) && Actor->HasActorBegunPlay();
		}

		void ActorJson(AActor* Actor, TSharedRef<FJsonObject> Row)
		{
			const FString SourcePackage = Actor->GetPackage()->GetLoadedPath().GetPackageName();
			const FString Level = Actor->GetLevel()->GetOutermost()->GetName();
			Row->SetStringField(TEXT("Path"), Actor->GetPathName());
			Row->SetStringField(TEXT("Class"), Actor->GetClass()->GetPathName());
			Row->SetStringField(TEXT("Level"), Level);
			Row->SetStringField(TEXT("SourcePackage"), SourcePackage);
			Row->SetBoolField(TEXT("Stage02"), Level.Contains(TEXT("Stage02")) || SourcePackage.Contains(TEXT("Stage02")));
			Row->SetObjectField(TEXT("Position"), VectorJson(Actor->GetActorLocation()));
		}

		APanelMoverSwitch* FindSwitch(UWorld* World, APanelMover* Panel)
		{
			for (TActorIterator<APanelMoverSwitch> It(World); It; ++It)
				if (IsPlacement(*It) && It->TargetPanel == Panel) return *It;
			return nullptr;
		}

		ACharacter* FindPlayer(UWorld* World, int32 Role)
		{
			// 원격 Pawn은 클라이언트에 Controller가 없다. 복제된 PlayerState의
			// 현재 역할 Pawn을 찾아 서버와 관찰자 모두 같은 두 캐릭터를 검사한다.
			for (TActorIterator<ACharacter> It(World); It; ++It)
			{
				const AOutlierPlayerState* PS = It->GetPlayerState<AOutlierPlayerState>();
				if (!PS || int32(PS->GetPlayerRole()) != Role) continue;
				if (PS->GetShooterCharacter() == *It || PS->GetPartnerCharacter() == *It) return *It;
			}
			return nullptr;
		}

		bool Board(UWorld* World, APanelMover* Panel, ACharacter* Player)
		{
			UStaticMeshComponent* Mesh = ObjectProperty<UStaticMeshComponent>(Panel, TEXT("PanelMesh"));
			if (!Mesh || !Player || !Player->GetCapsuleComponent() || !Player->GetCharacterMovement()) return false;
			const FBox Bounds = Mesh->Bounds.GetBox();
			const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
			const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			FCollisionQueryParams Query(SCENE_QUERY_STAT(Stage02BoardPanel), false, Player);
			int32 SurfaceHits = 0;
			FString Obstacle;
			// Bounds의 위가 실제 발판 표면이라는 보장은 없다. 메시를 직접 Trace하고
			// 다른 플레이어/천장까지 포함한 캡슐 여유가 있는 지점만 준비 위치로 쓴다.
			for (const float Y : {0.0f, -0.25f, 0.25f, -0.5f, 0.5f, -0.75f, 0.75f})
			{
				for (const float X : {0.0f, -0.25f, 0.25f, -0.5f, 0.5f, -0.75f, 0.75f})
				{
					const FVector XY = Bounds.GetCenter() + FVector(X * Bounds.GetExtent().X, Y * Bounds.GetExtent().Y, 0);
					FHitResult Hit;
					if (!Mesh->LineTraceComponent(Hit, FVector(XY.X, XY.Y, Bounds.Max.Z + 100),
						FVector(XY.X, XY.Y, Bounds.Min.Z - 100), Query) || Hit.ImpactNormal.Z < 0.5f) continue;
					++SurfaceHits;
					const FVector Location = Hit.ImpactPoint + FVector(0, 0, HalfHeight + 2.5f);
					if (World->OverlapBlockingTestByChannel(Location, Capsule->GetComponentQuat(),
						Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(-0.1f), Query,
						FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
					{
						TArray<FOverlapResult> Overlaps;
						World->OverlapMultiByChannel(Overlaps, Location, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
							Capsule->GetCollisionShape(-0.1f), Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
						for (const FOverlapResult& Overlap : Overlaps)
							if (Overlap.bBlockingHit) Obstacle = GetPathNameSafe(Overlap.GetComponent());
						continue;
					}
					Player->GetCharacterMovement()->StopMovementImmediately();
					Player->SetBase(nullptr);
					const bool bPlaced = Player->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
					Player->ForceNetUpdate();
					return bPlaced;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Board rejected Panel=%s Pawn=%s SurfaceHits=%d Obstacle=%s Collision=%s"),
				*PanelId(Panel).ToString(), *Player->GetName(), SurfaceHits, *Obstacle, *Mesh->GetCollisionProfileName().ToString());
			FixtureDiagnostic = FString::Printf(TEXT("Board rejected Panel=%s Pawn=%s SurfaceHits=%d Obstacle=%s"),
				*PanelId(Panel).ToString(), *Player->GetName(), SurfaceHits, *Obstacle);
			return false;
		}

		bool PrepareBarrier(UWorld* World, const TSharedPtr<FJsonObject>& Command)
		{
			FString Requested;
			if (!Command->TryGetStringField(TEXT("Barrier"), Requested)) return false;
			ARoomCombatBarrier* Barrier = nullptr;
			for (TActorIterator<ARoomCombatBarrier> It(World); It; ++It)
				if (IsPlacement(*It) && It->GetPathName() == Requested) { Barrier = *It; break; }
			ACharacter* Shooter = FindPlayer(World, int32(EOutlierPlayerRole::Shooter));
			ACharacter* Partner = FindPlayer(World, int32(EOutlierPlayerRole::Partner));
			URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
			UBoxComponent* Box = Barrier ? ObjectProperty<UBoxComponent>(Barrier, TEXT("BlockingBox")) : nullptr;
			if (!Barrier || Barrier->IsBlocked() || !Box || !Shooter || !Partner || !Combat)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Barrier fixture prerequisites Path=%s Barrier=%s Blocked=%d Box=%s Shooter=%s Partner=%s Combat=%s"),
					*Requested, *GetNameSafe(Barrier), Barrier ? Barrier->IsBlocked() : -1, *GetNameSafe(Box), *GetNameSafe(Shooter), *GetNameSafe(Partner), *GetNameSafe(Combat));
				return false;
			}
			TArray<FGameplayTag> Tags;
			Barrier->GetSafetyRoomTags(Tags);
			if (!Tags.ContainsByPredicate([](FGameplayTag Tag) { return Tag.ToString().StartsWith(TEXT("Room.Level02")); })) return false;
			FString RequestedRoom;
			if (!Command->TryGetStringField(TEXT("Room"), RequestedRoom)) return false;
			const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(*RequestedRoom), false);
			// 공유 출입구는 완료된 선행 방도 연결한다. 준비할 실제 방을 명시해
			// 태그 순서나 우연히 시작 가능한 다른 방으로 테스트가 통과하지 않게 한다.
			if (!Tags.Contains(RoomTag) || !Barrier->CanBlockRoom(RoomTag) || !Combat->GetRegisteredRoomVolume(RoomTag))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Barrier fixture room rejected Path=%s Room=%s Internal=%d"),
					*Requested, *RequestedRoom, Barrier->IsGroupInternalPassage());
				return false;
			}

			const UCapsuleComponent* SC = Shooter->GetCapsuleComponent();
			const UCapsuleComponent* PC = Partner->GetCapsuleComponent();
			if (!SC || !PC) return false;
			const FVector Extent = Box->GetScaledBoxExtent();
			const bool bThinX = Extent.X <= Extent.Y;
			const FVector Tangent = Box->GetComponentQuat().GetAxisY() * (bThinX ? 1 : 0)
				+ Box->GetComponentQuat().GetAxisX() * (bThinX ? 0 : 1);
			const float Separation = (SC->GetScaledCapsuleRadius() + PC->GetScaledCapsuleRadius() + 10) * 0.5f;
			const float AvailableWidth = bThinX ? Extent.Y : Extent.X;
			if (AvailableWidth < Separation + FMath::Max(SC->GetScaledCapsuleRadius(), PC->GetScaledCapsuleRadius()))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Barrier fixture too narrow Extent=%s Width=%f Separation=%f"), *Extent.ToString(), AvailableWidth, Separation);
				return false;
			}
			FVector Locations[2] = {Box->GetComponentLocation() - Tangent * Separation, Box->GetComponentLocation() + Tangent * Separation};
			Locations[0].Z = Box->Bounds.GetBox().Min.Z + SC->GetScaledCapsuleHalfHeight() + 2;
			Locations[1].Z = Box->Bounds.GetBox().Min.Z + PC->GetScaledCapsuleHalfHeight() + 2;
			ACharacter* Players[] = {Shooter, Partner};
			// 열린 차단면과 겹치되 두 캡슐은 서로 분리한다. 주변 지형을 비우거나
			// 충돌을 끄지 않고 실제 배치에서 준비할 수 있는 경우에만 전투를 요청한다.
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const UCapsuleComponent* Capsule = Players[Index]->GetCapsuleComponent();
				FCollisionQueryParams Query(SCENE_QUERY_STAT(Stage02BarrierFixture), false);
				Query.AddIgnoredActor(Shooter);
				Query.AddIgnoredActor(Partner);
				// 차단 Box의 아래 면은 바닥 높이가 아니다. 실제 지형이 가로막으면
				// Box 내부 높이만 유한하게 탐색하여 캡슐 여유가 있는 출입구를 찾는다.
				const float MaximumZ = Box->Bounds.GetBox().Max.Z - Capsule->GetScaledCapsuleHalfHeight() - 2;
				for (float Z = Locations[Index].Z; Z <= MaximumZ; Z += 25)
				{
					FVector Candidate = Locations[Index];
					Candidate.Z = Z;
					if (!World->OverlapBlockingTestByChannel(Candidate, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
						Capsule->GetCollisionShape(-0.1f), Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
					{
						Locations[Index] = Candidate;
						break;
					}
				}
				if (!Barrier->OverlapsJoinCapsule(Locations[Index], Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight())
					|| World->OverlapBlockingTestByChannel(Locations[Index], Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
						Capsule->GetCollisionShape(-0.1f), Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
				{
					TArray<FOverlapResult> Hits;
					World->OverlapMultiByChannel(Hits, Locations[Index], Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
						Capsule->GetCollisionShape(-0.1f), Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
					UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Barrier fixture rejected Role=%d Location=%s BoxMin=%s Extent=%s"),
						Index + 1, *Locations[Index].ToString(), *Box->Bounds.GetBox().Min.ToString(), *Extent.ToString());
					for (const auto& Hit : Hits)
						if (Hit.bBlockingHit) UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Barrier fixture obstacle=%s"), *GetPathNameSafe(Hit.GetComponent()));
					return false;
				}
			}
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Players[Index]->GetCharacterMovement()->StopMovementImmediately();
				Players[Index]->SetBase(nullptr);
				if (!Players[Index]->SetActorLocation(Locations[Index], false, nullptr, ETeleportType::TeleportPhysics)) return false;
				Players[Index]->ForceNetUpdate();
			}
			FRoomCombatPreparationContext Context;
			if (Combat->BeginInitialDetectionPreparation(RoomTag, Shooter, Context))
			{
				// Complete는 안전 분리가 pending이면 false여도 뒤의 정식 콜백이 재개한다.
				Combat->CompleteInitialDetectionPreparation(Context);
				return true;
			}
			UE_LOG(LogTemp, Warning, TEXT("[Stage02Probe] Barrier fixture preparation rejected Path=%s Room=%s"), *Requested, *RequestedRoom);
			return false;
		}
	}

	void Observe(UWorld* World, TSharedRef<FJsonObject> State)
	{
		State->SetStringField(TEXT("FixtureDiagnostic"), FixtureDiagnostic);
		if (!World) return;
		UOutlierSaveSubSystem* Save = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
		TArray<TSharedPtr<FJsonValue>> Panels;
		for (TActorIterator<APanelMover> It(World); It; ++It)
		{
			APanelMover* Panel = *It;
			if (!IsPlacement(Panel)) continue;
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			ActorJson(Panel, Row);
			const FName Id = PanelId(Panel);
			Row->SetStringField(TEXT("Id"), Id.ToString());
			Row->SetBoolField(TEXT("Moved"), Save && Save->HasWorldProgress(EOutlierWorldProgressType::MovedPanel, Id));
			if (AActor* Target = ObjectProperty<AActor>(Panel, TEXT("TargetLocationActor")))
			{
				Row->SetStringField(TEXT("TargetPath"), Target->GetPathName());
				Row->SetObjectField(TEXT("Target"), VectorJson(Target->GetActorLocation()));
			}
			if (const FFloatProperty* Duration = FindFProperty<FFloatProperty>(Panel->GetClass(), TEXT("MoveDuration")))
				Row->SetNumberField(TEXT("Duration"), Duration->GetPropertyValue_InContainer(Panel));
			if (UStaticMeshComponent* Mesh = ObjectProperty<UStaticMeshComponent>(Panel, TEXT("PanelMesh")))
			{
				Row->SetObjectField(TEXT("SurfaceMin"), VectorJson(Mesh->Bounds.GetBox().Min));
				Row->SetObjectField(TEXT("SurfaceMax"), VectorJson(Mesh->Bounds.GetBox().Max));
				Row->SetStringField(TEXT("SurfacePath"), Mesh->GetPathName());
				Row->SetBoolField(TEXT("SurfaceCollision"), Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
			}
			if (APanelMoverSwitch* Switch = FindSwitch(World, Panel))
			{
				Row->SetStringField(TEXT("SwitchId"), Switch->SwitchId.ToString());
				Row->SetStringField(TEXT("SwitchPath"), Switch->GetPathName());
				Row->SetBoolField(TEXT("SwitchActivated"), Switch->bIsActivated);
				Row->SetBoolField(TEXT("SwitchBlocked"), Switch->IsInteractionBlocked());
			}
			Panels.Add(MakeShared<FJsonValueObject>(Row));
		}
		State->SetArrayField(TEXT("Panels"), Panels);

		TArray<TSharedPtr<FJsonValue>> Barriers;
		for (TActorIterator<ARoomCombatBarrier> It(World); It; ++It)
		{
			if (!IsPlacement(*It)) continue;
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			ActorJson(*It, Row);
			Row->SetBoolField(TEXT("Blocked"), It->IsBlocked());
			Row->SetBoolField(TEXT("GroupInternalPassage"), It->IsGroupInternalPassage());
			TArray<TSharedPtr<FJsonValue>> OverlappingRoles;
			for (int32 Role : {1, 2})
				if (auto* Player = FindPlayer(World, Role))
					if (It->OverlapsJoinCapsule(Player->GetActorLocation(), Player->GetCapsuleComponent()->GetScaledCapsuleRadius() - 2,
						Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 2)) OverlappingRoles.Add(MakeShared<FJsonValueNumber>(Role));
			Row->SetArrayField(TEXT("OverlappingRoles"), OverlappingRoles);
			TArray<FGameplayTag> Tags;
			It->GetSafetyRoomTags(Tags);
			TArray<TSharedPtr<FJsonValue>> ObservedRoles;
			TArray<TSharedPtr<FJsonValue>> InsideRoomRoles;
			TSharedRef<FJsonObject> InsideRoomRolesByTag = MakeShared<FJsonObject>();
			for (FGameplayTag Tag : Tags) InsideRoomRolesByTag->SetArrayField(Tag.ToString(), TArray<TSharedPtr<FJsonValue>>());
			const auto* Combat = World->GetSubsystem<URoomCombatSubsystem>();
			for (int32 Role : {1, 2})
			{
				const ACharacter* Player = FindPlayer(World, Role);
				if (!Player || !Player->GetCapsuleComponent()) continue;
				ObservedRoles.Add(MakeShared<FJsonValueNumber>(Role));
				const FVector Extent(Player->GetCapsuleComponent()->GetScaledCapsuleRadius(), Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),
					Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				bool bInsideAnyRoom = false;
				for (FGameplayTag Tag : Tags)
				{
					const ARoomVolume* Room = Combat ? Combat->GetRegisteredRoomVolume(Tag) : nullptr;
					bool bInside = Room != nullptr;
					for (int32 Corner = 0; bInside && Corner < 8; ++Corner)
						bInside = Room->ContainsWorldLocation(Player->GetActorLocation() + Extent * FVector(
							(Corner & 1) ? 1 : -1, (Corner & 2) ? 1 : -1, (Corner & 4) ? 1 : -1));
					if (bInside)
					{
						bInsideAnyRoom = true;
						TArray<TSharedPtr<FJsonValue>> RoomRoles = InsideRoomRolesByTag->GetArrayField(Tag.ToString());
						RoomRoles.Add(MakeShared<FJsonValueNumber>(Role));
						InsideRoomRolesByTag->SetArrayField(Tag.ToString(), RoomRoles);
					}
				}
				if (bInsideAnyRoom) InsideRoomRoles.Add(MakeShared<FJsonValueNumber>(Role));
			}
			Row->SetArrayField(TEXT("ObservedRoles"), ObservedRoles);
			Row->SetArrayField(TEXT("InsideRoomRoles"), InsideRoomRoles);
			Row->SetObjectField(TEXT("InsideRoomRolesByTag"), InsideRoomRolesByTag);
			TArray<TSharedPtr<FJsonValue>> RoomTags;
			for (const FGameplayTag Tag : Tags) RoomTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			Row->SetArrayField(TEXT("RoomTags"), RoomTags);
			Barriers.Add(MakeShared<FJsonValueObject>(Row));
		}
		State->SetArrayField(TEXT("Barriers"), Barriers);
		TArray<TSharedPtr<FJsonValue>> CheckpointClearance;
		for (TActorIterator<AOutlierCheckpoint> It(World); It; ++It)
		{
			if (!It->GetCheckpointId().ToString().StartsWith(TEXT("Level02"))) continue;
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("Id"), It->GetCheckpointId().ToString());
			const FVector Position = It->GetPartnerSpawnTransform().GetLocation();
			Row->SetObjectField(TEXT("PartnerSpawn"), VectorJson(Position));
			const auto* Capsule = FindPlayer(World, 2) ? FindPlayer(World, 2)->GetCapsuleComponent()
				: GetDefault<APartnerCharacter>()->GetCapsuleComponent();
			TArray<FOverlapResult> Overlaps;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(Stage02CheckpointClearance));
			// 복원 직후에는 검사 대상 Partner 자신이 이 시작점에 서 있다.
			// 자기 캡슐을 출입구 장애물로 보고 반복 검증을 중단하지 않는다.
			Query.AddIgnoredActor(FindPlayer(World, 2));
			World->OverlapMultiByChannel(Overlaps, Position, FQuat::Identity, Capsule->GetCollisionObjectType(),
				Capsule->GetCollisionShape(), Query,
				FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
			TArray<TSharedPtr<FJsonValue>> Blockers;
			for (const auto& Overlap : Overlaps)
				if (Overlap.bBlockingHit) Blockers.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Overlap.GetComponent())));
			Row->SetArrayField(TEXT("PartnerBlockers"), Blockers);
			CheckpointClearance.Add(MakeShared<FJsonValueObject>(Row));
		}
		State->SetArrayField(TEXT("CheckpointClearance"), CheckpointClearance);
	}

	bool Execute(UWorld* World, const FString& Action, TSharedPtr<FJsonObject> Command)
	{
		if (!World || World->GetNetMode() == NM_Client || !Command.IsValid()) return false;
		if (Action == TEXT("PreparePanelArea"))
		{
			// 구간을 건너뛰어 배치하므로 기존 QA 무적만 사용한다. 레이저를 끄면
			// 튜토리얼 진행까지 완료되므로 원본 환경·충돌·진행은 그대로 둔다.
			ACharacter* Shooter = FindPlayer(World, int32(EOutlierPlayerRole::Shooter));
			ACharacter* Partner = FindPlayer(World, int32(EOutlierPlayerRole::Partner));
			AOutlierPlayerState* PS = Shooter ? Shooter->GetPlayerState<AOutlierPlayerState>() : nullptr;
			if (!PS || !Partner || !Partner->GetCapsuleComponent() || !PS->SetDebugPairInvulnerable(true)) return false;
			// 실제 통과한 문이 열린 상태를 준비한다. 체크포인트 Refresh만 호출하면
			// 문 앞 Partner 시작점을 닫힌 문 안에 저장하는 비정상 fixture가 된다.
			for (TActorIterator<AOutlierCheckpoint> It(World); It; ++It)
			{
				if (It->GetCheckpointId() != TEXT("Level02")) continue;
				const auto* Capsule = Partner->GetCapsuleComponent();
				TArray<FOverlapResult> Overlaps;
				World->OverlapMultiByChannel(Overlaps, It->GetPartnerSpawnTransform().GetLocation(), FQuat::Identity,
					Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), FCollisionQueryParams(),
					FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
				for (const auto& Overlap : Overlaps)
					if (auto* Door = Cast<AInteractableDoor>(Overlap.GetActor()); Overlap.bBlockingHit && Door)
					{
						if (!Door->TrySetDoorOpen(true)) return false;
						UE_LOG(LogTemp, Display, TEXT("[Stage02Probe] Prepared checkpoint passage Door=%s Id=%s"), *Door->GetPathName(), *Door->DoorId.ToString());
					}
			}
			return true;
		}
		if (Action != TEXT("BoardPanel") && Action != TEXT("ActivatePanel") && Action != TEXT("PrepareBarrier")) return false;
		if (Action == TEXT("PrepareBarrier")) return PrepareBarrier(World, Command);
		FString Requested;
		if (!Command->TryGetStringField(TEXT("Panel"), Requested) || Requested.IsEmpty()) return false;
		APanelMover* Panel = nullptr;
		for (TActorIterator<APanelMover> It(World); It; ++It)
		{
			if (IsPlacement(*It) && (PanelId(*It).ToString() == Requested || It->GetPathName() == Requested))
			{
				// 중복 ID를 임의로 선택하면 다른 패널을 검사한 결과가 된다.
				if (Panel) return false;
				Panel = *It;
			}
		}
		if (!Panel) return false;
		if (Action == TEXT("BoardPanel"))
		{
			double Role = 0;
			if (!Command->TryGetNumberField(TEXT("Role"), Role)) return false;
			return Board(World, Panel, FindPlayer(World, int32(Role)));
		}

		APanelMoverSwitch* Switch = FindSwitch(World, Panel);
		AShooterCharacter* Shooter = Cast<AShooterCharacter>(FindPlayer(World, int32(EOutlierPlayerRole::Shooter)));
		APartnerCharacter* Partner = Cast<APartnerCharacter>(FindPlayer(World, int32(EOutlierPlayerRole::Partner)));
		if (!Switch || !Shooter || !Partner) return false;
		// 미니게임 결과만 준비한다. 해금 RPC, 진행 저장, 실제 스위치→패널 호출은
		// 플레이와 같은 API를 거치며 활성화/이동 상태를 직접 덮어쓰지 않는다.
		if (Switch->IsInteractionBlocked())
		{
			UHackableComponent* Hack = Switch->GetHackableComponent();
			if (!Hack) return false;
			FHackResultContext Context;
			Context.InstigatorActor = Partner;
			Context.TargetActor = Switch;
			Context.Result = EHackResult::Success;
			Hack->CompleteHack(Context);
		}
		return Switch->Interact(Shooter);
	}
}

#endif
