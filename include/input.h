// Talon input manager (Jupiter runtime/kernel/src/sys/win/input.h). Talon has no
// GetDeviceObjectName, so IsDeviceEnabled sits at 0x54.
#ifndef __INPUT_H__
#define __INPUT_H__

#include "ltbasedefs.h"

struct ConsoleState;

class InputMgr
{
public:
	LTBOOL		(*Init)(InputMgr *pMgr, ConsoleState *pState);						// 0x00
	void		(*Term)(InputMgr *pMgr);											// 0x04
	LTBOOL		(*IsInitted)(InputMgr *pMgr);										// 0x08
	void		(*ListDevices)(InputMgr *pMgr);										// 0x0c
	long		(*PlayJoystickEffect)(InputMgr *pMgr, char *strEffectName, float x, float y);	// 0x10
	void		(*ReadInput)(InputMgr *pMgr, uint8 *pActionsOn, float axisOffsets[3]);	// 0x14
	LTBOOL		(*FlushInputBuffers)(InputMgr *pMgr);								// 0x18
	LTRESULT	(*ClearInput)();													// 0x1c
	void		(*AddAction)(InputMgr *pMgr, char *pActionName, int actionCode);		// 0x20
	LTBOOL		(*EnableDevice)(InputMgr *pMgr, char *pDeviceName);					// 0x24
	LTBOOL		(*ClearBindings)(InputMgr *pMgr, char *pDeviceName, char *pTriggerName);	// 0x28
	LTBOOL		(*AddBinding)(InputMgr *pMgr, char *pDeviceName, char *pTriggerName,
					char *pActionName, float rangeLow, float rangeHigh);			// 0x2c
	LTBOOL		(*ScaleTrigger)(InputMgr *pMgr, char *pDeviceName, char *pTriggerName, float scale);	// 0x30
	DeviceBinding*	(*GetDeviceBindings)(uint32 nDevice);							// 0x34
	void		(*FreeDeviceBindings)(DeviceBinding *pBindings);					// 0x38
	LTBOOL		(*StartDeviceTrack)(InputMgr *pMgr, uint32 nDevices, uint32 nBufferSize);	// 0x3c
	LTBOOL		(*TrackDevice)(DeviceInput *pInputArray, uint32 *pnInOut);			// 0x40
	LTBOOL		(*EndDeviceTrack)();												// 0x44
	DeviceObject*	(*GetDeviceObjects)(uint32 nDeviceFlags);						// 0x48
	void		(*FreeDeviceObjects)(DeviceObject *pList);							// 0x4c
	LTBOOL		(*GetDeviceName)(uint32 nDeviceType, char *pStrBuffer, uint32 nBufferSize);	// 0x50
	LTBOOL		(*IsDeviceEnabled)(char *pDeviceName);								// 0x54
};

#endif  // __INPUT_H__
