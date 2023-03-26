// Classic Sonic Improvement Mod CODE
// Modification of Chimera's work on his 3D spindash mod. (with permission of course)
//
// CREDITS:
// Showin		- Stuff.
// Skyth - Archive Patcher, Gens DLL Stuff
// Brianuuu - Param Manager, Common Functions, Archive Patcher
// Hyper - Some hooks

// 3D SPINDASH CREDITS:
// Chimera      - Mod development.
// Sajid        - Co-developer, overall help.
// Skyth        - DLLMods tools and reference material, additional assistance with Gens API and ASM

#include <iostream>
#include <xmmintrin.h>
#include <gmath/src/Vector3.hpp>
#include "Types.h"
#include "Configuration.h"
#include "ParamManager.h"

#pragma region Global Variables
// HACK: Rotation for the spindash charge is injected in assembly.
// To do this cleanly, rotation is saved as an __m128
// so it can be fed directly into the xmm0 register.
__m128 keyRotation = { 0, 0, 0, 0 };
bool rotationOverride = false;

// HACK: Using global variables for detecting input.
// Skyth doesn't use these so I'm wary--APPARENTLY this works with both controller and keyboard, but WHO KNOWS
auto* controllerButtons = reinterpret_cast<BYTE*>(0x01E77B54);
BYTE crouchButton = 0x02;
BYTE jumpButton = 0x01;

// TODO: Make the rotation rate configurable.
const float spinChargeRotationRate = 10.0;

bool wasGrounded = false;

#pragma endregion 

// These are just helpful to have.
#pragma region Helpers
#define EXPORT extern "C" void __declspec(dllexport)
constexpr float RAD2DEG = static_cast<float>(57.2958);
constexpr float DEG2RAD = static_cast<float>(0.0174533);

CSonicStateFlags* GetStateFlagsFromContext(CSonicContext* sonic_context)
{
	auto* const context = reinterpret_cast<int*>(sonic_context);
	return reinterpret_cast<CSonicStateFlags*>(*reinterpret_cast<int*>(context[0x14D] + 4));
}

// Lets us cleanly take a quat and turn it into an __m128,
// which can then easily be passed into the xmm0 register via assembly.
void SetRotation(Quaternion q)
{
	keyRotation = { q.X, q.Y, q.Z, q.W };
}

// We might need more flags to reset in the future,
// but for such a simple mod we only need one. For now.
void ResetFlags()
{
	rotationOverride = false;
}
#pragma endregion 

// These are for small, but necessary, overrides, for functions like rotation and input handling.
#pragma region Assembly Hooks
// Credit to Sajid for finding this out in his double jump mod.
void* changeStateOriginal = (void*)0xE4FF30;
void __cdecl ChangeState(GensString* state, int* context)
{
	__asm
		{
		mov eax, state
		mov ecx, context
		call[changeStateOriginal]
		}
}

// This is so we can apply sonic's rotation while charging spin.
uint32_t TP_Return = 0xE35389;
void __declspec(naked) TransformPlayer_Hook()
{
	__asm
		{
		mov ebx, [ebx + 0x10]
		lea esi, [ebx + 0x60]

		cmp rotationOverride, 0
		jz jump

		push eax
		lea eax, keyRotation
		movaps xmm0, xmmword ptr [eax]
		pop eax

		jump:
		movaps xmmword ptr[esi], xmm0
		jmp[TP_Return]
		}
}

// Here we override an odd quirk about Sonic Generations.
// The game has a "world input" direction vector, based on both your controller input and the camera/path you're on.
// However, for SOME REASON, there are actions that DISABLE this, forcing this input vector to be zero.
// Rather than dig around for what's causing this in a clean way, I decided to disable this check when spindash charging,
// because realistically (at time of writing), this is the only action affected by this that matters.
// Plus, we only want to override this in 3D; it makes sense to not change your orientation in 2D.
// (Granted, 2D spin charge is an entirely different class, but that aside...)
// TODO: Would be a good idea to find out how to remove SpindashCharge from this comparison in memory anyway, rather than this janky method.

// The following has to be done twice, because 3D gameplay is *technically speaking* two different types of play
// - Free roam     / "Standard"
// - Path-relative / "Forward View"

// 3D view standard
uint32_t WICO3D_ReturnAddr = 0x00E303DD;
uint32_t WICO3D_JumpAddr = 0x00E303E6;

void __declspec(naked) WorldInputComparisonOverride_3D()
{
	__asm
	{
		// custom comparison
		// compare to ONE for some reason??
		cmp rotationOverride, 1
		jz jump

		// original comparison
		cmp byte ptr[ecx + 0x98], 0
		jz jump

		// Zero out otherwise
		jmp[WICO3D_ReturnAddr]

		jump:
		jmp[WICO3D_JumpAddr]
	}
}

// Forward View
uint32_t WICOFV_ReturnAddr = 0x00E2E820;
uint32_t WICOFV_JumpAddr = 0x00E2E829;

void __declspec(naked) WorldInputComparisonOverride_Forward()
{
	__asm
	{
		// custom comparison
		// compare to ONE for some reason??
		cmp rotationOverride, 1
		jz jump

		// original comparison
		cmp byte ptr[ecx + 0x98], 0
		jz jump

		// Zero out otherwise
		jmp[WICOFV_ReturnAddr]

		jump:
		jmp[WICOFV_JumpAddr]
	}
}

#pragma endregion 

// Here's where the magic happens, it's very complicated are you ready
#pragma region 3D Roll

// This is used to alter memory in an easy manner. - Showin
#include <vector>
uintptr_t FindDMAAddy(uintptr_t ptr, std::vector<unsigned int> offsets)
{
	uintptr_t addr = ptr;
	for (unsigned int i = 0; i < offsets.size(); ++i)
	{
		addr = *(uintptr_t*)addr;
		addr += offsets[i];
	}
	return addr;
}

// This gets used in a few places to alter spindash speeds when needed. - Showin
bool spinny_spindash = 0;
bool spinny_spindash_reset = 0;

// Lmfao this just works
FUNCTION_PTR(int, __fastcall, Classic3DSlideMovement, 0x011D6140, int a1);
HOOK(void, __fastcall, Spin3DMovement, 0x01115DB0, _DWORD* a1)
{
	Classic3DSlideMovement(reinterpret_cast<int>(a1));
}

#pragma endregion 

// All this here controls 
#pragma region Spindash Charge Rotation
// SHOWIN COMMENTED THIS OUT UNTIL HE CAN FIX THE COMPILE ERRORS!
// This gets used in multiple places because Gens is silly I guess.
void RotatePlayerWhileSpinning(int a1)
{
	rotationOverride = true;

	const int playerContextOffset = *reinterpret_cast<_DWORD*>(a1 + 8);

	// We want to do a custom rotation routine here because Generations doesn't actually let you rotate while spinning by default.
	// There's some bugs with this method, probably involving how we get his up vector, so investigate why.
	// TODO: Consider getting the ground up normal via Generations' MsgGetGroundInfo
	// ... when you learn how to do that lol
	const auto rotation = static_cast<Quaternion>(keyRotation.m128_f32);
	const auto inputVec = *reinterpret_cast<Vector3*>(playerContextOffset + 0x130);

	Quaternion targetRotation = rotation;
	if (Vector3::SqrMagnitude(inputVec) >= 0.05f * 0.05f)
	{
		targetRotation = Quaternion::LookRotation(inputVec, Quaternion::Up(rotation));
		targetRotation = Quaternion::RotateTowards(rotation, targetRotation, spinChargeRotationRate * DEG2RAD);
	}

	SetRotation(targetRotation);
}

HOOK(int*, __fastcall, SonicSpinChargeMovement, 0x1250AA0, int This, void* edx)
{
	auto* const result = originalSonicSpinChargeMovement(This, edx);

	RotatePlayerWhileSpinning(This);

	return result;
}

HOOK(int*, __fastcall, SonicSpinChargeSlideMovement, 0x11D3BE0, int This, void* edx)
{
	auto* const result = originalSonicSpinChargeSlideMovement(This, edx);

	RotatePlayerWhileSpinning(This);

	return result;
}

// Was intended to let you change your orientation when doing B+A spindash.
// UNDONE: This is so bugged and I don't even know where to BEGIN fixing it LOL YOU TRY IT
//HOOK(int*, __fastcall, SonicSpinChargeSquatMovement, 0x1250600, int This, void* edx)
//{
//	auto* const result = originalSonicSpinChargeSquatMovement(This, edx);
//
//	RotatePlayerWhileSpinning(This);
//
//	// HACK: do this cuz apparently squat charge lets you move LOL?
//	// BUG: IT DOESN'T WORK AHAHAHA
//	const auto playerContextOffset = *reinterpret_cast<_DWORD*>((int)This + 8);
//	auto* const sonicContext = reinterpret_cast<CSonicContext*>(playerContextOffset);
//
//	sonicContext->Sonic->Velocity = Vector3::Zero();
//
//	return result;
//}

#pragma endregion 

//inline bool CheckCurrentStage(char const* stageID)
//{
//	char const* currentStageID = (char*)0x01E774D4;
//	return strcmp(currentStageID, stageID) == 0;
//}

enum ImpulseType : uint32_t
{
	None,
	DashPanel,
	UnknowCase_0x2,
	UnknowCase_0x3,
	UnknowCase_0x4,
	UnknowCase_0x5,
	JumpBoard,
	JumpBoardSpecial,
	DashRing,
	DashRingR,
	LookBack,
	HomingAttackAfter,
	BoardJumpBoard,
	UnknowCase_0xD,
	BoardJumpAdlibTrickA,
	BoardJumpAdlibTrickB,
	BoardJumpAdlibTrickC
};

struct MsgApplyImpulse
{
	INSERT_PADDING(0x10);
	Eigen::Vector3f m_position;
	INSERT_PADDING(0x4);
	Eigen::Vector3f m_impulse;
	INSERT_PADDING(0x4);
	float m_outOfControl;
	INSERT_PADDING(0x4);
	ImpulseType m_impulseType;
	float m_keepVelocityTime;
	bool m_notRelative; // if false, add impulse direction relative to Sonic
	bool m_snapPosition; // snap Sonic to m_position
	INSERT_PADDING(0x3);
	bool m_pathInterpolate; // linked to 80
	INSERT_PADDING(0xA);
	Eigen::Vector3f m_unknown80; // related to position interpolate?
	INSERT_PADDING(0x4);
	float m_alwaysMinusOne; // seems to be always -1.0f
	INSERT_PADDING(0xC);
};

// Stuff
bool dropdashing = 0;
bool hasjumpstarted = 0;
bool dropdash_effects = 0;
bool spinmomentum = 0;
bool supersonic = 0;
bool classicspindash = 0;
bool fixslidingspindash = 0;
bool rotation_reset = 1;
bool landslope_reset = 1;
bool OG_spindash_changed = 0;
bool OG_spindash_dropdash = 0;
bool metalsonic_dropdash = 0;
bool outrun_camera_enabled = 0;
bool actually_spindashed = 0;
bool actually_spindashed_sfx_fix = 0;

int dropdashtimer = 0;
int dropdashchargetimer = 0;
int dropdashcompleted = 0;
int outruntimer = 0;
int spindashtimer = 0;
int spindashtimer_soundfix = 0;
int metalsonic_dropdash_timer = 0;
int mouthcheck = 0;
int jumpcount = 0;
int spindashchargevalue = 0;
uint32_t current_stage = 0x00000000;
uint8_t current_state = 0;
uint32_t in_stage_check = 0;
uint32_t in_stage_check_test = 0;
uint32_t hangingcheckvalue = 0;
uint32_t monitorcheckvalue = 0;
uint32_t grindcheckvalue = 0;
uint32_t spinnycheckvalue = 0;
uint64_t spindashsoundfixvalue = 0;
int in_stage = 0;

//int jumpballtimer = 0;
//int jumpballtimer2 = 0;
//bool jumpball_enable = 0;
int can_x_dropdash = 0;
bool classic_sonic = 0;
int grindtimer = 0;
bool grinddropdash = 0;
bool sonic_goal = 0;
bool ring_check = 0;
int ssz_hang = 0;
int sonic_direction = 0;
int spindash_charge_level = 0;
int insta_shield = 0;
//bool HighSpeedEffectMaxVelocity_params_added = 0;

// Peel Out Stuff
bool peelout_start = 0;
bool peelout_ani_played = 0;
int peelout_timer = 0;

//int jumptimer = 0;
//bool jump_rebound = 0;

ParamValue* LandEnableMaxSlope = nullptr;
//ParamValue* HighSpeedEffectMaxVelocity = nullptr;
ParamValue* TargetFrontOffsetSpeedScale = nullptr;
//ParamValue* GrindAccelerationForce = nullptr;
//ParamValue* AccelerationForce = nullptr;

bool isGrounded = false;

bool DropDash_Disabled_2 = false;

// Lastly, this logs our rotation in standard play, AND lets us roll by pressing B! 

