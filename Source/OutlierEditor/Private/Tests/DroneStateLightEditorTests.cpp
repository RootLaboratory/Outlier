#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/VECDrone.h"
#include "Enemy/SelfDestructDrone.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

namespace
{
	const FName StateSettingsNames[] = {
		TEXT("NonCombatLightSettings"), TEXT("AlertLightSettings"),
		TEXT("CombatLightSettings"), TEXT("PossessedLightSettings") };
	const FName SettingFieldNames[] = {
		TEXT("LightColor"), TEXT("bPulseEnabled"), TEXT("MinIntensity"),
		TEXT("MaxIntensity"), TEXT("PulsePeriod") };
	const TCHAR* DroneBlueprintPaths[] = {
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Default.BP_VECDrone_Default_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun1.BP_VECDrone_Gun1_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun_Patrol.BP_VECDrone_Gun_Patrol_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion_Patrol.BP_VECDrone_Explosion_Patrol_C") };

	TSharedPtr<IPropertyHandle> FindSettingHandle(const TArray<TSharedRef<IDetailTreeNode>>& Nodes, FName Name)
	{
		for (const TSharedRef<IDetailTreeNode>& Node : Nodes)
		{
			TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
			if (Handle.IsValid() && Handle->GetProperty() && Handle->GetProperty()->GetFName() == Name)
			{
				return Handle;
			}
			TArray<TSharedRef<IDetailTreeNode>> Children;
			Node->GetChildren(Children);
			if (TSharedPtr<IPropertyHandle> Child = FindSettingHandle(Children, Name)) return Child;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneStateLightEditorDefaultsTest,
	"Outlier.Editor.DroneStateLight.ClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneStateLightEditorDefaultsTest::RunTest(const FString& Parameters)
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	for (const TCHAR* Path : DroneBlueprintPaths)
	{
		UClass* Class = LoadClass<AVECDrone>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		TestEqual(TEXT("Explosion Blueprint uses the self-destruct native hierarchy"),
			Class->IsChildOf<ASelfDestructDrone>(), FString(Path).Contains(TEXT("Explosion")));
		UObject* Defaults = Class->GetDefaultObject();
		UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
		const bool bWasDirty = Class->GetOutermost()->IsDirty();
		AddInfo(FString::Printf(TEXT("BP=%s Parent=%s Status=%d"), Path,
			*Class->GetSuperClass()->GetPathName(), Blueprint ? static_cast<int32>(Blueprint->Status) : -1));
		TestTrue(TEXT("Defaults have not been replaced by reinstancing objects"),
			!Class->HasAnyClassFlags(CLASS_NewerVersionExists));

		FPropertyRowGeneratorArgs Args;
		TSharedRef<IPropertyRowGenerator> Rows = PropertyEditor.CreatePropertyRowGenerator(Args);
		TSet<FString> AllowedPaths;
		for (FName Name : StateSettingsNames) AllowedPaths.Add(Name.ToString());
		Rows->SetPropertyGenerationAllowListPaths(AllowedPaths);
		Rows->SetObjects({ Defaults });
		for (FName Name : StateSettingsNames)
		{
			FStructProperty* Property = FindFProperty<FStructProperty>(Class, Name);
			if (!TestNotNull(*Name.ToString(), Property)) continue;
			TestTrue(TEXT("State settings are editable class defaults"),
				Property->HasAllPropertyFlags(CPF_Edit | CPF_DisableEditOnInstance)
				&& !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnTemplate));
			TestTrue(TEXT("CDO permits editing the state settings"), Defaults->CanEditChange(Property));
			TSharedPtr<IPropertyHandle> Handle = FindSettingHandle(Rows->GetRootTreeNodes(), Name);
			if (!TestTrue(*FString::Printf(TEXT("%s: %s appears in Property Editor"), Path, *Name.ToString()), Handle.IsValid())) continue;
			TestFalse(TEXT("State settings row is not read-only"), Handle->IsEditConst());
			for (FName Field : SettingFieldNames)
			{
				TSharedPtr<IPropertyHandle> Child = Handle->GetChildHandle(Field);
				if (!TestTrue(*FString::Printf(TEXT("%s.%s is visible"), *Name.ToString(), *Field.ToString()), Child.IsValid())) continue;
				TestTrue(TEXT("Setting field is editable through Property Editor"), Child->IsEditable());
			}
		}
		// 진단 과정에서 원본 CDO의 값이나 패키지 저장 상태를 변경하지 않는다.
		TestEqual(TEXT("Read-only inspection preserves package dirty state"), Class->GetOutermost()->IsDirty(), bWasDirty);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneStateLightBlueprintDefaultsTest,
	"Outlier.Editor.DroneStateLight.BlueprintDefaultsToInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneStateLightBlueprintDefaultsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DroneDefaults")));
	if (!TestNotNull(TEXT("Transient world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	for (const TCHAR* Path : DroneBlueprintPaths)
	{
		UClass* Class = LoadClass<AVECDrone>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVECDrone* Drone = World->SpawnActor<AVECDrone>(Class, FTransform::Identity, Spawn);
		if (!TestNotNull(TEXT("Production Blueprint instance"), Drone)) continue;
		for (FName Name : StateSettingsNames)
		{
			FStructProperty* Property = FindFProperty<FStructProperty>(Class, Name);
			if (!TestNotNull(*Name.ToString(), Property)) continue;
			TestTrue(*FString::Printf(TEXT("%s: %s survives construction"), Path, *Name.ToString()),
				Property->Identical_InContainer(Drone, Class->GetDefaultObject()));
		}
		FNameProperty* NameProperty = FindFProperty<FNameProperty>(Class, TEXT("StateSpotLightComponentName"));
		if (!TestNotNull(TEXT("Configured SpotLight name property"), NameProperty)) continue;
		const FName LightName = NameProperty->GetPropertyValue_InContainer(Drone);
		TInlineComponentArray<USpotLightComponent*> Lights;
		Drone->GetComponents(Lights);
		TestTrue(*FString::Printf(TEXT("%s has configured SpotLight %s"), Path, *LightName.ToString()),
			Lights.ContainsByPredicate([LightName](const USpotLightComponent* Light) { return Light->GetFName() == LightName; }));
		Drone->Destroy();
	}
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneStateLightTransientEditTest,
	"Outlier.Editor.DroneStateLight.TransientSettingsEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneStateLightTransientEditTest::RunTest(const FString& Parameters)
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	for (const TCHAR* Path : DroneBlueprintPaths)
	{
		UClass* Class = LoadClass<AVECDrone>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		for (FName Name : StateSettingsNames)
		{
			const FStructProperty* Property = FindFProperty<FStructProperty>(Class, Name);
			if (!TestNotNull(*Name.ToString(), Property)) continue;
			const FDroneStateLightSettings* Defaults = Property->ContainerPtrToValuePtr<FDroneStateLightSettings>(Class->GetDefaultObject());
			const FDroneStateLightSettings Original = *Defaults;
			// 실제 BP 설정의 임시 사본만 편집한다. Property Editor 쓰기 경로를 검사해도 원본 에셋은 오염시키지 않는다.
			TSharedRef<FStructOnScope> Copy = MakeShared<FStructOnScope>(FDroneStateLightSettings::StaticStruct());
			FDroneStateLightSettings* Edited = reinterpret_cast<FDroneStateLightSettings*>(Copy->GetStructMemory());
			*Edited = *Defaults;
			TSharedRef<IPropertyRowGenerator> Rows = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
			Rows->SetStructure(Copy);
			auto SetField = [this, &Rows](FName Field, const FString& Value)
			{
				TSharedPtr<IPropertyHandle> Handle = FindSettingHandle(Rows->GetRootTreeNodes(), Field);
				if (!TestTrue(*Field.ToString(), Handle.IsValid())) return;
				TestEqual(TEXT("Property Editor accepts edited settings"), Handle->SetValueFromFormattedString(Value), FPropertyAccess::Success);
			};
			SetField(TEXT("LightColor"), TEXT("(R=0.25,G=0.5,B=0.75,A=1.0)"));
			SetField(TEXT("bPulseEnabled"), TEXT("True"));
			SetField(TEXT("MinIntensity"), TEXT("321"));
			SetField(TEXT("MaxIntensity"), TEXT("987"));
			SetField(TEXT("PulsePeriod"), TEXT("2.5"));
			TestTrue(TEXT("Color edit reaches settings"), Edited->LightColor.Equals(FLinearColor(0.25f, 0.5f, 0.75f)));
			TestTrue(TEXT("Pulse edit reaches settings"), Edited->bPulseEnabled);
			TestEqual(TEXT("Minimum edit reaches settings"), Edited->MinIntensity, 321.0f);
			TestEqual(TEXT("Maximum edit reaches settings"), Edited->MaxIntensity, 987.0f);
			TestEqual(TEXT("Period edit reaches settings"), Edited->PulsePeriod, 2.5f);
			TestTrue(TEXT("Editing the copy preserves original Blueprint defaults"),
				Property->Struct->CompareScriptStruct(Defaults, &Original, PPF_None));
		}
	}
	return true;
}

#endif
