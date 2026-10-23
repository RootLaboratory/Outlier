#include "OutlierGameMode.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Save/PresetPlayerStart.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterPlayerController.h"

bool AOutlierGameMode::IsDirectPairTestWorld(const UWorld* World)
{
	if (!World || World->WorldType != EWorldType::PIE)
	{
		return false;
	}
	if (const AOutlierGameMode* Mode = World->GetAuthGameMode<AOutlierGameMode>())
	{
		return Mode->bDirectPairTest;
	}

	// 클라이언트에는 GameMode 인스턴스가 없다. GameState 복제 전에도 맵의 Override를 읽을 수 있다.
	const AGameStateBase* State = World->GetGameState();
	const AWorldSettings* Settings = World->GetWorldSettings();
	const TSubclassOf<AGameModeBase> ModeClass = State && State->GameModeClass
		? State->GameModeClass : Settings ? Settings->DefaultGameMode : nullptr;
	const AOutlierGameMode* Defaults = ModeClass
		? Cast<AOutlierGameMode>(ModeClass->GetDefaultObject()) : nullptr;
	return Defaults && Defaults->bDirectPairTest;
}

bool AOutlierGameMode::ResolveDirectPairTestSpawns(
	FTransform& ShooterSpawn, FTransform& PartnerSpawn) const
{
	TArray<AActor*> Starts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), Starts);
	APlayerStart* ShooterStart = nullptr;
	APlayerStart* PartnerStart = nullptr;
	int32 ShooterCount = 0;
	int32 PartnerCount = 0;
	for (AActor* Actor : Starts)
	{
		APlayerStart* Start = Cast<APlayerStart>(Actor);
		if (!Start || Start->IsA<APresetPlayerStart>() || Start->GetLevel() != GetWorld()->PersistentLevel)
		{
			continue;
		}
		if (Start->PlayerStartTag == TEXT("Shooter"))
		{
			ShooterStart = Start;
			++ShooterCount;
		}
		else if (Start->PlayerStartTag == TEXT("Partner"))
		{
			PartnerStart = Start;
			++PartnerCount;
		}
	}
	// 순서나 Start 프리셋에 기대지 않는다. 중복/누락은 원점 스폰 대신 설정 오류로 남긴다.
	if (ShooterCount != 1 || PartnerCount != 1)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DirectPairTest] Place exactly one ordinary PlayerStart per Player Start Tag in the persistent map: Shooter=%d Partner=%d"),
			ShooterCount, PartnerCount);
		return false;
	}
	ShooterSpawn = ShooterStart->GetActorTransform();
	PartnerSpawn = PartnerStart->GetActorTransform();
	return true;
}

void AOutlierGameMode::TryStartDirectPairTest()
{
	if (!HasAuthority() || !IsDirectPairTestWorld(GetWorld()) || bDirectPairTestStarted)
	{
		return;
	}
	DirectPairTestPlayers.RemoveAll([](const TWeakObjectPtr<APlayerController>& Player)
	{
		return !Player.IsValid();
	});
	if (DirectPairTestPlayers.Num() != 2)
	{
		return;
	}
	if (!ShooterClass || !PartnerClass || !ShooterControllerClass || !PartnerControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[DirectPairTest] Configure both character and controller classes on the test GameMode BP"));
		return;
	}
	for (const TWeakObjectPtr<APlayerController>& Player : DirectPairTestPlayers)
	{
		if (!Player->GetPlayerState<AOutlierPlayerState>())
		{
			UE_LOG(LogTemp, Error, TEXT("[DirectPairTest] Set Player State Class to OutlierPlayerState on the test GameMode BP"));
			return;
		}
	}

	// PostLogin과 기본 Spawn 처리가 끝난 다음 한 번만 기존 페어 생성 경로에 진입한다.
	ActiveMatchSaveContext = FOutlierMatchSaveContext();
	bDirectPairTestStarted = true;
	StartMatchedPair(DirectPairTestPlayers[0].Get(), DirectPairTestPlayers[1].Get(), 0,
		EOutlierPlayerRole::Shooter, EOutlierPlayerRole::Partner);
	if (ActiveMatchPairId == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[DirectPairTest] Pair setup failed. Fix the errors above, then restart PIE."));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[DirectPairTest] Pair created. First login=Shooter, second login=Partner; waiting for client content ready."));
}
