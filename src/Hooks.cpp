#include "Hooks.h"
#include "Globals.h"
#include "EquipWatcher.h"
#include "MenuWatcher.h"
#include "Configs.h"
#include "EditorUI.h"
#include "include/detours.h"
#include "Utils.h"

REL::Relocation<uintptr_t> DrawWorld_Set1stPersonCameraLocationOrig{ REL::ID(598404) };
REL::Relocation<uintptr_t> DrawWorld_Move1stPersonToOriginOrig{ REL::ID(76526) };
REL::Relocation<uintptr_t> MuzzleFlash_Position3D{ REL::ID(1311341) };
REL::Relocation<RE::NiCamera**> ptr_1stPersonCamera{ REL::ID(380177) };
const static float transitionDelay = 0.5f;

static float transitionTimer;
static RE::NiPoint3 adjustRot;
static RE::NiMatrix3 adjustRotMat;

bool Hooks::shouldAdjust = false;
RE::NiPoint3 Hooks::cachedProjectileNodeLocal;

float easePercentage = 1.f, currentTime = 0.f, duration = 0.f;
std::string clipName;
std::string animationInfo;
RE::NiPoint3 adjustDiff;

void Hooks::InitializeHooks()
{
	animationInfo.reserve(1024);
	Globals::ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(MenuWatcher::GetSingleton());
	RE::EquipEventSource::GetSingleton()->RegisterSink(EquipWatcher::GetSingleton());

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)DrawWorld_Set1stPersonCameraLocationOrig, HookedSet1stPersonCameraLocation);
	DetourAttach(&(PVOID&)DrawWorld_Move1stPersonToOriginOrig, HookedMove1stPersonToOrigin);
	DetourAttach(&(PVOID&)MuzzleFlash_Position3D, HookedPosition3D);
	DetourTransactionCommit();
}

namespace RE
{
	struct hkbBehaviorGraph
	{
		struct NodeData
		{
			hkbClipGenerator* clipGenerator;
			hkbClipGenerator* clipGenerator2;
			hkbBehaviorGraph* behaviorGraph;
		};

		std::uint64_t unk00[0xE0 >> 3];
		hkArray<NodeData*>* activeNodes;
		std::uint64_t unkE8[(0x1A8 - 0xE8) >> 3];
		std::uint8_t unk1A8;
		std::uint8_t unk1A9;
		bool isActive;
		bool isLinked;
		bool updateActiveNodes;
		bool stateOrTransitionChanged;
	};

	class hkbClipGenerator
	{
	public:
		std::uint64_t unk00[0x38 >> 3];
		const char* animName;
		std::uint32_t id;
		std::uint8_t cloneState;
		std::uint8_t pad43;
		std::uint32_t pad44;
		std::uint64_t unk40[(0x90 - 0x48) >> 3];
		const char* animPath;
	};
}

void GetClipInfo(float& _currentTime, float& _duration, std::string& _clipName) {
	if (Globals::p->currentProcess) {
		RE::BSAnimationGraphManager* graphManager = Globals::p->currentProcess->middleHigh->animationGraphManager.get();
		if (graphManager) {
			RE::BShkbAnimationGraph* graph = graphManager->variableCache.graphToCacheFor.get();
			if (graph) {
				RE::hkbBehaviorGraph* hkGraph = *(RE::hkbBehaviorGraph**)((uintptr_t)graph + 0x378);
				if (hkGraph->activeNodes && hkGraph->activeNodes->_size > 0 && !hkGraph->updateActiveNodes && !hkGraph->stateOrTransitionChanged) {
					for (int i = 0; i < hkGraph->activeNodes->_size; ++i) {
						if (hkGraph->activeNodes->_data[i]->clipGenerator2 && (*(uintptr_t*)hkGraph->activeNodes->_data[i]->clipGenerator2) == RE::VTABLE::hkbClipGenerator[0].address()) {
							uintptr_t clip = (uintptr_t)(hkGraph->activeNodes->_data[i]->clipGenerator2);
							uintptr_t animCtrl = *(uintptr_t*)(clip + 0xD0);
							if (animCtrl) {
								uintptr_t animBinding = *(uintptr_t*)(animCtrl + 0x38);
								uintptr_t anim = *(uintptr_t*)(animBinding + 0x18);
								_currentTime = *(float*)(clip + 0x140);
								_duration = *(float*)(anim + 0x14);
								_clipName = std::string(*(const char**)(clip + 0x38));
								return;
							}
						}
					}
				}
			}
		}
	}
}

