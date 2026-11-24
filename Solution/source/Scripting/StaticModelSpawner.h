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
		uintptr_t archetypeAddress = 0;
		int streamingIndex = -1;
		uint16_t archetypePoolIndex = 0;
		uint8_t modelType = 0; // 0 = Object, 1 = MLO, 2 = Time, 3 = Weapon, 4 = Vehicle, 5 = Ped, etc.
		uint32_t flags = 0;
		float radius = 0.0f;
		float aabbMin[3] = { 0.0f, 0.0f, 0.0f };
		float aabbMax[3] = { 0.0f, 0.0f, 0.0f };

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
		uintptr_t objectAddress = 0;
		bool spawnSuccess = false;
		std::string failureReason;

		std::string resolvedIplOrYtyp;
	};

	// Runtime settings
	extern bool bAutoResolveUnstreamedModels;
	extern bool bPinSpawnedStaticModels;
	extern bool bForceCollisionLoad;
	extern uint32_t g_StreamingTimeoutMs;

	// Model input parser (supports decimal, hex format like "0x5E6BD6D1", or literal model string names)
	Hash ParseModelInput(const std::string& input, std::string& outCleanName);

	// Diagnostic function: compares RAGE registry, CDImage, Request, and CreateObject, logging every step separately
	ModelDiagnosticResult RunDiagnostic(Hash modelHash, const std::string& modelName, bool createTestObject = true);

	// Resolves the model from game archives (locates YTYP/IPL and forces the streamer to load/register it at runtime)
	bool EnsureModelReady(Hash modelHash, const std::string& modelName = "", uint32_t timeoutMs = 5000);

	// Spawns the static model once streaming is verified ready, configuring flags and managing lifetime references
	int SpawnStaticModel(Hash modelHash, const std::string& modelName, const Vector3& position, const Vector3& rotation, bool dynamic = false, bool addToDb = true);

	// YTYP / IPL catalog and resolver
	std::string FindYtypOrIplForModel(Hash modelHash, const std::string& modelName);
	void RegisterCustomMapping(const std::string& modelPrefixOrName, const std::string& iplOrYtyp);

	// Pinned streaming reference manager (guarantees model never vanishes when moving away or closing menu)
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
