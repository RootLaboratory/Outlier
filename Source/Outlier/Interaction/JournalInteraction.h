#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "UI/JournalData.h"
#include "JournalInteraction.generated.h"

class UJournalWidget;
class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;

DECLARE_MULTICAST_DELEGATE(FJournalEntryChanged);

UCLASS(Blueprintable)
class OUTLIER_API AJournalInteraction : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AJournalInteraction();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	const FDataTableRowHandle& GetJournalEntry() const { return JournalEntry; }
	FJournalEntryChanged OnJournalEntryChanged;

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginDestroy() override;
#endif

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> JournalMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal",
		meta = (DisplayName = "Mesh", ToolTip = "지정하면 JournalMesh의 메시를 덮어씁니다. 비어 있으면 컴포넌트에 지정한 메시를 유지합니다."))
	TObjectPtr<UStaticMesh> JournalMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_JournalEntry,
		Category = "Journal", meta = (RowType = "/Script/Outlier.JournalRow"))
	FDataTableRowHandle JournalEntry;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal")
	TSubclassOf<UJournalWidget> JournalWidgetClass;

#if WITH_EDITORONLY_DATA
	// 표시용 복사본만 보관한다. 저장·복제하지 않으며 실제 읽기는 JournalEntry를 사용한다.
	UPROPERTY(VisibleAnywhere, Transient, DuplicateTransient, Category = "Journal|Preview", meta = (DisplayName = "Title"))
	FText PreviewTitle;

	UPROPERTY(VisibleAnywhere, Transient, DuplicateTransient, Category = "Journal|Preview", meta = (DisplayName = "Desc", MultiLine = true))
	FText PreviewDesc;
#endif

private:
	UFUNCTION()
	void OnRep_JournalEntry();

#if WITH_EDITOR
	void RefreshJournalPreview();
	void UnbindPreviewTable();
	TWeakObjectPtr<UDataTable> PreviewTable;
	FDelegateHandle PreviewTableChangedHandle;
#endif
};
