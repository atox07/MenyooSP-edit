#include "StaticModelSpawner.h"

#include "..\Natives\natives2.h"
#include "..\Util\FileLogger.h"
#include "..\Util\StringManip.h"
#include "..\Submenus\Spooner\EntityManagement.h"
#include "..\Submenus\Spooner\Databases.h"
#include "..\Submenus\Spooner\SpoonerSettings.h"
#include "..\main.h"
#include "..\macros.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <mutex>

namespace StaticModelSpawner
{
	bool bAutoResolveUnstreamedModels = true;
	bool bPinSpawnedStaticModels = true;
	extern bool bForceCollisionLoad = true;
	uint32_t g_StreamingTimeoutMs = 5000;

	// Pinned streaming reference tracking
	static std::unordered_map<Hash, int> s_pinnedModels;
	static std::unordered_map<std::string, int> s_pinnedIpls;
	static std::mutex s_pinnedMutex;

	// Dynamic prefix / name mapping catalog
	static std::unordered_map<std::string, std::string> s_customMappings;

	// Base known prefix to IPL/YTYP mapping table
	static const std::vector<std::pair<std::string, std::string>> s_defaultMappings = {
		// Apartments & Interiors
		{ "v_res_tre", "v_int_49" },        // Trevor's / Floyd's apartment
		{ "v_res_m", "v_michael" },         // Michael's mansion
		{ "v_res_fa", "v_franklin" },       // Franklin's Strawberry house
		{ "v_res_d", "v_franklin" },        // Strawberry interior
		{ "v_res_fh", "v_franklin_ph" },    // Franklin's Vinewood Hills pad
		{ "v_res_son", "v_michael" },       // Jimmy's room
		{ "v_res_tt", "v_trailer" },        // Trevor's trailer
		{ "v_club", "v_club" },             // Nightclubs
		{ "bah", "bahama" },                // Bahama Mama's
		{ "cc", "v_comedy" },               // Split Sides Comedy Club
		{ "vu", "v_vanilla" },              // Vanilla Unicorn
		{ "rock", "v_rock" },               // Tequi-la-la
		{ "lab", "humane_lab" },            // Humane Labs
		{ "cor", "morgue" },                // Morgue
		{ "hosp", "hospital" },             // Hospital
		{ "v_med", "v_med" },               // Medical facilities
		{ "v_ret", "v_retail" },            // Retail stores
		{ "247", "v_247" },                 // 24/7 Supermarkets
		{ "csr", "v_carshowroom" },         // Simeon's dealership
		{ "fh", "v_farmhouse" },            // O'Neil Ranch / Farmhouse
		{ "gc", "v_gunclub" },              // Ammunation Shooting Range
		{ "ml", "v_methlab" },              // Meth lab
		{ "ps", "v_ponsonbys" },            // Ponsonbys clothing
		{ "ta", "v_tattoo" },               // Tattoo parlours
		{ "v_serv", "v_services" },         // Services
		{ "bs", "v_barbers" },              // Barber shops
		{ "ct", "v_controltower" },         // Control towers
		{ "v_corp", "v_corp" },             // Corporate / FIB / IAA
		{ "face", "v_lifeinvader" },        // Lifeinvader offices
		{ "p", "v_psych" },                 // Dr. Friedlander's office

		// Heists & DLCs
		{ "hei_prop_carrier", "hei_carrier" }, // Aircraft carrier
		{ "hei_carrier", "hei_carrier" },
		{ "hei_yacht", "hei_yacht" },          // Heist Yacht
		{ "apa_ch2_04", "apa_ch2_04" },        // Vinewood Mansions (Executive DLC)
		{ "apa_ch2", "apa_ch2_04" },

		// Map Chunks (CodeWalker extracted static props)
		{ "ch3_01", "ch3_01" },             // Sandy Shores / Senora
		{ "ch3_02", "ch3_02" },
		{ "ch3_03", "ch3_03" },
		{ "ch3_04", "ch3_04" },
		{ "ch1_01", "ch1_01" },             // North Chumash
		{ "ch2_01", "ch2_01" },             // Banham Canyon
		{ "ch2_02", "ch2_02" },
		{ "ch2_03", "ch2_03" },
		{ "ch2_04", "ch2_04" },             // Vinewood Hills
		{ "cs1_01", "cs1_01" },             // Country South
		{ "cs1_02", "cs1_02" },
		{ "cs2_01", "cs2_01" },
		{ "cs3_01", "cs3_01" },
		{ "dt1_01", "dt1_01" },             // Downtown
		{ "dt1_02", "dt1_02" },
		{ "dt1_03", "dt1_03" },
		{ "dt1_04", "dt1_04" },
		{ "dt1_05", "dt1_05" },
		{ "kt1_01", "kt1_01" },             // Koreatown
		{ "kt1_02", "kt1_02" },
		{ "bh1_01", "bh1_01" },             // Rockford Hills
		{ "bh1_02", "bh1_02" },
		{ "id1_01", "id1_01" },             // Industrial
		{ "id1_02", "id1_02" },
		{ "sc1_01", "sc1_01" },             // South Central
		{ "sc1_02", "sc1_02" },
		{ "sm_01", "sm_01" },               // Santa Monica / Del Perro
		{ "sm_02", "sm_02" },
		{ "sm_03", "sm_03" },
		{ "ss1_01", "ss1_01" },             // South Shores / Port of LS
		{ "ss1_02", "ss1_02" },
		{ "vb_01", "vb_01" },               // Vespucci Beach
		{ "vb_02", "vb_02" },
		{ "vb_03", "vb_03" },
		{ "vb_04", "vb_04" }
	};

