/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#ifndef __BUFFEROBJECT_H__
#define __BUFFEROBJECT_H__

/*
================================================================================================

	Buffer Objects

================================================================================================
*/

#ifdef DOOM3_VULKAN
#include <vulkan/vulkan.h>
#endif

class idIndexBuffer;

enum bufferMapType_t {
	BM_READ,			// map for reading
	BM_WRITE			// map for writing
};

// Returns all targets to virtual memory use instead of buffer object use.
// Call this before doing any conventional buffer reads, like screenshots.
void UnbindBufferObjects();

class idBufferObject
{
public:
	idBufferObject();
	~idBufferObject();

	// Allocate or free the buffer.
	virtual bool				AllocBufferObject( const void * data, int allocSize )=0;
	virtual void				FreeBufferObject()=0;

	// Make this buffer a reference to another buffer.
	virtual void				Reference( const idBufferObject & other )=0;
	virtual void				Reference( const idBufferObject & other, int refOffset, int refSize )=0;

	// Copies data to the buffer. 'size' may be less than the originally allocated size.
	virtual void				Update( const void * data, int updateSize ) const=0;

	virtual void *				MapBuffer( bufferMapType_t mapType ) const=0;
	idDrawVert *		MapVertexBuffer( bufferMapType_t mapType ) const { return static_cast< idDrawVert * >( MapBuffer( mapType ) ); }
	virtual void				UnmapBuffer() const=0;
	bool				IsMapped() const { return ( size & MAPPED_FLAG ) != 0; }

	int					GetSize() const { return ( size & ~MAPPED_FLAG ); }
	int					GetAllocedSize() const { return ( ( size & ~MAPPED_FLAG ) + 15 ) & ~15; }
	int					GetOffset() const { return ( offsetInOtherBuffer & ~OWNS_BUFFER_FLAG ); }
	void*				GetAPIObject() const { return apiObject; }

	virtual void				Sync() {};
	
	// sizeof() confuses typeinfo...
	static const int	MAPPED_FLAG			= 1 << ( 4 /* sizeof( int ) */ * 8 - 1 );
	static const int	OWNS_BUFFER_FLAG	= 1 << ( 4 /* sizeof( int ) */ * 8 - 1 );

protected:
	virtual void				ClearWithoutFreeing()=0;
	void				SetMapped() const { const_cast< int & >( size ) |= MAPPED_FLAG; }
	void				SetUnmapped() const { const_cast< int & >( size ) &= ~MAPPED_FLAG; }
	bool				OwnsBuffer() const { return ( ( offsetInOtherBuffer & OWNS_BUFFER_FLAG ) != 0 ); }


	int					size;					// size in bytes
	int					offsetInOtherBuffer;	// offset in bytes
	void*				apiObject;
};

#ifdef DOOM3_VULKAN
/*
================================================
idBufferObjectVk
================================================
*/
class idBufferObjectVk : public idBufferObject
{
public:
	idBufferObjectVk();
	~idBufferObjectVk();

	// Allocate or free the buffer.
	virtual bool				AllocBufferObject( const void * data, int allocSize ) override;
	virtual void				FreeBufferObject() override;

	// Make this buffer a reference to another buffer.
	virtual void				Reference( const idBufferObject & other ) override;
	virtual void				Reference( const idBufferObject & other, int refOffset, int refSize ) override;

	// Copies data to the buffer. 'size' may be less than the originally allocated size.
	virtual void				Update( const void * data, int updateSize ) const override;

	virtual void *				MapBuffer( bufferMapType_t mapType ) const override;

	virtual void				UnmapBuffer() const override;

	virtual void				Sync() override;

	VkBuffer GetBuffer() const { return buffer; }

protected:
	virtual void				ClearWithoutFreeing() override;

	VkDeviceMemory stagingMemory;
	VkDeviceMemory memory;

	VkBuffer stagingBuffer;
	VkBuffer buffer;

