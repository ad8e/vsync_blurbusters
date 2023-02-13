#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
//copy pasted from the original source.
//https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/km/d3dkmthk.h
//https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/shared/d3dukmdt.h
//https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/shared/d3dkmdt.h

#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

typedef UINT D3DKMT_HANDLE;
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef long NTSTATUS;

typedef struct _D3DKMDT_VIDEO_PRESENT_SOURCE {
	// Unique ID used to reference the respective video present source by the miniport and the OS.
	D3DDDI_VIDEO_PRESENT_SOURCE_ID Id;

	// Other video present source descriptor properties go here.
	DWORD dwReserved;
} D3DKMDT_VIDEO_PRESENT_SOURCE;

typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT {
	D3DKMT_HANDLE hAdapter; // in: adapter handle
	D3DKMT_HANDLE hDevice; // in: device handle [Optional]
	D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; // in: adapter's VidPN Source ID
} D3DKMT_WAITFORVERTICALBLANKEVENT;

typedef struct _D3DKMT_GETSCANLINE {
	D3DKMT_HANDLE hAdapter; // in: Adapter handle
	D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; // in: Adapter's VidPN Source ID
	BOOLEAN InVerticalBlank; // out: Within vertical blank
	UINT ScanLine; // out: Current scan line
} D3DKMT_GETSCANLINE;

//msdn is completely wrong about this
typedef struct _D3DKMT_OPENADAPTERFROMHDC {
	HDC hDc; // in:  DC that maps to a single display
	D3DKMT_HANDLE hAdapter; // out: adapter handle
	LUID AdapterLuid; // out: adapter LUID
	D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; // out: VidPN source ID for that particular display
} D3DKMT_OPENADAPTERFROMHDC;

EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTOpenAdapterFromHdc(_Inout_ D3DKMT_OPENADAPTERFROMHDC*);
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTWaitForVerticalBlankEvent(_In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT*);
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTGetScanLine(_Inout_ D3DKMT_GETSCANLINE*);