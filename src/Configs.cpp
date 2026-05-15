#include <Configs.h>
#include <Utils.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <Globals.h>
#include <Hooks.h>
#include <EditorUI.h>

Configs::AdjustmentDataMap Configs::adjustDataMap;
Configs::AdjustmentData* Configs::adjustment = nullptr;

void Configs::LoadConfigs()
{
	Hooks::shouldAdjust = false;
	adjustDataMap.clear();
	adjustDataMap.insert({ 0xFFFFFFFF, { std::pair(0xFFFFFFFF, AdjustmentData()) } });
	adjustment = &adjustDataMap.at(0xFFFFFFFF).at(0xFFFFFFFF);
	namespace fs = std::filesystem;
	fs::path jsonPath = fs::current_path();
	jsonPath += "\\Data\\F4SE\\Plugins\\GunMover";
	std::stringstream stream;
	std::ifstream reader;
	fs::directory_entry jsonEntry{ jsonPath };
	if (!jsonEntry.exists()) {
		logger::error("GunMover directory does not exist!"sv);
		return;
	}
	try {
		for (auto& it : fs::directory_iterator(jsonEntry)) {
			if (it.path().extension().compare(".json") == 0) {
				stream << it.path().filename();
				logger::info("Loading adjustment data {}", stream.str().c_str());
				stream.str(std::string());
				reader.open(it.path());
				nlohmann::json customData;
				reader >> customData;

				for (auto wepit = customData.begin(); wepit != customData.end(); ++wepit) {
					std::string wepFormIDstr;
					std::string wepPlugin = Utils::SplitString(wepit.key(), "|", wepFormIDstr);
					std::unordered_set<RE::TESForm*> weaponsInMod;
					if (wepFormIDstr.length() != 0) {
						if (wepFormIDstr.compare("*") == 0) {
							logger::info("Finding all weapons in {}", wepPlugin.c_str());
							RE::TESDataHandler* dh = RE::TESDataHandler::GetSingleton();
							RE::BSTArray<RE::TESObjectWEAP*> weapons = dh->GetFormArray<RE::TESObjectWEAP>();
							for (auto wepdhit = weapons.begin(); wepdhit != weapons.end(); ++wepdhit) {
								for (auto fileit = (*wepdhit)->sourceFiles.array->begin(); fileit != (*wepdhit)->sourceFiles.array->end(); ++fileit) {
									if (strcmp(wepPlugin.c_str(), (*fileit)->filename) == 0) {
										weaponsInMod.insert(*wepdhit);
										logger::info("Found {:04X}", (*wepdhit)->formID);
										break;
									}
								}
							}
						} else {
							logger::info("Getting Weapon: Form ID {} from {}", wepFormIDstr.c_str(), wepPlugin.c_str());
							uint32_t wepFormID = std::stoi(wepFormIDstr, 0, 16);
							RE::TESForm* wepForm = Utils::GetFormFromMod(wepPlugin, wepFormID);
							if (wepForm && wepForm->formType == RE::ENUM_FORM_ID::kWEAP) {
								weaponsInMod.insert(wepForm);
								logger::info("Found {:04X}", wepForm->formID);
							} else {
								logger::error("Invalid form or form is not a weapon.");
							}
						}
						for (auto modit = weaponsInMod.begin(); modit != weaponsInMod.end(); ++modit) {
							for (auto omodit = wepit.value().begin(); omodit != wepit.value().end(); ++omodit) {
								std::string omodFormIDstr;
								std::string omodPlugin = Utils::SplitString(omodit.key(), "|", omodFormIDstr);
								if (omodFormIDstr.length() != 0) {
									RE::TESForm* omodForm = Utils::GetFormFromMod(omodPlugin, std::stoi(omodFormIDstr, 0, 16));
									if (omodForm && (omodForm->formType == RE::ENUM_FORM_ID::kOMOD || omodForm->formType == RE::ENUM_FORM_ID::kKYWD)) {
										RegisterAdjustmentData(omodit, (*modit)->formID, omodForm->formID, omodForm->formType == RE::ENUM_FORM_ID::kKYWD);
									} else {
										logger::error("Form is not OMOD or keyword. Recevied %s", (omodPlugin + omodFormIDstr).c_str());
									}
								} else {
									logger::error("OMOD data invalid. Check JSON. Recevied %s", (omodPlugin + omodFormIDstr).c_str());
								}
							}
						}
					} else {
						logger::error("Weapon data invalid. Check JSON. Recevied %s", (wepPlugin + wepFormIDstr).c_str());
					}
				}
				reader.close();
			}
		}
	} catch (...) {
		if (reader.is_open()) {
			logger::error("Malformed json file. Use https://jsoneditoronline.org/ for easier edits.");
			reader.close();
		} else {
			logger::error("Could not open json file.");
		}
	}
}

