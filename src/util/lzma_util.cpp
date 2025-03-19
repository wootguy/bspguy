// SPDX-License-Identifier: 0BSD

///////////////////////////////////////////////////////////////////////////////
//
/// \file       01_compress_easy.c
/// \brief      Compress from stdin to stdout in multi-call mode
///
/// Usage:      ./01_compress_easy PRESET < INFILE > OUTFILE
///
/// Example:    ./01_compress_easy 6 < foo > foo.xz
//
//  Author:     Lasse Collin
//
///////////////////////////////////////////////////////////////////////////////

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <lzma.h>
#include <string>
#include "util.h"


static bool
init_encoder(lzma_stream* strm, uint32_t preset)
{
	// Initialize the encoder using a preset. Set the integrity to check
	// to CRC64, which is the default in the xz command line tool. If
	// the .xz file needs to be decompressed with XZ Embedded, use
	// LZMA_CHECK_CRC32 instead.
	lzma_ret ret = lzma_easy_encoder(strm, preset, LZMA_CHECK_CRC64);

	// Return successfully if the initialization went fine.
	if (ret == LZMA_OK)
		return true;

	// Something went wrong. The possible errors are documented in
	// lzma/container.h (src/liblzma/api/lzma/container.h in the source
	// package or e.g. /usr/include/lzma/container.h depending on the
	// install prefix).
	const char* msg;
	switch (ret) {
	case LZMA_MEM_ERROR:
		msg = "Memory allocation failed";
		break;

	case LZMA_OPTIONS_ERROR:
		msg = "Specified preset is not supported";
		break;

	case LZMA_UNSUPPORTED_CHECK:
		msg = "Specified integrity check is not supported";
		break;

	default:
		// This is most likely LZMA_PROG_ERROR indicating a bug in
		// this program or in liblzma. It is inconvenient to have a
		// separate error message for errors that should be impossible
		// to occur, but knowing the error code is important for
		// debugging. That's why it is good to print the error code
		// at least when there is no good error message to show.
		msg = "Unknown error, possibly a bug";
		break;
	}

	fprintf(stderr, "Error initializing the encoder: %s (error code %u)\n",
		msg, ret);
	return false;
}

