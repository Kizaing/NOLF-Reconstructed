// Jupiter runtime/model/src/modelallocations.h, Talon version.
// Talon's header has 13 counts (no vertex animation or compressed animation data sizes).
#ifndef __MODELALLOCATIONS_H__
#define __MODELALLOCATIONS_H__

#ifndef __MODEL_H__
#include "model.h"
#endif

class ILTStream;

// --------------------------------------------------------------------------
// ModelAllocations
// Utility class that counts the size of a model.
// (The engine needs to know how big a model is going to be.)
// --------------------------------------------------------------------------
class ModelAllocations
{
public:
				ModelAllocations();			// 0x00459170

	// Clear everything.
	void		Clear();					// 0x00459180

	// Load/save.
	LTBOOL		Load(ILTStream &str);		// 0x004591b0

	// Fills in the number of bytes that need to be allocated for a single
	// block of memory for the model to load into.
	LTBOOL		CalcAllocationSize(uint32 &size);	// 0x00459270

public:
	uint32		m_nKeyFrames;		// 0x00 Number of keyframes.
	uint32		m_nParentAnims;		// 0x04 Number of animations (that come from us).
	uint32		m_nNodes;			// 0x08 Number of nodes.
	uint32		m_nPieces;			// 0x0c Number of pieces.
	uint32		m_nChildModels;		// 0x10 Number of child models (including the self child model).
	uint32		m_nTris;			// 0x14 Number of triangles.
	uint32		m_nVerts;			// 0x18 Number of vertices.
	uint32		m_nVertexWeights;	// 0x1c Number of vertex weights.
	uint32		m_nLODs;			// 0x20 Number of LODs.
	uint32		m_nSockets;			// 0x24 Number of sockets.
	uint32		m_nWeightSets;		// 0x28 Number of weight sets.
	uint32		m_nStrings;			// 0x2c How many strings we're allocating.
	uint32		m_StringLengths;	// 0x30 Sum of all string lengths (not including null terminator).
};

#endif
