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
#pragma hdrstop
#include "../idlib/precompiled.h"
#include "tr_local.h"

#ifdef DOOM3_VULKAN
#include <vulkan/vulkan.h>
#include "sys/win32/win_vkutil.h"
#include "tr_local.h"
extern VkDevice vkDevice;
#endif

idCVar r_showBuffers( "r_showBuffers", "0", CVAR_INTEGER, "" );


//static const GLenum bufferUsage = GL_STATIC_DRAW_ARB;
static const GLenum bufferUsage = GL_DYNAMIC_DRAW_ARB;

/*
==================
IsWriteCombined
==================
*/
bool IsWriteCombined( void * base ) {
	MEMORY_BASIC_INFORMATION info;
	SIZE_T size = VirtualQueryEx( GetCurrentProcess(), base, &info, sizeof( info ) );
	if ( size == 0 ) {
		DWORD error = GetLastError();
		error = error;
		return false;
	}
	bool isWriteCombined = ( ( info.AllocationProtect & PAGE_WRITECOMBINE ) != 0 );
	return isWriteCombined;
}



/*
================================================================================================

	Buffer Objects

================================================================================================
*/

/*
========================
UnbindBufferObjects
========================
*/
void UnbindBufferObjects() {
#ifdef DOOM3_OPENGL
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
#elif DOOM3_VULKAN
#endif
}

#ifdef ID_WIN_X86_SSE2_INTRIN

void CopyBuffer( byte * dst, const byte * src, int numBytes ) {
	assert_16_byte_aligned( dst );
	assert_16_byte_aligned( src );

	int i = 0;
	for ( ; i + 128 <= numBytes; i += 128 ) {
		__m128i d0 = _mm_load_si128( (__m128i *)&src[i + 0*16] );
		__m128i d1 = _mm_load_si128( (__m128i *)&src[i + 1*16] );
		__m128i d2 = _mm_load_si128( (__m128i *)&src[i + 2*16] );
		__m128i d3 = _mm_load_si128( (__m128i *)&src[i + 3*16] );
		__m128i d4 = _mm_load_si128( (__m128i *)&src[i + 4*16] );
		__m128i d5 = _mm_load_si128( (__m128i *)&src[i + 5*16] );
		__m128i d6 = _mm_load_si128( (__m128i *)&src[i + 6*16] );
		__m128i d7 = _mm_load_si128( (__m128i *)&src[i + 7*16] );
		_mm_stream_si128( (__m128i *)&dst[i + 0*16], d0 );
		_mm_stream_si128( (__m128i *)&dst[i + 1*16], d1 );
		_mm_stream_si128( (__m128i *)&dst[i + 2*16], d2 );
		_mm_stream_si128( (__m128i *)&dst[i + 3*16], d3 );
		_mm_stream_si128( (__m128i *)&dst[i + 4*16], d4 );
		_mm_stream_si128( (__m128i *)&dst[i + 5*16], d5 );
		_mm_stream_si128( (__m128i *)&dst[i + 6*16], d6 );
		_mm_stream_si128( (__m128i *)&dst[i + 7*16], d7 );
	}
	for ( ; i + 16 <= numBytes; i += 16 ) {
		__m128i d = _mm_load_si128( (__m128i *)&src[i] );
		_mm_stream_si128( (__m128i *)&dst[i], d );
	}
	for ( ; i + 4 <= numBytes; i += 4 ) {
		*(uint32 *)&dst[i] = *(const uint32 *)&src[i];
	}
	for ( ; i < numBytes; i++ ) {
		dst[i] = src[i];
	}
	_mm_sfence();
}

#else

void CopyBuffer( byte * dst, const byte * src, int numBytes ) {
	assert_16_byte_aligned( dst );
	assert_16_byte_aligned( src );
	memcpy( dst, src, numBytes );
}

#endif

/*
================================================================================================

	idVertexBuffer

================================================================================================
*/

/*
========================
idVertexBuffer::idVertexBuffer
========================
*/
idVertexBuffer::idVertexBuffer() {
	size = 0;
	offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	apiObject = NULL;

	SetUnmapped();
}