static bool
compress(lzma_stream* strm, FILE* infile, FILE* outfile)
{
	// This will be LZMA_RUN until the end of the input file is reached.
	// This tells lzma_code() when there will be no more input.
	lzma_action action = LZMA_RUN;

	// Buffers to temporarily hold uncompressed input
	// and compressed output.
	uint8_t inbuf[BUFSIZ];
	uint8_t outbuf[BUFSIZ];

	// Initialize the input and output pointers. Initializing next_in
	// and avail_in isn't really necessary when we are going to encode
	// just one file since LZMA_STREAM_INIT takes care of initializing
	// those already. But it doesn't hurt much and it will be needed
	// if encoding more than one file like we will in 02_decompress.c.
	//
	// While we don't care about strm->total_in or strm->total_out in this
	// example, it is worth noting that initializing the encoder will
	// always reset total_in and total_out to zero. But the encoder
	// initialization doesn't touch next_in, avail_in, next_out, or
	// avail_out.
	strm->next_in = NULL;
	strm->avail_in = 0;
	strm->next_out = outbuf;
	strm->avail_out = sizeof(outbuf);

	// Loop until the file has been successfully compressed or until
	// an error occurs.
	while (true) {
		// Fill the input buffer if it is empty.
		if (strm->avail_in == 0 && !feof(infile)) {
			strm->next_in = inbuf;
			strm->avail_in = fread(inbuf, 1, sizeof(inbuf),
				infile);

			if (ferror(infile)) {
				fprintf(stderr, "Read error: %s\n",
					strerror(errno));
				return false;
			}

			// Once the end of the input file has been reached,
			// we need to tell lzma_code() that no more input
			// will be coming and that it should finish the
			// encoding.
			if (feof(infile))
				action = LZMA_FINISH;
		}

		// Tell liblzma do the actual encoding.
		//
		// This reads up to strm->avail_in bytes of input starting
		// from strm->next_in. avail_in will be decremented and
		// next_in incremented by an equal amount to match the
		// number of input bytes consumed.
		//
		// Up to strm->avail_out bytes of compressed output will be
		// written starting from strm->next_out. avail_out and next_out
		// will be incremented by an equal amount to match the number
		// of output bytes written.
		//
		// The encoder has to do internal buffering, which means that
		// it may take quite a bit of input before the same data is
		// available in compressed form in the output buffer.
		lzma_ret ret = lzma_code(strm, action);

		// If the output buffer is full or if the compression finished
		// successfully, write the data from the output buffer to
		// the output file.
		if (strm->avail_out == 0 || ret == LZMA_STREAM_END) {
			// When lzma_code() has returned LZMA_STREAM_END,
			// the output buffer is likely to be only partially
			// full. Calculate how much new data there is to
			// be written to the output file.
			size_t write_size = sizeof(outbuf) - strm->avail_out;

			if (fwrite(outbuf, 1, write_size, outfile)
				!= write_size) {
				fprintf(stderr, "Write error: %s\n",
					strerror(errno));
				return false;
			}

			// Reset next_out and avail_out.
			strm->next_out = outbuf;
			strm->avail_out = sizeof(outbuf);
		}

		// Normally the return value of lzma_code() will be LZMA_OK
		// until everything has been encoded.
		if (ret != LZMA_OK) {
			// Once everything has been encoded successfully, the
			// return value of lzma_code() will be LZMA_STREAM_END.
			//
			// It is important to check for LZMA_STREAM_END. Do not
			// assume that getting ret != LZMA_OK would mean that
			// everything has gone well.
			if (ret == LZMA_STREAM_END)
				return true;

			// It's not LZMA_OK nor LZMA_STREAM_END,
			// so it must be an error code. See lzma/base.h
			// (src/liblzma/api/lzma/base.h in the source package
			// or e.g. /usr/include/lzma/base.h depending on the
			// install prefix) for the list and documentation of
			// possible values. Most values listen in lzma_ret
			// enumeration aren't possible in this example.
			const char* msg;
			switch (ret) {
			case LZMA_MEM_ERROR:
				msg = "Memory allocation failed";
				break;

			case LZMA_DATA_ERROR:
				// This error is returned if the compressed
				// or uncompressed size get near 8 EiB
				// (2^63 bytes) because that's where the .xz
				// file format size limits currently are.
				// That is, the possibility of this error
				// is mostly theoretical unless you are doing
				// something very unusual.
				//
				// Note that strm->total_in and strm->total_out
				// have nothing to do with this error. Changing
				// those variables won't increase or decrease
				// the chance of getting this error.
				msg = "File size limits exceeded";
				break;

			default:
				// This is most likely LZMA_PROG_ERROR, but
				// if this program is buggy (or liblzma has
				// a bug), it may be e.g. LZMA_BUF_ERROR or
				// LZMA_OPTIONS_ERROR too.
				//
				// It is inconvenient to have a separate
				// error message for errors that should be
				// impossible to occur, but knowing the error
				// code is important for debugging. That's why
				// it is good to print the error code at least
				// when there is no good error message to show.
				msg = "Unknown error, possibly a bug";
				break;
			}

			fprintf(stderr, "Encoder error: %s (error code %u)\n",
				msg, ret);
			return false;
		}
	}
}

bool lzmaCompress(std::string inPath, std::string outPath, uint32_t preset) {
	FILE* infile = fopen(inPath.c_str(), "rb");
	FILE* outfile = fopen(outPath.c_str(), "wb");

	if (!infile || !outfile) {
		return false;
	}

	// Initialize a lzma_stream structure. When it is allocated on stack,
	// it is simplest to use LZMA_STREAM_INIT macro like below. When it
	// is allocated on heap, using memset(strmptr, 0, sizeof(*strmptr))
	// works (as long as NULL pointers are represented with zero bits
	// as they are on practically all computers today).
	lzma_stream strm = LZMA_STREAM_INIT;

	// Initialize the encoder. If it succeeds, compress from
	// stdin to stdout.
	bool success = init_encoder(&strm, preset);
	if (success)
		success = compress(&strm, infile, outfile);

	// Free the memory allocated for the encoder. If we were encoding
	// multiple files, this would only need to be done after the last
	// file. See 02_decompress.c for handling of multiple files.
	//
	// It is OK to call lzma_end() multiple times or when it hasn't been
	// actually used except initialized with LZMA_STREAM_INIT.
	lzma_end(&strm);

	fclose(infile);
	fclose(outfile);

	return success;
}