void GetAllClipInfo(std::string& clipInfo)
{
	if (Globals::p->currentProcess) {
		RE::BSAnimationGraphManager* graphManager = Globals::p->currentProcess->middleHigh->animationGraphManager.get();
		if (graphManager) {
			RE::BShkbAnimationGraph* graph = graphManager->variableCache.graphToCacheFor.get();
			if (graph) {
				RE::hkbBehaviorGraph* hkGraph = *(RE::hkbBehaviorGraph**)((uintptr_t)graph + 0x378);
				if (hkGraph->activeNodes && hkGraph->activeNodes->_size > 0 && !hkGraph->updateActiveNodes && !hkGraph->stateOrTransitionChanged) {
					for (int i = 0; i < hkGraph->activeNodes->_size; ++i) {
						if (hkGraph->activeNodes->_data[i]->clipGenerator && (*(uintptr_t*)hkGraph->activeNodes->_data[i]->clipGenerator) == RE::VTABLE::hkbClipGenerator[0].address()) {
							uintptr_t clip = (uintptr_t)(hkGraph->activeNodes->_data[i]->clipGenerator);
							clipInfo.append("Clip: ");
							clipInfo.append(*(const char**)(clip + 0x38));
							clipInfo.append("\n");
							clipInfo.append(*(const char**)(clip + 0x90));
							clipInfo.append("\n");
						}
					}
				}
			}
		}
	}
}