	static std::string ToHexStr(Hash h)
	{
		std::ostringstream ss;
		ss << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << h;
		return ss.str();
	}

	Hash ParseModelInput(const std::string& input, std::string& outCleanName)
	{
		std::string str = input;
		// Trim leading/trailing whitespace
		str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) { return !std::isspace(ch); }));
		str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), str.end());

		if (str.empty())
		{
			outCleanName.clear();
			return 0;
		}

		// Hex notation: "0x1234ABCD" or "0X..."
		if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
		{
			try
			{
				Hash h = static_cast<Hash>(std::stoul(str, nullptr, 16));
				outCleanName = ToHexStr(h);
				return h;
			}
			catch (...) {}
		}

		// Numeric decimal notation (positive or negative)
		bool isNumeric = true;
		size_t startIdx = (str[0] == '-' || str[0] == '+') ? 1 : 0;
		if (startIdx >= str.size()) isNumeric = false;
		for (size_t i = startIdx; i < str.size(); ++i)
		{
			if (!std::isdigit(static_cast<unsigned char>(str[i])))
			{
				isNumeric = false;
				break;
			}
		}

		if (isNumeric)
		{
			try
			{
				if (str[0] == '-')
				{
					int32_t signedVal = std::stol(str);
					Hash h = static_cast<Hash>(signedVal);
					outCleanName = ToHexStr(h);
					return h;
				}
				else
				{
					Hash h = static_cast<Hash>(std::stoul(str));
					outCleanName = ToHexStr(h);
					return h;
				}
			}
			catch (...) {}
		}

		// Plain text string model name
		outCleanName = str;
		return GET_HASH_KEY(str.c_str());
	}

	std::string FindYtypOrIplForModel(Hash modelHash, const std::string& modelName)
	{
		std::string lowerName = modelName;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return std::tolower(c); });

		// 1. Check custom runtime overrides
		auto customIt = s_customMappings.find(lowerName);
		if (customIt != s_customMappings.end())
			return customIt->second;

		// 2. Check registered default prefix table
		for (const auto& mapping : s_defaultMappings)
		{
			if (lowerName.rfind(mapping.first, 0) == 0) // starts_with
			{
				return mapping.second;
			}
		}

		// 3. Heuristic map chunk extraction: e.g. "prop_ch2_04_gate" or "apa_ch2_04_mansion"
		// If name contains a sector code like "ch2_04", "dt1_01", "cs1_02", etc.
		static const std::vector<std::string> sectors = {
			"ch1_01", "ch1_02", "ch2_01", "ch2_02", "ch2_03", "ch2_04",
			"ch3_01", "ch3_02", "ch3_03", "ch3_04",
			"cs1_01", "cs1_02", "cs2_01", "cs3_01",
			"dt1_01", "dt1_02", "dt1_03", "dt1_04", "dt1_05",
			"kt1_01", "kt1_02", "bh1_01", "bh1_02",
			"id1_01", "id1_02", "sc1_01", "sc1_02",
			"sm_01", "sm_02", "sm_03", "ss1_01", "ss1_02",
			"vb_01", "vb_02", "vb_03", "vb_04"
		};

		for (const auto& sec : sectors)
		{
			if (lowerName.find(sec) != std::string::npos)
			{
				return sec;
			}
		}

		return "";
	}

	void RegisterCustomMapping(const std::string& modelPrefixOrName, const std::string& iplOrYtyp)
	{
		std::string lower = modelPrefixOrName;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
		s_customMappings[lower] = iplOrYtyp;
	}

	namespace PinnedManager
	{
		void PinModel(Hash modelHash)
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			s_pinnedModels[modelHash]++;
			REQUEST_MODEL(modelHash);
			addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] Pinned model " + ToHexStr(modelHash) + ", refcount=" + std::to_string(s_pinnedModels[modelHash]));
		}

		void UnpinModel(Hash modelHash)
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			auto it = s_pinnedModels.find(modelHash);
			if (it != s_pinnedModels.end())
			{
				it->second--;
				if (it->second <= 0)
				{
					s_pinnedModels.erase(it);
					SET_MODEL_AS_NO_LONGER_NEEDED(modelHash);
					addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] Unpinned and released model " + ToHexStr(modelHash));
				}
			}
		}

		void PinIpl(const std::string& iplName)
		{
			if (iplName.empty()) return;
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			s_pinnedIpls[iplName]++;
			REQUEST_IPL(iplName.c_str());
			addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] Pinned IPL/YTYP '" + iplName + "', refcount=" + std::to_string(s_pinnedIpls[iplName]));
		}

		void UnpinIpl(const std::string& iplName)
		{
			if (iplName.empty()) return;
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			auto it = s_pinnedIpls.find(iplName);
			if (it != s_pinnedIpls.end())
			{
				it->second--;
				if (it->second <= 0)
				{
					s_pinnedIpls.erase(it);
					REMOVE_IPL(iplName.c_str());
					addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] Unpinned and released IPL/YTYP '" + iplName + "'");
				}
			}
		}

		void Tick()
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			// Periodically verify pinned assets remain requested in RAGE streaming cache
			for (const auto& pair : s_pinnedModels)
			{
				if (!HAS_MODEL_LOADED(pair.first))
				{
					REQUEST_MODEL(pair.first);
				}
			}
			for (const auto& pair : s_pinnedIpls)
			{
				if (!IS_IPL_ACTIVE(pair.first.c_str()))
				{
					REQUEST_IPL(pair.first.c_str());
				}
			}
		}

		size_t GetPinnedModelCount()
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			return s_pinnedModels.size();
		}

		size_t GetPinnedIplCount()
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			return s_pinnedIpls.size();
		}

		void ClearAll()
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			for (const auto& pair : s_pinnedModels)
			{
				SET_MODEL_AS_NO_LONGER_NEEDED(pair.first);
			}
			for (const auto& pair : s_pinnedIpls)
			{
				REMOVE_IPL(pair.first.c_str());
			}
			s_pinnedModels.clear();
			s_pinnedIpls.clear();
			addlog(ige::LogType::LOG_INFO, "[StaticModelSpawner] All pinned models and IPLs cleared.");
		}

		std::vector<std::pair<Hash, int>> GetPinnedModelsList()
		{
			std::lock_guard<std::mutex> lock(s_pinnedMutex);
			std::vector<std::pair<Hash, int>> list;
			for (const auto& p : s_pinnedModels) list.push_back(p);
			return list;
		}
	}

	bool EnsureModelReady(Hash modelHash, const std::string& modelName, uint32_t timeoutMs)
	{
		if (!modelHash) return false;

		// 1. Direct registry check via RAGE's CModelInfo::GetModelInfo
		int streamingIndex = -1;
		CBaseModelInfo* pInfo = GetModelInfo ? GetModelInfo(modelHash, &streamingIndex) : nullptr;
		if (pInfo != nullptr)
		{
			addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] Model " + ToHexStr(modelHash) + " already registered in RAGE archetype registry at 0x" + std::to_string((uintptr_t)pInfo) + " (slot " + std::to_string(streamingIndex) + ").");
			return true;
		}

		// 2. Archetype is missing: resolve asset from game archives without teleporting player
		std::string resolvedTarget = FindYtypOrIplForModel(modelHash, modelName);
		if (!resolvedTarget.empty())
		{
			addlog(ige::LogType::LOG_INFO, "[StaticModelSpawner] Resolving model " + ToHexStr(modelHash) + " ('" + modelName + "') via asset container '" + resolvedTarget + "'...");
			REQUEST_IPL(resolvedTarget.c_str());

			DWORD startTime = GetTickCount();
			while (GetTickCount() - startTime < timeoutMs)
			{
				pInfo = GetModelInfo ? GetModelInfo(modelHash, &streamingIndex) : nullptr;
				if (pInfo != nullptr)
				{
					addlog(ige::LogType::LOG_INFO, "[StaticModelSpawner] Streamer loaded container '" + resolvedTarget + "'. Archetype registered at 0x" + std::to_string((uintptr_t)pInfo) + " (slot " + std::to_string(streamingIndex) + ") in " + std::to_string(GetTickCount() - startTime) + "ms.");
					PinnedManager::PinIpl(resolvedTarget);
					return true;
				}
				scriptWait(0);
			}

			addlog(ige::LogType::LOG_WARNING, "[StaticModelSpawner] Timed out waiting for container '" + resolvedTarget + "' to register archetype for model " + ToHexStr(modelHash));
		}
		else
		{
			addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] No matching IPL/YTYP mapping found for model " + ToHexStr(modelHash) + " ('" + modelName + "')");
		}

		// 3. Fallback: Check if CDImage has it directly or if it can be requested
		if (IS_MODEL_IN_CDIMAGE(modelHash))
		{
			addlog(ige::LogType::LOG_DEBUG, "[StaticModelSpawner] Model " + ToHexStr(modelHash) + " is present in CDImage directly.");
			return true;
		}

		return false;
	}

	ModelDiagnosticResult RunDiagnostic(Hash modelHash, const std::string& modelName, bool createTestObject)
	{
		ModelDiagnosticResult diag;
		diag.hash = modelHash;
		diag.inputName = modelName;

		GTAped playerPed = PLAYER_PED_ID();
		diag.playerPos = playerPed.GetPosition();

		addlog(ige::LogType::LOG_INFO, "================================================================================");
		addlog(ige::LogType::LOG_INFO, "[DIAGNOSTIC] Static Model Spawn Analysis for: '" + modelName + "' (" + ToHexStr(modelHash) + " / " + std::to_string(modelHash) + ")");
		addlog(ige::LogType::LOG_INFO, "[DIAGNOSTIC] Player Position: X=" + std::to_string(diag.playerPos.x) + ", Y=" + std::to_string(diag.playerPos.y) + ", Z=" + std::to_string(diag.playerPos.z));
		addlog(ige::LogType::LOG_INFO, "--------------------------------------------------------------------------------");

		// STEP 1: Archetype in RAGE Registry (CModelInfo::GetModelInfo / fwArchetypeManager)
		int streamingIndex = -1;
		CBaseModelInfo* pInfo = GetModelInfo ? GetModelInfo(modelHash, &streamingIndex) : nullptr;
		if (pInfo != nullptr)
		{
			diag.archetypeFound = true;
			diag.archetypeAddress = reinterpret_cast<uintptr_t>(pInfo);
			diag.streamingIndex = streamingIndex;
			diag.archetypePoolIndex = pInfo->m_index;
			diag.modelType = pInfo->GetModelType();
			diag.flags = pInfo->m_flags;
			diag.radius = pInfo->m_radius;
			diag.aabbMin[0] = pInfo->m_aabbMin[0];
			diag.aabbMin[1] = pInfo->m_aabbMin[1];
			diag.aabbMin[2] = pInfo->m_aabbMin[2];
			diag.aabbMax[0] = pInfo->m_aabbMax[0];
			diag.aabbMax[1] = pInfo->m_aabbMax[1];
			diag.aabbMax[2] = pInfo->m_aabbMax[2];

			addlog(ige::LogType::LOG_INFO, "[STEP 1 - RAGE ARCHETYPE REGISTRY]: FOUND (REGISTERED)");
			addlog(ige::LogType::LOG_INFO, "  -> Address: 0x" + std::to_string(diag.archetypeAddress));
			addlog(ige::LogType::LOG_INFO, "  -> Hash in Struct: " + ToHexStr(pInfo->m_hash));
			addlog(ige::LogType::LOG_INFO, "  -> Streaming Slot Index: " + std::to_string(streamingIndex));
			addlog(ige::LogType::LOG_INFO, "  -> Pool Index: " + std::to_string(diag.archetypePoolIndex));
			addlog(ige::LogType::LOG_INFO, "  -> ModelType: " + std::to_string(diag.modelType) + (diag.modelType == 0 ? " (CObject / Prop)" : diag.modelType == 1 ? " (CBuilding / MLO)" : diag.modelType == 2 ? " (CTime)" : " (Other)"));
			addlog(ige::LogType::LOG_INFO, "  -> Flags: 0x" + std::to_string(diag.flags));
			addlog(ige::LogType::LOG_INFO, "  -> Bounding Radius: " + std::to_string(diag.radius));
			addlog(ige::LogType::LOG_INFO, "  -> Bounding Box Min: (" + std::to_string(diag.aabbMin[0]) + ", " + std::to_string(diag.aabbMin[1]) + ", " + std::to_string(diag.aabbMin[2]) + ")");
			addlog(ige::LogType::LOG_INFO, "  -> Bounding Box Max: (" + std::to_string(diag.aabbMax[0]) + ", " + std::to_string(diag.aabbMax[1]) + ", " + std::to_string(diag.aabbMax[2]) + ")");
		}
		else
		{
			diag.archetypeFound = false;
			addlog(ige::LogType::LOG_INFO, "[STEP 1 - RAGE ARCHETYPE REGISTRY]: MISSING (NOT REGISTERED)");
			addlog(ige::LogType::LOG_INFO, "  -> CModelInfo::GetModelInfo returned NULL.");
			addlog(ige::LogType::LOG_INFO, "  -> Diagnosis: Model archetype is not in active fwArchetypeManager table. It belongs to an unstreamed YTYP map chunk.");
		}

		// STEP 2: IS_MODEL_IN_CDIMAGE & IS_MODEL_VALID
		diag.isInCdImage = (IS_MODEL_IN_CDIMAGE(modelHash) != 0);
		diag.isModelValid = (IS_MODEL_VALID(modelHash) != 0);
		addlog(ige::LogType::LOG_INFO, "--------------------------------------------------------------------------------");
		addlog(ige::LogType::LOG_INFO, "[STEP 2 - CDIMAGE & VALIDITY NATIVES]:");
		addlog(ige::LogType::LOG_INFO, "  -> IS_MODEL_IN_CDIMAGE: " + std::string(diag.isInCdImage ? "TRUE (1)" : "FALSE (0)"));
		addlog(ige::LogType::LOG_INFO, "  -> IS_MODEL_VALID: " + std::string(diag.isModelValid ? "TRUE (1)" : "FALSE (0)"));

		// STEP 3: REQUEST_MODEL, HAS_MODEL_LOADED & COLLISION
		addlog(ige::LogType::LOG_INFO, "--------------------------------------------------------------------------------");
		addlog(ige::LogType::LOG_INFO, "[STEP 3 - STREAMING REQUEST & TIMEOUT]:");
		REQUEST_MODEL(modelHash);
		REQUEST_COLLISION_FOR_MODEL(modelHash);

		DWORD startLoad = GetTickCount();
		while (GetTickCount() - startLoad < 3000)
		{
			if (HAS_MODEL_LOADED(modelHash))
			{
				diag.modelLoaded = true;
				diag.modelLoadTimeMs = GetTickCount() - startLoad;
				break;
			}
			scriptWait(0);
		}

		DWORD startColl = GetTickCount();
		while (GetTickCount() - startColl < 1500)
		{
			if (HAS_COLLISION_FOR_MODEL_LOADED(modelHash))
			{
				diag.collisionLoaded = true;
				diag.collisionLoadTimeMs = GetTickCount() - startColl;
				break;
			}
			scriptWait(0);
		}

		addlog(ige::LogType::LOG_INFO, "  -> HAS_MODEL_LOADED: " + std::string(diag.modelLoaded ? ("SUCCESS (Loaded in " + std::to_string(diag.modelLoadTimeMs) + "ms)") : "FAILED (Timed out after 3000ms)"));
		addlog(ige::LogType::LOG_INFO, "  -> HAS_COLLISION_FOR_MODEL_LOADED: " + std::string(diag.collisionLoaded ? ("SUCCESS (Loaded in " + std::to_string(diag.collisionLoadTimeMs) + "ms)") : "NOT LOADED / NO SEPARATE YBN BOUNDS"));

		// STEP 4: CREATE_OBJECT_NO_OFFSET
		addlog(ige::LogType::LOG_INFO, "--------------------------------------------------------------------------------");
		addlog(ige::LogType::LOG_INFO, "[STEP 4 - ENTITY INSTANTIATION]:");
		if (createTestObject && diag.modelLoaded)
		{
			Vector3 spawnPos = playerPed.GetOffsetInWorldCoords(0.0f, 3.5f, 0.0f);
			int obj = CREATE_OBJECT_NO_OFFSET(modelHash, spawnPos.x, spawnPos.y, spawnPos.z, true, true, false);
			if (obj == 0)
			{
				// Retry with dynamic = true
				obj = CREATE_OBJECT(modelHash, spawnPos.x, spawnPos.y, spawnPos.z, true, true, true);
			}

			if (obj != 0)
			{
				diag.spawnSuccess = true;
				diag.objectHandle = obj;
				diag.objectAddress = reinterpret_cast<uintptr_t>(getScriptHandleBaseAddress(obj));
				addlog(ige::LogType::LOG_INFO, "  -> CREATE_OBJECT_NO_OFFSET: SUCCESS");
				addlog(ige::LogType::LOG_INFO, "  -> Object Handle: " + std::to_string(obj));
				addlog(ige::LogType::LOG_INFO, "  -> Object Native Address: 0x" + std::to_string(diag.objectAddress));

				// Cleanup temporary test object
				SET_ENTITY_AS_MISSION_ENTITY(obj, true, true);
				DELETE_OBJECT(&obj);
			}
			else
			{
				diag.spawnSuccess = false;
				diag.failureReason = (diag.modelType != 0)
					? ("Model is loaded but Archetype Type is " + std::to_string(diag.modelType) + " (engine requires 0 for script CObject).")
					: "Engine CREATE_OBJECT factory returned 0.";
				addlog(ige::LogType::LOG_ERROR, "  -> CREATE_OBJECT_NO_OFFSET: FAILED (" + diag.failureReason + ")");
			}
		}
		else
		{
			diag.spawnSuccess = false;
			diag.failureReason = !diag.modelLoaded ? "Cannot attempt spawn because model failed to load into memory." : "Test object instantiation skipped.";
			addlog(ige::LogType::LOG_WARNING, "  -> Spawn check skipped: " + diag.failureReason);
		}

		// Try resolving YTYP/IPL for recommendation
		diag.resolvedIplOrYtyp = FindYtypOrIplForModel(modelHash, modelName);
		if (!diag.resolvedIplOrYtyp.empty())
		{
			addlog(ige::LogType::LOG_INFO, "--------------------------------------------------------------------------------");
			addlog(ige::LogType::LOG_INFO, "[RESOLVER SUGGESTION]: Associated YTYP/IPL asset is '" + diag.resolvedIplOrYtyp + "'.");
		}

		addlog(ige::LogType::LOG_INFO, "================================================================================");

		// On-screen notification summary
		std::string notifyHeader = "Diagnostic: " + (modelName.empty() ? ToHexStr(modelHash) : modelName);
		std::string notifyBody = "RAGE: " + std::string(diag.archetypeFound ? "~g~OK" : "~r~MISSING")
			+ " | CDImage: " + std::string(diag.isInCdImage ? "~g~YES" : "~r~NO")
			+ "\nLoad: " + std::string(diag.modelLoaded ? "~g~READY" : "~r~FAIL")
			+ " | Spawn: " + std::string(diag.spawnSuccess ? "~g~OK" : "~r~FAIL");

		Game::Print::ShowNotification(notifyHeader, notifyBody);

		return diag;
	}

	int SpawnStaticModel(Hash modelHash, const std::string& modelName, const Vector3& position, const Vector3& rotation, bool dynamic, bool addToDb)
	{
		if (!modelHash) return 0;

		// 1. Ensure archetype is registered in RAGE streamer
		if (!EnsureModelReady(modelHash, modelName, g_StreamingTimeoutMs))
		{
			addlog(ige::LogType::LOG_ERROR, "[StaticModelSpawner] Failed to resolve and register archetype for model " + ToHexStr(modelHash) + " ('" + modelName + "').");
			Game::Print::ShowNotification("~r~Spawn Error:", "Model " + (modelName.empty() ? ToHexStr(modelHash) : modelName) + " could not be resolved in game streaming.");
			return 0;
		}

		// 2. Request model asset from CDImage
		REQUEST_MODEL(modelHash);
		if (bForceCollisionLoad)
		{
			REQUEST_COLLISION_FOR_MODEL(modelHash);
		}

		DWORD startTime = GetTickCount();
		bool loaded = false;
		while (GetTickCount() - startTime < g_StreamingTimeoutMs)
		{
			if (HAS_MODEL_LOADED(modelHash))
			{
				loaded = true;
				break;
			}
			scriptWait(0);
		}

		if (!loaded)
		{
			addlog(ige::LogType::LOG_ERROR, "[StaticModelSpawner] Timed out waiting for HAS_MODEL_LOADED on " + ToHexStr(modelHash) + " ('" + modelName + "').");
			Game::Print::ShowNotification("~r~Spawn Error:", "Streaming timed out while loading model " + (modelName.empty() ? ToHexStr(modelHash) : modelName));
			return 0;
		}

		// 3. Create the object at target destination
		int obj = CREATE_OBJECT_NO_OFFSET(modelHash, position.x, position.y, position.z, true, true, dynamic);
		if (obj == 0)
		{
			// Fallback: try CREATE_OBJECT with dynamic
			obj = CREATE_OBJECT(modelHash, position.x, position.y, position.z, true, true, dynamic);
		}

		if (obj == 0)
		{
			// Investigate reason: check archetype type
			int streamingIdx = -1;
			CBaseModelInfo* pInfo = GetModelInfo ? GetModelInfo(modelHash, &streamingIdx) : nullptr;
			uint8_t mType = pInfo ? pInfo->GetModelType() : 255;
			addlog(ige::LogType::LOG_ERROR, "[StaticModelSpawner] CREATE_OBJECT_NO_OFFSET failed for model " + ToHexStr(modelHash) + ". Model is loaded, but Archetype Type=" + std::to_string(mType) + " (0=Object).");
			Game::Print::ShowNotification("~r~Spawn Error:", "Engine refused object creation. Archetype Type=" + std::to_string(mType));
			return 0;
		}

		// 4. Configure rotation and entity flags
		SET_ENTITY_ROTATION(obj, rotation.x, rotation.y, rotation.z, 2, true);
		SET_ENTITY_AS_MISSION_ENTITY(obj, true, true);
		SET_ENTITY_LOAD_COLLISION_FLAG(obj, true);
		FREEZE_ENTITY_POSITION(obj, !dynamic);
		SET_ENTITY_LOD_DIST(obj, 1000000);

		// 5. Pin streaming reference if enabled (prevents game culling)
		if (bPinSpawnedStaticModels)
		{
			PinnedManager::PinModel(modelHash);
		}

		// 6. Register into Spooner entity database if requested
		if (addToDb)
		{
			using namespace sub::Spooner;
			SpoonerEntity newEnt;
			newEnt.handle = obj;
			newEnt.type = EntityType::PROP;
			newEnt.hashName = modelName.empty() ? ToHexStr(modelHash) : modelName;
			newEnt.dynamic = dynamic;
			EntityManagement::AddEntityToDb(newEnt);
		}

		addlog(ige::LogType::LOG_INFO, "[StaticModelSpawner] Successfully spawned static model " + ToHexStr(modelHash) + " ('" + modelName + "') at (" + std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z) + ") -> Handle=" + std::to_string(obj));
		Game::Print::ShowNotification("Spawned Static Model:", (modelName.empty() ? ToHexStr(modelHash) : modelName));

		return obj;
	}
}
