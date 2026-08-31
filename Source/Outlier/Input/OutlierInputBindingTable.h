#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "OutlierInputBindingTable.generated.h"

class UInputAction;
class UInputMappingContext;

UENUM(BlueprintType)
enum class EOutlierInputBindingKind : uint8
{
	Action UMETA(DisplayName = "Action"),
	Axis UMETA(DisplayName = "Axis"),
	Mouse UMETA(DisplayName = "Mouse"),
	Gamepad UMETA(DisplayName = "Gamepad")
};

USTRUCT(BlueprintType)
struct FOutlierInputBindingTableRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FText CategoryName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FKey DefaultKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FName MappingName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FText MappingContextName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FName ConflictGroup = TEXT("Gameplay");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	EOutlierInputBindingKind BindingKind = EOutlierInputBindingKind::Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	int32 SortOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	int32 MappingContextOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	int32 SourceMappingIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	bool bVisibleInSettings = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	bool bRebindable = true;
};

UCLASS(BlueprintType)
class OUTLIER_API UOutlierInputBindingTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Input Binding")
	TArray<FOutlierInputBindingTableRow> GetVisibleRowsSorted() const;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Input Binding")
	void RebuildRowsFromConfiguredIMCs();

	UFUNCTION(CallInEditor, Category = "Input Binding")
	void SortRowsByMappingContext();
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TArray<FOutlierInputBindingTableRow> Rows;
};