	VkFlags bufferCreateFlags = 0;

	DISALLOW_COPY_AND_ASSIGN( idBufferObjectVk );
};

/*
================================================
idVertexBufferVk
================================================
*/
class idVertexBufferVk : public idBufferObjectVk
{
public:
	idVertexBufferVk() : idBufferObjectVk() 
	{
		bufferCreateFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
};

/*
================================================
idIndexBufferVk
================================================
*/
class idIndexBufferVk : public idBufferObjectVk
{
public:
	idIndexBufferVk() : idBufferObjectVk()
	{
		bufferCreateFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
};

/*
================================================
idJointBufferVk
================================================
*/
class idJointBufferVk : public idBufferObjectVk
{
public:
	idJointBufferVk() : idBufferObjectVk()
	{
		bufferCreateFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}

private:
	int numJoints;
};
#endif

/*
================================================
idVertexBuffer
================================================
*/
class idVertexBuffer : public idBufferObject {
public:
						idVertexBuffer();
						~idVertexBuffer();

	// Allocate or free the buffer.
	bool				AllocBufferObject( const void * data, int allocSize );
	void				FreeBufferObject();

	// Make this buffer a reference to another buffer.
	void				Reference( const idBufferObject & other );
	void				Reference( const idBufferObject & other, int refOffset, int refSize );

	// Copies data to the buffer. 'size' may be less than the originally allocated size.
	void				Update( const void * data, int updateSize ) const;

	void *				MapBuffer( bufferMapType_t mapType ) const;
	idDrawVert *		MapVertexBuffer( bufferMapType_t mapType ) const { return static_cast< idDrawVert * >( MapBuffer( mapType ) ); }
	void				UnmapBuffer() const;


private:

	void				ClearWithoutFreeing();

	DISALLOW_COPY_AND_ASSIGN( idVertexBuffer );
};

/*
================================================
idIndexBuffer
================================================
*/
class idIndexBuffer : public idBufferObject {
public:
						idIndexBuffer();
						~idIndexBuffer();

	// Allocate or free the buffer.
	bool				AllocBufferObject( const void * data, int allocSize );
	void				FreeBufferObject();

	// Make this buffer a reference to another buffer.
	void				Reference( const idBufferObject & other );
	void				Reference( const idBufferObject & other, int refOffset, int refSize );

	// Copies data to the buffer. 'size' may be less than the originally allocated size.
	void				Update( const void * data, int updateSize ) const;

	void *				MapBuffer( bufferMapType_t mapType ) const;
	void				UnmapBuffer() const;


private:

	void				ClearWithoutFreeing();

	DISALLOW_COPY_AND_ASSIGN( idIndexBuffer );
};

/*
================================================
idJointBuffer

IMPORTANT NOTICE: on the PC, binding to an offset in uniform buffer objects 
is limited to GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, which is 256 on current nvidia cards,
so joint offsets, which are multiples of 48 bytes, must be in multiples of 16 = 768 bytes.
================================================
*/
class idJointBuffer : public idBufferObject {
public:
						idJointBuffer();
						~idJointBuffer();

	// Allocate or free the buffer.
	bool				AllocBufferObject( const void * joints, int numBytes );
	void				FreeBufferObject();

	// Make this buffer a reference to another buffer.
	void				Reference( const idBufferObject & other );
	void				Reference( const idBufferObject & other, int jointRefOffset, int numRefJoints );

	// Copies data to the buffer. 'numJoints' may be less than the originally allocated size.
	void				Update( const void * joints, int numUpdateJoints ) const;

	void *				MapBuffer( bufferMapType_t mapType ) const;
	void				UnmapBuffer() const;

	int					GetNumJoints() const { return numJoints; }

	void				Swap( idJointBuffer & other );

private:
	int					numJoints;

	void				ClearWithoutFreeing();

	DISALLOW_COPY_AND_ASSIGN( idJointBuffer );
};

#endif // !__BUFFEROBJECT_H__