void Configs::RegisterAdjustmentData(auto a_iter, uint32_t wepFormID, uint32_t omodFormID, bool isKeyword)
{
	float x = 0, y = 0, z = 0, rotX = 0, rotY = 0, rotZ = 0;
	std::bitset<(size_t)REVERT_FLAG::kNumFlags> revertFlags;
	std::queue<AlternativeAdjustment> alternatives;

	for (auto adjustData = a_iter.value().begin(); adjustData != a_iter.value().end(); ++adjustData) {
		if (adjustData.key() == "X") {
			x = adjustData.value().get<float>();
		} else if (adjustData.key() == "Y") {
			y = adjustData.value().get<float>();
		} else if (adjustData.key() == "Z") {
			z = adjustData.value().get<float>();
		} else if (adjustData.key() == "rotX") {
			rotX = adjustData.value().get<float>();
		} else if (adjustData.key() == "rotY") {
			rotY = adjustData.value().get<float>();
		} else if (adjustData.key() == "rotZ") {
			rotZ = adjustData.value().get<float>();
		} else if (adjustData.key() == "RevertOnReload") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnReload, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnMelee") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnMelee, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnThrow") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnThrow, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnSprint") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnSprint, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnEquip") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnEquip, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnFastEquip") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnFastEquip, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnUnequip") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnUnequip, adjustData.value().get<bool>());
		} else if (adjustData.key() == "RevertOnGunDown") {
			revertFlags.set((size_t)REVERT_FLAG::kRevertOnGunDown, adjustData.value().get<bool>());
		} else if (adjustData.key() == "Persistent") {
			// 추가: Persistent 모드 파싱
			revertFlags.set((size_t)REVERT_FLAG::kPersistent, adjustData.value().get<bool>());
		} else if (adjustData.key() == "Alternatives") {
			for (auto altit = adjustData.value().begin(); altit != adjustData.value().end(); ++altit) {
				float altX = 0, altY = 0, altZ = 0, altRotX = 0, altRotY = 0, altRotZ = 0;
				for (auto altAdjustData = altit.value().begin(); altAdjustData != altit.value().end(); ++altAdjustData) {
					if (altAdjustData.key() == "X") {
						altX = altAdjustData.value().get<float>();
					} else if (altAdjustData.key() == "Y") {
						altY = altAdjustData.value().get<float>();
					} else if (altAdjustData.key() == "Z") {
						altZ = altAdjustData.value().get<float>();
					} else if (altAdjustData.key() == "rotX") {
						altRotX = altAdjustData.value().get<float>();
					} else if (altAdjustData.key() == "rotY") {
						altRotY = altAdjustData.value().get<float>();
					} else if (altAdjustData.key() == "rotZ") {
						altRotZ = altAdjustData.value().get<float>();
					}
				}
				alternatives.push(AlternativeAdjustment(RE::NiPoint3(altX, altY, altZ), RE::NiPoint3(altRotX * toRad, altRotY * toRad, altRotZ * toRad)));
			}
		}
	}

	auto wepExistit = adjustDataMap.find(wepFormID);
	bool succ = false;

	if (wepExistit == adjustDataMap.end()) {
		std::tie(wepExistit, succ) = adjustDataMap.insert({ wepFormID, {} });
	} else {
		succ = true;
	}

	if (succ) {
		auto& omodMap = wepExistit->second;
		auto omodExistit = omodMap.find(omodFormID);

		std::string typeStr = "OMOD";
		if (isKeyword)
			typeStr = "Keyword";
		if (omodExistit == omodMap.end()) {
			omodMap.insert({ omodFormID, AdjustmentData(x, y, z, rotX, rotY, rotZ, revertFlags, alternatives) });
			logger::info("Added Weapon {:04X} - {} {:04X} with adjustment data (X: {:.2f}, Y: {:.2f}, Z: {:.2f}, rotX: {:.2f}, rotY: {:.2f}, rotZ: {:.2f}, RevertFlags: {}, Persistent: {}, Alternatives: {})", wepFormID, typeStr.c_str(), omodFormID, x, y, z, rotX, rotY, rotZ, revertFlags.to_string(), revertFlags.test((size_t)REVERT_FLAG::kPersistent), alternatives.size());
		} else {
			omodExistit->second = AdjustmentData(x, y, z, rotX, rotY, rotZ, revertFlags, alternatives);
			logger::info("Duplicate {} data found for Weapon {:04X} - {} {:04X}. Overwriting with new adjustment data (X: {:.2f}, Y: {:.2f}, Z: {:.2f}, rotX: {:.2f}, rotY: {:.2f}, rotZ: {:.2f}, RevertFlags: {}, Persistent: {}, Alternatives: {})", typeStr.c_str(), wepFormID, typeStr.c_str(), omodFormID, x, y, z, rotX, rotY, rotZ, revertFlags.to_string(), revertFlags.test((size_t)REVERT_FLAG::kPersistent), alternatives.size());
		}
	} else {
		logger::error("Insertion failed for Weapon {:04X}", wepFormID);
	}
}

