#pragma once
#include <bitset>
#define MATH_PI 3.14159265358979323846

namespace Configs
{
	const static float toRad = (float)(MATH_PI / 180.f);
	const static float altTransitionDelay = 0.5f;

	enum class REVERT_FLAG : uint16_t
	{
		kNoRevert = 0,
		kRevertOnReload = 1,
		kRevertOnMelee = 2,
		kRevertOnThrow = 3,
		kRevertOnSprint = 4,
		kRevertOnEquip = 5,
		kRevertOnFastEquip = 6,
		kRevertOnUnequip = 7,
		kRevertOnGunDown = 8,
		kPersistent = 9,        // 추가: 영구 고정 모드 (Bodycam 스타일)
		kNumFlags = 10          // 9에서 10으로 변경
	};

	struct AlternativeAdjustment
	{
		RE::NiPoint3 translation;
		RE::NiPoint3 rotation;
		AlternativeAdjustment(const RE::NiPoint3& _trans, const RE::NiPoint3& _rot) :
			translation(_trans), rotation(_rot) {}
	};

	struct AdjustmentData
	{
		RE::NiPoint3 translation;
		RE::NiPoint3 rotation;
		std::bitset<(size_t)REVERT_FLAG::kNumFlags> revertFlag;
		std::queue<AlternativeAdjustment> alternatives;
		float transitionTimer = 0.f;
		bool isInTransition = false;
		AdjustmentData()
		{
			translation = RE::NiPoint3();
			rotation = RE::NiPoint3();
			revertFlag.reset();
			while (!alternatives.empty()) {
				alternatives.pop();
			}
		}

		AdjustmentData(float x, float y, float z, float rotX, float rotY, float rotZ, const std::bitset<(size_t)REVERT_FLAG::kNumFlags>& _revertFlag, const std::queue<AlternativeAdjustment>& _alternatives) :
			translation(x, y, z), rotation(rotX * toRad, rotY * toRad, rotZ * toRad), revertFlag(_revertFlag), alternatives(_alternatives) {}

		void SetAdjustmentFlag(REVERT_FLAG flagIndex, bool set)
		{
			revertFlag.set((size_t)flagIndex, set);
		}

		bool GetAdjustmentFlag(REVERT_FLAG flagIndex) const
		{
			return revertFlag.test((size_t)flagIndex);
		}

		void CycleAlternatives();

		void StepTransition(float deltaTime);
	};
	typedef std::unordered_map<uint32_t, std::unordered_map<uint32_t, AdjustmentData>> AdjustmentDataMap;
	extern AdjustmentDataMap adjustDataMap;
	extern AdjustmentData* adjustment;

	void LoadConfigs();
	void RegisterAdjustmentData(auto, uint32_t, uint32_t, bool isKeyword = false);
	void SetAdjustmentForEquipped();
}
