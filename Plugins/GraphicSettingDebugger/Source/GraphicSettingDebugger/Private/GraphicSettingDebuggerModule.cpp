#include "Modules/ModuleManager.h"

#include "GraphicSettingDebuggerSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

namespace
{
	UGraphicSettingDebuggerSubsystem* FindSubsystem(UWorld* World)
	{
		return World && World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UGraphicSettingDebuggerSubsystem>()
			: nullptr;
	}
}

class FGraphicSettingDebuggerModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		SetCommand = MakeUnique<FAutoConsoleCommandWithWorldAndArgs>(
			TEXT("gsd.set"), TEXT("gsd.set <ConsoleVariable> <Value>"),
			FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() >= 2)
				{
					if (UGraphicSettingDebuggerSubsystem* Subsystem = FindSubsystem(World))
					{
						Subsystem->ApplySetting(FName(*Args[0]), Args[1]);
					}
				}
			}));

		SnapshotCommand = MakeUnique<FAutoConsoleCommandWithWorldAndArgs>(
			TEXT("gsd.snapshot"), TEXT("gsd.snapshot <Label>"),
			FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
			{
				if (UGraphicSettingDebuggerSubsystem* Subsystem = FindSubsystem(World))
				{
					Subsystem->SaveSnapshot(Args.Num() > 0 ? Args[0] : TEXT("snapshot"));
				}
			}));

	}

	virtual void ShutdownModule() override
	{
		SetCommand.Reset();
		SnapshotCommand.Reset();
	}

private:
	TUniquePtr<FAutoConsoleCommandWithWorldAndArgs> SetCommand;
	TUniquePtr<FAutoConsoleCommandWithWorldAndArgs> SnapshotCommand;
};

IMPLEMENT_MODULE(FGraphicSettingDebuggerModule, GraphicSettingDebugger)
