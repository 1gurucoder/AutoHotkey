/*
AutoHotkey

Copyright 2003 Chris Mallett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef hotkey_h
#define hotkey_h

#include "keyboard.h"
#include "script.h"  // For which label (and in turn which line) in the script to jump to.
EXTERN_SCRIPT;  // For g_script.

// Due to control/alt/shift modifiers, quite a lot of hotkey combinations are possible, so support any
// conceivable use.  Note: Increasing this value will increase the memory required (i.e. any arrays
// that use this value):
#define MAX_HOTKEYS 512

// Note: 0xBFFF is the largest ID that can be used with RegisterHotkey().
// But further limit this to 0x3FFF (16,383) so that the two highest order bits
// are reserved for our other uses:
#define HOTKEY_NO_SUPPRESS             0x8000
#define HOTKEY_FUTURE_USE              0x4000
#define HOTKEY_ID_MASK                 0x3FFF
#define HOTKEY_ID_INVALID              HOTKEY_ID_MASK
#define HOTKEY_ID_ALT_TAB              0x3FFE
#define HOTKEY_ID_ALT_TAB_SHIFT        0x3FFD
#define HOTKEY_ID_ALT_TAB_MENU         0x3FFC
#define HOTKEY_ID_ALT_TAB_AND_MENU     0x3FFB
#define HOTKEY_ID_ALT_TAB_MENU_DISMISS 0x3FFA
#define HOTKEY_ID_MAX                  0x3FF9

#define COMPOSITE_DELIMITER " & "
#define COMPOSITE_DELIMITER_LENGTH 3

// Smallest workable size: to save mem in some large arrays that use this:
typedef USHORT HotkeyIDType;
typedef HotkeyIDType HookActionType;
enum HotkeyTypes {HK_UNDETERMINED, HK_NORMAL, HK_KEYBD_HOOK, HK_MOUSE_HOOK, HK_BOTH_HOOKS, HK_JOYSTICK};


class Hotkey
{
private:
	// These are done as static, rather than having an outer-class to contain all the hotkeys, because
	// the hotkey ID is used as the array index for performance reasons.  Having an outer class implies
	// the potential future use of more than one set of hotkeys, which could still be implemented
	// within static data and methods to retain the indexing/performance method:
	static bool sHotkeysAreLocked; // Whether the definition-stage of hotkey creation is finished.
	static HookType sWhichHookNeeded;
	static HookType sWhichHookAlways;
	static HookType sWhichHookActive;
	static DWORD sTimePrev;
	static DWORD sTimeNow;
	static Hotkey *shk[MAX_HOTKEYS];
	static HotkeyIDType sNextID;

	HotkeyIDType mID;  // Must be unique for each hotkey of a given thread.
	vk_type mVK; // virtual-key code, e.g. VK_TAB, VK_LWIN, VK_LMENU, VK_APPS, VK_F10.  If zero, use sc below.
	sc_type mSC; // Scan code.  All vk's have a scan code, but not vice versa.
	mod_type mModifiers;  // MOD_ALT, MOD_CONTROL, MOD_SHIFT, MOD_WIN, or some additive or bitwise-or combination of these.
	modLR_type mModifiersLR;  // Left-right centric versions of the above.
	bool mAllowExtraModifiers;  // False if the hotkey should not fire when extraneous modifiers are held down.
	#define NO_SUPPRESS_SUFFIX 0x01 // Bitwise: Bit #1
	#define NO_SUPPRESS_PREFIX 0x02 // Bitwise: Bit #2
	#define NO_SUPPRESS_NEXT_UP_EVENT 0x04 // Bitwise: Bit #3
	UCHAR mNoSuppress;  // Normally 0, but can be overridden by using the hotkey tilde (~) prefix.
	vk_type mModifierVK; // Any other virtual key that must be pressed down in order to activate "vk" itself.
	sc_type mModifierSC; // If mModifierVK is zero, this scan code, if non-zero, will be used as the modifier.
	modLR_type mModifiersConsolidated; // The combination of mModifierVK, mModifierSC, mModifiersLR, modifiers
	HotkeyTypes mType;
	bool mIsRegistered;  // Whether this hotkey has been successfully registered.
	HookActionType mHookAction;
	Label *mJumpToLabel;
	UCHAR mExistingThreads, mMaxThreads;
	bool mMaxThreadsBuffer;
	bool mRunAgainAfterFinished;
	DWORD mRunAgainTime;
	bool mConstructedOK;

	// Something in the compiler is currently preventing me from using HookType in place of UCHAR:
	friend UCHAR ChangeHookState(Hotkey *aHK[], int aHK_count, UCHAR aWhichHook, UCHAR aWhichHookAlways, bool aWarnIfHooksAlreadyInstalled
		, bool aActivateOnlySuspendHotkeys);

	ResultType TextInterpret();
	char *TextToModifiers(char *aText);
	ResultType TextToKey(char *aText, bool aIsModifier);

	bool IsExemptFromSuspend()
	{
		// Hotkey subroutines whose first line is the Suspend command are exempt from
		// being suspended themselves except when their first parameter is the literal
		// word "on":
		return mJumpToLabel && mJumpToLabel->mJumpToLine->mActionType == ACT_SUSPEND
			&& (!mJumpToLabel->mJumpToLine->mArgc || mJumpToLabel->mJumpToLine->ArgHasDeref(1)
				|| stricmp(mJumpToLabel->mJumpToLine->mArg[0].text, "on"));
	}

	bool PerformIsAllowed()
	{
		// For now, attempts to launch another simultaneous instance of this subroutine
		// are ignored if MaxThreadsPerHotkey (for this particular hotkey) has been reached.
		// In the future, it might be better to have this user-configurable, i.e. to devise
		// some way for the hotkeys to be kept queued up so that they take effect only when
		// the number of currently active threads drops below the max.  But doing such
		// might make "infinite key loops" harder to catch because the rate of incoming hotkeys
		// would be slowed down to prevent the subroutines from running concurrently:
		return mExistingThreads < mMaxThreads
			|| (mJumpToLabel && ACT_IS_ALWAYS_ALLOWED(mJumpToLabel->mJumpToLine->mActionType));
	}

	ResultType Perform() // Returns OK or FAIL.
	{
		if (!PerformIsAllowed())
			return FAIL;
		ResultType result;
		++mExistingThreads;  // This is the thread count for this particular hotkey only.
		for (;;)
		{
			// This is stored as an attribute of the script (semi-globally) rather than passed
			// as a param to ExecUntil (and from their on to any calls to SendKeys() that it
			// makes) because it's possible for SendKeys to be called asynchronously, namely
			// by a timed subroutine, while #HotkeyModifierTimeout is still in effect,
			// in which case we would want SendKeys() to take not of these modifiers even
			// if it was called from an ExecUntil() other than ours here:
			g_script.mThisHotkeyModifiersLR = mModifiersConsolidated;
			result = mJumpToLabel->mJumpToLine->ExecUntil(UNTIL_RETURN);
			if (result == FAIL)
			{
				mRunAgainAfterFinished = false;  // Ensure this is reset due to the error.
				break;
			}
			if (mRunAgainAfterFinished)
			{
				// But MsgSleep() can change it back to true again, when called by the above call
				// to ExecUntil(), to keep it auto-repeating:
				mRunAgainAfterFinished = false;  // i.e. this "run again" ticket has now been used up.
				// And if it was posted too long ago, don't do it.  This is because most users wouldn't
				// want a buffered hotkey to stay pending for a long time after it was pressed, because
				// that might lead to unexpected behavior:
				if (GetTickCount() - mRunAgainTime > 1000)
					break;
				// else don't break, but continue the loop until the flag becomes false.
			}
			else
				break;
		}
		--mExistingThreads;
		return (result == FAIL) ? FAIL : OK;
	}

	ResultType Register();
	ResultType Unregister();
	static ResultType AllDestruct();

	void *operator new(size_t aBytes) {return SimpleHeap::Malloc(aBytes);}
	void *operator new[](size_t aBytes) {return SimpleHeap::Malloc(aBytes);}
	void operator delete(void *aPtr) {}
	void operator delete[](void *aPtr) {}

	// For now, constructor & destructor are private so that only static methods can create new
	// objects.  This allow proper tracking of which OS hotkey IDs have been used.
	Hotkey(HotkeyIDType aID, Label *aJumpToLabel, HookActionType aHookAction);
	~Hotkey() {if (mIsRegistered) Unregister();}
public:
	// Make sHotkeyCount an alias for sNextID.  Make it const to enforce modifying the value in only one way:
	static const HotkeyIDType &sHotkeyCount;
	static bool sJoystickHasHotkeys[MAX_JOYSTICKS];
	static DWORD sJoyHotkeyCount;

	static void AllDestructAndExit(int exit_code);
	static ResultType AddHotkey(Label *aJumpToLabel, HookActionType aHookAction);
	static ResultType PerformID(HotkeyIDType aHotkeyID);
	static void TriggerJoyHotkeys(int aJoystickID, DWORD aButtonsNewlyDown);
	static ResultType AllDeactivate(bool aExcludeSuspendHotkeys = false);
	static void AllActivate();
	static void RequireHook(HookType aWhichHook) {sWhichHookAlways |= aWhichHook;}
	static HookType HookIsActive() {return sWhichHookActive;} // Returns bitwise values: HOOK_MOUSE, HOOK_KEYBD.

	static void InstallKeybdHook()
	{
		sWhichHookAlways |= HOOK_KEYBD;
		sWhichHookActive = ChangeHookState(shk, sHotkeyCount, sWhichHookNeeded, sWhichHookAlways, false, false);
	}

	static bool PerformIsAllowed(HotkeyIDType aHotkeyID)
	{
		return (aHotkeyID >= 0 && aHotkeyID < sHotkeyCount) ? shk[aHotkeyID]->PerformIsAllowed() : false;
	}

	static void RunAgainAfterFinished(HotkeyIDType aHotkeyID)
	{
		if (aHotkeyID >= 0 && aHotkeyID < sHotkeyCount && shk[aHotkeyID]->mMaxThreadsBuffer) // short-circuit order
		{
			shk[aHotkeyID]->mRunAgainAfterFinished = true;
			shk[aHotkeyID]->mRunAgainTime = GetTickCount();
			// Above: The time this event was buffered, to make sure it doesn't get too old.
		}
	}

	static void ResetRunAgainAfterFinished()  // For all hotkeys.
	{
		for (int i = 0; i < sHotkeyCount; ++i)
			shk[i]->mRunAgainAfterFinished = false;
	}

	static char GetType(HotkeyIDType aHotkeyID)
	{
		return (aHotkeyID >= 0 && aHotkeyID < sHotkeyCount) ? shk[aHotkeyID]->mType : -1;
	}

	static Label *GetLabel(HotkeyIDType aHotkeyID)
	{
		return (aHotkeyID >= 0 && aHotkeyID < sHotkeyCount) ? shk[aHotkeyID]->mJumpToLabel : NULL;
	}
	static ActionTypeType GetTypeOfFirstLine(HotkeyIDType aHotkeyID)
	{
		Label *label = GetLabel(aHotkeyID);
		if (!label)
			return ACT_INVALID;
		return label->mJumpToLine->mActionType;
	}

	static int FindHotkeyBySC(sc2_type aSC2, mod_type aModifiers, modLR_type aModifiersLR);
	static int FindHotkeyWithThisModifier(vk_type aVK, sc_type aSC);
	static int FindHotkeyContainingModLR(modLR_type aModifiersLR);  //, int hotkey_id_to_omit);

	static char *ListHotkeys(char *aBuf, size_t aBufSize);
	char *ToText(char *aBuf, size_t aBufSize, bool aAppendNewline);
};
#endif