/*
========================
idVertexBuffer::~idVertexBuffer
========================
*/
idVertexBuffer::~idVertexBuffer() {
	FreeBufferObject();
}

/*
========================
idVertexBuffer::AllocBufferObject
========================
*/
bool idVertexBuffer::AllocBufferObject( const void * data, int allocSize ) {
	assert( apiObject == NULL );
	assert_16_byte_aligned( data );

	if ( allocSize <= 0 ) {
		idLib::Error( "idVertexBuffer::AllocBufferObject: allocSize = %i", allocSize );
	}

	size = allocSize;

	bool allocationFailed = false;

	int numBytes = GetAllocedSize();

	// clear out any previous error
	qglGetError();

	GLuint bufferObject = 0xFFFF;
	qglGenBuffersARB( 1, & bufferObject );
	if ( bufferObject == 0xFFFF ) {
		idLib::FatalError( "idVertexBuffer::AllocBufferObject: failed" );
	}
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, bufferObject );

	// these are rewritten every frame
	qglBufferDataARB( GL_ARRAY_BUFFER_ARB, numBytes, NULL, bufferUsage );
	apiObject = reinterpret_cast< void * >( bufferObject );

	GLenum err = qglGetError();
	if ( err == GL_OUT_OF_MEMORY ) {
		idLib::Warning( "idVertexBuffer::AllocBufferObject: allocation failed" );
		allocationFailed = true;
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "vertex buffer alloc %p, api %p (%i bytes)\n", this, GetAPIObject(), GetSize() );
	}

	// copy the data
	if ( data != NULL ) {
		Update( data, allocSize );
	}

	return !allocationFailed;
}

/*
========================
idVertexBuffer::FreeBufferObject
========================
*/
void idVertexBuffer::FreeBufferObject() {
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if ( OwnsBuffer() == false ) {
		ClearWithoutFreeing();
		return;
	}

	if ( apiObject == NULL ) {
		return;
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "vertex buffer free %p, api %p (%i bytes)\n", this, GetAPIObject(), GetSize() );
	}
	
	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglDeleteBuffersARB( 1, & bufferObject );

	ClearWithoutFreeing();
}

/*
========================
idVertexBuffer::Reference
========================
*/
void idVertexBuffer::Reference( const idBufferObject & other ) {
	const idVertexBuffer& vb = static_cast<const idVertexBuffer&>(other);

	assert( IsMapped() == false );
	//assert( vb.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up idDrawVerts
	assert( vb.GetAPIObject() != NULL );
	assert( vb.GetSize() > 0 );

	FreeBufferObject();
	size = vb.GetSize();						// this strips the MAPPED_FLAG
	offsetInOtherBuffer = vb.GetOffset();	// this strips the OWNS_BUFFER_FLAG
	apiObject = vb.apiObject;
	assert( OwnsBuffer() == false );
}

/*
========================
idVertexBuffer::Reference
========================
*/
void idVertexBuffer::Reference( const idBufferObject & other, int refOffset, int refSize ) {
	const idVertexBuffer& vb = static_cast<const idVertexBuffer&>(other);

	assert( IsMapped() == false );
	//assert( vb.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up idDrawVerts
	assert( vb.GetAPIObject() != NULL );
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= vb.GetSize() );

	FreeBufferObject();
	size = refSize;
	offsetInOtherBuffer = vb.GetOffset() + refOffset;
	apiObject = vb.apiObject;
	assert( OwnsBuffer() == false );
}

/*
========================
idVertexBuffer::Update
========================
*/
void idVertexBuffer::Update( const void * data, int updateSize ) const {
	assert( apiObject != NULL );
	assert( IsMapped() == false );
	assert_16_byte_aligned( data );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( updateSize > size ) {
		idLib::FatalError( "idVertexBuffer::Update: size overrun, %i > %i\n", updateSize, GetSize() );
	}

	int numBytes = ( updateSize + 15 ) & ~15;

	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, bufferObject );
	qglBufferSubDataARB( GL_ARRAY_BUFFER_ARB, GetOffset(), (GLsizeiptrARB)numBytes, data );

