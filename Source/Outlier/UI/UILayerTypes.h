#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UILayerTypes.generated.h"

class AActor;
class UUserWidget;

struct FUILayerHandle
{
	int32 Id = INDEX_NONE;

	bool IsValid() const
	{
		return Id != INDEX_NONE;
	}

	void Reset()
	{
		Id = INDEX_NONE;
	}

	friend bool operator==(const FUILayerHandle& Left, const FUILayerHandle& Right)
	{
		return Left.Id == Right.Id;
	}
};

UENUM()
enum class EUILayerFocusTarget : uint8
{
	Widget,
	GameViewport, //Hack Emp
	None
};

USTRUCT()
struct FUILayerPushRequest
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UUserWidget> WidgetClass;

	UPROPERTY()
	FGameplayTag LayerTag;

	UPROPERTY()
	FGameplayTag InputModeTag;

	UPROPERTY()
	TObjectPtr<AActor> RequestOwner;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> ContextActors;

	UPROPERTY()
	EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::Widget;

	UPROPERTY()
	bool bShowCursor = true;

	UPROPERTY()
	bool bReceivesInput = true;
};
