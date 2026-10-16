#include "Tutorial/WorldGuide/OutlierWorldGuideDebug.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Outlier.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

namespace OutlierWorldGuideDebug
{
#if !UE_BUILD_SHIPPING
	static TAutoConsoleVariable<int32> DebugEnabled(TEXT("Outlier.WorldGuide.Debug"), 0,
		TEXT("1: log WorldGuide registration, queue, rendering, completion and replication. 0: off."));
#endif

	bool IsEnabled()
	{
#if !UE_BUILD_SHIPPING
		return DebugEnabled.GetValueOnGameThread() != 0;
#else
		return false;
#endif
	}

	void Trace(const UWorld* World, const FString& Message)
	{
#if !UE_BUILD_SHIPPING
		if (IsEnabled()) UE_LOG(LogOutlier, Log, TEXT("[WorldGuideDebug][%s][Net=%d] %s"),
			*GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : -1, *Message);
#endif
	}
}

#if !UE_BUILD_SHIPPING
namespace
{
	void DumpLocal(UWorld* World)
	{
		if (!World) return;
		int32 Managers = 0;
		for (TActorIterator<AOutlierWorldGuideManager> It(World); It; ++It)
		{
			++Managers;
			UE_LOG(LogOutlier, Log, TEXT("[WorldGuideDebug] %s"), *It->BuildDebugReport());
		}
		if (Managers == 1) return;
		UE_LOG(LogOutlier, Warning, TEXT("[WorldGuideDebug][%s] Manager count=%d; place exactly one BP_OutlierWorldGuideManager."),
			*World->GetName(), Managers);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UOutlierWorldGuideTargetComponent*> Targets;
			It->GetComponents(Targets);
			for (const auto* Target : Targets)
				if (Target->WidgetID.IsValid()) UE_LOG(LogOutlier, Log, TEXT("[WorldGuideDebug] %s"), *Target->DescribeDebugState());
		}
	}

	FGameplayTag ReadGuideID(const FString& Text)
	{
		const FString Name = Text.Contains(TEXT(".")) ? Text : FString(TEXT("UI.Info.World.LOC_TUT_")) + Text;
		return FGameplayTag::RequestGameplayTag(FName(*Name), false);
	}

	void Request(UWorld* World, FName Operation, const TArray<FString>& Args)
	{
		if (!World) return;
		FGameplayTag ID, Next;
		if (!Args.IsEmpty())
		{
			ID = ReadGuideID(Args[0]);
			if (!ID.IsValid()) { UE_LOG(LogOutlier, Warning, TEXT("[WorldGuideDebug] Unknown WidgetID '%s'."), *Args[0]); return; }
		}
		if (Args.Num() > 1)
		{
			Next = ReadGuideID(Args[1]);
			if (!Next.IsValid()) { UE_LOG(LogOutlier, Warning, TEXT("[WorldGuideDebug] Unknown next WidgetID '%s'."), *Args[1]); return; }
		}
		if (Operation == TEXT("show") && !ID.IsValid())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[WorldGuideDebug] Usage: Outlier.WorldGuide.Show <WidgetID or suffix>"));
			return;
		}
		AFirstPersonPlayerController* Controller = GEngine
			? Cast<AFirstPersonPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
		if (!Controller)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[WorldGuideDebug] No local FirstPersonPlayerController; run this in a PIE player window."));
			return;
		}
		// 클라이언트 명령은 소유 Controller가 전달한다. 레벨 Manager에서 서버 RPC를 직접 호출하지 않는다.
		if (Controller->HasAuthority()) Controller->ServerDebugWorldGuide_Implementation(Operation, ID, Next);
		else Controller->ServerDebugWorldGuide(Operation, ID, Next);
	}

	FAutoConsoleCommandWithWorldAndArgs WorldGuideDumpCommand(TEXT("Outlier.WorldGuide.Dump"),
		TEXT("Dump local setup/targets/state. Add 'server' for the server FIFO and completed IDs."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!Args.IsEmpty() && Args[0].Equals(TEXT("server"), ESearchCase::IgnoreCase)) Request(World, TEXT("dump"), {});
			else DumpLocal(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs ShowCommand(TEXT("Outlier.WorldGuide.Show"), TEXT("Show <ID>: enqueue the guide on the server; no HUD event."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Request(World, TEXT("show"), Args); }));
	FAutoConsoleCommandWithWorldAndArgs CompleteCommand(TEXT("Outlier.WorldGuide.Complete"), TEXT("Complete [ID] [NextID]: complete ID or active guide, optionally select a branch."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Request(World, TEXT("complete"), Args); }));
	FAutoConsoleCommandWithWorldAndArgs OverlapCommand(TEXT("Outlier.WorldGuide.Overlap"), TEXT("Overlap [ID]: complete only an Overlap guide; omit ID for active guide."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Request(World, TEXT("overlap"), Args); }));
	FAutoConsoleCommandWithWorldAndArgs CancelCommand(TEXT("Outlier.WorldGuide.Cancel"), TEXT("Cancel [ID]: remove the guide without chaining."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Request(World, TEXT("cancel"), Args); }));
	FAutoConsoleCommandWithWorldAndArgs ClearCommand(TEXT("Outlier.WorldGuide.Clear"), TEXT("Clear the shared FIFO; retain completed IDs."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Request(World, TEXT("clear"), {}); }));
	FAutoConsoleCommandWithWorldAndArgs WorldGuideResetCommand(TEXT("Outlier.WorldGuide.Reset"), TEXT("Clear shared guides AND completed IDs. Does not reset gameplay Volume/Actor one-shot state."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Request(World, TEXT("reset"), {}); }));

	FAutoConsoleCommandWithWorldAndArgs MarkCommand(TEXT("Outlier.WorldGuide.Mark"), TEXT("Mark [seconds=10]: draw local target anchors and IDs; green=active, yellow=inactive."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || World->GetNetMode() == NM_DedicatedServer) return;
			const float Seconds = Args.IsEmpty() ? 10.0f : FMath::Clamp(FCString::Atof(*Args[0]), 1.0f, 60.0f);
			int32 Count = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				TArray<UOutlierWorldGuideTargetComponent*> Targets;
				It->GetComponents(Targets);
				for (const auto* Target : Targets)
				{
					if (!Target->WidgetID.IsValid()) continue;
					++Count;
					const FVector Position = Target->GetComponentLocation() + Target->WorldOffset;
					const FColor Color = Target->IsGuideVisible() ? FColor::Green : FColor::Yellow;
					DrawDebugSphere(World, Position, 12.0f, 12, Color, false, Seconds);
					DrawDebugString(World, Position + FVector(0.0, 0.0, 18.0),
						FString::Printf(TEXT("%s\n%s"), *It->GetName(), *Target->WidgetID.ToString()), nullptr, Color, Seconds);
				}
			}
			UE_LOG(LogOutlier, Log, TEXT("[WorldGuideDebug][%s] Marked %d target anchors for %.1fs."), *World->GetName(), Count, Seconds);
		}));
}
#endif