/*
	void * buffer = MapBuffer( BM_WRITE );
	CopyBuffer( (byte *)buffer + GetOffset(), (byte *)data, numBytes );
	UnmapBuffer();
*/
}

/*
========================
idVertexBuffer::MapBuffer
========================
*/
void * idVertexBuffer::MapBuffer( bufferMapType_t mapType ) const {
	assert( IsMapped() == false );

	void * buffer = NULL;

	assert( apiObject != NULL );
	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, bufferObject );
	if ( mapType == BM_READ ) {
		//buffer = qglMapBufferARB( GL_ARRAY_BUFFER_ARB, GL_READ_ONLY_ARB );
		buffer = qglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, GetAllocedSize(), GL_MAP_READ_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
		if ( buffer != NULL ) {
			buffer = (byte *)buffer + GetOffset();
		}
	} else if ( mapType == BM_WRITE ) {
		//buffer = qglMapBufferARB( GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB );
		buffer = qglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, GetAllocedSize(), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
		if ( buffer != NULL ) {
			buffer = (byte *)buffer + GetOffset();
		}
		assert( IsWriteCombined( buffer ) );
	} else {
		assert( false );
	}

	SetMapped();

	if ( buffer == NULL ) {
		idLib::FatalError( "idVertexBuffer::MapBuffer: failed" );
	}
	return buffer;
}

/*
========================
idVertexBuffer::UnmapBuffer
========================
*/
void idVertexBuffer::UnmapBuffer() const {
	assert( apiObject != NULL );
	assert( IsMapped() );

	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, bufferObject );
	if ( !qglUnmapBufferARB( GL_ARRAY_BUFFER_ARB ) ) {
		idLib::Printf( "idVertexBuffer::UnmapBuffer failed\n" );
	}

	SetUnmapped();
}


/*
========================
idVertexBuffer::ClearWithoutFreeing
========================
*/
void idVertexBuffer::ClearWithoutFreeing() {
	size = 0;
	offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	apiObject = NULL;
}

/*
================================================================================================

	idIndexBuffer

================================================================================================
*/

/*
========================
idIndexBuffer::idIndexBuffer
========================
*/
idIndexBuffer::idIndexBuffer() {
	size = 0;
	offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	apiObject = NULL;
	SetUnmapped();
}

/*
========================
idIndexBuffer::~idIndexBuffer
========================
*/
idIndexBuffer::~idIndexBuffer() {
	FreeBufferObject();
}

/*
========================
idIndexBuffer::AllocBufferObject
========================
*/
bool idIndexBuffer::AllocBufferObject( const void * data, int allocSize ) {
	assert( apiObject == NULL );
	assert_16_byte_aligned( data );

	if ( allocSize <= 0 ) {
		idLib::Error( "idIndexBuffer::AllocBufferObject: allocSize = %i", allocSize );
	}

	size = allocSize;

	bool allocationFailed = false;

	int numBytes = GetAllocedSize();

	// clear out any previous error
	qglGetError();

	GLuint bufferObject = 0xFFFF;
	qglGenBuffersARB( 1, & bufferObject );
	if ( bufferObject == 0xFFFF ) {
		GLenum error = qglGetError();
		idLib::FatalError( "idIndexBuffer::AllocBufferObject: failed - GL_Error %d", error );
	}
	qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, bufferObject );

	// these are rewritten every frame
	qglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, numBytes, NULL, bufferUsage );
	apiObject = reinterpret_cast< void * >( bufferObject );

	GLenum err = qglGetError();
	if ( err == GL_OUT_OF_MEMORY ) {
		idLib::Warning( "idIndexBuffer:AllocBufferObject: allocation failed" );
		allocationFailed = true;
	}


	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "index buffer alloc %p, api %p (%i bytes)\n", this, GetAPIObject(), GetSize() );
	}

	// copy the data
	if ( data != NULL ) {
		Update( data, allocSize );
	}

	return !allocationFailed;
}