static bool
init_decoder(lzma_stream* strm)
{
	// Initialize a .xz decoder. The decoder supports a memory usage limit
	// and a set of flags.
	//
	// The memory usage of the decompressor depends on the settings used
	// to compress a .xz file. It can vary from less than a megabyte to
	// a few gigabytes, but in practice (at least for now) it rarely
	// exceeds 65 MiB because that's how much memory is required to
	// decompress files created with "xz -9". Settings requiring more
	// memory take extra effort to use and don't (at least for now)
	// provide significantly better compression in most cases.
	//
	// Memory usage limit is useful if it is important that the
	// decompressor won't consume gigabytes of memory. The need
	// for limiting depends on the application. In this example,
	// no memory usage limiting is used. This is done by setting
	// the limit to UINT64_MAX.
	//
	// The .xz format allows concatenating compressed files as is:
	//
	//     echo foo | xz > foobar.xz
	//     echo bar | xz >> foobar.xz
	//
	// When decompressing normal standalone .xz files, LZMA_CONCATENATED
	// should always be used to support decompression of concatenated
	// .xz files. If LZMA_CONCATENATED isn't used, the decoder will stop
	// after the first .xz stream. This can be useful when .xz data has
	// been embedded inside another file format.
	//
	// Flags other than LZMA_CONCATENATED are supported too, and can
	// be combined with bitwise-or. See lzma/container.h
	// (src/liblzma/api/lzma/container.h in the source package or e.g.
	// /usr/include/lzma/container.h depending on the install prefix)
	// for details.
	lzma_ret ret = lzma_stream_decoder(
		strm, UINT64_MAX, LZMA_CONCATENATED);

	// Return successfully if the initialization went fine.
	if (ret == LZMA_OK)
		return true;

	// Something went wrong. The possible errors are documented in
	// lzma/container.h (src/liblzma/api/lzma/container.h in the source
	// package or e.g. /usr/include/lzma/container.h depending on the
	// install prefix).
	//
	// Note that LZMA_MEMLIMIT_ERROR is never possible here. If you
	// specify a very tiny limit, the error will be delayed until
	// the first headers have been parsed by a call to lzma_code().
	const char* msg;
	switch (ret) {
	case LZMA_MEM_ERROR:
		msg = "Memory allocation failed";
		break;

	case LZMA_OPTIONS_ERROR:
		msg = "Unsupported decompressor flags";
		break;

	default:
		// This is most likely LZMA_PROG_ERROR indicating a bug in
		// this program or in liblzma. It is inconvenient to have a
		// separate error message for errors that should be impossible
		// to occur, but knowing the error code is important for
		// debugging. That's why it is good to print the error code
		// at least when there is no good error message to show.
		msg = "Unknown error, possibly a bug";
		break;
	}

	fprintf(stderr, "Error initializing the decoder: %s (error code %u)\n",
		msg, ret);
	return false;
}


