/***************************************************************************
                          io.cpp  -  GDL classes for file io
                             -------------------
    begin                : July 22 2002
    copyright            : (C) 2002 by Marc Schellens
    email                : m_schellens@users.sf.net
 ***************************************************************************/

/* *************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "includefirst.hpp"

#include <cstdio> // std::remove(...)

#include "objects.hpp"
#include "io.hpp"

using namespace std;

const string StreamInfo( ios* searchStream)
{
  if( searchStream == &cin) return "Unit: 0, <stdin>";
  if( searchStream == &cout) return "Unit: -1, <stdout>";
  if( searchStream == &cerr) return "Unit: -2, <stderr>";
  for( SizeT i=0; i<fileUnits.size(); i++)
    {
      if( fileUnits[ i].fStream == searchStream)
	{
	  return "Unit: "+i2s(i+1)+", File: "+fileUnits[ i].Name();
	}
    }
  return "Internal error: Stream not found.";
}

void GDLStream::Open( const string& name_,
		      ios_base::openmode mode_,
		      bool swapEndian_, bool dOC, bool xdr_, 
		      SizeT width_,
		      bool f77_, bool compress_)
{
 
  f77 = f77_;

//   if( f77_)
//     throw GDLException("F77_UNFORMATTED format not supported.");
    
  name=name_;

  compress = compress_;

  if( fStream == NULL)
    fStream = new fstream();
  else if( fStream->is_open())
    throw GDLIOException("File unit is already open.");

  fStream->open( name_.c_str(), mode_);

  if( fStream->fail())
    {
      delete fStream;
      fStream = NULL;
      throw GDLIOException("Error opening file.");
    }

  if (compress) {
    igzStream = new igzstream;
    igzStream->open( name_.c_str(), mode_);

    if( igzStream->fail())
      {
	delete igzStream;
	igzStream = NULL;
	throw GDLIOException("Error opening file.");
      }
  }

  mode=mode_;
  swapEndian = swapEndian_;
  deleteOnClose = dOC;

  if( xdr_)
    xdrs = new XDR;

  lastSeekPos = 0;
  lastRecord = 0;
  lastRecordStart = 0;
  width = width_;
}

void GDLStream::Socket( const string& host,
			DUInt port, bool swapEndian_,
			DDouble c_timeout_, DDouble r_timeout_, 
			DDouble w_timeout_)
{
  if( iSocketStream == NULL)
    iSocketStream = new istringstream;

  if( recvBuf  == NULL)
    recvBuf = new string;

  name=host;

  sockNum = socket( AF_INET, SOCK_STREAM, 0);

  c_timeout = c_timeout_;
  r_timeout = r_timeout_;
  w_timeout = w_timeout_;

  int on = 1;
  if (setsockopt( sockNum, SOL_SOCKET, SO_REUSEADDR, 
		   (const char*) &on, sizeof (on)) == -1)
  {
    throw GDLIOException("Error opening file.");
  }

  sockaddr_in m_addr;
  m_addr.sin_family = AF_INET;
  m_addr.sin_port = htons ( port);

  // Convert host to IPv4 format
  struct hostent *h;
  if ((h=gethostbyname( host.c_str())) == NULL) {  // get the host info
    throw GDLIOException("Unable to lookup host.");
  }

  //  cout << inet_ntoa(*((struct in_addr *)h->h_addr)) << endl;

  int status = inet_pton( AF_INET, inet_ntoa(*((struct in_addr *)h->h_addr)), 
			  &m_addr.sin_addr );

  status = connect( sockNum, ( sockaddr * ) &m_addr, sizeof (m_addr));

  swapEndian = swapEndian_;

 // BIG limit on socket send width to avoid leading \n in CheckNL
  width = 32768;
}

void GDLStream::Flush() 
{ 
  if( fStream != NULL)
    {
      fStream->flush();
    }
}

void GDLStream::Close() 
{ 
  if( fStream != NULL)
    {
      fStream->close();
      fStream->clear();
      if( deleteOnClose) 
	std::remove(name.c_str());
    }
  name="";
  f77=false;
  swapEndian=false;
  compress=false;
  deleteOnClose=false;

  delete xdrs;
  xdrs = NULL;

  width = defaultStreamWidth;

  sockNum = -1;
  c_timeout = 0.0;
  r_timeout = 0.0;
  w_timeout = 0.0;
}

void GDLStream::Free()
{ 
  Close();

  delete fStream;
  fStream = NULL;

  delete iSocketStream;
  iSocketStream = NULL;

  delete recvBuf;
  recvBuf = NULL;
}
 
igzstream& GDLStream::IgzStream()
{
  return *igzStream;
}

fstream& GDLStream::IStream()
{
  if( fStream == NULL || !fStream->is_open()) 
    throw GDLIOException("File unit is not open.");
  if( !(mode & ios::in))
    throw GDLIOException("File unit is not open for reading.");
  return *fStream;
}

fstream& GDLStream::OStream()
{
  if( fStream == NULL || !fStream->is_open()) 
    throw GDLIOException("File unit is not open.");
  if( !(mode & ios::out))
    throw GDLIOException("File unit is not open for writing.");
  return *fStream;
}

istringstream& GDLStream::ISocketStream()
{
  if( iSocketStream == NULL) 
    throw GDLIOException("Socket unit is not open.");
  return *iSocketStream;
}

void GDLStream::Pad( SizeT nBytes)
{
  const SizeT bufSize = 1024;
  static char buf[ bufSize];
  SizeT nBuf = nBytes / bufSize;
  SizeT lastBytes = nBytes % bufSize;
  for( SizeT i=0; i<nBuf; i++) fStream->write( buf, bufSize);
  if( lastBytes > 0) fStream->write( buf, lastBytes);
}

void GDLStream::F77Write( DULong tCount)
{
  if( fStream->eof()) fStream->clear();

  assert( sizeof( DULong) == 4);
  if( swapEndian)
    {
      char swapTCount[ sizeof( DULong)];
      for( SizeT i=0; i<sizeof( DULong); ++i)
	swapTCount[i] = 
	  reinterpret_cast<char*>(&tCount)[ sizeof( DULong)-1-i];
      fStream->write(swapTCount,sizeof( DULong));
    }
  else
    {
      fStream->write(reinterpret_cast<char*>(&tCount),sizeof( DULong));
    }

  if( !fStream->good())
    {
      throw GDLIOException("Error writing F77_UNFORMATTED record data.");
    }  
}

DULong GDLStream::F77ReadStart()
{
  if( fStream->eof())
    throw GDLIOException("End of file encountered.");

  assert( sizeof( DULong) == 4);
  DULong tCountRd;
  if( swapEndian)
    {
      char swapTCount[ sizeof( DULong)];
      fStream->read( swapTCount, sizeof( DULong));
      for( SizeT i=0; i<sizeof( DULong); ++i)
	reinterpret_cast<char*>(&tCountRd)[ sizeof( DULong)-1-i] = 
	  swapTCount[i];
    }
  else
    {
      fStream->read(reinterpret_cast<char*>(&tCountRd),sizeof( DULong));
    }

  if( fStream->eof())
    throw GDLIOException("End of file encountered.");

  if( !fStream->good())
    {
      throw GDLIOException("Error reading F77_UNFORMATTED record data.");
    }  

  lastRecord = tCountRd;
  lastRecordStart = Tell();
  return tCountRd;
}

void GDLStream::F77ReadEnd()
{
  if( fStream->eof())
    throw GDLIOException("End of file encountered.");

  SizeT actPos = Tell();
  if( actPos > (lastRecordStart+lastRecord))
    throw GDLIOException( "Read past end of Record of F77_UNFORAMTTED file.");

  if( actPos < (lastRecordStart+lastRecord))
    Seek( lastRecordStart+lastRecord);
  
  DULong tCountRd;
  if( swapEndian)
    {
      char swapTCount[ sizeof( DULong)];
      fStream->read( swapTCount, sizeof( DULong));
      for( SizeT i=0; i<sizeof( DULong); ++i)
	reinterpret_cast<char*>(&tCountRd)[ sizeof( DULong)-1-i] = 
	  swapTCount[i];
    }
  else
    {
      fStream->read(reinterpret_cast<char*>(&tCountRd),sizeof( DULong));
    }

  if( fStream->eof())
    throw GDLIOException("End of file encountered.");

  if( !fStream->good())
    {
      throw GDLIOException("Error reading F77_UNFORMATTED record data.");
    }  

  if( lastRecord != tCountRd)
    throw GDLIOException( "Logical error in F77_UNFORAMTTED file.");

}


// ============================================================================
// gzstream, C++ iostream classes wrapping the zlib compression library.
// Copyright (C) 2001  Deepak Bandyopadhyay, Lutz Kettner
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// ============================================================================
//
// File          : gzstream.C
// Revision      : $Revision: 1.18 $
// Revision_date : $Date: 2009-06-09 13:11:38 $
// Author(s)     : Deepak Bandyopadhyay, Lutz Kettner
// 
// Standard streambuf implementation following Nicolai Josuttis, "The 
// Standard C++ Library".
// ============================================================================

#ifdef GZSTREAM_NAMESPACE
namespace GZSTREAM_NAMESPACE {
#endif

// ----------------------------------------------------------------------------
// Internal classes to implement gzstream. See header file for user classes.
// ----------------------------------------------------------------------------

// --------------------------------------
// class gzstreambuf:
// --------------------------------------

gzstreambuf* gzstreambuf::open( const char* name, int open_mode) {
    if ( is_open())
        return (gzstreambuf*)0;
    mode = open_mode;
    // no append nor read/write mode
    if ((mode & std::ios::ate) || (mode & std::ios::app)
        || ((mode & std::ios::in) && (mode & std::ios::out)))
        return (gzstreambuf*)0;
    char  fmode[10];
    char* fmodeptr = fmode;
    if ( mode & std::ios::in)
        *fmodeptr++ = 'r';
    else if ( mode & std::ios::out)
        *fmodeptr++ = 'w';
    *fmodeptr++ = 'b';
    *fmodeptr = '\0';
    file = gzopen( name, fmode);
    if (file == 0)
        return (gzstreambuf*)0;
    opened = 1;
    return this;
}

gzstreambuf * gzstreambuf::close() {
    if ( is_open()) {
        sync();
        opened = 0;
        if ( gzclose( file) == Z_OK)
            return this;
    }
    return (gzstreambuf*)0;
}

int gzstreambuf::underflow() { // used for input buffer only
    if ( gptr() && ( gptr() < egptr()))
        return * reinterpret_cast<unsigned char *>( gptr());

    if ( ! (mode & std::ios::in) || ! opened)
        return EOF;
    // Josuttis' implementation of inbuf
    int n_putback = gptr() - eback();
    if ( n_putback > 4)
        n_putback = 4;
    memcpy( buffer + (4 - n_putback), gptr() - n_putback, n_putback);

    int num = gzread( file, buffer+4, bufferSize-4);
    if (num <= 0) // ERROR or EOF
        return EOF;

    // reset buffer pointers
    setg( buffer + (4 - n_putback),   // beginning of putback area
          buffer + 4,                 // read position
          buffer + 4 + num);          // end of buffer

    // return next character
    return * reinterpret_cast<unsigned char *>( gptr());    
}

int gzstreambuf::flush_buffer() {
    // Separate the writing of the buffer from overflow() and
    // sync() operation.
    int w = pptr() - pbase();
    if ( gzwrite( file, pbase(), w) != w)
        return EOF;
    pbump( -w);
    return w;
}

int gzstreambuf::overflow( int c) { // used for output buffer only
    if ( ! ( mode & std::ios::out) || ! opened)
        return EOF;
    if (c != EOF) {
        *pptr() = c;
        pbump(1);
    }
    if ( flush_buffer() == EOF)
        return EOF;
    return c;
}

int gzstreambuf::sync() {
    // Changed to use flush_buffer() instead of overflow( EOF)
    // which caused improper behavior with std::endl and flush(),
    // bug reported by Vincent Ricard.
    if ( pptr() && pptr() > pbase()) {
        if ( flush_buffer() == EOF)
            return -1;
    }
    return 0;
}

// --------------------------------------
// class gzstreambase:
// --------------------------------------

gzstreambase::gzstreambase( const char* name, int mode) {
    init( &buf);
    open( name, mode);
}

gzstreambase::~gzstreambase() {
    buf.close();
}

void gzstreambase::open( const char* name, int open_mode) {
    if ( ! buf.open( name, open_mode))
        clear( rdstate() | std::ios::badbit);
}

void gzstreambase::close() {
    if ( buf.is_open())
        if ( ! buf.close())
            clear( rdstate() | std::ios::badbit);
}

#ifdef GZSTREAM_NAMESPACE
} // namespace GZSTREAM_NAMESPACE
#endif

// ============================================================================
// EOF //


