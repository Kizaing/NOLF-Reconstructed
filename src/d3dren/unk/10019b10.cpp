// d3d.ren unk/10019b10 (0x10019b10-0x1001a380): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// one function: D3DAppErrorToString (DirectX SDK sample-framework text table: "No error.", D3DERR_*). Probably a C object
// (sample d3dapp is C). Name d3dapp (names_proposal) violates link order; no file name evidence.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren sys/d3d/d3d_init: device bring-up, the DDERR/D3DERR text function, the device capability dump and the RenderStruct
// context / 3D-frame slots (0x10019b10-0x1001bf10).  Jupiter's descendants of this code: render_a/src/sys/d3d/d3d_init.cpp and
// d3d_device.cpp (CD3D_Device).  /O2 /Ob2 object (16-byte aligned functions): the module default flags.
// FLAGS: /O2 /Ob2
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/lightmap.h"		// RenderContext
#include "pixelformat.h"			// PFormat

// ---- DDERR / D3DERR text -------------------------------------------------------------------------------------------------
// NAME: D3DAppErrorToString: the function of that name in the DirectX SDK d3dapp sample code (error code to description text);
// medium confidence (name from memory of the sample, shape and strings agree: one switch over DDERR_/D3DERR_ codes).
// FUNCTION: D3DREN 0x10019b10
char *D3DAppErrorToString(HRESULT hr)
{
	switch (hr)
	{
	case E_OUTOFMEMORY:
		return "DirectDraw does not have enough memory to perform the operation.";
	case E_FAIL:
		return "Generic failure.";
	case E_NOTIMPL:
		return "Action not supported.";
	case E_INVALIDARG:
		return "One or more of the parameters passed to the function are incorrect.";
	case DDERR_CANNOTDETACHSURFACE:
		return "This surface can not be detached from the requested surface.";
	case DDERR_CANNOTATTACHSURFACE:
		return "This surface can not be attached to the requested surface.";
	case DDERR_ALREADYINITIALIZED:
		return "This object is already initialized.";
	case DDERR_CURRENTLYNOTAVAIL:
		return "Support is currently not available.";
	case DDERR_EXCEPTION:
		return "An exception was encountered while performing the requested operation.";
	case DDERR_HEIGHTALIGN:
		return "Height of rectangle provided is not a multiple of reqd alignment.";
	case DDERR_INCOMPATIBLEPRIMARY:
		return "Unable to match primary surface creation request with existing primary surface.";
	case DDERR_INVALIDCAPS:
		return "One or more of the caps bits passed to the callback are incorrect.";
	case DDERR_INVALIDCLIPLIST:
		return "DirectDraw does not support the provided cliplist.";
	case DDERR_INVALIDMODE:
		return "DirectDraw does not support the requested mode.";
	case DDERR_INVALIDOBJECT:
		return "DirectDraw received a pointer that was an invalid DIRECTDRAW object.";
	case DDERR_INVALIDPIXELFORMAT:
		return "The pixel format was invalid as specified.";
	case DDERR_INVALIDRECT:
		return "Rectangle provided was invalid.";
	case DDERR_LOCKEDSURFACES:
		return "Operation could not be carried out because one or more surfaces are locked.";
	case DDERR_NO3D:
		return "There is no 3D present.";
	case DDERR_NOALPHAHW:
		return "Operation could not be carried out because there is no alpha accleration hardware present or available.";
	case DDERR_NOCLIPLIST:
		return "No cliplist available.";
	case DDERR_NOCOLORCONVHW:
		return "Operation could not be carried out because there is no color conversion hardware present or available.";
	case DDERR_NOCOLORKEY:
		return "Surface doesn't currently have a color key";
	case DDERR_NOCOLORKEYHW:
		return "Operation could not be carried out because there is no hardware support of the destination color key.";
	case DDERR_NOCOOPERATIVELEVELSET:
		return "Create function called without DirectDraw object method SetCooperativeLevel being called.";
	case DDERR_NOEXCLUSIVEMODE:
		return "Operation requires the application to have exclusive mode but the application does not have exclusive mode.";
	case DDERR_NOFLIPHW:
		return "Flipping visible surfaces is not supported.";
	case DDERR_NOGDI:
		return "There is no GDI present.";
	case DDERR_NOMIRRORHW:
		return "Operation could not be carried out because there is no hardware present or available.";
	case DDERR_NOTFOUND:
		return "Requested item was not found.";
	case DDERR_NOOVERLAYHW:
		return "Operation could not be carried out because there is no overlay hardware present or available.";
	case DDERR_NORASTEROPHW:
		return "Operation could not be carried out because there is no appropriate raster op hardware present or available.";
	case DDERR_NOROTATIONHW:
		return "Operation could not be carried out because there is no rotation hardware present or available.";
	case DDERR_NOSTRETCHHW:
		return "Operation could not be carried out because there is no hardware support for stretching.";
	case DDERR_NOT4BITCOLOR:
		return "DirectDrawSurface is not in 4 bit color palette and the requested operation requires 4 bit color palette.";
	case DDERR_NOT4BITCOLORINDEX:
		return "DirectDrawSurface is not in 4 bit color index palette and the requested operation requires 4 bit color index palette.";
	case DDERR_NOT8BITCOLOR:
		return "DirectDrawSurface is not in 8 bit color mode and the requested operation requires 8 bit color.";
	case DDERR_NOTEXTUREHW:
		return "Operation could not be carried out because there is no texture mapping hardware present or available.";
	case DDERR_NOVSYNCHW:
		return "Operation could not be carried out because there is no hardware support for vertical blank synchronized operations.";
	case DDERR_NOZBUFFERHW:
		return "Operation could not be carried out because there is no hardware support for zbuffer blitting.";
	case DDERR_NOZOVERLAYHW:
		return "Overlay surfaces could not be z layered based on their BltOrder because the hardware does not support z layering of overlays.";
	case DDERR_OUTOFCAPS:
		return "The hardware needed for the requested operation has already been allocated.";
	case DDERR_OUTOFVIDEOMEMORY:
		return "DirectDraw does not have enough memory to perform the operation.";
	case DDERR_OVERLAYCANTCLIP:
		return "The hardware does not support clipped overlays.";
	case DDERR_OVERLAYCOLORKEYONLYONEACTIVE:
		return "Can only have ony color key active at one time for overlays.";
	case DDERR_COLORKEYNOTSET:
		return "No src color key specified for this operation.";
	case DDERR_PALETTEBUSY:
		return "Access to this palette is being refused because the palette is already locked by another thread.";
	case DDERR_SURFACEALREADYATTACHED:
		return "This surface is already attached to the surface it is being attached to.";
	case DDERR_SURFACEALREADYDEPENDENT:
		return "This surface is already a dependency of the surface it is being made a dependency of.";
	case DDERR_SURFACEBUSY:
		return "Access to this surface is being refused because the surface is already locked by another thread.";
	case DDERR_SURFACEISOBSCURED:
		return "Access to surface refused because the surface is obscured.";
	case DDERR_SURFACELOST:
		return "Access to this surface is being refused because the surface memory is gone. The DirectDrawSurface object representing this surface should have Restore called on it.";
	case DDERR_SURFACENOTATTACHED:
		return "The requested surface is not attached.";
	case DDERR_TOOBIGHEIGHT:
		return "Height requested by DirectDraw is too large.";
	case DDERR_TOOBIGSIZE:
		return "Size requested by DirectDraw is too large, but the individual height and width are OK.";
	case DDERR_TOOBIGWIDTH:
		return "Width requested by DirectDraw is too large.";
	case DDERR_UNSUPPORTEDFORMAT:
		return "FOURCC format requested is unsupported by DirectDraw.";
	case DDERR_UNSUPPORTEDMASK:
		return "Bitmask in the pixel format requested is unsupported by DirectDraw.";
	case DDERR_VERTICALBLANKINPROGRESS:
		return "Vertical blank is in progress.";
	case DDERR_BLTFASTCANTCLIP:
		return "Return if a clipper object is attached to the source surface passed into a BltFast call.";
	case DDERR_CANTCREATEDC:
		return "Windows can not create any more DCs.";
	case DDERR_CANTDUPLICATE:
		return "Can't duplicate primary & 3D surfaces, or surfaces that are implicitly created.";
	case DDERR_CLIPPERISUSINGHWND:
		return "An attempt was made to set a cliplist for a clipper object that is already monitoring an hwnd.";
	case DDERR_DIRECTDRAWALREADYCREATED:
		return "A DirectDraw object representing this driver has already been created for this process.";
	case DDERR_EXCLUSIVEMODEALREADYSET:
		return "An attempt was made to set the cooperative level when it was already set to exclusive.";
	case DDERR_HWNDALREADYSET:
		return "The CooperativeLevel HWND has already been set. It can not be reset while the process has surfaces or palettes created.";
	case DDERR_HWNDSUBCLASSED:
		return "HWND used by DirectDraw CooperativeLevel has been subclassed, this prevents DirectDraw from restoring state.";
	case DDERR_INVALIDDIRECTDRAWGUID:
		return "The GUID passed to DirectDrawCreate is not a valid DirectDraw driver identifier.";
	case DDERR_INVALIDPOSITION:
		return "Returned when the position of the overlay on the destination is no longer legal for that destination.";
	case DDERR_NOBLTHW:
		return "No blitter hardware present.";
	case DDERR_NOCLIPPERATTACHED:
		return "No clipper object attached to surface object.";
	case DDERR_NODC:
		return "No DC was ever created for this surface.";
	case DDERR_NODDROPSHW:
		return "No DirectDraw ROP hardware.";
	case DDERR_NODIRECTDRAWHW:
		return "A hardware-only DirectDraw object creation was attempted but the driver did not support any hardware.";
	case DDERR_NOEMULATION:
		return "Software emulation not available.";
	case DDERR_NOHWND:
		return "Clipper notification requires an HWND or no HWND has previously been set as the CooperativeLevel HWND.";
	case DDERR_NOOVERLAYDEST:
		return "Returned when GetOverlayPosition is called on an overlay that UpdateOverlay has never been called on to establish a destination.";
	case DDERR_NOPALETTEATTACHED:
		return "No palette object attached to this surface.";
	case DDERR_NOPALETTEHW:
		return "No hardware support for 16 or 256 color palettes.";
	case DDERR_NOTAOVERLAYSURFACE:
		return "Returned when an overlay member is called for a non-overlay surface.";
	case DDERR_NOTFLIPPABLE:
		return "An attempt has been made to flip a surface that is not flippable.";
	case DDERR_NOTLOCKED:
		return "Surface was not locked.  An attempt to unlock a surface that was not locked at all, or by this process, has been attempted.";
	case DDERR_OVERLAYNOTVISIBLE:
		return "Returned when GetOverlayPosition is called on a hidden overlay.";
	case DDERR_PRIMARYSURFACEALREADYEXISTS:
		return "This process already has created a primary surface.";
	case DDERR_REGIONTOOSMALL:
		return "Region passed to Clipper::GetClipList is too small.";
	case DDERR_WASSTILLDRAWING:
		return "Informs DirectDraw that the previous Blt which is transfering information to or from this Surface is incomplete.";
	case DDERR_WRONGMODE:
		return "This surface can not be restored because it was created in a different mode.";
	case DDERR_XALIGN:
		return "Rectangle provided was not horizontally aligned on required boundary.";
	case DDERR_IMPLICITLYCREATED:
		return "This surface can not be restored because it is an implicitly created surface.";
	case DDERR_NOTPALETTIZED:
		return "The surface being used is not a palette-based surface.";
	case D3DERR_BADMAJORVERSION:
		return "D3DERR_BADMAJORVERSION";
	case D3DERR_BADMINORVERSION:
		return "D3DERR_BADMINORVERSION";
	case D3DERR_EXECUTE_LOCKED:
		return "D3DERR_EXECUTE_LOCKED";
	case D3DERR_EXECUTE_NOT_LOCKED:
		return "D3DERR_EXECUTE_NOT_LOCKED";
	case D3DERR_EXECUTE_CREATE_FAILED:
		return "D3DERR_EXECUTE_CREATE_FAILED";
	case D3DERR_EXECUTE_DESTROY_FAILED:
		return "D3DERR_EXECUTE_DESTROY_FAILED";
	case D3DERR_EXECUTE_LOCK_FAILED:
		return "D3DERR_EXECUTE_LOCK_FAILED";
	case D3DERR_EXECUTE_UNLOCK_FAILED:
		return "D3DERR_EXECUTE_UNLOCK_FAILED";
	case D3DERR_EXECUTE_FAILED:
		return "D3DERR_EXECUTE_FAILED";
	case D3DERR_EXECUTE_CLIPPED_FAILED:
		return "D3DERR_EXECUTE_CLIPPED_FAILED";
	case D3DERR_TEXTURE_NO_SUPPORT:
		return "D3DERR_TEXTURE_NO_SUPPORT";
	case D3DERR_TEXTURE_CREATE_FAILED:
		return "D3DERR_TEXTURE_CREATE_FAILED";
	case D3DERR_TEXTURE_DESTROY_FAILED:
		return "D3DERR_TEXTURE_DESTROY_FAILED";
	case D3DERR_TEXTURE_LOCK_FAILED:
		return "D3DERR_TEXTURE_LOCK_FAILED";
	case D3DERR_TEXTURE_NOT_LOCKED:
		return "D3DERR_TEXTURE_NOT_LOCKED";
	case D3DERR_TEXTURE_LOCKED:
		return "D3DERR_TEXTURELOCKED";
	case D3DERR_TEXTURE_UNLOCK_FAILED:
		return "D3DERR_TEXTURE_UNLOCK_FAILED";
	case D3DERR_TEXTURE_LOAD_FAILED:
		return "D3DERR_TEXTURE_LOAD_FAILED";
	case D3DERR_MATRIX_CREATE_FAILED:
		return "D3DERR_MATRIX_CREATE_FAILED";
	case D3DERR_MATRIX_DESTROY_FAILED:
		return "D3DERR_MATRIX_DESTROY_FAILED";
	case D3DERR_MATRIX_SETDATA_FAILED:
		return "D3DERR_MATRIX_SETDATA_FAILED";
	case D3DERR_SETVIEWPORTDATA_FAILED:
		return "D3DERR_SETVIEWPORTDATA_FAILED";
	case D3DERR_MATERIAL_CREATE_FAILED:
		return "D3DERR_MATERIAL_CREATE_FAILED";
	case D3DERR_MATERIAL_DESTROY_FAILED:
		return "D3DERR_MATERIAL_DESTROY_FAILED";
	case D3DERR_MATERIAL_SETDATA_FAILED:
		return "D3DERR_MATERIAL_SETDATA_FAILED";
	case D3DERR_LIGHT_SET_FAILED:
		return "D3DERR_LIGHT_SET_FAILED";
	default:
		return "Unrecognized error value.";
	case DD_OK:
		return "No error.";
	}
}

// ---- module init / term (called from d3d_Init / d3d_Term) ------------------------------------------------------------------
// d3d_surface.h declares a class with a static member whose definition consumes a static-initialiser number: include it after the
// ConVar definitions above so that their `_$E` numbers stay those of the exe.
#include "d3dren/d3d_surface.h"		// D3DShadowTextureFactory
