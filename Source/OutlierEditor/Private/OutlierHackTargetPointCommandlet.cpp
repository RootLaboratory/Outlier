#include "OutlierHackTargetPointCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackTestActor.h"
#include "Drone/Partner/MagneticGeneratorTower.h"
#include "Enemy/AutoTurret.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Interaction/InteractableSwitchBase.h"
#include "Interaction/InteractionStatMachine.h"
#include "Interface/HackableInterface.h"
#include "Jumper/Jumper.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHackTargetMigration, Log, All);

namespace HackTargetMigration
{
	const FName PointName(TEXT("HackTargetPoint"));

	bool IsConfigured(const FComponentReference& Ref)
	{
		return !Ref.ComponentProperty.IsNone() || !Ref.PathToComponent.IsEmpty()
			|| !Ref.OverrideComponent.IsExplicitlyNull() || !Ref.OtherActor.IsExplicitlyNull();
	}

	bool AttachTurretNeck(AAutoTurret* Turret)
	{
		USceneComponent* Point = FindObject<USceneComponent>(Turret, *PointName.ToString());
		USkeletalMeshComponent* Body = Turret->GetMesh();
		if (!Point || !Body || !Body->DoesSocketExist(TEXT("TurretNeck"))) return false;
		Point->Modify();
		return Point->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, TEXT("TurretNeck"));
	}

	USceneComponent* ResolvePoint(AActor* Actor, UHackableComponent* Hackable)
	{
		if (!IsConfigured(Hackable->HackTargetPointReference)) return nullptr;
		Hackable->RefreshHackTargetPoint();
		FVector Location;
		return Hackable->TryGetHackTargetLocation(Location)
			? Cast<USceneComponent>(Hackable->HackTargetPointReference.GetComponent(Actor)) : nullptr;
	}

	// 목표점 외의 기존 편집 설정을 비교한다. 자기 컴포넌트 경로는 미리보기 Actor의 임시 이름에 영향받지 않는다.
	TSharedRef<FJsonObject> CaptureGameplay(AActor* Actor)
	{
		TSharedRef<FJsonObject> Snapshot = MakeShared<FJsonObject>();
		Snapshot->SetStringField(TEXT("Class"), Actor->GetClass()->GetPathName());
		Snapshot->SetStringField(TEXT("Root"), GetNameSafe(Actor->GetRootComponent()));
		Snapshot->SetStringField(TEXT("ActorTransform"), Actor->GetActorTransform().ToString());
		auto CaptureObject = [&](UObject* Object, const FString& Prefix)
		{
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				FProperty* Property = *It;
				const FString OwnerPackage = Property->GetOwnerClass()->GetOutermost()->GetName();
				if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient)
					|| !(OwnerPackage.StartsWith(TEXT("/Script/Outlier")) || OwnerPackage.StartsWith(TEXT("/Game/")))
					|| Property->GetFName() == PointName || Property->GetFName() == TEXT("HackTargetPointReference")) continue;
				FString Value;
				Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
				Value.ReplaceInline(*Actor->GetPathName(), TEXT("$Self"));
				Snapshot->SetStringField(Prefix + Property->GetName(), Value);
			}
		};
		CaptureObject(Actor, TEXT("Actor."));
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetFName() != PointName)
				CaptureObject(Component, Component->GetName() + TEXT("."));
		}
		return Snapshot;
	}

	bool SameGameplay(const FJsonObject& Before, const FJsonObject& After)
	{
		if (Before.Values.Num() != After.Values.Num()) return false;
		for (const auto& Pair : Before.Values)
		{
			FString Value;
			if (!After.TryGetStringField(Pair.Key, Value) || Pair.Value->AsString() != Value)
			{
				UE_LOG(LogHackTargetMigration, Error, TEXT("기존 설정 변경 감지: %s"), *Pair.Key);
				return false;
			}
		}
		return true;
	}

	struct FRun
	{
		bool bApply = false;
		bool bCompileBlueprints = false;
		bool bMigrateTurretNeck = false;
		int32 Errors = 0;
		int32 Pending = 0;
		int32 Changed = 0;
		TArray<TSharedPtr<FJsonValue>> Rows;
		TSharedPtr<FJsonObject> Baseline;
		TSet<FString> Visited;
		TSet<FString> BaselineKeys;
		FString BackupDirectory;

		void Fail(const FString& Message)
		{
			++Errors;
			UE_LOG(LogHackTargetMigration, Error, TEXT("%s"), *Message);
		}

		bool Save(UPackage* Package)
		{
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(),
				Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
			const FString Backup = BackupDirectory / Package->GetName().RightChop(1)
				+ FPaths::GetExtension(Filename, true);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
			// 저장 전에 원본 패키지를 보관한다. 바이너리는 복사만 하고 편집은 Unreal API로만 한다.
			if (IFileManager::Get().Copy(*Backup, *Filename, false) != COPY_OK)
			{
				Fail(TEXT("원본 백업 실패: ") + Filename);
				return false;
			}
			if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
			{
				Fail(TEXT("패키지 저장 실패: ") + Filename);
				return false;
			}
			++Changed;
			return true;
		}

		bool Record(const FString& Key, AActor* Actor, const TSharedRef<FJsonObject>& Before, bool bChanged)
		{
			Visited.Add(Key);
			IHackableInterface* Interface = Cast<IHackableInterface>(Actor);
			UHackableComponent* Hackable = Interface ? Interface->GetHackableComponent() : nullptr;
			if (!Hackable) { Fail(TEXT("HackableComponent 누락: ") + Key); return false; }
			USceneComponent* Point = ResolvePoint(Actor, Hackable);
			TSharedRef<FJsonObject> Gameplay = CaptureGameplay(Actor);
			bool bValid = SameGameplay(*Before, *Gameplay);
			if (!bValid) Fail(TEXT("기존 게임플레이 설정이 달라짐: ") + Key);
			AAutoTurret* Turret = Cast<AAutoTurret>(Actor);
			// 기본 목표점의 목 부착을 검사한다. 사용자가 따로 지정한 목표점 참조는 허용한다.
			if (Point && Turret && Point->GetFName() == PointName
				&& (Point->GetAttachParent() != Turret->GetMesh() || Point->GetAttachSocketName() != FName(TEXT("TurretNeck"))))
			{
				bValid = false;
				Fail(TEXT("터렛 기본 목표점이 몸체의 목 본에 부착되지 않음: ") + Key);
			}
			if (!Point) ++Pending;
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("Key"), Key);
			Row->SetStringField(TEXT("Package"), Actor->GetPackage()->GetName());
			Row->SetStringField(TEXT("Label"), Actor->GetActorLabel());
			Row->SetObjectField(TEXT("Gameplay"), Gameplay);
			Row->SetStringField(TEXT("Point"), GetNameSafe(Point));
			Row->SetStringField(TEXT("Parent"), Point ? GetNameSafe(Point->GetAttachParent()) : TEXT(""));
			Row->SetStringField(TEXT("Socket"), Point ? Point->GetAttachSocketName().ToString() : TEXT(""));
			Row->SetStringField(TEXT("RelativeTransform"), Point ? Point->GetRelativeTransform().ToString() : TEXT(""));
			Row->SetStringField(TEXT("WorldLocation"), Point ? Point->GetComponentLocation().ToString() : TEXT(""));
			Row->SetStringField(TEXT("ReferenceProperty"), Hackable->HackTargetPointReference.ComponentProperty.ToString());
			Row->SetStringField(TEXT("ReferencePath"), Hackable->HackTargetPointReference.PathToComponent);
			Row->SetStringField(TEXT("HackTags"), Hackable->HackTags.ToString());
			Row->SetBoolField(TEXT("Changed"), bChanged);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			if (Baseline)
			{
				const TSharedPtr<FJsonObject>* Prior = nullptr;
				if (!Baseline->TryGetObjectField(Key, Prior) || !SameGameplay(*(*Prior)->GetObjectField(TEXT("Gameplay")), *Gameplay)
					|| !Point || (*Prior)->GetStringField(TEXT("RelativeTransform")) != Row->GetStringField(TEXT("RelativeTransform"))
					|| (*Prior)->GetStringField(TEXT("Parent")) != Row->GetStringField(TEXT("Parent"))
					|| ((*Prior)->HasField(TEXT("Socket")) && (*Prior)->GetStringField(TEXT("Socket")) != Row->GetStringField(TEXT("Socket"))))
				{
					Fail(TEXT("재로드 결과가 이전 보고서와 다름: ") + Key);
					bValid = false;
				}
			}
			Row->SetBoolField(TEXT("Valid"), Point && bValid);
			return Point && bValid;
		}
	};

	AActor* SpawnPreview(UWorld* World, UClass* Class)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bTemporaryEditorActor = true;
		return World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
	}

	void ProcessBlueprint(FRun& Run, UWorld* World, UBlueprint* Blueprint)
	{
		// 재로드 검증에서는 BP도 다시 컴파일해 저장된 참조가 CDO 재생성 뒤에도 유지되는지 확인한다.
		if (Run.bCompileBlueprints) FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			Run.Fail(TEXT("BP 컴파일 오류: ") + Blueprint->GetPathName());
			return;
		}
		UClass* Class = Blueprint->GeneratedClass;
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract)) return;
		AActor* Actor = SpawnPreview(World, Class);
		IHackableInterface* Interface = Cast<IHackableInterface>(Actor);
		if (!Interface) { if (Actor) Actor->Destroy(); return; }
		UHackableComponent* Hackable = Interface->GetHackableComponent();
		if (!Hackable) { Run.Fail(TEXT("BP HackableComponent 누락: ") + Blueprint->GetPathName()); Actor->Destroy(); return; }
		const TSharedRef<FJsonObject> Before = CaptureGameplay(Actor);
		bool bChanged = false;
		if (Run.bApply && Run.bMigrateTurretNeck && Class->IsChildOf(AAutoTurret::StaticClass()))
		{
			// 네이티브 부착 변경을 BP 기본값에도 저장한다. 참조·태그·목 기준 오프셋은 그대로 둔다.
			if (!AttachTurretNeck(CastChecked<AAutoTurret>(Class->GetDefaultObject())))
			{
				Run.Fail(TEXT("터렛 BP 목 부착 실패: ") + Blueprint->GetPathName());
				Actor->Destroy(); return;
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			Actor->Destroy();
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (Blueprint->Status == BS_Error) { Run.Fail(TEXT("터렛 BP 컴파일 실패: ") + Blueprint->GetPathName()); return; }
			Actor = SpawnPreview(World, Blueprint->GeneratedClass);
			Interface = Cast<IHackableInterface>(Actor);
			Hackable = Interface ? Interface->GetHackableComponent() : nullptr;
			if (!Hackable) { Run.Fail(TEXT("터렛 BP 재생성 실패: ") + Blueprint->GetPathName()); if (Actor) Actor->Destroy(); return; }
			bChanged = true;
		}
		if (!ResolvePoint(Actor, Hackable) && Run.bApply)
		{
			USceneComponent* Existing = FindObject<USceneComponent>(Actor, *PointName.ToString());
			USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
			if (!Existing && SCS && Actor->GetRootComponent())
			{
				USCS_Node* RootNode = nullptr;
				for (USCS_Node* Node : SCS->GetAllNodes())
					if (Node->GetVariableName() == Actor->GetRootComponent()->GetFName()) RootNode = Node;
				if (!RootNode)
				{
					Run.Fail(TEXT("기존 BP 루트의 SCS 노드를 확인할 수 없음: ") + Blueprint->GetPathName());
					Actor->Destroy(); return;
				}
				// 기존 루트의 자식으로만 추가한다. DefaultSceneRoot를 대체하거나 기존 메시를 재부착하지 않는다.
				USCS_Node* NewNode = SCS->CreateNode(USceneComponent::StaticClass(), PointName);
				RootNode->AddChildNode(NewNode);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				Actor->Destroy();
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				if (Blueprint->Status == BS_Error) { Run.Fail(TEXT("BP 컴파일 실패: ") + Blueprint->GetPathName()); return; }
				Actor = SpawnPreview(World, Blueprint->GeneratedClass);
				Interface = Cast<IHackableInterface>(Actor);
				Existing = Actor ? FindObject<USceneComponent>(Actor, *PointName.ToString()) : nullptr;
			}
			if (!Existing || !Interface) { Run.Fail(TEXT("목표점을 생성/조회할 수 없음: ") + Blueprint->GetPathName()); if (Actor) Actor->Destroy(); return; }
			UHackableComponent* Defaults = Cast<IHackableInterface>(Blueprint->GeneratedClass->GetDefaultObject())->GetHackableComponent();
			Defaults->HackTargetPointReference = FComponentReference();
			Defaults->HackTargetPointReference.ComponentProperty = PointName;
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			// CDO 변경 뒤 새 인스턴스로 검증해 임시 Actor에만 참조를 지정하는 실수를 막는다.
			Actor->Destroy();
			Actor = SpawnPreview(World, Blueprint->GeneratedClass);
			bChanged = true;
		}
		if (!Actor) { Run.Fail(TEXT("BP 미리보기 생성 실패: ") + Blueprint->GetPathName()); return; }
		const bool bValid = Run.Record(Blueprint->GetPathName(), Actor, Before, bChanged);
		Actor->Destroy();
		if (bChanged && bValid) Run.Save(Blueprint->GetOutermost());
	}
}