void Hooks::HookedSet1stPersonCameraLocation(RE::NiPoint3& a_loc)
{
	if (shouldAdjust && Configs::adjustment) {
		const float deltaTime = *Utils::ptr_deltaTime;
		Configs::adjustment->StepTransition(deltaTime);

		RE::NiNode* fireNode = nullptr;
		if (Globals::p->currentProcess) {
			Globals::p->currentProcess->middleHigh->equippedItemsLock.lock();
			RE::BSTArray<RE::EquippedItem>& equipped = Globals::p->currentProcess->middleHigh->equippedItems;
			if (equipped.size() != 0 && equipped[0].data.get()) {
				RE::EquippedWeaponData* weapData = static_cast<RE::EquippedWeaponData*>(equipped[0].data.get());
				if (weapData->fireNode) {
					fireNode = static_cast<RE::NiNode*>(weapData->fireNode);
				}
			}
			Globals::p->currentProcess->middleHigh->equippedItemsLock.unlock();
		}

		if (cachedProjectileNodeLocal.x == FLT_MAX && fireNode) {
			RE::NiAVObject* projCacheNode = fireNode->GetObjectByName(projCacheNodeName);
			if (projCacheNode) {
				cachedProjectileNodeLocal = projCacheNode->local.translate;
			} else {
				cachedProjectileNodeLocal = fireNode->local.translate;
				projCacheNode = Utils::CreateBone(projCacheNodeName);
				if (projCacheNode) {
					fireNode->AttachChild(projCacheNode, true);
					projCacheNode->local.translate = cachedProjectileNodeLocal;
				}
			}
		}

		RE::NiMatrix3 playerDirMat = Utils::GetRotationMatrix33(0, -Globals::p->data.angle.x, -Globals::p->data.angle.z);
		RE::NiPoint3 adjustWorld = playerDirMat * Configs::adjustment->translation;
		adjustRot = Configs::adjustment->rotation;
		float zoomPercentage = *(float*)((uintptr_t)Globals::pcam->cameraStates[RE::CameraState::kFirstPerson].get() + 0x78);

		currentTime = 0.f, duration = 0.f;
		GetClipInfo(currentTime, duration, clipName);
		if (EditorUI::Window::GetSingleton()->GetShouldDraw()) {
			animationInfo.clear();
			GetAllClipInfo(animationInfo);
		}

		// 수정된 부분: Persistent 모드 체크
		// Persistent가 true면 모든 revert 조건을 무시하고 항상 전체 적용 상태 유지
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent)) {
			// Persistent 모드: transitionTimer를 강제로 0으로 유지하여 easePercentage = 1.0 유지
			// 다만 줌인(zoom)은 여전히 적용되어야 하므로 zoomPercentage는 체크
			if (zoomPercentage > 0.f) {
				transitionTimer = min(transitionDelay, transitionTimer + deltaTime);
			} else {
				transitionTimer = max(0.f, transitionTimer - deltaTime);
			}
		} else {
			// 기존 리버트 로직 (원본 그대로)
			if (MenuWatcher::GetSingleton()->isInPipboyMenu || 
				(Globals::p->currentProcess && Globals::p->currentProcess->middleHigh->weaponCullCounter > 0) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnReload) && Globals::p->gunState == RE::GUN_STATE::kReloading && (duration - currentTime) >= duration / 3.f) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnMelee) && (Globals::p->meleeAttackState & 0x2) && (duration - currentTime) >= duration / 2.5f) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnThrow) && Globals::p->gunState == RE::GUN_STATE::kFire && (clipName == "WPNGrenadeThrow" || clipName == "WPNMineThrow") && (duration - currentTime) >= duration / 3.f) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnSprint) && (Globals::p->moveMode & 0x100) == 0x100) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnEquip) && clipName == "WPNEquip" && (duration - currentTime) >= duration / 3.f) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnFastEquip) && clipName == "WPNEquipFast" && (duration - currentTime) >= duration / 2.f) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnUnequip) && Globals::p->weaponState == RE::WEAPON_STATE::kSheathing) ||
				(Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnGunDown) && Globals::p->gunState == RE::GUN_STATE::kBlocked)) {
				transitionTimer = min(transitionDelay, transitionTimer + deltaTime);
			} else {
				transitionTimer = max(0.f, transitionTimer - deltaTime);
			}
		}

		float adjustPercentage = 0.f;
		if (zoomPercentage > 0.f || transitionTimer > 0.f) {
			adjustPercentage = min(1.f - zoomPercentage, Utils::easeInOutQuad(1.f - transitionTimer / transitionDelay));
		} else {
			adjustPercentage = 1.f;
		}
		easePercentage = 1.f;
		if (adjustPercentage < 1.f) {
			easePercentage = adjustPercentage;
			adjustWorld = adjustWorld * easePercentage;
			adjustRot = adjustRot * easePercentage;
		}

		adjustRotMat = Utils::GetRotationMatrix33(adjustRot.z, adjustRot.y, adjustRot.x);

		if (fireNode) {
			adjustDiff = Utils::GetRotationMatrix33(adjustRot.y, adjustRot.x, adjustRot.z) * (cachedProjectileNodeLocal + Configs::adjustment->translation * easePercentage) - cachedProjectileNodeLocal;
			fireNode->local.translate = cachedProjectileNodeLocal + adjustDiff;
		}

		a_loc = a_loc - adjustWorld;
	} else {
		transitionTimer = 0.f;
		adjustDiff = RE::NiPoint3();
	}
	typedef void (*FnSet1stPersonCameraLocation)(RE::NiPoint3&);
	FnSet1stPersonCameraLocation fn = (FnSet1stPersonCameraLocation)DrawWorld_Set1stPersonCameraLocationOrig.address();
	if (fn)
		(*fn)(a_loc);
}

void Hooks::HookedMove1stPersonToOrigin()
{
	typedef void (*FnMove1stPersonToOrigin)();
	FnMove1stPersonToOrigin fn = (FnMove1stPersonToOrigin)DrawWorld_Move1stPersonToOriginOrig.address();
	if (fn)
		(*fn)();

	if (shouldAdjust) {
		if (adjustRot.Length() > 0.f) {
			(*ptr_1stPersonCamera)->world.rotate = adjustRotMat * (*ptr_1stPersonCamera)->world.rotate;
		}
	}
}

void Hooks::HookedPosition3D(RE::MuzzleFlash* a_muz, RE::TESObjectREFR* a_ref, bool a_updateNode)
{
	typedef void (*FnPosition3D)(RE::MuzzleFlash*, RE::TESObjectREFR*, bool);
	FnPosition3D fn = (FnPosition3D)MuzzleFlash_Position3D.address();
	if (fn) {
		(*fn)(a_muz, a_ref, a_updateNode);
		if (shouldAdjust) {
			RE::NiNode* muzNode = *(RE::NiNode**)((uintptr_t)a_muz + 0x10);
			if (muzNode) {
				muzNode->local.translate = muzNode->local.translate - adjustDiff;
			}
		}
	}
}