void Configs::SetAdjustmentForEquipped()
{
	if (!Globals::p || !Globals::p->Get3D())
		return;

	adjustment = &adjustDataMap.at(0xFFFFFFFF).at(0xFFFFFFFF);
	if (!EditorUI::Window::GetSingleton()->GetShouldDraw()) {
		Hooks::shouldAdjust = false;
	}
	Hooks::cachedProjectileNodeLocal.x = FLT_MAX;

	if (!Globals::p->inventoryList) {
		logger::warn("Inventory list is null");
		return;
	}

	for (auto& invItem : Globals::p->inventoryList->data) {
		if (invItem.object && invItem.object->formType == RE::ENUM_FORM_ID::kWEAP && invItem.stackData && invItem.stackData->IsEquipped()) {
			auto wep = static_cast<RE::TESObjectWEAP*>(invItem.object);
			auto wepIt = adjustDataMap.find(wep->formID);

			if (wepIt != adjustDataMap.end()) {
				auto instanceData = static_cast<RE::TESObjectWEAP::InstanceData*>(invItem.GetInstanceData(0));
				bool keywordFound = false;
				if (instanceData && instanceData->keywords) {
					for (uint32_t keywordIdx = 0; keywordIdx < instanceData->keywords->numKeywords; ++keywordIdx) {
						uint32_t keywordFormID = instanceData->keywords->keywords[keywordIdx]->formID;
						auto keywordIt = wepIt->second.find(keywordFormID);
						if (keywordIt != wepIt->second.end()) {
							adjustment = &keywordIt->second;
							Hooks::shouldAdjust = true;
							logger::info("Weapon {:04X} Keyword {:04X} x: {:.2f}, y: {:.2f}, z: {:.2f}, rotX: {:.2f}, rotY: {:.2f}, rotZ: {:.2f}, Persistent: {}",
								wep->formID, keywordFormID, adjustment->translation.x, adjustment->translation.y, adjustment->translation.z, adjustment->rotation.x / Configs::toRad, adjustment->rotation.y / Configs::toRad, adjustment->rotation.z / Configs::toRad, adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent));
							keywordFound = true;
							break;
						}
					}
				}
				if (keywordFound) {
					break;
				}

				auto extra = invItem.stackData->extra;
				if (extra) {
					auto extraData = static_cast<RE::BGSObjectInstanceExtra*>(extra->GetByType(RE::EXTRA_DATA_TYPE::kObjectInstance));

					if (extraData && extraData->values && extraData->values->buffer) {
						uintptr_t buf = reinterpret_cast<uintptr_t>(extraData->values->buffer);
						bool omodFound = false;

						for (uint32_t i = 0; i < extraData->values->size / 0x8; ++i) {
							uint32_t omodFormID = *reinterpret_cast<uint32_t*>(buf + i * 0x8);
							auto omodIt = wepIt->second.find(omodFormID);

							if (omodIt != wepIt->second.end()) {
								adjustment = &omodIt->second;
								Hooks::shouldAdjust = true;
								logger::info("Weapon {:04X} OMOD {:04X} x: {:.2f}, y: {:.2f}, z: {:.2f}, rotX: {:.2f}, rotY: {:.2f}, rotZ: {:.2f}, Persistent: {}", 
									wep->formID, omodFormID, adjustment->translation.x, adjustment->translation.y, adjustment->translation.z, adjustment->rotation.x / Configs::toRad, adjustment->rotation.y / Configs::toRad, adjustment->rotation.z / Configs::toRad, adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent));
								omodFound = true;
								break;
							}
						}

						if (!omodFound) {
							logger::info("No matching OMOD found for weapon {:04X}", wep->formID);
						} else {
							break;
						}
					}
				}
			}
		}
	}
}

void Configs::AdjustmentData::CycleAlternatives()
{
	if (alternatives.empty() || isInTransition)
		return;
	transitionTimer = 0.f;
	alternatives.push(AlternativeAdjustment(translation, rotation));
	isInTransition = true;
}

void Configs::AdjustmentData::StepTransition(float deltaTime)
{
	if (alternatives.empty() || !isInTransition)
		return;
	AlternativeAdjustment prev = alternatives.back();
	AlternativeAdjustment next = alternatives.front();
	transitionTimer = min(altTransitionDelay, transitionTimer + deltaTime);
	float transitionPercentage = Utils::easeInOutQuad(transitionTimer / altTransitionDelay);
	translation = prev.translation + (next.translation - prev.translation) * transitionPercentage;
	rotation = prev.rotation + (next.rotation - prev.rotation) * transitionPercentage;

	if (transitionTimer >= altTransitionDelay) {
		translation = next.translation;
		rotation = next.rotation;
		alternatives.pop();
		isInTransition = false;
	}
}