bool decompress(lzma_stream* strm, const char* inname, FILE* infile, FILE* outfile)
{
	// When LZMA_CONCATENATED flag was used when initializing the decoder,
	// we need to tell lzma_code() when there will be no more input.
	// This is done by setting action to LZMA_FINISH instead of LZMA_RUN
	// in the same way as it is done when encoding.
	//
	// When LZMA_CONCATENATED isn't used, there is no need to use
	// LZMA_FINISH to tell when all the input has been read, but it
	// is still OK to use it if you want. When LZMA_CONCATENATED isn't
	// used, the decoder will stop after the first .xz stream. In that
	// case some unused data may be left in strm->next_in.
	lzma_action action = LZMA_RUN;

	uint8_t inbuf[BUFSIZ];
	uint8_t outbuf[BUFSIZ];

	strm->next_in = NULL;
	strm->avail_in = 0;
	strm->next_out = outbuf;
	strm->avail_out = sizeof(outbuf);

	while (true) {
		if (strm->avail_in == 0 && !feof(infile)) {
			strm->next_in = inbuf;
			strm->avail_in = fread(inbuf, 1, sizeof(inbuf),
				infile);

			if (ferror(infile)) {
				fprintf(stderr, "%s: Read error: %s\n",
					inname, strerror(errno));
				return false;
			}

			// Once the end of the input file has been reached,
			// we need to tell lzma_code() that no more input
			// will be coming. As said before, this isn't required
			// if the LZMA_CONATENATED flag isn't used when
			// initializing the decoder.
			if (feof(infile))
				action = LZMA_FINISH;
		}

		lzma_ret ret = lzma_code(strm, action);

		if (strm->avail_out == 0 || ret == LZMA_STREAM_END) {
			size_t write_size = sizeof(outbuf) - strm->avail_out;

			if (fwrite(outbuf, 1, write_size, outfile)
				!= write_size) {
				fprintf(stderr, "Write error: %s\n",
					strerror(errno));
				return false;
			}

			strm->next_out = outbuf;
			strm->avail_out = sizeof(outbuf);
		}

		if (ret != LZMA_OK) {
			// Once everything has been decoded successfully, the
			// return value of lzma_code() will be LZMA_STREAM_END.
			//
			// It is important to check for LZMA_STREAM_END. Do not
			// assume that getting ret != LZMA_OK would mean that
			// everything has gone well or that when you aren't
			// getting more output it must have successfully
			// decoded everything.
			if (ret == LZMA_STREAM_END)
				return true;

			// It's not LZMA_OK nor LZMA_STREAM_END,
			// so it must be an error code. See lzma/base.h
			// (src/liblzma/api/lzma/base.h in the source package
			// or e.g. /usr/include/lzma/base.h depending on the
			// install prefix) for the list and documentation of
			// possible values. Many values listen in lzma_ret
			// enumeration aren't possible in this example, but
			// can be made possible by enabling memory usage limit
			// or adding flags to the decoder initialization.
			const char* msg;
			switch (ret) {
			case LZMA_MEM_ERROR:
				msg = "Memory allocation failed";
				break;

			case LZMA_FORMAT_ERROR:
				// .xz magic bytes weren't found.
				msg = "The input is not in the .xz format";
				break;

			case LZMA_OPTIONS_ERROR:
				// For example, the headers specify a filter
				// that isn't supported by this liblzma
				// version (or it hasn't been enabled when
				// building liblzma, but no-one sane does
				// that unless building liblzma for an
				// embedded system). Upgrading to a newer
				// liblzma might help.
				//
				// Note that it is unlikely that the file has
				// accidentally became corrupt if you get this
				// error. The integrity of the .xz headers is
				// always verified with a CRC32, so
				// unintentionally corrupt files can be
				// distinguished from unsupported files.
				msg = "Unsupported compression options";
				break;

			case LZMA_DATA_ERROR:
				msg = "Compressed file is corrupt";
				break;

			case LZMA_BUF_ERROR:
				// Typically this error means that a valid
				// file has got truncated, but it might also
				// be a damaged part in the file that makes
				// the decoder think the file is truncated.
				// If you prefer, you can use the same error
				// message for this as for LZMA_DATA_ERROR.
				msg = "Compressed file is truncated or "
					"otherwise corrupt";
				break;

			default:
				// This is most likely LZMA_PROG_ERROR.
				msg = "Unknown error, possibly a bug";
				break;
			}

			fprintf(stderr, "%s: Decoder error: "
				"%s (error code %u)\n",
				inname, msg, ret);
			return false;
		}
	}
}

