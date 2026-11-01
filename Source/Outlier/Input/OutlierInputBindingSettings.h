#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Input/OutlierInputBindingTable.h"
#include "OutlierInputBindingSettings.generated.h"

class UInputMappingContext;
class UOutlierInputBindingTable;

USTRUCT(BlueprintType)
struct FOutlierInputMappingContextConflictRule
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FName ConflictGroup = TEXT("Gameplay");
};

USTRUCT(BlueprintType)
struct FOutlierInputMappingContextDisplayRule
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "Input Binding")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Config, EditAnywhere, Category = "Input Binding", meta = (Bitmask, BitmaskEnum = "/Script/Outlier.EOutlierInputSettingScope"))
	int32 DisplayScopes = static_cast<int32>(EOutlierInputSettingScope::Common);
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Outlier Input Binding"))
class OUTLIER_API UOutlierInputBindingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Input Binding")
	TArray<FOutlierInputMappingContextDisplayRule> MappingContextDisplayRules;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FDirectoryPath ContentInputDirectory;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TArray<TSoftObjectPtr<UInputMappingContext>> InputMappingContextsToScan;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TArray<FOutlierInputMappingContextConflictRule> MappingContextConflictRules;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TSoftObjectPtr<UOutlierInputBindingTable> GeneratedBindingTable;
};
