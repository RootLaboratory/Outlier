#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "OutlierInputBindingTable.generated.h"

class UInputAction;
class UInputMappingContext;

USTRUCT(BlueprintType)
struct FOutlierInputBindingTableRow
{
	GENERATED_BODY()

	// --- Editable by designers in the table ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FText DisplayName;

	// --- Filled in by RebuildRowsFromConfiguredIMCs; shown read-only for
	// reference, since editing them here is overwritten on the next rebuild. ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input Binding")
	FKey DefaultKey;

	// --- Internal wiring/ordering data used to rebuild and sort the table;
	// not meant to be browsed or edited directly. ---

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	FText CategoryName;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	FName MappingName;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	FText MappingContextName;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	FName ConflictGroup = TEXT("Gameplay");

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	int32 SortOrder = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	int32 MappingContextOrder = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	int32 SourceMappingIndex = 0;

	// Whether this mapping is rebindable from the settings screen. Not exposed
	// here: an Input Action only reaches this table via the Outlier Input
	// Mappable Tool's "Use" checkbox, which is already the designer's consent
	// to make it rebindable, so every row is rebindable by construction.
	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
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

	// Gamepad mappings aren't split out (no settings-screen use for them yet);
	// everything else lands in one of these two, by physical input type. A
	// mapping is pulled in once the Outlier Input Mappable Tool has been used
	// to give it a Name on the IMC side (i.e. the designer has opted it into
	// being rebindable).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TArray<FOutlierInputBindingTableRow> KeyboardRows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Binding")
	TArray<FOutlierInputBindingTableRow> MouseRows;
};