/*
========================
idIndexBuffer::FreeBufferObject
========================
*/
void idIndexBuffer::FreeBufferObject() {
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if ( OwnsBuffer() == false ) {
		ClearWithoutFreeing();
		return;
	}

	if ( apiObject == NULL ) {
		return;
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "index buffer free %p, api %p (%i bytes)\n", this, GetAPIObject(), GetSize() );
	}

	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglDeleteBuffersARB( 1, & bufferObject );

	ClearWithoutFreeing();
}

/*
========================
idIndexBuffer::Reference
========================
*/
void idIndexBuffer::Reference( const idBufferObject & other ) {
	const idIndexBuffer& ib = static_cast<const idIndexBuffer &>(other);

	assert( IsMapped() == false );
	//assert( ib.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up triIndex_t
	assert( ib.GetAPIObject() != NULL );
	assert( ib.GetSize() > 0 );

	FreeBufferObject();
	size = ib.GetSize();						// this strips the MAPPED_FLAG
	offsetInOtherBuffer = ib.GetOffset();	// this strips the OWNS_BUFFER_FLAG
	apiObject = ib.apiObject;
	assert( OwnsBuffer() == false );
}

/*
========================
idIndexBuffer::Reference
========================
*/
void idIndexBuffer::Reference( const idBufferObject & other, int refOffset, int refSize ) {
	const idIndexBuffer& ib = static_cast<const idIndexBuffer &>(other);

	assert( IsMapped() == false );
	//assert( ib.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up triIndex_t
	assert( ib.GetAPIObject() != NULL );
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= ib.GetSize() );

	FreeBufferObject();
	size = refSize;
	offsetInOtherBuffer = ib.GetOffset() + refOffset;
	apiObject = ib.apiObject;
	assert( OwnsBuffer() == false );
}

/*
========================
idIndexBuffer::Update
========================
*/
void idIndexBuffer::Update( const void * data, int updateSize ) const {

	assert( apiObject != NULL );
	assert( IsMapped() == false );
	assert_16_byte_aligned( data );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( updateSize > size ) {
		idLib::FatalError( "idIndexBuffer::Update: size overrun, %i > %i\n", updateSize, GetSize() );
	}

	int numBytes = ( updateSize + 15 ) & ~15;

	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, bufferObject );
	qglBufferSubDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, GetOffset(), (GLsizeiptrARB)numBytes, data );

/*
	void * buffer = MapBuffer( BM_WRITE );
	CopyBuffer( (byte *)buffer + GetOffset(), (byte *)data, numBytes );
	UnmapBuffer();
*/
}

/*
========================
idIndexBuffer::MapBuffer
========================
*/
void * idIndexBuffer::MapBuffer( bufferMapType_t mapType ) const {

	assert( apiObject != NULL );
	assert( IsMapped() == false );

	void * buffer = NULL;

	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, bufferObject );
	if ( mapType == BM_READ ) {
		//buffer = qglMapBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, GL_READ_ONLY_ARB );
		buffer = qglMapBufferRange( GL_ELEMENT_ARRAY_BUFFER_ARB, 0, GetAllocedSize(), GL_MAP_READ_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
		if ( buffer != NULL ) {
			buffer = (byte *)buffer + GetOffset();
		}
	} else if ( mapType == BM_WRITE ) {
		//buffer = qglMapBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB );
		buffer = qglMapBufferRange( GL_ELEMENT_ARRAY_BUFFER_ARB, 0, GetAllocedSize(), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
		if ( buffer != NULL ) {
			buffer = (byte *)buffer + GetOffset();
		}
		assert( IsWriteCombined( buffer ) );
	} else {
		assert( false );
	}

	SetMapped();

	if ( buffer == NULL ) {
		idLib::FatalError( "idIndexBuffer::MapBuffer: failed" );
	}
	return buffer;
}

/*
========================
idIndexBuffer::UnmapBuffer
========================
*/
void idIndexBuffer::UnmapBuffer() const {
	assert( apiObject != NULL );
	assert( IsMapped() );

	GLuint bufferObject = reinterpret_cast< GLuint >( apiObject );
	qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, bufferObject );
	if ( !qglUnmapBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB ) ) {
		idLib::Printf( "idIndexBuffer::UnmapBuffer failed\n" );
	}

	SetUnmapped();
}

