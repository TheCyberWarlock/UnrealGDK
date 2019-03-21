// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditor.h"

#include "Async/Async.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "SpatialGDKEditorSnapshotGenerator.h"
#include "SpatialGDKEditorCloudLauncher.h"

#include "Editor.h"

#include "AssetRegistryModule.h"
#include "GeneralProjectSettings.h"
#include "SpatialGDKEditorSettings.h"

DEFINE_LOG_CATEGORY(LogSpatialGDKEditor);

FSpatialGDKEditor::FSpatialGDKEditor()
	: bSchemaGeneratorRunning(false)
{
	TryLoadExistingSchemaDatabase();
}

void FSpatialGDKEditor::GenerateSchema(FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback, FSpatialGDKEditorErrorHandler ErrorCallback)
{
	if (bSchemaGeneratorRunning)
	{
		UE_LOG(LogSpatialGDKEditor, Warning, TEXT("Schema generation is already running"));
		return;
	}

	bSchemaGeneratorRunning = true;

	// Force spatial networking so schema layouts are correct
	UGeneralProjectSettings* GeneralProjectSettings = GetMutableDefault<UGeneralProjectSettings>();
	bool bCachedSpatialNetworking = GeneralProjectSettings->bSpatialNetworking;
	GeneralProjectSettings->bSpatialNetworking = true;

	TryLoadExistingSchemaDatabase();

	PreProcessSchemaMap();

	// Compile all dirty blueprints
	TArray<UBlueprint*> ErroredBlueprints;
	bool bPromptForCompilation = false;
	UEditorEngine::ResolveDirtyBlueprints(bPromptForCompilation, ErroredBlueprints);

	LoadDefaultGameModes();

	SchemaGeneratorResult = Async<bool>(EAsyncExecution::Thread, SpatialGDKGenerateSchema,
		[this, bCachedSpatialNetworking, ErroredBlueprints, SuccessCallback, FailureCallback]()
	{
		// We delay printing this error until after the schema spam to make it have a higher chance of being noticed.
		if (ErroredBlueprints.Num() > 0)
		{
			UE_LOG(LogSpatialGDKEditor, Error, TEXT("Errors compiling blueprints during schema generation! The following blueprints did not have schema generated for them:"));
			for (const auto& Blueprint : ErroredBlueprints)
			{
				UE_LOG(LogSpatialGDKEditor, Error, TEXT("%s"), *GetPathNameSafe(Blueprint));
			}
		}

		if (!SchemaGeneratorResult.IsReady() || SchemaGeneratorResult.Get() != true)
		{
			FailureCallback.ExecuteIfBound();
		}
		else
		{
			SuccessCallback.ExecuteIfBound();
		}
		GetMutableDefault<UGeneralProjectSettings>()->bSpatialNetworking = bCachedSpatialNetworking;
		bSchemaGeneratorRunning = false;
	});
}

void FSpatialGDKEditor::GenerateSnapshot(UWorld* World, FString SnapshotFilename, FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback, FSpatialGDKEditorErrorHandler ErrorCallback)
{
	const bool bSuccess = SpatialGDKGenerateSnapshot(World, SnapshotFilename);

	if (bSuccess)
	{
		SuccessCallback.ExecuteIfBound();
	}
	else
	{
		FailureCallback.ExecuteIfBound();
	}
}

void FSpatialGDKEditor::LaunchCloudDeployment(FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback)
{
	LaunchCloudResult = Async<bool>(EAsyncExecution::Thread, SpatialGDKCloudLaunch,
		[this, SuccessCallback, FailureCallback]
		{
			if (!LaunchCloudResult.IsReady() || LaunchCloudResult.Get() != true)
			{
				FailureCallback.ExecuteIfBound();
			}
			else
			{
				SuccessCallback.ExecuteIfBound();
			}
		});
}

void FSpatialGDKEditor::StopCloudDeployment(FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback)
{
	StopCloudResult = Async<bool>(EAsyncExecution::Thread, SpatialGDKCloudStop,
		[this, SuccessCallback, FailureCallback]
		{
			if (!StopCloudResult.IsReady() || StopCloudResult.Get() != true)
			{
				FailureCallback.ExecuteIfBound();
			}
			else
			{
				SuccessCallback.ExecuteIfBound();
			}
		});
}