UOutlierHackTargetPointCommandlet::UOutlierHackTargetPointCommandlet()
{
	IsEditor = true;
	IsClient = false;
	IsServer = false;
	LogToConsole = true;
}

int32 UOutlierHackTargetPointCommandlet::Main(const FString& Params)
{
	using namespace HackTargetMigration;
	FRun Run;
	Run.bApply = FParse::Param(*Params, TEXT("Apply"));
	Run.bCompileBlueprints = FParse::Param(*Params, TEXT("CompileBlueprints"));
	Run.bMigrateTurretNeck = FParse::Param(*Params, TEXT("TurretNeck"));
	FString OutputDirectory;
	FParse::Value(*Params, TEXT("Output="), OutputDirectory);
	if (OutputDirectory.IsEmpty()) OutputDirectory = FPaths::ProjectSavedDir() / TEXT("HackTargetPointMigration") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	Run.BackupDirectory = OutputDirectory / TEXT("Backup");
	FString BaselinePath;
	if (FParse::Value(*Params, TEXT("Baseline="), BaselinePath))
	{
		if (Run.bApply) { UE_LOG(LogHackTargetMigration, Error, TEXT("Apply와 Baseline은 동시에 사용할 수 없습니다.")); return 1; }
		FString Json;
		TSharedPtr<FJsonObject> Document;
		if (!FFileHelper::LoadFileToString(Json, *BaselinePath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Document)) return 1;
		Run.Baseline = MakeShared<FJsonObject>();
		for (const TSharedPtr<FJsonValue>& Row : Document->GetArrayField(TEXT("Rows")))
		{
			const TSharedPtr<FJsonObject> Object = Row->AsObject();
			Run.Baseline->SetObjectField(Object->GetStringField(TEXT("Key")), Object);
			Run.BaselineKeys.Add(Object->GetStringField(TEXT("Key")));
		}
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FTopLevelAssetPath> Roots = { AEnemyBase::StaticClass()->GetClassPathName(), AInteractableSwitchBase::StaticClass()->GetClassPathName(),
		AInteractionStatMachine::StaticClass()->GetClassPathName(), AJumper::StaticClass()->GetClassPathName(),
		AMagneticGeneratorTower::StaticClass()->GetClassPathName(), AHackTestActor::StaticClass()->GetClassPathName() };
	TSet<FTopLevelAssetPath> Derived;
	Registry.GetDerivedClassNames(Roots, {}, Derived);
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game/Blueprints"));
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
	UWorld* PreviewWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("HackMigrationPreview"));
	for (const FAssetData& Asset : Assets)
	{
		const FString GeneratedPath = Asset.GetTagValueRef<FString>(TEXT("GeneratedClass"));
		if (!Derived.Contains(FTopLevelAssetPath(FPackageName::ExportTextPathToObjectPath(GeneratedPath)))) continue;
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset())) ProcessBlueprint(Run, PreviewWorld, Blueprint);
		else Run.Fail(TEXT("BP 로드 실패: ") + Asset.GetSoftObjectPath().ToString());
	}
	PreviewWorld->DestroyWorld(false);
	PreviewWorld->RemoveFromRoot();

	// 알려진 인스턴스가 있는 두 맵을 실제 월드로 로드한다. WP는 로드되지 않은 외부 Actor까지 순회한다.
	const TCHAR* Maps[] = { TEXT("/Game/Maps/Arena/Build/D_L_Stage01_Gameplay"), TEXT("/Game/Maps/Arena/Build/D_L_Stage02_Gameplay") };
	for (const TCHAR* Map : Maps)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
		if (!World) { Run.Fail(FString(TEXT("맵 로드 실패: ")) + Map); continue; }
		TSet<AActor*> Processed;
		auto ProcessActor = [&](AActor* Actor)
		{
			if (!IsValid(Actor) || Processed.Contains(Actor)) return;
			Processed.Add(Actor);
			IHackableInterface* Interface = Cast<IHackableInterface>(Actor);
			UHackableComponent* Hackable = Interface ? Interface->GetHackableComponent() : nullptr;
			if (!Hackable) return;
			const TSharedRef<FJsonObject> Before = CaptureGameplay(Actor);
			bool bChanged = false;
			if (Run.bApply && Run.bMigrateTurretNeck)
			{
				if (AAutoTurret* Turret = Cast<AAutoTurret>(Actor))
				{
					// 로드·Construction으로 부착이 갱신된 인스턴스도 저장해 다음 로드의 계층을 확정한다.
					Actor->Modify();
					bChanged = AttachTurretNeck(Turret);
					if (!bChanged) { Run.Fail(TEXT("터렛 인스턴스 목 부착 실패: ") + Actor->GetPathName()); return; }
				}
			}
			if (!ResolvePoint(Actor, Hackable) && Run.bApply)
			{
				USceneComponent* Point = FindObject<USceneComponent>(Actor, *PointName.ToString());
				if (Point)
				{
					Actor->Modify();
					Hackable->Modify();
					Hackable->HackTargetPointReference = FComponentReference();
					Hackable->HackTargetPointReference.ComponentProperty = PointName;
					bChanged = true;
				}
			}
			if (Run.Record(Actor->GetPathName(), Actor, Before, bChanged) && bChanged)
				Run.Save(Actor->GetPackage());
		};
		for (ULevel* Level : World->GetLevels()) for (AActor* Actor : Level->Actors) ProcessActor(Actor);
		if (UWorldPartition* Partition = World->GetWorldPartition())
		{
			FWorldPartitionHelpers::FForEachActorWithLoadingParams Loading;
			Loading.ActorClasses = { AEnemyBase::StaticClass(), AInteractableSwitchBase::StaticClass(), AInteractionStatMachine::StaticClass(),
				AJumper::StaticClass(), AMagneticGeneratorTower::StaticClass(), AHackTestActor::StaticClass() };
			FWorldPartitionHelpers::ForEachActorWithLoading(Partition, [&](const FWorldPartitionActorDescInstance* Desc)
			{
				ProcessActor(Desc->GetActor()); return true;
			}, Loading);
		}

		// Spec에 기록된 외부 Actor가 실제 점검 결과에 포함됐는지 확인한다.
		const TCHAR* RequiredPackages[] = {
			TEXT("/Game/__ExternalActors__/Maps/Arena/Build/D_L_Stage01_Gameplay/6/K3/B2H0ORYHFXJ3ZO1PQD40AS"),
			TEXT("/Game/__ExternalActors__/Maps/Arena/Build/D_L_Stage02_Gameplay/C/CH/XT4I9YRE9ALOXXUI43HVQ4"),
			TEXT("/Game/__ExternalActors__/Maps/Arena/Build/D_L_Stage02_Gameplay/D/OS/PP820ZHJAWBPWE2QTGAC91")
		};
		for (const TCHAR* Required : RequiredPackages)
		{
			if (!FString(Required).Contains(FString(Map).Replace(TEXT("/Game/Maps/"), TEXT("/")))) continue;
			bool bFound = false;
			for (const TSharedPtr<FJsonValue>& Value : Run.Rows)
			{
				if (Value->AsObject()->GetStringField(TEXT("Package")) == Required) { bFound = true; break; }
			}
			if (!bFound) Run.Fail(FString(TEXT("필수 외부 Actor 확인 실패: ")) + Required);
		}
	}
	for (const FString& Key : Run.BaselineKeys)
		if (!Run.Visited.Contains(Key)) Run.Fail(TEXT("재로드 점검에서 대상 누락: ") + Key);
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("Apply"), Run.bApply);
	Report->SetNumberField(TEXT("Errors"), Run.Errors);
	Report->SetNumberField(TEXT("Pending"), Run.Pending);
	Report->SetNumberField(TEXT("SavedPackages"), Run.Changed);
	Report->SetArrayField(TEXT("Rows"), Run.Rows);
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	const FString ReportPath = OutputDirectory / TEXT("Report.json");
	if (!FFileHelper::SaveStringToFile(Json, *ReportPath)) return 1;
	UE_LOG(LogHackTargetMigration, Display, TEXT("대상=%d 저장=%d 미연결=%d 오류=%d 보고서=%s"), Run.Rows.Num(), Run.Changed, Run.Pending, Run.Errors, *ReportPath);
	return Run.Errors == 0 && (!Run.bApply && !Run.Baseline || Run.Pending == 0) ? 0 : 1;
}
