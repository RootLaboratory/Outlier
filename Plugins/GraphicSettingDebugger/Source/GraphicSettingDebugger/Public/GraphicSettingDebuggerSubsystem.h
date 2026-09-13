#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/Engine.h"
#include "GraphicSettingDebuggerSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FGraphicSettingDebuggerEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName Key;

	UPROPERTY(BlueprintReadOnly)
	FString Value;

	UPROPERTY(BlueprintReadOnly)
	bool bActive = false;
};

UCLASS()
class GRAPHICSETTINGDEBUGGER_API UGraphicSettingDebuggerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Graphic Setting Debugger")
	bool ApplySetting(FName Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Graphic Setting Debugger")
	bool SaveSnapshot(const FString& Label);

	UFUNCTION(BlueprintCallable, Category = "Graphic Setting Debugger")
	FString GetSettingValue(FName Key) const;

	UFUNCTION(BlueprintCallable, Category = "Graphic Setting Debugger")
	bool IsSettingActive(FName Key) const;

	UFUNCTION(BlueprintCallable, Category = "Graphic Setting Debugger")
	FString GetLastSnapshotPath() const { return LastSnapshotPath; }

private:
	FString LastSnapshotPath;
};
