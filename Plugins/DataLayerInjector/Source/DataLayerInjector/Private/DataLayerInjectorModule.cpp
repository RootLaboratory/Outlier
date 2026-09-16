#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerStart.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogDataLayerInjector, Log, All);

class FDataLayerInjectorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		InjectCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("DataLayerInjector.Inject"),
			TEXT("DataLayerInjector.Inject /Game/DataLayer/DL_Test.DL_Test"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDataLayerInjectorModule::Inject));

		RemoveCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("DataLayerInjector.Remove"),
			TEXT("DataLayerInjector.Remove /Game/DataLayer/DL_Test.DL_Test"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDataLayerInjectorModule::Remove));

		UE_LOG(LogDataLayerInjector, Display,
			TEXT("Data Layer Injector ready. Use DataLayerInjector.Inject <AssetPath> or DataLayerInjector.Remove <AssetPath>"));
	}

	virtual void ShutdownModule() override
	{
		if (InjectCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(InjectCommand);
			InjectCommand = nullptr;
		}

		if (RemoveCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(RemoveCommand);
			RemoveCommand = nullptr;
		}
	}

private:
	static UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static UDataLayerAsset* ResolveAsset(const TArray<FString>& Args, const TCHAR* Operation)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogDataLayerInjector, Error,
				TEXT("%s requires a Data Layer asset path. Example: /Game/DataLayer/DL_Test.DL_Test"),
				Operation);
			return nullptr;
		}

		UDataLayerAsset* Asset = LoadObject<UDataLayerAsset>(nullptr, *Args[0]);
		if (!Asset)
		{
			UE_LOG(LogDataLayerInjector, Error,
				TEXT("%s could not load Data Layer asset: %s"), Operation, *Args[0]);
		}
		return Asset;
	}

	void Inject(const TArray<FString>& Args)
	{
		Apply(Args, true);
	}

	void Remove(const TArray<FString>& Args)
	{
		Apply(Args, false);
	}

	void Apply(const TArray<FString>& Args, const bool bInject)
	{
		UDataLayerAsset* DataLayerAsset = ResolveAsset(
			Args, bInject ? TEXT("DataLayerInjector.Inject") : TEXT("DataLayerInjector.Remove"));
		UWorld* World = GetEditorWorld();
		if (!DataLayerAsset || !World)
		{
			if (!World)
			{
				UE_LOG(LogDataLayerInjector, Error, TEXT("No editor world is available"));
			}
			return;
		}

		const UDataLayerManager* DataLayerManager = UDataLayerManager::GetDataLayerManager(World);
		const UDataLayerInstance* DataLayerInstance = DataLayerManager
			? DataLayerManager->GetDataLayerInstance(DataLayerAsset)
			: nullptr;
		if (!DataLayerInstance)
		{
			UE_LOG(LogDataLayerInjector, Error,
				TEXT("Data Layer asset is not registered in world %s: %s"),
				*World->GetName(), *DataLayerAsset->GetPathName());
			return;
		}

		const FScopedTransaction Transaction(
			bInject ? NSLOCTEXT("DataLayerInjector", "Inject", "Inject Data Layer")
					: NSLOCTEXT("DataLayerInjector", "Remove", "Remove Data Layer"));

		int32 Visited = 0;
		int32 Changed = 0;
		int32 AlreadyInTarget = 0;
		int32 SkippedLevelInstances = 0;
		int32 SkippedPlayerStarts = 0;
		int32 SkippedTransient = 0;
		ULevelInstanceSubsystem* LevelInstanceSubsystem = World->GetSubsystem<ULevelInstanceSubsystem>();

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			++Visited;
			const bool bIsLevelInstanceActor = Actor->IsA<ALevelInstance>()
				|| (LevelInstanceSubsystem && LevelInstanceSubsystem->GetParentLevelInstance(Actor));
			if (bInject && bIsLevelInstanceActor)
			{
				++SkippedLevelInstances;
				continue;
			}
			if (bInject && Actor->IsA<APlayerStart>())
			{
				++SkippedPlayerStarts;
				continue;
			}

			if (Actor->HasAnyFlags(RF_Transient) || Actor->IsTemplate())
			{
				++SkippedTransient;
				continue;
			}

			const bool bContainsTarget = Actor->GetDataLayerAssets().Contains(DataLayerAsset);
			if (bInject)
			{
				if (bContainsTarget)
				{
					++AlreadyInTarget;
					continue;
				}

				Actor->Modify();
				if (DataLayerInstance->AddActor(Actor))
				{
					Actor->MarkPackageDirty();
					++Changed;
				}
			}
			else
			{
				if (!bContainsTarget)
				{
					continue;
				}

				Actor->Modify();
				if (DataLayerInstance->RemoveActor(Actor))
				{
					Actor->MarkPackageDirty();
					++Changed;
				}
			}
		}

		UE_LOG(LogDataLayerInjector, Display,
			TEXT("%s World=%s DataLayer=%s Visited=%d Changed=%d AlreadyInTarget=%d SkippedLevelInstances=%d SkippedPlayerStarts=%d SkippedTransient=%d"),
			bInject ? TEXT("Inject complete") : TEXT("Remove complete"),
			*World->GetName(),
			*DataLayerAsset->GetPathName(),
			Visited,
			Changed,
			AlreadyInTarget,
			SkippedLevelInstances,
			SkippedPlayerStarts,
			SkippedTransient);
	}

	IConsoleObject* InjectCommand = nullptr;
	IConsoleObject* RemoveCommand = nullptr;
};

IMPLEMENT_MODULE(FDataLayerInjectorModule, DataLayerInjector)