// RND shows that this is the general routine for moving Sonic around given a Vector3 for his velocity.
// It might do some other things I'm not sure about, hence the "maybe,"
// but for now this is a pretty good place to handle B-button rolling and setting his game-state rotation.
HOOK(void, __stdcall, SonicMovementMaybe, 0x00E32180, int a1, Vector3* a2)
{
	originalSonicMovementMaybe(a1, a2);

	// This method affects both Sonics so we wanna make sure we're not Modern sonic.
	// HACK: This is checking if Classic *exists* first, but what if there's a situation in the future where both are on screen via mods?
	// Looking at how this game's structured, that doesn't seem to be possible, but it's still worth considering. Probably not my problem tho.
	// TODO: Future-proof this by finding a way to check if a1 == Classic Sonic context, INSTEAD of comparing CSonicClassicContext to nullptr.

	// Check the current stage. This gets used in a few places.
	current_stage = *(uint32_t*)(0x1E774D4);

	auto input = Sonic::CInputState::GetInstance()->GetPadState();
	auto inputState = Sonic::CInputState::GetInstance();
	auto inputPtr = &inputState->m_PadStates[inputState->m_CurrentPadStateIndex];

	// Reset params if classic switches to modern
	if (classic_sonic == 1 && input.IsDown(Sonic::eKeyState_Y) && current_stage == 0x306D6170)
	{
		classic_sonic = 0;
		if (ring_check == 1)
		{
			WRITE_MEMORY(0xE6628E, uint8_t, 0x7C);
			WRITE_MEMORY(0xE6CCDE, uint8_t, 0x7C);
			ring_check = 0;
		}
		ParamManager::restoreParam(); // Fixes potential crashes with modern sonic.
		return;
	}

	const auto classicContextPtr = *reinterpret_cast<int*>(0x01E5E304);
	if (classicContextPtr == 0 || current_stage == 0x317A6E63 || current_stage == 0x00626C62)
	{
		//if (current_stage == 0x317A6E63)
		//{
		//	// If we're in casino night's minigame then don't run any new code.
		//	printf("Casino Night!");
		//}
		//if (current_stage == 0x00626C62)
		//{
		//	// If we're in the final boss then don't run any new code.
		//	printf("Final Boss!");
		//}
		classic_sonic = 0;
		if (ring_check == 1)
		{
			WRITE_MEMORY(0xE6628E, uint8_t, 0x7C);
			WRITE_MEMORY(0xE6CCDE, uint8_t, 0x7C);
			ring_check = 0;
		}
		ParamManager::restoreParam(); // Fixes potential crashes with modern sonic.
		return;
	}
	else
	{
		classic_sonic = 1;

		if (ring_check == 0)
		{
			// Drop all rings when getting damaged
			WRITE_MEMORY(0xE6628E, uint8_t, 0xEB);
			WRITE_MEMORY(0xE6CCDE, uint8_t, 0xEB);
			ring_check = 1;
		}
	}

	const auto playerContextOffset = *reinterpret_cast<_DWORD*>((int)a1 + 8);
	auto* const sonicContext = reinterpret_cast<CSonicContext*>(playerContextOffset);

	//const auto isGrounded = *reinterpret_cast<bool*>(playerContextOffset + 0x360);
	isGrounded = *reinterpret_cast<bool*>(playerContextOffset + 0x360);

	auto spinString = GensString("Spin");
	auto squatString = GensString("Squat");
	auto NormalDamageDeadString = GensString("NormalDamageDead");

	auto dropdashString = GensString("SquatCharge"); // This one doesn't cause issues when holding x button while dropdashing.
	auto dropdashSlidingString = GensString("SpinChargeSliding");
	auto dropdashSlideFixString1 = GensString("StartCrouching");
	auto dropdashSlideFixString2 = GensString("Walk");
	//auto dropdashString = GensString("SpinCharge");
	//auto dropdashchargeString = GensString("SpinChargeSliding");
	//auto dropdashchargeString = GensString("SpecialJump");
	//auto jumpString = GensString("Jump");
	auto slideString = GensString("Sliding");

	auto* const stateFlags = GetStateFlagsFromContext(sonicContext);

	// Check Animation Names (Used sometimes instead of checking states)
//const auto& animName = Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName();
	auto CurrentAnimation = Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName();

	bool DropDash_Disabled =
		CurrentAnimation == "HangPole" ||
		CurrentAnimation == "HangPoleLoop" ||
		CurrentAnimation == "HangPoleF" ||
		CurrentAnimation == "HangPoleB" ||
		CurrentAnimation == "HangPoleDamage" ||
		CurrentAnimation == "CatchClockBar" ||
		CurrentAnimation == "CatchClockBarLoop" ||
		CurrentAnimation == "UpReelStart" ||
		CurrentAnimation == "UpReelLoop" ||
		CurrentAnimation == "UpReelEnd" ||
		CurrentAnimation == "PulleyStart" ||
		CurrentAnimation == "PulleyLoop" ||
		CurrentAnimation == "PulleyJump" ||
		CurrentAnimation == "CatchHeri" ||
		CurrentAnimation == "CatchHeriLoop" ||
		CurrentAnimation == "HangOn" ||
		CurrentAnimation == "PoleSpinStart" ||
		CurrentAnimation == "PoleSpinLoop" ||
		CurrentAnimation == "PoleSpinJumpStart" ||
		CurrentAnimation == "PoleSpinJumpLoop" ||
		CurrentAnimation == "JumpCloudStart" ||
		CurrentAnimation == "JumpCloud" ||
		CurrentAnimation == "JumpCloudEnd" ||
		CurrentAnimation == "TransformSpike" ||
		CurrentAnimation == "WaterFlow" ||
		CurrentAnimation == "WaterFlowBar" ||
		CurrentAnimation == "WaterFlowBarR" ||
		CurrentAnimation == "WaterFlowBarL" ||
		CurrentAnimation == "TarzanF_Loop" ||
		CurrentAnimation == "TarzanF_B" ||
		CurrentAnimation == "TarzanB_Loop" ||
		CurrentAnimation == "TarzanB_F" ||
		CurrentAnimation == "JumpSpring" ||
		CurrentAnimation == "JumpSpringHeadLand" ||
		CurrentAnimation == "JumpBoard" ||
		CurrentAnimation == "JumpBoardRev" ||
		CurrentAnimation == "Float";

	bool Jump_Animation =
		CurrentAnimation == "JumpBall" ||
		CurrentAnimation == "SpinAttack" ||
		CurrentAnimation == "JumpShortBegin" ||
		CurrentAnimation == "JumpShort" ||
		CurrentAnimation == "JumpShortTop" ||
		CurrentAnimation == "JumpShortBegin";

	const bool canRoll = !stateFlags->eStateFlag_OutOfControl
		&& !stateFlags->eStateFlag_Squat
		&& !stateFlags->eStateFlag_SpinDash
		&& !stateFlags->eStateFlag_SpinChargeSliding
		&& !stateFlags->eStateFlag_SpikeSpin
		&& !stateFlags->eStateFlag_InvokeSkateBoard
		&& isGrounded
		&& wasGrounded;

	if (canRoll && (*controllerButtons & crouchButton) || CurrentAnimation == "Grind_Idle" && (*controllerButtons & crouchButton) || CurrentAnimation == "Grind_Fast" && (*controllerButtons & crouchButton))
	{
		if (Vector3::SqrMagnitude(*a2) > static_cast<float>(16.0))	// 4*4, seems reasonable. TODO: make configurable
		{
			ChangeState(&spinString, reinterpret_cast<int*>(sonicContext));
		}
		else
		{
			// Here I wanted to have Sonic crouch more consistently; in Gens, classic can only do that from a complete standstill.
			// However, there's some issues with that... Un-comment this code to find out!
			// BUG: This just doesn't work; if you crouch he just snaps to the ground.
			// TODO: Investigate how Gens handles sending sonic to crouch state.
			//ChangeState(&squatString, reinterpret_cast<int*>(sonicContext));
		}
	}

	//current_state = *(uint32_t*)(0x1E774D7); // Check if we're in 3D or 2D!!! This is a lazy way for now. Just checks if you're in a modern stage.
	//current_state = *(uint8_t*)(0x01A572E3); // Check if we're in 3D or 2D!!! // old and crap
	//current_state = *(uint8_t*)(0x1E5E2F0 + 0x172);
	uintptr_t current_state_address = FindDMAAddy(0x1E5E2F0, { 0x172 });
	current_state = *reinterpret_cast<int*>(current_state_address);

	// Fix 3D Spindash Speeds!
	//if (current_state == 0x40 || current_state == 0x3F) // Checks if it's set to 64 (or 63) which is 3D. 65 is 2D.
	if(current_state == 0) // 0 = 3D and 1 = 2D
	{
		if (spinny_spindash == 0)
		{
			// Make spindash speeds high enough to remove rolling on the spinny thing.
			// This makes it control much better!
			uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x428C0000); // 70 Speed!!!

			uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x42AA0000); // 85 Speed!!!

			uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42C80000); // 100 Speed!!!
		}

		// This doesn't exist on modern and cause MANY crashes. Have to be very careful and make sure this only gets added when playing classic sonic.
		// WE CANT DO THIS HERE ANYWAYS! IT HAS TO BE WHERE THE OTHER PARAM ADDS ARE OR ELSE IT CRASHES SEEMINGLY AT LEAST WITH QUICKBOOT.
		// JUST SCRAP IT FOR NOW.
		//if (HighSpeedEffectMaxVelocity_params_added == 0 && current_stage != 0x306D6170 && current_stage != 0x317A6E63 && current_stage != 0x00626C62)
		//{
		//	ParamManager::addParam(&HighSpeedEffectMaxVelocity, "HighSpeedEffectMaxVelocity"); // Fix Spinny Effect For 3D
		//	HighSpeedEffectMaxVelocity_params_added = 1;
		//}

		// Can't put it with the stuff above cuz it resets on stage restart and won't work for some reason.
		//if (HighSpeedEffectMaxVelocity_params_added == 1 && Configuration::cs_model == "graphics_marza" && current_stage != 0x306D6170 && current_stage != 0x317A6E63 && current_stage != 0x00626C62)
		//{
		//	*HighSpeedEffectMaxVelocity->m_funcData->m_pValue = 30.0f;
		//	HighSpeedEffectMaxVelocity->m_funcData->update();
		//}

		spinny_spindash = 1;
	}
	else
	{
		spinny_spindash_reset = 1;
	}

	// This is used to make sure Classic Sonic isn't rising due to a monitor.
	// If he is then we will allow him to drop dash.
	uintptr_t monitorcheckadd = FindDMAAddy(0x1B361E4, { 0x20, 0x1C, 0x14C });
	monitorcheckvalue = *(uint32_t*)(monitorcheckadd);

	// Check For X Button. Used for both x dropdash and spindash stuff later on.
	uintptr_t spindashchargecheck = (0x1E77B54);
	spindashchargevalue = *(uint32_t*)(spindashchargecheck);

	if (Configuration::dropdash_enabled && Configuration::dropdash_on_x)
	{
		// This is an incredibly shitty way to do this.
		//if (spindashchargevalue == 0x00100008 || spindashchargevalue == 0x00200008 || spindashchargevalue == 0x00040008 || spindashchargevalue == 0x00000008)
		// Good way to do it now. lol
		if (input.IsDown(Sonic::eKeyState_X) || input.IsDown(Sonic::eKeyState_LeftTrigger) || input.IsDown(Sonic::eKeyState_RightTrigger))
		{
			can_x_dropdash = 1;
		}
		else
		{
			can_x_dropdash = 0;
		}
	}
	else
	{
		can_x_dropdash = 0;
	}

	// groundcheckvalue == 0x00000001

	// Spindash sound fix
	//uintptr_t spindashsoundfix = (0x11BB493);
	//spindashsoundfixvalue = *(uint64_t*)(spindashsoundfix);
		
	// DROP DASH (SHOWIN ADDED) 
	// First check to see if we have any elemental shields or if we're in any other situations we shouldn't be able to drop dash in.
	if(stateFlags->eStateFlag_OutOfControl || stateFlags->eStateFlag_AirOutOfControl || can_x_dropdash == 0 && stateFlags->eStateFlag_InvokeFlameBarrier || can_x_dropdash == 0 && stateFlags->eStateFlag_InvokeAquaBarrier || can_x_dropdash == 0 && stateFlags->eStateFlag_InvokeThunderBarrier || stateFlags->eStateFlag_InvokeSkateBoard || stateFlags->eStateFlag_InvokePtmSpike || can_x_dropdash == 0 && stateFlags->eStateFlag_Homing || DropDash_Disabled)
	{
		// We cannot dropdash now. Disable all functionallity.
		hasjumpstarted = 0;
		dropdashchargetimer = 0;
		dropdashtimer = 0;
		dropdashing = 0;
		dropdashcompleted = 0;

		if (OG_spindash_dropdash == 1)
		{
			OG_spindash_changed = 0;
			OG_spindash_dropdash = 0;
		}

		// Also do this just in case!
		if (dropdash_effects == 1)
		{
			// Make spindash ball disappear!
			uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
			WRITE_MEMORY(ballAddr, uint8_t, 0x09);

			if (!stateFlags->eStateFlag_InvokeSuperSonic)
			{
				// Make sonic re-appear!
				uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
				WRITE_MEMORY(visAddr, uint8_t, 0x08);
			}
			else
			{
				// Make sonic re-appear!
				uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
				WRITE_MEMORY(visAddr, uint8_t, 0x01);
			}

			dropdash_effects = 0;
		}

		if (rotation_reset == 0)
		{
			// Switch rotation speed back to normal.
			uintptr_t rotationspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(rotationspeedAddr, uint32_t, 0x44BB8000); // Value of 1500!

			rotation_reset = 1;
		}

		if (landslope_reset == 0 && current_stage != 0x306D6170)
		{
			*LandEnableMaxSlope->m_funcData->m_pValue = 45.0f;
			LandEnableMaxSlope->m_funcData->update();

			landslope_reset = 1;
		}
	}
	// Check to see if the player has started jumping and let go of the jump button.
	else if (!isGrounded && (*controllerButtons & jumpButton) == 0 && Jump_Animation && stateFlags->eStateFlag_EnableAirOnceAction && !stateFlags->eStateFlag_NoGroundFall && hasjumpstarted == 0 || current_stage == 0x00736D62 && !isGrounded && (*controllerButtons & jumpButton) == 0 && hasjumpstarted == 0)
	{
		hasjumpstarted = 1;
		dropdashchargetimer = 0;

		// Also do this just in case!
		//if (dropdash_effects == 1 && jumpball_enable == 0)
		if (dropdash_effects == 1)
		{
			// Make spindash ball disappear!
			uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
			WRITE_MEMORY(ballAddr, uint8_t, 0x09);

			if (!stateFlags->eStateFlag_InvokeSuperSonic)
			{
				// Make sonic re-appear!
				uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
				WRITE_MEMORY(visAddr, uint8_t, 0x08);
			}
			else
			{
				// Make sonic re-appear!
				uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
				WRITE_MEMORY(visAddr, uint8_t, 0x01);
			}

			dropdash_effects = 0;
		}

		if (rotation_reset == 0)
		{
			// Switch rotation speed back to normal.
			uintptr_t rotationspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(rotationspeedAddr, uint32_t, 0x44BB8000); // Value of 1500!

			rotation_reset = 1;
		}

		if (landslope_reset == 0 && current_stage != 0x306D6170)
		{
			*LandEnableMaxSlope->m_funcData->m_pValue = 45.0f;
			LandEnableMaxSlope->m_funcData->update();

			landslope_reset = 1;
		}
	}
	// If the player let go then check if they pressed it again.
	else if (Configuration::dropdash_enabled && !isGrounded && (*controllerButtons & jumpButton) && dropdashing == 0 && Jump_Animation && !stateFlags->eStateFlag_NoGroundFall && hasjumpstarted == 1 || Configuration::dropdash_enabled && current_stage == 0x00736D62 && !isGrounded && (*controllerButtons & jumpButton) && dropdashing == 0 && hasjumpstarted == 1 || can_x_dropdash == 1 && Jump_Animation && dropdashing == 0 && hasjumpstarted == 1 && !isGrounded || can_x_dropdash == 1 && current_stage == 0x00736D62 && dropdashing == 0 && hasjumpstarted == 1 && !isGrounded)
	{
		// Make sure there is a short timer so you can't spam it too much.
		// At this point the drop dash is considered enabled.
		if (dropdashchargetimer > 10)
		{
			dropdashchargetimer = 0;
			dropdashing = 1;

			if (dropdash_effects == 0)
			{
				// Make spindash ball appear!
				uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
				WRITE_MEMORY(ballAddr, uint8_t, 0x08);

				if (!stateFlags->eStateFlag_InvokeSuperSonic)
				{
					// Make sonic disappear for now!
					uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
					WRITE_MEMORY(visAddr, uint8_t, 0x09);
				}
				else
				{
					// Make sonic disappear for now!
					uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
					WRITE_MEMORY(visAddr, uint8_t, 0x00);
				}
				
				// Check for hub world and use different id since that has a alternate loading csb.
				if (current_stage == 0x306D6170)
				{
					// Play Sound
					CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonicContext + 116);
					SharedPtrTypeless soundHandle;
					playSound(sonicContext, nullptr, soundHandle, 2001089, 1);
				}
				else
				{
					// Play Sound
					CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonicContext + 116);
					SharedPtrTypeless soundHandle;
					playSound(sonicContext, nullptr, soundHandle, 2001108, 1);
				}

				dropdash_effects = 1;
			}

			// GOTTA MAKE SURE WE CHECK FOR HUB OR IT CAN CRASH WITH MODERN!!!
			if (landslope_reset == 1 && current_stage != 0x306D6170)
			{
				*LandEnableMaxSlope->m_funcData->m_pValue = 100.0f;
				LandEnableMaxSlope->m_funcData->update();

				landslope_reset = 0;
			}

			//printf("GRIND DROPDASH!");
			uintptr_t grindbeginspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });

			//if (current_state == 0x40 && spinny_spindash == 1 || current_state == 0x3F && spinny_spindash == 1) // Check if we're in a 3D stage!
			if (current_state == 0 && spinny_spindash == 1)
			{
				WRITE_MEMORY(grindbeginspeedAddr, uint32_t, 0x42C80000); // 100
				grinddropdash = 1;
			}
			else
			{
				WRITE_MEMORY(grindbeginspeedAddr, uint32_t, 0x41F00000); // 30
				grinddropdash = 1;
			}
		}

		// If we're using OG Spindash settings then make sure dropdash still works.
		//if (Configuration::OG_spindash)
		if (Configuration::OG_spindash == "spindash_original") // New config system
		{
			uintptr_t chargelevel0Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(chargelevel0Addr, uint32_t, 0x00000000); // Value of 0!

			uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42480000); // 50 Speed!!!

			OG_spindash_dropdash = 1;
		}

		dropdashchargetimer++;
	}
	// One more check to make sure the player is still holding the button. If not then disable the dropdash.
	else if (!isGrounded && (*controllerButtons & jumpButton) == 0 && dropdashing == 1 && can_x_dropdash == 0 || stateFlags->eStateFlag_NoGroundFall)
	{
		dropdashtimer = 0;
		dropdashing = 0;
		dropdashchargetimer = 0;

		if (OG_spindash_dropdash == 1)
		{
			OG_spindash_changed = 0;
			OG_spindash_dropdash = 0;
		}

		if (dropdash_effects == 1)
		{
			// Make spindash ball disappear!
			uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
			WRITE_MEMORY(ballAddr, uint8_t, 0x09);

			if(!stateFlags->eStateFlag_InvokeSuperSonic)
			{
				// Make sonic re-appear!
				uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
				WRITE_MEMORY(visAddr, uint8_t, 0x08);
			}
			else
			{
				// Make sonic re-appear!
				uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
				WRITE_MEMORY(visAddr, uint8_t, 0x01);
			}

			dropdash_effects = 0;
		}
	}
	// Do the final checks to make sure we're clear for take off.
	if (isGrounded && dropdashing == 1)
	{
		dropdashchargetimer = 0;

		// Make dropdash effect grinding on rails!
		//uintptr_t grindbeginspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });
		//WRITE_MEMORY(grindbeginspeedAddr, uint32_t, 0x41B00000);

		if (rotation_reset == 1)
		{
			// This ensures the dropdash actually works up every slope because steeper slopes can get you a bit stuck due to this being too slow.
			uintptr_t rotationspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(rotationspeedAddr, uint32_t, 0x497423F0); // Value of 999999!

			rotation_reset = 0;
		}

		// We need a short timer to make this work. Gens will act weird otherwise and he'll just freeze up.
		if (dropdashtimer > 2)
		{

			// New Spindash Sound Fix 
			// Special thanks to ahremic!
			//if (spindashsoundfixvalue == 0x880000000092BF80)
			//{			
			//	WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880100000092BF80);
			//}

			ChangeState(&dropdashString, reinterpret_cast<int*>(sonicContext));
			dropdashtimer = 0;
			dropdashing = 0;
			hasjumpstarted = 0;
			dropdashcompleted = 1;

			// Disable grind dropdash since it should already be in effect by this point!
			//WRITE_MEMORY(grindbeginspeedAddr, uint32_t, 0x41000000);

			// Play The Spindash Full Charge Sound (which is the sound that matches the dropdash release in mania)
			//CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonicContext + 116);
			//SharedPtrTypeless soundHandle;
			//playSound(sonicContext, nullptr, soundHandle, 2001049, 1);

			if (dropdash_effects == 1)
			{
				// Make spindash ball disappear!
				uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
				WRITE_MEMORY(ballAddr, uint8_t, 0x09);

				if (!stateFlags->eStateFlag_InvokeSuperSonic)
				{
					// Make sonic re-appear!
					uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
					WRITE_MEMORY(visAddr, uint8_t, 0x08);
				}
				else
				{
					// Make sonic re-appear!
					uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
					WRITE_MEMORY(visAddr, uint8_t, 0x01);
				}

dropdash_effects = 0;

//if (spindashsoundfixvalue == 0x880100000092BF80)
//{				
//	WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880000000092BF80);
//}
			}

			if (current_stage == 0x00736D62)
			{
				// Change PLAYER_MIN_SPEED to a higher value to fix dropdash!
				WRITE_MEMORY(0x01A42050, uint32_t, 0x42F40000); // Value of 122!

				metalsonic_dropdash = 1;
				metalsonic_dropdash_timer = 0;
			}
		}

		dropdashtimer++;
	}
	// Reset some things just in case.
	else if (isGrounded && dropdashing == 0)
	{
	hasjumpstarted = 0;
	dropdashchargetimer = 0;
	dropdashtimer = 0;

	if (OG_spindash_dropdash == 1)
	{
		OG_spindash_changed = 0;
		OG_spindash_dropdash = 0;
	}

	if (dropdash_effects == 1)
	{
		// Make spindash ball disappear!
		uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
		WRITE_MEMORY(ballAddr, uint8_t, 0x09);

		if (!stateFlags->eStateFlag_InvokeSuperSonic)
		{
			// Make sonic re-appear!
			uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
			WRITE_MEMORY(visAddr, uint8_t, 0x08);
		}
		else
		{
			// Make sonic re-appear!
			uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
			WRITE_MEMORY(visAddr, uint8_t, 0x01);
		}

		dropdash_effects = 0;

	}

	if (rotation_reset == 0)
	{
		// Switch rotation speed back to normal.
		uintptr_t rotationspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });
		WRITE_MEMORY(rotationspeedAddr, uint32_t, 0x44BB8000); // Value of 1500!
		rotation_reset = 1;

		// New Spindash Sound Fix
		// We'll just revert this here. Easy spot to fit it.
		//if (spindashsoundfixvalue == 0x880100000092BF80)
		//{
		//	WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880000000092BF80);
		//}
	}

	if (landslope_reset == 0 && current_stage != 0x306D6170)
	{
		*LandEnableMaxSlope->m_funcData->m_pValue = 45.0f;
		LandEnableMaxSlope->m_funcData->update();

		landslope_reset = 1;
	}
	}

	if (current_stage == 0x00736D62 && metalsonic_dropdash == 1)
	{
		if (metalsonic_dropdash_timer > 120)
		{
			// Change PLAYER_MIN_SPEED to the original value value to fix dropdash!
			WRITE_MEMORY(0x01A42050, uint32_t, 0x42480000); // Value of 50!

			metalsonic_dropdash = 0;
			metalsonic_dropdash_timer = 0;
		}

		metalsonic_dropdash_timer++;
	}
	else if (metalsonic_dropdash == 1 || metalsonic_dropdash_timer > 0)
	{
		metalsonic_dropdash = 0;
		metalsonic_dropdash_timer = 0;
	}

	// Spindash Speeds!
	//if (!Configuration::OG_spindash && spinny_spindash == 1 && spinny_spindash_reset == 1)
	if (Configuration::OG_spindash == "spindash_csim" && spinny_spindash == 1 && spinny_spindash_reset == 1)
	{
		uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });

		if (stateFlags->eStateFlag_InvokeSuperSonic)
		{	
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42380000); // 46 Speed!!!		
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x424C0000); // 51 Speed!!!			
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42600000); // 56 Speed!!!

		}
		else
		{	
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42100000); // 36 Speed!!!
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x42240000); // 41 Speed!!!
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42380000); // 46 Speed!!!
		}

		uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x3F000000); // Value of 0.25!

		//if (HighSpeedEffectMaxVelocity_params_added == 1 && Configuration::cs_model == "graphics_marza" && current_stage != 0x306D6170 && current_stage != 0x317A6E63 && current_stage != 0x00626C62)
		//{
		//	*HighSpeedEffectMaxVelocity->m_funcData->m_pValue = 20.0f;
		//	HighSpeedEffectMaxVelocity->m_funcData->update();
		//}

		spinny_spindash = 0;
		spinny_spindash_reset = 0;
	}
	//else if (Configuration::OG_spindash && spinny_spindash == 1 && spinny_spindash_reset == 1 || Configuration::OG_spindash && OG_spindash_changed == 0)
	else if (Configuration::OG_spindash == "spindash_original" && spinny_spindash == 1 && spinny_spindash_reset == 1 || Configuration::OG_spindash == "spindash_original" && OG_spindash_changed == 0)
	{
		uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
		WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x40400000); // 3 Speed!!!

		uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
		WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x42480000); // 50 Speed!!!

		uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x428C0000); // 70 Speed!!!

		uintptr_t chargelevel0Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(chargelevel0Addr, uint32_t, 0x3E23D70A); // Value of 0.16!

		uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x3E75C28F); // Value of 0.24!

		uintptr_t chargelevel2Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x1C, 0xC, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x8, 0x10, 0x4 });
		WRITE_MEMORY(chargelevel2Addr, uint32_t, 0x3F000000); // Value of 0.5!

		//if (HighSpeedEffectMaxVelocity_params_added == 1 && Configuration::cs_model == "graphics_marza" && current_stage != 0x306D6170 && current_stage != 0x317A6E63 && current_stage != 0x00626C62)
		//{
		//	*HighSpeedEffectMaxVelocity->m_funcData->m_pValue = 20.0f;
		//	HighSpeedEffectMaxVelocity->m_funcData->update();
		//}

		OG_spindash_changed = 1;
		spinny_spindash = 0;
		spinny_spindash_reset = 0;
	}
	else if (Configuration::OG_spindash == "spindash_classic" && spinny_spindash == 1 && spinny_spindash_reset == 1 || Configuration::OG_spindash == "spindash_classic" && OG_spindash_changed == 0)
	{
		uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });

		if (stateFlags->eStateFlag_InvokeSuperSonic)
		{
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42380000); // 46 Speed!!!		
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x424C0000); // 51 Speed!!!			
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42600000); // 56 Speed!!!

		}
		else
		{
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x41C80000); // 25 Speed!!!
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x41F00000); // 30 Speed!!!
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42100000); // 36 Speed!!!
		}

		uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x3F000000); // Value of 0.25!

		OG_spindash_changed = 1;
		spinny_spindash = 0;
		spinny_spindash_reset = 0;
	}
	
	// Fix Spindash Charging (Showin Added)
	// When trying to spindash the classic way it doesn't charge right in gens. (almost always leaving u with a super nerfed spindash)
	// First check if we're pressing down.
	//uintptr_t spindashchargecheck = (0x1E77B54); // Added this check earlier cuz its also used for x dropdash.
	//spindashchargevalue = *(uint32_t*)(spindashchargecheck);
	//if (!Configuration::OG_spindash && isGrounded && dropdashing == 0 && dropdashtimer == 0 && spindashchargevalue == 0x04080001 || !Configuration::OG_spindash && isGrounded && dropdashing == 0 && dropdashtimer == 0 && spindashchargevalue == 0x00080000)
	//if (Configuration::OG_spindash == "spindash_csim" && isGrounded && dropdashing == 0 && dropdashtimer == 0 && spindashchargevalue == 0x04080001 || Configuration::OG_spindash == "spindash_csim" && isGrounded && dropdashing == 0 && dropdashtimer == 0 && spindashchargevalue == 0x00080000)
	if (Configuration::OG_spindash == "spindash_csim" || Configuration::OG_spindash == "spindash_classic")
	{ 
	if (isGrounded && dropdashing == 0 && dropdashtimer == 0 && spindashchargevalue == 0x04080001 || isGrounded && dropdashing == 0 && dropdashtimer == 0 && spindashchargevalue == 0x00080000)
	{
			if (spindashchargevalue == 0x04080001 && jumpcount == 0)
			{
				jumpcount = 1; 

				spindash_charge_level = 1;

				// Play Sound
				CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonicContext + 116);
				SharedPtrTypeless soundHandle;
				playSound(sonicContext, nullptr, soundHandle, 2001048, 1);
			}
			else if (spindashchargevalue == 0x00080000 && jumpcount == 1)
			{
				jumpcount = 2;
			}
			else if (spindashchargevalue == 0x04080001 && jumpcount == 2)
			{
				jumpcount = 3;
				spindash_charge_level = 2; // This will add an echo of the sound playing twice if we don't do this here. This is basically tapping it once after the inital charge.
			}
			else if (spindashchargevalue == 0x00080000 && jumpcount == 3)
			{
				jumpcount = 4;		
			}
			else if (spindashchargevalue == 0x04080001 && jumpcount == 4)
			{
				jumpcount = 5;
			}
			// Added more starting here. This makes it so it takes more than 3 button presses to fully charge which feels more classic accurate. About 6 button presses this way which matches about how long it takes to charge it with x if u spam it.
			// Definitly should clean this up and make it cases or something since it wasn't a lot before but now its a lot of else ifs to do this.
			else if (spindashchargevalue == 0x00080000 && jumpcount == 5)
			{
				jumpcount = 6;
			}
			else if (spindashchargevalue == 0x04080001 && jumpcount == 6)
			{
				jumpcount = 7;
			}
			else if (spindashchargevalue == 0x00080000 && jumpcount == 7)
			{
				jumpcount = 8;
			}
			else if (spindashchargevalue == 0x04080001 && jumpcount == 8)
			{
				jumpcount = 9;
			}
			else if (spindashchargevalue == 0x00080000 && jumpcount == 9)
			{
				jumpcount = 10;
			}
			else if (spindashchargevalue == 0x04080001 && jumpcount == 10)
			{
				jumpcount = 11;
			}

			//if (jumpcount <= 2)
			if (jumpcount <= 4)
			{
				// We don't need to ever change the level 0 timer.

				uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x461C3C00); // Value of 9999!

				uintptr_t chargelevel2Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x1C, 0xC, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x8, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel2Addr, uint32_t, 0x461C3C00); // Value of 9999!

				//spindash_charge_level = 2;

			}
			//else if (jumpcount <= 4 && jumpcount >= 3)
			else if (jumpcount <= 6 && jumpcount >= 5)
			{
				uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x00000000); // Value of 0!

				uintptr_t chargelevel2Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x1C, 0xC, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x8, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel2Addr, uint32_t, 0x461C3C00); // Value of 9999!

				spindash_charge_level = 3;

			}
			//else if (jumpcount >= 5)
			else if (jumpcount >= 11)
			{
				uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x00000000); // Value of 0!

				uintptr_t chargelevel2Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x1C, 0xC, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x8, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel2Addr, uint32_t, 0x00000000); // Value of 0!

				spindash_charge_level = 4;

			}

			classicspindash = 1;
			fixslidingspindash = 0;
			spindashtimer = 0;
	}
	//else if (!Configuration::OG_spindash && stateFlags->eStateFlag_SpinChargeSliding)
	//else if (Configuration::OG_spindash == "spindash_csim" && stateFlags->eStateFlag_SpinChargeSliding)
	else if (stateFlags->eStateFlag_SpinChargeSliding)
	{
		// Revert back to normal timers.
		if (classicspindash == 1)
		{
			uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x3F000000); // Value of 0.5!

			uintptr_t chargelevel2Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x1C, 0xC, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x8, 0x10, 0x4 });
			WRITE_MEMORY(chargelevel2Addr, uint32_t, 0x3F400000); // Value of 0.75!

			//spindash_charge_level = 0;

			//haschargestarted = 0;
			//spindashtimer = 0;
		}
		else if(fixslidingspindash == 0)
		{
			uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x00000000); // Value of 0!

			fixslidingspindash = 1;
			spindashtimer = 0;
		}
	}
	//else if(!Configuration::OG_spindash)
	//else if (Configuration::OG_spindash == "spindash_csim")
	else
	{
		// Revert back to normal timers.
		if (classicspindash == 1 || fixslidingspindash == 1)
		{
			if (spindashtimer > 30)
			{
				uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x3F000000); // Value of 0.25!

				uintptr_t chargelevel2Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x1C, 0xC, 0x0, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x0, 0x8, 0x10, 0x4 });
				WRITE_MEMORY(chargelevel2Addr, uint32_t, 0x3F400000); // Value of 0.75!

				//spindash_charge_level = 0;

				classicspindash = 0;
				fixslidingspindash = 0;
				//haschargestarted = 0;
			}

			spindashtimer++;
		}
		else if(spindashtimer != 0)
		{
			spindashtimer = 0;
		}

		//spindash_charge_level = 0;

		jumpcount = 0;
	}
	}

	// Fixed Rolling Physics / Super Physics (Showin Added)
	//if (isGrounded && stateFlags->eStateFlag_SpinDash) // Don't need to check for ground when spindashing since the state only counts u being on the ground anyways.
	// Use current_state to make sure we're in 2D!!!
	if (Configuration::roll_momentum && stateFlags->eStateFlag_SpinDash)
	{
		// Check for 2D so we don't mess with the 3D Classic Adventure mod.
		if (current_state == 1)
		{

		//uintptr_t currentvelocitycheck = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x1C, 0x124, 0xC, 0x54, 0x10, 0x680 });
		//currentvelocity = *(uint8_t*)(currentvelocitycheck);

		if (spinmomentum == 0)
		{
			// Fix spinning physics!

			// This makes it so sonic can't control rolling acceleration manually.
			// It's all down to momentum now.
			// SHOWIN! Make sure you add a decel address and set it to a low value so u can't stop instantly either.

			//uintptr_t outruncamAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x0, 0x0, 0x10, 0x4 });
			//WRITE_MEMORY(outruncamAddr, uint32_t, 0x42080000); // Value of 34!

			if (outrun_camera_enabled == 0 && actually_spindashed == 1 && current_stage != 0x306D6170)
			{
				*TargetFrontOffsetSpeedScale->m_funcData->m_pValue = -0.75f;
				TargetFrontOffsetSpeedScale->m_funcData->update();

				outrun_camera_enabled = 1;
			}

			uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FC00000); // Value of 1.5!

			uintptr_t sloperesistanceforceAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x0, 0x8, 0x8, 0x8, 0x0, 0x0, 0x8, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(sloperesistanceforceAddr, uint32_t, 0x40900000); // Value of 4.5!

			// This doesn't actually work in rolling.
			//uintptr_t accelAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
			//WRITE_MEMORY(accelAddr, uint32_t, 0x00000000); // Value of 0!

			//if (supersonic == 0)
			//{
			//	uintptr_t maxvelocityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x8, 0x10, 0x4 });
			//	WRITE_MEMORY(maxvelocityAddr, uint32_t, 0x42480000); // Value of 50!
			//}

			spinmomentum = 1;
			outruntimer = 0;

			//printf("OUTRUNNING CAMERA!!!");
		}

		//printf("LeftStickVertical Input %f", inputPtr->LeftStickVertical);
		//printf("LeftStickHorizontal Input %f", inputPtr->LeftStickHorizontal);

		// Remove this from spinmomentum section cuz we need to change depending on situation.
		// If we're forcing accel / decel then nerf rolling momentum.
		if (inputPtr->LeftStickHorizontal != 0.0f || input.IsDown(Sonic::eKeyState_DpadLeft) || input.IsDown(Sonic::eKeyState_DpadRight))
		{
			//*AccelerationForce->m_funcData->m_pValue = 11.0f;
			//AccelerationForce->m_funcData->update();

			// Have to use my pointer cuz that didn't work.
			uintptr_t AccelerationForceAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(AccelerationForceAddr, uint32_t, 0x41300000); // Value of 11!

			uintptr_t gravityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x10, 0x4 });
			WRITE_MEMORY(gravityAddr, uint32_t, 0x42780000); // Value of 62!

			if (spinmomentum == 1)
			{
				if (dropdashcompleted == 0)
				{
					uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
					WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FC00000); // Value of 1.5!
				}
			}
		}
		else
		{
			//*AccelerationForce->m_funcData->m_pValue = 0.0f;
			//AccelerationForce->m_funcData->update();

			uintptr_t AccelerationForceAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(AccelerationForceAddr, uint32_t, 0x00000000); // Value of 0!

			uintptr_t gravityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x10, 0x4 });
			WRITE_MEMORY(gravityAddr, uint32_t, 0x42960000); // Value of 75!

			// Makes sonic go through loops if not holding right.
			if (spinmomentum == 1)
			{
				if (dropdashcompleted == 0)
				{
					uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
					WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FC00000); // Value of 1.5!
					//WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FF33333); // Value of 1.9!
					//WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FD9999A); // Value of 1.7!
				}
			}
		}

		// Slope rate starts off high but goes lower if dropdashing.
		if (dropdashcompleted == 1 && spinmomentum == 1 && outruntimer < 15)
		{
			uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(sloperateAddr, uint32_t, 0x40200000); // Value of 2.5!
		}
		else if (dropdashcompleted == 1 && spinmomentum == 1 && outruntimer < 30)
		{
			uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(sloperateAddr, uint32_t, 0x40000000); // Value of 2!
		}
		else if (dropdashcompleted == 1 && spinmomentum == 1 && outruntimer < 45)
		{
			uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FC00000); // Value of 1.5!
		}
		else if (dropdashcompleted == 1 && spinmomentum == 1 && outruntimer < 60)
		{
			uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(sloperateAddr, uint32_t, 0x3F800000); // Value of 1!
		}

		// Add a timer to make sure its only when we start spindashing!
		// Doing this helps us simulate the outrunning camera for all spindash speeds but not for rolling in general.
		if (spinmomentum == 1 && outruntimer > 10 && actually_spindashed == 1 && outrun_camera_enabled == 1 && current_stage != 0x306D6170)
		{
			//uintptr_t outruncamAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x0, 0x0, 0x10, 0x4 });
			//WRITE_MEMORY(outruncamAddr, uint32_t, 0x425C0000); // Value of 55!

			*TargetFrontOffsetSpeedScale->m_funcData->m_pValue = 0.1f;
			TargetFrontOffsetSpeedScale->m_funcData->update();

			// Enable Classic Run Particles
			//uintptr_t spinnyfeetAddr = (0xDC1C6F);
			//WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x0098840F);

			outrun_camera_enabled = 0;
			actually_spindashed = 0; // This makes it so actual outrun camera can override this.
		}

		outruntimer++;

		}

	}
	else if (Configuration::roll_momentum && spinmomentum == 1)
	{
	// Revert to normal physics!

	uintptr_t outruncamAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x0, 0x0, 0x10, 0x4 });
	WRITE_MEMORY(outruncamAddr, uint32_t, 0x425C0000); // Value of 55!

	uintptr_t sloperateAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x8, 0x0, 0x0, 0x10, 0x4 });
	WRITE_MEMORY(sloperateAddr, uint32_t, 0x3FA66666); // Value of 1.3!

	uintptr_t sloperesistanceforceAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x0, 0x8, 0x8, 0x8, 0x0, 0x0, 0x8, 0x0, 0x10, 0x4 });
	WRITE_MEMORY(sloperesistanceforceAddr, uint32_t, 0x40200000); // Value of 2.5!

	//uintptr_t accelAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
	//WRITE_MEMORY(accelAddr, uint32_t, 0x41300000); // Value of 11!

	uintptr_t gravityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x10, 0x4 });
	WRITE_MEMORY(gravityAddr, uint32_t, 0x41F00000); // Value of 30!

	// Check for 2D. Make sure we switch back to 3D accel if not.
	//if (current_state == 1)
	//{
	//	*AccelerationForce->m_funcData->m_pValue = 11.0f;
	//	AccelerationForce->m_funcData->update();
	//}
	//else
	//{
	//	*AccelerationForce->m_funcData->m_pValue = 16.0f;
	//	AccelerationForce->m_funcData->update();
	//}
	uintptr_t AccelerationForceAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
	WRITE_MEMORY(AccelerationForceAddr, uint32_t, 0x41300000); // Value of 11!


	//if (supersonic == 0)
	//{
	//	uintptr_t maxvelocityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x8, 0x10, 0x4 });
	//	WRITE_MEMORY(maxvelocityAddr, uint32_t, 0x421C0000); // Value of 39!
	//}

	spinmomentum = 0;
	dropdashcompleted = 0;
	outruntimer = 0;
	}
	else if (Configuration::roll_momentum && stateFlags->eStateFlag_InvokeSuperSonic)
	{
	// If we're super sonic fix his speed settings.
	if (supersonic == 0)
	{
		//uintptr_t accelAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
		//WRITE_MEMORY(accelAddr, uint32_t, 0x41B00000); // Value of 22!

		uintptr_t maxvelocityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x8, 0x10, 0x4 });
		WRITE_MEMORY(maxvelocityAddr, uint32_t, 0x42C80000); // Value of 100! This fixes super not being able to go up to the max speed values.

		//if (!Configuration::OG_spindash)
		if(Configuration::OG_spindash == "spindash_csim")
		{
			uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
			uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
			uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42380000); // 46 Speed!!!		
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x424C0000); // 51 Speed!!!			
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42600000); // 56 Speed!!!
		}
		else if (Configuration::OG_spindash == "spindash_classic")
		{
			uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
			uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
			uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
			WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x41F00000); // 30 Speed!!!
			WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x42380000); // 36 Speed!!!
			WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42380000); // 46 Speed!!!
		}

		supersonic = 1;
	}
	spinmomentum = 0;
	outruntimer = 0;
	}
	else if (Configuration::roll_momentum && !stateFlags->eStateFlag_InvokeSuperSonic && supersonic == 1)
	{
	// Revert to normal physics!
	//uintptr_t accelAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x4, 0x8, 0x0, 0x10, 0x4 });
	//WRITE_MEMORY(accelAddr, uint32_t, 0x41300000); // Value of 11!

	//uintptr_t maxvelocityAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x0, 0x4, 0x8, 0x10, 0x4 });
	//WRITE_MEMORY(maxvelocityAddr, uint32_t, 0x42480000); // Value of 50!

	//if (!Configuration::OG_spindash)
	if(Configuration::OG_spindash == "spindash_csim")
	{
		uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42100000); // 36 Speed!!!
		WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x42240000); // 41 Speed!!!
		WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42380000); // 46 Speed!!!
	}
	else if (Configuration::OG_spindash == "spindash_classic")
	{
		uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
		uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
		WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x41C80000); // 25 Speed!!!
		WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x41F00000); // 30 Speed!!!
		WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42100000); // 36 Speed!!!
	}

	supersonic = 0;
	spinmomentum = 0;
	outruntimer = 0;
	}

	if (stateFlags->eStateFlag_Goal)
	{
		sonic_goal = 1;
	}
	else
	{
		sonic_goal = 0;
	}

	// Rotate sonic to face the proper direction when jumping. This fixes the fire shield dash and makes dropdash slightly more reliable.
	//if (Jump_Animation && current_state == 1 && !isGrounded && !stateFlags->eStateFlag_OutOfControl && !stateFlags->eStateFlag_InvokeSkateBoard && !stateFlags->eStateFlag_InvokePtmSpike)
	//{
	//	if (inputPtr->LeftStickHorizontal > 0.0f || input.IsDown(Sonic::eKeyState_DpadRight))
	//	{
	//		sonic_direction = 1;
	//		printf("Moving Right.");
	//		
	//	}
	//	else if(inputPtr->LeftStickHorizontal < 0.0f || input.IsDown(Sonic::eKeyState_DpadLeft))
	//	{
	//		sonic_direction = 1;
	//		printf("Moving Left.");
	//	}
	//	else
	//	{
	//		sonic_direction = 1;
	//	}
	//}
	//else if (sonic_direction != 0)
	//{
	//	sonic_direction = 0;
	//}

	// BUG: Gens seems to get upset if you roll the exact same frame you land.
	// HACK: Therefore we implement a heuristic to see if he was grounded on the last frame.
	// Without this, he's kind of... permanently in crouch?

	wasGrounded = isGrounded;

	// This lets us store the rotation whenever, since its always executing.
	// TODO: this COULD be bug prone, possibly consider doing this OnFrame before we reset our flag?

	if (!rotationOverride)
	{
		SetRotation(sonicContext->Transform->Rotation);
	}
}