/*
========================
idIndexBuffer::ClearWithoutFreeing
========================
*/
void idIndexBuffer::ClearWithoutFreeing() {
	size = 0;
	offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	apiObject = NULL;
}

/*
================================================================================================

	idJointBuffer

================================================================================================
*/

/*
========================
idJointBuffer::idJointBuffer
========================
*/
idJointBuffer::idJointBuffer() {
	numJoints = 0;
	offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	apiObject = NULL;
	SetUnmapped();
}

/*
========================
idJointBuffer::~idJointBuffer
========================
*/
idJointBuffer::~idJointBuffer() {
	FreeBufferObject();
}

/*
========================
idJointBuffer::AllocBufferObject
========================
*/
bool idJointBuffer::AllocBufferObject( const void * joints, int jointBytes ) {
	assert( apiObject == NULL );
	assert_16_byte_aligned( joints );

	numJoints = jointBytes / sizeof(idJointMat);

	if ( numJoints <= 0 ) {
		idLib::Error( "idJointBuffer::AllocBufferObject: joints = %i", numJoints );
	}


	bool allocationFailed = false;

	const int numBytes = GetAllocedSize();

	GLuint buffer = 0;
	qglGenBuffersARB( 1, &buffer );
	qglBindBufferARB( GL_UNIFORM_BUFFER, buffer );
	qglBufferDataARB( GL_UNIFORM_BUFFER, numBytes, NULL, GL_STREAM_DRAW_ARB );
	qglBindBufferARB( GL_UNIFORM_BUFFER, 0);
	apiObject = reinterpret_cast< void * >( buffer );

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "joint buffer alloc %p, api %p (%i joints)\n", this, GetAPIObject(), GetNumJoints() );
	}

	// copy the data
	if ( joints != NULL ) {
		Update( joints, numJoints );
	}

	return !allocationFailed;
}

/*
========================
idJointBuffer::FreeBufferObject
========================
*/
void idJointBuffer::FreeBufferObject() {
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if ( OwnsBuffer() == false ) {
		ClearWithoutFreeing();
		return;
	}

	if ( apiObject == NULL ) {
		return;
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "joint buffer free %p, api %p (%i joints)\n", this, GetAPIObject(), GetNumJoints() );
	}

	GLuint buffer = reinterpret_cast< GLuint > ( apiObject );
	qglBindBufferARB( GL_UNIFORM_BUFFER, 0 );
	qglDeleteBuffersARB( 1, & buffer );

	ClearWithoutFreeing();
}

/*
========================
idJointBuffer::Reference
========================
*/
void idJointBuffer::Reference( const idBufferObject & other ) {
	const idJointBuffer& jb = static_cast<const idJointBuffer &>(other);

	assert( IsMapped() == false );
	assert( jb.IsMapped() == false );
	assert( jb.GetAPIObject() != NULL );
	assert( jb.GetNumJoints() > 0 );

	FreeBufferObject();
	numJoints = jb.GetNumJoints();			// this strips the MAPPED_FLAG
	offsetInOtherBuffer = jb.GetOffset();	// this strips the OWNS_BUFFER_FLAG
	apiObject = jb.apiObject;
	assert( OwnsBuffer() == false );
}

/*
========================
idJointBuffer::Reference
========================
*/
void idJointBuffer::Reference( const idBufferObject & other, int jointRefOffset, int numRefJoints ) {
	const idJointBuffer& jb = static_cast<const idJointBuffer &>(other);

	assert( IsMapped() == false );
	assert( jb.IsMapped() == false );
	assert( jb.GetAPIObject() != NULL );
	assert( jointRefOffset >= 0 );
	assert( numRefJoints >= 0 );
	assert( jointRefOffset + numRefJoints * sizeof( idJointMat ) <= jb.GetNumJoints() * sizeof( idJointMat ) );
	assert_16_byte_aligned( numRefJoints * 3 * 4 * sizeof( float ) );

	FreeBufferObject();
	numJoints = numRefJoints;
	offsetInOtherBuffer = jb.GetOffset() + jointRefOffset;
	apiObject = jb.apiObject;
	assert( OwnsBuffer() == false );
}

