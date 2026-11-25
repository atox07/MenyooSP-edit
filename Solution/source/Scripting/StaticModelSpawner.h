#pragma once

#include "..\Natives\types.h"
#include "..\Util\GTAmath.h"
#include "..\Memory\GTAmemory.h"

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <cstdint>

namespace StaticModelSpawner
{
	// Result structure holding the full 4-step diagnostic breakdown
	struct ModelDiagnosticResult
	{
		Hash hash = 0;
		std::string inputName;
		Vector3 playerPos;

		// Step 1: RAGE Archetype Registry (CModelInfo / fwArchetypeManager)
		bool archetypeFound = false;
		int streamingIndex = -1;

		// Step 2: CDImage & Validity Checks
		bool isInCdImage = false;
		bool isModelValid = false;

		// Step 3: Streamer Request & Timeout
		bool modelLoaded = false;
		uint32_t modelLoadTimeMs = 0;
		bool collisionLoaded = false;
		uint32_t collisionLoadTimeMs = 0;

		// Step 4: Object Instantiation (CREATE_OBJECT_NO_OFFSET)
		int objectHandle = 0;
		bool spawnSuccess = false;
		std::string failureReason;
	};

	// Una sola posizione (origin YMAP) per modello, tabella ordinata per hash.
	struct StaticPlacement
	{
		Hash hash;
		float x, y, z;
	};
	const StaticPlacement* GetIndexedPlacements(size_t& count);

	// Runtime settings
	extern bool bAutoResolveUnstreamedModels;
	extern bool bPinSpawnedStaticModels;
	extern bool bForceCollisionLoad;
	extern uint32_t g_StreamingTimeoutMs;

	// Model input parser (supports decimal, hex format like "0x5E6BD6D1", or literal model string names)
	Hash ParseModelInput(const std::string& input, std::string& outCleanName);

	// Diagnostic function: compares RAGE registry, CDImage, Request, and CreateObject, logging every step separately
	ModelDiagnosticResult RunDiagnostic(Hash modelHash, const std::string& modelName, bool createTestObject = true);

	// Checks model availability and optionally tries an IPL hint for known name patterns.
	bool EnsureModelReady(Hash modelHash, const std::string& modelName = "", uint32_t timeoutMs = 5000);

	// Spawns the static model once streaming is verified ready, configuring flags and managing lifetime references
	int SpawnStaticModel(Hash modelHash, const std::string& modelName, const Vector3& position, const Vector3& rotation, bool dynamic = false, bool addToDb = true);

	// YTYP / IPL catalog and resolver
	std::string FindYtypOrIplForModel(Hash modelHash, const std::string& modelName);
	void RegisterCustomMapping(const std::string& modelPrefixOrName, const std::string& iplOrYtyp);

	// Tracks streaming requests held for spawned models and requested IPLs.
	namespace PinnedManager
	{
		void PinModel(Hash modelHash);
		void UnpinModel(Hash modelHash);
		void PinIpl(const std::string& iplName);
		void UnpinIpl(const std::string& iplName);
		void Tick();
		size_t GetPinnedModelCount();
		size_t GetPinnedIplCount();
		void ClearAll();
		std::vector<std::pair<Hash, int>> GetPinnedModelsList();
	}
}