static void* fCGlitterCreate
(
	void* pContext,
	SharedPtrTypeless& handle,
	void* pMatrixTransformNode,
	Hedgehog::Base::CSharedString const& name,
	uint32_t flag
)
{
	static void* const pCGlitterCreate = (void*)0xE73890;
	__asm
	{
		push    flag
		push    name
		push    pMatrixTransformNode
		mov     eax, pContext
		mov     esi, handle
		call[pCGlitterCreate]
	}
}

static void fCGlitterEnd
(
	void* pContext,
	SharedPtrTypeless& handle,
	bool instantStop
)
{
	static void* const pCGlitterEnd = (void*)0xE72650;
	static void* const pCGlitterKill = (void*)0xE72570;
	__asm
	{
		mov     eax, [handle]
		mov     ebx, [eax + 4]
		push    ebx
		test	ebx, ebx
		jz		noIncrement
		mov		edx, 1
		add		ebx, 4
		lock xadd [ebx], edx

		noIncrement:
		mov     ebx, [eax]
		push    ebx
		mov     eax, pContext
		cmp     instantStop, 0
		jnz     jump
		call	[pCGlitterEnd]
		//jmp     end // for some reason I had to remove this but it still seems to work

		jump:
		call	[pCGlitterKill]

		//end: // never used without the part above being commented out (hopefully this doesnt fuck shit up)
	}
}