/*
========================
idJointBuffer::Update
========================
*/
void idJointBuffer::Update( const void * joints, int numUpdateJoints ) const {
	assert( apiObject != NULL );
	assert( IsMapped() == false );
	assert_16_byte_aligned( joints );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( numUpdateJoints > numJoints ) {
		idLib::FatalError( "idJointBuffer::Update: size overrun, %i > %i\n", numUpdateJoints, numJoints );
	}

	const int numBytes = numUpdateJoints * 3 * 4 * sizeof( float );

	qglBindBufferARB( GL_UNIFORM_BUFFER, reinterpret_cast< GLuint >( apiObject ) );
	qglBufferSubDataARB( GL_UNIFORM_BUFFER, GetOffset(), (GLsizeiptrARB)numBytes, joints );
}

/*
========================
idJointBuffer::MapBuffer
========================
*/
void * idJointBuffer::MapBuffer( bufferMapType_t mapType ) const {
	assert( IsMapped() == false );
	assert( mapType == BM_WRITE );
	assert( apiObject != NULL );

	int numBytes = GetAllocedSize();

	void * buffer = NULL;

	qglBindBufferARB( GL_UNIFORM_BUFFER, reinterpret_cast< GLuint >( apiObject ) );
	numBytes = numBytes;
	assert( GetOffset() == 0 );
	//buffer = qglMapBufferARB( GL_UNIFORM_BUFFER, GL_WRITE_ONLY_ARB );
	buffer = qglMapBufferRange( GL_UNIFORM_BUFFER, 0, GetAllocedSize(), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
	if ( buffer != NULL ) {
		buffer = (byte *)buffer + GetOffset();
	}

	SetMapped();

	if ( buffer == NULL ) {
		idLib::FatalError( "idJointBuffer::MapBuffer: failed" );
	}
	return  buffer;
}

/*
========================
idJointBuffer::UnmapBuffer
========================
*/
void idJointBuffer::UnmapBuffer() const {
	assert( apiObject != NULL );
	assert( IsMapped() );

	qglBindBufferARB( GL_UNIFORM_BUFFER, reinterpret_cast< GLuint >( apiObject ) );
	if ( !qglUnmapBufferARB( GL_UNIFORM_BUFFER ) ) {
		idLib::Printf( "idJointBuffer::UnmapBuffer failed\n" );
	}

	SetUnmapped();
}

/*
========================
idJointBuffer::ClearWithoutFreeing
========================
*/
void idJointBuffer::ClearWithoutFreeing() {
	numJoints = 0;
	offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	apiObject = NULL;
}

/*
========================
idJointBuffer::Swap
========================
*/
void idJointBuffer::Swap( idJointBuffer & other ) {
	// Make sure the ownership of the buffer is not transferred to an unintended place.
	assert( other.OwnsBuffer() == OwnsBuffer() );

	SwapValues( other.numJoints, numJoints );
	SwapValues( other.offsetInOtherBuffer, offsetInOtherBuffer );
	SwapValues( other.apiObject, apiObject );
}

idBufferObject::idBufferObject()
{
	SetUnmapped();
}

idBufferObject::~idBufferObject()
{
}

#ifdef DOOM3_VULKAN

idBufferObjectVk::idBufferObjectVk() : idBufferObject()
{
	buffer = stagingBuffer = VK_NULL_HANDLE;
	memory = stagingMemory = VK_NULL_HANDLE;
	size = 0;
}

idBufferObjectVk::~idBufferObjectVk()
{
	FreeBufferObject();
}

bool idBufferObjectVk::AllocBufferObject(const void * data, int allocSize){
	assert_16_byte_aligned( data );

	if ( allocSize <= 0 ) {
		idLib::Error( "idVertexBuffer::AllocBufferObject: allocSize = %i", allocSize );
	}

	size = allocSize;

	bool allocationFailed = false;

	int numBytes = GetAllocedSize();

	VkMemoryRequirements memReq;

	//Staging buffer
	VkBufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.size = allocSize;
	
	VkCheck(vkCreateBuffer(vkDevice, &info, nullptr, &stagingBuffer));
	vkGetBufferMemoryRequirements(vkDevice, stagingBuffer, &memReq);

	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReq.size;
	alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &stagingMemory));
	VkCheck(vkBindBufferMemory(vkDevice, stagingBuffer, stagingMemory, 0));

	//Now the main buffer
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | bufferCreateFlags;
	VkCheck(vkCreateBuffer(vkDevice, &info, nullptr, &buffer));
	vkGetBufferMemoryRequirements(vkDevice, buffer, &memReq);

	alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &memory));
	VkCheck(vkBindBufferMemory(vkDevice, buffer, memory, 0));

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "vertex buffer alloc %p, api %p (%i bytes)\n", this, "vk", GetSize() );
	}

	// copy the data
	if ( data != NULL ) {
		Update( data, allocSize );
	}

	return !allocationFailed;
}