bool decompressMemory(lzma_stream* strm, uint8_t* inData, int inLen, vector<uint8_t>& outBytes)
{
	// When LZMA_CONCATENATED flag was used when initializing the decoder,
	// we need to tell lzma_code() when there will be no more input.
	// This is done by setting action to LZMA_FINISH instead of LZMA_RUN
	// in the same way as it is done when encoding.
	//
	// When LZMA_CONCATENATED isn't used, there is no need to use
	// LZMA_FINISH to tell when all the input has been read, but it
	// is still OK to use it if you want. When LZMA_CONCATENATED isn't
	// used, the decoder will stop after the first .xz stream. In that
	// case some unused data may be left in strm->next_in.
	lzma_action action = LZMA_FINISH;

	uint8_t inbuf[BUFSIZ];
	uint8_t outbuf[BUFSIZ];

	strm->next_in = inData;
	strm->avail_in = inLen;
	strm->next_out = outbuf;
	strm->avail_out = sizeof(outbuf);

	while (true) {
		if (strm->avail_in == 0) {
			logf("lzma wanted more data(?)\n");
			return false;
		}

		lzma_ret ret = lzma_code(strm, action);

		if (strm->avail_out == 0 || ret == LZMA_STREAM_END) {
			size_t write_size = sizeof(outbuf) - strm->avail_out;

			outBytes.insert(outBytes.end(), outbuf, outbuf + write_size);

			strm->next_out = outbuf;
			strm->avail_out = sizeof(outbuf);
		}

		if (ret != LZMA_OK) {
			// Once everything has been decoded successfully, the
			// return value of lzma_code() will be LZMA_STREAM_END.
			//
			// It is important to check for LZMA_STREAM_END. Do not
			// assume that getting ret != LZMA_OK would mean that
			// everything has gone well or that when you aren't
			// getting more output it must have successfully
			// decoded everything.
			if (ret == LZMA_STREAM_END)
				return true;

			// It's not LZMA_OK nor LZMA_STREAM_END,
			// so it must be an error code. See lzma/base.h
			// (src/liblzma/api/lzma/base.h in the source package
			// or e.g. /usr/include/lzma/base.h depending on the
			// install prefix) for the list and documentation of
			// possible values. Many values listen in lzma_ret
			// enumeration aren't possible in this example, but
			// can be made possible by enabling memory usage limit
			// or adding flags to the decoder initialization.
			const char* msg;
			switch (ret) {
			case LZMA_MEM_ERROR:
				msg = "Memory allocation failed";
				break;

			case LZMA_FORMAT_ERROR:
				// .xz magic bytes weren't found.
				msg = "The input is not in the .xz format";
				break;

			case LZMA_OPTIONS_ERROR:
				// For example, the headers specify a filter
				// that isn't supported by this liblzma
				// version (or it hasn't been enabled when
				// building liblzma, but no-one sane does
				// that unless building liblzma for an
				// embedded system). Upgrading to a newer
				// liblzma might help.
				//
				// Note that it is unlikely that the file has
				// accidentally became corrupt if you get this
				// error. The integrity of the .xz headers is
				// always verified with a CRC32, so
				// unintentionally corrupt files can be
				// distinguished from unsupported files.
				msg = "Unsupported compression options";
				break;

			case LZMA_DATA_ERROR:
				msg = "Compressed file is corrupt";
				break;

			case LZMA_BUF_ERROR:
				// Typically this error means that a valid
				// file has got truncated, but it might also
				// be a damaged part in the file that makes
				// the decoder think the file is truncated.
				// If you prefer, you can use the same error
				// message for this as for LZMA_DATA_ERROR.
				msg = "Compressed file is truncated or "
					"otherwise corrupt";
				break;

			default:
				// This is most likely LZMA_PROG_ERROR.
				msg = "Unknown error, possibly a bug";
				break;
			}

			logf("lzma decoder error: %s (error code %u)\n", msg, ret);
			return false;
		}
	}
}



bool lzmaDecompress(uint8_t* compressedData, int compressedDataLen, vector<uint8_t>& outBytes)
{
	lzma_stream strm = LZMA_STREAM_INIT;

	bool success = true;

	if (!init_decoder(&strm)) {
		// Decoder initialization failed. There's no point
		// to retry it so we need to exit.
		success = false;
		return false;
	}

	success = decompressMemory(&strm, compressedData, compressedDataLen, outBytes);

	// Free the memory allocated for the decoder. This only needs to be
	// done after the last file.
	lzma_end(&strm);

	return success;
}