// SHOWIN TO DO!!!
// MOVE THE STUFF ABOVE THAT ORIGINATED FROM 3D SPINDASH FORK TO BE BE DOWN HERE INSTEAD!!!! THIS IS MUCH MORE FLEXIABLE AND HAVING BOTH AT ONCE IS BAD!
// CERAMIC SHOWED ME THAT PlayerSpeedUpdate IS BAD TOO! GONNA HAVE TO REWORK EVERYTHING TO HOOK TO ACTUAL PLAYER STATES!

bool super_sonic_ani_walk = 0;
bool super_sonic_transform_fix = 0;
bool hub_idle = 0;
//bool CSeyewhite_fixed = 0;
//uint32_t ringcheckvalue = 0;
SharedPtrTypeless PeeloutFX;
HOOK(void, __fastcall, CPlayerSpeedUpdateParallel, 0xE6BF20, Sonic::Player::CPlayerSpeed* This, void* _, const hh::fnd::SUpdateInfo& updateInfo)
{
	if (classic_sonic == 1)
	{

		//Jumping = This->m_StateMachine.GetCurrentState()->GetStateName() == "Jump";
		//JumpShort = This->m_StateMachine.GetCurrentState()->GetStateName() == "JumpShort";
		//HangOn = This->m_StateMachine.GetCurrentState()->GetStateName() == "HangOn";

		// Animation State Checking For Middle Mouth
		//TakeBreathF = This->m_StateMachine.GetCurrentState()->GetStateName() == "TakeBreath";
		//DivingFloat = This->m_StateMachine.GetCurrentState()->GetStateName() == "DivingFloat";
		//ExternalControl = This->m_StateMachine.GetCurrentState()->GetStateName() == "ExternalControl"; // THIS IS FLOATING ANIMATION FOR FANS???
		//if (ExternalControl == true)
		//{
		//	printf("FLOATING");
		//}

		auto input = Sonic::CInputState::GetInstance()->GetPadState();
		auto inputState = Sonic::CInputState::GetInstance();
		auto inputPtr = &inputState->m_PadStates[inputState->m_CurrentPadStateIndex];

		auto sonic = This->GetContext();
		auto Flags = sonic->m_pStateFlag;
		//char const* Gameplay_State = This->m_StateMachine.GetCurrentState()->GetStateName().c_str();
		//printf(Gameplay_State);

		auto localVelocity = sonic->m_spMatrixNode->m_Transform.m_Rotation.inverse() * sonic->m_Velocity;

		// Added To check button presses
		//auto input = Sonic::CInputState::GetInstance()->GetPadState();
		//auto inputState = Sonic::CInputState::GetInstance();
		//auto inputPtr = &inputState->m_PadStates[inputState->m_CurrentPadStateIndex];

		//HeldX = input.IsDown(Sonic::eKeyState_X);
		//HeldDpadDown = input.IsDown(Sonic::eKeyState_DpadDown);

		// Grind Dropdash Stuff
		if (dropdash_effects == 0 && grinddropdash == 1)
		{
			if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Spin")
			{
				sonic->ChangeAnimation("Grind_Spin");

				// Not really needed.
				//*GrindAccelerationForce->m_funcData->m_pValue = 24.0f;
				//GrindAccelerationForce->m_funcData->update();
			}
			
			if (grindtimer > 3)
			{
				uintptr_t grindbeginspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });

				// Disable grind dropdash!
				WRITE_MEMORY(grindbeginspeedAddr, uint32_t, 0x41000000);


				grindtimer = 0;
				grinddropdash = 0;

				//printf("DISABLE GRIND DROPDASH!");
			}

			grindtimer++;
		}
		else if (current_state == 1 && This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Spin" && inputPtr->LeftStickVertical <= -0.9f || current_state == 1 && This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Spin" && input.IsDown(Sonic::eKeyState_DpadDown) || current_state == 1 && This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Spin" && input.IsDown(Sonic::eKeyState_B))
		{
			//*GrindAccelerationForce->m_funcData->m_pValue = 24.0f;
			//GrindAccelerationForce->m_funcData->update();
			sonic->ChangeAnimation("Grind_Spin");
			grindtimer = 0;

			// Play Sound
			CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
			SharedPtrTypeless soundHandle;
			playSound(sonic, nullptr, soundHandle, 2001047, 1);
		}
		else if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Idle" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Fast" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Land" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "Grind_Spin")
		{
			//*GrindAccelerationForce->m_funcData->m_pValue = 12.0f;
			//GrindAccelerationForce->m_funcData->update();
			sonic->ChangeAnimation("Grind_Land");
			grindtimer = 0;
		}
		else if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Grind_Idle" && localVelocity.z() > 20.0f)
		{
			sonic->ChangeAnimation("Grind_Fast");
			grindtimer = 0;
		}
		else if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Grind_Fast" && localVelocity.z() < 20.0f)
		{
			sonic->ChangeAnimation("Grind_Idle");
			grindtimer = 0;
		}
		else
		{
			grindtimer = 0;
		}
		
		DropDash_Disabled_2 =
			This->m_StateMachine.GetCurrentState()->GetStateName() == "HangOn" ||
			This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind" ||
			This->m_StateMachine.GetCurrentState()->GetStateName() == "ExternalControl";

		if (DropDash_Disabled_2 || spinnycheckvalue == 0x00000001)
		{

			// We cannot dropdash now. Disable all functionallity.
			hasjumpstarted = 0;
			dropdashchargetimer = 0;
			dropdashtimer = 0;
			dropdashing = 0;
			dropdashcompleted = 0;

			if (OG_spindash_dropdash == 1)
			{
				OG_spindash_changed = 0;
				OG_spindash_dropdash = 0;
			}

			// Also do this just in case!
			if (dropdash_effects == 1)
			{
				// Make spindash ball disappear!
				uintptr_t ballAddr = FindDMAAddy(0x1E5E2F0, { 0x18, 0xC, 0x10, 0x1C, 0x00, 0x80 });
				WRITE_MEMORY(ballAddr, uint8_t, 0x09);

				if (!Flags->m_Flags[sonic->eStateFlag_InvokeSuperSonic])
				{
					// Make sonic re-appear!
					uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
					WRITE_MEMORY(visAddr, uint8_t, 0x08);
				}
				else
				{
					// Make sonic re-appear!
					uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
					WRITE_MEMORY(visAddr, uint8_t, 0x01);
				}

				if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Grind")
				{
					// Play Sound
					CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
					SharedPtrTypeless soundHandle;
					playSound(sonic, nullptr, soundHandle, 2001049, 1);
				}

				dropdash_effects = 0;
			}

			if (rotation_reset == 0)
			{
				// Switch rotation speed back to normal.
				uintptr_t rotationspeedAddr = FindDMAAddy(0x1E61C5C, { 0x0, 0x4, 0x4, 0x8, 0x0, 0x0, 0x8, 0x0, 0x8, 0x8, 0x10, 0x4 });
				WRITE_MEMORY(rotationspeedAddr, uint32_t, 0x44BB8000); // Value of 1500!

				rotation_reset = 1;
			}

			if (landslope_reset == 0 && current_stage != 0x306D6170)
			{
				*LandEnableMaxSlope->m_funcData->m_pValue = 45.0f;
				LandEnableMaxSlope->m_funcData->update();

				landslope_reset = 1;
			}

		}

		// We have to fix ssz dropdash issue here! Not the best solution but it works. Can't do this in SonicMovementMaybe.
		// For now I'll check for drop_effects earlier to reduce the issue from happening.
		if (current_stage == 0x317A7373)
		{
			uintptr_t spinnycheckadd = FindDMAAddy(0x1E5E2F0, { 0x534, 0x4, 0x78 });
			spinnycheckvalue = *(uint32_t*)(spinnycheckadd);

			// Spindash Speeds!
			if (spinnycheckvalue == 0x00000001)
			{
				if (spinny_spindash == 0)
				{
					// Make spindash speeds high enough to remove rolling on the spinny thing.
					// This makes it control much better!
					uintptr_t spindashspeed_lvl1 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x8, 0x10, 0x4 });
					WRITE_MEMORY(spindashspeed_lvl1, uint32_t, 0x42C80000); // 100 Speed!!!

					uintptr_t spindashspeed_lvl2 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x4, 0x8, 0x0, 0x4, 0x8, 0x8, 0x8, 0x8, 0x8, 0x10, 0x4 });
					WRITE_MEMORY(spindashspeed_lvl2, uint32_t, 0x42C80000); // 100 Speed!!!

					uintptr_t spindashspeed_lvl3 = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x0, 0x10, 0x4 });
					WRITE_MEMORY(spindashspeed_lvl3, uint32_t, 0x42C80000); // 100 Speed!!!

					uintptr_t chargelevel1Addr = FindDMAAddy(0x1E61C5C, { 0x0, 0x18, 0x0, 0x4, 0x8, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x0, 0x8, 0x0, 0x10, 0x4 });
					WRITE_MEMORY(chargelevel1Addr, uint32_t, 0x00000000); // Value of 0!
				}

				spinny_spindash = 1;
				spinny_spindash_reset = 0;
			}
			else if (spinny_spindash_reset == 0)
			{
				spinny_spindash_reset = 1;
			}
		}


	// Showin Added
	// Extra Mouth Switching for new model!
	//printf(Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName().c_str());
	//printf(Configuration::cs_model.c_str());

	// Various Marza specific graphical changes
	if (Configuration::cs_model == "graphics_marza")
	{
		bool Middle_Mouth_Animations =
			This->m_StateMachine.GetCurrentState()->GetStateName() == "TakeBreath" ||
			This->m_StateMachine.GetCurrentState()->GetStateName() == "DivingFloat" ||
			This->m_StateMachine.GetCurrentState()->GetStateName() == "ExternalControl" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Float" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "GateTurnLoop" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "GateTurnLoopRev" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "TakeBreathF_E" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "TakeBreathB_E" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "BrakeStartL" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "BrakeStartR" ||
			Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "GrabByEnemy";

		if (mouthcheck == 0 && Middle_Mouth_Animations || Flags->m_Flags[sonic->eStateFlag_Damaging] && Flags->m_Flags[sonic->eStateFlag_OutOfControl] || Flags->m_Flags[sonic->eStateFlag_Damaging] && Flags->m_Flags[sonic->eStateFlag_AirOutOfControl] || mouthcheck == 0 && Flags->m_Flags[sonic->eStateFlag_HipSliding])
		{
			//if (mouthcheck == 0) // removed this an added to specific stuff above because damage in air would change back when we don't want it to
			//{
				uintptr_t mouthAddr = FindDMAAddy(0x1E5E304, { 0x110, 0x400 });
				WRITE_MEMORY(mouthAddr, uint8_t, 0x02);
				mouthcheck = 1;
			//}
		}
		else if (mouthcheck == 1 && !Middle_Mouth_Animations && !Flags->m_Flags[sonic->eStateFlag_HipSliding])
		{
			uintptr_t mouthAddr = FindDMAAddy(0x1E5E304, { 0x110, 0x400 });
			WRITE_MEMORY(mouthAddr, uint8_t, 0x00);
			mouthcheck = 0;
		}

		// Make classic death animation play on fall deaths too!
		if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Fall" && Flags->m_Flags[sonic->eStateFlag_Dead])
		{
			This->m_StateMachine.ChangeState("NormalDamageDeadAir");
		}
		//else if (current_stage == 0x306D6170 && supersonic == 0)	// added super check just in case you cheat to put him in hub
		else if (current_stage == 0x306D6170 && supersonic == 0)	// added super check just in case you cheat to put him in hub
		{
			// Restore Hub Idle Animations
			if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleA" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleA_rev" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleB" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleB_rev" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleC" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleC_rev" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleD" || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "IdleD_rev")
			{
				if (hub_idle == 0)
				{
					// Pick a random number between 0-9
					int random_number = rand() % 10;

					switch (random_number)
					{
						case 1:
						{
							sonic->ChangeAnimation("BGM_CLASSIC");
							//printf("BGM_CLASSIC");
							hub_idle = 1;
							break;
						}
						case 2:
						{
							sonic->ChangeAnimation("BGM_DANCE");
							//printf("BGM_DANCE");
							hub_idle = 1;
							break;
						}
						case 3:
						{
							sonic->ChangeAnimation("BGM_HIGH");
							//printf("BGM_HIGH");
							hub_idle = 1;
							break;
						}
						case 4:
						{
							sonic->ChangeAnimation("BGM_LOW");
							//printf("BGM_LOW");
							hub_idle = 1;
							break;
						}
						case 5:
						{
							sonic->ChangeAnimation("BGM_ROCK");
							//printf("BGM_ROCK");
							hub_idle = 1;
							break;
						}
						default:
						{
							hub_idle = 1;
						}						
					}
				}
			}
			else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "BGM_IDLE")
			{
				This->m_StateMachine.ChangeState("Stand");
				hub_idle = 0;
			}
			else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Stand")
			{
				hub_idle = 0;
			}
		}
	}

	// Let Sonic Outrun Camera (works with manual cams too)
	//auto localVelocity = sonic->m_spMatrixNode->m_Transform.m_Rotation.inverse() * sonic->m_Velocity;
	//printf("Current Velocity: %f", localVelocity.z());
	//if (current_state == 1 && localVelocity.z() > 42.0f || supersonic == 1 && current_state == 1 && localVelocity.z() > 36.0f || current_stage == 0x00736D62 && current_state == 1 && localVelocity.z() > 33.5f)
	if (current_state == 1 && localVelocity.z() > Configuration::peelout_display_speed || supersonic == 1 && current_state == 1 && localVelocity.z() > 36.0f || current_stage == 0x00736D62 && current_state == 1 && localVelocity.z() > 33.5f)
	{
		outrun_camera_enabled = 1;
		//peelout_ani_played = 1;
		peelout_ani_played = 0;

		if (current_stage != 0x00736D62 && current_stage != 0x306D6170)
		{
			*TargetFrontOffsetSpeedScale->m_funcData->m_pValue = -0.15f;
			TargetFrontOffsetSpeedScale->m_funcData->update();
		}

		//auto ChestNode = This->m_spCharacterModel->GetNode("Spine1"); //Set up Chest bone matrix for VFX
		///Eigen::Affine3f affine;
		//affine = ChestNode->m_WorldMatrix;

		//peelout_blastoff_timer = 0;

		// Super Peel Out Stuff!
		if (Configuration::peelout_enabled == true && !PeeloutFX && This->m_StateMachine.GetCurrentState()->GetStateName() == "Walk" && supersonic != 1)
		{
			//auto walkfixString = GensString("Walk");
			//ChangeState(&walkfixString, reinterpret_cast<int*>(sonic));
			This->m_StateMachine.ChangeState("Walk");

			// Play peel out animation
			sonic->ChangeAnimation("PeelOut");

			// Disable Classic Run Particles
			uintptr_t spinnyfeetAddr = (0xDC1C6F);
			WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x000099E9);

			// Play peel out effect
			//void* matrixNode = (void*)((uint32_t)sonic + 0x30);
			auto BodyNode = This->m_spCharacterModel->GetNode("Reference");
			Eigen::Affine3f affine;
			affine = BodyNode->m_WorldMatrix;
			fCGlitterCreate(This->m_spContext.get(), PeeloutFX, &BodyNode, "ef_ch_snc_yh1_sprun1_peelout", 1);
		}	
		else if (Configuration::peelout_enabled == true && !PeeloutFX && This->m_StateMachine.GetCurrentState()->GetStateName() == "Walk" && supersonic == 1)
		{
			//auto walkfixString = GensString("Walk");
			//ChangeState(&walkfixString, reinterpret_cast<int*>(sonic));
			This->m_StateMachine.ChangeState("Walk");

			// Play peel out animation
			sonic->ChangeAnimation("PeelOut");

			// Play peel out effect
			//void* matrixNode = (void*)((uint32_t)sonic + 0x30);
			auto BodyNode = This->m_spCharacterModel->GetNode("Spine");
			Eigen::Affine3f affine;
			affine = BodyNode->m_WorldMatrix;
			fCGlitterCreate(This->m_spContext.get(), PeeloutFX, &BodyNode, "ef_ch_snc_yh1_invincible", 1);
		}
		else if (PeeloutFX && This->m_StateMachine.GetCurrentState()->GetStateName() != "Walk" && supersonic != 1)
		{
			if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Fall")
			{
				//auto walkfixString = GensString("Fall");
				//ChangeState(&walkfixString, reinterpret_cast<int*>(sonic));
				This->m_StateMachine.ChangeState("Fall");

				// Play peel out animation
				sonic->ChangeAnimation("PeelOut");
			}
			else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
			{
				// Enable Classic Run Particles
				uintptr_t spinnyfeetAddr = (0xDC1C6F);
				WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x0098840F);

				fCGlitterEnd(This->m_spContext.get(), PeeloutFX, true);
				PeeloutFX = nullptr;
			}
		}
		//else if (Configuration::cs_model == "graphics_marza" && !PeeloutFX && This->m_StateMachine.GetCurrentState()->GetStateName() == "Walk" && supersonic == 1)
		//{
		//	This->m_StateMachine.ChangeState("Walk");
		//
		//	// Play high speed super animation!
		//sonic->ChangeAnimation("AirBoost");
		//}
		else if (PeeloutFX && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
		{
			// Enable Classic Run Particles
			uintptr_t spinnyfeetAddr = (0xDC1C6F);
			WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x0098840F);

			fCGlitterEnd(This->m_spContext.get(), PeeloutFX, true);
			PeeloutFX = nullptr;
		}

		//printf("OUTRUNNING CAMERA!!!");
	}
	else if(outrun_camera_enabled == 1 && !Flags->m_Flags[sonic->eStateFlag_SpinDash] || outrun_camera_enabled == 1 && actually_spindashed == 0)
	{
		outrun_camera_enabled = 0;

		if (current_stage != 0x306D6170)
		{
			*TargetFrontOffsetSpeedScale->m_funcData->m_pValue = 0.1f;
			TargetFrontOffsetSpeedScale->m_funcData->update();
		}

		//printf("STOP OUTRUNNING CAMERA!!!");

		if (PeeloutFX)
		{
			if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Walk")
			{
				if (localVelocity.z() > 2.0f)
				{
					This->m_StateMachine.ChangeState("Walk");
					sonic->ChangeAnimation("Walk");
				}
				else
				{
					This->m_StateMachine.ChangeState("Stand");
				}

				if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
				{
					// Enable Classic Run Particles
					uintptr_t spinnyfeetAddr = (0xDC1C6F);
					WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x0098840F);

					fCGlitterEnd(This->m_spContext.get(), PeeloutFX, true);
					PeeloutFX = nullptr;

					peelout_ani_played = 0;
				}
			}
			else if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Fall")
			{
				if (peelout_ani_played == 0)
				{
					This->m_StateMachine.ChangeState("Fall");
					sonic->ChangeAnimation("PeelOut");
				}

				peelout_ani_played = 1;
			}
			else if(This->m_StateMachine.GetCurrentState()->GetStateName() != "Fall" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
			{
				peelout_ani_played = 0;

				// Enable Classic Run Particles
				uintptr_t spinnyfeetAddr = (0xDC1C6F);
				WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x0098840F);

				fCGlitterEnd(This->m_spContext.get(), PeeloutFX, true);
				PeeloutFX = nullptr;
			}
		}
	}
	else if (PeeloutFX && This->m_StateMachine.GetCurrentState()->GetStateName() != "Fall" && This->m_StateMachine.GetCurrentState()->GetStateName() != "LookUp" && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
	{
		// Enable Classic Run Particles
		uintptr_t spinnyfeetAddr = (0xDC1C6F);
		WRITE_MEMORY(spinnyfeetAddr, uint32_t, 0x0098840F);

		fCGlitterEnd(This->m_spContext.get(), PeeloutFX, true);
		PeeloutFX = nullptr;
	}
	//else if (localVelocity.z() < 20.0f && supersonic == 1)
	//{
	//	This->m_StateMachine.ChangeState("Walk");
	//	sonic->ChangeAnimation("Walk_Super");
	//}

	//auto input = Sonic::CInputState::GetInstance()->GetPadState();
	//auto inputState = Sonic::CInputState::GetInstance();
	//auto inputPtr = &inputState->m_PadStates[inputState->m_CurrentPadStateIndex];

	// SUPER PEEL OUT
	if (Configuration::peelout_enabled == true && !Flags->m_Flags[sonic->eStateFlag_OutOfControl] && !Flags->m_Flags[sonic->eStateFlag_Restarting] && !Flags->m_Flags[sonic->eStateFlag_Dead] && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "LookUp" && peelout_start == 0)
	{
		if(input.IsTapped(Sonic::eKeyState_A))
		{ 
			// Makes it so you don't jump and don't have to hold up to maintain peelout state.
			Flags->m_Flags[sonic->eStateFlag_OutOfControl] = 1;

			//if (Configuration::cs_model == "graphics_marza" && !PeeloutFX && supersonic != 1)
			if (!PeeloutFX && supersonic != 1)
			{
				//if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
				//{
				//	This->m_StateMachine.ChangeState("LookUp");
				//}

				// Play peel out animation
				sonic->ChangeAnimation("PeelOut_Begin");

				// Play peel out effect
				auto BodyNode = This->m_spCharacterModel->GetNode("Reference");
				Eigen::Affine3f affine;
				affine = BodyNode->m_WorldMatrix;
				fCGlitterCreate(This->m_spContext.get(), PeeloutFX, &BodyNode, "ef_ch_snc_yh1_sprun1_peelout", 1);

				peelout_ani_played = 0;
			}
			//else if (Configuration::cs_model != "graphics_marza" && peelout_ani_played == 0 && supersonic != 1)
			//{
				// Maybe use archive patcher to force new animations and fixed dropdash models for og.
				//This->m_StateMachine.ChangeState("LookUp");
			//	sonic->ChangeAnimation("TrnsStIdle");
			//	peelout_ani_played = 1;
			//}
			else if (supersonic == 1)
			{			
				sonic->ChangeAnimation("HomingAttackAfter4");

				if (Configuration::cs_model == "graphics_marza" && !PeeloutFX)
				{
					// Play peel out effect
					auto BodyNode = This->m_spCharacterModel->GetNode("Spine");
					Eigen::Affine3f affine;
					affine = BodyNode->m_WorldMatrix;
					fCGlitterCreate(This->m_spContext.get(), PeeloutFX, &BodyNode, "ef_ch_snc_yh1_invincible", 1);
				}			

				peelout_ani_played = 0;
			}

			// If we're using classic sounds then do our custom ones instead.
			if (Configuration::sound_style == "sound_classic")
			{
				// Play Sound
				CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
				SharedPtrTypeless soundHandle;
				playSound(sonic, nullptr, soundHandle, 3000000, 1);
			}
			else if (current_stage == 0x306D6170) // Check for hub world and use different id since that has a alternate loading csb.
			{
				// Play Sound
				CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
				SharedPtrTypeless soundHandle;
				playSound(sonic, nullptr, soundHandle, 2001090, 1);
			}
			else
			{
				// Play Sound
				CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
				SharedPtrTypeless soundHandle;
				playSound(sonic, nullptr, soundHandle, 2001064, 1); // Pipeline which is basically the same sound as the peel out from CD.
			}

			peelout_start = 1;
			peelout_timer = 0;
			//peelout_blastoff_timer = 0;
			//printf("PEELOUT BEGIN!!!");
		}
	}
	else if (peelout_start == 1 && localVelocity.z() < 1.0f && !Flags->m_Flags[sonic->eStateFlag_Damaging] && !Flags->m_Flags[sonic->eStateFlag_Restarting] && !Flags->m_Flags[sonic->eStateFlag_Dead])
	{
		if (peelout_timer <= 16)
		{
			peelout_timer++;
		}
		else if(peelout_ani_played == 0)
		{
			if (supersonic == 0)
			{
				sonic->ChangeAnimation("PeelOut");
			}

			peelout_ani_played = 1;
		  //printf("PEELOUT CHARGED!!!");
		}

		//if (!input.IsDown(Sonic::eKeyState_A) && peelout_timer > 10)
		if (inputPtr->LeftStickVertical <= 0.0f && !input.IsDown(Sonic::eKeyState_DpadUp) && peelout_timer > 16 || inputPtr->LeftStickHorizontal != 0.0f && peelout_timer > 16 || input.IsDown(Sonic::eKeyState_DpadLeft) && peelout_timer > 16 || input.IsDown(Sonic::eKeyState_DpadRight) && peelout_timer > 16 || input.IsDown(Sonic::eKeyState_DpadDown) && peelout_timer > 16)
		{
			//auto walkfixString = GensString("Walk");
			//ChangeState(&walkfixString, reinterpret_cast<int*>(sonic));
			//This->m_StateMachine.ChangeState("Walk");

			//sonic->m_Velocity.z() = 42.0f;

			Flags->m_Flags[sonic->eStateFlag_OutOfControl] = 0;

			Eigen::Vector3f position;
			Eigen::Quaternionf rotation;

			const uint32_t result = *(uint32_t*)((uint32_t) * (void**)((uint32_t)*(CSonicContext**)0x1E5E2F0 + 0x110) + 0xAC);

			float* pPos = (float*)(*(uint32_t*)(result + 0x10) + 0x70);
			position.x() = pPos[0];
			position.y() = pPos[1];
			position.z() = pPos[2];

			float* pRot = (float*)(*(uint32_t*)(result + 0x10) + 0x60);
			rotation.x() = pRot[0];
			rotation.y() = pRot[1];
			rotation.z() = pRot[2];
			rotation.w() = pRot[3];

			// Thanks brainuu
			alignas(16) MsgApplyImpulse message {};
			message.m_position = position;
			message.m_impulse = rotation * Eigen::Vector3f::UnitZ();
			message.m_impulseType = ImpulseType::None;
			message.m_outOfControl = 0.1f;
			message.m_notRelative = true;
			message.m_snapPosition = false;
			message.m_pathInterpolate = false;
			message.m_alwaysMinusOne = -1.0f;
			//message.m_impulse *= -50.0f;
			//message.m_impulse *= 43.0f; // Default speed changed to let u customize it.
			message.m_impulse *= Configuration::peelout_speed;
			

			FUNCTION_PTR(void, __thiscall, processPlayerMsgAddImpulse, 0xE6CFA0, void* This, void* message);
			alignas(16) MsgApplyImpulse msgApplyImpulse = message;
			void* player = *(void**)((uint32_t)*(CSonicContext**)0x1E5E2F0 + 0x110);
			processPlayerMsgAddImpulse(player, &msgApplyImpulse);

			peelout_start = 0;
			peelout_timer = 0;
			outrun_camera_enabled = 1;

			// If we're using classic sounds then do our custom ones instead.
			if (Configuration::sound_style == "sound_classic")
			{
				// Play Sound
				CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
				SharedPtrTypeless soundHandle;
				playSound(sonic, nullptr, soundHandle, 3000001, 1);
			}
			// Check for hub world and use different id since that has a alternate loading csb.
			//else if (current_stage == 0x306D6170)
			//{
				//// Very funky and is playing the wrong sound for some strange reason. Not the biggest deal to just forget about this for now cuz im lazy.
				// Play Sound
				//CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
				//SharedPtrTypeless soundHandle;
				//playSound(sonic, nullptr, soundHandle, 2001091, 1);
			//}
			else
			{
				// Play Sound
				CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
				SharedPtrTypeless soundHandle;
				//playSound(sonic, nullptr, soundHandle, 2001094, 1); // Super Intro noise
				playSound(sonic, nullptr, soundHandle, 2001036, 1); // Wind Noise (or shootout despite it being set to this wind sound?)
			}

			//peelout_blastoff_timer = 1;
			//printf("PEELOUT BLASTOFF!!!");
		}
		else if (inputPtr->LeftStickVertical <= 0.0f && !input.IsDown(Sonic::eKeyState_DpadUp) && peelout_timer < 16 || inputPtr->LeftStickHorizontal != 0.0f && peelout_timer < 16)
		{
			peelout_start = 0;
			peelout_timer = 0;
			peelout_ani_played = 0;
			Flags->m_Flags[sonic->eStateFlag_OutOfControl] = 0;
		
			if (PeeloutFX && Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() != "PeelOut")
			{
				This->m_StateMachine.ChangeState("LookUp");
		
				fCGlitterEnd(This->m_spContext.get(), PeeloutFX, true);
				PeeloutFX = nullptr;
			}
		}
	}
	else if(peelout_start == 1)
	{
		peelout_start = 0;
		peelout_timer = 0;
		peelout_ani_played = 0;
		//peelout_blastoff_timer = 0;
		Flags->m_Flags[sonic->eStateFlag_OutOfControl] = 0;

		if (Flags->m_Flags[sonic->eStateFlag_Damaging])
		{
			This->m_StateMachine.ChangeState("DamageOnRunning");
		}
	}

	// Fix some super sonic issues.
	// Like the dropdash ball being small and offset when initally transforming into super or back from super.
	// To fix this we enter the spindash state for a split second so it fixes the model.
	if (supersonic == 1)
	{
		//if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Walk" && super_sonic_ani_walk == 0)
		//{
		//	sonic->ChangeAnimation("JumpBoard");
		//	super_sonic_ani_walk = 1;
		//}
		//else if (This->m_StateMachine.GetCurrentState()->GetStateName() != "Walk")
		//{
		//	super_sonic_ani_walk = 0;
		//}

		//if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "BrakeLoopL" && localVelocity.z() < 1.0f || Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "BrakeLoopR" && localVelocity.z() < 1.0f)
		//{
		//	sonic->ChangeAnimation("Stand");
		//}

		if (super_sonic_transform_fix == 0 && This->m_StateMachine.GetCurrentState()->GetStateName() != "TransformSp")
		{
			This->m_StateMachine.ChangeState("SpinCharge");
			super_sonic_transform_fix = 1;
			This->m_StateMachine.ChangeState("Stand");
		}
	}
	else if (This->m_StateMachine.GetCurrentState()->GetStateName() != "TransformStandard" && super_sonic_transform_fix == 1)
	{
		This->m_StateMachine.ChangeState("SpinCharge");
		super_sonic_transform_fix = 0;
		This->m_StateMachine.ChangeState("Stand");
	}

	// Spindash sound fix
	uintptr_t spindashsoundfix = (0x11BB493);
	spindashsoundfixvalue = *(uint64_t*)(spindashsoundfix);

	// If we spindashed and didn't roll then lets add a check for it.
	if (This->m_StateMachine.GetCurrentState()->GetStateName() == "SpinCharge" || This->m_StateMachine.GetCurrentState()->GetStateName() == "SpinChargeSliding" || This->m_StateMachine.GetCurrentState()->GetStateName() == "SquatCharge")
	{
		if (!Flags->m_Flags[sonic->eStateFlag_InvokeSuperSonic])
		{
			// Make sonic disappear for now!
			uintptr_t visAddr = FindDMAAddy(0x01E5E2F0, { 0x18, 0x1C, 0x0, 0x80 });
			WRITE_MEMORY(visAddr, uint8_t, 0x09);
		}
		else
		{
			// Make sonic disappear for now!
			uintptr_t visAddr = FindDMAAddy(0x01B24094, { 0xA8, 0x4, 0x80, 0x64, 0x4, 0x10, 0x0, 0x10, 0x18, 0x4 });
			WRITE_MEMORY(visAddr, uint8_t, 0x00);
		}

		actually_spindashed = 1;
		actually_spindashed_sfx_fix = 1;

		// New Spindash Sound Fix 
		// Special thanks to ahremic!
		if (spindashsoundfixvalue == 0x880100000092BF80 && spindash_charge_level != 0)
		{
			WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880000000092BF80);
		}
		else if (spindashsoundfixvalue == 0x880000000092BF80 && spindashtimer_soundfix <= 5 && spindash_charge_level == 0)
		{
			WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880100000092BF80);
		}
		else if (spindashsoundfixvalue == 0x880100000092BF80 && spindashtimer_soundfix > 5 && spindash_charge_level == 0)
		{
			WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880000000092BF80);
			//printf("Reverted Spindash sound.");
		}

		spindashtimer_soundfix++;

		// A quick fix for x button drop dash on slopes
		if (dropdashcompleted == 1)
		{
			This->m_StateMachine.ChangeState("Stand");
			This->m_StateMachine.ChangeState("Spin");

			//Flags->m_Flags[sonic->eStateFlag_OutOfControl] = 0;

			Eigen::Vector3f position;
			Eigen::Quaternionf rotation;

			const uint32_t result = *(uint32_t*)((uint32_t) * (void**)((uint32_t) * (CSonicContext**)0x1E5E2F0 + 0x110) + 0xAC);

			float* pPos = (float*)(*(uint32_t*)(result + 0x10) + 0x70);
			position.x() = pPos[0];
			position.y() = pPos[1];
			position.z() = pPos[2];

			float* pRot = (float*)(*(uint32_t*)(result + 0x10) + 0x60);
			rotation.x() = pRot[0];
			rotation.y() = pRot[1];
			rotation.z() = pRot[2];
			rotation.w() = pRot[3];

			// Thanks brainuu
			alignas(16) MsgApplyImpulse message {};
			message.m_position = position;
			message.m_impulse = rotation * Eigen::Vector3f::UnitZ();
			message.m_impulseType = ImpulseType::None;
			message.m_outOfControl = 0.1f;
			message.m_notRelative = true;
			message.m_snapPosition = false;
			message.m_pathInterpolate = false;
			message.m_alwaysMinusOne = -1.0f;

			if (Configuration::OG_spindash == "spindash_original")
			{
				message.m_impulse *= 50.0f;
			}
			else if (Configuration::OG_spindash == "spindash_classic")
			{
				message.m_impulse *= 25.0f;
			}
			else
			{
				message.m_impulse *= 36.0f;
			}


			FUNCTION_PTR(void, __thiscall, processPlayerMsgAddImpulse, 0xE6CFA0, void* This, void* message);
			alignas(16) MsgApplyImpulse msgApplyImpulse = message;
			void* player = *(void**)((uint32_t) * (CSonicContext**)0x1E5E2F0 + 0x110);
			processPlayerMsgAddImpulse(player, &msgApplyImpulse);

			dropdashcompleted = 0;
		}

		//printf("SPINDASHED!");
	}
	else if(!Flags->m_Flags[sonic->eStateFlag_SpinDash])
	{
		actually_spindashed = 0;
		actually_spindashed_sfx_fix = 0;
		spindash_charge_level = 0;
		spindashtimer_soundfix = 0;

		// New Spindash Sound Fix
		// We'll just revert this here. Easy spot to fit it.
		if (spindashsoundfixvalue == 0x880100000092BF80)
		{
			WRITE_MEMORY(spindashsoundfix, uint64_t, 0x880000000092BF80);
		}

		//printf("Reset Spindash Check.");
	}
	else if(actually_spindashed_sfx_fix == 1)
	{
		actually_spindashed_sfx_fix = 0;
		if (spindashsoundfixvalue == 0x880000000092BF80 && spindash_charge_level == 1)
		{
			// Play Sound
			CSonicSpeedContextPlaySound* playSound = *(CSonicSpeedContextPlaySound**)(*(uint32_t*)sonic + 116);
			SharedPtrTypeless soundHandle;
			playSound(sonic, nullptr, soundHandle, 2001049, 1); // Spindash release sound
		}
	}

	// Fix Flame Shield making sonic vulneralbe when doing the special move. Now it functions like the it did in sonic 3.
	if (This->m_StateMachine.GetCurrentState()->GetStateName() == "AirBoost" && Flags->m_Flags[sonic->eStateFlag_InvokeFlameBarrier])
	{
		This->m_StateMachine.ChangeState("Jump");
	}

	// Classic Accurate Crush Death Physics!
	if (Configuration::OG_crush_death && This->m_StateMachine.GetCurrentState()->GetStateName() == "PressDamage" && !Flags->m_Flags[sonic->eStateFlag_Dead])
	{
		This->m_StateMachine.ChangeState("PressDead");
	}

	// Make rolling on slopes similar to the ones near the end of green hill act 1 actually work correctly.
	if (Configuration::roll_momentum && current_state == 1 && localVelocity.z() > 2.0f && This->m_StateMachine.GetCurrentState()->GetStateName() == "Walk" && isGrounded && !Flags->m_Flags[sonic->eStateFlag_OutOfControl] && !Flags->m_Flags[sonic->eStateFlag_KeepRunning])
	{
		if (inputPtr->LeftStickVertical <= -0.9f || input.IsDown(Sonic::eKeyState_DpadDown))
		{
			This->m_StateMachine.ChangeState("Spin");
		}
	}

	// Make sonic use push animations when moving against a wall!
	// This is a bit buggy. It's a small visual detail so just scrap it for now.
	//if (Configuration::cs_model == "graphics_marza" && current_stage != 0x306D6170 && current_state == 1 && localVelocity.z() == 0.0f && !Flags->m_Flags[sonic->eStateFlag_Freeze] && !Flags->m_Flags[sonic->eStateFlag_OutOfControl] && !Flags->m_Flags[sonic->eStateFlag_IgnorePadInput] && !Flags->m_Flags[sonic->eStateFlag_NoLandOutOfControl])
	//{
	//	if (This->m_StateMachine.GetCurrentState()->GetStateName() == "Stand" || This->m_StateMachine.GetCurrentState()->GetStateName() == "MoveStopL" || This->m_StateMachine.GetCurrentState()->GetStateName() == "MoveStopR")
	//	{
	//		if (inputPtr->LeftStickHorizontal > 0.1f || input.IsDown(Sonic::eKeyState_DpadRight))
	//		{
	//			sonic->ChangeAnimation("Push_Start_Right");
	//		}
	//		else if (inputPtr->LeftStickHorizontal < -0.1f || input.IsDown(Sonic::eKeyState_DpadLeft))
	//		{
	//			sonic->ChangeAnimation("Push_Start_Left");
	//		}
	//		else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Push_Loop_Right")
	//		{
	//			if (inputPtr->LeftStickHorizontal < 0.1f && !input.IsDown(Sonic::eKeyState_DpadRight))
	//			{
	//				sonic->ChangeAnimation("Stand");
	//			}
	//		}
	//		else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Push_Loop_Left")
	//		{
	//			if (inputPtr->LeftStickHorizontal > -0.1f && !input.IsDown(Sonic::eKeyState_DpadLeft))
	//			{
	//				sonic->ChangeAnimation("Stand");
	//			}
	//		}
	//	}
	//}
	//else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Push_Loop_Right")
	//{
	//	if (inputPtr->LeftStickHorizontal < 0.1f && !input.IsDown(Sonic::eKeyState_DpadRight))
	//	{
	//		sonic->ChangeAnimation("Stand");
	//	}	
	//}
	//else if (Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName() == "Push_Loop_Left")
	//{
	//	if (inputPtr->LeftStickHorizontal > -0.1f && !input.IsDown(Sonic::eKeyState_DpadLeft))
	//	{
	//		sonic->ChangeAnimation("Stand");
	//	}
	//}

	// Insta Shield Fix
	// First check if we have the skill equipped. If so we check a few things to see if it's being used.
	const auto context = This->GetContext();
	const uint64_t skills = (uint64_t)context->m_Field1A4;
	bool insta_shield_skill = skills & 0x10000; // from skyth's quickboot

	if (insta_shield_skill && This->m_StateMachine.GetCurrentState()->GetStateName() == "Jump" || insta_shield_skill && This->m_StateMachine.GetCurrentState()->GetStateName() == "JumpShort")
	{
		if (!isGrounded && (*controllerButtons & jumpButton) && hasjumpstarted == 1 && insta_shield == 0)
		{
			insta_shield = 1;
			//printf("Insta Shield Active!");
		}
		else if (isGrounded && insta_shield != 0)
		{
			insta_shield = 0;
			//printf("Insta Shield Off!");
		}
	}
	else if(insta_shield != 0)
	{
		insta_shield = 0;
		//printf("Insta Shield Off!");
	}

	//char const* Gameplay_ANI = Sonic::Player::CPlayerSpeedContext::GetInstance()->GetCurrentAnimationName().c_str();
	//printf(Gameplay_ANI);
	if (insta_shield == 2 && !Flags->m_Flags[sonic->eStateFlag_Rising])
	{
		//printf("Insta Shield Jump!");

		// Way less code and carries momentum properly
		sonic->m_Velocity.y() = 15.0f;

		insta_shield = 3;
	}
	else if (insta_shield == 2)
	{
		insta_shield = 3;
	}

	}

	originalCPlayerSpeedUpdateParallel(This, _, updateInfo);
}