void idBufferObjectVk::FreeBufferObject()
{
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if ( OwnsBuffer() == false ) {
		ClearWithoutFreeing();
		return;
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "vertex buffer free %p, api %p (%i bytes)\n", this, "vk", GetSize() );
	}
	
	vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
	vkDestroyBuffer(vkDevice, buffer, nullptr);
	vkFreeMemory(vkDevice, stagingMemory, nullptr);
	vkFreeMemory(vkDevice, memory, nullptr);
	buffer = stagingBuffer = VK_NULL_HANDLE;
	memory = stagingMemory = VK_NULL_HANDLE;

	ClearWithoutFreeing();
}

void idBufferObjectVk::Reference(const idBufferObject & other)
{
	//TODO
}

void idBufferObjectVk::Reference(const idBufferObject & other, int refOffset, int refSize)
{
	//TODO
}

void idBufferObjectVk::Update(const void * data, int updateSize) const
{
	assert( IsMapped() == false );
	assert_16_byte_aligned( data );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( updateSize > size ) {
		idLib::FatalError( "idVertexBuffer::Update: size overrun, %i > %i\n", updateSize, GetSize() );
	}

	int numBytes = ( updateSize + 15 ) & ~15;

	void* dst;
	VkCheck(vkMapMemory(vkDevice, stagingMemory, GetOffset(), numBytes, 0, &dst));
	memcpy(dst, data, numBytes);
	vkUnmapMemory(vkDevice, stagingMemory);
	
	VkCommandBuffer cmd = Vk_StartOneShotCommandBuffer();

	VkBufferCopy copy = {};
	copy.size = GetAllocedSize();
	copy.dstOffset = copy.srcOffset = GetOffset();

	vkCmdCopyBuffer(cmd, stagingBuffer, buffer, 1, &copy);
	
	Vk_SubmitOneShotCommandBuffer(cmd);
}

void * idBufferObjectVk::MapBuffer(bufferMapType_t mapType) const
{
	assert( IsMapped() == false );

	void * buffer = NULL;

	VkCheck(vkMapMemory(vkDevice, stagingMemory, 0, GetAllocedSize(), 0, &buffer));

	SetMapped();

	if ( buffer == NULL ) {
		idLib::FatalError( "idVertexBuffer::MapBuffer: failed" );
	}
	return buffer;
}

void idBufferObjectVk::UnmapBuffer() const
{
	assert( IsMapped() );

	vkUnmapMemory(vkDevice, stagingMemory);

	SetUnmapped();
}

void idBufferObjectVk::Sync()
{
	VkCommandBuffer cmd = Vk_StartOneShotCommandBuffer();

	VkBufferCopy copy = {};
	copy.size = GetAllocedSize();
	copy.dstOffset = copy.srcOffset = 0;

	vkCmdCopyBuffer(cmd, stagingBuffer, buffer, 1, &copy);
	
	Vk_SubmitOneShotCommandBuffer(cmd);
}

void idBufferObjectVk::ClearWithoutFreeing()
{
	buffer = stagingBuffer = VK_NULL_HANDLE;
	memory = stagingMemory = VK_NULL_HANDLE;
}

#endif