void OnLoad()
{
	//if (current_stage == 0x306D6170 || current_stage == 0x317A6E63 || current_stage == 0x00626C62)
	//{
	//	classic_sonic = 0;
	//	//CSeyewhite_fixed = 1;
	//
	//	// I fixed most crashes but it seems like there is still a rare chance to crash in the hub and on restart in classic stages. (MIGHT BE FIXED NOW!!!)
	//	ParamManager::restoreParam(); // Fixes potential crashes with modern sonic.
	//}	
	//else if (classic_sonic == 1)
	if (classic_sonic == 1)
	{
		classic_sonic = 0;
		//CSeyewhite_fixed = 0;
	}

	peelout_start = 0;
	super_sonic_transform_fix = 0;
}

void __declspec(naked) LoadingMidAsmHook()
{
	static void* interruptAddress = (void*)0x65FCC0;
	static void* returnAddress = (void*)0x448E98;

	__asm
	{
		call[interruptAddress]

		call OnLoad

		jmp[returnAddress]
	}
}

void EnemyKill()
{
	if (insta_shield == 1)
	{	
		insta_shield = 2;
	}

	//if (jump_rebound == 0)
	//{
	//	jump_rebound = 1;
	//}
}

__declspec(naked) void ClassicSonicEnemyAsmHook()
{
	static void* returnAddress = (void*)0xBDDDA1;

	__asm
	{

		call EnemyKill

		jmp[returnAddress]
	}
}

inline bool IsFileExist(std::string const& file)
{
	struct stat buffer;
	return stat(file.c_str(), &buffer) == 0;
}

inline void GetModIniList(std::vector<std::string>& modIniList)
{
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::string exePath(buffer);
	std::string cpkRedirConfig = exePath.substr(0, exePath.find_last_of("\\")) + "\\cpkredir.ini";

	if (!IsFileExist(cpkRedirConfig))
	{
		printf("%s not exist.\n", cpkRedirConfig.c_str());
		return;
	}

	INIReader reader(cpkRedirConfig);
	std::string modsDatabase = reader.Get("CPKREDIR", "ModsDbIni", "mods\\ModsDB.ini");

	if (!IsFileExist(modsDatabase))
	{
		printf("%s not exist.\n", modsDatabase.c_str());
		return;
	}

	INIReader modsDatabaseReader(modsDatabase);
	int count = modsDatabaseReader.GetInteger("Main", "ActiveModCount", 0);
	for (int i = 0; i < count; i++)
	{
		std::string guid = modsDatabaseReader.Get("Main", "ActiveMod" + std::to_string(i), "");
		std::string config = modsDatabaseReader.Get("Mods", guid, "");
		if (!config.empty() && IsFileExist(config))
		{
			modIniList.push_back(config);
		}
	}
}

inline bool IsModEnabled(std::string const& testModName, std::string* o_iniPath = nullptr)
{
	std::vector<std::string> modIniList;
	GetModIniList(modIniList);
	for (size_t i = 0; i < modIniList.size(); i++)
	{
		std::string const& config = modIniList[i];
		INIReader configReader(config);
		std::string name = configReader.Get("Desc", "Title", "");
		if (name == testModName)
		{
			if (o_iniPath)
			{
				*o_iniPath = config;
			}

			return true;
		}
	}

	return false;
}

EXPORT Init()
{

	// Check to see if Skyth's mod is enabled. If not warn the player and don't run improvement mod. We'll allow the 9EX mod too cuz it also fixes the issue.
	// Basically some graphical stuff doesn't really work without it. Probably due to ram issues. To avoid any major issues we might as well use enforce skyth's mods that fix them.
	// There is virtually no reason somebody wouldn't want to use these anyways.
	if (!IsModEnabled("Direct3D 11") && !IsModEnabled("Direct3D 9 Ex"))
	{
		MessageBox(nullptr, TEXT("You do not have the Direct3D 11 mod installed. This is required for the 'Classic Sonic Improvement Mod' to run properly. Please install this mod before playing."), TEXT(" CLASSIC SONIC IMPROVEMENT MOD: ERROR"), MB_ICONERROR);
		exit(-1); // makes the game not run
	}

	WRITE_JUMP(0x00E35380, TransformPlayer_Hook)

	WRITE_JUMP(0x00E303D4, WorldInputComparisonOverride_3D)
	WRITE_JUMP(0x00E2E817, WorldInputComparisonOverride_Forward)

	// Thanks to Hyper for this! This helps resolve some pointers that don't work during loading!
	WRITE_JUMP(0x448E93, &LoadingMidAsmHook)
	WRITE_JUMP(0xBDDD9A, &ClassicSonicEnemyAsmHook);


	INSTALL_HOOK(Spin3DMovement)
	INSTALL_HOOK(SonicMovementMaybe)
	INSTALL_HOOK(SonicSpinChargeMovement)
	INSTALL_HOOK(SonicSpinChargeSlideMovement)

	INSTALL_HOOK(CPlayerSpeedUpdateParallel);

	// Read Config
	if (!Configuration::load("mod.ini"))
	{
		MessageBox(NULL, L"Can't find config file. Fix it, idiot! Make sure mod.ini is present in the mod directory.", NULL, MB_ICONERROR);
		exit(-1); // makes the game not run
	}

	ParamManager::applyPatches();

	ParamManager::addParam(&LandEnableMaxSlope, "LandEnableMaxSlope"); // Gonna stick with my addresses for everything else so it doesn't mess with the other 3D classic mod! (cuz this seems to effect both 2D and 3D)
	//ParamManager::addParam(&HighSpeedEffectMaxVelocity, "HighSpeedEffectMaxVelocity"); // Fix Spinny Effect For 3D (since modern doesn't have this it causes massive issues and crashes the game)
	ParamManager::addParam(&TargetFrontOffsetSpeedScale, "s_Param.TargetFrontOffsetSpeedScale"); // Add Outrunning Camera that replaces existing shittier one!
	//ParamManager::addParam(&GrindAccelerationForce, "GrindAccelerationForce"); // Apparantly modern sonic doesn't have this either but it's not super noticable so whatever. I guess grind rolling is more of a visual thing at this point.
	//ParamManager::addParam(&AccelerationForce, "AccelerationForce"); // Normal Acceleration Force. Used to fix rolling from slowing down too much when not holding left or right.

	if (Configuration::cs_model == "graphics_marza")
	{
		WRITE_MEMORY(0x015E2CfE, const char, "2"); // Changes CSeyewhiteL to CSeyewhite2 which stops the game from swapping these uvs to CSeyewhitedead
		WRITE_MEMORY(0x015E2D4E, const char, "2"); // Changes CSeyewhiteR to CSeyewhite2 which stops the game from swapping these uvs to CSeyewhitedead
		ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassic_MARZA_bms", { "bms", "bms100", "ev410", "ev411" }));
		//ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassic_MARZA_cmn100", { "cmn100" }));
	}
	else
	{
		ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassicOriginalAssets", { "SonicClassic", "cmn100" })); // FOR SOME REASON IT CRASHES FOR EVERYTHING BUT BALL ANI?
		ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassicOriginalAssets2", { "SonicClassic", "cmn100", "pam_cmn" })); // PUT BALL ANI HERE ONLY CUZ IT NEEDS PAM TO SHOW UP?

		//if (Configuration::little_tweaks == "graphics_little_touches")
		//{
		//	ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("MetalSonicFix", { "ev410" })); // Fix classic metal sonic when using vanilla / custom models
		//}
	}

	// Have to override for pam
	if (Configuration::sound_style == "sound_classic")
	{
		ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassic_Sound_pam", { "SonicClassicPam" }));
	}

	// Do this regardless.
	//if (Configuration::sound_style == "sound_classic")
	//{
	//	ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassicSounds", { "ghz100" }));
	//}

	// Do this so we don't replace modern's so it's more compatible with other mods.
	// Sadly doesn't seem to work?
	//ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassicPam_CSIM", { "pam000" }));

	// Not needed
	//if (Configuration::sound_style == "sound_classic")
	//{
	//	ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("cmn100_classic_sounds", { "cmn100" }));
	//}

	// It works in most ways but its too much trouble fixing all the issues that come with doing this when I already have a way of loading the assets as a config option.
	// This will definitly be better for certain things tho.
	//if (Configuration::cs_model == "graphics_marza")
	//{
		//ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("SonicClassic_MARZA", { "SonicClassic", "cmn100", "pam_cmn", "evSonicClassic" }));
	//}

	// Didn't work for certain things to the point that I might as well do it the other way.
	//ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("ssz_and_knuckles", { "ssz_cmn", "ssz100", "ssz200" }));
	// 	Can't get skeleton to work.
	//ArchiveTreePatcher::m_archiveDependencies.push_back(ArchiveDependency("metal_sonic_boss", { "bms", "bms001", "blb" }));

	ArchiveTreePatcher::applyPatches();
	AnimationSetPatcher::applyPatches();

	// Enable Debug Console
	//AllocConsole();
	//freopen("CONIN$", "r", stdin);
	//freopen("CONOUT$", "w", stdout);
	//freopen("CONOUT$", "w", stderr);
	//printf("Console Enabled!");
			
}

//extern "C" __declspec(dllexport) void PostInit() 
//{
//
//	MessageBox(nullptr, TEXT("This mod is in work in progress"), TEXT("Modren Sonic Spindash"), MB_ICONERROR);
//}

EXPORT OnFrame()
{	
	ResetFlags();
}